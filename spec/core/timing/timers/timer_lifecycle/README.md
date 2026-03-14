# Timer Lifecycle and Memory Safety Edge Cases

This specification models the lifecycle of actor receive timeout timers, specifically checking for race conditions where an actor exits before a pending scheduler timer fires.

## Discovered Vulnerability
TLC verification of this model reveals an invariant violation (`NoUseAfterFree`), identifying a real memory safety bug in the current C++ implementation.

In `include/agner/actor.hpp`, `ReceiveAwaiter::await_suspend` captures `[this]` and schedules a callback:
```cpp
actor_.scheduler_.schedule_after(timeout_, [this, handle] {
  if (!active_) return;
  // ...
});
```

Because logical cancellation just sets `active_ = false` instead of removing the timer from the scheduler queue, if an actor processes a message, completes its execution, and the coroutine frame is destroyed, the `this` pointer in the scheduled callback dangles. When the scheduler eventually fires the timer, it reads `this->active_`, triggering Undefined Behavior (Use-After-Free).

## Model Verification
Run TLC with the following to see the error trace:
```bash
tlc timer_lifecycle.tla
```

**Expected Result:**
Invariant `NoUseAfterFree` is violated, demonstrating the actor exit race.
