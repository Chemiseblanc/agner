#pragma once

/**
 * @file gen_event.hpp
 * @brief OTP-style event manager with dynamic handler registration.
 *
 * GenEvent extends Actor with a manager loop that supports:
 * - add_handler(): register a callback/state handler
 * - remove_handler(): unregister by handler reference
 * - notify(): broadcast an event to registered handlers in registration order
 */

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>
#include <variant>
#include <vector>

#include "agner/actor.hpp"
#include "agner/detail/gen_event_detail.hpp"
#include "agner/scheduler_concept.hpp"
#include "agner/task.hpp"

namespace agner {

/// @brief Opaque reference identifying a registered GenEvent handler.
struct HandlerRef {
  ActorRef owner{};
  uint64_t value = 0;

  constexpr bool valid() const noexcept { return owner.valid() && value != 0; }

  friend bool operator==(const HandlerRef&, const HandlerRef&) = default;
};

namespace detail {

template <typename... Events>
using event_view_variant_t =
    std::variant<std::reference_wrapper<const Events>...>;

template <typename... Events>
class EventHandlerBase {
 public:
  virtual ~EventHandlerBase() = default;
  virtual void handle(const event_view_variant_t<Events...>& event) = 0;
};

template <typename Handler, typename... Events>
class EventHandlerModel final : public EventHandlerBase<Events...> {
 public:
  explicit EventHandlerModel(Handler handler) : handler_(std::move(handler)) {}

  void handle(const event_view_variant_t<Events...>& event) override {
    std::visit(
        [&](const auto& typed_event) {
          invoke_handler_if_supported(handler_, typed_event.get());
        },
        event);
  }

 private:
  Handler handler_;
};

template <typename... Events>
using event_handler_ptr_t = std::shared_ptr<EventHandlerBase<Events...>>;

template <typename... Events>
struct AddHandlerMessage {
  HandlerRef handler_ref;
  event_handler_ptr_t<Events...> handler;
};

struct RemoveHandlerMessage {
  HandlerRef handler_ref;
};

template <typename... Events>
using gen_event_messages_t =
    Messages<AddHandlerMessage<Events...>, RemoveHandlerMessage, Events...>;

}  // namespace detail

template <SchedulerLike SchedulerType, typename Derived, typename HandlerPack>
class GenEvent;

/**
 * @brief Base class for OTP-style event managers using CRTP.
 *
 * Derive from this class and call serve() from run(). Use add_handler(),
 * remove_handler(), and notify() from actors to manage handlers and publish
 * events.
 *
 * @tparam SchedulerType Scheduler implementation.
 * @tparam Derived The derived manager class (CRTP).
 * @tparam Events Event message types wrapped in EventHandlers<...>.
 */
template <SchedulerLike SchedulerType, typename Derived, typename... Events>
class GenEvent<SchedulerType, Derived, EventHandlers<Events...>>
    : public Actor<SchedulerType, Derived, detail::gen_event_messages_t<Events...>> {
  using Base =
      Actor<SchedulerType, Derived, detail::gen_event_messages_t<Events...>>;
  using event_variant = detail::event_view_variant_t<Events...>;

  struct RegisteredHandler {
    HandlerRef handler_ref;
    detail::event_handler_ptr_t<Events...> handler;
  };

 public:
  using Base::Base;

  /// @brief Register a handler in the target GenEvent manager.
  /// @return A handler reference token for later removal.
  template <typename Handler>
    requires detail::EventHandlerFor<std::decay_t<Handler>, Events...>
  HandlerRef add_handler(ActorRef target, Handler&& handler) {
    HandlerRef handler_ref{this->self(), next_handler_id_++};
    using Model = detail::EventHandlerModel<std::decay_t<Handler>, Events...>;
    auto wrapped = std::make_shared<Model>(std::forward<Handler>(handler));
    this->send(target, detail::AddHandlerMessage<Events...>{
                           handler_ref, std::move(wrapped)});
    return handler_ref;
  }

  /// @brief Remove a previously registered handler from the target manager.
  void remove_handler(ActorRef target, HandlerRef handler_ref) {
    this->send(target, detail::RemoveHandlerMessage{handler_ref});
  }

  /// @brief Notify the target manager about an event.
  template <typename Event>
    requires detail::is_event_v<std::decay_t<Event>, Events...>
  void notify(ActorRef target, Event&& event) {
    this->send(target, std::forward<Event>(event));
  }

 protected:
  /// @brief Run the manager loop until ExitSignal is received.
  task<void> serve() {
    while (true) {
      bool should_stop = co_await dispatch_one();
      if (should_stop) {
        break;
      }
    }
  }

 private:
  struct MessageDispatcher {
    GenEvent& manager;

    bool operator()(detail::AddHandlerMessage<Events...>& message) {
      manager.add_or_replace_handler(message.handler_ref, std::move(message.handler));
      return false;
    }

    bool operator()(detail::RemoveHandlerMessage& message) {
      manager.remove_handler_by_ref(message.handler_ref);
      return false;
    }

    template <typename Event>
      requires detail::is_event_v<Event, Events...>
    bool operator()(Event& event) {
      manager.dispatch_event(event);
      return false;
    }

    bool operator()(ExitSignal&) { return true; }

    bool operator()(DownSignal&) { return false; }
  };

  task<bool> dispatch_one() {
    co_return co_await this->receive(MessageDispatcher{*this});
  }

  void add_or_replace_handler(HandlerRef handler_ref,
                              detail::event_handler_ptr_t<Events...> handler) {
    auto it = std::find_if(
        handlers_.begin(), handlers_.end(), [&](const RegisteredHandler& registered) {
          return registered.handler_ref == handler_ref;
        });
    if (it == handlers_.end()) {
      handlers_.push_back(RegisteredHandler{handler_ref, std::move(handler)});
      return;
    }
    it->handler = std::move(handler);
  }

  void remove_handler_by_ref(HandlerRef handler_ref) {
    std::erase_if(handlers_, [&](const RegisteredHandler& registered) {
      return registered.handler_ref == handler_ref;
    });
  }

  template <typename Event>
  void dispatch_event(const Event& event) {
    event_variant wrapped_event{std::cref(event)};
    for (auto& registered : handlers_) {
      registered.handler->handle(wrapped_event);
    }
  }

  std::vector<RegisteredHandler> handlers_;
  uint64_t next_handler_id_ = 1;
};

}  // namespace agner
