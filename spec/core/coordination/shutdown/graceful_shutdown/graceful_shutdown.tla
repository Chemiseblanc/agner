---- MODULE graceful_shutdown ----
(***************************************************************************)
(* Graceful Shutdown Protocol Scenario                                     *)
(*                                                                         *)
(* Validates the supervisor-initiated shutdown sequence.                   *)
(* Current C++ implementation missing: this model formalizes the *desired* *)
(* behavior where a supervisor awaits its children with a timeout window.  *)
(*                                                                         *)
(* Ordering and semantics:                                                 *)
(* 1. Supervisor receives a stop request.                                  *)
(* 2. Supervisor sends stop signals to all children in reverse start order *)
(* 3. Supervisor starts a shutdown timer for the stopping children.        *)
(* 4. If children exit normally the supervisor cleans them up.             *)
(* 5. If the shutdown timer fires, the supervisor forces termination.      *)
(* 6. Supervisor exits only after all children are handled.                *)
(***************************************************************************)
EXTENDS Naturals, Sequences, FiniteSets, TLC

CONSTANTS 
  Supervisor, Child1, Child2, 
  MaxTime, ShutdownTimeout

VARIABLES 
  actor_state,    \* "running", "stopping", "done"
  children,       \* Set of active child actors {Child1, Child2}
  timer_deadline, \* Nat or -1 (NoDeadline)
  time

vars == <<actor_state, children, timer_deadline, time>>

NoDeadline == 0 - 1
AllActors == {Supervisor, Child1, Child2}

(***************************************************************************)
(* Initial state                                                            *)
(***************************************************************************)
Init ==
  /\ actor_state = [a \in AllActors |-> "running"]
  /\ children = {Child1, Child2}
  /\ timer_deadline = NoDeadline
  /\ time = 0

(***************************************************************************)
(* Supervisor Initiate Shutdown                                            *)
(***************************************************************************)
InitiateShutdown ==
  /\ actor_state[Supervisor] = "running"
  /\ actor_state' = [actor_state EXCEPT ![Supervisor] = "stopping"]
  /\ time + ShutdownTimeout <= MaxTime
  /\ timer_deadline' = time + ShutdownTimeout
  /\ UNCHANGED <<children, time>>

(***************************************************************************)
(* Child Exits Voluntarily (Graceful exit)                                 *)
(***************************************************************************)
ChildExit(c) ==
  /\ c \in children
  /\ actor_state[c] = "running"
  /\ actor_state' = [actor_state EXCEPT ![c] = "done"]
  /\ children' = children \ {c}
  \* If last child exited, cancel timer
  /\ timer_deadline' = IF children' = {} THEN NoDeadline ELSE timer_deadline
  /\ UNCHANGED <<time>>

(***************************************************************************)
(* Forced Termination (Timeout fires)                                      *)
(***************************************************************************)
ForceTermination ==
  /\ actor_state[Supervisor] = "stopping"
  /\ timer_deadline # NoDeadline
  /\ time >= timer_deadline
  \* Supervisor forcefully kills all remaining children
  /\ actor_state' = [a \in AllActors |-> 
                      IF a \in children THEN "done" ELSE actor_state[a]]
  /\ children' = {}
  /\ timer_deadline' = NoDeadline
  /\ UNCHANGED <<time>>

(***************************************************************************)
(* Supervisor Completes Shutdown                                           *)
(***************************************************************************)
SupervisorComplete ==
  /\ actor_state[Supervisor] = "stopping"
  /\ children = {}
  /\ actor_state' = [actor_state EXCEPT ![Supervisor] = "done"]
  /\ UNCHANGED <<children, timer_deadline, time>>

(***************************************************************************)
(* Scheduler Advances Time                                                 *)
(***************************************************************************)
AdvanceTime ==
  /\ time < MaxTime
  /\ time' = time + 1
  /\ UNCHANGED <<actor_state, children, timer_deadline>>

(***************************************************************************)
(* Next-state relation                                                     *)
(***************************************************************************)
Next ==
  \/ InitiateShutdown
  \/ \E c \in children: ChildExit(c)
  \/ ForceTermination
  \/ SupervisorComplete
  \/ AdvanceTime

Spec == Init /\ [][Next]_vars

(***************************************************************************)
(* Properties and Invariants                                               *)
(***************************************************************************)

TypeOK ==
  /\ \A a \in AllActors : actor_state[a] \in {"running", "stopping", "done"}
  /\ children \subseteq {Child1, Child2}
  /\ timer_deadline \in Nat \cup {NoDeadline}
  /\ time \in Nat

\* If supervisor is done, all children must be done.
SupervisorDoneImpliesChildrenDone ==
  (actor_state[Supervisor] = "done") => 
    \A c \in {Child1, Child2} : actor_state[c] = "done" \/ c \notin children

\* Safety: Supervisor does not linger indefinitely, eventually hitting done
\* under fairness conditions. (Liveness property usually checked separately)

====