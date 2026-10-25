"""uav-msglayer reference model (pure Python, no I/O, time is injected so behaviour is deterministic).

Frame: SOF 0xA5 | len u8 | cls u8 | seq u16 | t_gen u32 (us, sender clock) | max_age u32 (us, 0 = none) | msgid u8 | payload<=32 | CRC-16/MCRF4XX (over everything after SOF)
cls: 0 TELEMETRY (best effort, newest wins), 1 COMMAND (ACK, stop-and-wait window 1, <=R retries), 2 ACK (payload = acked seq u16)
Deadline rule: the sender stops retransmitting when the deadline cannot be met; the receiver drops anything older than max_age.
Bound: a command is delivered or declared failed within (R+1)*RTO (+ one-way delay for the last attempt)."""
import struct
from collections import deque
from .crc import crc16_mcrf4xx

SOF = 0xA5; TELEM, CMD, ACK = 0, 1, 2; HDR = struct.Struct('<BBHIIB'); MAXP = 32

def pack(cls, seq, t_gen, max_age, msgid, payload=b''):
    assert len(payload) <= MAXP
    body = HDR.pack(len(payload), cls, seq & 0xFFFF, t_gen & 0xFFFFFFFF, max_age & 0xFFFFFFFF, msgid) + payload
    return bytes([SOF]) + body + struct.pack('<H', crc16_mcrf4xx(body))

def unpack(buf: bytes):
    """-> (cls, seq, t_gen, max_age, msgid, payload) or None if malformed. Never raises."""
    try:
        if len(buf) < 1 + HDR.size + 2 or buf[0] != SOF: return None
        ln, cls, seq, tg, ma, mid = HDR.unpack_from(buf, 1)
        if ln > MAXP or len(buf) != 1 + HDR.size + ln + 2 or cls > 2: return None
        body = buf[1:1 + HDR.size + ln]
        if struct.unpack_from('<H', buf, 1 + HDR.size + ln)[0] != crc16_mcrf4xx(body): return None
        return cls, seq, tg, ma, mid, buf[1 + HDR.size:1 + HDR.size + ln]
    except Exception: return None

def seq_newer(a, b):    # a newer than b, 16-bit wraparound
    return ((a - b) & 0xFFFF) != 0 and ((a - b) & 0xFFFF) < 0x8000

class Sender:
    def __init__(self, rto_us=20000, max_retries=3, queue_len=8):
        self.seq = 0; self.rto = rto_us; self.R = max_retries; self.q = deque(maxlen=queue_len)
        self.cur = None   # dict(frame, seq, t_gen, deadline, tries, last_tx)
        self.stats = dict(tx=0, retx=0, acked=0, failed=0, expired=0, queue_drop=0)
    def telemetry(self, now, msgid, payload, max_age_us=0):
        f = pack(TELEM, self.seq, now, max_age_us, msgid, payload); self.seq = (self.seq + 1) & 0xFFFF; self.stats['tx'] += 1; return f
    def command(self, now, msgid, payload, max_age_us=0):
        """queue a command; returns frames to transmit now (maybe empty if another command is in flight)"""
        if len(self.q) == self.q.maxlen: self.stats['queue_drop'] += 1; return []
        f = pack(CMD, self.seq, now, max_age_us, msgid, payload)
        self.q.append(dict(frame=f, seq=self.seq, t_gen=now, deadline=(now + max_age_us) if max_age_us else None, tries=0, last_tx=None))
        self.seq = (self.seq + 1) & 0xFFFF; return self._start(now)
    def _start(self, now):
        out = []
        while self.cur is None and self.q:
            c = self.q.popleft()
            if c['deadline'] is not None and now >= c['deadline']: self.stats['expired'] += 1; continue
            c['tries'] = 1; c['last_tx'] = now; self.cur = c; self.stats['tx'] += 1; out.append(c['frame'])
        return out
    def on_ack(self, now, seq):
        if self.cur and self.cur['seq'] == seq: self.cur = None; self.stats['acked'] += 1; return self._start(now)
        return []
    def tick(self, now):
        """returns frames to (re)transmit. Call whenever time advances."""
        out = []
        c = self.cur
        if c and now - c['last_tx'] >= self.rto:
            dead = c['deadline'] is not None and now + self.rto // 2 >= c['deadline'] + 0   # cannot arrive in time
            if c['tries'] > self.R: self.stats['failed'] += 1; self.cur = None
            elif dead: self.stats['expired'] += 1; self.cur = None
            else: c['tries'] += 1; c['last_tx'] = now; self.stats['retx'] += 1; out.append(c['frame'])
            if self.cur is None: out += self._start(now)
        return out

class Receiver:
    """offset_us = (sender clock - receiver clock) estimate; 0 when both share a clock."""
    def __init__(self, offset_us=0, ordered=True):
        self.off = offset_us; self.ordered = ordered; self.last_tel = None; self.last_cmd = None
        self.stats = dict(rx=0, bad=0, delivered=0, stale=0, old=0, dup=0)
    def on_frame(self, now, buf):
        """-> (delivered: (msgid, payload, age_us) | None, reply_frames: list)"""
        u = unpack(buf)
        if u is None: self.stats['bad'] += 1; return None, []
        cls, seq, tg, ma, mid, pl = u; self.stats['rx'] += 1
        age = (now + self.off - tg) & 0xFFFFFFFF
        if age > 0x80000000: age = 0                      # sender clock slightly ahead
        reply = []
        if cls == CMD: reply = [pack(ACK, 0, now, 0, mid, struct.pack('<H', seq))]
        if ma and age > ma: self.stats['stale'] += 1; return None, reply
        if cls == TELEM:
            if self.ordered and self.last_tel is not None and not seq_newer(seq, self.last_tel): self.stats['old'] += 1; return None, reply
            self.last_tel = seq
        elif cls == CMD:
            if self.last_cmd is not None and seq == self.last_cmd: self.stats['dup'] += 1; return None, reply
            self.last_cmd = seq
        else: return None, reply   # ACKs are handled by the caller via Sender.on_ack
        self.stats['delivered'] += 1; return (mid, pl, age), reply
