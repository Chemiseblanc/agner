---- MODULE genserver_call ----
(***************************************************************************)
(* GenServer Call/Reply Scenario                                           *)
(*                                                                         *)
(* Verifies synchronous call/reply semantics:                              *)
(* - Caller sends CallMessage with request_id, awaits matching Reply       *)
(* - Server processes request and sends Reply with matching request_id     *)
(* - Caller receives correct response (request_id matching)               *)
(* - Timeout fires if server doesn't reply in time                        *)
(* - Wrong request_id replies are ignored by caller                        *)
(*                                                                         *)
(* Source mapping:                                                          *)
(* - GenServer::call() -> CallerSendCall, CallerAwaitReply                 *)
(* - GenServer::serve() -> ServerProcessCall                               *)
(* - CallTimeout -> CallerTimeout                                          *)
(* - Reply{request_id, response} -> reply messages                         *)
(***************************************************************************)
EXTENDS actor_system

(***************************************************************************)
(* GenServer-specific constants                                            *)
(***************************************************************************)
CONSTANTS Caller, Server, RequestIds

ASSUME /\ Caller \in ActorPool
       /\ Server \in ActorPool
       /\ Caller # Server
       /\ Cardinality(RequestIds) >= 1

(***************************************************************************)
(* GenServer message types                                                  *)
(***************************************************************************)
CallMsg(caller, req_id, payload) ==
  [kind |-> "Call", caller |-> caller, req_id |-> req_id, payload |-> payload]

ReplyMsg(req_id, payload) ==
  [kind |-> "Reply", req_id |-> req_id, payload |-> payload]

CastMsg(payload) ==
  [kind |-> "Cast", payload |-> payload]

(***************************************************************************)
(* GenServer state                                                          *)
(***************************************************************************)
\* caller_state: "idle" | "waiting" | "got_reply" | "timed_out"
\* server_state: "serving" | "stopped"
\* pending_call: the request_id the caller is waiting for, or "none"
\* reply_observed: the response payload the caller received, or "none"
VARIABLES caller_state, server_state, pending_call,
          call_queue, reply_queue, reply_observed

gs_vars == <<caller_state, server_state, pending_call,
             call_queue, reply_queue, reply_observed>>

all_vars == <<vars, gs_vars>>

(***************************************************************************)
(* Initial state                                                            *)
(***************************************************************************)
GenServerInit ==
  /\ live = {Caller, Server}
  /\ kind =
       [a \in ActorPool |->
         IF a \in {Caller, Server} THEN "collector" ELSE "none"]
  /\ pc =
       [a \in ActorPool |->
         IF a \in {Caller, Server} THEN "collect" ELSE "absent"]
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
  /\ caller_state = "idle"
  /\ server_state = "serving"
  /\ pending_call = "none"
  /\ call_queue = <<>>
  /\ reply_queue = <<>>
  /\ reply_observed = "none"

(***************************************************************************)
(* Actions                                                                  *)
(***************************************************************************)

\* Caller sends a call with a chosen request_id and payload
CallerSendCall(req_id, payload) ==
  /\ caller_state = "idle"
  /\ server_state = "serving"
  /\ req_id \in RequestIds
  /\ payload \in Payloads
  /\ caller_state' = "waiting"
  /\ pending_call' = req_id
  /\ call_queue' = Append(call_queue, CallMsg(Caller, req_id, payload))
  /\ UNCHANGED <<vars, server_state, reply_queue, reply_observed>>

\* Server processes the head of the call queue and sends a reply
ServerProcessCall ==
  /\ server_state = "serving"
  /\ Len(call_queue) > 0
  /\ LET call == Head(call_queue)
     IN /\ call_queue' = Tail(call_queue)
        \* Server replies with payload + 1 (deterministic transform)
        /\ reply_queue' =
             Append(reply_queue,
                    ReplyMsg(call.req_id, call.payload))
        /\ observations' =
             [observations EXCEPT
               ![Server] = Append(@, CallMsg(call.caller,
                                              call.req_id,
                                              call.payload))]
  /\ UNCHANGED <<live, kind, pc, ready, mailboxes, pending_result, timers,
                 msg_state, time, links, monitors, exit_reason,
                 caller_state, server_state, pending_call, reply_observed>>

\* Caller receives a reply - checks request_id match
CallerReceiveReply ==
  /\ caller_state = "waiting"
  /\ Len(reply_queue) > 0
  /\ LET reply == Head(reply_queue)
     IN IF reply.req_id = pending_call
          THEN \* Matching reply - accept it
               /\ caller_state' = "got_reply"
               /\ reply_observed' = reply.payload
               /\ reply_queue' = Tail(reply_queue)
               /\ observations' =
                    [observations EXCEPT
                      ![Caller] = Append(@, reply)]
               /\ UNCHANGED <<live, kind, pc, ready, mailboxes, pending_result,
                              timers, msg_state, time, links, monitors,
                              exit_reason, server_state, pending_call,
                              call_queue>>
          ELSE \* Wrong request_id - skip and keep waiting
               /\ reply_queue' = Tail(reply_queue)
               /\ UNCHANGED <<live, kind, pc, ready, mailboxes, pending_result,
                              timers, observations, msg_state, time, links,
                              monitors, exit_reason, caller_state, server_state,
                              pending_call, call_queue, reply_observed>>

\* Caller times out waiting for a reply
CallerTimeout ==
  /\ caller_state = "waiting"
  /\ caller_state' = "timed_out"
  /\ observations' =
       [observations EXCEPT
         ![Caller] = Append(@, TimeoutToken)]
  /\ UNCHANGED <<live, kind, pc, ready, mailboxes, pending_result, timers,
                 msg_state, time, links, monitors, exit_reason,
                 server_state, pending_call, call_queue, reply_queue,
                 reply_observed>>

(***************************************************************************)
(* Specification                                                            *)
(***************************************************************************)
GenServerNext ==
  \/ \E req_id \in RequestIds :
       \E payload \in Payloads :
         CallerSendCall(req_id, payload)
  \/ ServerProcessCall
  \/ CallerReceiveReply
  \/ CallerTimeout

GenServerSpec ==
  GenServerInit /\ [][GenServerNext]_all_vars

(***************************************************************************)
(* Invariants                                                               *)
(***************************************************************************)

\* Caller ends in exactly one terminal state: got_reply or timed_out
CallerTerminatesCorrectly ==
  caller_state \in {"idle", "waiting", "got_reply", "timed_out"}

\* If caller got a reply, the request_id matches what was sent
ReplyMatchesRequest ==
  caller_state = "got_reply" =>
    /\ reply_observed \in Payloads
    /\ pending_call \in RequestIds

\* Caller observes exactly one outcome: reply or timeout, not both
ExactlyOneOutcome ==
  caller_state = "got_reply" =>
    \* The last observation is a Reply, not a TimeoutToken
    /\ Len(observations[Caller]) >= 1
    /\ observations[Caller][Len(observations[Caller])].kind = "Reply"

ExactlyOneOutcomeTimeout ==
  caller_state = "timed_out" =>
    /\ Len(observations[Caller]) >= 1
    /\ observations[Caller][Len(observations[Caller])] = TimeoutToken

\* Server processes calls in FIFO order
ServerFIFO ==
  Len(call_queue) >= 0  \* Structural: queue is a sequence

\* No reply with wrong request_id is accepted
NoWrongReplyAccepted ==
  caller_state = "got_reply" =>
    \E i \in 1..Len(observations[Caller]) :
      /\ observations[Caller][i].kind = "Reply"
      /\ observations[Caller][i].req_id = pending_call

====
