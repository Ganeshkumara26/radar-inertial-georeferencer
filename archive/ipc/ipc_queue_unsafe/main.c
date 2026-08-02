/*
 * v7_queue_ipc main application for STM32H753XI Cortex-M7
 * Demonstrating RTOS lockless queues and ISR context API violations.
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

void usart1_putchar(char c) {
    while (!(USART_ISR & USART_ISR_TXE)) {}
    USART_TDR = (uint32_t)c;
}
void usart1_print(const char *str) {
    while (*str) usart1_putchar(*str++);
}

/* Mock RTOS Queue */
volatile uint8_t rtos_queue[128];
volatile uint32_t queue_head = 0;

/* Mock RTOS Core API function (Thread Context ONLY) */
void xQueueSend(uint8_t byte) {
    uint32_t ipsr;
    __asm__ volatile ("mrs %0, ipsr" : "=r" (ipsr));
    
    if (ipsr != 0) {
        usart1_print("[RTOS ASSERT] FATAL: Thread API 'xQueueSend' called from Hardware ISR!\r\n");
        usart1_print("[RTOS ASSERT] System Halted. Use 'xQueueSendFromISR' instead.\r\n");
        while(1) {} /* Simulate Kernel Panic */
    }
    
    rtos_queue[queue_head++] = byte;
    usart1_print("[RTOS] Item queued successfully from Thread Mode.\r\n");
}

/* Mock RTOS Core API function (ISR Context ONLY) */
void xQueueSendFromISR(uint8_t byte, uint8_t *higherPriorityTaskWoken) {
    uint32_t ipsr;
    __asm__ volatile ("mrs %0, ipsr" : "=r" (ipsr));
    
    if (ipsr == 0) {
        usart1_print("[RTOS ASSERT] FATAL: ISR API 'xQueueSendFromISR' called from Thread Mode!\r\n");
        while(1) {}
    }
    
    rtos_queue[queue_head++] = byte;
    *higherPriorityTaskWoken = 1;
    usart1_print("[RTOS] Item safely queued from ISR context.\r\n");
}

void USART1_IRQHandler(void) {
    if (USART_ISR & USART_ISR_RXNE) {
        volatile uint8_t byte = USART_RDR;
        usart1_print("[USART_ISR] Received byte. Pushing to queue...\r\n");
        
        /*
         * ATTEMPT 2 FIX: Use ISR-Safe API
         * We call the non-blocking FromISR variant. If it wakes a higher-priority task,
         * we trigger PendSV to context switch out of the ISR!
         */
        uint8_t yieldRequired = 0;
        xQueueSendFromISR(byte, &yieldRequired);
        if (yieldRequired) {
            usart1_print("[USART_ISR] Requesting PendSV Context Switch...\r\n");
            /* SCB_ICSR |= SCB_ICSR_PENDSVSET; // Assuming SCB_ICSR is defined */
        }
    }
}

int main(void) {
    RCC_APB2ENR |= RCC_APB2ENR_USART1EN;
    USART_BRR = 0x222;
    USART_CR1 = (1UL << 3) | (1UL << 2) | (1UL << 5) | (1UL << 0);
    
    usart1_print("\r\n--- [EDP v7_queue_ipc] RTOS Queue & ISR Context Limits ---\r\n");
    
    volatile uint8_t *nvic_ipr = (volatile uint8_t *)0xE000E400UL;
    nvic_ipr[37] = 0x10;
    NVIC_ISER1 = (1UL << 5); 
    
    usart1_print("[MAIN] Waiting for UART telemetry...\r\n");
    
    while(1) {}
    return 0;
}
