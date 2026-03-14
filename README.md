# Agner - Erlang style actor framework using C++20 coroutines.

# Features
- Erlang process based actor framework
- OTP-style GenServer and Supervisor primitives
- FoundationDB style deterministic testing

# Formal model
- `spec/core_actor_system.tla` models the actor core with an abstract
  nondeterministic scheduler, selective receive, and `try_receive()` timeouts.
- `spec/core_actor_system.cfg` explores the bounded core model. Focused configs
  under `spec/` mirror mailbox ordering, receive suspension, timeout races, and
  missing-actor send semantics from the current test suite.
- Run TLC with `tlc -cleanup -config spec/core_actor_system.cfg
  spec/core_actor_system.tla`, or swap in one of the scenario configs for a
  narrower check.
- Link and monitor propagation are intentionally deferred so the first model can
  stay small enough for bug-finding runs.
