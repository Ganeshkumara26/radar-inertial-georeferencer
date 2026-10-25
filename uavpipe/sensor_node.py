"""ESP32 stand-in (runs on any PC/Pi). Sends radar detections as msglayer frames over UDP at --hz, t_gen = local monotonic us.
Wire-identical to what the ESP32 sketch sends (esp32/ + c/msglayer_core.h are cross-checked against uavpipe.msglayer in tests).
  python -m uavpipe.sensor_node --to 192.168.1.20:5005 --hz 50 --policy deadline"""
import argparse, os, socket, struct, time
from . import msglayer as ml
from .frames import load_stim
DATA = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'data')

def run(to, hz=50, policy='deadline', deadline_us=30000, n=200, loops=1, sock=None, R=3, rto_us=20000, t0=None):
    host, port = to.split(':'); peer = (host, int(port)); stim = load_stim(os.path.join(DATA, 'stimulus.h'))[:n]
    s = sock or socket.socket(socket.AF_INET, socket.SOCK_DGRAM); s.setblocking(False)
    snd = ml.Sender(rto_us=rto_us, max_retries=R, queue_len=64); mono = lambda: int(time.monotonic() * 1e6)
    period = int(1e6 / hz); start = mono() + 200000; k = 0; total = n * loops
    while True:
        now = mono()
        if k < total and now >= start + k * period:
            seq = k % n; pl = struct.pack('<I4f', seq, *stim[seq]); k += 1
            frames = snd.command(now, 1, pl) if policy == 'retx' else [snd.telemetry(now, 1, pl, deadline_us if policy == 'deadline' else 0)]
            for f in frames: s.sendto(f, peer)
        for f in snd.tick(now): s.sendto(f, peer)
        try:
            while True:
                d, _ = s.recvfrom(2048); u = ml.unpack(d)
                if u and u[0] == ml.ACK:
                    for f in snd.on_ack(now, struct.unpack('<H', u[5])[0]): s.sendto(f, peer)
        except BlockingIOError: pass
        if k >= total and now > start + total * period + 500000: return snd.stats
        time.sleep(0.0005)

if __name__ == '__main__':
    ap = argparse.ArgumentParser(); ap.add_argument('--to', required=True); ap.add_argument('--hz', type=int, default=50)
    ap.add_argument('--policy', default='deadline', choices=['raw', 'retx', 'deadline']); ap.add_argument('--loops', type=int, default=1)
    a = ap.parse_args(); print(run(a.to, a.hz, a.policy, loops=a.loops))
