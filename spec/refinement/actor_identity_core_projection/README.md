# Actor Identity Core Projection

This scenario is the second refinement bridge in the repository.

It projects implementation-oriented actor identity allocation state onto a coarse core-facing validity view. The bridge checks that spawned actors acquire non-zero unique identities, that retired identities stop projecting as live, and that stale sends remain observationally harmless at the abstract boundary.

Like the coroutine bridge, this is not a full refinement of `shared/actor_system.tla`. It establishes a smaller projection boundary that later refinement work can compose with stronger implementation arguments.