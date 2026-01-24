#include <gtest/gtest.h>

#include <map>
#include <type_traits>
#include <utility>
#include <vector>

#include "agner/supervisor.hpp"
#include "test_support.hpp"

namespace {

using namespace std::chrono_literals;
using namespace agner::test_support;
using agner::task;

inline int no_arg_starts = 0;

struct ChildLog {
  std::map<int, int> starts;
  std::map<int, agner::ActorRef> refs;
  std::vector<int> order;
  std::vector<agner::ChildId> ids;
  bool has_child_ref = false;
};

inline ChildLog* null_log() { return static_cast<ChildLog*>(nullptr); }

class LoggedChild : public DeterministicActor<LoggedChild, agner::Messages<>> {
 public:
  LoggedChild(agner::DeterministicScheduler& scheduler, ChildLog* log, int id)
      : Actor(scheduler), log_(log), id_(id) {}

  agner::task<void> run() {
    ++log_->starts[id_];
    log_->refs[id_] = self();
    log_->order.push_back(id_);
    co_await receive([&](agner::ExitSignal&) {});
    co_return;
  }

 private:
  ChildLog* log_;
  int id_ = 0;
};

class ImmediateExitChild
    : public DeterministicActor<ImmediateExitChild, agner::Messages<>> {
 public:
  ImmediateExitChild(agner::DeterministicScheduler& scheduler, ChildLog* log)
      : Actor(scheduler), log_(log) {}

  agner::task<void> run() {
    ++log_->starts[0];
    log_->refs[0] = self();
    co_return;
  }

 private:
  ChildLog* log_;
};

class SchedulerChild : public TestActor<SchedulerChild, agner::Messages<>> {
 public:
  SchedulerChild(agner::Scheduler& scheduler, ChildLog* log)
      : Actor(scheduler), log_(log) {}

  task<void> run() {
    ++log_->starts[0];
    log_->refs[0] = self();
    co_await receive([&](agner::ExitSignal&) {});
    co_return;
  }

 private:
  ChildLog* log_;
};

class LoopChild : public DeterministicActor<LoopChild, agner::Messages<Ping>> {
 public:
  LoopChild(agner::DeterministicScheduler& scheduler, ChildLog* log)
      : Actor(scheduler), log_(log) {}

  task<void> run() {
    ++log_->starts[0];
    log_->refs[0] = self();
    while (true) {
      bool done = false;
      co_await receive([&](Ping&) { send(self(), Pong{0}); },
                       [&](agner::ExitSignal&) { done = true; });
      if (done) {
        break;
      }
    }
    co_return;
  }

 private:
  ChildLog* log_;
};

class GateChild : public DeterministicActor<GateChild, agner::Messages<Ping>> {
 public:
  GateChild(agner::DeterministicScheduler& scheduler, ChildLog* log, int id)
      : Actor(scheduler), log_(log), id_(id) {}

  task<void> run() {
    ++log_->starts[id_];
    log_->refs[id_] = self();
    bool stop_requested = false;
    while (true) {
      bool done = false;
      co_await receive(
          [&](Ping&) {
            if (stop_requested) {
              done = true;
            }
          },
          [&](agner::ExitSignal&) { stop_requested = true; });
      if (done) {
        break;
      }
    }
    co_return;
  }

