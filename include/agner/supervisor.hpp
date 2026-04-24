#pragma once

/**
 * @file supervisor.hpp
 * @brief Supervisor for managing child actor lifecycles and restart strategies.
 *
 * Supervisors implement OTP-style supervision trees with configurable restart
 * policies (one-for-one, one-for-all, rest-for-one, simple-one-for-one).
 */

#include <algorithm>
#include <any>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <deque>
#include <map>
#include <optional>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "agner/actor.hpp"
#include "agner/actor_control.hpp"
#include "agner/detail/supervisor_detail.hpp"
#include "agner/scheduler_concept.hpp"

namespace agner {

/// @brief Child restart policy.
enum class Restart {
  permanent,  ///< Always restart the child.
  transient,  ///< Restart only if child exits abnormally.
  temporary   ///< Never restart the child.
};

/// @brief Supervisor restart strategy.
enum class Strategy {
  one_for_one,        ///< Restart only the failed child.
  one_for_all,        ///< Restart all children when one fails.
  rest_for_one,       ///< Restart the failed child and those started after it.
  simple_one_for_one  ///< Dynamic children with identical specs. ///< Dynamic
                      ///< children with identical specs.
};

/// @brief Restart intensity limit configuration.
struct Intensity {
  std::size_t max_restarts = 0;  ///< Maximum restarts allowed within window.
  std::chrono::steady_clock::duration within{};  ///< Time window for limit.
};

/// @brief Identifier for a child specification.
struct ChildId {
  std::string_view value;  ///< The child identifier string.
};

/// @brief Specification for a supervised child actor.
/// @tparam ActorType The actor class to supervise.
/// @tparam Args Constructor argument types.
template <typename ActorType, typename... Args>
struct ChildSpec {
  using actor_type = ActorType;
  using args_type = std::tuple<std::decay_t<Args>...>;

  ChildId id;                                      ///< Child identifier.
  Restart restart = Restart::permanent;            ///< Restart policy.
  std::chrono::steady_clock::duration shutdown{};  ///< Shutdown timeout.
  args_type args{};                                ///< Constructor arguments.
  bool simple = false;  ///< True for simple_one_for_one. ///< True for
                        ///< simple_one_for_one.
};

/// @brief Create a child specification with constructor arguments.
/// @param id Child identifier.
/// @param restart Restart policy.
/// @param shutdown Shutdown timeout duration.
/// @param args Constructor arguments for the child actor.
/// @return ChildSpec configured with the given parameters.
template <typename ActorType, typename... Args>
ChildSpec<ActorType, Args...> child(
    ChildId id, Restart restart, std::chrono::steady_clock::duration shutdown,
    Args&&... args) {
  return ChildSpec<ActorType, Args...>{
      id, restart, shutdown, std::make_tuple(std::forward<Args>(args)...),
      false};
}

/// @brief Create a child specification for simple_one_for_one supervisors.
/// @param id Child identifier.
/// @param restart Restart policy.
/// @param shutdown Shutdown timeout duration.
/// @return ChildSpec for dynamic child spawning.
template <typename ActorType, typename... Args>
ChildSpec<ActorType, Args...> simple_child(
    ChildId id, Restart restart, std::chrono::steady_clock::duration shutdown) {
  return ChildSpec<ActorType, Args...>{id, restart, shutdown, {}, true};
}

/**
 * @brief Base class for supervisors managing child actor lifecycles.
 *
 * Implement a static `specification()` method returning a Specification struct
 * to configure the supervision strategy, restart intensity, and child specs.
 *
 * @tparam SchedulerType Scheduler implementation.
 * @tparam Derived The derived supervisor class (CRTP).
 * @tparam ChildSpecs Child specification types.
 */
template <SchedulerLike SchedulerType, typename Derived, typename... ChildSpecs>
class Supervisor : public Actor<SchedulerType, Derived, Messages<>> {
 public:
  static_assert(sizeof...(ChildSpecs) > 0,
                "Supervisor requires at least one child specification.");
  using Base = Actor<SchedulerType, Derived, Messages<>>;
  using Clock = typename SchedulerType::Clock;
  using time_point = typename Clock::time_point;

