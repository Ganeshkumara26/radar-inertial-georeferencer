// Thin Arduino (STM32duino) wrapper. All logic lives in bench_core.c / hil_core.c.
// Serial = ST-LINK virtual COM port. Commands (single char): I info, B scenario, M micro,
// S stack report, G gpio kernel (scope), H enter HIL mode (until reset).
#include <Arduino.h>
#include <string.h>
#include <stdint.h>
extern "C" {
#include "bench_core.h"
#include "hil_core.h"
}
extern "C" uint32_t _ebss __attribute__((weak));

#ifndef BENCH_BAUD
#define BENCH_BAUD 460800
#endif
#ifdef BENCH_HSI_CLOCK  /* 48 MHz from internal HSI (8 MHz /2 x12): independent of the ST-LINK MCO clock */
extern "C" void SystemClock_Config(void) {
    RCC_OscInitTypeDef o = {}; RCC_ClkInitTypeDef c = {};
    o.OscillatorType = RCC_OSCILLATORTYPE_HSI; o.HSIState = RCC_HSI_ON; o.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    o.PLL.PLLState = RCC_PLL_ON; o.PLL.PLLSource = RCC_PLLSOURCE_HSI; o.PLL.PLLMUL = RCC_PLL_MUL12; o.PLL.PREDIV = RCC_PREDIV_DIV2;
    HAL_RCC_OscConfig(&o);
    c.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1; c.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    c.AHBCLKDivider = RCC_SYSCLK_DIV1; c.APB1CLKDivider = RCC_HCLK_DIV1;
    HAL_RCC_ClockConfig(&c, FLASH_LATENCY_1);
}
#endif
static uint32_t now_us() { return micros(); }
static void emit(const char *s) { Serial.write((const uint8_t *)s, strlen(s)); }
static void kv(const char *k, unsigned long v) { char b[64]; snprintf(b, sizeof b, "#INFO,%s,%lu\n", k, v); emit(b); }

static bool hil_mode = false;
static uint32_t sp0, paint_lo, paint_hi;

static void paint_stack() {
    uint32_t sp; __asm__ volatile("mov %0, sp" : "=r"(sp));
    sp0 = sp; paint_hi = sp - 256;
    uint32_t floor_ = (uint32_t)(uintptr_t)&_ebss + 1024;                 // keep clear of heap
    if (&_ebss == nullptr || paint_hi < floor_ + 512) { paint_lo = paint_hi = 0; return; }
    paint_lo = (paint_hi - floor_ > 6144) ? paint_hi - 6144 : floor_;
    memset((void *)(uintptr_t)paint_lo, 0xA5, paint_hi - paint_lo);
}
static void stack_report() {
    char b[96];
    if (!paint_hi) { emit("#STACK,unavailable\n"); return; }
    uint32_t a = paint_lo;
    while (a < paint_hi && *(volatile uint8_t *)(uintptr_t)a == 0xA5) a++;
    uint32_t used = sp0 - a;
    snprintf(b, sizeof b, "#STACK,used_bytes=%lu,painted_bytes=%lu,overflowed_paint=%d\n",
             (unsigned long)used, (unsigned long)(paint_hi - paint_lo), a == paint_lo);
    emit(b);
}
static void info() {
    emit("#INFO_BEGIN\n");
    kv("core_hz", (unsigned long)SystemCoreClock);
    kv("devid", (unsigned long)HAL_GetDEVID());
    kv("revid", (unsigned long)HAL_GetREVID());
#ifdef FLASHSIZE_BASE
    kv("flash_kb", (unsigned long)(*(volatile uint16_t *)FLASHSIZE_BASE));
#endif
#ifdef __ARM_ARCH
    kv("arm_arch", (unsigned long)__ARM_ARCH);
#endif
#ifdef __ARM_FP
    kv("hw_fpu", 1);
#else
    kv("hw_fpu", 0);
#endif
    kv("ebss_addr", (unsigned long)(uintptr_t)&_ebss);
    kv("sp_at_setup", (unsigned long)sp0);
    char b[96]; snprintf(b, sizeof b, "#INFO,gcc,%s\n#INFO,built,%s %s\n", __VERSION__, __DATE__, __TIME__); emit(b);
    emit("#INFO_END\n");
}
#ifdef LED_BUILTIN
static void g_hi() { digitalWrite(D2, HIGH); }
static void g_lo() { digitalWrite(D2, LOW); }
static void g_gap() { delay(2); }
#endif

void setup() {
    paint_stack();
    Serial.begin(BENCH_BAUD);
    delay(300);
    emit("#READY\n");
}
void loop() {
    while (Serial.available()) {
        int c = Serial.read();
        if (hil_mode) { hil_feed((uint8_t)c, now_us, emit); continue; }
        switch (c) {
            case 'I': info(); break;
            case 'B': bench_scenario(now_us, emit); break;
            case 'M': bench_micro(now_us, emit); break;
            case 'S': stack_report(); break;
            case 'P': bench_pipeline(now_us, emit); break;
#ifdef LED_BUILTIN
            case 'G': pinMode(D2, OUTPUT); emit("#GPIO_BEGIN (D2)\n"); bench_gpio_kernel(g_hi, g_lo, g_gap); emit("#GPIO_END\n"); break;
#endif
            case 'H': hil_init(); hil_mode = true; emit("#HIL_READY\n"); break;
            default: break;
        }
    }
}
