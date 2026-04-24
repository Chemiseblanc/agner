---- MODULE actor_lifecycle ----
EXTENDS Naturals, Sequences, FiniteSets, TLC

(***************************************************************************)
(* Source mapping                                                           *)
(*                                                                         *)
(* This module models actor lifecycle, bidirectional links, unidirectional  *)
(* monitors, and exit-signal propagation in the Agner actor framework.     *)
(*                                                                         *)
(* - SchedulerBase::spawn_impl()           -> Spawn, SpawnLink             *)
(* - SchedulerBase::link()                 -> Link (bidirectional)         *)
(* - SchedulerBase::monitor()              -> Monitor (unidirectional)     *)
(* - SchedulerBase::notify_exit()          -> Exit (sends signals, then    *)
(*     removes actor — atomic w.r.t. signal delivery)                      *)
(* - Actor::stop() / ExitSignal delivery   -> ProcessExitSignal            *)
(* - Actor::receive() matching ExitSignal  -> trap_exit (implicit via      *)
(*     visitor presence; modelled as trap_exit flag)                        *)
(* - ExitSignal -> linked peers            -> ExitSignal propagation       *)
(* - DownSignal -> monitors                -> DownSignal delivery          *)
(*                                                                         *)
(* Deliberate abstractions:                                                 *)
(* - Actors are reduced to lifecycle states; mailbox content is not        *)
(*   modelled (see core_actor_system for mailbox semantics).               *)
(* - ExitReason is collapsed to a single "killed" reason for cascading.   *)
(* - "trap_exit" is modelled as a boolean flag rather than visitor-based   *)
(*   selective receive, capturing the observable effect.                    *)
(* - Scheduling order is fully nondeterministic.                           *)
(***************************************************************************)

CONSTANTS Actors

ASSUME Cardinality(Actors) >= 1

(***************************************************************************)
(* State variables                                                          *)
(***************************************************************************)

VARIABLES
  state,          \* [Actors -> {"absent", "running", "stopped", "done"}]
  links,          \* [Actors -> SUBSET Actors]  (bidirectional link sets)
  monitors,       \* [Actors -> SUBSET Actors]  (who monitors this actor)
  trap_exit,      \* [Actors -> BOOLEAN]  (whether actor traps ExitSignal)
  exit_signals,   \* [Actors -> Seq(Actors)]  (pending ExitSignals from)
  down_signals,   \* [Actors -> Seq(Actors)]  (pending DownSignals from)
  stopped_reason  \* [Actors -> {"none", "normal", "killed"}]

vars == <<state, links, monitors, trap_exit, exit_signals, down_signals,
          stopped_reason>>

(***************************************************************************)
(* Type invariant                                                           *)
(***************************************************************************)

States == {"absent", "running", "stopped", "done"}
Reasons == {"none", "normal", "killed"}

TypeOK ==
  /\ state \in [Actors -> States]
  /\ \A a \in Actors : links[a] \subseteq Actors
  /\ \A a \in Actors : monitors[a] \subseteq Actors
  /\ trap_exit \in [Actors -> BOOLEAN]
  /\ \A a \in Actors :
       /\ exit_signals[a] \in Seq(Actors)
       /\ Len(exit_signals[a]) <= Cardinality(Actors)
  /\ \A a \in Actors :
       /\ down_signals[a] \in Seq(Actors)
       /\ Len(down_signals[a]) <= Cardinality(Actors)
  /\ stopped_reason \in [Actors -> Reasons]

(***************************************************************************)
(* Invariants                                                               *)
(***************************************************************************)

\* Links are always symmetric: if a links b, then b links a.
\* Source: SchedulerBase::link() adds both directions;
\*         notify_exit() removes both directions.
LinkSymmetry ==
  \A a, b \in Actors :
    b \in links[a] <=> a \in links[b]

