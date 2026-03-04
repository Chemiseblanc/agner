#include <gtest/gtest.h>

#include <coroutine>
#include <stdexcept>

#include "agner/scheduler.hpp"
#include "test_support.hpp"

namespace {

using namespace agner::test_support;

agner::task<void>&& move_task(agner::task<void>& task) {
  return std::move(task);
}

}  // namespace

// Summary: When an int task is detached, it shall run and resume its
// continuation. Description: This test detaches a chained int task so the
// continuation runs under the scheduler. The assertion on `value` confirms the
// awaited chain resumed and produced 7.
// EARS: When detached int task runs and resumes continuation occurs, the task
// component shall exhibit the expected behavior. Test method: This test drives
// the detached int task runs and resumes continuation scenario and asserts the
// observable outputs/state transitions. Justification: those assertions
// directly verify the requirement outcome.
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
// EARS: When detached void task runs and resumes continuation occurs, the task
// component shall exhibit the expected behavior. Test method: This test drives
// the detached void task runs and resumes continuation scenario and asserts the
// observable outputs/state transitions. Justification: those assertions
// directly verify the requirement outcome.
TEST(Task, DetachedVoidTaskRunsAndResumesContinuation) {
  agner::DeterministicScheduler scheduler;
  bool ran = false;

  auto task = chained_void(&ran);
  task.detach(scheduler);
  scheduler.run_until_idle();

  EXPECT_TRUE(ran);
}

// Summary: When detach is called on a moved-from task, it shall return without
// scheduling anything. Description: This test moves a task into another
// variable and then calls detach on the original (now moved-from) handle.
// Because the handle is null after the move, detach must exit early without
// scheduling. The assertion confirms the scheduler ran no work.
// EARS: When detach is called on a task with a null internal handle, the task
// component shall return immediately without resuming any coroutine. Test
// method: Move both an int task and a void task before calling detach on the
// originals; verify the scheduler remains idle (ran==false, value==0).
TEST(Task, MovedFromTaskDetachIsNoop) {
  agner::DeterministicScheduler scheduler;
  bool ran = false;
  int value = 0;

  auto int_task = chained_int(&value);
  auto moved_int = std::move(int_task);
  int_task.detach(scheduler);  // moved-from: handle_ is null, returns early

  auto void_task = chained_void(&ran);
  auto moved_void = std::move(void_task);
  void_task.detach(scheduler);  // moved-from: handle_ is null, returns early

  scheduler.run_until_idle();

  // Nothing was scheduled by the moved-from detach calls
  EXPECT_FALSE(ran);
  EXPECT_EQ(value, 0);

  // Clean up: detach the real tasks so they run
  moved_int.detach(scheduler);
  moved_void.detach(scheduler);
  scheduler.run_until_idle();
  EXPECT_EQ(value, 7);
  EXPECT_TRUE(ran);
}

// Summary: When a task throws, await_resume shall propagate the exception.
// Description: This test resumes tasks that throw and then calls
// `await_resume`. The `EXPECT_THROW` assertions confirm exceptions are
// propagated to the caller.
// EARS: When an unhandled exception escapes a coroutine body, the task
// component shall rethrow it when await_resume is called. Test method: Resume
// int and void throwing tasks, then assert await_resume throws runtime_error.
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

// Summary: When a non-empty void task is move-assigned, its existing handle
// shall be destroyed. Description: This test constructs a void task and then
// move-assigns another task over it while it still holds a live coroutine
// handle. The handle_.destroy() branch in operator= must run. The subsequent
// run confirms only the newly assigned task executes.
// EARS: When a task<void> holding a live handle is move-assigned, the task
// component shall destroy the existing coroutine before adopting the new one.
TEST(Task, MoveAssignDestroysPreviousHandle) {
  agner::DeterministicScheduler scheduler;
  bool ran_a = false;
  bool ran_b = false;

  auto task_a = chained_void(&ran_a);
  auto task_b = chained_void(&ran_b);

  // task_a holds a live handle; overwrite it with task_b (triggers
  // handle_.destroy())
  task_a = std::move(task_b);
  task_a.detach(scheduler);
  scheduler.run_until_idle();

  // Only task_b's work ran (via task_a after reassignment)
  EXPECT_FALSE(ran_a);
  EXPECT_TRUE(ran_b);
}

// Summary: Move assignment handles empty destination and self-move safely.
// Description: This test first move-assigns into a moved-from (empty) task to
// exercise the empty-destination path, then performs self move-assignment.
// EARS: When task<void> move assignment receives an empty destination or self
// assignment, it shall preserve correctness without destroying invalid handles.
TEST(Task, MoveAssignHandlesEmptyDestinationAndSelfMove) {
  agner::DeterministicScheduler scheduler;
  bool ran = false;

  auto source = chained_void(&ran);
  auto moved = std::move(source);

  source = std::move(moved);
  source = move_task(source);

  source.detach(scheduler);
  scheduler.run_until_idle();

  EXPECT_TRUE(ran);
}
