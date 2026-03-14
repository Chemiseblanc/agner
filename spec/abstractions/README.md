# Higher-Level Abstractions

This layer contains runtime abstractions built on top of the core actor guarantees.

- `genserver/contract/genserver_call` models call/reply behavior.
- `supervisor/failure/supervisor_restart` models restart policy behavior.

More abstractions can add their own decomposition axes inside this layer.