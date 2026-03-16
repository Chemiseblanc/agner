---- MODULE identity_coroutine_core_projection ----
(* ************************************************************************* *)
(* Identity + Coroutine Core Projection                                   *)
(*                                                                         *)
(* Composes identity allocation and coroutine lifecycle into one          *)
(* projected core-facing state view.                                      *)
(*                                                                         *)
(* This is still not a full refinement of actor_system.tla. It only       *)
(* checks a bounded coarse projection record containing:                  *)
(* - abstract liveness                                                    *)
(* - abstract pc shape                                                    *)
(* - abstract readiness                                                   *)
(* - abstract exit reasons                                                *)
(* - valid root references and stale targets                              *)
(* while continuing to hide the awaited child from the abstract boundary. *)
(* ************************************************************************* *)
EXTENDS actor_defs, TLC

CONSTANTS ParentTask, ChildTask, DetachedTask

ASSUME /\ ParentTask \in ActorPool
  /\ ChildTask \in ActorPool
  /\ DetachedTask \in ActorPool
  /\ Cardinality({ParentTask, ChildTask, DetachedTask}) = 3

ScenarioTasks == {ParentTask, ChildTask, DetachedTask}
RootTasks == {ParentTask, DetachedTask}
InternalTasks == {ChildTask}

ImplStates == {"none", "suspended", "running", "awaiting", "completed"}
ProjectionPhases == {"spawning", "lifecycle", "retiring", "dead_send", "done"}
ProjectedPcStates == {"absent", "collect", "done"}
NoContinuation == "none"

VARIABLES next_id, actor_ids, spawned, retired, dead_sends,
          impl_live, coro_state, continuation,
          has_result, has_exception, detached, phase

bridge_vars ==
  <<next_id, actor_ids, spawned, retired, dead_sends,
    impl_live, coro_state, continuation,
    has_result, has_exception, detached, phase>>

ProjectedValid(a) == actor_ids[a] > 0

ProjectedPc(a) ==
  CASE a \notin RootTasks -> "absent"
    [] a \notin spawned -> "absent"
    [] a \in retired -> "done"
    [] coro_state[a] = "completed" -> "done"
    [] a \in impl_live -> "collect"
    [] OTHER -> "absent"

ProjectedLive == {a \in RootTasks : ProjectedPc(a) = "collect"}

ProjectedReady ==
  {a \in RootTasks : a \in ProjectedLive /\ coro_state[a] = "running"}

ProjectedExitReason ==
  [a \in RootTasks |->
    CASE coro_state[a] = "completed" /\ has_exception[a] -> ExitError
      [] coro_state[a] = "completed" /\ has_result[a] -> ExitNormal
      [] a \in retired -> ExitNormal
      [] OTHER -> "none"]

ProjectedStaleTargets == {a \in RootTasks : ProjectedValid(a) /\ a \in retired}

ProjectedCoreState ==
  [live |-> ProjectedLive,
   pc |-> [a \in RootTasks |-> ProjectedPc(a)],
   ready |-> ProjectedReady,
   exit_reason |-> ProjectedExitReason,
   valid_refs |-> {a \in RootTasks : ProjectedValid(a)},
   stale_targets |-> ProjectedStaleTargets]

RootsLifecycleFinished(next_coro_state) ==
  \A a \in RootTasks : next_coro_state[a] = "completed"

CompletedState(t) ==
  [a \in ActorPool |->
    IF a = t THEN "completed"
    ELSE IF continuation[t] # NoContinuation /\ a = continuation[t]
      THEN "running"
    ELSE coro_state[a]]

ProjectionInit ==
  /\ next_id = 1
  /\ actor_ids = [a \in ActorPool |-> 0]
  /\ spawned = {}
  /\ retired = {}
  /\ dead_sends = 0
  /\ impl_live = {}
  /\ coro_state = [a \in ActorPool |-> "none"]
  /\ continuation = [a \in ActorPool |-> NoContinuation]
  /\ has_result = [a \in ActorPool |-> FALSE]
  /\ has_exception = [a \in ActorPool |-> FALSE]
  /\ detached =
       [a \in ActorPool |->
         IF a = DetachedTask THEN TRUE ELSE FALSE]
  /\ phase = "spawning"

