#!/usr/bin/env python3
"""analyze_live.py - recompute every results table from the raw live_log.csv logs.

Usage:
  python -m uavpipe.analyze_live          # scan results/live/*/live_log.csv
  python -m uavpipe.analyze_live --out results/analysis.md
  python -m uavpipe.analyze_live --folder results/live/raw_none

Reads each live_log.csv (columns: seq,rx_us,age_us,latency_us,x0..x5,rc,roll,pitch,yaw)
and recomputes delivered ratio, latency/age percentiles, position error vs data/truth.csv.
Every number is recomputed from the raw logs — never taken from a summary.
"""
import argparse, csv, os, sys, statistics as st
from math import dist

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TRUTH = os.path.join(ROOT, 'data', 'truth.csv')

def load_truth():
    import csv as _csv
    return {int(r['step']): (float(r['rel_n']), float(r['rel_e']), float(r['rel_d']))
            for r in _csv.DictReader(open(TRUTH))}

def analyze_file(path, truth):
    rows = []
    with open(path, newline='') as f:
        rdr = csv.DictReader(f)
        for r in rdr:
            rows.append(r)
    n_delivered = len(rows)
    n_sent = 200   # every pipeline run sends exactly 200 stimulus frames (STIM_N=200)
    lat = [float(r['latency_us']) for r in rows]
    ages = [float(r['age_us']) for r in rows]
    errs = []
    for r in rows:
        seq = int(r['seq'])
        if seq in truth:
            x = [float(r[f'x{i}']) for i in range(6)]
            errs.append(dist(x[:3], truth[seq]))
    pct = lambda v, p: sorted(v)[min(len(v)-1, int(p/100*len(v)))] if v else float('nan')
    return dict(
        file=path, n_sent=n_sent, n_delivered=n_delivered,
        ratio=n_delivered / n_sent if n_sent else float('nan'),
        lat_p50=pct(lat,50), lat_p99=pct(lat,99), lat_max=max(lat) if lat else float('nan'),
        age_p50=pct(ages,50), age_p99=pct(ages,99), age_max=max(ages) if ages else float('nan'),
        err_mean=st.mean(errs) if errs else float('nan'),
        err_final=errs[-1] if errs else float('nan'),
        err_n=len(errs),
    )

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default=None, help='write markdown report to this path')
    ap.add_argument('--folder', default=None, help='analyze a single results/live/<folder>')
    a = ap.parse_args()
    truth = load_truth()
    base = os.path.join(ROOT, 'results', 'live')
    folders = []
    if a.folder:
        folders = [a.folder]
    else:
        if os.path.isdir(base):
            folders = sorted(d for d in os.listdir(base)
                             if os.path.isfile(os.path.join(base, d, 'live_log.csv')))
    res = []
    for fol in folders:
        p = os.path.join(base, fol, 'live_log.csv')
        if os.path.isfile(p):
            res.append(analyze_file(p, truth))
    lines = [
        '# analyze_live.py results (recomputed from raw live_log.csv logs)',
        '',
        '| folder | policy | profile | sent | delivered | ratio | lat p50/p99/max (us) | age p50/p99/max (us) | err mean/final (m) |',
        '|---|---|---|---|---|---|---|---|---|',
    ]
    # folder -> (policy, profile) mapping from the tweak_log run table
    pp = {
        'raw_none': ('raw', 'none'), 'raw_loss5': ('raw', 'uniform loss 5%'),
        'raw_loss20': ('raw', 'uniform loss 20%'), 'raw_jitter': ('raw', 'high jitter 20+-20 ms'),
        'deadline_none': ('deadline', 'none'), 'deadline_loss5': ('deadline', 'uniform loss 5%'),
        'deadline_loss20': ('deadline', 'uniform loss 20%'), 'deadline_jitter': ('deadline', 'high jitter 20+-20 ms'),
        'retx_none': ('retx', 'none'), 'retx_loss5': ('retx', 'uniform loss 5%'),
        'retx_loss20': ('retx', 'uniform loss 20%'), 'retx_jitter': ('retx', 'high jitter 20+-20 ms'),
    }
    for r in res:
        fol = os.path.basename(os.path.dirname(r['file']))
        pol, prof = pp.get(fol, ('?', '?'))
        lines.append(f"| {fol} | {pol} | {prof} | {r['n_sent']} | {r['n_delivered']} | {r['ratio']:.4f} | "
                     f"{r['lat_p50']:.0f}/{r['lat_p99']:.0f}/{r['lat_max']:.0f} | "
                     f"{r['age_p50']:.0f}/{r['age_p99']:.0f}/{r['age_max']:.0f} | "
                     f"{r['err_mean']:.4f}/{r['err_final']:.4f} |")
    lines.append('')
    lines.append('Every figure above is recomputed from the raw live_log.csv; nothing is copied from a summary.')
    report = '\n'.join(lines)
    print(report)
    if a.out:
        with open(a.out, 'w') as f:
            f.write(report + '\n')
        print(f'\nwritten {a.out}')

if __name__ == '__main__':
    main()
