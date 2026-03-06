---- MODULE core_actor_system ----
EXTENDS Naturals, Sequences, FiniteSets, TLC

(***************************************************************************)
(* Source mapping                                                           *)
(*                                                                         *)
(* This module models the first formal boundary for Agner's actor runtime. *)
(*                                                                         *)
(* - SchedulerBase::spawn_impl() / run_actor() -> Spawn + RunReadyActor    *)
(* - SchedulerBase::send() and Actor::enqueue_message() -> Send            *)
(* - Actor::receive(), try_receive(), notify_waiter() -> Run* actions,     *)
(*   ready-set updates, pending_result, and timers                         *)
(* - Scheduler::schedule_after() plus deterministic logical time ->        *)
(*   timers, AdvanceTime, and TimeoutFire                                  *)
(*                                                                         *)
(* Deliberate abstractions:                                                 *)
(* - The scheduler is nondeterministic over enabled actors and due timers. *)
(* - Coroutines are reduced to observable suspension/resume points.        *)
(* - "Noise" stands in for unmatched system signals such as ExitSignal and *)
(*   DownSignal.                                                            *)
(* - Link/monitor propagation is intentionally deferred to a later module. *)
(***************************************************************************)

CONSTANTS ActorPool, MessageIds, Payloads, TimeoutDelay

ASSUME /\ Cardinality(ActorPool) >= 1
       /\ Cardinality(MessageIds) >= 2
       /\ Cardinality(Payloads) >= 2
       /\ TimeoutDelay \in Nat
       /\ TimeoutDelay > 0

SpawnKinds == {"collector", "sequence", "timeout"}
PcStates == {"absent", "collect", "seq_first", "seq_second", "try", "done"}
MessageKinds == {"Ping", "Noise"}
MessageStates == {"unused", "queued", "pending", "observed", "dropped"}

NoDeadline == 0 - 1
NoPending == [id |-> "none", kind |-> "None", value |-> "None"]
TimeoutToken == [id |-> "timeout", kind |-> "Timeout", value |-> "Timeout"]

Ping(id, value) == [id |-> id, kind |-> "Ping", value |-> value]
Noise(id, value) == [id |-> id, kind |-> "Noise", value |-> value]

MessageUniverse ==
  {Ping(id, value) : id \in MessageIds, value \in Payloads} \cup
  {Noise(id, value) : id \in MessageIds, value \in Payloads}

ObservationUniverse == MessageUniverse \cup {TimeoutToken}
MaxTick == Cardinality(ActorPool) * TimeoutDelay
TimeValues == 0..MaxTick
TimerValues == TimeValues \cup {NoDeadline}

ScenarioActor == CHOOSE actor \in ActorPool : TRUE
FirstMessageId == CHOOSE id \in MessageIds : TRUE
SecondMessageId == CHOOSE id \in (MessageIds \ {FirstMessageId}) : TRUE
FirstPayload == CHOOSE value \in Payloads : TRUE
SecondPayload == CHOOSE value \in (Payloads \ {FirstPayload}) : TRUE

ScenarioPing == Ping(FirstMessageId, FirstPayload)
ScenarioSecondPing == Ping(SecondMessageId, SecondPayload)

VARIABLES live, kind, pc, ready, mailboxes, pending_result, timers,
          observations, msg_state, time

vars ==
  <<live, kind, pc, ready, mailboxes, pending_result, timers,
    observations, msg_state, time>>

SpawnPc(actor_kind) ==
  CASE actor_kind = "collector" -> "collect"
    [] actor_kind = "sequence" -> "seq_first"
    [] actor_kind = "timeout" -> "try"
    [] OTHER -> "absent"

Matches(pc_state, msg) ==
  CASE pc_state = "collect" -> msg.kind = "Ping"
    [] pc_state = "seq_first" -> msg.kind = "Ping"
    [] pc_state = "seq_second" -> msg.kind = "Ping"
    [] pc_state = "try" -> msg.kind = "Ping"
    [] OTHER -> FALSE

MinNat(set) ==
  CHOOSE n \in set : \A m \in set : n <= m

MessageIdsIn(box) ==
  {box[i].id : i \in 1..Len(box)}

MatchingIndices(pc_state, box) ==
  {i \in 1..Len(box) : Matches(pc_state, box[i])}

HasMatch(pc_state, box) ==
  MatchingIndices(pc_state, box) # {}

FirstMatchIndex(pc_state, box) ==
  MinNat(MatchingIndices(pc_state, box))

RemoveAt(box, idx) ==
  SubSeq(box, 1, idx - 1) \o SubSeq(box, idx + 1, Len(box))

ObservationValues(obs) ==
  [i \in 1..Len(obs) |-> obs[i].value]

AvailableMessageIds ==
  {id \in MessageIds : msg_state[id] = "unused"}

ActiveTimers ==
  {t \in TimeValues :
     \E a \in ActorPool :
       /\ timers[a] # NoDeadline
       /\ timers[a] = t}

MarkDropped(state, box) ==
  [id \in MessageIds |->
    IF id \in MessageIdsIn(box) THEN "dropped" ELSE state[id]]

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

Spawn(a, actor_kind) ==
  /\ a \in ActorPool
  /\ pc[a] = "absent"
  /\ actor_kind \in SpawnKinds
  /\ live' = live \cup {a}
  /\ kind' = [kind EXCEPT ![a] = actor_kind]
  /\ pc' = [pc EXCEPT ![a] = SpawnPc(actor_kind)]
  /\ ready' = ready \cup {a}
  /\ UNCHANGED <<mailboxes, pending_result, timers, observations, msg_state, time>>

Send(target, msg) ==
  /\ target \in ActorPool
  /\ msg \in MessageUniverse
  /\ msg.id \in AvailableMessageIds
  /\ IF target \in live THEN
       /\ UNCHANGED <<live, kind, pc, observations, time>>
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
       ELSE /\ ready' = ready \ {a}
            /\ UNCHANGED <<live, kind, pc, mailboxes, pending_result, timers,
                           observations, msg_state, time>>

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
       ELSE /\ ready' = ready \ {a}
            /\ UNCHANGED <<live, kind, pc, mailboxes, pending_result, timers,
                           observations, msg_state, time>>

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
       ELSE /\ ready' = ready \ {a}
            /\ UNCHANGED <<live, kind, pc, mailboxes, pending_result, timers,
                           observations, msg_state, time>>

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

\* Focused scenarios mirroring runtime tests.

MailboxOrderingInit ==
  /\ live = {ScenarioActor}
  /\ kind =
       [a \in ActorPool |->
         IF a = ScenarioActor THEN "sequence" ELSE "none"]
  /\ pc =
       [a \in ActorPool |->
         IF a = ScenarioActor THEN "seq_first" ELSE "absent"]
  /\ ready = {ScenarioActor}
  /\ mailboxes =
       [a \in ActorPool |->
         IF a = ScenarioActor
           THEN <<ScenarioPing, ScenarioSecondPing>>
           ELSE <<>>]
  /\ pending_result = [a \in ActorPool |-> NoPending]
  /\ timers = [a \in ActorPool |-> NoDeadline]
  /\ observations = [a \in ActorPool |-> <<>>]
  /\ msg_state =
       [id \in MessageIds |->
         IF id = FirstMessageId \/ id = SecondMessageId
           THEN "queued"
           ELSE "unused"]
  /\ time = 0

MailboxOrderingNext ==
  RunReadyActor(ScenarioActor)

MailboxOrderingSpec ==
  MailboxOrderingInit /\ [][MailboxOrderingNext]_vars

MailboxOrderingOutcome ==
  pc[ScenarioActor] # "done" \/
  ObservationValues(observations[ScenarioActor]) =
    <<FirstPayload, SecondPayload>>

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

ReceiveSuspendsNext ==
  \/ RunReadyActor(ScenarioActor)
  \/ /\ pc[ScenarioActor] = "collect"
     /\ ScenarioActor \notin ready
     /\ pending_result[ScenarioActor] = NoPending
     /\ Send(ScenarioActor, ScenarioPing)

ReceiveSuspendsSpec ==
  ReceiveSuspendsInit /\ [][ReceiveSuspendsNext]_vars

ReceiveSuspendsOutcome ==
  pc[ScenarioActor] # "done" \/
  ObservationValues(observations[ScenarioActor]) = <<FirstPayload>>

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

TryReceiveRaceNext ==
  \/ RunReadyActor(ScenarioActor)
  \/ /\ pc[ScenarioActor] = "try"
     /\ ScenarioActor \notin ready
     /\ pending_result[ScenarioActor] = NoPending
     /\ msg_state[FirstMessageId] = "unused"
     /\ Send(ScenarioActor, ScenarioPing)
  \/ AdvanceTime
  \/ TimeoutFire(ScenarioActor)

TryReceiveRaceSpec ==
  TryReceiveRaceInit /\ [][TryReceiveRaceNext]_vars

TryReceiveOutcome ==
  pc[ScenarioActor] # "done" \/
  /\ Len(observations[ScenarioActor]) = 1
  /\ (
       /\ observations[ScenarioActor][1].kind = "Ping"
       /\ observations[ScenarioActor][1].value = FirstPayload
     \/ observations[ScenarioActor][1] = TimeoutToken
     )

MissingActorSendInit ==
  Init

MissingActorSendNext ==
  Send(ScenarioActor, ScenarioPing)

MissingActorSendSpec ==
  MissingActorSendInit /\ [][MissingActorSendNext]_vars

MissingActorSendOutcome ==
  /\ live = {}
  /\ ready = {}
  /\ msg_state[FirstMessageId] = "unused"
  /\ mailboxes[ScenarioActor] = <<>>
  /\ pending_result[ScenarioActor] = NoPending

=============================================================================
