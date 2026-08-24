/*
 * hw_timer.c — Hardware timer implementation
 * Target: STM32H753XI Cortex-M7
 *
 * TIM2: 32-bit general-purpose timer, free-running at 1 MHz
 * EXTI line 15_10: Frame-sync input capture on GPIO pin
 * Provides deterministic microsecond timestamps for radar frame alignment.
 */
#include "hw_timer.h"

/* RCC */
#define RCC_BASE          0x58024400UL
#define RCC_APB1LENR      (*(volatile uint32_t *)(RCC_BASE + 0x0E8UL))
#define RCC_APB1LENR_TIM2EN (1UL << 0)
#define RCC_APB2ENR       (*(volatile uint32_t *)(RCC_BASE + 0x0F0UL))
#define RCC_APB2ENR_SYSCFGEN (1UL << 1)

/* TIM2 (32-bit) */
#define TIM2_BASE         0x40000000UL
#define TIM2_CR1          (*(volatile uint32_t *)(TIM2_BASE + 0x00UL))
#define TIM2_CR2          (*(volatile uint32_t *)(TIM2_BASE + 0x04UL))
#define TIM2_DIER         (*(volatile uint32_t *)(TIM2_BASE + 0x0CUL))
#define TIM2_SR           (*(volatile uint32_t *)(TIM2_BASE + 0x10UL))
#define TIM2_CNT          (*(volatile uint32_t *)(TIM2_BASE + 0x24UL))
#define TIM2_PSC          (*(volatile uint32_t *)(TIM2_BASE + 0x28UL))
#define TIM2_ARR          (*(volatile uint32_t *)(TIM2_BASE + 0x2CUL))
#define TIM2_CCR1         (*(volatile uint32_t *)(TIM2_BASE + 0x34UL))
#define TIM2_CCMR1        (*(volatile uint32_t *)(TIM2_BASE + 0x18UL))
#define TIM2_CCER         (*(volatile uint32_t *)(TIM2_BASE + 0x20UL))
#define TIM2_EGR          (*(volatile uint32_t *)(TIM2_BASE + 0x14UL))

#define TIM_CR1_CEN       (1UL << 0)
#define TIM_CR1_ARPE      (1UL << 7)
#define TIM_DIER_CC1IE    (1UL << 1)
#define TIM_DIER_UIE      (1UL << 0)
#define TIM_SR_CC1IF      (1UL << 1)
#define TIM_SR_UIF        (1UL << 0)
#define TIM_CCMR1_CC1S_0  (1UL << 0)  /* CC1 channel as input, IC1 mapped on TI1 */
#define TIM_CCER_CC1E     (1UL << 0)  /* Capture enabled */
#define TIM_CCER_CC1P     (1UL << 1)  /* Capture on rising edge */
#define TIM_EGR_UG        (1UL << 0)

/* EXTI */
#define EXTI_BASE         0x58000000UL
#define EXTI_RTSR1        (*(volatile uint32_t *)(EXTI_BASE + 0x08UL))
#define EXTI_FTSR1        (*(volatile uint32_t *)(EXTI_BASE + 0x0CUL))
#define EXTI_PR1          (*(volatile uint32_t *)(EXTI_BASE + 0x14UL))

/* SYSCFG */
#define SYSCFG_BASE       0x58000400UL
#define SYSCFG_EXTICR3    (*(volatile uint32_t *)(SYSCFG_BASE + 0x0CUL))

/* NVIC */
#define NVIC_ISER0        (*(volatile uint32_t *)(0xE000E100UL))
#define NVIC_ICPR0        (*(volatile uint32_t *)(0xE000E180UL))
#define TIM2_IRQn         28
#define EXTI15_10_IRQn    40

/* Cortex-M7 internal */
#define SCB_SHPR3         (*(volatile uint32_t *)(0xE000ED20UL))

