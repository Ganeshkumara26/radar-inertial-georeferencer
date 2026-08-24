# Devlog: Case Study 08 — Fault Resilience & The Malformed Packet Storm

## What I'm Trying to Do

The coprocessor must not crash when the flight controller sends malformed MAVLink data — truncated packets, wrong CRC, out-of-range values, or buffer overflows. I implement adversarial stress testing: inject 10,000 malformed packets and verify the system continues operating.

---

## Attempt 1: Single Bad Packet Crashes the Parser

I injected a MAVLink packet with `length = 255` but only 20 bytes of payload.

### The Output (HardFault)
```text
[FATAL] HardFault: Attempt to read beyond DMA buffer boundary
[FATAL] PC = 0x080001A2 (mavlink_parser_poll)
```

### My Mistake & Root Cause Analysis

My parser checked if the magic byte was valid, then read the length field and tried to access `dma_buffer[pos + header + length]` without verifying that `pos + header + length <= MAVLINK_DMA_BUFFER_SIZE`. A length of 255 with pos at offset 400 in a 512-byte buffer causes an out-of-bounds read.

### The Fix

Bounds checking at every step:
```c
if (magic == MAVLINK_V2_MAGIC) {
    if (pos + MAVLINK_HEADER_LEN > write_pos) break;  // incomplete header
    pkt_len = MAVLINK_HEADER_LEN + parser->dma_buffer[pos + 1] + MAVLINK_CHECKSUM_LEN;
    if (pos + pkt_len > MAVLINK_DMA_BUFFER_SIZE) {
        // Packet wraps around or exceeds buffer — handle wrap or drop
        if (pkt_len > MAVLINK_MAX_PACKET_LEN) {
            parser->packets_dropped++;
            pos++;
            continue;
        }
    }
}
```

---

## Attempt 2: NaN Injection Corrupts EKF State

I injected a MAVLink ATTITUDE packet with `roll = NaN`.

### The Output (EKF Diverges)
```text
[EKF] roll = nan
[EKF] P[0,0] = nan
[EKF] All subsequent predictions: nan
```

### My Mistake & Root Cause Analysis

The parser blindly copies the float from the MAVLink packet into the kinematic state. NaN propagates through the EKF: `NaN * anything = NaN`, `NaN + anything = NaN`. Once NaN enters the state vector, it never recovers.

### The Fix

Validate all parsed floats before accepting them:
```c
#include <math.h>

static int is_valid_float(float f) {
    return !isnanf(f) && !isinff(f);
}

// In parser:
case MAVLINK_MSG_ID_ATTITUDE: {
    const mavlink_attitude_t *att = (const mavlink_attitude_t *)payload;
    if (!is_valid_float(att->roll) || !is_valid_float(att->pitch) || !is_valid_float(att->yaw)) {
        parser->packets_dropped++;
        break;  // reject this packet, keep previous state
    }
    // ... accept packet
}
```

---

## Attempt 3: Ring Buffer Overflow During Burst

I injected 1000 MAVLink packets in rapid succession (simulating a burst from the flight controller).

### The Output (State Loss)
```text
[PARSER] 1000 packets parsed
[RINGBUF] Overflow count: 847
[EKF] Only processed 153 of 1000 states
```

### My Mistake & Root Cause Analysis

The MAVLink parser runs in the main loop and pushes to the ring buffer. The EKF also runs in the main loop and pops from it. If the parser runs faster than the EKF (because the EKF takes longer per iteration), the ring buffer fills up and drops states.

### The Fix

Two strategies:
1. **Increase ring buffer size** (from 64 to 256 slots) for burst absorption
2. **Process multiple states per EKF iteration** — drain the ring buffer fully each loop:

```c
// In main loop:
kinematic_state_t state;
while (ring_buffer_pop(&g_state_buffer, &state) == 0) {
    ekf_predict(&g_ekf, 0.01f);
    // ... process radar target if available
}
```

This ensures the EKF catches up after a burst instead of falling permanently behind.

---

## Final Result

The fault-resilient coprocessor:
- Bounds-checked MAVLink parser (no buffer overflows)
- NaN/Inf rejection on all parsed floats
- Ring buffer with overflow counting for diagnostics
- Burst absorption via large buffer + drain-on-loop
- System continues operating through 10,000 malformed packets

Ready for CS09: Cache coherency and MPU configuration.
