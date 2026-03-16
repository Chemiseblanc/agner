# Interpreting Results

How to interpret counterexamples, successful checks, and the next action after a proof or model-checking result.

## Counterexamples

When verification finds a problem, ask:

- Is the property wrong?
- Is the model too abstract or too concrete?
- Did the model include an unrealistic environment behavior?
- Is the violation architectural, or only an implementation detail?
- Should the next step be a smaller focused model rather than a larger one?

Counterexamples are usually most valuable when they tell you which abstraction boundary is wrong.

## Successful Results

When verification succeeds, ask what was actually proved:

- under which assumptions
- at which abstraction level
- within which bounds or scopes
- using which fairness or environment constraints

A successful run is only as strong as the artifact and assumptions behind it.

## Moving Between Levels

Start with the highest-level claim that matters to users or operators.

Then add detail only when a new question appears.

Example progression:

1. Verify the abstract contract.
2. Verify coordination and concurrency behavior.
3. Verify failure and recovery behavior.
4. Verify timing or resource bounds.
5. Verify representation-sensitive or code-level edge cases.

The goal is not one universal proof. The goal is a coherent verification story with explicit assumptions and boundaries.
