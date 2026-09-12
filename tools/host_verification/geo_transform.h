/*
 * geo_transform.h — Coordinate transformation engine
 * Target: STM32H753XI Cortex-M7
 *
 * Converts local radar polar coordinates (slant range, azimuth, elevation)
 * into global ENU or WGS84 (lat/lon/alt) coordinates.
 * Strips out the drone's instantaneous velocity vector from Doppler.
 *
 * Pipeline:
 *   Radar polar (range, az, el) → Radar Cartesian (x,y,z)
 *   Radar Cartesian → Body frame (apply attitude quaternion)
 *   Body frame → ENU (apply heading rotation)
 *   ENU → WGS84 (apply reference lat/lon)
 */
#ifndef GEO_TRANSFORM_H
#define GEO_TRANSFORM_H

#include <stdint.h>

/* Quaternion representation */
typedef struct {
    float w, x, y, z;
} quaternion_t;

/* Radar target in polar coordinates (input) */
typedef struct {
    float range;      /* Slant range in meters */
    float azimuth;    /* Azimuth angle in radians (0 = forward) */
    float elevation;  /* Elevation angle in radians (0 = horizontal) */
    float doppler;    /* Radial velocity in m/s (positive = approaching) */
    float snr;        /* Signal-to-noise ratio in dB */
    uint32_t timestamp_us; /* Hardware timestamp of detection */
} radar_target_polar_t;

/* Target in ENE frame (intermediate) */
typedef struct {
    float e;          /* East in meters */
    float n;          /* North in meters */
    float u;          /* Up in meters */
    float vel_e;      /* Velocity East component (m/s) */
    float vel_n;      /* Velocity North component (m/s) */
    float vel_u;      /* Velocity Up component (m/s) */
} target_enu_t;

/* Target in WGS84 (output) */
typedef struct {
    double latitude;   /* degrees */
    double longitude;  /* degrees */
    float  altitude;   /* meters above MSL */
    float  vel_east;   /* m/s */
    float  vel_north;  /* m/s */
    float  vel_up;     /* m/s */
    float  snr;        /* dB */
    uint32_t timestamp_us;
    uint8_t valid;
} target_wgs84_t;

/* Reference position for ENU→WGS84 conversion */
typedef struct {
    double ref_latitude;   /* degrees */
    double ref_longitude;  /* degrees */
    float  ref_altitude;   /* meters */
    uint8_t valid;
} geo_reference_t;

/* WGS84 constants */
#define WGS84_A 6378137.0           /* Semi-major axis (m) */
#define WGS84_F (1.0 / 298.257223563) /* Flattening */
#define WGS84_B (WGS84_A * (1.0 - WGS84_F)) /* Semi-minor axis */
#define WGS84_E2 (2.0 * WGS84_F - WGS84_F * WGS84_F) /* First eccentricity squared */

void geo_init_reference(geo_reference_t *ref, double lat, double lon, float alt);

void polar_to_enu(const radar_target_polar_t *polar,
                  const quaternion_t *attitude,
                  float heading_rad,
                  target_enu_t *enu);

void enu_to_wgs84(const target_enu_t *enu,
                  const geo_reference_t *ref,
                  target_wgs84_t *wgs84);

void geo_transform_target(const radar_target_polar_t *polar,
                          const quaternion_t *attitude,
                          float heading_rad,
                          const geo_reference_t *ref,
                          target_wgs84_t *wgs84);

void euler_to_quaternion(float roll, float pitch, float yaw, quaternion_t *q);

#endif /* GEO_TRANSFORM_H */
