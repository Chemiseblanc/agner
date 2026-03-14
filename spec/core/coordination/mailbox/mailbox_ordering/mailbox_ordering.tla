---- MODULE mailbox_ordering ----
(***************************************************************************)
(* Mailbox Ordering Scenario                                               *)
(*                                                                         *)
(* Verifies that messages are processed in FIFO order when already         *)
(* present in the mailbox. A sequence actor with two queued Ping messages  *)
(* must observe them in the order they were enqueued.                      *)
(***************************************************************************)
EXTENDS actor_system

(***************************************************************************)
(* Scenario-specific initial state                                         *)
(***************************************************************************)
MailboxOrderingInit ==
  /\ live = {ScenarioActor}
  /\ kind =
       [a \in ActorPool |->
         IF a = ScenarioActor THEN "sequence" ELSE "none"]
  /\ pc =
       [a \in ActorPool |->
         IF a = ScenarioActor THEN "seq_first" ELSE "absent"]
  /\ ready = {ScenarioActor}
  /\ mailboxes =
       [a \in ActorPool |->
         IF a = ScenarioActor
           THEN <<ScenarioPing, ScenarioSecondPing>>
           ELSE <<>>]
  /\ pending_result = [a \in ActorPool |-> NoPending]
  /\ timers = [a \in ActorPool |-> NoDeadline]
  /\ observations = [a \in ActorPool |-> <<>>]
  /\ msg_state =
       [id \in MessageIds |->
         IF id = FirstMessageId \/ id = SecondMessageId
           THEN "queued"
           ELSE "unused"]
  /\ time = 0
  /\ links = {}
  /\ monitors = {}
  /\ exit_reason = [a \in ActorPool |-> "none"]

(***************************************************************************)
(* Scenario-specific next-state relation                                   *)
(***************************************************************************)
MailboxOrderingNext ==
  RunReadyActor(ScenarioActor)

(***************************************************************************)
(* Specification                                                            *)
(***************************************************************************)
MailboxOrderingSpec ==
  MailboxOrderingInit /\ [][MailboxOrderingNext]_vars

(***************************************************************************)
(* Expected outcome: messages observed in FIFO order                       *)
(***************************************************************************)
MailboxOrderingOutcome ==
  pc[ScenarioActor] # "done" \/
  ObservationValues(observations[ScenarioActor]) =
    <<FirstPayload, SecondPayload>>

====
