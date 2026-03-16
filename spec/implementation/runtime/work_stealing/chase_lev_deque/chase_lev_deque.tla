---- MODULE chase_lev_deque ----
(***************************************************************************)
(* Chase-Lev Deque                                                         *)
(*                                                                         *)
(* Bounded implementation evidence for the single-owner / multi-stealer    *)
(* deque shape used by MtScheduler.                                        *)
(*                                                                         *)
(* The model focuses on scheduler-visible safety properties:               *)
(* - push/pop/steal preserve item accounting                               *)
(* - growth preserves logical contents                                     *)
(* - the single-item pop-vs-steal race has exactly one winner              *)
(***************************************************************************)
EXTENDS Naturals, Sequences, FiniteSets, TLC

CONSTANTS Items, InitCapacity, MaxCapacity

ASSUME /\ InitCapacity = 2
       /\ MaxCapacity = 4
       /\ Cardinality(Items) = 3

NoItem == "none"
NoResult == "no_result"

ActionKinds == {
  "idle",
  "push",
  "pop",
  "steal",
  "pop_last_owner",
  "pop_last_lost",
  "steal_last_owner",
  "steal_last_lost",
  "grow",
  "pop_empty",
  "steal_empty"
}

VARIABLES deque, capacity, available, popped, stolen, last_action, last_result

vars == <<deque, capacity, available, popped, stolen, last_action, last_result>>

SeqSet(seq) == {seq[i] : i \in 1..Len(seq)}

Claimed == popped \cup stolen

Init ==
  /\ deque = <<>>
  /\ capacity = InitCapacity
  /\ available = Items
  /\ popped = {}
  /\ stolen = {}
  /\ last_action = "idle"
  /\ last_result = NoResult

Push(item) ==
  /\ item \in available
  /\ IF Len(deque) = capacity
        THEN /\ capacity < MaxCapacity
             /\ capacity' = capacity * 2
             /\ last_action' = "grow"
        ELSE /\ capacity' = capacity
             /\ last_action' = "push"
  /\ deque' = Append(deque, item)
  /\ available' = available \ {item}
  /\ popped' = popped
  /\ stolen' = stolen
  /\ last_result' = NoResult

PopEmpty ==
  /\ Len(deque) = 0
  /\ UNCHANGED <<deque, capacity, available, popped, stolen>>
  /\ last_action' = "pop_empty"
  /\ last_result' = NoResult

StealEmpty ==
  /\ Len(deque) = 0
  /\ UNCHANGED <<deque, capacity, available, popped, stolen>>
  /\ last_action' = "steal_empty"
  /\ last_result' = NoResult

PopMany ==
  /\ Len(deque) >= 2
  /\ LET item == deque[Len(deque)] IN
       /\ deque' = SubSeq(deque, 1, Len(deque) - 1)
       /\ capacity' = capacity
       /\ available' = available
       /\ popped' = popped \cup {item}
       /\ stolen' = stolen
       /\ last_action' = "pop"
       /\ last_result' = item

StealMany ==
  /\ Len(deque) >= 2
  /\ LET item == deque[1] IN
       /\ deque' = SubSeq(deque, 2, Len(deque))
       /\ capacity' = capacity
       /\ available' = available
       /\ popped' = popped
       /\ stolen' = stolen \cup {item}
       /\ last_action' = "steal"
       /\ last_result' = item

PopLastOwnerWins ==
  /\ Len(deque) = 1
  /\ LET item == deque[1] IN
       /\ deque' = <<>>
       /\ capacity' = capacity
       /\ available' = available
       /\ popped' = popped \cup {item}
       /\ stolen' = stolen
       /\ last_action' = "pop_last_owner"
       /\ last_result' = item

PopLastLosesToSteal ==
  /\ Len(deque) = 1
  /\ LET item == deque[1] IN
       /\ deque' = <<>>
       /\ capacity' = capacity
       /\ available' = available
       /\ popped' = popped
       /\ stolen' = stolen \cup {item}
       /\ last_action' = "pop_last_lost"
       /\ last_result' = NoResult

StealLastWins ==
  /\ Len(deque) = 1
  /\ LET item == deque[1] IN
       /\ deque' = <<>>
       /\ capacity' = capacity
       /\ available' = available
       /\ popped' = popped
       /\ stolen' = stolen \cup {item}
       /\ last_action' = "steal_last_owner"
       /\ last_result' = item

StealLastLosesToPop ==
  /\ Len(deque) = 1
  /\ LET item == deque[1] IN
       /\ deque' = <<>>
       /\ capacity' = capacity
       /\ available' = available
       /\ popped' = popped \cup {item}
       /\ stolen' = stolen
       /\ last_action' = "steal_last_lost"
       /\ last_result' = NoResult

Next ==
  \/ \E item \in available : Push(item)
  \/ PopEmpty
  \/ StealEmpty
  \/ PopMany
  \/ StealMany
  \/ PopLastOwnerWins
  \/ PopLastLosesToSteal
  \/ StealLastWins
  \/ StealLastLosesToPop

DequeSpec ==
  Init /\ [][Next]_vars

TypeOK ==
  /\ deque \in Seq(Items)
  /\ capacity \in {InitCapacity, MaxCapacity}
  /\ available \subseteq Items
  /\ popped \subseteq Items
  /\ stolen \subseteq Items
  /\ last_action \in ActionKinds
  /\ last_result \in Items \cup {NoResult}

CapacityDiscipline ==
  /\ Len(deque) <= capacity
  /\ capacity = InitCapacity \/ capacity = MaxCapacity

ClaimedSetsDisjoint ==
  popped \intersect stolen = {}

AccountingPreserved ==
  /\ available \intersect SeqSet(deque) = {}
  /\ available \intersect Claimed = {}
  /\ SeqSet(deque) \intersect Claimed = {}
  /\ available \cup SeqSet(deque) \cup popped \cup stolen = Items

LogicalContentsUnique ==
  /\ Len(deque) = Cardinality(SeqSet(deque))
  /\ Claimed = Items \ (available \cup SeqSet(deque))

LastItemRaceLeavesSingleWinner ==
  last_action \in {"pop_last_owner", "pop_last_lost", "steal_last_owner", "steal_last_lost"} =>
    /\ deque = <<>>
    /\ Cardinality(Claimed) >= 1
    /\ popped \intersect stolen = {}

=============================================================================