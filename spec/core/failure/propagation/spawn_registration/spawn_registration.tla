---- MODULE spawn_registration ----
(* ************************************************************************* *)
(* Spawn Registration Scenario                                             *)
(*                                                                         *)
(* Verifies that spawn_link() and spawn_monitor() establish link/monitor   *)
(* topology before the spawned actor can immediately exit.                 *)
(* ************************************************************************* *)
EXTENDS actor_system

CONSTANTS Parent, LinkedChild, MonitoredChild

ASSUME /\ Parent \in ActorPool
       /\ LinkedChild \in ActorPool
       /\ MonitoredChild \in ActorPool
       /\ Cardinality({Parent, LinkedChild, MonitoredChild}) = 3

VARIABLE phase

spawn_vars == <<phase>>
all_vars == <<vars, spawn_vars>>

SpawnRegistrationInit ==
  /\ live = {Parent}
  /\ kind = [a \in ActorPool |-> IF a = Parent THEN "collector" ELSE "none"]
  /\ pc = [a \in ActorPool |-> IF a = Parent THEN "collect" ELSE "absent"]
  /\ ready = {Parent}
  /\ mailboxes = [a \in ActorPool |-> <<>>]
  /\ pending_result = [a \in ActorPool |-> NoPending]
  /\ timers = [a \in ActorPool |-> NoDeadline]
  /\ observations = [a \in ActorPool |-> <<>>]
  /\ msg_state = [id \in MessageIds |-> "unused"]
  /\ time = 0
  /\ links = {}
  /\ monitors = {}
  /\ exit_reason = [a \in ActorPool |-> "none"]
  /\ phase = "start"

SpawnLinkedChild ==
  /\ phase = "start"
  /\ SpawnLink(Parent, LinkedChild, "collector")
  /\ phase' = "linked_spawned"

LinkedChildImmediateExit ==
  /\ phase = "linked_spawned"
  /\ LinkedChild \in live
  /\ <<Parent, LinkedChild>> \in links
  /\ <<LinkedChild, Parent>> \in links
  /\ live' = live \ {LinkedChild}
  /\ kind' = kind
  /\ pc' = [pc EXCEPT ![LinkedChild] = "done"]
  /\ ready' = ready \ {LinkedChild}
  /\ mailboxes' = [mailboxes EXCEPT ![LinkedChild] = <<>>]
  /\ pending_result' = [pending_result EXCEPT ![LinkedChild] = NoPending]
  /\ timers' = [timers EXCEPT ![LinkedChild] = NoDeadline]
  /\ observations' =
       [a \in ActorPool |->
         IF a = Parent
           THEN Append(observations[a], ExitSignal(LinkedChild, ExitError))
           ELSE observations[a]]
  /\ msg_state' = msg_state
  /\ time' = time
  /\ links' = {pair \in links : pair[1] # LinkedChild /\ pair[2] # LinkedChild}
  /\ monitors' = {pair \in monitors : pair[2] # LinkedChild}
  /\ exit_reason' = [exit_reason EXCEPT ![LinkedChild] = ExitError]
  /\ phase' = "linked_exited"

SpawnMonitoredChild ==
  /\ phase = "linked_exited"
  /\ SpawnMonitor(Parent, MonitoredChild, "collector")
  /\ phase' = "monitored_spawned"

MonitoredChildImmediateExit ==
  /\ phase = "monitored_spawned"
  /\ MonitoredChild \in live
  /\ <<Parent, MonitoredChild>> \in monitors
  /\ live' = live \ {MonitoredChild}
  /\ kind' = kind
  /\ pc' = [pc EXCEPT ![MonitoredChild] = "done"]
  /\ ready' = ready \ {MonitoredChild}
  /\ mailboxes' = [mailboxes EXCEPT ![MonitoredChild] = <<>>]
  /\ pending_result' = [pending_result EXCEPT ![MonitoredChild] = NoPending]
  /\ timers' = [timers EXCEPT ![MonitoredChild] = NoDeadline]
  /\ observations' =
       [a \in ActorPool |->
         IF a = Parent
           THEN Append(observations[a], DownSignal(MonitoredChild, ExitError))
           ELSE observations[a]]
  /\ msg_state' = msg_state
  /\ time' = time
  /\ links' = {pair \in links : pair[1] # MonitoredChild /\ pair[2] # MonitoredChild}
  /\ monitors' = {pair \in monitors : pair[2] # MonitoredChild}
  /\ exit_reason' = [exit_reason EXCEPT ![MonitoredChild] = ExitError]
  /\ phase' = "monitored_exited"

SpawnRegistrationNext ==
  \/ SpawnLinkedChild
  \/ LinkedChildImmediateExit
  \/ SpawnMonitoredChild
  \/ MonitoredChildImmediateExit

SpawnRegistrationSpec ==
  SpawnRegistrationInit /\ [][SpawnRegistrationNext]_all_vars

SpawnRegistrationTypeOK ==
  /\ phase \in {"start", "linked_spawned", "linked_exited", "monitored_spawned", "monitored_exited"}
  /\ TypeOK

LinkedSpawnRegistersBeforeRun ==
  phase = "linked_spawned" =>
    /\ LinkedChild \in live
    /\ <<Parent, LinkedChild>> \in links
    /\ <<LinkedChild, Parent>> \in links

MonitorSpawnRegistersBeforeRun ==
  phase = "monitored_spawned" =>
    /\ MonitoredChild \in live
    /\ <<Parent, MonitoredChild>> \in monitors

LinkedImmediateExitObserved ==
  phase \in {"linked_exited", "monitored_spawned", "monitored_exited"} =>
    /\ Len(observations[Parent]) >= 1
    /\ observations[Parent][1] = ExitSignal(LinkedChild, ExitError)

MonitoredImmediateExitObserved ==
  phase = "monitored_exited" =>
    /\ Len(observations[Parent]) = 2
    /\ observations[Parent][2] = DownSignal(MonitoredChild, ExitError)

SpawnTopologyCleanedAfterExit ==
  /\ phase \in {"linked_exited", "monitored_spawned", "monitored_exited"} =>
       /\ <<Parent, LinkedChild>> \notin links
       /\ <<LinkedChild, Parent>> \notin links
  /\ phase = "monitored_exited" =>
       <<Parent, MonitoredChild>> \notin monitors

=============================================================================