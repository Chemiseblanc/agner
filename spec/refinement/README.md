# Refinement Bridges

This layer contains bridge specs that relate implementation-oriented models back to abstract scheduler-visible views used by the core layer.

These scenarios are intentionally smaller than a full end-to-end refinement proof. Their purpose is to establish reusable projection boundaries that later system and abstraction arguments can depend on.

Common operators shared across the larger bridge specs live in `shared/refinement_vocabulary.tla`.

Unless a refinement scenario says otherwise, queue order is treated as an implementation witness rather than as part of the abstract contract. The reusable boundary is the projected `ready` set plus the projected mailbox, pending-result, timer, and topology state. Scheduler-adjacent bridges now express that witness through shared `ReadyMembers`, `ReadyAdd`, and `ReadyRemove` operators rather than direct queue mutation.

- `actor_identity_core_projection` checks a coarse projection from actor identity allocation state to core-facing validity and stale-send safety.
- `coroutine_core_projection` checks a first projection from coroutine lifecycle state to a coarse core-facing scheduler view.
- `identity_coroutine_core_projection` composes identity allocation and coroutine lifecycle into one projected core-state record covering liveness, pc shape, readiness, exit reasons, valid references, and stale targets.
- `message_delivery_core_projection` checks a bounded projection from implementation-style message delivery state to the core-facing mailbox, pending-result, observation, and message-state boundary.
- `scheduler_surface_core_projection` composes bounded ready-surface, delivery, timeout, and topology behavior into one full core-facing state view for the current queue-backed scheduler witness.
- `scheduler_surface_step_correspondence` adds a bounded implementation-state plus abstract-state correspondence model with nondeterministic interleavings across scheduling, delivery, timeout, and exit cleanup for the current queue-backed scheduler witness. The witness remains queue-backed, but it now exposes ready-surface add/remove/projection operators instead of direct queue mutation, and its time-advance step matches the abstract contract without requiring the projected ready set to drain first.
- `topology_signal_core_projection` checks a bounded projection from implementation-style link and monitor cleanup to the core-facing topology and signal-observation boundary.
- `timeout_delivery_core_projection` checks a bounded projection from implementation-style timeout arming and delivery state to the core-facing try-receive timer boundary.