# Choosing an Approach

How to choose the right modeling formalism and how to connect that model to implementation-level proof tools.

## Start with the Dominant Risk

Choose the modeling method based on the failure mode you most need to understand.

Typical categories:

- temporal behavior under concurrency
- relational consistency and structure
- protocol-state interaction between machines or services

Then decide which implementation-level obligations should be carried into the real code.

If the dominant risk is unclear, write down the claim first before choosing the formalism.

## Modeling Formalisms vs Implementation-Level Proof

TLA+, Alloy, and P are typically choices about how to model and analyze the design.

SPARK, Kani, and similar tools are usually not substitutes for those modeling choices. They live later in the argument, where properties from the spec or model are translated into proof obligations that can be checked directly against the implementation.

That means the usual flow is:

1. choose the modeling style that best exposes the system risk,
2. state and check the important properties at the right abstraction level,
3. translate the properties that must hold in the code into implementation-level contracts, assertions, or proof obligations,
4. use language-integrated tools to show that the implementation preserves them.

## TLA+

Use TLA+ when the main difficulty is temporal behavior under concurrency.

Good fit:

- multithreaded or distributed protocols
- schedulers, retries, timeouts, fairness, failover
- interleaving-sensitive safety or liveness properties
- message passing, shared state, and crash or recovery behavior

Typical outcome:

- an executable state-machine style model
- traces that explain how a violation happens over time

## Alloy

Use Alloy when the main difficulty is relational structure and bounded counterexample search.

Good fit:

- authorization and access-control rules
- object graph or topology constraints
- schema consistency and reachability
- configurations that must satisfy many cross-field constraints

Typical outcome:

- a small counterexample instance showing a violated constraint
- rapid exploration of design alternatives within a bounded scope

## P

Use P when the main difficulty is event-driven protocol behavior between communicating state machines.

Good fit:

- async protocols with explicit events and handlers
- actor or service orchestration with failure paths
- request or response workflows with protocol-state assertions
- implementations or prototypes that benefit from a machine-oriented specification language

Typical outcome:

- explicit protocol machines and monitors
- counterexample traces over sends, receives, and state transitions

## Language-Integrated Proof and Verification Tools

Use language-specific tooling to discharge implementation-level obligations that arise from the model, the spec, or the code-facing contract.

Examples:

- SPARK for Ada when you need proof of absence of runtime errors, contract preservation, or high-assurance code properties
- Kani for Rust when you need bounded verification of real code paths, unsafe blocks, arithmetic, or API invariants
- similar toolchains for other languages when the code itself is the artifact that must be trusted

Typical outcome:

- evidence attached directly to the implementation
- proof that implementation-level contracts preserve properties required by the higher-level model or spec
- stronger guarantees about the code path, with less abstraction flexibility than a standalone model

## Quick Heuristics

- If you need to reason about what can happen over time under many interleavings, prefer TLA+.
- If you need to reason about structures, relationships, and bounded examples, prefer Alloy.
- If the design is naturally a network of communicating state machines, consider P.
- If certification, runtime-error freedom, or code-level contracts matter most, plan an implementation-level proof step with a tool such as SPARK.
- If the risk lives in real Rust code, especially unsafe or arithmetic-heavy code, plan a Kani-style verification step for the relevant code paths.
- If one question is architectural and another is implementation-specific, use different artifacts for them and connect them explicitly.
