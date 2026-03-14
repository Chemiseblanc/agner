---- MODULE timer_lifecycle ----
(***************************************************************************)
(* Timer Lifecycle and Memory Safety Scenario                              *)
(*                                                                         *)
(* Validates the lifecycle of actor receive timeout timers, specifically   *)
(* checking for race conditions where an actor exits (destroying its       *)
(* coroutine frame and ReceiveAwaiter) before a pending scheduler timer    *)
(* fires.                                                                  *)
(*                                                                         *)
(* Since the C++ `ReceiveAwaiter::await_suspend` captures `[this]` in      *)
(* `schedule_after` and only uses `active_ = false` for logical            *)
(* cancellation instead of removing the timer from the scheduler, there    *)
(* is a potential Use-After-Free if the actor terminates before the        *)
(* timer expires.                                                          *)
(*                                                                         *)
(* Coroutine/Awaiter semantics:                                            *)
(* - When an actor invokes `try_receive`, it creates a ReceiveAwaiter      *)
(*   allocated on the coroutine frame and active = TRUE.                   *)
(* - If a message matches before timeout, active is set to FALSE, but      *)
(*   the timer remains in the queue.                                       *)
(* - If the actor finishes execution, the coroutine frame is destroyed,    *)
(*   effectively "freeing" the awaiter memory.                             *)
(* - The pending timer firing accesses `this->active_`.                    *)
(***************************************************************************)
EXTENDS Naturals, Sequences, FiniteSets, TLC

CONSTANTS 
  TimeoutDuration, MaxTime

VARIABLES 
  actor_state,    \* "running", "suspended", "done"
  awaiter_memory, \* "unallocated", "active", "cancelled", "freed"
  timer_queue,    \* set of records [id: Nat, deadline: Nat]
  next_timer_id,
  time

vars == <<actor_state, awaiter_memory, timer_queue, next_timer_id, time>>

(***************************************************************************)
(* Initial state                                                            *)
(***************************************************************************)
Init ==
  /\ actor_state = "running"
  /\ awaiter_memory = "unallocated"
  /\ timer_queue = {}
  /\ next_timer_id = 1
  /\ time = 0

(***************************************************************************)
(* StartReceive: Actor calls try_receive, creates awaiter and suspends.    *)
(***************************************************************************)
StartReceive ==
  /\ actor_state = "running"
  /\ time + TimeoutDuration <= MaxTime
  /\ actor_state' = "suspended"
  /\ awaiter_memory' = "active"
  /\ timer_queue' = timer_queue \cup {[id |-> next_timer_id, deadline |-> time + TimeoutDuration]}
  /\ next_timer_id' = next_timer_id + 1
  /\ UNCHANGED <<time>>

(***************************************************************************)
(* MatchMessage: A message arrives before timeout, matching the receive.   *)
(* The awaiter is logically cancelled, but the timer remains pending.      *)
(***************************************************************************)
MatchMessage ==
  /\ actor_state = "suspended"
  /\ awaiter_memory = "active"
  /\ awaiter_memory' = "cancelled"
  /\ actor_state' = "running"
  /\ UNCHANGED <<timer_queue, next_timer_id, time>>

(***************************************************************************)
(* ActorExit: Actor voluntarily exits while running.                       *)
(* This destroys the coroutine frame, freeing the awaiter's memory.        *)
(***************************************************************************)
ActorExit ==
  /\ actor_state = "running"
  /\ actor_state' = "done"
  /\ awaiter_memory \in {"unallocated", "cancelled"} \* Usually either unallocated or cancelled after a receive
  /\ awaiter_memory' = "freed"
  /\ UNCHANGED <<timer_queue, next_timer_id, time>>

(***************************************************************************)
(* AdvanceTime: The scheduler advances time.                               *)
(***************************************************************************)
AdvanceTime ==
  /\ time < MaxTime
  /\ time' = time + 1
  /\ UNCHANGED <<actor_state, awaiter_memory, timer_queue, next_timer_id>>

(***************************************************************************)
(* FireTimeout: The scheduler fires a timer that has reached its deadline. *)
(* The callback captures `this` and accesses `this->active_`.              *)
(***************************************************************************)
FireTimeout ==
  \E t \in timer_queue :
    /\ time >= t.deadline
    /\ timer_queue' = timer_queue \ {t}
    \* We explicitly check if memory is freed to simulate the C++ access:
    \* `if (!this->active_) return;`
    \* In TLA+, we evaluate it safely, but track the violation via invariants.
    /\ IF awaiter_memory = "active"
         THEN /\ awaiter_memory' = "cancelled"  \* Equivalent to active_ = false
              /\ actor_state' = "running"
         ELSE /\ UNCHANGED <<awaiter_memory, actor_state>>
    /\ UNCHANGED <<next_timer_id, time>>

(***************************************************************************)
(* Next-state relation                                                     *)
(***************************************************************************)
Next ==
  \/ StartReceive
  \/ MatchMessage
  \/ ActorExit
  \/ AdvanceTime
  \/ FireTimeout
  \/ (actor_state = "done" /\ timer_queue = {} /\ UNCHANGED vars)

Spec == Init /\ [][Next]_vars

(***************************************************************************)
(* Properties and Invariants                                               *)
(***************************************************************************)

TypeOK ==
  /\ actor_state \in {"running", "suspended", "done"}
  /\ awaiter_memory \in {"unallocated", "active", "cancelled", "freed"}
  /\ next_timer_id \in Nat
  /\ time \in Nat

\* Memory Safety: A timer must never access the awaiter if it has been freed.
NoUseAfterFree ==
  \A t \in timer_queue :
    (time >= t.deadline) => (awaiter_memory # "freed")

====