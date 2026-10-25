#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include "ekf.h"

static void assert_float_eq(float a, float b, float tol, const char *msg) {
    if (fabsf(a - b) > tol) {
        printf("FAIL: %s (expected %.6f, got %.6f)\n", msg, b, a);
        exit(1);
    }
}

static void test_ekf_init(void) {
    printf("test_ekf_init... ");
    ekf_state_t ekf;
    ekf_config_t cfg = {
        .dt = 0.01f,
        .process_noise = 0.1f,
        .meas_noise_range = 1.0f,
        .meas_noise_angle = 0.01f,
        .meas_noise_doppler = 0.1f
    };
    ekf_init(&ekf, &cfg);
    assert(ekf.initialized == 0);
    assert(ekf.predict_count == 0);
    assert(ekf.update_count == 0);
    printf("PASS\n");
}

static void test_ekf_predict(void) {
    printf("test_ekf_predict... ");
    ekf_state_t ekf;
    ekf_config_t cfg = {
        .dt = 0.01f,
        .process_noise = 0.1f,
        .meas_noise_range = 1.0f,
        .meas_noise_angle = 0.01f,
        .meas_noise_doppler = 0.1f
    };
    ekf_init(&ekf, &cfg);
    ekf.x[0] = 1.0f;
    ekf.x[3] = 10.0f;
    ekf_predict(&ekf, 0.01f);
    assert_float_eq(ekf.x[0], 1.0f + 10.0f * 0.01f, 1e-5f, "predict position");
    assert(ekf.predict_count == 1);
    printf("PASS\n");
}

static void test_ekf_update_no_divergence(void) {
    printf("test_ekf_update_no_divergence... ");
    ekf_state_t ekf;
    ekf_config_t cfg = {
        .dt = 0.01f,
        .process_noise = 0.1f,
        .meas_noise_range = 1.0f,
        .meas_noise_angle = 0.01f,
        .meas_noise_doppler = 0.1f
    };
    ekf_init(&ekf, &cfg);

    float innovations[4];
    for (int i = 0; i < 100; i++) {
        ekf_predict(&ekf, 0.01f);
        int ret = ekf_update_radar(&ekf, 100.0f, 0.1f, -0.5f, -5.0f, innovations);
        (void)ret;
    }

    for (int i = 0; i < 6; i++) {
        assert(!isnan(ekf.x[i]));
        for (int j = 0; j < 6; j++) {
            assert(!isnan(ekf.P[i * 6 + j]));
        }
    }
    printf("PASS\n");
}

int main(void) {
    printf("Running EKF tests on host\n");
    printf("==========================\n");
    test_ekf_init();
    test_ekf_predict();
    test_ekf_update_no_divergence();
    printf("\nAll tests PASSED\n");
    return 0;
}
