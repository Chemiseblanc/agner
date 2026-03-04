#pragma once

#include <any>
#include <cstdint>
#include <type_traits>

#include "agner/actor_control.hpp"

namespace agner {

/// @brief Wrapper to declare handler signatures for GenServer.
/// @tparam Sigs Function signatures like int(GetCount), void(Reset).
template <typename... Sigs>
struct Handlers {};

namespace detail {

/// @brief Extracts request and response types from a handler signature.
template <typename Sig>
struct handler_traits;

template <typename Response, typename Request>
struct handler_traits<Response(Request)> {
  using request_type = Request;
  using response_type = Response;
  static constexpr bool is_call = !std::is_void_v<Response>;
  static constexpr bool is_cast = std::is_void_v<Response>;
};

/// @brief Check if a signature is a call (non-void return).
template <typename Sig>
inline constexpr bool is_call_v = handler_traits<Sig>::is_call;

/// @brief Check if a signature is a cast (void return).
template <typename Sig>
inline constexpr bool is_cast_v = handler_traits<Sig>::is_cast;

/// @brief Get request type from signature.
template <typename Sig>
using request_t = typename handler_traits<Sig>::request_type;

/// @brief Get response type from signature.
template <typename Sig>
using response_t = typename handler_traits<Sig>::response_type;

/// @brief Find response type for a request among handler signatures.
template <typename Request, typename... Sigs>
struct response_for;

template <typename Request>
struct response_for<Request> {
  // Fallback - will cause substitution failure
  using type = void;
};

template <typename Request, typename Sig, typename... Rest>
struct response_for<Request, Sig, Rest...> {
  using type = std::conditional_t<
      std::is_same_v<Request, request_t<Sig>>,
      response_t<Sig>,
      typename response_for<Request, Rest...>::type>;
};

template <typename Request, typename... Sigs>
using response_for_t = typename response_for<Request, Sigs...>::type;

/// @brief Check if a request type exists in the handler signatures.
template <typename Request, typename... Sigs>
struct has_request : std::false_type {};

template <typename Request, typename Sig, typename... Rest>
struct has_request<Request, Sig, Rest...>
    : std::conditional_t<std::is_same_v<Request, request_t<Sig>>,
                         std::true_type,
                         has_request<Request, Rest...>> {};

template <typename Request, typename... Sigs>
inline constexpr bool has_request_v = has_request<Request, Sigs...>::value;

/// @brief Check if request is a call type (non-void response).
template <typename Request, typename... Sigs>
struct is_call_request : std::false_type {};

template <typename Request, typename Sig, typename... Rest>
struct is_call_request<Request, Sig, Rest...>
    : std::conditional_t<
          std::is_same_v<Request, request_t<Sig>>,
          std::bool_constant<is_call_v<Sig>>,
          is_call_request<Request, Rest...>> {};

template <typename Request, typename... Sigs>
inline constexpr bool is_call_request_v =
    is_call_request<Request, Sigs...>::value;

/// @brief Check if request is a cast type (void response).
template <typename Request, typename... Sigs>
inline constexpr bool is_cast_request_v =
    has_request_v<Request, Sigs...> && !is_call_request_v<Request, Sigs...>;

/// @brief Find the signature for a given request type (primary template).
template <typename Request, typename Enable, typename... Sigs>
struct signature_for_impl;

/// @brief Base case - no more signatures to check.
template <typename Request, typename Enable>
struct signature_for_impl<Request, Enable> {
  // No matching signature found - should not be instantiated for valid types
};

/// @brief Matching case - Request matches the current signature.
template <typename Request, typename Sig, typename... Rest>
struct signature_for_impl<Request, std::enable_if_t<std::is_same_v<Request, request_t<Sig>>>, Sig, Rest...> {
  using type = Sig;
};

/// @brief Non-matching case - recurse to next signature.
template <typename Request, typename Sig, typename... Rest>
struct signature_for_impl<Request, std::enable_if_t<!std::is_same_v<Request, request_t<Sig>>>, Sig, Rest...>
    : signature_for_impl<Request, void, Rest...> {};

/// @brief Wrapper that provides void as the Enable parameter.
template <typename Request, typename... Sigs>
using signature_for_t = typename signature_for_impl<Request, void, Sigs...>::type;

}  // namespace detail

/// @brief Message wrapper for synchronous call requests.
template <typename Request>
struct CallMessage {
  ActorRef caller;
  uint64_t request_id;
  Request request;
};

/// @brief Message wrapper for asynchronous cast requests.
template <typename Request>
struct CastMessage {
  Request request;
};

/// @brief Reply message sent back to caller after handling a call.
struct Reply {
  uint64_t request_id;
  std::any response;
};

}  // namespace agner
