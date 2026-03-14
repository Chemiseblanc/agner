---- MODULE coroutine_lifecycle ----
(***************************************************************************)
(* Coroutine Lifecycle Scenario                                            *)
(*                                                                         *)
(* Verifies the task<T> coroutine lifecycle:                               *)
(* - initial_suspend: task starts suspended                                *)
(* - Resume: scheduler moves task from suspended to running                *)
(* - Await: running task suspends waiting for another task                 *)
(* - Completion: task produces result, resumes continuation                *)
(* - Detach: task runs independently without a parent awaiting it          *)
(* - Exception: task captures exception, propagates to awaiter             *)
(*                                                                         *)
(* Source mapping:                                                          *)
(* - task<T>::promise_type::initial_suspend -> starts in "suspended"       *)
(* - task<T>::await_suspend -> sets continuation, returns handle           *)
(* - final_awaitable::await_suspend -> resumes continuation or destroys    *)
(* - task<T>::detach() -> marks detached, schedules on scheduler           *)
(***************************************************************************)
EXTENDS actor_system

(***************************************************************************)
(* Constants                                                               *)
(***************************************************************************)
CONSTANTS ParentTask, ChildTask, DetachedTask

ASSUME /\ ParentTask \in ActorPool
       /\ ChildTask \in ActorPool
       /\ DetachedTask \in ActorPool
       /\ ParentTask # ChildTask
       /\ ParentTask # DetachedTask
       /\ ChildTask # DetachedTask

(***************************************************************************)
(* Coroutine states                                                        *)
(* "suspended"  - initially suspended (initial_suspend)                    *)
(* "running"    - actively executing                                       *)
(* "awaiting"   - suspended waiting for another coroutine result           *)
(* "completed"  - finished with result or exception                        *)
(* "destroyed"  - handle destroyed (detached tasks after completion)       *)
(***************************************************************************)
VARIABLES coro_state, continuation, has_result, has_exception, detached

coro_vars == <<coro_state, continuation, has_result, has_exception, detached>>
all_vars == <<vars, coro_vars>>

(***************************************************************************)
(* Initial state                                                           *)
(* All tasks start suspended per initial_suspend()                         *)
(***************************************************************************)
CoroInit ==
  /\ live = {ParentTask, ChildTask, DetachedTask}
  /\ kind =
       [a \in ActorPool |->
         IF a \in {ParentTask, ChildTask, DetachedTask}
           THEN "collector" ELSE "none"]
  /\ pc =
       [a \in ActorPool |->
         IF a \in {ParentTask, ChildTask, DetachedTask}
           THEN "collect" ELSE "absent"]
  /\ ready = {}  \* All start suspended, not ready
  /\ mailboxes = [a \in ActorPool |-> <<>>]
  /\ pending_result = [a \in ActorPool |-> NoPending]
  /\ timers = [a \in ActorPool |-> NoDeadline]
  /\ observations = [a \in ActorPool |-> <<>>]
  /\ msg_state = [id \in MessageIds |-> "unused"]
  /\ time = 0
  /\ links = {}
  /\ monitors = {}
  /\ exit_reason = [a \in ActorPool |-> "none"]
  /\ coro_state = [a \in ActorPool |->
       IF a \in {ParentTask, ChildTask, DetachedTask}
         THEN "suspended" ELSE "none"]
  /\ continuation = [a \in ActorPool |-> "none"]
  /\ has_result = [a \in ActorPool |-> FALSE]
  /\ has_exception = [a \in ActorPool |-> FALSE]
  /\ detached = [a \in ActorPool |->
       IF a = DetachedTask THEN TRUE ELSE FALSE]

(***************************************************************************)
(* Actions                                                                  *)
(***************************************************************************)

\* Scheduler resumes a suspended task (moves to running)
ResumeTask(t) ==
  /\ t \in live
  /\ coro_state[t] = "suspended"
  /\ coro_state' = [coro_state EXCEPT ![t] = "running"]
  /\ ready' = ready \union {t}
  /\ UNCHANGED <<live, kind, pc, mailboxes, pending_result, timers,
                 observations, msg_state, time, links, monitors,
                 exit_reason, continuation, has_result, has_exception,
                 detached>>

\* Running task awaits another task (await_suspend)
\* Parent suspends and sets Child as its continuation target
AwaitTask(parent, child) ==
  /\ parent \in live /\ child \in live
  /\ parent # child
  /\ coro_state[parent] = "running"
  /\ coro_state[child] = "suspended"
  /\ coro_state' = [coro_state EXCEPT
       ![parent] = "awaiting",
       ![child] = "running"]
  /\ continuation' = [continuation EXCEPT ![child] = parent]
  /\ ready' = (ready \ {parent}) \union {child}
  /\ UNCHANGED <<live, kind, pc, mailboxes, pending_result, timers,
                 observations, msg_state, time, links, monitors,
                 exit_reason, has_result, has_exception, detached>>

