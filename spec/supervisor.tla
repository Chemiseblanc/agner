---- MODULE supervisor ----
EXTENDS Naturals, Sequences, FiniteSets, TLC

(***************************************************************************)
(* Source mapping                                                           *)
(*                                                                         *)
(* This module models Agner's Supervisor restart strategies and shutdown.   *)
(*                                                                         *)
(* - Supervisor::handle_termination()       -> HandleTermination            *)
(* - Supervisor::should_restart()           -> ShouldRestart                *)
(* - Supervisor::register_restart()         -> RegisterRestart              *)
(* - Supervisor::respawn_with_entry()       -> Respawn (inside Handle*)     *)
(* - Supervisor::begin_restart_group()      -> BeginRestartGroup            *)
(*     (embedded in HandleTermination for group strategies)                 *)
(* - Supervisor::handle_restart_group_stop()-> HandleRestartGroupStop       *)
(* - Supervisor::finalize_restart_group()   -> FinalizeRestartGroup         *)
(* - Supervisor::stop() / stop_all_children -> SupervisorStop               *)
(* - register_restart() sliding window      -> PruneWindow / RegisterRestart*)
(*                                                                         *)
(* Deliberate abstractions:                                                 *)
(* - Children are symbolic ids; spawning/linking is atomic.                 *)
(* - Exit reasons are {normal, abnormal}; maps to ExitReason::Kind.        *)
(* - Time is a discrete counter; the sliding window uses integer ticks.    *)
(* - The scheduler picks which live child exits nondeterministically.      *)
(* - Group restart collects pending_stops then finalizes atomically.        *)
(***************************************************************************)

CONSTANTS
  ChildIds,            \* Set of child identifiers (e.g., {c1, c2, c3})
  StrategyConst,       \* "one_for_one" | "one_for_all" | "rest_for_one"
                       \*   | "simple_one_for_one"
  MaxRestarts,         \* Max restarts in sliding window (Nat)
  WithinWindow,        \* Sliding window size in ticks (Nat)
  ChildRestartPolicy,  \* Function: ChildIds -> {"permanent","transient","temporary"}
  ChildStartOrder      \* Function: ChildIds -> 1..Cardinality(ChildIds)

ASSUME /\ Cardinality(ChildIds) >= 1
       /\ StrategyConst \in {"one_for_one", "one_for_all",
                              "rest_for_one", "simple_one_for_one"}
       /\ MaxRestarts \in Nat
       /\ WithinWindow \in Nat
       /\ WithinWindow >= 1
       /\ ChildRestartPolicy \in [ChildIds -> {"permanent", "transient", "temporary"}]
       /\ ChildStartOrder \in [ChildIds -> 1..Cardinality(ChildIds)]

(***************************************************************************)
(* Derived constants and helpers                                           *)
(***************************************************************************)

ExitReasons == {"normal", "abnormal"}

\* Maximum logical time bound for model checking.
MaxTime == (MaxRestarts + 2) * WithinWindow + Cardinality(ChildIds) + 2

\* Whether a child should be restarted given its policy and exit reason.
ShouldRestart(child, reason) ==
  LET policy == ChildRestartPolicy[child] IN
  CASE policy = "permanent" -> TRUE
    [] policy = "transient" -> reason # "normal"
    [] policy = "temporary" -> FALSE

\* Children started after `child` by start order (for rest_for_one).
LaterChildren(child) ==
  {c \in ChildIds : ChildStartOrder[c] > ChildStartOrder[child]}

(***************************************************************************)
(* Variables                                                                *)
(***************************************************************************)

VARIABLES
  sup_alive,        \* Boolean: supervisor is running
  stopping,         \* Boolean: supervisor is shutting down
  child_alive,      \* Function: ChildIds -> BOOLEAN
  suppress_restart, \* Function: ChildIds -> BOOLEAN
  restart_times,    \* Sequence of tick values (sliding window)
  restart_group,    \* Record: [active, failed, plan, pending_stops]
  time,             \* Current logical tick (Nat)
  observations      \* Sequence of observation records

vars == <<sup_alive, stopping, child_alive, suppress_restart,
          restart_times, restart_group, time, observations>>

NoGroup == [active |-> FALSE, failed |-> "none",
            plan |-> {}, pending_stops |-> {}]

(***************************************************************************)
(* Type invariant                                                           *)
(***************************************************************************)

TypeOK ==
  /\ sup_alive \in BOOLEAN
  /\ stopping \in BOOLEAN
  /\ child_alive \in [ChildIds -> BOOLEAN]
  /\ suppress_restart \in [ChildIds -> BOOLEAN]
  /\ restart_times \in Seq(0..MaxTime)
  /\ restart_group.active \in BOOLEAN
  /\ restart_group.plan \subseteq ChildIds
  /\ restart_group.pending_stops \subseteq ChildIds
  /\ time \in 0..MaxTime
  /\ observations \in Seq(
       [type : {"restart", "exit", "group_restart", "shutdown",
                "intensity_exceeded"},
        child : ChildIds \cup {"supervisor"},
        reason : ExitReasons \cup {"none"}])

(***************************************************************************)
(* Sliding window helpers                                                   *)
(* Prunes timestamps older than (now - WithinWindow) and appends `now`.    *)
(* Returns <<new_times, allowed>> where allowed is TRUE if under limit.    *)
(***************************************************************************)

PruneWindow(times, now) ==
  SelectSeq(times, LAMBDA t : now - t <= WithinWindow)

RegisterRestart(times, now) ==
  IF MaxRestarts = 0 THEN <<times, FALSE>>
  ELSE LET pruned == PruneWindow(times, now)
           updated == Append(pruned, now)
       IN IF Len(updated) > MaxRestarts
          THEN <<updated, FALSE>>
          ELSE <<updated, TRUE>>

(***************************************************************************)
(* Live children helpers                                                    *)
(***************************************************************************)

LiveChildren == {c \in ChildIds : child_alive[c]}

(***************************************************************************)
(* Initial state                                                            *)
(***************************************************************************)

Init ==
  /\ sup_alive = TRUE
  /\ stopping = FALSE
  /\ child_alive = [c \in ChildIds |-> TRUE]
  /\ suppress_restart = [c \in ChildIds |-> FALSE]
  /\ restart_times = <<>>
  /\ restart_group = NoGroup
  /\ time = 0
  /\ observations = <<>>

(***************************************************************************)
(* Action: HandleTermination                                               *)
(* A live child exits with some reason. Supervisor decides whether to      *)
(* restart it (one_for_one / simple_one_for_one) or initiate a group       *)
(* restart (one_for_all / rest_for_one).                                   *)
(* Maps to Supervisor::handle_termination().                               *)
(***************************************************************************)

HandleTermination(child, reason) ==
  \* Preconditions: supervisor alive, not stopping, child alive, no group
  /\ sup_alive
  /\ ~stopping
  /\ child_alive[child]
  /\ ~restart_group.active
  /\ LET will_restart == ~suppress_restart[child]
                          /\ ShouldRestart(child, reason)
     IN IF ~will_restart
        THEN \* Child exits, not restarted
             /\ child_alive' = [child_alive EXCEPT ![child] = FALSE]
             /\ observations' = Append(observations,
                  [type |-> "exit", child |-> child, reason |-> reason])
             /\ UNCHANGED <<sup_alive, stopping, suppress_restart,
                            restart_times, restart_group, time>>
        ELSE \* Check intensity before restarting
             LET result == RegisterRestart(restart_times, time)
                 new_times == result[1]
                 allowed == result[2]
             IN IF ~allowed
                THEN \* Intensity exceeded -> supervisor shuts down
                     /\ sup_alive' = FALSE
                     /\ stopping' = TRUE
                     /\ child_alive' = [c \in ChildIds |-> FALSE]
                     /\ suppress_restart' = [c \in ChildIds |-> TRUE]
                     /\ restart_times' = new_times
                     /\ restart_group' = NoGroup
                     /\ observations' = Append(Append(observations,
                          [type |-> "intensity_exceeded",
                           child |-> child, reason |-> reason]),
                          [type |-> "shutdown",
                           child |-> "supervisor", reason |-> "none"])
                     /\ UNCHANGED <<time>>
                ELSE \* Restart allowed
                     /\ restart_times' = new_times
                     /\ UNCHANGED <<time>>
                     /\ IF StrategyConst = "one_for_one"
                           \/ StrategyConst = "simple_one_for_one"
                        THEN \* Direct respawn: child dies and is reborn
                             /\ child_alive' =
                                  [child_alive EXCEPT ![child] = TRUE]
                             /\ observations' = Append(observations,
                                  [type |-> "restart", child |-> child,
                                   reason |-> reason])
                             /\ UNCHANGED <<sup_alive, stopping,
                                            suppress_restart, restart_group>>
                        ELSE \* Begin group restart (one_for_all/rest_for_one)
                             LET affected ==
                                   IF StrategyConst = "one_for_all"
                                   THEN LiveChildren \ {child}
                                   ELSE LaterChildren(child) \cap
                                        (LiveChildren \ {child})
                                 plan == {child} \cup affected
                             IN /\ child_alive' =
                                     [child_alive EXCEPT ![child] = FALSE]
                                /\ restart_group' =
                                     [active |-> TRUE,
                                      failed |-> child,
                                      plan |-> plan,
                                      pending_stops |-> affected]
                                /\ observations' = Append(observations,
                                     [type |-> "group_restart",
                                      child |-> child, reason |-> reason])
                                /\ UNCHANGED <<sup_alive, stopping,
                                              suppress_restart>>

