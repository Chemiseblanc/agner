# Supervisor Models

This subtree contains models for the Supervisor abstraction built on top of the core actor guarantees.

- `failure/supervisor_restart` checks restart policy behavior.
- `failure/supervisor_one_for_all` checks one-for-all restart behavior.
- `failure/supervisor_rest_for_one` checks rest-for-one restart behavior.
- `failure/supervisor_simple_one_for_one` checks simple-one-for-one restart behavior.
- `contract/supervisor_admin` checks supervisor administrative API behavior.