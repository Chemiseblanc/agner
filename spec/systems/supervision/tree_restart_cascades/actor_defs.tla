---- MODULE actor_defs ----
(***************************************************************************)
(* Shared definitions for Agner actor system specifications.              *)
(*                                                                         *)
(* This module provides common types, operators, and helper functions      *)
(* used across all actor system scenarios.                                 *)
(***************************************************************************)
EXTENDS Naturals, Sequences, FiniteSets

(***************************************************************************)
(* Constants (parameterized per-config)                                    *)
(***************************************************************************)
CONSTANTS ActorPool, MessageIds, Payloads, TimeoutDelay

ASSUME /\ Cardinality(ActorPool) >= 1
       /\ Cardinality(MessageIds) >= 2
       /\ Cardinality(Payloads) >= 2
       /\ TimeoutDelay \in Nat
       /\ TimeoutDelay > 0

(***************************************************************************)
(* Exit reasons                                                             *)
(***************************************************************************)
ExitNormal  == "normal"
ExitStopped == "stopped"
ExitError   == "error"
ExitReasons == {ExitNormal, ExitStopped, ExitError}

(***************************************************************************)
(* Type definitions                                                         *)
(***************************************************************************)
SpawnKinds == {"collector", "sequence", "timeout"}
PcStates == {"absent", "collect", "seq_first", "seq_second", "try", "done"}
MessageKinds == {"Ping", "Noise"}
MessageStates == {"unused", "queued", "pending", "observed", "dropped"}

NoDeadline == 0 - 1
NoPending == [id |-> "none", kind |-> "None", value |-> "None"]
TimeoutToken == [id |-> "timeout", kind |-> "Timeout", value |-> "Timeout"]

(***************************************************************************)
(* Signal constructors                                                      *)
(***************************************************************************)
ExitSignal(from, reason) == [kind |-> "ExitSignal", from |-> from, reason |-> reason]
DownSignal(from, reason) == [kind |-> "DownSignal", from |-> from, reason |-> reason]

SignalKinds == {"ExitSignal", "DownSignal"}

(***************************************************************************)
(* Message constructors                                                     *)
(***************************************************************************)
Ping(id, value) == [id |-> id, kind |-> "Ping", value |-> value]
Noise(id, value) == [id |-> id, kind |-> "Noise", value |-> value]

MessageUniverse ==
  {Ping(id, value) : id \in MessageIds, value \in Payloads} \cup
  {Noise(id, value) : id \in MessageIds, value \in Payloads}

SignalUniverse ==
  {ExitSignal(a, r) : a \in ActorPool, r \in ExitReasons} \cup
  {DownSignal(a, r) : a \in ActorPool, r \in ExitReasons}

ObservationUniverse == MessageUniverse \cup SignalUniverse \cup {TimeoutToken}

(***************************************************************************)
(* Time domain                                                              *)
(***************************************************************************)
MaxTick == Cardinality(ActorPool) * TimeoutDelay
TimeValues == 0..MaxTick
TimerValues == TimeValues \cup {NoDeadline}

(***************************************************************************)
(* Scenario constants (deterministic choices for focused tests)            *)
(***************************************************************************)
ScenarioActor == CHOOSE actor \in ActorPool : TRUE
FirstMessageId == CHOOSE id \in MessageIds : TRUE
SecondMessageId == CHOOSE id \in (MessageIds \ {FirstMessageId}) : TRUE
FirstPayload == CHOOSE value \in Payloads : TRUE
SecondPayload == CHOOSE value \in (Payloads \ {FirstPayload}) : TRUE

ScenarioPing == Ping(FirstMessageId, FirstPayload)
ScenarioSecondPing == Ping(SecondMessageId, SecondPayload)

(***************************************************************************)
(* Helper operators                                                         *)
(***************************************************************************)
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

(***************************************************************************)
(* Mailbox operations                                                       *)
(***************************************************************************)
SendMsg(mailboxes, actor, msg) ==
  [mailboxes EXCEPT ![actor] = Append(@, msg)]

ReceiveMsg(mailboxes, actor) ==
  [mailboxes EXCEPT ![actor] = Tail(@)]

HasMessages(mailboxes, actor) ==
  Len(mailboxes[actor]) > 0

MarkDropped(state, box) ==
  [id \in MessageIds |->
    IF id \in MessageIdsIn(box) THEN "dropped" ELSE state[id]]

====
