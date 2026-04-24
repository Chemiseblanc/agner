---- MODULE notify_waiter_multi ----
EXTENDS Naturals, Sequences, FiniteSets, TLC

(***************************************************************************)
(* Source mapping                                                           *)
(*                                                                         *)
(* This module models the notify_waiter one-shot resume guarantee when      *)
(* multiple senders send to the same blocked receiver concurrently.        *)
(*                                                                         *)
(* - Send        -> SchedulerBase::send() + Actor::enqueue_message() +     *)
(*                  Actor::notify_waiter()                                 *)
(* - Receive     -> Actor::receive() / ReceiveAwaiter                      *)
(* - TryReceive  -> Actor::try_receive() / ReceiveAwaiter with timeout     *)
(* - NotifyWaiter-> Actor::notify_waiter() — checks pending_, calls        *)
(*                  try_match(), schedules handle if match                 *)
(* - TimeoutFire -> schedule_after callback — checks active_ flag          *)
(* - ResumeActor -> Scheduler::run() resuming the coroutine handle         *)
(*                                                                         *)
(* The key property: when an actor is blocked on receive/try_receive and    *)
(* multiple messages arrive (from different senders or the scheduler fires  *)
(* a timeout), the actor resumes EXACTLY ONCE and processes EXACTLY ONE     *)
(* result (either one matching message or the timeout token, never both,   *)
(* never more than one message per receive).                               *)
(*                                                                         *)
(* Deliberate abstractions:                                                 *)
(* - All messages match the receiver's pattern (worst-case contention).    *)
(* - Each sender sends exactly once.                                       *)
(* - Only one receive cycle is modeled.                                    *)
(* - The scheduler is single-threaded, so enqueue_message + notify_waiter  *)
(*   execute atomically within the Send action.                            *)
(***************************************************************************)

CONSTANTS Senders, TimeoutDelay, UseTimeout

ASSUME /\ Cardinality(Senders) >= 1
       /\ TimeoutDelay \in Nat
       /\ TimeoutDelay > 0
       /\ UseTimeout \in BOOLEAN

(***************************************************************************)
(* Sentinel values and message constructors                                *)
(***************************************************************************)

NoPending == [tag |-> "NoPending"]
TimeoutToken == [tag |-> "Timeout"]
NoResult == [tag |-> "NoResult"]
NoTimer == 0 - 1

Msg(s) == [tag |-> "Message", sender |-> s]

MessageSet == {Msg(s) : s \in Senders}
PendingValues == MessageSet \cup {NoPending, TimeoutToken}
ResultValues == MessageSet \cup {NoResult, TimeoutToken}
ReceiverStates == {"idle", "blocked", "ready", "processing", "done"}

MaxTime == (Cardinality(Senders) + 1) * TimeoutDelay
TimeValues == 0..MaxTime
TimerValues == TimeValues \cup {NoTimer}

(***************************************************************************)
(* Variables                                                               *)
(*                                                                         *)
(* receiver_state   — lifecycle of the single receive operation            *)
(*   idle:       not yet called receive                                    *)
(*   blocked:    suspended on receive, pending_ is set, active_ is true    *)
(*   ready:      scheduled for resumption (one result determined)          *)
(*   processing: running the continuation                                  *)
(*   done:       receive completed                                         *)
(* receiver_mailbox — messages queued but not yet consumed                  *)
(* pending_result   — the pre-matched result from notify_waiter            *)
(* active           — one-shot gate modelling active_ in ReceiveAwaiter    *)
(* timer            — timeout deadline or NoTimer                          *)
(* resumed_count    — times receiver was scheduled for resume (must be ≤1) *)
(* result           — final result the receiver processed                  *)
(* sender_sent      — whether each sender has sent its message             *)
(* time             — logical clock                                        *)
(***************************************************************************)

VARIABLES receiver_state, receiver_mailbox, pending_result, active,
          timer, resumed_count, result, sender_sent, time

vars == <<receiver_state, receiver_mailbox, pending_result, active,
          timer, resumed_count, result, sender_sent, time>>

(***************************************************************************)
(* Helpers                                                                 *)
(***************************************************************************)

HasMatch(box) == Len(box) > 0

FirstMatch(box) == Head(box)

RemoveFirstMatch(box) == Tail(box)

(***************************************************************************)
(* Type invariant                                                          *)
(***************************************************************************)

TypeOK ==
  /\ receiver_state \in ReceiverStates
  /\ receiver_mailbox \in Seq(MessageSet)
  /\ pending_result \in PendingValues
  /\ active \in BOOLEAN
  /\ timer \in TimerValues
  /\ resumed_count \in Nat
  /\ result \in ResultValues
  /\ sender_sent \in [Senders -> BOOLEAN]
  /\ time \in TimeValues

(***************************************************************************)
(* Safety invariants                                                       *)
(***************************************************************************)

\* THE KEY PROPERTY: receiver is never double-resumed.
AtMostOneResume ==
  resumed_count <= 1

\* When receive completes, exactly one result was processed.
ExactlyOneResult ==
  receiver_state = "done" =>
    /\ result \in MessageSet \cup {TimeoutToken}
    /\ result # NoResult

\* If the receiver is ready, some path must have claimed the gate.
ActiveGateConsistency ==
  receiver_state = "ready" => pending_result # NoPending

\* resumed_count only increments via the blocked -> ready transition.
NoResumeWhenNotBlocked ==
  resumed_count > 0 => receiver_state \in {"ready", "done"}

\* While blocked with no pending result, the active gate is open.
BlockedImpliesActive ==
  receiver_state = "blocked" /\ pending_result = NoPending => active = TRUE

\* In a no-timeout configuration the result is always a message.
NoTimeoutResultIsMessage ==
  ~UseTimeout /\ receiver_state = "done" => result \in MessageSet

(***************************************************************************)
(* Initial state                                                           *)
(***************************************************************************)

Init ==
  /\ receiver_state = "idle"
  /\ receiver_mailbox = <<>>
  /\ pending_result = NoPending
  /\ active = FALSE
  /\ timer = NoTimer
  /\ resumed_count = 0
  /\ result = NoResult
  /\ sender_sent = [s \in Senders |-> FALSE]
  /\ time = 0

(***************************************************************************)
(* Actions                                                                 *)
(***************************************************************************)

(* ReceiverBlock: receiver suspends on receive/try_receive.
   Precondition: no matching message in mailbox (otherwise await_ready
   would have returned true).  Sets the one-shot gate and arms the timer
   if this is a try_receive.  Models await_suspend(). *)
ReceiverBlock ==
  /\ receiver_state = "idle"
  /\ ~HasMatch(receiver_mailbox)
  /\ receiver_state' = "blocked"
  /\ active' = TRUE
  /\ timer' = IF UseTimeout THEN time + TimeoutDelay ELSE NoTimer
  /\ UNCHANGED <<receiver_mailbox, pending_result, resumed_count, result,
                  sender_sent, time>>

(* ReceiverImmediateMatch: receiver's await_ready() finds a matching
   message already in the mailbox.  No suspension, no resume. *)
ReceiverImmediateMatch ==
  /\ receiver_state = "idle"
  /\ HasMatch(receiver_mailbox)
  /\ LET msg == FirstMatch(receiver_mailbox) IN
     /\ result' = msg
     /\ receiver_mailbox' = RemoveFirstMatch(receiver_mailbox)
  /\ receiver_state' = "done"
  /\ UNCHANGED <<pending_result, active, timer, resumed_count,
                  sender_sent, time>>

(* Send(s): sender s sends a message to the receiver.
   The message is enqueued via enqueue_message(), then notify_waiter()
   fires synchronously.  If the receiver is blocked, the gate is open,
   and no result is pending, try_match() succeeds (all messages match in
   this model), the message is consumed from the mailbox, the gate
   closes, and the coroutine handle is scheduled.

   If any of those preconditions fail the message simply stays in the
   mailbox for a future receive. *)
Send(s) ==
  /\ sender_sent[s] = FALSE
  /\ sender_sent' = [sender_sent EXCEPT ![s] = TRUE]
  /\ LET msg == Msg(s) IN
     IF /\ receiver_state = "blocked"
        /\ active = TRUE
        /\ pending_result = NoPending
     THEN \* notify_waiter path: enqueue + try_match + schedule(handle)
          \* Message is enqueued then immediately consumed by try_match,
          \* so the mailbox is unchanged (it was empty while blocked).
          /\ receiver_mailbox' = receiver_mailbox
          /\ pending_result' = msg
          /\ active' = FALSE
          /\ receiver_state' = "ready"
          /\ resumed_count' = resumed_count + 1
          /\ timer' = NoTimer
     ELSE \* Just enqueue; receiver not waiting or gate already closed
          /\ receiver_mailbox' = Append(receiver_mailbox, msg)
          /\ UNCHANGED <<pending_result, active, receiver_state,
                          resumed_count, timer>>
  /\ UNCHANGED <<result, time>>

(* TimeoutFire: the schedule_after callback runs.
   Checks the active_ flag.  If TRUE, claims the one-shot gate, sets
   the timeout token as the pending result, and schedules the receiver
   for resumption.
   If FALSE, the gate was already claimed — stale timer, no-op.
   Note: in this model the FALSE branch is unreachable because the Send
   matching path also clears the timer; it is retained for faithful
   correspondence with the C++ code. *)
TimeoutFire ==
  /\ timer # NoTimer
  /\ time >= timer
  /\ receiver_state = "blocked"
  /\ IF active = TRUE
     THEN /\ active' = FALSE
          /\ pending_result' = TimeoutToken
          /\ receiver_state' = "ready"
          /\ resumed_count' = resumed_count + 1
          /\ timer' = NoTimer
     ELSE /\ timer' = NoTimer
          /\ UNCHANGED <<active, pending_result, receiver_state,
                          resumed_count>>
  /\ UNCHANGED <<receiver_mailbox, result, sender_sent, time>>

(* ResumeReceiver: scheduler runs the coroutine handle.
   The pending result becomes the final result and the receive cycle
   completes. *)
ResumeReceiver ==
  /\ receiver_state = "ready"
  /\ pending_result # NoPending
  /\ result' = pending_result
  /\ pending_result' = NoPending
  /\ receiver_state' = "done"
  /\ UNCHANGED <<receiver_mailbox, active, timer, resumed_count,
                  sender_sent, time>>

(* AdvanceTime: advance logical clock to the pending timer deadline. *)
AdvanceTime ==
  /\ timer # NoTimer
  /\ time < timer
  /\ time' = timer
  /\ UNCHANGED <<receiver_state, receiver_mailbox, pending_result, active,
                  timer, resumed_count, result, sender_sent>>

(***************************************************************************)
(* Next-state relation and specification                                   *)
(***************************************************************************)

Next ==
  \/ ReceiverBlock
  \/ ReceiverImmediateMatch
  \/ \E s \in Senders : Send(s)
  \/ TimeoutFire
  \/ ResumeReceiver
  \/ AdvanceTime

Spec == Init /\ [][Next]_vars

(***************************************************************************)
(* Focused scenarios                                                       *)
(*                                                                         *)
(* 1. TwoSenderRace — 2 senders, no timeout.  Receiver blocks, both send  *)
(*    in interleaved order.  Exactly one message processed,                *)
(*    resumed_count = 1.                                                   *)
(*                                                                         *)
(* 2. ThreeSenderRace — 3 senders, no timeout.  Same property.            *)
(*                                                                         *)
(* 3. SenderVsTimeout — 1 sender + timeout.  Either the message or the    *)
(*    timeout token is the result, never both.                             *)
(*                                                                         *)
(* 4. MultipleSendersVsTimeout — 2 senders + timeout.  Exactly one of     *)
(*    {message, timeout} is the result, resumed_count = 1.                 *)
(*                                                                         *)
(* 5. AllSendBeforeBlock — all senders send before the receiver calls      *)
(*    receive.  Receiver matches in await_ready, resumed_count = 0.        *)
(*                                                                         *)
(* All scenarios are exercised by the general Spec with the appropriate    *)
(* CONSTANTS in each .cfg file.  The invariants AtMostOneResume and        *)
(* ExactlyOneResult are the key properties verified across every           *)
(* interleaving.                                                           *)
(***************************************************************************)

=============================================================================
