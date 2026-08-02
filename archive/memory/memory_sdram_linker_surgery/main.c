/*
 * v5_sdram_linker main application for STM32H753XI Cortex-M7
 * Demonstrating linker surgery for massive buffers exceeding internal SRAM.
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
#define USART_CR1_TE      (1UL << 3)

void usart1_putchar(char c) {
    while (!(USART_ISR & USART_ISR_TXE)) {}
    USART_TDR = (uint32_t)c;
}

void usart1_print(const char *str) {
    while (*str) usart1_putchar(*str++);
}

/* 
 * ATTEMPT 2 FIX: Linker Script SDRAM Surgery
 * By updating the linker script to map the external SDRAM chip at FMC Bank 2 (0xD0000000) 
 * and defining a custom `.sdram_data` section, we can explicitly command the compiler 
 * to allocate this massive array outside the internal AXI_SRAM.
 */
volatile uint8_t telemetry_circular_buffer[600 * 1024] __attribute__((section(".sdram_data")));

int main(void) {
    /* Ensure USART1 is clocked and configured for debug prints */
    RCC_APB2ENR |= RCC_APB2ENR_USART1EN;
    USART_BRR = 0x222;
    USART_CR1 = USART_CR1_TE | (1UL << 0);
    
    usart1_print("\r\n--- [EDP v5_sdram_linker] External SDRAM Mapping ---\r\n");
    usart1_print("[STATUS] Writing to 600KB telemetry buffer...\r\n");
    
    telemetry_circular_buffer[0] = 0xAA;
    telemetry_circular_buffer[600 * 1024 - 1] = 0xBB;
    
    if (telemetry_circular_buffer[0] == 0xAA && telemetry_circular_buffer[600 * 1024 - 1] == 0xBB) {
        usart1_print("[SUCCESS] Massive buffer boundaries accessed successfully.\r\n");
    }
    
    while(1) {}
    return 0;
}
