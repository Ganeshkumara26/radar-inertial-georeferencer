/*
 * v4_hw_crc_endian main application for STM32H753XI Cortex-M7
 * Demonstrating word-level byte reversal endianness collisions in hardware CRC.
 */
#include <stdint.h>

#define RCC_BASE          0x58024400UL
#define RCC_APB2ENR       (*(volatile uint32_t *)(RCC_BASE + 0x0F0UL))
#define RCC_AHB4ENR       (*(volatile uint32_t *)(RCC_BASE + 0x140UL))
#define RCC_APB2ENR_USART1EN (1UL << 4)
#define RCC_AHB4ENR_CRCEN    (1UL << 19)

#define USART1_BASE       0x40011000UL
#define USART_CR1         (*(volatile uint32_t *)(USART1_BASE + 0x00UL))
#define USART_BRR         (*(volatile uint32_t *)(USART1_BASE + 0x0CUL))
#define USART_TDR         (*(volatile uint32_t *)(USART1_BASE + 0x28UL))
#define USART_ISR         (*(volatile uint32_t *)(USART1_BASE + 0x1CUL))
#define USART_ISR_TXE     (1UL << 7)
#define USART_CR1_TE      (1UL << 3)

#define CRC_BASE          0x40023000UL
#define CRC_DR            (*(volatile uint32_t *)(CRC_BASE + 0x00UL))
#define CRC_CR            (*(volatile uint32_t *)(CRC_BASE + 0x08UL))
#define CRC_CR_RESET      (1UL << 0)

void usart1_putchar(char c) {
    while (!(USART_ISR & USART_ISR_TXE)) {}
    USART_TDR = (uint32_t)c;
}

void usart1_print(const char *str) {
    while (*str) usart1_putchar(*str++);
}

void print_hex32(uint32_t val) {
    usart1_print("0x");
    for (int i = 28; i >= 0; i -= 4) {
        uint8_t nibble = (val >> i) & 0xF;
        usart1_putchar(nibble < 10 ? '0' + nibble : 'A' + (nibble - 10));
    }
}

int main(void) {
    /* Ensure USART1 is clocked and configured for debug prints */
    RCC_APB2ENR |= RCC_APB2ENR_USART1EN;
    USART_BRR = 0x222;
    USART_CR1 = USART_CR1_TE | (1UL << 0);
    
    usart1_print("\r\n--- [EDP v4_hw_crc_endian] Hardware CRC32 Validation ---\r\n");
    
    /* Enable CRC Hardware Clock */
    RCC_AHB4ENR |= RCC_AHB4ENR_CRCEN;
    
    const uint8_t packet[4] = {'1', '2', '3', '4'}; 
    
    /* Compute Baseline CRC by writing bytes sequentially (SLOW but logically correct) */
    CRC_CR = CRC_CR_RESET;
    for(int i = 0; i < 4; i++) {
        *(volatile uint8_t *)&CRC_DR = packet[i];
    }
    uint32_t expected_crc = CRC_DR;
    
    /* 
     * ATTEMPT 1 MISTAKE: The Word-Level Optimization Trap
     * Engineer optimizes the 8-bit loop by writing a full 32-bit word directly.
     * Because the ARM core is Little Endian, the bytes ['1', '2', '3', '4'] 
     * are loaded into the register as 0x34333231.
     * The hardware CRC engine strictly processes the MSB first (0x34), effectively 
     * feeding the bytes backwards into the polynomial!
     */
    CRC_CR = CRC_CR_RESET;
    uint32_t *word_ptr = (uint32_t *)packet;
    CRC_DR = *word_ptr;
    uint32_t optimized_crc = CRC_DR;
    
    usart1_print("[DEBUG] Sequential 8-bit CRC : "); print_hex32(expected_crc); usart1_print("\r\n");
    usart1_print("[DEBUG] Optimized 32-bit CRC : "); print_hex32(optimized_crc); usart1_print("\r\n");
    
    if (expected_crc != optimized_crc) {
        usart1_print("[ERROR] Endianness Collision! The 32-bit optimization destroyed the checksum.\r\n");
    }
    
    while(1) {}
    return 0;
}
