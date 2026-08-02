/*
 * v8_fault_resilience main application for STM32H753XI Cortex-M7
 * Demonstrating adversarial packet injection and stack smashing vulnerabilities.
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

void *my_memcpy(void *dest, const void *src, uint32_t n) {
    uint8_t *d = (uint8_t*)dest;
    const uint8_t *s = (const uint8_t*)src;
    while(n--) { *d++ = *s++; }
    return dest;
}

typedef struct __attribute__((packed)) {
    uint8_t magic;
    uint8_t len;
    uint8_t payload[255]; /* Max theoretical payload size */
} mavlink_packet_t;

/* Prevent compiler from inlining to ensure standard stack frame creation */
__attribute__((noinline)) void process_telemetry_packet(mavlink_packet_t *packet) {
    /* 
     * Local stack buffer to hold the payload securely for processing. 
     * We expect payloads to be a maximum of 32 bytes for orientation data.
     */
    uint8_t secure_payload_buffer[32];
    
    usart1_print("[PARSER] Extracting payload from packet...\r\n");
    
    /* 
     * ATTEMPT 1 MISTAKE: Blindly Trusting the Header Length
     * The engineer uses the untrusted length field specified IN the packet itself
     * to dictate how many bytes to copy into the fixed 32-byte stack buffer.
     * An adversarial or corrupted packet with len = 200 will cause my_memcpy
     * to write upwards, obliterating the caller's stack frame and overwriting 
     * the Return Address (LR).
     */
    
    /* 
     * ATTEMPT 2 FIX: Defensive Length Verification
     * By validating the untrusted packet header against our fixed architectural limits,
     * we guarantee memory safety regardless of RF corruption or adversarial intent.
     */
    if (packet->len > sizeof(secure_payload_buffer)) {
        usart1_print("[PARSER] SECURITY FAULT: Adversarial packet length exceeds buffer!\r\n");
        usart1_print("[PARSER] Packet dropped safely. System resilient.\r\n");
        return;
    }
    
    my_memcpy(secure_payload_buffer, packet->payload, packet->len);
    
    usart1_print("[PARSER] Payload successfully extracted into stack buffer.\r\n");
}

int main(void) {
    RCC_APB2ENR |= RCC_APB2ENR_USART1EN;
    USART_BRR = 0x222;
    USART_CR1 = (1UL << 3) | (1UL << 2) | (1UL << 0);
    
    usart1_print("\r\n--- [EDP v8_fault_resilience] Adversarial Packet Stress Test ---\r\n");
    
    /* Construct a simulated adversarial packet in memory */
    mavlink_packet_t adversarial_packet;
    adversarial_packet.magic = 0xFD;
    
    /* 
     * The payload is only supposed to be 32 bytes max. 
     * We inject an adversarial length of 200 to intentionally smash the stack.
     */
    adversarial_packet.len = 200; 
    
    for(int i = 0; i < 200; i++) {
        adversarial_packet.payload[i] = 0xDE; /* 0xDE creates a 0xDEDEDEDE return address */
    }
    
    usart1_print("[MAIN] Passing adversarial packet to parser...\r\n");
    
    process_telemetry_packet(&adversarial_packet);
    
    usart1_print("[MAIN] System survived! Executing next cycle.\r\n");
    
    while(1) {}
    return 0;
}
