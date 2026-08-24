# Architecture

This is a deterministic radar-inertial georeferencing coprocessor targeting the ARM Cortex-M7 (STM32H753XI). It sits between the drone's flight controller and the radar SoC's digital backend, solving the spatial drift problem that makes raw radar detections useless for rescue mapping.

## System Context

```mermaid
flowchart LR
    subgraph Drone["Drone Platform"]
        FC["Pixhawk Flight Controller<br/>(PX4/ArduPilot)<br/>MAVLink @ 100+ Hz"]
        Radar["Radar SoC<br/>Frame-sync signal<br/>Range-Doppler target lists"]
    end

    subgraph Coprocessor["STM32H753XI Coprocessor"]
        MAVPARSE["Zero-Copy<br/>MAVLink Parser<br/>(DMA circular buffer)"]
        TIMSYNC["Hardware Timer<br/>Sync<br/>(Microsecond timestamps)"]
        RINGBUF["Lock-Free<br/>State Buffer<br/>(Kinematic history)"]
        EKF["Fixed-Point EKF<br/>(CMSIS-DSP matrices)"]
        GEO["Coordinate<br/>Transform<br/>(Polar→ENU→Lat/Long)"]
        OUT["Deterministic<br/>Output<br/>(SPI/USB-CDC)"]
    end

    subgraph Ground["Ground Station"]
        LAPTOP["Laptop UI<br/>Georeferenced overlay"]
    end

    FC -->|"UART1 (MAVLink)"| MAVPARSE
    Radar -->|"Frame-sync IRQ"| TIMSYNC
    TIMSYNC -->|"Timestamped frame"| RINGBUF
    MAVPARSE -->|"Attitude + Position"| RINGBUF
    RINGBUF -->|"State window"| EKF
    EKF -->|"Fused state"| GEO
    GEO -->|"Georeferenced targets"| OUT
    OUT -->|"Binary metadata stream"| LAPTOP

    style FC fill:#e3f2fd
    style Radar fill:#e3f2fd
    style Coprocessor fill:#f3e5f5
    style LAPTOP fill:#e8f5e7
```

## Firmware Pipeline

```mermaid
flowchart TD
    subgraph HW["ARM Cortex-M7 (STM32H753XI)"]
        CPU["CPU Core @ 480MHz<br/>Thumb-2, FPv5-D16"]
        FLASH["Internal Flash<br/>0x0800_0000<br/>2MB"]
        SRAM["AXI SRAM<br/>0x2400_0000<br/>512KB"]
        SDRAM["FMC SDRAM<br/>0xD000_0000<br/>32MB"]

        USART["USART1<br/>0x4001_1000<br/>MAVLink input (DMA)"]
        DMA["DMA1_Stream0<br/>+ DMAMUX1<br/>Zero-copy RX"]
        HWCRC["CRC Peripheral<br/>0x4002_3000<br/>Packet validation"]
        TIMER["TIM2/TIM5<br/>32-bit general purpose<br/>Microsecond timestamps"]
        NVIC["NVIC<br/>IRQ priority management"]
        MPU["MPU<br/>0xE000_ED90<br/>Memory protection"]
        ICache["I-Cache/L1 D-Cache<br/>Coherency management"]
        SPI["SPI1/USB-CDC<br/>Deterministic output"]
    end

    CPU --> FLASH
    CPU --> SRAM
    CPU --> SDRAM
    CPU --> USART
    CPU --> DMA
    CPU --> HWCRC
    CPU --> TIMER
    CPU --> NVIC
    CPU --> MPU
    CPU --> SPI
    DMA -->|zero-copy| SRAM
    TIMER -->|timestamp| NVIC
    USART -->|byte stream| DMA
```

## Memory Map

| Region | Address | Size | Purpose |
|--------|---------|------|---------|
| Internal Flash | `0x0800_0000` | 2MB | Firmware code, vector table, constants |
| AXI SRAM | `0x2400_0000` | 512KB | Stack, `.data`, `.bss`, ring buffers, EKF state matrices |
| SDRAM (FMC) | `0xD000_0000` | 32MB | Large kinematic history buffer, radar frame window |

## Build Toolchain

```
arm-none-eabi-gcc -mcpu=cortex-m7 -mthumb -mfloat-abi=hard -mfpu=fpv5-d16
```

Linked with `--specs=nosys.specs` and `-nostdlib` to keep it truly bare-metal. The Renode simulation is driven by a `.rescript` monitor file that injects MAVLink packets and radar frame-sync signals, then runs the emulation for a fixed virtual time window.

## Pipeline Stages

| Stage | Peripheral | Function |
|-------|-----------|----------|
| 1. Boot | — | Vector table, Reset_Handler, USART debug |
| 2. MAVLink Ingestion | USART1 + DMA1 | Zero-copy DMA circular buffer for attitude packets |
| 3. Hardware Timestamp | TIM2/TIM5 + NVIC | Microsecond-precise frame-sync capture |
| 4. State Buffer | AXI SRAM | Lock-free ring buffer for kinematic history |
| 5. EKF Fusion | CMSIS-DSP | Fixed-point matrix operations for state estimation |
| 6. Coordinate Transform | FPU (FPv5-D16) | Polar→ENU→Lat/Long with velocity compensation |
| 7. Output | SPI1/USB-CDC | Binary georeferenced target frames to ground |

## Milestone Evolution

```mermaid
gantt
    title EDP Georeferencing Coprocessor Development
    dateFormat  YYYY-MM-DD
    section Month 1: Ingestion
    CS01 Bare-metal boot + USART debug       :done, m1, 2026-07-01, 3d
    CS02 Zero-copy MAVLink DMA parser        :done, m2, after m1, 4d
    CS03 Hardware timer frame-sync           :done, m3, after m2, 4d
    CS04 Lock-free state ring buffer         :done, m4, after m3, 4d
    section Month 2: Georeferencing
    CS05 Fixed-point EKF implementation      :done, m5, after m4, 5d
    CS06 Coordinate transform engine         :done, m6, after m5, 5d
    CS07 Deterministic output streaming      :done, m7, after m6, 4d
    section Resilience
    CS08 Fault resilience under stress       :done, m8, after m7, 3d
    CS09 Cache coherency + MPU              :done, m9, after m8, 3d
    CS10 Power management (WFI)              :done, m10, after m9, 2d
    CS11 Full pipeline integration           :done, m11, after m10, 4d
```
