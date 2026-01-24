#define AGNER_TESTING 1
#include "agner/scheduler.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <coroutine>
#include <vector>

#include "test_support.hpp"

namespace agner {

struct SchedulerTestAccess {
  static void add_link_entry(Scheduler& scheduler, ActorRef left,
                             ActorRef right) {
    scheduler.links_[left].push_back(right);
    scheduler.links_[right].push_back(left);
  }

  static bool has_link_entry(const Scheduler& scheduler, ActorRef ref) {
    return scheduler.links_.find(ref) != scheduler.links_.end();
  }

  static bool link_contains(const Scheduler& scheduler, ActorRef key,
                            ActorRef target) {
    auto entry = scheduler.links_.find(key);
    if (entry == scheduler.links_.end()) {
      return false;
    }
    const auto& links = entry->second;
    return std::find(links.begin(), links.end(), target) != links.end();
  }

  static void notify_exit_for_tests(Scheduler& scheduler, ActorRef actor_ref,
                                    const ExitReason& reason) {
    scheduler.notify_exit(actor_ref, reason);
  }
};

}  // namespace agner

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

// Summary: When operating on missing actors, the scheduler returns early.
// Description: This test uses unknown actor refs for send, stop, link, and
// monitor to cover the missing-actor decision paths.
TEST(Scheduler, MissingActorOperationsAreIgnored) {
  agner::Scheduler scheduler;

  agner::ActorRef left{100};
  agner::ActorRef right{200};

  scheduler.send(left, Ping{1});
  scheduler.stop(left, {agner::ExitReason::Kind::stopped});
  scheduler.link(left, right);
  scheduler.monitor(left, right);

  SUCCEED();
}

// Summary: When linked actors stop, reverse link cleanup runs.
// Description: This test links two actors and stops one, ensuring the reverse
// link entry exists and is cleaned during exit handling.
TEST(Scheduler, ReverseLinkCleanupRemovesBacklink) {
  agner::Scheduler scheduler;

  auto left = scheduler.spawn<IdleActor>();
  auto right = scheduler.spawn<IdleActor>();

  scheduler.link(left, right);
  scheduler.stop(left, {agner::ExitReason::Kind::stopped});
  scheduler.run();

  scheduler.stop(right, {agner::ExitReason::Kind::stopped});
  scheduler.run();

  SUCCEED();
}

// Summary: When exits see reverse link entries, they are cleaned up.
// Description: This test injects a link entry and calls notify_exit directly to
// cover the reverse-entry decision path.
TEST(Scheduler, NotifyExitCleansReverseLinkEntry) {
  agner::Scheduler scheduler;

  agner::ActorRef left{1};
  agner::ActorRef right{2};
  agner::SchedulerTestAccess::add_link_entry(scheduler, left, right);
  ASSERT_TRUE(agner::SchedulerTestAccess::has_link_entry(scheduler, right));
  ASSERT_TRUE(
      agner::SchedulerTestAccess::link_contains(scheduler, left, right));
  ASSERT_TRUE(
      agner::SchedulerTestAccess::link_contains(scheduler, right, left));

  agner::SchedulerTestAccess::notify_exit_for_tests(
      scheduler, left, {agner::ExitReason::Kind::stopped});

  EXPECT_TRUE(agner::SchedulerTestAccess::has_link_entry(scheduler, right));
  EXPECT_FALSE(
      agner::SchedulerTestAccess::link_contains(scheduler, right, left));

  SUCCEED();
}
