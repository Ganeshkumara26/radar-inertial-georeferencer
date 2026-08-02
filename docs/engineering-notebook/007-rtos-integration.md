# Devlog: Version 6 (RTOS Integration & PendSV Context Switching)

## What I'm Trying to Do
With memory architecture stabilized (`v5`), the firmware is scaling into a multi-threaded system. A Real-Time Operating System (RTOS) like FreeRTOS requires the CPU to rapidly context-switch between background processing tasks and foreground telemetry parser tasks.

On ARM Cortex-M microcontrollers, context switching is entirely driven by two hardware exceptions:
1. `SysTick_Handler`: A hardware timer that fires every millisecond to invoke the RTOS scheduler algorithm.
2. `PendSV_Handler` (Pendable Service Call): The actual assembly routine that pushes the current task's registers to the Process Stack Pointer (PSP) and restores the next task's registers.

## Attempt 1: The PendSV Priority Inversion Trap
I configured `SysTick` to fire periodically, and in its handler, it requests a context switch by setting the `PENDSVSET` bit in the System Control Block (`SCB->ICSR`). I assigned the telemetry `USART1_IRQHandler` an interrupt priority of `1`.

Crucially, I left the `PendSV` and `SysTick` exceptions at their default ARM priority level of `0` (Highest Priority).

```bash
make clean && make simulate
```
### The Output (Fatal Priority Inversion)
```text
[SysTick] Tick! Triggering PendSV for RTOS Context Switch...
[PendSV] Executing Context Switch...
[FATAL] PendSV preempted an active Hardware ISR! Stack corrupted!
[FATAL] UsageFault / HardFault triggered.
```
Because `PendSV` ran at the highest priority, when `SysTick` fired during an active `USART_ISR`, `PendSV` immediately executed. In an RTOS, `PendSV` is designed to context-switch out of Thread Mode using the `PSP`. Preempting an active Hardware ISR means the core was using the Main Stack Pointer (`MSP`). Attempting to swap stacks here catastrophically corrupted the CPU state, triggering a HardFault.

## Attempt 2: Setting Lowest Priority
To fix this, I set the `PendSV` priority to the lowest possible value (`0xFF`) in the `SCB->SHPR3` register. This instructs the NVIC to keep `PendSV` pending in the background until all other hardware interrupts (like USART) have completely finished and the CPU returns to Thread Mode. 

However, even with the priority fixed, the `PendSV_Handler` STILL reported a corrupted stack! 

## Attempt 3: The C Compiler Prologue Trap
Digging into the exception mechanics, my `PendSV_Handler` was checking the `EXC_RETURN` value stored in the Link Register (`LR`) to verify it was preempting Thread Mode (`0xFFFFFFF9`). 
```c
uint32_t lr;
__asm__ volatile ("mov %0, lr" : "=r" (lr));
```
I analyzed the disassembled ELF and realized the standard GCC compiler was inserting a standard C function prologue (`push {r7, lr}`) *before* my inline assembly executed. The compiler destroyed the hardware `EXC_RETURN` value in `LR` by pushing it to the stack and using `LR` for standard C linkage!

To fix this, I declared the handler as an `__attribute__((naked))` wrapper, capturing `LR` immediately into `R0` and passing it safely to a C subroutine:
```c
__attribute__((naked)) void PendSV_Handler(void) {
    __asm__ volatile ("mov r0, lr\n\t" "b PendSV_Handler_C\n\t");
}
```

### The Output (Verified RTOS Tail-Chaining)
```text
[USART_ISR] Entering Hardware Interrupt (IRQ 37)...
[SysTick] Tick! Triggering PendSV for RTOS Context Switch...
[SysTick] Tick! Triggering PendSV for RTOS Context Switch...
[SysTick] Tick! Triggering PendSV for RTOS Context Switch...
[USART_ISR] Exiting Hardware Interrupt.
[PendSV] Executing Context Switch...
[SUCCESS] PendSV safely preempted Thread Mode.
```
The output perfectly demonstrates ARM Cortex-M exception tail-chaining. `SysTick` repeatedly requested context switches *while* the `USART_ISR` was running. Because `PendSV` was Priority `0xFF`, the NVIC deferred it perfectly. The moment `USART_ISR` exited, `PendSV` safely executed from Thread Mode. We are now ready to scale into multi-threaded RTOS queues in `v7_queue_ipc`.
