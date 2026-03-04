#include <gtest/gtest.h>

#include <chrono>
#include <optional>
#include <vector>

#include "test_support.hpp"

namespace {

using namespace std::chrono_literals;
using namespace agner::test_support;

}  // namespace

// Summary: When an actor receives a message, it shall capture the payload
// value. Description: This test sends `Ping{42}` to a `Collector`, so the only
// way for `value` to change is through the receive path. The assertion
// `EXPECT_EQ(value, 42)` confirms the handler ran and stored the payload,
// validating message delivery.
// EARS: When spawn and receive occurs, the actor component shall exhibit the
// expected behavior. Test method: This test drives the spawn and receive
// scenario and asserts the observable outputs/state transitions. Justification:
// those assertions directly verify the requirement outcome.
TEST(Actor, SpawnAndReceive) {
  agner::Scheduler scheduler;
  int value = 0;
  auto actor = scheduler.spawn<Collector>(&value);

  scheduler.send(actor, Ping{42});
  scheduler.run();

  EXPECT_EQ(value, 42);
}

// Summary: When try-receive times out without a message, it shall return the
// timeout value. Description: This test schedules only system signals and never
// sends a `Ping`, so no user message matches. The timeout handler returns `-1`,
// and `EXPECT_EQ(value, -1)` confirms the timeout path ran.
// EARS: When try receive timeout occurs, the actor component shall exhibit the
// expected behavior. Test method: This test drives the try receive timeout
// scenario and asserts the observable outputs/state transitions. Justification:
// those assertions directly verify the requirement outcome.
TEST(Actor, TryReceiveTimeout) {
  agner::Scheduler scheduler;
  int value = 0;
  auto actor = scheduler.spawn<TimeoutReceiver>(&value);

  scheduler.schedule_after(
      agner::Scheduler::Clock::duration::zero(), [&scheduler, actor] {
        scheduler.send(actor, make_exit_signal(agner::ExitReason::Kind::error));
      });
  scheduler.schedule_after(
      agner::Scheduler::Clock::duration::zero(), [&scheduler, actor] {
        scheduler.send(actor, make_down_signal(agner::ExitReason::Kind::error));
      });

  scheduler.run();

  EXPECT_EQ(value, -1);
}

// Summary: When a message arrives before try-receive times out, it shall return
// the message value. Description: This test schedules an immediate `Ping{8}` so
// the receive happens before timeout. The `EXPECT_EQ(value, 8)` assertion
// confirms the visitor ran and timeout was bypassed.
// EARS: When try receive timeout early message occurs, the actor component
// shall exhibit the expected behavior. Test method: This test drives the try
// receive timeout early message scenario and asserts the observable
// outputs/state transitions. Justification: those assertions directly verify
// the requirement outcome.
TEST(Actor, TryReceiveTimeoutEarlyMessage) {
  agner::Scheduler scheduler;
  int value = 0;
  auto actor = scheduler.spawn<TimeoutReceiver>(&value);

  scheduler.schedule_after(
      agner::Scheduler::Clock::duration::zero(),
      [&scheduler, actor] { scheduler.send(actor, Ping{8}); });

  scheduler.run();

  EXPECT_EQ(value, 8);
}

// Summary: When try-receive has a queued message, it shall return the message
// value. Description: This test queues a `Ping{7}` before running the scheduler
// so the message is ready. The `EXPECT_EQ(value, 7)` assertion confirms the
// try-receive returned the queued payload.
// EARS: When try receive success occurs, the actor component shall exhibit the
// expected behavior. Test method: This test drives the try receive success
// scenario and asserts the observable outputs/state transitions. Justification:
// those assertions directly verify the requirement outcome.
TEST(Actor, TryReceiveSuccess) {
  agner::Scheduler scheduler;
  int value = 0;
  auto actor = scheduler.spawn<TryReceiveSuccess>(&value);

  scheduler.send(actor, Ping{7});
  scheduler.run();

  EXPECT_EQ(value, 7);
}

