#include "agner/genserver.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <optional>

#include "deterministic_scheduler.hpp"
#include "test_support.hpp"

namespace {

using namespace std::chrono_literals;

// Request types
struct GetCount {};
struct Increment {
  int amount;
};
struct Reset {};
struct Add {
  int value;
};

// A counter GenServer with call and cast operations
template <typename SchedulerType>
class CounterServer
    : public agner::GenServer<SchedulerType, CounterServer<SchedulerType>,
                              agner::Handlers<int(GetCount), void(Increment),
                                              void(Reset), int(Add)>> {
 public:
  using Base = agner::GenServer<
      SchedulerType, CounterServer<SchedulerType>,
      agner::Handlers<int(GetCount), void(Increment), void(Reset), int(Add)>>;

  CounterServer(SchedulerType& scheduler, int initial = 0)
      : Base(scheduler), count_(initial) {}

  agner::task<void> run() { co_await this->serve(); }

  // Call handler: returns current count
  int handle(GetCount) { return count_; }

  // Call handler: adds value and returns new count
  int handle(Add request) {
    count_ += request.value;
    return count_;
  }

  // Cast handler: increments count by amount
  void handle(Increment request) { count_ += request.amount; }

  // Cast handler: resets count to zero
  void handle(Reset) { count_ = 0; }

 private:
  int count_ = 0;
};

// A client actor that makes calls to the counter server
template <typename SchedulerType>
class CounterClient
    : public agner::GenServer<SchedulerType, CounterClient<SchedulerType>,
                              agner::Handlers<int(GetCount), void(Increment),
                                              void(Reset), int(Add)>> {
 public:
  using Base = agner::GenServer<
      SchedulerType, CounterClient<SchedulerType>,
      agner::Handlers<int(GetCount), void(Increment), void(Reset), int(Add)>>;

  CounterClient(SchedulerType& scheduler, agner::ActorRef server, int* out)
      : Base(scheduler), server_(server), out_(out) {}

  agner::task<void> run() {
    // Make a call to get the count
    auto count = co_await this->call(server_, GetCount{}, 100ms);
    *out_ = count;
    co_return;
  }

 private:
  agner::ActorRef server_;
  int* out_;
};

// A client that performs multiple operations
template <typename SchedulerType>
class MultiOpClient
    : public agner::GenServer<SchedulerType, MultiOpClient<SchedulerType>,
                              agner::Handlers<int(GetCount), void(Increment),
                                              void(Reset), int(Add)>> {
 public:
  using Base = agner::GenServer<
      SchedulerType, MultiOpClient<SchedulerType>,
      agner::Handlers<int(GetCount), void(Increment), void(Reset), int(Add)>>;

  MultiOpClient(SchedulerType& scheduler, agner::ActorRef server, int* out)
      : Base(scheduler), server_(server), out_(out) {}

  agner::task<void> run() {
    // Cast: increment by 5
    this->cast(server_, Increment{5});

    // Call: add 10 and get result (discard result, we just want the side
    // effect)
    (void)co_await this->call(server_, Add{10}, 100ms);

    // Call: get final count
    auto final_count = co_await this->call(server_, GetCount{}, 100ms);

    *out_ = final_count;
    co_return;
  }

 private:
  agner::ActorRef server_;
  int* out_;
};

}  // namespace

// Summary: A GenServer call shall return the response from the handler.
// Description: This test spawns a CounterServer with initial value 42, then
// a client that calls GetCount. The assertion confirms the correct value
// is returned through the call mechanism.
// EARS: When call returns response occurs, the gen server component shall
// exhibit the expected behavior. Test method: This test drives the call returns
// response scenario and asserts the observable outputs/state transitions.
// Justification: those assertions directly verify the requirement outcome.
TEST(GenServer, CallReturnsResponse) {
  agner::Scheduler scheduler;
  int result = 0;

  auto server = scheduler.spawn<CounterServer<agner::Scheduler>>(42);
  scheduler.spawn<CounterClient<agner::Scheduler>>(server, &result);

  scheduler.run();

  EXPECT_EQ(result, 42);
}

