# VERIFIED FACTS (reuse verbatim, do not alter)
Target part: STM32H753XI (Arm Cortex-M7). Core clock as printed by firmware: 480 MHz.
RCC base 0x58024400. RCC_APB2ENR +0x0F0 bit4 = USART1EN. RCC_AHB4ENR +0x140 bit19 = CRCEN.
USART1 base 0x40011000 (CR1 +0x00, BRR +0x0C, ISR +0x1C, ICR +0x20, RDR +0x24, TDR +0x28).
USART1 IRQ number 37 -> NVIC_ISER[1] bit 5 (37/32=1, 37%32=5).
Vector table: 57 entries idx 0-56; USART1_IRQHandler at index 53.
CRC base 0x40023000 (DR +0x00, CR +0x08); CRC_CR_RESET=bit0; REV_IN=bits[6:5]=01.
FLASH: ORIGIN=0x08000000 LENGTH=2048K. SRAM: ORIGIN=0x24000000 LENGTH=512K.
SDRAM: ORIGIN=0xD0000000 LENGTH=32M (FMC Bank2).
TIM2 base 0x40000000 (32-bit timer). TIM2_IRQn = 28.
SPI1 base 0x40013000.
SCB base 0xE000ED00. NVIC ISER0 base 0xE000E100.
MPU: TYPE 0xE000ED90, CTRL 0xE000ED94, RNR 0xE000ED98, RBAR 0xE000ED9C, RASR 0xE000EDA0.
DMA1 Stream 0: S0CR 0x40020010, S0NDTR 0x40020014, S0PAR 0x40020018, S0M0AR 0x4002001C.
DMA1 Stream 0 IRQn = 11. DMA1_LIFCR 0x40020008.
EXTI: RTSR1 0x58000008, PR1 0x58000014. EXTI15_10_IRQn = 40.
CPACR 0xE000ED88 (FPU enable bits [23:20]).
EKF_STATE_DIM=6, EKF_MEAS_DIM=4.
Backdoors: SIM_RX_COUNT_ADDR=0x24000010, SIM_FRAME_SYNC_ADDR=0x24000014,
  SIM_FRAME_TS_ADDR=0x24000018, SIM_RADAR_TRIGGER_ADDR=0x2400001C.
DMA_BUFFER at 0x24000020 (g_mavlink_parser.dma_buffer, 512 bytes).
g_ekf at 0x24001668, g_hw_timer at 0x24000240, g_state_buffer at 0x24000258.

# TITLE (recommended, use exactly)
Deterministic Radar-Inertial Georeferencing Coprocessor on STM32H7 (Arm Cortex-M7):
Bare-Metal Firmware Design and Renode Hardware-in-the-Loop Verification

# ABSTRACT (~290 words)
Drone-based radar search-and-rescue requires real-time spatial georeferencing: translating local radar detections into global GPS coordinates by fusing the drone's inertial state with radar range-Doppler measurements. This thesis presents the design and verification of a deterministic radar-inertial georeferencing coprocessor on the STM32H753 (Arm Cortex-M7), built entirely at the bare-metal register level and verified using Antmicro's open-source Renode functional simulator with hardware-in-the-loop testing using real MAVLink telemetry data. The coprocessor ingests high-rate MAVLink attitude and position telemetry over USART1 using DMA, hardware-timestampes radar frames using a 32-bit timer at 1 us resolution, runs a 6-state Extended Kalman Filter fusing inertial and radar measurements, converts local polar radar coordinates to global WGS84 via quaternion transforms, and streams georeferenced targets over SPI. Eleven progressive case studies isolate pipeline-stage defects: build-system path resolution, IRQn suffix typo, linker C library dependencies, DMA-to-CPU cache coherency, struct-packing alignment, timer prescaler shadow loading, interrupt priority inversion, memory barriers, EKF matrix aliasing, quaternion rotation direction, output frame length, and parser packet management. Hardware-in-the-loop testing injects real MAVLink v2 packets (pymavlink) via AXI SRAM backdoors. Renode-silicon divergences (DMAMUX1, timer input capture, CRC hardware) are documented and worked around; firmware is engineered defensively for silicon correctness.

KEYWORDS: Embedded Systems; ARM Cortex-M7; STM32H7; Renode; Kalman Filtering;
Georeferencing; MAVLink; Sensor Fusion; Deterministic Real-Time Systems; DMA;
Cache Coherency; Quaternion; Coordinate Transformation.

# REFERENCES (IEEE numeric)
[1] STMicroelectronics, RM0433 -- STM32H742, STM32H743/753 and STM32H750 Value line advanced Arm-based 32-bit MCUs, reference manual.
[2] STMicroelectronics, PM0253 -- STM32F7 series and STM32H7 series Cortex-M7 processor programming manual.
[3] STMicroelectronics, DS12117 -- STM32H753xI datasheet.
[4] Arm Ltd., Cortex-M7 Technical Reference Manual, developer.arm.com.
[5] Arm Ltd., Armv7-M Architecture Reference Manual, developer.arm.com.
[6] Antmicro, Renode open-source simulation framework, https://github.com/renode/renode.
[7] MAVLink protocol specification, https://mavlink.io.
[8] S. Thrun, W. Burgard, D. Fox, Probabilistic Robotics, MIT Press, 2005.
[9] M. Grewal, A. Andrews, Kalman Filtering: Theory and Practice with MATLAB, Wiley.
[10] J. B. Kuipers, Quaternions and Rotation Sequences, Princeton University Press.
[11] ARM, CMSIS-DSP Library, https://arm-software.github.io/CMSIS-DSP.