// Summary: When a message arrives within the timeout window, it shall return
// the message value. Description: This test delays a `Ping{9}` within the 5ms
// timeout window while scheduling exit/down signals. The `EXPECT_EQ(value, 9)`
// assertion confirms the message beat the timeout and was delivered.
// EARS: When try receive success delayed send occurs, the actor component shall
// exhibit the expected behavior. Test method: This test drives the try receive
// success delayed send scenario and asserts the observable outputs/state
// transitions. Justification: those assertions directly verify the requirement
// outcome.
TEST(Actor, TryReceiveSuccessDelayedSend) {
  agner::Scheduler scheduler;
  int value = 0;
  auto actor = scheduler.spawn<TryReceiveSuccess>(&value);

  scheduler.schedule_after(1ms, [&scheduler, actor] {
    scheduler.send(actor, make_exit_signal(agner::ExitReason::Kind::error));
  });
  scheduler.schedule_after(1ms, [&scheduler, actor] {
    scheduler.send(actor, make_down_signal(agner::ExitReason::Kind::error));
  });
  scheduler.schedule_after(
      2ms, [&scheduler, actor] { scheduler.send(actor, Ping{9}); });

  scheduler.run();

  EXPECT_EQ(value, 9);
}

// Summary: When no message arrives before timeout, it shall return the timeout
// value. Description: This test never sends a message, so only the timeout path
// can complete the await. The `EXPECT_EQ(value, -1)` assertion confirms the
// timeout value is returned.
// EARS: When try receive success timeout occurs, the actor component shall
// exhibit the expected behavior. Test method: This test drives the try receive
// success timeout scenario and asserts the observable outputs/state
// transitions. Justification: those assertions directly verify the requirement
// outcome.
TEST(Actor, TryReceiveSuccessTimeout) {
  agner::Scheduler scheduler;
  int value = 0;
  scheduler.spawn<TryReceiveSuccess>(&value);

  scheduler.run();

  EXPECT_EQ(value, -1);
}

// Summary: When try-receive with void visitor times out, it shall invoke the
// timeout handler. Description: This test schedules only system signals so the
// void visitor never runs. The assertions `EXPECT_TRUE(timed_out)` and
// `EXPECT_FALSE(received)` confirm the timeout handler ran.
// EARS: When try receive void timeout occurs, the actor component shall exhibit
// the expected behavior. Test method: This test drives the try receive void
// timeout scenario and asserts the observable outputs/state transitions.
// Justification: those assertions directly verify the requirement outcome.
TEST(Actor, TryReceiveVoidTimeout) {
  agner::Scheduler scheduler;
  bool received = false;
  bool timed_out = false;
  auto actor = scheduler.spawn<TryReceiveVoidTimeout>(&received, &timed_out);

  scheduler.schedule_after(
      agner::Scheduler::Clock::duration::zero(), [&scheduler, actor] {
        scheduler.send(actor, make_exit_signal(agner::ExitReason::Kind::error));
      });
  scheduler.schedule_after(
      agner::Scheduler::Clock::duration::zero(), [&scheduler, actor] {
        scheduler.send(actor, make_down_signal(agner::ExitReason::Kind::error));
      });

  scheduler.run();

  EXPECT_TRUE(timed_out);
  EXPECT_FALSE(received);
}

// Summary: When a message arrives before timeout, it shall invoke the visitor
// instead of timing out. Description: This test schedules an immediate
// `Ping{11}` so the visitor should run before any timeout. The assertions
// `EXPECT_TRUE(received)` and `EXPECT_FALSE(timed_out)` confirm the receive
// path won.
// EARS: When try receive void timeout early message occurs, the actor component
// shall exhibit the expected behavior. Test method: This test drives the try
// receive void timeout early message scenario and asserts the observable
// outputs/state transitions. Justification: those assertions directly verify
// the requirement outcome.
TEST(Actor, TryReceiveVoidTimeoutEarlyMessage) {
  agner::Scheduler scheduler;
  bool received = false;
  bool timed_out = false;
  auto actor = scheduler.spawn<TryReceiveVoidTimeout>(&received, &timed_out);

  scheduler.schedule_after(
      agner::Scheduler::Clock::duration::zero(),
      [&scheduler, actor] { scheduler.send(actor, Ping{11}); });

  scheduler.run();

  EXPECT_TRUE(received);
  EXPECT_FALSE(timed_out);
}

// Summary: When a message matches a secondary visitor, it shall return that
// visitor's result. Description: This test sends `Pong{21}` so only the second
// visitor matches and doubles the value. The `EXPECT_EQ(value, 42)` assertion
// confirms the selected visitor's result.
// EARS: When multi visitor common result occurs, the actor component shall
// exhibit the expected behavior. Test method: This test drives the multi
// visitor common result scenario and asserts the observable outputs/state
// transitions. Justification: those assertions directly verify the requirement
// outcome.
TEST(Actor, MultiVisitorCommonResult) {
  agner::Scheduler scheduler;
  int value = 0;
  auto actor = scheduler.spawn<MultiVisitorActor>(&value);

  scheduler.send(actor, Pong{21});
  scheduler.run();

  EXPECT_EQ(value, 42);
}

