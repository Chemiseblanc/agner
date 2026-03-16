# Model Organization

How to organize Alloy models so the structure stays readable and each file has a clear role.

## Keep the Core Vocabulary Small

Start with a compact core module that defines the main signatures and stable relations.

Good candidates for the core vocabulary:

- principal entities
- ownership or parent-child relations
- role or permission relations
- key reachability or containment structure

Do not mix every scenario and every claim into that first file.

## Separate Stable Facts from Scenario Assumptions

Keep true invariants in `fact` blocks.

Put scenario-specific assumptions in `pred` blocks so you can run several bounded worlds against the same structural model.

This separation makes it easier to tell whether a failure comes from the model or from a particular setup.

## Prefer Small Scenario Predicates

Use small predicates to capture representative situations:

- one user attempting escalation
- one resource with conflicting owners
- one topology containing a back-edge
- one configuration family with a few optional components

Small scenario predicates are easier to inspect than one giant predicate that encodes every use case at once.

## Keep Assertions Focused

Write each assertion around one claim.

Examples:

- no resource has two distinct owners
- reachability never crosses a tenant boundary
- every admin capability has a valid derivation
- the containment relation is acyclic

If an assertion needs several paragraphs of explanation, split it.

## A Simple Layout Pattern

One practical Alloy layout is:

```text
alloy/
├── core.als          # signatures, fields, stable facts
├── scenarios.als     # representative predicates and runs
├── assertions.als    # focused claims and checks
└── helpers.als       # reusable predicates or functions
```

You do not always need separate files, but you usually do need separate roles.

## When to Split Models

Split the model when:

- different claims need different scopes
- one part is about authorization and another is about topology
- a scenario adds many assumptions that are not globally true
- the counterexamples are hard to interpret because too many concerns are mixed together

## Bottom Line

Organize Alloy models so that:

- signatures explain the world,
- facts explain what is always true,
- predicates explain which situation you want to explore,
- assertions explain which claim you want challenged.