  /// @brief Supervisor configuration.
  struct Specification {
    Strategy strategy = Strategy::one_for_one;  ///< Restart strategy.
    Intensity intensity{};                      ///< Restart intensity limit.
    std::tuple<ChildSpecs...> children{};       ///< Child specifications.
  };

  /// @brief Construct a supervisor bound to a scheduler.
  explicit Supervisor(SchedulerType& scheduler)
      : Base(scheduler), specification_(Derived::specification()) {
    initialize_states();
  }

  /// @brief Initialize the supervisor and start static children.
  task<void> init() {
    const bool should_start_static_children =
        specification_.strategy != Strategy::simple_one_for_one;
    if (!should_start_static_children) {
      co_return;
    }
    co_await start_all_from_specs();
    co_return;
  }

  /// @brief Start a child actor dynamically.
  /// @tparam Selector ChildSpec type or index to select which child.
  /// @param args Constructor arguments for the child.
  /// @return Reference to the started child actor.
  template <typename Selector, typename... Args>
  task<ActorRef> start_child(Args&&... args) {
    constexpr std::size_t index =
        detail::resolve_selector_index_v<Selector, ChildSpecs...>;
    if constexpr (sizeof...(Args) == 0) {
      co_return co_await start_child_by_index<index>();
    } else {
      co_return co_await start_child_by_index<index>(
          std::forward<Args>(args)...);
    }
  }

  /// @brief Stop a running child actor.
  /// @tparam Selector ChildSpec type or index to select which child.
  template <typename Selector>
  task<void> stop_child() {
    constexpr std::size_t index =
        detail::resolve_selector_index_v<Selector, ChildSpecs...>;
    stop_children_by_index<index>();
    co_return;
  }

  /// @brief Restart a child actor.
  /// @tparam Selector ChildSpec type or index to select which child.
  template <typename Selector>
  task<void> restart_child() {
    constexpr std::size_t index =
        detail::resolve_selector_index_v<Selector, ChildSpecs...>;
    restart_children_by_index<index>();
    co_return;
  }

  /// @brief Remove a child specification and stop any running instance.
  /// @tparam Selector ChildSpec type or index to select which child.
  template <typename Selector>
  task<void> delete_child() {
    constexpr std::size_t index =
        detail::resolve_selector_index_v<Selector, ChildSpecs...>;
    delete_children_by_index<index>();
    co_return;
  }

  /// @brief List identifiers of all child specifications.
  /// @return Vector of ChildId values.
  std::vector<ChildId> which_children() const {
    std::vector<ChildId> ids;
    ids.reserve(sizeof...(ChildSpecs));
    std::apply(
        [&](const auto&... state) { (ids.push_back(state.spec.id), ...); },
        states_);
    return ids;
  }

  /// @brief Get the ActorRef for a running child by its identifier.
  /// @param id The child identifier.
  /// @return The child's ActorRef, or nullopt if not running.
  std::optional<ActorRef> child_ref(ChildId id) const {
    std::optional<ActorRef> result;
    std::apply(
        [&](const auto&... state) {
          ((result = result ? result : state.find_child_ref(id, children_)),
           ...);
        },
        states_);
    return result;
  }

  /// @brief Run the supervisor's main loop.
  task<void> run() {
    co_await init();
    co_await supervise_loop();
  }

  /// @brief Stop the supervisor and all children.
  /// @param reason Exit reason to propagate.
  void stop(ExitReason reason = {}) override {
    if (!stopping_) {
      stopping_ = true;
      stop_all_children();
      restart_group_.reset();
    }
    Base::stop(reason);
  }

 protected:
  task<void> supervise_loop() {
    while (true) {
      bool stop_requested = false;
      co_await this->receive(
          [&](DownSignal& signal) {
            handle_signal(signal.from, signal.reason);
          },
          [&](ExitSignal& signal) {
            if (stopping_) {
              if (signal.from == this->self()) {
                stop_requested = true;
                return;
              }
            }
            handle_signal(signal.from, signal.reason);
          });
      if (stop_requested) {
        co_return;
      }
    }
  }

 private:
  // Unified child registry - single source of truth for all child state
  struct ChildEntry {
    std::size_t spec_index = 0;
    std::size_t start_order = 0;
    std::any args;  // Type-erased args tuple for respawning
    bool stopping = false;
    bool suppress_restart = false;
  };

