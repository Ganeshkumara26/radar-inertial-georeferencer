"""Gateway (Raspberry Pi role) for the real setup:
   sensor (ESP32 or stand-in) --UDP--> [msglayer rx] -> attitude interpolation (Pixhawk) -> estimator (Nucleo F070RB or Python) -> CSV log
Examples
  all-software smoke test :  python -m uavpipe.live --sensor emulate --pixhawk synthetic --estimator py --seconds 8
  Nucleo + synthetic att  :  python -m uavpipe.live --sensor emulate --pixhawk synthetic --estimator nucleo:COM5 --hz 50
  full real setup (Pi)    :  python -m uavpipe.live --sensor udp --bind 0.0.0.0:5005 --pixhawk /dev/ttyACM0 --estimator nucleo:/dev/ttyACM1 --hz 50
Clock handling: sender/Pixhawk clocks are mapped to the gateway clock with a minimum-delay filter (offset = min(rx - t_gen)); reported ages are
therefore 'excess over the minimum observed delay'; the alignment uncertainty is about the difference between the two paths' minimum delays (ms)."""
import argparse, csv, os, socket, struct, threading, time, statistics as st
from . import msglayer as ml
from .attitude import AttitudeBuffer, SyntheticAttitude
from .estimator import PyEstimator

mono = lambda: int(time.monotonic() * 1e6)
def pct(v, p): v = sorted(v); return v[min(len(v) - 1, int(p / 100 * len(v)))] if v else float('nan')

def main(argv=None):
    ap = argparse.ArgumentParser(); ap.add_argument('--sensor', default='emulate', choices=['emulate', 'udp']); ap.add_argument('--bind', default='127.0.0.1:5005')
    ap.add_argument('--pixhawk', default='synthetic'); ap.add_argument('--pix-baud', type=int, default=115200); ap.add_argument('--att-hz', type=int, default=100)
    ap.add_argument('--estimator', default='py'); ap.add_argument('--hz', type=int, default=50); ap.add_argument('--policy', default='deadline', choices=['raw', 'retx', 'deadline'])
    ap.add_argument('--seconds', type=float, default=8); ap.add_argument('--out', default='results/live'); ap.add_argument('--hold-timeout-ms', type=float, default=30)
    a = ap.parse_args(argv); os.makedirs(a.out, exist_ok=True)
    host, port = a.bind.split(':'); sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); sock.bind((host, int(port))); sock.setblocking(False)
    if a.sensor == 'emulate':
        from . import sensor_node
        threading.Thread(target=sensor_node.run, kwargs=dict(to=f'127.0.0.1:{port}', hz=a.hz, policy=a.policy, loops=max(1, int(a.seconds * a.hz / 200))), daemon=True).start()
    est = PyEstimator() if a.estimator == 'py' else None
    if est is None:
        from .estimator import NucleoEstimator; est = NucleoEstimator(a.estimator.split(':', 1)[1] if a.estimator.startswith('nucleo:') else a.estimator)
    pix = None; synth = SyntheticAttitude(); abuf = AttitudeBuffer(512); off_att = None; next_syn = mono()
    if a.pixhawk != 'synthetic':
        from .attitude import PixhawkAttitude; pix = PixhawkAttitude(a.pixhawk, a.pix_baud, a.att_hz)
    rcv = ml.Receiver(ordered=(a.policy != 'raw')); off_rad = None; pending = []; inflight = {}; rows = []; t_end = mono() + int((a.seconds + 1.5) * 1e6)
    async_est = hasattr(est, 'send'); asent = 0
    def finish(seq, rx_us, t_tx, out, rpy, age):
        done = mono(); rows.append([seq, rx_us, age, done - rx_us] + [f'{v:.6f}' for v in out['x']] + [out.get('rc', 0), *[f'{v:.5f}' for v in rpy]])
    while mono() < t_end:
        now = mono()
        if pix:
            for t_us, rpy, rx_s in pix.poll():
                d = int(rx_s * 1e6) - t_us; off_att = d if off_att is None else min(off_att, d); abuf.add(t_us + off_att, rpy)
        elif now >= next_syn: abuf.add(next_syn, synth.sample(next_syn * 1e-6 - 0)); next_syn += 1000000 // a.att_hz
        try:
            while True:
                data, _ = sock.recvfrom(2048); rx = mono(); u = ml.unpack(data)
                if u and u[0] != ml.ACK: d_ = rx - u[2]; off_rad = d_ if off_rad is None else min(off_rad, d_)
                rcv.off = -(off_rad or 0); dl, replies = rcv.on_frame(rx, data)
                for r in replies: sock.sendto(r, _)
                if dl:
                    mid, pl, age = dl; seq, rng, az, el, dop = struct.unpack('<I4f', pl); pending.append((rx, u[2] + off_rad, seq, (rng, az, el, dop), age))
        except BlockingIOError: pass
        keep = []
        for rx, t_rad, seq, m, age in pending:                               # wait for an attitude sample newer than the radar time (or timeout)
            if abuf.t and (abuf.t[-1] >= t_rad or now - rx > a.hold_timeout_ms * 1000):
                rpy = abuf.interp(t_rad) or (0.0, 0.0, 0.0)
                if async_est: est.send(seq, *m, rpy, t_ms=(t_rad // 1000)); inflight[seq] = (rx, rpy, age)
                else: finish(seq, rx, now, est.process(seq, *m, rpy), rpy, age)
            else: keep.append((rx, t_rad, seq, m, age))
        pending = keep
        if async_est:
            for r in est.poll():
                if r['seq'] in inflight: rx, rpy, age = inflight.pop(r['seq']); finish(r['seq'], rx, 0, r, rpy, age)
        time.sleep(0.0003)
    hdr = ['seq', 'rx_us', 'age_us', 'latency_us', 'x0', 'x1', 'x2', 'x3', 'x4', 'x5', 'rc', 'roll', 'pitch', 'yaw']
    with open(os.path.join(a.out, 'live_log.csv'), 'w', newline='') as f: w = csv.writer(f); w.writerow(hdr); w.writerows(rows)
    lat = [r[3] for r in rows]; ages = [r[2] for r in rows]
    print(f"policy={a.policy} estimator={a.estimator} pixhawk={a.pixhawk}\n results={len(rows)} rx_stats={rcv.stats}\n"
          f" gateway latency (rx -> estimator result) us: p50={pct(lat,50):.0f} p99={pct(lat,99):.0f} max={max(lat) if lat else float('nan'):.0f}\n"
          f" age at delivery (excess over min delay) us: p50={pct(ages,50):.0f} p99={pct(ages,99):.0f} max={max(ages) if ages else float('nan'):.0f}\n log: {a.out}/live_log.csv")
    return rows
if __name__ == '__main__': main()
