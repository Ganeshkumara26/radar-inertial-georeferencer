#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include "mavlink_parser.h"
#include "platform_stub.h"

static void assert_float_eq(float a, float b, float tol, const char *msg) {
    if (fabsf(a - b) > tol) {
        printf("FAIL: %s (expected %.6f, got %.6f)\n", msg, b, a);
        exit(1);
    }
}

static void test_parser_init(void) {
    printf("test_parser_init... ");
    mavlink_parser_t parser;
    mavlink_parser_init(&parser);
    assert(parser.packets_parsed == 0);
    assert(parser.packets_dropped == 0);
    assert(parser.crc_errors == 0);
    printf("PASS\n");
}

static void test_parser_attitude(void) {
    printf("test_parser_attitude... ");
    mavlink_parser_t parser;
    mavlink_parser_init(&parser);

    uint8_t packet[] = {
        0xFD,       // MAVLink v2 magic
        32,         // length
        0x00,       // incompatible flags
        0x00,       // compatible flags
        0x1E,       // system ID
        0x01,       // component ID
        0x00,       // unused/reserved
        30,         // message ID (ATTITUDE) at offset 7 per original parser
        0x00,       // sequence
        0x00,       // extra header byte
        /* payload: 32 bytes starting at offset 10 */
        0x00, 0x00, 0x00, 0x00,  // time_boot_ms
        0x00, 0x00, 0x80, 0x3F,  // roll = 1.0
        0x00, 0x00, 0x00, 0x00,  // pitch = 0.0
        0x00, 0x00, 0x00, 0x00,  // yaw = 0.0
        0x00, 0x00, 0x00, 0x00,  // rollspeed
        0x00, 0x00, 0x00, 0x00,  // pitchspeed
        0x00, 0x00, 0x00, 0x00,  // yawspeed
        0x00, 0x00, 0x00, 0x00,  // padding
        0x12, 0x34  // CRC (dummy)
    };

    platform_set_dma_stub(sizeof(packet), packet);
    parser.dma_last_read_pos = 0;

    kinematic_state_t state;
    int ret = mavlink_parser_poll(&parser, &state);
    assert(ret == 1);
    assert(state.has_attitude == 1);
    assert_float_eq(state.roll, 1.0f, 1e-5f, "roll");
    printf("PASS\n");
}

int main(void) {
    printf("Running MAVLink Parser tests on host\n");
    printf("=====================================\n");
    test_parser_init();
    test_parser_attitude();
    printf("\nAll tests PASSED\n");
    return 0;
}
