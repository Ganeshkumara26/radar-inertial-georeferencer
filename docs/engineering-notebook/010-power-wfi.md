# Devlog: Version 9 (Quiescent Power & WFI Sleep)

## What I'm Trying to Do
With the system proving resilient against adversarial packets (`v8`), the final major architectural hurdle is power consumption. Spinning in a busy-wait loop (`while(1) { if(packet_ready) process(); }`) keeps the Cortex-M7 core running at full clock speed, unnecessarily draining the battery and generating immense thermal load.
My goal in `v9` is to utilize the ARM Cortex `WFI` (Wait For Interrupt) instruction. This puts the core into a low-power sleep state, waking up instantly when a hardware interrupt (like a telemetry USART byte) triggers.

## Attempt 1: The WFI Race Condition (Lost Wakeup)
I implemented a standard event loop that checks if a packet is ready. If not, it executes `WFI`.

To stress-test the architectural edge-cases, I explicitly modeled the most infamous race condition in embedded systems: the **Lost Wakeup Trap**. I mocked a scenario where the `USART_ISR` fires exactly in the microsecond *after* the `if (packet_ready == 0)` check evaluates to true, but *before* the `WFI` instruction executes.

```bash
make clean && make simulate
```
### The Output (Deadlock)
```text
[MAIN] Flag is 0. CPU preparing to enter low-power WFI sleep...
[USART_ISR] (Mock) Hardware Interrupt fired! packet_ready = 1.
[MAIN] Executing WFI...
```
The system deadlocked. Because the interrupt fired and set the flag *before* the CPU went to sleep, the CPU resumed and executed `WFI`, completely unaware that work was already pending. It slept forever waiting for a new interrupt that would never arrive.

## Attempt 2: The Critical Section Fix (PRIMASK)
To safely enter `WFI`, the flag check and the sleep instruction must be executed atomically. I fixed this using the ARM `PRIMASK` register via the `cpsid i` (disable global interrupts) and `cpsie i` (enable) instructions.

```c
__asm__ volatile ("cpsid i"); /* Disable global interrupts */
if (packet_ready == 0) {
    /* WFI still wakes up on pending interrupts even if PRIMASK is set! */
    __asm__ volatile ("wfi"); 
}
__asm__ volatile ("cpsie i"); /* Re-enable global interrupts */
```

This is a beautiful architectural feature of the ARM Cortex-M core: `WFI` acts as an execution barrier that will wake the CPU if an interrupt is pending in the NVIC, **even if global interrupts are currently disabled via PRIMASK**. 
By disabling interrupts, checking the flag, and executing `WFI`, we guarantee that if an interrupt fires during the check, it is held pending by the NVIC. `WFI` immediately detects the pending interrupt, refuses to sleep, and continues execution, preventing the lost wakeup trap. The race condition is solved. Moving to the final milestone: `v10_cache_mpu`.
