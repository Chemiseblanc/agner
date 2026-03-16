# Identity + Coroutine Core Projection

This scenario composes the existing identity and coroutine refinement bridges into a single projected core-facing state view.

It still does not attempt a full refinement of `shared/actor_system.tla`. Instead, it checks that bounded implementation-oriented identity allocation and coroutine lifecycle steps admit one stable coarse projection record containing core-facing liveness, program-counter shape, readiness, exit reasons, valid references, and stale-target visibility.

The projection keeps the awaited child internal and only exposes scheduler-visible root tasks at the abstract boundary. That makes it a stronger bridge than the earlier isolated scenarios without forcing the full mailbox, timer, or observation story into the same model.