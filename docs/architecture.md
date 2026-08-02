# Architecture

This is a bare-metal firmware project targeting the ARM Cortex-M7 (STM32H753XI). No RTOS, no HAL, no standard library — just the CPU, the peripherals, and a lot of `volatile` pointer casts.

## Firmware Stack

```mermaid
flowchart TD
    subgraph HW["ARM Cortex-M7 (STM32H753XI)"]
        CPU["CPU Core @ 480MHz<br/>Thumb-2, FPv5-D16"]
        FLASH["Internal Flash<br/>0x0800_0000<br/>2MB"]
        SRAM["AXI SRAM<br/>0x2400_0000<br/>512KB"]
        SDRAM["FMC SDRAM<br/>0xD000_0000<br/>32MB"]

        USART["USART1<br/>0x4001_1000<br/>Polled→ISR"]
        DMA["DMA1_Stream0<br/>+ DMAMUX1"]
        HWCRC["CRC Peripheral<br/>0x4002_3000<br/>CRC-32"]
        SYSTICK["SysTick<br/>1kHz tick"]
        PENDSV["PendSV<br/>Context switch"]
        MPU["MPU<br/>0xE000_ED90<br/>4 regions"]
        ICache["I-Cache/L1 D-Cache"]
    end

    CPU --> FLASH
    CPU --> SRAM
    CPU --> SDRAM
    CPU --> USART
    CPU --> DMA
    CPU --> HWCRC
    CPU --> SYSTICK
    CPU --> PENDSV
    CPU --> MPU
    SYSTICK -->|tick| PENDSV
    PENDSV -->|swap| CPU
```

## Memory Map

| Region | Address | Size | Purpose |
|--------|---------|------|---------|
| Internal Flash | `0x0800_0000` | 2MB | Firmware code |
| AXI SRAM | `0x2400_0000` | 512KB | Stack, `.data`, `.bss`, ring buffers |
| SDRAM (FMC) | `0xD000_0000` | 32MB | Large telemetry circular buffer (v5+) |

## Build Toolchain

```
arm-none-eabi-gcc -mcpu=cortex-m7 -mthumb -mfloat-abi=hard -mfpu=fpv5-d16
```

Linked with `--specs=nosys.specs` and `-nostdlib` to keep it truly bare-metal. The Renode simulation is driven by a `.rescript` monitor file that injects test characters and runs the emulation for a fixed virtual time window.

## Milestone Evolution

```mermaid
gantt
    title EDP Firmware Development Milestones
    dateFormat  YYYY-MM-DD
    section Core
    v0 Bare-metal polling      :done, m1, 2023-01-01, 7d
    v1 NVIC interrupts         :done, m2, 2023-01-08, 7d
    v2 MAVLink framing         :done, m3, 2023-01-15, 7d
    v3 DMA + cache coherency   :done, m4, 2023-01-22, 7d
    v4 Hardware CRC + endian   :done, m5, 2023-01-29, 7d
    v5 SDRAM linker surgery    :done, m6, 2023-02-05, 7d
    v6 RTOS integration        :done, m7, 2023-02-12, 7d
    v7 Queue IPC               :done, m8, 2023-02-19, 7d
    v8 Fault resilience        :done, m9, 2023-02-26, 7d
    v9 Power management        :done, m10, 2023-03-04, 7d
    v10 MPU + cache            :done, m11, 2023-03-11, 7d
```