// Summary: A GenServer cast shall update server state without blocking.
// Description: This test spawns a CounterServer with initial value 0, sends
// a cast to increment, then calls to verify the state was updated.
// EARS: When cast updates state occurs, the gen server component shall exhibit
// the expected behavior. Test method: This test drives the cast updates state
// scenario and asserts the observable outputs/state transitions. Justification:
// those assertions directly verify the requirement outcome.
TEST(GenServer, CastUpdatesState) {
  agner::Scheduler scheduler;
  int result = 0;

  auto server = scheduler.spawn<CounterServer<agner::Scheduler>>(0);
  scheduler.spawn<MultiOpClient<agner::Scheduler>>(server, &result);

  scheduler.run();

  // 0 + 5 (increment cast) + 10 (add call) = 15
  EXPECT_EQ(result, 15);
}

// Summary: Multiple calls to a GenServer shall return correct responses.
// Description: This test makes multiple sequential calls to verify the
// request/response correlation works correctly.
// EARS: When multiple calls occurs, the gen server component shall exhibit the
// expected behavior. Test method: This test drives the multiple calls scenario
// and asserts the observable outputs/state transitions. Justification: those
// assertions directly verify the requirement outcome.
TEST(GenServer, MultipleCalls) {
  agner::DeterministicScheduler scheduler;
  int result = 0;

  auto server =
      scheduler.spawn<CounterServer<agner::DeterministicScheduler>>(0);
  scheduler.spawn<MultiOpClient<agner::DeterministicScheduler>>(server,
                                                                &result);

  scheduler.run_until_idle();

  EXPECT_EQ(result, 15);
}

// Summary: When a GenServer receives an ExitSignal, it shall stop serving.
// Description: This test sends an ExitSignal to a running CounterServer, then
// verifies the server has exited by checking that a monitor observer receives a
// DownSignal. This confirms that serve()'s loop breaks on ExitSignal delivery.
// EARS: When a GenServer receives an ExitSignal while serving, the gen server
// component shall exit its serve loop and terminate. Test method: Monitor the
// server; send it an ExitSignal; run until idle; assert the observer receives a
// DownSignal with a normal exit kind.
TEST(GenServer, StopsOnExitSignalAndExits) {
  agner::DeterministicScheduler scheduler;
  agner::test_support::SignalCapture cap;

  auto server =
      scheduler.spawn<CounterServer<agner::DeterministicScheduler>>(0);
  // Observer uses monitor() inside run() to watch the server
  scheduler
      .spawn<agner::test_support::ObserverT<agner::DeterministicScheduler>>(
          server, &cap);

  scheduler.run_until_idle();

  // Stop the server with a stopped reason to trigger serve()'s ExitSignal path
  scheduler.stop(server, agner::ExitReason{agner::ExitReason::Kind::stopped});
  scheduler.run_until_idle();

  ASSERT_TRUE(cap.down_kind.has_value());
  EXPECT_EQ(*cap.down_kind, agner::ExitReason::Kind::stopped);
}

// Summary: A GenServer call shall throw CallTimeout when no reply is received.
// Description: This test uses the deterministic scheduler to avoid actually
// waiting. The client makes a call to a non-existent server and the timeout
// should trigger.
// EARS: When call timeout throws occurs, the gen server component shall exhibit
// the expected behavior. Test method: This test drives the call timeout throws
// scenario and asserts the observable outputs/state transitions. Justification:
// those assertions directly verify the requirement outcome.
TEST(GenServer, CallTimeoutThrows) {
  agner::DeterministicScheduler scheduler;
  bool threw_timeout = false;

  // Custom client that catches timeout
  class TimeoutTestClient
      : public agner::GenServer<agner::DeterministicScheduler,
                                TimeoutTestClient,
                                agner::Handlers<int(GetCount)>> {
   public:
    using Base =
        agner::GenServer<agner::DeterministicScheduler, TimeoutTestClient,
                         agner::Handlers<int(GetCount)>>;

    TimeoutTestClient(agner::DeterministicScheduler& scheduler,
                      agner::ActorRef server, bool* threw)
        : Base(scheduler), server_(server), threw_(threw) {}

    agner::task<void> run() {
      try {
        // Call to server that will never reply (invalid ref)
        // Use 5 ticks as timeout for deterministic scheduler
        co_await this->call(server_, GetCount{},
                            agner::DeterministicScheduler::duration{5});
      } catch (const agner::CallTimeout&) {
        *threw_ = true;
      }
      co_return;
    }

   private:
    agner::ActorRef server_;
    bool* threw_;
  };

  // Invalid ActorRef - no server to respond
  agner::ActorRef invalid_server{};
  scheduler.spawn<TimeoutTestClient>(invalid_server, &threw_timeout);

  // Run until idle, then advance time past timeout
  scheduler.run_until_idle();
  scheduler.run_for(agner::DeterministicScheduler::duration{10});

  EXPECT_TRUE(threw_timeout);
}

