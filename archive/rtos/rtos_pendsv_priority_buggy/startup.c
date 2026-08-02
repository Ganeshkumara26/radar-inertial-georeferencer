/*
 * Bare-metal startup file for STM32H753XI Cortex-M7
 */
#include <stdint.h>

extern uint32_t _estack;
extern uint32_t _sdata;
extern uint32_t _edata;
extern uint32_t _sbss;
extern uint32_t _ebss;
extern uint32_t _etext;

extern int main(void);

void Reset_Handler(void);
void Default_Handler(void);

void NMI_Handler(void)        __attribute__((weak, alias("Default_Handler")));
void HardFault_Handler(void)  __attribute__((weak, alias("Default_Handler")));
void MemManage_Handler(void)  __attribute__((weak, alias("Default_Handler")));
void BusFault_Handler(void)   __attribute__((weak, alias("Default_Handler")));
void UsageFault_Handler(void) __attribute__((weak, alias("Default_Handler")));
void PendSV_Handler(void)     __attribute__((weak, alias("Default_Handler")));
void SysTick_Handler(void)    __attribute__((weak, alias("Default_Handler")));
void USART1_IRQHandler(void)  __attribute__((weak, alias("Default_Handler")));

/* Cortex-M7 Vector Table placed in .isr_vector section */
__attribute__((section(".isr_vector")))
const void* g_pfnVectors[] = {
    (const void*)(&_estack),
    (const void*)Reset_Handler,
    (const void*)NMI_Handler,
    (const void*)HardFault_Handler,
    (const void*)MemManage_Handler,
    (const void*)BusFault_Handler,
    (const void*)UsageFault_Handler,
    0, 0, 0, 0, /* Reserved */
    (const void*)Default_Handler, /* SVC_Handler */
    (const void*)Default_Handler, /* DebugMon_Handler */
    0, /* Reserved */
    (const void*)PendSV_Handler,  /* PendSV_Handler */
    (const void*)SysTick_Handler, /* SysTick_Handler (Index 15) */
    
    /* External NVIC Interrupts (IRQ 0 to IRQ 40) */
    (const void*)Default_Handler, /* IRQ 0  (Index 16) */
    (const void*)Default_Handler, /* IRQ 1  (Index 17) */
    (const void*)Default_Handler, /* IRQ 2  (Index 18) */
    (const void*)Default_Handler, /* IRQ 3  (Index 19) */
    (const void*)Default_Handler, /* IRQ 4  (Index 20) */
    (const void*)Default_Handler, /* IRQ 5  (Index 21) */
    (const void*)Default_Handler, /* IRQ 6  (Index 22) */
    (const void*)Default_Handler, /* IRQ 7  (Index 23) */
    (const void*)Default_Handler, /* IRQ 8  (Index 24) */
    (const void*)Default_Handler, /* IRQ 9  (Index 25) */
    (const void*)Default_Handler, /* IRQ 10 (Index 26) */
    (const void*)Default_Handler, /* IRQ 11 (Index 27) */
    (const void*)Default_Handler, /* IRQ 12 (Index 28) */
    (const void*)Default_Handler, /* IRQ 13 (Index 29) */
    (const void*)Default_Handler, /* IRQ 14 (Index 30) */
    (const void*)Default_Handler, /* IRQ 15 (Index 31) */
    (const void*)Default_Handler, /* IRQ 16 (Index 32) */
    (const void*)Default_Handler, /* IRQ 17 (Index 33) */
    (const void*)Default_Handler, /* IRQ 18 (Index 34) */
    (const void*)Default_Handler, /* IRQ 19 (Index 35) */
    (const void*)Default_Handler, /* IRQ 20 (Index 36) */
    (const void*)Default_Handler, /* IRQ 21 (Index 37) */
    (const void*)Default_Handler, /* IRQ 22 (Index 38) */
    (const void*)Default_Handler, /* IRQ 23 (Index 39) */
    (const void*)Default_Handler, /* IRQ 24 (Index 40) */
    (const void*)Default_Handler, /* IRQ 25 (Index 41) */
    (const void*)Default_Handler, /* IRQ 26 (Index 42) */
    (const void*)Default_Handler, /* IRQ 27 (Index 43) */
    (const void*)Default_Handler, /* IRQ 28 (Index 44) */
    (const void*)Default_Handler, /* IRQ 29 (Index 45) */
    (const void*)Default_Handler, /* IRQ 30 (Index 46) */
    (const void*)Default_Handler, /* IRQ 31 (Index 47) */
    (const void*)Default_Handler, /* IRQ 32 (Index 48) */
    (const void*)Default_Handler, /* IRQ 33 (Index 49) */
    (const void*)Default_Handler, /* IRQ 34 (Index 50) */
    (const void*)Default_Handler, /* IRQ 35 (Index 51) */
    (const void*)Default_Handler, /* IRQ 36 (Index 52) */
    (const void*)USART1_IRQHandler, /* IRQ 37 (Index 53 - USART1) */
    (const void*)Default_Handler, /* IRQ 38 (Index 54) */
    (const void*)Default_Handler, /* IRQ 39 (Index 55) */
    (const void*)Default_Handler  /* IRQ 40 (Index 56) */
};

void Reset_Handler(void) {
    uint32_t *pSrc = &_etext;
    uint32_t *pDest = &_sdata;

    /* Copy initialized data from FLASH to AXI_SRAM */
    while (pDest < &_edata) {
        *pDest++ = *pSrc++;
    }

    /* Zero out .bss section in AXI_SRAM */
    pDest = &_sbss;
    while (pDest < &_ebss) {
        *pDest++ = 0;
    }

    /* Jump to firmware entry point */
    main();

    /* Fallback infinite loop if main ever exits */
    while (1) {}
}

void Default_Handler(void) {
    while (1) {
        /* Trap unexpected exceptions */
    }
}
