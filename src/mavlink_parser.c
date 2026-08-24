/*
 * mavlink_parser.c — Zero-copy MAVLink v2 parser implementation
 * Target: STM32H753XI Cortex-M7
 *
 * Uses DMA1_Stream0 in circular mode for zero-CPU UART RX.
 * Parses directly from DMA buffer with SCB cache invalidation.
 * Hardware CRC-32 for packet validation.
 */
#include "mavlink_parser.h"

/* RCC */
#define RCC_BASE          0x58024400UL
#define RCC_AHB4ENR       (*(volatile uint32_t *)(RCC_BASE + 0x0E0UL))
#define RCC_AHB4ENR_DMA1EN (1UL << 0)
#define RCC_AHB4ENR_CRCEN  (1UL << 19)
#define RCC_APB2ENR       (*(volatile uint32_t *)(RCC_BASE + 0x0F0UL))
#define RCC_APB2ENR_USART1EN (1UL << 4)

/* USART1 */
#define USART1_BASE       0x40011000UL
#define USART1_CR1        (*(volatile uint32_t *)(USART1_BASE + 0x00UL))
#define USART1_CR3        (*(volatile uint32_t *)(USART1_BASE + 0x08UL))
#define USART1_BRR        (*(volatile uint32_t *)(USART1_BASE + 0x0CUL))
#define USART1_ISR        (*(volatile uint32_t *)(USART1_BASE + 0x1CUL))
#define USART1_ICR        (*(volatile uint32_t *)(USART1_BASE + 0x20UL))
#define USART1_RDR        (*(volatile uint32_t *)(USART1_BASE + 0x24UL))
#define USART1_TDR        (*(volatile uint32_t *)(USART1_BASE + 0x28UL))
#define USART1_RQR        (*(volatile uint32_t *)(USART1_BASE + 0x28UL))
#define USART_ISR_RXNE    (1UL << 5)
#define USART_ISR_TC      (1UL << 6)
#define USART_ISR_TXE     (1UL << 7)
#define USART_CR1_RXNEIE  (1UL << 5)
#define USART_CR3_DMAR     (1UL << 6)

/* DMA1 Stream 0 */
#define DMA1_BASE         0x40020000UL
#define DMA1_S0CR         (*(volatile uint32_t *)(DMA1_BASE + 0x010UL))
#define DMA1_S0NDTR       (*(volatile uint32_t *)(DMA1_BASE + 0x014UL))
#define DMA1_S0PAR        (*(volatile uint32_t *)(DMA1_BASE + 0x018UL))
#define DMA1_S0M0AR       (*(volatile uint32_t *)(DMA1_BASE + 0x01CUL))
#define DMA1_S0FCR        (*(volatile uint32_t *)(DMA1_BASE + 0x024UL))
#define DMA1_LIFCR        (*(volatile uint32_t *)(DMA1_BASE + 0x008UL))
#define DMA_CR_EN         (1UL << 0)
#define DMA_CR_CIRC       (1UL << 8)
#define DMA_CR_DIR_P2M    (0UL << 4)
#define DMA_CR_MINC       (1UL << 10)
#define DMA_CR_PINC       (0UL << 6)
#define DMA_CR_MSIZE_8    (0UL << 11)
#define DMA_CR_PSIZE_8    (0UL << 13)
#define DMA_CR_PL_HIGH    (3UL << 16)
#define DMA_LIFCR_CTCIF0  (1UL << 5)
#define DMA_LIFCR_CHTIF0  (1UL << 4)

/* CRC */
#define CRC_BASE          0x40023000UL
#define CRC_DR            (*(volatile uint32_t *)(CRC_BASE + 0x00UL))
#define CRC_CR            (*(volatile uint32_t *)(CRC_BASE + 0x08UL))
#define CRC_INIT          (*(volatile uint32_t *)(CRC_BASE + 0x10UL))
#define CRC_CR_RESET      (1UL << 0)
#define CRC_CR_REV_IN     (3UL << 5)  /* byte reversal */
#define CRC_CR_REV_OUT    (1UL << 7)

/* NVIC */
#define NVIC_ISER0        (*(volatile uint32_t *)(0xE000E100UL))
#define NVIC_ICPR0        (*(volatile uint32_t *)(0xE000E180UL))
#define DMA1_STREAM0_IRQn 11

/* SCB Cache maintenance */
#define SCB_DCCIMVAC      (*(volatile uint32_t *)(0xE000EF6CUL))
#define SCB_CCSIDR        (*(volatile uint32_t *)(0xE000ED80UL))

/* Debug output (polled USART TX) */
extern void usart1_putchar(char c);
extern void usart1_print(const char *str);

