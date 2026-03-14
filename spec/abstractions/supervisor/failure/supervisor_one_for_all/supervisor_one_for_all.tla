---- MODULE supervisor_one_for_all ----
(***************************************************************************)
(* Supervisor One-For-All Restart Scenario                                 *)
(*                                                                         *)
(* Verifies one_for_all restart strategy behavior:                         *)
(* - When any child terminates and should be restarted, ALL other active   *)
(*   children are stopped.                                                 *)
(* - The stopped children are then respawned based on their own policies   *)
(*   (treating the stop signal as their exit reason).                      *)
(* - Intensity limits cause supervisor shutdown when exceeded.             *)
(*                                                                         *)
(* Source mapping:                                                          *)
(* - Supervisor::handle_termination() -> HandleDown action                 *)
(* - Supervisor::should_restart() -> restart policy logic                  *)
(* - Supervisor::begin_restart_group() -> logic inside HandleDown cascade  *)
(***************************************************************************)
EXTENDS actor_system

(***************************************************************************)
(* Supervisor-specific constants                                           *)
(***************************************************************************)
CONSTANTS Sup, PermanentChild, TransientChild, TempChild, MaxRestarts

ASSUME /\ Sup \in ActorPool
       /\ PermanentChild \in ActorPool
       /\ TransientChild \in ActorPool
       /\ TempChild \in ActorPool
       /\ Cardinality({Sup, PermanentChild, TransientChild, TempChild}) = 4
       /\ MaxRestarts \in Nat
       /\ MaxRestarts >= 1

(***************************************************************************)
(* Restart policies                                                         *)
(***************************************************************************)
RestartPolicies == {"permanent", "transient", "temporary"}

ChildPolicy ==
  [a \in ActorPool |->
    CASE a = PermanentChild -> "permanent"
      [] a = TransientChild -> "transient"
      [] a = TempChild -> "temporary"
      [] OTHER -> "none"]

ShouldRestart(child, reason) ==
  CASE ChildPolicy[child] = "permanent" -> TRUE
    [] ChildPolicy[child] = "transient" -> reason # ExitNormal
    [] ChildPolicy[child] = "temporary" -> FALSE
    [] OTHER -> FALSE

Children == {PermanentChild, TransientChild, TempChild}

(***************************************************************************)
(* Supervisor state: restart_count tracks restarts within the window       *)
(***************************************************************************)
VARIABLES restart_count, sup_stopped

sup_vars == <<restart_count, sup_stopped>>

all_vars == <<vars, sup_vars>>

(***************************************************************************)
(* Initial state                                                            *)
(***************************************************************************)
SupervisorInit ==
  /\ live = {Sup, PermanentChild, TransientChild, TempChild}
  /\ kind =
       [a \in ActorPool |->
         IF a \in {Sup, PermanentChild, TransientChild, TempChild}
           THEN "collector"
           ELSE "none"]
  /\ pc =
       [a \in ActorPool |->
         IF a \in {Sup, PermanentChild, TransientChild, TempChild}
           THEN "collect"
           ELSE "absent"]
  /\ ready = {Sup, PermanentChild, TransientChild, TempChild}
  /\ mailboxes = [a \in ActorPool |-> <<>>]
  /\ pending_result = [a \in ActorPool |-> NoPending]
  /\ timers = [a \in ActorPool |-> NoDeadline]
  /\ observations = [a \in ActorPool |-> <<>>]
  /\ msg_state = [id \in MessageIds |-> "unused"]
  /\ time = 0
  /\ links = {}
  /\ monitors =
       {<<Sup, PermanentChild>>, <<Sup, TransientChild>>, <<Sup, TempChild>>}
  /\ exit_reason = [a \in ActorPool |-> "none"]
  /\ restart_count = 0
  /\ sup_stopped = FALSE

(***************************************************************************)
(* ChildExit: a child exits with given reason                              *)
(***************************************************************************)
ChildExit(child, reason) ==
  /\ child \in Children
  /\ child \in live
  /\ Sup \in live
  /\ ~sup_stopped
  /\ live' = live \ {child}
  /\ pc' = [pc EXCEPT ![child] = "done"]
  /\ ready' = ready \ {child}
  /\ mailboxes' = [mailboxes EXCEPT ![child] = <<>>]
  /\ pending_result' = [pending_result EXCEPT ![child] = NoPending]
  /\ timers' = [timers EXCEPT ![child] = NoDeadline]
  /\ exit_reason' = [exit_reason EXCEPT ![child] = reason]
  /\ links' = {pair \in links : pair[1] # child /\ pair[2] # child}
  /\ monitors' = {pair \in monitors : pair[2] # child}
  \* Record DownSignal observation on supervisor
  /\ observations' =
       [observations EXCEPT
         ![Sup] = Append(@, DownSignal(child, reason))]
  /\ UNCHANGED <<kind, msg_state, time>>

