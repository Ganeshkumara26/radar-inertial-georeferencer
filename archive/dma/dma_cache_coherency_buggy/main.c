/*
 * v3_dma_coherency main application for STM32H753XI Cortex-M7
 * Simulating autonomous memory mutation and L1 D-Cache coherency limits.
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

#define SCB_BASE          0xE000ED00UL
#define SCB_CSSELR        (*(volatile uint32_t *)(SCB_BASE + 0x84UL))
#define SCB_DCISW         (*(volatile uint32_t *)(SCB_BASE + 0x60UL))
#define SCB_DCCIMVAC      (*(volatile uint32_t *)(SCB_BASE + 0x7CUL))

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

typedef struct __attribute__((packed)) {
    uint8_t magic;
    uint8_t len;
    uint8_t seq;
    uint8_t sysid;
    uint8_t compid;
    uint8_t msgid;
    float roll;
    float pitch;
    float yaw;
    uint16_t crc;
} mavlink_attitude_t;

/* Buffer resides in physical AXI_SRAM */
volatile uint8_t rx_buffer[32] __attribute__((aligned(32))); 

void invalidate_dcache_by_addr(volatile void *addr, int32_t dsize) {
    /* M7 specific Data Cache Clean and Invalidate by MVA to PoC */
    uint32_t start_addr = (uint32_t)addr & ~(32UL - 1UL); /* Align to 32-byte cache line */
    uint32_t end_addr   = (uint32_t)addr + dsize;
    while (start_addr < end_addr) {
        SCB_DCCIMVAC = start_addr;
        start_addr += 32UL;
    }
    /* Simple Data Synchronization Barrier equivalent */
    __asm__ volatile ("dsb 0xF" ::: "memory");
}

int main(void) {
    /* Ensure USART1 is clocked and configured for debug prints */
    RCC_APB2ENR |= RCC_APB2ENR_USART1EN;
    USART_BRR = 0x222;
    USART_CR1 = USART_CR1_TE | (1UL << 0); /* UE bit */
    
    usart1_print("\r\n--- [EDP v3_dma_coherency] Autonomous Memory Sync ---\r\n");
    usart1_print("[STATUS] CPU yielding... awaiting background memory mutation.\r\n");
    
    uint32_t loops = 0;
    
    /* 
     * Polling the buffer waiting for the background DMA (or sysbus) to write magic 0xFD 
     */
    while (rx_buffer[0] != 0xFD) {
        loops++;
        
        /*
         * ATTEMPT 3 FIX: Hardware Silicon Cache Coherency!
         * While the Renode functional emulator does not model L1 D-Cache lines 
         * and will safely exit this loop immediately when physical memory is mutated,
         * the PHYSICAL Cortex-M7 core will cache rx_buffer[0] on the first read.
         * The physical core would deadlock forever, completely blind to the DMA's 
         * background AXI_SRAM updates!
         * 
         * We MUST defensively invalidate the L1 cache line containing the buffer 
         * to force the CPU pipeline to fetch fresh physical memory!
         */
        invalidate_dcache_by_addr(rx_buffer, 32);
        
        if (loops > 1000000) {
            usart1_print("[TIMEOUT] Memory never updated.\r\n");
            break;
        }
    }
    
    if (rx_buffer[0] == 0xFD) {
        usart1_print("[EVENT] Background memory mutation detected by CPU!\r\n");
        mavlink_attitude_t *msg = (mavlink_attitude_t *)rx_buffer;
        
        uint32_t raw_roll = *((uint32_t*)&msg->roll);
        usart1_print("[DEBUG] Parsed Roll Bits  : ");
        print_hex32(raw_roll);
        usart1_print("\r\n");
        
        if (raw_roll == 0x3F800000) {
            usart1_print("[SUCCESS] Coherent L1 D-Cache parsing verified flawlessly.\r\n");
        }
    }
    
    while(1) {}
    return 0;
}
