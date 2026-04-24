#pragma once

#include <algorithm>
#include <any>
#include <atomic>
#include <coroutine>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

#include "agner/actor_control.hpp"
#include "agner/detail/actor_detail.hpp"
#include "agner/detail/scheduler_traits.hpp"
#include "agner/scheduler_concept.hpp"
#include "agner/task.hpp"

namespace agner {

/**
 * @brief CRTP base class providing actor management for schedulers.
 *
 * Implements spawn, send, link, and monitor operations. Derived schedulers
 * must implement schedule(), schedule_after(), and run().
 *
 * All map access is guarded by base_mutex_. For single-threaded schedulers
 * this is a null_mutex (compiles to nothing). For concurrent schedulers it
 * is std::mutex.
 *
 * @tparam Derived The derived scheduler class (CRTP).
 */
template <typename Derived>
// GCOVR_EXCL_BR_START
class SchedulerBase {
 public:
  using base_mutex_type = detail::scheduler_mutex_t<Derived>;

  /// @brief Send a message to an actor.
  /// @param target The recipient actor.
  /// @param message The message to deliver.
  void send(ActorRef target, MessageType auto&& message) {
    std::function<void(std::any&&)> send_fn;
    {
      std::lock_guard<base_mutex_type> lock(base_mutex_);
      auto entry = actors_.find(target);
      if (entry == actors_.end()) {
        return;
      }
      send_fn = entry->second.send;
    }
    send_fn(std::any(std::forward<decltype(message)>(message)));
  }

  /// @brief Send a message to a typed actor handle.
  template <typename ActorType, typename Message>
    requires detail::MessageForActor<ActorType, Message>
  void send(ActorHandle<ActorType> target, Message&& message) {
    send(target.ref(), std::forward<Message>(message));
  }

  template <typename ActorType, typename Message>
    requires(!detail::MessageForActor<ActorType, Message>)
  void send(ActorHandle<ActorType>, Message&&) = delete;

  /// @brief Establish a bidirectional link between two actors.
  void link(ActorRef left, ActorRef right) {
    std::lock_guard<base_mutex_type> lock(base_mutex_);
    actors_.at(left);
    actors_.at(right);
    links_[left].push_back(right);
    links_[right].push_back(left);
  }

  /// @brief Set up monitoring of one actor by another.
  /// @param monitor_ref The actor that will receive DownSignal.
  /// @param target_ref The actor to monitor.
  void monitor(ActorRef monitor_ref, ActorRef target_ref) {
    std::lock_guard<base_mutex_type> lock(base_mutex_);
    actors_.at(monitor_ref);
    actors_.at(target_ref);
    monitors_[target_ref].push_back(monitor_ref);
  }

  /// @brief Spawn a new actor.
  /// @return Reference to the spawned actor.
  template <typename ActorType, typename... Args>
  ActorHandle<ActorType> spawn(Args&&... args) {
    return spawn_impl<ActorType>(ActorRef{}, ActorRef{},
                                 std::forward<Args>(args)...);
  }

  /// @brief Spawn a new actor and link it to another.
  /// @param linker The actor to link with.
  /// @return Reference to the spawned actor.
  template <typename ActorType, typename... Args>
  ActorHandle<ActorType> spawn_link(ActorRef linker, Args&&... args) {
    return spawn_impl<ActorType>(linker, ActorRef{},
                                 std::forward<Args>(args)...);
  }

  /// @brief Spawn a new actor and monitor it.
  /// @param monitor_ref The actor that will monitor the spawned actor.
  /// @return Reference to the spawned actor.
  template <typename ActorType, typename... Args>
  ActorHandle<ActorType> spawn_monitor(ActorRef monitor_ref, Args&&... args) {
    return spawn_impl<ActorType>(ActorRef{}, monitor_ref,
                                 std::forward<Args>(args)...);
  }

  /// @brief Get the number of live actors.
  size_t active_actor_count() const noexcept {
    return active_actor_count_.load(std::memory_order_relaxed);
  }

 protected:
  struct ActorEntry {
    std::shared_ptr<ActorControl> control;
    std::function<void(std::any&&)> send;
  };

