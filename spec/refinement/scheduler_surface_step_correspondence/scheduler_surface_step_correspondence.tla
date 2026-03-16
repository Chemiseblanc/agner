---- MODULE scheduler_surface_step_correspondence ----
(***************************************************************************)
(* Scheduler Surface Step Correspondence                                  *)
(*                                                                         *)
(* This scenario strengthens the refinement layer beyond one scripted      *)
(* trace. It keeps a bounded implementation-style scheduler surface, but   *)
(* lets scheduling, delivery, timeout, and exit-cleanup steps interleave   *)
(* nondeterministically.                                                    *)
(*                                                                         *)
(* Each implementation step is paired with an explicit abstract           *)
(* actor_system step over a separate state record. The checked result is   *)
(* still bounded, but it now discharges four stronger obligations at once: *)
(* - a general bounded implementation-surface Next relation                *)
(* - explicit step correspondence to actor_system actions                  *)
(* - bounded interleavings rather than one fixed phase trace               *)
(* - preservation of the shared actor_system invariants under that         *)
(*   correspondence                                                        *)
(***************************************************************************)
EXTENDS actor_defs, refinement_vocabulary, TLC

CONSTANTS Watcher, TimeoutActor, LinkedChild

ASSUME /\ Watcher \in ActorPool
       /\ TimeoutActor \in ActorPool
       /\ LinkedChild \in ActorPool
       /\ Cardinality({Watcher, TimeoutActor, LinkedChild}) = 3

ScenarioActors == {Watcher, TimeoutActor, LinkedChild}
ImplPcStates == {"absent", "collect", "try", "done"}
StepKinds == {"init", "run_watcher_block", "run_timeout_arm",
              "run_linked_child", "send_watcher_queued",
              "send_watcher_pending", "advance_time", "timeout_fire",
              "run_timeout_complete", "run_watcher_complete"}

VARIABLES impl_live, impl_kind, impl_pc, ready_queue,
          impl_mailboxes, impl_pending, impl_timers,
          impl_observations, impl_msg_state, impl_time,
          impl_links, impl_monitors, impl_exit_reason,
          abs_live, abs_kind, abs_pc, abs_ready,
          abs_mailboxes, abs_pending, abs_timers,
          abs_observations, abs_msg_state, abs_time,
          abs_links, abs_monitors, abs_exit_reason,
          last_step

      AS == INSTANCE actor_system
           WITH live <- abs_live,
              kind <- abs_kind,
              pc <- abs_pc,
              ready <- abs_ready,
              mailboxes <- abs_mailboxes,
              pending_result <- abs_pending,
              timers <- abs_timers,
              observations <- abs_observations,
              msg_state <- abs_msg_state,
              time <- abs_time,
              links <- abs_links,
              monitors <- abs_monitors,
              exit_reason <- abs_exit_reason

impl_vars ==
  <<impl_live, impl_kind, impl_pc, ready_queue,
    impl_mailboxes, impl_pending, impl_timers,
    impl_observations, impl_msg_state, impl_time,
    impl_links, impl_monitors, impl_exit_reason>>

abs_vars ==
  <<abs_live, abs_kind, abs_pc, abs_ready,
    abs_mailboxes, abs_pending, abs_timers,
    abs_observations, abs_msg_state, abs_time,
    abs_links, abs_monitors, abs_exit_reason>>

all_vars == <<impl_vars, abs_vars, last_step>>

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

ProjectionAgreement ==
  /\ abs_live = ProjectedLive
  /\ abs_kind = ProjectedKind
  /\ abs_pc = ProjectedPc
  /\ abs_ready = ProjectedReady
  /\ abs_mailboxes = ProjectedMailboxes
  /\ abs_pending = ProjectedPending
  /\ abs_timers = ProjectedTimers
  /\ abs_observations = ProjectedObservations
  /\ abs_msg_state = ProjectedMsgState
  /\ abs_time = ProjectedTime
  /\ abs_links = ProjectedLinks
  /\ abs_monitors = ProjectedMonitors
  /\ abs_exit_reason = ProjectedExitReason

