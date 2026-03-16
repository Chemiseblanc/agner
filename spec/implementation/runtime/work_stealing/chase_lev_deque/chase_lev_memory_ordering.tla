---- MODULE chase_lev_memory_ordering ----
(***************************************************************************)
(* Chase-Lev Memory Ordering                                               *)
(*                                                                         *)
(* Bounded publication model for the key memory-ordering obligations in    *)
(* the Chase-Lev deque implementation.                                     *)
(*                                                                         *)
(* The model abstracts the C++ atomics into explicit publication steps:    *)
(* - push_write / push_publish model write-before-release publication       *)
(* - grow_prepare / grow_publish model copy-before-buffer publication       *)
(* - last-item pop/steal actions model the seq_cst race arbitration        *)
(***************************************************************************)
EXTENDS Naturals, Sequences, FiniteSets, TLC

CONSTANTS Items, InitCapacity, MaxCapacity

ASSUME /\ InitCapacity = 2
       /\ MaxCapacity = 4
       /\ Cardinality(Items) = 3

NoItem == "none"
NoBuffer == "none"
NoResult == "no_result"

Buffers == {"buf0", "buf1"}
MaxIndex == MaxCapacity - 1

ActionKinds == {
  "idle",
  "push_write",
  "push_publish",
  "grow_prepare",
  "grow_publish",
  "pop",
  "steal",
  "pop_last_owner",
  "pop_last_lost",
  "steal_last_owner",
  "steal_last_lost",
  "pop_empty",
  "steal_empty"
}

VARIABLES logical, capacity, top, published_bottom,
          active_buffer, prepared_buffer, slots,
          pending_push_item, pending_push_index,
          available, popped, stolen,
          last_action, last_result

vars ==
  <<logical, capacity, top, published_bottom,
    active_buffer, prepared_buffer, slots,
    pending_push_item, pending_push_index,
    available, popped, stolen,
    last_action, last_result>>

SeqSet(seq) == {seq[i] : i \in 1..Len(seq)}

Claimed == popped \cup stolen

OtherBuffer(buf) == IF buf = "buf0" THEN "buf1" ELSE "buf0"

EmptySlots == [i \in 0..MaxIndex |-> NoItem]

SlotAt(buffer, idx) == slots[buffer][idx]

BufferMatchesLogical(buffer) ==
  /\ \A k \in 1..Len(logical) :
       SlotAt(buffer, top + k - 1) = logical[k]
  /\ \A idx \in 0..MaxIndex :
       idx < top \/ idx >= published_bottom \/ idx \in {} => TRUE

VisibleIndices ==
  IF published_bottom = top THEN {}
  ELSE top..(published_bottom - 1)

PreparedSlots ==
  [idx \in 0..MaxIndex |->
    IF idx \in VisibleIndices
      THEN SlotAt(active_buffer, idx)
      ELSE NoItem]

Init ==
  /\ logical = <<>>
  /\ capacity = InitCapacity
  /\ top = 0
  /\ published_bottom = 0
  /\ active_buffer = "buf0"
  /\ prepared_buffer = NoBuffer
  /\ slots = [buf \in Buffers |-> EmptySlots]
  /\ pending_push_item = NoItem
  /\ pending_push_index = 0
  /\ available = Items
  /\ popped = {}
  /\ stolen = {}
  /\ last_action = "idle"
  /\ last_result = NoResult

Stable ==
  /\ pending_push_item = NoItem
  /\ prepared_buffer = NoBuffer

PushWrite(item) ==
  /\ Stable
  /\ item \in available
  /\ Len(logical) < capacity
  /\ slots' = [slots EXCEPT ![active_buffer][published_bottom] = item]
  /\ pending_push_item' = item
  /\ pending_push_index' = published_bottom
  /\ UNCHANGED <<logical, capacity, top, published_bottom,
                 active_buffer, prepared_buffer,
                 available, popped, stolen>>
  /\ last_action' = "push_write"
  /\ last_result' = NoResult

PushPublish ==
  /\ pending_push_item # NoItem
  /\ logical' = Append(logical, pending_push_item)
  /\ published_bottom' = published_bottom + 1
  /\ available' = available \ {pending_push_item}
  /\ pending_push_item' = NoItem
  /\ pending_push_index' = 0
  /\ UNCHANGED <<capacity, top, active_buffer, prepared_buffer,
                 slots, popped, stolen>>
  /\ last_action' = "push_publish"
  /\ last_result' = NoResult

GrowPrepare ==
  /\ Stable
  /\ Len(logical) = capacity
  /\ capacity < MaxCapacity
  /\ prepared_buffer' = OtherBuffer(active_buffer)
  /\ slots' = [slots EXCEPT ![OtherBuffer(active_buffer)] = PreparedSlots]
  /\ UNCHANGED <<logical, capacity, top, published_bottom,
                 active_buffer, pending_push_item, pending_push_index,
                 available, popped, stolen>>
  /\ last_action' = "grow_prepare"
  /\ last_result' = NoResult

GrowPublish ==
  /\ prepared_buffer # NoBuffer
  /\ active_buffer' = prepared_buffer
  /\ capacity' = capacity * 2
  /\ prepared_buffer' = NoBuffer
  /\ UNCHANGED <<logical, top, published_bottom, slots,
                 pending_push_item, pending_push_index,
                 available, popped, stolen>>
  /\ last_action' = "grow_publish"
  /\ last_result' = NoResult

PopEmpty ==
  /\ Stable
  /\ Len(logical) = 0
  /\ UNCHANGED <<logical, capacity, top, published_bottom,
                 active_buffer, prepared_buffer, slots,
                 pending_push_item, pending_push_index,
                 available, popped, stolen>>
  /\ last_action' = "pop_empty"
  /\ last_result' = NoResult

