# State Space Optimization

Techniques for reducing state space without sacrificing model fidelity. The goal is faster verification and shorter counterexample traces while preserving the ability to find real bugs.

## Core Principle

**Model the property, not the implementation.** Only include detail that affects the properties you're verifying. Extra fidelity that doesn't change observable behavior just multiplies states.

## Constant Selection

### Start Minimal
Begin with the smallest constants that exercise the behavior:

```
\* Start here
ActorPool = {a1, a2}
MessageIds = {m1, m2}

\* Not here
ActorPool = {a1, a2, a3, a4, a5}
MessageIds = {m1, m2, m3, m4, m5, m6}
```

### Scale Strategically
Increase constants one at a time to find the threshold where bugs appear:

| Scenario | Recommended Starting Point |
|----------|---------------------------|
| Two-party protocol | 2 actors, 2 messages |
| Leader election | 3 nodes (minimal quorum) |
| Bounded queue | capacity = 2 |
| Timeout scenarios | delay = 1 tick |

### Use Symmetry Sets
When actors/nodes are interchangeable, declare them as symmetry sets:

```
CONSTANTS
  \* Model values with symmetry
  Actors = {a1, a2, a3}
  
SYMMETRY ActorSymmetry
  ActorSymmetry == Permutations(Actors)
```

This can reduce state space by `n!` for `n` symmetric elements.

## Data Abstraction

### Abstract Away Irrelevant Detail

**Bad:** Modeling actual message payloads when only message type matters:
```tla
\* Explodes state space with every possible payload value
Message == [type: {"Request", "Response"}, payload: 0..1000]
```

**Good:** Use symbolic values or omit when irrelevant:
```tla
\* Payloads abstracted to small set
Payloads == {1, 2}  \* Just enough to distinguish "different" payloads
Message == [type: {"Request", "Response"}, payload: Payloads]

\* Or omit if payload doesn't affect protocol
Message == [type: {"Request", "Response"}]
```

### Bound Unbounded Structures

**Mailboxes/Queues:**
```tla
\* Add explicit bounds
ASSUME MaxMailboxLen \in 1..3

Send(dest, msg) ==
  /\ Len(mailboxes[dest]) < MaxMailboxLen  \* Bounded
  /\ mailboxes' = [mailboxes EXCEPT ![dest] = Append(@, msg)]
```

**Counters:**
```tla
\* Saturating counter instead of unbounded
Increment(counter) ==
  IF counter < MaxCount THEN counter + 1 ELSE counter
```

### Model Fixed-Width Integers Explicitly

If the implementation uses machine integers, do not model them with unbounded `Int` or `Nat`. That both changes behavior and expands the state space unnecessarily.

Model the actual domain and overflow semantics you care about:

```tla
Bits == 8
UIntMax == (2^Bits) - 1
IntMin == -(2^(Bits - 1))
IntMax == (2^(Bits - 1)) - 1

UInt == 0..UIntMax
SInt == IntMin..IntMax

WrapUnsigned(n) ==
  ((n % (UIntMax + 1)) + (UIntMax + 1)) % (UIntMax + 1)

WrapSigned(n) ==
  LET modulus == 2^Bits
      wrapped == ((n % modulus) + modulus) % modulus
  IN IF wrapped <= IntMax THEN wrapped ELSE wrapped - modulus

AddUInt(x, y) == WrapUnsigned(x + y)
AddSInt(x, y) == WrapSigned(x + y)
```

Use different operators if the implementation saturates, traps, or forbids overflow instead of wrapping. The model should match the semantics that matter for the property being checked.

For C and C++ in particular, do not silently treat signed arithmetic as mathematical integers when overflow is relevant. Either:

- model the constrained range in which overflow cannot happen, or
- encode the intended implementation contract explicitly, such as two's-complement wraparound or overflow rejection.

### Use Smaller Widths When Valid

It is often valid to model a production `int64` or `uint64` as `int8`, `int16`, or another much smaller fixed width to reduce the state space.

Do this only when the reduced width preserves the behaviors relevant to the property:

- the property does not depend on values outside the reduced range,
- all important boundary cases are still represented, and
- overflow behavior is still exercised in the same way the real system relies on.

Example: if a protocol only needs to distinguish negative, zero, positive, and wraparound behavior, an `int8` model is often sufficient and much cheaper than `int64`.

### Canonicalize Equivalent States

If order doesn't matter, use sets instead of sequences:
```tla
\* BAD: <<m1, m2>> and <<m2, m1>> are different states
pending \in Seq(Messages)

\* GOOD: {m1, m2} is one state regardless of insertion order
pending \subseteq Messages
```

## Temporal Abstraction

### Collapse Time When Possible

**Bad:** Modeling every tick when only deadlines matter:
```tla
time \in 0..100  \* 100 time values × other state
```

**Good:** Track only relative ordering or deadline relationships:
```tla
\* Deadlines as logical ordering, not absolute time
timers \in [Actors -> {"none", "pending", "fired"}]
```

### Bound Time Explicitly