  struct RestartPlanItem {
    ActorRef ref;
    ChildEntry entry;
    ExitReason reason;
  };

  struct RestartGroup {
    Strategy strategy = Strategy::one_for_all;
    std::vector<RestartPlanItem> plan;
    std::vector<ActorRef> pending_stops;
  };

  template <typename Spec>
  struct SpecState {
    Spec spec;

    std::optional<ActorRef> find_child_ref(
        ChildId id, const std::map<ActorRef, ChildEntry>& children) const {
      if (spec.id.value != id.value) {
        return std::nullopt;
      }
      for (const auto& [ref, entry] : children) {
        if (entry.spec_index == spec_index) {
          return ref;
        }
      }
      return std::nullopt;
    }

    std::size_t spec_index = 0;
  };

  using StatesTuple = std::tuple<SpecState<ChildSpecs>...>;

  template <std::size_t... Is>
  void initialize_states_impl(std::index_sequence<Is...>) {
    states_ = std::apply(
        [&](const auto&... spec) {
          return std::tuple<SpecState<ChildSpecs>...>{
              SpecState<ChildSpecs>{spec, Is}...};
        },
        specification_.children);
  }

  void initialize_states() {
    initialize_states_impl(std::index_sequence_for<ChildSpecs...>{});
  }

  template <std::size_t Index>
  task<ActorRef> start_child_by_index() {
    auto& state = std::get<Index>(states_);
    if (specification_.strategy == Strategy::simple_one_for_one) {
      using ArgsTuple = typename decltype(state.spec)::args_type;
      auto args = ArgsTuple{};
      co_return start_child_with_args<Index>(std::move(args));
    }
    co_return start_child_with_args<Index>(state.spec.args);
  }

  template <std::size_t Index, typename... Args>
  task<ActorRef> start_child_by_index(Args&&... args) {
    using ArgsTuple =
        typename std::tuple_element<Index,
                                    std::tuple<ChildSpecs...>>::type::args_type;
    auto args_tuple = ArgsTuple{std::forward<Args>(args)...};
    co_return start_child_with_args<Index>(std::move(args_tuple));
  }

  template <std::size_t Index>
  ActorRef start_child_with_args(
      typename std::tuple_element<
          Index, std::tuple<ChildSpecs...>>::type::args_type args) {
    const bool is_simple =
        specification_.strategy == Strategy::simple_one_for_one;

    // For non-simple strategies, check if child already exists for this spec
    if (!is_simple) {
      auto existing_ref = find_running_child_for_spec<Index>();
      if (existing_ref) {
        return *existing_ref;
      }
    }

    // Spawn new child
    ActorRef actor_ref = spawn_child_with_args<Index>(args);

    // Register in unified registry
    ChildEntry entry;
    entry.spec_index = Index;
    entry.start_order = next_start_order_++;
    entry.args = std::move(args);
    entry.stopping = false;
    entry.suppress_restart = false;
    children_[actor_ref] = std::move(entry);

    return actor_ref;
  }

  template <std::size_t Index>
  std::optional<ActorRef> find_running_child_for_spec() const {
    auto it = std::find_if(children_.begin(), children_.end(),
                           [&](const auto& child) {
                             return child.second.spec_index == Index;
                           });
    if (it == children_.end()) {
      return std::nullopt;
    }
    return it->first;
  }

  template <std::size_t Index>
  ActorRef spawn_child_with_args(
      typename std::tuple_element<
          Index, std::tuple<ChildSpecs...>>::type::args_type& args) {
    using ActorType = typename std::tuple_element<
        Index, std::tuple<ChildSpecs...>>::type::actor_type;
    auto actor_ref = std::apply(
        [&](auto&... a) {
          return this->template spawn<ActorType>(detail::clone_or_move(a)...);
        },
        args);
    this->monitor(actor_ref);
    return actor_ref;
  }

  template <std::size_t Index>
  void stop_and_suppress_by_index() {
    std::for_each(children_.begin(), children_.end(), [&](auto& child) {
      auto& [ref, entry] = child;
      if (entry.spec_index == Index) {
        entry.suppress_restart = true;
        request_stop(ref);
      }
    });
  }

