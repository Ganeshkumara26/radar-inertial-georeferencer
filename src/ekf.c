/*
 * ekf.c — Fixed Extended Kalman Filter (corrected)
 * Target: STM32H753XI Cortex-M7
 *
 * OPTIMISED for soft-float Cortex-M0 (same equations, structure exploited; see ekf_ref_orig.c for the original dense version):
 *   predict: analytic F=[[I,dt I],[0,I]]; update: sparse H, symmetric P/S, diagonal R, one reciprocal per pivot.
 *   Joseph form kept: P+ = (I-KH)P(I-KH)^T + K R K^T evaluated as A + G K^T, A=P-K(HP). P/S kept exactly symmetric by construction.
 *   Assumes Q symmetric and R diagonal (true for ekf_init).
 *
 * Corrections applied:
 *   1. Distinct scratchpad buffers for every intermediate matrix op
 *   2. sqrtf() on range prediction
 *   3. atan2f() for angle predictions (not raw position values)
 *   4. Full 4x6 Jacobian with correct row offsets and column indices
 *   5. P_new stored in temp buffer, copied back only after computation
 *   6. Covariance symmetry enforced after each update
 */
#include "ekf.h"
#include <string.h>
#include <math.h>

#include <math.h>

void ekf_init(ekf_state_t *ekf, const ekf_config_t *config) {
    /* Manual zero instead of memset (Renode memset crash workaround) */
    uint8_t *p = (uint8_t *)ekf;
    for (uint32_t i = 0; i < sizeof(ekf_state_t); i++) p[i] = 0;
    ekf->process_noise = config->process_noise;
    ekf->meas_noise_range = config->meas_noise_range;
    ekf->meas_noise_angle = config->meas_noise_angle;
    ekf->meas_noise_doppler = config->meas_noise_doppler;
    ekf->innovation_gate = 9.0f;
    ekf->initialized = 0;
    ekf->meas_noise_range = config->meas_noise_range;
    ekf->meas_noise_angle = config->meas_noise_angle;
    ekf->meas_noise_doppler = config->meas_noise_doppler;
    ekf->innovation_gate = 9.0f;
    ekf->initialized = 0;

    for (int i = 0; i < EKF_STATE_DIM * EKF_STATE_DIM; i++) ekf->Q[i] = 0.0f;
    ekf->Q[0] = ekf->Q[7] = ekf->Q[14] = config->process_noise;
    ekf->Q[21] = ekf->Q[28] = ekf->Q[35] = config->process_noise * 0.1f;

    for (int i = 0; i < EKF_MEAS_DIM * EKF_MEAS_DIM; i++) ekf->R[i] = 0.0f;
    ekf->R[0] = config->meas_noise_range;
    ekf->R[5] = config->meas_noise_angle;
    ekf->R[10] = config->meas_noise_angle;
    ekf->R[15] = config->meas_noise_doppler;

    for (int i = 0; i < EKF_STATE_DIM * EKF_STATE_DIM; i++) ekf->P[i] = 0.0f;
    ekf->P[0] = ekf->P[7] = ekf->P[14] = 100.0f;
    ekf->P[21] = ekf->P[28] = ekf->P[35] = 25.0f;
}

void ekf_predict(ekf_state_t *ekf, float dt) {
    ekf->x[0] += ekf->x[3] * dt;
    ekf->x[1] += ekf->x[4] * dt;
    ekf->x[2] += ekf->x[5] * dt;

    float Pn[36]; const float *P = ekf->P;
    for (int i = 0; i < 3; i++) {
        for (int j = i; j < 3; j++) {
            float tl = (P[i*6+j] + dt * P[(3+i)*6+j]) + dt * (P[i*6+3+j] + dt * P[(3+i)*6+3+j]);
            Pn[i*6+j] = tl + ekf->Q[i*6+j];
        }
        for (int j = 0; j < 3; j++) {
            float tr = P[i*6+3+j] + dt * P[(3+i)*6+3+j];
            Pn[i*6+3+j] = tr + ekf->Q[i*6+3+j];
        }
        for (int j = i; j < 3; j++) Pn[(3+i)*6+3+j] = P[(3+i)*6+3+j] + ekf->Q[(3+i)*6+3+j];
    }
    for (int i = 0; i < 6; i++) for (int j = 0; j < i; j++) Pn[i*6+j] = Pn[j*6+i];   /* mirror upper -> lower */
    for (int i = 0; i < 36; i++) ekf->P[i] = Pn[i];
    ekf->predict_count++;
}

