---- MODULE supervisor_admin ----
EXTENDS actor_defs, TLC, Sequences

CONSTANTS Sup, ChildA, ChildB

ASSUME /\ Sup \in ActorPool
       /\ ChildA \in ActorPool
       /\ ChildB \in ActorPool
       /\ Cardinality({Sup, ChildA, ChildB}) = 3

ChildId == "child"
NoChild == "none"

AdminPhases == {
  "init",
  "started_once",
  "reused_start",
  "lookup_running",
  "which_running",
  "stop_requested",
  "stopped",
  "lookup_stopped",
  "started_again",
  "restart_requested",
  "restarted",
  "delete_requested",
  "deleted",
  "which_deleted",
  "started_after_delete"
}

VARIABLES phase, current_child, next_child,
          last_start_result, lookup_result,
          which_children_view, last_replaced_ref

admin_vars ==
  <<phase, current_child, next_child,
    last_start_result, lookup_result,
    which_children_view, last_replaced_ref>>

SpawnedAfter(current, next) ==
  /\ current' = next
  /\ next' = IF next = ChildA THEN ChildB ELSE ChildA
  /\ last_start_result' = next
  /\ lookup_result' = lookup_result
  /\ which_children_view' = which_children_view
  /\ last_replaced_ref' = last_replaced_ref

AdminInit ==
  /\ phase = "init"
  /\ current_child = NoChild
  /\ next_child = ChildA
  /\ last_start_result = NoChild
  /\ lookup_result = NoChild
  /\ which_children_view = <<>>
  /\ last_replaced_ref = NoChild

StartChildFirst ==
  /\ phase = "init"
  /\ SpawnedAfter(current_child, next_child)
  /\ phase' = "started_once"

StartChildReuse ==
  /\ phase = "started_once"
  /\ current_child # NoChild
  /\ phase' = "reused_start"
  /\ current_child' = current_child
  /\ next_child' = next_child
  /\ last_start_result' = current_child
  /\ lookup_result' = lookup_result
  /\ which_children_view' = which_children_view
  /\ last_replaced_ref' = last_replaced_ref

LookupRunning ==
  /\ phase = "reused_start"
  /\ phase' = "lookup_running"
  /\ current_child' = current_child
  /\ next_child' = next_child
  /\ last_start_result' = last_start_result
  /\ lookup_result' = current_child
  /\ which_children_view' = which_children_view
  /\ last_replaced_ref' = last_replaced_ref

ObserveWhichRunning ==
  /\ phase = "lookup_running"
  /\ phase' = "which_running"
  /\ current_child' = current_child
  /\ next_child' = next_child
  /\ last_start_result' = last_start_result
  /\ lookup_result' = lookup_result
  /\ which_children_view' = <<ChildId>>
  /\ last_replaced_ref' = last_replaced_ref

StopChild ==
  /\ phase = "which_running"
  /\ current_child # NoChild
  /\ phase' = "stop_requested"
  /\ UNCHANGED <<current_child, next_child, last_start_result,
                 lookup_result, which_children_view, last_replaced_ref>>

FinishStop ==
  /\ phase = "stop_requested"
  /\ phase' = "stopped"
  /\ current_child' = NoChild
  /\ next_child' = next_child
  /\ last_start_result' = last_start_result
  /\ lookup_result' = lookup_result
  /\ which_children_view' = which_children_view
  /\ last_replaced_ref' = last_replaced_ref

LookupStopped ==
  /\ phase = "stopped"
  /\ phase' = "lookup_stopped"
  /\ current_child' = current_child
  /\ next_child' = next_child
  /\ last_start_result' = last_start_result
  /\ lookup_result' = NoChild
  /\ which_children_view' = which_children_view
  /\ last_replaced_ref' = last_replaced_ref

StartChildAgain ==
  /\ phase = "lookup_stopped"
  /\ SpawnedAfter(current_child, next_child)
  /\ phase' = "started_again"

