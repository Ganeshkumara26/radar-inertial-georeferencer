# Devlog: Version 5 (External SDRAM Linker Surgery)

## What I'm Trying to Do
With telemetry packets successfully validating their CRCs in hardware (`v4`), the firmware now needs to store thousands of incoming orientation packets before committing them to persistent flash storage. We need a massive **600 KB Circular Buffer**. 

However, the STM32H753XI microcontroller only has 512 KB of contiguous internal `AXI_SRAM` mapped at `0x24000000`. To store 600 KB, we must utilize the external SDRAM chip mounted on the evaluation board, which is interfaced via the **Flexible Memory Controller (FMC)** at Bank 2 (`0xD0000000`).

## Attempt 1: The AXI_SRAM `.bss` Overflow
Without applying explicit memory section attributes, the GCC compiler treated our `volatile uint8_t telemetry_circular_buffer[600 * 1024];` variable as standard uninitialized data. The default linker script (`stm32h753.ld`) directed all uninitialized data into the `.bss` section, which is mapped strictly to `AXI_SRAM`.

```bash
make clean && make
```

### The Output (Linker Failure)
```text
/usr/lib/gcc/arm-none-eabi/bin/ld: build/edp_m7_firmware.elf section `.bss' will not fit in region `SRAM'
/usr/lib/gcc/arm-none-eabi/bin/ld: region `SRAM' overflowed by 94208 bytes
```
The compilation correctly and deterministically failed. The linker identified that attempting to pack 600 KB of data into a 512 KB physical hardware region would catastrophically overwrite stack memory or fault during execution.

## Attempt 2: Linker Script Surgery & Memory Attributes
To physically relocate the massive buffer to external SDRAM, I performed linker surgery on `stm32h753.ld`:
1. Mapped the FMC Bank 2 SDRAM into the `MEMORY` block (`SDRAM (xrw) : ORIGIN = 0xD0000000, LENGTH = 32M`).
2. Created a dedicated custom section `> SDRAM` named `.sdram_data (NOLOAD)`.

In `main.c`, I decorated the buffer declaration with `__attribute__((section(".sdram_data")))`.

### The Relocation Truncation Limit Nuance
It's critical to note why this succeeds without throwing an `R_ARM_THM_JUMP24` relocation truncation error. The delta between Flash (`0x08000000`) and SDRAM (`0xD0000000`) is roughly 3.3 GB. If I had placed an executable *function* in SDRAM, the ARM Thumb-2 branch instructions (`BL`) would fail at link time because their maximum relative jump distance is ±16 MB. 
However, because we only placed *data* in SDRAM, the compiler uses `MOVW` (Move Wide) and `MOVT` (Move Top) instruction pairs, which can construct and load absolute 32-bit addresses without relative branch limitations.

### The Output (Verified SDRAM Execution)
```bash
make clean && make simulate
```
```text
arm-none-eabi-size build/edp_m7_firmware.elf
   text    data     bss     dec     hex filename
    492     228  618496  619216   972d0 build/edp_m7_firmware.elf

09:08:03.8890 [INFO] sysbus: Loading block of 614400 bytes length at 0xD0000000.
...
--- [EDP v5_sdram_linker] External SDRAM Mapping ---
[STATUS] Writing to 600KB telemetry buffer...
[SUCCESS] Massive buffer boundaries accessed successfully.
```
The firmware compiled successfully, pushing the `.bss` equivalent block up to 618 KB. 
Renode successfully caught the ELF segment mapped to `0xD0000000`, loaded the massive 614,400-byte block into its internal emulator SDRAM model, and the simulated Cortex-M7 successfully accessed both boundaries (`0` and `600 * 1024 - 1`) via 32-bit absolute addressing. Milestone verified.
