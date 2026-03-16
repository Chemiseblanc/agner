---
name: tlaplus-modeling
description: 'Model and reason about concurrent or distributed systems with TLA+ and PlusCal. Use when: designing multithreaded or distributed behavior before code exists, deriving invariants from an informal design, writing TLA+ specs, creating PlusCal algorithms, model checking with TLC, organizing multi-module specifications, debugging verification failures, or reducing state space. Covers informal concurrency design, MCP tool usage, PlusCal preferred syntax (call/await over goto), TLA+ module organization, and state-space optimization.'
argument-hint: 'Describe the concurrent design, spec, or verification question'
---

# TLA+ Modeling and Verification

Use this skill for concurrency-oriented design and verification with TLA+ and PlusCal.

It is valid to use this skill before a formal spec exists. A good outcome can be an informal design review, a candidate state-machine sketch, a set of invariants, or a PlusCal outline that clarifies the system before you commit to a full specification.

## When to Use

- Planning a concurrent or distributed design before writing the spec
- Identifying processes, shared state, messages, failure modes, and fairness assumptions
- Deriving candidate invariants and progress claims from an informal design
- Writing new TLA+ specifications from system designs
- Translating code/pseudocode to PlusCal algorithms  
- Model checking specifications with TLC
- Debugging invariant violations or liveness failures
- Organizing related specifications that share definitions
- Understanding counterexamples from TLC
- Optimizing state space for faster verification

If the main question is whether TLA+ is the right formalism, step back and choose the method based on the dominant verification risk: temporal behavior, relational structure, protocol-state interaction, or implementation-level proof.

## Design Before the Spec

When there is no spec yet, start by naming the behavioral pieces that TLA+ will eventually need.

Extract:

- processes, actors, or roles
- shared variables and ownership boundaries
- message kinds or externally visible actions
- blocking conditions and wake-up conditions
- failure modes, retries, and timeout paths
- safety invariants and liveness expectations

Then decide whether the next artifact should be:

- a plain-language state machine,
- a PlusCal algorithm,
- a small TLA+ state transition system,
- or just a list of properties to preserve while the design is still moving.

## Tooling

Use MCP tools when available (preferred), otherwise fall back to command line.

### MCP Tools (Preferred)

| Tool | Purpose |
|------|---------|
| `mcp_tla_mcp_serve_tlaplus_mcp_sany_parse` | Parse and type-check a TLA+ module |
| `mcp_tla_mcp_serve_tlaplus_mcp_sany_symbol` | Extract CONSTANTS, Spec, invariants for config generation |
| `mcp_tla_mcp_serve_tlaplus_mcp_sany_modules` | List available standard modules to extend |
| `mcp_tla_mcp_serve_tlaplus_mcp_tlc_check` | Exhaustive model checking |
| `mcp_tla_mcp_serve_tlaplus_mcp_tlc_smoke` | Quick random simulation (smoke test) |
| `mcp_tla_mcp_serve_tlaplus_mcp_tlc_explore` | Generate sample behaviors for understanding |
| `mcp_tla_mcp_serve_tlaplus_mcp_tlc_trace` | Replay and analyze counterexample traces |

Always provide **fully qualified file paths** to MCP tools.

### Command Line Fallback

```bash
# Parse with SANY
java -cp tla2tools.jar tla2sany.SANY MySpec.tla

# Model check with TLC  
java -jar tla2tools.jar -modelcheck MySpec.tla

# With config file
java -jar tla2tools.jar -modelcheck -config MySpec.cfg MySpec.tla

# Simulation mode (smoke test)
java -jar tla2tools.jar -simulate MySpec.tla
```

## PlusCal Guidelines

**Prefer PlusCal over raw TLA+** for algorithm specifications—it's easier for developers to read and maps directly to imperative code patterns.

### Use `call` and `await` (Preferred)

```pluscal
\* PREFERRED: Structured procedure calls
procedure DoWork(input)
variable result;
begin
  await resource_available;
  result := ProcessInput(input);
  return;
end procedure;

process Worker \in Workers
begin
  MainLoop:
    while TRUE do
      call DoWork(task);
    end while;
end process;
```

### Avoid Raw `goto` (Discouraged)

