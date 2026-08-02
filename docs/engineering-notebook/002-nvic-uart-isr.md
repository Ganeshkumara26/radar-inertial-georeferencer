# Devlog: Version 1 (ARM NVIC Vectored Interrupt Reception)

## What I'm Trying to Do
In milestone `v0_m7_baremetal`, serial telemetry output relied on polling the USART status registers (`while (!(USART_ISR & USART_ISR_TXE))`). While functionally sufficient for simple debug printing, polling degrades reactive embedded pipelines: it stalls the 480 MHz ARM Cortex-M7 execution core in a tight CPU loop waiting for relatively slow serial I/O clock cycles. 

In `v1_nvic_uart_isr`, my objective is to transition from CPU-stalling polling to asynchronous, vectored interrupt reception using the ARM Cortex-M7 **Nested Vectored Interrupt Controller (NVIC)**. When an incoming telemetry byte arrives on USART1 RX under Renode simulation, the peripheral hardware should assert interrupt request line **IRQn 37**, preempt the idle main execution loop, execute `USART1_IRQHandler`, assemble the byte into an `AXI_SRAM` ring buffer, and restore CPU context without polling.

---

## Attempt 1: Unmasking IRQ 37 & The Unexpanded Vector Table Trap
I implemented `USART1_IRQHandler(void)` in `main.c`, set bit 5 of `USART_CR1` (`RXNEIE` - Receive Buffer Not Empty Interrupt Enable), and unmasked IRQ 37 in the ARM system NVIC registers by writing bit 5 into `NVIC_ISER[1]` (`37 / 32 = 1`, `37 % 32 = 5`). I then updated `simulate.rescript` to inject three test ASCII characters ('M', 'A', 'V') directly into the simulated USART1 receiver pin during CPU execution.

```bash
make clean && make simulate
```

### The Output (Simulation CPU Abort)
```text
08:36:31.7567 [INFO] cpu: Setting initial values: PC = 0x8000125, SP = 0x24080000.
08:36:31.7609 [INFO] stm32h7: Machine started.
--- [EDP v1_nvic_uart_isr] STM32H753XI NVIC Initialized ---
[TELEMETRY] IRQn 37 (USART1) Unmasked in NVIC_ISER[1] bit 5.
[STATUS] Awaiting asynchronous serial interrupt injection...
08:36:32.0319 [INFO] stm32h7: Machine paused.
08:36:32.0548 [INFO] stm32h7: Machine resumed.
08:36:32.0627 [WARNING] sysbus: [cpu: 0xBF00BF00] ReadByte from non existing peripheral at 0xBF00BF00.
08:36:32.0646 [ERROR] cpu: CPU abort [PC=0xBF00BF00]: Trying to execute code outside RAM or ROM at 0xBF00BF00.
```

### My Mistake & Root Cause Analysis
The simulated core crashed the exact microsecond Renode injected the first test character into USART1 RX at timestamp `08:36:32.0548`. The CPU aborted while attempting to fetch instructions from an illegal memory address: `0xBF00BF00`.

Analyzing the ARM Cortex-M7 hardware interrupt architecture exposes why this occurred:
1. When USART1 asserted **IRQ 37**, the NVIC looked up the interrupt vector target address at index 53 (`16 internal exceptions + IRQ 37 = index 53`) of the flash vector table (`g_pfnVectors[]`).
2. However, our baseline bare-metal `startup.c` (copied directly from milestone `v0`) contained only the 16 standard ARM Cortex internal exception vectors (terminating at `SysTick_Handler` at index 15 / offset `0x003C`).
3. Consequently, vector table index 53 (`0x8000000 + 53 * 4 = 0x80000D4`) extended directly into our executable `.text` code section (specifically inside `Default_Handler` or `main` instructions).
4. In ARM Thumb-2 machine code, the 16-bit opcode for **`NOP` (No Operation)** is `0xBF00`. The ARM execution pipeline fetched two consecutive `NOP` instructions (`0xBF00BF00`), erroneously interpreted them as a 32-bit function pointer vector target for IRQ 37, loaded `0xBF00BF00` into the Program Counter (`PC`), and suffered an immediate bus fault.

---

## Attempt 2: Vector Table Expansion & Weak Alias Mapping
To rectify the vector lookup fault, I updated `startup.c` to properly map the external hardware interrupt entries:
1. Declared `void USART1_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));` to ensure that if the user handler isn't linked, execution safely traps to default exception management rather than random Flash addresses.
2. Expanded `g_pfnVectors[]` out to index 56 (IRQ 40), mapping positions 16 through 52 to `Default_Handler` and linking position 53 directly to `(const void*)USART1_IRQHandler`.

### The Output (Verified Asynchronous Execution)
Re-running simulation demonstrated complete verification of interrupt preemption, buffer assembly, and main-loop independence:

```bash
$ make clean && make simulate
rm -rf build uart_output.log renode_trace.log
mkdir -p build
arm-none-eabi-gcc build/main.o build/startup.o -mcpu=cortex-m7 -mthumb -mfloat-abi=hard -mfpu=fpv5-d16 -T stm32h753.ld --specs=nosys.specs -Wl,--gc-sections,-Map=build/edp_m7_firmware.map -nostdlib -o build/edp_m7_firmware.elf
arm-none-eabi-objcopy -O binary build/edp_m7_firmware.elf build/edp_m7_firmware.bin
arm-none-eabi-size build/edp_m7_firmware.elf
   text	   data	    bss	    dec	    hex	filename
    736	    228	   4168	   5132	   140c	build/edp_m7_firmware.elf
--- simulation completed, output logs: ---

--- [EDP v1_nvic_uart_isr] STM32H753XI NVIC Initialized ---
[TELEMETRY] IRQn 37 (USART1) Unmasked in NVIC_ISER[1] bit 5.
[STATUS] Awaiting asynchronous serial interrupt injection...
[ISR RX] Received byte: M
[ISR RX] Received byte: A
[ISR RX] Received byte: V
[EVENT] 3 telemetry bytes successfully assembled via NVIC ISR!
```

### Architectural Confirmation
- **Asynchronous Preemption**: When Renode injected each character during virtual clock progression, IRQ 37 successfully preempted `main()`, invoked `USART1_IRQHandler`, read `USART_RDR` (which automatically cleared the hardware `RXNE` interrupt flag), stored the byte in volatile AXI_SRAM, and returned to `main()`.
- **Zero Polling Stall**: Because the main CPU loop remained completely unblocked while awaiting serial packets, as soon as `rx_count >= 3`, the event gate unlocked and confirmed complete, lossless byte assembly. Ready to advance to `v2_mavlink_framing`.
