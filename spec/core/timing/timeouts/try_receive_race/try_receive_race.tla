---- MODULE try_receive_race ----
(***************************************************************************)
(* Try-Receive Race Scenario                                               *)
(*                                                                         *)
(* Verifies correct behavior when a timeout actor races between receiving  *)
(* a message and timing out. The actor must observe exactly one outcome:   *)
(* either the message arrives first or the timeout fires.                  *)
(***************************************************************************)
EXTENDS actor_system

(***************************************************************************)
(* Scenario-specific initial state                                         *)
(***************************************************************************)
TryReceiveRaceInit ==
  /\ live = {ScenarioActor}
  /\ kind =
       [a \in ActorPool |->
         IF a = ScenarioActor THEN "timeout" ELSE "none"]
  /\ pc =
       [a \in ActorPool |->
         IF a = ScenarioActor THEN "try" ELSE "absent"]
  /\ ready = {ScenarioActor}
  /\ mailboxes = [a \in ActorPool |-> <<>>]
  /\ pending_result = [a \in ActorPool |-> NoPending]
  /\ timers = [a \in ActorPool |-> NoDeadline]
  /\ observations = [a \in ActorPool |-> <<>>]
  /\ msg_state = [id \in MessageIds |-> "unused"]
  /\ time = 0
  /\ links = {}
  /\ monitors = {}
  /\ exit_reason = [a \in ActorPool |-> "none"]

(***************************************************************************)
(* Scenario-specific next-state relation                                   *)
(***************************************************************************)
TryReceiveRaceNext ==
  \/ RunReadyActor(ScenarioActor)
  \/ /\ pc[ScenarioActor] = "try"
     /\ ScenarioActor \notin ready
     /\ pending_result[ScenarioActor] = NoPending
     /\ msg_state[FirstMessageId] = "unused"
     /\ Send(ScenarioActor, ScenarioPing)
  \/ AdvanceTime
  \/ TimeoutFire(ScenarioActor)

(***************************************************************************)
(* Specification                                                            *)
(***************************************************************************)
TryReceiveRaceSpec ==
  TryReceiveRaceInit /\ [][TryReceiveRaceNext]_vars

(***************************************************************************)
(* Expected outcome: exactly one of message or timeout observed            *)
(***************************************************************************)
TryReceiveOutcome ==
  pc[ScenarioActor] # "done" \/
  /\ Len(observations[ScenarioActor]) = 1
  /\ (
       /\ observations[ScenarioActor][1].kind = "Ping"
       /\ observations[ScenarioActor][1].value = FirstPayload
     \/ observations[ScenarioActor][1] = TimeoutToken
     )

====
