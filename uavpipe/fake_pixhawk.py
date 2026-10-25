"""Simulated Pixhawk: real MAVLink v2 HEARTBEAT + ATTITUDE (yaw = 0.5*sin(pi*t_boot)) over UDP, honours MAV_CMD_SET_MESSAGE_INTERVAL for msg 30.
Lets you test the whole Pixhawk code path (pymavlink, interval request, clock mapping) before the drone is connected.
  terminal 1: python -m uavpipe.fake_pixhawk --to 127.0.0.1:14550
  terminal 2: python -m uavpipe.live --pixhawk udpin:0.0.0.0:14550 ..."""
import argparse, math, time

def run(to='127.0.0.1:14550', rate_hz=50, seconds=15, boot_offset_ms=123456):
    from pymavlink import mavutil
    m = mavutil.mavlink_connection(f'udpout:{to}', source_system=1, source_component=1, dialect='common')
    t0 = time.monotonic(); last_hb = 0; nxt = t0; sent = 0
    while time.monotonic() - t0 < seconds:
        now = time.monotonic()
        if now - last_hb >= 1: m.mav.heartbeat_send(2, 3, 0, 0, 4); last_hb = now      # MAV_TYPE_QUADROTOR, MAV_AUTOPILOT_ARDUPILOTMEGA
        msg = m.recv_match(type='COMMAND_LONG', blocking=False)
        if msg is not None and msg.command == 511 and int(msg.param1) == 30 and msg.param2 > 0: rate_hz = 1e6 / msg.param2
        if now >= nxt:
            tb = now - t0; m.mav.attitude_send(int(tb * 1000) + boot_offset_ms, 0.1, 0.05, 0.5 * math.sin(math.pi * tb), 0, 0, 0); sent += 1; nxt += 1.0 / rate_hz
        time.sleep(0.0005)
    return sent

if __name__ == '__main__':
    ap = argparse.ArgumentParser(); ap.add_argument('--to', default='127.0.0.1:14550'); ap.add_argument('--rate', type=float, default=50); ap.add_argument('--seconds', type=float, default=15)
    a = ap.parse_args(); print('sent', run(a.to, a.rate, a.seconds))
