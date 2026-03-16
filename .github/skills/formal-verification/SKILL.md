---
name: formal-verification
description: 'OPT-IN workflow for involved formal-verification planning. Use only when: the user explicitly asks for formal verification, this skill is referenced by workspace instructions, or the task explicitly calls for structuring a full formal-verification effort across models, refinement steps, and implementation-level proof obligations.'
argument-hint: 'Describe the system, property, or verification question'
---

# Formal Verification

Use this skill to decide whether formal methods are appropriate, which style of verification fits the problem, and how to structure the work so the verification effort stays focused and defensible.

This skill is intentionally opt-in. Do not use it opportunistically for ordinary planning or design work; use it when the user or workspace instructions explicitly call for a formal-verification workflow.

Keep the main skill focused on triage and high-level direction. Use the reference docs for deeper workflow guidance.

## When to Use

- The user explicitly asks for formal verification or a workspace instruction directs you here
- Deciding whether a problem needs a model, a proof, a bounded checker, or lightweight property testing
- Choosing between temporal modeling, relational modeling, and protocol-state-machine verification, and deciding how implementation-level proof fits into refinement
- Decomposing a large verification effort into several smaller models or proof targets
- Deriving safety, liveness, refinement, resource, or information-flow properties from an informal design
- Understanding what a counterexample means and what abstraction level should be adjusted next

## Start with the Question, Not the Tool

Write down the claim that matters before choosing a formalism.

Typical claim categories:

- Safety: something bad never happens
- Liveness: something good eventually happens
- Refinement: a concrete design preserves the behavior of an abstract design
- Protocol conformance: state transitions and message flows obey the contract
- Structural consistency: relationships, permissions, or topologies stay valid
- Resource bounds: queues, counters, timers, capacities, or arithmetic stay within intended limits

If the claim is vague, the model will usually be vague too.

## Choosing an Approach

Use [Choosing an Approach](references/choosing-an-approach.md) to decide which modeling formalism fits the problem and how implementation-level proof tools should participate in refinement.

Short version:

- TLA+ for temporal behavior under concurrency
- Alloy for relational structure and bounded counterexamples
- P for communicating state-machine protocols
- use language-integrated proof to carry spec-level obligations into the implementation and check that the code preserves them

## Decompose the Problem

Do not try to prove everything in one giant artifact.

Prefer a small family of focused models or proof targets. Each one should answer one class of questions at one abstraction level with only the detail needed for that question.

Useful decomposition axes:

- contract or API semantics
- coordination or interleaving behavior
- failure and recovery behavior
- time, fairness, and scheduling
- resource bounds and representation details
- implementation-level proof obligations

See [references/model-decomposition.md](references/model-decomposition.md) for a reusable decomposition pattern.

## Organize Specs as a Verification Story

Use [Organizing Specs](references/organizing-specs.md) for the full layout pattern, including the layered `shared/core/implementation/refinement/abstractions/systems` structure used in this repo.

Short version:

- keep shared vocabulary separate from scenario-specific specs
- separate base guarantees, implementation evidence, and refinement bridges
- place each scenario at one clear layer and one clear question axis
- maintain a proof ledger alongside the directory tree

## Interpreting Results

Use [Interpreting Results](references/interpreting-results.md) for the checklist on counterexamples, successful checks, and how to decide whether the next step is a smaller model, a lower-level artifact, or a revised property.

Short version:

- ask what assumptions actually shaped the result
- ask whether a failure is architectural or only an implementation detail
- add detail only when a new verification question appears
- prefer a smaller focused artifact over a larger mixed-level one

## References

- [Choosing an Approach](references/choosing-an-approach.md) - How to choose a modeling formalism and carry obligations into implementation-level proof
- [Model Decomposition](references/model-decomposition.md) - How to break one verification problem into a linked family of focused artifacts
- [Organizing Specs](references/organizing-specs.md) - How to structure a verification repository and proof ledger
- [Interpreting Results](references/interpreting-results.md) - How to read counterexamples, successful checks, and abstraction-level boundaries
- Alloy resources are covered by the separate `alloy-modeling` skill
- TLA+ resources are covered by the separate `tlaplus-modeling` skill
