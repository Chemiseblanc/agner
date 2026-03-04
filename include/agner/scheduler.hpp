#pragma once

/**
 * @file scheduler.hpp
 * @brief Event loop scheduler for running actors and timers.
 *
 * The Scheduler executes coroutines, manages actor lifecycles, and handles
 * delayed callbacks using a single-threaded cooperative event loop.
 */

#include <chrono>
#include <coroutine>
#include <deque>
#include <functional>
#include <map>
#include <thread>

#include "agner/scheduler_base.hpp"

namespace agner {

/// @brief Default single-threaded scheduler implementation.
class Scheduler : public SchedulerBase<Scheduler> {
 public:
  /// Clock type used for timing.
  using Clock = std::chrono::steady_clock;

  /// @brief Schedule a coroutine handle for execution.
  void schedule(std::coroutine_handle<> handle) {
    if (handle) {
      ready_.push_back(handle);
    }
  }

  /// @brief Schedule a callback to run after a delay.
  /// @param delay Duration to wait before invoking the callback.
  /// @param fn Callback to invoke.
  void schedule_after(DurationLike auto delay, std::invocable auto&& fn) {
    timers_.emplace(Clock::now() + delay,
                    std::function<void()>(std::forward<decltype(fn)>(fn)));
  }

  /// @brief Run the event loop until all work is complete.
  void run() {
    for (;;) {
      if (ready_.empty() && timers_.empty()) {
        break;
      }
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

  /// @brief Request an actor to stop.
  /// @param target The actor to stop.
  /// @param reason Exit reason to set.
  void stop(ActorRef target, ExitReason reason = {}) {
    this->actors_.at(target).control->stop(reason);
  }

 private:
  std::deque<std::coroutine_handle<>> ready_;
  std::multimap<Clock::time_point, std::function<void()>> timers_;
};

}  // namespace agner