\* Done actors have no links, monitors, or pending signals.
DoneActorsClean ==
  \A a \in Actors :
    state[a] = "done" =>
      /\ links[a] = {}
      /\ monitors[a] = {}
      /\ exit_signals[a] = <<>>
      /\ down_signals[a] = <<>>

\* Absent actors have pristine state.
AbsentActorsClean ==
  \A a \in Actors :
    state[a] = "absent" =>
      /\ links[a] = {}
      /\ monitors[a] = {}
      /\ trap_exit[a] = FALSE
      /\ exit_signals[a] = <<>>
      /\ down_signals[a] = <<>>
      /\ stopped_reason[a] = "none"

\* Links and monitors only reference live actors (running or stopped).
LinksOnlyLive ==
  \A a \in Actors :
    \A b \in links[a] :
      state[b] \in {"running", "stopped"}

MonitorsOnlyLive ==
  \A a \in Actors :
    \A b \in monitors[a] :
      state[b] \in {"running", "stopped"}

\* No actor links to itself.
NoSelfLinks ==
  \A a \in Actors : a \notin links[a]

(***************************************************************************)
(* Helpers                                                                  *)
(***************************************************************************)

\* Set of actors in signal source sequence.
SignalSources(seq) ==
  {seq[i] : i \in 1..Len(seq)}

\* Remove an actor from all link sets (used during exit).
RemoveFromLinks(actor) ==
  [a \in Actors |->
    IF a = actor THEN {}
    ELSE links[a] \ {actor}]

\* Remove an actor from all monitor sets (used during exit).
RemoveFromMonitors(actor) ==
  [a \in Actors |->
    IF a = actor THEN {}
    ELSE monitors[a] \ {actor}]

\* Append an ExitSignal to each linked peer's signal queue.
\* Source: notify_exit() iterates links and sends ExitSignal via send().
SendExitSignals(actor, peers) ==
  [a \in Actors |->
    IF a \in peers /\ a # actor /\ state[a] \in {"running", "stopped"}
    THEN Append(exit_signals[a], actor)
    ELSE exit_signals[a]]

\* Append a DownSignal to each monitor's signal queue.
\* Source: notify_exit() iterates monitors and sends DownSignal via send().
SendDownSignals(actor, watchers) ==
  [a \in Actors |->
    IF a \in watchers /\ a # actor /\ state[a] \in {"running", "stopped"}
    THEN Append(down_signals[a], actor)
    ELSE down_signals[a]]

(***************************************************************************)
(* Actions                                                                  *)
(***************************************************************************)

\* Source: SchedulerBase::spawn_impl() — spawn without link.
Spawn(a) ==
  /\ state[a] = "absent"
  /\ state' = [state EXCEPT ![a] = "running"]
  /\ trap_exit' = trap_exit  \* default FALSE from Init
  /\ UNCHANGED <<links, monitors, exit_signals, down_signals, stopped_reason>>

\* Source: SchedulerBase::spawn_impl() with linker — spawn_link atomicity.
\* Link is established before the child can run any step.
SpawnLink(child, parent) ==
  /\ child # parent
  /\ state[child] = "absent"
  /\ state[parent] = "running"
  /\ state' = [state EXCEPT ![child] = "running"]
  /\ links' = [links EXCEPT ![child] = @ \cup {parent},
                             ![parent] = @ \cup {child}]
  /\ UNCHANGED <<monitors, trap_exit, exit_signals, down_signals,
                 stopped_reason>>

\* Source: SchedulerBase::link() — explicit bidirectional link.
Link(a, b) ==
  /\ a # b
  /\ state[a] = "running"
  /\ state[b] = "running"
  /\ b \notin links[a]  \* idempotency guard
  /\ links' = [links EXCEPT ![a] = @ \cup {b},
                             ![b] = @ \cup {a}]
  /\ UNCHANGED <<state, monitors, trap_exit, exit_signals, down_signals,
                 stopped_reason>>

