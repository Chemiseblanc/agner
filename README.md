# Agner - Erlang style actor framework using C++20 coroutines.

# Features
- Erlang process based actor framework
- OTP-style GenServer and Supervisor primitives
- FoundationDB style deterministic testing
- Optional Boost.Asio-backed scheduler and await bridges

# Boost.Asio support
- Enable with CMake: `cmake --preset default -DAGNER_ENABLE_BOOST_ASIO=ON`.
- When enabled, `<agner/agner.hpp>` exposes `agner::AsioScheduler`; otherwise the
  aggregate header does not include the optional Boost-dependent header.
- `AsioScheduler` runs actors on a `boost::asio::io_context` and provides
  explicit await bridges via `scheduler.await(...)` or
  `asio_await(scheduler, ...)`.
- Supported await inputs are Boost.Thread `boost::future` and
  `boost::shared_future`, `std::future` values such as those returned by
  Boost.Asio `use_future`, and `boost::asio::awaitable`.

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
