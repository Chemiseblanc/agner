#pragma once

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

#include "actor.hpp"
#include "actor_control.hpp"
#include "errors.hpp"
#include "scheduler_concept.hpp"

namespace agner {

enum class Restart { permanent, transient, temporary };

enum class Strategy {
  one_for_one,
  one_for_all,
  rest_for_one,
  simple_one_for_one
};

struct Intensity {
  std::size_t max_restarts = 0;
  std::chrono::steady_clock::duration within{};
};

template <std::size_t Index>
using ChildIndex = std::integral_constant<std::size_t, Index>;

struct ChildId {
  std::string_view value;
};

template <typename ActorType, typename... Args>
struct ChildSpec {
  using actor_type = ActorType;
  using args_type = std::tuple<std::decay_t<Args>...>;

  ChildId id;
  Restart restart = Restart::permanent;
  std::chrono::steady_clock::duration shutdown{};
  args_type args{};
  bool simple = false;
};

template <typename ActorType, typename... Args>
ChildSpec<ActorType, Args...> child(
    ChildId id, Restart restart, std::chrono::steady_clock::duration shutdown,
    Args&&... args) {
  return ChildSpec<ActorType, Args...>{
      id, restart, shutdown, std::make_tuple(std::forward<Args>(args)...),
      false};
}

template <typename ActorType, typename... Args>
ChildSpec<ActorType, Args...> simple_child(
    ChildId id, Restart restart, std::chrono::steady_clock::duration shutdown) {
  return ChildSpec<ActorType, Args...>{id, restart, shutdown, {}, true};
}

namespace detail {

template <typename T>
struct is_child_index : std::false_type {};

template <std::size_t Index>
struct is_child_index<std::integral_constant<std::size_t, Index>>
    : std::true_type {};

template <typename T>
inline constexpr bool is_child_index_v = is_child_index<T>::value;

template <typename ActorType, typename Spec>
struct is_spec_for : std::false_type {};

template <typename ActorType, typename... Args>
struct is_spec_for<ActorType, ChildSpec<ActorType, Args...>> : std::true_type {
};

template <typename ActorType, typename... Specs>
struct spec_count;

template <typename ActorType>
struct spec_count<ActorType> : std::integral_constant<std::size_t, 0> {};

template <typename ActorType, typename Spec, typename... Rest>
struct spec_count<ActorType, Spec, Rest...>
    : std::integral_constant<std::size_t,
                             (is_spec_for<ActorType, Spec>::value ? 1 : 0) +
                                 spec_count<ActorType, Rest...>::value> {};

template <typename ActorType, std::size_t Index, typename... Specs>
struct spec_index_impl;

template <typename ActorType, std::size_t Index, typename Spec,
          typename... Rest>
struct spec_index_impl<ActorType, Index, Spec, Rest...> {
  static constexpr std::size_t value =
      is_spec_for<ActorType, Spec>::value
          ? Index
          : spec_index_impl<ActorType, Index + 1, Rest...>::value;
};

template <typename ActorType, std::size_t Index>
struct spec_index_impl<ActorType, Index> {
  static constexpr std::size_t value = Index;
};

template <typename ActorType, typename... Specs>
constexpr std::size_t spec_index_v =
    spec_index_impl<ActorType, 0, Specs...>::value;

template <typename T>
std::decay_t<T> clone_or_move(T& value) {
  if constexpr (std::is_copy_constructible_v<T>) {
    return value;
  } else {
    return std::move(value);
  }
}

template <typename SchedulerType>
auto scheduler_now(SchedulerType& scheduler) ->
    typename SchedulerType::Clock::time_point {
  if constexpr (requires { scheduler.now(); }) {
    return scheduler.now();
  } else {
    return SchedulerType::Clock::now();
  }
}

}  // namespace detail

template <SchedulerLike SchedulerType, typename Derived, typename... ChildSpecs>
class Supervisor : public Actor<SchedulerType, Derived, Messages<>> {
 public:
  static_assert(sizeof...(ChildSpecs) > 0,
                "Supervisor requires at least one child specification.");
  using Base = Actor<SchedulerType, Derived, Messages<>>;
  using Clock = typename SchedulerType::Clock;
  using time_point = typename Clock::time_point;
#if defined(AGNER_TESTING)
  friend struct SupervisorTestAccess;
#endif

