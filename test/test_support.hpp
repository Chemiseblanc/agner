#pragma once

#include <chrono>
#include <coroutine>
#include <exception>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#include "agner/actor.hpp"
#include "deterministic_scheduler.hpp"

namespace agner::test_support {

using agner::ActorRef;

struct Ping {
  int value;
};

struct Pong {
  int value;
};

struct Stop {};

struct SignalCapture {
  std::optional<ExitReason::Kind> exit_kind;
  std::optional<ExitReason::Kind> down_kind;
};

inline ExitSignal make_exit_signal(ExitReason::Kind kind) {
  return ExitSignal{ActorRef{}, ExitReason{kind}};
}

inline DownSignal make_down_signal(ExitReason::Kind kind) {
  return DownSignal{ActorRef{}, ExitReason{kind}};
}

template <typename Derived, typename MessagePack>
using TestActor = Actor<Scheduler, Derived, MessagePack>;

template <typename Derived, typename MessagePack>
using DeterministicActor = Actor<DeterministicScheduler, Derived, MessagePack>;

class Collector : public TestActor<Collector, Messages<Ping>> {
 public:
  Collector(Scheduler& scheduler, int* out) : Actor(scheduler), out_(out) {}

  task<void> run() {
    auto value = co_await receive([&](Ping& ping) { return ping.value; });
    *out_ = value;
    co_return;
  }

 private:
  int* out_;
};

class Worker : public TestActor<Worker, Messages<Stop>> {
 public:
  using Actor::Actor;

  task<void> run() {
    co_await receive([&](Stop&) {});
    co_return;
  }
};

class Observer : public TestActor<Observer, Messages<>> {
 public:
  Observer(Scheduler& scheduler, ActorRef target, SignalCapture* capture)
      : Actor(scheduler), target_(target), capture_(capture) {}

  task<void> run() {
    monitor(target_);
    co_await receive(
        [&](ExitSignal& signal) { capture_->exit_kind = signal.reason.kind; },
        [&](DownSignal& signal) { capture_->down_kind = signal.reason.kind; });
    co_return;
  }

 private:
  ActorRef target_{};
  SignalCapture* capture_;
};

class SignalObserver : public TestActor<SignalObserver, Messages<>> {
 public:
  SignalObserver(Scheduler& scheduler, SignalCapture* capture)
      : Actor(scheduler), capture_(capture) {}

  task<void> run() {
    co_await receive(
        [&](ExitSignal& signal) { capture_->exit_kind = signal.reason.kind; },
        [&](DownSignal& signal) { capture_->down_kind = signal.reason.kind; });
    co_return;
  }

 private:
  SignalCapture* capture_;
};

class LinkedObserver : public TestActor<LinkedObserver, Messages<>> {
 public:
  LinkedObserver(Scheduler& scheduler, ActorRef target, SignalCapture* capture)
      : Actor(scheduler), target_(target), capture_(capture) {}

  task<void> run() {
    link(target_);
    monitor(target_);
    for (int i = 0; i < 2; ++i) {
      co_await receive(
          [&](ExitSignal& signal) { capture_->exit_kind = signal.reason.kind; },
          [&](DownSignal& signal) {
            capture_->down_kind = signal.reason.kind;
          });
    }
    co_return;
  }

 private:
  ActorRef target_{};
  SignalCapture* capture_;
};

class TimeoutReceiver : public TestActor<TimeoutReceiver, Messages<Ping>> {
 public:
  TimeoutReceiver(Scheduler& scheduler, int* out)
      : Actor(scheduler), out_(out) {}

  task<void> run() {
    auto visitor = [&](Ping& ping) { return ping.value; };
    auto timeout = [&] { return -1; };
    auto value = co_await this->try_receive(std::chrono::milliseconds{1},
                                            timeout, visitor);
    *out_ = value;
    co_return;
  }

 private:
  int* out_;
};

class TryReceiveSuccess : public TestActor<TryReceiveSuccess, Messages<Ping>> {
 public:
  TryReceiveSuccess(Scheduler& scheduler, int* out)
      : Actor(scheduler), out_(out) {}

  task<void> run() {
    auto visitor = [&](Ping& ping) { return ping.value; };
    auto timeout = [&] { return -1; };
    auto value = co_await this->try_receive(std::chrono::milliseconds{5},
                                            timeout, visitor);
    *out_ = value;
    co_return;
  }

