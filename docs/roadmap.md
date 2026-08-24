# Roadmap

This project follows a 2-month execution plan for building a deterministic radar-inertial georeferencing coprocessor on the STM32H753XI. Each case study in the engineering notebook corresponds to one milestone.

---

## Month 1: Deterministic Telemetry Ingestion & Hardware Time-Stamping

### Week 1-2: Zero-Copy MAVLink Parser

Configure STM32H7 UART peripherals with DMA circular buffers to ingest high-rate MAVLink telemetry packets (`ATTITUDE`, `LOCAL_POSITION_NED`, `GLOBAL_POSITION_INT`) from the flight controller at 100+ Hz without CPU overhead.

**Key deliverables:**
- USART1 configured for DMA1_Stream0 circular mode
- L1 D-Cache invalidation for coherency between DMA writes and CPU reads
- MAVLink v2 packet parser with `__attribute__((packed))` struct overlay
- Hardware CRC-32 validation with `REV_IN` byte-order correction

**Failure modes addressed:** Struct packing offset shifts, DMA cache coherency stalls, CRC endianness collisions, NVIC ISR Thumb-bit omission.

### Week 3-4: Hardware Timer Synchronization & State Buffer

Utilize high-resolution 32-bit general-purpose timers (TIM2/TIM5) on the STM32H7 to generate precise microsecond timestamps. Bind these timestamps directly to the hardware interrupt triggered by the radar SoC's frame-sync signal, ensuring sub-millisecond temporal alignment between the drone's physical pose and the radar sweep.

**Key deliverables:**
- TIM2/TIM5 configured in free-running 32-bit upcounting mode at 1 MHz (1 us resolution)
- Frame-sync input capture interrupt (EXTI) latching the timer value atomically
- Lock-free single-producer/single-consumer ring buffer in AXI SRAM for kinematic history
- Timestamped attitude samples with monotonic sequence numbers

**Failure modes addressed:** Lost wakeup races between timer interrupt and WFI idle, ring buffer overflow during DMA bursts, priority inversion in nested interrupts.

---

## Month 2: Kinematic State Estimation & Georeferencing Pipeline

### Week 5-6: Fixed-Point EKF Implementation

Develop a lightweight, highly optimized Extended Kalman Filter using CMSIS-DSP matrix libraries on the Cortex-M7. Fuse the drone's inertial state (position, velocity, orientation from MAVLink) with the radar's raw range-Doppler target lists to produce drift-compensated target positions.

**Key deliverables:**
- 6-state EKF: position (3), velocity (3); orientation fused separately via quaternion
- CMSIS-DSP `arm_mat_mult_f32`, `arm_mat_inverse_f32` for prediction/update steps
- Fixed-point Q15/Q31 fallback for deterministic timing on matrix operations
- Innovation covariance gating to reject outlier radar detections

**Failure modes addressed:** Matrix singularity during inverse, numerical overflow in fixed-point accumulation, stack overflow from large matrix frames.

### Week 7: Coordinate Transformation Engine

Write the mathematical transformation pipeline that converts local radar polar coordinates (slant range, angle, Doppler shift) into global East-North-Up (ENU) or latitude/longitude coordinates, mathematically stripping out the drone's instantaneous velocity vector.

**Key deliverables:**
- Polar-to-Cartesian: `(range, azimuth, elevation) → (x, y, z)` in radar frame
- Body-to-ENU: rotation by attitude quaternion (roll, pitch, yaw)
- ENU-to-WGS84: latitude/longitude/altitude output
- Velocity compensation: subtract drone velocity vector from Doppler measurement

**Failure modes addressed:** Gimbal lock in Euler angle representation (mitigated by quaternion math), floating-point precision loss in coordinate conversion, unaligned memory access in packed structs.

### Week 8: Deterministic Output & Host Stream

Package the validated, georeferenced target metadata into clean binary frames and stream them over high-speed SPI or USB-CDC to the ground laptop, ensuring the UI overlay matches the physical world down to the centimeter.

**Key deliverables:**
- Binary output frame: magic, length, sequence, target_count, [lat, lon, alt, confidence, timestamp] per target
- SPI1 configured for master mode DMA transfer to ground laptop radio bridge
- USB-CDC fallback for direct wired connection
- Frame sequencing and CRC-32 for output integrity

**Failure modes addressed:** SPI DMA bus contention, USB enumeration failures, frame fragmentation during high target counts.

---

## Beyond the 2-Month Plan

### Resilience & Hardening

- **Adversarial fault resilience**: Stress testing with malformed MAVLink packets, ensuring no single bad packet can crash the pipeline
- **Cache coherency + MPU**: Configure MPU regions for execute-never on SDRAM, cache maintenance for DMA buffers
- **Power management**: WFI idle between radar frames with PRIMASK critical sections to prevent lost wakeups

### Integration & Validation

- **Full pipeline integration**: End-to-end validation with Renode-injected MAVLink + frame-sync stimuli
- **Physical silicon cross-check**: CRC peripheral validation on real STM32H753XI hardware (Renode's CRC model is simplified)
- **Latency measurement**: GPIO toggling at pipeline stages to measure deterministic execution time on logic analyzer

### The Dream

A coprocessor firmware stack that's small enough to audit, deterministic enough to trust, and precise enough to map a breathing human under rubble from a moving drone — not because it uses the latest framework, but because every line was written deliberately, tested openly, and debugged on real hardware.
