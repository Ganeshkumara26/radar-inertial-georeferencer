# Radar-Inertial Georeferencing Coprocessor

**STM32H753XI | Bare-metal C | ARM Cortex-M7**

This is my project done during a 2-month internship at Bharat 5G Labs, IIITDM Kurnool, under the guidance of Dr. K. Krishna Naik sir. My main task was to separate the hardware from the software by designing an Edge Data Plane coprocessor that parses drone telemetry and processes it before reaching the server.

## The Problem

One critical problem in Cognitive Search and Rescue (CSSR) is spatial drift. When a radar detects a breathing human under rubble, the raw detection says "range bin 47" — but the drone is moving at 3 m/s and drifting with the wind. Without translating that detection into global coordinates at the exact microsecond of the radar sweep, the rescue map drifts by meters. 

You can't run this on a Linux companion computer — OS jitter introduces milliseconds of latency spikes, which is forever when the drone moves centimeters in that time. That's why we need a microcontroller: deterministic, low-latency, zero OS overhead. This coprocessor sits between the flight controller and the radar, parses MAVLink attitude data, timestamps radar frames with microsecond precision, runs a fixed-point EKF to compensate for drone motion, and outputs actual lat/long coordinates to the server.

## How It Works

```
Pixhawk (PX4/ArduPilot)
        │
        │ MAVLink @ 100+ Hz
        ▼
┌─────────────────────────────┐
│   STM32H753XI Coprocessor   │
│                             │
│  DMA MAVLink Parser         │ ← zero-copy, no CPU overhead
│  Hardware Timer Sync        │ ← microsecond timestamps
│  Lock-Free Ring Buffer      │ ← rolling state history
│  Fixed-Point EKF            │ ← CMSIS-DSP matrix ops
│  Coordinate Transform       │ │ polar → ENU → lat/long
│  Output Stream              │ → SPI/USB-CDC
└─────────────────────────────┘
        │
        ▼
Ground Laptop (target overlay)
```

## What's in This Repo

- `src/` — firmware source, organized by pipeline stage
- `sim/` — Renode simulation scripts
- `scripts/` — build Makefile
- `docs/` — architecture notes, engineering notebook, verification

## Things That Broke (and How I Fixed Them)

Building bare-metal firmware means you hit problems that an OS would hide from you:

- **Vector table misalignment** — CPU booted to the wrong handler. Fixed with `__attribute__((section(".isr_vector")))`.
- **MAVLink packet loss** — NVIC ISER wasn't enabled and the handler address was missing the Thumb bit. Fixed by setting ISER and OR-ing `| 0x1`.
- **EKF divergence due to matrix aliasing** — CMSIS-DSP `arm_mat_mult_f32` corrupts intermediate matrices if source and destination share the same memory (e.g. `P * H^T` and `K`). Fixed by allocating distinct statically-sized scratch buffers (`scratch_PHT`, `scratch_K`) for every step.
- **Struct packing mismatch** — GCC inserted padding in the MAVLink header, shifting all payload data. Fixed with `__attribute__((packed))`.
- **DMA cache coherency** — CPU read stale cached data instead of fresh DMA writes. Fixed with `SCB_InvalidateDCache_by_Addr()`.
- **CRC endianness** — hardware CRC processes MSB-first but CPU writes LSB-first. Fixed with `CRC_CR_REV_IN`.
- **Stack overflow into SDRAM** — linker put the stack in the same region as the state buffer. Fixed by linker script surgery.
- **PendSV priority inversion** — PendSV at highest priority was preempting fault handlers. Moved to lowest priority.
- **Compiler clobbering EXC_RETURN** — GCC's prologue overwrote the hardware token in `lr`. Fixed with `__attribute__((naked))`.
- **Queue race condition** — non-ISR-safe API called from interrupt context. Fixed with `xQueueSendFromISR`.
- **WFI lost-wakeup race** — interrupt fired between check and sleep. Fixed with PRIMASK critical sections.
- **SDRAM execute fault** — MPU had no region configured. Fixed with `MPU_RASR`.

## Running It

```bash
make clean && make simulate
```

Output goes to `uart_output.log`. The Makefile handles compilation, linking, loading into Renode, and UART capture. Some things (CRC, MPU) need real hardware — Renode's models for those are simplified.

## What I Learned

1. **Spatial drift is a systems problem, not just an algorithm problem.** A perfect EKF means nothing if your timestamps are jittery.
2. **The linker is your enemy and your friend.** Things work fine until the linker puts your stack in the same region as your data buffer. Read the `.map` file.
3. **Renode's peripheral models aren't complete.** DMAMUX1 isn't modeled. Cache coherency isn't enforced. I worked around the gaps and cross-validated on real hardware.
4. **ARM exception priorities are non-obvious.** PendSV at priority 0 preempts fault handlers. Always lowest priority.
5. **Trust nothing, verify everything.** Every register write was confirmed by reading it back. Every interrupt was tested under load.

## License

MIT
