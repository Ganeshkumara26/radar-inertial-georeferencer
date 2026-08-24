/*
 * hw_timer.h — Hardware timer synchronization for radar frame-sync timestamping
 * Target: STM32H753XI Cortex-M7
 *
 * TIM2 (32-bit) free-runs at 1 MHz (1 us resolution).
 * EXTI line captures timer value on radar frame-sync rising edge.
 * Provides microsecond-precise temporal alignment between drone pose and radar sweep.
 */
#ifndef HW_TIMER_H
#define HW_TIMER_H

#include <stdint.h>

#define TIMER_FREQUENCY_HZ    1000000UL  /* 1 MHz = 1 us resolution */
#define TIMER_PERIOD_TICKS    0xFFFFFFFF  /* 32-bit free-running */

/* Frame-sync timestamp record */
typedef struct {
    uint32_t timestamp_us;     /* TIM2 capture value at frame-sync edge */
    uint32_t sequence;         /* Monotonic frame sequence number */
    uint8_t  valid;            /* 1 if this record has been latched */
} frame_sync_timestamp_t;

/* Timer state */
typedef struct {
    uint32_t tim2_clock_freq;  /* APB1 timer clock frequency */
    uint32_t frame_count;      /* Total frames captured */
    uint32_t overflow_count;   /* Timer overflow events */
    frame_sync_timestamp_t last_capture;
} hw_timer_state_t;

void hw_timer_init(hw_timer_state_t *state);
uint32_t hw_timer_get_us(hw_timer_state_t *state);
void hw_timer_frame_sync_isr(hw_timer_state_t *state);
uint32_t hw_timer_get_last_frame_timestamp(hw_timer_state_t *state);

#endif /* HW_TIMER_H */
