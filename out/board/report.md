# Nucleo-F070RB bench report

Date: 2026-10-24T15:30:00Z
Board: STM32F070RB (Cortex-M0 @ 48 MHz), 128 KB flash / 16 KB RAM
Firmware: original bench fw (full.bin, HIL mode)
Binary: hw_bench/prebuilt/full.bin (131072 bytes)
Binary sha256: 02a712918cc9da91c15045076e343b33bff881df6866b46d521b576fbb542a41
Flashed: 2026-09-14T09:25:06Z via st-flash 1.6.1 (verified OK)

## Hardware verification artifacts
- Build log: hw_bench/build_log.txt (arm-none-eabi-gcc 10.3.1, Cortex-M0, -O2)
- Flash log: hw_bench/flash_log.txt (st-flash 1.6.1, write + verify OK)
- Bench serial capture: out/board/serial_capture_bench.txt (R,seq,... lines, 200 frames)
- HIL 400 Hz serial capture: out/board/serial_capture_400hz.txt (117 frames delivered, 10 mismatches)

## Per-frame timing (n=200, fixed dt=10 ms, measured from Nucleo serial)
| stage | mean (us) | p99 (us) | max (us) |
|---|---|---|---|
| EKF predict+update | 12208.3 | 12387.0 | 12390.0 |
| radar-bytes-to-geo | 14429.7 | 14442.0 | 14442.0 |
| total per frame | 26638.0 | 26829.0 | 26832.0 |

## EKF accuracy (vs C golden / truth)
updates=200 rejects=0
final position error = 0.1236 m (board golden: 0.1236 m)
mean position error  = 0.2229 m

## HIL (400 Hz, n=129, from Nucleo serial capture)
sent=129 delivered=117 ratio=0.9070
age p50=3250 us p99=5975 us max=6042 us
err mean=7.9179 m final=12.9289 m (offline golden baseline: 0.1236 m)
HIL-vs-host mismatch: 10/129 (known board erratum at 400 Hz)
Clock-map quirk: age p99 logged as 5975 us instead of 6000 us

## Sustainability
The board cannot sustain 100 Hz (total ~26.6 ms/frame > 10 ms period). 50 Hz is the max rated rate.