When time must be modeled:
```tla
\* Tight bound based on scenario
MaxTick == Cardinality(ActorPool) * TimeoutDelay

\* Cap time advancement
AdvanceTime ==
  /\ time < MaxTick
  /\ time' = time + 1
```

### Use Event-Driven Time

Instead of clock ticks, advance time only when meaningful:
```tla
\* Only advance when a timer would fire
AdvanceToNextDeadline ==
  LET next == Min({timers[a] : a \in Actors} \ {NoDeadline})
  IN /\ next # {}
     /\ time' = next
```

## Action Granularity

### Combine Atomic Steps

If steps always happen together, combine them:

**Before (3 states per send):**
```tla
PrepareMessage ==
  /\ phase = "idle"
  /\ phase' = "preparing"
  /\ msg' = CreateMsg()

ValidateMessage ==
  /\ phase = "preparing"  
  /\ phase' = "validated"

SendMessage ==
  /\ phase = "validated"
  /\ phase' = "idle"
  /\ mailboxes' = ...
```

**After (1 state per send):**
```tla
SendMessage ==
  /\ phase = "idle"
  /\ LET msg == CreateMsg()
     IN mailboxes' = [mailboxes EXCEPT ![dest] = Append(@, msg)]
  /\ UNCHANGED phase  \* No intermediate phases
```

### Use Macros for Compound Operations

In PlusCal, macros execute atomically without creating intermediate states:
```pluscal
macro atomic_enqueue(q, item)
begin
  q := Append(q, item);
end macro;

\* Single atomic step, no intermediate state
atomic_enqueue(queue, msg);
```

### Reduce Interleaving Points

Fewer labels = fewer interleaving points = smaller state space:
```pluscal
\* BAD: Each label is an interleaving point  
CheckAndSend:
  if CanSend() then
Send:
    call DoSend();
  end if;

\* GOOD: Combined when atomicity is acceptable
CheckAndSend:
  if CanSend() then
    call DoSend();
  end if;
```

## Constraint Strengthening

### Add Enabling Conditions

Restrict when actions can fire to eliminate impossible states:
```tla
\* Without constraint: explores states where dead actors receive
Receive(actor) ==
  /\ Len(mailboxes[actor]) > 0
  /\ ...

\* With constraint: only live actors receive
Receive(actor) ==
  /\ actor \in live           \* Added constraint
  /\ Len(mailboxes[actor]) > 0
  /\ ...
```

### Use State Constraints in Config

Prune unreachable states early:
```
CONSTRAINT
  \* Don't explore states with more than 5 messages total
  Cardinality(UNION {ToSet(mailboxes[a]) : a \in Actors}) <= 5
```

### Restrict Initial States

Tighter Init reduces reachable states:
```tla
\* LOOSE: Any actor could be initial sender
Init ==
  /\ sender \in Actors
  /\ ...

\* TIGHT: Fix one actor as sender (use symmetry if needed)
Init ==
  /\ sender = CHOOSE a \in Actors : TRUE
  /\ ...
```

## Verification Strategy

### Progressive Verification

1. **Smoke test** with simulation first
2. **Minimal constants** for exhaustive check
3. **Increase constants** incrementally if passes
4. **Stop and simplify** if an exhaustive check takes more than 60 seconds
5. **Stop** when confident or when state space becomes impractical

Treat 60 seconds as a hard warning sign, not a performance nuisance. A model that takes longer than that is usually too large for fast iteration and should be examined for simplification before increasing constants further.

### Targeted Properties

Check different properties with different model sizes:

```
\* Safety: small model is often sufficient
ActorPool = {a1, a2}

\* Liveness: may need more actors to expose fairness issues
ActorPool = {a1, a2, a3}
```

### State Count Guidelines

| States | Typical Time | Action |
|--------|--------------|--------|
| < 100K | < 1 min | Good for iteration |
| 100K - 1M | 1-5 min | Too large for normal modeling iteration; simplify if possible |
| 1M - 10M | 5-30 min | Use for final verification |
| > 10M | > 30 min | Reduce model or use simulation |

For this skill, anything above 60 seconds should be treated as too large and reviewed for unnecessary detail, excessive nondeterminism, or missing abstractions.

## Common Pitfalls

### Accidental State Explosion

**Unbounded message IDs:**
```tla
\* BAD: Fresh ID every send creates unbounded states
nextId' = nextId + 1

\* GOOD: Reuse IDs from fixed pool
\E id \in AvailableIds : ...
```

**Recording full history:**
```tla
\* BAD: History grows unboundedly
history' = Append(history, event)

\* GOOD: Track only what's needed for properties
lastEvent' = event
```

### Over-Specification

**Modeling implementation details:**
```tla
\* BAD: Modeling internal buffer management
buffer \in [1..BufferSize -> Message \cup {Empty}]
head \in 0..BufferSize
tail \in 0..BufferSize

\* GOOD: Abstract queue behavior
queue \in Seq(Message)
```

### Unnecessary Nondeterminism

**Choosing when choice doesn't matter:**
```tla
\* BAD: Nondeterministic choice of equivalent values
\E x \in {1, 2, 3} : result' = x * 0

\* GOOD: Deterministic when outcome is same
result' = 0
```
