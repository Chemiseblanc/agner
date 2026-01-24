#pragma once

#include <stdexcept>

namespace agner {

/// @brief Exception thrown when a supervisor encounters an internal invariant violation.
class SupervisorInvariantError : public std::logic_error {
 public:
  /// @brief Construct with an error message.
  explicit SupervisorInvariantError(const char* message)
      : std::logic_error(message) {}
};

}  // namespace agner
