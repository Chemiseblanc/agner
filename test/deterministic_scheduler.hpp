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
#include <random>
#include <utility>
#include <vector>

#include "agner/actor_control.hpp"
#include "agner/actor_detail.hpp"
#include "agner/scheduler_concept.hpp"
#include "agner/task.hpp"

namespace agner {

class DeterministicScheduler {
 public:
  struct Clock {
    using rep = int64_t;
    using period = std::nano;
    using duration = std::chrono::duration<rep, period>;
    using time_point = std::chrono::time_point<Clock>;
    static constexpr bool is_steady = true;
  };

  using duration = Clock::duration;
  using time_point = Clock::time_point;

  explicit DeterministicScheduler(uint64_t seed = 0) : rng_(seed) {}

  void schedule(std::coroutine_handle<> handle) {
    if (handle) {
      ready_.push_back(handle);
    }
  }

  template <typename Rep, typename Period, typename Fn>
  void schedule_after(std::chrono::duration<Rep, Period> delay, Fn&& fn) {
    auto cast_delay = std::chrono::duration_cast<duration>(delay);
    timers_.emplace(current_time_ + cast_delay,
                    std::function<void()>(std::forward<Fn>(fn)));
  }

  void run() {
    while (!ready_.empty() || !timers_.empty()) {
      run_until_idle();
      if (!timers_.empty()) {
        current_time_ = timers_.begin()->first;
      }
    }
  }

  void run_until_idle() {
    while (true) {
      process_due_timers();
      if (ready_.empty()) {
        break;
      }
      auto batch = take_ready();
      shuffle(batch);
      for (auto handle : batch) {
        if (!handle.done()) {
          handle.resume();
        }
      }
    }
  }

  void advance(duration ticks) {
    current_time_ += ticks;
    run_until_idle();
  }

  void run_for(duration ticks) { run_until(current_time_ + ticks); }

  void run_until(time_point time) {
    if (time < current_time_) {
      current_time_ = time;
      run_until_idle();
      return;
    }

    while (true) {
      run_until_idle();
      if (timers_.empty() || timers_.begin()->first > time) {
        current_time_ = time;
        run_until_idle();
        break;
      }
      current_time_ = timers_.begin()->first;
    }
  }

  time_point now() const noexcept { return current_time_; }

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
    assert(entry != actors_.end());
    if (stop_delivery_deferred_) {
      pending_stops_.push_back(StopRequest{target, reason});
      return;
    }
    deliver_stop(target, reason);
  }

  void defer_stop_delivery() { stop_delivery_deferred_ = true; }

  void resume_stop_delivery() { stop_delivery_deferred_ = false; }

  bool stop_delivery_deferred() const noexcept {
    return stop_delivery_deferred_;
  }

  std::size_t pending_stop_count() const noexcept {
    return pending_stops_.size();
  }

  void flush_next_stop() {
    if (pending_stops_.empty()) {
      return;
    }
    auto request = pending_stops_.front();
    pending_stops_.pop_front();
    deliver_stop(request.target, request.reason);
  }

  void flush_stop(ActorRef target) {
    auto it = std::find_if(
        pending_stops_.begin(), pending_stops_.end(),
        [&](const auto& request) { return request.target == target; });
    if (it == pending_stops_.end()) {
      return;
    }
    auto request = *it;
    pending_stops_.erase(it);
    deliver_stop(request.target, request.reason);
  }

  void flush_all_stops() {
    while (!pending_stops_.empty()) {
      flush_next_stop();
    }
  }

  void link(ActorRef left, ActorRef right) {
    if (!left.valid() || !right.valid()) {
      return;
    }
    assert(actor_exists(left));
    assert(actor_exists(right));
    links_[left].push_back(right);
    links_[right].push_back(left);
  }

  void monitor(ActorRef monitor_ref, ActorRef target_ref) {
    if (!monitor_ref.valid() || !target_ref.valid()) {
      return;
    }
    assert(actor_exists(monitor_ref));
    assert(actor_exists(target_ref));
    monitors_[target_ref].push_back(monitor_ref);
  }

 private:
  struct ActorEntry {
    std::shared_ptr<ActorControl> control;
    std::function<void(std::any&&)> send;
  };

  struct StopRequest {
    ActorRef target;
    ExitReason reason;
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
    auto actor =
        std::make_shared<ActorType>(*this, std::forward<Args>(args)...);
    auto actor_ref = next_actor_ref();
    actor->set_actor_ref(actor_ref);
    actors_.emplace(actor_ref, ActorEntry{actor, [actor](std::any&& message) {
                                            detail::dispatch_any_message(
                                                actor, std::move(message));
                                          }});

    if (linker.valid()) {
      link(linker, actor_ref);
    }
    if (monitor_ref.valid()) {
      monitor(monitor_ref, actor_ref);
    }

    run_actor(actor, actor_ref).detach(*this);
    return actor_ref;
  }

  ActorRef next_actor_ref() noexcept {
    ActorRef actor_ref{next_actor_id_};
    ++next_actor_id_;
    return actor_ref;
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

  void deliver_stop(ActorRef target, ExitReason reason) {
    auto entry = actors_.find(target);
    if (entry == actors_.end()) {
      return;
    }
    entry->second.control->stop(reason);
  }

  void process_due_timers() {
    while (!timers_.empty()) {
      if (timers_.begin()->first > current_time_) {
        break;
      }
      auto time = timers_.begin()->first;
      std::vector<std::function<void()>> callbacks;
      while (!timers_.empty()) {
        if (timers_.begin()->first != time) {
          break;
        }
        callbacks.push_back(std::move(timers_.begin()->second));
        timers_.erase(timers_.begin());
      }
      shuffle(callbacks);
      for (auto& callback : callbacks) {
        callback();
      }
    }
  }

  std::vector<std::coroutine_handle<>> take_ready() {
    std::vector<std::coroutine_handle<>> batch;
    batch.reserve(ready_.size());
    while (!ready_.empty()) {
      batch.push_back(ready_.front());
      ready_.pop_front();
    }
    return batch;
  }

  template <typename T>
  void shuffle(std::vector<T>& items) {
    if (items.size() > 1) {
      std::shuffle(items.begin(), items.end(), rng_);
    }
  }

  std::deque<std::coroutine_handle<>> ready_;
  std::multimap<time_point, std::function<void()>> timers_;
  std::map<ActorRef, ActorEntry> actors_;
  std::map<ActorRef, std::vector<ActorRef>> links_;
  std::map<ActorRef, std::vector<ActorRef>> monitors_;
  std::deque<StopRequest> pending_stops_;
  time_point current_time_{};
  uint64_t next_actor_id_ = 1;
  std::mt19937_64 rng_;
  bool stop_delivery_deferred_ = false;
};

}  // namespace agner
