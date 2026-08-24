/*
 * ring_buffer.c — Lock-free ring buffer implementation
 * Target: STM32H753XI Cortex-M7
 *
 * Single-producer (parser ISR) / single-consumer (EKF main loop).
 * Uses DMB (Data Memory Barrier) to ensure ordering on Cortex-M7.
 * Power-of-2 size allows bitwise AND for fast modulo.
 */
#include "ring_buffer.h"

void ring_buffer_init(ring_buffer_t *rb) {
    rb->head = 0;
    rb->tail = 0;
    rb->overflow_count = 0;

    for (int i = 0; i < RING_BUFFER_SIZE; i++) {
        rb->entries[i].timestamp_us = 0;
        rb->entries[i].has_attitude = 0;
        rb->entries[i].has_local_pos = 0;
        rb->entries[i].has_global_pos = 0;
        rb->entries[i].has_velocity = 0;
    }
}

int ring_buffer_push(ring_buffer_t *rb, const kinematic_state_t *state) {
    uint32_t next_head = (rb->head + 1) & RING_BUFFER_MASK;

    if (next_head == rb->tail) {
        rb->overflow_count++;
        return -1;  /* Buffer full */
    }

    /* Copy state into buffer */
    rb->entries[rb->head] = *state;

    /* Memory barrier: ensure write completes before updating head */
    __asm__ volatile ("dmb" ::: "memory");

    rb->head = next_head;
    return 0;
}

int ring_buffer_pop(ring_buffer_t *rb, kinematic_state_t *state) {
    if (rb->head == rb->tail) {
        return -1;  /* Buffer empty */
    }

    /* Memory barrier: ensure we read valid data */
    __asm__ volatile ("dmb" ::: "memory");

    *state = rb->entries[rb->tail];
    rb->tail = (rb->tail + 1) & RING_BUFFER_MASK;
    return 0;
}

int ring_buffer_peek(const ring_buffer_t *rb, kinematic_state_t *state) {
    if (rb->head == rb->tail) {
        return -1;  /* Buffer empty */
    }

    __asm__ volatile ("dmb" ::: "memory");
    *state = rb->entries[rb->tail];
    return 0;
}

uint32_t ring_buffer_count(const ring_buffer_t *rb) {
    return (rb->head - rb->tail) & RING_BUFFER_MASK;
}

int ring_buffer_is_full(const ring_buffer_t *rb) {
    return ((rb->head + 1) & RING_BUFFER_MASK) == rb->tail;
}

int ring_buffer_is_empty(const ring_buffer_t *rb) {
    return rb->head == rb->tail;
}
