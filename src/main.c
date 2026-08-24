/*
 * main.c — Deterministic Radar-Inertial Georeferencing Coprocessor
 * Target: STM32H753XI Cortex-M7
 *
 * Entry point for the georeferencing pipeline:
 *   1. Initialize bare-metal peripherals (USART debug, DMA, Timers, CRC)
 *   2. Initialize pipeline stages (MAVLink parser, ring buffer, EKF, output)
 *   3. Main loop: parse MAVLink → timestamp → EKF predict → georeference → output
 *
 * Pipeline architecture:
 *   Pixhawk (MAVLink) → [DMA USART1] → MAVLink Parser → Ring Buffer → EKF → Geo Transform → Output
 *   Radar SoC (frame-sync) → [TIM2 Capture] → Timestamp → Ring Buffer
 */

#include <stdint.h>
#include <string.h>

#include "mavlink_parser.h"
#include "hw_timer.h"
#include "ring_buffer.h"
#include "ekf.h"
#include "geo_transform.h"
#include "output_stream.h"

/* USART debug output (polled) */
#define USART1_BASE       0x40011000UL
#define USART1_CR1        (*(volatile uint32_t *)(USART1_BASE + 0x00UL))
#define USART1_BRR        (*(volatile uint32_t *)(USART1_BASE + 0x0CUL))
#define USART1_TDR        (*(volatile uint32_t *)(USART1_BASE + 0x28UL))
#define USART1_ISR        (*(volatile uint32_t *)(USART1_BASE + 0x1CUL))
#define USART_ISR_TXE     (1UL << 7)

/* RCC */
#define RCC_BASE          0x58024400UL
#define RCC_APB2ENR       (*(volatile uint32_t *)(RCC_BASE + 0x0F0UL))
#define RCC_APB2ENR_USART1EN (1UL << 4)

/* MPU */
#define MPU_TYPE          (*(volatile uint32_t *)(0xE000ED90UL))
#define MPU_CTRL          (*(volatile uint32_t *)(0xE000ED94UL))
#define MPU_RNR           (*(volatile uint32_t *)(0xE000ED98UL))
#define MPU_RBAR          (*(volatile uint32_t *)(0xE000ED9CUL))
#define MPU_RASR          (*(volatile uint32_t *)(0xE000EDA0UL))

void usart1_putchar(char c) {
    while (!(USART1_ISR & USART_ISR_TXE)) {}
    USART1_TDR = (uint32_t)c;
}

void usart1_print(const char *str) {
    while (*str) usart1_putchar(*str++);
}

/* Pipeline state */
static mavlink_parser_t   g_mavlink_parser;
static hw_timer_state_t   g_hw_timer;
static ring_buffer_t      g_state_buffer;
static ekf_state_t        g_ekf;
static output_stream_t    g_output;
static geo_reference_t    g_geo_ref;

/* Simulated radar target (in real system, this comes from radar SoC via SPI) */
static radar_target_polar_t g_radar_target;
static uint8_t              g_radar_target_available = 0;

/* System status */
static uint32_t g_frame_count = 0;

/* Simulation backdoor: check for frame-sync trigger from Renode */
#ifdef SIMULATION_BUILD
static void check_sim_frame_sync(void) {
    volatile uint32_t *sim_fs = (volatile uint32_t *)SIM_FRAME_SYNC_ADDR;
    if (*sim_fs) {
        /* Trigger the frame-sync handler */
        hw_timer_frame_sync_isr(&g_hw_timer);
    }
}
#endif

static void pipeline_init(void) {
    /* Enable USART1 clock and configure for debug output */
    RCC_APB2ENR |= RCC_APB2ENR_USART1EN;
    USART1_BRR = 0x222;  /* ~921600 baud at 480MHz */
    USART1_CR1 = (1UL << 3) | (1UL << 0);  /* TE, UE */

    usart1_print("\r\n--- [EDP Georeferencing Coprocessor] STM32H753XI ---\r\n");
    usart1_print("[INIT] Pipeline stages: MAVLink DMA → HW Timer → Ring Buffer → EKF → Geo → Output\r\n");

    /* Initialize MAVLink parser (DMA-based) */
    mavlink_parser_init(&g_mavlink_parser);
    usart1_print("[INIT] MAVLink DMA parser ready (circular buffer)\r\n");

    /* Initialize hardware timer (microsecond timestamps) */
    hw_timer_init(&g_hw_timer);
    usart1_print("[INIT] HW Timer (TIM2) ready (1 us resolution)\r\n");

    /* Initialize ring buffer */
    ring_buffer_init(&g_state_buffer);
    usart1_print("[INIT] Lock-free ring buffer ready (64 slots)\r\n");

    /* Initialize EKF */
    ekf_config_t ekf_config = {
        .dt = 0.01f,              /* 100 Hz prediction */
        .process_noise = 0.1f,    /* m/s^2 process noise */
        .meas_noise_range = 1.0f, /* m^2 range noise */
        .meas_noise_angle = 0.01f,/* rad^2 angle noise */
        .meas_noise_doppler = 0.5f /* (m/s)^2 Doppler noise */
    };
    ekf_init(&g_ekf, &ekf_config);
    usart1_print("[INIT] EKF ready (6-state, fixed-point CMSIS-DSP)\r\n");

    /* Initialize geo reference (example: Chennai, India) */
    geo_init_reference(&g_geo_ref, 13.0827, 80.2707, 0.0f);
    usart1_print("[INIT] Geo reference set (13.0827N, 80.2707E)\r\n");

    /* Initialize output stream (USART polled for debug) */
    output_stream_init(&g_output, 0);  /* 0 = USART polled */
    usart1_print("[INIT] Output stream ready (binary frames)\r\n");

    usart1_print("[INIT] Pipeline initialized. Entering main loop...\r\n\r\n");
}

