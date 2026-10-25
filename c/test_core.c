/* stdin protocol for tests/test_c_core.py:  "P cls seq tgen maxage msgid hexpayload|-"  -> hex frame ;  "U hexframe" -> "OK cls seq tgen maxage msgid hexpayload|-" or "BAD" */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../firmware/esp32_sensor/msglayer_core.h"
static size_t unhex(const char *s, uint8_t *o) { if (s[0] == '-') return 0; size_t n = strlen(s) / 2; for (size_t i = 0; i < n; i++) { unsigned v; sscanf(s + 2 * i, "%2x", &v); o[i] = (uint8_t)v; } return n; }
int main(void) {
    char line[512];
    while (fgets(line, sizeof line, stdin)) {
        char tag; if (line[0] == 'P') {
            unsigned cls, seq, mid; unsigned long tg, ma; char hx[200] = "-"; uint8_t pl[64], fr[128];
            if (sscanf(line, "%c %u %u %lu %lu %u %199s", &tag, &cls, &seq, &tg, &ma, &mid, hx) < 6) { puts("ERR"); continue; }
            size_t n = unhex(hx, pl), m = ml_pack(fr, (uint8_t)cls, (uint16_t)seq, (uint32_t)tg, (uint32_t)ma, (uint8_t)mid, pl, (uint8_t)n);
            if (!m) { puts("TOOLONG"); continue; } for (size_t i = 0; i < m; i++) printf("%02x", fr[i]); puts("");
        } else if (line[0] == 'U') {
            char hx[400]; uint8_t b[200]; ml_msg_t g; if (sscanf(line, "%c %399s", &tag, hx) < 2) { puts("BAD"); continue; }
            size_t n = unhex(hx, b);
            if (!ml_unpack(b, n, &g)) { puts("BAD"); continue; }
            printf("OK %u %u %lu %lu %u ", g.cls, g.seq, (unsigned long)g.t_gen, (unsigned long)g.max_age, g.msgid);
            if (!g.len) printf("-");
            for (int i = 0; i < g.len; i++) printf("%02x", g.payload[i]);
            puts("");
        }
    }
    return 0;
}
