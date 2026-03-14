---- MODULE actor_system ----
(***************************************************************************)
(* Actor System Core Specification                                         *)
(*                                                                         *)
(* This module models the formal boundary for Agner's actor runtime.       *)
(*                                                                         *)
(* Source mapping:                                                          *)
(* - SchedulerBase::spawn_impl() / run_actor() -> Spawn + RunReadyActor    *)
(* - SchedulerBase::send() and Actor::enqueue_message() -> Send            *)
(* - Actor::receive(), try_receive(), notify_waiter() -> Run* actions,     *)
(*   ready-set updates, pending_result, and timers                         *)
(* - Scheduler::schedule_after() plus deterministic logical time ->        *)
(*   timers, AdvanceTime, and TimeoutFire                                  *)
(* - SchedulerBase::link() -> Link, links variable                         *)
(* - SchedulerBase::monitor() -> Monitor, monitors variable                *)
(* - SchedulerBase::notify_exit() -> NotifyExit action                     *)
(*                                                                         *)
(* Deliberate abstractions:                                                 *)
(* - The scheduler is nondeterministic over enabled actors and due timers. *)
(* - Coroutines are reduced to observable suspension/resume points.        *)
(* - "Noise" stands in for unmatched system signals.                       *)
(***************************************************************************)
EXTENDS actor_defs, TLC

VARIABLES live, kind, pc, ready, mailboxes, pending_result, timers,
          observations, msg_state, time,
          links, monitors, exit_reason

vars ==
  <<live, kind, pc, ready, mailboxes, pending_result, timers,
    observations, msg_state, time, links, monitors, exit_reason>>

(***************************************************************************)
(* Convenience: unchanged groups for backward compatibility                *)
(***************************************************************************)
link_vars == <<links, monitors, exit_reason>>

(***************************************************************************)
(* State-dependent operators                                                *)
(***************************************************************************)
AvailableMessageIds ==
  {id \in MessageIds : msg_state[id] = "unused"}

ActiveTimers ==
  {t \in TimeValues :
     \E a \in ActorPool :
       /\ timers[a] # NoDeadline
       /\ timers[a] = t}

QueuedMessage(mid) ==
  \E a \in ActorPool :
    mid \in MessageIdsIn(mailboxes[a])

PendingMessage(mid) ==
  \E a \in ActorPool :
    /\ pending_result[a].kind \in MessageKinds
    /\ pending_result[a].id = mid

ObservedMessage(mid) ==
  \E a \in ActorPool :
    \E i \in 1..Len(observations[a]) :
      /\ observations[a][i].kind \in MessageKinds
      /\ observations[a][i].id = mid

(***************************************************************************)
(* Link/monitor helpers                                                     *)
(***************************************************************************)
LinkedTo(a) ==
  {b \in ActorPool : <<a, b>> \in links}

MonitoredBy(target) ==
  {m \in ActorPool : <<m, target>> \in monitors}

(***************************************************************************)
(* Type invariant                                                           *)
(***************************************************************************)
TypeOK ==
  /\ live \subseteq ActorPool
  /\ kind \in [ActorPool -> (SpawnKinds \cup {"none"})]
  /\ pc \in [ActorPool -> PcStates]
  /\ ready \subseteq ActorPool
  /\ mailboxes \in [ActorPool -> Seq(MessageUniverse)]
  /\ pending_result \in [ActorPool -> (MessageUniverse \cup {NoPending, TimeoutToken})]
  /\ DOMAIN timers = ActorPool
  /\ \A a \in ActorPool :
       timers[a] = NoDeadline \/ timers[a] \in TimeValues
  /\ observations \in [ActorPool -> Seq(ObservationUniverse)]
  /\ msg_state \in [MessageIds -> MessageStates]
  /\ time \in TimeValues
  /\ links \subseteq (ActorPool \X ActorPool)
  /\ monitors \subseteq (ActorPool \X ActorPool)
  /\ exit_reason \in [ActorPool -> ExitReasons \cup {"none"}]

ReadyActorsAreLive ==
  ready \subseteq live

PendingResultsAreReady ==
  \A a \in ActorPool :
    pending_result[a] # NoPending =>
      /\ a \in live
      /\ a \in ready
      /\ CASE pending_result[a] = TimeoutToken -> pc[a] = "try"
         [] OTHER -> Matches(pc[a], pending_result[a])

BlockedActorsHaveNoMatches ==
  \A a \in live \ ready :
    /\ pending_result[a] = NoPending
    /\ IF pc[a] \in {"collect", "seq_first", "seq_second", "try"}
          THEN MatchingIndices(pc[a], mailboxes[a]) = {}
          ELSE TRUE

TimerDiscipline ==
  \A a \in ActorPool :
    timers[a] # NoDeadline =>
      /\ a \in live
      /\ a \notin ready
      /\ pc[a] = "try"
      /\ pending_result[a] = NoPending
      /\ timers[a] >= time

CompletedActorsClearedState ==
  \A a \in ActorPool :
    pc[a] = "done" =>
      /\ a \notin live
      /\ mailboxes[a] = <<>>
      /\ pending_result[a] = NoPending
      /\ timers[a] = NoDeadline

AbsentActorsStayEmpty ==
  \A a \in ActorPool :
    pc[a] = "absent" =>
      /\ a \notin live
      /\ kind[a] = "none"
      /\ mailboxes[a] = <<>>
      /\ pending_result[a] = NoPending
      /\ timers[a] = NoDeadline
      /\ observations[a] = <<>>

MessageOwnership ==
  \A mid \in MessageIds :
    LET queued == QueuedMessage(mid) IN
    LET pending == PendingMessage(mid) IN
    LET observed == ObservedMessage(mid) IN
      CASE msg_state[mid] = "unused" ->
             /\ ~queued
             /\ ~pending
             /\ ~observed
        [] msg_state[mid] = "queued" ->
             /\ queued
             /\ ~pending
             /\ ~observed
        [] msg_state[mid] = "pending" ->
             /\ ~queued
             /\ pending
             /\ ~observed
        [] msg_state[mid] = "observed" ->
             /\ ~queued
             /\ ~pending
             /\ observed
        [] msg_state[mid] = "dropped" ->
             /\ ~queued
             /\ ~pending
             /\ ~observed
        [] OTHER -> FALSE

(***************************************************************************)
(* Link/monitor invariants                                                  *)
(***************************************************************************)
LinksAreBidirectional ==
  \A a, b \in ActorPool :
    <<a, b>> \in links => <<b, a>> \in links

LinksOnlyBetweenLive ==
  \A a, b \in ActorPool :
    <<a, b>> \in links => /\ a \in live /\ b \in live

MonitorsOnlyFromLive ==
  \A m, t \in ActorPool :
    <<m, t>> \in monitors => m \in live

DeadActorsHaveExitReason ==
  \A a \in ActorPool :
    pc[a] = "done" => exit_reason[a] \in ExitReasons

LiveActorsNoExitReason ==
  \A a \in ActorPool :
    a \in live => exit_reason[a] = "none"

Init ==
  /\ live = {}
  /\ kind = [a \in ActorPool |-> "none"]
  /\ pc = [a \in ActorPool |-> "absent"]
  /\ ready = {}
  /\ mailboxes = [a \in ActorPool |-> <<>>]
  /\ pending_result = [a \in ActorPool |-> NoPending]
  /\ timers = [a \in ActorPool |-> NoDeadline]
  /\ observations = [a \in ActorPool |-> <<>>]
  /\ msg_state = [id \in MessageIds |-> "unused"]
  /\ time = 0
  /\ links = {}
  /\ monitors = {}
  /\ exit_reason = [a \in ActorPool |-> "none"]

Spawn(a, actor_kind) ==
  /\ a \in ActorPool
  /\ pc[a] = "absent"
  /\ actor_kind \in SpawnKinds
  /\ live' = live \cup {a}
  /\ kind' = [kind EXCEPT ![a] = actor_kind]
  /\ pc' = [pc EXCEPT ![a] = SpawnPc(actor_kind)]
  /\ ready' = ready \cup {a}
  /\ UNCHANGED <<mailboxes, pending_result, timers, observations,
                 msg_state, time, links, monitors, exit_reason>>

(***************************************************************************)
(* Spawn with link: atomically spawn and establish bidirectional link       *)
(***************************************************************************)
SpawnLink(linker, a, actor_kind) ==
  /\ a \in ActorPool
  /\ pc[a] = "absent"
  /\ actor_kind \in SpawnKinds
  /\ linker \in live
  /\ linker # a
  /\ live' = live \cup {a}
  /\ kind' = [kind EXCEPT ![a] = actor_kind]
  /\ pc' = [pc EXCEPT ![a] = SpawnPc(actor_kind)]
  /\ ready' = ready \cup {a}
  /\ links' = links \cup {<<linker, a>>, <<a, linker>>}
  /\ UNCHANGED <<mailboxes, pending_result, timers, observations,
                 msg_state, time, monitors, exit_reason>>

(***************************************************************************)
(* Spawn with monitor: atomically spawn and set up monitoring              *)
(***************************************************************************)
SpawnMonitor(watcher, a, actor_kind) ==
  /\ a \in ActorPool
  /\ pc[a] = "absent"
  /\ actor_kind \in SpawnKinds
  /\ watcher \in live
  /\ watcher # a
  /\ live' = live \cup {a}
  /\ kind' = [kind EXCEPT ![a] = actor_kind]
  /\ pc' = [pc EXCEPT ![a] = SpawnPc(actor_kind)]
  /\ ready' = ready \cup {a}
  /\ monitors' = monitors \cup {<<watcher, a>>}
  /\ UNCHANGED <<mailboxes, pending_result, timers, observations,
                 msg_state, time, links, exit_reason>>

(***************************************************************************)
(* Link: establish bidirectional link between two live actors              *)
(***************************************************************************)
Link(a, b) ==
  /\ a \in live
  /\ b \in live
  /\ a # b
  /\ <<a, b>> \notin links
  /\ links' = links \cup {<<a, b>>, <<b, a>>}
  /\ UNCHANGED <<live, kind, pc, ready, mailboxes, pending_result, timers,
                 observations, msg_state, time, monitors, exit_reason>>

(***************************************************************************)
(* Monitor: actor a monitors actor b                                       *)
(***************************************************************************)
SetMonitor(a, b) ==
  /\ a \in live
  /\ b \in live
  /\ a # b
  /\ <<a, b>> \notin monitors
  /\ monitors' = monitors \cup {<<a, b>>}
  /\ UNCHANGED <<live, kind, pc, ready, mailboxes, pending_result, timers,
                 observations, msg_state, time, links, exit_reason>>

Send(target, msg) ==
  /\ target \in ActorPool
  /\ msg \in MessageUniverse
  /\ msg.id \in AvailableMessageIds
  /\ IF target \in live THEN
       /\ UNCHANGED <<live, kind, pc, observations, time, links, monitors,
                      exit_reason>>
       /\ IF /\ target \notin ready
             /\ pending_result[target] = NoPending
             /\ Matches(pc[target], msg)
            THEN /\ ready' = ready \cup {target}
                 /\ mailboxes' = mailboxes
                 /\ pending_result' = [pending_result EXCEPT ![target] = msg]
                 /\ timers' =
                      [timers EXCEPT
                        ![target] =
                          IF pc[target] = "try"
                            THEN NoDeadline
                            ELSE timers[target]]
                 /\ msg_state' = [msg_state EXCEPT ![msg.id] = "pending"]
            ELSE /\ ready' = ready
                 /\ mailboxes' = [mailboxes EXCEPT ![target] = Append(@, msg)]
                 /\ pending_result' = pending_result
                 /\ timers' = timers
                 /\ msg_state' = [msg_state EXCEPT ![msg.id] = "queued"]
     ELSE UNCHANGED vars

(***************************************************************************)
(* CompleteActor: shared cleanup when an actor finishes (normal or error)   *)
(* This is used by RunCollector, RunSequence*, RunTimeout and higher-level *)
(* actor kinds. It removes links, sends ExitSignal/DownSignal, and cleans  *)
(* up the actor's state.                                                    *)
(***************************************************************************)
CompleteActor(a, reason) ==
  /\ exit_reason' = [exit_reason EXCEPT ![a] = reason]
  /\ links' = {pair \in links :
                  pair[1] # a /\ pair[2] # a}
  /\ monitors' = {pair \in monitors : pair[2] # a}

RunCollector(a) ==
  /\ a \in ready
  /\ kind[a] = "collector"
  /\ pc[a] = "collect"
  /\ IF pending_result[a].kind = "Ping" \/ HasMatch("collect", mailboxes[a])
       THEN LET from_pending == pending_result[a].kind = "Ping"
                msg ==
                  IF from_pending
                    THEN pending_result[a]
                    ELSE mailboxes[a][FirstMatchIndex("collect", mailboxes[a])]
                rest ==
                  IF from_pending
                    THEN mailboxes[a]
                    ELSE RemoveAt(mailboxes[a],
                                  FirstMatchIndex("collect", mailboxes[a]))
                next_msg_state ==
                  MarkDropped([msg_state EXCEPT ![msg.id] = "observed"], rest)
            IN /\ live' = live \ {a}
               /\ kind' = kind
               /\ pc' = [pc EXCEPT ![a] = "done"]
               /\ ready' = ready \ {a}
               /\ mailboxes' = [mailboxes EXCEPT ![a] = <<>>]
               /\ pending_result' = [pending_result EXCEPT ![a] = NoPending]
               /\ timers' = [timers EXCEPT ![a] = NoDeadline]
               /\ observations' = [observations EXCEPT ![a] = Append(@, msg)]
               /\ msg_state' = next_msg_state
               /\ time' = time
               /\ CompleteActor(a, ExitNormal)
       ELSE /\ ready' = ready \ {a}
            /\ UNCHANGED <<live, kind, pc, mailboxes, pending_result, timers,
                           observations, msg_state, time, links, monitors,
                           exit_reason>>

RunSequenceFirst(a) ==
  /\ a \in ready
  /\ kind[a] = "sequence"
  /\ pc[a] = "seq_first"
  /\ IF pending_result[a].kind = "Ping" \/ HasMatch("seq_first", mailboxes[a])
       THEN LET from_pending == pending_result[a].kind = "Ping"
                msg1 ==
                  IF from_pending
                    THEN pending_result[a]
                    ELSE mailboxes[a][FirstMatchIndex("seq_first", mailboxes[a])]
                box1 ==
                  IF from_pending
                    THEN mailboxes[a]
                    ELSE RemoveAt(mailboxes[a],
                                  FirstMatchIndex("seq_first", mailboxes[a]))
                state1 == [msg_state EXCEPT ![msg1.id] = "observed"]
            IN IF HasMatch("seq_second", box1)
                 THEN LET idx2 == FirstMatchIndex("seq_second", box1)
                          msg2 == box1[idx2]
                          rest2 == RemoveAt(box1, idx2)
                          state2 ==
                            MarkDropped([state1 EXCEPT ![msg2.id] = "observed"],
                                        rest2)
                      IN /\ live' = live \ {a}
                         /\ kind' = kind
                         /\ pc' = [pc EXCEPT ![a] = "done"]
                         /\ ready' = ready \ {a}
                         /\ mailboxes' = [mailboxes EXCEPT ![a] = <<>>]
                         /\ pending_result' =
                              [pending_result EXCEPT ![a] = NoPending]
                         /\ timers' = [timers EXCEPT ![a] = NoDeadline]
                         /\ observations' =
                              [observations EXCEPT
                                ![a] = Append(Append(observations[a], msg1), msg2)]
                         /\ msg_state' = state2
                         /\ time' = time
                         /\ CompleteActor(a, ExitNormal)
                 ELSE /\ live' = live
                      /\ kind' = kind
                      /\ pc' = [pc EXCEPT ![a] = "seq_second"]
                      /\ ready' = ready \ {a}
                      /\ mailboxes' = [mailboxes EXCEPT ![a] = box1]
                      /\ pending_result' =
                           [pending_result EXCEPT ![a] = NoPending]
                      /\ timers' = timers
                      /\ observations' =
                           [observations EXCEPT ![a] = Append(@, msg1)]
                      /\ msg_state' = state1
                      /\ time' = time
                      /\ UNCHANGED link_vars
       ELSE /\ ready' = ready \ {a}
            /\ UNCHANGED <<live, kind, pc, mailboxes, pending_result, timers,
                           observations, msg_state, time, links, monitors,
                           exit_reason>>

RunSequenceSecond(a) ==
  /\ a \in ready
  /\ kind[a] = "sequence"
  /\ pc[a] = "seq_second"
  /\ IF pending_result[a].kind = "Ping" \/ HasMatch("seq_second", mailboxes[a])
       THEN LET from_pending == pending_result[a].kind = "Ping"
                msg ==
                  IF from_pending
                    THEN pending_result[a]
                    ELSE mailboxes[a][FirstMatchIndex("seq_second", mailboxes[a])]
                rest ==
                  IF from_pending
                    THEN mailboxes[a]
                    ELSE RemoveAt(mailboxes[a],
                                  FirstMatchIndex("seq_second", mailboxes[a]))
                next_msg_state ==
                  MarkDropped([msg_state EXCEPT ![msg.id] = "observed"], rest)
            IN /\ live' = live \ {a}
               /\ kind' = kind
               /\ pc' = [pc EXCEPT ![a] = "done"]
               /\ ready' = ready \ {a}
               /\ mailboxes' = [mailboxes EXCEPT ![a] = <<>>]
               /\ pending_result' = [pending_result EXCEPT ![a] = NoPending]
               /\ timers' = [timers EXCEPT ![a] = NoDeadline]
               /\ observations' = [observations EXCEPT ![a] = Append(@, msg)]
               /\ msg_state' = next_msg_state
               /\ time' = time
               /\ CompleteActor(a, ExitNormal)
       ELSE /\ ready' = ready \ {a}
            /\ UNCHANGED <<live, kind, pc, mailboxes, pending_result, timers,
                           observations, msg_state, time, links, monitors,
                           exit_reason>>

RunTimeout(a) ==
  /\ a \in ready
  /\ kind[a] = "timeout"
  /\ pc[a] = "try"
  /\ IF pending_result[a] = TimeoutToken
       THEN LET next_msg_state == MarkDropped(msg_state, mailboxes[a]) IN
            /\ live' = live \ {a}
            /\ kind' = kind
            /\ pc' = [pc EXCEPT ![a] = "done"]
            /\ ready' = ready \ {a}
            /\ mailboxes' = [mailboxes EXCEPT ![a] = <<>>]
            /\ pending_result' = [pending_result EXCEPT ![a] = NoPending]
            /\ timers' = [timers EXCEPT ![a] = NoDeadline]
            /\ observations' =
                 [observations EXCEPT ![a] = Append(@, TimeoutToken)]
            /\ msg_state' = next_msg_state
            /\ time' = time
            /\ CompleteActor(a, ExitNormal)
       ELSE IF pending_result[a].kind = "Ping" \/ HasMatch("try", mailboxes[a])
              THEN LET from_pending == pending_result[a].kind = "Ping"
                       msg ==
                         IF from_pending
                           THEN pending_result[a]
                           ELSE mailboxes[a][FirstMatchIndex("try", mailboxes[a])]
                       rest ==
                         IF from_pending
                           THEN mailboxes[a]
                           ELSE RemoveAt(mailboxes[a],
                                         FirstMatchIndex("try", mailboxes[a]))
                       next_msg_state ==
                         MarkDropped([msg_state EXCEPT ![msg.id] = "observed"],
                                     rest)
                   IN /\ live' = live \ {a}
                      /\ kind' = kind
                      /\ pc' = [pc EXCEPT ![a] = "done"]
                      /\ ready' = ready \ {a}
                      /\ mailboxes' = [mailboxes EXCEPT ![a] = <<>>]
                      /\ pending_result' =
                           [pending_result EXCEPT ![a] = NoPending]
                      /\ timers' = [timers EXCEPT ![a] = NoDeadline]
                      /\ observations' =
                           [observations EXCEPT ![a] = Append(@, msg)]
                      /\ msg_state' = next_msg_state
                      /\ time' = time
                      /\ CompleteActor(a, ExitNormal)
              ELSE /\ live' = live
                   /\ kind' = kind
                   /\ pc' = pc
                   /\ ready' = ready \ {a}
                   /\ mailboxes' = mailboxes
                   /\ pending_result' = pending_result
                   /\ timers' = [timers EXCEPT ![a] = time + TimeoutDelay]
                   /\ observations' = observations
                   /\ msg_state' = msg_state
                   /\ time' = time
                   /\ UNCHANGED link_vars

RunReadyActor(a) ==
  \/ RunCollector(a)
  \/ RunSequenceFirst(a)
  \/ RunSequenceSecond(a)
  \/ RunTimeout(a)

AdvanceTime ==
  /\ ready = {}
  /\ ActiveTimers # {}
  /\ time < MinNat(ActiveTimers)
  /\ live' = live
  /\ kind' = kind
  /\ pc' = pc
  /\ ready' = ready
  /\ mailboxes' = mailboxes
  /\ pending_result' = pending_result
  /\ timers' = timers
  /\ observations' = observations
  /\ msg_state' = msg_state
  /\ time' = MinNat(ActiveTimers)
  /\ UNCHANGED link_vars

TimeoutFire(a) ==
  /\ a \in ActorPool
  /\ a \in live
  /\ a \notin ready
  /\ pc[a] = "try"
  /\ pending_result[a] = NoPending
  /\ timers[a] # NoDeadline
  /\ timers[a] = time
  /\ live' = live
  /\ kind' = kind
  /\ pc' = pc
  /\ ready' = ready \cup {a}
  /\ mailboxes' = mailboxes
  /\ pending_result' = [pending_result EXCEPT ![a] = TimeoutToken]
  /\ timers' = [timers EXCEPT ![a] = NoDeadline]
  /\ observations' = observations
  /\ msg_state' = msg_state
  /\ time' = time
  /\ UNCHANGED link_vars

Next ==
  \/ \E a \in ActorPool :
       \E actor_kind \in SpawnKinds :
         Spawn(a, actor_kind)
  \/ \E target \in ActorPool :
       \E id \in AvailableMessageIds :
         \E value \in Payloads :
           Send(target, Ping(id, value))
  \/ \E target \in ActorPool :
       \E id \in AvailableMessageIds :
         \E value \in Payloads :
           Send(target, Noise(id, value))
  \/ \E a \in ready :
       RunReadyActor(a)
  \/ AdvanceTime
  \/ \E a \in ActorPool :
       TimeoutFire(a)

Spec ==
  Init /\ [][Next]_vars

=============================================================================
