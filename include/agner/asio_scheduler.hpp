#pragma once

/**
 * @file asio_scheduler.hpp
 * @brief Optional Boost.Asio-backed scheduler for actors and timers.
 *
 * AsioScheduler drives actors and delayed callbacks on a boost::asio::io_context.
 * It is intended for single-threaded use: call run() from one thread at a time.
 */

#ifndef AGNER_HAS_BOOST_ASIO
#error "agner/asio_scheduler.hpp requires AGNER_ENABLE_BOOST_ASIO"
#endif

#include <chrono>
#include <concepts>
#include <coroutine>
#include <exception>
#include <future>
#include <functional>
#include <memory>
#include <optional>
#include <thread>
#include <type_traits>
#include <utility>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/system/error_code.hpp>
#include <boost/thread/future.hpp>

#include "agner/scheduler_base.hpp"

namespace agner {

namespace detail {

struct asio_scheduler_state {
  using executor_type = boost::asio::io_context::executor_type;

  executor_type executor() noexcept { return io_context.get_executor(); }

  void schedule(std::coroutine_handle<> handle) {
    if (!handle || handle.done()) {
      return;
    }

    boost::asio::post(io_context, [handle] {
      if (!handle.done()) {
        handle.resume();
      }
    });
  }

  boost::asio::io_context io_context;
};

}  // namespace detail

/// @brief Single-threaded scheduler backed by boost::asio::io_context.
class AsioScheduler : public SchedulerBase<AsioScheduler> {
 public:
  /// Clock type used for timing.
  using Clock = boost::asio::steady_timer::clock_type;
  using executor_type = detail::asio_scheduler_state::executor_type;

  AsioScheduler()
      : impl_(std::make_shared<detail::asio_scheduler_state>()) {}

  AsioScheduler(const AsioScheduler&) = delete;
  AsioScheduler& operator=(const AsioScheduler&) = delete;

  /// @brief Schedule a coroutine handle for execution on the io_context.
  void schedule(std::coroutine_handle<> handle) {
    impl_->schedule(handle);
  }

  /// @brief Schedule a callback to run after a delay.
  /// @param delay Duration to wait before invoking the callback.
  /// @param fn Callback to invoke.
  void schedule_after(DurationLike auto delay, std::invocable auto&& fn) {
    auto impl = impl_;
    auto timer =
        std::make_shared<boost::asio::steady_timer>(impl->io_context, delay);
    auto callback = std::make_shared<std::function<void()>>(
        std::forward<decltype(fn)>(fn));

    timer->async_wait(
        [impl, timer, callback](const boost::system::error_code& error) mutable {
          (void)impl;
          if (!error) {
            (*callback)();
          }
        });
  }

  /// @brief Run the io_context until all queued work is complete.
  void run() {
    impl_->io_context.restart();
    impl_->io_context.run();
  }

  /// @brief Stop the underlying io_context.
  void stop() { impl_->io_context.stop(); }

  /// @brief Get the underlying io_context.
  boost::asio::io_context& io_context() noexcept { return impl_->io_context; }

  /// @brief Get the underlying io_context.
  const boost::asio::io_context& io_context() const noexcept {
    return impl_->io_context;
  }

  /// @brief Get the executor used by the scheduler.
  executor_type executor() noexcept { return impl_->executor(); }

  /// @brief Await a Boost.Thread future and resume through this scheduler.
  template <typename T>
  auto await(boost::future<T>&& future);

  /// @brief Await a Boost.Thread shared_future and resume through this scheduler.
  template <typename T>
  auto await(boost::shared_future<T> future);

  /// @brief Await a std::future and resume through this scheduler.
  template <typename T>
  auto await(std::future<T>&& future);

  /// @brief Await a Boost.Asio awaitable and resume through this scheduler.
  template <typename T, typename Executor>
  auto await(boost::asio::awaitable<T, Executor>&& awaitable);

