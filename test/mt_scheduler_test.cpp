#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#include "agner/actor.hpp"
#include "agner/mt_scheduler.hpp"
#include "test_support.hpp"

namespace {

using namespace std::chrono_literals;
using namespace agner;

struct Ping {
  int value;
};

struct Pong {
  int value;
};

struct Stop {};

// Simple collector: receives one Ping and stores the value.
class MtCollector
    : public Actor<MtScheduler, MtCollector, Messages<Ping>> {
 public:
  MtCollector(MtScheduler& scheduler, std::atomic<int>* out)
      : Actor(scheduler), out_(out) {}

  task<void> run() {
    auto value = co_await receive([](Ping& p) { return p.value; });
    out_->store(value, std::memory_order_relaxed);
    co_return;
  }

 private:
  std::atomic<int>* out_;
};

// Echoer: receives a Ping, sends back a Pong.
class Echoer
    : public Actor<MtScheduler, Echoer, Messages<Ping>> {
 public:
  using Actor::Actor;

  task<void> run() {
    co_await receive([&](Ping& p) { send(p.value != 0 ? ActorRef{static_cast<uint64_t>(p.value)} : self(), Pong{p.value}); });
    co_return;
  }
};

// PingPonger: does N rounds of ping-pong with a peer.
class PingPonger
    : public Actor<MtScheduler, PingPonger, Messages<Ping, Pong>> {
 public:
  PingPonger(MtScheduler& scheduler, int rounds, std::atomic<int>* count)
      : Actor(scheduler), rounds_(rounds), count_(count) {}

  task<void> run() {
    // Receive initial ping with peer ref encoded in value.
    auto peer_val = co_await receive(
        [](Ping& p) { return p.value; },
        [](Pong& p) { return p.value; });
    ActorRef peer{static_cast<uint64_t>(peer_val)};
    for (int i = 0; i < rounds_; ++i) {
      send(peer, Ping{static_cast<int>(self().value)});
      co_await receive(
          [](Ping&) {},
          [](Pong&) {});
      count_->fetch_add(1, std::memory_order_relaxed);
    }
    co_return;
  }

 private:
  int rounds_;
  std::atomic<int>* count_;
};

// Worker: waits for Stop or ExitSignal.
class MtWorker
    : public Actor<MtScheduler, MtWorker, Messages<Stop>> {
 public:
  using Actor::Actor;

  task<void> run() {
    co_await receive(
        [](Stop&) {},
        [](ExitSignal&) {});
    co_return;
  }
};

// Observer that captures signal kind via shared atomic.
class MtSignalObserver
    : public Actor<MtScheduler, MtSignalObserver, Messages<>> {
 public:
  MtSignalObserver(MtScheduler& scheduler, std::atomic<int>* signal_kind)
      : Actor(scheduler), signal_kind_(signal_kind) {}

  task<void> run() {
    co_await receive(
        [&](ExitSignal& s) {
          signal_kind_->store(static_cast<int>(s.reason.kind),
                              std::memory_order_relaxed);
        },
        [&](DownSignal& s) {
          signal_kind_->store(static_cast<int>(s.reason.kind) + 100,
                              std::memory_order_relaxed);
        });
    co_return;
  }

 private:
  std::atomic<int>* signal_kind_;
};

// Thrower: throws an exception to test error exit propagation.
class Thrower
    : public Actor<MtScheduler, Thrower, Messages<Ping>> {
 public:
  using Actor::Actor;

  task<void> run() {
    co_await receive([](Ping&) {});
    throw std::runtime_error("intentional");
    co_return;
  }
};

// StressActor: receives N pings, counting them.
class StressActor
    : public Actor<MtScheduler, StressActor, Messages<Ping>> {
 public:
  StressActor(MtScheduler& scheduler, int expected, std::atomic<int>* count)
      : Actor(scheduler), expected_(expected), count_(count) {}

  task<void> run() {
    for (int i = 0; i < expected_; ++i) {
      co_await receive([&](Ping&) {
        count_->fetch_add(1, std::memory_order_relaxed);
      });
    }
    co_return;
  }

 private:
  int expected_;
  std::atomic<int>* count_;
};

// TimeoutActor: tests try_receive timeout.
class MtTimeoutActor
    : public Actor<MtScheduler, MtTimeoutActor, Messages<Ping>> {
 public:
  MtTimeoutActor(MtScheduler& scheduler, std::atomic<int>* out)
      : Actor(scheduler), out_(out) {}

  task<void> run() {
    auto result = co_await try_receive(
        1ms,
        [] { return -1; },
        [](Ping& p) { return p.value; });
    out_->store(result, std::memory_order_relaxed);
    co_return;
  }

 private:
  std::atomic<int>* out_;
};

task<void> detached_work_item(MtScheduler& scheduler,
                              ActorRef keeper,
                              std::thread::id owner_thread,
                              std::atomic<int>* executed,
                              std::atomic<int>* stolen,
                              int total) {
  std::this_thread::sleep_for(50us);

  if (std::this_thread::get_id() != owner_thread) {
    stolen->store(1, std::memory_order_relaxed);
  }

  auto now_done = executed->fetch_add(1, std::memory_order_relaxed) + 1;
  if (now_done == total) {
    scheduler.send(keeper, Stop{});
  }
  co_return;
}

// Schedules many detached tasks from a worker thread to exercise local queue
// push and steal paths.
class MtStealProducer
    : public Actor<MtScheduler, MtStealProducer, Messages<Ping>> {
 public:
  MtStealProducer(MtScheduler& scheduler,
                  ActorRef keeper,
                  int item_count,
                  std::atomic<int>* executed,
                  std::atomic<int>* stolen)
      : Actor(scheduler),
        keeper_(keeper),
        item_count_(item_count),
        executed_(executed),
        stolen_(stolen) {}

  task<void> run() {
    co_await receive([&](Ping&) {
      auto owner_thread = std::this_thread::get_id();
      for (int i = 0; i < item_count_; ++i) {
        auto item = detached_work_item(
            scheduler(), keeper_, owner_thread, executed_, stolen_, item_count_);
        item.detach(scheduler());
      }
    });
    co_return;
  }

 private:
  ActorRef keeper_;
  int item_count_;
  std::atomic<int>* executed_;
  std::atomic<int>* stolen_;
};

}  // namespace

