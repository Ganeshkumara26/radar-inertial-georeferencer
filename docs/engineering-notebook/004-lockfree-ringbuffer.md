# Devlog: Case Study 04 — Lock-Free Ring Buffer & The Memory Barrier

## What I'm Trying to Do

The MAVLink parser (running in main loop context) produces timestamped kinematic state. The EKF (also in main loop, but logically a separate consumer) consumes these states. I need a **single-producer/single-consumer ring buffer** that passes data between them without locks, mutexes, or disabling interrupts.

---

## Attempt 1: The Naive Implementation

I wrote a simple ring buffer with `head` (write index) and `tail` (read index):

```c
void ring_buffer_push(ring_buffer_t *rb, const kinematic_state_t *state) {
    rb->entries[rb->head] = *state;
    rb->head = (rb->head + 1) & RING_BUFFER_MASK;
}

void ring_buffer_pop(ring_buffer_t *rb, kinematic_state_t *state) {
    *state = rb->entries[rb->tail];
    rb->tail = (rb->tail + 1) & RING_BUFFER_MASK;
}
```

### The Output (Intermittent Corruption)
```text
[PUSH] State at head=3: roll=0.100
[POP]  State at tail=3: roll=0.000  ← stale data!
```

### My Mistake & Root Cause Analysis

The Cortex-M7 is an **out-of-order execution** pipeline with a write buffer. When `ring_buffer_push` executes:
1. `rb->entries[rb->head] = *state` — the write goes to the write buffer, not immediately to SRAM
2. `rb->head = (rb->head + 1) & MASK` — this write completes immediately (it's a different address)

Now if the consumer (in an ISR or higher-priority context) runs between steps 1 and 2, it sees the updated `head` but the `entries[head]` write is still in the write buffer. It reads stale data.

### The Fix

A **Data Memory Barrier (DMB)** between the data write and the index update:
```c
void ring_buffer_push(ring_buffer_t *rb, const kinematic_state_t *state) {
    rb->entries[rb->head] = *state;
    __asm__ volatile ("dmb" ::: "memory");  /* Ensure write completes before index update */
    rb->head = (rb->head + 1) & RING_BUFFER_MASK;
}
```

The `DMB` instruction ensures all explicit data memory accesses before the barrier complete before any explicit data memory accesses after the barrier begin. The `"memory"` clobber tells the compiler not to reorder memory operations across the barrier.

Similarly on the consumer side:
```c
void ring_buffer_pop(ring_buffer_t *rb, kinematic_state_t *state) {
    __asm__ volatile ("dmb" ::: "memory");  /* Ensure we read valid data */
    *state = rb->entries[rb->tail];
    rb->tail = (rb->tail + 1) & RING_BUFFER_MASK;
}
```

---

## Attempt 2: Buffer Full Detection

I needed to detect when the buffer is full (producer is faster than consumer).

### The Output (Off-by-One Ambiguity)
```text
[PUSH] head=7, tail=0 → "buffer full"
[PUSH] head=0, tail=0 → "buffer full"  ← but it's actually empty!
```

### My Mistake & Root Cause Analysis

With a power-of-2 buffer size, `head == tail` is ambiguous — it means both "empty" and "full". My original check `if (head == tail) return FULL;` incorrectly reports full when the buffer is actually empty (after initialization or after consuming all entries).

### The Fix

The standard solution: **sacrifice one slot**. The buffer is full when `(head + 1) % SIZE == tail`. This means a buffer of size N can hold N-1 entries, but the empty/full states are unambiguous.

```c
int ring_buffer_is_full(const ring_buffer_t *rb) {
    return ((rb->head + 1) & RING_BUFFER_MASK) == rb->tail;
}
```

With `RING_BUFFER_SIZE = 64`, we get 63 usable slots — more than enough for the kinematic history window needed by the EKF.

---

## Final Result

The lock-free ring buffer:
- Single-producer (parser), single-consumer (EKF) — no locks needed
- DMB barriers ensure correct memory ordering on Cortex-M7
- Power-of-2 size with sacrificed slot for unambiguous full/empty
- Overflow counter for diagnostics (how many states were dropped)

Ready for CS05: Fixed-point EKF implementation.
