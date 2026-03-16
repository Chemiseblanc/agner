---- MODULE timeout_delivery_core_projection ----
(* ************************************************************************* *)
(* Timeout Delivery Core Projection                                       *)
(*                                                                         *)
(* Projects a bounded implementation-oriented timeout-delivery model onto *)
(* the core-facing timeout boundary from actor_system.tla.                *)
(*                                                                         *)
(* This bridge covers one timeout actor and the two observable outcomes   *)
(* the core timeout specs depend on:                                      *)
(* - a direct message wakeup before the deadline                           *)
(* - a timeout token delivered at the deadline                             *)
(* ************************************************************************* *)
EXTENDS actor_defs, refinement_vocabulary, TLC

CONSTANTS Receiver

ASSUME Receiver \in ActorPool

ImplPcStates == {"absent", "try", "done"}
AwaiterStates == {"idle", "armed", "cancelled", "timed_out"}
BridgePhases == {"ready", "armed", "message_ready", "timeout_ready", "done"}

VARIABLES impl_live, impl_pc, ready_queue,
          impl_pending, impl_timers, impl_observations,
          impl_msg_state, impl_time, awaiter_state, phase

bridge_vars ==
  <<impl_live, impl_pc, ready_queue,
    impl_pending, impl_timers, impl_observations,
    impl_msg_state, impl_time, awaiter_state, phase>>

ProjectedLive == impl_live

ProjectedPc == impl_pc

ProjectedReady == ReadyMembers(ready_queue)

ProjectedPending == impl_pending

ProjectedTimers == impl_timers

ProjectedObservations == impl_observations

ProjectedMsgState == impl_msg_state

ProjectedTime == impl_time

ReceiverReady == ReadyContains(ready_queue, Receiver)

ProjectionInit ==
  /\ impl_live = {Receiver}
  /\ impl_pc = [a \in ActorPool |-> IF a = Receiver THEN "try" ELSE "absent"]
  /\ ready_queue = ReadySingleton(Receiver)
  /\ impl_pending = [a \in ActorPool |-> NoPending]
  /\ impl_timers = [a \in ActorPool |-> NoDeadline]
  /\ impl_observations = [a \in ActorPool |-> <<>>]
  /\ impl_msg_state = [id \in MessageIds |-> "unused"]
  /\ impl_time = 0
  /\ awaiter_state = "idle"
  /\ phase = "ready"

ArmReceive ==
  /\ phase = "ready"
  /\ ReceiverReady
  /\ impl_pc[Receiver] = "try"
  /\ ready_queue' = ReadyEmpty
  /\ impl_timers' = [impl_timers EXCEPT ![Receiver] = impl_time + TimeoutDelay]
  /\ awaiter_state' = "armed"
  /\ phase' = "armed"
  /\ UNCHANGED <<impl_live, impl_pc, impl_pending,
                 impl_observations, impl_msg_state, impl_time>>

DeliverMessage ==
  /\ phase = "armed"
  /\ ~ReceiverReady
  /\ awaiter_state = "armed"
  /\ impl_pending[Receiver] = NoPending
  /\ impl_msg_state[FirstMessageId] = "unused"
  /\ impl_pending' = [impl_pending EXCEPT ![Receiver] = ScenarioPing]
  /\ ready_queue' = ReadySingleton(Receiver)
  /\ impl_timers' = [impl_timers EXCEPT ![Receiver] = NoDeadline]
  /\ impl_msg_state' = [impl_msg_state EXCEPT ![FirstMessageId] = "pending"]
  /\ awaiter_state' = "cancelled"
  /\ phase' = "message_ready"
  /\ UNCHANGED <<impl_live, impl_pc, impl_observations, impl_time>>

AdvanceTime ==
  /\ phase = "armed"
  /\ impl_timers[Receiver] # NoDeadline
  /\ impl_time < impl_timers[Receiver]
  /\ impl_time' = impl_timers[Receiver]
  /\ UNCHANGED <<impl_live, impl_pc, ready_queue, impl_pending,
                 impl_timers, impl_observations, impl_msg_state,
                 awaiter_state, phase>>

FireTimeout ==
  /\ phase = "armed"
  /\ impl_timers[Receiver] # NoDeadline
  /\ impl_time = impl_timers[Receiver]
  /\ impl_pending' = [impl_pending EXCEPT ![Receiver] = TimeoutToken]
  /\ ready_queue' = ReadySingleton(Receiver)
  /\ impl_timers' = [impl_timers EXCEPT ![Receiver] = NoDeadline]
  /\ awaiter_state' = "timed_out"
  /\ phase' = "timeout_ready"
  /\ UNCHANGED <<impl_live, impl_pc, impl_observations, impl_msg_state, impl_time>>

