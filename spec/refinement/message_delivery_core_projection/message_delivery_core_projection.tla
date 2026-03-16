---- MODULE message_delivery_core_projection ----
(* ************************************************************************* *)
(* Message Delivery Core Projection                                       *)
(*                                                                         *)
(* Projects a small implementation-oriented message delivery model onto   *)
(* the core-facing mailbox, pending-result, observation, readiness, and  *)
(* message-state view used by actor_system.tla.                           *)
(*                                                                         *)
(* This is a bounded bridge, not a full mailbox refinement. It covers     *)
(* one collector actor and two delivery paths:                             *)
(* - a queued message that is later consumed                               *)
(* - a blocked receive that is woken by direct pending delivery            *)
(* ************************************************************************* *)
EXTENDS actor_defs, refinement_vocabulary, TLC

CONSTANTS Receiver

ASSUME Receiver \in ActorPool

ImplPcStates == {"absent", "collect", "done"}
BridgePhases == {"select", "queue_path", "queue_sent", "pending_path", "pending_sent", "done"}

VARIABLES impl_live, impl_pc, ready_queue,
          impl_mailboxes, impl_pending,
          impl_observations, impl_msg_state, phase

bridge_vars ==
  <<impl_live, impl_pc, ready_queue,
    impl_mailboxes, impl_pending,
    impl_observations, impl_msg_state, phase>>

ProjectedLive == impl_live

ProjectedPc == impl_pc

ProjectedReady == ReadyMembers(ready_queue)

ProjectedMailboxes == impl_mailboxes

ProjectedPending == impl_pending

ProjectedObservations == impl_observations

ProjectedMsgState == impl_msg_state

ReceiverReady == ReadyContains(ready_queue, Receiver)

ConsumedMessage ==
  IF ProjectedPending[Receiver].kind = "Ping"
    THEN ProjectedPending[Receiver]
    ELSE ProjectedMailboxes[Receiver][FirstMatchIndex("collect", ProjectedMailboxes[Receiver])]

RemainingMailbox ==
  IF ProjectedPending[Receiver].kind = "Ping"
    THEN ProjectedMailboxes[Receiver]
    ELSE RemoveAt(ProjectedMailboxes[Receiver],
                  FirstMatchIndex("collect", ProjectedMailboxes[Receiver]))

ProjectionInit ==
  /\ impl_live = {Receiver}
  /\ impl_pc = [a \in ActorPool |-> IF a = Receiver THEN "collect" ELSE "absent"]
  /\ ready_queue = ReadySingleton(Receiver)
  /\ impl_mailboxes = [a \in ActorPool |-> <<>>]
  /\ impl_pending = [a \in ActorPool |-> NoPending]
  /\ impl_observations = [a \in ActorPool |-> <<>>]
  /\ impl_msg_state = [id \in MessageIds |-> "unused"]
  /\ phase = "select"

ChooseQueuePath ==
  /\ phase = "select"
  /\ phase' = "queue_path"
  /\ UNCHANGED <<impl_live, impl_pc, ready_queue,
                 impl_mailboxes, impl_pending,
                 impl_observations, impl_msg_state>>

ChoosePendingPath ==
  /\ phase = "select"
  /\ ReadyMembers(ready_queue) = {Receiver}
  /\ ready_queue' = ReadyEmpty
  /\ phase' = "pending_path"
  /\ UNCHANGED <<impl_live, impl_pc, impl_mailboxes,
                 impl_pending, impl_observations, impl_msg_state>>

QueueSend ==
  /\ phase = "queue_path"
  /\ Receiver \in impl_live
  /\ ReceiverReady
  /\ impl_msg_state[FirstMessageId] = "unused"
  /\ impl_mailboxes' = [impl_mailboxes EXCEPT ![Receiver] = Append(@, ScenarioPing)]
  /\ impl_msg_state' = [impl_msg_state EXCEPT ![FirstMessageId] = "queued"]
  /\ phase' = "queue_sent"
  /\ UNCHANGED <<impl_live, impl_pc, ready_queue,
                 impl_pending, impl_observations>>

PendingSend ==
  /\ phase = "pending_path"
  /\ Receiver \in impl_live
  /\ ~ReceiverReady
  /\ impl_pending[Receiver] = NoPending
  /\ impl_msg_state[FirstMessageId] = "unused"
  /\ impl_pending' = [impl_pending EXCEPT ![Receiver] = ScenarioPing]
  /\ ready_queue' = ReadySingleton(Receiver)
  /\ impl_msg_state' = [impl_msg_state EXCEPT ![FirstMessageId] = "pending"]
  /\ phase' = "pending_sent"
  /\ UNCHANGED <<impl_live, impl_pc, impl_mailboxes, impl_observations>>

