# Core Coordination Models

This axis of the core layer captures concurrency structure, mailbox behavior, and the shared actor runtime interleavings.

- `core/core_system` explores the shared runtime state machine broadly.
- `mailbox/mailbox_ordering` checks FIFO mailbox behavior.
- `mailbox/receive_suspends` checks suspension and direct wakeup semantics.