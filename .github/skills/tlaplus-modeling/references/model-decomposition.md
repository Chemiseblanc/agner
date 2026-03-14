# Model Decomposition

How to break a program into a series of TLA+ or PlusCal models at different abstraction levels, and how to connect those models with the right properties.

## Core Principle

Do not try to prove everything in one giant specification.

Build a small family of models instead. Each model should answer one class of questions at one abstraction level, with only the detail needed for those questions.

This gives you:

- smaller state spaces,
- clearer counterexamples,
- better isolation of bugs and missing assumptions,
- a cleaner path from high-level intent to implementation-adjacent behavior.

## What to Decompose By

Large systems usually need decomposition across one or more of these axes:

- protocol semantics: what messages or API actions mean,
- coordination semantics: how concurrent components interleave,
- failure semantics: crashes, retries, supervision, cancellation,
- time semantics: deadlines, timeouts, fairness, scheduling,
- data representation: bounds, integer width, queue capacity, overflow.

If one model tries to cover all of them at once, it usually becomes too large and too hard to understand.

## Recommended Model Ladder

The most useful pattern is a sequence of models from abstract to concrete.

### 1. Contract Model

Model only the externally visible behavior.

Include:

- abstract operations,
- abstract state transitions,
- minimal nondeterminism required by the interface.

Check properties such as:

- safety invariants,
- allowed state transitions,
- eventual response or completion at the API level.

Do not include:

- queues,
- scheduler behavior,
- retries,
- exact message formats,
- machine-sized arithmetic.

### 2. Coordination Model

Add concurrency structure and interaction between components.

Include:

- processes or actors,
- message passing,
- mailbox or channel behavior,
- ordering rules,
- blocking points.

Check properties such as:

- no lost messages,
- no duplicated processing,
- mailbox ordering,
- absence of bad interleavings,
- protocol progress under fair scheduling assumptions.

Still avoid:

- low-level resource constraints unless they matter to the property,
- implementation-only bookkeeping.

### 3. Failure and Recovery Model

Add the failure behavior that matters to correctness.

Include:

- crashes,
- exit propagation,
- supervision or restart policy,
- cancellation,
- timeout outcomes.

Check properties such as:

- failures do not violate safety,
- restart restores required invariants,
- linked or monitored components observe failures correctly,
- retries do not create duplicates or leaks.

### 4. Resource and Timing Model

Add finite resources and explicit time only when needed.

Include:

- bounded queues,
- timer states,
- deadlines,
- scheduler fairness assumptions,
- rate limits or capacity bounds.

Check properties such as:

- bounded waiting,
- no overflow of resource pools,
- timeout behavior,
- fairness-sensitive liveness.

### 5. Implementation-Adjacent Model

Add concrete representation details that could invalidate the higher-level argument.

Include:

- fixed-width signed or unsigned integers,
- overflow behavior,
- exact bounded capacities,
- specific restart or batching rules,
- representation choices that affect correctness.

Check properties such as:

- no correctness break at numeric boundaries,
- wraparound or saturation behaves as intended,
- concrete limits still preserve contract-level safety.

Do not treat this as the only model. It should validate implementation-sensitive details, not replace the more abstract proofs.

## Assign Properties to the Right Level

Every important property should live at the highest level where it makes sense.

That usually means:

- prove API or protocol safety in the contract model,
- prove ordering and interleaving properties in the coordination model,
- prove crash and recovery behavior in the failure model,
- prove timeout and fairness behavior in the timing model,
- prove representation-sensitive edge cases in the implementation-adjacent model.

Do not force a low-level model to carry every property from every higher level. Carry down only the properties that should still hold after adding more detail.

## Tie Models Together

The models should not be isolated documents. Connect them explicitly.

### Shared Vocabulary Module

Put common names, types, and helper operators in a shared definitions module.

```tla
---- MODULE shared_defs ----
EXTENDS FiniteSets, Sequences

CONSTANTS Clients, Servers, Requests

Statuses == {"idle", "busy", "done", "failed"}

RequestIds == Requests

====
```

This does not prove anything by itself. It keeps related models aligned so that names and domains do not drift.

### State Projection

Define how a more concrete state maps onto a more abstract one.

```tla
\* In the concrete model
AbsView ==
  [ status |-> IF worker_failed THEN "failed"
              ELSE IF current = None THEN "idle"
              ELSE "busy",
    completed |-> completed ]
```

Then phrase the abstract model in terms of records or operators that can consume that projected state.

```tla
\* In the abstract model
AbsTypeOK(s) ==
  /\ s.status \in Statuses
  /\ s.completed \subseteq RequestIds

AbsNoResurrection(s) ==
  s.status = "failed" => UNCHANGED s.status
```

The point is not that the concrete variables match exactly. The point is that the concrete state can be viewed as an abstract state on which the abstract properties still make sense.

### Property Preservation

