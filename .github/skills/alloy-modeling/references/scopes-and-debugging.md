# Scopes and Debugging

How to choose scopes, interpret instances, and debug Alloy models without getting lost in accidental complexity.

## Scope Is Part of the Model

The scope is not just a performance setting. It determines which bounded worlds Alloy may explore.

That means a result should always be read together with its scope.

## Start Small

Start with the smallest scope that can falsify the claim.

Examples:

- 2 users, 2 roles, 2 resources for simple authorization
- 3 nodes for acyclicity or reachability
- 2 owners and 2 objects for exclusivity checks

Small scopes usually give the clearest counterexamples.

## Widen Deliberately

Increase scope only when:

- the smaller scope finds no issue,
- the property meaningfully depends on larger cardinality,
- or the counterexample you need cannot exist in the current bound.

Do not increase scope blindly. Larger scopes can hide the structural reason a failure occurs.

## Use Exact Scopes When Cardinality Matters

If the question depends on having exactly `n` instances of something, use an exact scope.

Typical cases:

- exactly one root
- exactly three roles
- exactly two tenants with at least one resource each

This avoids misleading examples where the analyzer satisfies the model with too few objects.

## Debugging Overconstraint

If `run` yields no instance:

- one of the facts may be too strong,
- multiplicities may conflict,
- a scenario predicate may silently contradict the stable model,
- or the scope may be too small for the requested world.

The quickest response is usually to remove assumptions until the first instance appears, then add them back one at a time.

## Debugging Counterexamples

If `check` fails:

- inspect the smallest counterexample first,
- identify which relation created the unwanted structure,
- ask whether the problem is the assertion, the facts, or the abstraction,
- and check whether the scope is exposing a real design issue or only a bounded artifact.

Counterexamples are most useful when they reveal a missing invariant or a mistaken derivation rule.

## Common Failure Modes

### Overconstraint

The model has so many facts that Alloy can generate no legal world.

### Underconstraint

The model allows nonsense structures because a key invariant was never stated.

### Scope Confusion

A claim appears true only because the scope is too small to falsify it.

### Mixed Concerns

Authorization, ownership, and topology all live in one assertion, making the resulting instance hard to interpret.

## Bottom Line

Use scopes to sharpen the question, not to compensate for a blurry model.

Start small, inspect concrete instances, and adjust the facts or assertions only after you understand which relation is actually responsible for the result.
