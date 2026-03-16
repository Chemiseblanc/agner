---
name: alloy-modeling
description: 'Model and analyze relational systems with Alloy. Use when: designing object graphs, authorization rules, topology constraints, schemas, ownership relations, or other structure-heavy systems; writing Alloy signatures, relations, facts, predicates, assertions, and scope-bounded checks; debugging counterexample instances; or deciding what should be abstracted into a bounded relational model.'
argument-hint: 'Describe the structure, constraint system, or Alloy question'
---

# Alloy Modeling

Use this skill for relational modeling and bounded counterexample search with Alloy.

It is a good fit when the main problem is not temporal interleaving but structural consistency: which objects may exist, how they relate, which configurations are legal, and how a small counterexample can expose a bad assumption.

It is valid to use this skill before any Alloy model exists. A good outcome can be an informal relational design review, a list of candidate signatures and relations, a sketch of facts and assertions, or a bounded scenario plan that clarifies what should be modeled next.

## When to Use

- Planning a relational model before writing Alloy
- Designing or reviewing schemas, object graphs, ownership models, and topologies
- Modeling authorization, reachability, exclusivity, containment, and integrity constraints
- Searching for small counterexamples to relational assertions
- Deciding what should be represented as signatures, fields, facts, predicates, and assertions
- Tuning scopes so bounded analysis stays useful and interpretable

If the main difficulty is temporal execution, fairness, or long-running interleavings, step back and choose a method that models time and scheduling directly.

## Design Before the Model

When there is no Alloy model yet, start by naming the structural pieces Alloy will need.

Extract:

- the entities that exist
- the relations between those entities
- global invariants that should hold in every world
- scenario assumptions that are only true in specific runs
- claims that should be challenged with `check`
- the smallest scope that could falsify the design

Then decide whether the next artifact should be:

- a list of signatures and fields,
- a compact world model with a few facts,
- a scenario predicate for `run`,
- or a focused assertion for `check`.

## Tooling

Use the Alloy Analyzer as the primary workflow.

Preferred loop:

1. edit the model
2. `run` a small scenario predicate
3. inspect the generated instance
4. `check` a focused assertion in a small scope
5. widen the scope only when the smaller bound stops finding useful mistakes

The analyzer is most useful when you alternate between instance generation and assertion checking rather than writing a large model first and only checking at the end.

## Core Alloy Mindset

Alloy is strongest when you describe legal structures and let the analyzer search for instances.

Start from:

- the entities that exist,
- the relations between them,
- the global invariants that must always hold,
- the properties you want checked,
- the smallest interesting scope that can falsify the design.

Prefer a small, sharp model over a broad one with many accidental details.

## Basic Structure

Typical Alloy organization:

- `sig` for entity sets
- fields on signatures for relations
- `fact` for always-on global constraints
- `pred` for named scenarios or operations
- `assert` for claims you want to check
- `run` to generate example instances
- `check` to search for counterexamples to assertions

Keep these roles distinct. When a model becomes hard to read, split structural definitions, scenarios, and assertions into separate files or sections.

## Modeling Guidance

### Use Alloy for Structure First

Model:

- users, roles, resources, and permissions
- parent-child or ownership graphs
- references, containment, and reachability
- compatibility matrices and configuration rules
- uniqueness, cardinality, partitioning, and acyclicity constraints

Avoid forcing heavily temporal behavior into Alloy unless the question is still mostly bounded structure.

### Keep Facts Global and Minimal

Put only true invariants in `fact` blocks.

If a condition is scenario-specific, keep it in a `pred` or `assert` so the analyzer can still explore alternative legal worlds.

### Use Assertions for Claims, Not Definitions

Write assertions as the thing you want challenged.

If an assertion fails, inspect the smallest counterexample and ask whether:

- the assertion is wrong,
- the facts are incomplete,
- the scope is too small or too large,
- the abstraction forgot a crucial relation.

### Scope Is Part of the Argument

Scopes are not just performance settings. They define what the analyzer is allowed to explore.

Use:

- small scopes to get fast, interpretable counterexamples
- exact scopes when cardinality itself matters
- larger scopes only after the small ones stop finding design mistakes

