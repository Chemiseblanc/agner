#pragma once

#include <cstddef>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace agner {

template <std::size_t Index>
using ChildIndex = std::integral_constant<std::size_t, Index>;

template <typename ActorType, typename... Args>
struct ChildSpec;

namespace detail {

template <typename T>
struct is_child_index : std::false_type {};

template <std::size_t Index>
struct is_child_index<std::integral_constant<std::size_t, Index>>
    : std::true_type {};

template <typename T>
inline constexpr bool is_child_index_v = is_child_index<T>::value;

template <typename ActorType, typename Spec>
struct is_spec_for : std::false_type {};

template <typename ActorType, typename... Args>
struct is_spec_for<ActorType, ChildSpec<ActorType, Args...>> : std::true_type {
};

template <typename ActorType, typename... Specs>
struct spec_count;

template <typename ActorType>
struct spec_count<ActorType> : std::integral_constant<std::size_t, 0> {};

template <typename ActorType, typename Spec, typename... Rest>
struct spec_count<ActorType, Spec, Rest...>
    : std::integral_constant<std::size_t,
                             (is_spec_for<ActorType, Spec>::value ? 1 : 0) +
                                 spec_count<ActorType, Rest...>::value> {};

template <typename ActorType, std::size_t Index, typename... Specs>
struct spec_index_impl;

template <typename ActorType, std::size_t Index, typename Spec,
          typename... Rest>
struct spec_index_impl<ActorType, Index, Spec, Rest...> {
  static constexpr std::size_t value =
      is_spec_for<ActorType, Spec>::value
          ? Index
          : spec_index_impl<ActorType, Index + 1, Rest...>::value;
};

template <typename ActorType, std::size_t Index>
struct spec_index_impl<ActorType, Index> {
  static constexpr std::size_t value = Index;
};

template <typename ActorType, typename... Specs>
constexpr std::size_t spec_index_v =
    spec_index_impl<ActorType, 0, Specs...>::value;

// Resolves a Selector (either ChildIndex<N> or ActorType) to a spec index.
// For type-based selectors, asserts that exactly one matching spec exists.
template <typename Selector, typename... Specs>
constexpr std::size_t resolve_selector_index() {
  if constexpr (is_child_index_v<Selector>) {
    return Selector::value;
  } else {
    constexpr std::size_t count = spec_count<Selector, Specs...>::value;
    static_assert(count == 1,
                  "Selector must match exactly one child specification.");
    return spec_index_v<Selector, Specs...>;
  }
}

// Returns a copy if T is copy-constructible, otherwise moves.
// Note: Move-only args mean a child can only be started once (e.g., for
// temporary children). Permanent children should use copyable args.
auto clone_or_move(auto& value) {
  using T = std::decay_t<decltype(value)>;
  if constexpr (std::is_copy_constructible_v<T>) {
    return value;
  } else {
    return std::move(value);
  }
}

template <typename SchedulerType>
auto scheduler_now(SchedulerType& scheduler) {
  using Clock = typename std::decay_t<SchedulerType>::Clock;
  if constexpr (requires { scheduler.now(); }) {
    return scheduler.now();
  } else {
    return Clock::now();
  }
}

// Generic index visitor using fold expressions. Calls f.template operator()<I>()
// for exactly one index I matching target_index. Returns true if index was valid.
template <std::size_t N, typename F, std::size_t... Is>
bool visit_at_index_impl(std::size_t target_index, F&& f,
                         std::index_sequence<Is...>) {
  return ((Is == target_index ? (f.template operator()<Is>(), true) : false) ||
          ...);
}

template <std::size_t N, typename F>
bool visit_at_index(std::size_t target_index, F&& f) {
  return visit_at_index_impl<N>(target_index, std::forward<F>(f),
                                std::make_index_sequence<N>{});
}

}  // namespace detail

}  // namespace agner