  void stop_all_children() {
    std::for_each(children_.begin(), children_.end(), [&](auto& child) {
      auto& [ref, entry] = child;
      entry.suppress_restart = true;
      request_stop(ref);
    });
  }

  template <std::size_t Index>
  void stop_children_by_index() {
    stop_and_suppress_by_index<Index>();
  }

  template <std::size_t Index>
  void restart_children_by_index() {
    // Collect refs to restart (can't modify map while iterating)
    std::vector<ActorRef> to_restart;
    std::for_each(children_.begin(), children_.end(), [&](auto& child) {
      auto& [ref, entry] = child;
      if (entry.spec_index == Index) {
        entry.suppress_restart = false;
        to_restart.push_back(ref);
      }
    });
    std::for_each(to_restart.begin(), to_restart.end(), [&](ActorRef ref) {
      auto& child = children_.at(ref);
      restart_child_entry<Index>(ref, child);
    });
  }

  template <std::size_t Index>
  void delete_children_by_index() {
    stop_and_suppress_by_index<Index>();
    std::erase_if(children_,
                  [&](const auto& child) { return child.second.spec_index == Index; });
  }

  void handle_signal(ActorRef from, const ExitReason& reason) {
    if (!from.valid()) {
      return;
    }
    handle_termination(from, reason);
  }

  void handle_termination(ActorRef actor_ref, const ExitReason& reason) {
    if (restart_group_) {
      handle_restart_group_stop(actor_ref, reason);
      return;
    }

    auto child_it = children_.find(actor_ref);
    if (child_it == children_.end()) {
      return;
    }

    auto entry = std::move(child_it->second);
    children_.erase(child_it);

    const Restart restart = restart_at_spec_index(entry.spec_index);
    const bool will_restart =
        !entry.suppress_restart && should_restart(restart, reason);

    if (!will_restart) {
      return;
    }

    if (!register_restart()) {
      return;
    }

    // one_for_one and simple_one_for_one restart directly
    if (specification_.strategy == Strategy::one_for_one ||
        specification_.strategy == Strategy::simple_one_for_one) {
      respawn_with_entry(entry);
      return;
    }

    // one_for_all and rest_for_one use group restart
    begin_restart_group(actor_ref, entry, reason);
  }

  void handle_restart_group_stop(ActorRef actor_ref,
                                 const ExitReason& /*reason*/) {
    // Remove from children_ if present
    auto it = children_.find(actor_ref);
    if (it != children_.end()) {
      // Store entry in restart_group plan for respawn
      children_.erase(it);
    }

    auto& pending = restart_group_->pending_stops;
    auto pending_it = std::find(pending.begin(), pending.end(), actor_ref);
    if (pending_it != pending.end()) {
      pending.erase(pending_it);
    }

    if (pending.empty()) {
      finalize_restart_group();
    }
  }

  void begin_restart_group(ActorRef failed_ref, const ChildEntry& failed_entry,
                           const ExitReason& reason) {
    RestartGroup group;
    group.strategy = specification_.strategy;

    // Add failed child to plan first
    group.plan.push_back(RestartPlanItem{failed_ref, failed_entry, reason});

    std::for_each(children_.begin(), children_.end(), [&](const auto& child) {
      const auto& [ref, entry] = child;
      group.plan.push_back(
          RestartPlanItem{ref, entry, ExitReason{ExitReason::Kind::stopped}});
    });
    auto plan_it = group.plan.begin();
    ++plan_it;  // Keep failed child entry at index 0.
    group.plan.erase(
        std::remove_if(plan_it, group.plan.end(),
                       [&](const auto& plan_item) {
                         return group.strategy == Strategy::rest_for_one &&
                                plan_item.entry.start_order <=
                                    failed_entry.start_order;
                       }),
        group.plan.end());

    restart_group_ = std::move(group);

    // Send stop signals to all children in the plan (except failed one already
    // removed)
    std::for_each(restart_group_->plan.begin(), restart_group_->plan.end(),
                  [&](const auto& plan_item) {
                    if (children_.contains(plan_item.ref)) {
                      auto& child = children_.at(plan_item.ref);
                      restart_group_->pending_stops.push_back(plan_item.ref);
                      child.suppress_restart = false;
                      request_stop(plan_item.ref);
                    }
                  });

    if (restart_group_->pending_stops.empty()) {
      finalize_restart_group();
    }
  }