 private:
  ChildLog* log_;
  int id_ = 0;
};

class DeterministicObserver
    : public DeterministicActor<DeterministicObserver, agner::Messages<>> {
 public:
  DeterministicObserver(agner::DeterministicScheduler& scheduler,
                        agner::ActorRef target, SignalCapture* capture)
      : Actor(scheduler), target_(target), capture_(capture) {}

  task<void> run() {
    monitor(target_);
    co_await receive(
        [&](agner::DownSignal& signal) {
          capture_->down_kind = signal.reason.kind;
        },
        [&](agner::ExitSignal& signal) {
          capture_->exit_kind = signal.reason.kind;
        });
    co_return;
  }

 private:
  agner::ActorRef target_{};
  SignalCapture* capture_;
};

class ExitSignalSender
    : public DeterministicActor<ExitSignalSender, agner::Messages<>> {
 public:
  ExitSignalSender(agner::DeterministicScheduler& scheduler,
                   agner::ActorRef target, agner::ActorRef from)
      : Actor(scheduler), target_(target), from_(from) {}

  task<void> run() {
    send(target_, agner::ExitSignal{from_, agner::ExitReason{}});
    co_return;
  }

 private:
  agner::ActorRef target_{};
  agner::ActorRef from_{};
};

class DownSignalSender
    : public DeterministicActor<DownSignalSender, agner::Messages<>> {
 public:
  DownSignalSender(agner::DeterministicScheduler& scheduler,
                   agner::ActorRef target, agner::ActorRef from)
      : Actor(scheduler), target_(target), from_(from) {}

  task<void> run() {
    send(target_, agner::DownSignal{from_, agner::ExitReason{}});
    co_return;
  }

 private:
  agner::ActorRef target_{};
  agner::ActorRef from_{};
};

class ReuseSupervisor
    : public agner::Supervisor<agner::DeterministicScheduler, ReuseSupervisor,
                               agner::ChildSpec<LoopChild, ChildLog*>> {
 public:
  using Base = agner::Supervisor<agner::DeterministicScheduler, ReuseSupervisor,
                                 agner::ChildSpec<LoopChild, ChildLog*>>;

  ReuseSupervisor(agner::DeterministicScheduler& scheduler, ChildLog* log)
      : Base(scheduler), log_(log) {}

  static Specification specification() {
    return {.strategy = agner::Strategy::one_for_one,
            .intensity = {3, 10ms},
            .children = std::make_tuple(agner::child<LoopChild, ChildLog*>(
                {"child"}, agner::Restart::permanent, 0ms, null_log()))};
  }

  task<void> run() {
    this->template set_child_args<0>(std::make_tuple(log_));
    auto first = co_await this->template start_child<agner::ChildIndex<0>>();
    auto second = co_await this->template start_child<agner::ChildIndex<0>>();
    if (first.valid() && second.valid()) {
      log_->refs[1] = first;
      log_->refs[2] = second;
    }
    co_await Base::supervise_loop();
  }

 private:
  ChildLog* log_;
};

class OneForOneSupervisor
    : public agner::Supervisor<agner::DeterministicScheduler,
                               OneForOneSupervisor,
                               agner::ChildSpec<LoggedChild, ChildLog*, int>> {
 public:
  using Base =
      agner::Supervisor<agner::DeterministicScheduler, OneForOneSupervisor,
                        agner::ChildSpec<LoggedChild, ChildLog*, int>>;

  OneForOneSupervisor(agner::DeterministicScheduler& scheduler, ChildLog* log)
      : Base(scheduler), log_(log) {}

  static Specification specification() {
    return {
        .strategy = agner::Strategy::one_for_one,
        .intensity = {3, 10ms},
        .children = std::make_tuple(agner::child<LoggedChild, ChildLog*, int>(
            {"child"}, agner::Restart::permanent, 0ms, null_log(), 1))};
  }

  task<void> run() {
    this->template set_child_args<0>(std::make_tuple(log_, 1));
    co_await Base::run();
  }

 private:
  ChildLog* log_;
};

class TransientSupervisor
    : public agner::Supervisor<
          agner::DeterministicScheduler, TransientSupervisor,
          agner::ChildSpec<ImmediateExitChild, ChildLog*>> {
 public:
  using Base =
      agner::Supervisor<agner::DeterministicScheduler, TransientSupervisor,
                        agner::ChildSpec<ImmediateExitChild, ChildLog*>>;

  TransientSupervisor(agner::DeterministicScheduler& scheduler, ChildLog* log)
      : Base(scheduler), log_(log) {}

  static Specification specification() {
    return {
        .strategy = agner::Strategy::one_for_one,
        .intensity = {3, 10ms},
        .children = std::make_tuple(agner::child<ImmediateExitChild, ChildLog*>(
            {"child"}, agner::Restart::transient, 0ms, null_log()))};
  }

  task<void> run() {
    this->template set_child_args<0>(std::make_tuple(log_));
    co_await Base::run();
  }

 private:
  ChildLog* log_;
};

class TemporarySupervisor
    : public agner::Supervisor<agner::DeterministicScheduler,
                               TemporarySupervisor,
                               agner::ChildSpec<LoggedChild, ChildLog*, int>> {
 public:
  using Base =
      agner::Supervisor<agner::DeterministicScheduler, TemporarySupervisor,
                        agner::ChildSpec<LoggedChild, ChildLog*, int>>;

  TemporarySupervisor(agner::DeterministicScheduler& scheduler, ChildLog* log)
      : Base(scheduler), log_(log) {}

  static Specification specification() {
    return {
        .strategy = agner::Strategy::one_for_one,
        .intensity = {3, 10ms},
        .children = std::make_tuple(agner::child<LoggedChild, ChildLog*, int>(
            {"child"}, agner::Restart::temporary, 0ms, null_log(), 1))};
  }

  task<void> run() {
    this->template set_child_args<0>(std::make_tuple(log_, 1));
    co_await Base::run();
  }

 private:
  ChildLog* log_;
};

class OneForAllSupervisor
    : public agner::Supervisor<agner::DeterministicScheduler,
                               OneForAllSupervisor,
                               agner::ChildSpec<LoggedChild, ChildLog*, int>,
                               agner::ChildSpec<LoggedChild, ChildLog*, int>> {
 public:
  using Base =
      agner::Supervisor<agner::DeterministicScheduler, OneForAllSupervisor,
                        agner::ChildSpec<LoggedChild, ChildLog*, int>,
                        agner::ChildSpec<LoggedChild, ChildLog*, int>>;

  OneForAllSupervisor(agner::DeterministicScheduler& scheduler, ChildLog* log)
      : Base(scheduler), log_(log) {}

  static Specification specification() {
    return {
        .strategy = agner::Strategy::one_for_all,
        .intensity = {3, 10ms},
        .children = std::make_tuple(
            agner::child<LoggedChild, ChildLog*, int>(
                {"child_a"}, agner::Restart::permanent, 0ms, null_log(), 1),
            agner::child<LoggedChild, ChildLog*, int>(
                {"child_b"}, agner::Restart::permanent, 0ms, null_log(), 2))};
  }

  task<void> run() {
    this->template set_child_args<0>(std::make_tuple(log_, 1));
    this->template set_child_args<1>(std::make_tuple(log_, 2));
    co_await Base::run();
  }

 private:
  ChildLog* log_;
};

class RestForOneSupervisor
    : public agner::Supervisor<agner::DeterministicScheduler,
                               RestForOneSupervisor,
                               agner::ChildSpec<LoggedChild, ChildLog*, int>,
                               agner::ChildSpec<LoggedChild, ChildLog*, int>> {
 public:
  using Base =
      agner::Supervisor<agner::DeterministicScheduler, RestForOneSupervisor,
                        agner::ChildSpec<LoggedChild, ChildLog*, int>,
                        agner::ChildSpec<LoggedChild, ChildLog*, int>>;

  RestForOneSupervisor(agner::DeterministicScheduler& scheduler, ChildLog* log)
      : Base(scheduler), log_(log) {}

  static Specification specification() {
    return {
        .strategy = agner::Strategy::rest_for_one,
        .intensity = {3, 10ms},
        .children = std::make_tuple(
            agner::child<LoggedChild, ChildLog*, int>(
                {"child_a"}, agner::Restart::permanent, 0ms, null_log(), 1),
            agner::child<LoggedChild, ChildLog*, int>(
                {"child_b"}, agner::Restart::permanent, 0ms, null_log(), 2))};
  }

  task<void> run() {
    this->template set_child_args<0>(std::make_tuple(log_, 1));
    this->template set_child_args<1>(std::make_tuple(log_, 2));
    co_await Base::run();
  }

  void stop(agner::ExitReason reason = {}) override { Base::stop(reason); }

 private:
  ChildLog* log_;
};

class SimpleSupervisor
    : public agner::Supervisor<agner::DeterministicScheduler, SimpleSupervisor,
                               agner::ChildSpec<LoggedChild, ChildLog*, int>> {
 public:
  using Base =
      agner::Supervisor<agner::DeterministicScheduler, SimpleSupervisor,
                        agner::ChildSpec<LoggedChild, ChildLog*, int>>;

  SimpleSupervisor(agner::DeterministicScheduler& scheduler, ChildLog* log)
      : Base(scheduler), log_(log) {}

  static Specification specification() {
    return {.strategy = agner::Strategy::simple_one_for_one,
            .intensity = {3, 10ms},
            .children = std::make_tuple(
                agner::simple_child<LoggedChild, ChildLog*, int>(
                    {"child"}, agner::Restart::permanent, 0ms))};
  }

  task<void> run() {
    co_await start_child<agner::ChildIndex<0>>(log_, 1);
    co_await start_child<agner::ChildIndex<0>>(log_, 2);
    co_await Base::supervise_loop();
  }

 private:
  ChildLog* log_;
};

class NoArgChild
    : public DeterministicActor<NoArgChild, agner::Messages<Stop>> {
 public:
  using DeterministicActor<NoArgChild,
                           agner::Messages<Stop>>::DeterministicActor;

  task<void> run() {
    ++no_arg_starts;
    co_await receive([&](Stop&) {});
    co_return;
  }
};

class NoArgSupervisor
    : public agner::Supervisor<agner::DeterministicScheduler, NoArgSupervisor,
                               agner::ChildSpec<NoArgChild>> {
 public:
  using Base = agner::Supervisor<agner::DeterministicScheduler, NoArgSupervisor,
                                 agner::ChildSpec<NoArgChild>>;

  using Base::Base;

  static Specification specification() {
    return {.strategy = agner::Strategy::simple_one_for_one,
            .intensity = {3, 10ms},
            .children = std::make_tuple(agner::simple_child<NoArgChild>(
                {"child"}, agner::Restart::permanent, 0ms))};
  }

  task<void> run() {
    co_await start_child<agner::ChildIndex<0>>();
    this->stop();
    co_await Base::supervise_loop();
  }
};

class RestartLoopSupervisor
    : public agner::Supervisor<agner::DeterministicScheduler,
                               RestartLoopSupervisor,
                               agner::ChildSpec<LoggedChild, ChildLog*, int>> {
 public:
  using Base =
      agner::Supervisor<agner::DeterministicScheduler, RestartLoopSupervisor,
                        agner::ChildSpec<LoggedChild, ChildLog*, int>>;

  RestartLoopSupervisor(agner::DeterministicScheduler& scheduler, ChildLog* log)
      : Base(scheduler), log_(log) {}

  static Specification specification() {
    return {
        .strategy = agner::Strategy::one_for_one,
        .intensity = {1, 0ns},
        .children = std::make_tuple(agner::child<LoggedChild, ChildLog*, int>(
            {"child"}, agner::Restart::permanent, 0ms, null_log(), 1))};
  }

  task<void> run() {
    this->template set_child_args<0>(std::make_tuple(log_, 1));
    co_await Base::run();
    scheduler().advance(2ms);
    scheduler().advance(2ms);
    scheduler().advance(2ms);
    scheduler().advance(2ms);
    co_return;
  }

 private:
  ChildLog* log_;
};

class StopDeleteSupervisor
    : public agner::Supervisor<agner::DeterministicScheduler,
                               StopDeleteSupervisor,
                               agner::ChildSpec<LoggedChild, ChildLog*, int>> {
 public:
  using Base =
      agner::Supervisor<agner::DeterministicScheduler, StopDeleteSupervisor,
                        agner::ChildSpec<LoggedChild, ChildLog*, int>>;

  StopDeleteSupervisor(agner::DeterministicScheduler& scheduler, ChildLog* log)
      : Base(scheduler), log_(log) {}

  static Specification specification() {
    return {
        .strategy = agner::Strategy::one_for_one,
        .intensity = {3, 10ms},
        .children = std::make_tuple(agner::child<LoggedChild, ChildLog*, int>(
            {"child"}, agner::Restart::permanent, 0ms, null_log(), 1))};
  }

  task<void> run() {
    this->template set_child_args<0>(std::make_tuple(log_, 1));
    co_await Base::run();
    co_await stop_child<agner::ChildIndex<0>>();
    co_await delete_child<agner::ChildIndex<0>>();
    co_await stop_child<agner::ChildIndex<0>>();
    co_await delete_child<agner::ChildIndex<0>>();
    this->stop();
    co_await Base::supervise_loop();
  }

 private:
  ChildLog* log_;
};

class SimpleTemporarySupervisor
    : public agner::Supervisor<agner::DeterministicScheduler,
                               SimpleTemporarySupervisor,
                               agner::ChildSpec<LoggedChild, ChildLog*, int>> {
 public:
  using Base = agner::Supervisor<agner::DeterministicScheduler,
                                 SimpleTemporarySupervisor,
                                 agner::ChildSpec<LoggedChild, ChildLog*, int>>;

  SimpleTemporarySupervisor(agner::DeterministicScheduler& scheduler,
                            ChildLog* log)
      : Base(scheduler), log_(log) {}

  static Specification specification() {
    return {.strategy = agner::Strategy::simple_one_for_one,
            .intensity = {3, 10ms},
            .children = std::make_tuple(
                agner::simple_child<LoggedChild, ChildLog*, int>(
                    {"child"}, agner::Restart::temporary, 0ms))};
  }

  task<void> run() {
    co_await start_child<agner::ChildIndex<0>>(log_, 1);
    co_await Base::supervise_loop();
  }

  void stop(agner::ExitReason reason = {}) override { Base::stop(reason); }

 private:
  ChildLog* log_;
};

class PopRestartSupervisor
    : public agner::Supervisor<agner::DeterministicScheduler,
                               PopRestartSupervisor,
                               agner::ChildSpec<LoggedChild, ChildLog*, int>> {
 public:
  using Base =
      agner::Supervisor<agner::DeterministicScheduler, PopRestartSupervisor,
                        agner::ChildSpec<LoggedChild, ChildLog*, int>>;

  PopRestartSupervisor(agner::DeterministicScheduler& scheduler, ChildLog* log)
      : Base(scheduler), log_(log) {}

  static Specification specification() {
    return {
        .strategy = agner::Strategy::one_for_one,
        .intensity = {1, 0ns},
        .children = std::make_tuple(agner::child<LoggedChild, ChildLog*, int>(
            {"child"}, agner::Restart::permanent, 0ms, null_log(), 1))};
  }

  task<void> run() {
    this->template set_child_args<0>(std::make_tuple(log_, 1));
    co_await Base::run();
  }

 private:
  ChildLog* log_;
};

class TemporaryGroupSupervisor
    : public agner::Supervisor<agner::DeterministicScheduler,
                               TemporaryGroupSupervisor,
                               agner::ChildSpec<GateChild, ChildLog*, int>,
                               agner::ChildSpec<GateChild, ChildLog*, int>> {
 public:
  using Base =
      agner::Supervisor<agner::DeterministicScheduler, TemporaryGroupSupervisor,
                        agner::ChildSpec<GateChild, ChildLog*, int>,
                        agner::ChildSpec<GateChild, ChildLog*, int>>;

  TemporaryGroupSupervisor(agner::DeterministicScheduler& scheduler,
                           ChildLog* log)
      : Base(scheduler), log_(log) {
    (void)log_;
  }

  static Specification specification() {
    return {
        .strategy = agner::Strategy::one_for_all,
        .intensity = {3, 10ms},
        .children = std::make_tuple(
            agner::child<GateChild, ChildLog*, int>(
                {"child_a"}, agner::Restart::temporary, 0ms, null_log(), 1),
            agner::child<GateChild, ChildLog*, int>(
                {"child_b"}, agner::Restart::temporary, 0ms, null_log(), 2))};
  }

  task<void> run() {
    this->template set_child_args<0>(std::make_tuple(log_, 1));
    this->template set_child_args<1>(std::make_tuple(log_, 2));
    co_await Base::run();
    co_await Base::supervise_loop();
  }

 private:
  ChildLog* log_;
};

class SchedulerSupervisor
    : public agner::Supervisor<agner::Scheduler, SchedulerSupervisor,
                               agner::ChildSpec<SchedulerChild, ChildLog*>> {
 public:
  using Base = agner::Supervisor<agner::Scheduler, SchedulerSupervisor,
                                 agner::ChildSpec<SchedulerChild, ChildLog*>>;

  SchedulerSupervisor(agner::Scheduler& scheduler, ChildLog* log)
      : Base(scheduler), log_(log) {}

  static Specification specification() {
    return {.strategy = agner::Strategy::one_for_one,
            .intensity = {3, 10ms},
            .children = std::make_tuple(agner::child<SchedulerChild, ChildLog*>(
                {"child"}, agner::Restart::permanent, 0ms, null_log()))};
  }

  task<void> run() {
    this->template set_child_args<0>(std::make_tuple(log_));
    co_await Base::run();
  }

  void stop(agner::ExitReason reason = {}) override { Base::stop(reason); }

 private:
  ChildLog* log_;
};

class ZeroIntensityGroupSupervisor
    : public agner::Supervisor<agner::DeterministicScheduler,
                               ZeroIntensityGroupSupervisor,
                               agner::ChildSpec<LoggedChild, ChildLog*, int>,
                               agner::ChildSpec<LoggedChild, ChildLog*, int>> {
 public:
  using Base = agner::Supervisor<agner::DeterministicScheduler,
                                 ZeroIntensityGroupSupervisor,
                                 agner::ChildSpec<LoggedChild, ChildLog*, int>,
                                 agner::ChildSpec<LoggedChild, ChildLog*, int>>;

  ZeroIntensityGroupSupervisor(agner::DeterministicScheduler& scheduler,
                               ChildLog* log)
      : Base(scheduler), log_(log) {}

  static Specification specification() {
    return {
        .strategy = agner::Strategy::one_for_all,
        .intensity = {0, 10ms},
        .children = std::make_tuple(
            agner::child<LoggedChild, ChildLog*, int>(
                {"child_a"}, agner::Restart::permanent, 0ms, null_log(), 1),
            agner::child<LoggedChild, ChildLog*, int>(
                {"child_b"}, agner::Restart::permanent, 0ms, null_log(), 2))};
  }

  task<void> run() {
    this->template set_child_args<0>(std::make_tuple(log_, 1));
    this->template set_child_args<1>(std::make_tuple(log_, 2));
    co_await Base::run();
  }

 private:
  ChildLog* log_;
};

class NoopRestartGroupSupervisor
    : public agner::Supervisor<agner::DeterministicScheduler,
                               NoopRestartGroupSupervisor,
                               agner::ChildSpec<LoggedChild, ChildLog*, int>,
                               agner::ChildSpec<LoggedChild, ChildLog*, int>> {
 public:
  using Base = agner::Supervisor<agner::DeterministicScheduler,
                                 NoopRestartGroupSupervisor,
                                 agner::ChildSpec<LoggedChild, ChildLog*, int>,
                                 agner::ChildSpec<LoggedChild, ChildLog*, int>>;

  NoopRestartGroupSupervisor(agner::DeterministicScheduler& scheduler,
                             ChildLog* log)
      : Base(scheduler), log_(log) {}

  static Specification specification() {
    return {
        .strategy = agner::Strategy::one_for_all,
        .intensity = {3, 10ms},
        .children = std::make_tuple(
            agner::child<LoggedChild, ChildLog*, int>(
                {"child_a"}, agner::Restart::permanent, 0ms, null_log(), 1),
            agner::child<LoggedChild, ChildLog*, int>(
                {"child_b"}, agner::Restart::permanent, 0ms, null_log(), 2))};
  }

  task<void> run() {
    this->template set_child_args<0>(std::make_tuple(log_, 1));
    this->template set_child_args<1>(std::make_tuple(log_, 2));
    co_await Base::run();
    co_await stop_child<agner::ChildIndex<0>>();
    co_await stop_child<agner::ChildIndex<1>>();
    co_await Base::supervise_loop();
  }

  void stop(agner::ExitReason reason = {}) override { Base::stop(reason); }

 private:
  ChildLog* log_;
};

class IntensitySupervisor
    : public agner::Supervisor<agner::DeterministicScheduler,
                               IntensitySupervisor,
                               agner::ChildSpec<LoggedChild, ChildLog*, int>> {
 public:
  using Base =
      agner::Supervisor<agner::DeterministicScheduler, IntensitySupervisor,
                        agner::ChildSpec<LoggedChild, ChildLog*, int>>;

  IntensitySupervisor(agner::DeterministicScheduler& scheduler, ChildLog* log,
                      std::size_t max_restarts)
      : Base(scheduler), log_(log), max_restarts_(max_restarts) {}

  static Specification specification() {
    return {
        .strategy = agner::Strategy::one_for_one,
        .intensity = {3, 10ms},
        .children = std::make_tuple(agner::child<LoggedChild, ChildLog*, int>(
            {"child"}, agner::Restart::permanent, 0ms, null_log(), 1))};
  }

  task<void> run() {
    specification_mutable().intensity.max_restarts = max_restarts_;
    this->template set_child_args<0>(std::make_tuple(log_, 1));
    co_await Base::run();
  }

 private:
  ChildLog* log_;
  std::size_t max_restarts_ = 0;
};

}  // namespace

