#pragma once

#include <compare>
#include <cstdint>

namespace agner {

struct ActorRef {
  constexpr explicit ActorRef(uint64_t actor_value = 0) noexcept
      : value(actor_value) {}

  uint64_t value{};

  constexpr bool valid() const noexcept { return value != 0; }

  friend bool operator==(const ActorRef&, const ActorRef&) = default;
  friend std::strong_ordering operator<=>(const ActorRef&,
                                          const ActorRef&) = default;
  friend bool operator<(const ActorRef& left, const ActorRef& right) noexcept {
    return left.value < right.value;
  }
};

struct ExitReason {
  enum class Kind { normal, stopped, error } kind = Kind::normal;
};

class ActorControl {
 public:
  virtual ~ActorControl() = default;

  ActorRef actor_ref() const noexcept { return actor_ref_; }
  void set_actor_ref(ActorRef actor_ref) noexcept { actor_ref_ = actor_ref; }

  virtual void stop(ExitReason reason = {}) = 0;

  virtual ExitReason exit_reason() const noexcept = 0;

 private:
  ActorRef actor_ref_{};
};

struct ExitSignal {
  ActorRef from;
  ExitReason reason;
};

struct DownSignal {
  ActorRef from;
  ExitReason reason;
};

}  // namespace agner
