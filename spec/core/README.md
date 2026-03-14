# Core Guarantees

This layer defines the guarantees of the base actor runtime that higher layers depend on.

- `contract/` captures interface-level guarantees such as safe no-op sends.
- `coordination/` captures mailbox behavior and scheduler-visible interleavings.
- `failure/` captures base exit propagation semantics.
- `timing/` captures fairness and timeout-sensitive behavior.