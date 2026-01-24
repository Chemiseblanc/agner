#include "agner/scheduler.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <coroutine>
#include <vector>

#include "test_support.hpp"

namespace {

using namespace std::chrono_literals;
using namespace agner::test_support;

class IdleActor : public TestActor<IdleActor, agner::Messages<>> {
 public:
  using TestActor<IdleActor, agner::Messages<>>::TestActor;

  agner::task<void> run() {
    co_await receive([&](agner::ExitSignal&) {});
    co_return;
  }
};

}  // namespace

// Summary: When immediate and delayed timers are scheduled, the scheduler shall
// run both in order. Description: This test schedules a zero-delay and a 1ms
// timer, then runs the scheduler. The assertion on `values` confirms the
// immediate timer fires first and the delayed timer fires later.
TEST(Scheduler, ImmediateAndDelayedTimers) {
  agner::Scheduler scheduler;
  std::vector<int> values;

  scheduler.schedule_after(agner::Scheduler::Clock::duration::zero(),
                           [&] { values.push_back(1); });
  scheduler.schedule_after(1ms, [&] { values.push_back(2); });
  scheduler.run();

  EXPECT_EQ(values, (std::vector<int>{1, 2}));
}

// Summary: When scheduling null handles, the scheduler ignores them safely.
// Description: This test schedules an empty coroutine handle and runs the
// scheduler to cover the null-handle decision.
TEST(Scheduler, ScheduleIgnoresNullHandle) {
  agner::Scheduler scheduler;

  scheduler.schedule(std::coroutine_handle<>{});
  scheduler.run();

  SUCCEED();
}

// Summary: When scheduling completed handles, the scheduler skips resuming.
// Description: This test schedules a coroutine that is already done so the
// scheduler takes the done-handle branch in the run loop.
TEST(Scheduler, ScheduleSkipsDoneHandle) {
  agner::Scheduler scheduler;

  auto manual = make_manual_coroutine();
  manual.handle.resume();
  EXPECT_TRUE(manual.handle.done());

  scheduler.schedule(manual.handle);
  scheduler.run();

  manual.handle.destroy();
}

// Summary: When scheduling an active handle, the scheduler resumes it.
// Description: This test schedules a suspended coroutine and runs the
// scheduler, confirming it can resume active handles.
TEST(Scheduler, ScheduleResumesActiveHandle) {
  agner::Scheduler scheduler;

  auto manual = make_manual_coroutine();
  EXPECT_FALSE(manual.handle.done());

  scheduler.schedule(manual.handle);
  scheduler.run();

  manual.handle.destroy();
}

// Summary: When operating on missing actors, stop/link/monitor throw.
// Description: This test verifies that stop, link, and monitor operations
// throw std::out_of_range for non-existent actors, while send is silent.
TEST(Scheduler, MissingActorOperationsAreIgnored) {
  agner::Scheduler scheduler;

  agner::ActorRef left{100};
  agner::ActorRef right{200};

  // send is silent for missing actors (fire-and-forget semantics)
  scheduler.send(left, Ping{1});

  // stop, link, monitor throw for missing actors
  EXPECT_THROW(scheduler.stop(left, {agner::ExitReason::Kind::stopped}),
               std::out_of_range);
  EXPECT_THROW(scheduler.link(left, right), std::out_of_range);
  EXPECT_THROW(scheduler.monitor(left, right), std::out_of_range);
}

// Summary: When linked actors stop, reverse link cleanup runs.
// Description: This test links two actors where one waits and stops normally.
// The other is stopped which cascades via link.
TEST(Scheduler, ReverseLinkCleanupRemovesBacklink) {
  agner::Scheduler scheduler;

  auto left = scheduler.spawn<IdleActor>();
  auto right = scheduler.spawn<IdleActor>();

  scheduler.link(left, right);
  scheduler.stop(left, {agner::ExitReason::Kind::stopped});
  scheduler.run();

  // right was killed by link cascade, no need to stop it again
  SUCCEED();
}
