#pragma once

/**
 * @file mt_scheduler.hpp
 * @brief Multi-threaded work-stealing scheduler for actors.
 *
 * Uses std::jthread worker threads with Chase-Lev work-stealing deques.
 * The main thread manages timers and startup/shutdown; all actor coroutines
 * run on worker threads.
 */

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <coroutine>
#include <cstddef>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#include "agner/detail/chase_lev_deque.hpp"
#include "agner/detail/scheduler_traits.hpp"
#include "agner/scheduler_base.hpp"

namespace agner {

class MtScheduler;

}  // namespace agner

namespace agner::detail {

template <>
struct is_concurrent_scheduler<MtScheduler> : std::true_type {};

}  // namespace agner::detail

namespace agner {

/// @brief Multi-threaded scheduler with work-stealing.
///
/// Worker threads each own a Chase-Lev deque and steal from other workers
/// when idle. The calling thread (main thread) runs timers and coordinates
/// startup/shutdown. All actor coroutines execute on worker threads.
// GCOVR_EXCL_BR_START
class MtScheduler : public SchedulerBase<MtScheduler> {
 public:
  using Clock = std::chrono::steady_clock;

  /// @brief Construct a scheduler with the given number of worker threads.
  /// @param thread_count Number of worker threads to launch.
  explicit MtScheduler(size_t thread_count) : thread_count_(thread_count) {}

  MtScheduler(const MtScheduler&) = delete;
  MtScheduler& operator=(const MtScheduler&) = delete;

  /// @brief Schedule a coroutine handle for execution.
  ///
  /// If called from a worker thread, pushes to that worker's local deque.
  /// Otherwise pushes to a shared injection queue.
  void schedule(std::coroutine_handle<> handle) {
    if (!handle) {
      return;
    }

    auto idx = current_worker_index();
    if (idx.has_value()) {
      workers_[*idx].local_queue.push(handle);
    } else {
      std::lock_guard<std::mutex> lock(inject_mutex_);
      inject_queue_.push_back(handle);
    }
    work_cv_.notify_one();
  }

  /// @brief Schedule a callback to run after a delay.
  /// @param delay Duration to wait before invoking the callback.
  /// @param fn Callback to invoke.
  void schedule_after(DurationLike auto delay, std::invocable auto&& fn) {
    {
      std::lock_guard<std::mutex> lock(timer_mutex_);
      timers_.emplace(Clock::now() + delay,
                      std::function<void()>(std::forward<decltype(fn)>(fn)));
    }
    timer_cv_.notify_one();
  }

  /// @brief Run until all actors have exited.
  ///
  /// Launches worker threads, runs the timer loop on the calling thread,
  /// then joins all workers before returning.
  void run() {
    shutdown_.store(false, std::memory_order_relaxed);

    if (active_actor_count() == 0) {
      return;
    }

    workers_.reserve(thread_count_);
    for (size_t i = 0; i < thread_count_; ++i) {
      workers_.emplace_back();
    }

    for (size_t i = 0; i < thread_count_; ++i) {
      workers_[i].thread = std::jthread([this, i](std::stop_token token) {
        worker_index_ = i;
        worker_loop(token, i);
      });
    }

    timer_loop();

    for (auto& w : workers_) {
      w.thread.request_stop();
    }
    work_cv_.notify_all();
    for (auto& w : workers_) {
      if (w.thread.joinable()) {
        w.thread.join();
      }
    }

    workers_.clear();
  }

  /// @brief Request an actor to stop.
  /// @param target The actor to stop.
  /// @param reason Exit reason to set.
  void stop(ActorRef target, ExitReason reason = {}) {
    std::shared_ptr<ActorControl> ctrl;
    {
      std::lock_guard<base_mutex_type> lock(base_mutex_);
      ctrl = this->actors_.at(target).control;
    }
    ctrl->stop(reason);
  }

  /// @brief Called when an actor exits. Overrides SchedulerBase default.
  void on_actor_exit(ActorRef actor_ref, const ExitReason& reason) {
    this->notify_exit(actor_ref, reason);
    if (this->active_actor_count() == 0) {
      shutdown_.store(true, std::memory_order_release);
      timer_cv_.notify_one();
      work_cv_.notify_all();
    }
  }