```pluscal
\* DISCOURAGED: goto creates spaghetti control flow
process Worker \in Workers
begin
  Start:
    if resource_available then goto Process;
    else goto Start;
    end if;
  Process:
    result := ProcessInput(task);
    goto Start;
end process;
```

### PlusCal Syntax Summary

```pluscal
---- MODULE MyAlgorithm ----
EXTENDS Naturals, Sequences, TLC

CONSTANTS NumWorkers, MaxItems

(*--algorithm my_algorithm

variables
  queue = <<>>,
  processed = {};

define
  \* Definitions visible to both PlusCal and TLA+
  QueueNotEmpty == Len(queue) > 0
  AllProcessed == processed = Items
end define;

macro enqueue(item)
begin
  queue := Append(queue, item);
end macro;

procedure process_item(item)
begin
  ProcessStart:
    processed := processed \cup {item};
    return;
end procedure;

fair process worker \in 1..NumWorkers
variable current = <<>>;
begin
  WorkerLoop:
    while TRUE do
      await QueueNotEmpty;   \* Block until condition holds
      current := Head(queue);
      queue := Tail(queue);
      call process_item(current);  \* Structured call
    end while;
end process;

end algorithm; *)

\* TLA+ translation appears below after running PlusCal translator
====
```

### Key PlusCal Constructs

| Construct | Use For |
|-----------|---------|
| `await expr` | Block process until `expr` is true (preferred over spin-loops) |
| `call proc(args)` | Structured procedure invocation with automatic return |
| `with x \in Set do` | Nondeterministic choice from set |
| `either ... or ... end either` | Nondeterministic branch |
| `macro` | Inline expansion without separate labels (no atomicity break) |
| `procedure` | Subroutine with own variables and labeled steps |
| `fair process` | Weak fairness (process eventually runs if continuously enabled) |
| `fair+ process` | Strong fairness (if repeatedly enabled, eventually runs) |

## Module Organization

For related TLA+ models that share definitions, use a shared library module pattern and keep each scenario focused on one verification question.

```
spec/
├── actor_defs.tla       # Shared: types, operators, helper functions
├── basic_actor.tla      # EXTENDS actor_defs - simple scenario
├── basic_actor.cfg
├── actor_linking.tla    # EXTENDS actor_defs - link/monitor behavior  
├── actor_linking.cfg
└── actor_supervision.tla # EXTENDS actor_defs - supervisor trees
    actor_supervision.cfg
```

### Shared Definitions Module

```tla
---- MODULE actor_defs ----
EXTENDS Naturals, Sequences, FiniteSets

\* Shared constants (parameterized per-config)
CONSTANTS ActorPool, MessageIds

\* Shared type definitions
MessageKinds == {"Ping", "Pong", "Exit"}
Message == [id: MessageIds, kind: MessageKinds, payload: Nat]

\* Shared operators
SendMsg(mailboxes, actor, msg) ==
  [mailboxes EXCEPT ![actor] = Append(@, msg)]

ReceiveMsg(mailboxes, actor) ==
  [mailboxes EXCEPT ![actor] = Tail(@)]

\* Shared predicates
HasMessages(mailboxes, actor) ==
  Len(mailboxes[actor]) > 0

====
```

### Scenario Module

```tla
---- MODULE basic_actor ----
EXTENDS actor_defs, TLC

\* Scenario-specific definitions
VARIABLES mailboxes, live, processed

Init ==
  /\ mailboxes = [a \in ActorPool |-> <<>>]
  /\ live = ActorPool
  /\ processed = {}

\* ... actions using shared operators from actor_defs ...

Spec == Init /\ [][Next]_vars
====
```

### Configuration per Scenario

Each scenario has its own `.cfg` file with small constants:

```
SPECIFICATION Spec

CONSTANTS
  ActorPool = {a1, a2}
  MessageIds = {m1, m2}

INVARIANTS
  TypeOK
  SafetyProperty

CHECK_DEADLOCK FALSE
```

## Verification Workflow

### 1. Parse First
Always parse before model checking to catch syntax errors:
```
Use: mcp_tla_mcp_serve_tlaplus_mcp_sany_parse
```

### 2. Extract Symbols for Config
Generate proper config by examining what the spec defines:
```
Use: mcp_tla_mcp_serve_tlaplus_mcp_sany_symbol
```

