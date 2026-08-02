/*
 * v9_power_wfi main application for STM32H753XI Cortex-M7
 * Demonstrating the WFI (Wait For Interrupt) Race Condition (Lost Wakeup).
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
#define USART_ISR_TXE     (1UL << 7)

void usart1_putchar(char c) {
    while (!(USART_ISR & USART_ISR_TXE)) {}
    USART_TDR = (uint32_t)c;
}
void usart1_print(const char *str) {
    while (*str) usart1_putchar(*str++);
}

volatile uint8_t packet_ready = 0;

int main(void) {
    RCC_APB2ENR |= RCC_APB2ENR_USART1EN;
    USART_BRR = 0x222;
    USART_CR1 = (1UL << 3) | (1UL << 2) | (1UL << 0);
    
    usart1_print("\r\n--- [EDP v9_power_wfi] Quiescent Power & WFI Sleep ---\r\n");
    
    while(1) {
        /* 
         * ATTEMPT 2 FIX: Critical Section (PRIMASK)
         * By disabling interrupts globally, we prevent the ISR from firing *during* our check.
         * The WFI instruction architecturally guarantees it will wake the CPU if an 
         * interrupt becomes pending, EVEN IF interrupts are globally disabled!
         */
        __asm__ volatile ("cpsid i"); /* Disable global interrupts */
        if (packet_ready == 0) {
            usart1_print("[MAIN] Flag is 0. CPU preparing to enter low-power WFI sleep...\r\n");
            
            /* Mocking the hardware interrupt firing while IRQs are disabled */
            usart1_print("[USART_ISR] (Mock) Hardware Interrupt becomes PENDING! packet_ready = 1.\r\n");
            packet_ready = 1;
            
            usart1_print("[MAIN] Executing WFI (Safe Critical Section)...\r\n");
            /* WFI still wakes up on pending interrupts! */
            __asm__ volatile ("wfi"); 
            usart1_print("[MAIN] CPU Woke Up!\r\n");
        }
        __asm__ volatile ("cpsie i"); /* Re-enable global interrupts */
         
        if (packet_ready) {
            usart1_print("[MAIN] Processing packet!\r\n");
            packet_ready = 0;
            break; /* Exit test successfully */
        }
    }
    
    while(1) {}
    return 0;
}
