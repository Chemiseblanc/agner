# Try-Receive Race

**What this verifies:** When an actor uses `try_receive()` with a timeout, it observes exactly one outcome—either the message OR the timeout, never both or neither.

## The Property

An actor that waits for a message with a timeout faces a race condition:
- A message might arrive before the timeout
- The timeout might fire before any message arrives
- They might "tie" (arrive at the same logical instant)

The specification verifies that the actor always observes **exactly one** outcome, regardless of timing.

## Scenario: Message Wins

```mermaid
sequenceDiagram
    participant S as Scheduler
    participant A as Timeout Actor
    participant T as Timer

    Note over A: Calls try_receive(timeout)
    A->>T: Set timer for deadline
    Note over A: Suspends (blocked)

    S->>A: Send(Ping)
    Note over A: Message arrives first
    T--xA: Timer cancelled
    Note over A: State: ready

    S->>A: Resume
    Note over A: Observes: [Ping]
    Note over A: ✓ Message wins
```

## Scenario: Timeout Wins

```mermaid
sequenceDiagram
    participant S as Scheduler
    participant A as Timeout Actor
    participant T as Timer

    Note over A: Calls try_receive(timeout)
    A->>T: Set timer for deadline
    Note over A: Suspends (blocked)

    S->>S: AdvanceTime
    Note over S: Time reaches deadline

    T->>A: Timeout fires
    Note over A: State: ready

    S->>A: Resume
    Note over A: Observes: [Timeout]
    Note over A: ✓ Timeout wins
```

## What Could Go Wrong

```mermaid
sequenceDiagram
    participant A as Actor (Buggy)
    participant T as Timer

    Note over A: try_receive(timeout)

    par Race condition
        T->>A: Timeout fires
    and
        A->>A: Message arrives
    end

    Note over A: ❌ Sees BOTH or NEITHER!
```

Without proper synchronization, the actor could:
- See both the message and the timeout (double-delivery)
- See neither (lost wakeup)
- Corrupt internal state

## The Invariant

```
TryReceiveOutcome ==
  pc[ScenarioActor] # "done" \/
  /\ Len(observations[ScenarioActor]) = 1
  /\ (
       /\ observations[ScenarioActor][1].kind = "Ping"
       /\ observations[ScenarioActor][1].value = FirstPayload
     \/ observations[ScenarioActor][1] = TimeoutToken
     )
```

**In plain English:** When the actor finishes, it observed exactly one thing—either the Ping message OR the Timeout token.

## Related Invariants

- **TimerDiscipline**: Timers are only active on blocked try-receive actors
- **PendingResultsAreReady**: Whether message or timeout, the actor becomes ready

## Running This Spec

```bash
cd spec/core/timing/timeouts/try_receive_race
java -jar tla2tools.jar -modelcheck -config try_receive_race.cfg try_receive_race.tla
```
