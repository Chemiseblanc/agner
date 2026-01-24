#pragma once

#include <cassert>
#include <chrono>
#include <deque>
#include <functional>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

#include "actor_concepts.hpp"
#include "actor_control.hpp"
#include "actor_detail.hpp"
#include "scheduler.hpp"
#include "scheduler_concept.hpp"

namespace agner {

template <typename... Functors>
struct overload : Functors... {
  using Functors::operator()...;
};

template <typename... Functors>
overload(Functors...) -> overload<Functors...>;

template <typename... MessageTypes>
struct Messages {};

namespace detail {}  // namespace detail

template <SchedulerLike SchedulerType, typename Derived, typename MessagePack>
class Actor;

template <SchedulerLike SchedulerType, typename Derived,
          typename... MessageTypes>
class Actor<SchedulerType, Derived, Messages<MessageTypes...>>
    : public ActorControl {
 public:
  using message_variant = std::variant<MessageTypes..., ExitSignal, DownSignal>;

  explicit Actor(SchedulerType& scheduler) : scheduler_(scheduler) {}

  SchedulerType& scheduler() noexcept { return scheduler_; }

  ActorRef self() const noexcept { return this->actor_ref(); }

  template <typename Message>
  void send(Message&& message) {
    enqueue_message(std::forward<Message>(message));
  }

  template <typename Message>
  void send(ActorRef target, Message&& message) {
    scheduler_.send(target, std::forward<Message>(message));
  }

  template <typename ActorType, typename... Args>
  ActorRef spawn(Args&&... args) {
    return scheduler_.template spawn<ActorType>(std::forward<Args>(args)...);
  }

  template <typename ActorType, typename... Args>
  ActorRef spawn_link(Args&&... args) {
    return scheduler_.template spawn_link<ActorType>(
        self(), std::forward<Args>(args)...);
  }

  template <typename ActorType, typename... Args>
  ActorRef spawn_monitor(Args&&... args) {
    return scheduler_.template spawn_monitor<ActorType>(
        self(), std::forward<Args>(args)...);
  }

  void link(ActorRef other) { scheduler_.link(self(), other); }
  void monitor(ActorRef other) { scheduler_.monitor(self(), other); }

  template <typename... Visitors>
  task<detail::receive_result_t<message_variant, Visitors...>> receive(
      Visitors&&... visitors) {
    using result_t = detail::receive_result_t<message_variant, Visitors...>;
    co_return co_await ReceiveAwaiter<result_t, std::nullptr_t, Visitors...>(
        *this, std::forward<Visitors>(visitors)...);
  }

  template <typename Rep, typename Period, typename TimeoutVisitor,
            typename... Visitors>
    requires detail::HasVisitors<Visitors...> &&
             detail::TimeoutVisitorFor<
                 TimeoutVisitor,
                 detail::receive_result_t<message_variant, Visitors...>>
  task<detail::receive_result_t<message_variant, Visitors...>> try_receive(
      std::chrono::duration<Rep, Period> timeout,
      TimeoutVisitor&& timeout_visitor, Visitors&&... visitors) {
    using result_t = detail::receive_result_t<message_variant, Visitors...>;
    auto cast_timeout =
        std::chrono::duration_cast<typename SchedulerType::Clock::duration>(
            timeout);
    auto awaiter =
        ReceiveAwaiter<result_t, std::decay_t<TimeoutVisitor>, Visitors...>(
            *this, cast_timeout, std::forward<TimeoutVisitor>(timeout_visitor),
            std::forward<Visitors>(visitors)...);
    co_return co_await awaiter;
  }

  task<ExitReason> start() {
    co_await static_cast<Derived&>(*this).run();
    co_return exit_reason();
  }

  void stop(ExitReason reason = {}) override {
    exit_reason_ = reason;
    enqueue_message(ExitSignal{self(), reason});
  }

  ExitReason exit_reason() const noexcept override {
    return exit_reason_.value_or(ExitReason{ExitReason::Kind::normal});
  }

 private:
  struct PendingReceive {
    std::coroutine_handle<> handle{};
    std::function<bool()> try_match;
  };

  template <typename Result>
  struct ReceiveStorage {
    using type = std::conditional_t<std::is_void_v<Result>, std::monostate,
                                    std::optional<Result>>;

    static type make() { return type{}; }
  };

  template <typename Result, typename TimeoutVisitor, typename... Visitors>
  class ReceiveAwaiter final {
   public:
    ReceiveAwaiter(Actor& actor, Visitors&&... visitors)
      requires detail::NullTimeoutVisitor<TimeoutVisitor>
        : actor_(actor),
          visitors_(std::forward<Visitors>(visitors)...),
          storage_(ReceiveStorage<Result>::make()) {}

    ReceiveAwaiter(Actor& actor,
                   typename SchedulerType::Clock::duration timeout,
                   TimeoutVisitor timeout_visitor, Visitors&&... visitors)
      requires detail::HasTimeoutVisitor<TimeoutVisitor>
        : actor_(actor),
          timeout_(timeout),
          timeout_visitor_(std::move(timeout_visitor)),
          visitors_(std::forward<Visitors>(visitors)...),
          storage_(ReceiveStorage<Result>::make()) {}

    bool await_ready() { return try_match(); }

    void await_suspend(std::coroutine_handle<> handle) {
      actor_.pending_ = PendingReceive{handle, [this] { return try_match(); }};
      if constexpr (!std::is_same_v<TimeoutVisitor, std::nullptr_t>) {
        active_ = true;
        actor_.scheduler_.schedule_after(timeout_, [this, handle] {
          if (!active_) {
            return;
          }
          active_ = false;
          timeout();
          actor_.pending_.reset();
          actor_.scheduler_.schedule(handle);
        });
      }
    }

    Result await_resume() {
      if constexpr (!std::is_void_v<Result>) {
        return std::move(*storage_);
      }
    }

    bool try_match() {
      if (actor_.try_match_mailbox(storage_, visitors_)) {
        if constexpr (!std::is_same_v<TimeoutVisitor, std::nullptr_t>) {
          active_ = false;
        }
        return true;
      }
      return false;
    }

   private:
    void timeout() {
      if constexpr (std::is_void_v<Result>) {
        std::invoke(timeout_visitor_);
      } else {
        storage_ = std::invoke(timeout_visitor_);
      }
    }

    Actor& actor_;
    typename SchedulerType::Clock::duration timeout_{};
    TimeoutVisitor timeout_visitor_{};
    std::tuple<std::decay_t<Visitors>...> visitors_;
    typename ReceiveStorage<Result>::type storage_;
    bool active_ = false;
  };

  template <typename Storage, typename Tuple>
  bool try_match_mailbox(Storage& storage, Tuple& visitors) {
    for (auto it = mailbox_.begin(); it != mailbox_.end(); ++it) {
      bool matched = std::apply(
          [&](auto&... visitor) {
            return detail::try_match_visitors(*it, storage, visitor...);
          },
          visitors);
      if (matched) {
        mailbox_.erase(it);
        return true;
      }
    }
    return false;
  }

  void notify_waiter() {
    if (!pending_) {
      return;
    }
    auto pending = pending_;
    pending_.reset();
    if (pending->try_match()) {
      auto handle = pending->handle;
      scheduler_.schedule(handle);
      return;
    }
    pending_ = std::move(pending);
  }

  template <typename Message>
  void enqueue_message(Message&& message) {
    mailbox_.emplace_back(std::forward<Message>(message));
    notify_waiter();
  }

  SchedulerType& scheduler_;
  std::deque<message_variant> mailbox_;
  std::optional<PendingReceive> pending_;
  std::optional<ExitReason> exit_reason_;
};

}  // namespace agner
