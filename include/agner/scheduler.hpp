#pragma once

#include <algorithm>
#include <any>
#include <cassert>
#include <chrono>
#include <coroutine>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

#include "actor_control.hpp"
#include "actor_detail.hpp"
#include "scheduler_concept.hpp"
#include "task.hpp"

namespace agner {

class Scheduler {
 public:
  using Clock = std::chrono::steady_clock;
#if defined(AGNER_TESTING)
  friend struct SchedulerTestAccess;
#endif

  void schedule(std::coroutine_handle<> handle) {
    if (handle) {
      ready_.push_back(handle);
    }
  }

  template <typename Rep, typename Period, typename Fn>
  void schedule_after(std::chrono::duration<Rep, Period> delay, Fn&& fn) {
    timers_.emplace(Clock::now() + delay,
                    std::function<void()>(std::forward<Fn>(fn)));
  }

  void run() {
    while (!ready_.empty() || !timers_.empty()) {
      if (!ready_.empty()) {
        auto handle = ready_.front();
        ready_.pop_front();
        if (!handle.done()) {
          handle.resume();
        }
        continue;
      }

      while (!timers_.empty()) {
        auto now = Clock::now();
        if (timers_.begin()->first > now) {
          std::this_thread::sleep_until(timers_.begin()->first);
          continue;
        }
        auto callback = std::move(timers_.begin()->second);
        timers_.erase(timers_.begin());
        callback();
      }
    }
  }

  template <typename ActorType, typename... Args>
  ActorRef spawn(Args&&... args) {
    return spawn_impl<ActorType>(ActorRef{}, ActorRef{},
                                 std::forward<Args>(args)...);
  }

  template <typename ActorType, typename... Args>
  ActorRef spawn_link(ActorRef linker, Args&&... args) {
    return spawn_impl<ActorType>(linker, ActorRef{},
                                 std::forward<Args>(args)...);
  }

  template <typename ActorType, typename... Args>
  ActorRef spawn_monitor(ActorRef monitor, Args&&... args) {
    return spawn_impl<ActorType>(ActorRef{}, monitor,
                                 std::forward<Args>(args)...);
  }

  template <typename Message>
  void send(ActorRef target, Message&& message) {
    auto entry = actors_.find(target);
    if (entry == actors_.end()) {
      return;
    }
    entry->second.send(std::any(std::forward<Message>(message)));
  }

  void stop(ActorRef target, ExitReason reason = {}) {
    auto entry = actors_.find(target);
    if (entry == actors_.end()) {
      return;
    }
    entry->second.control->stop(reason);
  }

  void link(ActorRef left, ActorRef right) {
    if (!left.valid() || !right.valid()) {
      return;
    }
    if (!actor_exists(left) || !actor_exists(right)) {
      return;
    }
    links_[left].push_back(right);
    links_[right].push_back(left);
  }

  void monitor(ActorRef monitor_ref, ActorRef target_ref) {
    if (!monitor_ref.valid() || !target_ref.valid()) {
      return;
    }
    if (!actor_exists(monitor_ref) || !actor_exists(target_ref)) {
      return;
    }
    monitors_[target_ref].push_back(monitor_ref);
  }

 private:
  struct ActorEntry {
    std::shared_ptr<ActorControl> control;
    std::function<void(std::any&&)> send;
  };

  task<void> run_task(task<ExitReason> actor_task, ActorRef actor_ref) {
    ExitReason reason{};
    try {
      reason = co_await actor_task;
    } catch (...) {
      reason.kind = ExitReason::Kind::error;
    }
    notify_exit(actor_ref, reason);
    actors_.erase(actor_ref);
    co_return;
  }

  template <typename ActorType>
  task<void> run_actor(std::shared_ptr<ActorType> actor, ActorRef actor_ref) {
    co_return co_await run_task(actor->start(), actor_ref);
  }

  template <typename ActorType, typename... Args>
  ActorRef spawn_impl(ActorRef linker, ActorRef monitor_ref, Args&&... args) {
    auto actor =
        std::make_shared<ActorType>(*this, std::forward<Args>(args)...);
    auto actor_ref = next_actor_ref();
    actor->set_actor_ref(actor_ref);
    actors_.emplace(actor_ref, ActorEntry{actor, [actor](std::any&& message) {
                                            detail::dispatch_any_message(
                                                actor, std::move(message));
                                          }});

    setup_links(linker, monitor_ref, actor_ref);

    run_actor(actor, actor_ref).detach(*this);
    return actor_ref;
  }

  void setup_links(ActorRef linker, ActorRef monitor_ref, ActorRef actor_ref) {
    if (linker.valid()) {
      link(linker, actor_ref);
    }
    if (monitor_ref.valid()) {
      monitor(monitor_ref, actor_ref);
    }
  }

  bool actor_exists(ActorRef actor_ref) const {
    return actors_.find(actor_ref) != actors_.end();
  }

  void notify_exit(ActorRef actor_ref, const ExitReason& reason) {
    auto links_entry = links_.find(actor_ref);
    if (links_entry != links_.end()) {
      for (auto linked : links_entry->second) {
        send(linked, ExitSignal{actor_ref, reason});
        auto reverse_entry = links_.find(linked);
        if (reverse_entry != links_.end()) {
          auto& reverse_links = reverse_entry->second;
          reverse_links.erase(std::remove(reverse_links.begin(),
                                          reverse_links.end(), actor_ref),
                              reverse_links.end());
        }
      }
      links_.erase(links_entry);
    }

    auto monitors_entry = monitors_.find(actor_ref);
    if (monitors_entry != monitors_.end()) {
      for (auto monitor_ref : monitors_entry->second) {
        send(monitor_ref, DownSignal{actor_ref, reason});
      }
      monitors_.erase(monitors_entry);
    }
  }

  ActorRef next_actor_ref() noexcept {
    ActorRef actor_ref{next_actor_id_};
    ++next_actor_id_;
    return actor_ref;
  }

  std::deque<std::coroutine_handle<>> ready_;
  std::multimap<Clock::time_point, std::function<void()>> timers_;
  std::map<ActorRef, ActorEntry> actors_;
  std::map<ActorRef, std::vector<ActorRef>> links_;
  std::map<ActorRef, std::vector<ActorRef>> monitors_;
  uint64_t next_actor_id_ = 1;
};

}  // namespace agner
