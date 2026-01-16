#pragma once

#include <chrono>
#include <coroutine>
#include <memory>
#include <utility>

namespace agner {

template <typename SchedulerType>
concept SchedulerLike =
    requires(SchedulerType scheduler, std::coroutine_handle<> handle,
             typename SchedulerType::Clock::duration delay) {
      scheduler.schedule(handle);
      scheduler.schedule_after(delay, [] {});
      scheduler.run();
    };

template <typename ActorType, typename SchedulerType, typename... Args>
std::shared_ptr<ActorType> spawn_actor(SchedulerType& scheduler,
                                       Args&&... args) {
  auto actor =
      std::make_shared<ActorType>(scheduler, std::forward<Args>(args)...);
  actor->start().detach(scheduler);
  return actor;
}

}  // namespace agner