// Summary: GenServer serve() shall ignore Reply messages.
// Description: This test sends a Reply message directly to a server running
// serve() to verify it is ignored and the server continues processing.
// EARS: When serve ignores reply messages occurs, the gen server component
// shall exhibit the expected behavior. Test method: This test drives the serve
// ignores reply messages scenario and asserts the observable outputs/state
// transitions. Justification: those assertions directly verify the requirement
// outcome.
TEST(GenServer, ServeIgnoresReplyMessages) {
  agner::DeterministicScheduler scheduler;
  int result = 0;

  auto server =
      scheduler.spawn<CounterServer<agner::DeterministicScheduler>>(42);

  // Start a client to verify server is working
  class ReplyTestClient
      : public agner::GenServer<agner::DeterministicScheduler, ReplyTestClient,
                                agner::Handlers<int(GetCount)>> {
   public:
    using Base =
        agner::GenServer<agner::DeterministicScheduler, ReplyTestClient,
                         agner::Handlers<int(GetCount)>>;

    ReplyTestClient(agner::DeterministicScheduler& scheduler,
                    agner::ActorRef server, int* out)
        : Base(scheduler), server_(server), out_(out) {}

    agner::task<void> run() {
      // Send a bogus Reply to the server - it should be ignored
      agner::Reply bogus_reply{999, std::any(123)};
      this->send(server_, bogus_reply);

      // Now make a real call - it should still work
      auto count = co_await this->call(
          server_, GetCount{}, agner::DeterministicScheduler::duration{100});
      *out_ = count;
      co_return;
    }

   private:
    agner::ActorRef server_;
    int* out_;
  };

  scheduler.spawn<ReplyTestClient>(server, &result);
  scheduler.run_until_idle();

  EXPECT_EQ(result, 42);
}

// Summary: GenServer call shall timeout when receiving wrong request_id.
// Description: This test sends a Reply with an incorrect request_id to a
// client, verifying that mismatched replies cause the call to timeout.
// Summary: A GenServer call shall skip replies with wrong request_id.
// Description: This test verifies that when a call receives a reply with a
// different request_id (e.g., a stale reply from a previous call), it ignores
// that reply and continues waiting for the correct one.
// EARS: When call skips wrong request id occurs, the gen server component shall
// exhibit the expected behavior. Test method: This test drives the call skips
// wrong request id scenario and asserts the observable outputs/state
// transitions. Justification: those assertions directly verify the requirement
// outcome.
TEST(GenServer, CallSkipsWrongRequestId) {
  agner::DeterministicScheduler scheduler;
  int result = -1;

  // Create a server that will return 99
  auto server =
      scheduler.spawn<CounterServer<agner::DeterministicScheduler>>(99);

  class WrongIdTestClient
      : public agner::GenServer<agner::DeterministicScheduler,
                                WrongIdTestClient,
                                agner::Handlers<int(GetCount)>> {
   public:
    using Base =
        agner::GenServer<agner::DeterministicScheduler, WrongIdTestClient,
                         agner::Handlers<int(GetCount)>>;

    WrongIdTestClient(agner::DeterministicScheduler& scheduler,
                      agner::ActorRef server, int* result)
        : Base(scheduler), server_(server), result_(result) {}

    agner::task<void> run() {
      // Send ourselves a reply with a wrong request_id
      // This simulates receiving a stale reply from a previous call
      agner::Reply wrong_reply{12345, std::any(777)};
      this->send(this->self(), wrong_reply);

      // This call should skip the wrong reply and get the correct one
      *result_ = co_await this->call(
          server_, GetCount{}, agner::DeterministicScheduler::duration{100});
      co_return;
    }

   private:
    agner::ActorRef server_;
    int* result_;
  };

  scheduler.spawn<WrongIdTestClient>(server, &result);
  scheduler.run_until_idle();

  // Should have received the correct response (99) from server
  EXPECT_EQ(result, 99);
}

