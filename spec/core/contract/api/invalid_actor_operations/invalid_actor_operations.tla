---- MODULE invalid_actor_operations ----
(* ************************************************************************* *)
(* Invalid Actor Operations Scenario                                       *)
(*                                                                         *)
(* Verifies the mixed missing-actor API contract:                          *)
(* - send() to an absent actor is a silent no-op                           *)
(* - stop(), link(), and monitor() reject absent actors                   *)
(* ************************************************************************* *)
EXTENDS actor_system

CONSTANTS MissingLeft, MissingRight

ASSUME /\ MissingLeft \in ActorPool
       /\ MissingRight \in ActorPool
       /\ MissingLeft # MissingRight

VARIABLES phase, stop_result, link_result, monitor_result

api_vars == <<phase, stop_result, link_result, monitor_result>>
all_vars == <<vars, api_vars>>

InvalidActorOperationsInit ==
  /\ Init
  /\ phase = "start"
  /\ stop_result = "none"
  /\ link_result = "none"
  /\ monitor_result = "none"

MissingSend ==
  /\ phase = "start"
  /\ MissingLeft \notin live
  /\ Send(MissingLeft, ScenarioPing)
  /\ phase' = "after_send"
  /\ UNCHANGED <<stop_result, link_result, monitor_result>>

MissingStopRejects ==
  /\ phase = "after_send"
  /\ MissingLeft \notin live
  /\ UNCHANGED vars
  /\ phase' = "after_stop"
  /\ stop_result' = "out_of_range"
  /\ UNCHANGED <<link_result, monitor_result>>

MissingLinkRejects ==
  /\ phase = "after_stop"
  /\ MissingLeft \notin live
  /\ MissingRight \notin live
  /\ UNCHANGED vars
  /\ phase' = "after_link"
  /\ link_result' = "out_of_range"
  /\ UNCHANGED <<stop_result, monitor_result>>

MissingMonitorRejects ==
  /\ phase = "after_link"
  /\ MissingLeft \notin live
  /\ MissingRight \notin live
  /\ UNCHANGED vars
  /\ phase' = "after_monitor"
  /\ monitor_result' = "out_of_range"
  /\ UNCHANGED <<stop_result, link_result>>

InvalidActorOperationsNext ==
  \/ MissingSend
  \/ MissingStopRejects
  \/ MissingLinkRejects
  \/ MissingMonitorRejects

InvalidActorOperationsSpec ==
  InvalidActorOperationsInit /\ [][InvalidActorOperationsNext]_all_vars

InvalidActorOperationsTypeOK ==
  /\ TypeOK
  /\ phase \in {"start", "after_send", "after_stop", "after_link", "after_monitor"}
  /\ stop_result \in {"none", "out_of_range"}
  /\ link_result \in {"none", "out_of_range"}
  /\ monitor_result \in {"none", "out_of_range"}

SendToMissingActorIsSilent ==
  phase \in {"after_send", "after_stop", "after_link", "after_monitor"} =>
    /\ live = {}
    /\ ready = {}
    /\ msg_state[FirstMessageId] = "unused"
    /\ mailboxes[MissingLeft] = <<>>
    /\ pending_result[MissingLeft] = NoPending

StopRejectsMissingActor ==
  phase \in {"after_stop", "after_link", "after_monitor"} =>
    stop_result = "out_of_range"

LinkRejectsMissingActors ==
  phase \in {"after_link", "after_monitor"} =>
    link_result = "out_of_range"

MonitorRejectsMissingActors ==
  phase = "after_monitor" =>
    monitor_result = "out_of_range"

RejectedOperationsLeaveTopologyUnchanged ==
  phase \in {"after_stop", "after_link", "after_monitor"} =>
    /\ links = {}
    /\ monitors = {}

=============================================================================