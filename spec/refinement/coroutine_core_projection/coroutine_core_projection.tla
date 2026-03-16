---- MODULE coroutine_core_projection ----
(* ************************************************************************* *)
(* Coroutine Core Projection                                               *)
(*                                                                         *)
(* Establishes a first refinement bridge from implementation-oriented      *)
(* coroutine lifecycle state to a coarse scheduler-visible core view.      *)
(*                                                                         *)
(* This is intentionally not a full refinement of actor_system.tla.        *)
(* It proves that root tasks admit a stable projection with:               *)
(* - abstract liveness                                                     *)
(* - abstract readiness                                                    *)
(* - abstract completion / exit reason                                     *)
(* while hiding internal awaited child tasks from the projected boundary.  *)
(* ************************************************************************* *)
EXTENDS actor_defs, TLC

CONSTANTS ParentTask, ChildTask, DetachedTask

ASSUME /\ ParentTask \in ActorPool
       /\ ChildTask \in ActorPool
       /\ DetachedTask \in ActorPool
       /\ Cardinality({ParentTask, ChildTask, DetachedTask}) = 3

ImplStates == {"none", "suspended", "running", "awaiting", "completed"}
ProjectedPcStates == {"absent", "collect", "done"}
NoContinuation == "none"

RootTasks == {ParentTask, DetachedTask}
InternalTasks == {ChildTask}

VARIABLES impl_live, coro_state, continuation,
          has_result, has_exception, detached

impl_vars == <<impl_live, coro_state, continuation,
               has_result, has_exception, detached>>

(* ************************************************************************* *)
(* Projection operators                                                    *)
(* ************************************************************************* *)
ProjectedPc(t) ==
  CASE t \notin RootTasks -> "absent"
    [] coro_state[t] = "completed" -> "done"
    [] t \in impl_live -> "collect"
    [] OTHER -> "absent"

ProjectedLive == {t \in RootTasks : ProjectedPc(t) = "collect"}

ProjectedReady == {t \in RootTasks : coro_state[t] = "running"}

ProjectedExitReason ==
  [t \in RootTasks |->
    CASE coro_state[t] = "completed" /\ has_exception[t] -> ExitError
      [] coro_state[t] = "completed" /\ has_result[t] -> ExitNormal
      [] OTHER -> "none"]

(* ************************************************************************* *)
(* Initial state                                                           *)
(* ************************************************************************* *)
ProjectionInit ==
  /\ impl_live = {ParentTask, ChildTask, DetachedTask}
  /\ coro_state =
       [a \in ActorPool |->
         IF a \in {ParentTask, ChildTask, DetachedTask}
           THEN "suspended" ELSE "none"]
  /\ continuation = [a \in ActorPool |-> NoContinuation]
  /\ has_result = [a \in ActorPool |-> FALSE]
  /\ has_exception = [a \in ActorPool |-> FALSE]
  /\ detached =
       [a \in ActorPool |->
         IF a = DetachedTask THEN TRUE
         ELSE IF a \in {ParentTask, ChildTask} THEN FALSE
         ELSE FALSE]

(* ************************************************************************* *)
(* Implementation-style lifecycle actions                                  *)
(* ************************************************************************* *)
ResumeTask(t) ==
  /\ t \in impl_live
  /\ coro_state[t] = "suspended"
  /\ coro_state' = [coro_state EXCEPT ![t] = "running"]
  /\ UNCHANGED <<impl_live, continuation, has_result, has_exception, detached>>

AwaitTask(parent, child) ==
  /\ parent \in impl_live
  /\ child \in impl_live
  /\ parent # child
  /\ coro_state[parent] = "running"
  /\ coro_state[child] = "suspended"
  /\ coro_state' = [coro_state EXCEPT ![parent] = "awaiting", ![child] = "running"]
  /\ continuation' = [continuation EXCEPT ![child] = parent]
  /\ UNCHANGED <<impl_live, has_result, has_exception, detached>>

