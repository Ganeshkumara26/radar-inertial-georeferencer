/* Byte-wise MAVLink v2 receiver + EKF + geo transform. ATTITUDE (id 30, crc_extra 39) and a
 * custom RADAR frame (id 0x4001, crc_extra 0x5A, payload: u32 seq, f32 range, az, el, doppler). */
#include "hil_core.h"
#include "ekf.h"
#include "geo_transform.h"
#include <stdio.h>
#include <string.h>

void ekf_seed(ekf_state_t *e);
#define ID_ATT 30u
#define ID_RADAR 0x4001u
static uint8_t buf[12 + 255];
static uint16_t idx, need;
static uint32_t ok, crc_err, unknown, bad_len;
static float roll, pitch, yaw;
static ekf_state_t ekf;
static geo_reference_t ref;
static int inited;

void hil_init(void) {
    ekf_seed(&ekf); geo_init_reference(&ref, 13.0827, 80.2707, 0.0f);
    idx = 0; need = 0; ok = crc_err = unknown = bad_len = 0; roll = pitch = yaw = 0; inited = 1;
}
static uint32_t rd32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static float rdf(const uint8_t *p) { float f; memcpy(&f, p, 4); return f; }
static uint32_t f2u(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }

static void handle(const uint8_t *f, bench_now_fn now, bench_emit_fn emit) {
    uint8_t len = f[1];
    uint32_t id = f[7] | ((uint32_t)f[8] << 8) | ((uint32_t)f[9] << 16);
    const uint8_t *p = f + 10;
    uint8_t extra = (id == ID_ATT) ? 39 : (id == ID_RADAR) ? 0x5A : 0;
    if (id != ID_ATT && id != ID_RADAR) { unknown++; return; }
    uint16_t want = (uint16_t)(f[10 + len] | (f[11 + len] << 8));
    if (crc_x25(f + 1, 9u + len, extra) != want) { crc_err++; return; }
    ok++;
    if (id == ID_ATT && len >= 16) { roll = rdf(p + 4); pitch = rdf(p + 8); yaw = rdf(p + 12); }
    else if (id == ID_RADAR && len >= 20) {
        char b[140];
        uint32_t seq = rd32(p);
        radar_target_polar_t pol = { rdf(p + 4), rdf(p + 8), rdf(p + 12), rdf(p + 16), 20.0f, 0 };
        quaternion_t q; target_wgs84_t w; float innov[4];
        uint32_t t0 = now();
        ekf_predict(&ekf, 0.01f);
        int rc = ekf_update_radar(&ekf, pol.range, pol.azimuth, pol.elevation, pol.doppler, innov);
        uint32_t t1 = now();
        euler_to_quaternion(roll, pitch, yaw, &q);
        geo_transform_target(&pol, &q, yaw, &ref, &w);
        uint32_t t2 = now();
        snprintf(b, sizeof b, "R,%lu,%lu,%lu,%d,%08lx,%08lx,%08lx,%08lx,%08lx,%08lx\n",
                 (unsigned long)seq, (unsigned long)(t1 - t0), (unsigned long)(t2 - t1), rc,
                 (unsigned long)f2u(ekf.x[0]), (unsigned long)f2u(ekf.x[1]), (unsigned long)f2u(ekf.x[2]),
                 (unsigned long)f2u(ekf.x[3]), (unsigned long)f2u(ekf.x[4]), (unsigned long)f2u(ekf.x[5]));
        emit(b);
        if (seq % 100 == 99) {
            snprintf(b, sizeof b, "H,ok=%lu,crc_err=%lu,unknown=%lu,bad_len=%lu,upd=%lu,rej=%lu\n",
                     (unsigned long)ok, (unsigned long)crc_err, (unsigned long)unknown, (unsigned long)bad_len,
                     (unsigned long)ekf.update_count, (unsigned long)ekf.reject_count);
            emit(b);
        }
    }
}

void hil_feed(uint8_t c, bench_now_fn now, bench_emit_fn emit) {
    if (!inited) hil_init();
    if (idx == 0) { if (c == 0xFD) buf[idx++] = c; return; }
    buf[idx++] = c;
    if (idx == 10) {
        if (buf[2] & 1) { bad_len++; idx = 0; return; }          /* signed frames unsupported */
        need = (uint16_t)(12u + buf[1]);
    }
    if (idx >= 10 && idx == need) { handle(buf, now, emit); idx = 0; }
}
