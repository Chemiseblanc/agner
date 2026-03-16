# Proof Obligations Ledger

This file records how the specification layers connect.

The goal is not a single end-to-end refinement proof inside one model. The goal is an explicit argument about which guarantees are established in the core layer, which concrete runtime choices must preserve them, and which higher-level abstractions are allowed to depend on them.

## Core Guarantees

These models establish the base actor-runtime guarantees that other layers may assume.

| Core model | Establishes |
|------------|-------------|
| `core/contract/api/invalid_actor_operations` | Missing-actor API calls either no-op or reject without corrupting runtime state. |
| `core/contract/messaging/missing_actor_send` | Sends to absent actors are safe no-ops. |
| `core/coordination/core/core_system` | Shared runtime invariants hold across broad interleavings. |
| `core/coordination/mailbox/mailbox_ordering` | Mailbox delivery preserves FIFO order for queued messages. |
| `core/coordination/mailbox/receive_suspends` | `receive()` suspends without spinning and resumes on matching delivery. |
| `core/failure/propagation/link_propagation` | Link and monitor notifications propagate to the right observers. |
| `core/failure/propagation/spawn_registration` | `spawn_link()` and `spawn_monitor()` register topology before an immediate child exit can bypass signal delivery. |
| `core/failure/propagation/exception_propagation` | Exception exits preserve failure reasons through propagated signals. |
| `core/timing/scheduler/scheduler_progress` | The scheduler contract remains implementation-agnostic: dispatch is over the ready set, time can advance independently of queue policy, and due timeouts can compete with other runnable actors. |
| `core/timing/scheduler/README.md` | The minimal scheduler contract other layers may assume: ready-set dispatch, matching-delivery wakeup, timeout arming discipline, logical-time advancement to due deadlines, and timeout eligibility without queue-draining. |
| `core/timing/timeouts/try_receive_race` | `try_receive()` observes exactly one of message or timeout. |

## Implementation Obligations

These models do not define new user-facing guarantees. They show that concrete runtime mechanisms preserve the core story rather than invalidating it.

| Implementation model | Must preserve or justify | Connection to core |
|----------------------|--------------------------|--------------------|
| `implementation/representation/identity/actor_identity` | Actor identity allocation remains unique, non-zero, and stale sends stay harmless. | Supports the core contract that sends to invalid or retired actors do not violate safety. |
| `implementation/runtime/coroutines/coroutine_lifecycle` | Coroutine suspension, resumption, continuation, and cleanup behave consistently with the scheduler-visible runtime model. | Supports the core coordination claims about blocking, wakeup, and completion behavior. |
| `implementation/runtime/work_stealing/chase_lev_deque` | The bounded single-owner/multi-stealer deque does not duplicate tasks, preserves logical contents across growth, publishes pushes only after the written slot is visible to stealers, publishes grown buffers only after the copied live range is visible, and resolves the last-item pop/steal race with a single winner. | Supports the multi-threaded scheduler witness that projects per-worker ready structures to one abstract ready surface without task duplication, stale-buffer publication, or ghost loss. |

## Refinement Bridges

These models sit between implementation evidence and the core story. They do not yet provide a full end-to-end refinement proof. They establish projection boundaries that later refinement work can strengthen.

| Refinement model | Bridges | Current status |
|------------------|---------|----------------|
| `refinement/actor_identity_core_projection` | Projects implementation-oriented actor identity allocation state onto a coarse core-facing validity and stale-reference view. | Partial bridge: proves a stable projection boundary for identity validity and stale-send safety, but not a full refinement of `shared/actor_system.tla`. |
| `refinement/identity_coroutine_core_projection` | Composes implementation-oriented actor identity allocation and coroutine lifecycle into one projected core-state record for live/pc/ready/exit-reason and stale-target visibility. | Unified identity-coroutine projection: covers bounded liveness, pc, readiness, and exit-reason projection. Mailbox, timer, pending-result, and message-lifecycle projection are composed further by `refinement/scheduler_surface_core_projection`. |
| `refinement/message_delivery_core_projection` | Projects a bounded implementation-style delivery state onto the core-facing mailbox, pending-result, observation, readiness, and message-state boundary for a collector actor. | Partial bridge: proves message-delivery projection for queued and blocked-receive wakeup paths. Broader mailbox, timeout, and topology composition is covered by the bounded aggregate `refinement/scheduler_surface_core_projection`. |
| `refinement/scheduler_surface_step_correspondence` | Couples a bounded implementation-style scheduler surface to a separate abstract `actor_system.tla` state with one explicit abstract witness step per implementation step. | Step-correspondence bridge: discharges a bounded implementation `Next` for the current queue-backed scheduler witness, bounded interleavings across scheduling, delivery, timeout, and exit cleanup, and preservation of the shared `actor_system.tla` invariants under that correspondence. The reusable boundary is now ready-surface projection plus ready add/remove effects rather than direct queue mutation. It remains bounded rather than a universal refinement theorem, but it no longer requires implementation time advancement to wait for the projected ready set to become empty. |
| `refinement/scheduler_surface_core_projection` | Composes bounded ready-surface, queued delivery, timeout delivery, and topology cleanup into one projected full core-state view. | Aggregate bridge: proves one finite queue-backed scheduler witness can preserve the main shared core invariants and produce a coherent full `actor_system.tla` state view while exposing only ready-surface projection and ready add/remove effects to the refinement layer. It is still scenario-bounded rather than a general refinement theorem over all scheduler interleavings. |
| `refinement/topology_signal_core_projection` | Projects bounded implementation-style link/monitor cleanup and signal propagation onto the core-facing topology and signal-observation boundary. | Partial bridge: proves bounded exit cleanup and signal observation for watcher/child topologies. Broader composition is covered by the bounded aggregate `refinement/scheduler_surface_core_projection`. |
| `refinement/timeout_delivery_core_projection` | Projects a bounded implementation-style timeout arming and delivery state onto the core-facing `try_receive()` boundary for a timeout actor. | Partial bridge: proves timer arming, cancellation on matching delivery, and timeout-token delivery at the core timeout boundary. Broader composition is covered by the bounded aggregate `refinement/scheduler_surface_core_projection`. |
| `refinement/coroutine_core_projection` | Projects implementation-oriented coroutine lifecycle state onto a coarse scheduler-visible core view for root tasks. | Partial bridge: proves a stable projection boundary for root coroutines, but not a full refinement of `shared/actor_system.tla`. |

