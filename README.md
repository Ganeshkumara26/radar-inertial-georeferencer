# Deterministic Radar-Inertial Georeferencing & Target Tracking Coprocessor

A bare-metal firmware pipeline for the ARM Cortex-M7 (STM32H753XI) that solves the spatial drift problem in drone-based radar search-and-rescue. Sits between the flight controller and the radar SoC's digital backend, ingesting high-rate MAVLink attitude data, hardware-timestamping incoming radar frames, running a fixed-point Extended Kalman Filter to compensate for drone motion, and outputting precisely georeferenced target coordinates.

## Why This Exists

A radar detects a human breathing signature beneath the rubble. The raw detection says "range bin 47." But the drone is moving forward at 3 m/s and shifting due to wind. Range bin 47 at time T1 does not map to the same physical spot on the ground as range bin 47 at time T2. Without instantly translating local radar detections into global GPS/SLAM coordinates by factoring in the drone's exact attitude (roll, pitch, yaw) and velocity at the exact microsecond of the radar sweep, the rescue map drifts by meters. The visual overlay becomes useless.

You cannot run this tight attitude-to-radar synchronization on a Linux companion computer. OS scheduling jitter introduces milliseconds of latency spikes — an eternity when the drone moves centimeters in that time. This is where the STM32H7 Cortex-M7 core becomes indispensable: deterministic, low-latency, real-time peripheral handling and sensor fusion with zero OS overhead.

## System Context

```
Pixhawk (PX4/ArduPilot)
        │
        │ MAVLink @ 100+ Hz
        │ (ATTITUDE, LOCAL_POSITION_NED, GLOBAL_POSITION_INT)
        ▼
┌─────────────────────────────────┐
│   STM32H753XI Coprocessor       │
│   ┌───────────────────────────┐ │
│   │ Zero-Copy MAVLink Parser  │ │◄── DMA circular buffer, no CPU overhead
│   │ Hardware Timer Sync       │ │◄── Microsecond timestamps on radar frame-sync
│   │ Lock-Free State Buffer    │ │◄── Rolling kinematic history window
│   │ Fixed-Point EKF           │ │◄── CMSIS-DSP matrix operations
│   │ Coordinate Transform      │ │◄── Polar radar → ENU → lat/long
│   │ Deterministic Output      │ │──► SPI/USB-CDC to ground laptop
│   └───────────────────────────┘ │
└─────────────────────────────────┘
        │
        │ Georeferenced target metadata
        │ (lat, long, altitude, confidence, timestamp)
        ▼
Ground Laptop UI Overlay
```

## What's Inside

`src/` contains the firmware for the STM32H753XI, organized by pipeline stage:

- **Bare-metal boot** — vector table, Reset_Handler, USART debug output
- **MAVLink DMA parser** — zero-copy UART ingestion with DMA circular buffers and L1 D-Cache coherency
- **Hardware timer synchronization** — microsecond-precision timestamping bound to radar frame-sync interrupt
- **Lock-free ring buffer** — thread-safe kinematic state history for back-projection calculations
- **Fixed-point EKF** — optimized Extended Kalman Filter using CMSIS-DSP matrix libraries
- **Coordinate transformation** — polar radar coordinates to ENU/lat-long with velocity vector stripping
- **Deterministic output** — binary frame packaging over SPI/USB-CDC to ground station

Plus the linker script (`stm32h753.ld`) and startup code.

## Things That Broke (and How We Fixed Them)

These are the real bugs that happen when you build a deterministic georeferencing pipeline without a safety net:

- **Vector table misalignment**: The CPU booted to the wrong handler because the vector table wasn't placed in the `.isr_vector` section. Fixed with `__attribute__((section(".isr_vector")))`.
- **MAVLink packet loss**: NVIC ISER wasn't enabled, and the interrupt handler address was missing the Thumb bit. Fixed by setting `NVIC_ISER` and OR-ing `| 0x1` on the handler address.
- **Struct packing mismatch**: A 6-byte MAVLink header followed by `float` fields caused GCC to insert 2 bytes of padding, shifting all payload data. Fixed with `__attribute__((packed))`.
- **DMA cache coherency failure**: DMA writes directly to memory, bypassing the L1 D-Cache. CPU reads stale cached data instead of fresh DMA output. Fixed with `SCB_InvalidateDCache_by_Addr()`.
- **CRC endianness collision**: The hardware CRC peripheral processes words MSB-first, but the CPU writes them LSB-first, producing wrong checksums. Fixed by enabling `CRC_CR_REV_IN`.
- **Stack overflow into SDRAM buffer**: The large state buffer and task stack were both placed in AXI SRAM. Fixed by linker script surgery to isolate them.
- **PendSV priority inversion**: PendSV was set to highest priority, preempting fault handlers. Fixed by moving it to lowest priority via `SCB_SHPR3`.
- **Compiler clobbering EXC_RETURN**: GCC's function prologue overwrote the hardware's `EXC_RETURN` token in `lr` before inline assembly could read it. Fixed with `__attribute__((naked))` pure assembly handler.
- **Queue race condition**: Non-ISR-safe queue APIs called from interrupt context caused data corruption. Fixed with `xQueueSendFromISR`.
- **WFI lost-wakeup race**: Interrupt fired between the WFI check and sleep entry, losing the wakeup. Fixed with PRIMASK critical sections.
- **SDRAM execute fault**: MPU had no region configured for SDRAM code execution. Fixed with `MPU_RASR` allowing execution (`XN=0`).

## Running This

```bash
# Make sure you have arm-none-eabi-gcc and renode installed
make clean && make simulate

# Output goes to uart_output.log
```

The Makefile handles compilation, linking, loading into Renode, and UART output capture. For CRC and MPU validation steps, physical STM32H753XI hardware is required — Renode's models for those peripherals are simplified.

## Directory Structure

| Directory | Description |
|-----------|-------------|
| [`src/`](src) | Firmware source — pipeline stages, linker script, startup code |
| [`sim/`](sim) | Renode simulation script |
| [`scripts/`](scripts) | Build Makefile |
| [`docs/`](docs) | Architecture, protocol, verification, roadmap, engineering notebook |
| [`docs/engineering-notebook/`](docs/engineering-notebook) | 11 numbered devlog entries following the 2-month roadmap |

## Lessons Learned

1. **Spatial drift is a systems problem, not an algorithm problem.** A perfect EKF means nothing if your timestamps are jittery. Determinism starts at the hardware layer.
2. **The linker is your enemy and your friend.** The large state buffer worked fine until the linker put the stack in the same memory region. Read the `.map` file every time.
3. **Renode's peripheral models are not complete.** DMAMUX1 isn't modeled. The CRC peripheral is simplified. Cache coherency isn't enforced. We worked around every gap and cross-validated on real hardware.
4. **ARM exception priorities are non-obvious.** PendSV at priority 0 preempts everything, including fault handlers. Always set it to the lowest priority.
5. **Volatility is not optional.** Every peripheral register is `((volatile uint32_t *)addr)`. The optimizer *will* reorder your code and break hardware access if you forget it.
6. **Trust nothing, verify everything.** Every register write was confirmed by reading it back. Every interrupt was tested under load. If it's not in the Renode log or the silicon oscilloscope, it didn't happen.

## License

MIT
