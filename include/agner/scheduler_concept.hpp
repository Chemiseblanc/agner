#pragma once

#include <chrono>
#include <coroutine>
#include <utility>

#include "actor_control.hpp"

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

template <typename SchedulerType>
concept SchedulerLike = requires(
    SchedulerType scheduler, std::coroutine_handle<> handle,
    typename SchedulerType::Clock::duration delay, ActorRef actor_ref) {
  scheduler.schedule(handle);
  scheduler.schedule_after(delay, [] {});
  scheduler.run();
  {
    scheduler.template spawn<detail::SchedulerProbeActor<SchedulerType>>()
  } -> std::same_as<ActorRef>;
  {
    scheduler.template spawn_link<detail::SchedulerProbeActor<SchedulerType>>(
        actor_ref)
  } -> std::same_as<ActorRef>;
  {
    scheduler
        .template spawn_monitor<detail::SchedulerProbeActor<SchedulerType>>(
            actor_ref)
  } -> std::same_as<ActorRef>;
  scheduler.send(actor_ref, detail::SchedulerProbeMessage{});
};

}  // namespace agner