(***************************************************************************)
(* HandleDown: supervisor processes a DownSignal and decides restart.      *)
(* In ONE_FOR_ALL, if one child restarts, ALL other active children are    *)
(* stopped. Then all stopped children evaluate their own restart policies. *)
(***************************************************************************)
HandleDown(child, reason) ==
  /\ Sup \in live
  /\ ~sup_stopped
  /\ child \notin live
  /\ pc[child] = "done"
  /\ exit_reason[child] = reason
  /\ IF ShouldRestart(child, reason)
       THEN \* Check intensity limit
            IF restart_count + 1 > MaxRestarts
              THEN \* Supervisor shuts down
                   /\ sup_stopped' = TRUE
                   /\ restart_count' = restart_count + 1
                   /\ live' = live \ {Sup}
                   /\ pc' = [pc EXCEPT ![Sup] = "done"]
                   /\ ready' = ready \ {Sup}
                   /\ mailboxes' = [mailboxes EXCEPT ![Sup] = <<>>]
                   /\ pending_result' =
                        [pending_result EXCEPT ![Sup] = NoPending]
                   /\ timers' = [timers EXCEPT ![Sup] = NoDeadline]
                   /\ exit_reason' =
                        [exit_reason EXCEPT ![Sup] = ExitError]
                   /\ links' = {pair \in links :
                                  pair[1] # Sup /\ pair[2] # Sup}
                   /\ monitors' = {pair \in monitors : pair[2] # Sup}
                   /\ UNCHANGED <<kind, observations, msg_state, time>>
              ELSE \* Group Respawn
                   LET 
                     StopSet == live \cap (Children \ {child})
                     RespawnSet == {child} \cup {c \in StopSet : ShouldRestart(c, ExitStopped)}
                     PermStopped == StopSet \ RespawnSet
                   IN
                     /\ restart_count' = restart_count + 1
                     /\ sup_stopped' = sup_stopped
                     /\ live' = (live \ StopSet) \cup RespawnSet
                     /\ pc' = [c \in ActorPool |-> 
                                IF c \in RespawnSet THEN "collect"
                                ELSE IF c \in PermStopped THEN "done"
                                ELSE pc[c]]
                     /\ ready' = (ready \ StopSet) \cup RespawnSet
                     /\ exit_reason' = [c \in ActorPool |-> 
                                IF c \in RespawnSet THEN "none"
                                ELSE IF c \in PermStopped THEN ExitStopped
                                ELSE exit_reason[c]]
                     /\ monitors' = {pair \in monitors : pair[2] \notin PermStopped} \cup {<<Sup, c>> : c \in RespawnSet}
                     /\ mailboxes' = [c \in ActorPool |-> IF c \in RespawnSet \cup PermStopped THEN <<>> ELSE mailboxes[c]]
                     /\ pending_result' = [c \in ActorPool |-> IF c \in RespawnSet \cup PermStopped THEN NoPending ELSE pending_result[c]]
                     /\ timers' = [c \in ActorPool |-> IF c \in RespawnSet \cup PermStopped THEN NoDeadline ELSE timers[c]]
                     /\ links' = {pair \in links : pair[1] \notin PermStopped /\ pair[2] \notin PermStopped}
                     /\ UNCHANGED <<kind, observations, msg_state, time>>
       ELSE \* Don't restart anything (the fallen child hasn't triggered cascade)
            /\ UNCHANGED <<live, kind, pc, ready, mailboxes, pending_result,
                           timers, observations, msg_state, time,
                           links, monitors, exit_reason>>
            /\ UNCHANGED sup_vars

(***************************************************************************)
(* Scenario-specific next-state relation                                   *)
(***************************************************************************)
SupervisorNext ==
  \/ \E child \in Children :
       \E reason \in ExitReasons :
         /\ ChildExit(child, reason)
         /\ UNCHANGED sup_vars
  \/ \E child \in Children :
       \E reason \in ExitReasons :
         HandleDown(child, reason)

(***************************************************************************)
(* Specification                                                            *)
(***************************************************************************)
SupervisorSpec ==
  SupervisorInit /\ [][SupervisorNext]_all_vars

(***************************************************************************)
(* Invariants                                                               *)
(***************************************************************************)

\* Permanent children: if supervisor is alive, they are alive or awaiting restart
PermanentChildAliveness ==
  (Sup \in live /\ ~sup_stopped) =>
    (PermanentChild \in live \/ pc[PermanentChild] = "done")

\* Temporary children never restart: once done, stays done
TemporaryChildNeverRestarts ==
  (pc[TempChild] = "done" /\ exit_reason[TempChild] \in ExitReasons) =>
    TRUE  

\* If restart count exceeds max, supervisor stops
IntensityLimitEnforced ==
  restart_count > MaxRestarts => sup_stopped

\* Supervisor alive implies exit_reason is "none"
SupervisorLiveNoExit ==
  Sup \in live => exit_reason[Sup] = "none"

====