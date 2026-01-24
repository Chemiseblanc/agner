#pragma once

#include <coroutine>

namespace agner {

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

}  // namespace agner
