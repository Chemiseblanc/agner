---- MODULE receive_suspends ----
(***************************************************************************)
(* Receive Suspends Scenario                                               *)
(*                                                                         *)
(* Verifies that receive() suspends the actor when no matching message     *)
(* is available, and resumes correctly when a matching message arrives.    *)
(***************************************************************************)
EXTENDS actor_system

(***************************************************************************)
(* Scenario-specific initial state                                         *)
(***************************************************************************)
ReceiveSuspendsInit ==
  /\ live = {ScenarioActor}
  /\ kind =
       [a \in ActorPool |->
         IF a = ScenarioActor THEN "collector" ELSE "none"]
  /\ pc =
       [a \in ActorPool |->
         IF a = ScenarioActor THEN "collect" ELSE "absent"]
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
ReceiveSuspendsNext ==
  \/ RunReadyActor(ScenarioActor)
  \/ /\ pc[ScenarioActor] = "collect"
     /\ ScenarioActor \notin ready
     /\ pending_result[ScenarioActor] = NoPending
     /\ Send(ScenarioActor, ScenarioPing)

(***************************************************************************)
(* Specification                                                            *)
(***************************************************************************)
ReceiveSuspendsSpec ==
  ReceiveSuspendsInit /\ [][ReceiveSuspendsNext]_vars

(***************************************************************************)
(* Expected outcome: single message observed after suspension/resume       *)
(***************************************************************************)
ReceiveSuspendsOutcome ==
  pc[ScenarioActor] # "done" \/
  ObservationValues(observations[ScenarioActor]) = <<FirstPayload>>

====