(***************************************************************************)
(* Action: HandleRestartGroupStop                                          *)
(* A child in the restart group's pending_stops has stopped.               *)
(* Maps to Supervisor::handle_restart_group_stop().                        *)
(***************************************************************************)

HandleRestartGroupStop(child) ==
  /\ sup_alive
  /\ restart_group.active
  /\ child \in restart_group.pending_stops
  /\ child_alive[child]
  /\ child_alive' = [child_alive EXCEPT ![child] = FALSE]
  /\ restart_group' =
       [restart_group EXCEPT
         !.pending_stops = @ \ {child}]
  /\ observations' = Append(observations,
       [type |-> "exit", child |-> child, reason |-> "normal"])
  /\ UNCHANGED <<sup_alive, stopping, suppress_restart,
                  restart_times, time>>

(***************************************************************************)
(* Action: FinalizeRestartGroup                                            *)
(* All pending_stops drained; respawn children per their restart policy.   *)
(* All children in the plan were forcefully stopped (abnormal); restart    *)
(* policy is evaluated with reason "abnormal" for every child.            *)
(* Maps to Supervisor::finalize_restart_group().                           *)
(***************************************************************************)

FinalizeRestartGroup ==
  /\ sup_alive
  /\ restart_group.active
  /\ restart_group.pending_stops = {}
  /\ LET plan == restart_group.plan
     IN /\ child_alive' =
             [c \in ChildIds |->
               IF c \in plan
               THEN ShouldRestart(c, "abnormal")
               ELSE child_alive[c]]
        /\ restart_group' = NoGroup
        /\ UNCHANGED <<sup_alive, stopping, suppress_restart,
                        restart_times, time, observations>>

