# Devlog: Version 7 (RTOS Queue IPC & Context Violations)

## What I'm Trying to Do
With context switching verified (`v6`), the high-frequency telemetry interrupt (`USART1_IRQHandler`) needs a thread-safe method to pass received bytes to the background processing task without corrupting memory or missing data. 

In a Real-Time Operating System like FreeRTOS, the standard solution is a thread-safe **Message Queue**. My goal in `v7` is to establish Inter-Process Communication (IPC) by having the ISR push bytes to an RTOS Queue, which the background task can then pop and process.

## Attempt 1: The ISR Context API Violation
I initialized a mock RTOS queue and instructed the `USART1_IRQHandler` to push incoming bytes into the queue using the standard `xQueueSend()` API.

```bash
make clean && make simulate
```
### The Output (RTOS Kernel Panic)
```text
[USART_ISR] Received byte. Pushing to queue...
[RTOS ASSERT] FATAL: Thread API 'xQueueSend' called from Hardware ISR!
[RTOS ASSERT] System Halted. Use 'xQueueSendFromISR' instead.
```
This is a classic trap for RTOS beginners. Real-Time OS kernels strictly isolate APIs intended for Thread Mode from APIs intended for Handler Mode (ISRs). 
Standard Thread Mode APIs (like `xQueueSend`) are designed to block or put the calling thread to sleep if the queue is full. However, if a *Hardware Interrupt* blocks or goes to sleep, it paralyzes the entire CPU, deadlocking the system!
The RTOS assertion logic correctly read the `IPSR` (Interrupt Program Status Register), realized it was executing inside IRQ 37, and threw a kernel panic to prevent the deadlock.

## Attempt 2: Using the ISR-Safe API
To fix this, I replaced the call with the dedicated `xQueueSendFromISR()` API. 
Unlike the standard API, the `FromISR` variant is strictly non-blocking. If the queue is full, it simply returns an error rather than halting the CPU. Additionally, it returns a boolean flag (`higherPriorityTaskWoken`) indicating if the newly queued item unblocked a high-priority task.

```c
uint8_t yieldRequired = 0;
xQueueSendFromISR(byte, &yieldRequired);
if (yieldRequired) {
    /* Trigger a PendSV exception to context switch out of the ISR */
    SCB_ICSR |= SCB_ICSR_PENDSVSET;
}
```

### The Output (Verified IPC)
```text
[USART_ISR] Received byte. Pushing to queue...
[RTOS] Item safely queued from ISR context.
[USART_ISR] Requesting PendSV Context Switch...
```
The item was safely inserted into the lockless queue from the interrupt context without blocking the hardware. The ISR then successfully requested a PendSV context switch, perfectly bridging the gap between high-speed hardware interrupts and Thread Mode RTOS tasks. Moving to `v8_fault_resilience`.
