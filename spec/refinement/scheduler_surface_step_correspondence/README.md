# Scheduler Surface Step Correspondence

This scenario is the first bounded refinement module in the repository that carries a separate abstract actor-system state alongside the implementation-style scheduler surface.

Unlike the earlier projection bridges, it does not force one scripted phase order. The implementation side can interleave watcher blocking, direct or queued delivery, timeout arming, time advance, timeout firing, child completion, and watcher completion in whatever bounded order remains enabled. Each such implementation step is paired with an explicit `actor_system.tla` action over the abstract state.

The checked claim is still bounded rather than universal, and it is specific to the current queue-based scheduler surface rather than to all possible schedulers. Within that scope it closes the immediate non-TLAPS obligations for a stronger correctness argument: a general bounded implementation `Next`, explicit step correspondence, bounded interleavings across the main runtime surfaces, and preservation of the shared core invariants under that correspondence.