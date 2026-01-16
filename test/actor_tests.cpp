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
TEST(Actor, SpawnAndReceive) {
  agner::Scheduler scheduler;
  int value = 0;
  auto actor = scheduler.spawn<Collector>(&value);

  actor->send(Ping{42});
  scheduler.run();

  EXPECT_EQ(value, 42);
}

// Summary: When try-receive times out without a message, it shall return the
// timeout value. Description: This test schedules only system signals and never
// sends a `Ping`, so no user message matches. The timeout handler returns `-1`,
// and `EXPECT_EQ(value, -1)` confirms the timeout path ran.
TEST(Actor, TryReceiveTimeout) {
  agner::Scheduler scheduler;
  int value = 0;
  auto actor = scheduler.spawn<TimeoutReceiver>(&value);

  scheduler.schedule_after(agner::Scheduler::Clock::duration::zero(), [actor] {
    actor->deliver_exit({agner::ExitReason::Kind::error}, actor);
  });
  scheduler.schedule_after(agner::Scheduler::Clock::duration::zero(), [actor] {
    actor->deliver_down({agner::ExitReason::Kind::error}, actor);
  });

  scheduler.run();

  EXPECT_EQ(value, -1);
}

// Summary: When a message arrives before try-receive times out, it shall return
// the message value. Description: This test schedules an immediate `Ping{8}` so
// the receive happens before timeout. The `EXPECT_EQ(value, 8)` assertion
// confirms the visitor ran and timeout was bypassed.
TEST(Actor, TryReceiveTimeoutEarlyMessage) {
  agner::Scheduler scheduler;
  int value = 0;
  auto actor = scheduler.spawn<TimeoutReceiver>(&value);

  scheduler.schedule_after(agner::Scheduler::Clock::duration::zero(),
                           [actor] { actor->send(Ping{8}); });

  scheduler.run();

  EXPECT_EQ(value, 8);
}

// Summary: When try-receive has a queued message, it shall return the message
// value. Description: This test queues a `Ping{7}` before running the scheduler
// so the message is ready. The `EXPECT_EQ(value, 7)` assertion confirms the
// try-receive returned the queued payload.
TEST(Actor, TryReceiveSuccess) {
  agner::Scheduler scheduler;
  int value = 0;
  auto actor = scheduler.spawn<TryReceiveSuccess>(&value);

  actor->send(Ping{7});
  scheduler.run();

  EXPECT_EQ(value, 7);
}

// Summary: When a message arrives within the timeout window, it shall return
// the message value. Description: This test delays a `Ping{9}` within the 5ms
// timeout window while scheduling exit/down signals. The `EXPECT_EQ(value, 9)`
// assertion confirms the message beat the timeout and was delivered.
TEST(Actor, TryReceiveSuccessDelayedSend) {
  agner::Scheduler scheduler;
  int value = 0;
  auto actor = scheduler.spawn<TryReceiveSuccess>(&value);

  scheduler.schedule_after(1ms, [actor] {
    actor->deliver_exit({agner::ExitReason::Kind::error}, actor);
  });
  scheduler.schedule_after(1ms, [actor] {
    actor->deliver_down({agner::ExitReason::Kind::error}, actor);
  });
  scheduler.schedule_after(2ms, [actor] { actor->send(Ping{9}); });

  scheduler.run();

  EXPECT_EQ(value, 9);
}

