# Core Contract Models

This axis of the core layer captures interface-level guarantees of the actor runtime without pulling in extra failure, timing, or implementation detail unless the property requires it.

- `api/invalid_actor_operations` checks the mixed missing-actor API contract.
- `messaging/missing_actor_send` checks that sends to absent actors are safe no-ops.