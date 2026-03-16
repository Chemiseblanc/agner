#pragma once

#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace agner::detail {

/// @brief Lock-free Chase-Lev work-stealing deque.
///
/// The owning thread calls push() and pop() (operating on the bottom).
/// Other threads call steal() (operating on the top).
///
/// Based on "Dynamic Circular Work-Stealing Deque" (Chase & Lev, 2005)
/// with the C++11 memory-model refinements from
/// "Correct and Efficient Work-Stealing for Weak Memory Models" (Le et al.).
///
/// @tparam T Element type (e.g. std::coroutine_handle<>).
template <typename T>
class ChaseLevDeque {
 public:
  explicit ChaseLevDeque(size_t initial_capacity = 256)
      : buffer_(new CircularBuffer(initial_capacity)) {
    top_.store(0, std::memory_order_relaxed);
    bottom_.store(0, std::memory_order_relaxed);
  }

  ChaseLevDeque(const ChaseLevDeque&) = delete;
  ChaseLevDeque& operator=(const ChaseLevDeque&) = delete;

  ChaseLevDeque(ChaseLevDeque&& other) noexcept
      : buffer_(other.buffer_.load(std::memory_order_relaxed)),
        top_(other.top_.load(std::memory_order_relaxed)),
        bottom_(other.bottom_.load(std::memory_order_relaxed)) {
    other.buffer_.store(nullptr, std::memory_order_relaxed);
    other.top_.store(0, std::memory_order_relaxed);
    other.bottom_.store(0, std::memory_order_relaxed);
  }

  ChaseLevDeque& operator=(ChaseLevDeque&& other) noexcept {
    if (this != &other) {  // GCOVR_EXCL_BR_LINE
      delete buffer_.load(std::memory_order_relaxed);
      buffer_.store(other.buffer_.load(std::memory_order_relaxed),
                    std::memory_order_relaxed);
      top_.store(other.top_.load(std::memory_order_relaxed),
                 std::memory_order_relaxed);
      bottom_.store(other.bottom_.load(std::memory_order_relaxed),
                    std::memory_order_relaxed);
      other.buffer_.store(nullptr, std::memory_order_relaxed);
      other.top_.store(0, std::memory_order_relaxed);
      other.bottom_.store(0, std::memory_order_relaxed);
    }
    return *this;
  }

  ~ChaseLevDeque() {
    delete buffer_.load(std::memory_order_relaxed);
    for (auto* old : retired_buffers_) {
      delete old;
    }
  }

  /// @brief Push an element onto the bottom (owner only).
  void push(T value) {
    auto b = bottom_.load(std::memory_order_relaxed);
    auto t = top_.load(std::memory_order_acquire);
    auto* buf = buffer_.load(std::memory_order_relaxed);

    if (static_cast<int64_t>(b - t) >= static_cast<int64_t>(buf->capacity())) {
      buf = grow(buf, t, b);
    }

    buf->store(b, std::move(value));
    std::atomic_thread_fence(std::memory_order_release);
    bottom_.store(b + 1, std::memory_order_relaxed);
  }

  /// @brief Pop an element from the bottom (owner only).
  /// @return The element, or std::nullopt if the deque is empty.
  std::optional<T> pop() {
    auto b = bottom_.load(std::memory_order_relaxed) - 1;
    auto* buf = buffer_.load(std::memory_order_relaxed);
    bottom_.store(b, std::memory_order_relaxed);

    std::atomic_thread_fence(std::memory_order_seq_cst);

    auto t = top_.load(std::memory_order_relaxed);

    if (static_cast<int64_t>(t) <= static_cast<int64_t>(b)) {
      auto value = buf->load(b);
        if (t == static_cast<int64_t>(b)) {
          if (!top_.compare_exchange_strong(t, t + 1,
                                            std::memory_order_seq_cst,
                                            std::memory_order_relaxed)) {  // GCOVR_EXCL_BR_LINE
            bottom_.store(b + 1, std::memory_order_relaxed);  // GCOVR_EXCL_LINE
            return std::nullopt;  // GCOVR_EXCL_LINE
        }
        bottom_.store(b + 1, std::memory_order_relaxed);
      }
      return value;
    }

    bottom_.store(b + 1, std::memory_order_relaxed);
    return std::nullopt;
  }

  /// @brief Steal an element from the top (called by other threads).
  /// @return The element, or std::nullopt if the deque is empty or contended.
  std::optional<T> steal() {
    auto t = top_.load(std::memory_order_acquire);

    std::atomic_thread_fence(std::memory_order_seq_cst);

    auto b = bottom_.load(std::memory_order_acquire);

      if (static_cast<int64_t>(t) < static_cast<int64_t>(b)) {
      auto* buf = buffer_.load(std::memory_order_consume);
      auto value = buf->load(t);
      if (!top_.compare_exchange_strong(t, t + 1,
                                        std::memory_order_seq_cst,
                                          std::memory_order_relaxed)) {  // GCOVR_EXCL_BR_LINE
          return std::nullopt;  // GCOVR_EXCL_LINE
      }
      return value;
    }

    return std::nullopt;
  }

  /// @brief Check if the deque appears empty (approximate, no synchronization).
  bool empty() const noexcept {
    auto b = bottom_.load(std::memory_order_relaxed);
    auto t = top_.load(std::memory_order_relaxed);
    return static_cast<int64_t>(b) <= static_cast<int64_t>(t);
  }

 private:
  struct CircularBuffer {
    explicit CircularBuffer(size_t cap)
        : mask_(cap - 1), data_(cap) {
      assert((cap & (cap - 1)) == 0 && "capacity must be power of 2");
    }

    size_t capacity() const noexcept { return mask_ + 1; }

    void store(int64_t index, T value) {
      data_[static_cast<size_t>(index) & mask_].store(
          std::move(value), std::memory_order_relaxed);
    }

    T load(int64_t index) {
      return data_[static_cast<size_t>(index) & mask_].load(
          std::memory_order_relaxed);
    }

    CircularBuffer* grow(int64_t top, int64_t bottom) {
      auto new_cap = capacity() * 2;
      auto* grown = new CircularBuffer(new_cap);
      for (auto i = top; i < bottom; ++i) {
        grown->store(i, load(i));
      }
      return grown; }

   private:
    size_t mask_;
    std::vector<std::atomic<T>> data_;
  };

  CircularBuffer* grow(CircularBuffer* old_buf, int64_t top, int64_t bottom) {
    auto* new_buf = old_buf->grow(top, bottom);
    retired_buffers_.push_back(old_buf);
    buffer_.store(new_buf, std::memory_order_release);
    return new_buf;
  }

  std::atomic<CircularBuffer*> buffer_;
  std::atomic<int64_t> top_;
  std::atomic<int64_t> bottom_;
  std::vector<CircularBuffer*> retired_buffers_;
};

}  // namespace agner::detail