static void pipeline_run(void) {
    kinematic_state_t state;

#ifdef SIMULATION_BUILD
    /* Check for simulated frame-sync */
    check_sim_frame_sync();
    /* Check for radar target re-arm (must be before EKF processing) */
    volatile uint32_t *sim_radar = (volatile uint32_t *)SIM_RADAR_TRIGGER_ADDR;
    if (*sim_radar) {
        g_radar_target_available = 1;
        *sim_radar = 0;
    }
#endif

    /* Stage 1: Poll MAVLink parser for new attitude/position data */
    while (mavlink_parser_poll(&g_mavlink_parser, &state) > 0) {
        /* Tag with hardware timestamp */
        state.timestamp_us = hw_timer_get_us(&g_hw_timer);

        /* Push to ring buffer for EKF consumption */
        if (ring_buffer_push(&g_state_buffer, &state) != 0) {
            /* Buffer full — drop oldest and retry */
            kinematic_state_t dummy;
            ring_buffer_pop(&g_state_buffer, &dummy);
            ring_buffer_push(&g_state_buffer, &state);
        }
    }

    /* Stage 2: Process state buffer through EKF */
    kinematic_state_t buf_state;
#ifdef SIMULATION_BUILD
    uint32_t ekf_iters = 0;
#endif
    while (ring_buffer_peek(&g_state_buffer, &buf_state) == 0) {
#ifdef SIMULATION_BUILD
        ekf_iters++;
#endif
        /* EKF predict step */
        ekf_predict(&g_ekf, 0.01f);  /* 100 Hz */

        /* If we have a radar target, run EKF update */
        if (g_radar_target_available && buf_state.has_attitude && buf_state.has_local_pos) {
            float innovation[4];
            int result = ekf_update_radar(&g_ekf,
                                          g_radar_target.range,
                                          g_radar_target.azimuth,
                                          g_radar_target.elevation,
                                          g_radar_target.doppler,
                                          innovation);

            if (result == 0) {
                /* EKF update successful — georeference the target */
                quaternion_t attitude;
                euler_to_quaternion(buf_state.roll, buf_state.pitch, buf_state.yaw, &attitude);

                target_wgs84_t georeferenced;
                geo_transform_target(&g_radar_target, &attitude, buf_state.yaw,
                                     &g_geo_ref, &georeferenced);

                /* Output georeferenced target */
                output_stream_write_targets(&g_output, &georeferenced, 1,
                                            hw_timer_get_us(&g_hw_timer));

                g_frame_count++;

                /* Print georeferenced result */
                usart1_print("[GEO] Target: lat=");
                int lat_int = (int)georeferenced.latitude;
                usart1_putchar('0' + (lat_int / 10) % 10);
                usart1_putchar('0' + lat_int % 10);
                usart1_print(" lon=");
                int lon_int = (int)georeferenced.longitude;
                usart1_putchar('0' + (lon_int / 100) % 10);
                usart1_putchar('0' + (lon_int / 10) % 10);
                usart1_putchar('0' + lon_int % 10);
                usart1_print(" alt=");
                int alt_int = (int)georeferenced.altitude;
                usart1_putchar('0' + (alt_int / 10) % 10);
                usart1_putchar('0' + alt_int % 10);
                usart1_print("m frame=");
                usart1_putchar('0' + (g_frame_count % 10));
                usart1_print("\r\n");
            }

            g_radar_target_available = 0;
        }

#ifdef SIMULATION_BUILD
        /* Re-arm radar target for next state (simulation only) */
        if (buf_state.has_attitude && buf_state.has_local_pos) {
            g_radar_target_available = 1;
        }
#endif

        /* Consume the buffer entry */
        ring_buffer_pop(&g_state_buffer, &buf_state);
    }

#ifdef SIMULATION_BUILD
    /* Debug: print EKF iterations */
    if (ekf_iters > 0) {
        usart1_print("[EKF] iters=");
        usart1_putchar('0' + (ekf_iters % 10));
        usart1_print(" frames=");
        usart1_putchar('0' + (g_frame_count % 10));
        usart1_print("\r\n");
    }
    /* Re-arm radar target for next frame (simulation only) */
    g_radar_target_available = 1;
#endif
}

int main(void) {
    pipeline_init();

    /* Simulate incoming radar target for testing */
    g_radar_target.range = 10.0f;
    g_radar_target.azimuth = 0.1f;
    g_radar_target.elevation = -0.2f;
    g_radar_target.doppler = 0.5f;
    g_radar_target.snr = 20.0f;
    g_radar_target.timestamp_us = 0;
    g_radar_target_available = 1;

    while (1) {
        pipeline_run();
    }

    return 0;
}
