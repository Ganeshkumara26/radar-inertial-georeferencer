# Devlog: Case Study 07 — Deterministic Output Streaming & The SPI Bus Contention

## What I'm Trying to Do

Package georeferenced target metadata into binary frames and stream them to the ground laptop. The frame format must be:
- Self-describing (magic byte, version, length)
- Checksummed (CRC-32)
- Deterministically parseable (fixed-size target records)
- Streamable over SPI DMA or USART polled

---

## Attempt 1: Frame Fragmentation Under Load

I wrote the output streamer to pack targets into frames and send them. At low target counts it worked. At high counts, frames arrived corrupted.

### The Output (Corrupted Frames)
```text
[OUT] Sent frame: magic=0xED, len=46, targets=1
[OUT] Ground received: magic=0xED, len=46, CRC=OK
[OUT] Sent frame: magic=0xED, len=142, targets=4
[OUT] Ground received: magic=0xED, len=78, CRC=FAIL  ← truncated!
```

### My Mistake & Root Cause Analysis

I was computing the frame length based on `count * OUTPUT_TARGET_SIZE`, but some targets had `valid=0` and were skipped during packing. The actual packed length was shorter than the declared length. The ground station read `len` bytes, got garbage for the missing targets, and CRC failed.

### The Fix

Track the actual packed offset, not the declared count:
```c
uint32_t offset = OUTPUT_HEADER_SIZE;
for (uint8_t i = 0; i < count; i++) {
    if (!targets[i].valid) continue;
    // pack target...
    offset += OUTPUT_TARGET_SIZE;
}
hdr->length = offset + 4;  // actual length, not count-based
```

---

## Attempt 2: SPI DMA Bus Contention

I configured SPI1 with DMA for high-speed output. It worked until the MAVLink DMA parser and SPI DMA tried to access the bus simultaneously.

### The Output (SPI Data Corruption)
```text
[OUT] SPI frame: byte[0]=0xED, byte[1]=0x01, byte[2]=0xFF  ← byte[2] should be 0x00
```

### My Mistake & Root Cause Analysis

On the STM32H7, DMA1 is used for USART1 RX and DMA2 for SPI1 TX. Both share the AXI bus matrix. When both DMAs request simultaneously, one must wait. If the SPI TX DMA is starved, it outputs stale FIFO data.

### The Fix

Use DMA2 for SPI (not DMA1) and set SPI DMA to **high priority** in the DMA channel configuration:
```c
// SPI1 TX on DMA2 Stream 3, Channel 3, high priority
DMA2_S3CR |= DMA_CR_PL_HIGH;
```

Also ensure the SPI DMA buffer is in a non-cacheable region (or clean the cache before starting the DMA transfer).

---

## Attempt 3: USART Polled Mode Blocks EKF

When using USART polled output (no SPI), the `usart1_putchar` function blocks until the TX FIFO is empty. At 921600 baud, each byte takes ~10 us to transmit. A 142-byte frame takes ~1.4 ms — during which the EKF can't run.

### The Output (EKF Misses Deadlines)
```text
[EKF] Predict step took 1.8ms (deadline: 10ms) — OK
[OUT] Frame transmit took 1.4ms
[EKF] Predict step took 12.1ms (deadline: 10ms) — MISSED!
```

### My Mistake & Root Cause Analysis

Polled UART TX blocks the CPU. During frame transmission, the EKF can't run. If a radar frame-sync arrives during this window, the timestamp is processed late, and the EKF prediction step uses stale data.

### The Fix

Use a **ring buffer for output** and DMA for USART TX. The `output_stream_write_targets` function writes to the ring buffer, and a DMA channel autonomously streams it to USART1. The CPU is never blocked.

```c
// Write to ring buffer (non-blocking)
ring_buffer_push(&output_rb, frame_buffer, len);

// Start DMA if not already running
if (!(DMA1_S6CR & DMA_CR_EN)) {
    // Configure DMA for USART1 TX from ring buffer
    DMA1_S6CR |= DMA_CR_EN;
}
```

---

## Final Result

The deterministic output streamer:
- Self-describing binary frames with magic, version, length, CRC-32
- Actual packed length (not declared count) for correct frame boundaries
- SPI DMA on separate DMA bus from MAVLink RX to avoid contention
- Ring buffer + DMA for non-blocking USART output
- EKF never blocked by output operations

Ready for CS08: Fault resilience under adversarial input.