(***************************************************************************)
(* Action: SupervisorStop                                                  *)
(* Supervisor initiates shutdown. Sets stopping, suppress_restart on all,  *)
(* clears any active restart group. No restart churn during shutdown.      *)
(* Maps to Supervisor::stop().                                             *)
(***************************************************************************)

SupervisorStop ==
  /\ sup_alive
  /\ ~stopping
  /\ stopping' = TRUE
  /\ suppress_restart' = [c \in ChildIds |-> TRUE]
  /\ restart_group' = NoGroup
  /\ observations' = Append(observations,
       [type |-> "shutdown", child |-> "supervisor", reason |-> "none"])
  /\ UNCHANGED <<sup_alive, child_alive, restart_times, time>>

(***************************************************************************)
(* Action: ShutdownChildExit                                               *)
(* During shutdown, a child exits and is NOT restarted.                    *)
(* When the last child exits, the supervisor itself terminates.            *)
(***************************************************************************)

ShutdownChildExit(child) ==
  /\ sup_alive
  /\ stopping
  /\ child_alive[child]
  /\ child_alive' = [child_alive EXCEPT ![child] = FALSE]
  /\ observations' = Append(observations,
       [type |-> "exit", child |-> child, reason |-> "normal"])
  /\ IF LiveChildren \ {child} = {}
     THEN sup_alive' = FALSE
     ELSE sup_alive' = TRUE
  /\ UNCHANGED <<stopping, suppress_restart, restart_times,
                  restart_group, time>>

(***************************************************************************)
(* Action: AdvanceTime                                                     *)
(* Advance logical clock by one tick.                                      *)
(***************************************************************************)

AdvanceTime ==
  /\ sup_alive
  /\ time < MaxTime
  /\ time' = time + 1
  /\ UNCHANGED <<sup_alive, stopping, child_alive, suppress_restart,
                  restart_times, restart_group, observations>>

(***************************************************************************)
(* Next-state relation                                                      *)
(***************************************************************************)

Next ==
  \/ \E child \in ChildIds :
       \E reason \in ExitReasons :
         HandleTermination(child, reason)
  \/ \E child \in ChildIds :
       HandleRestartGroupStop(child)
  \/ FinalizeRestartGroup
  \/ SupervisorStop
  \/ \E child \in ChildIds :
       ShutdownChildExit(child)
  \/ AdvanceTime

Spec == Init /\ [][Next]_vars

(***************************************************************************)
(* Invariants                                                               *)
(***************************************************************************)

