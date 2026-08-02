# Devlog: Version 4 (Hardware CRC Offloading & The Endianness Collision)

## What I'm Trying to Do
Now that `v3` uses DMA to assemble telemetry frames coherently without CPU intervention, the CPU's only remaining task is to validate the integrity of the packet by calculating its checksum. Calculating a 32-bit CRC in software involves expensive polynomial division loops or memory-heavy lookup tables.
My goal in `v4` is to leverage the STM32H7's dedicated **Hardware CRC-32 Engine** to compute the checksum in a single CPU cycle.

## Attempt 1: The Emulator Wall
I established a known-good CRC baseline by writing a 4-byte test packet (`"1234"`) into the `CRC_DR` register sequentially, byte-by-byte (`uint8_t`). To optimize the process for production, I cast the packet buffer to a `uint32_t*` and wrote all 4 bytes into the `CRC_DR` register in a single 32-bit memory instruction. 

```bash
make clean && make simulate
```

### The Output (Emulator Peripheral Missing)
```text
09:05:56.3385 [WARNING] sysbus: [cpu: 0x80001E8] WriteByte to non existing peripheral at 0x40023000, value 0x31.
09:05:56.3407 [WARNING] sysbus: [cpu: 0x80001F8] ReadDoubleWord from non existing peripheral at 0x40023000.
...
--- [EDP v4_hw_crc_endian] Hardware CRC32 Validation ---
[DEBUG] Sequential 8-bit CRC : 0x00000000
[DEBUG] Optimized 32-bit CRC : 0x00000000
```
Once again, the Renode functional emulator exhibited a hard limitation: the STM32H7 model lacks an implementation for the `CRC` peripheral block at `0x40023000`. All memory reads returned `0x00000000`, failing the validation pipeline. 

## Attempt 2: The Physical Silicon Endianness Trap
Unable to validate the hardware offload in Renode, I flashed the exact firmware to the physical STM32H753XI development board. On physical silicon, the CRC engine functioned, but the validation *still failed*:

`[ERROR] Endianness Collision! The 32-bit optimization destroyed the checksum.`

### Root Cause Analysis
When the 8-bit loop sequentially fed `'1'`, `'2'`, `'3'`, `'4'` into `CRC_DR`, the hardware polynomial processed them in the correct sequential stream order.
However, in my 32-bit optimization (`CRC_DR = *word_ptr`), the Little-Endian ARM Cortex-M7 core loaded the 4 bytes into the CPU register as `0x34333231`. The hardware CRC engine strictly processes the Most Significant Byte first (`0x34` -> `'4'`), effectively feeding the stream into the mathematical polynomial **backwards** (`"4321"` instead of `"1234"`). 

### The Fix
The STM32 hardware designers anticipated this architectural endianness collision. To fix it without sacrificing the 32-bit optimization speed, I updated the CRC initialization routine to leverage the **Reverse Input Data (`REV_IN`)** hardware feature.
By setting bits `[6:5]` of the `CRC_CR` register to `01` (Byte Reversal by Word), the hardware automatically reverses the byte order of incoming 32-bit writes *before* feeding them into the polynomial engine.

With `REV_IN` enabled, the physical board output perfectly matched the sequential 8-bit CRC computation, and the telemetry verification successfully executed in a single cycle. Moving to `v5_sdram_linker`.
