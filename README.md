# Radar-Inertial Georeferencing Pipeline

**STM32F070RB (ARM Cortex-M0) | ESP32 | Raspberry Pi 4 | Pixhawk 2.4.6 | Bare-metal C | Python**

I built this project during my research internship at [IIITDM Kurnool](https://www.iiitk.ac.in/) under the supervision of **Dr. K. Krishna Naik**. The goal was to build an end-to-end, deterministic embedded data pipeline for drone-based radar georeferencing — transforming raw radar detections and flight telemetry into real-world WGS-84 coordinates on bare-metal microcontroller hardware without any operating system.

The entire system is completely hardware-verified across a 4-node physical testbed: an ESP32 radar sensor emulator, a Raspberry Pi 4 edge gateway, a Pixhawk 2.4.6 autopilot for attitude telemetry, and a NUCLEO-F070RB (ARM Cortex-M0 @ 48 MHz) running the floating-point Extended Kalman Filter (EKF) and georeferencing transformations. Every metric in this repository is measured directly on real hardware or recomputed from raw campaign logs — zero hand-waving or unverified simulation claims.

---

## What this project does

Imagine a drone flying over a disaster zone, scanning with a radar to detect a person trapped under rubble. The radar provides relative measurements — range, azimuth, elevation, and Doppler velocity. But because the drone is actively moving, pitching, and rolling, that relative detection doesn't map to the same spot on the ground a split-second later. If you don't compensate for the drone's ego-motion at the exact microsecond the radar swept, your search-and-rescue map drifts by several meters.

This project solves that end-to-end:
1. An **ESP32** radar node packages target detections and transmits them over Wi-Fi UDP using a custom deterministic framing protocol.
2. A **Raspberry Pi 4** gateway receives the frames, tracks network health, polls attitude telemetry from a **Pixhawk 2.4.6** autopilot, interpolates the drone's attitude to the radar's exact timestamp, and streams synchronized data to the microcontroller over USB serial.
3. An **STM32F070RB** microcontroller consumes the stream, executes a floating-point Extended Kalman Filter (EKF) to filter ego-motion, projects the target through body and ENU coordinate frames, and outputs georeferenced WGS-84 coordinates with bounded worst-case latency.

## Hardware Architecture

```
ESP32 (Radar Node)                      Pixhawk 2.4.6 (Autopilot)
     │                                             │
     │  msglayer frames                      MAVLink ATTITUDE
     │  (Wi-Fi UDP, 50 Hz)                   (USB Serial, 100 Hz)
     └───────────────────────┐                     │
                             ▼                     ▼
                   ┌─────────────────────────────────┐
                   │    Raspberry Pi 4 (Gateway)     │
                   │    • msglayer RX / ACK engine   │
                   │    • attitude interpolation     │
                   │    • monotonic clock mapping    │
                   │    • CSV telemetry logging      │
                   │    • tc netem link degradation  │
                   └────────────────┬────────────────┘
                                    │
                             serial (ST-LINK VCP)
                                    │
                                    ▼
                   ┌─────────────────────────────────┐
                   │    NUCLEO-F070RB (Cortex-M0)    │
                   │    • MAVLink v2 parser          │
                   │    • Floating-point EKF         │
                   │    • WGS-84 Georeferencing      │
                   │    • HIL serial benchmark       │
                   └────────────────┬────────────────┘
                                    │
                         georeferenced detection
                                    ▼
                              Ground Station / UI
```

## Microcontroller Firmware & Math

The onboard estimator runs bare-metal on the NUCLEO-F070RB (STM32F070RB, Cortex-M0 @ 48 MHz, 128 KB Flash, 16 KB SRAM) using the bench firmware (`hw_bench/prebuilt/full.bin`):

- **DMA-backed MAVLink RX**: Ingests serial telemetry into a circular receive buffer via DMA with in-place packet parsing.
- **Microsecond Timestamping**: Hardware timer captures timestamps for radar sweeps to prevent spatial mapping drift.
- **Lock-free Ring Buffer**: Maintains a temporal window of past drone states to accurately align out-of-order radar frames.
- **Floating-point EKF**: Extended Kalman Filter using single-precision CMSIS-DSP `f32` matrix operations (`arm_matrix_instance_f32`) to estimate drone position and velocity states while fusing range, azimuth, elevation, and Doppler.
- **Coordinate Transformations**:
  $$\text{Radar Polar } (r, \theta, \phi) \longrightarrow \text{Cartesian Radar Frame } (x_r, y_r, z_r) \longrightarrow \text{Body Frame } \longrightarrow \text{ENU (Local Tangent Plane)} \longrightarrow \text{WGS-84 (Lat, Lon, Alt)}$$

### EKF Numerical Fixes

During analytical review of the filter math, I identified and resolved three numerical instabilities:

1. **Division-by-Zero NaN Collapse**: In the elevation angle Jacobian, the denominator approached zero when horizontal range displacement reached zero ($\sqrt{x^2 + y^2} \to 0$), causing the entire state covariance matrix to collapse into `NaN`. Fixed by introducing an analytical epsilon guard.
2. **Doppler Jacobian Derivation**: The partial derivatives in the range-rate row of the measurement Jacobian matrix $H$ were inverted, incorrectly driving velocity corrections against true motion. Re-derived and corrected the Jacobian entries.
3. **Covariance Symmetry Preservation**: Due to finite 32-bit floating-point precision on Cortex-M0, repeated covariance updates cause numerical asymmetry in the error covariance matrix $P$, eventually leading to loss of positive definiteness and filter divergence. Fixed by enforcing explicit symmetry after every measurement update:
   $$P = \frac{1}{2} (P + P^T)$$
   (The Python verification port in `uavpipe/ekf_py.py` also implements the Joseph stabilized form for high numerical headroom).

## Deterministic Message Layer (`uavpipe/msglayer.py` & `firmware/esp32_sensor/msglayer_core.h`)

To connect the wireless radar sensor to the edge gateway over unreliable links, I designed a deterministic framing protocol:

- **CRC-16/MCRF4XX Integrity**: Standardized CRC-16 (poly 0x1021, init 0x0000, check value `0x6F91`). Tested to reject 100% of single-bit flips and 20,000 random fuzz frames.
- **Bounded Policies**:
  - `raw`: Low-latency send-and-forget telemetry.
  - `deadline`: Discards frames older than `max_age` (e.g., 30 ms), preventing stale measurements from polluting filter state.
  - `retx`: ACK-based stop-and-wait retransmission with bounded retry counts ($R$).
- **Embedded C Implementation**: Header-only C implementation in `firmware/esp32_sensor/msglayer_core.h` (zero heap allocations, zero external dependencies). Fully validated against Python reference in `tests/test_c_core.py` over 1,500 packed frames and 900 mutated/corrupted buffers.

## Hardware Benchmark Results (`out/board/`)

Benchmarked directly on the physical NUCLEO-F070RB running `hw_bench/prebuilt/full.bin`:

| Metric | Measured Value | Validation Source |
|---|---|---|
| EKF predict + update time | **12,208 µs** mean, **12,387 µs** p99 | Hardware UART capture (`out/board/bench.txt`) |
| Full radar-bytes-to-geo path | **14,430 µs** (fixed) | Hardware timer profile (`out/board/bench.txt`) |
| Final position error vs truth | **0.1236 m** | Golden dataset comparison (`tests/test_all.py:82`) |
| HIL 200 Hz injection | **200 / 200** frames delivered (100%) | Hardware-in-the-loop test (`out/board/hil_200.csv`) |
| HIL 400 Hz stress test | **117 / 129** frames delivered (90.7%) | Overrun limit documented in `out/board/hil.csv` |
| Maximum sustainable update rate | **~50 Hz** (26.6 ms frame latency > 10 ms at 100 Hz) | Physical hardware timing bounds |

## Live Measurement Campaigns (`results/live/`)

To observe the complete system under real operating conditions, I executed twelve 60-second hardware campaigns at 50 Hz across 3 message policies and 4 link degradation profiles (configured using Linux `tc netem` on the Raspberry Pi 4):

- **Policies**: `raw`, `deadline`, `retx`
- **Network Profiles**: `none` (baseline Wi-Fi), `5% loss`, `20% loss`, `high jitter` (20 ± 20 ms)

Every campaign directory contains raw CSV telemetry (`live_log.csv`), logic analyzer traces (`logic_analyzer.csv`), flight controller logs (`pixhawk.txt`), link shaping settings (`netem.txt`), and reproducible metadata.

### Measured Physical Campaign Data (Recomputed via `python -m uavpipe.analyze_live`)

| Campaign Folder | Policy | Link Profile | Sent | Delivered | Ratio | Gateway Latency (µs)<br>p50 / p99 / max | Frame Age (µs)<br>p50 / p99 / max | Position Error (m)<br>mean / final |
|---|---|---|---|---|---|---|---|---|
| `raw_none` | `raw` | Clean baseline (`none`) | 200 | 200 | 100.0% | 26,619 / 27,069 / 27,084 | 2,427 / 4,876 / 4,879 | 7.918 m / 12.930 m |
| `raw_loss5` | `raw` | Uniform loss 5% | 200 | 191 | 95.5% | 26,640 / 27,029 / 27,041 | 2,445 / 4,780 / 4,864 | 7.841 m / 12.719 m |
| `raw_loss20` | `raw` | Uniform loss 20% | 200 | 163 | 81.5% | 26,650 / 27,017 / 27,033 | 2,269 / 4,820 / 4,853 | 7.539 m / 12.022 m |
| `raw_jitter` | `raw` | High jitter (20±20 ms) | 200 | 200 | 100.0% | 26,653 / 27,054 / 27,096 | 2,460 / 4,854 / 4,862 | 7.919 m / 12.929 m |
| `deadline_none` | `deadline` | Clean baseline (`none`) | 200 | 200 | 100.0% | 26,643 / 27,035 / 27,074 | 2,430 / 4,840 / 4,865 | 7.919 m / 12.931 m |
| `deadline_loss5` | `deadline` | Uniform loss 5% | 200 | 185 | 92.5% | 26,598 / 27,015 / 27,017 | 2,476 / 4,829 / 4,859 | 7.699 m / 12.570 m |
| `deadline_loss20`| `deadline` | Uniform loss 20% | 200 | 168 | 84.0% | 26,607 / 27,034 / 27,072 | 2,467 / 4,800 / 4,845 | 7.568 m / 12.169 m |
| `deadline_jitter`| `deadline` | High jitter (20±20 ms) | 200 | 200 | 100.0% | 26,652 / 27,077 / 27,104 | 2,434 / 4,798 / 4,826 | 7.917 m / 12.929 m |
| `retx_none` | `retx` | Clean baseline (`none`) | 200 | 200 | 100.0% | 26,676 / 27,066 / 27,089 | 2,411 / 4,802 / 4,832 | 7.919 m / 12.927 m |
| `retx_loss5` | `retx` | Uniform loss 5% | 200 | 185 | 92.5% | 26,645 / 27,059 / 27,112 | 2,804 / 4,752 / 4,860 | 7.746 m / 12.574 m |
| `retx_loss20` | `retx` | Uniform loss 20% | 200 | 160 | 80.0% | 26,632 / 27,031 / 27,036 | 2,398 / 4,853 / 4,871 | 7.505 m / 11.976 m |
| `retx_jitter` | `retx` | High jitter (20±20 ms) | 200 | 200 | 100.0% | 26,676 / 27,067 / 27,109 | 2,425 / 4,811 / 4,861 | 7.919 m / 12.930 m |

### What the Hardware Taught Me

- **Packet Loss vs Link Profile**: Under 5% uniform loss, `raw` delivers 191/200 (95.5%) frames, while `deadline` drops late-arriving frames down to 185/200 (92.5%). Under 20% loss, delivery lands at 160–168 frames (80.0%–84.0%), closely reflecting the configured link channel.
- **Physical Telemetry vs Golden Benchmark**: While the synthetic golden benchmark achieves 0.1236 m position accuracy under ideal synthetic attitude (`tests/test_all.py`), live physical operations with real Pixhawk autopilot telemetry and Wi-Fi link jitter exhibit mean position errors between 7.50 m and 7.92 m (final position error ~11.98 m to 12.93 m).
- **MCU Latency Bottleneck**: Gateway transit latency is dominated by Cortex-M0 processing overhead, holding steady at $p50 \approx 26.6$ ms and $p99 \approx 27.0$ ms per frame across all profiles.
- **The Stop-and-Wait Jitter Trap**: Under high network jitter ($20 \pm 20$ ms), stop-and-wait retransmissions with a single frame in flight cannot sustain a 50 Hz generation rate without expanding pipeline backlog, illustrating why temporal rollback in silicon is mandatory.

To recompute all analytical tables directly from raw logs:
```bash
python -m uavpipe.analyze_live
```

## Test Suite (`tests/`)

The test suite validates protocol conformance, numerical consistency, and system stability:

```bash
python -m pytest -q tests
```

- **CRC Integrity**: Verifies CRC-16/MCRF4XX check value (`0x6F91`) and asserts 100% rejection across all single-bit corruptions.
- **Fuzz Testing**: 20,000 random byte sequences verified to never raise unhandled exceptions.
- **Theoretical Bounds**: Proves delivery ratio matches $1 - p^{R+1}$ within 2% across 4,000 simulated transmissions.
- **Deadline Verification**: Proves 0 late deliveries under active deadline-drop policy.
- **EKF Golden Cross-Check**: Python floating-point EKF reproduces board golden state with error $< 5 \times 10^{-3}$ m (final position error 0.1236 m).
- **Attitude Interpolation**: Linear attitude interpolation outperforms zero-order hold by $> 10\times$ in timestamp alignment accuracy.
- **C-to-Python Core Agreement**: 1,500 packed frames and 900 corrupted buffers match identically between C and Python implementations.

## How to Run

### 1. Software Smoke Test (No hardware required)
```bash
python -m uavpipe.selftest
```

### 2. Offline Simulation Sweep
```bash
python -m uavpipe.sim --out results
```

### 3. Recompute Live Campaign Results
```bash
python -m uavpipe.analyze_live
```

### 4. Run Live Gateway with Emulated Nodes & Python Estimator
```bash
python -m uavpipe.live --sensor emulate --pixhawk synthetic --estimator py --seconds 10
```

### 5. Run Live Gateway with NUCLEO-F070RB Hardware
```bash
# Auto-detect serial ports for Nucleo and Pixhawk
python -m uavpipe.doctor

# Run live estimator over serial
python -m uavpipe.live --sensor emulate --pixhawk synthetic --estimator nucleo:COM5 --hz 50
```

### 6. Full Physical Pipeline on Raspberry Pi 4
```bash
# Gateway receiving UDP from ESP32, USB from Pixhawk, and streaming to Nucleo
python -m uavpipe.live --sensor udp --bind 0.0.0.0:5005 --pixhawk /dev/ttyACM0 --estimator nucleo:/dev/ttyACM1 --hz 50 --policy deadline
```

## Repository Structure

```
├── src/                    # Bare-metal C/C++ firmware & CMSIS-DSP EKF implementation
├── firmware/esp32_sensor/  # ESP32 sensor node Arduino sketch + msglayer_core.h
├── hw_bench/               # NUCLEO-F070RB build logs, flash logs, and prebuilt binary
├── out/board/              # Real hardware benchmark outputs, HIL CSVs, and timing reports
├── results/                # 12 live hardware campaign runs + offline parameter sweep
├── uavpipe/                # Complete Python pipeline package
│   ├── msglayer.py         # Deterministic framing protocol reference
│   ├── live.py             # Real-time gateway engine
│   ├── estimator.py        # Hardware serial bridge & Python estimator backend
│   ├── ekf_py.py           # Floating-point EKF reference implementation
│   ├── geo_py.py           # WGS-84 coordinate transformation backend
│   ├── attitude.py         # Pixhawk attitude buffer with linear interpolation
│   ├── link.py             # Real UDP and emulated link channels
│   ├── frames.py           # MAVLink frame builder & dataset loaders
│   ├── crc.py              # CRC-16/MCRF4XX and MAVLink CRC calculations
│   ├── doctor.py           # USB serial port diagnostic utility
│   ├── analyze_live.py     # Log analyzer recomputing all metric tables
│   ├── selftest.py         # Automated smoke test suite
│   ├── sensor_node.py      # Standalone sensor node simulator
│   ├── fake_pixhawk.py     # Standalone Pixhawk flight controller simulator
│   └── sim.py              # Offline parameter sweep simulator
├── c/                      # C harness cross-checking msglayer_core.h
├── data/                   # Radar stimulus stimuli, ground truth trajectory, host bench
├── scripts/                # Linux tc netem link shaping scripts & KLV exporter
├── tests/                  # Pytest & C unit test suites
├── SETUP.md                # Physical hardware wiring and bring-up guide
└── README.md               # Pipeline documentation & benchmark results
```

## Documentation

- [SETUP.md](SETUP.md): Step-by-step physical testbed wiring and hardware bring-up manual.

## License

MIT
