---- MODULE scheduler_surface_core_projection ----
(* ************************************************************************* *)
(* Scheduler Surface Core Projection                                      *)
(*                                                                         *)
(* Projects a bounded implementation-style scheduler surface into one     *)
(* full core-facing state view.                                           *)
(*                                                                         *)
(* The scenario composes:                                                  *)
(* - queued message delivery to a collector                                *)
(* - timeout arming and timeout-token delivery                             *)
(* - linked actor exit cleanup and ExitSignal observation                  *)
(* - monitor cleanup on target completion                                  *)
(* - ready queue to ready set projection                                   *)
(* ************************************************************************* *)
EXTENDS actor_defs, refinement_vocabulary, TLC

CONSTANTS Watcher, TimeoutActor, LinkedChild

ASSUME /\ Watcher \in ActorPool
       /\ TimeoutActor \in ActorPool
       /\ LinkedChild \in ActorPool
       /\ Cardinality({Watcher, TimeoutActor, LinkedChild}) = 3

ScenarioActors == {Watcher, TimeoutActor, LinkedChild}

ImplPcStates == {"absent", "collect", "try", "done"}
BridgePhases == {"start", "message_queued", "timer_armed", "linked_exited",
                 "time_advanced", "timeout_ready", "timeout_done",
                 "watcher_done", "done"}

VARIABLES impl_live, impl_kind, impl_pc, ready_queue,
          impl_mailboxes, impl_pending, impl_timers,
          impl_observations, impl_msg_state, impl_time,
          impl_links, impl_monitors, impl_exit_reason, phase

bridge_vars ==
  <<impl_live, impl_kind, impl_pc, ready_queue,
    impl_mailboxes, impl_pending, impl_timers,
    impl_observations, impl_msg_state, impl_time,
    impl_links, impl_monitors, impl_exit_reason, phase>>

ProjectedLive == impl_live

ProjectedKind == impl_kind

ProjectedPc == impl_pc

ProjectedReady == ReadyMembers(ready_queue)

ProjectedMailboxes == impl_mailboxes

ProjectedPending == impl_pending

ProjectedTimers == impl_timers

ProjectedObservations == impl_observations

ProjectedMsgState == impl_msg_state

ProjectedTime == impl_time

ProjectedLinks == impl_links

ProjectedMonitors == impl_monitors

ProjectedExitReason == impl_exit_reason

ProjectedVars ==
  [live |-> ProjectedLive,
   kind |-> ProjectedKind,
   pc |-> ProjectedPc,
   ready |-> ProjectedReady,
   mailboxes |-> ProjectedMailboxes,
   pending_result |-> ProjectedPending,
   timers |-> ProjectedTimers,
   observations |-> ProjectedObservations,
   msg_state |-> ProjectedMsgState,
   time |-> ProjectedTime,
   links |-> ProjectedLinks,
   monitors |-> ProjectedMonitors,
   exit_reason |-> ProjectedExitReason]

ProjectionInit ==
  /\ impl_live = ScenarioActors
  /\ impl_kind =
       [a \in ActorPool |->
         IF a = Watcher THEN "collector"
         ELSE IF a = TimeoutActor THEN "timeout"
         ELSE IF a = LinkedChild THEN "collector"
         ELSE "none"]
  /\ impl_pc =
       [a \in ActorPool |->
         IF a = Watcher THEN "collect"
         ELSE IF a = TimeoutActor THEN "try"
         ELSE IF a = LinkedChild THEN "collect"
         ELSE "absent"]
  /\ ready_queue = ReadyAdd(ReadySingleton(Watcher), TimeoutActor)
  /\ impl_mailboxes = [a \in ActorPool |-> <<>>]
  /\ impl_pending = [a \in ActorPool |-> NoPending]
  /\ impl_timers = [a \in ActorPool |-> NoDeadline]
  /\ impl_observations = [a \in ActorPool |-> <<>>]
  /\ impl_msg_state = [id \in MessageIds |-> "unused"]
  /\ impl_time = 0
  /\ impl_links = {<<Watcher, LinkedChild>>, <<LinkedChild, Watcher>>}
  /\ impl_monitors = {<<Watcher, TimeoutActor>>}
  /\ impl_exit_reason = [a \in ActorPool |-> "none"]
  /\ phase = "start"

