# Receive Suspends

**What this verifies:** When an actor calls `receive()` and no matching message exists, it suspends until one arrives.

## The Property

An actor that calls `receive()` with an empty mailbox should:
1. Suspend (not spin or busy-wait)
2. Automatically resume when a matching message arrives
3. Correctly observe the message that woke it up

## Scenario Setup

```mermaid
sequenceDiagram
    participant S as Scheduler
    participant A as Collector Actor
    participant M as Mailbox

    Note over M: Empty mailbox

    S->>A: Run actor
    A->>M: receive()
    Note over M: No matching message
    Note over A: Suspends (blocked)
    Note over A: State: live, NOT ready

    S->>M: Send(Ping)
    Note over M: Message arrives
    M-->>A: Ping delivered directly
    Note over A: State: live, ready

    S->>A: Resume actor
    A->>A: Process Ping
    Note over A: Observes: [Ping]
    Note over A: State: done
```

## Why This Matters

```mermaid
sequenceDiagram
    participant A as Actor (Wrong Impl)
    participant M as Mailbox

    Note over M: Empty

    loop Busy-wait (BAD!)
        A->>M: Check mailbox
        M-->>A: Empty
        Note over A: Spin, wasting CPU
    end
```

A naive implementation might poll the mailbox in a loop. This specification verifies that the actor framework correctly:
- Removes the actor from the ready set when blocked
- Delivers messages directly to blocked actors (pending_result)
- Re-adds the actor to the ready set when a message arrives

## The Invariant

```
ReceiveSuspendsOutcome ==
  pc[ScenarioActor] # "done" \/
  ObservationValues(observations[ScenarioActor]) = <<FirstPayload>>
```

**In plain English:** When the actor finishes, it must have observed exactly the message that was sent while it was waiting.

## Related Invariants

This scenario also verifies:
- **BlockedActorsHaveNoMatches**: The actor only blocks when there's truly nothing to process
- **PendingResultsAreReady**: Once a message arrives, the actor is immediately schedulable

## Running This Spec

```bash
cd spec/core/coordination/mailbox/receive_suspends
java -jar tla2tools.jar -modelcheck -config receive_suspends.cfg receive_suspends.tla
```
