# Devlog: Case Study 01 — Pipeline Foundation & Bare-Metal Boot

## What I'm Trying to Do

Before ingesting MAVLink telemetry or running an EKF, I need a verified bare-metal execution environment for the **STM32H753XI Cortex-M7** that serves as the foundation for the georeferencing coprocessor. The firmware must boot from internal Flash, initialize the vector table, configure USART1 for debug output, and establish the memory map that the entire pipeline will use: AXI SRAM for ring buffers and EKF state matrices, FMC SDRAM for large kinematic history windows.

---

## Attempt 1: Getting the Build System Right

I created the full pipeline source tree — `main.c`, `startup.c`, `mavlink_parser.c/h`, `hw_timer.c/h`, `ring_buffer.c/h`, `ekf.c/h`, `geo_transform.c/h`, `output_stream.c/h` — and a Makefile to build them all.

### The Output (Makefile Path Resolution Error)
```text
make: *** No rule to make target 'build/main.o', needed by 'build/edp_m7_firmware.elf'.  Stop.
```

### My Mistake & Root Cause Analysis

My original Makefile used `$(PROJ_ROOT)` variable for source paths and a pattern rule `$(BUILD_DIR)/%.o: $(PROJ_ROOT)/src/%.c`. The pattern substitution `$(SRCS:$(PROJ_ROOT)/src/%.c=$(BUILD_DIR)/%.o)` failed silently because Make's `patsubst` doesn't expand nested variables in the way I expected — `$(PROJ_ROOT)` is `..`, and Make's pattern matching doesn't handle `../src/%.c` → `build/%.o` cleanly when the prefix contains a directory traversal.

### The Fix

I replaced the pattern rule with explicit per-file rules for each source source file:
```makefile
$(BUILD_DIR)/main.o: $(SRC_DIR)/main.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@
```
Explicit rules are less elegant but don't depend on Make's pattern expansion quirks.

---

## Attempt 2: The IRQn Suffix Typo

With explicit rules, compilation started — and immediately failed.

### The Output (Undeclared Identifier)
```text
src/mavlink_parser.c:130:26: error: 'DMA1_STREAM0_IRQN' undeclared
src/hw_timer.c:92:27: error: 'EXTI15_10_IRQN' undeclared
```

### My Mistake & Root Cause Analysis

I used `DMA1_STREAM0_IRQN` and `EXTI15_10_IRQN` (trailing `N`) instead of the correct `DMA1_STREAM0_IRQn` and `EXTI15_10_IRQn` (trailing `n`, lowercase). The CMSIS headers for STM32H7 use the `IRQn` suffix with a lowercase `n`. The uppercase `N` variant doesn't exist anywhere in the vendor headers.

This is a pure typo — but it's insidious because both forms *look* correct to a human reader, and neither the editor nor the compiler gives you a "did you mean..." hint that's close enough to matter.

### The Fix

```c
NVIC_ICPR0 = (1UL << DMA1_STREAM0_IRQn);   // was DMA1_STREAM0_IRQN
NVIC_ISER0 = (1UL << DMA1_STREAM0_IRQn);
```

---

## Attempt 3: The Linker Refuses libc

All source files compiled, but linking failed.

### The Output (Undefined References)
```text
undefined reference to `memcpy'
undefined reference to `memset'
undefined reference to `__aeabi_l2d'
undefined reference to `__errno'
```

### My Mistake & Root Cause Analysis

I was using `-nostdlib` (correct for bare-metal — no hosted environment) but linking with `-lm` for the math library (`sqrtf`, `sinf`, `cosf`, `atan2f`). The math library was compiled **against** newlib and expects three things that `-nostdlib` strips away:

1. **`memcpy`/`memset`** — newlib's libm uses these internally for setup
2. **`__aeabi_l2d`** — ARM EABI long-to-double conversion, provided by libgcc
3. **`__errno`** — newlib's errno location pointer, used by math error handlers

The fix requires satisfying these dependencies without pulling in the full hosted libc.

### The Fix

Three linker additions:
```makefile
LDFLAGS += -lc -lnosys -lgcc
```
- `-lc`: links newlib-nano (already using `--specs=nano.specs`)
- `-lnosys`: provides `_sbrk`, `_write`, `_read` stubs via `--specs=nosys.specs`
- `-lgcc`: provides `__aeabi_*` helper functions

Plus a minimal `syscalls.c` providing `__errno`:
```c
int *__errno(void) {
    static int _errno = 0;
    return &_errno;
}
```
In a single-threaded bare-metal system, errno is just a global int. No OS, no threads, no reentrancy concerns.

---

## Final Result: Clean Build

```bash
$ make -f scripts/Makefile clean && make -f scripts/Makefile all
arm-none-eabi-size build/edp_m7_firmware.elf
   text	   data	    bss	    dec	    hex	filename
  19568	    232	  12400	  32200	   7dc8	build/edp_m7_firmware.elf
```

The georeferencing coprocessor firmware compiles and links cleanly. 19.5KB of code in Flash, 232 bytes of initialized data, 12.4KB BSS (ring buffers, EKF matrices, output frame buffer). Ready to advance to CS02: MAVLink DMA parser validation.
