---- MODULE actor_identity_core_projection ----
(* ************************************************************************* *)
(* Actor Identity Core Projection                                          *)
(*                                                                         *)
(* Establishes a partial refinement bridge from implementation-oriented    *)
(* actor identity allocation state to a coarse core-facing validity view.  *)
(*                                                                         *)
(* The projected boundary only tracks whether a root actor reference is    *)
(* valid, live, and stale-send safe. It does not attempt to encode the     *)
(* full shared actor_system state.                                          *)
(* ************************************************************************* *)
EXTENDS actor_defs, TLC

VARIABLES next_id, actor_ids, spawned, retired, dead_sends, phase

IdentityPhases == {"spawning", "retiring", "dead_send", "done"}

id_vars == <<next_id, actor_ids, spawned, retired, dead_sends, phase>>

ProjectedValid(a) == actor_ids[a] > 0

ProjectedLive == spawned \ retired

ProjectedAbsent == ActorPool \ spawned

ProjectedRetired == retired

ProjectedStaleTargets == {a \in ActorPool : ProjectedValid(a) /\ a \in retired}

ProjectionInit ==
  /\ next_id = 1
  /\ actor_ids = [a \in ActorPool |-> 0]
  /\ spawned = {}
  /\ retired = {}
  /\ dead_sends = 0
  /\ phase = "spawning"

SpawnActor(a) ==
  /\ phase = "spawning"
  /\ a \in ActorPool
  /\ a \notin spawned
  /\ actor_ids' = [actor_ids EXCEPT ![a] = next_id]
  /\ next_id' = next_id + 1
  /\ spawned' = spawned \union {a}
  /\ retired' = retired
  /\ dead_sends' = dead_sends
  /\ phase' = IF spawned \union {a} = ActorPool THEN "retiring" ELSE "spawning"

RetireActor(a) ==
  /\ phase = "retiring"
  /\ a \in spawned
  /\ a \notin retired
  /\ retired' = retired \union {a}
  /\ phase' = "dead_send"
  /\ UNCHANGED <<next_id, actor_ids, spawned, dead_sends>>

SendToStale(target) ==
  /\ phase = "dead_send"
  /\ target \in spawned
  /\ target \in retired
  /\ dead_sends = 0
  /\ dead_sends' = 1
  /\ phase' = "done"
  /\ UNCHANGED <<next_id, actor_ids, spawned, retired>>

Done ==
  /\ phase = "done"
  /\ UNCHANGED id_vars

ProjectionNext ==
  \/ \E a \in ActorPool : SpawnActor(a)
  \/ \E a \in ActorPool : RetireActor(a)
  \/ \E a \in ActorPool : SendToStale(a)
  \/ Done

ProjectionSpec ==
  ProjectionInit /\ [][ProjectionNext]_id_vars

ImplTypeOK ==
  /\ next_id \in Nat
  /\ actor_ids \in [ActorPool -> Nat]
  /\ spawned \subseteq ActorPool
  /\ retired \subseteq spawned
  /\ dead_sends \in 0..1
  /\ phase \in IdentityPhases

ProjectionTypeOK ==
  /\ ProjectedLive \subseteq ActorPool
  /\ ProjectedAbsent \subseteq ActorPool
  /\ ProjectedRetired \subseteq ActorPool
  /\ ProjectedStaleTargets \subseteq ActorPool
  /\ ProjectedLive \intersect ProjectedRetired = {}
  /\ ProjectedLive \intersect ProjectedAbsent = {}

NonZeroIdsProjectValidRefs ==
  \A a \in spawned : ProjectedValid(a)

UniqueSpawnedIdsProjectDistinctRefs ==
  \A a, b \in spawned :
    a # b => actor_ids[a] # actor_ids[b]

RetiredActorsNoLongerProjectLive ==
  \A a \in retired : a \notin ProjectedLive

UnspawnedActorsProjectAbsent ==
  \A a \in ActorPool :
    a \notin spawned =>
      /\ a \in ProjectedAbsent
      /\ ~ProjectedValid(a)

StaleSendLeavesProjectionUnchanged ==
  phase = "done" =>
    /\ dead_sends = 1
    /\ ProjectedStaleTargets = retired

MonotoneIdentityCounterProjectsFreshness ==
  \A a \in spawned : actor_ids[a] < next_id

=============================================================================