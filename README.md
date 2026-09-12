# Radar-Inertial Georeferencing Coprocessor

**STM32H753XI | Bare-metal C | ARM Cortex-M7**

This project implements a real-time embedded radar-inertial georeferencing pipeline for drone-based search-and-rescue. It associates radar detections with timestamped vehicle states, compensates for platform motion, and transforms detections from the radar frame into a global navigation frame. When a radar gets a hit, it gives us a relative detection based on phase and time-of-flight (it's way more complex than just "range bin 47") — but the drone is moving, so that detection doesn't map to the same global spot a second later. This coprocessor sits between the flight controller and the radar, takes in MAVLink attitude data, timestamp each radar-frame event using a hardware timer at microsecond resolution, runs a fixed-point EKF to compensate for drone motion, and outputs a georeferenced latitude/longitude estimate.

## The Problem

A radar detects a breathing human under rubble. But the drone is moving at 3 m/s and drifting with wind. Without translating that detection into global coordinates at the exact microsecond of the radar sweep, the rescue map drifts by meters. A general-purpose Linux companion computer introduces scheduling, driver, cache, and software-stack variability into the timing path unless specifically engineered for real-time operation. This design instead keeps the latency-critical telemetry, timestamping, and estimation path on a microcontroller with bounded firmware execution.

## How It Works

```text
             Pixhawk / FC
                  │
          MAVLink telemetry
                  │
                  ▼
        ┌──────────────────────┐
        │   STM32H753XI        │
        │                      │
Radar ─►│  Timestamping        │
        │  MAVLink RX          │
        │  State History       │
        │  Fixed-Point EKF     │
        │  Frame Transform     │
        └──────────┬───────────┘
                   │
          Georeferenced detection
                   ▼
             Ground / UI
```

## The Architecture

Instead of just slapping a Raspberry Pi on the drone, we went bare-metal on an ARM Cortex-M7. The STM32 receives radar detections/measurements rather than raw radar samples, and processes them through the following pipeline:

- **DMA-backed MAVLink RX**: Transfers telemetry into memory with minimal CPU intervention. DMA places incoming telemetry into a preallocated receive buffer, allowing the parser to consume packets in place without an intermediate copy.
- **Hardware Timer Timestamping**: microsecond-resolution frame timestamps. If your timestamps are off by even a little, your whole map is garbage.
- **Lock-Free Ring Buffer**: A timestamped lock-free ring buffer stores recent navigation states so delayed radar measurements can be associated with the appropriate historical state and corrected without corrupting concurrent producer/consumer access.
- **Fixed-Point EKF**: The heart of the system. We run an Extended Kalman Filter using CMSIS-DSP fixed-point math to fuse the inertial data and radar detections, compensating for the drone's ego-motion in real time.
- **Coordinate Transform**: Converts the radar's raw polar coordinates (range, azimuth, elevation) into a local ENU (East, North, Up) frame, and then maps the local ENU detection into the global navigation frame to obtain a latitude/longitude estimate.

## EKF Math Bugs (and How I Fixed Them)

While analytically evaluating the EKF, I found and resolved 3 critical numerical bugs that were tanking the filter:

- **Division-by-zero NaN state collapse**: The elevation-row Jacobian term (`r2rg = r2 * r_ground`) used an unguarded `r2` value, causing the entire state vector to become NaN at `x = 0`. Fixed by properly guarding against division by zero.
- **Incorrect Doppler Jacobian**: The measurement model's Jacobian matrix `H` had a derivative error in the Doppler velocity term, causing updates to pull the state in the wrong direction. Re-derived the math and corrected the partial derivatives.
- **Covariance loss of symmetry/positive definiteness**: Fixed-point rounding caused numerical degradation of `P` over repeated updates. Replaced the conventional covariance update with the Joseph form to improve numerical robustness under finite-precision arithmetic (note: this increases instructions per update by ~49.9%).

## Things That Broke (and How I Fixed Them)

Building bare-metal firmware means you hit problems that an OS would normally hide from you:

- **Vector table misalignment** — CPU booted to the wrong handler. Fixed with `__attribute__((section(".isr_vector")))`.
- **MAVLink packet loss** — NVIC ISER wasn't enabled for the peripheral interrupts. Fixed by writing to the `NVIC_ISER0` register.
- **Struct packing mismatch** — GCC inserted padding in the MAVLink header, shifting all payload data. Fixed with `__attribute__((packed))`.
- **DMA cache coherency** — CPU read stale cached data instead of fresh DMA writes. Fixed with `SCB_InvalidateDCache_by_Addr()`.
- **CRC convention mismatch** — Hardware and software CRC calculations used different input bit/byte ordering conventions. Matched the STM32 CRC peripheral's input-reversal configuration (`CRC_CR_REV_IN`) to the software reference implementation.
- **Stack overflow into SDRAM** — linker put the stack in the same region as the state buffer. Fixed by linker script surgery.

## Running It

```bash
make clean && make simulate
```

Output goes to `uart_output.log`. The Makefile handles compilation, linking, loading into Renode, and UART capture. Some things (CRC, MPU) need real hardware — Renode's models for those are simplified.

## License

MIT
