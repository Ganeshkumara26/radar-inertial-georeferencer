/*
 * v1_nvic_uart_isr main application for STM32H753XI Cortex-M7
 * Transitioning from CPU-stalling polling to ARM NVIC vectored reception.
 */
#include <stdint.h>

#define RCC_BASE          0x58024400UL
#define RCC_APB2ENR       (*(volatile uint32_t *)(RCC_BASE + 0x0F0UL))
#define RCC_APB2ENR_USART1EN (1UL << 4)

#define USART1_BASE       0x40011000UL
#define USART_CR1         (*(volatile uint32_t *)(USART1_BASE + 0x00UL))
#define USART_ICR         (*(volatile uint32_t *)(USART1_BASE + 0x20UL))
#define USART_RDR         (*(volatile uint32_t *)(USART1_BASE + 0x24UL))
#define USART_TDR         (*(volatile uint32_t *)(USART1_BASE + 0x28UL))
#define USART_ISR         (*(volatile uint32_t *)(USART1_BASE + 0x1CUL))
#define USART_BRR         (*(volatile uint32_t *)(USART1_BASE + 0x0CUL))

#define USART_ISR_TXE     (1UL << 7)
#define USART_ISR_RXNE    (1UL << 5)
#define USART_CR1_RXNEIE  (1UL << 5)
#define USART_CR1_TE      (1UL << 3)
#define USART_CR1_RE      (1UL << 2)
#define USART_CR1_UE      (1UL << 0)

#define NVIC_ISER0_BASE   0xE000E100UL
#define NVIC_ISER(n)      (*(volatile uint32_t *)(NVIC_ISER0_BASE + ((n) * 4UL)))

/* Volatile reception buffer in Primary AXI_SRAM */
volatile char rx_buffer[64];
volatile uint32_t rx_head = 0;
volatile uint32_t rx_count = 0;

void usart1_init(void) {
    /* Enable clock for USART1 */
    RCC_APB2ENR |= RCC_APB2ENR_USART1EN;
    USART_BRR = 0x222;
    
    /* Enable Transmitter, Receiver, Receive Interrupt, and USART Core */
    USART_CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE | USART_CR1_UE;
    
    /* Enable USART1 Global Interrupt in ARM Cortex-M7 NVIC
       On STM32H753XI, USART1 is IRQ number 37.
       Register offset: 37 / 32 = 1. Bit offset: 37 % 32 = 5. */
    NVIC_ISER(1) = (1UL << 5);
}

void usart1_putchar(char c) {
    while (!(USART_ISR & USART_ISR_TXE)) {}
    USART_TDR = (uint32_t)c;
}

void usart1_print(const char *str) {
    while (*str) {
        usart1_putchar(*str++);
    }
}

/* Interrupt Handler for USART1 */
void USART1_IRQHandler(void) {
    if (USART_ISR & USART_ISR_RXNE) {
        char c = (char)(USART_RDR & 0xFF);
        rx_buffer[rx_head] = c;
        rx_head = (rx_head + 1) % 64;
        rx_count++;
        
        /* Echo back telemetry acknowledgement */
        usart1_print("[ISR RX] Received byte: ");
        usart1_putchar(c);
        usart1_print("\r\n");
    }
}

int main(void) {
    usart1_init();
    
    usart1_print("\r\n--- [EDP v1_nvic_uart_isr] STM32H753XI NVIC Initialized ---\r\n");
    usart1_print("[TELEMETRY] IRQn 37 (USART1) Unmasked in NVIC_ISER[1] bit 5.\r\n");
    usart1_print("[STATUS] Awaiting asynchronous serial interrupt injection...\r\n");
    
    while (1) {
        /* CPU loop remains unblocked and reactive to ISR events */
        if (rx_count >= 3) {
            usart1_print("[EVENT] 3 telemetry bytes successfully assembled via NVIC ISR!\r\n");
            rx_count = 0; /* Reset trigger for simulation verification */
        }
    }
    return 0;
}