  template <typename ActorType>
  task<void> run_actor(std::shared_ptr<ActorType> actor, ActorRef actor_ref) {
    ExitReason reason{};
    try {
      reason = co_await actor->start();
    } catch (...) {
      reason.kind = ExitReason::Kind::error;
    }
    self().on_actor_exit(actor_ref, reason);
    co_return;
  }

  /// @brief Default handler for actor exit. Derived schedulers may override.
  void on_actor_exit(ActorRef actor_ref, const ExitReason& reason) {
    notify_exit(actor_ref, reason);
  }

  template <typename ActorType, typename... Args>
  ActorHandle<ActorType> spawn_impl(ActorRef linker, ActorRef monitor_ref,
                                    Args&&... args) {
    auto actor = std::make_shared<ActorType>(self(), std::forward<Args>(args)...);
    auto actor_ref = next_actor_ref();
    actor->set_actor_ref(actor_ref);

    {
      std::lock_guard<base_mutex_type> lock(base_mutex_);
      actors_.emplace(actor_ref, ActorEntry{actor, [actor](std::any&& message) {
                                              detail::dispatch_any_message(
                                                  actor, std::move(message));
                                            }});

      if (linker.valid()) {
        links_[linker].push_back(actor_ref);
        links_[actor_ref].push_back(linker);
      }
      if (monitor_ref.valid()) {
        monitors_[actor_ref].push_back(monitor_ref);
      }
    }

    active_actor_count_.fetch_add(1, std::memory_order_relaxed);
    run_actor(actor, actor_ref).detach(self());
    return ActorHandle<ActorType>{actor_ref};
  }

  bool actor_exists(ActorRef actor_ref) const {
    std::lock_guard<base_mutex_type> lock(base_mutex_);
    return actors_.find(actor_ref) != actors_.end();
  }

  /// @brief Collect and deliver exit/down signals, erase actor from registry.
  ///
  /// Signals are collected under the lock, then delivered outside it to
  /// avoid holding base_mutex_ during cross-actor message delivery.
  void notify_exit(ActorRef actor_ref, const ExitReason& reason) {
    struct PendingSignal {
      ActorRef target;
      ExitSignal exit_signal;
    };
    struct PendingDown {
      ActorRef target;
      DownSignal down_signal;
    };

    std::vector<PendingSignal> exit_signals;
    std::vector<PendingDown> down_signals;

    {
      std::lock_guard<base_mutex_type> lock(base_mutex_);

      auto links_entry = links_.find(actor_ref);
      if (links_entry != links_.end()) {
        for (auto linked : links_entry->second) {
          exit_signals.push_back(
              PendingSignal{linked, ExitSignal{actor_ref, reason}});
          auto reverse_entry = links_.find(linked);
          if (reverse_entry != links_.end()) {
            auto& reverse_links = reverse_entry->second;
            reverse_links.erase(std::remove(reverse_links.begin(),
                                            reverse_links.end(),
                                            actor_ref),
                                reverse_links.end());
          }
        }
        links_.erase(links_entry);
      }

      auto monitors_entry = monitors_.find(actor_ref);
      if (monitors_entry != monitors_.end()) {
        for (auto mon : monitors_entry->second) {
          down_signals.push_back(
              PendingDown{mon, DownSignal{actor_ref, reason}});
        }
        monitors_.erase(monitors_entry);
      }

      actors_.erase(actor_ref);
    }

    active_actor_count_.fetch_sub(1, std::memory_order_relaxed);

    for (auto& sig : exit_signals) {
      static_cast<Derived*>(this)->send(sig.target, std::move(sig.exit_signal));
    }
    for (auto& sig : down_signals) {
      static_cast<Derived*>(this)->send(sig.target, std::move(sig.down_signal));
    }
  }

  ActorRef next_actor_ref() noexcept {
    return ActorRef{next_actor_id_.fetch_add(1, std::memory_order_relaxed)};
  }

  Derived& self() noexcept { return *static_cast<Derived*>(this); }

  mutable base_mutex_type base_mutex_;
  std::map<ActorRef, ActorEntry> actors_;
  std::map<ActorRef, std::vector<ActorRef>> links_;
  std::map<ActorRef, std::vector<ActorRef>> monitors_;
  std::atomic<uint64_t> next_actor_id_{1};
  std::atomic<size_t> active_actor_count_{0};
};
// GCOVR_EXCL_BR_STOP

}  // namespace agner