## Abstraction Obligations

These higher-level models are allowed to depend on selected core guarantees. Their proof burden is to show abstraction-specific correctness assuming those guarantees.

| Abstraction model | Depends on | Additional guarantee checked here |
|-------------------|------------|----------------------------------|
| `abstractions/genserver/contract/genserver_call` | Safe send semantics, mailbox ordering, receive suspension, timeout race discipline. | Call/reply correlation and timeout outcomes for the GenServer API. |
| `abstractions/genserver/contract/cast_ordering` | Safe send semantics, mailbox ordering. | Cast delivery preserves fire-and-forget enqueue order at the server boundary. |
| `abstractions/genserver/contract/serve_dispatch` | Mailbox ordering, exit propagation, monitor propagation. | `serve()` ignores `Reply` and `DownSignal` messages while treating `ExitSignal` as a stop condition. |
| `abstractions/supervisor/contract/supervisor_admin` | Safe send semantics, actor liveness, graceful shutdown ordering. | `start_child`, `stop_child`, `restart_child`, `delete_child`, `which_children`, and `child_ref` preserve supervisor registry semantics. |
| `abstractions/supervisor/failure/supervisor_restart` | Exit propagation, exception reason preservation, base runtime liveness of monitored actors. | One-for-one restart behavior and supervisor shutdown on restart intensity limits. |
| `abstractions/supervisor/failure/supervisor_one_for_all` | Exit propagation, exception reason preservation, graceful shutdown ordering. | One-for-all restart stops and replaces the whole active child set. |
| `abstractions/supervisor/failure/supervisor_rest_for_one` | Exit propagation, exception reason preservation, graceful shutdown ordering. | Rest-for-one restart stops and replaces only the failed child and later-started siblings. |
| `abstractions/supervisor/failure/supervisor_simple_one_for_one` | Exit propagation, exception reason preservation, base runtime liveness of dynamically started children. | Dynamically spawned children restart according to the shared simple-one-for-one policy. |

## Dependency Notes

### GenServer

- Assumes core send safety so dropped or absent deliveries do not corrupt request state.
- Assumes the missing-actor API contract so invalid operational calls do not leave the runtime in a half-mutated state.
- Assumes mailbox and receive semantics so replies are consumed in the intended order and blocked callers wake correctly.
- Assumes timeout race discipline so a call cannot observe both a reply and a timeout.
- Assumes only the abstract scheduler contract from `core/timing/scheduler/README.md`, not any ready-queue order or single-threaded run-loop structure.
- Proves the higher-level request/reply contract, cast ordering contract, and `serve()` dispatch semantics on top of those assumptions.

### Supervisor

- Assumes spawn-time registration so `spawn_link()` and `spawn_monitor()` establish topology before newly started children can fail.
- Assumes core exit propagation and failure-reason preservation.
- Assumes the base runtime correctly maintains actor liveness and monitor topology.
- Assumes graceful shutdown ordering when a supervisor explicitly stops children.
- Assumes only scheduler-visible liveness and timeout behavior from `core/timing/scheduler/README.md`, not a specific ready-queue policy.
- Proves both the supervisor administrative contract and the restart-policy rules that are specific to supervision rather than to the actor runtime itself.

## Future System Layer

Models added under `systems/` should follow the same pattern:

1. Name the abstraction-layer guarantees they rely on.
2. Name any direct core guarantees they still need.
3. State the new system-level property being checked.
4. Record any assumption that is not already discharged by another model.

## Gaps To Track

- `refinement/scheduler_surface_core_projection` provides a composed aggregate bridge projecting bounded implementation-style scheduler surface state onto a coherent full core state view, though only for bounded scenarios rather than general interleavings.
- `refinement/scheduler_surface_step_correspondence` adds bounded explicit step correspondence to a separate abstract `actor_system.tla` state, but it still stops short of a universal refinement theorem over arbitrary actor counts and unbounded executions.
- The current connection is an explicit assumption/guarantee ledger rather than a full formal refinement chain.
- The refinement layer includes a bounded aggregate full-core-state projection in `refinement/scheduler_surface_core_projection`. The remaining gap is generalization to arbitrary concurrent multi-actor interleavings rather than absence of a bounded composed core-state projection.
- Future scheduler witnesses should target the abstract properties in `core/timing/scheduler/README.md` and the shared ready-surface operators in `shared/refinement_vocabulary.tla`, rather than inheriting queue order or drain-before-time assumptions from the current queue-backed witness.
- Future `systems/` models should extend this ledger as new abstraction dependencies appear.