QueueWatcherMessage ==
  /\ phase = "start"
  /\ Watcher \in ProjectedReady
  /\ impl_msg_state[FirstMessageId] = "unused"
  /\ impl_mailboxes' = [impl_mailboxes EXCEPT ![Watcher] = Append(@, ScenarioPing)]
  /\ impl_msg_state' = [impl_msg_state EXCEPT ![FirstMessageId] = "queued"]
  /\ phase' = "message_queued"
  /\ UNCHANGED <<impl_live, impl_kind, impl_pc, ready_queue, impl_pending,
                 impl_timers, impl_observations, impl_time,
                 impl_links, impl_monitors, impl_exit_reason>>

ArmTimeoutActor ==
  /\ phase = "message_queued"
  /\ ReadyMembers(ready_queue) = {Watcher, TimeoutActor}
  /\ ready_queue' = ReadyRemove(ready_queue, TimeoutActor)
  /\ impl_timers' = [impl_timers EXCEPT ![TimeoutActor] = impl_time + TimeoutDelay]
  /\ phase' = "timer_armed"
  /\ UNCHANGED <<impl_live, impl_kind, impl_pc, impl_mailboxes,
                 impl_pending, impl_observations, impl_msg_state,
                 impl_time, impl_links, impl_monitors, impl_exit_reason>>

ExitLinkedChild ==
  /\ phase = "timer_armed"
  /\ LinkedChild \in impl_live
  /\ impl_live' = impl_live \ {LinkedChild}
  /\ impl_pc' = [impl_pc EXCEPT ![LinkedChild] = "done"]
  /\ impl_mailboxes' = [impl_mailboxes EXCEPT ![LinkedChild] = <<>>]
  /\ impl_pending' = [impl_pending EXCEPT ![LinkedChild] = NoPending]
  /\ impl_timers' = [impl_timers EXCEPT ![LinkedChild] = NoDeadline]
  /\ impl_observations' =
       [impl_observations EXCEPT ![Watcher] = Append(@, ExitSignal(LinkedChild, ExitNormal))]
  /\ impl_time' = impl_time
  /\ impl_links' = {pair \in impl_links : pair[1] # LinkedChild /\ pair[2] # LinkedChild}
  /\ impl_monitors' = impl_monitors
  /\ impl_exit_reason' = [impl_exit_reason EXCEPT ![LinkedChild] = ExitNormal]
  /\ phase' = "linked_exited"
  /\ UNCHANGED <<impl_kind, ready_queue, impl_msg_state>>

AdvanceTimeToDeadline ==
  /\ phase = "linked_exited"
  /\ impl_timers[TimeoutActor] # NoDeadline
  /\ impl_time < impl_timers[TimeoutActor]
  /\ impl_time' = impl_timers[TimeoutActor]
  /\ phase' = "time_advanced"
  /\ UNCHANGED <<impl_live, impl_kind, impl_pc, ready_queue,
                 impl_mailboxes, impl_pending, impl_timers,
                 impl_observations, impl_msg_state,
                 impl_links, impl_monitors, impl_exit_reason>>

FireTimeout ==
  /\ phase = "time_advanced"
  /\ impl_time = impl_timers[TimeoutActor]
  /\ impl_pending' = [impl_pending EXCEPT ![TimeoutActor] = TimeoutToken]
  /\ ready_queue' = ReadyAdd(ready_queue, TimeoutActor)
  /\ impl_timers' = [impl_timers EXCEPT ![TimeoutActor] = NoDeadline]
  /\ phase' = "timeout_ready"
  /\ UNCHANGED <<impl_live, impl_kind, impl_pc, impl_mailboxes,
                 impl_observations, impl_msg_state, impl_time,
                 impl_links, impl_monitors, impl_exit_reason>>

