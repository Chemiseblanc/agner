#pragma once

#include <compare>
#include <cstdint>
#include <type_traits>
#include <variant>

namespace agner {

/// @brief Unique identifier for an actor instance.
struct ActorRef {
  /// @brief Construct an ActorRef with an optional value.
  constexpr explicit ActorRef(uint64_t actor_value = 0) noexcept
      : value(actor_value) {}

  uint64_t value{};  ///< Internal actor identifier.

  /// @brief Check if this reference points to a valid actor.
  constexpr bool valid() const noexcept { return value != 0; }

  friend bool operator==(const ActorRef&, const ActorRef&) = default;
  friend std::strong_ordering operator<=>(const ActorRef&,
                                          const ActorRef&) = default;
};

/// @brief Typed reference to an actor instance.
template <typename ActorType>
class ActorHandle {
 public:
  constexpr ActorHandle() noexcept = default;
  constexpr explicit ActorHandle(ActorRef actor_ref) noexcept
      : actor_ref_(actor_ref) {}

  constexpr ActorRef ref() const noexcept { return actor_ref_; }
  constexpr bool valid() const noexcept { return actor_ref_.valid(); }

  constexpr operator ActorRef() const noexcept { return actor_ref_; }

  friend bool operator==(const ActorHandle&, const ActorHandle&) = default;
  friend std::strong_ordering operator<=>(const ActorHandle&,
                                          const ActorHandle&) = default;

 private:
  ActorRef actor_ref_{};
};

namespace detail {

template <typename Message, typename Variant>
struct variant_contains;

template <typename Message, typename... Messages>
struct variant_contains<Message, std::variant<Messages...>>
    : std::bool_constant<(std::is_same_v<std::decay_t<Message>, Messages> ||
                          ...)> {};

template <typename ActorType, typename Message>
concept MessageForActor =
    variant_contains<Message, typename ActorType::message_variant>::value;

}  // namespace detail

/// @brief Describes why an actor exited.
struct ExitReason {
  /// @brief Exit reason categories.
  enum class Kind {
    normal,   ///< Actor completed normally.
    stopped,  ///< Actor was explicitly stopped.
    error     ///< Actor exited due to an error.
  } kind = Kind::normal;
};

/// @brief Abstract base providing actor lifecycle control.
class ActorControl {
 public:
  virtual ~ActorControl() = default;

  /// @brief Get this actor's reference.
  ActorRef actor_ref() const noexcept { return actor_ref_; }

  /// @brief Set this actor's reference (called by scheduler).
  void set_actor_ref(ActorRef actor_ref) noexcept { actor_ref_ = actor_ref; }

  /// @brief Request the actor to stop.
  virtual void stop(ExitReason reason = {}) = 0;

  /// @brief Get the actor's exit reason.
  virtual ExitReason exit_reason() const noexcept = 0;

 private:
  ActorRef actor_ref_{};
};

/// @brief Signal sent to linked actors when one exits.
struct ExitSignal {
  ActorRef from;      ///< The actor that exited.
  ExitReason reason;  ///< Why the actor exited.
};

/// @brief Signal sent to monitoring actors when a monitored actor exits.
struct DownSignal {
  ActorRef from;      ///< The actor that exited.
  ExitReason reason;  ///< Why the actor exited.
};

}  // namespace agner
