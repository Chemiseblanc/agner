#pragma once

#include <concepts>
#include <type_traits>
#include <utility>

namespace agner::detail {

template <typename Visitor, typename Message>
concept MessageVisitor = std::invocable<Visitor, Message&>;

template <typename Visitor, typename... Messages>
concept VisitorForAnyMessage = (MessageVisitor<Visitor, Messages> || ...);

template <typename... Visitors>
concept HasVisitors = (sizeof...(Visitors) > 0);

template <typename TimeoutVisitor>
concept NullTimeoutVisitor =
    std::is_same_v<std::decay_t<TimeoutVisitor>, std::nullptr_t>;

template <typename TimeoutVisitor>
concept HasTimeoutVisitor = !NullTimeoutVisitor<TimeoutVisitor>;

template <typename TimeoutVisitor, typename Result>
concept TimeoutVisitorFor =
    std::invocable<TimeoutVisitor> &&
    (std::is_void_v<Result>
         ? std::is_void_v<std::invoke_result_t<TimeoutVisitor>>
         : std::convertible_to<std::invoke_result_t<TimeoutVisitor>, Result>);

}  // namespace agner::detail