StealEmpty ==
  /\ Stable
  /\ Len(logical) = 0
  /\ UNCHANGED <<logical, capacity, top, published_bottom,
                 active_buffer, prepared_buffer, slots,
                 pending_push_item, pending_push_index,
                 available, popped, stolen>>
  /\ last_action' = "steal_empty"
  /\ last_result' = NoResult

PopMany ==
  /\ Stable
  /\ Len(logical) >= 2
  /\ LET item == logical[Len(logical)] IN
       /\ logical' = SubSeq(logical, 1, Len(logical) - 1)
       /\ published_bottom' = published_bottom - 1
       /\ popped' = popped \cup {item}
       /\ UNCHANGED <<capacity, top, active_buffer, prepared_buffer, slots,
                      pending_push_item, pending_push_index,
                      available, stolen>>
       /\ last_action' = "pop"
       /\ last_result' = item

StealMany ==
  /\ Stable
  /\ Len(logical) >= 2
  /\ LET item == logical[1] IN
       /\ logical' = SubSeq(logical, 2, Len(logical))
       /\ top' = top + 1
       /\ stolen' = stolen \cup {item}
       /\ UNCHANGED <<capacity, published_bottom, active_buffer,
                      prepared_buffer, slots,
                      pending_push_item, pending_push_index,
                      available, popped>>
       /\ last_action' = "steal"
       /\ last_result' = item

PopLastOwnerWins ==
  /\ Stable
  /\ Len(logical) = 1
  /\ LET item == logical[1] IN
       /\ logical' = <<>>
       /\ published_bottom' = top
       /\ popped' = popped \cup {item}
       /\ UNCHANGED <<capacity, top, active_buffer, prepared_buffer, slots,
                      pending_push_item, pending_push_index,
                      available, stolen>>
       /\ last_action' = "pop_last_owner"
       /\ last_result' = item

PopLastLosesToSteal ==
  /\ Stable
  /\ Len(logical) = 1
  /\ LET item == logical[1] IN
       /\ logical' = <<>>
       /\ top' = top + 1
       /\ stolen' = stolen \cup {item}
       /\ UNCHANGED <<capacity, published_bottom, active_buffer,
                      prepared_buffer, slots,
                      pending_push_item, pending_push_index,
                      available, popped>>
       /\ last_action' = "pop_last_lost"
       /\ last_result' = NoResult

StealLastWins ==
  /\ Stable
  /\ Len(logical) = 1
  /\ LET item == logical[1] IN
       /\ logical' = <<>>
       /\ top' = top + 1
       /\ stolen' = stolen \cup {item}
       /\ UNCHANGED <<capacity, published_bottom, active_buffer,
                      prepared_buffer, slots,
                      pending_push_item, pending_push_index,
                      available, popped>>
       /\ last_action' = "steal_last_owner"
       /\ last_result' = item

StealLastLosesToPop ==
  /\ Stable
  /\ Len(logical) = 1
  /\ LET item == logical[1] IN
       /\ logical' = <<>>
       /\ published_bottom' = top
       /\ popped' = popped \cup {item}
       /\ UNCHANGED <<capacity, top, active_buffer, prepared_buffer, slots,
                      pending_push_item, pending_push_index,
                      available, stolen>>
       /\ last_action' = "steal_last_lost"
       /\ last_result' = NoResult

Next ==
  \/ \E item \in available : PushWrite(item)
  \/ PushPublish
  \/ GrowPrepare
  \/ GrowPublish
  \/ PopEmpty
  \/ StealEmpty
  \/ PopMany
  \/ StealMany
  \/ PopLastOwnerWins
  \/ PopLastLosesToSteal
  \/ StealLastWins
  \/ StealLastLosesToPop

DequeMemorySpec ==
  Init /\ [][Next]_vars

TypeOK ==
  /\ logical \in Seq(Items)
  /\ capacity \in {InitCapacity, MaxCapacity}
  /\ top \in 0..MaxCapacity
  /\ published_bottom \in 0..MaxCapacity
  /\ active_buffer \in Buffers
  /\ prepared_buffer \in Buffers \cup {NoBuffer}
  /\ slots \in [Buffers -> [0..MaxIndex -> Items \cup {NoItem}]]
  /\ pending_push_item \in Items \cup {NoItem}
  /\ pending_push_index \in 0..MaxCapacity
  /\ available \subseteq Items
  /\ popped \subseteq Items
  /\ stolen \subseteq Items
  /\ last_action \in ActionKinds
  /\ last_result \in Items \cup {NoResult}

PublishedBounds ==
  /\ top <= published_bottom
  /\ published_bottom = top + Len(logical)
  /\ Len(logical) <= capacity

ActiveBufferMatchesLogical ==
  BufferMatchesLogical(active_buffer)

PendingPushWrittenButUnpublished ==
  pending_push_item # NoItem =>
    /\ pending_push_index = published_bottom
    /\ SlotAt(active_buffer, pending_push_index) = pending_push_item
    /\ pending_push_item \notin SeqSet(logical)
    /\ pending_push_item \notin Claimed

PreparedGrowthMatchesLogical ==
  prepared_buffer # NoBuffer =>
    BufferMatchesLogical(prepared_buffer)

AccountingPreserved ==
  /\ available \intersect SeqSet(logical) = {}
  /\ available \intersect Claimed = {}
  /\ SeqSet(logical) \intersect Claimed = {}
  /\ available \cup SeqSet(logical) \cup popped \cup stolen = Items

LastItemRaceLeavesSingleWinner ==
  last_action \in {"pop_last_owner", "pop_last_lost", "steal_last_owner", "steal_last_lost"} =>
    /\ logical = <<>>
    /\ Cardinality(Claimed) >= 1
    /\ popped \intersect stolen = {}

=============================================================================