ImplReady(a) == ReadyContains(ready_queue, a)

ActiveImplTimers ==
  {t \in TimeValues :
     \E a \in ActorPool :
       /\ impl_timers[a] # NoDeadline
       /\ impl_timers[a] = t}

WatcherConsumedMessage ==
  IF impl_pending[Watcher].kind = "Ping"
    THEN impl_pending[Watcher]
    ELSE impl_mailboxes[Watcher][FirstMatchIndex("collect", impl_mailboxes[Watcher])]

WatcherRemainingMailbox ==
  IF impl_pending[Watcher].kind = "Ping"
    THEN impl_mailboxes[Watcher]
    ELSE RemoveAt(impl_mailboxes[Watcher],
                  FirstMatchIndex("collect", impl_mailboxes[Watcher]))

WatcherNextMsgState ==
  MarkDropped([impl_msg_state EXCEPT ![WatcherConsumedMessage.id] = "observed"],
              WatcherRemainingMailbox)

Init ==
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
  /\ ready_queue = ReadyAdd(ReadyAdd(ReadySingleton(Watcher), TimeoutActor), LinkedChild)
  /\ impl_mailboxes = [a \in ActorPool |-> <<>>]
  /\ impl_pending =
       [a \in ActorPool |->
         IF a = LinkedChild THEN ScenarioSecondPing ELSE NoPending]
  /\ impl_timers = [a \in ActorPool |-> NoDeadline]
  /\ impl_observations = [a \in ActorPool |-> <<>>]
  /\ impl_msg_state =
       [id \in MessageIds |->
         IF id = SecondMessageId THEN "pending" ELSE "unused"]
  /\ impl_time = 0
  /\ impl_links = {<<Watcher, LinkedChild>>, <<LinkedChild, Watcher>>}
  /\ impl_monitors = {}
  /\ impl_exit_reason = [a \in ActorPool |-> "none"]
  /\ abs_live = impl_live
  /\ abs_kind = impl_kind
  /\ abs_pc = impl_pc
  /\ abs_ready = ReadyMembers(ready_queue)
  /\ abs_mailboxes = impl_mailboxes
  /\ abs_pending = impl_pending
  /\ abs_timers = impl_timers
  /\ abs_observations = impl_observations
  /\ abs_msg_state = impl_msg_state
  /\ abs_time = impl_time
  /\ abs_links = impl_links
  /\ abs_monitors = impl_monitors
  /\ abs_exit_reason = impl_exit_reason
  /\ last_step = "init"

ImplRunWatcherBlock ==
  /\ ImplReady(Watcher)
  /\ impl_pc[Watcher] = "collect"
  /\ impl_pending[Watcher] = NoPending
  /\ ~HasMatch("collect", impl_mailboxes[Watcher])
  /\ ready_queue' = ReadyRemove(ready_queue, Watcher)
  /\ UNCHANGED <<impl_live, impl_kind, impl_pc, impl_mailboxes,
                 impl_pending, impl_timers, impl_observations,
                 impl_msg_state, impl_time, impl_links,
                 impl_monitors, impl_exit_reason>>

ImplRunTimeoutArm ==
  /\ ImplReady(TimeoutActor)
  /\ impl_pc[TimeoutActor] = "try"
  /\ impl_pending[TimeoutActor] = NoPending
  /\ ~HasMatch("try", impl_mailboxes[TimeoutActor])
  /\ ready_queue' = ReadyRemove(ready_queue, TimeoutActor)
  /\ impl_timers' = [impl_timers EXCEPT ![TimeoutActor] = impl_time + TimeoutDelay]
  /\ UNCHANGED <<impl_live, impl_kind, impl_pc, impl_mailboxes,
                 impl_pending, impl_observations, impl_msg_state,
                 impl_time, impl_links, impl_monitors, impl_exit_reason>>

