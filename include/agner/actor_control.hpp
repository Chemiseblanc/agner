#pragma once

#include <memory>
#include <vector>

namespace agner {

struct ExitReason {
  enum class Kind { normal, stopped, error } kind = Kind::normal;
};

class Scheduler;

class ActorControl : public std::enable_shared_from_this<ActorControl> {
 public:
  virtual ~ActorControl() = default;

  void link(const std::shared_ptr<ActorControl>& other) {
    if (!other) {
      return;
    }
    add_link(other);
    other->add_link(shared_from_this());
  }

  void monitor(const std::shared_ptr<ActorControl>& other) {
    if (!other) {
      return;
    }
    other->add_monitor(shared_from_this());
  }

  virtual void stop(ExitReason reason = {}) = 0;
  virtual void deliver_exit(const ExitReason& reason,
                            const std::shared_ptr<ActorControl>& from) = 0;
  virtual void deliver_down(const ExitReason& reason,
                            const std::shared_ptr<ActorControl>& from) = 0;

 protected:
  void add_link(const std::shared_ptr<ActorControl>& other) {
    links_.push_back(other);
  }

  void add_monitor(const std::shared_ptr<ActorControl>& other) {
    monitors_.push_back(other);
  }

  void notify_exit(const ExitReason& reason) {
    auto self = shared_from_this();
    for (auto& weak_link : links_) {
      if (auto link = weak_link.lock()) {
        link->deliver_exit(reason, self);
      }
    }
    for (auto& weak_monitor : monitors_) {
      if (auto monitor = weak_monitor.lock()) {
        monitor->deliver_down(reason, self);
      }
    }
  }

 private:
  std::vector<std::weak_ptr<ActorControl>> links_;
  std::vector<std::weak_ptr<ActorControl>> monitors_;
};

struct ExitSignal {
  std::weak_ptr<ActorControl> from;
  ExitReason reason;
};

struct DownSignal {
  std::weak_ptr<ActorControl> from;
  ExitReason reason;
};

}  // namespace agner