  /// @brief Request an actor to stop.
  /// @param target The actor to stop.
  /// @param reason Exit reason to set.
  void stop(ActorRef target, ExitReason reason = {}) {
    this->actors_.at(target).control->stop(reason);
  }

 private:
  template <typename T>
  friend auto asio_await(AsioScheduler& scheduler, boost::future<T>&& future);
  template <typename T>
  friend auto asio_await(AsioScheduler& scheduler,
                         boost::shared_future<T> future);
  template <typename T>
  friend auto asio_await(AsioScheduler& scheduler, std::future<T>&& future);
  template <typename T, typename Executor>
  friend auto asio_await(AsioScheduler& scheduler,
                         boost::asio::awaitable<T, Executor>&& awaitable);

  std::shared_ptr<detail::asio_scheduler_state> state() const noexcept {
    return impl_;
  }

  std::shared_ptr<detail::asio_scheduler_state> impl_;
};

namespace detail {

template <typename T>
class asio_result_storage {
 public:
  template <typename U>
  void set(U&& value) {
    value_.emplace(std::forward<U>(value));
  }

  T take() { return std::move(*value_); }

 private:
  std::optional<T> value_;
};

template <typename T>
class asio_result_storage<T&> {
 public:
  void set(T& value) noexcept { value_ = std::addressof(value); }

  T& take() const noexcept { return *value_; }

 private:
  T* value_ = nullptr;
};

template <>
class asio_result_storage<void> {
 public:
  void set() noexcept {}

  void take() const noexcept {}
};

template <typename T>
struct asio_await_state {
  using guard_type =
      boost::asio::executor_work_guard<asio_scheduler_state::executor_type>;

  explicit asio_await_state(std::shared_ptr<asio_scheduler_state> scheduler)
      : scheduler(std::move(scheduler)), guard(this->scheduler->executor()) {}

  void resume() {
    scheduler->schedule(handle);
    guard.reset();
  }

  std::shared_ptr<asio_scheduler_state> scheduler;
  guard_type guard;
  std::coroutine_handle<> handle{};
  asio_result_storage<T> result;
  std::exception_ptr exception;
};

template <typename T, typename Future>
void set_result_from_future(asio_await_state<T>& state, Future& future) {
  if constexpr (std::is_void_v<T>) {
    future.get();
    state.result.set();
  } else {
    state.result.set(future.get());
  }
}

template <typename T>
T resume_result(asio_await_state<T>& state) {
  if (state.exception) {
    std::rethrow_exception(state.exception);
  }
  if constexpr (std::is_void_v<T>) {
    state.result.take();
  } else {
    return state.result.take();
  }
}

template <typename Future>
class boost_future_awaiter {
 public:
  using value_type = typename Future::value_type;

  boost_future_awaiter(std::shared_ptr<asio_scheduler_state> scheduler,
                       Future&& future)
      : scheduler_(std::move(scheduler)), future_(std::move(future)) {}

  bool await_ready() const noexcept { return false; }

  void await_suspend(std::coroutine_handle<> handle) {
    state_ = std::make_shared<asio_await_state<value_type>>(scheduler_);
    state_->handle = handle;
    auto state = state_;
    continuation_ = std::move(future_).then([state](Future ready) mutable {
      try {
        set_result_from_future(*state, ready);
      } catch (...) {
        state->exception = std::current_exception();
      }
      state->resume();
    });
  }

  value_type await_resume() { return resume_result(*state_); }

 private:
  std::shared_ptr<asio_scheduler_state> scheduler_;
  Future future_;
  std::shared_ptr<asio_await_state<value_type>> state_;
  boost::future<void> continuation_;
};

template <typename T>
class std_future_awaiter {
 public:
  std_future_awaiter(std::shared_ptr<asio_scheduler_state> scheduler,
                     std::future<T>&& future)
      : scheduler_(std::move(scheduler)), future_(std::move(future)) {}

  bool await_ready() const noexcept { return false; }

