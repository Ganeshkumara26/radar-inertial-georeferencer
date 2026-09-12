#include <stdint.h>

extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _estack;
extern int main(void);

#define CPACR (*(volatile uint32_t*)0xE000ED88)

void Reset_Handler(void) {
    uint32_t *src = &_sidata, *dst = &_sdata;
    while (dst < &_edata) *dst++ = *src++;
    dst = &_sbss;
    while (dst < &_ebss) *dst++ = 0;

    /* Enable FPU (CP10, CP11 full access) — required before any hardware
     * floating-point instruction executes on real Cortex-M7 silicon;
     * without this, the first float operation traps into UsageFault. */
    CPACR |= (0xF << 20);
    __asm__ volatile("dsb");
    __asm__ volatile("isb");

    main();
    while (1) { __asm__("wfi"); }
}

static void semihost_write0_startup(const char *s) {
    register uint32_t r0 __asm__("r0") = 0x04;
    register const char *r1 __asm__("r1") = s;
    __asm__ volatile("bkpt 0xAB" : : "r"(r0), "r"(r1));
}

void Default_Handler(void) {
    semihost_write0_startup("FAULT: Default_Handler entered (NMI/Hard/Mem/Bus/UsageFault)\n");
    while (1) {}
}

/* Minimal vector table: initial SP, Reset, and enough stub entries
 * to satisfy the Cortex-M7 vector table layout QEMU expects. */
__attribute__((section(".isr_vector")))
void (* const vector_table[])(void) = {
    (void(*)(void))&_estack,
    Reset_Handler,
    Default_Handler, /* NMI */
    Default_Handler, /* HardFault */
    Default_Handler, /* MemManage */
    Default_Handler, /* BusFault */
    Default_Handler, /* UsageFault */
};
