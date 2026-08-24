#!/usr/bin/env python3
"""
generate_mavlink_stimuli.py — Generate real MAVLink v2 packets for Renode HIL simulation.
Outputs a Renode .rescript snippet that injects packets via simulation backdoors.
Writes one frame at a time with emulation runs between frames.
"""

import struct
import math
from pymavlink import mavutil

def make_mavlink_v2(msg_id, payload_bytes, seq, sysid=1, compid=1):
    """Build a MAVLink v2 packet with proper CRC."""
    header = bytes([
        0xFD, len(payload_bytes), 0x00, 0x00,
        seq & 0xFF, sysid, compid,
        msg_id & 0xFF, (msg_id >> 8) & 0xFF, (msg_id >> 16) & 0xFF,
    ])
    data_for_crc = header[1:] + payload_bytes
    crc_extra = {30: 3, 32: 185, 33: 104}
    crc = 0xFFFF
    for b in data_for_crc:
        tmp = b ^ (crc & 0xFF)
        tmp = (tmp ^ (tmp << 4)) & 0xFF
        crc = ((crc >> 8) ^ (tmp << 8) ^ (tmp << 3) ^ (tmp >> 4)) & 0xFFFF
    extra = crc_extra.get(msg_id, 0)
    tmp = extra ^ (crc & 0xFF)
    tmp = (tmp ^ (tmp << 4)) & 0xFF
    crc = ((crc >> 8) ^ (tmp << 8) ^ (tmp << 3) ^ (tmp >> 4)) & 0xFFFF
    return header + payload_bytes + struct.pack('<H', crc)


def make_attitude(seq, roll, pitch, yaw, t=0):
    payload = struct.pack('<Iffffff', t, roll, pitch, yaw, 0, 0, 0)
    return make_mavlink_v2(30, payload, seq)


def make_local_position_ned(seq, x, y, z, vx=0, vy=0, vz=0, t=0):
    payload = struct.pack('<Iffffff', t, x, y, z, vx, vy, vz)
    return make_mavlink_v2(32, payload, seq)


def main():
    offset = 0x24000020  # dma_buffer address
    seq = 0

    # Simulate 50 frames at 100Hz (0.5 seconds)
    for frame in range(50):
        t_ms = frame * 10

        roll = 0.05 * math.sin(2 * math.pi * 0.5 * t_ms / 1000)
        pitch = 0.03 * math.cos(2 * math.pi * 0.3 * t_ms / 1000)
        yaw = 0.0

        att_pkt = make_attitude(seq, roll, pitch, yaw, t_ms)

        # Write ATTITUDE packet
        for i, b in enumerate(att_pkt):
            print(f"sysbus WriteByte {offset + i:#010x} {b:#04x}")

        n_pos = 3.0 * (t_ms / 1000)
        e_pos = 0.0
        d_pos = -10.0 + 0.5 * (t_ms / 1000)

        ned_pkt = make_local_position_ned(seq + 1, n_pos, e_pos, d_pos, 3.0, 0.0, -0.5, t_ms)
        ned_offset = offset + len(att_pkt)

        for i, b in enumerate(ned_pkt):
            print(f"sysbus WriteByte {ned_offset + i:#010x} {b:#04x}")

        total_bytes = len(att_pkt) + len(ned_pkt)
        print(f"sysbus WriteDoubleWord 0x24000010 {total_bytes:#010x}")
        print(f"emulation RunFor \"0.02\"")
        print()

        seq += 2

    # Trigger frame-sync
    print("# Frame-sync")
    print("sysbus WriteDoubleWord 0x24000018 0x00001388")
    print("sysbus WriteDoubleWord 0x24000014 0x00000001")
    print("sysbus WriteDoubleWord 0x2400001C 0x00000001")
    print("emulation RunFor \"0.05\"")


if __name__ == "__main__":
    main()
