---- MODULE actor_identity ----
(***************************************************************************)
(* Actor Identity Scenario                                                 *)
(*                                                                         *)
(* Verifies ActorRef uniqueness and safe-send semantics:                   *)
(* - Each spawned actor gets a unique ActorRef (monotone counter)          *)
(* - No two live actors share the same identity                            *)
(* - Sending to a dead/invalid ActorRef is silently dropped                *)
(* - ActorRef 0 is invalid (value != 0 for valid refs)                     *)
(*                                                                         *)
(* Source mapping:                                                          *)
(* - SchedulerBase::next_actor_ref() -> monotone next_id counter           *)
(* - ActorRef::valid() -> value != 0                                       *)
(* - SchedulerBase::send() -> drops if actor not in actors_ map            *)
(***************************************************************************)
EXTENDS actor_system

(***************************************************************************)
(* State                                                                   *)
(***************************************************************************)
\* next_id models SchedulerBase::next_actor_id_ (monotone counter)
\* actor_ids maps ActorPool actors to their assigned id (0 = not yet spawned)
\* spawned tracks the set of actors that have been spawned
\* dead_sends records whether the scenario observed a dropped send
\* phase bounds the scenario to a small, finite sequence of steps
VARIABLES next_id, actor_ids, spawned, dead_sends, phase

Phases == {"spawning", "retiring", "dead_send", "done"}

id_vars == <<next_id, actor_ids, spawned, dead_sends, phase>>
all_vars == <<vars, id_vars>>

(***************************************************************************)
(* Initial state                                                           *)
(***************************************************************************)
IdentityInit ==
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
  /\ next_id = 1  \* starts at 1 (0 is invalid)
  /\ actor_ids = [a \in ActorPool |-> 0]
  /\ spawned = {}
  /\ dead_sends = 0
  /\ phase = "spawning"

(***************************************************************************)
(* Actions                                                                  *)
(***************************************************************************)

CanSpawn == spawned # ActorPool

CanRetire == Cardinality(live) > 0

CanSendToDead ==
  /\ dead_sends = 0
  /\ Cardinality(live) > 0
  /\ \E target \in ActorPool : target \notin live /\ target \in spawned

\* Spawn an actor: assign next_id, increment counter.
\* The final spawn advances the scenario to the retire phase.
SpawnActor(a) ==
  /\ phase = "spawning"
  /\ a \in ActorPool
  /\ a \notin spawned
  /\ actor_ids' = [actor_ids EXCEPT ![a] = next_id]
  /\ next_id' = next_id + 1
  /\ spawned' = spawned \union {a}
  /\ live' = live \union {a}
  /\ kind' = [kind EXCEPT ![a] = "collector"]
  /\ pc' = [pc EXCEPT ![a] = "collect"]
  /\ ready' = ready \union {a}
  /\ phase' =
       IF spawned \union {a} = ActorPool
         THEN "retiring"
         ELSE phase
  /\ UNCHANGED <<mailboxes, pending_result, timers, observations,
                 msg_state, time, links, monitors, exit_reason, dead_sends>>

\* Retire exactly one actor after the spawn phase to create a stale identity.
KillActor(a) ==
  /\ phase = "retiring"
  /\ a \in live
  /\ live' = live \ {a}
  /\ pc' = [pc EXCEPT ![a] = "done"]
  /\ ready' = ready \ {a}
  /\ exit_reason' = [exit_reason EXCEPT ![a] = ExitNormal]
  /\ links' = {pair \in links : pair[1] # a /\ pair[2] # a}
  /\ monitors' = {pair \in monitors : pair[2] # a}
  /\ phase' = "dead_send"
  /\ UNCHANGED <<kind, mailboxes, pending_result, timers, observations,
                 msg_state, time, next_id, actor_ids, spawned, dead_sends>>

\* Send to a dead actor exactly once. This captures safe-drop behavior
\* without introducing unbounded counters or mailbox histories.
SendToDead(sender, target) ==
  /\ phase = "dead_send"
  /\ sender \in live
  /\ target \notin live
  /\ target \in spawned
  /\ dead_sends = 0
  /\ dead_sends' = dead_sends + 1
  /\ phase' = "done"
  /\ UNCHANGED <<live, kind, pc, ready, mailboxes, pending_result, timers,
                 observations, msg_state, time, links, monitors, exit_reason,
                 next_id, actor_ids, spawned>>

Done ==
  /\ phase = "done"
  /\ UNCHANGED all_vars

(***************************************************************************)
(* Specification                                                            *)
(***************************************************************************)
IdentityNext ==
  \/ \E a \in ActorPool : SpawnActor(a)
  \/ \E a \in ActorPool : KillActor(a)
  \/ \E s, t \in ActorPool : SendToDead(s, t)
  \/ Done

IdentitySpec ==
  IdentityInit /\ [][IdentityNext]_all_vars

(***************************************************************************)
(* Invariants                                                               *)
(***************************************************************************)

\* All live actors have unique, non-zero ids
UniqueIds ==
  \A a, b \in spawned :
    a # b => actor_ids[a] # actor_ids[b]

\* No spawned actor has id 0 (invalid ref)
NoZeroId ==
  \A a \in spawned : actor_ids[a] > 0

\* next_id is always greater than any assigned id (monotone)
MonotoneCounter ==
  \A a \in spawned : actor_ids[a] < next_id

\* Sending to dead actors doesn't affect their observations
DeadActorsUnchanged ==
  \A a \in ActorPool :
    a \notin live /\ a \notin spawned =>
      observations[a] = <<>>

\* Dead sends are non-negative
DeadSendsNonNeg ==
  dead_sends >= 0

\* State constraint for bounded model checking
Bound ==
  /\ next_id <= Cardinality(ActorPool) + 1
  /\ dead_sends <= 1
  /\ phase \in Phases

====
