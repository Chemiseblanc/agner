# Coroutine Core Projection

This scenario is the first refinement bridge in the repository.

It does not attempt to prove that the coroutine implementation fully refines `shared/actor_system.tla`. Instead, it establishes a smaller claim: coroutine lifecycle states admit a stable projection to a scheduler-visible core view for root tasks.

The projection deliberately hides internal awaited children and only exposes root tasks that would be visible at the scheduler boundary. This keeps the state space small while still checking the first reusable refinement boundary.