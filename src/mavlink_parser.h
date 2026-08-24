/*
 * mavlink_parser.h — Zero-copy MAVLink v2 parser with DMA circular buffer
 * Target: STM32H753XI Cortex-M7
 *
 * Ingests ATTITUDE, LOCAL_POSITION_NED, GLOBAL_POSITION_INT from Pixhawk
 * via USART1 DMA. Parses directly from DMA buffer with L1 D-Cache coherency.
 */
#ifndef MAVLINK_PARSER_H
#define MAVLINK_PARSER_H

#include <stdint.h>

#define MAVLINK_MAX_PAYLOAD_LEN  255
#define MAVLINK_HEADER_LEN       10
#define MAVLINK_CHECKSUM_LEN     2
#define MAVLINK_DMA_BUFFER_SIZE  512
#define MAVLINK_MAX_PACKET_LEN   (MAVLINK_HEADER_LEN + MAVLINK_MAX_PAYLOAD_LEN + MAVLINK_CHECKSUM_LEN)

/* MAVLink v2 magic bytes */
#define MAVLINK_V1_MAGIC         0xFE
#define MAVLINK_V2_MAGIC         0xFD

/* Message IDs we care about */
#define MAVLINK_MSG_ID_ATTITUDE              30
#define MAVLINK_MSG_ID_LOCAL_POSITION_NED    32
#define MAVLINK_MSG_ID_GLOBAL_POSITION_INT   33

/* Simulation backdoor: Renode writes RX byte count here */
#ifdef SIMULATION_BUILD
/* Addresses in AXI SRAM gap between .data end (0x24000004) and .bss start (0x24000020) */
#define SIM_RX_COUNT_ADDR    0x24000010  /* 4 bytes: RX byte count */
#define SIM_FRAME_SYNC_ADDR  0x24000014  /* 4 bytes: nonzero triggers frame-sync */
#define SIM_FRAME_TS_ADDR    0x24000018  /* 4 bytes: timestamp value (microseconds) */
#define SIM_RADAR_TRIGGER_ADDR 0x2400001C  /* 4 bytes: nonzero re-arms radar target */
#endif

/* Component state */
typedef struct {
    uint8_t  dma_buffer[MAVLINK_DMA_BUFFER_SIZE] __attribute__((aligned(32)));
    uint32_t dma_last_read_pos;
    uint32_t dma_last_ndtr;
    uint32_t packets_parsed;
    uint32_t packets_dropped;
    uint32_t crc_errors;
} mavlink_parser_t;

/* Parsed attitude message (MAVLink ATTITUDE #30) */
typedef struct __attribute__((packed)) {
    uint32_t time_boot_ms;
    float    roll;       /* rad */
    float    pitch;      /* rad */
    float    yaw;        /* rad */
    float    rollspeed;  /* rad/s */
    float    pitchspeed; /* rad/s */
    float    yawspeed;   /* rad/s */
} mavlink_attitude_t;

/* Parsed local position (MAVLink LOCAL_POSITION_NED #32) */
typedef struct __attribute__((packed)) {
    uint32_t time_boot_ms;
    float    x;  /* meters, NED */
    float    y;
    float    z;
    float    vx; /* m/s, NED */
    float    vy;
    float    vz;
} mavlink_local_position_ned_t;

/* Parsed global position (MAVLink GLOBAL_POSITION_INT #33) */
typedef struct __attribute__((packed)) {
    uint32_t time_boot_ms;
    int32_t  lat;        /* degrees * 1e7 */
    int32_t  lon;        /* degrees * 1e7 */
    int32_t  alt;        /* mm above MSL */
    int32_t  relative_alt; /* mm above home */
    int16_t  vx;         /* cm/s */
    int16_t  vy;
    int16_t  vz;
    uint16_t hdg;        /* cdegrees */
} mavlink_global_position_int_t;

/* Unified kinematic state output from parser */
typedef struct {
    uint32_t timestamp_us;     /* microsecond timestamp from HW timer */
    uint32_t time_boot_ms;     /* from MAVLink message */

    /* Position (NED frame, meters) */
    float pos_n, pos_e, pos_d;

    /* Velocity (NED frame, m/s) */
    float vel_n, vel_e, vel_d;

    /* Attitude (rad) */
    float roll, pitch, yaw;

    /* Angular rates (rad/s) */
    float rollspeed, pitchspeed, yawspeed;

    /* Global position (WGS84) */
    double latitude;   /* degrees */
    double longitude;  /* degrees */
    float  altitude;   /* meters above MSL */

    /* Validity flags */
    uint8_t has_attitude     : 1;
    uint8_t has_local_pos    : 1;
    uint8_t has_global_pos   : 1;
    uint8_t has_velocity     : 1;
} kinematic_state_t;

void mavlink_parser_init(mavlink_parser_t *parser);
int  mavlink_parser_poll(mavlink_parser_t *parser, kinematic_state_t *state);
void mavlink_parser_dma_isr(mavlink_parser_t *parser);

#endif /* MAVLINK_PARSER_H */