int ekf_update_radar(ekf_state_t *ekf, float range, float azimuth, float elevation, float doppler, float *innovation_out) {
    float pn = ekf->x[0], pe = ekf->x[1], pd = ekf->x[2];
    float vn = ekf->x[3], ve = ekf->x[4], vd = ekf->x[5];

    float r2 = pn*pn + pe*pe + pd*pd;
    if (r2 < 1e-6f) r2 = 1e-6f;
    float r = sqrtf(r2);
    float rg2_raw = pn*pn + pe*pe;
    if (rg2_raw < 1e-6f) rg2_raw = 1e-6f;
    float r_ground = sqrtf(rg2_raw);
    float azimuth_pred = atan2f(pe, pn);
    float elevation_pred = atan2f(-pd, r_ground);

    float ir = 1.0f / r, ir2 = 1.0f / r2, irg = 1.0f / r_ground, irg2 = 1.0f / (r_ground * r_ground);
    float doppler_pred = (vn*pn + ve*pe + vd*pd) * ir;

    float y[4];
    y[0] = range - r;
    y[1] = azimuth - azimuth_pred;
    y[2] = elevation - elevation_pred;
    y[3] = doppler - doppler_pred;

    /* Sparse Jacobian rows; row i has nz[i] leading non-zeros (row 3: d0..d2 then a0..a2) */
    static const uint8_t nz[4] = { 3, 2, 3, 6 };
    float h[4][6];
    float a0 = pn * ir, a1 = pe * ir, a2 = pd * ir;
    h[0][0] = a0; h[0][1] = a1; h[0][2] = a2;
    h[1][0] = -pe * irg2; h[1][1] = pn * irg2;
    float pdirg2 = pd * ir2 * irg;
    h[2][0] = pdirg2 * pn; h[2][1] = pdirg2 * pe; h[2][2] = -r_ground * ir2;
    h[3][0] = (vn - doppler_pred * a0) * ir;
    h[3][1] = (ve - doppler_pred * a1) * ir;
    h[3][2] = (vd - doppler_pred * a2) * ir;
    h[3][3] = a0; h[3][4] = a1; h[3][5] = a2;

    const float *P = ekf->P;
    float HP[4][6];
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 6; j++) {
            float s = h[i][0] * P[j];
            for (int k = 1; k < nz[i]; k++) s += h[i][k] * P[k*6+j];
            HP[i][j] = s;
        }

    float S[4][4];                       /* upper triangle, then mirrored */
    for (int i = 0; i < 4; i++)
        for (int j = i; j < 4; j++) {
            float s = HP[i][0] * h[j][0];
            for (int k = 1; k < nz[j]; k++) s += HP[i][k] * h[j][k];
            S[i][j] = s + ekf->R[i*4+j];
        }
    for (int i = 1; i < 4; i++) for (int j = 0; j < i; j++) S[i][j] = S[j][i];

    float s_gate = S[0][0] + S[1][1] + S[2][2] + S[3][3];
    if (s_gate > ekf->innovation_gate * 100.0f) { ekf->reject_count++; return -1; }

    /* in-place Gauss-Jordan inverse of S (no pivoting, same |pivot| < 1e-12 test), one reciprocal per pivot */
    float Si[4][4]; for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) Si[i][j] = S[i][j];
    for (int k = 0; k < 4; k++) {
        float p = Si[k][k];
        if (fabsf(p) < 1e-12f) { ekf->reject_count++; return -2; }
        float ip = 1.0f / p;
        for (int j = 0; j < 4; j++) if (j != k) Si[k][j] *= ip;
        for (int i = 0; i < 4; i++) {
            if (i == k) continue;
            float f = Si[i][k];
            for (int j = 0; j < 4; j++) if (j != k) Si[i][j] -= f * Si[k][j];
            Si[i][k] = -f * ip;
        }
        Si[k][k] = ip;
    }

    float K[6][4];                       /* K = P H^T S^-1 ; P H^T = (H P)^T because P is exactly symmetric */
    for (int i = 0; i < 6; i++)
        for (int m = 0; m < 4; m++) {
            float s = HP[0][i] * Si[0][m];
            for (int j = 1; j < 4; j++) s += HP[j][i] * Si[j][m];
            K[i][m] = s;
        }

    for (int i = 0; i < 6; i++) {
        float ky = K[i][0] * y[0];
        for (int m = 1; m < 4; m++) ky += K[i][m] * y[m];
        ekf->x[i] += ky;
    }

    /* Joseph form: A = (I-KH)P = P - K(HP);  P+ = A (I-KH)^T + K R K^T = A + G K^T,  G = K R - A H^T */
    float A[6][6];
    for (int i = 0; i < 6; i++)
        for (int j = 0; j < 6; j++) {
            float s = K[i][0] * HP[0][j];
            for (int m = 1; m < 4; m++) s += K[i][m] * HP[m][j];
            A[i][j] = P[i*6+j] - s;
        }
    float G[6][4];
    for (int i = 0; i < 6; i++)
        for (int m = 0; m < 4; m++) {
            float s = A[i][0] * h[m][0];
            for (int k = 1; k < nz[m]; k++) s += A[i][k] * h[m][k];
            G[i][m] = K[i][m] * ekf->R[m*5] - s;
        }
    float Pn[36];
    for (int i = 0; i < 6; i++)
        for (int j = i; j < 6; j++) {
            float s = G[i][0] * K[j][0];
            for (int m = 1; m < 4; m++) s += G[i][m] * K[j][m];
            Pn[i*6+j] = A[i][j] + s;
        }
    for (int i = 0; i < 6; i++) for (int j = i; j < 6; j++) { ekf->P[i*6+j] = Pn[i*6+j]; ekf->P[j*6+i] = Pn[i*6+j]; }

    ekf->update_count++;
    if (innovation_out) { innovation_out[0] = y[0]; innovation_out[1] = y[1]; innovation_out[2] = y[2]; innovation_out[3] = y[3]; }
    return 0;
}

void ekf_get_position(const ekf_state_t *ekf, float *pn, float *pe, float *pd) {
    *pn = ekf->x[0]; *pe = ekf->x[1]; *pd = ekf->x[2];
}

void ekf_get_velocity(const ekf_state_t *ekf, float *vn, float *ve, float *vd) {
    *vn = ekf->x[3]; *ve = ekf->x[4]; *vd = ekf->x[5];
}
