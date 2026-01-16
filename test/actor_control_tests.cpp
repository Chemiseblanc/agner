#include <gtest/gtest.h>

#include <memory>

#include "test_support.hpp"

namespace {

using namespace agner::test_support;

}  // namespace

// Summary: When a linked actor exits, it shall deliver an exit signal to the
// linker. Description: This test links an observer to a worker and stops the
// worker so it exits normally. The observer only learns about exits via the
// link, and the assertion on `capture.exit_kind` confirms the normal reason was
// delivered.
TEST(Actor, LinkReceivesExit) {
  agner::Scheduler scheduler;
  SignalCapture capture;
  auto worker = scheduler.spawn<Worker>();
  auto observer = scheduler.spawn<LinkedObserver>(worker, &capture);

  scheduler.run();

  worker->send(Stop{});
  scheduler.run();

  ASSERT_TRUE(capture.exit_kind.has_value());
  EXPECT_EQ(*capture.exit_kind, agner::ExitReason::Kind::normal);
}

// Summary: When a monitored actor exits, it shall deliver a down signal to the
// monitor. Description: This test monitors a worker, stops it to trigger a
// normal exit, and runs the scheduler. The down signal can only arrive through
// the monitor path, and the assertion on `capture.down_kind` confirms the
// reason is recorded.
TEST(Actor, MonitorReceivesDown) {
  agner::Scheduler scheduler;
  SignalCapture capture;
  auto worker = scheduler.spawn<Worker>();
  auto observer = scheduler.spawn<Observer>(worker, &capture);

  scheduler.run();

  worker->send(Stop{});
  scheduler.run();

  ASSERT_TRUE(capture.down_kind.has_value());
  EXPECT_EQ(*capture.down_kind, agner::ExitReason::Kind::normal);
}

// Summary: When a monitor control expires, exit handling shall ignore it
// safely. Description: This test monitors with a temporary control that is
// released before the worker exits. The test passes if no crash occurs,
// demonstrating expired monitors are ignored safely.
TEST(Actor, ExpiredMonitorIsIgnoredOnExit) {
  agner::Scheduler scheduler;
  auto worker = scheduler.spawn<Worker>();

  auto monitor = std::make_shared<DummyControl>();
  monitor->monitor(worker);
  monitor.reset();

  worker->send(Stop{});
  scheduler.run();
}

// Summary: When a link control expires, exit handling shall ignore it safely.
// Description:
// This test links a temporary control that is released before the worker exits.
// The test passes if no crash occurs, demonstrating expired links are ignored
// safely.
TEST(Actor, ExpiredLinkIsIgnoredOnExit) {
  agner::Scheduler scheduler;
  auto worker = scheduler.spawn<Worker>();

  auto link = std::make_shared<DummyControl>();
  worker->link(link);
  link.reset();

  worker->send(Stop{});
  scheduler.run();
}

// Summary: When a linked actor stops with a reason, it shall deliver the same
// exit reason. Description: This test links an observer, stops the worker with
// `stopped` reason, and runs the scheduler. The assertion on
// `capture.exit_kind` confirms the stopped reason is delivered through the
// link.
TEST(Actor, LinkReceivesStoppedReason) {
  agner::Scheduler scheduler;
  SignalCapture capture;
  auto worker = scheduler.spawn<Stoppable>();
  auto observer = scheduler.spawn<LinkedObserver>(worker, &capture);

  scheduler.run();

  worker->stop({agner::ExitReason::Kind::stopped});
  scheduler.run();

  ASSERT_TRUE(capture.exit_kind.has_value());
  EXPECT_EQ(*capture.exit_kind, agner::ExitReason::Kind::stopped);
}

// Summary: When a monitored actor stops with an error, it shall deliver a down
// signal with error reason. Description: This test monitors an actor and stops
// it with an error reason. The assertion on `capture.down_kind` confirms the
// error reason is delivered through monitoring.
TEST(Actor, MonitorReceivesErrorReason) {
  agner::Scheduler scheduler;
  SignalCapture capture;
  auto worker = scheduler.spawn<Stoppable>();
  auto observer = scheduler.spawn<Observer>(worker, &capture);

  scheduler.run();

  worker->stop({agner::ExitReason::Kind::error});
  scheduler.run();

  ASSERT_TRUE(capture.down_kind.has_value());
  EXPECT_EQ(*capture.down_kind, agner::ExitReason::Kind::error);
}

