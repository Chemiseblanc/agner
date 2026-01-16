#include <gtest/gtest.h>

#include <algorithm>
#include <coroutine>
#include <random>
#include <vector>

#include "test_support.hpp"

namespace {

using namespace agner::test_support;

}  // namespace

// Summary: When null or completed handles are scheduled, the deterministic
// scheduler shall ignore them safely. Description: This test schedules a null
// handle and a done handle on the deterministic scheduler. The assertions and
// lack of failure confirm invalid handles are ignored safely.
TEST(DeterministicScheduler, HandlesNullAndDoneHandles) {
  agner::DeterministicScheduler scheduler;

  scheduler.schedule(std::coroutine_handle<>{});

  auto done = make_manual_coroutine();
  done.handle.resume();
  EXPECT_TRUE(done.handle.done());

  scheduler.schedule(done.handle);
  scheduler.run_until_idle();

  done.handle.destroy();
}

// Summary: When tasks are detached, running the scheduler shall process the
// ready queue. Description: This test detaches a task to the ready queue and
// runs the scheduler. The assertion on `values` confirms the ready queue was
// processed.
TEST(DeterministicScheduler, RunProcessesReadyQueue) {
  agner::DeterministicScheduler scheduler;
  std::vector<int> values;

  record_value(&values, 3).detach(scheduler);
  scheduler.run();

  EXPECT_EQ(values, (std::vector<int>{3}));
}

// Summary: When a seed is provided, the ready queue shall shuffle
// deterministically. Description: This test detaches multiple tasks with a
// fixed seed and runs until idle. The assertion comparing against a seeded
// shuffle confirms deterministic ordering.
TEST(DeterministicScheduler, ReadyQueueIsShuffledDeterministically) {
  agner::DeterministicScheduler scheduler(123);
  std::vector<int> values;

  record_value(&values, 1).detach(scheduler);
  record_value(&values, 2).detach(scheduler);
  record_value(&values, 3).detach(scheduler);
  record_value(&values, 4).detach(scheduler);

  scheduler.run_until_idle();

  std::vector<int> expected{1, 2, 3, 4};
  std::mt19937_64 rng(123);
  std::shuffle(expected.begin(), expected.end(), rng);

  EXPECT_EQ(values, expected);
}

// Summary: When timers share the same tick, their execution order shall be
// deterministic. Description: This test schedules three timers at the same tick
// with a fixed seed. The assertion against the seeded shuffle confirms
// deterministic ordering within the tick.
TEST(DeterministicScheduler, TimersShareSameTickOrder) {
  agner::DeterministicScheduler scheduler(321);
  std::vector<int> values;

  scheduler.schedule_after(agner::DeterministicScheduler::duration{5},
                           [&] { values.push_back(1); });
  scheduler.schedule_after(agner::DeterministicScheduler::duration{5},
                           [&] { values.push_back(2); });
  scheduler.schedule_after(agner::DeterministicScheduler::duration{5},
                           [&] { values.push_back(3); });

  scheduler.run_until(agner::DeterministicScheduler::time_point{
      agner::DeterministicScheduler::duration{5}});

  std::vector<int> expected{1, 2, 3};
  std::mt19937_64 rng(321);
  std::shuffle(expected.begin(), expected.end(), rng);

  EXPECT_EQ(values, expected);
}

// Summary: When time advances in steps, timers shall fire in per-tick batches.
// Description:
// This test schedules timers at ticks 1 and 2, then advances time in steps.
// The assertions confirm only timers due in each batch fire per run_for call.
TEST(DeterministicScheduler, ProcessesTimersInBatches) {
  agner::DeterministicScheduler scheduler(0);
  std::vector<int> values;

  scheduler.schedule_after(agner::DeterministicScheduler::duration{1},
                           [&] { values.push_back(1); });
  scheduler.schedule_after(agner::DeterministicScheduler::duration{2},
                           [&] { values.push_back(2); });

  scheduler.run_for(agner::DeterministicScheduler::duration{1});
  EXPECT_EQ(values, (std::vector<int>{1}));

  scheduler.run_for(agner::DeterministicScheduler::duration{1});
  EXPECT_EQ(values, (std::vector<int>{1, 2}));
}

// Summary: When running and advancing time, the deterministic scheduler shall
// update time consistently. Description: This test runs to trigger a timer,
// then advances time and runs until an earlier time point. The assertions on
// `fired` and `now()` confirm time advancement and reset semantics.
TEST(DeterministicScheduler, RunAdvanceAndRunUntil) {
  agner::DeterministicScheduler scheduler;
  bool fired = false;

  scheduler.schedule_after(agner::DeterministicScheduler::duration{2},
                           [&] { fired = true; });
  scheduler.run();

  EXPECT_TRUE(fired);

  scheduler.advance(agner::DeterministicScheduler::duration{3});
  EXPECT_EQ(scheduler.now(), agner::DeterministicScheduler::time_point{
                                 agner::DeterministicScheduler::duration{5}});

  scheduler.run_until(agner::DeterministicScheduler::time_point{
      agner::DeterministicScheduler::duration{1}});
  EXPECT_EQ(scheduler.now(), agner::DeterministicScheduler::time_point{
                                 agner::DeterministicScheduler::duration{1}});
}

// Summary: When running until a time point, the scheduler shall process
// intermediate timers. Description: This test schedules timers at 2 and 4, then
// runs until time 5. The assertions on `values` and `now()` confirm
// intermediate timers are processed and time advanced.
TEST(DeterministicScheduler, RunUntilProcessesIntermediateTimers) {
  agner::DeterministicScheduler scheduler;
  std::vector<int> values;

  scheduler.schedule_after(agner::DeterministicScheduler::duration{2},
                           [&] { values.push_back(1); });
  scheduler.schedule_after(agner::DeterministicScheduler::duration{4},
                           [&] { values.push_back(2); });

  scheduler.run_until(agner::DeterministicScheduler::time_point{
      agner::DeterministicScheduler::duration{5}});

  EXPECT_EQ(values, (std::vector<int>{1, 2}));
  EXPECT_EQ(scheduler.now(), agner::DeterministicScheduler::time_point{
                                 agner::DeterministicScheduler::duration{5}});
}

// Summary: When running for a duration, the scheduler shall advance time and
// trigger due timers. Description: This test schedules a timer at 5, runs for 3
// then 2. The assertions confirm the timer fires only after full duration and
// time advances.
TEST(DeterministicScheduler, RunForAdvancesTime) {
  agner::DeterministicScheduler scheduler(0);
  bool fired = false;

  scheduler.schedule_after(agner::DeterministicScheduler::duration{5},
                           [&] { fired = true; });

  scheduler.run_for(agner::DeterministicScheduler::duration{3});
  EXPECT_FALSE(fired);

  scheduler.run_for(agner::DeterministicScheduler::duration{2});
  EXPECT_TRUE(fired);
}
