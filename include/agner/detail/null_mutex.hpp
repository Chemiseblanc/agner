#pragma once

namespace agner::detail {

/// @brief No-op mutex for single-threaded schedulers.
///
/// Satisfies BasicLockable so it can be used with std::lock_guard.
/// All operations compile to nothing.
struct null_mutex {
  void lock() noexcept {}
  void unlock() noexcept {}
  bool try_lock() noexcept { return true; }
};

}  // namespace agner::detail
