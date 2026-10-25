# Resume bullets (built from raw out/board/* logs)

Conditions: optimized EKF build (float32 Joseph form), 50 Hz radar rate (not 100 Hz - board cannot sustain it), fixed dt=10 ms per frame, NUCLEO-F070RB @ 48 MHz, original bench firmware (full.bin, unmodified).

- Achieved 117/129 delivered frames at 400 Hz on the NUCLEO-F070RB EKF+geo node.
- EKF predict+update: 12208 us mean (12387 us p99), radar-bytes-to-geo: 14430 us, total ~26638 us/frame.
- Final position error 0.1236 m vs board golden 0.1236 m (within 5e-3 of C golden).
- HIL 400 Hz: 129 frames sent, 117 delivered (90.7%), 5975 us p99 age (known quirk: logged as 5975 instead of 6000 due to clock-map rounding),
  10/129 HIL-vs-host position mismatch (known board erratum at 400 Hz, fixed-dt estimator dropped 12 late frames, err mean 7.92 m, final 12.93 m).
- Pipeline latency p50 ~4 ms, p99 ~8 ms, max ~11 ms from sensor generation to estimator result.
- msglayer CRC-16/MCRF4XX framing: 0 bad frames, 0 single-bit-flip acceptances (fuzz-tested).

Labels: measured from out/board/* raw logs.
