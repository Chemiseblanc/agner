---- MODULE refinement_vocabulary ----
(***************************************************************************)
(* Shared refinement vocabulary                                            *)
(*                                                                         *)
(* This module centralizes small reusable operators shared by refinement   *)
(* bridge specs. It is intentionally lightweight: it provides projection   *)
(* helpers and invariant skeletons, while scenario-specific phase logic    *)
(* and bounded action structure remain in each scenario module.            *)
(***************************************************************************)
EXTENDS actor_defs

ExtractReady(queue) ==
  {queue[i] : i \in 1..Len(queue)}

ReadyMembers(ready_surface) ==
  ExtractReady(ready_surface)

ReadyContains(ready_surface, actor) ==
  actor \in ReadyMembers(ready_surface)

ReadyRemove(ready_surface, actor) ==
  IF ReadyContains(ready_surface, actor)
    THEN LET idx == CHOOSE i \in 1..Len(ready_surface) : ready_surface[i] = actor
         IN RemoveAt(ready_surface, idx)
    ELSE ready_surface

ReadyAdd(ready_surface, actor) ==
  IF ReadyContains(ready_surface, actor)
    THEN ready_surface
    ELSE Append(ready_surface, actor)

ReadyEmpty ==
  <<>>

ReadySingleton(actor) ==
  ReadyAdd(ReadyEmpty, actor)

ReadySurfaceTypeOK(ready_surface) ==
  /\ ready_surface \in Seq(ActorPool)
  /\ \A i, j \in 1..Len(ready_surface) :
       i # j => ready_surface[i] # ready_surface[j]

DefineBridgeSpec(init, next, vars) ==
  init /\ [][next]_vars

ReadySubsumesLive(ready, live) ==
  ready \subseteq live

PendingResultsReady(pending_result, live, ready, pc) ==
  \A a \in ActorPool :
    pending_result[a] # NoPending =>
      /\ a \in live
      /\ a \in ready
      /\ CASE pending_result[a] = TimeoutToken -> pc[a] = "try"
         [] OTHER -> Matches(pc[a], pending_result[a])

BlockedActorsNoMatches(live, ready, pending_result, pc, mailboxes) ==
  \A a \in live \ ready :
    /\ pending_result[a] = NoPending
    /\ IF pc[a] \in {"collect", "seq_first", "seq_second", "try"}
          THEN MatchingIndices(pc[a], mailboxes[a]) = {}
          ELSE TRUE

TimerDisciplineHolds(timers, live, ready, pc, pending_result, time) ==
  \A a \in ActorPool :
    timers[a] # NoDeadline =>
      /\ a \in live
      /\ a \notin ready
      /\ pc[a] = "try"
      /\ pending_result[a] = NoPending
      /\ timers[a] >= time

CompletedActorsCleared(pc, live, mailboxes, pending_result, timers) ==
  \A a \in ActorPool :
    pc[a] = "done" =>
      /\ a \notin live
      /\ mailboxes[a] = <<>>
      /\ pending_result[a] = NoPending
      /\ timers[a] = NoDeadline

QueuedMessageIn(mailboxes, mid) ==
  \E a \in ActorPool : mid \in MessageIdsIn(mailboxes[a])

PendingMessageIn(pending_result, mid) ==
  \E a \in ActorPool :
    /\ pending_result[a].kind \in MessageKinds
    /\ pending_result[a].id = mid

ObservedMessageIn(observations, mid) ==
  \E a \in ActorPool :
    \E i \in 1..Len(observations[a]) :
      /\ observations[a][i].kind \in MessageKinds
      /\ observations[a][i].id = mid

MessageOwnershipHolds(msg_state, mailboxes, pending_result, observations) ==
  \A mid \in MessageIds :
    LET queued == QueuedMessageIn(mailboxes, mid) IN
    LET pending == PendingMessageIn(pending_result, mid) IN
    LET observed == ObservedMessageIn(observations, mid) IN
      CASE msg_state[mid] = "unused" ->
             /\ ~queued
             /\ ~pending
             /\ ~observed
        [] msg_state[mid] = "queued" ->
             /\ queued
             /\ ~pending
             /\ ~observed
        [] msg_state[mid] = "pending" ->
             /\ ~queued
             /\ pending
             /\ ~observed
        [] msg_state[mid] = "observed" ->
             /\ ~queued
             /\ ~pending
             /\ observed
        [] msg_state[mid] = "dropped" ->
             /\ ~queued
             /\ ~pending
             /\ ~observed
        [] OTHER -> FALSE

BiDirectionalLinks(links) ==
  \A a, b \in ActorPool : <<a, b>> \in links => <<b, a>> \in links

LinksBoundToLive(links, live) ==
  \A a, b \in ActorPool : <<a, b>> \in links => /\ a \in live /\ b \in live

MonitorsBoundToLive(monitors, live) ==
  \A m, t \in ActorPool : <<m, t>> \in monitors => m \in live

DeadHaveReason(pc, exit_reason) ==
  \A a \in ActorPool : pc[a] = "done" => exit_reason[a] \in ExitReasons

LiveHaveNoReason(live, exit_reason) ==
  \A a \in ActorPool : a \in live => exit_reason[a] = "none"

=============================================================================