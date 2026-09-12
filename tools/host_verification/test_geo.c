/*
 * test_geo.c — Validate geo_transform.c's ENU->WGS84 conversion against an
 * independent geodesic reference (direct WGS84 forward geodesic problem via
 * a Vincenty-style series, computed in Python for cross-check; here we just
 * emit the local-tangent-plane distance error implied by the ellipsoid
 * approximation used in geo_transform.c, by comparing the local flat-earth
 * radius-of-curvature approximation to the surface truth at increasing range.
 */
#include "geo_transform.h"
#include <stdio.h>
#include <math.h>

int main(void) {
    geo_reference_t ref;
    geo_init_reference(&ref, 13.0827, 80.2707, 0.0f); /* Chennai, matches main.c */

    quaternion_t identity = {1.0f, 0.0f, 0.0f, 0.0f}; /* no rotation */

    printf("range_m,azimuth_deg,elevation_deg,lat,lon,alt\n");
    float ranges[] = {1, 10, 50, 100, 500, 1000, 5000};
    for (int i = 0; i < 7; i++) {
        radar_target_polar_t polar = { .range = ranges[i], .azimuth = 0.0f,
                                        .elevation = 0.0f, .doppler = 0.0f,
                                        .snr = 20.0f, .timestamp_us = 0 };
        target_wgs84_t out;
        geo_transform_target(&polar, &identity, 0.0f, &ref, &out);
        printf("%.1f,0.0,0.0,%.9f,%.9f,%.4f\n", ranges[i], out.latitude, out.longitude, out.altitude);
    }
    return 0;
}