SpawnActor(a) ==
  /\ phase = "spawning"
  /\ a \in ScenarioTasks
  /\ a \notin spawned
  /\ next_id' = next_id + 1
  /\ actor_ids' = [actor_ids EXCEPT ![a] = next_id]
  /\ spawned' = spawned \union {a}
  /\ retired' = retired
  /\ dead_sends' = dead_sends
  /\ impl_live' = impl_live \union {a}
  /\ coro_state' = [coro_state EXCEPT ![a] = "suspended"]
  /\ continuation' = continuation
  /\ has_result' = has_result
  /\ has_exception' = has_exception
  /\ detached' = detached
  /\ phase' =
       IF spawned \union {a} = ScenarioTasks
         THEN "lifecycle"
         ELSE "spawning"

ResumeTask(t) ==
  /\ phase = "lifecycle"
  /\ t \in ScenarioTasks \ retired
  /\ coro_state[t] = "suspended"
  /\ coro_state' = [coro_state EXCEPT ![t] = "running"]
  /\ UNCHANGED <<next_id, actor_ids, spawned, retired, dead_sends,
                 impl_live, continuation, has_result,
                 has_exception, detached, phase>>

AwaitTask(parent, child) ==
  /\ phase = "lifecycle"
  /\ parent \in impl_live
  /\ child \in impl_live
  /\ parent # child
  /\ coro_state[parent] = "running"
  /\ coro_state[child] = "suspended"
  /\ coro_state' = [coro_state EXCEPT ![parent] = "awaiting", ![child] = "running"]
  /\ continuation' = [continuation EXCEPT ![child] = parent]
  /\ UNCHANGED <<next_id, actor_ids, spawned, retired, dead_sends,
                 impl_live, has_result, has_exception, detached, phase>>

CompleteTask(t) ==
  /\ phase = "lifecycle"
  /\ t \in ScenarioTasks \ retired
  /\ coro_state[t] = "running"
  /\ next_id' = next_id
  /\ actor_ids' = actor_ids
  /\ spawned' = spawned
  /\ retired' = retired
  /\ dead_sends' = dead_sends
  /\ impl_live' =
       IF detached[t] /\ continuation[t] = NoContinuation
         THEN impl_live \ {t}
         ELSE impl_live
  /\ coro_state' = CompletedState(t)
  /\ continuation' = continuation
  /\ has_result' = [has_result EXCEPT ![t] = TRUE]
  /\ has_exception' = has_exception
  /\ detached' = detached
  /\ phase' =
       IF RootsLifecycleFinished(CompletedState(t))
         THEN "retiring"
         ELSE "lifecycle"

FailTask(t) ==
  /\ phase = "lifecycle"
  /\ t \in ScenarioTasks \ retired
  /\ coro_state[t] = "running"
  /\ next_id' = next_id
  /\ actor_ids' = actor_ids
  /\ spawned' = spawned
  /\ retired' = retired
  /\ dead_sends' = dead_sends
  /\ impl_live' =
       IF detached[t] /\ continuation[t] = NoContinuation
         THEN impl_live \ {t}
         ELSE impl_live
  /\ coro_state' = CompletedState(t)
  /\ continuation' = continuation
  /\ has_result' = has_result
  /\ has_exception' = [has_exception EXCEPT ![t] = TRUE]
  /\ detached' = detached
  /\ phase' =
       IF RootsLifecycleFinished(CompletedState(t))
         THEN "retiring"
         ELSE "lifecycle"

RetireActor(a) ==
  /\ phase = "retiring"
  /\ a \in RootTasks
  /\ a \in spawned
  /\ a \notin retired
  /\ retired' = retired \union {a}
  /\ impl_live' = impl_live \ {a}
  /\ phase' = "dead_send"
  /\ UNCHANGED <<next_id, actor_ids, spawned, dead_sends, coro_state,
                 continuation, has_result, has_exception, detached>>