// Summary: When a one-for-one child exits, it shall restart the same child.
// Description: This test spawns a supervisor with one permanent child, then
// stops the child with an error reason. The child is expected to restart once,
// which is verified by the child start count.
TEST(Supervisor, OneForOneRestartsChild) {
  agner::DeterministicScheduler scheduler;
  ChildLog log;
  scheduler.spawn<OneForOneSupervisor>(&log);

  scheduler.run_until_idle();
  ASSERT_EQ(log.starts[1], 1);

  scheduler.stop(log.refs[1], {agner::ExitReason::Kind::error});
  scheduler.run_until_idle();

  EXPECT_EQ(log.starts[1], 2);
}

// Summary: When a transient child exits normally, it shall not restart.
// Description: This test uses a transient child that exits immediately with a
// normal reason. The supervisor should not restart it, so the start count
// remains one.
TEST(Supervisor, TransientDoesNotRestartOnNormalExit) {
  agner::DeterministicScheduler scheduler;
  ChildLog log;
  scheduler.spawn<TransientSupervisor>(&log);

  scheduler.run_until_idle();

  EXPECT_EQ(log.starts[0], 1);
}

// Summary: When a temporary child exits with error, it shall not restart.
// Description: This test stops a temporary child with an error reason. The
// supervisor should not restart it, which is verified by the start count.
TEST(Supervisor, TemporaryDoesNotRestartOnError) {
  agner::DeterministicScheduler scheduler;
  ChildLog log;
  scheduler.spawn<TemporarySupervisor>(&log);

  scheduler.run_until_idle();
  ASSERT_EQ(log.starts[1], 1);

  scheduler.stop(log.refs[1], {agner::ExitReason::Kind::error});
  scheduler.run_until_idle();

  EXPECT_EQ(log.starts[1], 1);
}

