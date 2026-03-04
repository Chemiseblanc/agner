#pragma once

#include <stdexcept>

namespace agner {

/// @brief Exception thrown when a GenServer call times out waiting for a reply.
class CallTimeout : public std::runtime_error {
 public:
  CallTimeout() : std::runtime_error("GenServer call timed out") {}
};

}  // namespace agner