RunTimeoutActor ==
  /\ phase = "timeout_ready"
  /\ TimeoutActor \in ProjectedReady
  /\ impl_pending[TimeoutActor] = TimeoutToken
  /\ impl_live' = impl_live \ {TimeoutActor}
  /\ impl_pc' = [impl_pc EXCEPT ![TimeoutActor] = "done"]
  /\ ready_queue' = ReadyRemove(ready_queue, TimeoutActor)
  /\ impl_mailboxes' = [impl_mailboxes EXCEPT ![TimeoutActor] = <<>>]
  /\ impl_pending' = [impl_pending EXCEPT ![TimeoutActor] = NoPending]
  /\ impl_timers' = [impl_timers EXCEPT ![TimeoutActor] = NoDeadline]
  /\ impl_observations' =
       [impl_observations EXCEPT ![TimeoutActor] = Append(@, TimeoutToken)]
  /\ impl_msg_state' = impl_msg_state
  /\ impl_links' = impl_links
  /\ impl_monitors' = {pair \in impl_monitors : pair[2] # TimeoutActor}
  /\ impl_exit_reason' = [impl_exit_reason EXCEPT ![TimeoutActor] = ExitNormal]
  /\ phase' = "timeout_done"
  /\ UNCHANGED <<impl_kind, impl_time>>

RunWatcher ==
  /\ phase = "timeout_done"
  /\ Watcher \in ProjectedReady
  /\ HasMatch("collect", impl_mailboxes[Watcher])
  /\ impl_live' = impl_live \ {Watcher}
  /\ impl_pc' = [impl_pc EXCEPT ![Watcher] = "done"]
  /\ ready_queue' = ReadyRemove(ready_queue, Watcher)
  /\ impl_mailboxes' = [impl_mailboxes EXCEPT ![Watcher] = <<>>]
  /\ impl_pending' = [impl_pending EXCEPT ![Watcher] = NoPending]
  /\ impl_timers' = [impl_timers EXCEPT ![Watcher] = NoDeadline]
  /\ impl_observations' =
       [impl_observations EXCEPT ![Watcher] = Append(@, ScenarioPing)]
  /\ impl_msg_state' = [impl_msg_state EXCEPT ![FirstMessageId] = "observed"]
  /\ impl_time' = impl_time
  /\ impl_links' = impl_links
  /\ impl_monitors' = impl_monitors
  /\ impl_exit_reason' = [impl_exit_reason EXCEPT ![Watcher] = ExitNormal]
  /\ phase' = "watcher_done"
  /\ UNCHANGED impl_kind

Done ==
  /\ phase \in {"watcher_done", "done"}
  /\ phase' = "done"
  /\ UNCHANGED <<impl_live, impl_kind, impl_pc, ready_queue,
                 impl_mailboxes, impl_pending, impl_timers,
                 impl_observations, impl_msg_state, impl_time,
                 impl_links, impl_monitors, impl_exit_reason>>

ProjectionNext ==
  \/ QueueWatcherMessage
  \/ ArmTimeoutActor
  \/ ExitLinkedChild
  \/ AdvanceTimeToDeadline
  \/ FireTimeout
  \/ RunTimeoutActor
  \/ RunWatcher
  \/ Done

ProjectionSpec ==
  ProjectionInit /\ [][ProjectionNext]_bridge_vars

ImplTypeOK ==
  /\ impl_live \subseteq ActorPool
  /\ impl_kind \in [ActorPool -> (SpawnKinds \cup {"none"})]
  /\ impl_pc \in [ActorPool -> ImplPcStates]
  /\ ReadySurfaceTypeOK(ready_queue)
  /\ impl_mailboxes \in [ActorPool -> Seq(MessageUniverse)]
  /\ impl_pending \in [ActorPool -> (MessageUniverse \cup {NoPending, TimeoutToken})]
  /\ DOMAIN impl_timers = ActorPool
  /\ \A a \in ActorPool : impl_timers[a] \in TimerValues
  /\ impl_observations \in [ActorPool -> Seq(ObservationUniverse)]
  /\ impl_msg_state \in [MessageIds -> MessageStates]
  /\ impl_time \in TimeValues
  /\ impl_links \subseteq (ActorPool \X ActorPool)
  /\ impl_monitors \subseteq (ActorPool \X ActorPool)
  /\ impl_exit_reason \in [ActorPool -> ExitReasons \cup {"none"}]
  /\ phase \in BridgePhases