CompleteTask(t) ==
  /\ t \in impl_live
  /\ coro_state[t] = "running"
  /\ coro_state' =
       [a \in ActorPool |->
         IF a = t THEN "completed"
         ELSE IF continuation[t] # NoContinuation /\ a = continuation[t]
           THEN "running"
         ELSE coro_state[a]]
  /\ has_result' = [has_result EXCEPT ![t] = TRUE]
  /\ IF detached[t] /\ continuation[t] = NoContinuation
       THEN impl_live' = impl_live \ {t}
       ELSE impl_live' = impl_live
  /\ UNCHANGED <<continuation, has_exception, detached>>

FailTask(t) ==
  /\ t \in impl_live
  /\ coro_state[t] = "running"
  /\ coro_state' =
       [a \in ActorPool |->
         IF a = t THEN "completed"
         ELSE IF continuation[t] # NoContinuation /\ a = continuation[t]
           THEN "running"
         ELSE coro_state[a]]
  /\ has_exception' = [has_exception EXCEPT ![t] = TRUE]
  /\ IF detached[t] /\ continuation[t] = NoContinuation
       THEN impl_live' = impl_live \ {t}
       ELSE impl_live' = impl_live
  /\ UNCHANGED <<continuation, has_result, detached>>

ProjectionNext ==
  \/ \E t \in {ParentTask, ChildTask, DetachedTask} : ResumeTask(t)
  \/ AwaitTask(ParentTask, ChildTask)
  \/ \E t \in {ParentTask, ChildTask, DetachedTask} : CompleteTask(t)
  \/ \E t \in {ParentTask, ChildTask, DetachedTask} : FailTask(t)

ProjectionSpec ==
  ProjectionInit /\ [][ProjectionNext]_impl_vars

(* ************************************************************************* *)
(* Invariants over the implementation side                                 *)
(* ************************************************************************* *)
ImplTypeOK ==
  /\ impl_live \subseteq ActorPool
  /\ coro_state \in [ActorPool -> ImplStates]
  /\ continuation \in [ActorPool -> (ActorPool \cup {NoContinuation})]
  /\ has_result \in [ActorPool -> BOOLEAN]
  /\ has_exception \in [ActorPool -> BOOLEAN]
  /\ detached \in [ActorPool -> BOOLEAN]

(* ************************************************************************* *)
(* Invariants over the projected core-facing view                          *)
(* ************************************************************************* *)
ProjectionTypeOK ==
  /\ RootTasks \subseteq ActorPool
  /\ InternalTasks \subseteq ActorPool
  /\ \A t \in RootTasks : ProjectedPc(t) \in ProjectedPcStates
  /\ ProjectedLive \subseteq RootTasks
  /\ ProjectedReady \subseteq RootTasks
  /\ \A t \in RootTasks : ProjectedExitReason[t] \in ExitReasons \cup {"none"}

ProjectedReadyActorsLive ==
  ProjectedReady \subseteq ProjectedLive

InternalChildHiddenFromProjection ==
  /\ ChildTask \in InternalTasks
  /\ ChildTask \notin RootTasks
  /\ ChildTask \notin ProjectedLive
  /\ ChildTask \notin ProjectedReady

AwaitProjectsBlockedParent ==
  coro_state[ParentTask] = "awaiting" =>
    /\ ProjectedPc(ParentTask) = "collect"
    /\ ParentTask \notin ProjectedReady

DetachedCompletionProjectsDone ==
  coro_state[DetachedTask] = "completed" =>
    /\ ProjectedPc(DetachedTask) = "done"
    /\ DetachedTask \notin ProjectedLive

ChildCompletionResumesParent ==
  /\ continuation[ChildTask] = ParentTask
  /\ coro_state[ChildTask] = "completed"
  => coro_state[ParentTask] # "awaiting"

SuccessfulRootCompletionProjectsNormal ==
  \A t \in RootTasks :
    (coro_state[t] = "completed" /\ has_result[t]) =>
      ProjectedExitReason[t] = ExitNormal

ExceptionalRootCompletionProjectsError ==
  \A t \in RootTasks :
    (coro_state[t] = "completed" /\ has_exception[t]) =>
      ProjectedExitReason[t] = ExitError

=============================================================================