 private:
  int* out_;
};

class TryReceiveVoidTimeout
    : public TestActor<TryReceiveVoidTimeout, Messages<Ping>> {
 public:
  TryReceiveVoidTimeout(Scheduler& scheduler, bool* received, bool* timed_out)
      : Actor(scheduler), received_(received), timed_out_(timed_out) {}

  task<void> run() {
    auto visitor = [&](Ping&) { *received_ = true; };
    auto timeout = [&] { *timed_out_ = true; };
    co_await this->try_receive(std::chrono::milliseconds{1}, timeout, visitor);
    co_return;
  }

 private:
  bool* received_;
  bool* timed_out_;
};

class MultiVisitorActor
    : public TestActor<MultiVisitorActor, Messages<Ping, Pong>> {
 public:
  MultiVisitorActor(Scheduler& scheduler, int* out)
      : Actor(scheduler), out_(out) {}

  task<void> run() {
    auto value = co_await receive([&](Ping& ping) { return ping.value; },
                                  [&](Pong& pong) { return pong.value * 2; });
    *out_ = value;
    co_return;
  }

 private:
  int* out_;
};

class SequenceCollector : public TestActor<SequenceCollector, Messages<Ping>> {
 public:
  SequenceCollector(Scheduler& scheduler, std::vector<int>* out)
      : Actor(scheduler), out_(out) {}

  task<void> run() {
    auto first = co_await receive([&](Ping& ping) { return ping.value; });
    auto second = co_await receive([&](Ping& ping) { return ping.value; });
    out_->push_back(first);
    out_->push_back(second);
    co_return;
  }

 private:
  std::vector<int>* out_;
};

class DeferredCollector
    : public DeterministicActor<DeferredCollector, Messages<Ping>> {
 public:
  DeferredCollector(DeterministicScheduler& scheduler, std::optional<int>* out)
      : Actor(scheduler), out_(out) {}

  task<void> run() {
    auto value = co_await receive([&](Ping& ping) { return ping.value; });
    *out_ = value;
    co_return;
  }

 private:
  std::optional<int>* out_;
};

class DeferredTryReceive
    : public DeterministicActor<DeferredTryReceive, Messages<Ping>> {
 public:
  DeferredTryReceive(DeterministicScheduler& scheduler, std::optional<int>* out,
                     bool* timed_out)
      : Actor(scheduler), out_(out), timed_out_(timed_out) {}

  task<void> run() {
    auto visitor = [&](Ping& ping) { return ping.value; };
    auto timeout = [&] {
      *timed_out_ = true;
      return -1;
    };
    auto value = co_await this->try_receive(DeterministicScheduler::duration{5},
                                            timeout, visitor);
    *out_ = value;
    co_return;
  }

 private:
  std::optional<int>* out_;
  bool* timed_out_;
};

struct SpawnReport {
  int exit_signals = 0;
  int down_signals = 0;
};

class SpawnTester : public TestActor<SpawnTester, Messages<>> {
 public:
  SpawnTester(Scheduler& scheduler, SpawnReport* report)
      : Actor(scheduler), report_(report) {}

  task<void> run() {
    auto worker = spawn<Worker>();
    auto linked = spawn_link<Worker>();
    auto monitored = spawn_monitor<Worker>();

    send(worker, Stop{});
    send(linked, Stop{});
    send(monitored, Stop{});

    for (int i = 0; i < 2; ++i) {
      co_await receive([&](ExitSignal&) { ++report_->exit_signals; },
                       [&](DownSignal&) { ++report_->down_signals; });
    }
    co_return;
  }

 private:
  SpawnReport* report_;
};

class DummyControl : public ActorControl {
 public:
  void stop(ExitReason) override {}
  ExitReason exit_reason() const noexcept override { return {}; }
};

class Stoppable : public TestActor<Stoppable, Messages<>> {
 public:
  using Actor::Actor;

  task<void> run() {
    co_await receive([&](ExitSignal&) {});
    co_return;
  }
};

inline task<void> record_value(std::vector<int>* out, int value) {
  out->push_back(value);
  co_return;
}

struct ManualCoroutine {
  struct promise_type {
    ManualCoroutine get_return_object() {
      return ManualCoroutine{
          std::coroutine_handle<promise_type>::from_promise(*this)};
    }

    std::suspend_always initial_suspend() noexcept { return {}; }
    std::suspend_always final_suspend() noexcept { return {}; }
    void return_void() noexcept {}
    void unhandled_exception() { std::terminate(); }
  };

  std::coroutine_handle<promise_type> handle;
};

inline ManualCoroutine make_manual_coroutine() { co_return; }

inline task<int> immediate_int(int value) { co_return value; }

inline task<void> immediate_void(bool* ran) {
  *ran = true;
  co_return;
}

inline task<int> chained_int(int* out) {
  *out = co_await immediate_int(7);
  co_return *out;
}

inline task<void> chained_void(bool* ran) {
  co_await immediate_void(ran);
  co_return;
}

inline task<int> throwing_int_task() {
  throw std::runtime_error("boom");
  co_return 0;
}

inline task<void> throwing_void_task() {
  throw std::runtime_error("boom");
  co_return;
}

}  // namespace agner::test_support
