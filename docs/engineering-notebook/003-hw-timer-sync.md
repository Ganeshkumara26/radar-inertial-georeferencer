# Devlog: Case Study 03 — Hardware Timer Synchronization & The Priority Inversion

## What I'm Trying to Do

The radar SoC asserts a frame-sync pulse every time it completes a sweep. The coprocessor must latch the **exact microsecond** of this edge to timestamp the radar data. A 1 ms timing error at 3 m/s drone velocity = 3 mm spatial drift. I configure **TIM2 (32-bit)** to free-run at 1 MHz (1 us resolution) and use input capture to latch the timer value atomically on the frame-sync edge.

---

## Attempt 1: Timer Configured But Never Counts

I enabled TIM2, set prescaler to 239 (for 1 MHz from 240 MHz APB1 timer clock), and started it.

### The Output (Timer Stuck at 0)
```text
[TIM2] CNT = 0x00000000 after 100ms
[TIM2] Timer not incrementing!
```

### My Mistake & Root Cause Analysis

I forgot to generate an **update event** after writing the prescaler. On the STM32H7, the prescaler register has a shadow copy. Writing `TIM2_PSC = 239` updates the shadow register, but the actual prescaler isn't reloaded until the next update event. Without generating one, the prescaler stays at its previous value (0), and the timer clock is 240 MHz / (0+1) = 240 MHz — but more importantly, the timer doesn't start counting because the configuration isn't committed.

### The Fix

```c
TIM2_PSC = 239;
TIM2_ARR = 0xFFFFFFFF;
TIM2_EGR = TIM_EGR_UG;  /* Generate update event to load PSC */
TIM2_CR1 = TIM_CR1_CEN | TIM_CR1_ARPE;
```

The `EGR_UG` bit generates an update event that loads the shadow prescaler into the active prescaler. Only then does the counter start incrementing at the correct rate.

---

## Attempt 2: Frame-Sync Interrupt Priority Inversion

I configured EXTI line 12 (frame-sync input) with a high priority and TIM2 overflow with a lower priority. The frame-sync ISR calls `hw_timer_frame_sync_isr()` which reads `TIM2_CCR1`.

### The Output (Spurious Timestamps)
```text
[SYNC] Frame 0 timestamp: 0x000004A2
[SYNC] Frame 1 timestamp: 0x000004A1  ← timestamp went BACKWARDS!
[SYNC] Frame 2 timestamp: 0x00001A3F
```

### My Mistake & Root Cause Analysis

The EXTI frame-sync interrupt was configured at a **higher priority** than the TIM2 update interrupt. When the frame-sync fires, it reads `TIM2_CCR1` — but the hardware capture hasn't occurred yet because the EXTI interrupt preempted the timer's capture complete sequence. The stale CCR1 value from a previous capture is returned.

In an EKF, a non-monotonic timestamp is catastrophic — it causes the prediction step to use a negative `dt`, injecting energy into the system instead of damping it.

### The Fix

Two corrections:
1. **Use input capture, not GPIO read:** Configure TIM2 Channel 1 in input capture mode (`TIM_CCMR1_CC1S_0`). The hardware automatically latches `TIM2_CNT` into `TIM2_CCR1` on the rising edge — no software involvement, no latency.
2. **If using EXTI, set it lower than timer:** The EXTI ISR should only *read* the already-captured CCR1 value, not attempt to capture itself.

```c
/* Hardware capture — no ISR needed for latching */
TIM2_CCMR1 = TIM_CCMR1_CC1S_0;  /* IC1 mapped on TI1 */
TIM2_CCER = TIM_CCER_CC1E | TIM_CCER_CC1P;  /* Capture on rising edge */
```

The hardware guarantees the capture is atomic. The ISR just reads the latched value and clears the flag.

---

## Final Result

TIM2 now provides deterministic microsecond timestamps:
- 32-bit free-running counter at 1 MHz (wraps every 71 minutes)
- Hardware input capture latches timestamp on frame-sync edge
- EXTI ISR reads latched value, increments frame sequence
- Overflow interrupt tracks wraparound for long-duration missions

Ready for CS04: Lock-free ring buffer for kinematic state history.
