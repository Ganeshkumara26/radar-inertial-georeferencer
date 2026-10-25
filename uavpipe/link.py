"""Emulated link for offline runs (one-way): fixed delay + jitter + random loss + optional burst loss (Gilbert-Elliott).
Deterministic given the seed. Time is injected (microseconds). For real runs use UdpLink + `tc netem` on the Pi instead.
Label results from this class 'Wi-Fi link', never '5G'."""
import heapq, random, socket

class EmulatedLink:
    def __init__(self, delay_us=5000, jitter_us=2000, loss=0.0, burst_len=0.0, seed=1, reorder=True):
        """loss = average loss probability. burst_len > 1 -> Gilbert-Elliott with mean burst length burst_len (same average loss)."""
        self.d, self.j, self.p, self.rng, self.reorder = delay_us, jitter_us, loss, random.Random(seed), reorder
        self.q = []; self.n = 0; self.bad = False; self.last_t = 0; self.sent = 0; self.dropped = 0
        if burst_len and burst_len > 1 and 0 < loss < 1:
            self.r = 1.0 / burst_len; self.g = self.r * loss / (1 - loss)      # P(bad->good), P(good->bad)
        else: self.r = None
    def _lost(self):
        if self.r is None: return self.rng.random() < self.p
        if self.bad:
            if self.rng.random() < self.r: self.bad = False
        elif self.rng.random() < self.g: self.bad = True
        return self.bad
    def send(self, now, data):
        self.sent += 1
        if self._lost(): self.dropped += 1; return
        t = now + self.d + (self.rng.uniform(-self.j, self.j) if self.j else 0)
        t = max(t, now)
        if not self.reorder: t = max(t, self.last_t); self.last_t = t
        self.n += 1; heapq.heappush(self.q, (t, self.n, data))
    def poll(self, now):
        out = []
        while self.q and self.q[0][0] <= now: out.append(heapq.heappop(self.q))
        return [(t, d) for t, _, d in out]
    def next_time(self): return self.q[0][0] if self.q else None

class UdpLink:
    """real transport (ESP32 <-> Pi). send(addr) / non-blocking poll. Time is the caller's monotonic clock."""
    def __init__(self, bind=None, peer=None):
        self.s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); self.s.setblocking(False); self.peer = peer
        if bind: self.s.bind(bind)
    def send(self, now, data, peer=None): self.s.sendto(data, peer or self.peer)
    def poll(self, now):
        out = []
        while True:
            try: d, a = self.s.recvfrom(2048); out.append((now, d)); self.last_peer = a
            except BlockingIOError: return out
