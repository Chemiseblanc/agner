---- MODULE serve_dispatch ----
EXTENDS actor_defs, TLC, Sequences

CONSTANTS Server, Sender

ASSUME /\ Server \in ActorPool
       /\ Sender \in ActorPool
       /\ Server # Sender

ReplyMsg(req_id, payload) ==
  [kind |-> "Reply", req_id |-> req_id, payload |-> payload]

ControlUniverse ==
  {ReplyMsg(id, value) : id \in MessageIds, value \in Payloads} \cup
  {DownSignal(a, r) : a \in ActorPool, r \in ExitReasons} \cup
  {ExitSignal(a, r) : a \in ActorPool, r \in ExitReasons}

ExpectedHistory == <<"Reply", "DownSignal", "ExitSignal">>

VARIABLES serve_state, queue, dispatch_history,
          ignored_reply_count, ignored_down_count, exit_reason_observed

serve_vars ==
  <<serve_state, queue, dispatch_history,
    ignored_reply_count, ignored_down_count, exit_reason_observed>>

ServeInit ==
  /\ serve_state = "running"
  /\ queue = <<ReplyMsg(FirstMessageId, FirstPayload),
               DownSignal(Sender, ExitNormal),
               ExitSignal(Sender, ExitStopped)>>
  /\ dispatch_history = <<>>
  /\ ignored_reply_count = 0
  /\ ignored_down_count = 0
  /\ exit_reason_observed = "none"

DispatchHead ==
  /\ serve_state = "running"
  /\ Len(queue) > 0
  /\ LET msg == Head(queue)
     IN /\ queue' = Tail(queue)
        /\ dispatch_history' = Append(dispatch_history, msg.kind)
        /\ IF msg.kind = "Reply"
              THEN /\ serve_state' = "running"
                   /\ ignored_reply_count' = ignored_reply_count + 1
                   /\ ignored_down_count' = ignored_down_count
                   /\ exit_reason_observed' = exit_reason_observed
              ELSE IF msg.kind = "DownSignal"
                     THEN /\ serve_state' = "running"
                          /\ ignored_reply_count' = ignored_reply_count
                          /\ ignored_down_count' = ignored_down_count + 1
                          /\ exit_reason_observed' = exit_reason_observed
                     ELSE /\ msg.kind = "ExitSignal"
                          /\ serve_state' = "stopped"
                          /\ ignored_reply_count' = ignored_reply_count
                          /\ ignored_down_count' = ignored_down_count
                          /\ exit_reason_observed' = msg.reason

ServeSpec ==
  ServeInit /\ [][DispatchHead]_serve_vars /\ WF_serve_vars(DispatchHead)

TypeOK ==
  /\ serve_state \in {"running", "stopped"}
  /\ queue \in Seq(ControlUniverse)
  /\ dispatch_history \in {
       <<>>,
       <<"Reply">>,
       <<"Reply", "DownSignal">>,
       <<"Reply", "DownSignal", "ExitSignal">>
     }
  /\ ignored_reply_count \in Nat
  /\ ignored_down_count \in Nat
  /\ exit_reason_observed \in ExitReasons \cup {"none"}

DispatcherHistoryIsPrefix ==
  dispatch_history = SubSeq(ExpectedHistory, 1, Len(dispatch_history))

ReplyAndDownKeepServing ==
  dispatch_history \in {<<>>, <<"Reply">>, <<"Reply", "DownSignal">>} =>
    serve_state = "running"

ExitStopsServe ==
  dispatch_history = ExpectedHistory =>
    /\ serve_state = "stopped"
    /\ exit_reason_observed = ExitStopped

IgnoredCountsTrackHistory ==
  /\ ignored_reply_count =
       Cardinality({i \in 1..Len(dispatch_history) : dispatch_history[i] = "Reply"})
  /\ ignored_down_count =
       Cardinality({i \in 1..Len(dispatch_history) : dispatch_history[i] = "DownSignal"})

=============================================================================