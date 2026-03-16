# Model Decomposition

How to break a large verification effort into a series of focused models or proof targets, and how to connect them without forcing every question into one artifact.

## Core Principle

Do not try to prove everything in one giant model.

Build a small family of artifacts instead. Each artifact should answer one class of questions at one abstraction level, with only the detail needed for those questions.

This gives you:

- smaller search spaces,
- clearer counterexamples,
- better isolation of missing assumptions,
- a more credible path from high-level intent to implementation-sensitive detail.

## What to Decompose By

Large systems usually need decomposition across one or more of these axes:

- contract semantics: what the interface guarantees,
- coordination semantics: how concurrent components interact,
- failure semantics: crashes, retries, restarts, cancellation,
- time semantics: deadlines, timeouts, fairness, scheduling,
- structural semantics: topology, permissions, reachability, ownership,
- representation semantics: bounds, integer width, queue capacity, overflow,
- implementation semantics: code-level preconditions, postconditions, and runtime-error freedom.

If one artifact tries to cover all of them at once, it usually becomes too large and too hard to interpret.

## Recommended Verification Ladder

### 1. Contract Model

Model only the externally visible behavior.

Include:

- abstract operations,
- abstract state transitions,
- minimal nondeterminism required by the interface.

Check properties such as:

- safety invariants,
- allowed state transitions,
- eventual response or completion at the interface level.

Do not include concrete scheduling, retries, exact transport details, or machine-sized arithmetic unless the claim depends on them.

### 2. Coordination Model

Add concurrency structure and interaction between components.

Include:

- threads, actors, services, or machines,
- shared state or message passing,
- ordering rules,
- blocking points and enabledness conditions.

Check properties such as:

- no lost work,
- no duplicated processing,
- ordering guarantees,
- absence of bad interleavings,
- progress under explicit fairness assumptions.

### 3. Failure and Recovery Model

Add the failure behavior that matters to correctness.

Include:

- crashes,
- retries,
- cancellation,
- supervision or restart policy,
- degraded or partial-service states.

Check properties such as:

- failures do not violate safety,
- recovery restores required invariants,
- retries do not create duplication or leaks,
- observers see failures consistently.

### 4. Resource and Timing Model

Add explicit bounds and time only when required by the property.

Include:

- bounded queues,
- timer states,
- deadlines,
- fairness or scheduler assumptions,
- rate or capacity limits.

Check properties such as:

- bounded waiting,
- timeout behavior,
- no overflow of resource pools,
- fairness-sensitive liveness.

### 5. Implementation-Adjacent Artifact

Add concrete representation details that could invalidate the higher-level argument.

Include:

- fixed-width arithmetic,
- explicit overflow or saturation behavior,
- exact bounded capacities,
- concrete API contracts,
- code-level proof obligations.

Check properties such as:

- representation limits do not break contract-level safety,
- numeric boundaries behave as intended,
- implementation constraints still preserve the required properties.

This is also where model- or spec-level properties can be translated into implementation-facing contracts and proof obligations for tools such as SPARK, Kani, or similar verifiers.

Do not treat this as the only artifact. It validates implementation-sensitive details; it does not replace the more abstract reasoning.

## Assign Properties to the Right Level

Every important property should live at the highest level where it makes sense.

That usually means:

- prove API or protocol safety in the contract model,
- prove ordering and interleaving properties in the coordination model,
- prove crash and recovery behavior in the failure model,
- prove timeout and fairness behavior in the timing model,
- prove representation-sensitive edge cases in the implementation-adjacent artifact.

Do not force a low-level artifact to carry every property from every higher level.

## Tie Artifacts Together

The artifacts should not be isolated documents. Connect them explicitly.

Useful connection patterns:

- shared vocabulary for domains and named states,
- state projection from concrete state into an abstract view,
- property preservation across abstraction levels,
- assumption or guarantee handoffs between sibling artifacts,
- a short ledger showing which property is checked where.

Not every connection needs a full mechanized refinement proof. Use the lightest connection that still makes the overall argument explicit.

## A Practical Workflow

### Start from the Highest-Level Claim

Write down the user-visible or safety-critical claim first.

Examples:

- every request gets at most one response,
- only authorized principals can reach this state,
- restart never loses durable data,
- counter wraparound cannot violate the invariant.

Create the smallest artifact that can express and check that claim.

### Add Detail Only When a New Question Appears

Do not add queues, timers, scheduler rules, topology constraints, or machine integers until the property requires them.

Each new artifact should exist because of a new verification question, not because the implementation has more detail.

### Keep a Property Ledger

Maintain a short mapping from properties to artifacts so assumptions do not drift.

Example:

| Property | Checked In | Depends On |
|----------|------------|------------|
| No duplicate responses | Contract model | API assumptions |
| FIFO delivery | Coordination model | Mailbox semantics |
| Restart preserves durable state | Failure model | Restart semantics |
| Timeout eventually fires | Timing model | Fair time advance |
| Wraparound is safe | Implementation-adjacent artifact | Fixed-width arithmetic semantics |

## Common Failure Modes

### One Giant Artifact

If the model or proof target includes protocol semantics, crash handling, queue bounds, timer logic, topology constraints, and exact representation details all at once, it is usually too large and too hard to debug.

### Untracked Assumptions

A lower-level artifact may silently assume a higher-level property without proving it.

Write assumptions down and connect them either to another artifact or to an explicit out-of-scope decision.

### Property Drift

The same named property may mean different things at different abstraction levels.

Be precise about what is preserved.

### Over-Refinement

Not every relationship needs a full formal refinement proof.

Sometimes a shared scenario, projection, or assumption or guarantee boundary is enough.

## Heuristics

- If a model checker or proof run becomes hard to interpret, the artifact likely mixes abstraction levels.
- If a property needs a paragraph to explain, it probably deserves its own focused artifact.
- If a variable exists only to mimic implementation structure and does not affect the property, remove it.
- If two concerns can fail independently, model them independently first.
- If the key risk is in concrete code, move sooner toward a language-integrated verifier.

## Bottom Line

The goal is not one perfect artifact. The goal is a coherent verification story.

Use a ladder of models or proof targets:

- abstract enough to verify the core contract,
- concrete enough to catch real edge cases,
- connected enough that the overall argument is clear.
