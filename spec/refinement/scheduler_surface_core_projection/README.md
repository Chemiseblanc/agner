# Scheduler Surface Core Projection

This scenario is the aggregate refinement bridge for the current repository proof story.

It composes bounded scheduler-surface mechanics into one projected core-facing state view covering `live`, `kind`, `pc`, `ready`, `mailboxes`, `pending_result`, `timers`, `observations`, `msg_state`, `time`, `links`, `monitors`, and `exit_reason`.

The model is intentionally finite and scenario-driven. It does not claim a full general refinement for all scheduler interleavings or all scheduler implementations. Instead, it proves that one bounded execution of the current queue-based scheduler surface, combining queued delivery, timeout delivery, link cleanup, monitor cleanup, signal observation, and ready-queue projection, can preserve the main shared core invariants in a single composed state space.