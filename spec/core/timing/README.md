# Core Timing Models

This axis of the core layer captures timeout-sensitive and fairness-sensitive behavior.

- `scheduler/scheduler_progress` checks the implementation-agnostic scheduler contract over ready-set dispatch, logical time, and timeout eligibility.
- `scheduler/README.md` records the minimal scheduler properties the rest of the proof story may assume and the scheduler details that remain intentionally out of scope.
- `timeouts/try_receive_race` checks the message-versus-timeout race for `try_receive()`.