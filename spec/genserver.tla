---- MODULE genserver ----
EXTENDS Naturals, Sequences, FiniteSets, TLC

(***************************************************************************)
(* Source mapping                                                           *)
(*                                                                         *)
(* This module models the GenServer call/reply protocol in Agner's actor   *)
(* framework.                                                               *)
(*                                                                         *)
(* - GenServer::call() (genserver.hpp:93–127)                              *)
(*   -> ClientCall, ClientReceiveCorrectReply, ClientSkipStaleReply,       *)
(*      ClientTimeout                                                       *)
(* - GenServer::cast() (genserver.hpp:135–139)                             *)
(*   -> ClientCast                                                          *)
(* - GenServer::serve() / dispatch_one() (genserver.hpp:147–155)           *)
(*   -> ServerDispatchCall, ServerDispatchCast, ServerIgnoreReply,          *)
(*      ServerExit                                                          *)
(* - CallMessage<Request>{caller, request_id, request}                     *)
(*   (genserver_detail.hpp:125–129) -> CallMsg(caller, rid)                *)
(* - CastMessage<Request>{request}                                         *)
(*   (genserver_detail.hpp:132–134) -> CastMsg(caller)                     *)
(* - Reply{request_id, any response}                                       *)
(*   (genserver_detail.hpp:137–140) -> ReplyMsg(rid)                       *)
(* - next_request_id_ (genserver.hpp:186) -> next_request_id per client    *)
(* - deadline = now() + timeout (genserver.hpp:109–110)                    *)
(*   -> client_pending[c].deadline (absolute, not reset by stale replies)  *)
(*                                                                         *)
(* Deliberate abstractions:                                                 *)
(* - Request/response payloads are omitted; only request_id controls       *)
(*   correlation.                                                           *)
(* - Server processes its mailbox FIFO (Head/Tail on Seq).                 *)
(* - Client scans its reply mailbox FIFO for matching request_id.          *)
(* - ExitSignal terminates the serve() loop.                               *)
(* - DownSignal is omitted (returns false, no observable effect).          *)
(***************************************************************************)

CONSTANTS Clients, MaxRequestId, TimeoutDelay

ASSUME /\ Cardinality(Clients) >= 1
       /\ MaxRequestId \in Nat
       /\ MaxRequestId >= 1
       /\ TimeoutDelay \in Nat
       /\ TimeoutDelay >= 1

RequestIds == 0..(MaxRequestId - 1)

MaxTime == MaxRequestId * TimeoutDelay * 2 + TimeoutDelay

ClientStates == {"idle", "waiting", "got_reply", "timed_out"}

NoPending == [request_id |-> 0 - 1, deadline |-> 0 - 1]

(***************************************************************************)
(* Message constructors                                                     *)
(*                                                                         *)
(* CallMsg models CallMessage<Request>{caller, request_id, request}.       *)
(* CastMsg models CastMessage<Request>{request}.                           *)
(* StaleReplyMsg models Reply{request_id, response} arriving in the        *)
(*   server mailbox (serve loop ignores these).                             *)
(* ReplyMsg models Reply{request_id, response} in a client mailbox.        *)
(***************************************************************************)

CallMsg(caller, rid) ==
  [type |-> "call", caller |-> caller, request_id |-> rid]

CastMsg(caller) ==
  [type |-> "cast", caller |-> caller]

StaleReplyMsg(rid) ==
  [type |-> "reply", request_id |-> rid]

ReplyMsg(rid) ==
  [request_id |-> rid]

ServerMsgUniverse ==
  {CallMsg(c, rid) : c \in Clients, rid \in RequestIds} \cup
  {CastMsg(c) : c \in Clients} \cup
  {StaleReplyMsg(rid) : rid \in RequestIds}

ReplyMsgUniverse ==
  {ReplyMsg(rid) : rid \in RequestIds}

(***************************************************************************)
(* Variables                                                                *)
(*                                                                         *)
(* server_alive     : server is running the serve() loop                   *)
(* server_mailbox   : FIFO of CallMsg / CastMsg / StaleReplyMsg            *)
(* client_state     : per-client call lifecycle state                       *)
(* client_mailbox   : per-client FIFO of Reply messages                    *)
(* next_request_id  : per-client monotonic counter (next_request_id_)      *)
(* client_pending   : per-client pending call info [request_id, deadline]  *)
(* time             : logical clock                                         *)
(* reply_sent       : auxiliary history — set of <<caller, rid>> pairs     *)
(*                    for which the server has sent a reply                 *)
(***************************************************************************)

VARIABLES server_alive, server_mailbox, client_state, client_mailbox,
          next_request_id, client_pending, time, reply_sent

vars ==
  <<server_alive, server_mailbox, client_state, client_mailbox,
    next_request_id, client_pending, time, reply_sent>>

(***************************************************************************)
(* Type invariant                                                           *)
(***************************************************************************)

TypeOK ==
  /\ server_alive \in BOOLEAN
  /\ server_mailbox \in Seq(ServerMsgUniverse)
  /\ client_state \in [Clients -> ClientStates]
  /\ client_mailbox \in [Clients -> Seq(ReplyMsgUniverse)]
  /\ next_request_id \in [Clients -> 0..MaxRequestId]
  /\ \A c \in Clients :
       client_pending[c] = NoPending \/
       (/\ client_pending[c].request_id \in RequestIds
        /\ client_pending[c].deadline \in 0..(MaxTime + TimeoutDelay))
  /\ time \in 0..MaxTime
  /\ reply_sent \subseteq (Clients \times RequestIds)

(***************************************************************************)
(* Initial state                                                            *)
(***************************************************************************)

Init ==
  /\ server_alive = TRUE
  /\ server_mailbox = <<>>
  /\ client_state = [c \in Clients |-> "idle"]
  /\ client_mailbox = [c \in Clients |-> <<>>]
  /\ next_request_id = [c \in Clients |-> 0]
  /\ client_pending = [c \in Clients |-> NoPending]
  /\ time = 0
  /\ reply_sent = {}

(***************************************************************************)
(* Client actions                                                           *)
(***************************************************************************)

\* genserver.hpp:93–110  call() sends CallMessage, computes absolute
\* deadline (now() + timeout), enters the receive loop.
ClientCall(c) ==
  /\ client_state[c] = "idle"
  /\ next_request_id[c] \in RequestIds
  /\ LET rid == next_request_id[c]
         deadline == time + TimeoutDelay
     IN
     /\ client_state' = [client_state EXCEPT ![c] = "waiting"]
     /\ next_request_id' = [next_request_id EXCEPT ![c] = rid + 1]
     /\ client_pending' =
          [client_pending EXCEPT
            ![c] = [request_id |-> rid, deadline |-> deadline]]
     /\ server_mailbox' =
          IF server_alive
            THEN Append(server_mailbox, CallMsg(c, rid))
            ELSE server_mailbox
     /\ UNCHANGED <<server_alive, client_mailbox, time, reply_sent>>

\* genserver.hpp:135–139  cast() sends CastMessage, no response expected.
ClientCast(c) ==
  /\ client_state[c] = "idle"
  /\ server_mailbox' =
       IF server_alive
         THEN Append(server_mailbox, CastMsg(c))
         ELSE server_mailbox
  /\ UNCHANGED <<server_alive, client_state, client_mailbox,
                  next_request_id, client_pending, time, reply_sent>>

\* genserver.hpp:118–121  try_receive returns a Reply whose request_id
\* matches the pending call; caller transitions to got_reply.
\* Only fires when time < deadline (absolute timeout not yet reached).
ClientReceiveCorrectReply(c) ==
  /\ client_state[c] = "waiting"
  /\ time < client_pending[c].deadline
  /\ Len(client_mailbox[c]) > 0
  /\ Head(client_mailbox[c]).request_id = client_pending[c].request_id
  /\ client_state' = [client_state EXCEPT ![c] = "got_reply"]
  /\ client_mailbox' = [client_mailbox EXCEPT ![c] = Tail(@)]
  /\ UNCHANGED <<server_alive, server_mailbox, next_request_id,
                  client_pending, time, reply_sent>>

\* genserver.hpp:122–127  try_receive returns a Reply with wrong
\* request_id (stale from an older call); caller discards it and loops.
ClientSkipStaleReply(c) ==
  /\ client_state[c] = "waiting"
  /\ time < client_pending[c].deadline
  /\ Len(client_mailbox[c]) > 0
  /\ Head(client_mailbox[c]).request_id # client_pending[c].request_id
  /\ client_mailbox' = [client_mailbox EXCEPT ![c] = Tail(@)]
  /\ UNCHANGED <<server_alive, server_mailbox, client_state,
                  next_request_id, client_pending, time, reply_sent>>

\* genserver.hpp:104–106 and 113–115  remaining time is negative or
\* try_receive itself timed out -> throw CallTimeout.
\* Absolute deadline: fires when time >= deadline regardless of mailbox
\* contents — even if a matching reply is queued.
ClientTimeout(c) ==
  /\ client_state[c] = "waiting"
  /\ time >= client_pending[c].deadline
  /\ client_state' = [client_state EXCEPT ![c] = "timed_out"]
  /\ UNCHANGED <<server_alive, server_mailbox, client_mailbox,
                  next_request_id, client_pending, time, reply_sent>>

\* Models return from call() (success or timeout) and re-entering idle
\* state for a subsequent call.  Needed for multi-call scenarios such as
\* stale reply skip.
ClientReset(c) ==
  /\ client_state[c] \in {"got_reply", "timed_out"}
  /\ client_state' = [client_state EXCEPT ![c] = "idle"]
  /\ client_pending' = [client_pending EXCEPT ![c] = NoPending]
  /\ UNCHANGED <<server_alive, server_mailbox, client_mailbox,
                  next_request_id, time, reply_sent>>

(***************************************************************************)
(* Server actions (serve() loop via dispatch_one / MessageDispatcher)       *)
(***************************************************************************)

\* genserver.hpp:161–168  MessageDispatcher::operator()(CallMessage<Req>&)
\* handles request, sends Reply{request_id, response} to msg.caller.
ServerDispatchCall ==
  /\ server_alive
  /\ Len(server_mailbox) > 0
  /\ Head(server_mailbox).type = "call"
  /\ LET msg == Head(server_mailbox) IN
     /\ server_mailbox' = Tail(server_mailbox)
     /\ client_mailbox' =
          [client_mailbox EXCEPT
            ![msg.caller] = Append(@, ReplyMsg(msg.request_id))]
     /\ reply_sent' = reply_sent \cup {<<msg.caller, msg.request_id>>}
     /\ UNCHANGED <<server_alive, client_state, next_request_id,
                    client_pending, time>>

\* genserver.hpp:170–174  MessageDispatcher::operator()(CastMessage<Req>&)
\* handles request, no reply.
ServerDispatchCast ==
  /\ server_alive
  /\ Len(server_mailbox) > 0
  /\ Head(server_mailbox).type = "cast"
  /\ server_mailbox' = Tail(server_mailbox)
  /\ UNCHANGED <<server_alive, client_state, client_mailbox,
                  next_request_id, client_pending, time, reply_sent>>

\* genserver.hpp:176–179  MessageDispatcher::operator()(Reply&) returns
\* false — serve loop discards Reply messages that arrive in its mailbox.
ServerIgnoreReply ==
  /\ server_alive
  /\ Len(server_mailbox) > 0
  /\ Head(server_mailbox).type = "reply"
  /\ server_mailbox' = Tail(server_mailbox)
  /\ UNCHANGED <<server_alive, client_state, client_mailbox,
                  next_request_id, client_pending, time, reply_sent>>

\* genserver.hpp:181  MessageDispatcher::operator()(ExitSignal&) returns
\* true, breaking out of the serve() while-loop.
ServerExit ==
  /\ server_alive
  /\ server_alive' = FALSE
  /\ UNCHANGED <<server_mailbox, client_state, client_mailbox,
                  next_request_id, client_pending, time, reply_sent>>

(***************************************************************************)
(* Time and noise actions                                                   *)
(***************************************************************************)

\* Advance logical clock by one tick.  Only enabled when at least one
\* client is waiting (otherwise no deadline to reach).
Tick ==
  /\ time < MaxTime
  /\ \E c \in Clients : client_state[c] = "waiting"
  /\ time' = time + 1
  /\ UNCHANGED <<server_alive, server_mailbox, client_state, client_mailbox,
                  next_request_id, client_pending, reply_sent>>

\* Inject a stale Reply into the server mailbox.  Models Reply messages
\* arriving from external calls the server made (abstracted away).
\* genserver.hpp:176–179  the serve loop discards these via
\* operator()(Reply&).
\* Bounded: at most one Reply in the server mailbox at a time.
InjectStaleReply(rid) ==
  /\ server_alive
  /\ ~\E i \in 1..Len(server_mailbox) : server_mailbox[i].type = "reply"
  /\ server_mailbox' = Append(server_mailbox, StaleReplyMsg(rid))
  /\ UNCHANGED <<server_alive, client_state, client_mailbox,
                  next_request_id, client_pending, time, reply_sent>>

(***************************************************************************)
(* Next-state relation and specification                                    *)
(***************************************************************************)

Next ==
  \/ \E c \in Clients : ClientCall(c)
  \/ \E c \in Clients : ClientCast(c)
  \/ ServerDispatchCall
  \/ ServerDispatchCast
  \/ ServerIgnoreReply
  \/ ServerExit
  \/ \E c \in Clients : ClientReceiveCorrectReply(c)
  \/ \E c \in Clients : ClientSkipStaleReply(c)
  \/ \E c \in Clients : ClientTimeout(c)
  \/ \E c \in Clients : ClientReset(c)
  \/ Tick
  \/ \E rid \in RequestIds : InjectStaleReply(rid)

Spec == Init /\ [][Next]_vars

(***************************************************************************)
(* Safety invariants                                                        *)
(***************************************************************************)

\* Every reply sitting in a client's mailbox was generated by the server
\* for a call that client actually made, and carries a request_id that
\* client has issued.
ReplyCorrelation ==
  \A c \in Clients :
    \A i \in 1..Len(client_mailbox[c]) :
      /\ client_mailbox[c][i].request_id < next_request_id[c]
      /\ <<c, client_mailbox[c][i].request_id>> \in reply_sent

\* For each (caller, request_id) pair, at most one Reply exists in
\* the caller's mailbox.  The server processes each CallMessage exactly
\* once (FIFO).
AtMostOneReplyPerCall ==
  \A c \in Clients :
    \A rid \in RequestIds :
      Cardinality(
        {i \in 1..Len(client_mailbox[c]) :
          client_mailbox[c][i].request_id = rid}) <= 1

\* A reply generated for one caller is never delivered to a different
\* caller.  Follows from the server routing Reply to msg.caller
\* (genserver.hpp:166).
NoWrongCallerReply ==
  \A c \in Clients :
    \A i \in 1..Len(client_mailbox[c]) :
      <<c, client_mailbox[c][i].request_id>> \in reply_sent

(***************************************************************************)
(* Focused scenarios mirroring runtime behaviour.                           *)
(***************************************************************************)

ScenarioClient == CHOOSE c \in Clients : TRUE

\* --- SingleCallReply -------------------------------------------------
\* One client makes a call, server replies, client receives.

SingleCallReplyInit == Init

SingleCallReplyNext ==
  \/ ClientCall(ScenarioClient)
  \/ ServerDispatchCall
  \/ ClientReceiveCorrectReply(ScenarioClient)

SingleCallReplySpec ==
  SingleCallReplyInit /\ [][SingleCallReplyNext]_vars

SingleCallReplyOutcome ==
  client_state[ScenarioClient] # "got_reply" \/
  /\ <<ScenarioClient, client_pending[ScenarioClient].request_id>>
       \in reply_sent
  /\ client_pending[ScenarioClient].request_id = 0

\* --- MultiCallerInterleaving ----------------------------------------
\* All clients call concurrently; server processes FIFO; replies go to
\* correct callers.

MultiCallerInterleavingInit == Init

MultiCallerInterleavingNext ==
  \/ \E c \in Clients : ClientCall(c)
  \/ ServerDispatchCall
  \/ ServerDispatchCast
  \/ ServerIgnoreReply
  \/ \E c \in Clients : ClientReceiveCorrectReply(c)
  \/ \E c \in Clients : ClientSkipStaleReply(c)
  \/ \E c \in Clients : ClientTimeout(c)
  \/ \E c \in Clients : ClientReset(c)
  \/ Tick

MultiCallerInterleavingSpec ==
  MultiCallerInterleavingInit /\ [][MultiCallerInterleavingNext]_vars

MultiCallerInterleavingOutcome ==
  \A c \in Clients :
    \A i \in 1..Len(client_mailbox[c]) :
      /\ <<c, client_mailbox[c][i].request_id>> \in reply_sent
      /\ client_mailbox[c][i].request_id < next_request_id[c]

\* --- TimeoutNoReply --------------------------------------------------
\* Client calls but server never dispatches; client times out.

TimeoutNoReplyInit == Init

TimeoutNoReplyNext ==
  \/ ClientCall(ScenarioClient)
  \/ ClientTimeout(ScenarioClient)
  \/ Tick

TimeoutNoReplySpec ==
  TimeoutNoReplyInit /\ [][TimeoutNoReplyNext]_vars

TimeoutNoReplyOutcome ==
  client_state[ScenarioClient] # "timed_out" \/
  /\ time >= client_pending[ScenarioClient].deadline
  /\ reply_sent = {}

\* --- StaleReplySkip ---------------------------------------------------
\* Client calls, times out, calls again; the server's late reply for the
\* first call arrives while the client waits for the second.  The client
\* skips the stale reply and accepts only the matching one.

StaleReplySkipInit == Init

StaleReplySkipNext ==
  \/ ClientCall(ScenarioClient)
  \/ ServerDispatchCall
  \/ ClientReceiveCorrectReply(ScenarioClient)
  \/ ClientSkipStaleReply(ScenarioClient)
  \/ ClientTimeout(ScenarioClient)
  \/ ClientReset(ScenarioClient)
  \/ Tick

StaleReplySkipSpec ==
  StaleReplySkipInit /\ [][StaleReplySkipNext]_vars

StaleReplySkipOutcome ==
  \* If the client got a reply, it was for the correct (most recent)
  \* call, not a stale one from a previous timed-out call.
  client_state[ScenarioClient] # "got_reply" \/
  <<ScenarioClient, client_pending[ScenarioClient].request_id>>
    \in reply_sent

=============================================================================
