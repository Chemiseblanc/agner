#include "agner/gen_event.hpp"

#include <gtest/gtest.h>

#include <stdexcept>
#include <vector>

#include "deterministic_scheduler.hpp"
#include "test_support.hpp"

namespace {

struct Tick {
  int value;
};

template <typename SchedulerType>
class TickManager
    : public agner::GenEvent<SchedulerType, TickManager<SchedulerType>,
                             agner::EventHandlers<Tick>> {
 public:
  using Base = agner::GenEvent<SchedulerType, TickManager<SchedulerType>,
                               agner::EventHandlers<Tick>>;
  using Base::Base;

  agner::task<void> run() {
    co_await this->serve();
    co_return;
  }
};

struct RecordingHandler {
  std::vector<int>* values;

  void operator()(const Tick& tick) { values->push_back(tick.value); }
};

struct OrderedHandler {
  std::vector<int>* order;
  int marker = 0;

  void operator()(const Tick&) { order->push_back(marker); }
};

struct FailingHandler {
  void operator()(const Tick&) { throw std::runtime_error("handler failure"); }
};

template <typename SchedulerType>
class AddRemoveClient
    : public agner::GenEvent<SchedulerType, AddRemoveClient<SchedulerType>,
                             agner::EventHandlers<Tick>> {
 public:
  using Base = agner::GenEvent<SchedulerType, AddRemoveClient<SchedulerType>,
                               agner::EventHandlers<Tick>>;

  AddRemoveClient(SchedulerType& scheduler, agner::ActorRef manager,
                  std::vector<int>* values)
      : Base(scheduler), manager_(manager), values_(values) {}

  agner::task<void> run() {
    auto handler_ref = this->add_handler(manager_, RecordingHandler{values_});
    this->notify(manager_, Tick{1});
    this->remove_handler(manager_, handler_ref);
    this->notify(manager_, Tick{2});
    co_return;
  }

 private:
  agner::ActorRef manager_{};
  std::vector<int>* values_;
};

template <typename SchedulerType>
class OrderedNotifyClient
    : public agner::GenEvent<SchedulerType, OrderedNotifyClient<SchedulerType>,
                             agner::EventHandlers<Tick>> {
 public:
  using Base =
      agner::GenEvent<SchedulerType, OrderedNotifyClient<SchedulerType>,
                      agner::EventHandlers<Tick>>;

  OrderedNotifyClient(SchedulerType& scheduler, agner::ActorRef manager,
                      std::vector<int>* order)
      : Base(scheduler), manager_(manager), order_(order) {}

  agner::task<void> run() {
    this->add_handler(manager_, OrderedHandler{order_, 1});
    this->add_handler(manager_, OrderedHandler{order_, 2});
    this->notify(manager_, Tick{0});
    co_return;
  }

 private:
  agner::ActorRef manager_{};
  std::vector<int>* order_;
};

template <typename SchedulerType>
class FailingNotifyClient
    : public agner::GenEvent<SchedulerType, FailingNotifyClient<SchedulerType>,
                             agner::EventHandlers<Tick>> {
 public:
  using Base =
      agner::GenEvent<SchedulerType, FailingNotifyClient<SchedulerType>,
                      agner::EventHandlers<Tick>>;

  FailingNotifyClient(SchedulerType& scheduler, agner::ActorRef manager)
      : Base(scheduler), manager_(manager) {}

  agner::task<void> run() {
    this->add_handler(manager_, FailingHandler{});
    this->notify(manager_, Tick{1});
    co_return;
  }

 private:
  agner::ActorRef manager_{};
};

}  // namespace

// Summary: A GenEvent manager shall stop delivering to removed handlers.
// Description: This test registers one handler, sends a notify, removes the
// handler, then sends another notify. Only the first event should be observed.
// EARS: When remove handler occurs, the gen event component shall stop
// delivering subsequent events to that handler.
TEST(GenEvent, AddRemoveHandlerControlsDelivery) {
  agner::DeterministicScheduler scheduler;
  std::vector<int> values;

  auto manager = scheduler.spawn<TickManager<agner::DeterministicScheduler>>();
  scheduler.spawn<AddRemoveClient<agner::DeterministicScheduler>>(manager,
                                                                  &values);

  scheduler.run_until_idle();

  ASSERT_EQ(values.size(), 1u);
  EXPECT_EQ(values[0], 1);
}

// Summary: A GenEvent manager shall notify handlers in registration order.
// Description: This test registers two handlers and sends one event. The
// observed callback order must match registration order.
// EARS: When notify fanout occurs, the gen event component shall deliver in
// registration order.
TEST(GenEvent, NotifyFanoutUsesRegistrationOrder) {
  agner::DeterministicScheduler scheduler;
  std::vector<int> order;

  auto manager = scheduler.spawn<TickManager<agner::DeterministicScheduler>>();
  scheduler.spawn<OrderedNotifyClient<agner::DeterministicScheduler>>(manager,
                                                                      &order);

  scheduler.run_until_idle();

  ASSERT_EQ(order.size(), 2u);
  EXPECT_EQ(order[0], 1);
  EXPECT_EQ(order[1], 2);
}

// Summary: A GenEvent manager shall crash when a handler throws.
// Description: This test monitors the manager, registers a throwing handler,
// and notifies once. The observer should receive an error DownSignal.
// EARS: When handler failure occurs, the gen event component shall terminate
// with an error exit reason.
TEST(GenEvent, HandlerFailureStopsManagerWithError) {
  agner::DeterministicScheduler scheduler;
  agner::test_support::SignalCapture capture;

  auto manager = scheduler.spawn<TickManager<agner::DeterministicScheduler>>();
  scheduler
      .spawn<agner::test_support::ObserverT<agner::DeterministicScheduler>>(
          manager, &capture);
  scheduler.run_until_idle();

  scheduler.spawn<FailingNotifyClient<agner::DeterministicScheduler>>(manager);
  scheduler.run_until_idle();

  ASSERT_TRUE(capture.down_kind.has_value());
  EXPECT_EQ(*capture.down_kind, agner::ExitReason::Kind::error);
}

// Summary: A GenEvent manager shall exit its serve loop on ExitSignal.
// Description: This test monitors the manager, requests stop(), and verifies a
// stopped DownSignal is observed.
// EARS: When exit signal occurs, the gen event component shall terminate
// cleanly with the provided stop reason.
TEST(GenEvent, StopsOnExitSignalAndExits) {
  agner::DeterministicScheduler scheduler;
  agner::test_support::SignalCapture capture;

  auto manager = scheduler.spawn<TickManager<agner::DeterministicScheduler>>();
  scheduler
      .spawn<agner::test_support::ObserverT<agner::DeterministicScheduler>>(
          manager, &capture);
  scheduler.run_until_idle();

  scheduler.stop(manager, agner::ExitReason{agner::ExitReason::Kind::stopped});
  scheduler.run_until_idle();

  ASSERT_TRUE(capture.down_kind.has_value());
  EXPECT_EQ(*capture.down_kind, agner::ExitReason::Kind::stopped);
}