ImplRunLinkedChild ==
  /\ ImplReady(LinkedChild)
  /\ impl_pc[LinkedChild] = "collect"
  /\ impl_pending[LinkedChild] = ScenarioSecondPing
  /\ impl_live' = impl_live \ {LinkedChild}
  /\ impl_kind' = impl_kind
  /\ impl_pc' = [impl_pc EXCEPT ![LinkedChild] = "done"]
  /\ ready_queue' = ReadyRemove(ready_queue, LinkedChild)
  /\ impl_mailboxes' = [impl_mailboxes EXCEPT ![LinkedChild] = <<>>]
  /\ impl_pending' = [impl_pending EXCEPT ![LinkedChild] = NoPending]
  /\ impl_timers' = [impl_timers EXCEPT ![LinkedChild] = NoDeadline]
  /\ impl_observations' =
       [impl_observations EXCEPT ![LinkedChild] = Append(@, ScenarioSecondPing)]
  /\ impl_msg_state' = [impl_msg_state EXCEPT ![SecondMessageId] = "observed"]
  /\ impl_time' = impl_time
  /\ impl_links' = {pair \in impl_links : pair[1] # LinkedChild /\ pair[2] # LinkedChild}
  /\ impl_monitors' = impl_monitors
  /\ impl_exit_reason' = [impl_exit_reason EXCEPT ![LinkedChild] = ExitNormal]

ImplSendWatcherQueued ==
  /\ Watcher \in impl_live
  /\ ImplReady(Watcher)
  /\ impl_msg_state[FirstMessageId] = "unused"
  /\ impl_mailboxes' = [impl_mailboxes EXCEPT ![Watcher] = Append(@, ScenarioPing)]
  /\ impl_msg_state' = [impl_msg_state EXCEPT ![FirstMessageId] = "queued"]
  /\ UNCHANGED <<impl_live, impl_kind, impl_pc, ready_queue,
                 impl_pending, impl_timers, impl_observations,
                 impl_time, impl_links, impl_monitors, impl_exit_reason>>

ImplSendWatcherPending ==
  /\ Watcher \in impl_live
  /\ ~ImplReady(Watcher)
  /\ impl_pending[Watcher] = NoPending
  /\ Matches(impl_pc[Watcher], ScenarioPing)
  /\ impl_msg_state[FirstMessageId] = "unused"
  /\ ready_queue' = ReadyAdd(ready_queue, Watcher)
  /\ impl_mailboxes' = impl_mailboxes
  /\ impl_pending' = [impl_pending EXCEPT ![Watcher] = ScenarioPing]
  /\ impl_timers' = impl_timers
  /\ impl_msg_state' = [impl_msg_state EXCEPT ![FirstMessageId] = "pending"]
  /\ UNCHANGED <<impl_live, impl_kind, impl_pc, impl_observations,
                 impl_time, impl_links, impl_monitors, impl_exit_reason>>

ImplAdvanceTime ==
  /\ ActiveImplTimers # {}
  /\ impl_time < MinNat(ActiveImplTimers)
  /\ impl_time' = MinNat(ActiveImplTimers)
  /\ UNCHANGED <<impl_live, impl_kind, impl_pc, ready_queue,
                 impl_mailboxes, impl_pending, impl_timers,
                 impl_observations, impl_msg_state, impl_links,
                 impl_monitors, impl_exit_reason>>

ImplTimeoutFire ==
  /\ TimeoutActor \in impl_live
  /\ ~ImplReady(TimeoutActor)
  /\ impl_pc[TimeoutActor] = "try"
  /\ impl_pending[TimeoutActor] = NoPending
  /\ impl_timers[TimeoutActor] # NoDeadline
  /\ impl_timers[TimeoutActor] = impl_time
  /\ ready_queue' = ReadyAdd(ready_queue, TimeoutActor)
  /\ impl_pending' = [impl_pending EXCEPT ![TimeoutActor] = TimeoutToken]
  /\ impl_timers' = [impl_timers EXCEPT ![TimeoutActor] = NoDeadline]
  /\ UNCHANGED <<impl_live, impl_kind, impl_pc, impl_mailboxes,
                 impl_observations, impl_msg_state, impl_time,
                 impl_links, impl_monitors, impl_exit_reason>>