ProjectionTypeOK ==
  /\ ProjectedLive \subseteq ActorPool
  /\ ProjectedKind \in [ActorPool -> (SpawnKinds \cup {"none"})]
  /\ \A a \in ActorPool : ProjectedPc[a] \in PcStates
  /\ ProjectedReady \subseteq ActorPool
  /\ ProjectedMailboxes \in [ActorPool -> Seq(MessageUniverse)]
  /\ ProjectedPending \in [ActorPool -> (MessageUniverse \cup {NoPending, TimeoutToken})]
  /\ DOMAIN ProjectedTimers = ActorPool
  /\ \A a \in ActorPool : ProjectedTimers[a] \in TimerValues
  /\ ProjectedObservations \in [ActorPool -> Seq(ObservationUniverse)]
  /\ ProjectedMsgState \in [MessageIds -> MessageStates]
  /\ ProjectedTime \in TimeValues
  /\ ProjectedLinks \subseteq (ActorPool \X ActorPool)
  /\ ProjectedMonitors \subseteq (ActorPool \X ActorPool)
  /\ ProjectedExitReason \in [ActorPool -> ExitReasons \cup {"none"}]

ProjectedReadyActorsLive ==
  ReadySubsumesLive(ProjectedReady, ProjectedLive)

ProjectedPendingResultsReady ==
  PendingResultsReady(ProjectedPending, ProjectedLive, ProjectedReady, ProjectedPc)

ProjectedBlockedActorsHaveNoMatches ==
  BlockedActorsNoMatches(ProjectedLive, ProjectedReady,
                         ProjectedPending, ProjectedPc, ProjectedMailboxes)

ProjectedTimerDiscipline ==
  TimerDisciplineHolds(ProjectedTimers, ProjectedLive,
                       ProjectedReady, ProjectedPc,
                       ProjectedPending, ProjectedTime)

ProjectedCompletedActorsClearedState ==
  CompletedActorsCleared(ProjectedPc, ProjectedLive,
                         ProjectedMailboxes, ProjectedPending,
                         ProjectedTimers)

ProjectedMessageOwnership ==
  MessageOwnershipHolds(ProjectedMsgState, ProjectedMailboxes,
            ProjectedPending, ProjectedObservations)

ProjectedLinksAreBidirectional ==
  BiDirectionalLinks(ProjectedLinks)

ProjectedLinksOnlyBetweenLive ==
  LinksBoundToLive(ProjectedLinks, ProjectedLive)

ProjectedMonitorsOnlyFromLive ==
  MonitorsBoundToLive(ProjectedMonitors, ProjectedLive)

ProjectedDeadActorsHaveExitReason ==
  DeadHaveReason(ProjectedPc, ProjectedExitReason)

ProjectedLiveActorsNoExitReason ==
  LiveHaveNoReason(ProjectedLive, ProjectedExitReason)

FinalOutcomeConsistent ==
  phase = "done" =>
    /\ ProjectedLive = {}
    /\ ProjectedLinks = {}
    /\ ProjectedMonitors = {}
    /\ Len(ProjectedObservations[Watcher]) = 2
    /\ ProjectedObservations[Watcher][1].kind = "ExitSignal"
    /\ ProjectedObservations[Watcher][1].from = LinkedChild
    /\ ProjectedObservations[Watcher][2].kind = "Ping"
    /\ ProjectedObservations[TimeoutActor] = <<TimeoutToken>>
    /\ ProjectedMsgState[FirstMessageId] = "observed"

=============================================================================