 private:
  struct Worker {
    std::jthread thread;
    detail::ChaseLevDeque<std::coroutine_handle<>> local_queue;
  };

  /// @brief Get the index of the current worker thread, if on one.
  std::optional<size_t> current_worker_index() const {
    if (worker_index_ < thread_count_) {
      return worker_index_;
    }
    return std::nullopt;
  }

  /// @brief Try to take work from the injection queue.
  std::optional<std::coroutine_handle<>> try_inject(size_t worker_idx) {
    std::lock_guard<std::mutex> lock(inject_mutex_);
    if (inject_queue_.empty()) {
      return std::nullopt;
    }
    auto handle = inject_queue_.front();
    inject_queue_.pop_front();
    return handle;
  }

  /// @brief Try to steal work from another worker.
  std::optional<std::coroutine_handle<>> try_steal(size_t self_idx) {
    for (size_t i = 1; i <= workers_.size(); ++i) {
      size_t victim = (self_idx + i) % workers_.size();
      if (victim == self_idx) {  // GCOVR_EXCL_BR_LINE
        continue;
      }
      if (auto h = workers_[victim].local_queue.steal()) {
        return h;
      }
    }
    return std::nullopt;
  }

  /// @brief Worker thread main loop.
  // GCOVR_EXCL_BR_START
  void worker_loop(std::stop_token token, size_t idx) {
    while (!token.stop_requested()) {
      // 1. Pop from own deque.
      if (auto h = workers_[idx].local_queue.pop()) {
        if (!h->done()) {
          h->resume();
        }
        continue;
      }

      // 2. Check injection queue.
      if (auto h = try_inject(idx)) {
        if (!h->done()) {
          h->resume();
        }
        continue;
      }

      // 3. Steal from another worker.
      if (auto h = try_steal(idx)) {
        if (!h->done()) {
          h->resume();
        }
        continue;
      }

      // 4. Nothing found — wait.
      {
        std::unique_lock<std::mutex> lock(work_mutex_);
        work_cv_.wait_for(lock, std::chrono::milliseconds(1), [&] {
          return token.stop_requested() ||
                 !workers_[idx].local_queue.empty();
        });
      }
    }
  }
  // GCOVR_EXCL_BR_STOP

  /// @brief Timer loop runs on the main/calling thread.
  // GCOVR_EXCL_BR_START
  void timer_loop() {
    while (!shutdown_.load(std::memory_order_acquire)) {
      std::function<void()> callback;
      {
        std::unique_lock<std::mutex> lock(timer_mutex_);
        timer_cv_.wait(lock, [this] {
          return shutdown_.load(std::memory_order_acquire) ||
                 !timers_.empty();
        });
        if (shutdown_.load(std::memory_order_acquire) && timers_.empty()) {
          break;
        }

        auto deadline = timers_.begin()->first;
        if (deadline > Clock::now()) {
          timer_cv_.wait_until(lock, deadline, [this, deadline] {
            return shutdown_.load(std::memory_order_acquire) ||
                   timers_.begin()->first < deadline;
          });
        }

        if (!shutdown_.load(std::memory_order_acquire) &&
            timers_.begin()->first <= Clock::now()) {
          callback = std::move(timers_.begin()->second);
          timers_.erase(timers_.begin());
        }
      }
      if (callback) {
        callback();
      }
    }
  }
  // GCOVR_EXCL_BR_STOP

  size_t thread_count_;
  std::vector<Worker> workers_;

  std::mutex inject_mutex_;
  std::deque<std::coroutine_handle<>> inject_queue_;

  std::mutex timer_mutex_;
  std::multimap<Clock::time_point, std::function<void()>> timers_;
  std::condition_variable timer_cv_;

  std::mutex work_mutex_;
  std::condition_variable work_cv_;
  std::atomic<bool> shutdown_{false};

  static thread_local size_t worker_index_;
};

inline thread_local size_t MtScheduler::worker_index_ =
    std::numeric_limits<size_t>::max();

// GCOVR_EXCL_BR_STOP

}  // namespace agner