// Summary: When one-for-all is used, all children restart after a failure.
// Description: This test stops the first child with an error. Both children are
// expected to restart, so the start counts for each child increment.
TEST(Supervisor, OneForAllRestartsAllChildren) {
  agner::DeterministicScheduler scheduler;
  ChildLog log;
  scheduler.spawn<OneForAllSupervisor>(&log);

  scheduler.run_until_idle();
  ASSERT_EQ(log.starts[1], 1);
  ASSERT_EQ(log.starts[2], 1);

  scheduler.stop(log.refs[1], {agner::ExitReason::Kind::error});
  scheduler.run_until_idle();

  EXPECT_EQ(log.starts[1], 2);
  EXPECT_EQ(log.starts[2], 2);
}

// Summary: When rest-for-one fails the last child, only that child restarts.
// Description: This test stops the second child and verifies only that child
// restarts. It then stops the first child and verifies both restart because the
// second was started after the first.
TEST(Supervisor, RestForOneRestartsLaterChildren) {
  agner::DeterministicScheduler scheduler;
  ChildLog log;
  scheduler.spawn<RestForOneSupervisor>(&log);

  scheduler.run_until_idle();
  ASSERT_EQ(log.starts[1], 1);
  ASSERT_EQ(log.starts[2], 1);

  scheduler.stop(log.refs[2], {agner::ExitReason::Kind::error});
  scheduler.run_until_idle();

  EXPECT_EQ(log.starts[1], 1);
  EXPECT_EQ(log.starts[2], 2);

  scheduler.stop(log.refs[1], {agner::ExitReason::Kind::error});
  scheduler.run_until_idle();

  EXPECT_EQ(log.starts[1], 2);
  EXPECT_EQ(log.starts[2], 3);
}

