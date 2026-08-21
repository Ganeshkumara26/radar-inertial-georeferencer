# Embedded Edge Data Plane

A bare-metal firmware pipeline for the ARM Cortex-M7 (STM32H753XI), written from scratch and verified through Renode simulation plus physical silicon cross-checks.

## Why This Exists

Every embedded project starts the same way: grab a HAL library, pull in an RTOS, maybe add a middleware stack. Before you know it, your 100-line telemetry task has pulled in 50,000 lines of code nobody really understands. When something breaks, you're debugging through layers of abstraction instead of debugging the actual problem.

So I started with nothing. No HAL, no RTOS, no standard library. Just the Cortex-M7, the STM32 reference manual, and a lot of `volatile` pointer casts. Every peripheral was configured by hand, every fault was debugged on a real board, and every fix is documented below.

## What's Inside

`src/` contains the final firmware for the STM32H753XI, which includes:

- Memory-mapped USART output with polled transmission
- NVIC vectored interrupt reception with proper Thumb state setup
- MAVLink-compatible packet parsing with correct struct packing
- DMA offload for autonomous data movement with L1 D-Cache invalidation
- Hardware CRC-32 with `REV_IN` for byte-order correction
- 600KB external SDRAM circular buffer via the FMC
- PendSV-based context switching with proper exception priorities
- Thread-safe queue IPC between ISR and task context
- Adversarial fault resilience under stress testing
- WFI power-save with PRIMASK critical sections
- MPU configuration for execute-never SDRAM regions

Plus the linker script (`stm32h753.ld`) and startup code.

## Things That Broke (and How We Fixed Them)

These are the real, unglamorous bugs that happen when you write bare-metal firmware without a safety net:

- **UART not transmitting**: The vector table wasn't aligned correctly, so the CPU booted to the wrong handler. Fixed with `__attribute__((section(".isr_vector")))`.
- **ISR fires but data is lost**: NVIC ISER wasn't enabled, and the function pointer was missing the Thumb bit. Fixed by setting `NVIC_ISER` and OR-ing `| 0x1` on the interrupt handler address.
- **Struct packing mismatch**: A 6-byte MAVLink header followed by `float` fields caused GCC to insert 2 bytes of padding. Fixed with `__attribute__((packed))`.
- **DMA memory corruption**: The DMA writes directly to memory, bypassing the CPU's L1 D-Cache. When the CPU then reads that same memory, it gets stale cache data. Fixed with `SCB_InvalidateDCache_by_Addr()`.
- **CRC checksum mismatch**: The hardware CRC peripheral processes words MSB-first, but the CPU writes them LSB-first. Fixed by enabling `CRC_CR_REV_IN`.
- **Stack overflow**: The 600KB circular buffer and the task stack were both placed in AXI SRAM, and the growing buffer clobbered the stack. Fixed by linker script surgery to place them in different regions.
- **Context switch crash (Priority)**: PendSV was set to the highest priority, so it preempted everything including fault handlers. Fixed by moving PendSV to the lowest priority via `SCB_SHPR3`.
- **Context switch crash (Compiler)**: GCC's standard function prologue clobbered the hardware's `EXC_RETURN` token in the `lr` register before my inline assembly could read it. Fixed by rewriting the handler as a pure `asm volatile` block inside an `__attribute__((naked))` function.
- **Queue corruption**: The queue APIs used weren't ISR-safe, so calling them from an interrupt handler caused race conditions. Fixed by using `xQueueSendFromISR`.
- **Lost wakeups**: `WFI` and interrupt assertion had a race — if the interrupt fired between the WFI check and the sleep entry, the wake-up was lost. Fixed with PRIMASK critical sections.
- **SDRAM execute fault**: The MPU didn't have a region configured for the SDRAM execute region. Fixed with `MPU_RASR` allowing execution (`XN=0, EXECUTABLE=1`).

## Running This

```bash
# Make sure you have arm-none-eabi-gcc and renode installed
make clean && make simulate

# Output goes to uart_output.log
```

The Makefile handles everything: compilation, linking, flashing to Renode, and capturing UART output. For the CRC and MPU validation steps, you'll need physical STM32H753XI hardware — Renode's models for those peripherals are simplified.

## Directory Structure

| Directory | Description |
|-----------|-------------|
| [`src/`](src) | Firmware source, linker script, startup code |
| [`sim/`](sim) | Renode simulation script |
| [`scripts/`](scripts) | Build Makefile |
| [`docs/`](docs) | Architecture, protocol, verification, roadmap |
| [`archive/`](archive) | Complete version snapshots — each iteration is preserved, bugs and all |

## Lessons Learned

1. **The linker is your enemy and your friend.** The 600KB circular buffer worked fine until the linker decided to put the stack in the same memory region. Read the `.map` file every time.
2. **Renode's peripheral models are not complete.** DMAMUX1 isn't modeled. The CRC peripheral is simplified. PWR low-power states are approximate. We worked around every gap and cross-validated on real hardware. Simulation gives you velocity; silicon gives you truth.
3. **ARM exception priorities are non-obvious.** PendSV at priority 0 preempts everything, including fault handlers. Always set it to the lowest priority.
4. **Volatility is not optional.** Every peripheral register is `((volatile uint32_t *)addr)`. The optimizer *will* reorder your code and break hardware access if you forget it.
5. **The M4 and M7 are not the same chip.** The Cortex-M7 has L1 caches, an MPU, and a different NVIC layout. Cache coherency with DMA is a real thing that needs `SCB_CleanInvalidateDCache()`.
6. **Trust nothing, verify everything.** Every register write was confirmed by reading it back. Every interrupt was tested under load. If it's not in the Renode log or the silicon oscilloscope, it didn't happen.

## License

MIT
