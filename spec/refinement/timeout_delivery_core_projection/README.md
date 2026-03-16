# Timeout Delivery Core Projection

This scenario extends the refinement layer to the timeout boundary used by `try_receive()` and timeout actors.

It projects a small implementation-oriented timeout-delivery model onto the core-facing `pc`, `ready`, `pending_result`, `timers`, `observations`, `msg_state`, and `time` view from `shared/actor_system.tla`. The model is intentionally bounded to one timeout actor and the two mutually exclusive outcomes the core specs rely on: a direct message wakeup before the deadline, or a timeout firing at the deadline.

The goal is not a full scheduler-time refinement. The goal is to prove that timer arming, timer cancellation on matching delivery, and timeout-token delivery admit a stable coarse projection to the core timeout boundary.