  void await_suspend(std::coroutine_handle<> handle) {
    state_ = std::make_shared<asio_await_state<T>>(scheduler_);
    state_->handle = handle;
    auto state = state_;
    // std::future has no continuation API, so a waiter thread owns the future
    // and posts the Agner coroutine back to the scheduler without blocking Asio.
    std::thread([state, future = std::move(future_)]() mutable {
      try {
        set_result_from_future(*state, future);
      } catch (...) {
        state->exception = std::current_exception();
      }
      state->resume();
    }).detach();
  }

  T await_resume() { return resume_result(*state_); }

 private:
  std::shared_ptr<asio_scheduler_state> scheduler_;
  std::future<T> future_;
  std::shared_ptr<asio_await_state<T>> state_;
};

template <typename T, typename Executor>
class asio_awaitable_awaiter {
 public:
  asio_awaitable_awaiter(std::shared_ptr<asio_scheduler_state> scheduler,
                         boost::asio::awaitable<T, Executor>&& awaitable)
      : scheduler_(std::move(scheduler)), awaitable_(std::move(awaitable)) {}

  bool await_ready() const noexcept { return false; }

  void await_suspend(std::coroutine_handle<> handle) {
    state_ = std::make_shared<asio_await_state<T>>(scheduler_);
    state_->handle = handle;
    auto state = state_;
    if constexpr (std::is_void_v<T>) {
      boost::asio::co_spawn(
          scheduler_->io_context, std::move(awaitable_),
          [state](std::exception_ptr exception) mutable {
            state->exception = exception;
            if (!exception) {
              state->result.set();
            }
            state->resume();
          });
    } else {
      boost::asio::co_spawn(
          scheduler_->io_context, std::move(awaitable_),
          [state](std::exception_ptr exception, T value) mutable {
            state->exception = exception;
            if (!exception) {
              state->result.set(std::move(value));
            }
            state->resume();
          });
    }
  }

  T await_resume() { return resume_result(*state_); }

 private:
  std::shared_ptr<asio_scheduler_state> scheduler_;
  boost::asio::awaitable<T, Executor> awaitable_;
  std::shared_ptr<asio_await_state<T>> state_;
};

}  // namespace detail

/// @brief Await a Boost.Thread future and resume through the AsioScheduler.
template <typename T>
auto asio_await(AsioScheduler& scheduler, boost::future<T>&& future) {
  return detail::boost_future_awaiter<boost::future<T>>(scheduler.state(),
                                                        std::move(future));
}

/// @brief Await a Boost.Thread shared_future and resume through AsioScheduler.
template <typename T>
auto asio_await(AsioScheduler& scheduler, boost::shared_future<T> future) {
  return detail::boost_future_awaiter<boost::shared_future<T>>(
      scheduler.state(), std::move(future));
}

/// @brief Await a std::future, using a waiter thread that posts completion.
template <typename T>
auto asio_await(AsioScheduler& scheduler, std::future<T>&& future) {
  return detail::std_future_awaiter<T>(scheduler.state(), std::move(future));
}

/// @brief Await a Boost.Asio awaitable and resume through AsioScheduler.
template <typename T, typename Executor>
auto asio_await(AsioScheduler& scheduler,
                boost::asio::awaitable<T, Executor>&& awaitable) {
  return detail::asio_awaitable_awaiter<T, Executor>(scheduler.state(),
                                                     std::move(awaitable));
}

template <typename T>
auto AsioScheduler::await(boost::future<T>&& future) {
  return asio_await(*this, std::move(future));
}

template <typename T>
auto AsioScheduler::await(boost::shared_future<T> future) {
  return asio_await(*this, std::move(future));
}

template <typename T>
auto AsioScheduler::await(std::future<T>&& future) {
  return asio_await(*this, std::move(future));
}

template <typename T, typename Executor>
auto AsioScheduler::await(boost::asio::awaitable<T, Executor>&& awaitable) {
  return asio_await(*this, std::move(awaitable));
}

}  // namespace agner
