#pragma once

#include "agner/actor_control.hpp"

namespace agner {

namespace detail {

struct SchedulerProbeMessage {};

template <typename SchedulerType>
class SchedulerProbeActor : public ActorControl {
 public:
  explicit SchedulerProbeActor(SchedulerType&) {}

  void stop(ExitReason) override {}
  ExitReason exit_reason() const noexcept override { return {}; }
};

}  // namespace detail

}  // namespace agner
