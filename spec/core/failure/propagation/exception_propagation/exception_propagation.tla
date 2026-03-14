---- MODULE exception_propagation ----
(***************************************************************************)
(* Exception Propagation Scenario                                          *)
(*                                                                         *)
(* Verifies that unhandled exceptions in actor tasks propagate correctly:   *)
(* - An exception in an actor sets exit_reason to ExitError                *)
(* - Linked actors receive ExitSignal with ExitError reason                *)
(* - Monitoring actors receive DownSignal with ExitError reason            *)
(* - Normal completion sets exit_reason to ExitNormal                      *)
(* - Links are cleaned up after exit                                       *)
(*                                                                         *)
(* Source mapping:                                                          *)
(* - task<T>::promise_type::unhandled_exception() -> ExitActor w/ ExitError*)
(* - SchedulerBase::run_actor catch(...) -> ExitActor w/ ExitError         *)
(* - SchedulerBase::notify_exit() -> NotifyExit action                     *)
(***************************************************************************)
EXTENDS actor_system

(***************************************************************************)
(* Constants                                                               *)
(***************************************************************************)
CONSTANTS Parent, Child, Sibling

ASSUME /\ Parent \in ActorPool
       /\ Child \in ActorPool
       /\ Sibling \in ActorPool
       /\ Parent # Child /\ Parent # Sibling /\ Child # Sibling

(***************************************************************************)
(* State                                                                   *)
(***************************************************************************)
VARIABLES exit_propagated

ep_vars == <<exit_propagated>>
all_vars == <<vars, ep_vars>>

(***************************************************************************)
(* Initial state                                                           *)
(* Parent is linked to Child, Parent monitors Sibling                      *)
(***************************************************************************)
ExcPropInit ==
  /\ live = {Parent, Child, Sibling}
  /\ kind =
       [a \in ActorPool |->
         IF a \in {Parent, Child, Sibling} THEN "collector" ELSE "none"]
  /\ pc =
       [a \in ActorPool |->
         IF a \in {Parent, Child, Sibling} THEN "collect" ELSE "absent"]
  /\ ready = {Parent, Child, Sibling}
  /\ mailboxes = [a \in ActorPool |-> <<>>]
  /\ pending_result = [a \in ActorPool |-> NoPending]
  /\ timers = [a \in ActorPool |-> NoDeadline]
  /\ observations = [a \in ActorPool |-> <<>>]
  /\ msg_state = [id \in MessageIds |-> "unused"]
  /\ time = 0
  /\ links = {<<Parent, Child>>, <<Child, Parent>>}
  /\ monitors = {<<Sibling, Parent>>}
  /\ exit_reason = [a \in ActorPool |-> "none"]
  /\ exit_propagated = [a \in ActorPool |-> FALSE]

(***************************************************************************)
(* Actions                                                                  *)
(***************************************************************************)

\* An actor exits with a given reason and signals are propagated atomically.
\* This models: run_actor completes -> notify_exit() runs immediately.
\* In C++, notify_exit is called synchronously before actors_ cleanup.
ExitActor(a, reason) ==
  /\ a \in live
  /\ exit_propagated[a] = FALSE
  /\ LET linked == LinkedTo(a)
         watchers == MonitoredBy(a)
     IN
       /\ live' = live \ {a}
       /\ pc' = [pc EXCEPT ![a] = "done"]
       /\ mailboxes' = [mailboxes EXCEPT ![a] = <<>>]
       \* Deliver ExitSignal to linked, DownSignal to watchers
       /\ observations' =
            [b \in ActorPool |->
              IF b \in (linked \intersect live) /\ b # a
                THEN Append(observations[b], ExitSignal(a, reason))
              ELSE IF b \in (watchers \intersect live) /\ b # a
                THEN Append(observations[b], DownSignal(a, reason))
              ELSE observations[b]]
       /\ exit_reason' = [exit_reason EXCEPT ![a] = reason]
       /\ links' = {pair \in links : pair[1] # a /\ pair[2] # a}
       /\ monitors' = {pair \in monitors : pair[2] # a}
       /\ exit_propagated' = [exit_propagated EXCEPT ![a] = TRUE]
  /\ UNCHANGED <<kind, ready, pending_result, timers, msg_state, time>>

(***************************************************************************)
(* Specification                                                            *)
(***************************************************************************)
ExcPropNext ==
  \* Any actor can exit normally
  \/ \E a \in {Parent, Child, Sibling} :
       ExitActor(a, ExitNormal)
  \* Any actor can exit with an exception (error)
  \/ \E a \in {Parent, Child, Sibling} :
       ExitActor(a, ExitError)

ExcPropSpec ==
  ExcPropInit /\ [][ExcPropNext]_all_vars

(***************************************************************************)
(* Invariants                                                               *)
(***************************************************************************)

\* Dead actors always have an exit reason
DeadHaveReason ==
  \A a \in ActorPool :
    a \notin live => exit_reason[a] \in {ExitNormal, ExitError, ExitStopped}

\* If Child dies and Parent is still alive, Parent (linked to Child)
\* must have received an ExitSignal with the correct reason
LinkedActorGetsExitSignal ==
  Child \notin live /\ exit_propagated[Child] /\ Parent \in live =>
    \E i \in 1..Len(observations[Parent]) :
      observations[Parent][i] = ExitSignal(Child, exit_reason[Child])

\* If Parent dies and Sibling is still alive, Sibling (monitoring Parent)
\* must have received a DownSignal with the correct reason
MonitorGetsDownSignal ==
  Parent \notin live /\ exit_propagated[Parent] /\ Sibling \in live =>
    \E i \in 1..Len(observations[Sibling]) :
      observations[Sibling][i] = DownSignal(Parent, exit_reason[Parent])

\* Error exits produce signals with ExitError reason
ErrorReasonPreserved ==
  \A a \in ActorPool :
    a \notin live /\ exit_reason[a] = ExitError =>
      \A b \in ActorPool :
        \A i \in 1..Len(observations[b]) :
          /\ observations[b][i].kind = "ExitSignal"
          /\ observations[b][i].from = a
          => observations[b][i].reason = ExitError

\* Normal exits produce signals with ExitNormal reason
NormalReasonPreserved ==
  \A a \in ActorPool :
    a \notin live /\ exit_reason[a] = ExitNormal =>
      \A b \in ActorPool :
        \A i \in 1..Len(observations[b]) :
          /\ observations[b][i].kind = "ExitSignal"
          /\ observations[b][i].from = a
          => observations[b][i].reason = ExitNormal

\* Links are bidirectional: cleaned up from both sides
LinksCleanedAfterExit ==
  \A a \in ActorPool :
    a \notin live /\ exit_propagated[a] =>
      /\ {l \in links : l[1] = a} = {}
      /\ {l \in links : l[2] = a} = {}

====
