#include <gtest/gtest.h>

#include <string>
#include <type_traits>

#include "test_support.hpp"

namespace {

using namespace agner::test_support;

using agner::ActorRef;

}  // namespace

static_assert(std::is_same_v<
              decltype(std::declval<agner::Scheduler&>().spawn<Collector>(
                  static_cast<int*>(nullptr))),
              agner::ActorHandle<Collector>>);

static_assert(requires(agner::Scheduler& scheduler,
                       agner::ActorHandle<Collector> actor) {
  scheduler.send(actor, Ping{42});
});

static_assert(!agner::detail::MessageForActor<Collector, std::string>);

// Summary: When a linked actor exits, it shall deliver an exit signal to the
// linker. Description: This test links an observer to a worker and stops the
// worker so it exits normally. The observer only learns about exits via the
// link, and the assertion on `capture.exit_kind` confirms the normal reason was
// delivered.
// EARS: When link receives exit occurs, the actor component shall exhibit the expected behavior.
// Test method: This test drives the link receives exit scenario and asserts the observable outputs/state transitions. Justification: those assertions directly verify the requirement outcome.
TEST(Actor, LinkReceivesExit) {
  agner::Scheduler scheduler;
  SignalCapture capture;
  auto worker = scheduler.spawn<Worker>();
  auto observer = scheduler.spawn<LinkedObserver>(worker, &capture);

  (void)observer;

  scheduler.run();

  scheduler.send(worker, Stop{});
  scheduler.run();

  ASSERT_TRUE(capture.exit_kind.has_value());
  EXPECT_EQ(*capture.exit_kind, agner::ExitReason::Kind::normal);
}

// Summary: When a monitored actor exits, it shall deliver a down signal to the
// monitor. Description: This test monitors a worker, stops it to trigger a
// normal exit, and runs the scheduler. The down signal can only arrive through
// the monitor path, and the assertion on `capture.down_kind` confirms the
// reason is recorded.
// EARS: When monitor receives down occurs, the actor component shall exhibit the expected behavior.
// Test method: This test drives the monitor receives down scenario and asserts the observable outputs/state transitions. Justification: those assertions directly verify the requirement outcome.
TEST(Actor, MonitorReceivesDown) {
  agner::Scheduler scheduler;
  SignalCapture capture;
  auto worker = scheduler.spawn<Worker>();
  auto observer = scheduler.spawn<Observer>(worker, &capture);

  (void)observer;

  scheduler.run();

  scheduler.send(worker, Stop{});
  scheduler.run();

  ASSERT_TRUE(capture.down_kind.has_value());
  EXPECT_EQ(*capture.down_kind, agner::ExitReason::Kind::normal);
}

// Summary: When a linked actor stops with a reason, it shall deliver the same
// exit reason. Description: This test links an observer, stops the worker with
// `stopped` reason, and runs the scheduler. The assertion on
// `capture.exit_kind` confirms the stopped reason is delivered through the
// link.
// EARS: When link receives stopped reason occurs, the actor component shall exhibit the expected behavior.
// Test method: This test drives the link receives stopped reason scenario and asserts the observable outputs/state transitions. Justification: those assertions directly verify the requirement outcome.
TEST(Actor, LinkReceivesStoppedReason) {
  agner::Scheduler scheduler;
  SignalCapture capture;
  auto worker = scheduler.spawn<Stoppable>();
  auto observer = scheduler.spawn<LinkedObserver>(worker, &capture);

  (void)observer;

  scheduler.run();

  scheduler.stop(worker, {agner::ExitReason::Kind::stopped});
  scheduler.run();

  ASSERT_TRUE(capture.exit_kind.has_value());
  EXPECT_EQ(*capture.exit_kind, agner::ExitReason::Kind::stopped);
}

// Summary: When a monitored actor stops with an error, it shall deliver a down
// signal with error reason. Description: This test monitors an actor and stops
// it with an error reason. The assertion on `capture.down_kind` confirms the
// error reason is delivered through monitoring.
// EARS: When monitor receives error reason occurs, the actor component shall exhibit the expected behavior.
// Test method: This test drives the monitor receives error reason scenario and asserts the observable outputs/state transitions. Justification: those assertions directly verify the requirement outcome.
TEST(Actor, MonitorReceivesErrorReason) {
  agner::Scheduler scheduler;
  SignalCapture capture;
  auto worker = scheduler.spawn<Stoppable>();
  auto observer = scheduler.spawn<Observer>(worker, &capture);

  (void)observer;

  scheduler.run();

  scheduler.stop(worker, {agner::ExitReason::Kind::error});
  scheduler.run();

  ASSERT_TRUE(capture.down_kind.has_value());
  EXPECT_EQ(*capture.down_kind, agner::ExitReason::Kind::error);
}

