# Invalid Actor Operations

This scenario verifies the public missing-actor API contract.

The model starts from an empty runtime and applies four operations in sequence against absent actors: `send`, `stop`, `link`, and `monitor`. It checks that `send` is a silent no-op while the other three operations are rejected without mutating runtime topology or mailbox state.