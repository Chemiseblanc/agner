---- MODULE core_system ----
(***************************************************************************)
(* Core System Full Exploration                                            *)
(*                                                                         *)
(* This module enables exhaustive exploration of the actor system with     *)
(* nondeterministic spawning, sending, and scheduling. It verifies that    *)
(* all core invariants hold across all reachable states.                   *)
(***************************************************************************)
EXTENDS actor_system

(***************************************************************************)
(* Specification                                                            *)
(***************************************************************************)
CoreSystemSpec ==
  Init /\ [][Next]_vars

====