// Summary: When simple-one-for-one fails a child, only that instance restarts.
// Description: This test starts two children and stops the first. The first
// child restarts while the second remains at a single start.
TEST(Supervisor, SimpleOneForOneRestartsInstance) {
  agner::DeterministicScheduler scheduler;
  ChildLog log;
  scheduler.spawn<SimpleSupervisor>(&log);

  scheduler.run_until_idle();
  ASSERT_EQ(log.starts[1], 1);
  ASSERT_EQ(log.starts[2], 1);

  scheduler.stop(log.refs[1], {agner::ExitReason::Kind::error});
  scheduler.run_until_idle();

  EXPECT_EQ(log.starts[1], 2);
  EXPECT_EQ(log.starts[2], 1);
}

// Summary: When restart intensity is exceeded, the supervisor stops with error.
// Description: This test allows only one restart and then forces two failures.
// The observer should receive a down signal with error once the limit is hit.
TEST(Supervisor, IntensityStopsSupervisorOnLimit) {
  agner::DeterministicScheduler scheduler;
  ChildLog log;
  SignalCapture capture;

  auto supervisor = scheduler.spawn<IntensitySupervisor>(&log, 1);
  scheduler.spawn<DeterministicObserver>(supervisor, &capture);

  scheduler.run_until_idle();
  scheduler.stop(log.refs[1], {agner::ExitReason::Kind::error});
  scheduler.run_until_idle();
  scheduler.stop(log.refs[1], {agner::ExitReason::Kind::error});
  scheduler.run_until_idle();

  ASSERT_TRUE(capture.down_kind.has_value());
  EXPECT_EQ(*capture.down_kind, agner::ExitReason::Kind::error);
}

// Summary: When restart intensity is zero, the supervisor stops immediately.
// Description: This test configures zero restarts and fails the child once. The
// observer should capture an error down signal from the supervisor.
TEST(Supervisor, ZeroIntensityStopsSupervisor) {
  agner::DeterministicScheduler scheduler;
  ChildLog log;
  SignalCapture capture;

  auto supervisor = scheduler.spawn<IntensitySupervisor>(&log, 0);
  scheduler.spawn<DeterministicObserver>(supervisor, &capture);

  scheduler.run_until_idle();
  scheduler.stop(log.refs[1], {agner::ExitReason::Kind::error});
  scheduler.run_until_idle();

  ASSERT_TRUE(capture.down_kind.has_value());
  EXPECT_EQ(*capture.down_kind, agner::ExitReason::Kind::error);
}

// Summary: When stop and delete are requested, children shall not restart.
// Description: This test stops and deletes the child from within the supervisor
// and confirms the child only starts once.
TEST(Supervisor, StopAndDeleteSuppressRestart) {
  agner::DeterministicScheduler scheduler;
  ChildLog log;
  scheduler.spawn<StopDeleteSupervisor>(&log);

  scheduler.run_until_idle();

  EXPECT_EQ(log.starts[1], 1);
}

// Summary: When using the standard scheduler, restarts shall still occur.
// Description: This test uses the non-deterministic scheduler to cover its
// restart path, stopping a child and verifying a restart.
TEST(Supervisor, SchedulerBasedRestartWorks) {
  agner::Scheduler scheduler;
  ChildLog log;
  scheduler.spawn<SchedulerSupervisor>(&log);

  scheduler.run();
  ASSERT_EQ(log.starts[0], 1);

  scheduler.stop(log.refs[0], {agner::ExitReason::Kind::error});
  scheduler.run();

  EXPECT_EQ(log.starts[0], 2);
}