\* Source: SchedulerBase::monitor() — unidirectional monitoring.
Monitor(watcher, target) ==
  /\ watcher # target
  /\ state[watcher] = "running"
  /\ state[target] \in {"running", "stopped"}
  /\ watcher \notin monitors[target]  \* idempotency guard
  /\ monitors' = [monitors EXCEPT ![target] = @ \cup {watcher}]
  /\ UNCHANGED <<state, links, trap_exit, exit_signals, down_signals,
                 stopped_reason>>

\* Source: Actor::stop() — set trap_exit flag on running actor.
\* Models the actor electing to handle ExitSignals (visitor matches ExitSignal).
SetTrapExit(a) ==
  /\ state[a] = "running"
  /\ ~trap_exit[a]
  /\ trap_exit' = [trap_exit EXCEPT ![a] = TRUE]
  /\ UNCHANGED <<state, links, monitors, exit_signals, down_signals,
                 stopped_reason>>

\* Source: SchedulerBase::notify_exit() + actors_.erase() — actor exits.
\* Signals are sent BEFORE the actor is removed (atomic w.r.t. delivery).
\* The actor transitions to "done" and its links/monitors are cleaned up.
Exit(a) ==
  /\ state[a] = "stopped"
  /\ LET peers == links[a]
         watchers == monitors[a]
     IN /\ exit_signals' =
             LET sent == SendExitSignals(a, peers)
             IN [sent EXCEPT ![a] = <<>>]
        /\ down_signals' =
             LET sent == SendDownSignals(a, watchers)
             IN [sent EXCEPT ![a] = <<>>]
        /\ links' = RemoveFromLinks(a)
        /\ monitors' = RemoveFromMonitors(a)
        /\ state' = [state EXCEPT ![a] = "done"]
        /\ stopped_reason' = stopped_reason
        /\ UNCHANGED <<trap_exit>>

\* Source: Actor receives stop command or normal completion.
\* Running actor transitions to stopped with a reason.
StopNormal(a) ==
  /\ state[a] = "running"
  /\ state' = [state EXCEPT ![a] = "stopped"]
  /\ stopped_reason' = [stopped_reason EXCEPT ![a] = "normal"]
  /\ UNCHANGED <<links, monitors, trap_exit, exit_signals, down_signals>>

StopKilled(a) ==
  /\ state[a] = "running"
  /\ state' = [state EXCEPT ![a] = "stopped"]
  /\ stopped_reason' = [stopped_reason EXCEPT ![a] = "killed"]
  /\ UNCHANGED <<links, monitors, trap_exit, exit_signals, down_signals>>

\* Source: Actor::receive() matches ExitSignal — cascading propagation.
\* When a non-trapping actor receives an ExitSignal, it transitions to
\* "stopped" (killed), triggering its own Exit in a subsequent step.
\* When a trapping actor receives an ExitSignal, it consumes the signal
\* and stays running.
ProcessExitSignal(a) ==
  /\ state[a] = "running"
  /\ Len(exit_signals[a]) > 0
  /\ LET sender == Head(exit_signals[a])
         rest == Tail(exit_signals[a])
     IN IF trap_exit[a]
        THEN \* Trapping actor: consume signal, stay running.
             /\ exit_signals' = [exit_signals EXCEPT ![a] = rest]
             /\ UNCHANGED <<state, links, monitors, trap_exit, down_signals,
                            stopped_reason>>
        ELSE \* Non-trapping actor: cascade — mark as stopped/killed.
             /\ state' = [state EXCEPT ![a] = "stopped"]
             /\ stopped_reason' = [stopped_reason EXCEPT ![a] = "killed"]
             /\ exit_signals' = [exit_signals EXCEPT ![a] = rest]
             /\ UNCHANGED <<links, monitors, trap_exit, down_signals>>