\* Temporary children are never directly restarted.
TemporaryNeverRestarted ==
  \A i \in 1..Len(observations) :
    observations[i].type = "restart" =>
      ChildRestartPolicy[observations[i].child] # "temporary"

\* Transient children with normal exit are not directly restarted.
TransientNormalNotRestarted ==
  \A i \in 1..Len(observations) :
    (observations[i].type = "restart"
     /\ ChildRestartPolicy[observations[i].child] = "transient") =>
      observations[i].reason # "normal"

\* Sliding window intensity is respected: restart_times never exceeds limit.
IntensityRespected ==
  sup_alive => Len(restart_times) <= MaxRestarts

\* No restart or group_restart occurs after supervisor begins shutdown.
NoRestartDuringShutdown ==
  LET has_shutdown ==
        \E j \in 1..Len(observations) :
          observations[j].type = "shutdown"
  IN has_shutdown =>
       LET shutdown_idx ==
             CHOOSE j \in 1..Len(observations) :
               observations[j].type = "shutdown"
       IN \A i \in 1..Len(observations) :
            i > shutdown_idx =>
              /\ observations[i].type # "restart"
              /\ observations[i].type # "group_restart"

\* rest_for_one only affects children started after the failed child.
RestForOneOnlyLater ==
  StrategyConst = "rest_for_one" =>
    (restart_group.active =>
       \A c \in restart_group.plan \ {restart_group.failed} :
         ChildStartOrder[c] > ChildStartOrder[restart_group.failed])

(***************************************************************************)
(* Focused scenarios                                                        *)
(***************************************************************************)

\* --- OneForOneRestart ---
\* A single child exits abnormally and is restarted directly.

OneForOneRestartInit ==
  /\ Init
  /\ StrategyConst = "one_for_one"

OneForOneRestartNext ==
  \/ \E child \in ChildIds :
       \E reason \in ExitReasons :
         HandleTermination(child, reason)
  \/ AdvanceTime

OneForOneRestartSpec ==
  OneForOneRestartInit /\ [][OneForOneRestartNext]_vars

\* --- OneForAllRestart ---
\* A child exits abnormally, all children are restarted via group restart.

OneForAllRestartInit ==
  /\ Init
  /\ StrategyConst = "one_for_all"

OneForAllRestartNext ==
  \/ \E child \in ChildIds :
       \E reason \in ExitReasons :
         HandleTermination(child, reason)
  \/ \E child \in ChildIds :
       HandleRestartGroupStop(child)
  \/ FinalizeRestartGroup
  \/ AdvanceTime

OneForAllRestartSpec ==
  OneForAllRestartInit /\ [][OneForAllRestartNext]_vars

\* --- RestForOneRestart ---
\* A child exits abnormally, it and later children are group-restarted.

RestForOneRestartInit ==
  /\ Init
  /\ StrategyConst = "rest_for_one"

RestForOneRestartNext == OneForAllRestartNext

RestForOneRestartSpec ==
  RestForOneRestartInit /\ [][RestForOneRestartNext]_vars

\* --- IntensityExceeded ---
\* Restart intensity exceeded causes supervisor shutdown.

IntensityExceededInit == Init

IntensityExceededNext ==
  \/ \E child \in ChildIds :
       \E reason \in ExitReasons :
         HandleTermination(child, reason)
  \/ \E child \in ChildIds :
       HandleRestartGroupStop(child)
  \/ FinalizeRestartGroup
  \/ \E child \in ChildIds :
       ShutdownChildExit(child)
  \/ AdvanceTime

IntensityExceededSpec ==
  IntensityExceededInit /\ [][IntensityExceededNext]_vars

\* --- ShutdownSuppressRestart ---
\* Supervisor stop suppresses all restarts.

ShutdownSuppressRestartInit == Init

ShutdownSuppressRestartNext ==
  \/ SupervisorStop
  \/ \E child \in ChildIds :
       ShutdownChildExit(child)
  \/ \E child \in ChildIds :
       \E reason \in ExitReasons :
         HandleTermination(child, reason)
  \/ AdvanceTime

ShutdownSuppressRestartSpec ==
  ShutdownSuppressRestartInit /\ [][ShutdownSuppressRestartNext]_vars

\* --- TemporaryInGroupRestart ---
\* Temporary child in a group restart is not respawned after finalize.

TemporaryInGroupRestartInit ==
  /\ Init
  /\ StrategyConst \in {"one_for_all", "rest_for_one"}

TemporaryInGroupRestartNext == OneForAllRestartNext

TemporaryInGroupRestartSpec ==
  TemporaryInGroupRestartInit /\ [][TemporaryInGroupRestartNext]_vars

=============================================================================
