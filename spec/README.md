# Agner Actor System Specifications

Formal TLA+ specifications for verifying the Agner actor runtime as a layered proof story.

## Verification Layers

The top level is organized by dependency layers:

- `core/` defines the actor-system guarantees that everything else depends on.
- `implementation/` contains implementation-adjacent models that show concrete runtime machinery satisfies or preserves the core guarantees.
- `abstractions/` contains higher-level runtime abstractions such as `genserver` and `supervisor`, built on top of the core layer.
- `systems/` is reserved for future models built on the abstraction layer.
- `shared/` contains the common vocabulary and actor runtime state machine used across all layers.

Within each layer, there can still be an abstraction axis such as `contract`, `coordination`, `failure`, `timing`, `runtime`, or `representation`.

## Directory Structure

```text
spec/
├── README.md
├── shared/
│   ├── actor_defs.tla
│   └── actor_system.tla
├── core/
│   ├── contract/
│   │   └── messaging/
│   │       └── missing_actor_send/
│   ├── coordination/
│   │   ├── core/
│   │   │   └── core_system/
│   │   └── mailbox/
│   │       ├── mailbox_ordering/
│   │       └── receive_suspends/
│   ├── failure/
│   │   └── propagation/
│   │       ├── exception_propagation/
│   │       └── link_propagation/
│   └── timing/
│       ├── scheduler/
│       │   └── scheduler_fairness/
│       └── timeouts/
│           └── try_receive_race/
├── implementation/
│   ├── representation/
│   │   └── identity/
│   │       └── actor_identity/
│   └── runtime/
│       └── coroutines/
│           └── coroutine_lifecycle/
├── abstractions/
│   ├── genserver/
│   │   └── contract/
│   │       └── genserver_call/
│   └── supervisor/
│       └── failure/
│           └── supervisor_restart/
└── systems/
```

Each scenario folder contains symlinks back to `shared/actor_defs.tla` and `shared/actor_system.tla`. All scenario directories now live at the same depth, so the symlink rule is uniform.

## Layer Ledger

| Layer | Axis | Scenario | Primary question |
|-------|------|----------|------------------|
| Core | Contract | `core/contract/messaging/missing_actor_send` | Is sending to an absent actor a safe no-op? |
| Core | Coordination | `core/coordination/core/core_system` | Do the core actor runtime invariants hold across broad interleavings? |
| Core | Coordination | `core/coordination/mailbox/mailbox_ordering` | Does the mailbox preserve FIFO delivery? |
| Core | Coordination | `core/coordination/mailbox/receive_suspends` | Does `receive()` suspend and resume correctly? |
| Core | Failure | `core/failure/propagation/link_propagation` | Do links and monitors observe exits correctly? |
| Core | Failure | `core/failure/propagation/exception_propagation` | Are exception exits propagated with the right reasons? |
| Core | Timing | `core/timing/scheduler/scheduler_fairness` | Does the ready-queue model preserve FIFO fairness? |
| Core | Timing | `core/timing/timeouts/try_receive_race` | Does `try_receive()` observe exactly one of message or timeout? |
| Implementation | Representation | `implementation/representation/identity/actor_identity` | Do concrete actor identity allocation rules preserve the core safety story? |
| Implementation | Runtime | `implementation/runtime/coroutines/coroutine_lifecycle` | Do coroutine state transitions preserve the runtime behavior assumed by the core specs? |
| Abstractions | Contract | `abstractions/genserver/contract/genserver_call` | Does the GenServer call/reply abstraction preserve request correlation and timeout behavior? |
| Abstractions | Failure | `abstractions/supervisor/failure/supervisor_restart` | Does supervisor restart policy behave correctly on top of the core runtime semantics? |

## Dependency Story

```text
shared/actor_defs.tla
      │
      ▼
shared/actor_system.tla
      │
      ├── core/*
      │
      ├── implementation/*   (evidence that concrete runtime choices preserve core guarantees)
      │
      ├── abstractions/*     (GenServer, Supervisor, and similar higher-level constructs)
      │
      └── systems/*          (future application or subsystem models built on abstractions)
```

This keeps the dependency ladder explicit while still allowing decomposition by abstraction axis within each layer.

## Proof Ledger

See `PROOF_LEDGER.md` for the current assumption/guarantee mapping between core models, implementation evidence, and higher-level abstractions.

## Running Model Checks

Using MCP tools:

```bash
# Parse a spec
mcp_tla_mcp_serve_tlaplus_mcp_sany_parse <spec_path.tla>

# Smoke test
mcp_tla_mcp_serve_tlaplus_mcp_tlc_smoke <spec_path.tla> <config_path.cfg>

# Full model check
mcp_tla_mcp_serve_tlaplus_mcp_tlc_check <spec_path.tla> <config_path.cfg>
```

Command line:

```bash
cd spec/<layer>/<axis>/<subgroup>/<scenario>
java -jar tla2tools.jar -modelcheck -config <scenario>.cfg <scenario>.tla
```

## Shared Model Mapping

| Spec Concept | C++ Implementation |
|--------------|-------------------|
| `Spawn(a, kind)` | `SchedulerBase::spawn_impl()` |
| `Send(target, msg)` | `SchedulerBase::send()`, `Actor::enqueue_message()` |
| `RunReadyActor(a)` | `SchedulerBase::run_actor()` |
| `ready` set | Actors with runnable coroutine handles |
| `pending_result` | Direct delivery via waiter notification |
| `timers` | `Scheduler::schedule_after()` deadlines |
| `AdvanceTime` | Deterministic scheduler logical time advancement |
| `links` / `monitors` | `SchedulerBase::link()` / `SchedulerBase::monitor()` |

## Core Invariants

The core, implementation, and abstraction layers rely on the shared runtime invariants in `actor_system.tla`, including:

- `TypeOK`
- `ReadyActorsAreLive`
- `PendingResultsAreReady`
- `BlockedActorsHaveNoMatches`
- `TimerDiscipline`
- `CompletedActorsClearedState`
- `AbsentActorsStayEmpty`
- `MessageOwnership`

## Adding New Scenarios

1. Choose the dependency layer first: `core`, `implementation`, `abstractions`, or `systems`.
2. Choose the abstraction axis within that layer.
3. Create a folder under the appropriate subgroup, for example `spec/core/failure/propagation/<scenario_name>/`.
4. Add symlinks from the scenario folder to the shared modules:
   ```bash
      cd spec/<layer>/<axis>/<subgroup>/<scenario_name>
      ln -s ../../../../shared/actor_defs.tla .
      ln -s ../../../../shared/actor_system.tla .
   ```
5. Create `<scenario_name>.tla` extending `actor_system` or, if needed, only `actor_defs`.
6. Create `<scenario_name>.cfg` with the smallest constants that still exercise the property.
7. Add a short `README.md` that states the property, the scenario, and why that dependency layer and abstraction axis are the right place for it.
