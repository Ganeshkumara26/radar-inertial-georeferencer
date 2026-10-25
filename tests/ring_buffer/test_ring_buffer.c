#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "ring_buffer.h"

static void test_ring_buffer_basic(void) {
    printf("test_ring_buffer_basic... ");
    ring_buffer_t rb;
    ring_buffer_init(&rb);

    kinematic_state_t state;
    state.timestamp_us = 1000;
    state.has_attitude = 1;

    int ret = ring_buffer_push(&rb, &state);
    assert(ret == 0);
    assert(ring_buffer_count(&rb) == 1);

    kinematic_state_t out;
    ret = ring_buffer_pop(&rb, &out);
    assert(ret == 0);
    assert(out.timestamp_us == 1000);
    assert(out.has_attitude == 1);
    assert(ring_buffer_is_empty(&rb));
    printf("PASS\n");
}

static void test_ring_buffer_overflow(void) {
    printf("test_ring_buffer_overflow... ");
    ring_buffer_t rb;
    ring_buffer_init(&rb);

    kinematic_state_t state;
    state.timestamp_us = 0;
    state.has_attitude = 1;

    for (int i = 0; i < RING_BUFFER_SIZE - 1; i++) {
        state.timestamp_us = (uint32_t)i;
        ring_buffer_push(&rb, &state);
    }

    int ret = ring_buffer_push(&rb, &state);
    assert(ret == -1);
    assert(rb.overflow_count == 1);
    printf("PASS\n");
}

int main(void) {
    printf("Running Ring Buffer tests on host\n");
    printf("==================================\n");
    test_ring_buffer_basic();
    test_ring_buffer_overflow();
    printf("\nAll tests PASSED\n");
    return 0;
}