// Summary: When an actor is both linked and monitored, it shall deliver both
// exit and down signals. Description: This test both links and monitors a
// worker, then stops it to trigger normal exit. The assertions on `exit_kind`
// and `down_kind` confirm both signals were delivered.
TEST(Actor, LinkAndMonitorBothFire) {
  agner::Scheduler scheduler;
  SignalCapture capture;
  auto worker = scheduler.spawn<Worker>();
  auto observer = scheduler.spawn<LinkedObserver>(worker, &capture);

  scheduler.run();

  worker->send(Stop{});
  scheduler.run();

  ASSERT_TRUE(capture.exit_kind.has_value());
  ASSERT_TRUE(capture.down_kind.has_value());
  EXPECT_EQ(*capture.exit_kind, agner::ExitReason::Kind::normal);
  EXPECT_EQ(*capture.down_kind, agner::ExitReason::Kind::normal);
}

// Summary: When link or monitor is called with null, it shall be a no-op.
// Description:
// This test calls `link` and `monitor` with empty controls.
// The test passes if no crash occurs, confirming null operations are no-ops.
TEST(ActorControl, NullLinkAndMonitorNoOp) {
  agner::Scheduler scheduler;
  auto worker = scheduler.spawn<Worker>();

  worker->link({});
  worker->monitor({});
}

// Summary: When control operations are applied to each actor type, they shall
// accept link, monitor, and signals. Description: This test applies
// link/monitor and exit/down/stop operations to every actor type. The absence
// of assertions and failures confirms the operations are supported across
// actors.
TEST(ActorControl, OperationsCoverAllActors) {
  agner::Scheduler scheduler;
  agner::DeterministicScheduler deterministic;
  auto dummy = std::make_shared<DummyControl>();
  int value = 0;
  bool received = false;
  bool timed_out = false;
  int multi_value = 0;
  std::vector<int> sequence_values;
  SignalCapture capture;
  std::optional<int> deferred_value;
  bool deferred_timeout = false;

  auto collector = std::make_shared<Collector>(scheduler, &value);
  auto worker = std::make_shared<Worker>(scheduler);
  auto observer = std::make_shared<Observer>(scheduler, worker, &capture);
  auto signal_observer = std::make_shared<SignalObserver>(scheduler, &capture);
  auto timeout_receiver = std::make_shared<TimeoutReceiver>(scheduler, &value);
  auto try_receive_success =
      std::make_shared<TryReceiveSuccess>(scheduler, &value);
  auto try_receive_void =
      std::make_shared<TryReceiveVoidTimeout>(scheduler, &received, &timed_out);
  auto multi = std::make_shared<MultiVisitorActor>(scheduler, &multi_value);
  auto sequence =
      std::make_shared<SequenceCollector>(scheduler, &sequence_values);
  auto stoppable = std::make_shared<Stoppable>(scheduler);
  auto linked = std::make_shared<LinkedObserver>(scheduler, worker, &capture);
  auto deferred_collector =
      std::make_shared<DeferredCollector>(deterministic, &deferred_value);
  auto deferred_try = std::make_shared<DeferredTryReceive>(
      deterministic, &deferred_value, &deferred_timeout);

  const agner::ExitReason error{agner::ExitReason::Kind::error};
  const agner::ExitReason stopped{agner::ExitReason::Kind::stopped};

  std::vector<std::shared_ptr<agner::ActorControl>> actors{collector,
                                                           worker,
                                                           observer,
                                                           signal_observer,
                                                           timeout_receiver,
                                                           try_receive_success,
                                                           try_receive_void,
                                                           multi,
                                                           sequence,
                                                           stoppable,
                                                           linked};

  for (auto& actor : actors) {
    actor->link(dummy);
    dummy->monitor(actor);
    actor->deliver_exit(error, actor);
    actor->deliver_down(error, actor);
    actor->stop(stopped);
  }

  std::vector<std::shared_ptr<agner::ActorControl>> deterministic_actors{
      deferred_collector, deferred_try};

  for (auto& actor : deterministic_actors) {
    actor->link(dummy);
    dummy->monitor(actor);
    actor->deliver_exit(error, actor);
    actor->deliver_down(error, actor);
    actor->stop(stopped);
  }
}
