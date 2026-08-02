/*
 * v6_rtos_integration main application for STM32H753XI Cortex-M7
 * Demonstrating the RTOS PendSV context switch priority inversion trap.
 */
#include <stdint.h>

#define RCC_BASE          0x58024400UL
#define RCC_APB2ENR       (*(volatile uint32_t *)(RCC_BASE + 0x0F0UL))
#define RCC_APB2ENR_USART1EN (1UL << 4)

#define USART1_BASE       0x40011000UL
#define USART_CR1         (*(volatile uint32_t *)(USART1_BASE + 0x00UL))
#define USART_BRR         (*(volatile uint32_t *)(USART1_BASE + 0x0CUL))
#define USART_TDR         (*(volatile uint32_t *)(USART1_BASE + 0x28UL))
#define USART_ISR         (*(volatile uint32_t *)(USART1_BASE + 0x1CUL))
#define USART_RDR         (*(volatile uint32_t *)(USART1_BASE + 0x24UL))
#define USART_ISR_TXE     (1UL << 7)
#define USART_ISR_RXNE    (1UL << 5)

#define NVIC_ISER1        (*(volatile uint32_t *)(0xE000E104UL))

#define SCB_BASE          0xE000ED00UL
#define SCB_ICSR          (*(volatile uint32_t *)(SCB_BASE + 0x04UL))
#define SCB_ICSR_PENDSVSET (1UL << 28)

#define SYSTICK_BASE      0xE000E010UL
#define SYSTICK_CSR       (*(volatile uint32_t *)(SYSTICK_BASE + 0x00UL))
#define SYSTICK_RVR       (*(volatile uint32_t *)(SYSTICK_BASE + 0x04UL))
#define SYSTICK_CVR       (*(volatile uint32_t *)(SYSTICK_BASE + 0x08UL))

void usart1_putchar(char c) {
    while (!(USART_ISR & USART_ISR_TXE)) {}
    USART_TDR = (uint32_t)c;
}
void usart1_print(const char *str) {
    while (*str) usart1_putchar(*str++);
}

void delay_cycles(uint32_t cycles) {
    while (cycles--) { __asm__ volatile("nop"); }
}

void SysTick_Handler(void) {
    usart1_print("[SysTick] Tick! Triggering PendSV for RTOS Context Switch...\r\n");
    SCB_ICSR |= SCB_ICSR_PENDSVSET;
}

void PendSV_Handler_C(uint32_t lr) {
    usart1_print("[PendSV] Executing Context Switch...\r\n");
    
    if ((lr & (1UL << 3)) == 0) {
        usart1_print("[FATAL] PendSV preempted an active Hardware ISR! Stack corrupted!\r\n");
        usart1_print("[FATAL] UsageFault / HardFault triggered.\r\n");
        while(1) {} /* Simulate CPU lockup */
    }
    
    usart1_print("[SUCCESS] PendSV safely preempted Thread Mode.\r\n");
}

__attribute__((naked)) void PendSV_Handler(void) {
    /* 
     * ATTEMPT 3 FIX: The C Compiler Prologue Trap
     * If we don't use a naked wrapper, GCC inserts `push {r7, lr}` before our inline assembly,
     * destroying the hardware EXC_RETURN value. We capture it in r0 and branch to C.
     */
    __asm__ volatile (
        "mov r0, lr\n\t"
        "b PendSV_Handler_C\n\t"
    );
}

void USART1_IRQHandler(void) {
    if (USART_ISR & USART_ISR_RXNE) {
        volatile uint32_t dummy = USART_RDR; /* Clear RXNE flag */
        usart1_print("[USART_ISR] Entering Hardware Interrupt (IRQ 37)...\r\n");
        
        /* Deliberately stall in ISR to allow SysTick to preempt us */
        delay_cycles(2000000);
        
        usart1_print("[USART_ISR] Exiting Hardware Interrupt.\r\n");
    }
}

int main(void) {
    RCC_APB2ENR |= RCC_APB2ENR_USART1EN;
    USART_BRR = 0x222;
    USART_CR1 = (1UL << 3) | (1UL << 2) | (1UL << 5) | (1UL << 0); /* TE, RE, RXNEIE, UE */
    
    usart1_print("\r\n--- [EDP v6_rtos_integration] RTOS Context Switching ---\r\n");
    
    /* 
     * ATTEMPT 1 MISTAKE: PendSV Priority Inversion
     * The engineer leaves PendSV and SysTick at default priority 0 (Highest).
     * USART1 is assigned priority 1 (Lower priority than 0).
     */
    volatile uint8_t *nvic_ipr = (volatile uint8_t *)0xE000E400UL;
    nvic_ipr[37] = 0x10; /* Set IRQ 37 to Priority 1 */
    NVIC_ISER1 = (1UL << 5); /* Enable IRQ 37 */
    
    /* 
     * ATTEMPT 2 FIX: Set PendSV to lowest priority 0xFF!
     * This guarantees PendSV will only execute when no other hardware ISRs are active,
     * safely performing the context switch out of Thread Mode.
     */
    volatile uint8_t *scb_shpr = (volatile uint8_t *)0xE000ED18UL;
    scb_shpr[10] = 0xFF; /* PendSV priority = Lowest */
    
    /* Start SysTick to fire rapidly and trigger context switches */
    SYSTICK_RVR = 500000;
    SYSTICK_CVR = 0;
    SYSTICK_CSR = (1UL << 2) | (1UL << 1) | (1UL << 0); /* CLKSOURCE, TICKINT, ENABLE */
    
    while(1) {}
    return 0;
}
