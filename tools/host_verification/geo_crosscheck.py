"""
Independent cross-check of geo_transform.c's local ENU->WGS84 approximation
against the exact WGS84 geodesic (GeographicLib / Karney's algorithm), which
solves the direct geodesic problem to double precision (<1e-9 m accuracy).

geo_transform.c uses a first-order local-tangent-plane (flat-Earth) approximation:
  dlat = dN / (N_curv + h)
  dlon = dE / ((N_curv + h) * cos(lat))
where N_curv is the *prime-vertical* radius of curvature, applied to BOTH the
north and east displacement (the correct formula uses the *meridian* radius of
curvature M for the north/lat term, and the prime-vertical radius N for the
east/lon term -- using N for both is a documented flat-Earth-with-single-radius
simplification).
"""
import math
from geographiclib.geodesic import Geodesic

WGS84_A = 6378137.0
WGS84_F = 1.0/298.257223563
WGS84_E2 = 2*WGS84_F - WGS84_F**2

ref_lat, ref_lon = 13.0827, 80.2707

def code_approx(range_m, bearing_deg):
    """Reproduce geo_transform.c's enu_to_wgs84 for a pure-north or general bearing offset."""
    az = math.radians(bearing_deg)
    n = range_m * math.cos(az)
    e = range_m * math.sin(az)
    lat_rad = math.radians(ref_lat)
    N = WGS84_A / math.sqrt(1 - WGS84_E2 * math.sin(lat_rad)**2)
    dlat = n / N
    dlon = e / (N * math.cos(lat_rad))
    return ref_lat + math.degrees(dlat), ref_lon + math.degrees(dlon)

geod = Geodesic.WGS84
print(f"{'range_m':>8} {'bearing':>8} {'err_m (vs exact geodesic)':>26}")
for range_m in [1, 10, 50, 100, 500, 1000, 5000, 10000]:
    for bearing in [0, 45, 90]:
        lat_c, lon_c = code_approx(range_m, bearing)
        # exact geodesic destination point
        g = geod.Direct(ref_lat, ref_lon, bearing, range_m)
        lat_exact, lon_exact = g['lat2'], g['lon2']
        # error = geodesic distance between code's approx point and exact point
        inv = geod.Inverse(lat_c, lon_c, lat_exact, lon_exact)
        err_m = inv['s12']
        print(f"{range_m:8.0f} {bearing:8.0f} {err_m:26.6f}")