RequestRestart ==
  /\ phase = "started_again"
  /\ current_child # NoChild
  /\ phase' = "restart_requested"
  /\ current_child' = current_child
  /\ next_child' = next_child
  /\ last_start_result' = last_start_result
  /\ lookup_result' = lookup_result
  /\ which_children_view' = which_children_view
  /\ last_replaced_ref' = current_child

FinishRestart ==
  /\ phase = "restart_requested"
  /\ last_replaced_ref # NoChild
  /\ SpawnedAfter(current_child, next_child)
  /\ phase' = "restarted"

RequestDelete ==
  /\ phase = "restarted"
  /\ current_child # NoChild
  /\ phase' = "delete_requested"
  /\ UNCHANGED <<current_child, next_child, last_start_result,
                 lookup_result, which_children_view, last_replaced_ref>>

FinishDelete ==
  /\ phase = "delete_requested"
  /\ phase' = "deleted"
  /\ current_child' = NoChild
  /\ next_child' = next_child
  /\ last_start_result' = last_start_result
  /\ lookup_result' = lookup_result
  /\ which_children_view' = which_children_view
  /\ last_replaced_ref' = last_replaced_ref

ObserveWhichDeleted ==
  /\ phase = "deleted"
  /\ phase' = "which_deleted"
  /\ current_child' = current_child
  /\ next_child' = next_child
  /\ last_start_result' = last_start_result
  /\ lookup_result' = lookup_result
  /\ which_children_view' = <<ChildId>>
  /\ last_replaced_ref' = last_replaced_ref

StartChildAfterDelete ==
  /\ phase = "which_deleted"
  /\ SpawnedAfter(current_child, next_child)
  /\ phase' = "started_after_delete"

AdminNext ==
  \/ StartChildFirst
  \/ StartChildReuse
  \/ LookupRunning
  \/ ObserveWhichRunning
  \/ StopChild
  \/ FinishStop
  \/ LookupStopped
  \/ StartChildAgain
  \/ RequestRestart
  \/ FinishRestart
  \/ RequestDelete
  \/ FinishDelete
  \/ ObserveWhichDeleted
  \/ StartChildAfterDelete

AdminSpec ==
  AdminInit /\ [][AdminNext]_admin_vars

TypeOK ==
  /\ phase \in AdminPhases
  /\ current_child \in {ChildA, ChildB, NoChild}
  /\ next_child \in {ChildA, ChildB}
  /\ last_start_result \in {ChildA, ChildB, NoChild}
  /\ lookup_result \in {ChildA, ChildB, NoChild}
  /\ which_children_view \in {<<>>, <<ChildId>>}
  /\ last_replaced_ref \in {ChildA, ChildB, NoChild}

StartChildIsIdempotent ==
  phase \in {"reused_start", "lookup_running", "which_running", "stop_requested"} =>
    /\ current_child = ChildA
    /\ last_start_result = ChildA

ChildRefMatchesRunningChild ==
  /\ phase = "lookup_running" => lookup_result = current_child
  /\ phase = "lookup_stopped" => lookup_result = NoChild

WhichChildrenReportsSpecs ==
  phase \in {"which_running", "which_deleted", "started_after_delete"} =>
    which_children_view = <<ChildId>>

RestartProducesFreshChildRef ==
  phase \in {"restarted", "delete_requested", "deleted", "which_deleted", "started_after_delete"} =>
    last_replaced_ref = ChildB

RestartCompletesWithReplacement ==
  phase \in {"restarted", "delete_requested"} =>
    /\ current_child = ChildA
    /\ current_child # last_replaced_ref

DeleteDoesNotRemoveSpec ==
  phase \in {"which_deleted", "started_after_delete"} =>
    which_children_view = <<ChildId>>

StartAfterDeleteRespawnsChild ==
  phase = "started_after_delete" =>
    /\ current_child = ChildB
    /\ current_child # NoChild

=============================================================================