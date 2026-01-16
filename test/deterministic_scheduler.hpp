#pragma once

#include <algorithm>
#include <chrono>
#include <coroutine>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <random>
#include <utility>
#include <vector>

#include "agner/scheduler_concept.hpp"

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
  std::shared_ptr<ActorType> spawn(Args&&... args) {
    return spawn_actor<ActorType>(*this, std::forward<Args>(args)...);
  }

 private:
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
  time_point current_time_{};
  std::mt19937_64 rng_;
};

}  // namespace agner