  struct Specification {
    Strategy strategy = Strategy::one_for_one;
    Intensity intensity{};
    std::tuple<ChildSpecs...> children{};
  };

  explicit Supervisor(SchedulerType& scheduler)
      : Base(scheduler), specification_(Derived::specification()) {
    initialize_states();
  }

  task<void> init() {
    if (specification_.strategy != Strategy::simple_one_for_one) {
      co_await start_all_from_specs();
    }
    co_return;
  }

  template <typename Selector, typename... Args>
  task<ActorRef> start_child(Args&&... args) {
    if constexpr (detail::is_child_index_v<Selector>) {
      constexpr std::size_t index = Selector::value;
      co_return co_await start_child_by_index<index>(
          std::forward<Args>(args)...);
    } else {
      static constexpr std::size_t count =
          detail::spec_count<Selector, ChildSpecs...>::value;
      static_assert(count == 1,
                    "start_child requires a unique ActorType selector.");
      constexpr std::size_t index =
          detail::spec_index_v<Selector, ChildSpecs...>;
      co_return co_await start_child_by_index<index>(
          std::forward<Args>(args)...);
    }
  }

  template <typename Selector>
  task<void> stop_child() {
    if constexpr (detail::is_child_index_v<Selector>) {
      constexpr std::size_t index = Selector::value;
      stop_children_by_index<index>();
    } else {
      static constexpr std::size_t count =
          detail::spec_count<Selector, ChildSpecs...>::value;
      static_assert(count == 1,
                    "stop_child requires a unique ActorType selector.");
      constexpr std::size_t index =
          detail::spec_index_v<Selector, ChildSpecs...>;
      stop_children_by_index<index>();
    }
    co_return;
  }

  template <typename Selector>
  task<void> restart_child() {
    if constexpr (detail::is_child_index_v<Selector>) {
      constexpr std::size_t index = Selector::value;
      restart_children_by_index<index>();
    } else {
      static constexpr std::size_t count =
          detail::spec_count<Selector, ChildSpecs...>::value;
      static_assert(count == 1,
                    "restart_child requires a unique ActorType selector.");
      constexpr std::size_t index =
          detail::spec_index_v<Selector, ChildSpecs...>;
      restart_children_by_index<index>();
    }
    co_return;
  }

  template <typename Selector>
  task<void> delete_child() {
    if constexpr (detail::is_child_index_v<Selector>) {
      constexpr std::size_t index = Selector::value;
      delete_children_by_index<index>();
    } else {
      static constexpr std::size_t count =
          detail::spec_count<Selector, ChildSpecs...>::value;
      static_assert(count == 1,
                    "delete_child requires a unique ActorType selector.");
      constexpr std::size_t index =
          detail::spec_index_v<Selector, ChildSpecs...>;
      delete_children_by_index<index>();
    }
    co_return;
  }

  std::vector<ChildId> which_children() const {
    std::vector<ChildId> ids;
    ids.reserve(sizeof...(ChildSpecs));
    std::apply(
        [&](const auto&... state) { (ids.push_back(state.spec.id), ...); },
        states_);
    return ids;
  }

  std::optional<ActorRef> child_ref(ChildId id) const {
    std::optional<ActorRef> result;
    std::apply(
        [&](const auto&... state) {
          ((result = result ? result : state.find_child_ref(id)), ...);
        },
        states_);
    return result;
  }

  task<void> run() {
    co_await init();
    co_await supervise_loop();
  }

  void stop(ExitReason reason = {}) override {
    stopping_ = true;
    Base::stop(reason);
  }

 protected:
  task<void> supervise_loop() {
    while (true) {
      bool stop_requested = false;
      co_await this->receive([&](DownSignal& signal) { handle_down(signal); },
                             [&](ExitSignal& signal) {
                               if (stopping_) {
                                 if (signal.from == this->self()) {
                                   stop_requested = true;
                                   return;
                                 }
                               }
                               handle_exit(signal);
                             });
      if (stop_requested) {
        co_return;
      }
    }
  }

 private:
  struct ChildLocation {
    std::size_t spec_index = 0;
    std::size_t instance_index = 0;
    std::size_t start_order = 0;
  };

