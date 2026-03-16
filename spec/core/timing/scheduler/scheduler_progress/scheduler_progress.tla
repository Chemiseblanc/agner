---- MODULE scheduler_progress ----
(***************************************************************************)
(* Scheduler Progress Scenario                                             *)
(*                                                                         *)
(* Verifies implementation-agnostic scheduler obligations:                 *)
(* - Dispatch may choose any ready actor                                   *)
(* - Logical time may advance while other actors remain ready              *)
(* - Due timeouts may become ready without draining other runnable actors  *)
(* - Queue discipline is intentionally out of scope                        *)
(*                                                                         *)
(* Source mapping:                                                          *)
(* - SchedulerBase::run_actor() -> RunReadyActor(a) over ready actors      *)
(* - Scheduler::schedule_after() -> timers and TimeoutFire                 *)
(* - Scheduler clock progression -> AdvanceTime                            *)
(***************************************************************************)
EXTENDS actor_system

(***************************************************************************)
(* Constants                                                               *)
(***************************************************************************)
CONSTANTS Worker1, Worker2, TimeoutWorker

ASSUME /\ Worker1 \in ActorPool
       /\ Worker2 \in ActorPool
  /\ TimeoutWorker \in ActorPool
  /\ Worker1 # Worker2
  /\ Worker1 # TimeoutWorker
  /\ Worker2 # TimeoutWorker

(***************************************************************************)
(* State                                                                   *)
(***************************************************************************)
PhaseValues == {"choose_dispatch", "advance_with_ready", "fire_timeout", "done"}

VARIABLES phase, first_dispatch, saw_time_advance_with_ready,
     saw_timeout_ready_with_ready

sched_vars == <<phase, first_dispatch, saw_time_advance_with_ready,
      saw_timeout_ready_with_ready>>
all_vars == <<vars, sched_vars>>

Workers == {Worker1, Worker2}

(***************************************************************************)
(* Initial state                                                           *)
(***************************************************************************)
SchedInit ==
  /\ live = Workers \cup {TimeoutWorker}
  /\ kind =
       [a \in ActorPool |->
         IF a \in Workers THEN "collector"
         ELSE IF a = TimeoutWorker THEN "timeout"
         ELSE "none"]
  /\ pc =
       [a \in ActorPool |->
         IF a \in Workers THEN "collect"
         ELSE IF a = TimeoutWorker THEN "try"
         ELSE "absent"]
  /\ ready = Workers
  /\ mailboxes = [a \in ActorPool |-> <<>>]
  /\ pending_result = [a \in ActorPool |-> NoPending]
  /\ timers =
       [a \in ActorPool |->
         IF a = TimeoutWorker THEN TimeoutDelay ELSE NoDeadline]
  /\ observations = [a \in ActorPool |-> <<>>]
  /\ msg_state = [id \in MessageIds |-> "unused"]
  /\ time = 0
  /\ links = {}
  /\ monitors = {}
  /\ exit_reason = [a \in ActorPool |-> "none"]
  /\ phase = "choose_dispatch"
  /\ first_dispatch = "none"
  /\ saw_time_advance_with_ready = FALSE
  /\ saw_timeout_ready_with_ready = FALSE

(***************************************************************************)
(* Actions                                                                  *)
(***************************************************************************)

DispatchReady(w) ==
  /\ phase = "choose_dispatch"
  /\ w \in Workers
  /\ RunReadyActor(w)
  /\ phase' = "advance_with_ready"
  /\ first_dispatch' = w
  /\ saw_time_advance_with_ready' = saw_time_advance_with_ready
  /\ saw_timeout_ready_with_ready' = saw_timeout_ready_with_ready

AdvanceWithReady ==
  /\ phase = "advance_with_ready"
  /\ ready # {}
  /\ AdvanceTime
  /\ phase' = "fire_timeout"
  /\ UNCHANGED first_dispatch
  /\ saw_time_advance_with_ready' = TRUE
  /\ saw_timeout_ready_with_ready' = saw_timeout_ready_with_ready

FireTimeoutWithCompetition ==
  /\ phase = "fire_timeout"
  /\ ready # {}
  /\ TimeoutFire(TimeoutWorker)
  /\ phase' = "done"
  /\ UNCHANGED <<first_dispatch, saw_time_advance_with_ready>>
  /\ saw_timeout_ready_with_ready' = TRUE

Done ==
  /\ phase = "done"
  /\ UNCHANGED all_vars

(***************************************************************************)
(* Specification                                                            *)
(***************************************************************************)
SchedNext ==
  \/ \E w \in Workers : DispatchReady(w)
  \/ AdvanceWithReady
  \/ FireTimeoutWithCompetition
  \/ Done

ProgressStep ==
  /\ phase # "done"
  /\ SchedNext

SchedSpec ==
  SchedInit /\ [][SchedNext]_all_vars /\ WF_all_vars(ProgressStep)

(***************************************************************************)
(* Invariants                                                               *)
(***************************************************************************)

SchedulerWitnessTypeOK ==
  /\ phase \in PhaseValues
  /\ first_dispatch \in Workers \cup {"none"}
  /\ saw_time_advance_with_ready \in BOOLEAN
  /\ saw_timeout_ready_with_ready \in BOOLEAN

ReadyActorsRemainLive ==
  ready \subseteq live

RecordedDispatchIsReadyWorker ==
  first_dispatch = "none" \/ first_dispatch \in Workers

TimeAdvanceWitnessConsistent ==
  saw_time_advance_with_ready /\ ~saw_timeout_ready_with_ready =>
    /\ time = TimeoutDelay
    /\ ready # {}
    /\ TimeoutWorker \notin ready

TimeoutCompetitionWitnessConsistent ==
  saw_timeout_ready_with_ready =>
    /\ time = TimeoutDelay
    /\ TimeoutWorker \in ready
    /\ \E w \in Workers : w \in ready

EventuallyAdvanceWithReady ==
  <>(saw_time_advance_with_ready)

EventuallyTimeoutReadyWithCompetition ==
  <>(saw_timeout_ready_with_ready)

====
