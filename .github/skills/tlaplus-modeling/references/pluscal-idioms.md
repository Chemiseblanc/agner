# PlusCal Idioms

Common patterns and recipes for PlusCal specifications.

## Process Patterns

### Worker Pool

```pluscal
fair process worker \in 1..NumWorkers
variable task = <<>>;
begin
  Loop:
    while TRUE do
      await Len(taskQueue) > 0;
      task := Head(taskQueue);
      taskQueue := Tail(taskQueue);
      \* Process task
      completed := completed \cup {task};
    end while;
end process;
```

### Producer-Consumer

```pluscal
fair process producer = "producer"
variable item = 0;
begin
  Produce:
    while item < MaxItems do
      await Len(buffer) < BufferSize;
      buffer := Append(buffer, item);
      item := item + 1;
    end while;
end process;

fair process consumer = "consumer"
variable consumed = 0;
begin
  Consume:
    while consumed < MaxItems do
      await Len(buffer) > 0;
      consumed := consumed + 1;
      buffer := Tail(buffer);
    end while;
end process;
```

### Request-Response

```pluscal
fair process client \in Clients
variable request = <<>>, response = <<>>;
begin
  SendRequest:
    request := [from |-> self, data |-> "query"];
    requests := Append(requests, request);
  WaitResponse:
    await \E i \in 1..Len(responses) : responses[i].to = self;
    with i \in {j \in 1..Len(responses) : responses[j].to = self} do
      response := responses[i];
      responses := RemoveAt(responses, i);
    end with;
end process;

fair process server = "server"
variable req = <<>>;
begin
  HandleLoop:
    while TRUE do
      await Len(requests) > 0;
      req := Head(requests);
      requests := Tail(requests);
      responses := Append(responses, [to |-> req.from, data |-> "result"]);
    end while;
end process;
```

## Synchronization Patterns

### Mutex with await

```pluscal
variables mutex = TRUE;

fair process worker \in Workers
begin
  Acquire:
    await mutex;
    mutex := FALSE;
  CriticalSection:
    \* Protected work here
    skip;
  Release:
    mutex := TRUE;
end process;
```

### Semaphore

```pluscal
variables semaphore = MaxCount;

macro P()  \* wait/acquire
begin
  await semaphore > 0;
  semaphore := semaphore - 1;
end macro;

macro V()  \* signal/release
begin
  semaphore := semaphore + 1;
end macro;
```

### Barrier Synchronization

```pluscal
variables arrived = 0, phase = 0;

fair process worker \in 1..NumWorkers
variable myPhase = 0;
begin
  Work:
    while myPhase < MaxPhases do
      \* Do work for this phase
      skip;
  Arrive:
      arrived := arrived + 1;
  Wait:
      await phase > myPhase;
      myPhase := myPhase + 1;
    end while;
end process;

fair process coordinator = "coordinator"
begin
  Coordinate:
    while phase < MaxPhases do
      await arrived = NumWorkers;
      arrived := 0;
      phase := phase + 1;
    end while;
end process;
```

## Communication Patterns

### Channel Send/Receive

```pluscal
variables channels = [c \in ChannelIds |-> <<>>];

macro send(ch, msg)
begin
  channels[ch] := Append(channels[ch], msg);
end macro;

macro receive(ch, var)
begin
  await Len(channels[ch]) > 0;
  var := Head(channels[ch]);
  channels[ch] := Tail(channels[ch]);
end macro;
```

### Selective Receive (Pattern Matching)

```pluscal
\* Receive first message matching predicate
procedure selective_receive(pred)
variable idx = 0, found = FALSE;
begin
  FindMatch:
    idx := 1;
  Scan:
    while idx <= Len(mailbox) /\ ~found do
      if pred(mailbox[idx]) then
        found := TRUE;
        current := mailbox[idx];
        mailbox := RemoveAt(mailbox, idx);
      else
        idx := idx + 1;
      end if;
    end while;
  Block:
    if ~found then
      await \E i \in 1..Len(mailbox) : pred(mailbox[i]);
      goto FindMatch;
    end if;
    return;
end procedure;
```

