#pragma once

#include <algorithm>
#include <any>
#include <coroutine>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <utility>
#include <vector>

#include "agner/actor_control.hpp"
#include "agner/detail/actor_detail.hpp"
#include "agner/scheduler_concept.hpp"
#include "agner/task.hpp"

namespace agner {

/**
 * @brief CRTP base class providing actor management for schedulers.
 *
 * Implements spawn, send, link, and monitor operations. Derived schedulers
 * must implement schedule(), schedule_after(), and run().
 *
 * @tparam Derived The derived scheduler class (CRTP).
 */
template <typename Derived>
class SchedulerBase {
 public:
  /// @brief Send a message to an actor.
  /// @param target The recipient actor.
  /// @param message The message to deliver.
  void send(ActorRef target, MessageType auto&& message) {
    auto entry = actors_.find(target);
    if (entry == actors_.end()) {
      return;
    }
    entry->second.send(std::any(std::forward<decltype(message)>(message)));
  }

  /// @brief Establish a bidirectional link between two actors.
  void link(ActorRef left, ActorRef right) {
    actors_.at(left);
    actors_.at(right);
    links_[left].push_back(right);
    links_[right].push_back(left);
  }

  /// @brief Set up monitoring of one actor by another.
  /// @param monitor_ref The actor that will receive DownSignal.
  /// @param target_ref The actor to monitor.
  void monitor(ActorRef monitor_ref, ActorRef target_ref) {
    actors_.at(monitor_ref);
    actors_.at(target_ref);
    monitors_[target_ref].push_back(monitor_ref);
  }

  /// @brief Spawn a new actor.
  /// @return Reference to the spawned actor.
  template <typename ActorType, typename... Args>
  ActorRef spawn(Args&&... args) {
    return spawn_impl<ActorType>(ActorRef{}, ActorRef{},
                                 std::forward<Args>(args)...);
  }

  /// @brief Spawn a new actor and link it to another.
  /// @param linker The actor to link with.
  /// @return Reference to the spawned actor.
  template <typename ActorType, typename... Args>
  ActorRef spawn_link(ActorRef linker, Args&&... args) {
    return spawn_impl<ActorType>(linker, ActorRef{},
                                 std::forward<Args>(args)...);
  }

  /// @brief Spawn a new actor and monitor it.
  /// @param monitor_ref The actor that will monitor the spawned actor.
  /// @return Reference to the spawned actor.
  template <typename ActorType, typename... Args>
  ActorRef spawn_monitor(ActorRef monitor_ref, Args&&... args) {
    return spawn_impl<ActorType>(ActorRef{}, monitor_ref,
                                 std::forward<Args>(args)...);
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
    notify_exit(actor_ref, reason);
    actors_.erase(actor_ref);
    co_return;
  }

  template <typename ActorType, typename... Args>
  ActorRef spawn_impl(ActorRef linker, ActorRef monitor_ref, Args&&... args) {
    auto actor = std::make_shared<ActorType>(self(), std::forward<Args>(args)...);
    auto actor_ref = next_actor_ref();
    actor->set_actor_ref(actor_ref);
    actors_.emplace(actor_ref, ActorEntry{actor, [actor](std::any&& message) {
                                            detail::dispatch_any_message(
                                                actor, std::move(message));
                                          }});

    if (linker.valid()) {
      static_cast<Derived*>(this)->link(linker, actor_ref);
    }
    if (monitor_ref.valid()) {
      static_cast<Derived*>(this)->monitor(monitor_ref, actor_ref);
    }

    run_actor(actor, actor_ref).detach(self());
    return actor_ref;
  }

  bool actor_exists(ActorRef actor_ref) const {
    return actors_.find(actor_ref) != actors_.end();
  }

  void notify_exit(ActorRef actor_ref, const ExitReason& reason) {
    auto links_entry = links_.find(actor_ref);
    if (links_entry != links_.end()) {
      std::for_each(links_entry->second.begin(), links_entry->second.end(),
                    [&](ActorRef linked) {
                      static_cast<Derived*>(this)->send(
                          linked, ExitSignal{actor_ref, reason});
                      auto reverse_entry = links_.find(linked);
                      if (reverse_entry != links_.end()) {
                        auto& reverse_links = reverse_entry->second;
                        reverse_links.erase(std::remove(reverse_links.begin(),
                                                        reverse_links.end(),
                                                        actor_ref),
                                            reverse_links.end());
                      }
                    });
      links_.erase(links_entry);
    }

    auto monitors_entry = monitors_.find(actor_ref);
    if (monitors_entry != monitors_.end()) {
      std::for_each(monitors_entry->second.begin(), monitors_entry->second.end(),
                    [&](ActorRef monitor_ref) {
                      static_cast<Derived*>(this)->send(
                          monitor_ref, DownSignal{actor_ref, reason});
                    });
      monitors_.erase(monitors_entry);
    }
  }

  ActorRef next_actor_ref() noexcept {
    ActorRef actor_ref{next_actor_id_};
    ++next_actor_id_;
    return actor_ref;
  }

  Derived& self() noexcept { return *static_cast<Derived*>(this); }

  std::map<ActorRef, ActorEntry> actors_;
  std::map<ActorRef, std::vector<ActorRef>> links_;
  std::map<ActorRef, std::vector<ActorRef>> monitors_;
  uint64_t next_actor_id_ = 1;
};

}  // namespace agner
