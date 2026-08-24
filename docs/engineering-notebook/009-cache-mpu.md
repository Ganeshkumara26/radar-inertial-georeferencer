# Devlog: Case Study 09 — Cache Coherency, MPU & The Execute-Never Fault

## What I'm Trying to Do

Configure the Cortex-M7 MPU (Memory Protection Unit) to:
1. Mark the SDRAM region as **Execute-Never (XN)** — prevent accidental code execution from radar data buffers
2. Mark DMA buffers as **non-cacheable** or **write-through** — avoid coherency issues without manual invalidation
3. Enable the **MemManage fault** — catch any protection violations for debugging

---

## Attempt 1: SDRAM Code Execution Fault

I placed the large kinematic history buffer in SDRAM (0xD0000000). By default, the Cortex-M7 MPU marks this region as Execute-Never.

### The Output (MemManage Fault)
```text
[FATAL] MemManage Fault Triggered!
[FATAL] Instruction Access Violation (IACCVIOL) at 0xD0000000!
```

### My Mistake & Root Cause Analysis

I accidentally placed a function pointer table (radar processing lookup) in the `.sdram_data` section. When the CPU tried to call through this function, it fetched instructions from SDRAM, which is XN by default. The MPU correctly raised a MemManage fault.

### The Fix

**Don't put code in SDRAM.** The kinematic history buffer should contain data only. If you need executable code in SDRAM (e.g., for a large lookup table), you must configure an MPU region with `XN=0`:

```c
/* Region 0: SDRAM data (XN=1, no execution) */
MPU_RNR = 0;
MPU_RBAR = 0xD0000000;
MPU_RASR = (0x0 << 28) |  /* TEX: Normal */
           (0x3 << 24) |  /* AP: Full access */
           (0x1 << 19) |  /* TEX[2] */
           (0x0 << 18) |  /* S: Not shareable */
           (0x1 << 17) |   /* C: Cacheable */
           (0x1 << 16) |   /* B: Bufferable */
           (0x17 << 1) |  /* SIZE: 32MB (0x17 = 23 → 2^24 = 16MB... adjusted) */
           (1UL);         /* ENABLE */
/* XN bit is 0 by default in RASR — need to explicitly set for XN */
```

For our use case, **keep XN=1 on SDRAM**. The fault was correct behavior — it caught a real bug.

---

## Attempt 2: DMA Buffer Cache Coherency Without Manual Invalidation

I wanted to avoid the overhead of manually invalidating cache lines before reading DMA buffers.

### The Output (Stale Data Despite Clean Architecture)
```text
[DMA] Wrote 0xFD to buffer[0]
[CPU] Read buffer[0] = 0x00  ← stale!
```

### My Mistake & Root Cause Analysis

I tried to mark the DMA buffer region as **non-cacheable** via the MPU. But the STM32H7's AXI SRAM (0x24000000) is in the TCM memory region, which is always non-cacheable. My DMA buffer was placed in AXI SRAM, but the linker script didn't ensure it was in the TCM region.

### The Fix

Two options:
1. **Place DMA buffers in TCM SRAM (0x20000000)** — always non-cacheable, zero-wait-state
2. **Keep in AXI SRAM with manual invalidation** — works but requires `SCB_InvalidateDCache_by_Addr()`

I chose option 2 because TCM SRAM is only 128KB and we need most of it for stacks. The manual invalidation in `mavlink_parser_poll()` is acceptable overhead.

For the MPU, I configured:
```c
/* Region 1: DMA buffer in AXI SRAM (non-shareable, cacheable, but with explicit maintenance) */
MPU_RNR = 1;
MPU_RBAR = (uint32_t)g_mavlink_parser.dma_buffer;
MPU_RASR = (0x0 << 28) |  (0x3 << 24) | (0x0 << 18) |  /* Non-shareable */
           (0x1 << 17) |  (0x1 << 16) |                  /* Cacheable, Bufferable */
           (0x08 << 1) |  (1UL);                        /* SIZE: 512 bytes → 2^9 = 512 */
```

---

## Attempt 3: Stack Overflow Into Ring Buffer

The ring buffer and task stack were both in AXI SRAM. During a deep function call chain, the stack grew into the ring buffer.

### The Output (Corrupted Kinematic State)
```text
[STATE] roll = 3.72E38  ← clearly garbage
[STATE] timestamp_us = 0xDEADBEEF  ← stack artifact
```

### My Mistake & Root Cause Analysis

The linker script placed `.bss` (which includes the ring buffer) immediately after `.data` in AXI SRAM. The stack grows downward from `ORIGIN(SRAM) + LENGTH(SRAM)`. During a burst of function calls (EKF + parser + output all nested), the stack pointer moved below the end of `.bss` and clobbered the ring buffer.

### The Fix

Linker script surgery: place the ring buffer at the **bottom** of AXI SRAM and reserve a guard region:

```ld
/* Ring buffer at bottom of AXI SRAM */
.ring_buffer (NOLOAD) :
{
    . = ALIGN(32);  /* Cache line alignment */
    *(.ring_buffer)
    *(.ring_buffer*)
    . = ALIGN(32);
} > SRAM

/* Main BSS after ring buffer */
.bss :
{
    . = ALIGN(4);
    _sbss = .;
    *(.bss)
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
} > SRAM
```

And add a stack canary:
```c
#define STACK_CANARY 0xDEADBEEF

void stack_check_init(void) {
    uint32_t *stack_bottom = &_estack - 0x1000;  /* 4KB from top */
    *stack_bottom = STACK_CANARY;
}

void stack_check_verify(void) {
    uint32_t *stack_bottom = &_estack - 0x1000;
    if (*stack_bottom != STACK_CANARY) {
        /* Stack overflow detected! */
        usart1_print("[FATAL] Stack overflow detected!\r\n");
        while(1) {}
    }
}
```

---

## Final Result

The MPU/cache configuration:
- SDRAM: XN=1, cacheable for data access (catches accidental execution)
- DMA buffers: explicit cache invalidation in parser
- Stack canary for overflow detection
- Ring buffer isolated at bottom of AXI SRAM

Ready for CS10: Power management with WFI.