// Summary: When a supervisor handles an exit signal, it should process it.
// Description: This test stops a child with an error and verifies the
// supervisor restarts it.
TEST(Supervisor, ExitSignalTriggersHandleExit) {
  agner::DeterministicScheduler scheduler;
  ChildLog log;
  auto supervisor = scheduler.spawn<OneForOneSupervisor>(&log);

  scheduler.run_until_idle();
  ASSERT_EQ(log.starts[1], 1);

  scheduler.stop(log.refs[1], {agner::ExitReason::Kind::error});
  scheduler.run_until_idle();

  EXPECT_EQ(log.starts[1], 2);
  scheduler.stop(supervisor, {agner::ExitReason::Kind::stopped});
  scheduler.run_until_idle();
}

// Summary: When a non-simple supervisor is asked to start an already running
// child, it should return the existing ref.
// Description: This test starts a child twice via start_child and verifies the
// same ActorRef is returned in both cases.
TEST(Supervisor, StartChildReturnsExistingRef) {
  agner::DeterministicScheduler scheduler;
  ChildLog log;
  scheduler.spawn<ReuseSupervisor>(&log);

  scheduler.run_until_idle();

  ASSERT_TRUE(log.refs[1].valid());
  ASSERT_TRUE(log.refs[2].valid());
  EXPECT_EQ(log.refs[1], log.refs[2]);
}

// Summary: When a simple-one-for-one temporary child exits, it should be
// removed.
// Description: This test stops a temporary simple child with an error reason
// and verifies it does not restart.
TEST(Supervisor, SimpleTemporaryChildIsRemoved) {
  agner::DeterministicScheduler scheduler;
  ChildLog log;
  scheduler.spawn<SimpleTemporarySupervisor>(&log);

  scheduler.run_until_idle();
  ASSERT_EQ(log.starts[1], 1);

  scheduler.stop(log.refs[1], {agner::ExitReason::Kind::error});
  scheduler.run_until_idle();

  EXPECT_EQ(log.starts[1], 1);
}

// Summary: When restart intensity windows are exceeded, old entries are pruned.
// Description: This test triggers restarts across time to cover pruning of
// restart history.
TEST(Supervisor, RestartWindowPrunesOldEntries) {
  agner::DeterministicScheduler scheduler;
  ChildLog log;
  scheduler.spawn<PopRestartSupervisor>(&log);

  scheduler.run_until_idle();
  ASSERT_EQ(log.starts[1], 1);

  scheduler.stop(log.refs[1], {agner::ExitReason::Kind::error});
  scheduler.run_until_idle();
  scheduler.advance(1ns);
  scheduler.stop(log.refs[1], {agner::ExitReason::Kind::error});
  scheduler.run_until_idle();

  EXPECT_EQ(log.starts[1], 3);
}

// Summary: When a restart group has no running children, it finalizes directly.
// Description: This test stops all children before any failure so the restart
// group plan contains no running instances when triggered.
TEST(Supervisor, RestartGroupFinalizesWithNoPendingStops) {
  agner::DeterministicScheduler scheduler;
  ChildLog log;
  scheduler.spawn<NoopRestartGroupSupervisor>(&log);

  scheduler.run_until_idle();
  ASSERT_EQ(log.starts[1], 1);
  ASSERT_EQ(log.starts[2], 1);

  scheduler.stop(log.refs[1], {agner::ExitReason::Kind::error});
  scheduler.run_until_idle();

  EXPECT_GE(log.starts[1], 1);
}

// Summary: When stop is requested for a stop/delete supervisor, it exits.
// Description: This test stops the supervisor after it calls stop/delete to
// ensure its supervise loop can exit cleanly.
TEST(Supervisor, StopDeleteSupervisorStops) {
  agner::DeterministicScheduler scheduler;
  ChildLog log;
  auto supervisor = scheduler.spawn<StopDeleteSupervisor>(&log);

  scheduler.run_until_idle();

  scheduler.stop(supervisor, {agner::ExitReason::Kind::stopped});
  scheduler.run_until_idle();

  EXPECT_EQ(log.starts[1], 1);
}

// Summary: When an unknown actor sends an exit signal, the supervisor ignores
// it. Description: This test sends a fabricated exit signal with an unknown
// ActorRef and verifies no restarts occur.
TEST(Supervisor, UnknownExitSignalIsIgnored) {
  agner::DeterministicScheduler scheduler;
  ChildLog log;
  auto supervisor = scheduler.spawn<OneForOneSupervisor>(&log);

  scheduler.run_until_idle();
  ASSERT_EQ(log.starts[1], 1);

  scheduler.spawn<ExitSignalSender>(supervisor, agner::ActorRef{999});
  scheduler.run_until_idle();

  EXPECT_EQ(log.starts[1], 1);
}

// Summary: When an exit signal comes from self without stopping, it is handled.
// Description: This test sends an exit signal from the supervisor ref and
// verifies no stop is requested.
TEST(Supervisor, ExitSignalFromSelfWithoutStoppingIsIgnored) {
  agner::DeterministicScheduler scheduler;
  ChildLog log;
  auto supervisor = scheduler.spawn<OneForOneSupervisor>(&log);

  scheduler.run_until_idle();
  ASSERT_EQ(log.starts[1], 1);

  scheduler.spawn<ExitSignalSender>(supervisor, supervisor);
  scheduler.run_until_idle();

  EXPECT_EQ(log.starts[1], 1);

  scheduler.stop(supervisor, {agner::ExitReason::Kind::stopped});
  scheduler.run_until_idle();
}

// Summary: When a down signal arrives from self, it is treated like a child.
// Description: This test sends a self down signal with a live child and ensures
// no unexpected restarts happen.
TEST(Supervisor, DownSignalFromSelfIsHandled) {
  agner::DeterministicScheduler scheduler;
  ChildLog log;
  auto supervisor = scheduler.spawn<OneForOneSupervisor>(&log);

  scheduler.run_until_idle();
  ASSERT_EQ(log.starts[1], 1);

  scheduler.spawn<DownSignalSender>(supervisor, supervisor);
  scheduler.run_until_idle();

  EXPECT_EQ(log.starts[1], 1);

  scheduler.stop(supervisor, {agner::ExitReason::Kind::stopped});
  scheduler.run_until_idle();
}

// Summary: When restart limits are exceeded in group strategies, it stops.
// Description: This test uses zero intensity to force register_restart to fail
// and skip starting a restart group.
TEST(Supervisor, RestartGroupStopsOnZeroIntensity) {
  agner::DeterministicScheduler scheduler;
  ChildLog log;
  SignalCapture capture;

  auto supervisor = scheduler.spawn<ZeroIntensityGroupSupervisor>(&log);
  scheduler.spawn<DeterministicObserver>(supervisor, &capture);

  scheduler.run_until_idle();
  scheduler.stop(log.refs[1], {agner::ExitReason::Kind::error});
  scheduler.run_until_idle();

  ASSERT_TRUE(capture.down_kind.has_value());
  EXPECT_EQ(*capture.down_kind, agner::ExitReason::Kind::error);
}

