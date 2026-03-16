# Spawn Registration

This scenario verifies that `spawn_link()` and `spawn_monitor()` register link and monitor topology before the spawned actor can immediately exit.

The model uses a parent actor that first spawns a linked child and then a monitored child. Each child exits immediately with `ExitError`, and the invariants check that the parent still observes the expected `ExitSignal` or `DownSignal` and that the temporary topology is cleaned up afterward.