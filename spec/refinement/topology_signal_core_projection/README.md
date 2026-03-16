# Topology Signal Core Projection

This scenario extends the refinement layer to link and monitor topology cleanup on actor exit.

It projects a small implementation-oriented topology model onto the core-facing `live`, `pc`, `links`, `monitors`, `observations`, and `exit_reason` view from `shared/actor_system.tla`. The model is intentionally bounded to one watcher, one linked child, and one monitored child, with two mutually exclusive exit paths.

The goal is not a full concurrent signal-delivery refinement. The goal is to prove that bounded `notify_exit()`-style cleanup removes dead topology edges and projects the right `ExitSignal` or `DownSignal` observation to the watcher.