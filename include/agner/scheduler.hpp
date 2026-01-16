#pragma once

#include <chrono>
#include <coroutine>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <thread>
#include <utility>

#include "scheduler_concept.hpp"
#include "task.hpp"

namespace agner {

class Scheduler {
 public:
  using Clock = std::chrono::steady_clock;

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
  std::shared_ptr<ActorType> spawn(Args&&... args) {
    return spawn_actor<ActorType>(*this, std::forward<Args>(args)...);
  }

 private:
  std::deque<std::coroutine_handle<>> ready_;
  std::multimap<Clock::time_point, std::function<void()>> timers_;
};

}  // namespace agner
