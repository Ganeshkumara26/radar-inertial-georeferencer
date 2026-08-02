/*
 * v10_cache_mpu main application for STM32H753XI Cortex-M7
 * Demonstrating the MPU Execute-Never (XN) Fault on SDRAM Code Execution.
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

#define SCB_SHCSR         (*(volatile uint32_t *)(0xE000ED24UL))
#define SCB_CFSR          (*(volatile uint32_t *)(0xE000ED28UL))

/* MPU Registers */
#define MPU_TYPE          (*(volatile uint32_t *)(0xE000ED90UL))
#define MPU_CTRL          (*(volatile uint32_t *)(0xE000ED94UL))
#define MPU_RNR           (*(volatile uint32_t *)(0xE000ED98UL))
#define MPU_RBAR          (*(volatile uint32_t *)(0xE000ED9CUL))
#define MPU_RASR          (*(volatile uint32_t *)(0xE000EDA0UL))

void usart1_putchar(char c) {
    while (!(USART_ISR & USART_ISR_TXE)) {}
    USART_TDR = (uint32_t)c;
}
void usart1_print(const char *str) {
    while (*str) usart1_putchar(*str++);
}

void MemManage_Handler(void) {
    usart1_print("[FATAL] MemManage Fault Triggered!\r\n");
    if (SCB_CFSR & (1 << 0)) { /* IACCVIOL */
        usart1_print("[FATAL] Instruction Access Violation (IACCVIOL) at 0xD0000000!\r\n");
    }
    while(1) {}
}

/* 
 * We explicitly place this processing function in the SDRAM section (.sdram_data at 0xD0000000)
 * defined in our linker script.
 */
__attribute__((section(".sdram_data"))) void sdram_payload_processor(void) {
    usart1_print("[SDRAM] Executing code from External SDRAM successfully!\r\n");
}

int main(void) {
    RCC_APB2ENR |= RCC_APB2ENR_USART1EN;
    USART_BRR = 0x222;
    USART_CR1 = (1UL << 3) | (1UL << 2) | (1UL << 0);
    
    /* Enable MemManage Fault */
    SCB_SHCSR |= (1 << 16);
    
    usart1_print("\r\n--- [EDP v10_cache_mpu] Memory Protection Unit & XN Execution ---\r\n");
    
    /*
     * ATTEMPT 1 MISTAKE: Unconfigured MPU Execute-Never (XN) Trap
     * By default, the ARM Cortex-M7 treats the external RAM region (0xD0000000) 
     * as Execute-Never (XN). The engineer attempts to execute a function relocated 
     * there without configuring the MPU to allow instruction fetches.
     */
     
    /* 
     * ATTEMPT 2 FIX: MPU Configuration
     * We map Region 0 to 0xD0000000, setting it to Normal Memory, Cacheable, and EXECUTABLE.
     * 
     * MPU_RNR = 0; // Region 0
     * MPU_RBAR = 0xD0000000;
     * // SIZE=16MB (0x17), AP=Full Access (0x3), TEX=1, S=0, C=1, B=1, XN=0, ENABLE=1
     * MPU_RASR = (0x0 << 28) | (0x3 << 24) | (0x1 << 19) | (0x0 << 18) | (0x1 << 17) | (0x1 << 16) | (0x17 << 1) | 1;
     * MPU_CTRL = 1; // Enable MPU
     * __asm__ volatile ("dsb\n\t" "isb\n\t"); // Flush pipeline
     * usart1_print("[MPU] Region 0 Configured: 0xD0000000 is now Executable.\r\n");
     */

    usart1_print("[MAIN] Attempting to execute function in SDRAM (0xD0000000)...\r\n");
    
    /* Calling the function mapped to SDRAM */
    sdram_payload_processor();
    
    usart1_print("[MAIN] Execution completed safely. System resilient.\r\n");
    
    while(1) {}
    return 0;
}