\* Source: Actor::receive() matches DownSignal — consume notification.
\* DownSignals are informational; the actor stays running.
ProcessDownSignal(a) ==
  /\ state[a] = "running"
  /\ Len(down_signals[a]) > 0
  /\ down_signals' = [down_signals EXCEPT ![a] = Tail(@)]
  /\ UNCHANGED <<state, links, monitors, trap_exit, exit_signals,
                 stopped_reason>>

(***************************************************************************)
(* Specification                                                            *)
(***************************************************************************)

Init ==
  /\ state = [a \in Actors |-> "absent"]
  /\ links = [a \in Actors |-> {}]
  /\ monitors = [a \in Actors |-> {}]
  /\ trap_exit = [a \in Actors |-> FALSE]
  /\ exit_signals = [a \in Actors |-> <<>>]
  /\ down_signals = [a \in Actors |-> <<>>]
  /\ stopped_reason = [a \in Actors |-> "none"]

Next ==
  \/ \E a \in Actors : Spawn(a)
  \/ \E a, b \in Actors : SpawnLink(a, b)
  \/ \E a, b \in Actors : Link(a, b)
  \/ \E a, b \in Actors : Monitor(a, b)
  \/ \E a \in Actors : SetTrapExit(a)
  \/ \E a \in Actors : StopNormal(a)
  \/ \E a \in Actors : StopKilled(a)
  \/ \E a \in Actors : Exit(a)
  \/ \E a \in Actors : ProcessExitSignal(a)
  \/ \E a \in Actors : ProcessDownSignal(a)

Spec == Init /\ [][Next]_vars

(***************************************************************************)
(* Focused scenarios                                                        *)
(***************************************************************************)

\* --- CascadeExit: 3 chain-linked actors, killing head cascades to tail ---
\* Models: a1 --link-- a2 --link-- a3, then a1 is killed.
\* Expected: ExitSignal propagates a1 -> a2 -> a3, all reach "done".

CONSTANTS a1, a2, a3

CascadeActors == {a1, a2, a3}

CascadeExitInit ==
  /\ state = [a \in Actors |->
       CASE a = a1 -> "stopped"
         [] a \in {a2, a3} -> "running"
         [] OTHER -> "absent"]
  /\ links = [a \in Actors |->
       CASE a = a1 -> {a2}
         [] a = a2 -> {a1, a3}
         [] a = a3 -> {a2}
         [] OTHER -> {}]
  /\ monitors = [a \in Actors |-> {}]
  /\ trap_exit = [a \in Actors |-> FALSE]
  /\ exit_signals = [a \in Actors |-> <<>>]
  /\ down_signals = [a \in Actors |-> <<>>]
  /\ stopped_reason = [a \in Actors |->
       IF a = a1 THEN "killed" ELSE "none"]

CascadeExitNext ==
  \/ Exit(a1)
  \/ Exit(a2)
  \/ Exit(a3)
  \/ ProcessExitSignal(a1)
  \/ ProcessExitSignal(a2)
  \/ ProcessExitSignal(a3)

CascadeExitSpec ==
  CascadeExitInit /\ [][CascadeExitNext]_vars
    /\ WF_vars(CascadeExitNext)

\* All three actors eventually reach "done".
CascadeExitAllDone ==
  state[a1] = "done" /\ state[a2] = "done" /\ state[a3] = "done"

\* Until all are done, the cascade is in progress — used as a liveness check.
CascadeExitProgress ==
  <>(state[a1] = "done" /\ state[a2] = "done" /\ state[a3] = "done")

\* --- SpawnLinkAtomicity: link established before child runs ---
\* Models: parent spawns child with spawn_link; child is immediately linked.

SpawnLinkAtomicityInit ==
  /\ state = [a \in Actors |->
       IF a = a1 THEN "running" ELSE "absent"]
  /\ links = [a \in Actors |-> {}]
  /\ monitors = [a \in Actors |-> {}]
  /\ trap_exit = [a \in Actors |-> FALSE]
  /\ exit_signals = [a \in Actors |-> <<>>]
  /\ down_signals = [a \in Actors |-> <<>>]
  /\ stopped_reason = [a \in Actors |-> "none"]

