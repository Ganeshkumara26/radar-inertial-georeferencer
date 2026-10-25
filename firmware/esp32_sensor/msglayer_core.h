/* uav-msglayer core (header-only C, no malloc, no I/O, no OS). Wire format identical to uavpipe/msglayer.py (cross-checked in tests).
 * Frame: 0xA5 | len u8 | cls u8 | seq u16 | t_gen u32 | max_age u32 | msgid u8 | payload<=32 | CRC-16/MCRF4XX (LE) over bytes after SOF. All fields little-endian. */
#ifndef MSGLAYER_CORE_H
#define MSGLAYER_CORE_H
#include <stdint.h>
#include <stddef.h>
#define ML_SOF 0xA5u
#define ML_MAXP 32u
#define ML_HDR 14u            /* SOF + 13 header bytes */
#define ML_TELEM 0u
#define ML_CMD 1u
#define ML_ACK 2u
#define ML_MAX_FRAME (ML_HDR + ML_MAXP + 2u)

static inline uint16_t ml_crc16(const uint8_t *d, size_t n) {
    uint16_t crc = 0xFFFFu;
    for (size_t i = 0; i < n; i++) { crc ^= d[i]; for (int b = 0; b < 8; b++) crc = (crc & 1u) ? (uint16_t)((crc >> 1) ^ 0x8408u) : (uint16_t)(crc >> 1); }
    return crc;
}
static inline void ml_put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static inline uint32_t ml_get32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

/* returns frame length, or 0 if payload too long */
static inline size_t ml_pack(uint8_t *out, uint8_t cls, uint16_t seq, uint32_t t_gen, uint32_t max_age, uint8_t msgid, const uint8_t *pl, uint8_t len) {
    if (len > ML_MAXP) return 0;
    out[0] = ML_SOF; out[1] = len; out[2] = cls; out[3] = (uint8_t)seq; out[4] = (uint8_t)(seq >> 8);
    ml_put32(out + 5, t_gen); ml_put32(out + 9, max_age); out[13] = msgid;
    for (uint8_t i = 0; i < len; i++) out[ML_HDR + i] = pl[i];
    uint16_t c = ml_crc16(out + 1, (size_t)(ML_HDR - 1u) + len);
    out[ML_HDR + len] = (uint8_t)c; out[ML_HDR + len + 1] = (uint8_t)(c >> 8);
    return (size_t)ML_HDR + len + 2u;
}
typedef struct { uint8_t cls, msgid, len; uint16_t seq; uint32_t t_gen, max_age; const uint8_t *payload; } ml_msg_t;
/* returns 1 and fills m if buf is exactly one valid frame; 0 otherwise. Never reads past n. */
static inline int ml_unpack(const uint8_t *buf, size_t n, ml_msg_t *m) {
    if (n < ML_HDR + 2u || buf[0] != ML_SOF) return 0;
    uint8_t len = buf[1];
    if (len > ML_MAXP || n != (size_t)ML_HDR + len + 2u || buf[2] > ML_ACK) return 0;
    uint16_t want = (uint16_t)(buf[ML_HDR + len] | ((uint16_t)buf[ML_HDR + len + 1] << 8));
    if (ml_crc16(buf + 1, (size_t)(ML_HDR - 1u) + len) != want) return 0;
    m->len = len; m->cls = buf[2]; m->seq = (uint16_t)(buf[3] | ((uint16_t)buf[4] << 8)); m->t_gen = ml_get32(buf + 5); m->max_age = ml_get32(buf + 9);
    m->msgid = buf[13]; m->payload = buf + ML_HDR; return 1;
}
#endif
