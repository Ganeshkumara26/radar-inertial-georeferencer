/*
 * arm_math.h — MINIMAL COMPATIBILITY SHIM, NOT ARM'S CMSIS-DSP LIBRARY.
 *
 * Arm's real CMSIS-DSP library is hand-tuned (SIMD/DSP-extension assembly
 * kernels on Cortex-M7) and is not redistributed here. This shim exists
 * solely so that ekf.c's non-SIMULATION_BUILD code path — which is the
 * exact code path the real target firmware compiles — can be built and
 * run on real Cortex-M7 hardware emulation (QEMU) for a genuine,
 * measured instruction/cycle comparison between the simplified and
 * Joseph-form covariance updates.
 *
 * The five functions below are transcribed VERBATIM from the reference
 * scalar-loop implementation the project's own ekf.c already provides
 * under its `#else` (SIMULATION_BUILD) branch — i.e. this is the
 * project author's own fallback algorithm, not an independent
 * reimplementation, compiled for ARM instead of x86.
 *
 * CAVEAT (stated plainly, not hidden): real CMSIS-DSP on a Cortex-M7
 * would very likely be FASTER than this scalar-loop shim, because it can
 * use the M7's dual-issue pipeline and, where applicable, DSP SIMD
 * instructions. The cycle counts measured against this shim are
 * therefore a conservative (upper-bound-ish) estimate of the ADDITIONAL
 * cost of Joseph form relative to the simplified update — the absolute
 * cycle numbers for either form should not be read as "this is what
 * Arm's optimized CMSIS-DSP would measure."
 */
#ifndef ARM_MATH_SHIM_H
#define ARM_MATH_SHIM_H

#include <stdint.h>
#include <math.h>

typedef struct { uint32_t numRows; uint32_t numCols; float *pData; } arm_matrix_instance_f32;

static inline void arm_mat_init_f32(arm_matrix_instance_f32 *S, uint32_t nr, uint32_t nc, float *p) {
    S->numRows = nr; S->numCols = nc; S->pData = p;
}

static inline int arm_mat_mult_f32(const arm_matrix_instance_f32 *A, const arm_matrix_instance_f32 *B, arm_matrix_instance_f32 *C) {
    for (uint32_t i = 0; i < A->numRows; i++)
        for (uint32_t j = 0; j < B->numCols; j++) {
            float s = 0.0f;
            for (uint32_t k = 0; k < A->numCols; k++)
                s += A->pData[i*A->numCols+k] * B->pData[k*B->numCols+j];
            C->pData[i*C->numCols+j] = s;
        }
    return 0;
}

static inline int arm_mat_inverse_f32(const arm_matrix_instance_f32 *S, arm_matrix_instance_f32 *D) {
    uint32_t n = S->numRows;
    float tmp[72]; /* 6x12 augmented, matches shipped code's fixed bound */
    for (uint32_t i = 0; i < n; i++) {
        for (uint32_t j = 0; j < n; j++) {
            tmp[i*(2*n)+j] = S->pData[i*n+j];
            tmp[i*(2*n)+j+n] = (i==j) ? 1.0f : 0.0f;
        }
    }
    for (uint32_t i = 0; i < n; i++) {
        float piv = tmp[i*(2*n)+i];
        if (fabsf(piv) < 1e-12f) return -1;
        for (uint32_t j = 0; j < 2*n; j++) tmp[i*(2*n)+j] /= piv;
        for (uint32_t k = 0; k < n; k++) {
            if (k == i) continue;
            float f = tmp[k*(2*n)+i];
            for (uint32_t j = 0; j < 2*n; j++) tmp[k*(2*n)+j] -= f*tmp[i*(2*n)+j];
        }
    }
    for (uint32_t i = 0; i < n; i++)
        for (uint32_t j = 0; j < n; j++)
            D->pData[i*n+j] = tmp[i*(2*n)+j+n];
    return 0;
}

static inline int arm_mat_add_f32(const arm_matrix_instance_f32 *A, const arm_matrix_instance_f32 *B, arm_matrix_instance_f32 *C) {
    for (uint32_t i = 0; i < A->numRows*A->numCols; i++) C->pData[i] = A->pData[i] + B->pData[i];
    return 0;
}

static inline int arm_mat_trans_f32(const arm_matrix_instance_f32 *S, arm_matrix_instance_f32 *D) {
    for (uint32_t i = 0; i < S->numRows; i++)
        for (uint32_t j = 0; j < S->numCols; j++)
            D->pData[j*D->numCols+i] = S->pData[i*S->numCols+j];
    return 0;
}

#endif
