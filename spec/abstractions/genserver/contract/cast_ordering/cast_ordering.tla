---- MODULE cast_ordering ----
EXTENDS actor_system

CONSTANTS Caller, Server

ASSUME /\ Caller \in ActorPool
       /\ Server \in ActorPool
       /\ Caller # Server

(***************************************************************************)
(* Cast message                                                            *)
(***************************************************************************)
CastMsg(payload) ==
  [kind |-> "Cast", payload |-> payload]

(***************************************************************************)
(* GenServer state                                                         *)
(***************************************************************************)
VARIABLES caller_state, cast_queue, server_history, sent_count

gs_vars == <<caller_state, cast_queue, server_history, sent_count>>
all_vars == <<vars, gs_vars>>

(***************************************************************************)
(* Initial state                                                            *)
(***************************************************************************)
GenServerInit ==
  /\ live = {Caller, Server}
  /\ kind = [a \in ActorPool |-> "none"]
  /\ pc = [a \in ActorPool |-> "absent"]
  /\ ready = {Caller, Server}
  /\ mailboxes = [a \in ActorPool |-> <<>>]
  /\ pending_result = [a \in ActorPool |-> NoPending]
  /\ timers = [a \in ActorPool |-> NoDeadline]
  /\ observations = [a \in ActorPool |-> <<>>]
  /\ msg_state = [id \in MessageIds |-> "unused"]
  /\ time = 0
  /\ links = {}
  /\ monitors = {}
  /\ exit_reason = [a \in ActorPool |-> "none"]
  /\ caller_state = "sending"
  /\ cast_queue = <<>>
  /\ server_history = <<>>
  /\ sent_count = 0

(***************************************************************************)
(* Actions                                                                 *)
(***************************************************************************)

CallerSendCast ==
  /\ caller_state = "sending"
  /\ sent_count < 2
  /\ cast_queue' = Append(cast_queue, CastMsg(sent_count + 1))
  /\ sent_count' = sent_count + 1
  /\ IF sent_count' = 2 THEN caller_state' = "done" ELSE caller_state' = "sending"
  /\ UNCHANGED <<server_history, vars>>

ServerProcessCast ==
  /\ Len(cast_queue) > 0
  /\ server_history' = Append(server_history, cast_queue[1].payload)
  /\ cast_queue' = Tail(cast_queue)
  /\ UNCHANGED <<caller_state, sent_count, vars>>

GenServerNext ==
  \/ CallerSendCast
  \/ ServerProcessCast

GenServerSpec ==
  GenServerInit /\ [][GenServerNext]_all_vars /\ WF_all_vars(GenServerNext)

(***************************************************************************)
(* Properties to Check                                                     *)
(***************************************************************************)

ValidHistory(h) ==
  \/ h = <<>>
  \/ h = <<1>>
  \/ h = <<1, 2>>

Safety ==
  ValidHistory(server_history)

Liveness ==
  <>[](server_history = <<1, 2>> /\ caller_state = "done")

=============================================================================