// Summary: When a message matches the first visitor, it shall return the first
// visitor's result. Description: This test sends `Ping{9}` so the first visitor
// matches immediately. The `EXPECT_EQ(value, 9)` assertion confirms the first
// visitor's result is used.
// EARS: When multi visitor first match occurs, the actor component shall
// exhibit the expected behavior. Test method: This test drives the multi
// visitor first match scenario and asserts the observable outputs/state
// transitions. Justification: those assertions directly verify the requirement
// outcome.
TEST(Actor, MultiVisitorFirstMatch) {
  agner::Scheduler scheduler;
  int value = 0;
  auto actor = scheduler.spawn<MultiVisitorActor>(&value);

  scheduler.send(actor, Ping{9});
  scheduler.run();

  EXPECT_EQ(value, 9);
}

// Summary: When multiple messages are sent, the actor shall process them in
// FIFO order. Description: This test sends `Ping{1}` then `Ping{2}` to the same
// actor in order. The assertions on `values[0]` and `values[1]` confirm FIFO
// message processing.
// EARS: When mailbox ordering occurs, the actor component shall exhibit the
// expected behavior. Test method: This test drives the mailbox ordering
// scenario and asserts the observable outputs/state transitions. Justification:
// those assertions directly verify the requirement outcome.
TEST(Actor, MailboxOrdering) {
  agner::Scheduler scheduler;
  std::vector<int> values;
  auto actor = scheduler.spawn<SequenceCollector>(&values);

  scheduler.send(actor, Ping{1});
  scheduler.send(actor, Ping{2});
  scheduler.run();

  ASSERT_EQ(values.size(), 2u);
  EXPECT_EQ(values[0], 1);
  EXPECT_EQ(values[1], 2);
}

// Summary: When an actor spawns linked and monitored children, it shall receive
// exit and down signals. Description: This test runs `SpawnTester`, which
// spawns a worker, a linked worker, and a monitored worker, then stops them.
// The assertions on `report` confirm one exit and one down signal were
// delivered.
// EARS: When spawn link and monitor from actor occurs, the actor component
// shall exhibit the expected behavior. Test method: This test drives the spawn
// link and monitor from actor scenario and asserts the observable outputs/state
// transitions. Justification: those assertions directly verify the requirement
// outcome.
TEST(Actor, SpawnLinkAndMonitorFromActor) {
  agner::Scheduler scheduler;
  SpawnReport report;
  scheduler.spawn<SpawnTester>(&report);

  scheduler.run();

  EXPECT_EQ(report.exit_signals, 1);
  EXPECT_EQ(report.down_signals, 1);
}

// Summary: When no message is available, receive shall suspend until a message
// arrives. Description: This test runs the deterministic scheduler while no
// message is sent, so the receive remains suspended. After sending `Ping{11}`,
// the assertion on `value` confirms the receive resumed and captured the
// payload.
// EARS: When receive suspends until message occurs, the actor component shall
// exhibit the expected behavior. Test method: This test drives the receive
// suspends until message scenario and asserts the observable outputs/state
// transitions. Justification: those assertions directly verify the requirement
// outcome.
TEST(Actor, ReceiveSuspendsUntilMessage) {
  agner::DeterministicScheduler scheduler;
  std::optional<int> value;
  auto actor = scheduler.spawn<DeferredCollector>(&value);

  scheduler.run_until_idle();
  EXPECT_FALSE(value.has_value());

  scheduler.send(actor, Ping{11});
  scheduler.run_until_idle();

  ASSERT_TRUE(value.has_value());
  EXPECT_EQ(*value, 11);
}