RunCollector ==
  /\ phase \in {"queue_sent", "pending_sent"}
  /\ ReceiverReady
  /\ impl_pc[Receiver] = "collect"
  /\ impl_live = {Receiver}
  /\ (impl_pending[Receiver].kind = "Ping" \/ HasMatch("collect", impl_mailboxes[Receiver]))
  /\ impl_live' = {}
  /\ impl_pc' = [impl_pc EXCEPT ![Receiver] = "done"]
  /\ ready_queue' = ReadyEmpty
  /\ impl_mailboxes' = [impl_mailboxes EXCEPT ![Receiver] = <<>>]
  /\ impl_pending' = [impl_pending EXCEPT ![Receiver] = NoPending]
  /\ impl_observations' =
       [impl_observations EXCEPT ![Receiver] = Append(@, ConsumedMessage)]
  /\ impl_msg_state' =
       MarkDropped([impl_msg_state EXCEPT ![ConsumedMessage.id] = "observed"],
                   RemainingMailbox)
  /\ phase' = "done"

Done ==
  /\ phase = "done"
  /\ UNCHANGED bridge_vars

ProjectionNext ==
  \/ ChooseQueuePath
  \/ ChoosePendingPath
  \/ QueueSend
  \/ PendingSend
  \/ RunCollector
  \/ Done

ProjectionSpec ==
  ProjectionInit /\ [][ProjectionNext]_bridge_vars

ImplTypeOK ==
  /\ impl_live \subseteq ActorPool
  /\ impl_pc \in [ActorPool -> ImplPcStates]
  /\ ReadySurfaceTypeOK(ready_queue)
  /\ impl_mailboxes \in [ActorPool -> Seq(MessageUniverse)]
  /\ impl_pending \in [ActorPool -> (MessageUniverse \cup {NoPending})]
  /\ impl_observations \in [ActorPool -> Seq(MessageUniverse)]
  /\ impl_msg_state \in [MessageIds -> MessageStates]
  /\ phase \in BridgePhases

ProjectionTypeOK ==
  /\ ProjectedLive \subseteq ActorPool
  /\ ProjectedReady \subseteq ActorPool
  /\ \A a \in ActorPool : ProjectedPc[a] \in PcStates
  /\ ProjectedMailboxes \in [ActorPool -> Seq(MessageUniverse)]
  /\ ProjectedPending \in [ActorPool -> (MessageUniverse \cup {NoPending})]
  /\ ProjectedObservations \in [ActorPool -> Seq(MessageUniverse)]
  /\ ProjectedMsgState \in [MessageIds -> MessageStates]

ProjectedReadyActorsLive ==
  ReadySubsumesLive(ProjectedReady, ProjectedLive)

PendingPathProjectsBlockedReceiver ==
  phase = "pending_path" =>
    /\ Receiver \notin ProjectedReady
    /\ ProjectedPending[Receiver] = NoPending
    /\ ProjectedPc[Receiver] = "collect"

PendingDeliveryProjectsWakeup ==
  phase = "pending_sent" =>
    /\ Receiver \in ProjectedReady
    /\ ProjectedPending[Receiver] = ScenarioPing
    /\ ProjectedMsgState[FirstMessageId] = "pending"

QueuedDeliveryProjectsMailbox ==
  phase = "queue_sent" =>
    /\ ProjectedMailboxes[Receiver] = <<ScenarioPing>>
    /\ ProjectedPending[Receiver] = NoPending
    /\ ProjectedMsgState[FirstMessageId] = "queued"

CompletionProjectsObservedMessage ==
  phase = "done" =>
    /\ ProjectedPc[Receiver] = "done"
    /\ ObservationValues(ProjectedObservations[Receiver]) = <<FirstPayload>>
    /\ ProjectedMsgState[FirstMessageId] = "observed"

CompletedReceiverLeavesNoMailbox ==
  phase = "done" =>
    /\ Receiver \notin ProjectedLive
    /\ Receiver \notin ProjectedReady
    /\ ProjectedMailboxes[Receiver] = <<>>
    /\ ProjectedPending[Receiver] = NoPending

=============================================================================