# Message Delivery Core Projection

This scenario extends the refinement layer toward the observable runtime boundary.

It projects a small implementation-oriented delivery model onto the core-facing mailbox, pending-result, observation, readiness, and message-state view used by `shared/actor_system.tla`. The model is intentionally bounded to a single collector actor and two mutually exclusive delivery paths: a queued delivery path and a blocked-receive wakeup path.

The goal is not a full mailbox refinement. The goal is to establish that a coarse implementation delivery state can project cleanly to the core variables that higher-level messaging and receive contracts depend on.