"""Python port of the board EKF (6 states [pn,pe,pd,vn,ve,vd], radar [range, az, el, doppler]) incl. Joseph form, gate and the
same fixed-dt-per-frame behaviour as the C code. float64 numpy: matches the C float32 golden to ~1e-3 (see tests), not bit-for-bit."""
import numpy as np

class PyEKF:
    def __init__(self, q=0.1, r_range=1.0, r_angle=0.01, r_dop=0.5, gate=9.0):
        self.x = np.array([2.0, 0.5, 8.0, 3.0, 0.0, 0.5])
        self.P = np.diag([100.0] * 3 + [25.0] * 3)
        self.Q = np.diag([q] * 3 + [q * 0.1] * 3); self.R = np.diag([r_range, r_angle, r_angle, r_dop]); self.gate = gate
        self.updates = self.rejects = 0
    def predict(self, dt=0.01):
        F = np.eye(6); F[0, 3] = F[1, 4] = F[2, 5] = dt
        self.x = F @ self.x; self.P = F @ self.P @ F.T + self.Q
    def update(self, rng, az, el, dop):
        pn, pe, pd, vn, ve, vd = self.x
        r2 = max(pn * pn + pe * pe + pd * pd, 1e-6); r = np.sqrt(r2); rg = np.sqrt(max(pn * pn + pe * pe, 1e-6))
        dp = (vn * pn + ve * pe + vd * pd) / r
        y = np.array([rng - r, az - np.arctan2(pe, pn), el - np.arctan2(-pd, rg), dop - dp])
        H = np.zeros((4, 6)); H[0, :3] = [pn / r, pe / r, pd / r]
        H[1, :2] = [-pe / rg ** 2, pn / rg ** 2]
        H[2, :3] = [pd * pn / (r2 * rg), pd * pe / (r2 * rg), -rg / r2]
        H[3, :3] = [(vn - dp * pn / r) / r, (ve - dp * pe / r) / r, (vd - dp * pd / r) / r]; H[3, 3:] = [pn / r, pe / r, pd / r]
        S = H @ self.P @ H.T + self.R
        if np.trace(S) > self.gate * 100.0: self.rejects += 1; return -1
        K = self.P @ H.T @ np.linalg.inv(S); self.x = self.x + K @ y
        M = np.eye(6) - K @ H; self.P = M @ self.P @ M.T + K @ self.R @ K.T; self.P = 0.5 * (self.P + self.P.T)
        self.updates += 1; return 0
