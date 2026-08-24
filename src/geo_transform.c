/*
 * geo_transform.c — Coordinate transformation implementation
 * Target: STM32H753XI Cortex-M7 (FPv5-D16 FPU)
 *
 * All floating-point operations use the Cortex-M7 hardware FPU.
 * Trigonometric functions use CMSIS-DSP or standard math library.
 * For simulation builds, standard math is used.
 */
#include "geo_transform.h"
#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void geo_init_reference(geo_reference_t *ref, double lat, double lon, float alt) {
    ref->ref_latitude = lat;
    ref->ref_longitude = lon;
    ref->ref_altitude = alt;
    ref->valid = 1;
}

void euler_to_quaternion(float roll, float pitch, float yaw, quaternion_t *q) {
    float cr = cosf(roll * 0.5f);
    float sr = sinf(roll * 0.5f);
    float cp = cosf(pitch * 0.5f);
    float sp = sinf(pitch * 0.5f);
    float cy = cosf(yaw * 0.5f);
    float sy = sinf(yaw * 0.5f);

    q->w = cr * cp * cy + sr * sp * sy;
    q->x = sr * cp * cy - cr * sp * sy;
    q->y = cr * sp * cy + sr * cp * sy;
    q->z = cr * cp * sy - sr * sp * cy;
}

/* Rotate vector by quaternion: v' = q * v * q^-1 */
static void quat_rotate_vector(const quaternion_t *q,
                                float vx, float vy, float vz,
                                float *ox, float *oy, float *oz) {
    /* Optimized quaternion rotation (avoids full quaternion multiply) */
    float qw = q->w, qx = q->x, qy = q->y, qz = q->z;

    /* t = 2 * cross(q.xyz, v) */
    float tx = 2.0f * (qy * vz - qz * vy);
    float ty = 2.0f * (qz * vx - qx * vz);
    float tz = 2.0f * (qx * vy - qy * vx);

    /* v' = v + qw * t + cross(q.xyz, t) */
    *ox = vx + qw * tx + (qy * tz - qz * ty);
    *oy = vy + qw * ty + (qz * tx - qx * tz);
    *oz = vz + qw * tz + (qx * ty - qy * tx);
}

void polar_to_enu(const radar_target_polar_t *polar,
                  const quaternion_t *attitude,
                  float heading_rad,
                  target_enu_t *enu) {
    /* Step 1: Polar → Radar Cartesian (forward-right-down frame) */
    float r = polar->range;
    float cos_az = cosf(polar->azimuth);
    float sin_az = sinf(polar->azimuth);
    float cos_el = cosf(polar->elevation);
    float sin_el = sinf(polar->elevation);

    /* Radar frame: x=forward, y=right, z=down */
    float xr = r * cos_el * cos_az;
    float yr = r * cos_el * sin_az;
    float zr = -r * sin_el;  /* Negative because elevation up = negative down */

    /* Step 2: Radar Cartesian → Body frame (apply attitude quaternion) */
    float xb, yb, zb;
    quat_rotate_vector(attitude, xr, yr, zr, &xb, &yb, &zb);

    /* Step 3: Body frame → ENU (apply heading rotation around down axis) */
    float ch = cosf(heading_rad);
    float sh = sinf(heading_rad);

    /* Rotate from body (forward-right-down) to ENU (east-north-up) */
    enu->e = xb * sh + yb * ch;   /* East */
    enu->n = xb * ch - yb * sh;   /* North */
    enu->u = -zb;                  /* Up (negate down) */

    /* Transform Doppler velocity similarly */
    float vr = polar->doppler;  /* Radial velocity in radar frame */
    float vxr = vr * cos_el * cos_az;
    float vyr = vr * cos_el * sin_az;
    float vzr = -vr * sin_el;

    float vxb, vyb, vzb;
    quat_rotate_vector(attitude, vxr, vyr, vzr, &vxb, &vyb, &vzb);

    enu->vel_e = vxb * sh + vyb * ch;
    enu->vel_n = vxb * ch - vyb * sh;
    enu->vel_u = -vzb;
}

void enu_to_wgs84(const target_enu_t *enu,
                  const geo_reference_t *ref,
                  target_wgs84_t *wgs84) {
    if (!ref->valid) {
        wgs84->valid = 0;
        return;
    }

    /* Convert reference lat/lon to radians */
    double lat_rad = ref->ref_latitude * M_PI / 180.0;
    (void)ref->ref_longitude; /* used implicitly in dlon calculation below */

    /* Radius of curvature in the prime vertical */
    double N = WGS84_A / sqrt(1.0 - WGS84_E2 * sin(lat_rad) * sin(lat_rad));

    /* ENU → WGS84 conversion */
    double dlat = (double)enu->n / (N + (double)ref->ref_altitude);
    double dlon = (double)enu->e / ((N + (double)ref->ref_altitude) * cos(lat_rad));

    wgs84->latitude = ref->ref_latitude + dlat * 180.0 / M_PI;
    wgs84->longitude = ref->ref_longitude + dlon * 180.0 / M_PI;
    wgs84->altitude = ref->ref_altitude + enu->u;

    wgs84->vel_east = enu->vel_e;
    wgs84->vel_north = enu->vel_n;
    wgs84->vel_up = enu->vel_u;

    wgs84->valid = 1;
}

void geo_transform_target(const radar_target_polar_t *polar,
                          const quaternion_t *attitude,
                          float heading_rad,
                          const geo_reference_t *ref,
                          target_wgs84_t *wgs84) {
    target_enu_t enu;

    polar_to_enu(polar, attitude, heading_rad, &enu);
    enu_to_wgs84(&enu, ref, wgs84);

    wgs84->snr = polar->snr;
    wgs84->timestamp_us = polar->timestamp_us;
}