static uint32_t crc32_compute(const uint8_t *data, uint32_t len) {
    CRC_CR = CRC_CR_RESET;
    CRC_CR |= CRC_CR_REV_IN | CRC_CR_REV_OUT;

    uint32_t i = 0;
    /* Process 4 bytes at a time */
    for (; i + 4 <= len; i += 4) {
        CRC_DR = ((uint32_t)data[i])       |
                 ((uint32_t)data[i+1] << 8) |
                 ((uint32_t)data[i+2] << 16)|
                 ((uint32_t)data[i+3] << 24);
    }
    /* Process remaining bytes */
    if (i < len) {
        uint32_t word = 0;
        for (uint32_t j = 0; j < (len - i); j++) {
            word |= ((uint32_t)data[i+j]) << (j * 8);
        }
        CRC_DR = word;
    }

    return CRC_DR;
}

static void invalidate_cache_line(uint32_t addr) {
    SCB_DCCIMVAC = addr & ~0x1FU;
    __asm__ volatile ("dsb 0xF" ::: "memory");
    __asm__ volatile ("isb 0xF");
}

void mavlink_parser_init(mavlink_parser_t *parser) {
    /* Enable peripheral clocks */
    RCC_AHB4ENR |= RCC_AHB4ENR_DMA1EN | RCC_AHB4ENR_CRCEN;
    RCC_APB2ENR |= RCC_APB2ENR_USART1EN;

    /* Configure USART1: 921600 baud, 8N1, DMA RX */
    USART1_BRR = 0x222;  /* 480MHz / 16 / 921600 ≈ 32.5 → 0x222 gives ~921600 */
    USART1_CR3 = USART_CR3_DMAR;
    USART1_CR1 = (1UL << 3) | (1UL << 2) | (1UL << 0);  /* TE, RE, UE */

    /* Configure DMA1 Stream 0: USART1_RX → memory, circular */
    DMA1_S0CR &= ~DMA_CR_EN;  /* Disable stream first */
    while (DMA1_S0CR & DMA_CR_EN) {}  /* Wait for disable */

    DMA1_S0PAR = (uint32_t)&USART1_RDR;
    DMA1_S0M0AR = (uint32_t)parser->dma_buffer;
    DMA1_S0NDTR = MAVLINK_DMA_BUFFER_SIZE;
    DMA1_S0FCR = 0;  /* Direct mode, no FIFO */

    DMA1_S0CR = DMA_CR_CIRC | DMA_CR_MINC | DMA_CR_MSIZE_8 | DMA_CR_PSIZE_8 |
                DMA_CR_PL_HIGH | DMA_CR_DIR_P2M;
    DMA1_S0CR |= DMA_CR_EN;

    /* Enable DMA interrupt in NVIC */
    NVIC_ICPR0 = (1UL << DMA1_STREAM0_IRQn);
    NVIC_ISER0 = (1UL << DMA1_STREAM0_IRQn);

    /* Initialize CRC */
    CRC_CR = CRC_CR_RESET;

    parser->dma_last_read_pos = 0;
    parser->dma_last_ndtr = MAVLINK_DMA_BUFFER_SIZE;
    parser->packets_parsed = 0;
    parser->packets_dropped = 0;
    parser->crc_errors = 0;
}

void mavlink_parser_dma_isr(mavlink_parser_t *parser) {
    /* Clear DMA transfer complete flag */
    DMA1_LIFCR = DMA_LIFCR_CTCIF0 | DMA_LIFCR_CHTIF0;
    parser->dma_last_ndtr = MAVLINK_DMA_BUFFER_SIZE;
}

