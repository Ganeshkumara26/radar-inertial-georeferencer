/*
 * output_stream.h — Deterministic binary output stream
 * Target: STM32H753XI Cortex-M7
 *
 * Packages georeferenced target metadata into clean binary frames.
 * Streams over SPI1 (DMA) or USART1 (polled) to ground laptop.
 * Frame format designed for minimal overhead and deterministic parsing.
 */
#ifndef OUTPUT_STREAM_H
#define OUTPUT_STREAM_H

#include <stdint.h>
#include "geo_transform.h"

#define OUTPUT_MAX_TARGETS   16
#define OUTPUT_MAGIC         0xED  /* EDP output frame magic */
#define OUTPUT_VERSION       0x01
#define OUTPUT_HEADER_SIZE   14
#define OUTPUT_TARGET_SIZE   32
#define OUTPUT_MAX_FRAME_SIZE (OUTPUT_HEADER_SIZE + OUTPUT_MAX_TARGETS * OUTPUT_TARGET_SIZE + 4)

/* Output frame header */
typedef struct __attribute__((packed)) {
    uint8_t  magic;          /* 0xED */
    uint8_t  version;        /* 0x01 */
    uint16_t length;         /* Total frame length including header and CRC */
    uint32_t sequence;       /* Monotonic frame sequence number */
    uint32_t timestamp_us;   /* Frame creation timestamp (microseconds) */
    uint8_t  target_count;   /* Number of targets in this frame */
    uint8_t  status;         /* System status flags */
} output_header_t;

/* Per-target output record */
typedef struct __attribute__((packed)) {
    double   latitude;       /* WGS84 degrees */
    double   longitude;      /* WGS84 degrees */
    float    altitude;       /* meters above MSL */
    float    vel_east;       /* m/s */
    float    vel_north;      /* m/s */
    float    vel_up;         /* m/s */
    float    snr;            /* dB */
    uint32_t timestamp_us;   /* Detection timestamp */
} output_target_record_t;

/* Output stream state */
typedef struct {
    uint8_t  frame_buffer[OUTPUT_MAX_FRAME_SIZE] __attribute__((aligned(32)));
    uint32_t sequence;
    uint32_t frames_sent;
    uint32_t bytes_sent;
    uint8_t  use_spi;        /* 1=SPI, 0=USART polled */
} output_stream_t;

void output_stream_init(output_stream_t *stream, uint8_t use_spi);
int  output_stream_write_targets(output_stream_t *stream,
                                  const target_wgs84_t *targets,
                                  uint8_t count,
                                  uint32_t timestamp_us);
uint32_t output_stream_get_bytes_sent(const output_stream_t *stream);

#endif /* OUTPUT_STREAM_H */
