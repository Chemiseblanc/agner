---- MODULE topology_signal_core_projection ----
(* ************************************************************************* *)
(* Topology Signal Core Projection                                        *)
(*                                                                         *)
(* Projects a bounded implementation-oriented exit cleanup and signal      *)
(* propagation model onto the core-facing topology boundary.              *)
(*                                                                         *)
(* This bridge covers one watcher, one linked child, and one monitored    *)
(* child, with two mutually exclusive paths:                               *)
(* - linked child exit produces ExitSignal and removes both link edges     *)
(* - monitored child exit produces DownSignal and removes the monitor edge *)
(* ************************************************************************* *)
EXTENDS actor_defs, refinement_vocabulary, TLC

CONSTANTS Watcher, LinkedChild, MonitoredChild

ASSUME /\ Watcher \in ActorPool
       /\ LinkedChild \in ActorPool
       /\ MonitoredChild \in ActorPool
       /\ Cardinality({Watcher, LinkedChild, MonitoredChild}) = 3

ScenarioActors == {Watcher, LinkedChild, MonitoredChild}
ImplPcStates == {"absent", "collect", "done"}
BridgePhases == {"choose", "linked_done", "monitored_done", "done"}

VARIABLES impl_live, impl_pc, impl_links, impl_monitors,
          impl_observations, impl_exit_reason, phase

bridge_vars ==
  <<impl_live, impl_pc, impl_links, impl_monitors,
    impl_observations, impl_exit_reason, phase>>

ProjectedLive == impl_live

ProjectedPc == impl_pc

ProjectedLinks == impl_links

ProjectedMonitors == impl_monitors

ProjectedObservations == impl_observations

ProjectedExitReason == impl_exit_reason

ProjectionInit ==
  /\ impl_live = ScenarioActors
  /\ impl_pc =
       [a \in ActorPool |->
         IF a \in ScenarioActors THEN "collect" ELSE "absent"]
  /\ impl_links = {<<Watcher, LinkedChild>>, <<LinkedChild, Watcher>>}
  /\ impl_monitors = {<<Watcher, MonitoredChild>>}
  /\ impl_observations = [a \in ActorPool |-> <<>>]
  /\ impl_exit_reason = [a \in ActorPool |-> "none"]
  /\ phase = "choose"

ExitLinkedChild(reason) ==
  /\ phase = "choose"
  /\ reason \in ExitReasons
  /\ LinkedChild \in impl_live
  /\ impl_live' = impl_live \ {LinkedChild}
  /\ impl_pc' = [impl_pc EXCEPT ![LinkedChild] = "done"]
  /\ impl_links' = {pair \in impl_links : pair[1] # LinkedChild /\ pair[2] # LinkedChild}
  /\ impl_monitors' = impl_monitors
  /\ impl_observations' =
       [impl_observations EXCEPT
         ![Watcher] = Append(@, ExitSignal(LinkedChild, reason))]
  /\ impl_exit_reason' = [impl_exit_reason EXCEPT ![LinkedChild] = reason]
  /\ phase' = "linked_done"

ExitMonitoredChild(reason) ==
  /\ phase = "choose"
  /\ reason \in ExitReasons
  /\ MonitoredChild \in impl_live
  /\ impl_live' = impl_live \ {MonitoredChild}
  /\ impl_pc' = [impl_pc EXCEPT ![MonitoredChild] = "done"]
  /\ impl_links' = impl_links
  /\ impl_monitors' = {pair \in impl_monitors : pair[2] # MonitoredChild}
  /\ impl_observations' =
       [impl_observations EXCEPT
         ![Watcher] = Append(@, DownSignal(MonitoredChild, reason))]
  /\ impl_exit_reason' = [impl_exit_reason EXCEPT ![MonitoredChild] = reason]
  /\ phase' = "monitored_done"

Done ==
  /\ phase \in {"linked_done", "monitored_done", "done"}
  /\ phase' = "done"
  /\ UNCHANGED <<impl_live, impl_pc, impl_links, impl_monitors,
                 impl_observations, impl_exit_reason>>

ProjectionNext ==
  \/ \E reason \in ExitReasons : ExitLinkedChild(reason)
  \/ \E reason \in ExitReasons : ExitMonitoredChild(reason)
  \/ Done

ProjectionSpec ==
  ProjectionInit /\ [][ProjectionNext]_bridge_vars

ImplTypeOK ==
  /\ impl_live \subseteq ActorPool
  /\ impl_pc \in [ActorPool -> ImplPcStates]
  /\ impl_links \subseteq (ActorPool \X ActorPool)
  /\ impl_monitors \subseteq (ActorPool \X ActorPool)
  /\ impl_observations \in [ActorPool -> Seq(SignalUniverse)]
  /\ impl_exit_reason \in [ActorPool -> ExitReasons \cup {"none"}]
  /\ phase \in BridgePhases

ProjectionTypeOK ==
  /\ ProjectedLive \subseteq ActorPool
  /\ \A a \in ActorPool : ProjectedPc[a] \in PcStates
  /\ ProjectedLinks \subseteq (ActorPool \X ActorPool)
  /\ ProjectedMonitors \subseteq (ActorPool \X ActorPool)
  /\ ProjectedObservations \in [ActorPool -> Seq(SignalUniverse)]
  /\ ProjectedExitReason \in [ActorPool -> ExitReasons \cup {"none"}]

ProjectedLinksStayBidirectional ==
  BiDirectionalLinks(ProjectedLinks)

LinkedExitProjectsSignalAndCleanup ==
  phase \in {"linked_done", "done"} /\ LinkedChild \notin ProjectedLive =>
    /\ ProjectedPc[LinkedChild] = "done"
    /\ \A a \in ActorPool : <<LinkedChild, a>> \notin ProjectedLinks
    /\ \A a \in ActorPool : <<a, LinkedChild>> \notin ProjectedLinks
    /\ \E i \in 1..Len(ProjectedObservations[Watcher]) :
         /\ ProjectedObservations[Watcher][i].kind = "ExitSignal"
         /\ ProjectedObservations[Watcher][i].from = LinkedChild

MonitoredExitProjectsSignalAndCleanup ==
  phase \in {"monitored_done", "done"} /\ MonitoredChild \notin ProjectedLive =>
    /\ ProjectedPc[MonitoredChild] = "done"
    /\ \A a \in ActorPool : <<a, MonitoredChild>> \notin ProjectedMonitors
    /\ \E i \in 1..Len(ProjectedObservations[Watcher]) :
         /\ ProjectedObservations[Watcher][i].kind = "DownSignal"
         /\ ProjectedObservations[Watcher][i].from = MonitoredChild

NoSpuriousSignalKinds ==
  \A a \in ActorPool \ {Watcher} : Len(ProjectedObservations[a]) = 0

DeadActorsProjectExitReason ==
  \A a \in ScenarioActors :
    a \notin ProjectedLive => ProjectedExitReason[a] \in ExitReasons

=============================================================================