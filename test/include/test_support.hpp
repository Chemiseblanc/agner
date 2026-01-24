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

// Generic actor templated on scheduler type
template <typename SchedulerType, typename Derived, typename MessagePack>
using GenericActor = Actor<SchedulerType, Derived, MessagePack>;

// Templated collector actor that works with any scheduler
template <typename SchedulerType>
class CollectorT
    : public GenericActor<SchedulerType, CollectorT<SchedulerType>,
                          Messages<Ping>> {
 public:
  using Base = GenericActor<SchedulerType, CollectorT<SchedulerType>,
                            Messages<Ping>>;

  CollectorT(SchedulerType& scheduler, int* out) : Base(scheduler), out_(out) {}

  task<void> run() {
    auto value = co_await this->receive([&](Ping& ping) { return ping.value; });
    *out_ = value;
    co_return;
  }

 private:
  int* out_;
};

// Type aliases for convenience
using Collector = CollectorT<Scheduler>;
using DeferredCollectorT = CollectorT<DeterministicScheduler>;

// Templated worker actor
template <typename SchedulerType>
class WorkerT
    : public GenericActor<SchedulerType, WorkerT<SchedulerType>,
                          Messages<Stop>> {
 public:
  using Base = GenericActor<SchedulerType, WorkerT<SchedulerType>,
                            Messages<Stop>>;
  using Base::Base;

  task<void> run() {
    co_await this->receive([&](Stop&) {});
    co_return;
  }
};

using Worker = WorkerT<Scheduler>;

// Templated observer actor for monitoring another actor
template <typename SchedulerType>
class ObserverT
    : public GenericActor<SchedulerType, ObserverT<SchedulerType>,
                          Messages<>> {
 public:
  using Base = GenericActor<SchedulerType, ObserverT<SchedulerType>,
                            Messages<>>;

  ObserverT(SchedulerType& scheduler, ActorRef target, SignalCapture* capture)
      : Base(scheduler), target_(target), capture_(capture) {}

  task<void> run() {
    if (target_.valid()) {
      this->monitor(target_);
    }
    co_await this->receive(
        [&](ExitSignal& signal) { capture_->exit_kind = signal.reason.kind; },
        [&](DownSignal& signal) { capture_->down_kind = signal.reason.kind; });
    co_return;
  }

 private:
  ActorRef target_{};
  SignalCapture* capture_;
};

using Observer = ObserverT<Scheduler>;

// Templated signal observer
template <typename SchedulerType>
class SignalObserverT
    : public GenericActor<SchedulerType, SignalObserverT<SchedulerType>,
                          Messages<>> {
 public:
  using Base = GenericActor<SchedulerType, SignalObserverT<SchedulerType>,
                            Messages<>>;

  SignalObserverT(SchedulerType& scheduler, SignalCapture* capture)
      : Base(scheduler), capture_(capture) {}

  task<void> run() {
    co_await this->receive(
        [&](ExitSignal& signal) { capture_->exit_kind = signal.reason.kind; },
        [&](DownSignal& signal) { capture_->down_kind = signal.reason.kind; });
    co_return;
  }

 private:
  SignalCapture* capture_;
};

using SignalObserver = SignalObserverT<Scheduler>;

