"""Estimator back-ends with one interface: process(seq, rng, az, el, dop, rpy) -> dict(x=[6], rc, lat, lon, alt, ekf_us?)
  PyEstimator    : pure Python port of the board EKF -> offline tests, adjacent version
  NucleoEstimator: the real STM32F070RB over its ST-LINK serial port (original firmware, HIL mode). The firmware keeps its own
                    latest attitude, so we send an ATTITUDE frame (the interpolated attitude) right before each RADAR frame."""
import time
from .ekf_py import PyEKF
from .geo_py import target_wgs84
from .frames import attitude_frame, radar_frame, parse_reply

class PyEstimator:
    def __init__(self): self.ekf = PyEKF()
    def process(self, seq, rng, az, el, dop, rpy, t_ms=0):
        self.ekf.predict(0.01); rc = self.ekf.update(rng, az, el, dop)
        lat, lon, alt = target_wgs84(rng, az, el, *rpy)
        return dict(seq=seq, x=list(self.ekf.x), rc=rc, lat=lat, lon=lon, alt=alt)

class _ProcSerial:
    """serial-like wrapper around the host stand-in binary (hil_host) so the Nucleo bridge can be tested without a board"""
    def __init__(self, exe):
        import subprocess, threading, queue
        self.p = subprocess.Popen([exe], stdin=subprocess.PIPE, stdout=subprocess.PIPE, bufsize=0); self.q = queue.Queue()
        def pump():
            while True:
                d = self.p.stdout.read(4096)          # raw pipe: returns whatever is available
                if not d: return
                self.q.put(d)
        threading.Thread(target=pump, daemon=True).start()
    def write(self, b): self.p.stdin.write(b); self.p.stdin.flush()
    def read(self, n=1):
        out = b''
        try:
            while True: out += self.q.get_nowait()
        except Exception: pass
        return out
    def reset_input_buffer(self): pass
    def close(self): self.p.kill()

class NucleoEstimator:
    def __init__(self, port, baud=460800):
        if port.startswith('proc:'): self.s = _ProcSerial(port[5:])
        else:
            import serial
            self.s = serial.Serial(port, baud, timeout=0)    # non-blocking reads: a blocking read would throttle the gateway loop
        time.sleep(0.8); self.s.reset_input_buffer()
        self.s.write(b'H'); t0 = time.time(); buf = b''
        while time.time() - t0 < 3 and b'#HIL_READY' not in buf and not getattr(self.s, 'p', None): buf += self.s.read(200); time.sleep(0.01)
        if not getattr(self.s, 'p', None) and b'#HIL_READY' not in buf: raise RuntimeError('Nucleo did not enter HIL mode (is the original firmware flashed? press RESET and retry)')
        self.rx = b''; self.aseq = 0
    def send(self, seq, rng, az, el, dop, rpy, t_ms=0):
        self.s.write(attitude_frame(self.aseq, t_ms, *rpy) + radar_frame(seq, rng, az, el, dop)); self.aseq += 1
    def poll(self):
        self.rx += self.s.read(4096); out = []
        while b'\n' in self.rx:
            ln, self.rx = self.rx.split(b'\n', 1)
            r = parse_reply(ln.decode(errors='replace'))
            if r: out.append(r)
        return out
