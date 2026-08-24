# Devlog: Case Study 02 — Zero-Copy MAVLink DMA Parser & The Cache Coherency Trap

## What I'm Trying to Do

The coprocessor ingests MAVLink telemetry from the Pixhawk flight controller at 100+ Hz. Each `ATTITUDE` message is 40 bytes; at 100 Hz that's 4000 bytes/second. Using CPU-driven UART interrupts for every byte would waste cycles better spent on the EKF. Instead, I configure **DMA1_Stream0 in circular mode** to autonomously move USART1 RX bytes into a 512-byte AXI SRAM buffer. The CPU only wakes when a full packet is available.

---

## Attempt 1: DMA Works in Simulation, Deadlocks on Silicon

I wrote the parser to poll `DMA1_S0NDTR` (the DMA remaining-count register) to detect new data, then read the buffer directly.

### The Output (Simulation Passes, Silicon Would Hang Forever)

In Renode, the parser correctly detects injected bytes and assembles packets. But I realized this exact code would **deadlock on physical silicon**.

### My Mistake & Root Cause Analysis

The Cortex-M7 has a **16KB L1 Data Cache**. When the CPU first reads the DMA buffer address, that cache line is loaded into L1. The DMA controller then writes new bytes directly to physical AXI SRAM — but the CPU is reading the **stale cached copy**, not the physical memory. The NDTR register updates (it's a peripheral register, not cached memory), but the buffer contents the CPU sees never change.

This is the classic **DMA-to-CPU cache coherency problem** on Cortex-M7. The D-Cache is write-back, so:
1. CPU reads `buffer[0]` → cache miss → loaded from SRAM into L1
2. DMA writes `buffer[0] = 0xFD` → goes to SRAM, but L1 still has stale `0x00`
3. CPU reads `buffer[0]` again → cache hit → returns stale `0x00` forever

### The Fix

Defensive cache maintenance before reading the DMA buffer region:
```c
static void invalidate_cache_line(uint32_t addr) {
    SCB_DCCIMVAC = addr & ~0x1FU;  /* Clean + Invalidate D-Cache line by address */
    __asm__ volatile ("dsb 0xF" ::: "memory");
    __asm__ volatile ("isb 0xF");
}
```

I invalidate all cache lines overlapping the region between `dma_last_read_pos` and the current DMA write pointer. The `DSB` ensures the invalidation completes before subsequent reads; the `ISB` flushes the pipeline so no stale prefetches survive.

**Key insight:** Even though Renode doesn't model L1 cache, I write the code for silicon correctness first. The simulation passes *because* it doesn't model the problem — not because the code is correct.

---

## Attempt 2: The MAVLink Struct Packing Trap

With DMA working, I needed to parse the raw byte stream into C structs. I defined:
```c
typedef struct {
    uint8_t magic, length, seq, sysid, compid, msgid;
    float roll, pitch, yaw;
} mavlink_attitude_t;
```

### The Output (Silent Data Corruption)
```text
[DEBUG] Expected Roll Bits: 0x3F800000 (1.0f)
[DEBUG] Parsed Roll Bits  : 0x00003F80
[ERROR] Struct padding offset shift detected!
```

### My Mistake & Root Cause Analysis

GCC aligns `float` fields to 4-byte boundaries on the 32-bit Cortex-M7. The 6-byte header causes the compiler to insert **2 bytes of invisible padding** before `roll`. When I cast the wire buffer directly to this struct, `roll` reads from byte offset 8 instead of byte 6, consuming the last 2 bytes of roll and the first 2 bytes of pitch.

### The Fix

```c
typedef struct __attribute__((packed)) {
    uint32_t time_boot_ms;
    float roll, pitch, yaw;
    float rollspeed, pitchspeed, yawspeed;
} mavlink_attitude_t;
```

`__attribute__((packed))` eliminates all padding. The struct layout now matches the wire byte-for-byte. On Cortex-M7 with `-mfloat-abi=hard`, unaligned float accesses are supported by the hardware (unlike Cortex-M0), so there's no performance penalty.

---

## Attempt 3: MAVLink CRC Validation

MAVLink v2 appends a 16-bit CRC (CRC-16-CCITT with a seed per message type) over the header + payload. I used the STM32's hardware CRC peripheral.

### The Output (CRC Mismatch on Every Packet)
```text
[DEBUG] Computed CRC: 0x3A7F, Expected: 0xE2B4
[ERROR] CRC mismatch — packet dropped
```

### My Mistake & Root Cause Analysis

The STM32 hardware CRC peripheral processes 32-bit words **MSB-first** by default, but the Cortex-M7 writes them **LSB-first** (little-endian). A write of `0x34333231` to `CRC_DR` feeds bytes `0x34, 0x33, 0x32, 0x31` into the CRC engine — but the wire order is `0x31, 0x32, 0x33, 0x34`. The CRC computes over the wrong byte sequence.

### The Fix

```c
CRC_CR |= CRC_CR_REV_IN | CRC_CR_REV_OUT;
```
`CRC_CR_REV_IN = 0b11` reverses input bytes within each word. `CRC_CR_REV_OUT` reverses the final CRC result. Together they make the hardware CRC match the wire byte order.

---

## Final Result

The zero-copy DMA parser correctly:
1. Ingests bytes via DMA circular buffer (zero CPU overhead)
2. Invalidates L1 D-Cache lines before reading (silicon-correct)
3. Parses MAVLink v2 packets with packed structs (no padding)
4. Validates CRC using hardware peripheral with byte-order correction

Ready for CS03: Hardware timer synchronization.