// Summary: When try-receive is pending, it shall return the message and avoid
// timing out. Description: This test runs idle with no message, ensuring
// try-receive has not completed or timed out. After sending `Ping{13}`, the
// assertions confirm a value was captured and `timed_out` stayed false.
// EARS: When try receive suspends until message occurs, the actor component
// shall exhibit the expected behavior. Test method: This test drives the try
// receive suspends until message scenario and asserts the observable
// outputs/state transitions. Justification: those assertions directly verify
// the requirement outcome.
TEST(Actor, TryReceiveSuspendsUntilMessage) {
  agner::DeterministicScheduler scheduler;
  std::optional<int> value;
  bool timed_out = false;
  auto actor = scheduler.spawn<DeferredTryReceive>(&value, &timed_out);

  scheduler.run_until_idle();
  EXPECT_FALSE(value.has_value());
  EXPECT_FALSE(timed_out);

  scheduler.send(actor, Ping{13});
  scheduler.run_until_idle();

  ASSERT_TRUE(value.has_value());
  EXPECT_EQ(*value, 13);
  EXPECT_FALSE(timed_out);
}

// Summary: When try-receive times out in the deterministic scheduler, it shall
// return the timeout value. Description: This test delivers exit/down signals
// that do not satisfy the `Ping` visitor, then advances time past timeout. The
// assertions on `timed_out` and `value` confirm the timeout handler returned
// `-1`.
// EARS: When deferred try receive times out occurs, the actor component shall
// exhibit the expected behavior. Test method: This test drives the deferred try
// receive times out scenario and asserts the observable outputs/state
// transitions. Justification: those assertions directly verify the requirement
// outcome.
TEST(Actor, DeferredTryReceiveTimesOut) {
  agner::DeterministicScheduler scheduler;
  std::optional<int> value;
  bool timed_out = false;
  auto actor = scheduler.spawn<DeferredTryReceive>(&value, &timed_out);

  scheduler.run_until_idle();
  scheduler.send(actor, make_exit_signal(agner::ExitReason::Kind::error));
  scheduler.send(actor, make_down_signal(agner::ExitReason::Kind::error));

  scheduler.run_for(agner::DeterministicScheduler::duration{10});

  EXPECT_TRUE(timed_out);
  ASSERT_TRUE(value.has_value());
  EXPECT_EQ(*value, -1);
}

// Summary: When system signals are unmatched, a pending receive shall still
// accept the next message. Description: This test delivers system signals
// before a `Ping` to ensure they do not satisfy the receive. The
// `EXPECT_EQ(*value, 99)` assertion confirms the pending receive skipped
// signals and accepted the message.
// EARS: When pending skips unmatched system signal occurs, the actor component
// shall exhibit the expected behavior. Test method: This test drives the
// pending skips unmatched system signal scenario and asserts the observable
// outputs/state transitions. Justification: those assertions directly verify
// the requirement outcome.
TEST(Actor, PendingSkipsUnmatchedSystemSignal) {
  agner::DeterministicScheduler scheduler;
  std::optional<int> value;
  auto actor = scheduler.spawn<DeferredCollector>(&value);

  scheduler.run_until_idle();

  scheduler.send(actor, make_exit_signal(agner::ExitReason::Kind::error));
  scheduler.send(actor, make_down_signal(agner::ExitReason::Kind::error));
  scheduler.send(actor, Ping{99});
  scheduler.run_until_idle();

  ASSERT_TRUE(value.has_value());
  EXPECT_EQ(*value, 99);
}

// Summary: When a message arrives before timeout, the timeout shall be
// cancelled. Description: This test sends a `Ping{42}` after signals, so the
// message should satisfy the pending try-receive. The assertions confirm the
// value is set and `timed_out` remains false even after advancing time.
// EARS: When try receive timeout cancelled when message arrives occurs, the
// actor component shall exhibit the expected behavior. Test method: This test
// drives the try receive timeout cancelled when message arrives scenario and
// asserts the observable outputs/state transitions. Justification: those
// assertions directly verify the requirement outcome.
TEST(Actor, TryReceiveTimeoutCancelledWhenMessageArrives) {
  agner::DeterministicScheduler scheduler;
  std::optional<int> value;
  bool timed_out = false;
  auto actor = scheduler.spawn<DeferredTryReceive>(&value, &timed_out);

  scheduler.run_until_idle();

  scheduler.send(actor, make_exit_signal(agner::ExitReason::Kind::error));
  scheduler.send(actor, make_down_signal(agner::ExitReason::Kind::error));
  scheduler.send(actor, Ping{42});
  scheduler.run_until_idle();

  ASSERT_TRUE(value.has_value());
  EXPECT_EQ(*value, 42);
  EXPECT_FALSE(timed_out);

  scheduler.run_for(agner::DeterministicScheduler::duration{10});
  EXPECT_FALSE(timed_out);
}
