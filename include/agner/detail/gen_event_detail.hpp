#pragma once

#include <functional>
#include <type_traits>

namespace agner {

/// @brief Wrapper to declare supported event message types for GenEvent.
/// @tparam Events Event message types.
template <typename... Events>
struct EventHandlers {};

namespace detail {

/// @brief Check whether Event is in the supported event type list.
template <typename Event, typename... Events>
inline constexpr bool is_event_v = (std::is_same_v<Event, Events> || ...);

/// @brief Check whether a handler can process an event type.
template <typename Handler, typename Event>
inline constexpr bool handles_event_v =
    std::is_invocable_r_v<void, Handler&, const Event&>;

/// @brief Constraint for handlers that process at least one supported event.
template <typename Handler, typename... Events>
concept EventHandlerFor = (handles_event_v<Handler, Events> || ...);

/// @brief Invoke handler only when it supports the concrete event type.
template <typename Handler, typename Event>
void invoke_handler_if_supported(Handler& handler, const Event& event) {
  if constexpr (handles_event_v<Handler, Event>) {
    std::invoke(handler, event);
  }
}

}  // namespace detail

}  // namespace agner
