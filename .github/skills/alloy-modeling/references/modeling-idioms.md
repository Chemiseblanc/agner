# Modeling Idioms

Common Alloy patterns that help keep models clear and analyzable.

## Partitioning a Universe

Use disjoint signatures or explicit partition constraints when an entity must belong to exactly one class.

```alloy
abstract sig Role {}
one sig Admin, Operator, Guest extends Role {}
```

This is usually clearer than encoding category membership indirectly.

## Exclusive Ownership

When something must have at most one owner, model the field with `lone` or assert exclusivity directly.

```alloy
sig Resource {
  owner: lone User
}
```

If ownership is derived indirectly, add an assertion that no two distinct principals can derive ownership of the same object.

## Acyclic Containment

Use transitive closure to say that a containment graph has no cycles.

```alloy
sig Node {
  parent: lone Node
}

fact Acyclic {
  no n: Node | n in n.^parent
}
```

## Reachability Boundaries

When access or visibility is allowed only through certain paths, define a reusable reachability expression and assert the boundary directly.

```alloy
fun visible[u: User]: set Resource {
  u.permits.target
}
```

Then check whether the computed set ever includes something outside the intended region.

## Cardinality as Intent

Use multiplicities to express what the model means, not just to make the analyzer happy.

- `one` means exactly one
- `lone` means zero or one
- `some` means one or more
- `set` means unconstrained cardinality

If the multiplicity matters to the design, put it in the type.

## Facts for Invariants, Predicates for Situations

Use `fact` for properties that should hold in every world.

Use `pred` when the condition is only part of the scenario you want to explore.

Moving scenario assumptions into facts is a common source of overconstraint.

## Assertions for Claims Under Test

Use `assert` for the claim you want Alloy to challenge.

```alloy
assert NoCrossTenantAccess {
  all u: User, r: Resource |
    r in visible[u] implies u.tenant = r.tenant
}
```

Then run `check NoCrossTenantAccess for ...` in the smallest scope that can falsify it.

## Bottom Line

Prefer idioms that make the model read like a constraint system:

- the type tells you the shape,
- the fact tells you what is always true,
- the predicate tells you which world you are asking for,
- the assertion tells you which claim might fail.
