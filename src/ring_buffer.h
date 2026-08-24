/*
 * ring_buffer.h — Lock-free single-producer/single-consumer ring buffer
 * Target: STM32H753XI Cortex-M7
 *
 * Thread-safe for: one writer (DMA ISR or parser), one consumer (EKF pipeline).
 * No locks needed — uses atomic index updates with memory barriers.
 * Stores timestamped kinematic state history for back-projection.
 */
#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include <stdint.h>
#include "mavlink_parser.h"

#define RING_BUFFER_SIZE  64  /* Must be power of 2 for fast modulo */
#define RING_BUFFER_MASK  (RING_BUFFER_SIZE - 1)

typedef struct {
    kinematic_state_t entries[RING_BUFFER_SIZE];
    volatile uint32_t head;  /* Write index (producer) */
    volatile uint32_t tail;  /* Read index (consumer) */
    uint32_t overflow_count;
} ring_buffer_t;

void ring_buffer_init(ring_buffer_t *rb);
int  ring_buffer_push(ring_buffer_t *rb, const kinematic_state_t *state);
int  ring_buffer_pop(ring_buffer_t *rb, kinematic_state_t *state);
int  ring_buffer_peek(const ring_buffer_t *rb, kinematic_state_t *state);
uint32_t ring_buffer_count(const ring_buffer_t *rb);
int  ring_buffer_is_full(const ring_buffer_t *rb);
int  ring_buffer_is_empty(const ring_buffer_t *rb);

#endif /* RING_BUFFER_H */