// Templated linked observer
template <typename SchedulerType>
class LinkedObserverT
    : public GenericActor<SchedulerType, LinkedObserverT<SchedulerType>,
                          Messages<>> {
 public:
  using Base = GenericActor<SchedulerType, LinkedObserverT<SchedulerType>,
                            Messages<>>;

  LinkedObserverT(SchedulerType& scheduler, ActorRef target,
                  SignalCapture* capture)
      : Base(scheduler), target_(target), capture_(capture) {}

  task<void> run() {
    if (target_.valid()) {
      this->link(target_);
      this->monitor(target_);
    }
    for (int i = 0; i < 2; ++i) {
      co_await this->receive(
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

using LinkedObserver = LinkedObserverT<Scheduler>;

// Templated timeout receiver
template <typename SchedulerType>
class TimeoutReceiverT
    : public GenericActor<SchedulerType, TimeoutReceiverT<SchedulerType>,
                          Messages<Ping>> {
 public:
  using Base = GenericActor<SchedulerType, TimeoutReceiverT<SchedulerType>,
                            Messages<Ping>>;

  TimeoutReceiverT(SchedulerType& scheduler, int* out)
      : Base(scheduler), out_(out) {}

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

using TimeoutReceiver = TimeoutReceiverT<Scheduler>;

// Templated try receive success actor
template <typename SchedulerType>
class TryReceiveSuccessT
    : public GenericActor<SchedulerType, TryReceiveSuccessT<SchedulerType>,
                          Messages<Ping>> {
 public:
  using Base = GenericActor<SchedulerType, TryReceiveSuccessT<SchedulerType>,
                            Messages<Ping>>;

  TryReceiveSuccessT(SchedulerType& scheduler, int* out)
      : Base(scheduler), out_(out) {}

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

using TryReceiveSuccess = TryReceiveSuccessT<Scheduler>;

// Templated try receive void timeout actor
template <typename SchedulerType>
class TryReceiveVoidTimeoutT
    : public GenericActor<SchedulerType, TryReceiveVoidTimeoutT<SchedulerType>,
                          Messages<Ping>> {
 public:
  using Base = GenericActor<SchedulerType, TryReceiveVoidTimeoutT<SchedulerType>,
                            Messages<Ping>>;

  TryReceiveVoidTimeoutT(SchedulerType& scheduler, bool* received,
                         bool* timed_out)
      : Base(scheduler), received_(received), timed_out_(timed_out) {}

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

using TryReceiveVoidTimeout = TryReceiveVoidTimeoutT<Scheduler>;

// Templated multi visitor actor
template <typename SchedulerType>
class MultiVisitorActorT
    : public GenericActor<SchedulerType, MultiVisitorActorT<SchedulerType>,
                          Messages<Ping, Pong>> {
 public:
  using Base = GenericActor<SchedulerType, MultiVisitorActorT<SchedulerType>,
                            Messages<Ping, Pong>>;

  MultiVisitorActorT(SchedulerType& scheduler, int* out)
      : Base(scheduler), out_(out) {}

  task<void> run() {
    auto value = co_await this->receive(
        [&](Ping& ping) { return ping.value; },
        [&](Pong& pong) { return pong.value * 2; });
    *out_ = value;
    co_return;
  }

 private:
  int* out_;
};

using MultiVisitorActor = MultiVisitorActorT<Scheduler>;

// Templated sequence collector
template <typename SchedulerType>
class SequenceCollectorT
    : public GenericActor<SchedulerType, SequenceCollectorT<SchedulerType>,
                          Messages<Ping>> {
 public:
  using Base = GenericActor<SchedulerType, SequenceCollectorT<SchedulerType>,
                            Messages<Ping>>;

  SequenceCollectorT(SchedulerType& scheduler, std::vector<int>* out)
      : Base(scheduler), out_(out) {}

  task<void> run() {
    auto first = co_await this->receive([&](Ping& ping) { return ping.value; });
    auto second = co_await this->receive([&](Ping& ping) { return ping.value; });
    out_->push_back(first);
    out_->push_back(second);
    co_return;
  }

 private:
  std::vector<int>* out_;
};

using SequenceCollector = SequenceCollectorT<Scheduler>;

// DeferredCollector with optional output (different signature, kept separate)
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

// DeferredTryReceive with optional output (different signature, kept separate)
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

// Templated spawn tester
template <typename SchedulerType>
class SpawnTesterT
    : public GenericActor<SchedulerType, SpawnTesterT<SchedulerType>,
                          Messages<>> {
 public:
  using Base = GenericActor<SchedulerType, SpawnTesterT<SchedulerType>,
                            Messages<>>;

  SpawnTesterT(SchedulerType& scheduler, SpawnReport* report)
      : Base(scheduler), report_(report) {}

  task<void> run() {
    auto worker = this->template spawn<WorkerT<SchedulerType>>();
    auto linked = this->template spawn_link<WorkerT<SchedulerType>>();
    auto monitored = this->template spawn_monitor<WorkerT<SchedulerType>>();

    this->send(worker, Stop{});
    this->send(linked, Stop{});
    this->send(monitored, Stop{});

    for (int i = 0; i < 2; ++i) {
      co_await this->receive([&](ExitSignal&) { ++report_->exit_signals; },
                             [&](DownSignal&) { ++report_->down_signals; });
    }
    co_return;
  }

 private:
  SpawnReport* report_;
};

using SpawnTester = SpawnTesterT<Scheduler>;

class DummyControl : public ActorControl {
 public:
  void stop(ExitReason) override {}
  ExitReason exit_reason() const noexcept override { return {}; }
};

// Templated stoppable actor
template <typename SchedulerType>
class StoppableT
    : public GenericActor<SchedulerType, StoppableT<SchedulerType>,
                          Messages<>> {
 public:
  using Base = GenericActor<SchedulerType, StoppableT<SchedulerType>,
                            Messages<>>;
  using Base::Base;

  task<void> run() {
    co_await this->receive([&](ExitSignal&) {});
    co_return;
  }
};

using Stoppable = StoppableT<Scheduler>;

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
