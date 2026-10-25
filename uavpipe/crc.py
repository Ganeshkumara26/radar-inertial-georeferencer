"""CRC helpers. crc16_mcrf4xx: msglayer frames (check value of b'123456789' = 0x6F91).
crc_x25_mavlink: MAVLink v2 CRC incl. crc_extra (identical to host/capture.py and the board firmware)."""
def crc16_mcrf4xx(data: bytes, crc: int = 0xFFFF) -> int:
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0x8408 if crc & 1 else crc >> 1
    return crc & 0xFFFF

def crc_x25_mavlink(data: bytes, extra: int) -> int:
    crc = 0xFFFF
    for b in bytes(data) + bytes([extra]):
        t = b ^ (crc & 0xFF); t = (t ^ (t << 4)) & 0xFF
        crc = ((crc >> 8) ^ (t << 8) ^ (t << 3) ^ (t >> 4)) & 0xFFFF
    return crc
