#pragma once

#include <compare>
#include <cstdint>

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
