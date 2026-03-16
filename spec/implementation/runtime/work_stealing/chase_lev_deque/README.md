# Chase-Lev Deque

This scenario adds direct implementation evidence for the lock-free work-stealing deque used by `MtScheduler`.

The directory now contains two bounded models.

- `chase_lev_deque.tla` checks the logical deque safety claims the runtime depends on: owner `push()`/`pop()` behavior, thief `steal()` behavior, growth preserving logical contents, and the single-item pop-vs-steal race resolving with exactly one winner.
- `chase_lev_memory_ordering.tla` checks the publication and visibility obligations abstracted from the C++ memory orders in the implementation: a pushed item is only visible to stealers after the owner's publication step, a grown buffer is only published after its live range has been copied, and the last-item race still resolves with a single winner.

These models do not attempt a full proof of the weak-memory algorithm from the paper. They establish a smaller claim: the bounded abstract state machine for the current deque shape preserves the ownership, accounting, and publication-safety properties that the scheduler refinement witnesses rely on.