# Devlog: Case Study 06 — Coordinate Transform & The Gimbal Lock Near Equator

## What I'm Trying to Do

Convert radar polar coordinates (range, azimuth, elevation) into global WGS84 (lat/lon/alt) coordinates. The pipeline:
1. Polar → Radar Cartesian (forward-right-down frame)
2. Radar Cartesian → Body frame (apply attitude quaternion)
3. Body frame → ENU (apply heading rotation)
4. ENU → WGS84 (apply reference position)

---

## Attempt 1: Quaternion Rotation Direction

I implemented quaternion rotation using the formula `v' = q * v * q^-1`.

### The Output (Rotated in Wrong Direction)
```text
[GEO] Drone rolled right by 0.1 rad
[GEO] Target should shift LEFT in ENU
[GEO] Target shifted RIGHT in ENU  ← wrong direction!
```

### My Mistake & Root Cause Analysis

I used the **conjugate** of the attitude quaternion. The attitude quaternion `q` represents the rotation from body to NED frame. To rotate a vector from body to NED, you use `q * v * q_conj`. But I was using `q_conj * v * q`, which rotates in the opposite direction.

### The Fix

Use the correct quaternion rotation direction. For body-to-ENU:
```c
// q rotates body→NED, so use q (not q_conj) for forward rotation
quat_rotate_vector(q, xr, yr, zr, &xn, &yn, &zn);
```

---

## Attempt 2: ENU Conversion Degrades Near Equator

The ENU-to-WGS84 conversion uses:
```c
dlon = e / ((N + alt) * cos(lat))
```

### The Output (Longitude Jitter)
```text
[GEO] At lat=0.001°: dlon = 0.0001° (noisy)
[GEO] At lat=45°:     dlon = 0.00001° (stable)
```

### My Mistake & Root Cause Analysis

Near the equator, `cos(lat)` approaches 1.0, which is fine. But the issue was that I was computing `N` (radius of curvature) using `sin(lat)` which loses precision for small latitudes in single-precision float. The `N` value was slightly off, causing the longitude conversion to accumulate error.

### The Fix

Use `double` for the ENU-to-WGS84 conversion. The Cortex-M7 FPU is single-precision only, but `double` is emulated in software and only runs at georeferencing time (not in the EKF loop). The precision gain is worth the cost:

```c
double lat_rad = ref->ref_latitude * M_PI / 180.0;
double N = WGS84_A / sqrt(1.0 - WGS84_E2 * sin(lat_rad) * sin(lat_rad));
double dlon = (double)enu->e / ((N + (double)ref->ref_altitude) * cos(lat_rad));
```

---

## Attempt 3: Velocity Vector Not Transformed

I transformed position correctly but forgot to also transform the Doppler velocity vector.

### The Output (Velocity in Wrong Frame)
```text
[GEO] Target at 10m range, drone moving 3 m/s North
[GEO] Reported target velocity: 0 m/s (should be -3 m/s relative)
```

### My Mistake & Root Cause Analysis

The Doppler measurement gives radial velocity in the radar frame. I transformed the position but left the velocity in the radar frame. The EKF needs velocity in ENU frame to properly compensate for drone motion.

### The Fix

Apply the same quaternion rotation to the velocity vector:
```c
float vxr = vr * cos_el * cos_az;
float vyr = vr * cos_el * sin_az;
float vzr = -vr * sin_el;
quat_rotate_vector(attitude, vxr, vyr, vzr, &vxb, &vyb, &vzb);
enu->vel_e = vxb * sh + yb * ch;
enu->vel_n = vxb * ch - yb * sh;
enu->vel_u = -vzb;
```

---

## Final Result

The coordinate transform pipeline:
- Correct quaternion rotation direction (body→NED, not NED→body)
- Double-precision ENU-to-WGS84 for numerical stability
- Velocity vector transformed alongside position
- Gimbal lock avoided by using quaternions throughout (no Euler angles in the transform chain)

Ready for CS07: Deterministic output streaming.
