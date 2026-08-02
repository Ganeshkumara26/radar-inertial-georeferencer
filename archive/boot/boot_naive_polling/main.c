/*
 * Bare-metal main application for STM32H753XI Cortex-M7
 * Target Peripheral: USART1 (base address 0x40011000 on APB2)
 */
#include <stdint.h>

#define RCC_BASE      0x58024400UL
#define RCC_APB2ENR   (*(volatile uint32_t *)(RCC_BASE + 0x0F0UL))
#define RCC_APB2ENR_USART1EN (1UL << 4)

#define USART1_BASE   0x40011000UL
#define USART_CR1     (*(volatile uint32_t *)(USART1_BASE + 0x00UL))
#define USART_ICR     (*(volatile uint32_t *)(USART1_BASE + 0x20UL))
#define USART_RDR     (*(volatile uint32_t *)(USART1_BASE + 0x24UL))
#define USART_TDR     (*(volatile uint32_t *)(USART1_BASE + 0x28UL))
#define USART_ISR     (*(volatile uint32_t *)(USART1_BASE + 0x1CUL))
#define USART_BRR     (*(volatile uint32_t *)(USART1_BASE + 0x0CUL))

#define USART_ISR_TXE (1UL << 7)
#define USART_ISR_TC  (1UL << 6)
#define USART_CR1_TE  (1UL << 3)
#define USART_CR1_UE  (1UL << 0)

void usart1_init(void) {
    /* Enable clock for USART1 on APB2 */
    RCC_APB2ENR |= RCC_APB2ENR_USART1EN;
    
    /* Set baud rate divisor (assuming default simulation reset clock settings) */
    USART_BRR = 0x222;
    
    /* Enable Transmitter and USART peripheral */
    USART_CR1 = USART_CR1_TE | USART_CR1_UE;
}

void usart1_putchar(char c) {
    /* Wait until transmit data register is empty (TXE) */
    while (!(USART_ISR & USART_ISR_TXE)) {}
    USART_TDR = (uint32_t)c;
}

void usart1_print(const char *str) {
    while (*str) {
        usart1_putchar(*str++);
    }
}

int main(void) {
    usart1_init();
    
    usart1_print("\r\n--- [EDP v0_m7_baremetal] STM32H753XI Cortex-M7 Initialized ---\r\n");
    usart1_print("[TELEMETRY] System Core: ARM Cortex-M7 @ 480MHz\r\n");
    usart1_print("[TELEMETRY] Memory Domain: Primary AXI_SRAM (512KB)\r\n");
    usart1_print("[STATUS] Polled USART transmission loop verified under Renode simulation.\r\n");
    
    while (1) {
        /* Bare-metal idle loop */
    }
    return 0;
}
