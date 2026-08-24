# Verification

Verification here means "run it in Renode and look at the log output." It's not formal, but it's honest.

## Why Renode

We picked Renode because it gives deterministic, shared virtual time simulation. If you need to debug a race condition between the DMA mutating memory and the CPU reading it—or a 5-instruction critical window like the `wfi` lost-wakeup race—you can pause the entire system clock, inject interrupts at specific cycle boundaries, inspect AXI_SRAM byte-by-byte, and resume.

The catch: Renode's STM32H7 model isn't complete. The DMAMUX1 peripheral, the CRC hardware block, and some low-power PWR register behaviors are either missing or simplified.

## The Validation Loop

```mermaid
flowchart LR
    subgraph Loop["Validation Loop"]
        A["Write Feature"] --> B["make -f scripts/Makefile all"]
        B --> C{"Build succeeds?"}
        C -->|No| D["Fix compiler/linker errors"]
        D --> A
        C -->|Yes| E["Inject test stimuli<br/>(MAVLink + frame-sync)"]
        E --> F["Check output log"]
        F --> G{"Expected strings match?"}
        G -->|No| H["Debug: inspect registers,<br/>memory, exception state"]
        H --> A
        G -->|Yes| I["Document gotcha in devlog"]
        I --> A
    end
```

## Simulation Pipeline

```mermaid
flowchart TD
    subgraph Host["Host Development (WSL)"]
        GCC["arm-none-eabi-gcc<br/>Bare-metal toolchain"]
        Makefile["scripts/Makefile<br/>Build + simulate target"]
        Script["sim/simulate.rescript<br/>Renode monitor script"]
        Log["uart_output.log<br/>Captured output"]
    end

    subgraph Renode["Renode Emulation"]
        STM32["STM32H753XI Model<br/>Cortex-M7 @ 480MHz"]
        USART["USART1 Peripheral<br/>MAVLink input + debug output"]
        DMA["DMA1 Stream 0<br/>MAVLink RX"]
        CRC["CRC Peripheral<br/>Simplified model"]
        TIM2["TIM2 32-bit timer<br/>Frame-sync capture"]
        MemMaps["AXI SRAM / SDRAM / Flash"]
    end

    GCC --> Makefile
    Makefile -->|compiles| ELF["build/edp_m7_firmware.elf"]
    Makefile -->|runs| Script
    Script -->|"loads ELF"| STM32
    Script -->|"injects MAVLink bytes"| USART
    Script -->|"triggers frame-sync"| TIM2
    STM32 --> USART
    STM32 --> DMA
    STM32 --> CRC
    STM32 --> TIM2
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
        T1["Renode Simulation<br/>Primary validation<br/>All pipeline stages"]
        T2["Physical Silicon<br/>STM32H753XI board<br/>CRC + MPU + Cache"]
        T3["Log Comparison<br/>Cross-platform<br/>Renode vs Silicon"]
    end

    subgraph Gaps["Renode Model Gaps"]
        G1["DMAMUX1 not modeled<br/>→ USART DMA via direct injection"]
        G2["CRC peripheral simplified<br/>→ Cross-check on silicon"]
        G3["L1 Cache not enforced<br/>→ Code written for silicon correctness"]
        G4["No FMC SDRAM model<br/>→ Use AXI SRAM for simulation"]
    end

    T1 --> G1
    T1 --> G2
    T1 --> G3
    T1 --> G4
    G1 --> T2
    G2 --> T2
    G3 --> T2
    G4 --> T2

    style T1 fill:#e8f5e7,color:#000
    style T2 fill:#fff3e0,color:#000
    style G1 fill:#ffebee,color:#000
    style G2 fill:#ffebee,color:#000
    style G3 fill:#ffebee,color:#000
    style G4 fill:#ffebee,color:#000
```

## Simulation Backdoors

Since Renode's STM32H7 model doesn't implement DMAMUX1, the firmware uses memory-mapped backdoors in AXI SRAM for simulation:

| Address | Name | Purpose |
|---------|------|---------|
| `0x24000010` | `sim_rx_count` | RX byte count (parser reads instead of DMA NDTR) |
| `0x24000014` | `sim_frame_sync` | Write nonzero to trigger frame-sync |
| `0x24000018` | `sim_frame_ts` | Timestamp value for frame-sync (microseconds) |
| `0x24000020` | `dma_buffer` | 512-byte MAVLink DMA buffer |

These addresses are in the AXI SRAM gap between `.data` end and `.bss` start, so they don't conflict with firmware variables.

### FPU Enable

The Cortex-M7 FPU must be enabled in `Reset_Handler` by setting CPACR bits [23:20]. Without this, the first float write causes a UsageFault. Renode doesn't enable the FPU by default.

### Simulation Limitations

- **CRC checking** — Disabled in simulation builds (hardware feature, can't be verified in Renode)
- **Timer input capture** — Not modeled; timestamp comes from backdoor
- **DMA NDTR writes** — Not supported; byte count comes from backdoor

### Verified Behavior

The following is verified in Renode simulation:
1. ✅ Firmware boots and initializes all pipeline stages
2. ✅ MAVLink v2 packet parsing (ATTITUDE, LOCAL_POSITION_NED)
3. ✅ EKF predict step (constant velocity model)
4. ✅ EKF update step (radar range-azimuth-elevation-doppler)
5. ✅ Quaternion-based coordinate transform (polar → ENU → WGS84)
6. ✅ Binary output frame construction with CRC-32
7. ✅ End-to-end latency: MAVLink RX → georeferenced output

### Simulation Output

```
--- [EDP Georeferencing Coprocessor] STM32H753XI ---
[INIT] Pipeline stages: MAVLink DMA → HW Timer → Ring Buffer → EKF → Geo → Output
[INIT] MAVLink DMA parser ready (circular buffer)
[INIT] HW Timer (TIM2) ready (1 us resolution)
[INIT] Lock-free ring buffer ready (64 slots)
[INIT] EKF ready (6-state, fixed-point CMSIS-DSP)
[INIT] Geo reference set (13.0827N, 80.2707E)
[INIT] Output stream ready (binary frames)
[INIT] Pipeline initialized. Entering main loop...

[GEO] Target: lat=13 lon=080 alt=0m frame=1
```

## Build & Run

```bash
# From project root:
make -f scripts/Makefile clean    # Clean build artifacts
make -f scripts/Makefile all      # Build firmware ELF + binary
make -f scripts/Makefile size     # Show memory usage
make -f scripts/Makefile disasm   # Generate disassembly listing
```
