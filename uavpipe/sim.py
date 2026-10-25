"""Offline full-pipeline sweep: sensor node (ESP32 role) -> link -> gateway (Pi role) -> estimator (STM32 role).
Everything here is an offline sweep for parameter exploration. Run: python -m uavpipe.sim --out results"""
import argparse, csv, os, struct, statistics as st
from . import msglayer as ml
from .link import EmulatedLink
from .estimator import PyEstimator
from .frames import load_stim, load_truth
from .attitude import SyntheticAttitude, AttitudeBuffer

HERE = os.path.dirname(os.path.abspath(__file__)); DATA = os.path.join(HERE, '..', 'data')
MSG_RADAR = 1
JIT = (0, 300, -200, 500, 100, -400, 250, -100)

def pct(v, p):
    v = sorted(v); return v[min(len(v) - 1, int(p / 100 * len(v)))] if v else float('nan')

def run_once(policy, loss, seed, delay_us=5000, jitter_us=2000, burst=0.0, hz=50, n=200, deadline_us=30000, R=3, rto_us=20000, stim=None, truth=None):
    stim = stim or load_stim(os.path.join(DATA, 'stimulus.h')); truth = truth or load_truth(os.path.join(DATA, 'truth.csv'))
    up = EmulatedLink(delay_us, jitter_us, loss, burst, seed=seed); down = EmulatedLink(delay_us, jitter_us, loss, burst, seed=seed + 10000)
    snd = ml.Sender(rto_us=rto_us, max_retries=R, queue_len=64); rcv = ml.Receiver(ordered=(policy != 'raw'))
    est = PyEstimator(); att = SyntheticAttitude(); abuf = AttitudeBuffer(256)
    period = int(1e6 / hz); end = n * period + 400000; sent = 0
    ages, errs, hold_err, int_err = [], [], [], []; delivered = 0; ak = 0
    for now in range(0, end, 1000):                                   # 1 ms ticks
        while abuf.t == [] or abuf.t[-1] < now:                       # Pixhawk-like attitude, 100 Hz, 3.7 ms phase offset, +/-0.5 ms timestamp jitter
            t = 10000 * ak + 3700 + JIT[ak & 7]; abuf.add(t, att.sample(t * 1e-6)); ak += 1
        if sent < n and now >= sent * period:
            r = stim[sent]; pl = struct.pack('<I4f', sent, *r); sent += 1
            if policy == 'retx': frames = snd.command(now, MSG_RADAR, pl)
            else: frames = [snd.telemetry(now, MSG_RADAR, pl, deadline_us if policy == 'deadline' else 0)]
            for f in frames: up.send(now, f)
        for f in snd.tick(now): up.send(now, f)
        for _, f in up.poll(now):
            d, replies = rcv.on_frame(now, f)
            for rp in replies: down.send(now, rp)
            if d:
                mid, pl, age = d; seq, rng, az, el, dop = struct.unpack('<I4f', pl); t_radar = seq * period
                rpy = abuf.interp(t_radar); rpy = rpy or (0, 0, 0)
                out = est.process(seq, rng, az, el, dop, rpy); delivered += 1; ages.append(age)
                errs.append(sum((out['x'][k] - truth[seq][k]) ** 2 for k in range(3)) ** .5)
                tr = att.sample(t_radar * 1e-6)[2]; hh = abuf.hold(t_radar)
                int_err.append(abs(rpy[2] - tr)); hold_err.append(abs(hh[2] - tr) if hh else 0)
        for _, f in down.poll(now):
            u = ml.unpack(f)
            if u and u[0] == ml.ACK:
                for fr in snd.on_ack(now, struct.unpack('<H', u[5])[0]): up.send(now, fr)
    return dict(policy=policy, loss=loss, seed=seed, sent=n, delivered=delivered, ratio=delivered / n,
                age_p50=pct(ages, 50), age_p99=pct(ages, 99), age_max=max(ages) if ages else float('nan'),
                err_mean=st.mean(errs) if errs else float('nan'), err_final=errs[-1] if errs else float('nan'),
                yaw_err_interp=max(int_err) if int_err else float('nan'), yaw_err_hold=max(hold_err) if hold_err else float('nan'),
                retx=snd.stats['retx'], failed=snd.stats['failed'], stale=rcv.stats['stale'], old=rcv.stats['old'])

def main():
    ap = argparse.ArgumentParser(); ap.add_argument('--out', default='results'); ap.add_argument('--seeds', type=int, default=20)
    ap.add_argument('--hz', type=int, default=50); a = ap.parse_args(); os.makedirs(a.out, exist_ok=True)
    stim = load_stim(os.path.join(DATA, 'stimulus.h')); truth = load_truth(os.path.join(DATA, 'truth.csv'))
    rows = []; profiles = [(f'{nm}', p, b, d, j) for nm, b, d, j in (('uniform loss, 5+-2 ms', 0.0, 5000, 2000),) for p in (0.0, 0.05, 0.10, 0.20, 0.30)]
    profiles += [('burst loss (mean 5), 5+-2 ms', p, 5.0, 5000, 2000) for p in (0.10, 0.20)]
    profiles += [('high jitter, 20+-20 ms', p, 0.0, 20000, 20000) for p in (0.0, 0.05, 0.20)]
    for name, p, burst, dly, jit in profiles:
        for pol in ('raw', 'retx', 'deadline'):
            rs = [run_once(pol, p, s, delay_us=dly, jitter_us=jit, burst=burst, hz=a.hz, stim=stim, truth=truth) for s in range(1, a.seeds + 1)]
            m = lambda k: st.mean(r[k] for r in rs if r[k] == r[k])
            rows.append(dict(profile=name, loss=p, policy=pol, seeds=a.seeds, ratio=m('ratio'), age_p50_ms=m('age_p50') / 1e3, age_p99_ms=m('age_p99') / 1e3,
                             age_max_ms=max(r['age_max'] for r in rs) / 1e3, err_mean_m=m('err_mean'), err_final_m=m('err_final'), retx=m('retx'), failed=m('failed')))
    with open(os.path.join(a.out, 'sweep.csv'), 'w', newline='') as f:
        w = csv.DictWriter(f, rows[0].keys()); w.writeheader(); w.writerows(rows)
    L = ['# Offline sweep', '', f'{a.seeds} seeds per cell, {a.hz} Hz radar, one-way delay/jitter per profile, deadline 30 ms, R=3, RTO 20 ms. Estimator = Python port of the board EKF.', '',
         '| link profile | loss | policy | delivered | age p50 / p99 / max (ms) | mean pos. error (m) | final err (m) |', '|---|---|---|---|---|---|---|']
    for r in rows: L.append(f"| {r['profile']} | {r['loss']:.2f} | {r['policy']} | {100*r['ratio']:.1f}% | {r['age_p50_ms']:.1f} / {r['age_p99_ms']:.1f} / {r['age_max_ms']:.1f} | {r['err_mean_m']:.3f} | {r['err_final_m']:.3f} |")
    open(os.path.join(a.out, 'sweep.md'), 'w').write('\n'.join(L) + '\n'); print('\n'.join(L))
if __name__ == '__main__': main()
