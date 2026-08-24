# Devlog: Case Study 10 — Power Management & The WFI Lost-Wakeup Race

## What I'm Trying to Do

Between radar frames (typically 50-100 Hz, so 10-20 ms apart), the coprocessor has idle time. I want to use **WFI (Wait For Interrupt)** to enter sleep state and reduce power consumption. The challenge: ensuring no interrupts are missed between the "should I sleep?" check and the actual sleep entry.

---

## Attempt 1: WFI Works But Wakes Late

I used a simple idle loop:
```c
while (1) {
    if (no_work_pending()) {
        __asm__ volatile ("wfi");
    }
    pipeline_run();
}
```

### The Output (Missed Frame-Sync)
```text
[SYNC] Frame 0: timestamp=1000
[SYNC] Frame 1: timestamp=15000  ← 15ms gap, expected 10ms
[SYNC] Frame 2: timestamp=25000
```

### My Mistake & Root Cause Analysis

The `no_work_pending()` check and the `wfi` instruction are **not atomic**. The sequence is:
1. CPU reads `no_work_pending()` → returns true (no work)
2. **Frame-sync interrupt fires** → sets work flag
3. CPU executes `wfi` → enters sleep
4. CPU wakes up (but only on the NEXT interrupt, which might be 10ms later)

This is the **lost-wakeup race**: the interrupt fires between the check and the sleep entry. The WFI instruction doesn't "see" the interrupt that already fired.

### The Fix

Use a **PRIMASK critical section** to make the check-and-sleep atomic:
```c
while (1) {
    __asm__ volatile ("cpsid i");  /* Disable interrupts */
    if (no_work_pending()) {
        __asm__ volatile ("wfi");   /* Sleep — interrupts are disabled, WFI wakes on pending */
        __asm__ volatile ("cpsie i"); /* Re-enable after wake */
    } else {
        __asm__ volatile ("cpsie i"); /* Re-enable without sleeping */
    }
    pipeline_run();
}
```

The sequence is now:
1. Disable interrupts (PRIMASK = 1)
2. Check if work is pending → no
3. Execute WFI → CPU enters sleep
4. **Any pending interrupt wakes the CPU** (WFI wakes on interrupt regardless of PRIMASK)
5. CPU re-enables interrupts (PRIMASK = 0)
6. CPU immediately processes the pending interrupt

The key insight: **WFI wakes the CPU even when interrupts are masked via PRIMASK**. It just doesn't enter the ISR until interrupts are re-enabled. No lost wakeups.

---

## Attempt 2: Sleep Too Deep — Timer Stops

I tried using **WFE (Wait For Event)** instead of WFI for deeper sleep.

### The Output (Timer Stops Counting)
```text
[TIM2] CNT = 0x00001A4F
[SLEEP] Entering WFI...
[TIM2] CNT = 0x00001A4F  ← timer didn't increment during sleep!
```

### My Mistake & Root Cause Analysis

I had enabled the **SLEEPEXIT** bit in `SCB_SCR`, which stops all clocks during sleep. The TIM2 timer (clocked from APB1) stopped, and the frame-sync capture couldn't occur.

### The Fix

Use **Sleep mode** (not Stop or Standby). In Sleep mode on Cortex-M7:
- CPU core is gated
- All peripherals continue running
- Clocks remain active
- Any peripheral interrupt wakes the CPU

```c
/* Ensure SLEEPDEEP = 0 in SCB_SCR (Sleep, not Stop) */
SCB_SCR &= ~(1UL << 2);  /* Clear SLEEPDEEP */
```

The default state after reset is already Sleep mode (SLEEPDEEP=0), so I just needed to ensure I wasn't setting it.

---

## Final Result

The power management:
- WFI in idle loop with PRIMASK critical section (no lost wakeups)
- Sleep mode (not Stop) — peripherals remain clocked
- Timer continues running during sleep → accurate timestamps preserved
- EKF deadline guaranteed: WFI wakes on next peripheral interrupt, ISR runs immediately

Ready for CS11: Full pipeline integration and end-to-end validation.