See [Scopes and Debugging](./references/scopes-and-debugging.md) for a deeper scope strategy and common failure modes.

## Model Organization

For related Alloy models, keep the core vocabulary separate from scenario-specific assumptions and from the claims under test.

Use [Model Organization](./references/model-organization.md) for a fuller layout pattern.

Short version:

- keep signatures and stable facts in a compact core model
- keep scenario predicates small and purpose-specific
- keep assertions focused on one claim each
- split files when authorization, topology, and configuration concerns start to blur together

## Verification Workflow

### 1. Define the World

Start with signatures, fields, and the smallest stable facts that describe the legal structure.

### 2. Run a Representative Scenario

Write a small predicate and `run` it so you can inspect whether the analyzer builds the kind of world you intended.

### 3. Check Focused Claims

Turn each important property into an assertion and `check` it in the smallest scope that could falsify it.

### 4. Inspect the Smallest Counterexample

When `check` fails, understand which relation or missing invariant produced the bad structure before adding more facts.

### 5. Widen the Scope Carefully

Only increase scope when the property genuinely depends on a larger world or the smaller bound has stopped finding useful mistakes.

## Common Patterns

### Exclusive Ownership

```alloy
sig User {}

sig Resource {
	owner: lone User
}
```

### Acyclic Containment

```alloy
sig Node {
	parent: lone Node
}

fact Acyclic {
	no n: Node | n in n.^parent
}
```

### Focused Assertion

```alloy
assert NoSharedOwnership {
	all disj u1, u2: User, r: Resource |
		r.owner = u1 implies r.owner != u2
}
```

### Scenario Predicate

```alloy
pred conflictingOwners {
	some disj u1, u2: User
	some r: Resource
}

run conflictingOwners for 2 User, 1 Resource
```

For more reusable patterns, see [Modeling Idioms](./references/modeling-idioms.md).

## Common Use Cases

### Authorization Models

Check that:

- permissions propagate only along intended relations
- no principal gains access without a valid derivation
- separation-of-duty rules cannot be violated by a small configuration

### Ownership and Topology

Check that:

- containment graphs stay acyclic
- exclusive ownership really is exclusive
- references cannot escape their allowed region

### Schemas and Configuration Rules

Check that:

- all mandatory links can be satisfied simultaneously
- uniqueness constraints are sufficient
- “valid by construction” assumptions actually hold in bounded examples

## Practical Workflow

1. Define the core signatures and relations.
2. Add the smallest set of facts that represent non-negotiable invariants.
3. Write one or two predicates for representative scenarios.
4. Use `run` to inspect whether the model matches your intent.
5. Write assertions for the claims you care about.
6. Use `check` in small scopes first, then widen only when useful.

## Debugging Tips

### No Instance on `run`

- one of the facts may be too strong
- multiplicities may conflict
- the scenario predicate may contradict the stable model
- the scope may be too small for the requested world

### Bad Counterexample on `check`

- inspect the smallest instance first
- identify which relation made the bad structure possible
- ask whether the issue is the assertion, the facts, or the abstraction
- avoid adding broad new facts before you understand the failure

### Scope Confusion

- a property may appear true only because the scope is too small
- use exact scopes when cardinality itself matters
- widen scope deliberately, not by habit

See [Scopes and Debugging](./references/scopes-and-debugging.md) for a deeper debugging workflow.

## When Alloy Is the Wrong Tool

Consider another method when the main difficulty is:

- long-running temporal behavior and fairness
- scheduler or interleaving-sensitive liveness
- protocol execution order over many steps
- proving properties of concrete implementation code

In those cases, switch to a method that is better suited to temporal execution, event ordering, or code-level proof obligations.

## References

- [Model Organization](./references/model-organization.md) - How to split structural definitions, scenarios, and assertions cleanly
- [Modeling Idioms](./references/modeling-idioms.md) - Common Alloy patterns for ownership, containment, reachability, and focused assertions
- [Scopes and Debugging](./references/scopes-and-debugging.md) - How to choose scopes, debug empty worlds, and interpret counterexamples
- [Alloy Documentation](https://alloytools.org/documentation.html)
- [Alloy Tutorial](https://alloytools.org/tutorials/online/)
