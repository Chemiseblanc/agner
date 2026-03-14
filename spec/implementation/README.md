# Implementation Evidence

This layer captures representation choices and concrete runtime mechanics that could invalidate the core guarantees if modeled incorrectly.

- `representation/identity/actor_identity` checks bounded actor identity allocation and stale-send safety.
- `runtime/coroutines/coroutine_lifecycle` checks the modeled coroutine lifecycle and continuation behavior.