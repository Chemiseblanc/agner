---- MODULE supervisor_simple_one_for_one ----
(***************************************************************************)
(* Supervisor Simple One-For-One Scenario                                  *)
(*                                                                         *)
(* Verifies simple_one_for_one strategy behavior:                          *)
(* - Starts with 0 active children                                         *)
(* - Can dynamically spawn children sharing ONE generic spec               *)
(* - When a dynamically started child dies, handles it like one_for_one  *)
(***************************************************************************)
EXTENDS actor_system

CONSTANTS Sup, DynC1, DynC2, MaxRestarts, SimpleStrategyRestartPolicy

ASSUME /\ Sup \in ActorPool
       /\ DynC1 \in ActorPool
       /\ DynC2 \in ActorPool
       /\ Cardinality({Sup, DynC1, DynC2}) = 3
       /\ MaxRestarts \in Nat
       /\ MaxRestarts >= 1
       /\ SimpleStrategyRestartPolicy \in {"permanent", "transient", "temporary"}

Children == {DynC1, DynC2}

ChildPolicy[child \in ActorPool] ==
  IF child \in Children THEN SimpleStrategyRestartPolicy ELSE "none"

ShouldRestart(child, reason) ==
  CASE ChildPolicy[child] = "permanent" -> TRUE
    [] ChildPolicy[child] = "transient" -> reason # ExitNormal
    [] ChildPolicy[child] = "temporary" -> FALSE
    [] OTHER -> FALSE

VARIABLES restart_count, sup_stopped

sup_vars == <<restart_count, sup_stopped>>
all_vars == <<vars, sup_vars>>

SupervisorInit ==
  /\ live = {Sup}
  /\ kind = [a \in ActorPool |-> IF a = Sup THEN "collector" ELSE "none"]
  /\ pc = [a \in ActorPool |-> IF a = Sup THEN "collect" ELSE "absent"]
  /\ ready = {Sup}
  /\ mailboxes = [a \in ActorPool |-> <<>>]
  /\ pending_result = [a \in ActorPool |-> NoPending]
  /\ timers = [a \in ActorPool |-> NoDeadline]
  /\ observations = [a \in ActorPool |-> <<>>]
  /\ msg_state = [id \in MessageIds |-> "unused"]
  /\ time = 0
  /\ links = {}
  /\ monitors = {}
  /\ exit_reason = [a \in ActorPool |-> "none"]
  /\ restart_count = 0
  /\ sup_stopped = FALSE

SpawnChild(child) ==
  /\ Sup \in live
  /\ ~sup_stopped
  /\ child \in Children
  /\ child \notin live
  /\ exit_reason[child] = "none"
  /\ live' = live \cup {child}
  /\ kind' = [kind EXCEPT ![child] = "collector"]
  /\ pc' = [pc EXCEPT ![child] = "collect"]
  /\ ready' = ready \cup {child}
  /\ monitors' = monitors \cup {<<Sup, child>>}
  /\ UNCHANGED <<mailboxes, pending_result, timers, observations, msg_state, time, links, exit_reason, sup_vars>>

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
  /\ observations' =
       [observations EXCEPT
         ![Sup] = Append(@, DownSignal(child, reason))]
  /\ UNCHANGED <<kind, msg_state, time, sup_vars>>

HandleDown(child, reason) ==
  /\ Sup \in live
  /\ ~sup_stopped
  /\ Len(observations[Sup]) > 0
  /\ Head(observations[Sup]) = DownSignal(child, reason)
  /\ IF ShouldRestart(child, reason)
       THEN IF restart_count < MaxRestarts
              THEN /\ live' = live \cup {child}
                   /\ kind' = [kind EXCEPT ![child] = "collector"]
                   /\ pc' = [pc EXCEPT ![child] = "collect"]
                   /\ ready' = ready \cup {child}
                   /\ exit_reason' = [exit_reason EXCEPT ![child] = "none"]
                   /\ restart_count' = restart_count + 1
                   /\ monitors' = monitors \cup {<<Sup, child>>}
                   /\ observations' =
                        [observations EXCEPT ![Sup] = Tail(@)]
                   /\ UNCHANGED <<mailboxes, pending_result, timers, msg_state, time, links, sup_stopped>>
              ELSE /\ sup_stopped' = TRUE
                   /\ observations' =
                        [observations EXCEPT ![Sup] = Tail(@)]
                   /\ UNCHANGED <<live, kind, pc, ready, mailboxes, pending_result,
                                  timers, msg_state, time, links, monitors,
                                  exit_reason, restart_count>>
       ELSE /\ observations' =
                 [observations EXCEPT ![Sup] = Tail(@)]
            /\ UNCHANGED <<live, kind, pc, ready, mailboxes, pending_result,
                           timers, msg_state, time, links, monitors,
                           exit_reason, sup_vars>>

SupervisorNext ==
  \/ \E c \in Children : SpawnChild(c)
  \/ \E c \in Children, r \in ExitReasons : ChildExit(c, r)
  \/ \E c \in Children, r \in ExitReasons : HandleDown(c, r)

SupervisorSpec ==
  SupervisorInit /\ [][SupervisorNext]_all_vars

IntensityLimitEnforced ==
  sup_stopped => restart_count >= MaxRestarts

SupervisorTypeOK ==
  /\ sup_stopped \in BOOLEAN
  /\ restart_count \in Nat

=============================================================================