  struct RestartPlanItem {
    ChildLocation location;
    ExitReason reason;
  };

  struct RestartGroup {
    Strategy strategy = Strategy::one_for_all;
    std::vector<RestartPlanItem> plan;
    std::vector<ActorRef> pending_stops;
  };

  template <typename Spec>
  struct SpecState {
    using ActorType = typename Spec::actor_type;
    using ArgsTuple = typename Spec::args_type;

    struct Instance {
      std::optional<ActorRef> ref;
      ArgsTuple args{};
      std::size_t start_order = 0;
      bool stopping = false;
      bool suppress_restart = false;
    };

    Spec spec;
    std::vector<Instance> instances;

    std::optional<ActorRef> find_child_ref(ChildId id) const {
      if (spec.id.value != id.value) {
        return std::nullopt;
      }
      for (const auto& instance : instances) {
        if (instance.ref) {
          return instance.ref;
        }
      }
      return std::nullopt;
    }
  };

  using StatesTuple = std::tuple<SpecState<ChildSpecs>...>;
  using SpecsTuple = std::tuple<ChildSpecs...>;

  void initialize_states() {
    states_ = std::apply(
        [&](const auto&... spec) {
          return std::tuple<SpecState<ChildSpecs>...>{
              SpecState<ChildSpecs>{spec}...};
        },
        specification_.children);
  }

  template <std::size_t Index>
  task<ActorRef> start_child_by_index() {
    auto& state = std::get<Index>(states_);
    if (specification_.strategy == Strategy::simple_one_for_one) {
      co_return co_await start_child_by_index<Index>(
          typename decltype(state.spec)::args_type{});
    }
    co_return co_await start_child_by_index<Index>(state.spec.args);
  }

  template <std::size_t Index>
  task<ActorRef> start_child_by_index(
      typename std::tuple_element<
          Index, std::tuple<ChildSpecs...>>::type::args_type args) {
    auto& state = std::get<Index>(states_);
    co_return start_child_with_args(state, std::move(args),
                                    ChildLocation{Index, 0, 0});
  }

  template <std::size_t Index, typename... Args>
  task<ActorRef> start_child_by_index(Args&&... args) {
    auto& state = std::get<Index>(states_);
    auto args_tuple =
        typename decltype(state.spec)::args_type{std::forward<Args>(args)...};
    co_return start_child_with_args(state, std::move(args_tuple),
                                    ChildLocation{Index, 0, 0});
  }

  template <typename State>
  ActorRef start_child_with_args(State& state, typename State::ArgsTuple args,
                                 ChildLocation location_hint) {
    const bool is_simple =
        specification_.strategy == Strategy::simple_one_for_one;
    if (!is_simple) {
      if (state.instances.empty()) {
        state.instances.emplace_back();
      }
      auto& root_instance = state.instances.front();
      if (root_instance.ref) {
        return *root_instance.ref;
      }
    }

    auto instance =
        is_simple ? typename State::Instance{} : state.instances.front();
    instance.args = std::move(args);
    instance.start_order = next_start_order_++;
    instance.stopping = false;
    instance.suppress_restart = false;

    ActorRef actor_ref = spawn_child(state, instance);
    instance.ref = actor_ref;

    if (specification_.strategy == Strategy::simple_one_for_one) {
      state.instances.push_back(std::move(instance));
      location_hint.instance_index = state.instances.size() - 1;
    } else {
      state.instances.front() = std::move(instance);
      location_hint.instance_index = 0;
    }
    location_hint.start_order = instance.start_order;
    live_children_[actor_ref] = location_hint;

    return actor_ref;
  }

  template <typename State>
  ActorRef spawn_child(State& state, typename State::Instance& instance) {
    auto actor_ref = std::apply(
        [&](auto&... args) {
          return this->template spawn<typename State::ActorType>(
              detail::clone_or_move(args)...);
        },
        instance.args);
    this->monitor(actor_ref);
    return actor_ref;
  }

  template <std::size_t Index>
  void stop_children_by_index() {
    auto& state = std::get<Index>(states_);
    for (auto& instance : state.instances) {
      if (!instance.ref) {
        continue;
      }
      instance.suppress_restart = true;
      request_stop(instance, *instance.ref);
    }
  }