For each lower-level model, ask which higher-level properties must still hold after projection.

Typical checks:

- the concrete initial states map to valid abstract initial states,
- each concrete step maps to an abstract step or an abstract stutter,
- concrete invariants imply the abstract invariants after projection.

In practice, TLC often checks this most effectively by carrying a projected view or shadow abstract state inside the concrete model and asserting invariants over it.

### Assumption/Guarantee Boundaries

Sometimes two models are siblings rather than a strict abstraction chain.

Example:

- a protocol model assumes in-order delivery,
- a transport model proves in-order delivery under bounded retransmission.

Write that connection down explicitly:

- Model A assumes property `P`.
- Model B proves property `P` under assumptions `Q`.
- The combined argument is valid only if `Q` is acceptable for the system you care about.

This is often clearer than forcing both concerns into one larger specification.

## A Practical Workflow

### Start from the Highest-Level Claim

Write down the most important user-visible property first.

Examples:

- every request gets at most one response,
- a linked actor exits when its parent crashes,
- a restart never loses durable state,
- a mailbox preserves FIFO order.

Create the smallest model that can express and check that claim.

### Add Detail Only When a New Question Appears

Do not add queues, timers, scheduler rules, or machine integers until the property requires them.

Each new model should exist because of a new verification question, not because the implementation contains more detail.

### Keep a Property Ledger

Maintain a short mapping from properties to models.

Example:

| Property | Checked In | Depends On |
|----------|------------|------------|
| No duplicate responses | Contract model | None beyond API assumptions |
| FIFO mailbox delivery | Coordination model | Mailbox semantics |
| Crash propagates to links | Failure model | Link relation, crash semantics |
| Timeout eventually fires | Timing model | Fair time advance |
| Counter wraparound is safe | Implementation-adjacent model | Fixed-width integer semantics |

This avoids the common failure mode where a property is assumed to have been checked somewhere, but was not.

### Reuse Scenarios, Not Necessarily Full Structure

It is often useful to reuse the same small scenarios across multiple models.

For example:

- two actors and one link for failure propagation,
- one client and one server for request-response,
- one bounded mailbox and one receiver for queue semantics.

What should stay stable is the scenario intent, not necessarily the exact variable structure.

## Example Decomposition

Suppose you are modeling an actor runtime.

### Model A: API Contract

Check:

- send to a live actor eventually becomes observable,
- replies correlate to requests,
- exits are observable at the interface level.

Representation:

- actor identities,
- abstract mailbox effect,
- no scheduler, no queue internals.

### Model B: Mailbox and Scheduling

Check:

- FIFO ordering,
- receive only consumes one message,
- fair scheduling eventually runs a ready actor.

Representation:

- explicit mailboxes,
- ready set,
- scheduler step relation.

### Model C: Failure Propagation

Check:

- link and monitor notifications,
- supervisor restart rules,
- failures do not resurrect terminated actors accidentally.

Representation:

- links,
- monitors,
- restart policy,
- failure causes.

### Model D: Representation Limits

Check:

- mailbox capacity bound,
- bounded timer range,
- fixed-width message counters.

Representation:

- int8 or int16 counters instead of int64 when valid,
- explicit overflow or saturation,
- bounded queue lengths.

The useful result is not one enormous proof. It is a chain of smaller arguments:

- the high-level contract is correct,
- the concurrency structure preserves that contract,
- the failure rules preserve the relevant safety properties,
- the concrete bounds do not invalidate the argument.

## Common Failure Modes

### One Giant Model

If the model includes protocol semantics, crash handling, queue bounds, timer logic, and exact integer representation all at once, it is usually too large and too hard to debug.

### Untracked Assumptions

A lower-level model may silently assume a higher-level property without proving it.

Write assumptions down and connect them to another model or to an explicit out-of-scope decision.

### Property Drift

The same named property may mean different things at different abstraction levels.

Be precise about what is preserved. For example, abstract "eventual delivery" may become concrete "delivery under weak fairness and bounded mailbox occupancy".

### Over-Refinement

Not every model needs a full formal refinement proof.

Sometimes the right connection is lighter:

- a shared scenario,
- a projection checked by invariants,
- an assumption/guarantee handoff,
- a focused edge-case model.

Use the lightest connection that still makes the overall argument explicit and defensible.

## Heuristics

- If a model takes more than 60 seconds to check, simplify it or split it.
- If a property needs a paragraph to explain, it probably deserves its own focused model.
- If a variable exists only to mimic implementation structure and does not affect the property, remove it.
- If a counterexample is hard to interpret, the model likely mixes abstraction levels.
- If two concerns can fail independently, model them independently first.

## Bottom Line

The goal is not one perfect specification. The goal is a coherent verification story.

Use a ladder of models:

- abstract enough to verify the core contract,
- concrete enough to catch the real edge cases,
- connected enough that the overall argument is clear.