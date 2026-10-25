"""Bench-format MAVLink v2 frames (what the Nucleo firmware parses) + reply-line parser. Same bytes as host/capture.py."""
import struct, re
from .crc import crc_x25_mavlink

def mav_frame(seq, msgid, payload, extra):
    body = bytes([len(payload), 0, 0, seq & 0xFF, 1, 1]) + msgid.to_bytes(3, 'little') + payload
    return b'\xFD' + body + struct.pack('<H', crc_x25_mavlink(body, extra))

def attitude_frame(seq, t_ms, roll, pitch, yaw):      # MAVLink ATTITUDE (id 30, crc_extra 39)
    return mav_frame(seq, 30, struct.pack('<Iffffff', t_ms & 0xFFFFFFFF, roll, pitch, yaw, 0, 0, 0), 39)

def radar_frame(seq, rng, az, el, dop):               # custom RADAR (id 0x4001, crc_extra 0x5A)
    return mav_frame(seq, 0x4001, struct.pack('<I4f', seq, rng, az, el, dop), 0x5A)

def parse_reply(line: str):
    """'R,seq,ekf_us,geo_us,rc,x0..x5(hex float32)' -> dict or None"""
    f = line.strip().split(',')
    if len(f) != 11 or f[0] != 'R': return None
    x = [struct.unpack('<f', struct.pack('<I', int(h, 16)))[0] for h in f[5:11]]
    return dict(seq=int(f[1]), ekf_us=int(f[2]), geo_us=int(f[3]), rc=int(f[4]), x=x)

def load_stim(path):
    txt = open(path).read()
    rows = re.findall(r'\{(0x[0-9a-f]{8}),(0x[0-9a-f]{8}),(0x[0-9a-f]{8}),(0x[0-9a-f]{8})\}', txt)
    return [tuple(struct.unpack('<f', struct.pack('<I', int(x, 16)))[0] for x in r) for r in rows]   # (range, az, el, doppler)

def load_truth(path):
    import csv
    return {int(r['step']): (float(r['rel_n']), float(r['rel_e']), float(r['rel_d'])) for r in csv.DictReader(open(path))}
