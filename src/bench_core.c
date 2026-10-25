#include "bench_core.h"
#include "ekf.h"
#include "geo_transform.h"
#include "ring_buffer.h"
#include "stimulus.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

static uint32_t f2u(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static float u2f(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }

uint16_t crc_x25(const uint8_t *d, size_t n, uint8_t extra) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i <= n; i++) {
        uint8_t b = (i < n) ? d[i] : extra;
        uint8_t t = b ^ (uint8_t)(crc & 0xFF);
        t ^= (uint8_t)(t << 4);
        crc = (uint16_t)((crc >> 8) ^ ((uint16_t)t << 8) ^ ((uint16_t)t << 3) ^ ((uint16_t)t >> 4));
    }
    return crc;
}

void ekf_seed(ekf_state_t *e) {
    ekf_config_t cfg = { .dt = 0.01f, .process_noise = 0.1f, .meas_noise_range = 1.0f,
                         .meas_noise_angle = 0.01f, .meas_noise_doppler = 0.5f };
    ekf_init(e, &cfg);
    e->x[0] = 2.0f; e->x[1] = 0.5f; e->x[2] = 8.0f;
    e->x[3] = 3.0f; e->x[4] = 0.0f; e->x[5] = 0.5f;
}

void bench_scenario(bench_now_fn now, bench_emit_fn emit) {
    static ekf_state_t ekf;
    char b[160];
    ekf_seed(&ekf);
    emit("#SCEN_BEGIN\n");
    for (int i = 0; i < STIM_N; i++) {
        uint32_t t0 = now();
        ekf_predict(&ekf, 0.01f);
        uint32_t t1 = now();
        float innov[4];
        int rc = ekf_update_radar(&ekf, u2f(STIM[i][0]), u2f(STIM[i][1]),
                                  u2f(STIM[i][2]), u2f(STIM[i][3]), innov);
        uint32_t t2 = now();
        snprintf(b, sizeof b, "S,%d,%lu,%lu,%d,%08lx,%08lx,%08lx,%08lx,%08lx,%08lx\n",
                 i, (unsigned long)(t1 - t0), (unsigned long)(t2 - t1), rc,
                 (unsigned long)f2u(ekf.x[0]), (unsigned long)f2u(ekf.x[1]), (unsigned long)f2u(ekf.x[2]),
                 (unsigned long)f2u(ekf.x[3]), (unsigned long)f2u(ekf.x[4]), (unsigned long)f2u(ekf.x[5]));
        emit(b);
    }
    snprintf(b, sizeof b, "#SCEN_END,predict=%lu,update=%lu,reject=%lu\n",
             (unsigned long)ekf.predict_count, (unsigned long)ekf.update_count, (unsigned long)ekf.reject_count);
    emit(b);
}

#define TIME_LOOP(NAME, N, BODY) do { \
    uint32_t t0 = now(); \
    for (int k = 0; k < (N); k++) { BODY; } \
    uint32_t t1 = now(); \
    snprintf(b, sizeof b, "M,%s,%d,%lu\n", NAME, (N), (unsigned long)(t1 - t0)); emit(b); } while (0)

void bench_micro(bench_now_fn now, bench_emit_fn emit) {
    char b[120];
    volatile float fa = 1.2345f, fb = 2.3456f, fr = 0.0f;
    volatile double da = 1.2345, db = 2.3456, dr = 0.0;
    static uint8_t frame[40];
#ifndef BENCH_NO_RING
    static ring_buffer_t rb;
    static kinematic_state_t ks, ko;
#endif
    static ekf_state_t e;
    radar_target_polar_t pol = { 100.0f, 0.3f, 0.1f, 2.0f, 20.0f, 0 };
    quaternion_t q; geo_reference_t ref; target_wgs84_t w;

    emit("#MICRO_BEGIN\n");
    snprintf(b, sizeof b, "M,sizeof_ekf_state,%u,0\nM,sizeof_kinematic_state,%u,0\n",
             (unsigned)sizeof(ekf_state_t), (unsigned)sizeof(kinematic_state_t)); emit(b);
    TIME_LOOP("empty_loop", 2000, fr = fr);
    TIME_LOOP("f32_mul", 2000, fr = fa * fb);
    TIME_LOOP("f32_add", 2000, fr = fa + fb);
    TIME_LOOP("f32_div", 2000, fr = fa / fb);
    TIME_LOOP("f32_sqrtf", 500, fr = sqrtf(fa));
    TIME_LOOP("f32_atan2f", 200, fr = atan2f(fa, fb));
    TIME_LOOP("f64_mul", 2000, dr = da * db);
    for (int i = 0; i < 40; i++) frame[i] = (uint8_t)(i * 7 + 1);
    TIME_LOOP("crc_x25_40B", 500, fr = (float)crc_x25(frame, 40, 39));
#ifndef BENCH_NO_RING
    ring_buffer_init(&rb);
    memset(&ks, 0, sizeof ks);
    TIME_LOOP("ring_push_pop", 500, { ring_buffer_push(&rb, &ks); ring_buffer_pop(&rb, &ko); });
#endif
    geo_init_reference(&ref, 13.0827, 80.2707, 0.0f);
    euler_to_quaternion(0.1f, 0.05f, 1.0f, &q);
    TIME_LOOP("geo_transform_target", 50, geo_transform_target(&pol, &q, 1.0f, &ref, &w));
    ekf_seed(&e);
    TIME_LOOP("ekf_predict", 200, ekf_predict(&e, 0.01f));
    emit("#MICRO_END\n");
}

/* GPIO kernel: hi() before / lo() after each ekf_update, so a scope or logic analyser gives an
 * independent measurement of the update time (compare with the micros()-based numbers). */
void bench_gpio_kernel(void (*hi)(void), void (*lo)(void), void (*gap)(void)) {
    static ekf_state_t ekf;
    float innov[4];
    ekf_seed(&ekf);
    for (int i = 0; i < STIM_N; i++) {
        ekf_predict(&ekf, 0.01f);
        hi();
        ekf_update_radar(&ekf, u2f(STIM[i][0]), u2f(STIM[i][1]), u2f(STIM[i][2]), u2f(STIM[i][3]), innov);
        lo();
        gap();
    }
}