### 3. Smoke Test for Quick Feedback
Before exhaustive checking, run simulation to find obvious bugs:
```
Use: mcp_tla_mcp_serve_tlaplus_mcp_tlc_smoke
```

### 4. Explore Sample Behaviors
Understand what the spec does by generating traces:
```
Use: mcp_tla_mcp_serve_tlaplus_mcp_tlc_explore
behaviorLength: 5-20 (based on expected interesting behavior depth)
```

### 5. Exhaustive Model Check
Full verification with small constant values:
```
Use: mcp_tla_mcp_serve_tlaplus_mcp_tlc_check
```
If an exhaustive check takes longer than 60 seconds, treat the model as too large and simplify it before pushing further.

### 6. Analyze Counterexamples
When TLC finds a violation:
```
Use: mcp_tla_mcp_serve_tlaplus_mcp_tlc_trace
- Create ALIAS expressions for readable output
- Step through state transitions
```

## Common Patterns

### Mailbox/Queue Pattern
```tla
Send(dest, msg) ==
  mailboxes' = [mailboxes EXCEPT ![dest] = Append(@, msg)]

Receive(actor) ==
  /\ Len(mailboxes[actor]) > 0
  /\ mailboxes' = [mailboxes EXCEPT ![actor] = Tail(@)]
```

### State Machine Pattern
```tla
PcStates == {"init", "running", "done"}

\* Transition
StartRunning(actor) ==
  /\ pc[actor] = "init"
  /\ pc' = [pc EXCEPT ![actor] = "running"]
```

### Timer/Timeout Pattern
```tla
\* Absolute deadline times
timers \in [Actors -> TimeValues \cup {NoDeadline}]

SetTimer(actor, delay) ==
  timers' = [timers EXCEPT ![actor] = time + delay]

TimerFires(actor) ==
  /\ timers[actor] # NoDeadline
  /\ time >= timers[actor]
  /\ timers' = [timers EXCEPT ![actor] = NoDeadline]
```

### Nondeterministic Choice
```tla
\* Choose any enabled actor
RunActor ==
  \E actor \in ready :
    ActorStep(actor)

\* With PlusCal:
with actor \in ready do
  \* process actor
end with;
```

## Debugging Tips

### Invariant Violations
1. Use `tlc_trace` to replay the counterexample
2. Add ALIAS in config to show derived values
3. Look at the transition that caused the violation

### Stuttering/Deadlock Issues  
- Ensure at least one action is always enabled
- Check for unintended blocking conditions
- Use `CHECK_DEADLOCK FALSE` if deadlock is expected

### State Space Explosion
See [State Space Optimization](./references/state-space.md) for techniques to reduce state space without losing model fidelity.
In particular, model machine integers as bounded signed/unsigned domains with the intended overflow behavior instead of using unbounded `Int`/`Nat`, and prefer the smallest bit width that preserves the validity of the properties under verification.
Small constant sets alone are not sufficient if the transition system still contains unbounded growth. A scenario with a two-element actor pool can still be effectively infinite if it accumulates ever-growing sequences (for example, observation logs or mailboxes) or monotone counters that are not structurally capped.
For focused scenario models, prefer a bounded phase-driven state machine over arbitrary repeated behavior. If the property only needs evidence that something can happen once or in a short fixed sequence, encode that directly with a small control variable such as `phase \in {"init", "spawned", "retired", "done"}` and cap auxiliary counters to the minimum needed by the property.
When a model does not finish quickly, first ask which variables can grow without bound across steps. Typical culprits are append-only histories, retry counters, message IDs allocated forever, and bookkeeping fields that record how many times an action happened. Replace those with bounded summaries or single-step witnesses unless the property explicitly depends on the full history.

## References

- [State Space Optimization](./references/state-space.md) — Techniques for smaller models
- [PlusCal Idioms](./references/pluscal-idioms.md) — Common patterns and recipes
- [TLA+ Tools Documentation](https://lamport.azurewebsites.net/tla/tools.html)
- [PlusCal Manual](https://lamport.azurewebsites.net/tla/p-manual.pdf)
- [Specifying Systems (Lamport)](https://lamport.azurewebsites.net/tla/book.html)
