# Organizing Specs

How to organize a verification repository so the directory tree itself helps explain the overall argument.

## Organize Specs as a Verification Story

Once the problem is decomposed, organize the specs so the repository itself explains the verification argument.

The structure used in this repo is a good reference model:

- `shared/` holds common vocabulary, state-machine definitions, and projection helpers used across multiple specs.
- `core/` holds the fundamental guarantees that everything else depends on.
- `implementation/` holds implementation-adjacent evidence that concrete runtime choices preserve the core story.
- `refinement/` holds bridge specs that project concrete or bounded views onto the abstract interface or state that the core models care about.
- `abstractions/` holds higher-level libraries or APIs built on top of the core layer.
- `systems/` holds larger composed subsystems or application-level scenarios.

Within each layer, decompose again by the kind of question being asked. In this repo that second axis includes categories such as:

- `contract`
- `coordination`
- `failure`
- `timing`
- `runtime`
- `representation`

This yields a directory tree where each scenario sits at the intersection of a dependency layer and an abstraction axis, instead of mixing every concern into one flat folder.

## Organizing Principles

- Put shared definitions in one place so names, domains, and helper operators do not drift.
- Keep each scenario folder focused on one primary question.
- Separate abstract guarantees from implementation evidence.
- Use refinement or projection specs as explicit bridges instead of silently assuming the lower-level model preserves the higher-level story.
- Maintain a ledger that states which property is checked in which scenario and what assumptions it depends on.

## Reusable Layout Pattern

```text
spec/
├── shared/
├── core/
├── implementation/
├── refinement/
├── abstractions/
└── systems/
```

Then refine within each layer by concern:

```text
spec/<layer>/<axis>/<subgroup>/<scenario>/
```

That pattern works well when:

- several specs share vocabulary,
- some specs define the base contract,
- some specs justify concrete implementation choices,
- some specs bridge abstraction levels,
- and some specs model higher-level APIs or full subsystems.

## Shared Modules and Local Scenarios

This repo keeps shared modules in one common location and links them into scenario folders so every scenario can stay small and locally runnable while still using one consistent vocabulary.

That is often better than copying shared definitions into every scenario, especially when several layers depend on the same abstract state machine or domain definitions.

## Keep a Proof Ledger Beside the Specs

The repository should not only contain specs. It should also explain why those specs collectively support the overall argument.

Keep a short ledger that records:

- the scenario path,
- the primary property or question,
- the layer it belongs to,
- the assumptions it depends on,
- and which other specs or projections it relies on.

This repo's `spec/README.md` and `spec/PROOF_LEDGER.md` are the right pattern: the directory tree explains where a scenario lives, and the ledger explains why it exists.
