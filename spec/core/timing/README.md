# Core Timing Models

This axis of the core layer captures timeout-sensitive and fairness-sensitive behavior.

- `scheduler/scheduler_fairness` checks ready-queue fairness and dispatch structure.
- `timeouts/try_receive_race` checks the message-versus-timeout race for `try_receive()`.