int mavlink_parser_poll(mavlink_parser_t *parser, kinematic_state_t *state) {
    /* Get current write position */
#ifdef SIMULATION_BUILD
    /* Simulation backdoor: Renode writes RX byte count to fixed address */
    volatile uint32_t *sim_rx_count = (volatile uint32_t *)SIM_RX_COUNT_ADDR;
    uint32_t total_bytes = *sim_rx_count;
    /* Process only up to buffer size at a time (handle wrap) */
    uint32_t write_pos = total_bytes;
    if (write_pos > MAVLINK_DMA_BUFFER_SIZE) {
        write_pos = MAVLINK_DMA_BUFFER_SIZE;
    }
#else
    /* Real hardware: read DMA remaining count */
    uint32_t ndtr = DMA1_S0NDTR;
    uint32_t write_pos = MAVLINK_DMA_BUFFER_SIZE - ndtr;
#endif

    if (write_pos == parser->dma_last_read_pos) {
#ifdef SIMULATION_BUILD
        /* Check if there's more data beyond buffer size */
        if (total_bytes > MAVLINK_DMA_BUFFER_SIZE) {
            /* Reset for next chunk */
            parser->dma_last_read_pos = 0;
            *sim_rx_count = total_bytes - MAVLINK_DMA_BUFFER_SIZE;
            /* Don't return — process the next chunk */
        } else {
            return 0;  /* No new data */
        }
#else
        return 0;  /* No new data */
#endif
    }

    /* Invalidate cache for the region we're about to read */
    uint32_t read_start = parser->dma_last_read_pos;
    uint32_t read_end = write_pos;

    if (read_end > read_start) {
        for (uint32_t addr = read_start & ~0x1FU; addr < read_end; addr += 32) {
            invalidate_cache_line((uint32_t)&parser->dma_buffer[addr]);
        }
    } else {
        /* Wrapped around */
        for (uint32_t addr = read_start & ~0x1FU; addr < MAVLINK_DMA_BUFFER_SIZE; addr += 32) {
            invalidate_cache_line((uint32_t)&parser->dma_buffer[addr]);
        }
        for (uint32_t addr = 0; addr < read_end; addr += 32) {
            invalidate_cache_line((uint32_t)&parser->dma_buffer[addr]);
        }
    }

    /* Process ONE packet per call (save state for next call) */
    uint32_t pos = parser->dma_last_read_pos;
    int found = 0;

    if (pos >= write_pos) {
        return 0;  /* No new data */
    }

    uint8_t magic = parser->dma_buffer[pos];

    if (magic == MAVLINK_V1_MAGIC || magic == MAVLINK_V2_MAGIC) {
        uint32_t pkt_len;
        if (magic == MAVLINK_V2_MAGIC) {
            if ((pos + MAVLINK_HEADER_LEN) > write_pos) return 0;
            pkt_len = MAVLINK_HEADER_LEN + parser->dma_buffer[pos + 1] + MAVLINK_CHECKSUM_LEN;
        } else {
            if ((pos + 8) > write_pos) return 0;
            pkt_len = 8 + parser->dma_buffer[pos + 1] + MAVLINK_CHECKSUM_LEN;
        }

        /* Check if full packet is available */
        if ((pos + pkt_len) > write_pos) {
            return 0;  /* Incomplete */
        }

        /* Validate CRC (skip in simulation) */
#ifndef SIMULATION_BUILD
        uint32_t crc = crc32_compute(&parser->dma_buffer[pos], pkt_len - MAVLINK_CHECKSUM_LEN);
        uint16_t recv_crc = (uint16_t)parser->dma_buffer[pos + pkt_len - 2] |
                           ((uint16_t)parser->dma_buffer[pos + pkt_len - 1] << 8);
        if (crc != recv_crc) {
            parser->crc_errors++;
            parser->dma_last_read_pos = pos + 1;  /* Skip this byte */
            return 0;
        }
#endif

        /* Parse message ID and dispatch */
        uint8_t msgid;
        uint8_t payload_offset;
        if (magic == MAVLINK_V2_MAGIC) {
            msgid = parser->dma_buffer[pos + 7];
            payload_offset = MAVLINK_HEADER_LEN;
        } else {
            msgid = parser->dma_buffer[pos + 5];
            payload_offset = 6;
        }

        const uint8_t *payload = &parser->dma_buffer[pos + payload_offset];

        switch (msgid) {
            case MAVLINK_MSG_ID_ATTITUDE: {
                const mavlink_attitude_t *att = (const mavlink_attitude_t *)payload;
                state->roll = att->roll;
                state->pitch = att->pitch;
                state->yaw = att->yaw;
                state->rollspeed = att->rollspeed;
                state->pitchspeed = att->pitchspeed;
                state->yawspeed = att->yawspeed;
                state->time_boot_ms = att->time_boot_ms;
                state->has_attitude = 1;
                found = 1;
                break;
            }
            case MAVLINK_MSG_ID_LOCAL_POSITION_NED: {
                const mavlink_local_position_ned_t *lp = (const mavlink_local_position_ned_t *)payload;
                state->pos_n = lp->x;
                state->pos_e = lp->y;
                state->pos_d = lp->z;
                state->vel_n = lp->vx;
                state->vel_e = lp->vy;
                state->vel_d = lp->vz;
                state->time_boot_ms = lp->time_boot_ms;
                state->has_local_pos = 1;
                state->has_velocity = 1;
                found = 1;
                break;
            }
            case MAVLINK_MSG_ID_GLOBAL_POSITION_INT: {
                const mavlink_global_position_int_t *gp = (const mavlink_global_position_int_t *)payload;
                state->latitude = gp->lat / 1e7;
                state->longitude = gp->lon / 1e7;
                state->altitude = gp->alt / 1000.0f;
                state->time_boot_ms = gp->time_boot_ms;
                state->has_global_pos = 1;
                found = 1;
                break;
            }
            default:
                break;
        }

        if (found) {
            parser->packets_parsed++;
        }
        parser->dma_last_read_pos = pos + pkt_len;
    } else {
        /* Not a magic byte — skip */
        parser->dma_last_read_pos = pos + 1;
    }

#ifdef SIMULATION_BUILD
    parser->dma_last_ndtr = MAVLINK_DMA_BUFFER_SIZE - parser->dma_last_read_pos;
#else
    parser->dma_last_ndtr = ndtr;
#endif

    return found;
}