// Summary: Basic spawn and message delivery across threads.
// Description: Spawns a collector on the MtScheduler, sends it a Ping, and
// verifies the value is received. Validates basic multi-threaded scheduling.
TEST(MtScheduler, SpawnAndReceive) {
  MtScheduler scheduler(2);
  std::atomic<int> value{0};
  auto actor = scheduler.spawn<MtCollector>(&value);
  scheduler.send(actor, Ping{42});
  scheduler.run();
  EXPECT_EQ(value.load(), 42);
}

// Summary: Multiple actors run concurrently and all complete.
// Description: Spawns several collectors, sends each a message, and checks all
// received their values.
TEST(MtScheduler, MultipleActors) {
  MtScheduler scheduler(4);
  constexpr int count = 8;
  std::atomic<int> values[count];
  std::vector<ActorRef> actors;
  for (int i = 0; i < count; ++i) {
    values[i].store(0);
    actors.push_back(scheduler.spawn<MtCollector>(&values[i]));
  }
  for (int i = 0; i < count; ++i) {
    scheduler.send(actors[i], Ping{i + 1});
  }
  scheduler.run();
  for (int i = 0; i < count; ++i) {
    EXPECT_EQ(values[i].load(), i + 1);
  }
}

// Summary: Timer-based schedule_after fires callback and resumes actor.
// Description: Spawns an actor that uses try_receive with a 1ms timeout.
// No message is sent, so the timeout fires and returns -1.
TEST(MtScheduler, ScheduleAfterTimeout) {
  MtScheduler scheduler(2);
  std::atomic<int> value{0};
  scheduler.spawn<MtTimeoutActor>(&value);
  scheduler.run();
  EXPECT_EQ(value.load(), -1);
}

// Summary: Monitor delivers DownSignal when monitored actor exits.
// Description: Spawns an observer, then spawns a worker monitored by the
// observer. Stops the worker and checks the observer receives a DownSignal.
TEST(MtScheduler, MonitorSignal) {
  MtScheduler scheduler(2);
  std::atomic<int> signal_kind{-1};
  auto observer = scheduler.spawn<MtSignalObserver>(&signal_kind);
  auto worker = scheduler.spawn_monitor<MtWorker>(observer);
  scheduler.schedule_after(10ms, [&scheduler, worker] {
    scheduler.stop(worker);
  });
  scheduler.run();
  // 100 + normal(0) = 100 for DownSignal with normal kind.
  EXPECT_EQ(signal_kind.load(), 100);
}