// Summary: When no message arrives before timeout, it shall return the timeout
// value. Description: This test never sends a message, so only the timeout path
// can complete the await. The `EXPECT_EQ(value, -1)` assertion confirms the
// timeout value is returned.
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
TEST(Actor, TryReceiveVoidTimeout) {
  agner::Scheduler scheduler;
  bool received = false;
  bool timed_out = false;
  auto actor = scheduler.spawn<TryReceiveVoidTimeout>(&received, &timed_out);

  scheduler.schedule_after(agner::Scheduler::Clock::duration::zero(), [actor] {
    actor->deliver_exit({agner::ExitReason::Kind::error}, actor);
  });
  scheduler.schedule_after(agner::Scheduler::Clock::duration::zero(), [actor] {
    actor->deliver_down({agner::ExitReason::Kind::error}, actor);
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
TEST(Actor, TryReceiveVoidTimeoutEarlyMessage) {
  agner::Scheduler scheduler;
  bool received = false;
  bool timed_out = false;
  auto actor = scheduler.spawn<TryReceiveVoidTimeout>(&received, &timed_out);

  scheduler.schedule_after(agner::Scheduler::Clock::duration::zero(),
                           [actor] { actor->send(Ping{11}); });

  scheduler.run();

  EXPECT_TRUE(received);
  EXPECT_FALSE(timed_out);
}

// Summary: When a message matches a secondary visitor, it shall return that
// visitor's result. Description: This test sends `Pong{21}` so only the second
// visitor matches and doubles the value. The `EXPECT_EQ(value, 42)` assertion
// confirms the selected visitor's result.
TEST(Actor, MultiVisitorCommonResult) {
  agner::Scheduler scheduler;
  int value = 0;
  auto actor = scheduler.spawn<MultiVisitorActor>(&value);

  actor->send(Pong{21});
  scheduler.run();

  EXPECT_EQ(value, 42);
}

// Summary: When a message matches the first visitor, it shall return the first
// visitor's result. Description: This test sends `Ping{9}` so the first visitor
// matches immediately. The `EXPECT_EQ(value, 9)` assertion confirms the first
// visitor's result is used.
TEST(Actor, MultiVisitorFirstMatch) {
  agner::Scheduler scheduler;
  int value = 0;
  auto actor = scheduler.spawn<MultiVisitorActor>(&value);

  actor->send(Ping{9});
  scheduler.run();

  EXPECT_EQ(value, 9);
}

// Summary: When multiple messages are sent, the actor shall process them in
// FIFO order. Description: This test sends `Ping{1}` then `Ping{2}` to the same
// actor in order. The assertions on `values[0]` and `values[1]` confirm FIFO
// message processing.
TEST(Actor, MailboxOrdering) {
  agner::Scheduler scheduler;
  std::vector<int> values;
  auto actor = scheduler.spawn<SequenceCollector>(&values);

  actor->send(Ping{1});
  actor->send(Ping{2});
  scheduler.run();

  ASSERT_EQ(values.size(), 2u);
  EXPECT_EQ(values[0], 1);
  EXPECT_EQ(values[1], 2);
}

// Summary: When no message is available, receive shall suspend until a message
// arrives. Description: This test runs the deterministic scheduler while no
// message is sent, so the receive remains suspended. After sending `Ping{11}`,
// the assertion on `value` confirms the receive resumed and captured the
// payload.
TEST(Actor, ReceiveSuspendsUntilMessage) {
  agner::DeterministicScheduler scheduler;
  std::optional<int> value;
  auto actor = scheduler.spawn<DeferredCollector>(&value);

  scheduler.run_until_idle();
  EXPECT_FALSE(value.has_value());

  actor->send(Ping{11});
  scheduler.run_until_idle();

  ASSERT_TRUE(value.has_value());
  EXPECT_EQ(*value, 11);
}

// Summary: When try-receive is pending, it shall return the message and avoid
// timing out. Description: This test runs idle with no message, ensuring
// try-receive has not completed or timed out. After sending `Ping{13}`, the
// assertions confirm a value was captured and `timed_out` stayed false.
TEST(Actor, TryReceiveSuspendsUntilMessage) {
  agner::DeterministicScheduler scheduler;
  std::optional<int> value;
  bool timed_out = false;
  auto actor = scheduler.spawn<DeferredTryReceive>(&value, &timed_out);

  scheduler.run_until_idle();
  EXPECT_FALSE(value.has_value());
  EXPECT_FALSE(timed_out);

  actor->send(Ping{13});
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
TEST(Actor, DeferredTryReceiveTimesOut) {
  agner::DeterministicScheduler scheduler;
  std::optional<int> value;
  bool timed_out = false;
  auto actor = scheduler.spawn<DeferredTryReceive>(&value, &timed_out);

  scheduler.run_until_idle();
  actor->deliver_exit({agner::ExitReason::Kind::error}, actor);
  actor->deliver_down({agner::ExitReason::Kind::error}, actor);

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
TEST(Actor, PendingSkipsUnmatchedSystemSignal) {
  agner::DeterministicScheduler scheduler;
  std::optional<int> value;
  auto actor = scheduler.spawn<DeferredCollector>(&value);

  scheduler.run_until_idle();

  actor->deliver_exit({agner::ExitReason::Kind::error}, actor);
  actor->deliver_down({agner::ExitReason::Kind::error}, actor);
  actor->send(Ping{99});
  scheduler.run_until_idle();

  ASSERT_TRUE(value.has_value());
  EXPECT_EQ(*value, 99);
}

// Summary: When a message arrives before timeout, the timeout shall be
// cancelled. Description: This test sends a `Ping{42}` after signals, so the
// message should satisfy the pending try-receive. The assertions confirm the
// value is set and `timed_out` remains false even after advancing time.
TEST(Actor, TryReceiveTimeoutCancelledWhenMessageArrives) {
  agner::DeterministicScheduler scheduler;
  std::optional<int> value;
  bool timed_out = false;
  auto actor = scheduler.spawn<DeferredTryReceive>(&value, &timed_out);

  scheduler.run_until_idle();

  actor->deliver_exit({agner::ExitReason::Kind::error}, actor);
  actor->deliver_down({agner::ExitReason::Kind::error}, actor);
  actor->send(Ping{42});
  scheduler.run_until_idle();

  ASSERT_TRUE(value.has_value());
  EXPECT_EQ(*value, 42);
  EXPECT_FALSE(timed_out);

  scheduler.run_for(agner::DeterministicScheduler::duration{10});
  EXPECT_FALSE(timed_out);
}

// Summary: When messages arrive after delayed signals, actors shall still
// process user messages and signals. Description: This test schedules delayed
// exit/down signals alongside user messages for multiple actors. The assertions
// on collected values and signal kinds confirm messages are still processed
// despite delayed signals.
TEST(Actor, ReceiveSuspendsWithDelayedSignals) {
  agner::Scheduler scheduler;
  int collector_value = 0;
  int multi_value = 0;
  std::vector<int> sequence_values;
  SignalCapture observer_capture;
  SignalCapture combined_capture;

  auto collector = scheduler.spawn<Collector>(&collector_value);
  auto worker = scheduler.spawn<Worker>();
  auto observer = scheduler.spawn<SignalObserver>(&observer_capture);
  auto multi = scheduler.spawn<MultiVisitorActor>(&multi_value);
  auto sequence = scheduler.spawn<SequenceCollector>(&sequence_values);
  auto stoppable = scheduler.spawn<Stoppable>();
  auto combined = scheduler.spawn<SignalObserver>(&combined_capture);

  scheduler.schedule_after(1ms, [collector] {
    collector->deliver_exit({agner::ExitReason::Kind::error}, collector);
  });
  scheduler.schedule_after(1ms, [collector] {
    collector->deliver_down({agner::ExitReason::Kind::error}, collector);
  });
  scheduler.schedule_after(2ms, [collector] { collector->send(Ping{5}); });

  scheduler.schedule_after(1ms, [worker] {
    worker->deliver_exit({agner::ExitReason::Kind::error}, worker);
  });
  scheduler.schedule_after(1ms, [worker] {
    worker->deliver_down({agner::ExitReason::Kind::error}, worker);
  });
  scheduler.schedule_after(2ms, [worker] { worker->send(Stop{}); });

  scheduler.schedule_after(1ms, [multi] {
    multi->deliver_exit({agner::ExitReason::Kind::error}, multi);
  });
  scheduler.schedule_after(1ms, [multi] {
    multi->deliver_down({agner::ExitReason::Kind::error}, multi);
  });
  scheduler.schedule_after(2ms, [multi] { multi->send(Pong{11}); });

  scheduler.schedule_after(1ms, [sequence] {
    sequence->deliver_exit({agner::ExitReason::Kind::error}, sequence);
  });
  scheduler.schedule_after(1ms, [sequence] {
    sequence->deliver_down({agner::ExitReason::Kind::error}, sequence);
  });
  scheduler.schedule_after(2ms, [sequence] { sequence->send(Ping{1}); });
  scheduler.schedule_after(3ms, [sequence] { sequence->send(Ping{2}); });

  scheduler.schedule_after(1ms, [observer] {
    observer->deliver_down({agner::ExitReason::Kind::error}, observer);
  });

  scheduler.schedule_after(1ms, [combined] {
    combined->deliver_exit({agner::ExitReason::Kind::normal}, combined);
  });

  scheduler.schedule_after(1ms, [stoppable] {
    stoppable->deliver_down({agner::ExitReason::Kind::error}, stoppable);
  });
  scheduler.schedule_after(2ms, [stoppable] {
    stoppable->deliver_exit({agner::ExitReason::Kind::normal}, stoppable);
  });

  scheduler.run();

  EXPECT_EQ(collector_value, 5);
  EXPECT_EQ(multi_value, 22);
  EXPECT_EQ(sequence_values, (std::vector<int>{1, 2}));
  ASSERT_TRUE(observer_capture.down_kind.has_value());
  EXPECT_EQ(*observer_capture.down_kind, agner::ExitReason::Kind::error);
  ASSERT_TRUE(combined_capture.exit_kind.has_value());
  EXPECT_EQ(*combined_capture.exit_kind, agner::ExitReason::Kind::normal);
}
