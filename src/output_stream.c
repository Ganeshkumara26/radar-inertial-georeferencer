/*
 * output_stream.c — Deterministic binary output implementation
 * Target: STM32H753XI Cortex-M7
 *
 * Builds binary frames in SRAM, computes CRC-32, then streams via SPI DMA
 * or polled USART. Frame structure:
 *
 *   [MAGIC(1)] [VERSION(1)] [LENGTH(2)] [SEQ(4)] [TIMESTAMP(4)] [COUNT(1)] [STATUS(1)]
 *   [TARGET_RECORD(32) * COUNT]
 *   [CRC32(4)]
 */
#include "output_stream.h"
#include <string.h>

/* USART1 for polled output */
#define USART1_BASE       0x40011000UL
#define USART1_TDR        (*(volatile uint32_t *)(USART1_BASE + 0x28UL))
#define USART1_ISR        (*(volatile uint32_t *)(USART1_BASE + 0x1CUL))
#define USART_ISR_TXE     (1UL << 7)

/* CRC peripheral */
#define CRC_BASE          0x40023000UL
#define CRC_DR            (*(volatile uint32_t *)(CRC_BASE + 0x00UL))
#define CRC_CR            (*(volatile uint32_t *)(CRC_BASE + 0x08UL))
#define CRC_CR_RESET      (1UL << 0)
#define CRC_CR_REV_IN     (3UL << 5)
#define CRC_CR_REV_OUT    (1UL << 7)

/* SPI1 */
#define SPI1_BASE         0x40013000UL
#define SPI1_CR1          (*(volatile uint32_t *)(SPI1_BASE + 0x00UL))
#define SPI1_CR2          (*(volatile uint32_t *)(SPI1_BASE + 0x04UL))
#define SPI1_SR           (*(volatile uint32_t *)(SPI1_BASE + 0x08UL))
#define SPI1_DR           (SPI1_BASE + 0x0CUL)
#define SPI_CR1_SPE       (1UL << 6)
#define SPI_CR1_MSTR      (1UL << 2)
#define SPI_CR1_SSI       (1UL << 8)
#define SPI_CR1_SSM       (1UL << 9)
#define SPI_SR_TXE        (1UL << 1)
#define SPI_SR_BSY        (1UL << 7)

/* RCC */
#define RCC_BASE          0x58024400UL
#define RCC_APB2ENR       (*(volatile uint32_t *)(RCC_BASE + 0x0F0UL))
#define RCC_APB2ENR_SPI1EN (1UL << 12)

static uint32_t crc32_compute_buffer(const uint8_t *data, uint32_t len) {
    CRC_CR = CRC_CR_RESET;
    CRC_CR |= CRC_CR_REV_IN | CRC_CR_REV_OUT;

    uint32_t i = 0;
    for (; i + 4 <= len; i += 4) {
        CRC_DR = ((uint32_t)data[i])       |
                 ((uint32_t)data[i+1] << 8) |
                 ((uint32_t)data[i+2] << 16)|
                 ((uint32_t)data[i+3] << 24);
    }
    if (i < len) {
        uint32_t word = 0;
        for (uint32_t j = 0; j < (len - i); j++) {
            word |= ((uint32_t)data[i+j]) << (j * 8);
        }
        CRC_DR = word;
    }

    return CRC_DR;
}

static void usart1_send_byte(uint8_t byte) {
    while (!(USART1_ISR & USART_ISR_TXE)) {}
    USART1_TDR = byte;
}

static void spi1_send_byte(uint8_t byte) {
    while (!(*(volatile uint32_t *)(SPI1_SR) & SPI_SR_TXE)) {}
    *(volatile uint8_t *)(SPI1_DR) = byte;
    while (*(volatile uint32_t *)(SPI1_SR) & SPI_SR_BSY) {}
}

void output_stream_init(output_stream_t *stream, uint8_t use_spi) {
    stream->sequence = 0;
    stream->frames_sent = 0;
    stream->bytes_sent = 0;
    stream->use_spi = use_spi;

    memset(stream->frame_buffer, 0, OUTPUT_MAX_FRAME_SIZE);

    if (use_spi) {
        RCC_APB2ENR |= RCC_APB2ENR_SPI1EN;
        SPI1_CR1 = SPI_CR1_MSTR | SPI_CR1_SSI | SPI_CR1_SSM | SPI_CR1_SPE;
    }

    /* Ensure CRC is ready */
    CRC_CR = CRC_CR_RESET;
}

int output_stream_write_targets(output_stream_t *stream,
                                const target_wgs84_t *targets,
                                uint8_t count,
                                uint32_t timestamp_us) {
    if (count > OUTPUT_MAX_TARGETS) {
        count = OUTPUT_MAX_TARGETS;
    }

    uint8_t *buf = stream->frame_buffer;
    uint32_t offset = 0;

    /* Build header */
    output_header_t *hdr = (output_header_t *)buf;
    hdr->magic = OUTPUT_MAGIC;
    hdr->version = OUTPUT_VERSION;
    hdr->length = OUTPUT_HEADER_SIZE + (count * OUTPUT_TARGET_SIZE) + 4;  /* +4 for CRC */
    hdr->sequence = stream->sequence++;
    hdr->timestamp_us = timestamp_us;
    hdr->target_count = count;
    hdr->status = 0;  /* TODO: fill from system status */

    offset = OUTPUT_HEADER_SIZE;

    /* Pack target records */
    for (uint8_t i = 0; i < count; i++) {
        if (!targets[i].valid) continue;

        output_target_record_t *rec = (output_target_record_t *)&buf[offset];
        rec->latitude = targets[i].latitude;
        rec->longitude = targets[i].longitude;
        rec->altitude = targets[i].altitude;
        rec->vel_east = targets[i].vel_east;
        rec->vel_north = targets[i].vel_north;
        rec->vel_up = targets[i].vel_up;
        rec->snr = targets[i].snr;
        rec->timestamp_us = targets[i].timestamp_us;

        offset += OUTPUT_TARGET_SIZE;
    }

    /* Update actual length (may be less if some targets were invalid) */
    hdr->length = offset + 4;  /* +4 for CRC */

    /* Compute CRC-32 over header + payload */
    uint32_t crc = crc32_compute_buffer(buf, offset);

    /* Append CRC (little-endian) */
    buf[offset++] = (uint8_t)(crc & 0xFF);
    buf[offset++] = (uint8_t)((crc >> 8) & 0xFF);
    buf[offset++] = (uint8_t)((crc >> 16) & 0xFF);
    buf[offset++] = (uint8_t)((crc >> 24) & 0xFF);

    /* Stream frame */
#ifdef SIMULATION_BUILD
    /* In simulation, don't write binary data to debug UART */
    (void)buf;
#else
    if (stream->use_spi) {
        for (uint32_t i = 0; i < offset; i++) {
            spi1_send_byte(buf[i]);
        }
    } else {
        for (uint32_t i = 0; i < offset; i++) {
            usart1_send_byte(buf[i]);
        }
    }
#endif

    stream->frames_sent++;
    stream->bytes_sent += offset;

    return 0;
}

uint32_t output_stream_get_bytes_sent(const output_stream_t *stream) {
    return stream->bytes_sent;
}
