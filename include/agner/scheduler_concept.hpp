#pragma once

#include <chrono>
#include <coroutine>
#include <utility>

#include "agner/actor_concepts.hpp"
#include "agner/actor_control.hpp"
#include "agner/detail/scheduler_concept_detail.hpp"

namespace agner {

/// @brief Concept defining the scheduler interface for actor execution.
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