void hw_timer_init(hw_timer_state_t *state) {
    /* Enable TIM2 and SYSCFG clocks */
    RCC_APB1LENR |= RCC_APB1LENR_TIM2EN;
    RCC_APB2ENR |= RCC_APB2ENR_SYSCFGEN;

    /* TIM2 clock: APB1 timer clock = 240 MHz (D2 domain)
     * Prescaler = 240 - 1 gives 1 MHz timer clock (1 us resolution)
     */
    TIM2_CR1 = 0;  /* Disable timer */
    TIM2_PSC = 239;  /* 240 MHz / 240 = 1 MHz */
    TIM2_ARR = TIMER_PERIOD_TICKS;  /* Max 32-bit auto-reload */
    TIM2_EGR = TIM_EGR_UG;  /* Generate update event to load PSC */

    /* Configure Channel 1 as input capture on TI1 (rising edge) */
    TIM2_CCMR1 = TIM_CCMR1_CC1S_0;  /* IC1 mapped on TI1 */
    TIM2_CCER = TIM_CCER_CC1E | TIM_CCER_CC1P;  /* Capture on rising edge */

    /* Enable update interrupt for overflow tracking */
    TIM2_DIER = TIM_DIER_UIE | TIM_DIER_CC1IE;

    /* Configure EXTI line for frame-sync (using PD12 as example) */
    /* Map PD12 to EXTI12 via SYSCFG_EXTICR3 */
    SYSCFG_EXTICR3 &= ~(0xFUL << 8);  /* Clear bits [11:8] for EXTI12 */
    SYSCFG_EXTICR3 |= (3UL << 8);     /* 0011 = Port D */

    /* Rising edge trigger on EXTI12 */
    EXTI_RTSR1 |= (1UL << 12);
    EXTI_FTSR1 &= ~(1UL << 12);  /* Disable falling edge */

    /* Enable EXTI12 interrupt in NVIC */
    NVIC_ICPR0 = (1UL << (EXTI15_10_IRQn & 0x1F));
    NVIC_ISER0 = (1UL << (EXTI15_10_IRQn & 0x1F));

    /* Set TIM2 interrupt priority (high priority for deterministic timing) */
    /* TIM2_IRQn = 28 → ISER0 bit 28 */
    NVIC_ICPR0 = (1UL << (TIM2_IRQn & 0x1F));
    NVIC_ISER0 = (1UL << (TIM2_IRQn & 0x1F));

    /* Clear pending flags */
    TIM2_SR = 0;

    /* Start timer */
    TIM2_CR1 = TIM_CR1_CEN | TIM_CR1_ARPE;

    /* Initialize state */
    state->tim2_clock_freq = TIMER_FREQUENCY_HZ;
    state->frame_count = 0;
    state->overflow_count = 0;
    state->last_capture.timestamp_us = 0;
    state->last_capture.sequence = 0;
    state->last_capture.valid = 0;
}

#include "mavlink_parser.h"  /* For SIMULATION_BUILD and SIM addresses */

uint32_t hw_timer_get_us(hw_timer_state_t *state) {
    (void)state;
#ifdef SIMULATION_BUILD
    /* Simulation: read timestamp from fixed address (Renode writes it) */
    return (*(volatile uint32_t *)SIM_FRAME_TS_ADDR);
#else
    /* Read TIM2 counter — atomic 32-bit read on Cortex-M7 */
    return TIM2_CNT;
#endif
}

void hw_timer_frame_sync_isr(hw_timer_state_t *state) {
#ifdef SIMULATION_BUILD
    /* Simulation: read timestamp from fixed address */
    uint32_t captured = (*(volatile uint32_t *)SIM_FRAME_TS_ADDR);
    /* Clear the trigger */
    (*(volatile uint32_t *)SIM_FRAME_SYNC_ADDR) = 0;
#else
    /* Read TIM2 CCR1 — hardware-captured value at frame-sync edge */
    uint32_t captured = TIM2_CCR1;
    /* Clear capture flag */
    TIM2_SR &= ~TIM_SR_CC1IF;
#endif

    /* Latch the timestamp */
    state->last_capture.timestamp_us = captured;
    state->last_capture.sequence = state->frame_count++;
    state->last_capture.valid = 1;
}

void tim2_overflow_isr(hw_timer_state_t *state) {
    /* Clear update interrupt flag */
    TIM2_SR &= ~TIM_SR_UIF;
    state->overflow_count++;
}

uint32_t hw_timer_get_last_frame_timestamp(hw_timer_state_t *state) {
    if (state->last_capture.valid) {
        return state->last_capture.timestamp_us;
    }
    return 0;
}
