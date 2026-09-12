/*
 * main.c — Bare-metal Cortex-M7 timing harness for the shipped ekf_update_radar().
 * Runs under QEMU (mps2-an500, -cpu cortex-m7), uses the DWT cycle counter
 * (a real Cortex-M7 peripheral) for timing, and reports results via semihosting.
 *
 * Compiled twice: once against ekf_simplified.c (shipped, non-Joseph form)
 * and once against ekf_joseph.c (Joseph-form patch), producing two ELF
 * images whose cycle counts are directly comparable since everything else
 * (compiler, flags, CPU model, input data) is held identical.
 */
#include <stdint.h>
#include <math.h>
#include "ekf.h"

/* DWT registers (real Cortex-M7 peripheral, memory-mapped, modeled by QEMU) */
#define DWT_CTRL   (*(volatile uint32_t*)0xE0001000)
#define DWT_CYCCNT (*(volatile uint32_t*)0xE0001004)
#define DEMCR      (*(volatile uint32_t*)0xE000EDFC)

static void dwt_init(void) {
    DEMCR |= (1 << 24);      /* TRCENA */
    DWT_CYCCNT = 0;
    DWT_CTRL |= 1;           /* CYCCNTENA */
}

/* ARM semihosting: SYS_WRITE0 writes a null-terminated string to the host console. */
static void semihost_write0(const char *s) {
    register uint32_t r0 __asm__("r0") = 0x04; /* SYS_WRITE0 */
    register const char *r1 __asm__("r1") = s;
    __asm__ volatile("bkpt 0xAB" : : "r"(r0), "r"(r1));
}

static void print_u32(const char *label, uint32_t v) {
    char buf[64];
    char digits[12];
    int n = 0;
    if (v == 0) digits[n++] = '0';
    while (v > 0) { digits[n++] = '0' + (v % 10); v /= 10; }
    int i = 0;
    while (label[i]) { buf[i] = label[i]; i++; }
    for (int j = n - 1; j >= 0; j--) buf[i++] = digits[j];
    buf[i++] = '\n';
    buf[i] = '\0';
    semihost_write0(buf);
}

int main(void) {
    dwt_init();
    semihost_write0("STAGE: dwt_init done\n");

    ekf_config_t cfg = { .dt = 0.01f, .process_noise = 0.1f, .meas_noise_range = 1.0f,
                          .meas_noise_angle = 0.01f, .meas_noise_doppler = 0.5f };
    ekf_state_t ekf;
    ekf_init(&ekf, &cfg);
    semihost_write0("STAGE: ekf_init done\n");
    ekf.x[0] = 2.0f; ekf.x[1] = 0.5f; ekf.x[2] = 8.0f;
    ekf.x[3] = 3.0f; ekf.x[4] = 0.0f; ekf.x[5] = 0.5f;

    ekf_predict(&ekf, 0.01f);
    semihost_write0("STAGE: ekf_predict done\n");

    float rel_n = 2.97f, rel_e = 0.0f, rel_d = 9.995f;
    float r = sqrtf(rel_n*rel_n + rel_e*rel_e + rel_d*rel_d);
    float rg = sqrtf(rel_n*rel_n + rel_e*rel_e);
    float az = atan2f(rel_e, rel_n), el = atan2f(-rel_d, rg);
    float doppler = -(rel_n*(-3.0f) + rel_e*0 + rel_d*(-0.5f)) / r;
    float innov[4];
    semihost_write0("STAGE: precompute done\n");

    /* Warm up (first call may include one-time branch prediction/cache effects) */
    ekf_update_radar(&ekf, r, az, el, doppler, innov);
    semihost_write0("STAGE: warmup update done\n");

    /* Re-initialize before each timed call so every call executes the
     * IDENTICAL full code path (same P, same x, same measurement) in
     * both the simplified and Joseph-form builds. This is necessary
     * because letting the state evolve call-to-call causes the two
     * builds to diverge differently (per Defect 2/3 in the paper),
     * which would make the instruction counts not a fair comparison
     * of the covariance-update arithmetic itself. */
    #define N_TRIALS 100
    uint32_t start = DWT_CYCCNT;
    for (int i = 0; i < N_TRIALS; i++) {
        ekf_init(&ekf, &cfg);
        ekf.x[0] = 2.0f; ekf.x[1] = 0.5f; ekf.x[2] = 8.0f;
        ekf.x[3] = 3.0f; ekf.x[4] = 0.0f; ekf.x[5] = 0.5f;
        ekf_predict(&ekf, 0.01f);
        ekf_update_radar(&ekf, r, az, el, doppler, innov);
    }
    uint32_t end = DWT_CYCCNT;
    uint32_t total_cycles = end - start;
    uint32_t avg_cycles = total_cycles / N_TRIALS;

    semihost_write0("=== EKF update_radar timing (Cortex-M7, QEMU mps2-an500) ===\n");
    print_u32("total_cycles_100_calls=", total_cycles);
    print_u32("avg_cycles_per_call=", avg_cycles);

    /* Exit semihosting cleanly (ADP_Stopped_ApplicationExit) */
    register uint32_t r0 __asm__("r0") = 0x18;
    register uint32_t r1 __asm__("r1") = 0x20026;
    __asm__ volatile("bkpt 0xAB" : : "r"(r0), "r"(r1));

    while (1) {}
    return 0;
}
