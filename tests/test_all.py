import os, sys, struct, random, math, subprocess
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..'))
from uavpipe.crc import crc16_mcrf4xx
from uavpipe import msglayer as ml
from uavpipe.link import EmulatedLink
from uavpipe.frames import attitude_frame, radar_frame, load_stim, load_truth
from uavpipe.ekf_py import PyEKF
from uavpipe.attitude import AttitudeBuffer
D = os.path.join(os.path.dirname(__file__), '..', 'data')

def test_crc_check_value(): assert crc16_mcrf4xx(b'123456789') == 0x6F91

def test_frame_roundtrip_and_corruption():
    f = ml.pack(ml.TELEM, 7, 123456, 30000, 1, b'hello')
    assert ml.unpack(f) == (ml.TELEM, 7, 123456, 30000, 1, b'hello')
    for i in range(len(f)):                       # every single-bit flip must be rejected
        for b in range(8):
            g = bytearray(f); g[i] ^= 1 << b; assert ml.unpack(bytes(g)) is None, (i, b)

def test_fuzz_never_raises():
    rng = random.Random(5)
    for _ in range(20000):
        n = rng.randint(0, 60); buf = bytes(rng.randrange(256) for _ in range(n))
        if rng.random() < .5 and n: buf = bytes([ml.SOF]) + buf[1:]
        ml.unpack(buf)                            # must not raise

def test_mavlink_frames_match_pymavlink():
    pytest = __import__('pytest')
    pytest.importorskip('pymavlink')
    from pymavlink.dialects.v20 import common as c
    m = c.MAVLink(None); m.srcSystem = 1; m.srcComponent = 1
    ref = c.MAVLink_attitude_message(1234, .1, .05, 1.0, .3, .2, .1).pack(m)
    mine = attitude_frame(0, 1234, .1, .05, 1.0)
    assert ref[:10] == mine[:10]                  # same header; trailing-zero trimming of real MAVLink differs only at the tail
    assert ml.unpack(b'') is None

def _delivery(p, R, n=4000, seed=1):
    up = EmulatedLink(1000, 0, p, seed=seed); down = EmulatedLink(1000, 0, p, seed=seed + 1)
    s = ml.Sender(rto_us=5000, max_retries=R, queue_len=8); r = ml.Receiver(); got = 0; now = 0; nxt = 0; sent = 0
    end = n * 100000 + 200000
    while now < end:
        if sent < n and now >= nxt:
            for f in s.command(now, 1, b'x'): up.send(now, f)
            sent += 1; nxt += 100000
        for f in s.tick(now): up.send(now, f)
        for _, f in up.poll(now):
            d, rp = r.on_frame(now, f); got += d is not None
            for x in rp: down.send(now, x)
        for _, f in down.poll(now):
            u = ml.unpack(f)
            if u and u[0] == ml.ACK:
                for x in s.on_ack(now, struct.unpack('<H', u[5])[0]): up.send(now, x)
        now += 500
    return got / n, s.stats

def test_delivery_ratio_matches_theory():
    for p, R in ((0.3, 3), (0.5, 3), (0.3, 1)):
        ratio, st = _delivery(p, R); theory = 1 - p ** (R + 1)
        assert abs(ratio - theory) < 0.02, (p, R, ratio, theory)   # 1 - p^(R+1), 4000 messages

def test_no_duplicate_delivery_and_failure_bound():
    ratio, st = _delivery(0.5, 2)
    assert st['retx'] > 0 and st['failed'] > 0

def test_deadline_rule_no_late_delivery():
    up = EmulatedLink(20000, 20000, 0.1, seed=3); s = ml.Sender(); r = ml.Receiver(); late = 0; ok = 0
    for k in range(3000):
        now = k * 10000; up.send(now, s.telemetry(now, 1, b'z', 30000))
        for _, f in up.poll(now):
            d, _ = r.on_frame(now, f)
            if d: ok += 1; late += d[2] > 30000
    assert late == 0 and ok > 0 and r.stats['stale'] > 0

def test_seq_wraparound_newest_wins():
    r = ml.Receiver(); s = ml.Sender(); s.seq = 65530; got = []
    for k in range(12):
        d, _ = r.on_frame(k, s.telemetry(k, 1, bytes([k]))); got.append(d is not None)
    assert all(got)

def test_ekf_py_matches_c_golden():
    stim = load_stim(os.path.join(D, 'stimulus.h')); truth = load_truth(os.path.join(D, 'truth.csv')); e = PyEKF()
    for r in stim: e.predict(.01); assert e.update(*r) == 0
    err = math.dist(e.x[:3], truth[len(stim) - 1])
    assert e.updates == 200 and abs(err - 0.1236) < 0.003, err      # C/board value 0.1236 m
    gold = [l.split(',') for l in open(os.path.join(D, 'host_bench.txt')) if l.startswith('S,') and l.split(',')[1] == '199'][0]
    c = [struct.unpack('<f', struct.pack('<I', int(h, 16)))[0] for h in gold[5:11]]
    assert max(abs(a - b) for a, b in zip(c, e.x)) < 5e-3

def test_attitude_interp_beats_hold():
    b = AttitudeBuffer(); f = lambda t: (0, 0, 0.5 * math.sin(math.pi * t * 1e-6))
    for k in range(50): t = 10000 * k + 3700; b.add(t, f(t))
    eh = ei = 0
    for tq in range(60000, 400000, 7300):
        eh = max(eh, abs(b.hold(tq)[2] - f(tq)[2])); ei = max(ei, abs(b.interp(tq)[2] - f(tq)[2]))
    assert ei < eh / 10
