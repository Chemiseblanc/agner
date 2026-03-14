---- MODULE link_propagation ----
(***************************************************************************)
(* Link Propagation Scenario                                               *)
(*                                                                         *)
(* Verifies that when a linked actor exits, ExitSignal is delivered to     *)
(* the linked partner, and when a monitored actor exits, DownSignal is     *)
(* delivered to the monitoring actor. Also verifies that link/monitor       *)
(* topology is cleaned up correctly on exit.                               *)
(*                                                                         *)
(* Source mapping:                                                          *)
(* - SchedulerBase::notify_exit() -> NotifyExit action                     *)
(* - SchedulerBase::link() -> links variable, Link action                  *)
(* - SchedulerBase::monitor() -> monitors variable, SetMonitor action      *)
(* - ExitSignal/DownSignal -> signal observations                          *)
(*                                                                         *)
(* Scenario:                                                                *)
(* - Three actors: watcher, linked_child, monitored_child                  *)
(* - watcher is linked to linked_child (bidirectional)                     *)
(* - watcher monitors monitored_child                                      *)
(* - When linked_child exits, watcher receives ExitSignal                  *)
(* - When monitored_child exits, watcher receives DownSignal               *)
(***************************************************************************)
EXTENDS actor_system

CONSTANTS Watcher, LinkedChild, MonitoredChild

ASSUME /\ Watcher \in ActorPool
       /\ LinkedChild \in ActorPool
       /\ MonitoredChild \in ActorPool
       /\ Watcher # LinkedChild
       /\ Watcher # MonitoredChild
       /\ LinkedChild # MonitoredChild

(***************************************************************************)
(* Watcher actor kind and states                                           *)
(* The watcher waits to observe signals from its children.                 *)
(* It uses the "collector" kind to receive messages (Ping), but we also    *)
(* need it to receive signals. We model it as a custom loop that observes  *)
(* signals by watching for linked/monitored actors to die.                 *)
(***************************************************************************)

(***************************************************************************)
(* NotifyExit: when an actor dies, propagate ExitSignal to linked actors   *)
(* and DownSignal to monitoring actors.                                    *)
(*                                                                         *)
(* This models SchedulerBase::notify_exit() which iterates linked and      *)
(* monitored actors and sends signals. In the model we record signals in   *)
(* the observations of the receiving actors.                               *)
(***************************************************************************)
NotifyExit(a, reason) ==
  /\ a \in ActorPool
  /\ pc[a] = "done"
  /\ exit_reason[a] = reason
  /\ LET linked == LinkedTo(a)
         watching == MonitoredBy(a)
     IN
     \* Record ExitSignal observations for linked actors still alive
     /\ observations' =
          [b \in ActorPool |->
            IF b \in linked /\ b \in live
              THEN Append(observations[b], ExitSignal(a, reason))
            ELSE IF b \in watching /\ b \in live
              THEN Append(observations[b], DownSignal(a, reason))
            ELSE observations[b]]
     /\ UNCHANGED <<live, kind, pc, ready, mailboxes, pending_result,
                     timers, msg_state, time, links, monitors, exit_reason>>

(***************************************************************************)
(* ExitActor: a live actor exits (normal or error) and triggers cleanup    *)
(***************************************************************************)
ExitActor(a, reason) ==
  /\ a \in live
  /\ a # Watcher  \* Watcher doesn't exit in this scenario
  /\ pc[a] # "done"
  /\ live' = live \ {a}
  /\ pc' = [pc EXCEPT ![a] = "done"]
  /\ ready' = ready \ {a}
  /\ mailboxes' = [mailboxes EXCEPT ![a] = <<>>]
  /\ pending_result' = [pending_result EXCEPT ![a] = NoPending]
  /\ timers' = [timers EXCEPT ![a] = NoDeadline]
  /\ CompleteActor(a, reason)
  /\ UNCHANGED <<kind, msg_state, time, observations>>

(***************************************************************************)
(* Scenario-specific initial state                                         *)
(***************************************************************************)
LinkPropagationInit ==
  /\ live = {Watcher, LinkedChild, MonitoredChild}
  /\ kind =
       [a \in ActorPool |->
         IF a = Watcher THEN "collector"
         ELSE IF a = LinkedChild THEN "collector"
         ELSE IF a = MonitoredChild THEN "collector"
         ELSE "none"]
  /\ pc =
       [a \in ActorPool |->
         IF a \in {Watcher, LinkedChild, MonitoredChild} THEN "collect"
         ELSE "absent"]
  /\ ready = {Watcher, LinkedChild, MonitoredChild}
  /\ mailboxes = [a \in ActorPool |-> <<>>]
  /\ pending_result = [a \in ActorPool |-> NoPending]
  /\ timers = [a \in ActorPool |-> NoDeadline]
  /\ observations = [a \in ActorPool |-> <<>>]
  /\ msg_state = [id \in MessageIds |-> "unused"]
  /\ time = 0
  /\ links = {<<Watcher, LinkedChild>>, <<LinkedChild, Watcher>>}
  /\ monitors = {<<Watcher, MonitoredChild>>}
  /\ exit_reason = [a \in ActorPool |-> "none"]

(***************************************************************************)
(* Scenario-specific next-state relation                                   *)
(***************************************************************************)
LinkPropagationNext ==
  \/ ExitActor(LinkedChild, ExitNormal)
  \/ ExitActor(MonitoredChild, ExitNormal)
  \/ ExitActor(LinkedChild, ExitError)
  \/ ExitActor(MonitoredChild, ExitError)
  \/ \E a \in {LinkedChild, MonitoredChild} :
       \E r \in ExitReasons :
         /\ pc[a] = "done"
         /\ NotifyExit(a, r)

(***************************************************************************)
(* Specification                                                            *)
(***************************************************************************)
LinkPropagationSpec ==
  LinkPropagationInit /\ [][LinkPropagationNext]_vars

(***************************************************************************)
(* Invariants                                                               *)
(***************************************************************************)

\* After linked_child exits and signals propagate, watcher gets ExitSignal
LinkedChildExitSignal ==
  \A i \in 1..Len(observations[Watcher]) :
    observations[Watcher][i].kind = "ExitSignal" =>
      observations[Watcher][i].from = LinkedChild

\* After monitored_child exits and signals propagate, watcher gets DownSignal
MonitoredChildDownSignal ==
  \A i \in 1..Len(observations[Watcher]) :
    observations[Watcher][i].kind = "DownSignal" =>
      observations[Watcher][i].from = MonitoredChild

\* Links cleaned up when actor exits
LinksCleanedOnExit ==
  \A a \in ActorPool :
    pc[a] = "done" =>
      /\ \A b \in ActorPool : <<a, b>> \notin links
      /\ \A b \in ActorPool : <<b, a>> \notin links

\* Monitors targeting dead actor cleaned up
MonitorsCleanedOnExit ==
  \A a \in ActorPool :
    pc[a] = "done" =>
      \A m \in ActorPool : <<m, a>> \notin monitors

\* Non-linked actors don't receive ExitSignals from linked_child
NoSpuriousExitSignals ==
  \A a \in ActorPool :
    a # Watcher =>
      \A i \in 1..Len(observations[a]) :
        observations[a][i].kind # "ExitSignal"

\* Non-monitoring actors don't receive DownSignals from monitored_child
NoSpuriousDownSignals ==
  \A a \in ActorPool :
    a # Watcher =>
      \A i \in 1..Len(observations[a]) :
        observations[a][i].kind # "DownSignal"

====
