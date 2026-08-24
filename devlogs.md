# EDP Georeferencing Coprocessor — Development Logs

Consolidated record of all case studies in the deterministic radar-inertial georeferencing pipeline.

## Case Study Index

| # | Title | Pipeline Stage | Key Bug |
|---|-------|---------------|---------|
| 01 | Pipeline Foundation & Bare-Metal Boot | Boot | IRQn typo, linker nostdlib |
| 02 | Zero-Copy MAVLink DMA Parser | Ingestion | Cache coherency, struct packing, CRC endian |
| 03 | Hardware Timer Synchronization | Timestamp | Prescaler load, priority inversion |
| 04 | Lock-Free Ring Buffer | State Buffer | DMB memory barrier, full/empty ambiguity |
| 05 | Fixed-Point EKF | Georeferencing | Buffer aliasing, sqrtf, atan2f, Jacobian |
| 06 | Coordinate Transform Engine | Georeferencing | Quaternion direction, double precision |
| 07 | Deterministic Output Stream | Output | Frame length, SPI contention, polled TX blocking |
| 08 | Fault Resilience | Hardening | Buffer bounds, NaN rejection, burst handling |
| 09 | Cache Coherency & MPU | Hardening | XN fault, DMA buffer placement, stack canary |
| 10 | Power Management (WFI) | Hardening | Lost-wakeup race, sleep mode vs stop mode |
| 11 | Full Pipeline Integration | Integration | Deadlock, timestamp interpolation, CRC race |

## Build Status

```
$ make -f scripts/Makefile all
   text	   data	    bss	    dec	    hex	filename
  19568	    232	  12400	  32200	   7dc8	build/edp_m7_firmware.elf
```

Target: STM32H753XI Cortex-M7 @ 480MHz
Toolchain: arm-none-eabi-gcc 10.3.1
Build flags: -O0 -g3 -DSIMULATION_BUILD

## Bugs Fixed During Development

### CS01: Boot & Build System
- **IRQn suffix typo**: `DMA1_STREAM0_IRQN` → `DMA1_STREAM0_IRQn` (CMSIS uses lowercase n)
- **Linker nostdlib**: Added `-lc -lnosys -lgcc` for memcpy/memset/__aeabi_l2d
- **errno missing**: Added `syscalls.c` with `__errno` stub for libm

### CS02: MAVLink DMA Parser
- **Cache coherency**: DMA writes bypass L1 D-Cache → added `SCB_DCCIMVAC` invalidation
- **Struct packing**: GCC inserts 2-byte padding before float fields → `__attribute__((packed))`
- **CRC endianness**: Hardware CRC processes MSB-first, CPU writes LSB-first → `CRC_CR_REV_IN`

### CS03: Hardware Timer
- **Prescaler not loaded**: Shadow register needs update event → `TIM2_EGR = TIM_EGR_UG`
- **Priority inversion**: EXTI ISR read stale CCR1 → use hardware input capture mode

### CS04: Ring Buffer
- **Out-of-order execution**: Cortex-M7 write buffer delays SRAM writes → `DMB` barrier
- **Full/empty ambiguity**: `head == tail` means both → sacrifice one slot

### CS05: EKF
- **Buffer aliasing**: CMSIS-DSP overwrites own inputs when src=dst → distinct scratchpads
- **Missing sqrtf**: range_pred used r² instead of r → added `sqrtf()`
- **Wrong measurement model**: angles in meters instead of radians → `atan2f()`
- **Incomplete Jacobian**: rows 1,2 of H were zero → full 4×6 population
- **Covariance asymmetry**: Joseph form loses symmetry → enforce after each update

### CS06: Geo Transform
- **Quaternion direction**: used conjugate instead of q for forward rotation → corrected
- **Precision near equator**: single-precision `sin(lat)` loses precision → use double

### CS07: Output Stream
- **Frame length mismatch**: declared length ≠ actual packed length → track offset
- **SPI contention**: DMA1 shared between USART RX and SPI TX → use DMA2 for SPI
- **Polled TX blocking**: blocks EKF during transmit → ring buffer + DMA

### CS08: Fault Resilience
- **Buffer overflow**: MAVLink length field can exceed buffer → bounds checking
- **NaN injection**: malformed float crashes EKF → validate all parsed floats
- **Ring buffer overflow**: parser faster than EKF → drain buffer each loop

### CS09: Cache & MPU
- **XN fault**: code placed in SDRAM by linker → separate code from data sections
- **DMA coherency**: need explicit invalidation or non-cacheable region → manual inv
- **Stack overflow**: stack grows into BSS → linker isolation + stack canary

### CS10: Power
- **Lost wakeup**: interrupt between check and WFI → PRIMASK critical section
- **Timer stops in sleep**: SLEEPDEEP bit set → use Sleep mode, not Stop

### CS11: Integration
- **Deadlock**: parser retries on full buffer → drop and continue
- **Timestamp misalignment**: attitude up to 10ms old → interpolate to frame-sync time
- **CRC race**: buffer modified during computation → critical section around build