// Summary: When a simple-one-for-one child exits stopped, it is erased.
// Description: This test stops the child with a stopped reason and confirms the
// child does not restart.
TEST(Supervisor, SimpleTemporaryChildRemovedOnStop) {
  agner::DeterministicScheduler scheduler;
  ChildLog log;
  scheduler.spawn<SimpleTemporarySupervisor>(&log);

  scheduler.run_until_idle();
  ASSERT_EQ(log.starts[1], 1);

  scheduler.stop(log.refs[1], {agner::ExitReason::Kind::stopped});
  scheduler.run_until_idle();

  EXPECT_EQ(log.starts[1], 1);
}

// Summary: When a restart group plans a stopped child, it is not restarted.
// Description: This test triggers a one-for-all restart group and verifies the
// stopped child is skipped in the plan.
TEST(Supervisor, RestartGroupSkipsStoppedChild) {
  agner::DeterministicScheduler scheduler;
  ChildLog log;
  scheduler.spawn<OneForAllSupervisor>(&log);

  scheduler.run_until_idle();
  scheduler.stop(log.refs[2], {agner::ExitReason::Kind::error});
  scheduler.run_until_idle();

  EXPECT_GE(log.starts[1], 1);
}

// Summary: When a no-arg child is started, it should use default arguments.
// Description: This test starts a simple-one-for-one no-arg child and verifies
// the child ran once.
TEST(Supervisor, SimpleOneForOneNoArgChildStarts) {
  agner::DeterministicScheduler scheduler;
  no_arg_starts = 0;

  scheduler.spawn<NoArgSupervisor>();
  scheduler.run_until_idle();

  EXPECT_EQ(no_arg_starts, 1);
}

// Summary: When restart history exceeds the time window, entries are pruned.
// Description: This test advances the deterministic scheduler to prune history
// in register_restart.
TEST(Supervisor, RestartLoopPrunesHistory) {
  agner::DeterministicScheduler scheduler;
  ChildLog log;
  scheduler.spawn<RestartLoopSupervisor>(&log);

  scheduler.run_until_idle();

  EXPECT_GE(log.starts[1], 1);
}

// Summary: When stop/delete are called on active refs, they should stop
// normally. Description: This test uses stop/delete with a live ref and ensures
// the supervisor exits cleanly.
TEST(Supervisor, StopDeleteStopsActiveRef) {
  agner::DeterministicScheduler scheduler;
  ChildLog log;
  scheduler.spawn<StopDeleteSupervisor>(&log);

  scheduler.run_until_idle();

  EXPECT_GE(log.starts[1], 1);
}

// Summary: When a temporary group restarts, it should not restart children.
// Description: This test triggers a one-for-all restart on temporary children
// and verifies there are no restarts after stopping.
TEST(Supervisor, TemporaryRestartGroupSkipsChildren) {
  agner::DeterministicScheduler scheduler;
  ChildLog log;
  scheduler.spawn<TemporaryGroupSupervisor>(&log);

  scheduler.run_until_idle();
  ASSERT_EQ(log.starts[1], 1);
  ASSERT_EQ(log.starts[2], 1);

  scheduler.stop(log.refs[1], {agner::ExitReason::Kind::error});
  scheduler.run_until_idle();

  EXPECT_EQ(log.starts[1], 1);
  EXPECT_EQ(log.starts[2], 1);
}

// Summary: When an exit signal has no from actor, it is ignored.
// Description: This test sends an exit signal with an invalid ActorRef and
// verifies no restart occurs.
TEST(Supervisor, ExitSignalWithoutFromIsIgnored) {
  agner::DeterministicScheduler scheduler;
  ChildLog log;
  auto supervisor = scheduler.spawn<OneForOneSupervisor>(&log);

  scheduler.run_until_idle();
  ASSERT_EQ(log.starts[1], 1);

  scheduler.spawn<ExitSignalSender>(supervisor, agner::ActorRef{});
  scheduler.run_until_idle();

  EXPECT_EQ(log.starts[1], 1);
}

// Summary: When a down signal has no from actor, it is ignored.
// Description: This test sends a down signal with an invalid ActorRef and
// verifies no restart occurs.
TEST(Supervisor, DownSignalWithoutFromIsIgnored) {
  agner::DeterministicScheduler scheduler;
  ChildLog log;
  auto supervisor = scheduler.spawn<OneForOneSupervisor>(&log);

  scheduler.run_until_idle();
  ASSERT_EQ(log.starts[1], 1);

  scheduler.spawn<DownSignalSender>(supervisor, agner::ActorRef{});
  scheduler.run_until_idle();

  EXPECT_EQ(log.starts[1], 1);
}

// Summary: When restart_child is called on an active child, it stops and
// restarts. Description: This test calls restart_child on a running child and
// verifies it restarts.
TEST(Supervisor, RestartChildStopsAndRestartsActiveChild) {
  agner::DeterministicScheduler scheduler;
  ChildLog log;

  // Create a supervisor that exposes restart_child through a message
  class RestartChildSupervisor
      : public agner::Supervisor<agner::DeterministicScheduler,
                                 RestartChildSupervisor,
                                 agner::ChildSpec<LoggedChild, ChildLog*, int>> {
   public:
    using Base = agner::Supervisor<agner::DeterministicScheduler,
                                   RestartChildSupervisor,
                                   agner::ChildSpec<LoggedChild, ChildLog*, int>>;

    RestartChildSupervisor(agner::DeterministicScheduler& scheduler,
                           ChildLog* log)
        : Base(scheduler), log_(log) {}

    static Specification specification() {
      return {
          .strategy = agner::Strategy::one_for_one,
          .intensity = {3, 10ms},
          .children = std::make_tuple(agner::child<LoggedChild, ChildLog*, int>(
              {"child"}, agner::Restart::permanent, 0ms, null_log(), 1))};
    }

    task<void> run() {
      this->template set_child_args<0>(std::make_tuple(log_, 1));
      co_await Base::init();
      // Restart the running child
      co_await restart_child<agner::ChildIndex<0>>();
      co_await Base::supervise_loop();
    }

   private:
    ChildLog* log_;
  };

  auto supervisor = scheduler.spawn<RestartChildSupervisor>(&log);
  scheduler.run_until_idle();

  // Child should have started twice: once initially, once after restart
  EXPECT_EQ(log.starts[1], 2);

  scheduler.stop(supervisor, {agner::ExitReason::Kind::stopped});
  scheduler.run_until_idle();
}

