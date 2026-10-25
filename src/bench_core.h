#ifndef BENCH_CORE_H
#define BENCH_CORE_H
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef uint32_t (*bench_now_fn)(void);          /* microsecond clock */
typedef void (*bench_emit_fn)(const char *line); /* line output */
uint16_t crc_x25(const uint8_t *d, size_t n, uint8_t crc_extra);
void bench_scenario(bench_now_fn now, bench_emit_fn emit); /* 200-step EKF run, bit-exact states */
void bench_micro(bench_now_fn now, bench_emit_fn emit);    /* per-op and per-module timings */
void bench_pipeline(bench_now_fn now, bench_emit_fn emit); /* parser+EKF+geo latency, attitude alignment */
void bench_gpio_kernel(void (*hi)(void), void (*lo)(void), void (*gap)(void));
#ifdef __cplusplus
}
#endif
#endif