// Summary: When an actor is both linked and monitored, it shall deliver both
// exit and down signals. Description: This test both links and monitors a
// worker, then stops it to trigger normal exit. The assertions on `exit_kind`
// and `down_kind` confirm both signals were delivered.
// EARS: When link and monitor both fire occurs, the actor component shall exhibit the expected behavior.
// Test method: This test drives the link and monitor both fire scenario and asserts the observable outputs/state transitions. Justification: those assertions directly verify the requirement outcome.
TEST(Actor, LinkAndMonitorBothFire) {
  agner::Scheduler scheduler;
  SignalCapture capture;
  auto worker = scheduler.spawn<Worker>();
  auto observer = scheduler.spawn<LinkedObserver>(worker, &capture);

  (void)observer;

  scheduler.run();

  scheduler.send(worker, Stop{});
  scheduler.run();

  ASSERT_TRUE(capture.exit_kind.has_value());
  ASSERT_TRUE(capture.down_kind.has_value());
  EXPECT_EQ(*capture.exit_kind, agner::ExitReason::Kind::normal);
  EXPECT_EQ(*capture.down_kind, agner::ExitReason::Kind::normal);
}

// Summary: When an actor is spawned with spawn_link, it shall be linked to the
// caller. Description: This test spawns a worker using spawn_link and stops it
// to trigger an exit signal. The assertion on `capture.exit_kind` confirms the
// link was established at spawn time.
// EARS: When spawn link establishes link occurs, the scheduler component shall exhibit the expected behavior.
// Test method: This test drives the spawn link establishes link scenario and asserts the observable outputs/state transitions. Justification: those assertions directly verify the requirement outcome.
TEST(Scheduler, SpawnLinkEstablishesLink) {
  agner::Scheduler scheduler;
  SignalCapture capture;
  auto observer = scheduler.spawn<LinkedObserver>(ActorRef{}, &capture);
  auto worker = scheduler.spawn_link<Worker>(observer);

  scheduler.run();
  scheduler.send(worker, Stop{});
  scheduler.run();

  ASSERT_TRUE(capture.exit_kind.has_value());
  EXPECT_EQ(*capture.exit_kind, agner::ExitReason::Kind::normal);
}

// Summary: When an actor is spawned with spawn_monitor, it shall be monitored
// by the caller. Description: This test spawns a worker using spawn_monitor and
// stops it to trigger a down signal. The assertion on `capture.down_kind`
// confirms the monitor was established at spawn time.
// EARS: When spawn monitor establishes monitor occurs, the scheduler component shall exhibit the expected behavior.
// Test method: This test drives the spawn monitor establishes monitor scenario and asserts the observable outputs/state transitions. Justification: those assertions directly verify the requirement outcome.
TEST(Scheduler, SpawnMonitorEstablishesMonitor) {
  agner::Scheduler scheduler;
  SignalCapture capture;
  auto observer = scheduler.spawn<Observer>(ActorRef{}, &capture);
  auto worker = scheduler.spawn_monitor<Worker>(observer);

  scheduler.run();
  scheduler.send(worker, Stop{});
  scheduler.run();

  ASSERT_TRUE(capture.down_kind.has_value());
  EXPECT_EQ(*capture.down_kind, agner::ExitReason::Kind::normal);
}

// Summary: When an untyped message is sent that matches no handler, it shall be
// silently dropped. Description: This test sends an std::any with a string type
// to an actor expecting Ping. The test passes if no crash occurs, confirming
// unmatched messages are silently dropped.
// EARS: When unmatched message is dropped occurs, the actor control component shall exhibit the expected behavior.
// Test method: This test drives the unmatched message is dropped scenario and asserts the observable outputs/state transitions. Justification: those assertions directly verify the requirement outcome.
TEST(ActorControl, UnmatchedMessageIsDropped) {
  agner::Scheduler scheduler;
  int value = 0;
  auto actor = scheduler.spawn<Collector>(&value);

  // Send a message type that doesn't match any handler
  scheduler.send(actor.ref(), std::string("wrong type"));

  // Send a valid message to complete the actor
  scheduler.send(actor, Ping{42});
  scheduler.run();

  EXPECT_EQ(value, 42);
}