// Summary: When stop_child is called on a running child, it sets suppress flag.
// Description: This test stops a child via stop_child and verifies it doesn't
// restart when it exits.
TEST(Supervisor, StopChildSuppressesRestartOnActiveChild) {
  agner::DeterministicScheduler scheduler;
  ChildLog log;

  class StopChildSupervisor
      : public agner::Supervisor<agner::DeterministicScheduler,
                                 StopChildSupervisor,
                                 agner::ChildSpec<LoggedChild, ChildLog*, int>> {
   public:
    using Base = agner::Supervisor<agner::DeterministicScheduler,
                                   StopChildSupervisor,
                                   agner::ChildSpec<LoggedChild, ChildLog*, int>>;

    StopChildSupervisor(agner::DeterministicScheduler& scheduler, ChildLog* log)
        : Base(scheduler), log_(log) {}

    static Specification specification() {
      return {
          .strategy = agner::Strategy::one_for_one,
          .intensity = {3, 10ms},
          .children = std::make_tuple(agner::child<LoggedChild, ChildLog*, int>(
              {"child"}, agner::Restart::permanent, 0ms, null_log(), 1))};
    }

    task<void> run() {
      this->template set_child_args<0>(std::make_tuple(log_, 1));
      co_await Base::init();
      // Stop the running child (should suppress restart)
      co_await stop_child<agner::ChildIndex<0>>();
      co_await Base::supervise_loop();
    }

   private:
    ChildLog* log_;
  };

  auto supervisor = scheduler.spawn<StopChildSupervisor>(&log);
  scheduler.run_until_idle();

  // Child should only start once - restart is suppressed
  EXPECT_EQ(log.starts[1], 1);

  scheduler.stop(supervisor, {agner::ExitReason::Kind::stopped});
  scheduler.run_until_idle();
}

// Summary: When delete_child is called, it removes the child from registry.
// Description: This test deletes a child and verifies it doesn't restart.
TEST(Supervisor, DeleteChildRemovesFromRegistry) {
  agner::DeterministicScheduler scheduler;
  ChildLog log;

  class DeleteChildSupervisor
      : public agner::Supervisor<agner::DeterministicScheduler,
                                 DeleteChildSupervisor,
                                 agner::ChildSpec<LoggedChild, ChildLog*, int>> {
   public:
    using Base = agner::Supervisor<agner::DeterministicScheduler,
                                   DeleteChildSupervisor,
                                   agner::ChildSpec<LoggedChild, ChildLog*, int>>;

    DeleteChildSupervisor(agner::DeterministicScheduler& scheduler,
                          ChildLog* log)
        : Base(scheduler), log_(log) {}

    static Specification specification() {
      return {
          .strategy = agner::Strategy::one_for_one,
          .intensity = {3, 10ms},
          .children = std::make_tuple(agner::child<LoggedChild, ChildLog*, int>(
              {"child"}, agner::Restart::permanent, 0ms, null_log(), 1))};
    }

    task<void> run() {
      this->template set_child_args<0>(std::make_tuple(log_, 1));
      co_await Base::init();
      // Delete removes child from registry
      co_await delete_child<agner::ChildIndex<0>>();
      co_await Base::supervise_loop();
    }

   private:
    ChildLog* log_;
  };

  auto supervisor = scheduler.spawn<DeleteChildSupervisor>(&log);
  scheduler.run_until_idle();

  // Child should only start once - completely removed
  EXPECT_EQ(log.starts[1], 1);

  scheduler.stop(supervisor, {agner::ExitReason::Kind::stopped});
  scheduler.run_until_idle();
}

// Summary: Multi-spec supervisor operations skip children of other spec indices.
// Description: This test creates a supervisor with two child specs, starts both,
// then calls stop/restart/delete on one spec while the other remains active.
// This exercises the continue statements in stop_and_suppress_by_index,
// restart_children_by_index, and delete_children_by_index.
TEST(Supervisor, MultiSpecOperationsSkipOtherIndices) {
  agner::DeterministicScheduler scheduler;
  ChildLog log;

  class MultiSpecSupervisor
      : public agner::Supervisor<agner::DeterministicScheduler,
                                 MultiSpecSupervisor,
                                 agner::ChildSpec<LoggedChild, ChildLog*, int>,
                                 agner::ChildSpec<LoggedChild, ChildLog*, int>> {
   public:
    using Base = agner::Supervisor<agner::DeterministicScheduler,
                                   MultiSpecSupervisor,
                                   agner::ChildSpec<LoggedChild, ChildLog*, int>,
                                   agner::ChildSpec<LoggedChild, ChildLog*, int>>;

    MultiSpecSupervisor(agner::DeterministicScheduler& scheduler, ChildLog* log)
        : Base(scheduler), log_(log) {}

    static Specification specification() {
      return {
          .strategy = agner::Strategy::one_for_one,
          .intensity = {3, 10ms},
          .children = std::make_tuple(
              agner::child<LoggedChild, ChildLog*, int>(
                  {"child_a"}, agner::Restart::permanent, 0ms, null_log(), 1),
              agner::child<LoggedChild, ChildLog*, int>(
                  {"child_b"}, agner::Restart::permanent, 0ms, null_log(), 2))};
    }

    task<void> run() {
      this->template set_child_args<0>(std::make_tuple(log_, 1));
      this->template set_child_args<1>(std::make_tuple(log_, 2));
      co_await Base::init();
      // Stop child 0 while child 1 still exists (triggers continue in loop)
      co_await stop_child<agner::ChildIndex<0>>();
      // Restart child 0 while child 1 still exists (triggers continue in loop)
      co_await restart_child<agner::ChildIndex<0>>();
      // Delete child 0 while child 1 still exists (triggers continue in loop)
      co_await delete_child<agner::ChildIndex<0>>();
      co_await Base::supervise_loop();
    }

   private:
    ChildLog* log_;
  };

  auto supervisor = scheduler.spawn<MultiSpecSupervisor>(&log);
  scheduler.run_until_idle();

  // Child 1 should start once initially (stop_child stopped it first)
  // Child 2 should keep running during all operations on child 1
  EXPECT_EQ(log.starts[1], 1);
  EXPECT_EQ(log.starts[2], 1);

  scheduler.stop(supervisor, {agner::ExitReason::Kind::stopped});
  scheduler.run_until_idle();
}
