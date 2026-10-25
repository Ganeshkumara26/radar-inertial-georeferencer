"""One command to check the whole software side without any hardware:  python -m uavpipe.selftest
(unit tests, all-software live smoke test, fake-Pixhawk smoke test, Nucleo-bridge smoke test if hil_host_standin exists)"""
import os, subprocess, sys, threading, time
ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
def step(name, cmd, bg=None):
    p = subprocess.Popen(bg, cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL) if bg else None
    if bg: time.sleep(1)
    r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, timeout=300); out = r.stdout + r.stderr
    ok = r.returncode == 0 and ('results=200' in out or 'passed' in out); print(f"[{'PASS' if ok else 'FAIL'}] {name}")
    if not ok: print(out[-800:])
    if p: p.kill()
    return ok
def main():
    py = sys.executable; res = [step('unit tests (CRC, framing, fuzz, delivery vs 1-p^(R+1), deadline, EKF vs board golden, C core)', [py, '-m', 'pytest', '-q', 'tests'])]
    res.append(step('live gateway: sensor -> UDP -> Python estimator', [py, '-m', 'uavpipe.live', '--seconds', '5', '--out', 'results/selftest_py']))
    res.append(step('live gateway + Pixhawk attitude (real MAVLink via pymavlink)', [py, '-m', 'uavpipe.live', '--pixhawk', 'udpin:0.0.0.0:14550', '--seconds', '6', '--out', 'results/selftest_pix'],
                    bg=[py, '-m', 'uavpipe.fake_pixhawk', '--seconds', '12']))
    if os.path.exists(os.path.join(ROOT, 'hil_host_standin')):
        res.append(step('live gateway -> Nucleo bridge (host stand-in of the board firmware)', [py, '-m', 'uavpipe.live', '--estimator', 'nucleo:proc:./hil_host_standin', '--seconds', '5', '--out', 'results/selftest_nuc']))
    print('ALL PASS' if all(res) else 'SOME FAILED'); return 0 if all(res) else 1
if __name__ == '__main__': sys.exit(main())
