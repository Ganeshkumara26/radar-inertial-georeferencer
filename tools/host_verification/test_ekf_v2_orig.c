/*
 * test_ekf_v2.c — Same scenario, but state initialized from a plausible
 * relative-position prior (as would occur once ring buffer / MAVLink LOCAL_POSITION_NED
 * seeds anything nonzero), to characterize steady-state convergence rather than
 * the pn=pe=pd=0 singular start.
 */
#define SIMULATION_BUILD
#include "ekf.h"
#include <stdio.h>
#include <math.h>

int main(void) {
    ekf_config_t cfg = { .dt=0.01f, .process_noise=0.1f, .meas_noise_range=1.0f,
                          .meas_noise_angle=0.01f, .meas_noise_doppler=0.5f };
    ekf_state_t ekf;
    ekf_init(&ekf, &cfg);

    /* Seed with a rough, deliberately-imperfect prior (as if velocity is known,
       relative position guessed): true first relative pos is (3,0,10) NED */
    ekf.x[0] = 2.0f; ekf.x[1] = 0.5f; ekf.x[2] = 8.0f;
    ekf.x[3] = 3.0f; ekf.x[4] = 0.0f; ekf.x[5] = 0.5f;

    float target_n = 3.0f, target_e = 0.0f, target_d = 10.0f;
    printf("step,t_s,pn,pe,pd,vn,ve,vd,err_pos_m,update_ok\n");

    float dt = 0.01f;
    for (int step = 0; step < 200; step++) {
        ekf_predict(&ekf, dt);

        float t = (step + 1) * dt;
        float dn = 3.0f * t, de = 0.0f, dd = -10.0f + 0.5f * t;
        float rel_n = target_n - dn, rel_e = target_e - de, rel_d = target_d - dd;

        float r = sqrtf(rel_n*rel_n + rel_e*rel_e + rel_d*rel_d);
        float rg = sqrtf(rel_n*rel_n + rel_e*rel_e);
        float az = atan2f(rel_e, rel_n);
        float el = atan2f(-rel_d, rg);
        float vn=3.0f, ve=0.0f, vd=0.5f;
        float doppler = -(rel_n*(-vn) + rel_e*(-ve) + rel_d*(-vd)) / r;

        float innov[4];
        int rc = ekf_update_radar(&ekf, r, az, el, doppler, innov);

        float ex = ekf.x[0]-rel_n, ey = ekf.x[1]-rel_e, ez = ekf.x[2]-rel_d;
        float err = sqrtf(ex*ex+ey*ey+ez*ez);

        printf("%d,%.3f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%d\n",
               step, t, ekf.x[0], ekf.x[1], ekf.x[2], ekf.x[3], ekf.x[4], ekf.x[5], err, rc==0);
    }
    fprintf(stderr, "predict_count=%u update_count=%u reject_count=%u\n",
            ekf.predict_count, ekf.update_count, ekf.reject_count);
    return 0;
}
