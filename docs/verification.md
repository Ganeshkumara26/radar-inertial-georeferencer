# Verification

Verification here means "run it in Renode and look at the log output." It's not formal, but it's honest.

## Why Renode

We picked Renode because it gives deterministic, shared virtual time simulation. If you need to debug a race condition between the DMA mutating memory and the CPU reading it, you can pause the entire system clock, inspect AXI_SRAM byte-by-byte, and resume. Try doing that with a logic analyzer on a physical NUCLEO board.

The catch: Renode's STM32H7 model isn't complete. The DMAMUX1 peripheral, the CRC hardware block, and some low-power PWR register behaviors are either missing or simplified.

## The Validation Loop

```mermaid
flowchart LR
    subgraph Loop["Validation Loop"]
        A["Write Feature"] --> B["make clean && make simulate"]
        B --> C["Check Renode Log Output"]
        C --> D{Expected strings match?}
        D -->|No| E["Debug: inspect registers,<br/>memory, exception state"]
        E --> A
        D -->|Yes| F["Document gotcha in devlog"]
        F --> A
    end
```

## Simulation Pipeline

```mermaid
flowchart TD
    subgraph Host["Host Development (WSL)"]
        GCC["arm-none-eabi-gcc<br/>Bare-metal toolchain"]
        Makefile["Makefile<br/>Build + simulate target"]
        Script["simulate.rescript<br/>Renode monitor script"]
        Log["uart_output.log<br/>Captured output"]
    end

    subgraph Renode["Renode Emulation"]
        STM32["STM32H753XI Model<br/>Cortex-M7 @ 480MHz"]
        USART["USART1 Peripheral"]
        DMA["DMA1 Stream 0"]
        CRC["CRC Peripheral"]
        MemMaps["AXI SRAM / SDRAM / Flash"]
    end

    GCC --> Makefile
    Makefile -->|compiles| ELF["firmware.elf"]
    Makefile -->|runs| Script
    Script -->|"loads ELF"| STM32
    STM32 --> USART
    STM32 --> DMA
    STM32 --> CRC
    STM32 --> MemMaps
    USART -->|UART output| Log

    style STM32 fill:#e3f2fd
    style ELF fill:#e8f5e7
    style Log fill:#fff3e0
```

## Cross-Validation Tiers

```mermaid
flowchart LR
    subgraph Tiers["Validation Tiers"]
        T1["Renode Simulation<br/>Primary validation"]
        T2["Physical Silicon<br/>Board testing"]
        T3["Log Comparison<br/>Cross-platform"]
    end

    subgraph Gaps["Renode Model Gaps"]
        G1["DMAMUX1 not modeled"]
        G2["CRC peripheral simplified"]
        G3["PWR low-power states incomplete"]
    end

    T1 --> G1
    T1 --> G2
    T1 --> G3
    G1 --> T2
    G2 --> T2
    G3 --> T2

    style T1 fill:#e8f5e7,color:#000
    style T2 fill:#fff3e0,color:#000
    style G1 fill:#ffebee,color:#000
    style G2 fill:#ffebee,color:#000
    style G3 fill:#ffebee,color:#000
```
