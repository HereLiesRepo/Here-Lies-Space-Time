/* opsdrop -- remove one op from a loop-free OPSTRM01 stream.
 *
 *   opsdrop KIND N < in.ops > out.ops
 *
 * Copies the stream and leaves out the N-th op (0-based, in stream order)
 * whose kind is KIND (X Z NEG CX CZ SWAP CCX CCZ R HMR OXOR).  The FLAT
 * segment's n_ops and the END expanded-op count are each decremented by
 * one; every other byte is copied unchanged.  Only REGS, FLAT and END
 * segments are accepted (the hhl-angle discovered stream is REGS + FLAT +
 * END); a LOOP or DATA segment, or a table-gated op, is refused.
 * The dropped op is described on stderr.
 *
 * Layout (uncompute/ops.py): "OPSTRM01" | REGS 0x01 | FLAT 0x03 u32 n_ops,
 * op records | END 0xFF u64 count.  Op record at depth 0: u8 kind, u8 flags,
 * u32 per required operand, then u32 oc if flags & 1, u32 cc if flags & 4.
 *
 * cc -std=c11 -O2 -Wall -Wextra -o opsdrop opsdrop.c
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *KN[] = {"X", "Z", "NEG", "CX", "CZ", "SWAP", "CCX", "CCZ",
                           "R", "HMR", "OXOR", "BIT_INVERT", "BIT_STORE0", "BIT_STORE1"};
static const int NREQ[] = {1, 1, 0, 2, 2, 2, 3, 3, 1, 2, 2, 1, 1, 1};
#define NK 14

static unsigned char *buf;
static size_t len, pos;

static void die(const char *m) { fprintf(stderr, "opsdrop: %s\n", m); exit(2); }
static void need(size_t n) { if (pos + n > len) die("truncated stream"); }
static uint64_t rd(int n) {
    uint64_t v = 0;
    need((size_t)n);
    for (int i = 0; i < n; i++) v |= (uint64_t)buf[pos + i] << (8 * i);
    pos += (size_t)n;
    return v;
}
static void wr(unsigned char *p, uint64_t v, int n) {
    for (int i = 0; i < n; i++) p[i] = (unsigned char)(v >> (8 * i));
}

int main(int argc, char **argv) {
    if (argc != 3) die("usage: opsdrop KIND N < in > out");
    int kind = -1;
    for (int k = 0; k < NK; k++) if (!strcmp(argv[1], KN[k])) kind = k;
    if (kind < 0) die("unknown kind");
    uint64_t nth = strtoull(argv[2], NULL, 0), seen = 0;

    size_t cap = 1 << 20;
    buf = malloc(cap);
    if (!buf) die("out of memory");
    for (size_t r; (r = fread(buf + len, 1, cap - len, stdin)) > 0;) {
        len += r;
        if (len == cap && !(buf = realloc(buf, cap *= 2))) die("out of memory");
    }
    if (len < 8 || memcmp(buf, "OPSTRM01", 8)) die("bad magic");
    pos = 8;

    size_t drop_at = 0, drop_len = 0, nops_at = 0, end_at = 0;
    uint64_t opi = 0;
    int found = 0, done = 0;
    while (!done) {
        int seg = (int)rd(1);
        if (seg == 0x01) {                      /* REGS */
            uint64_t nr = rd(4);
            for (uint64_t i = 0; i < nr; i++) { rd(1); uint64_t nw = rd(4); need(4 * nw); pos += 4 * nw; }
        } else if (seg == 0x03) {               /* FLAT */
            if (nops_at) die("more than one FLAT segment");
            nops_at = pos;
            uint64_t n = rd(4);
            for (uint64_t i = 0; i < n; i++, opi++) {
                size_t at = pos;
                int k = (int)rd(1), fl = (int)rd(1);
                if (k >= NK) die("unknown op kind");
                if (fl & ~5) die("table-gated or unknown flags");
                int nop = NREQ[k] + !!(fl & 1) + !!(fl & 4);
                uint64_t a[5];
                for (int j = 0; j < nop; j++) a[j] = rd(4);
                if (k == kind && seen++ == nth) {
                    found = 1; drop_at = at; drop_len = pos - at;
                    fprintf(stderr, "opsdrop: dropped %s #%llu (op %llu, byte %zu, operands",
                            KN[k], (unsigned long long)nth, (unsigned long long)opi, at);
                    for (int j = 0; j < nop; j++) fprintf(stderr, " %llu", (unsigned long long)a[j]);
                    fprintf(stderr, ", flags %d)\n", fl);
                }
            }
        } else if (seg == 0xFF) {               /* END */
            end_at = pos;
            rd(8);
            done = 1;
        } else {
            die("only REGS, FLAT and END segments are supported");
        }
    }
    if (pos != len) die("trailing bytes after END");
    if (!found) die("fewer ops of that kind than N+1");

    uint64_t n_ops = 0, endc = 0;
    for (int i = 0; i < 4; i++) n_ops |= (uint64_t)buf[nops_at + i] << (8 * i);
    for (int i = 0; i < 8; i++) endc |= (uint64_t)buf[end_at + i] << (8 * i);
    /* 0 - 1 would read back as UINT64_MAX ("unknown") and drop the END check */
    if (endc == 0) die("END expanded-op count is 0 but the stream has ops");
    wr(buf + nops_at, n_ops - 1, 4);
    if (endc != UINT64_MAX) wr(buf + end_at, endc - 1, 8);
    if (fwrite(buf, 1, drop_at, stdout) != drop_at ||
        fwrite(buf + drop_at + drop_len, 1, len - drop_at - drop_len, stdout) != len - drop_at - drop_len)
        die("write failed");
    return 0;
}
