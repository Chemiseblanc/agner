#pragma once

#include <algorithm>
#include <cassert>
#include <chrono>
#include <coroutine>
#include <deque>
#include <functional>
#include <map>
#include <random>
#include <vector>

#include "agner/scheduler_base.hpp"

namespace agner {

class DeterministicScheduler : public SchedulerBase<DeterministicScheduler> {
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

  void stop(ActorRef target, ExitReason reason = {}) {
    this->actors_.at(target);  // Validate actor exists
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
    this->actors_.at(left);
    this->actors_.at(right);
    this->links_[left].push_back(right);
    this->links_[right].push_back(left);
  }

  void monitor(ActorRef monitor_ref, ActorRef target_ref) {
    this->actors_.at(monitor_ref);
    this->actors_.at(target_ref);
    this->monitors_[target_ref].push_back(monitor_ref);
  }

 private:
  struct StopRequest {
    ActorRef target;
    ExitReason reason;
  };

  void deliver_stop(ActorRef target, ExitReason reason) {
    this->actors_.at(target).control->stop(reason);
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
  std::deque<StopRequest> pending_stops_;
  time_point current_time_{};
  std::mt19937_64 rng_;
  bool stop_delivery_deferred_ = false;
};

}  // namespace agner
