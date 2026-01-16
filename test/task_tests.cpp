#include <gtest/gtest.h>

#include <coroutine>
#include <stdexcept>
#include <utility>

#include "test_support.hpp"

namespace {

using namespace agner::test_support;

template <typename T>
T&& self_move(T& value) {
  return std::move(value);
}

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

// Summary: When a task handle is manually resumed, it shall yield the expected
// result. Description: This test manually resumes the coroutine handle without
// detaching or continuation. The `EXPECT_EQ(task.await_resume(), 3)` assertion
// confirms manual resume produces the result.
TEST(Task, ManualRunWithoutContinuationOrDetach) {
  auto task = immediate_int(3);
  auto handle = task.await_suspend(std::coroutine_handle<>{});

  handle.resume();

  EXPECT_EQ(task.await_resume(), 3);
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

// Summary: When tasks are moved or detached while empty, they shall behave
// safely. Description: This test move-assigns int/void tasks, resumes them, and
// detaches moved/empty tasks. The assertions on returned values and lack of
// failures confirm move and empty detach safety.
TEST(Task, MoveAssignmentAndEmptyDetachAreSafe) {
  agner::DeterministicScheduler scheduler;
  bool ran = false;

  auto task_void = immediate_void(&ran);
  auto other_void = immediate_void(&ran);
  task_void = std::move(other_void);

  auto void_handle = task_void.await_suspend(std::coroutine_handle<>{});
  void_handle.resume();
  task_void.await_resume();

  auto task_int = immediate_int(4);
  task_int = self_move(task_int);
  auto int_handle = task_int.await_suspend(std::coroutine_handle<>{});
  int_handle.resume();
  EXPECT_EQ(task_int.await_resume(), 4);

  auto moved_int = std::move(task_int);
  task_int.detach(scheduler);

  auto moved_void = std::move(task_void);
  task_void.detach(scheduler);
}

// Summary: When move-assigning a task, it shall replace the existing handle
// safely. Description: This test move-assigns over an existing task and resumes
// it. The `EXPECT_EQ(first.await_resume(), 2)` assertion confirms the new
// handle replaced the old one.
TEST(Task, MoveAssignmentDestroysExistingHandle) {
  auto first = immediate_int(1);
  auto second = immediate_int(2);

  first = std::move(second);

  auto handle = first.await_suspend(std::coroutine_handle<>{});
  handle.resume();
  EXPECT_EQ(first.await_resume(), 2);
}

// Summary: When move-assigning into an empty handle, both tasks shall remain
// valid. Description: This test move-assigns into an original task and resumes
// both original and moved handles. The assertions confirm both tasks retain
// their values after the moves.
TEST(Task, MoveAssignmentIntoEmptyHandle) {
  auto original = immediate_int(1);
  auto moved = std::move(original);
  auto replacement = immediate_int(2);

  original = std::move(replacement);

  auto handle = original.await_suspend(std::coroutine_handle<>{});
  handle.resume();
  EXPECT_EQ(original.await_resume(), 2);

  auto moved_handle = moved.await_suspend(std::coroutine_handle<>{});
  moved_handle.resume();
  EXPECT_EQ(moved.await_resume(), 1);
}

// Summary: When void tasks are self-moved or replaced, they shall remain
// usable. Description: This test self-moves and replaces a void task, then
// resumes both handles. The lack of failures confirms void move-assignment and
// self-move are safe.
TEST(Task, VoidMoveAssignmentHandlesEmptyAndSelf) {
  bool ran = false;
  auto task = immediate_void(&ran);

  task = self_move(task);

  auto moved = std::move(task);
  auto replacement = immediate_void(&ran);
  task = std::move(replacement);

  auto handle = task.await_suspend(std::coroutine_handle<>{});
  handle.resume();
  task.await_resume();

  auto moved_handle = moved.await_suspend(std::coroutine_handle<>{});
  moved_handle.resume();
  moved.await_resume();
}

// Summary: When empty tasks are detached, the scheduler shall run without
// error. Description: This test detaches empty and moved tasks, then runs the
// scheduler. The `EXPECT_TRUE(ran)` assertion confirms the valid task ran
// despite empty detaches.
TEST(Task, EmptyDetachOnScheduler) {
  agner::Scheduler scheduler;
  bool ran = false;

  auto task = immediate_void(&ran);
  auto moved = std::move(task);

  task.detach(scheduler);
  moved.detach(scheduler);
  scheduler.run();

  EXPECT_TRUE(ran);
}
