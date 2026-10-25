import os, sys, random, subprocess, struct, shutil
import pytest
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..'))
from uavpipe import msglayer as ml
HERE = os.path.dirname(__file__)
def _run(lines):
    exe = os.path.join(HERE, '..', 'c', 'test_core')
    if not os.path.exists(exe):
        subprocess.check_call(['gcc', '-O2', '-fsanitize=address,undefined', '-o', exe, exe + '.c'])
    return subprocess.run([exe], input='\n'.join(lines) + '\n', capture_output=True, text=True, check=True).stdout.split('\n')[:len(lines)]

@pytest.mark.skipif(shutil.which('gcc') is None, reason='gcc not installed (C cross-check skipped)')
def test_c_pack_equals_python_and_unpack_agrees():
    rng = random.Random(11); cases = []
    for _ in range(1500):
        n = rng.randint(0, 32); pl = bytes(rng.randrange(256) for _ in range(n))
        cases.append((rng.randint(0, 2), rng.randrange(65536), rng.randrange(2**32), rng.choice([0, 30000, rng.randrange(2**32)]), rng.randrange(256), pl))
    out = _run([f"P {c} {s} {t} {a} {m} {pl.hex() or '-'}" for c, s, t, a, m, pl in cases])
    for (c, s, t, a, m, pl), o in zip(cases, out): assert o == ml.pack(c, s, t, a, m, pl).hex()
    # random + mutated frames: C accepts exactly what Python accepts, with identical fields
    bufs = []
    for c, s, t, a, m, pl in cases[:600]:
        f = bytearray(ml.pack(c, s, t, a, m, pl)); r = rng.random()
        if r < .4: f[rng.randrange(len(f))] ^= 1 << rng.randrange(8)
        elif r < .6: f = f[:rng.randrange(len(f))]
        elif r < .7: f += bytes([rng.randrange(256)])
        bufs.append(bytes(f))
    bufs += [bytes(rng.randrange(256) for _ in range(rng.randint(1, 50))) for _ in range(300)]
    res = _run([f"U {b.hex() or '00'}" for b in bufs]); ok = 0
    for b, o in zip(bufs, res):
        u = ml.unpack(b if b else b'\x00')
        if u is None: assert o == 'BAD', (b.hex(), o)
        else:
            cls, seq, tg, ma, mid, pl = u; assert o == f"OK {cls} {seq} {tg} {ma} {mid} {pl.hex() or '-'}", (b.hex(), o); ok += 1
    assert ok > 100   # ~30% of the 600 frames are left unmodified