### Broadcast

```pluscal
macro broadcast(msg)
begin
  mailboxes := [a \in Actors |-> 
    IF a # self THEN Append(mailboxes[a], msg) 
    ELSE mailboxes[a]];
end macro;
```

## Control Flow Patterns

### Timeout with Nondeterminism

```pluscal
either
  \* Normal path: receive message
  await Len(mailbox) > 0;
  msg := Head(mailbox);
  mailbox := Tail(mailbox);
or
  \* Timeout path
  timedOut := TRUE;
end either;
```

### Retry Loop

```pluscal
procedure retry_operation()
variable attempts = 0, success = FALSE;
begin
  RetryLoop:
    while attempts < MaxRetries /\ ~success do
      attempts := attempts + 1;
      either
        \* Operation succeeds
        success := TRUE;
      or
        \* Operation fails, will retry
        skip;
      end either;
    end while;
    return;
end procedure;
```

### State Machine

```pluscal
fair process fsm = "fsm"
variable state = "init", event = <<>>;
begin
  FSMLoop:
    while state # "terminal" do
      \* Wait for event
      await Len(events) > 0;
      event := Head(events);
      events := Tail(events);
      
      \* Transition based on state and event
      if state = "init" /\ event.type = "start" then
        state := "running";
      elsif state = "running" /\ event.type = "pause" then
        state := "paused";
      elsif state = "running" /\ event.type = "stop" then
        state := "terminal";
      elsif state = "paused" /\ event.type = "resume" then
        state := "running";
      end if;
    end while;
end process;
```

## Procedure Patterns

### Recursive Procedure (with explicit stack)

PlusCal doesn't support recursion directly; use explicit stack:

```pluscal
variables stack = <<>>, result = 0;

procedure factorial(n)
begin
  FactStart:
    if n <= 1 then
      result := 1;
    else
      stack := Append(stack, n);
      call factorial(n - 1);
  FactCont:
      result := result * Head(stack);
      stack := Tail(stack);
    end if;
    return;
end procedure;
```

### Callback Pattern

```pluscal
variables callbacks = <<>>;

macro register_callback(cb)
begin
  callbacks := Append(callbacks, cb);
end macro;

procedure invoke_callbacks(event)
variable i = 1;
begin
  InvokeLoop:
    while i <= Len(callbacks) do
      \* "Call" callback by recording invocation
      invocations := Append(invocations, [cb |-> callbacks[i], ev |-> event]);
      i := i + 1;
    end while;
    return;
end procedure;
```

## Helper Definitions

### Common Sequence Operations

```pluscal
define
  \* Remove element at index
  RemoveAt(seq, idx) ==
    SubSeq(seq, 1, idx - 1) \o SubSeq(seq, idx + 1, Len(seq))
  
  \* Find first matching index
  FindFirst(seq, pred) ==
    LET matches == {i \in 1..Len(seq) : pred(seq[i])}
    IN IF matches = {} THEN 0 ELSE Min(matches)
  
  \* Convert sequence to set
  ToSet(seq) == {seq[i] : i \in 1..Len(seq)}
  
  \* Check if element in sequence
  Contains(seq, elem) == elem \in ToSet(seq)
end define;
```

### Common Set Operations

```pluscal
define
  \* Minimum of non-empty set
  Min(S) == CHOOSE x \in S : \A y \in S : x <= y
  
  \* Maximum of non-empty set  
  Max(S) == CHOOSE x \in S : \A y \in S : x >= y
  
  \* Pick arbitrary element
  Pick(S) == CHOOSE x \in S : TRUE
end define;
```

## Fairness Annotations

```pluscal
\* Weak fairness: if continuously enabled, eventually runs
fair process worker \in Workers

\* Strong fairness: if repeatedly enabled, eventually runs
fair+ process scheduler = "scheduler"

\* No fairness: may never run (for adversarial modeling)
process crashable \in CrashableNodes
```

Use `fair` (weak) for most processes. Use `fair+` (strong) only when a process may be repeatedly enabled/disabled but should still eventually run.
