# Proof Obligations Ledger

This file records how the specification layers connect.

The goal is not a single end-to-end refinement proof inside one model. The goal is an explicit argument about which guarantees are established in the core layer, which concrete runtime choices must preserve them, and which higher-level abstractions are allowed to depend on them.

## Core Guarantees

These models establish the base actor-runtime guarantees that other layers may assume.

| Core model | Establishes |
|------------|-------------|
| `core/contract/messaging/missing_actor_send` | Sends to absent actors are safe no-ops. |
| `core/coordination/core/core_system` | Shared runtime invariants hold across broad interleavings. |
| `core/coordination/mailbox/mailbox_ordering` | Mailbox delivery preserves FIFO order for queued messages. |
| `core/coordination/mailbox/receive_suspends` | `receive()` suspends without spinning and resumes on matching delivery. |
| `core/failure/propagation/link_propagation` | Link and monitor notifications propagate to the right observers. |
| `core/failure/propagation/exception_propagation` | Exception exits preserve failure reasons through propagated signals. |
| `core/timing/scheduler/scheduler_fairness` | The ready-queue scheduler model preserves FIFO dispatch structure. |
| `core/timing/timeouts/try_receive_race` | `try_receive()` observes exactly one of message or timeout. |

## Implementation Obligations

These models do not define new user-facing guarantees. They show that concrete runtime mechanisms preserve the core story rather than invalidating it.

| Implementation model | Must preserve or justify | Connection to core |
|----------------------|--------------------------|--------------------|
| `implementation/representation/identity/actor_identity` | Actor identity allocation remains unique, non-zero, and stale sends stay harmless. | Supports the core contract that sends to invalid or retired actors do not violate safety. |
| `implementation/runtime/coroutines/coroutine_lifecycle` | Coroutine suspension, resumption, continuation, and cleanup behave consistently with the scheduler-visible runtime model. | Supports the core coordination claims about blocking, wakeup, and completion behavior. |

## Abstraction Obligations

These higher-level models are allowed to depend on selected core guarantees. Their proof burden is to show abstraction-specific correctness assuming those guarantees.

| Abstraction model | Depends on | Additional guarantee checked here |
|-------------------|------------|----------------------------------|
| `abstractions/genserver/contract/genserver_call` | Safe send semantics, mailbox ordering, receive suspension, timeout race discipline. | Call/reply correlation and timeout outcomes for the GenServer API. |
| `abstractions/supervisor/failure/supervisor_restart` | Exit propagation, exception reason preservation, base runtime liveness of monitored actors. | Restart policy behavior and supervisor shutdown on restart intensity limits. |

## Dependency Notes

### GenServer

- Assumes core send safety so dropped or absent deliveries do not corrupt request state.
- Assumes mailbox and receive semantics so replies are consumed in the intended order and blocked callers wake correctly.
- Assumes timeout race discipline so a call cannot observe both a reply and a timeout.
- Proves the higher-level request/reply contract on top of those assumptions.

### Supervisor

- Assumes core exit propagation and failure-reason preservation.
- Assumes the base runtime correctly maintains actor liveness and monitor topology.
- Proves restart-policy rules that are specific to supervision rather than to the actor runtime itself.

## Future System Layer

Models added under `systems/` should follow the same pattern:

1. Name the abstraction-layer guarantees they rely on.
2. Name any direct core guarantees they still need.
3. State the new system-level property being checked.
4. Record any assumption that is not already discharged by another model.

## Gaps To Track

- There is not yet a machine-checked projection from the implementation layer back to a single abstract core state view.
- The current connection is an explicit assumption/guarantee ledger rather than a full formal refinement chain.
- Future `systems/` models should extend this ledger as new abstraction dependencies appear.
## Gap Analysis and Action Plan

Based on a review of the implemented C++ features versus the current TLA+ models, several critical gaps exist in the specification.

### 1. Missing Abstractions
- **GenServer**: The `gen_event` feature is fully implemented in C++ but lacks a corresponding TLA+ model. A model is needed to verify handler registration, removal, event delivery ordering, and failure semantics.
- **Supervisor Strategies**: The current model (`supervisor_restart`) only covers the `one_for_one` strategy. Models are required for `one_for_all`, `rest_for_one`, and `simple_one_for_one` to match the C++ implementation.
- **GenServer Cast & Info**: The `genserver_call` model is thorough for synchronous requests, but asynchronous `cast` (fire-and-forget) and general `info` message handling must also be modeled to clarify ordering and asynchronous contracts.

### 2. Missing Core Coordination and Timing
- **Graceful Shutdown Protocol**: There is currently no model for supervisor-initiated shutdown, child termination ordering, or system-wide shutdown. The timeout window and forced termination mechanics must be verified.
- **Timer Lifecycle Edge Cases**: Models are missing for multiple pending timers per actor, timer cancellation, and race conditions where an actor exits while a timer is pending.

### Action Plan
1. **Phase 1: Complete Existing Abstractions**
   - Create `spec/abstractions/gen_event/contract/handler_ordering/` to model GenServer handler contracts.
   - Expand `spec/abstractions/supervisor/failure/` to include `supervisor_one_for_all`, `supervisor_rest_for_one`, and `supervisor_simple_one_for_one`.
   - Create `spec/abstractions/genserver/contract/cast_ordering/` for GenServer asynchronous semantics.
2. **Phase 2: Core Timeout & Termination Models**
   - Create `spec/core/coordination/shutdown/graceful_shutdown/` for termination ordering and shutdown timeouts.
   - [X] Create `spec/core/timing/timers/timer_lifecycle/` to cover complex timer cancellation and actor exit races. Found and fixed UAF bug in C++.
3. **Phase 3: Advanced Systems Patterns**
   - Address multi-level supervision trees (`systems/supervision/tree_restart_cascades/`).
   - Address GenServer cascading handlers (`systems/events/handler_dependency_chains/`).
