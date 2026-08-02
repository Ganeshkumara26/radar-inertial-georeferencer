# Roadmap

The firmware is functionally complete. Here's what comes next if this becomes a production telemetry stack rather than a portfolio project.

## Near-term

- **DMA + DMAMUX fix**: The current DMA workaround avoids the issue rather than solving it. The real fix is either waiting for Renode to model DMAMUX1 or doing the integration on physical silicon with a debugger.
- **FreeRTOS port**: The context switching and queue IPC are proven. The natural next step is pulling in actual FreeRTOS and replacing the mock scheduler.
- **Flash logging**: The 600KB SDRAM circular buffer has nowhere to persist its data. Adding an FMC/QUADSPI flash driver with wear-leveling would make this a real data logger.
- **Bootloader**: The firmware is flashed via OpenOCD. A tiny UART bootloader would make this deployable in the field.

## Medium-term

- **Crypto**: CRC-32 catches accidental corruption but not adversarial tampering. Adding AES-128 or a lightweight cipher would enable authenticated telemetry.
- **Multi-core**: The STM32H753XI has two Cortex-M7 cores. Splitting the parser to Core 1 and the logger to Core 0 with an inter-core mailbox would actually exercise the IPMMU and cache-coherency logic.
- **Aggressive power gating**: The current WFI idle is basic. Real power savings need clock gating per peripheral, dynamic voltage scaling, and measurement of actual current draw.

## The Dream

A firmware stack that's small enough to audit, verified enough to trust, and efficient enough to run on a coin cell for weeks. Not because it uses the latest framework, but because every line of it was written deliberately, tested openly, and debugged on real hardware.
