/*
 * v2_mavlink_framing main application for STM32H753XI Cortex-M7
 * Demonstrating the classic C compiler struct padding trap when casting serial buffers.
 */
#include <stdint.h>

#define RCC_BASE          0x58024400UL
#define RCC_APB2ENR       (*(volatile uint32_t *)(RCC_BASE + 0x0F0UL))
#define RCC_APB2ENR_USART1EN (1UL << 4)

#define USART1_BASE       0x40011000UL
#define USART_CR1         (*(volatile uint32_t *)(USART1_BASE + 0x00UL))
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
#define NVIC_ISER1        (*(volatile uint32_t *)(0xE000E104UL))

void usart1_init(void) {
    RCC_APB2ENR |= RCC_APB2ENR_USART1EN;
    USART_BRR = 0x222;
    USART_CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE | USART_CR1_UE;
    NVIC_ISER1 = (1UL << 5); /* IRQ 37 */
}

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

void print_hex8(uint8_t val) {
    for (int i = 4; i >= 0; i -= 4) {
        uint8_t nibble = (val >> i) & 0xF;
        usart1_putchar(nibble < 10 ? '0' + nibble : 'A' + (nibble - 10));
    }
}

/* 
 * ATTEMPT 2 FIX: Packed structure definition.
 * By applying __attribute__((packed)), we force the GCC compiler to eliminate 
 * the 2-byte alignment padding after msgid, perfectly overlaying the 
 * C structure memory offsets onto the contiguous serial wire protocol bytes.
 */
typedef struct __attribute__((packed)) {
    uint8_t magic;   // offset 0
    uint8_t len;     // offset 1
    uint8_t seq;     // offset 2
    uint8_t sysid;   // offset 3
    uint8_t compid;  // offset 4
    uint8_t msgid;   // offset 5
    float roll;      // offset 6
    float pitch;     // offset 10
    float yaw;       // offset 14
    uint16_t crc;    // offset 18
} mavlink_attitude_t;

volatile uint8_t rx_buffer[256];
volatile uint32_t rx_head = 0;
volatile uint8_t packet_ready = 0;

void USART1_IRQHandler(void) {
    if (USART_ISR & USART_ISR_RXNE) {
        uint8_t byte = (uint8_t)(USART_RDR & 0xFF);
        rx_buffer[rx_head++] = byte;
        
        /* 20-byte packet expected */
        if (rx_head == 20 && rx_buffer[0] == 0xFD) {
            packet_ready = 1;
        }
    }
}

int main(void) {
    usart1_init();
    usart1_print("\r\n--- [EDP v2_mavlink_framing] MAVLink Parser ---\r\n");
    
    while (1) {
        if (packet_ready) {
            usart1_print("[EVENT] 20-byte packet received.\r\n");
            
            /* DANGEROUS PATTERN: Casting serial byte buffer directly to unpacked struct */
            mavlink_attitude_t *msg = (mavlink_attitude_t *)rx_buffer;
            
            usart1_print("[DEBUG] Expected Magic: 0xFD, Parsed: 0x");
            print_hex8(msg->magic);
            usart1_print("\r\n");
            
            /* Print the raw hex bits underlying the parsed float fields */
            uint32_t raw_roll = *((uint32_t*)&msg->roll);
            usart1_print("[DEBUG] Expected Roll Bits: 0x3F800000 (1.0f)\r\n");
            usart1_print("[DEBUG] Parsed Roll Bits  : ");
            print_hex32(raw_roll);
            usart1_print("\r\n");
            
            if (raw_roll != 0x3F800000) {
                usart1_print("[ERROR] Struct padding offset shift detected! Data is corrupted.\r\n");
            } else {
                usart1_print("[SUCCESS] Struct packing verified! Floating point payload decoded flawlessly.\r\n");
            }
            
            packet_ready = 0;
            rx_head = 0;
        }
    }
    return 0;
}
