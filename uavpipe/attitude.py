"""Attitude sources + interpolation. SyntheticAttitude = same analytic yaw trajectory as the board's alignment test.
PixhawkAttitude = real MAVLink ATTITUDE stream (pymavlink) from a Pixhawk 2.4.6 (ArduPilot/PX4)."""
import math, time, bisect

class SyntheticAttitude:
    """roll/pitch constant, yaw = 0.5*sin(pi t). t in seconds of *source* clock."""
    def sample(self, t): return (0.1, 0.05, 0.5 * math.sin(math.pi * t))

class AttitudeBuffer:
    """timestamped samples (t_us on a common clock); interp() = linear (yaw wrap-safe), hold() = zero-order hold."""
    def __init__(self, n=64): self.t = []; self.v = []; self.n = n
    def add(self, t_us, rpy):
        if self.t and t_us <= self.t[-1]: return
        self.t.append(t_us); self.v.append(rpy)
        if len(self.t) > self.n: self.t.pop(0); self.v.pop(0)
    def hold(self, tq):
        i = bisect.bisect_right(self.t, tq) - 1
        return self.v[i] if i >= 0 else None
    def interp(self, tq):
        i = bisect.bisect_right(self.t, tq) - 1
        if i < 0: return None
        if i >= len(self.t) - 1: return self.v[i]           # no newer sample yet: hold
        a = (tq - self.t[i]) / (self.t[i + 1] - self.t[i]); y0, y1 = self.v[i], self.v[i + 1]
        out = []
        for k in range(3):
            d = y1[k] - y0[k]; d = (d + math.pi) % (2 * math.pi) - math.pi      # shortest angle
            out.append(y0[k] + a * d)
        return tuple(out)

class PixhawkAttitude:
    """Real Pixhawk. port e.g. 'COM7' / '/dev/ttyACM0' (USB) or a TELEM UART via FTDI. Requests ATTITUDE at rate_hz with
    MAV_CMD_SET_MESSAGE_INTERVAL (511), which ArduPilot 4.x and PX4 both accept. poll() returns list of (t_boot_us, (r,p,y), rx_monotonic_s)."""
    def __init__(self, port, baud=115200, rate_hz=50):
        from pymavlink import mavutil
        self.m = mavutil.mavlink_connection(port, baud=baud, dialect='common'); self.rate = rate_hz
        self.m.wait_heartbeat(timeout=10)
        self.m.mav.command_long_send(self.m.target_system, self.m.target_component, 511, 0, 30, int(1e6 / rate_hz), 0, 0, 0, 0, 0)
    def poll(self):
        out = []
        while True:
            msg = self.m.recv_match(type='ATTITUDE', blocking=False)
            if msg is None: return out
            out.append((msg.time_boot_ms * 1000, (msg.roll, msg.pitch, msg.yaw), time.monotonic()))
