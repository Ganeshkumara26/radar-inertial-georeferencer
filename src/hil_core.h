#ifndef HIL_CORE_H
#define HIL_CORE_H
#include "bench_core.h"
#ifdef __cplusplus
extern "C" {
#endif
void hil_init(void);
void hil_feed(uint8_t byte, bench_now_fn now, bench_emit_fn emit);
#ifdef __cplusplus
}
#endif
#endif
