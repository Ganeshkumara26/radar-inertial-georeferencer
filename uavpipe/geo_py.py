"""Python port of geo_transform.c (polar radar -> ENU -> WGS84) for the offline estimator."""
import math
A, F = 6378137.0, 1.0 / 298.257223563; E2 = 2 * F - F * F

def euler_to_quat(roll, pitch, yaw):
    cr, sr, cp, sp, cy, sy = (f(a * 0.5) for a in (roll, pitch, yaw) for f in (math.cos, math.sin))
    return (cr * cp * cy + sr * sp * sy, sr * cp * cy - cr * sp * sy, cr * sp * cy + sr * cp * sy, cr * cp * sy - sr * sp * cy)

def _rot(q, v):
    w, x, y, z = q; vx, vy, vz = v
    tx, ty, tz = 2 * (y * vz - z * vy), 2 * (z * vx - x * vz), 2 * (x * vy - y * vx)
    return (vx + w * tx + (y * tz - z * ty), vy + w * ty + (z * tx - x * tz), vz + w * tz + (x * ty - y * tx))

def target_wgs84(rng, az, el, roll, pitch, yaw, lat0=13.0827, lon0=80.2707, alt0=0.0):
    ce, se, ca, sa = math.cos(el), math.sin(el), math.cos(az), math.sin(az)
    xb, yb, zb = _rot(euler_to_quat(roll, pitch, yaw), (rng * ce * ca, rng * ce * sa, -rng * se))
    ch, sh = math.cos(yaw), math.sin(yaw)
    e, n, u = xb * sh + yb * ch, xb * ch - yb * sh, -zb
    lat = math.radians(lat0); N = A / math.sqrt(1 - E2 * math.sin(lat) ** 2)
    return lat0 + math.degrees(n / (N + alt0)), lon0 + math.degrees(e / ((N + alt0) * math.cos(lat))), alt0 + u
