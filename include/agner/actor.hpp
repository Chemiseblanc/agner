#pragma once

/**
 * @file actor.hpp
 * @brief Actor base class for message-driven concurrent computation.
 *
 * Actors are lightweight processes that communicate via asynchronous messages.
 * Each actor has a mailbox and processes messages using coroutine-based receive.
 */

#include <cassert>
#include <chrono>
#include <algorithm>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

#include "agner/actor_concepts.hpp"
#include "agner/actor_control.hpp"
#include "agner/detail/actor_detail.hpp"
#include "agner/detail/scheduler_traits.hpp"
#include "agner/scheduler.hpp"
#include "agner/scheduler_concept.hpp"

namespace agner {

/// @brief Helper for creating overloaded visitor lambdas.
/// @tparam Functors Callable types to combine.
template <typename... Functors>
struct overload : Functors... {
  using Functors::operator()...;
};

template <typename... Functors>
overload(Functors...) -> overload<Functors...>;

/// @brief Wrapper to declare the message types an actor can receive.
/// @tparam MessageTypes The message types this actor handles.
template <typename... MessageTypes>
struct Messages {};

template <SchedulerLike SchedulerType, typename Derived, typename MessagePack>
class Actor;

/**
 * @brief Base class for actors using CRTP.
 *
 * Derive from this class and implement a `run()` coroutine to define behavior.
 * Use `receive()` to await messages and `send()` to dispatch them.
 *
 * @tparam SchedulerType Scheduler implementation (must satisfy SchedulerLike).
 * @tparam Derived The derived actor class (CRTP).
 * @tparam MessageTypes Message types wrapped in Messages<...>.
 */
template <SchedulerLike SchedulerType, typename Derived,
          typename... MessageTypes>
class Actor<SchedulerType, Derived, Messages<MessageTypes...>>
    : public ActorControl {
 public:
  /// Variant type holding any receivable message including signals.
  using message_variant = std::variant<MessageTypes..., ExitSignal, DownSignal>;

  /// Mutex type: std::mutex for concurrent schedulers, null_mutex otherwise.
  using actor_mutex_type = detail::scheduler_mutex_t<SchedulerType>;

  /// @brief Construct an actor bound to a scheduler.
  explicit Actor(SchedulerType& scheduler) : scheduler_(scheduler) {}

  /// @brief Get the scheduler this actor is bound to.
  SchedulerType& scheduler() noexcept { return scheduler_; }

  /// @brief Get this actor's reference.
  ActorRef self() const noexcept { return this->actor_ref(); }

  /// @brief Send a message to this actor's own mailbox.
  void send(MessageType auto&& message) {
    enqueue_message(std::forward<decltype(message)>(message));
  }

  /// @brief Send a message to another actor.
  /// @param target The recipient actor reference.
  /// @param message The message to send.
  void send(ActorRef target, MessageType auto&& message) {
    scheduler_.send(target, std::forward<decltype(message)>(message));
  }

  /// @brief Spawn a new actor.
  /// @return Reference to the spawned actor.
  template <typename ActorType, typename... Args>
  ActorRef spawn(Args&&... args) {
    return scheduler_.template spawn<ActorType>(std::forward<Args>(args)...);
  }

  /// @brief Spawn a new actor and establish a bidirectional link.
  /// @return Reference to the spawned actor.
  template <typename ActorType, typename... Args>
  ActorRef spawn_link(Args&&... args) {
    return scheduler_.template spawn_link<ActorType>(
        self(), std::forward<Args>(args)...);
  }

  /// @brief Spawn a new actor and monitor it for exit.
  /// @return Reference to the spawned actor.
  template <typename ActorType, typename... Args>
  ActorRef spawn_monitor(Args&&... args) {
    return scheduler_.template spawn_monitor<ActorType>(
        self(), std::forward<Args>(args)...);
  }

  /// @brief Establish a bidirectional link with another actor.
  void link(ActorRef other) { scheduler_.link(self(), other); }

  /// @brief Monitor another actor for exit (receive DownSignal on exit).
  void monitor(ActorRef other) { scheduler_.monitor(self(), other); }

  /// @brief Await and process a message from the mailbox.
  /// @param visitors Callable handlers for each message type.
  /// @return The result from the matching visitor.
  template <typename... Visitors>
  task<detail::receive_result_t<message_variant, Visitors...>> receive(
      Visitors&&... visitors) {
    using result_t = detail::receive_result_t<message_variant, Visitors...>;
    co_return co_await ReceiveAwaiter<result_t, std::nullptr_t, Visitors...>(
        *this, std::forward<Visitors>(visitors)...);
  }

  /// @brief Await a message with a timeout.
  /// @param timeout Maximum duration to wait.
  /// @param timeout_visitor Called if no message arrives in time.
  /// @param visitors Callable handlers for each message type.
  /// @return The result from the matching visitor or timeout handler.
  template <typename TimeoutVisitor, typename... Visitors>
    requires detail::HasVisitors<Visitors...> &&
             detail::TimeoutVisitorFor<
                 TimeoutVisitor,
                 detail::receive_result_t<message_variant, Visitors...>>
  task<detail::receive_result_t<message_variant, Visitors...>> try_receive(
      DurationLike auto timeout,
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

  /// @brief Start the actor by invoking its run() coroutine.
  /// @return The exit reason when the actor completes.
  task<ExitReason> start() {
    co_await static_cast<Derived&>(*this).run();
    co_return exit_reason();
  }

  /// @brief Request the actor to stop.
  /// @param reason The exit reason to set.
  void stop(ExitReason reason = {}) override {
    std::coroutine_handle<> to_schedule{};
    {
      std::lock_guard<actor_mutex_type> lock(actor_mutex_);
      exit_reason_ = reason;
      mailbox_.emplace_back(ExitSignal{self(), reason});
      to_schedule = notify_waiter_locked();
    }
    if (to_schedule) {
      scheduler_.schedule(to_schedule);
    }
  }

  /// @brief Get the actor's exit reason.
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

    bool await_ready() {
      std::lock_guard<actor_mutex_type> lock(actor_.actor_mutex_);
      return try_match();
    }

    void await_suspend(std::coroutine_handle<> handle) {
      {
        std::lock_guard<actor_mutex_type> lock(actor_.actor_mutex_);
        actor_.pending_ = PendingReceive{handle, [this] { return try_match(); }};
      }
      if constexpr (!std::is_same_v<TimeoutVisitor, std::nullptr_t>) {
        auto active_flag = std::make_shared<std::atomic<bool>>(true);
        active_ = active_flag;
        actor_.scheduler_.schedule_after(timeout_, [this, handle, active_flag]() mutable {
          if (!active_flag->load(std::memory_order_acquire)) {
            return;
          }
          active_flag->store(false, std::memory_order_release);
          timeout();
          {
            std::lock_guard<actor_mutex_type> lock(actor_.actor_mutex_);
            actor_.pending_.reset();
          }
          actor_.scheduler_.schedule(handle);
        });
      }
    }

    Result await_resume() {
      if constexpr (!std::is_void_v<Result>) {
        return std::move(*storage_);
      }
    }

    /// @brief Try to match a mailbox message against the visitors.
    /// Caller must hold actor_mutex_.
    bool try_match() {
      if (actor_.try_match_mailbox(storage_, visitors_)) {
        if constexpr (!std::is_same_v<TimeoutVisitor, std::nullptr_t>) {
          if (active_) {
            active_->store(false, std::memory_order_release);
          }
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
    std::shared_ptr<std::atomic<bool>> active_{nullptr};
  };

  template <typename Storage, typename Tuple>
  bool try_match_mailbox(Storage& storage, Tuple& visitors) {
    auto it = std::find_if(mailbox_.begin(), mailbox_.end(),
                           [&](auto& queued_message) {
                             return std::apply(
                                 [&](auto&... visitor) {
                                   return detail::try_match_visitors(
                                       queued_message, storage, visitor...);
                                 },
                                 visitors);
                           });
    if (it == mailbox_.end()) {
      return false;
    }
    mailbox_.erase(it);
    return true;
  }

  /// @brief Returns a coroutine handle to schedule, or null.
  /// Caller must hold actor_mutex_.
  std::coroutine_handle<> notify_waiter_locked() {
    if (!pending_) {
      return {};
    }
    auto pending = pending_;
    pending_.reset();
    if (pending->try_match()) {
      return pending->handle;
    }
    pending_ = std::move(pending);
    return {};
  }

  void notify_waiter() {
    std::coroutine_handle<> to_schedule{};
    {
      std::lock_guard<actor_mutex_type> lock(actor_mutex_);
      to_schedule = notify_waiter_locked();
    }
    if (to_schedule) {
      scheduler_.schedule(to_schedule);
    }
  }

  template <typename Message>
  void enqueue_message(Message&& message) {
    std::coroutine_handle<> to_schedule{};
    {
      std::lock_guard<actor_mutex_type> lock(actor_mutex_);
      mailbox_.emplace_back(std::forward<Message>(message));
      to_schedule = notify_waiter_locked();
    }
    if (to_schedule) {
      scheduler_.schedule(to_schedule);
    }
  }

  SchedulerType& scheduler_;
  std::deque<message_variant> mailbox_;
  std::optional<PendingReceive> pending_;
  std::optional<ExitReason> exit_reason_;
  mutable actor_mutex_type actor_mutex_;
};

}  // namespace agner
