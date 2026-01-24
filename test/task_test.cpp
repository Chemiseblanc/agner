#include <gtest/gtest.h>

#include <coroutine>
#include <stdexcept>

#include "agner/scheduler.hpp"
#include "test_support.hpp"

namespace {

using namespace agner::test_support;

}  // namespace

// Summary: When an int task is detached, it shall run and resume its
// continuation. Description: This test detaches a chained int task so the
// continuation runs under the scheduler. The assertion on `value` confirms the
// awaited chain resumed and produced 7.
TEST(Task, DetachedIntTaskRunsAndResumesContinuation) {
  agner::DeterministicScheduler scheduler;
  int value = 0;

  auto task = chained_int(&value);
  task.detach(scheduler);
  scheduler.run_until_idle();

  EXPECT_EQ(value, 7);
}

// Summary: When a void task is detached, it shall run and resume its
// continuation. Description: This test detaches a chained void task and runs
// until idle. The `EXPECT_TRUE(ran)` assertion confirms the continuation
// executed.
TEST(Task, DetachedVoidTaskRunsAndResumesContinuation) {
  agner::DeterministicScheduler scheduler;
  bool ran = false;

  auto task = chained_void(&ran);
  task.detach(scheduler);
  scheduler.run_until_idle();

  EXPECT_TRUE(ran);
}

// Summary: When a task throws, await_resume shall propagate the exception.
// Description:
// This test resumes tasks that throw and then calls `await_resume`.
// The `EXPECT_THROW` assertions confirm exceptions are propagated to the
// caller.
TEST(Task, PropagatesExceptions) {
  auto int_task = throwing_int_task();
  auto int_handle = int_task.await_suspend(std::coroutine_handle<>{});

  int_handle.resume();
  EXPECT_THROW(int_task.await_resume(), std::runtime_error);

  auto void_task = throwing_void_task();
  auto void_handle = void_task.await_suspend(std::coroutine_handle<>{});

  void_handle.resume();
  EXPECT_THROW(void_task.await_resume(), std::runtime_error);
}

// Summary: When detaching moved-from tasks, the detach shall return early.
// Description: This test detaches moved-from int and void tasks to cover the
// empty-handle detach decisions while still running the valid task.
TEST(Task, DetachMovedFromTasksReturnsEarly) {
  agner::Scheduler scheduler;
  bool ran = false;

  auto int_task = immediate_int(9);
  auto moved_int = std::move(int_task);
  int_task.detach(scheduler);

  auto void_task = immediate_void(&ran);
  auto moved_void = std::move(void_task);
  void_task.detach(scheduler);

  moved_int.detach(scheduler);
  moved_void.detach(scheduler);
  scheduler.run();

  EXPECT_TRUE(ran);
}
