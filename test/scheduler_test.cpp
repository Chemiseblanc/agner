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

class ThrowingActor : public TestActor<ThrowingActor, agner::Messages<>> {
 public:
  using TestActor<ThrowingActor, agner::Messages<>>::TestActor;

  agner::task<void> run() {
    throw std::runtime_error("actor boom");
    co_return;
  }
};

agner::task<void> completed_task() { co_return; }

class InspectableScheduler : public agner::Scheduler {
 public:
  void erase_links_entry(agner::ActorRef actor_ref) { links_.erase(actor_ref); }

  bool has_actor(agner::ActorRef actor_ref) const { return actor_exists(actor_ref); }
};

}  // namespace

// Summary: When an actor's run() throws an unhandled exception, the scheduler
// shall record an error exit reason.
// Description: This test spawns a ThrowingActor whose run() throws immediately,
// and a linked Observer. After the scheduler runs the Observer receives an
// ExitSignal with Kind::error, confirming the scheduler catch block propagates
// the exception as an error reason.
// EARS: When an actor throws during start, the scheduler shall set the exit
// reason kind to error and notify linked actors.
TEST(Scheduler, ActorStartThrowsPropagatesErrorReason) {
  agner::Scheduler scheduler;
  agner::test_support::SignalCapture cap;

  using SignalObserver = agner::test_support::SignalObserverT<agner::Scheduler>;

  // Establish the monitor at spawn time so it is registered before any
  // coroutine runs, even if the thrower exits on its very first resumption.
  auto observer = scheduler.spawn<SignalObserver>(&cap);
  scheduler.spawn_monitor<ThrowingActor>(observer);

  scheduler.run();

  ASSERT_TRUE(cap.down_kind.has_value());
  EXPECT_EQ(*cap.down_kind, agner::ExitReason::Kind::error);
}

// Summary: When immediate and delayed timers are scheduled, the scheduler shall
// run both in order. Description: This test schedules a zero-delay and a 1ms
// timer, then runs the scheduler. The assertion on `values` confirms the
// immediate timer fires first and the delayed timer fires later.
// EARS: When immediate and delayed timers occurs, the scheduler component shall
// exhibit the expected behavior. Test method: This test drives the immediate
// and delayed timers scenario and asserts the observable outputs/state
// transitions. Justification: those assertions directly verify the requirement
// outcome.
TEST(Scheduler, ImmediateAndDelayedTimers) {
  agner::Scheduler scheduler;
  std::vector<int> values;

  scheduler.schedule_after(agner::Scheduler::Clock::duration::zero(),
                           [&] { values.push_back(1); });
  scheduler.schedule_after(1ms, [&] { values.push_back(2); });
  scheduler.run();

  EXPECT_EQ(values, (std::vector<int>{1, 2}));
}

// Summary: When operating on missing actors, stop/link/monitor throw.
// Description: This test verifies that stop, link, and monitor operations
// throw std::out_of_range for non-existent actors, while send is silent.
// EARS: When missing actor operations are ignored occurs, the scheduler
// component shall exhibit the expected behavior. Test method: This test drives
// the missing actor operations are ignored scenario and asserts the observable
// outputs/state transitions. Justification: those assertions directly verify
// the requirement outcome.
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

// Summary: Null and completed coroutine handles are skipped by the scheduler.
// Description: This test schedules a null handle and a handle that has already
// completed, then runs the scheduler to exercise the skip paths.
// EARS: When scheduler encounters null or done handles, it shall not resume
// them and shall continue processing.
TEST(Scheduler, ScheduleSkipsNullAndDoneHandles) {
  agner::Scheduler scheduler;

  scheduler.schedule(std::coroutine_handle<>{});

  auto done_task = completed_task();
  auto done_handle = done_task.await_suspend(std::coroutine_handle<>{});
  done_handle.resume();
  ASSERT_TRUE(done_handle.done());

  scheduler.schedule(done_handle);
  scheduler.run();
}

// Summary: notify_exit tolerates missing reverse link entries.
// Description: This test links two actors, removes one reverse link table entry
// manually, then stops the other actor and verifies the scheduler proceeds.
// EARS: When reverse link entry is missing during notify_exit, the scheduler
// shall skip reverse cleanup for that peer without failing.
TEST(Scheduler, NotifyExitSkipsMissingReverseLinkEntry) {
  InspectableScheduler scheduler;

  auto left = scheduler.spawn<IdleActor>();
  auto right = scheduler.spawn<IdleActor>();
  scheduler.run();

  scheduler.link(left, right);
  scheduler.erase_links_entry(right);

  scheduler.stop(left, {agner::ExitReason::Kind::stopped});
  scheduler.run();

  EXPECT_FALSE(scheduler.has_actor(left));
}
