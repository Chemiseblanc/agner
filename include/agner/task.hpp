#pragma once

#include <coroutine>
#include <exception>
#include <optional>
#include <type_traits>
#include <utility>

#include "scheduler_concept.hpp"

namespace agner {

class Scheduler;

namespace detail {

template <typename Promise>
struct final_awaitable {
  bool await_ready() const noexcept { return false; }

  void await_suspend(std::coroutine_handle<Promise> handle) const noexcept {
    auto& promise = handle.promise();
    if (promise.continuation) {
      promise.continuation.resume();
    } else if (promise.detached) {
      handle.destroy();
    }
    await_resume();
  }

  void await_resume() const noexcept {}
};

}  // namespace detail

template <typename T>
class task {
 public:
  struct promise_type;
  using handle_type = std::coroutine_handle<promise_type>;

  explicit task(handle_type handle) : handle_(handle) {}
  task(task&& other) noexcept : handle_(std::exchange(other.handle_, {})) {}

  task& operator=(task&& other) noexcept {
    if (this != &other) {
      if (handle_) {
        handle_.destroy();
      }
      handle_ = std::exchange(other.handle_, {});
    }
    return *this;
  }

  ~task() {
    if (handle_) {
      handle_.destroy();
    }
  }

  template <SchedulerLike SchedulerType>
  void detach(SchedulerType& scheduler);

  bool await_ready() const noexcept { return false; }

  std::coroutine_handle<> await_suspend(std::coroutine_handle<> handle) {
    handle_.promise().continuation = handle;
    return handle_;
  }

  T await_resume() {
    auto& promise = handle_.promise();
    if (promise.exception) {
      std::rethrow_exception(promise.exception);
    }
    return std::move(*promise.result);
  }

 private:
  handle_type handle_{};
};

template <typename T>
struct task<T>::promise_type {
  std::coroutine_handle<> continuation{};
  bool detached = false;
  std::optional<T> result;
  std::exception_ptr exception;

  task get_return_object() { return task{handle_type::from_promise(*this)}; }

  std::suspend_always initial_suspend() noexcept { return {}; }

  auto final_suspend() noexcept {
    return detail::final_awaitable<promise_type>{};
  }

  void unhandled_exception() { exception = std::current_exception(); }

  void return_value(T value) { result = std::move(value); }
};

template <>
class task<void> {
 public:
  struct promise_type;
  using handle_type = std::coroutine_handle<promise_type>;

  explicit task(handle_type handle);
  task(task&& other) noexcept;
  task& operator=(task&& other) noexcept;
  ~task();

  template <SchedulerLike SchedulerType>
  void detach(SchedulerType& scheduler);

  bool await_ready() const noexcept;
  std::coroutine_handle<> await_suspend(std::coroutine_handle<> handle);
  void await_resume();

 private:
  handle_type handle_{};
};

struct task<void>::promise_type {
  std::coroutine_handle<> continuation{};
  bool detached = false;
  std::exception_ptr exception;

  task get_return_object() { return task{handle_type::from_promise(*this)}; }

  std::suspend_always initial_suspend() noexcept { return {}; }

  auto final_suspend() noexcept {
    return detail::final_awaitable<promise_type>{};
  }

  void unhandled_exception() { exception = std::current_exception(); }

  void return_void() noexcept {}
};

inline task<void>::task(handle_type handle) : handle_(handle) {}

inline task<void>::task(task&& other) noexcept
    : handle_(std::exchange(other.handle_, {})) {}

inline task<void>& task<void>::operator=(task&& other) noexcept {
  if (this != &other) {
    if (handle_) {
      handle_.destroy();
    }
    handle_ = std::exchange(other.handle_, {});
  }
  return *this;
}

inline task<void>::~task() {
  if (handle_) {
    handle_.destroy();
  }
}

inline bool task<void>::await_ready() const noexcept { return false; }

inline std::coroutine_handle<> task<void>::await_suspend(
    std::coroutine_handle<> handle) {
  handle_.promise().continuation = handle;
  return handle_;
}

inline void task<void>::await_resume() {
  auto& promise = handle_.promise();
  if (promise.exception) {
    std::rethrow_exception(promise.exception);
  }
}

template <typename T>
template <SchedulerLike SchedulerType>
void task<T>::detach(SchedulerType& scheduler) {
  if (!handle_) {
    return;
  }
  handle_.promise().detached = true;
  scheduler.schedule(handle_);
  handle_ = {};
}

template <SchedulerLike SchedulerType>
inline void task<void>::detach(SchedulerType& scheduler) {
  if (!handle_) {
    return;
  }
  handle_.promise().detached = true;
  scheduler.schedule(handle_);
  handle_ = {};
}

}  // namespace agner