// Summary: Link delivers ExitSignal when linked actor exits.
// Description: Spawns a signal observer, then spawns a worker linked to it.
// Stops the worker and checks the observer receives an ExitSignal.
TEST(MtScheduler, LinkSignal) {
  MtScheduler scheduler(2);
  std::atomic<int> signal_kind{-1};
  auto observer = scheduler.spawn<MtSignalObserver>(&signal_kind);
  auto worker = scheduler.spawn_link<MtWorker>(observer);
  scheduler.schedule_after(10ms, [&scheduler, worker] {
    scheduler.stop(worker);
  });
  scheduler.run();
  // ExitSignal with normal kind = 0 (stop() default).
  EXPECT_EQ(signal_kind.load(), static_cast<int>(ExitReason::Kind::normal));
}

// Summary: Exception in actor run() sets error exit reason and signals linked.
// Description: Spawns a signal observer, then spawns a Thrower linked to it.
// Sends a Ping to unblock the Thrower which then throws. Observer receives
// ExitSignal with error kind.
TEST(MtScheduler, ExceptionPropagation) {
  MtScheduler scheduler(2);
  std::atomic<int> signal_kind{-1};
  auto observer = scheduler.spawn<MtSignalObserver>(&signal_kind);
  auto thrower = scheduler.spawn_link<Thrower>(observer);
  scheduler.send(thrower, Ping{1});
  scheduler.run();
  EXPECT_EQ(signal_kind.load(), static_cast<int>(ExitReason::Kind::error));
}

// Summary: Stress test — many actors, many messages, all complete.
// Description: Spawns many actors each expecting several messages. Sends all
// messages and verifies the total count matches.
TEST(MtScheduler, StressTest) {
  constexpr int num_actors = 32;
  constexpr int msgs_per_actor = 50;
  MtScheduler scheduler(4);

  std::atomic<int> total_count{0};
  std::vector<ActorRef> actors;
  for (int i = 0; i < num_actors; ++i) {
    actors.push_back(scheduler.spawn<StressActor>(msgs_per_actor, &total_count));
  }
  for (int j = 0; j < msgs_per_actor; ++j) {
    for (int i = 0; i < num_actors; ++i) {
      scheduler.send(actors[i], Ping{1});
    }
  }
  scheduler.run();
  EXPECT_EQ(total_count.load(), num_actors * msgs_per_actor);
}

// Summary: Scheduler shuts down cleanly when no actors are spawned.
// Description: Creating a scheduler and calling run() with no work should
// return immediately without hanging.
TEST(MtScheduler, EmptyRun) {
  MtScheduler scheduler(2);
  scheduler.run();
}

// Summary: Null coroutine handles are ignored by schedule().
// Description: Schedules a null handle, then runs with no actors to verify the
// guard path is exercised and execution returns immediately.
TEST(MtScheduler, ScheduleIgnoresNullHandle) {
  MtScheduler scheduler(2);
  scheduler.schedule(std::coroutine_handle<>{});
  scheduler.run();
}

// Summary: Stop request is delivered to a running actor.
// Description: Spawns a worker, waits briefly, then stops it. Worker should
// exit and the scheduler should shut down cleanly.
TEST(MtScheduler, StopActor) {
  MtScheduler scheduler(2);
  auto worker = scheduler.spawn<MtWorker>();
  scheduler.schedule_after(5ms, [&scheduler, worker] {
    scheduler.send(worker, Stop{});
  });
  scheduler.run();
}

// Summary: Detached work enqueued by one worker can be stolen by another.
// Description: A producer actor schedules many detached work items from a
// worker thread. Another worker must steal and execute at least one item.
TEST(MtScheduler, WorkerStealsDetachedWorkFromPeerQueue) {
  constexpr int item_count = 256;

  MtScheduler scheduler(4);
  std::atomic<int> executed{0};
  std::atomic<int> stolen{0};

  auto keeper = scheduler.spawn<MtWorker>();
  auto producer =
      scheduler.spawn<MtStealProducer>(keeper, item_count, &executed, &stolen);
  scheduler.send(producer, Ping{1});

  scheduler.run();

  EXPECT_EQ(executed.load(std::memory_order_relaxed), item_count);
  EXPECT_EQ(stolen.load(std::memory_order_relaxed), 1);
}