  template <std::size_t Index>
  void restart_children_by_index() {
    auto& state = std::get<Index>(states_);
    for (std::size_t i = 0; i < state.instances.size(); ++i) {
      auto& instance = state.instances[i];
      instance.suppress_restart = false;
      if (!instance.ref) {
        if (instance.start_order == 0) {
          continue;
        }
      }
      restart_instance(state, instance, {Index, i, instance.start_order});
    }
  }

  template <std::size_t Index>
  void delete_children_by_index() {
    auto& state = std::get<Index>(states_);
    for (auto& instance : state.instances) {
      if (instance.ref) {
        instance.suppress_restart = true;
        request_stop(instance, *instance.ref);
      }
    }
    state.instances.clear();
  }

  void handle_down(DownSignal& signal) {
    if (!signal.from.valid()) {
      return;
    }
    handle_termination(signal.from, signal.reason);
  }

  void handle_exit(ExitSignal& signal) {
    if (!signal.from.valid()) {
      return;
    }
    handle_termination(signal.from, signal.reason);
  }

  void handle_termination(ActorRef actor_ref, const ExitReason& reason) {
    if (restart_group_) {
      handle_restart_group_stop(actor_ref, reason);
      return;
    }

    auto location_it = live_children_.find(actor_ref);
    if (location_it == live_children_.end()) {
      return;
    }

    auto location = location_it->second;
    live_children_.erase(location_it);

    auto& state = state_at(location.spec_index);
    auto& spec = spec_at(location.spec_index);
    auto& instance = state.instances.at(location.instance_index);
    instance.ref.reset();
    instance.stopping = false;

    if (instance.suppress_restart) {
      if (specification_.strategy == Strategy::simple_one_for_one) {
        state.instances.erase(
            state.instances.begin() +
            static_cast<std::ptrdiff_t>(location.instance_index));
      }
      return;
    }

    if (!should_restart(spec.restart, reason)) {
      if (specification_.strategy == Strategy::simple_one_for_one) {
        state.instances.erase(
            state.instances.begin() +
            static_cast<std::ptrdiff_t>(location.instance_index));
      }
      return;
    }

    if (!register_restart()) {
      return;
    }

    if (specification_.strategy == Strategy::one_for_one) {
      restart_instance(state, instance, location);
      return;
    }

    if (specification_.strategy == Strategy::simple_one_for_one) {
      restart_instance(state, instance, location);
      return;
    }

    begin_restart_group(location, reason);
  }

  void handle_restart_group_stop(ActorRef actor_ref, const ExitReason& reason) {
    auto location_it = live_children_.find(actor_ref);
    if (location_it != live_children_.end()) {
      auto location = location_it->second;
      live_children_.erase(location_it);
      auto& state = state_at(location.spec_index);
      auto& instance = state.instances.at(location.instance_index);
      instance.ref.reset();
      instance.stopping = false;
    }

    auto& pending = restart_group_->pending_stops;
    auto pending_it = std::find(pending.begin(), pending.end(), actor_ref);
    if (pending_it != pending.end()) {
      pending.erase(pending_it);
    }

    if (pending.empty()) {
      finalize_restart_group();
    }
    (void)reason;
  }

  void begin_restart_group(const ChildLocation& failed_location,
                           const ExitReason& reason) {
    RestartGroup group;
    group.strategy = specification_.strategy;

    std::vector<ChildLocation> restart_targets;
    restart_targets.reserve(live_children_.size() + 1);

    if (group.strategy == Strategy::one_for_all) {
      for (const auto& entry : live_children_) {
        restart_targets.push_back(entry.second);
      }
      restart_targets.push_back(failed_location);
    } else if (group.strategy == Strategy::rest_for_one) {
      restart_targets.push_back(failed_location);
      for (const auto& entry : live_children_) {
        if (entry.second.start_order > failed_location.start_order) {
          restart_targets.push_back(entry.second);
        }
      }
    }

    for (const auto& target : restart_targets) {
      ExitReason target_reason = reason;
      if (target.start_order != failed_location.start_order) {
        target_reason = ExitReason{ExitReason::Kind::stopped};
      }
      group.plan.push_back(RestartPlanItem{target, target_reason});
    }

    restart_group_ = std::move(group);

    for (const auto& target : restart_targets) {
      auto& state = state_at(target.spec_index);
      auto& instance = state.instances.at(target.instance_index);
      if (!instance.ref) {
        continue;
      }
      instance.suppress_restart = false;
      auto ref = *instance.ref;
      restart_group_->pending_stops.push_back(ref);
      request_stop(instance, ref);
    }

    if (restart_group_->pending_stops.empty()) {
      finalize_restart_group();
    }
  }