ImplRunTimeoutComplete ==
  /\ ImplReady(TimeoutActor)
  /\ impl_pc[TimeoutActor] = "try"
  /\ impl_pending[TimeoutActor] = TimeoutToken
  /\ impl_live' = impl_live \ {TimeoutActor}
  /\ impl_kind' = impl_kind
  /\ impl_pc' = [impl_pc EXCEPT ![TimeoutActor] = "done"]
  /\ ready_queue' = ReadyRemove(ready_queue, TimeoutActor)
  /\ impl_mailboxes' = [impl_mailboxes EXCEPT ![TimeoutActor] = <<>>]
  /\ impl_pending' = [impl_pending EXCEPT ![TimeoutActor] = NoPending]
  /\ impl_timers' = [impl_timers EXCEPT ![TimeoutActor] = NoDeadline]
  /\ impl_observations' =
       [impl_observations EXCEPT ![TimeoutActor] = Append(@, TimeoutToken)]
  /\ impl_msg_state' = impl_msg_state
  /\ impl_time' = impl_time
  /\ impl_links' = {pair \in impl_links : pair[1] # TimeoutActor /\ pair[2] # TimeoutActor}
  /\ impl_monitors' = {pair \in impl_monitors : pair[2] # TimeoutActor}
  /\ impl_exit_reason' = [impl_exit_reason EXCEPT ![TimeoutActor] = ExitNormal]

ImplRunWatcherComplete ==
  /\ ImplReady(Watcher)
  /\ impl_pc[Watcher] = "collect"
  /\ impl_pending[Watcher].kind = "Ping" \/ HasMatch("collect", impl_mailboxes[Watcher])
  /\ impl_live' = impl_live \ {Watcher}
  /\ impl_kind' = impl_kind
  /\ impl_pc' = [impl_pc EXCEPT ![Watcher] = "done"]
  /\ ready_queue' = ReadyRemove(ready_queue, Watcher)
  /\ impl_mailboxes' = [impl_mailboxes EXCEPT ![Watcher] = <<>>]
  /\ impl_pending' = [impl_pending EXCEPT ![Watcher] = NoPending]
  /\ impl_timers' = [impl_timers EXCEPT ![Watcher] = NoDeadline]
  /\ impl_observations' =
       [impl_observations EXCEPT ![Watcher] = Append(@, WatcherConsumedMessage)]
  /\ impl_msg_state' = WatcherNextMsgState
  /\ impl_time' = impl_time
  /\ impl_links' = {pair \in impl_links : pair[1] # Watcher /\ pair[2] # Watcher}
  /\ impl_monitors' = {pair \in impl_monitors : pair[2] # Watcher}
  /\ impl_exit_reason' = [impl_exit_reason EXCEPT ![Watcher] = ExitNormal]

ImplNext ==
  \/ ImplRunWatcherBlock
  \/ ImplRunTimeoutArm
  \/ ImplRunLinkedChild
  \/ ImplSendWatcherQueued
  \/ ImplSendWatcherPending
  \/ ImplAdvanceTime
  \/ ImplTimeoutFire
  \/ ImplRunTimeoutComplete
  \/ ImplRunWatcherComplete

RunWatcherBlockCorrespondence ==
  /\ ImplRunWatcherBlock
  /\ AS!RunCollector(Watcher)
  /\ ProjectionAgreement'
  /\ last_step' = "run_watcher_block"

RunTimeoutArmCorrespondence ==
  /\ ImplRunTimeoutArm
  /\ AS!RunTimeout(TimeoutActor)
  /\ ProjectionAgreement'
  /\ last_step' = "run_timeout_arm"

