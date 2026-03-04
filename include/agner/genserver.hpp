#pragma once

/**
 * @file genserver.hpp
 * @brief Generic server pattern for request/response message handling.
 *
 * GenServer extends Actor with synchronous call/reply semantics similar to
 * Erlang/OTP's gen_server. Handlers are specified as function signatures
 * where void returns indicate asynchronous casts and non-void returns
 * indicate synchronous calls expecting a response.
 */

#include <cassert>
#include <chrono>
#include <cstdint>
#include <optional>
#include <utility>

#include "agner/actor.hpp"
#include "agner/actor_concepts.hpp"
#include "agner/detail/genserver_detail.hpp"
#include "agner/errors.hpp"
#include "agner/scheduler_concept.hpp"
#include "agner/task.hpp"

namespace agner {

namespace detail {

/// @brief Collect request types wrapped in CallMessage/CastMessage as Messages.
template <typename... Sigs>
struct genserver_messages;

template <>
struct genserver_messages<> {
  using type = Messages<Reply>;
};

template <typename Sig, typename... Rest>
struct genserver_messages<Sig, Rest...> {
 private:
  using Request = request_t<Sig>;
  using Wrapped = std::conditional_t<is_call_v<Sig>, CallMessage<Request>,
                                     CastMessage<Request>>;
  using RestMessages = typename genserver_messages<Rest...>::type;

  template <typename... Existing>
  static auto prepend(Messages<Existing...>) -> Messages<Wrapped, Existing...>;

 public:
  using type = decltype(prepend(RestMessages{}));
};

template <typename... Sigs>
using genserver_messages_t = typename genserver_messages<Sigs...>::type;

}  // namespace detail

template <SchedulerLike SchedulerType, typename Derived, typename HandlerPack>
class GenServer;

/**
 * @brief Base class for generic servers using CRTP.
 *
 * Derive from this class and implement handle() overloads for each request
 * type. Use serve() to run the main message loop. External callers use
 * call() for synchronous requests and cast() for fire-and-forget messages.
 *
 * @tparam SchedulerType Scheduler implementation.
 * @tparam Derived The derived server class (CRTP).
 * @tparam Sigs Handler signatures wrapped in Handlers<...>.
 */
template <SchedulerLike SchedulerType, typename Derived, typename... Sigs>
class GenServer<SchedulerType, Derived, Handlers<Sigs...>>
    : public Actor<SchedulerType, Derived,
                   detail::genserver_messages_t<Sigs...>> {
  using Base =
      Actor<SchedulerType, Derived, detail::genserver_messages_t<Sigs...>>;

 public:
  using Base::Base;

  /// @brief Make a synchronous call to a GenServer and await the response.
  /// @tparam Request The request type.
  /// @tparam Response The expected response type (deduced from Handlers).
  /// @param target The server actor to call.
  /// @param request The request payload.
  /// @param timeout Maximum time to wait for response.
  /// @return The response value.
  template <typename Request>
    requires detail::is_call_request_v<std::decay_t<Request>, Sigs...>
  task<detail::response_for_t<std::decay_t<Request>, Sigs...>> call(
      ActorRef target, Request&& request, DurationLike auto timeout) {
    using Response = detail::response_for_t<std::decay_t<Request>, Sigs...>;
    using SchedulerDuration = typename SchedulerType::Clock::duration;

    auto id = next_request_id_++;
    CallMessage<std::decay_t<Request>> msg{this->self(), id,
                                           std::forward<Request>(request)};
    this->send(target, std::move(msg));

    auto now = [this]() {
      auto& scheduler = this->scheduler();
      if constexpr (requires { scheduler.now(); }) {
        return scheduler.now();
      } else {
        return SchedulerType::Clock::now();
      }
    };
    auto deadline =
        now() + std::chrono::duration_cast<SchedulerDuration>(timeout);

    // Loop until we get the matching reply or timeout
    while (true) {
      auto remaining = deadline - now();
      if (remaining < SchedulerDuration::zero()) {
        throw CallTimeout{};
      }

      bool timed_out = false;
      auto result = co_await this->try_receive(
          remaining,
          [&timed_out]() -> std::optional<Response> {
            timed_out = true;
            return std::nullopt;
          },
          [id](Reply& reply) -> std::optional<Response> {
            if (reply.request_id == id) {
              return std::any_cast<Response>(std::move(reply.response));
            }
            // Wrong request_id - ignore and continue waiting
            return std::nullopt;
          });

      if (timed_out) {
        throw CallTimeout{};
      }

      if (result) {
        co_return std::move(*result);
      }

      // Wrong request_id received - loop and wait for correct reply
    }
  }

  /// @brief Send an asynchronous cast message to a GenServer.
  /// @tparam Request The request type.
  /// @param target The server actor.
  /// @param request The request payload.
  template <typename Request>
    requires detail::is_cast_request_v<std::decay_t<Request>, Sigs...>
  void cast(ActorRef target, Request&& request) {
    CastMessage<std::decay_t<Request>> msg{std::forward<Request>(request)};
    this->send(target, std::move(msg));
  }

 protected:
  /// @brief Run the main server loop, dispatching to handle() methods.
  ///
  /// Call this from run() to process incoming messages until stopped.
  /// The derived class must provide handle() overloads for each request type.
  task<void> serve() {
    while (true) {
      bool should_stop = co_await dispatch_one();
      if (should_stop) {
        break;
      }
    }
  }

 private:
  /// @brief Dispatcher that handles all message types for serve() loop.
  struct MessageDispatcher {
    GenServer& server;

    template <typename Request>
      requires detail::is_call_v<detail::signature_for_t<Request, Sigs...>>
    bool operator()(CallMessage<Request>& msg) {
      using Response = detail::response_for_t<Request, Sigs...>;
      auto& derived = static_cast<Derived&>(server);
      Response response = derived.handle(std::move(msg.request));
      Reply reply{msg.request_id, std::any(std::move(response))};
      server.send(msg.caller, std::move(reply));
      return false;
    }

    template <typename Request>
      requires detail::is_cast_v<detail::signature_for_t<Request, Sigs...>>
    bool operator()(CastMessage<Request>& msg) {
      auto& derived = static_cast<Derived&>(server);
      derived.handle(std::move(msg.request));
      return false;
    }

    bool operator()(Reply&) {
      // Ignore Reply messages in serve() - they're for call() awaits
      return false;
    }

    bool operator()(ExitSignal&) { return true; }

    bool operator()(DownSignal&) { return false; }
  };

  /// @brief Dispatch a single message from the mailbox.
  /// @return true if the server should stop.
  task<bool> dispatch_one() {
    co_return co_await this->receive(MessageDispatcher{*this});
  }

  uint64_t next_request_id_ = 0;
};

}  // namespace agner
