/* End-to-end on-board pipeline benchmark (no UART in the timed path):
 *  P: MAVLink bytes -> byte-wise parser + CRC -> EKF predict/update -> geo transform -> reply line.
 *     Latency = first byte of the RADAR frame fed -> reply ready (parser+CRC+EKF+geo). Wire time is NOT included.
 *  ALIGN: attitude interpolation vs zero-order-hold at radar timestamps (same linear formula as the repo's
 *     attitude-interpolation stage), against an analytic yaw trajectory. */
#include "bench_core.h"
#include "hil_core.h"
#include "stimulus.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

static bench_now_fn g_now; static uint32_t g_t_reply; static int g_replies; static char g_last[150];
static void cap(const char *s) { if (s[0] == 'R') { g_t_reply = g_now(); g_replies++; strncpy(g_last, s, sizeof g_last - 1); g_last[sizeof g_last - 1] = 0; } }

static size_t build(uint8_t *o, uint8_t seq, uint32_t id, const uint8_t *pl, uint8_t len, uint8_t extra) {
    o[0] = 0xFD; o[1] = len; o[2] = 0; o[3] = 0; o[4] = seq; o[5] = 1; o[6] = 1;
    o[7] = (uint8_t)id; o[8] = (uint8_t)(id >> 8); o[9] = (uint8_t)(id >> 16);
    memcpy(o + 10, pl, len);
    uint16_t c = crc_x25(o + 1, 9u + len, extra);
    o[10 + len] = (uint8_t)c; o[11 + len] = (uint8_t)(c >> 8);
    return 12u + len;
}
static void feed(const uint8_t *f, size_t n) { for (size_t i = 0; i < n; i++) hil_feed(f[i], g_now, cap); }

#define PI_F 3.14159265f
static float yaw_true(float t) { return 0.5f * sinf(PI_F * t); }               /* 0.25 Hz... rad */
static float yaw_rate(float t) { return 0.5f * PI_F * cosf(PI_F * t); }
static const int16_t JIT[8] = { 0, 300, -200, 500, 100, -400, 250, -100 };      /* attitude timestamp jitter, us */
static uint32_t ts_k(uint32_t k) { return (uint32_t)(10000 * (int32_t)k + JIT[k & 7]) + 10000; }

static void align_test(bench_now_fn now, bench_emit_fn emit) {
    char b[200];
    float max_i = 0, max_z = 0, max_age = 0, max_eq = 0; int n_eq = 0, n = 0;
    uint32_t tt0 = now();
    for (uint32_t j = 1; j <= 150; j++) {
        uint32_t tr = 20000u * j + 3700u + 10000u;     /* radar event, 50 Hz, offset */
        uint32_t k = (tr - 10000u) / 10000u;           /* candidate bracket */
        while (ts_k(k) > tr) k--;
        while (ts_k(k + 1) <= tr) k++;
        uint32_t t0 = ts_k(k), t1 = ts_k(k + 1);
        float y0 = yaw_true(t0 * 1e-6f), y1 = yaw_true(t1 * 1e-6f);
        float a = (float)(tr - t0) / (float)(t1 - t0);
        float yi = y0 + a * (y1 - y0);
        float tru = yaw_true(tr * 1e-6f), rt = yaw_rate(tr * 1e-6f);
        float ei = fabsf(yi - tru), ez = fabsf(y0 - tru), age = (float)(tr - t0);
        if (ei > max_i) max_i = ei; if (ez > max_z) max_z = ez; if (age > max_age) max_age = age;
        if (fabsf(rt) > 0.2f) { float eq = ei / fabsf(rt) * 1e6f; if (eq > max_eq) max_eq = eq; n_eq++; }
        n++;
    }
    uint32_t tt1 = now();
    snprintf(b, sizeof b, "#ALIGN,events=%d,interp_max_err_mrad=%d,zoh_max_err_mrad=%d,zoh_max_age_us=%d,"
             "interp_equiv_time_err_max_us=%d,eq_samples=%d,loop_us_per_event=%d\n", n,
             (int)(max_i * 1000.0f + 0.5f), (int)(max_z * 1000.0f + 0.5f), (int)max_age,
             (int)(max_eq + 0.5f), n_eq, (int)((tt1 - tt0) / (uint32_t)n)); emit(b);
}

void bench_pipeline(bench_now_fn now, bench_emit_fn emit) {
    char b[200]; uint8_t fa[40], fr[32], pa[28], pr[20];
    g_now = now; hil_init(); g_replies = 0;
    emit("#PIPE_BEGIN\n");
    for (int i = 0; i < STIM_N; i++) {
        memset(pa, 0, sizeof pa);
        uint32_t tms = (uint32_t)i * 10u; float roll = 0.1f, pitch = 0.05f, yaw = 1.0f;
        memcpy(pa, &tms, 4); memcpy(pa + 4, &roll, 4); memcpy(pa + 8, &pitch, 4); memcpy(pa + 12, &yaw, 4);
        uint32_t seq = (uint32_t)i; memcpy(pr, &seq, 4);
        for (int k = 0; k < 4; k++) { uint32_t u = STIM[i][k]; memcpy(pr + 4 + 4 * k, &u, 4); }
        size_t na = build(fa, (uint8_t)i, 30, pa, 28, 39), nr = build(fr, (uint8_t)i, 0x4001, pr, 20, 0x5A);
        uint32_t a0 = now(); feed(fa, na); uint32_t a1 = now();
        int before = g_replies; uint32_t t0 = now(); feed(fr, nr);
        uint32_t lat = g_t_reply - t0;
        if (g_replies != before + 1) lat = 0;
        snprintf(b, sizeof b, "P,%d,%lu,%lu,%s", i, (unsigned long)(a1 - a0), (unsigned long)lat, g_last + 2);
        emit(b);
    }
    /* sustained parse capacity for attitude frames (CRC + parse + store), no EKF */
    uint32_t t0 = now();
    for (int k = 0; k < 500; k++) feed(fa, 40);
    uint32_t t1 = now();
    snprintf(b, sizeof b, "#PIPE_END,att_frames=500,att_total_us=%lu,att_frame_bytes=40,radar_frame_bytes=32\n", (unsigned long)(t1 - t0));
    emit(b);
    align_test(now, emit);
    emit("#ALIGN_END\n");
}