  void finalize_restart_group() {
    assert(restart_group_);

    auto group = std::move(*restart_group_);
    restart_group_.reset();

    std::for_each(group.plan.begin(), group.plan.end(), [&](const auto& plan_item) {
      if (should_restart(restart_at_spec_index(plan_item.entry.spec_index),
                         plan_item.reason)) {
        respawn_with_entry(plan_item.entry);
      }
    });
  }

  // Respawn a child using the stored args from a ChildEntry
  void respawn_with_entry(const ChildEntry& entry) {
    detail::visit_at_index<sizeof...(ChildSpecs)>(
        entry.spec_index, [&]<std::size_t Index>() {
          using ArgsTuple = typename std::tuple_element<
              Index, std::tuple<ChildSpecs...>>::type::args_type;
          auto& args = std::any_cast<const ArgsTuple&>(entry.args);
          auto args_copy = args;
          start_child_with_args<Index>(std::move(args_copy));
        });
  }

  // Restart a specific child entry, stopping it first if running
  template <std::size_t Index>
  void restart_child_entry(ActorRef ref, ChildEntry& entry) {
    using ArgsTuple =
        typename std::tuple_element<Index,
                                    std::tuple<ChildSpecs...>>::type::args_type;
    // If child is still running, stop it first
    if (!entry.stopping) {
      entry.suppress_restart = false;
      request_stop(ref);
      return;
    }
    // Child already stopped, respawn with same args
    auto& args = std::any_cast<const ArgsTuple&>(entry.args);
    auto args_copy = args;
    start_child_with_args<Index>(std::move(args_copy));
  }

  void request_stop(ActorRef actor_ref) {
    auto& child = children_.at(actor_ref);
    if (child.stopping) {
      return;
    }
    child.stopping = true;
    this->scheduler().stop(actor_ref, ExitReason{ExitReason::Kind::stopped});
  }

  bool should_restart(Restart restart, const ExitReason& reason) const {
    if (restart == Restart::permanent) {
      return true;
    }
    if (restart == Restart::transient) {
      return reason.kind != ExitReason::Kind::normal;
    }
    assert(restart == Restart::temporary);
    return false;
  }

  bool register_restart() {
    if (specification_.intensity.max_restarts == 0) {
      this->stop(ExitReason{ExitReason::Kind::error});
      return false;
    }

    auto now = detail::scheduler_now(this->scheduler());
    auto within = specification_.intensity.within;
    std::erase_if(restart_times_,
                  [&](const auto& restart_time) {
                    return now - restart_time > within;
                  });
    restart_times_.push_back(now);
    if (restart_times_.size() > specification_.intensity.max_restarts) {
      this->stop(ExitReason{ExitReason::Kind::error});
      return false;
    }
    return true;
  }

  Restart restart_at_spec_index(std::size_t index) const {
    Restart restart = Restart::permanent;
    const bool found = detail::visit_at_index<sizeof...(ChildSpecs)>(
        index, [&]<std::size_t Index>() {
          restart = std::get<Index>(states_).spec.restart;
        });
    assert(found);
    return restart;
  }

  template <std::size_t Index = 0>
  task<void> start_all_from_specs() {
    if constexpr (Index >= sizeof...(ChildSpecs)) {
      co_return;
    } else {
      co_await start_child_by_index<Index>();
      co_await start_all_from_specs<Index + 1>();
    }
  }

  Specification specification_{};

 protected:
  std::tuple<SpecState<ChildSpecs>...> states_{};

  Specification& specification_mutable() { return specification_; }

  template <std::size_t Index>
  void set_child_args(typename std::tuple_element<
                      Index, std::tuple<ChildSpecs...>>::type::args_type args) {
    auto& state = std::get<Index>(states_);
    state.spec.args = std::move(args);
  }

  template <std::size_t Index>
  auto& state_for_specs() {
    return std::get<Index>(states_);
  }

 private:
  std::map<ActorRef, ChildEntry> children_;
  std::deque<time_point> restart_times_;
  std::optional<RestartGroup> restart_group_;
  std::size_t next_start_order_ = 1;
  bool stopping_ = false;
};

}  // namespace agner
