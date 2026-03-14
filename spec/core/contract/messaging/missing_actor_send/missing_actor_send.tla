---- MODULE missing_actor_send ----
(***************************************************************************)
(* Missing Actor Send Scenario                                             *)
(*                                                                         *)
(* Verifies that sending a message to a non-existent (absent) actor is     *)
(* a no-op: the system state remains unchanged and the message is not      *)
(* consumed.                                                                *)
(***************************************************************************)
EXTENDS actor_system

(***************************************************************************)
(* Scenario-specific initial state (empty system)                          *)
(***************************************************************************)
MissingActorSendInit ==
  Init

(***************************************************************************)
(* Scenario-specific next-state relation (only send attempts)              *)
(***************************************************************************)
MissingActorSendNext ==
  Send(ScenarioActor, ScenarioPing)

(***************************************************************************)
(* Specification                                                            *)
(***************************************************************************)
MissingActorSendSpec ==
  MissingActorSendInit /\ [][MissingActorSendNext]_vars

(***************************************************************************)
(* Expected outcome: no state changes from send to absent actor            *)
(***************************************************************************)
MissingActorSendOutcome ==
  /\ live = {}
  /\ ready = {}
  /\ msg_state[FirstMessageId] = "unused"
  /\ mailboxes[ScenarioActor] = <<>>
  /\ pending_result[ScenarioActor] = NoPending

====