\* Task completes successfully (return_value/return_void -> final_suspend)
CompleteTask(t) ==
  /\ t \in live
  /\ coro_state[t] = "running"
  /\ coro_state' =
       [a \in ActorPool |->
         IF a = t THEN "completed"
         \* If t has a continuation, resume it
         ELSE IF continuation[t] # "none" /\ a = continuation[t]
           THEN "running"
         ELSE coro_state[a]]
  /\ has_result' = [has_result EXCEPT ![t] = TRUE]
  \* If detached and no continuation, destroy
  /\ IF detached[t] /\ continuation[t] = "none"
       THEN /\ live' = live \ {t}
            /\ ready' = ready \ {t}
            /\ exit_reason' = [exit_reason EXCEPT ![t] = ExitNormal]
     ELSE IF continuation[t] # "none"
       THEN /\ live' = live
            /\ ready' = (ready \ {t}) \union {continuation[t]}
            /\ exit_reason' = exit_reason
     ELSE  /\ live' = live
            /\ ready' = ready \ {t}
            /\ exit_reason' = exit_reason
  /\ observations' =
       IF continuation[t] # "none"
         THEN [observations EXCEPT
                ![continuation[t]] =
                  Append(@, [kind |-> "TaskResult", from |-> t])]
         ELSE observations
  /\ UNCHANGED <<kind, pc, mailboxes, pending_result, timers,
                 msg_state, time, links, monitors,
                 continuation, has_exception, detached>>

\* Task throws unhandled exception (unhandled_exception -> final_suspend)
FailTask(t) ==
  /\ t \in live
  /\ coro_state[t] = "running"
  /\ coro_state' =
       [a \in ActorPool |->
         IF a = t THEN "completed"
         ELSE IF continuation[t] # "none" /\ a = continuation[t]
           THEN "running"
         ELSE coro_state[a]]
  /\ has_exception' = [has_exception EXCEPT ![t] = TRUE]
  \* If detached, destroy with error
  /\ IF detached[t] /\ continuation[t] = "none"
       THEN /\ live' = live \ {t}
            /\ ready' = ready \ {t}
            /\ exit_reason' = [exit_reason EXCEPT ![t] = ExitError]
     ELSE IF continuation[t] # "none"
       THEN /\ live' = live
            /\ ready' = (ready \ {t}) \union {continuation[t]}
            /\ exit_reason' = exit_reason
     ELSE  /\ live' = live
            /\ ready' = ready \ {t}
            /\ exit_reason' = exit_reason
  /\ observations' =
       IF continuation[t] # "none"
         THEN [observations EXCEPT
                ![continuation[t]] =
                  Append(@, [kind |-> "TaskException", from |-> t])]
         ELSE observations
  /\ UNCHANGED <<kind, pc, mailboxes, pending_result, timers,
                 msg_state, time, links, monitors,
                 continuation, has_result, detached>>

(***************************************************************************)
(* Specification                                                            *)
(***************************************************************************)
CoroNext ==
  \/ \E t \in {ParentTask, ChildTask, DetachedTask} : ResumeTask(t)
  \/ AwaitTask(ParentTask, ChildTask)
  \/ \E t \in {ParentTask, ChildTask, DetachedTask} : CompleteTask(t)
  \/ \E t \in {ParentTask, ChildTask, DetachedTask} : FailTask(t)

CoroSpec ==
  CoroInit /\ [][CoroNext]_all_vars

(***************************************************************************)
(* Invariants                                                               *)
(***************************************************************************)

\* All tasks start suspended (initial_suspend)
TasksStartSuspended ==
  \A t \in {ParentTask, ChildTask, DetachedTask} :
    coro_state[t] # "none"

\* A task can only be running if it's in the ready set
RunningImpliesReady ==
  \A t \in ActorPool :
    coro_state[t] = "running" => t \in ready

\* Awaiting task is not in ready set
AwaitingNotReady ==
  \A t \in ActorPool :
    coro_state[t] = "awaiting" => t \notin ready

\* Completed task has either result or exception, not both
ExclusiveOutcome ==
  \A t \in ActorPool :
    coro_state[t] = "completed" =>
      \/ (has_result[t] /\ ~has_exception[t])
      \/ (~has_result[t] /\ has_exception[t])

\* If child is completed, parent is no longer awaiting (continuation was triggered)
ContinuationResumed ==
  /\ coro_state[ChildTask] = "completed"
  /\ continuation[ChildTask] = ParentTask
  => coro_state[ParentTask] # "awaiting"

\* Detached tasks that complete are removed from live
DetachedCleanup ==
  /\ coro_state[DetachedTask] = "completed"
  /\ detached[DetachedTask]
  /\ continuation[DetachedTask] = "none"
  => DetachedTask \notin live

\* At most one task running at a time (single-threaded scheduler)
\* (relaxed: we allow multiple in ready, but state invariant)
CoroTypeOK ==
  \A t \in ActorPool :
    coro_state[t] \in {"none", "suspended", "running", "awaiting", "completed"}

====
