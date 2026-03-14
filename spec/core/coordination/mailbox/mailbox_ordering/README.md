# Mailbox Ordering

**What this verifies:** Messages in an actor's mailbox are processed in FIFO (first-in, first-out) order.

## The Property

When an actor receives multiple messages before processing them, it must see them in the order they arrived. This is fundamental to actor model semantics—message ordering from a single sender is preserved.

## Scenario Setup

```mermaid
sequenceDiagram
    participant Sender
    participant Mailbox as Actor Mailbox
    participant Actor as Sequence Actor

    Note over Mailbox: Initial: [Ping₁, Ping₂]
    Note over Actor: Expects two messages

    Actor->>Mailbox: receive() #1
    Mailbox-->>Actor: Ping₁ (first message)
    Note over Actor: Observes: [Ping₁]

    Actor->>Mailbox: receive() #2
    Mailbox-->>Actor: Ping₂ (second message)
    Note over Actor: Observes: [Ping₁, Ping₂]

    Note over Actor: ✓ Order preserved!
```

## What Could Go Wrong (Without This Property)

```mermaid
sequenceDiagram
    participant Mailbox as Actor Mailbox
    participant Actor as Sequence Actor

    Note over Mailbox: Initial: [Ping₁, Ping₂]

    Actor->>Mailbox: receive() #1
    Mailbox-->>Actor: Ping₂ (wrong!)
    Note over Actor: ❌ Out of order!
```

If the implementation used the wrong data structure (e.g., an unordered set instead of a queue), messages could arrive out of order, breaking application logic that depends on sequencing.

## The Invariant

```
MailboxOrderingOutcome ==
  pc[ScenarioActor] # "done" \/
  ObservationValues(observations[ScenarioActor]) = <<FirstPayload, SecondPayload>>
```

**In plain English:** When the actor finishes, the messages it observed must be in the same order they were queued.

## Running This Spec

```bash
cd spec/core/coordination/mailbox/mailbox_ordering
java -jar tla2tools.jar -modelcheck -config mailbox_ordering.cfg mailbox_ordering.tla
```