RunTimeout ==
  /\ phase \in {"message_ready", "timeout_ready"}
  /\ ReceiverReady
  /\ impl_pc[Receiver] = "try"
  /\ impl_pending[Receiver] # NoPending
  /\ impl_live' = {}
  /\ impl_pc' = [impl_pc EXCEPT ![Receiver] = "done"]
  /\ ready_queue' = ReadyEmpty
  /\ impl_pending' = [impl_pending EXCEPT ![Receiver] = NoPending]
  /\ impl_timers' = [impl_timers EXCEPT ![Receiver] = NoDeadline]
  /\ impl_observations' =
       [impl_observations EXCEPT ![Receiver] = Append(@, impl_pending[Receiver])]
  /\ impl_msg_state' =
       IF impl_pending[Receiver] = TimeoutToken
         THEN impl_msg_state
         ELSE [impl_msg_state EXCEPT ![impl_pending[Receiver].id] = "observed"]
  /\ awaiter_state' =
       IF phase = "message_ready" THEN "cancelled" ELSE "timed_out"
  /\ phase' = "done"
  /\ UNCHANGED impl_time

Done ==
  /\ phase = "done"
  /\ UNCHANGED bridge_vars

ProjectionNext ==
  \/ ArmReceive
  \/ DeliverMessage
  \/ AdvanceTime
  \/ FireTimeout
  \/ RunTimeout
  \/ Done

ProjectionSpec ==
  ProjectionInit /\ [][ProjectionNext]_bridge_vars

ImplTypeOK ==
  /\ impl_live \subseteq ActorPool
  /\ impl_pc \in [ActorPool -> ImplPcStates]
  /\ ReadySurfaceTypeOK(ready_queue)
  /\ impl_pending \in [ActorPool -> (MessageUniverse \cup {NoPending, TimeoutToken})]
  /\ DOMAIN impl_timers = ActorPool
  /\ \A a \in ActorPool : impl_timers[a] \in TimerValues
  /\ impl_observations \in [ActorPool -> Seq(ObservationUniverse)]
  /\ impl_msg_state \in [MessageIds -> MessageStates]
  /\ impl_time \in TimeValues
  /\ awaiter_state \in AwaiterStates
  /\ phase \in BridgePhases

ProjectionTypeOK ==
  /\ ProjectedLive \subseteq ActorPool
  /\ ProjectedReady \subseteq ActorPool
  /\ \A a \in ActorPool : ProjectedPc[a] \in PcStates
  /\ ProjectedPending \in [ActorPool -> (MessageUniverse \cup {NoPending, TimeoutToken})]
  /\ DOMAIN ProjectedTimers = ActorPool
  /\ \A a \in ActorPool : ProjectedTimers[a] \in TimerValues
  /\ ProjectedObservations \in [ActorPool -> Seq(ObservationUniverse)]
  /\ ProjectedMsgState \in [MessageIds -> MessageStates]
  /\ ProjectedTime \in TimeValues

ProjectedReadyActorsLive ==
  ReadySubsumesLive(ProjectedReady, ProjectedLive)

ArmedTimerProjectsBlockedTryReceive ==
  phase = "armed" =>
    /\ Receiver \notin ProjectedReady
    /\ ProjectedPc[Receiver] = "try"
    /\ ProjectedPending[Receiver] = NoPending
    /\ ProjectedTimers[Receiver] # NoDeadline
    /\ ProjectedTimers[Receiver] >= ProjectedTime

MessageWakeupCancelsTimer ==
  phase = "message_ready" =>
    /\ Receiver \in ProjectedReady
    /\ ProjectedPending[Receiver] = ScenarioPing
    /\ ProjectedTimers[Receiver] = NoDeadline
    /\ ProjectedMsgState[FirstMessageId] = "pending"

TimeoutFireProjectsPendingToken ==
  phase = "timeout_ready" =>
    /\ Receiver \in ProjectedReady
    /\ ProjectedPending[Receiver] = TimeoutToken
    /\ ProjectedTimers[Receiver] = NoDeadline
    /\ ProjectedTime = TimeoutDelay

CompletionProjectsSingleOutcome ==
  phase = "done" =>
    /\ ProjectedPc[Receiver] = "done"
    /\ Len(ProjectedObservations[Receiver]) = 1
    /\ (
         /\ ProjectedObservations[Receiver][1].kind = "Ping"
         /\ ProjectedObservations[Receiver][1].value = FirstPayload
         /\ ProjectedMsgState[FirstMessageId] = "observed"
       \/ /\ ProjectedObservations[Receiver][1] = TimeoutToken
          /\ ProjectedMsgState[FirstMessageId] = "unused"
       )

DoneLeavesNoPendingTimer ==
  phase = "done" =>
    /\ Receiver \notin ProjectedLive
    /\ Receiver \notin ProjectedReady
    /\ ProjectedPending[Receiver] = NoPending
    /\ ProjectedTimers[Receiver] = NoDeadline

=============================================================================