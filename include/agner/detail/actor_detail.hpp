#pragma once

#include <any>
#include <cassert>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>
#include <variant>

#include "agner/actor_concepts.hpp"

namespace agner::detail {

template <typename... Types>
struct type_list {};

template <typename List>
struct list_size;

template <typename... Types>
struct list_size<type_list<Types...>>
    : std::integral_constant<std::size_t, sizeof...(Types)> {};

template <typename List>
struct list_front;

template <typename Head, typename... Tail>
struct list_front<type_list<Head, Tail...>> {
  using type = Head;
};

template <typename List, typename Type>
struct list_contains;

template <typename Type>
struct list_contains<type_list<>, Type> : std::false_type {};

template <typename Head, typename... Tail, typename Type>
struct list_contains<type_list<Head, Tail...>, Type>
    : std::conditional_t<std::is_same_v<Head, Type>, std::true_type,
                         list_contains<type_list<Tail...>, Type>> {};

template <typename List, typename Type>
struct list_push;

template <typename... Types, typename Type>
struct list_push<type_list<Types...>, Type> {
  using type = type_list<Types..., Type>;
};

template <typename List, typename Type>
using list_push_unique_t =
    std::conditional_t<list_contains<List, Type>::value, List,
                       typename list_push<List, Type>::type>;

template <typename List, typename Visitor, typename Message>
struct add_result_if_invocable {
  using type = List;
};

template <typename List, typename Visitor, typename Message>
  requires MessageVisitor<Visitor, Message>
struct add_result_if_invocable<List, Visitor, Message> {
  using type =
      list_push_unique_t<List, std::invoke_result_t<Visitor, Message&>>;
};

template <typename List, typename Visitor, typename... Messages>
struct collect_results_for_messages;

template <typename List, typename Visitor>
struct collect_results_for_messages<List, Visitor> {
  using type = List;
};

template <typename List, typename Visitor, typename Message,
          typename... Messages>
struct collect_results_for_messages<List, Visitor, Message, Messages...> {
  using next = typename add_result_if_invocable<List, Visitor, Message>::type;
  using type =
      typename collect_results_for_messages<next, Visitor, Messages...>::type;
};

template <typename List, typename... Visitors>
struct collect_results_for_visitors;

template <typename List>
struct collect_results_for_visitors<List> {
  template <typename... Messages>
  using type = List;
};

template <typename List, typename Visitor, typename... Visitors>
struct collect_results_for_visitors<List, Visitor, Visitors...> {
  template <typename... Messages>
  using type = typename collect_results_for_visitors<
      typename collect_results_for_messages<List, Visitor, Messages...>::type,
      Visitors...>::template type<Messages...>;
};

template <typename MessageList, typename... Visitors>
struct receive_result;

template <typename... Messages, typename... Visitors>
struct receive_result<type_list<Messages...>, Visitors...> {
  using list = typename collect_results_for_visitors<
      type_list<>, Visitors...>::template type<Messages...>;

  static constexpr std::size_t count = list_size<list>::value;
  static_assert(count > 0, "receive requires at least one matching visitor.");
  static_assert(count == 1, "Visitors must share a single common return type.");

  using type = typename list_front<list>::type;
};

template <typename Variant>
struct variant_types;

template <typename... Types>
struct variant_types<std::variant<Types...>> {
  using type = type_list<Types...>;
};

template <typename Variant, typename... Visitors>
using receive_result_t =
    typename receive_result<typename variant_types<Variant>::type,
                            Visitors...>::type;

template <typename Variant, typename List>
struct receive_result_from_list;

template <typename Variant, typename... Visitors>
struct receive_result_from_list<Variant, type_list<Visitors...>> {
  using type = receive_result_t<Variant, Visitors...>;
};

template <typename Variant, typename List>
using receive_result_from_list_t =
    typename receive_result_from_list<Variant, List>::type;

bool invoke_visitor(auto& variant, auto& visitor, auto& storage) {
  bool matched = false;
  std::visit(
      [&](auto& message) {
        using Message = std::decay_t<decltype(message)>;
        if constexpr (MessageVisitor<decltype(visitor), Message>) {
          if constexpr (std::is_same_v<std::decay_t<decltype(storage)>,
                                       std::monostate>) {
            std::invoke(visitor, message);
          } else {
            storage = std::invoke(visitor, message);
          }
          matched = true;
        }
      },
      variant);
  return matched;
}

template <typename... Visitors>
bool try_match_visitors(auto& variant, auto& storage, Visitors&... visitors) {
  return (invoke_visitor(variant, visitors, storage) || ...);
}

template <typename ActorType, typename Message>
bool try_send_any(const std::shared_ptr<ActorType>& actor, std::any& message) {
  using Decayed = std::decay_t<Message>;
  if (message.type() != typeid(Decayed)) {
    return false;
  }
  actor->send(std::any_cast<Decayed>(std::move(message)));
  return true;
}

template <typename ActorType, typename... Messages>
void dispatch_any_message(const std::shared_ptr<ActorType>& actor,
                          std::any&& message, type_list<Messages...>) {
  [[maybe_unused]] bool matched =
      (try_send_any<ActorType, Messages>(actor, message) || ...);
}

template <typename ActorType>
void dispatch_any_message(const std::shared_ptr<ActorType>& actor,
                          std::any&& message) {
  using list =
      typename variant_types<typename ActorType::message_variant>::type;
  dispatch_any_message(actor, std::move(message), list{});
}

}  // namespace agner::detail
