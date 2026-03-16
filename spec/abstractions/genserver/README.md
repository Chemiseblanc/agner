# GenServer Models

This subtree contains models for the GenServer abstraction built on top of the core actor guarantees.

- `contract/genserver_call` checks synchronous call/reply behavior.
- `contract/cast_ordering` checks asynchronous cast ordering.
- `contract/serve_dispatch` checks `serve()` control-message dispatch behavior.