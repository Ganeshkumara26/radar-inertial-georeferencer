/*
 * test_ekf.c — Host-side validation harness for the EKF pipeline (ekf.c, unmodified).
 * Drives ekf_predict/ekf_update_radar with a scenario matching the repo's own
 * HIL stimuli generator (sim/generate_stimuli.py): drone flying north at 3 m/s
 * while descending, radar target fixed in world/NED frame under the aircraft.
 *
 * Compiled with -DSIMULATION_BUILD so ekf.c uses its portable float32 math
 * fallback (the same path exercised in the Renode simulation build).
 */
#define SIMULATION_BUILD
#include "ekf_fixed.h"
#include <stdio.h>
#include <math.h>

int main(void) {
    ekf_config_t cfg = {
        .dt = 0.01f,
        .process_noise = 0.1f,
        .meas_noise_range = 1.0f,
        .meas_noise_angle = 0.01f,
        .meas_noise_doppler = 0.5f
    };
    ekf_state_t ekf;
    ekf_init(&ekf, &cfg);

    /* True drone trajectory (NED), matching generate_stimuli.py:
     *   n(t) = 3.0 * t
     *   e(t) = 0
     *   d(t) = -10.0 + 0.5 * t
     *   v = (3.0, 0.0, 0.5)
     * A stationary world-frame target sits 10 m ahead of the drone's start
     * position, on the ground (d_target = 0 in NED, i.e. altitude = start alt).
     */
    float target_n = 3.0f, target_e = 0.0f, target_d = 10.0f; /* NED, ahead+below start */

    printf("step,t_s,pn,pe,pd,vn,ve,vd,range,az,el,doppler,innov_r,innov_az,innov_el,innov_dop,update_ok\n");

    float dt = 0.01f;
    for (int step = 0; step < 100; step++) {
        ekf_predict(&ekf, dt);

        /* Simulate true drone state at this time to derive relative target obs */
        float t = (step + 1) * dt;
        float dn = 3.0f * t;
        float de = 0.0f;
        float dd = -10.0f + 0.5f * t;

        /* Relative target position (target - drone) in NED */
        float rel_n = target_n - dn;
        float rel_e = target_e - de;
        float rel_d = target_d - dd;

        float r = sqrtf(rel_n*rel_n + rel_e*rel_e + rel_d*rel_d);
        float rg = sqrtf(rel_n*rel_n + rel_e*rel_e);
        float az = atan2f(rel_e, rel_n);
        float el = atan2f(-rel_d, rg);
        /* Doppler: relative closing rate; drone velocity subtracted (radar frame) */
        float vn = 3.0f, ve = 0.0f, vd = 0.5f;
        float doppler = -(rel_n*(-vn) + rel_e*(-ve) + rel_d*(-vd)) / r; /* range-rate wrt drone motion */

        float innov[4];
        int rc = ekf_update_radar(&ekf, r, az, el, doppler, innov);

        printf("%d,%.3f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.5f,%.5f,%.5f,%.5f,%d\n",
               step, t, ekf.x[0], ekf.x[1], ekf.x[2], ekf.x[3], ekf.x[4], ekf.x[5],
               r, az, el, doppler,
               rc == 0 ? innov[0] : NAN, rc == 0 ? innov[1] : NAN,
               rc == 0 ? innov[2] : NAN, rc == 0 ? innov[3] : NAN, rc == 0);
    }

    fprintf(stderr, "\nFinal state: pn=%.4f pe=%.4f pd=%.4f vn=%.4f ve=%.4f vd=%.4f\n",
            ekf.x[0], ekf.x[1], ekf.x[2], ekf.x[3], ekf.x[4], ekf.x[5]);
    fprintf(stderr, "predict_count=%u update_count=%u reject_count=%u\n",
            ekf.predict_count, ekf.update_count, ekf.reject_count);

    /* Final covariance trace (position block) as convergence metric */
    float tr_pos = ekf.P[0] + ekf.P[7] + ekf.P[14];
    float tr_vel = ekf.P[21] + ekf.P[28] + ekf.P[35];
    fprintf(stderr, "trace(P_pos)=%.6f trace(P_vel)=%.6f\n", tr_pos, tr_vel);

    return 0;
}
