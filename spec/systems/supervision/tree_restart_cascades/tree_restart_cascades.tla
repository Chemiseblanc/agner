---- MODULE tree_restart_cascades ----
(***************************************************************************)
(* Multi-level Supervision Tree Restart Cascades                           *)
(*                                                                         *)
(* Verifies that errors bubble up supervision trees correctly:             *)
(* - Bottom worker crashing triggers local restarts.                       *)
(* - Hitting local restart intensity stops the mid-level supervisor.       *)
(* - The mid-level supervisor's stop bubbles up to the top supervisor.     *)
(* - The top supervisor restarts the mid-level supervisor.                 *)
(* - Restarting a sub-tree respawns its children.                          *)
(***************************************************************************)
EXTENDS actor_system

CONSTANTS TopSup, MidSup, Worker, MaxRestarts

ASSUME /\ TopSup \in ActorPool
       /\ MidSup \in ActorPool
       /\ Worker \in ActorPool
       /\ Cardinality({TopSup, MidSup, Worker}) = 3
       /\ MaxRestarts \in Nat
       /\ MaxRestarts >= 1

Sups == {TopSup, MidSup}

VARIABLES restart_count, sup_stopped

sup_vars == <<restart_count, sup_stopped>>

all_vars == <<vars, sup_vars>>

TreeInit ==
  /\ live = {TopSup, MidSup, Worker}
  /\ kind = [a \in ActorPool |-> IF a \in {TopSup, MidSup, Worker} THEN "collector" ELSE "none"]
  /\ pc = [a \in ActorPool |-> IF a \in {TopSup, MidSup, Worker} THEN "collect" ELSE "absent"]
  /\ ready = {TopSup, MidSup, Worker}
  /\ mailboxes = [a \in ActorPool |-> <<>>]
  /\ pending_result = [a \in ActorPool |-> NoPending]
  /\ timers = [a \in ActorPool |-> NoDeadline]
  /\ observations = [a \in ActorPool |-> <<>>]
  /\ msg_state = [id \in MessageIds |-> "unused"]
  /\ time = 0
  /\ links = {}
  /\ monitors = {<<TopSup, MidSup>>, <<MidSup, Worker>>}
  /\ exit_reason = [a \in ActorPool |-> "none"]
  /\ restart_count = [s \in Sups |-> 0]
  /\ sup_stopped = [s \in Sups |-> FALSE]

WorkerExit(reason) ==
  /\ Worker \in live
  /\ live' = live \ {Worker}
  /\ pc' = [pc EXCEPT ![Worker] = "done"]
  /\ ready' = ready \ {Worker}
  /\ mailboxes' = [mailboxes EXCEPT ![Worker] = <<>>]
  /\ pending_result' = [pending_result EXCEPT ![Worker] = NoPending]
  /\ timers' = [timers EXCEPT ![Worker] = NoDeadline]
  /\ exit_reason' = [exit_reason EXCEPT ![Worker] = reason]
  /\ links' = {pair \in links : pair[1] # Worker /\ pair[2] # Worker}
  /\ monitors' = {pair \in monitors : pair[2] # Worker}
  /\ observations' =
       IF MidSup \in live
         THEN [observations EXCEPT ![MidSup] = Append(@, DownSignal(Worker, reason))]
         ELSE observations
  /\ UNCHANGED <<kind, msg_state, time, sup_vars>>

MidSupExit(reason) ==
  /\ MidSup \in live
  /\ sup_stopped[MidSup] \* Only exit when requested (due to limits)
  /\ live' = live \ {MidSup}
  /\ pc' = [pc EXCEPT ![MidSup] = "done"]
  /\ ready' = ready \ {MidSup}
  /\ mailboxes' = [mailboxes EXCEPT ![MidSup] = <<>>]
  /\ pending_result' = [pending_result EXCEPT ![MidSup] = NoPending]
  /\ timers' = [timers EXCEPT ![MidSup] = NoDeadline]
  /\ exit_reason' = [exit_reason EXCEPT ![MidSup] = reason]
  /\ links' = {pair \in links : pair[1] # MidSup /\ pair[2] # MidSup}
  /\ monitors' = {pair \in monitors : pair[2] # MidSup}
  /\ observations' =
       IF TopSup \in live
         THEN [observations EXCEPT ![TopSup] = Append(@, DownSignal(MidSup, reason))]
         ELSE observations
  /\ UNCHANGED <<kind, msg_state, time, restart_count, sup_stopped>>

HandleWorkerDown(reason) ==
  /\ MidSup \in live
  /\ ~sup_stopped[MidSup]
  /\ Len(observations[MidSup]) > 0
  /\ Head(observations[MidSup]) = DownSignal(Worker, reason)
  /\ IF restart_count[MidSup] < MaxRestarts
       THEN /\ live' = live \cup {Worker}
            /\ kind' = [kind EXCEPT ![Worker] = "collector"]
            /\ pc' = [pc EXCEPT ![Worker] = "collect"]
            /\ ready' = ready \cup {Worker}
            /\ exit_reason' = [exit_reason EXCEPT ![Worker] = "none"]
            /\ restart_count' = [restart_count EXCEPT ![MidSup] = @ + 1]
            /\ monitors' = monitors \cup {<<MidSup, Worker>>}
            /\ observations' = [observations EXCEPT ![MidSup] = Tail(@)]
            /\ UNCHANGED <<mailboxes, pending_result, timers, msg_state, time, links, sup_stopped>>
       ELSE /\ sup_stopped' = [sup_stopped EXCEPT ![MidSup] = TRUE]
            /\ observations' = [observations EXCEPT ![MidSup] = Tail(@)]
            /\ UNCHANGED <<live, kind, pc, ready, mailboxes, pending_result, timers, msg_state, time, links, monitors, exit_reason, restart_count>>

HandleMidDown(reason) ==
  /\ TopSup \in live
  /\ ~sup_stopped[TopSup]
  /\ Len(observations[TopSup]) > 0
  /\ Head(observations[TopSup]) = DownSignal(MidSup, reason)
  /\ IF restart_count[TopSup] < MaxRestarts
       \* Start MidSup which recursively starts Worker
       THEN /\ live' = live \cup {MidSup, Worker}
            /\ kind' = [kind EXCEPT ![MidSup] = "collector", ![Worker] = "collector"]
            /\ pc' = [pc EXCEPT ![MidSup] = "collect", ![Worker] = "collect"]
            /\ ready' = ready \cup {MidSup, Worker}
            /\ exit_reason' = [exit_reason EXCEPT ![MidSup] = "none", ![Worker] = "none"]
            /\ restart_count' = [restart_count EXCEPT ![TopSup] = @ + 1, ![MidSup] = 0]
            /\ sup_stopped' = [sup_stopped EXCEPT ![MidSup] = FALSE]
            /\ monitors' = monitors \cup {<<TopSup, MidSup>>, <<MidSup, Worker>>}
            /\ observations' = [observations EXCEPT ![TopSup] = Tail(@)]
            /\ UNCHANGED <<mailboxes, pending_result, timers, msg_state, time, links>>
       ELSE /\ sup_stopped' = [sup_stopped EXCEPT ![TopSup] = TRUE]
            /\ observations' = [observations EXCEPT ![TopSup] = Tail(@)]
            /\ UNCHANGED <<live, kind, pc, ready, mailboxes, pending_result, timers, msg_state, time, links, monitors, exit_reason, restart_count>>

TreeNext ==
  \/ \E r \in {"error"} : WorkerExit(r)
  \/ \E r \in {"error"} : MidSupExit(r)
  \/ \E r \in {"error"} : HandleWorkerDown(r)
  \/ \E r \in {"error"} : HandleMidDown(r)

TreeSpec == TreeInit /\ [][TreeNext]_all_vars

TreeTypeOK ==
  /\ restart_count \in [Sups -> Nat]
  /\ sup_stopped \in [Sups -> BOOLEAN]

IntensityLimitEnforced ==
  \A s \in Sups : sup_stopped[s] => restart_count[s] >= MaxRestarts

Liveness ==
  <>(sup_stopped[TopSup] = TRUE)

=============================================================================