SpawnLinkAtomicityNext ==
  \/ SpawnLink(a2, a1)
  \/ StopKilled(a2)
  \/ Exit(a2)
  \/ ProcessExitSignal(a1)

SpawnLinkAtomicitySpec ==
  SpawnLinkAtomicityInit /\ [][SpawnLinkAtomicityNext]_vars

\* After SpawnLink, the link is immediately present (before child acts).
\* The child never runs without the link being established.
SpawnLinkAtomicityInvariant ==
  state[a2] \in {"running", "stopped"} => a1 \in links[a2]

\* --- TrapExitStopsCascade: trap_exit prevents cascade propagation ---
\* Models: a1 --link-- a2 (trapping) --link-- a3, a1 killed.
\* Expected: a2 traps the signal; a3 is never killed.

TrapExitStopsCascadeInit ==
  /\ state = [a \in Actors |->
       CASE a = a1 -> "stopped"
         [] a \in {a2, a3} -> "running"
         [] OTHER -> "absent"]
  /\ links = [a \in Actors |->
       CASE a = a1 -> {a2}
         [] a = a2 -> {a1, a3}
         [] a = a3 -> {a2}
         [] OTHER -> {}]
  /\ monitors = [a \in Actors |-> {}]
  /\ trap_exit = [a \in Actors |->
       IF a = a2 THEN TRUE ELSE FALSE]
  /\ exit_signals = [a \in Actors |-> <<>>]
  /\ down_signals = [a \in Actors |-> <<>>]
  /\ stopped_reason = [a \in Actors |->
       IF a = a1 THEN "killed" ELSE "none"]

TrapExitStopsCascadeNext ==
  \/ Exit(a1)
  \/ Exit(a2)
  \/ Exit(a3)
  \/ ProcessExitSignal(a1)
  \/ ProcessExitSignal(a2)
  \/ ProcessExitSignal(a3)
  \/ ProcessDownSignal(a2)
  \/ ProcessDownSignal(a3)

TrapExitStopsCascadeSpec ==
  TrapExitStopsCascadeInit /\ [][TrapExitStopsCascadeNext]_vars

\* a3 never reaches "stopped" or "done" due to cascading (stays running).
TrapExitProtectsA3 ==
  stopped_reason[a3] = "none"

\* --- MonitorNotLink: monitors deliver DownSignal, not ExitSignal ---
\* Models: a1 monitors a2 (no link). a2 exits.
\* Expected: a1 gets DownSignal, never ExitSignal. a1 stays running.

MonitorNotLinkInit ==
  /\ state = [a \in Actors |->
       CASE a = a1 -> "running"
         [] a = a2 -> "stopped"
         [] OTHER -> "absent"]
  /\ links = [a \in Actors |-> {}]
  /\ monitors = [a \in Actors |->
       IF a = a2 THEN {a1} ELSE {}]
  /\ trap_exit = [a \in Actors |-> FALSE]
  /\ exit_signals = [a \in Actors |-> <<>>]
  /\ down_signals = [a \in Actors |-> <<>>]
  /\ stopped_reason = [a \in Actors |->
       IF a = a2 THEN "normal" ELSE "none"]

MonitorNotLinkNext ==
  \/ Exit(a2)
  \/ ProcessDownSignal(a1)
  \/ ProcessExitSignal(a1)

MonitorNotLinkSpec ==
  MonitorNotLinkInit /\ [][MonitorNotLinkNext]_vars

\* a1 never receives an ExitSignal (monitors are not links).
MonitorNoExitSignal ==
  exit_signals[a1] = <<>>

\* a1 stays running (DownSignal is informational, not lethal).
MonitorA1StaysRunning ==
  state[a1] = "running"

=============================================================================