SendToStale(target) ==
  /\ phase = "dead_send"
  /\ target \in retired
  /\ dead_sends = 0
  /\ dead_sends' = 1
  /\ phase' = "done"
  /\ UNCHANGED <<next_id, actor_ids, spawned, retired,
                 impl_live, coro_state, continuation,
                 has_result, has_exception, detached>>

Done ==
  /\ phase = "done"
  /\ UNCHANGED bridge_vars

ProjectionNext ==
  \/ \E a \in ScenarioTasks : SpawnActor(a)
  \/ \E t \in ScenarioTasks : ResumeTask(t)
  \/ AwaitTask(ParentTask, ChildTask)
  \/ \E t \in ScenarioTasks : CompleteTask(t)
  \/ \E t \in ScenarioTasks : FailTask(t)
  \/ \E a \in RootTasks : RetireActor(a)
  \/ \E a \in RootTasks : SendToStale(a)
  \/ Done

ProjectionSpec ==
  ProjectionInit /\ [][ProjectionNext]_bridge_vars

ImplTypeOK ==
  /\ next_id \in Nat
  /\ actor_ids \in [ActorPool -> Nat]
  /\ spawned \subseteq ScenarioTasks
  /\ retired \subseteq spawned
  /\ dead_sends \in 0..1
  /\ impl_live \subseteq spawned
  /\ coro_state \in [ActorPool -> ImplStates]
  /\ continuation \in [ActorPool -> (ActorPool \cup {NoContinuation})]
  /\ has_result \in [ActorPool -> BOOLEAN]
  /\ has_exception \in [ActorPool -> BOOLEAN]
  /\ detached \in [ActorPool -> BOOLEAN]
  /\ phase \in ProjectionPhases

ProjectionTypeOK ==
  /\ ProjectedCoreState.live \subseteq RootTasks
  /\ ProjectedCoreState.ready \subseteq RootTasks
  /\ \A a \in RootTasks : ProjectedCoreState.pc[a] \in ProjectedPcStates
  /\ \A a \in RootTasks :
       ProjectedCoreState.exit_reason[a] \in ExitReasons \cup {"none"}
  /\ ProjectedCoreState.valid_refs \subseteq RootTasks
  /\ ProjectedCoreState.stale_targets \subseteq RootTasks

ProjectedReadyActorsLive ==
  ProjectedCoreState.ready \subseteq ProjectedCoreState.live

RootsWithProjectedPcHaveValidRefs ==
  \A a \in RootTasks :
    ProjectedCoreState.pc[a] # "absent" => a \in ProjectedCoreState.valid_refs

InternalChildHiddenFromCoreView ==
  /\ ChildTask \in InternalTasks
  /\ ChildTask \notin RootTasks
  /\ ChildTask \notin ProjectedCoreState.live
  /\ ChildTask \notin ProjectedCoreState.ready

AwaitProjectsBlockedParent ==
  coro_state[ParentTask] = "awaiting" =>
    /\ ProjectedCoreState.pc[ParentTask] = "collect"
    /\ ParentTask \notin ProjectedCoreState.ready

SuccessfulCompletedRootsProjectNormal ==
  \A a \in RootTasks :
    (coro_state[a] = "completed" /\ has_result[a]) =>
      ProjectedCoreState.exit_reason[a] = ExitNormal

ExceptionalCompletedRootsProjectError ==
  \A a \in RootTasks :
    (coro_state[a] = "completed" /\ has_exception[a]) =>
      ProjectedCoreState.exit_reason[a] = ExitError

RetiredRootsProjectDoneAndStale ==
  \A a \in retired \cap RootTasks :
    /\ ProjectedCoreState.pc[a] = "done"
    /\ a \in ProjectedCoreState.stale_targets
    /\ a \notin ProjectedCoreState.live

DistinctSpawnedActorsProjectDistinctRefs ==
  \A a, b \in spawned :
    a # b => actor_ids[a] # actor_ids[b]

MonotoneIdentityCounterProjectsFreshness ==
  \A a \in spawned : actor_ids[a] < next_id

StaleSendLeavesProjectedViewStable ==
  phase = "done" =>
    /\ dead_sends = 1
    /\ ProjectedCoreState.stale_targets = retired \cap RootTasks

=============================================================================