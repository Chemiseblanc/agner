#pragma once

#include <stdexcept>

namespace agner {

class SupervisorInvariantError : public std::logic_error {
 public:
  explicit SupervisorInvariantError(const char* message)
      : std::logic_error(message) {}
};

}  // namespace agner
