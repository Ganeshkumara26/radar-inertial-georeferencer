# Devlog: Case Study 05 — Fixed-Point EKF & The Matrix Aliasing Catastrophe

## What I'm Trying to Do

The core of the coprocessor: a 6-state Extended Kalman Filter that fuses the drone's inertial state (from MAVLink) with radar range-Doppler measurements to produce drift-compensated target positions. State vector: `[pn, pe, pd, vn, ve, vd]^T`. Measurement: `[range, azimuth, elevation, doppler]` from radar.

I use CMSIS-DSP `arm_mat_*` functions for matrix operations on the Cortex-M7 FPU.

---

## Attempt 1: The EKF Diverges Immediately

I implemented the standard EKF predict-update cycle. On the first radar measurement, the state vector exploded to NaN.

### The Output (NaN Propagation)
```text
[EKF] After update: pn=nan, pe=nan, pd=nan
[EKF] P diagonal: nan, nan, nan, nan, nan, nan
```

### My Mistake & Root Cause Analysis — Bug #1: Static Buffer Aliasing

I used a single `tmp6x6[36]` buffer for all intermediate 6×6 matrix operations:
```c
static float tmp6x6[36];
arm_mat_init_f32(&matFT, 6, 6, tmp6x6);  // F transpose
arm_mat_init_f32(&matFP, 6, 6, tmp6x6);  // F*P — overwrites F^T!
arm_mat_init_f32(&matFPFT, 6, 6, tmp6x6); // F*P*F^T — reads FP and FT, both in same buffer!
```

CMSIS-DSP's `arm_mat_mult_f32` writes results sequentially. When both source and destination share a buffer, the function **overwrites its own inputs mid-computation**. The result is garbage.

### The Fix

Distinct scratchpad buffers for every intermediate:
```c
static float scratch_F[36];      // State transition matrix
static float scratch_FT[36];     // F transpose
static float scratch_FP[36];     // F*P product
static float scratch_FPFt[36];   // F*P*F^T product
static float scratch_Pnew[36];   // Updated covariance (separate from P!)
```

This is the most expensive lesson in embedded EKF implementation: **you cannot alias CMSIS-DSP matrix buffers**. Each intermediate product needs its own memory.

---

## Attempt 2: Range Prediction Off by Orders of Magnitude

After fixing aliasing, the EKF still diverged — but more slowly.

### The Output (Innovation Saturation)
```text
[EKF] Innovation: y[0] = -90.0  (range residual)
[EKF] Innovation: y[1] = 0.05   (azimuth residual)
```

A range innovation of -90 meters means the predicted range is 90 meters off. For a target at 10 meters, this is nonsensical.

### My Mistake & Root Cause Analysis — Bug #2: Missing sqrtf

```c
float r2 = pn*pn + pe*pe + pd*pd;
float r = (r2 > 0.0001f) ? r2 : 0.0001f;  // r2 is distance SQUARED
float range_pred = r;  // BUG: assigning r^2, not r
```

`r2` is in m². `range_pred` should be in meters. I forgot `sqrtf()`.

### The Fix

```c
float r2 = pn*pn + pe*pe + pd*pd;
float range_pred = sqrtf((r2 > 1e-6f) ? r2 : 1e-6f);
```

---

## Attempt 3: Azimuth/Elevation Predictions in Meters, Not Radians

After fixing sqrtf, the angle innovations were still wrong.

### The Output (Unit Mismatch)
```text
[EKF] y[1] = 5.7   (azimuth: measured 0.1 rad, predicted 5.8 rad)
[EKF] y[2] = -3.2  (elevation: measured -0.2 rad, predicted -3.0 rad)
```

### My Mistake & Root Cause Analysis — Bug #3: Cartesian Instead of Polar

```c
float azimuth_pred = pe;    // pe is in METERS, not radians!
float elevation_pred = pd;  // pd is in METERS, not radians!
```

I lazily substituted position coordinates for the actual trigonometric measurement model. The EKF measurement function `h(x)` must predict what the radar **actually measures** — angles in radians, not positions in meters.

### The Fix

```c
float r_ground = sqrtf((pn*pn + pe*pe) > 1e-6f ? (pn*pn + pe*pe) : 1e-6f);
float azimuth_pred = atan2f(pe, pn);
float elevation_pred = atan2f(-pd, r_ground);
```

---

## Attempt 4: Jacobian H Has Zero Rows — EKF Never Updates Angles

With correct predictions, the EKF still didn't correct azimuth or elevation.

### The Output (Stagnant Angle Estimates)
```text
[EKF] After 100 updates: azimuth error = 0.5 rad (unchanged!)
[EKF] After 100 updates: range error = 0.01 rad (converged)
```

### My Mistake & Root Cause Analysis — Bug #4: Incomplete Jacobian

I only populated Row 0 (range) and Row 3 (doppler) of the 4×6 Jacobian `H`. Rows 1 and 2 (azimuth and elevation) were left as zeros. With zero rows in H, the Kalman gain for those measurements is zero, and the state never updates based on angle measurements.

Additionally, Row 3 had the Doppler partial derivatives in the wrong columns — indices 18, 19, 20 (position derivatives) instead of 21, 22, 23 (velocity derivatives).

### The Fix

Full Jacobian population:
```c
// Row 0: dRange/dx = [pn/r, pe/r, pd/r, 0, 0, 0]
scratch_H[0] = pn/r; scratch_H[1] = pe/r; scratch_H[2] = pd/r;

// Row 1: dAzimuth/dx = [-pe/rg², pn/rg², 0, 0, 0, 0]
scratch_H[6] = -pe/rg2; scratch_H[7] = pn/rg2;

// Row 2: dElevation/dx = [pd·pn/(r²·rg), pd·pe/(r²·rg), -rg/r², 0, 0, 0]
scratch_H[12] = (pd*pn)/r2rg; scratch_H[13] = (pd*pe)/r2rg; scratch_H[14] = -rg/r2;

// Row 3: dDoppler/dx = [..., pn/r, pe/r, pd/r]  (velocity partials at indices 21,22,23)
scratch_H[21] = pn/r; scratch_H[22] = pe/r; scratch_H[23] = pd/r;
```

---

## Attempt 5: Covariance Matrix Loses Symmetry

After many update cycles, the EKF became unstable again.

### The Output (Negative Variance)
```text
[EKF] P[0,0] = -0.003  ← variance cannot be negative!
```

### My Mistake & Root Cause Analysis — Bug #5: Floating-Point Truncation

The Joseph form covariance update `P = (I - KH)P` loses symmetry on single-precision ARM due to floating-point truncation. Once asymmetry creeps in, the diagonal can go negative, and the EKF diverges.

### The Fix

Force symmetry after each update:
```c
for (int i = 0; i < 6; i++)
    for (int j = i+1; j < 6; j++) {
        float avg = 0.5f * (P[i*6+j] + P[j*6+i]);
        P[i*6+j] = avg;
        P[j*6+i] = avg;
    }
```

---

## Final Result

The corrected EKF:
- Distinct scratchpad buffers for all matrix operations (no aliasing)
- Correct measurement model with sqrtf and atan2f
- Full 4×6 Jacobian with proper row/column indexing
- Covariance symmetry enforcement after each update
- Innovation gating to reject outlier measurements

Ready for CS06: Coordinate transformation engine.