  void finalize_restart_group() {
    if (!restart_group_) {
      return;
    }

    auto group = std::move(*restart_group_);
    restart_group_.reset();

    for (const auto& plan_item : group.plan) {
      auto& state = state_at(plan_item.location.spec_index);
      auto& spec = spec_at(plan_item.location.spec_index);
      auto& instance = state.instances.at(plan_item.location.instance_index);
      if (instance.suppress_restart) {
        continue;
      }

      if (!should_restart(spec.restart, plan_item.reason)) {
        continue;
      }

      if (instance.ref) {
        if (instance.stopping) {
          continue;
        }
      }
      restart_instance(state, instance, plan_item.location);
    }
  }

  template <typename State>
  void restart_instance(State& state, typename State::Instance& instance,
                        ChildLocation location) {
    if (instance.ref) {
      request_stop(instance, *instance.ref);
      return;
    }
    instance.start_order = next_start_order_++;
    auto actor_ref = spawn_child(state, instance);
    instance.ref = actor_ref;
    location.start_order = instance.start_order;
    live_children_[actor_ref] = location;
  }

  template <typename Instance>
  void request_stop(Instance& instance, ActorRef actor_ref) {
    if (instance.stopping) {
      return;
    }
    instance.stopping = true;
    this->scheduler().stop(actor_ref, ExitReason{ExitReason::Kind::stopped});
  }

  bool should_restart(Restart restart, const ExitReason& reason) const {
    switch (restart) {
      case Restart::permanent:
        return true;
      case Restart::transient:
        return reason.kind != ExitReason::Kind::normal;
      case Restart::temporary:
        return false;
    }
    return false;
  }

  bool register_restart() {
    if (specification_.intensity.max_restarts == 0) {
      this->stop(ExitReason{ExitReason::Kind::error});
      return false;
    }

    auto now = detail::scheduler_now(this->scheduler());
    auto within = specification_.intensity.within;
    while (!restart_times_.empty()) {
      if (now - restart_times_.front() <= within) {
        break;
      }
      restart_times_.pop_front();
    }
    restart_times_.push_back(now);
    if (restart_times_.size() > specification_.intensity.max_restarts) {
      this->stop(ExitReason{ExitReason::Kind::error});
      return false;
    }
    return true;
  }

  template <std::size_t Index>
  auto& state_at() {
    return std::get<Index>(states_);
  }

  auto& state_at(std::size_t index) { return state_at_index<0>(index); }

  template <std::size_t Index>
  auto state_at_index(std::size_t index)
      -> decltype(std::get<0>(std::declval<StatesTuple&>())) {
    if constexpr (Index >= sizeof...(ChildSpecs)) {
      throw SupervisorInvariantError("Supervisor invalid state index");
    } else {
      if (Index == index) {
        return std::get<Index>(states_);
      }
      return state_at_index<Index + 1>(index);
    }
  }

  template <std::size_t Index>
  auto& spec_at() {
    return std::get<Index>(specification_.children);
  }

  auto& spec_at(std::size_t index) { return spec_at_index<0>(index); }

  template <std::size_t Index>
  auto spec_at_index(std::size_t index)
      -> decltype(std::get<0>(std::declval<SpecsTuple&>())) {
    if constexpr (Index >= sizeof...(ChildSpecs)) {
      throw SupervisorInvariantError("Supervisor invalid spec index");
    } else {
      if (Index == index) {
        return std::get<Index>(specification_.children);
      }
      return spec_at_index<Index + 1>(index);
    }
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
  std::map<ActorRef, ChildLocation> live_children_;
  std::deque<time_point> restart_times_;
  std::optional<RestartGroup> restart_group_;
  std::size_t next_start_order_ = 1;
  bool stopping_ = false;
};

}  // namespace agner
