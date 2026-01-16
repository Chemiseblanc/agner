#include <gtest/gtest.h>

#include <chrono>
#include <coroutine>
#include <vector>

#include "test_support.hpp"

namespace {

using namespace std::chrono_literals;
using namespace agner::test_support;

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

// Summary: When null or completed handles are scheduled, the scheduler shall
// ignore them safely. Description: This test schedules a null handle and a
// handle that is already done. The assertions and lack of failure confirm the
// scheduler ignores invalid handles safely.
TEST(Scheduler, HandlesNullAndDoneHandles) {
  agner::Scheduler scheduler;

  scheduler.schedule(std::coroutine_handle<>{});

  auto done = make_manual_coroutine();
  done.handle.resume();
  EXPECT_TRUE(done.handle.done());

  scheduler.schedule(done.handle);
  scheduler.run();

  done.handle.destroy();
}
