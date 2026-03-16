# Scheduler Contract

This directory records the scheduler properties the core and abstraction layers are allowed to depend on.

The key point is that these properties are expressed at the abstract scheduler-visible boundary from `shared/actor_system.tla`, not in terms of a particular queue data structure or a single-threaded run loop. A future multi-threaded scheduler can satisfy the same proof story as long as it preserves the properties below.

## Required Properties

- Ready-set dispatch: a scheduler step that runs an actor must choose an actor from the current `ready` set. The model does not require FIFO, round-robin, or any other queue order.
- Readiness soundness: actors in `ready` are live, and any direct pending delivery still projects as a ready actor with a matching `pending_result`.
- Matching-delivery wakeup: if a blocked actor receives a matching message, the scheduler-visible effect is that the actor becomes ready and the timeout deadline for a blocked `try_receive()` is cancelled.
- Timeout arming discipline: receive timeouts are only armed for blocked `try` actors with no pending result, and armed deadlines are at or after the current logical time.
- Logical-time progress: logical time may advance to the next active deadline even when other actors remain runnable. Queue draining is not part of the contract.
- Timeout eligibility: when logical time reaches a deadline, `TimeoutFire` may make the blocked timeout actor ready without first draining other runnable actors.
- Single-outcome timeout race: a timed receive projects exactly one observable outcome, either the matching message or `TimeoutToken`.

## Explicit Non-Requirements

- No FIFO ready-queue requirement.
- No single ready queue requirement.
- No single-threaded dispatcher requirement.
- No core fairness or starvation-freedom guarantee beyond whatever a scenario states explicitly.
- No requirement that time advancement waits for the runnable set to become empty.

## Current Refinement Status

- `scheduler_progress` checks the abstract contract above.
- `timeout_delivery_core_projection` and `message_delivery_core_projection` already forget queue order and now express their scheduler witness only through ready-surface projection and ready add/remove effects.
- `scheduler_surface_core_projection` and `scheduler_surface_step_correspondence` still use a queue-backed implementation surface as one witness for the current runtime, but their reusable boundary is now the shared ready-surface operators in `shared/refinement_vocabulary.tla` rather than direct queue mutation.
- `scheduler_surface_step_correspondence` now allows implementation time to advance with ready actors still present, matching the abstract scheduler contract while exposing only ready-surface add/remove/projection effects to the refinement layer.