RunLinkedChildCorrespondence ==
  /\ ImplRunLinkedChild
  /\ AS!RunCollector(LinkedChild)
  /\ ProjectionAgreement'
  /\ last_step' = "run_linked_child"

SendWatcherQueuedCorrespondence ==
  /\ ImplSendWatcherQueued
  /\ AS!Send(Watcher, ScenarioPing)
  /\ ProjectionAgreement'
  /\ last_step' = "send_watcher_queued"

SendWatcherPendingCorrespondence ==
  /\ ImplSendWatcherPending
  /\ AS!Send(Watcher, ScenarioPing)
  /\ ProjectionAgreement'
  /\ last_step' = "send_watcher_pending"

AdvanceTimeCorrespondence ==
  /\ ImplAdvanceTime
  /\ AS!AdvanceTime
  /\ ProjectionAgreement'
  /\ last_step' = "advance_time"

TimeoutFireCorrespondence ==
  /\ ImplTimeoutFire
  /\ AS!TimeoutFire(TimeoutActor)
  /\ ProjectionAgreement'
  /\ last_step' = "timeout_fire"

RunTimeoutCompleteCorrespondence ==
  /\ ImplRunTimeoutComplete
  /\ AS!RunTimeout(TimeoutActor)
  /\ ProjectionAgreement'
  /\ last_step' = "run_timeout_complete"

RunWatcherCompleteCorrespondence ==
  /\ ImplRunWatcherComplete
  /\ AS!RunCollector(Watcher)
  /\ ProjectionAgreement'
  /\ last_step' = "run_watcher_complete"

Next ==
  \/ RunWatcherBlockCorrespondence
  \/ RunTimeoutArmCorrespondence
  \/ RunLinkedChildCorrespondence
  \/ SendWatcherQueuedCorrespondence
  \/ SendWatcherPendingCorrespondence
  \/ AdvanceTimeCorrespondence
  \/ TimeoutFireCorrespondence
  \/ RunTimeoutCompleteCorrespondence
  \/ RunWatcherCompleteCorrespondence

Spec ==
  Init /\ [][Next]_all_vars

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
  /\ last_step \in StepKinds

AbstractTypeOK == AS!TypeOK

AbstractCoreInvariants ==
  /\ AS!ReadyActorsAreLive
  /\ AS!PendingResultsAreReady
  /\ AS!BlockedActorsHaveNoMatches
  /\ AS!TimerDiscipline
  /\ AS!CompletedActorsClearedState
  /\ AS!AbsentActorsStayEmpty
  /\ AS!MessageOwnership
  /\ AS!LinksAreBidirectional
  /\ AS!LinksOnlyBetweenLive
  /\ AS!MonitorsOnlyFromLive
  /\ AS!DeadActorsHaveExitReason
  /\ AS!LiveActorsNoExitReason

LinkedCompletionCleansTopology ==
  abs_pc[LinkedChild] = "done" =>
    /\ <<Watcher, LinkedChild>> \notin abs_links
    /\ <<LinkedChild, Watcher>> \notin abs_links

WatcherCompletionObservesFirstPing ==
  abs_pc[Watcher] = "done" =>
    /\ abs_msg_state[FirstMessageId] = "observed"
    /\ \E i \in 1..Len(abs_observations[Watcher]) :
         abs_observations[Watcher][i] = ScenarioPing

LinkedChildCompletionObservesSecondPing ==
  abs_pc[LinkedChild] = "done" =>
    /\ abs_msg_state[SecondMessageId] = "observed"
    /\ \E i \in 1..Len(abs_observations[LinkedChild]) :
         abs_observations[LinkedChild][i] = ScenarioSecondPing

TimeoutCompletionObservesToken ==
  abs_pc[TimeoutActor] = "done" =>
    /\ \E i \in 1..Len(abs_observations[TimeoutActor]) :
         abs_observations[TimeoutActor][i] = TimeoutToken

=============================================================================