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
    (const void*)Default_Handler, /* PendSV_Handler */
    (const void*)Default_Handler  /* SysTick_Handler */
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