// Summary: A GenServer call shall enforce one overall timeout deadline.
// Description: This test injects multiple wrong-id replies before the timeout
// while calling an invalid server. The call must still timeout at the original
// deadline instead of extending the timeout after each wrong-id reply.
// EARS: When call timeout uses overall deadline occurs, the gen server
// component shall exhibit the expected behavior. Test method: This test drives
// the call timeout uses overall deadline scenario and asserts the observable
// outputs/state transitions. Justification: those assertions directly verify
// the requirement outcome.
TEST(GenServer, CallTimeoutUsesOverallDeadline) {
  agner::DeterministicScheduler scheduler;
  bool threw_timeout = false;

  class TimeoutDeadlineClient
      : public agner::GenServer<agner::DeterministicScheduler,
                                TimeoutDeadlineClient,
                                agner::Handlers<int(GetCount)>> {
   public:
    using Base =
        agner::GenServer<agner::DeterministicScheduler, TimeoutDeadlineClient,
                         agner::Handlers<int(GetCount)>>;

    TimeoutDeadlineClient(agner::DeterministicScheduler& scheduler,
                          agner::ActorRef server, bool* threw_timeout)
        : Base(scheduler), server_(server), threw_timeout_(threw_timeout) {}

    agner::task<void> run() {
      auto self_ref = this->self();
      auto* scheduler = &this->scheduler();
      for (int tick = 1; tick <= 4; ++tick) {
        scheduler->schedule_after(
            agner::DeterministicScheduler::duration{tick},
            [self_ref, scheduler, tick] {
              scheduler->send(self_ref,
                              agner::Reply{1000 + static_cast<uint64_t>(tick),
                                           std::any(777)});
            });
      }

      try {
        co_await this->call(server_, GetCount{},
                            agner::DeterministicScheduler::duration{5});
      } catch (const agner::CallTimeout&) {
        *threw_timeout_ = true;
      }
      co_return;
    }

   private:
    agner::ActorRef server_;
    bool* threw_timeout_;
  };

  agner::ActorRef invalid_server{};
  scheduler.spawn<TimeoutDeadlineClient>(invalid_server, &threw_timeout);

  scheduler.run_until_idle();
  scheduler.run_for(agner::DeterministicScheduler::duration{4});
  EXPECT_FALSE(threw_timeout);
  scheduler.run_for(agner::DeterministicScheduler::duration{2});

  EXPECT_TRUE(threw_timeout);
}

// Summary: A GenServer call shall throw CallTimeout when remaining time is
// already negative before entering try_receive.
// Description: This test uses a zero-duration timeout and pre-queues a wrong-id
// reply. When call() processes the wrong-id reply, the loop re-checks remaining
// time. With the real scheduler, wall clock time has advanced past the zero
// deadline, so the negative-remaining guard fires before entering try_receive
// again. The assertion confirms CallTimeout is thrown.
// EARS: When call remaining time is already negative before entering
// try_receive occurs, the gen server component shall throw CallTimeout.
// Test method: This test drives the call timeout on negative remaining scenario
// and asserts CallTimeout is thrown. Justification: the assertion directly
// verifies the requirement outcome.
TEST(GenServer, CallTimeoutOnNegativeRemaining) {
  agner::Scheduler scheduler;
  bool threw_timeout = false;

  class NegativeRemainingClient
      : public agner::GenServer<agner::Scheduler, NegativeRemainingClient,
                                agner::Handlers<int(GetCount)>> {
   public:
    using Base =
        agner::GenServer<agner::Scheduler, NegativeRemainingClient,
                         agner::Handlers<int(GetCount)>>;

    NegativeRemainingClient(agner::Scheduler& scheduler,
                            agner::ActorRef server, bool* threw)
        : Base(scheduler), server_(server), threw_(threw) {}

    agner::task<void> run() {
      // Pre-queue a wrong-id reply so try_receive matches it immediately
      agner::Reply wrong_reply{99999, std::any(777)};
      this->send(this->self(), wrong_reply);

      try {
        // Zero timeout: deadline = now(). After processing the wrong-id reply,
        // the loop re-checks remaining = deadline - now(). Since wall clock
        // has advanced, remaining < 0 and the guard on L116-117 fires.
        co_await this->call(server_, GetCount{},
                            std::chrono::steady_clock::duration::zero());
      } catch (const agner::CallTimeout&) {
        *threw_ = true;
      }
      co_return;
    }

   private:
    agner::ActorRef server_;
    bool* threw_;
  };

  auto server = scheduler.spawn<CounterServer<agner::Scheduler>>(42);
  scheduler.spawn<NegativeRemainingClient>(server, &threw_timeout);

  scheduler.run();

  EXPECT_TRUE(threw_timeout);
}
