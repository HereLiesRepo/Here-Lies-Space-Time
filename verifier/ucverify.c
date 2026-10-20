/*
 * ucverify -- a standalone, dependency-free C11 verifier for OPSTRM01 op
 * streams (the format of the "uncompute" evaluator, arXiv:2610.22367).
 *
 * Written from the format as the evaluator's own reader defines it:
 * uncompute/ops.py (layout, operand rules, billing), and
 * uncompute/evalcore/compact_reader.py (normative interpreter, check order).
 * No code is shared with that implementation.
 *
 * What it computes
 *   - The billed counts exactly as ops._CC_PROMOTION prices them: CCX/CCZ
 *     are Toffolis; a bit-conditioned op bills at its promoted kind
 *     (X->CX, CX->CCX, Z->CZ, CZ->CCZ, NEG->Z). Peak qubits = largest qubit
 *     id in the expansion or in a Qubit register, plus 1.
 *   - A bit-sliced simulation, 64 lanes per word, in which every HMR (and
 *     R) outcome is a free variable. Wire values never depend on outcomes
 *     (outcomes condition only Z/CZ/NEG), so the global sign of a lane is an
 *     affine GF(2) polynomial in the outcome bits. ucverify tracks that
 *     polynomial exactly: the constant term and one coefficient per live
 *     draw. A lane whose coefficient on any draw is nonzero has an
 *     outcome-dependent sign; that is a failure for every outcome branch
 *     the evaluator's sampled RNG would or would not hit.
 *   - Task adapters that follow each task README's input/output contract
 *     (hhl-angle, coulomb-potential, gaussian), and a generic test-vector
 *     adapter for the larger tasks.
 *
 * Build: cc -std=c11 -O2 -Wall -Wextra -Werror -o ucverify ucverify.c -lm
 */
#define _POSIX_C_SOURCE 200809L
#include <inttypes.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ util */

_Noreturn static void die(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fputs("ucverify: ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
    exit(2);
}

static void *xmalloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p) die("out of memory (%zu bytes)", n);
    return p;
}

static void *xcalloc(size_t a, size_t b)
{
    void *p = calloc(a ? a : 1, b ? b : 1);
    if (!p) die("out of memory (%zu x %zu bytes)", a, b);
    return p;
}

static void *xrealloc(void *p, size_t n)
{
    void *q = realloc(p, n ? n : 1);
    if (!q) die("out of memory (%zu bytes)", n);
    return q;
}

static int popc64(uint64_t x)
{
    int c = 0;
    while (x) {
        x &= x - 1;
        c++;
    }
    return c;
}

/* ---------------------------------------------------------------- sha256 */
/* FIPS 180-4, used only to print h_ops (the evaluator's seed input). */

typedef struct {
    uint32_t h[8];
    uint64_t len;
    uint8_t buf[64];
    size_t nbuf;
} Sha256;

static const uint32_t SHA_K[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u,
    0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu,
    0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu,
    0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
    0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
    0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u,
    0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u,
    0xc67178f2u};

static uint32_t ror32(uint32_t x, int r) { return (x >> r) | (x << (32 - r)); }

static void sha_block(Sha256 *s, const uint8_t *p)
{
    uint32_t w[64], a, b, c, d, e, f, g, h;
    int i;
    for (i = 0; i < 16; i++)
        w[i] = ((uint32_t)p[4 * i] << 24) | ((uint32_t)p[4 * i + 1] << 16) |
               ((uint32_t)p[4 * i + 2] << 8) | (uint32_t)p[4 * i + 3];
    for (i = 16; i < 64; i++) {
        uint32_t s0 = ror32(w[i - 15], 7) ^ ror32(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = ror32(w[i - 2], 17) ^ ror32(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    a = s->h[0]; b = s->h[1]; c = s->h[2]; d = s->h[3];
    e = s->h[4]; f = s->h[5]; g = s->h[6]; h = s->h[7];
    for (i = 0; i < 64; i++) {
        uint32_t S1 = ror32(e, 6) ^ ror32(e, 11) ^ ror32(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t t1 = h + S1 + ch + SHA_K[i] + w[i];
        uint32_t S0 = ror32(a, 2) ^ ror32(a, 13) ^ ror32(a, 22);
        uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = S0 + mj;
        h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    s->h[0] += a; s->h[1] += b; s->h[2] += c; s->h[3] += d;
    s->h[4] += e; s->h[5] += f; s->h[6] += g; s->h[7] += h;
}

static void sha_init(Sha256 *s)
{
    static const uint32_t iv[8] = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                                   0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
    memcpy(s->h, iv, sizeof iv);
    s->len = 0;
    s->nbuf = 0;
}

static void sha_update(Sha256 *s, const uint8_t *p, size_t n)
{
    s->len += n;
    while (n) {
        size_t take = 64 - s->nbuf;
        if (take > n) take = n;
        memcpy(s->buf + s->nbuf, p, take);
        s->nbuf += take;
        p += take;
        n -= take;
        if (s->nbuf == 64) {
            sha_block(s, s->buf);
            s->nbuf = 0;
        }
    }
}

static void sha_final(Sha256 *s, char hex[65])
{
    uint64_t bits = s->len * 8u;
    uint8_t pad = 0x80, z = 0, lb[8];
    int i;
    sha_update(s, &pad, 1);
    while (s->nbuf != 56) sha_update(s, &z, 1);
    for (i = 0; i < 8; i++) lb[i] = (uint8_t)(bits >> (56 - 8 * i));
    sha_update(s, lb, 8);
    for (i = 0; i < 8; i++) sprintf(hex + 8 * i, "%08" PRIx32, s->h[i]);
    hex[64] = 0;
}

/* --------------------------------------------------------------- op model */

enum {
    K_X = 0, K_Z, K_NEG, K_CX, K_CZ, K_SWAP, K_CCX, K_CCZ, K_R, K_HMR, K_OXOR,
    K_BIT_INVERT, K_BIT_STORE0, K_BIT_STORE1, K_NKINDS
};
static const char *KIND_NAME[K_NKINDS] = {"X", "Z", "NEG", "CX", "CZ", "SWAP", "CCX",
                                          "CCZ", "R", "HMR", "OXOR", "BIT_INVERT",
                                          "BIT_STORE0", "BIT_STORE1"};

/* operand slots, in stream order */
enum { S_QA = 0, S_QB, S_QT, S_OT, S_OC, S_CT, S_CC, S_N };
enum { SP_QUBIT = 0, SP_OUT, SP_BIT };
static const int SLOT_SPACE[S_N] = {SP_QUBIT, SP_QUBIT, SP_QUBIT, SP_OUT, SP_OUT, SP_BIT, SP_BIT};

/* required operand slots per kind (ops.SIGNATURES), -1 terminated */
static const int8_t REQ[K_NKINDS][4] = {
    {S_QT, -1},             /* X */
    {S_QT, -1},             /* Z */
    {-1},                   /* NEG */
    {S_QB, S_QT, -1},       /* CX */
    {S_QB, S_QT, -1},       /* CZ */
    {S_QB, S_QT, -1},       /* SWAP */
    {S_QA, S_QB, S_QT, -1}, /* CCX */
    {S_QA, S_QB, S_QT, -1}, /* CCZ */
    {S_QT, -1},             /* R */
    {S_QT, S_OT, -1},       /* HMR */
    {S_OT, S_OC, -1},       /* OXOR */
    {S_CT, -1},             /* BIT_INVERT */
    {S_CT, -1},             /* BIT_STORE0 */
    {S_CT, -1},             /* BIT_STORE1 */
};

static int oc_allowed(int k) { return k == K_Z || k == K_CZ || k == K_NEG; }
static int cc_allowed(int k)
{
    return k == K_X || k == K_Z || k == K_NEG || k == K_CX || k == K_CZ || k == K_BIT_INVERT ||
           k == K_BIT_STORE0 || k == K_BIT_STORE1;
}

/* billed class: 2 = Toffoli, 1 = Clifford, 0 = free */
static int bill_class(int k, int conditioned)
{
    if (conditioned) {
        switch (k) {
        case K_X: k = K_CX; break;
        case K_CX: k = K_CCX; break;
        case K_Z: k = K_CZ; break;
        case K_CZ: k = K_CCZ; break;
        case K_NEG: k = K_Z; break;
        default: break; /* bit-only kinds bill as themselves */
        }
    }
    if (k == K_CCX || k == K_CCZ) return 2;
    if (k == K_CX || k == K_CZ || k == K_SWAP || k == K_R || k == K_HMR) return 1;
    return 0;
}

#define MAX_DEPTH 4
#define ID_LIMIT (((int64_t)1) << 32)
#define MAX_QUBIT_ADDR (1u << 20)
#define MAX_OUT_ADDR (1u << 24)
#define MAX_BIT_ADDR (1u << 24)
#define UNKNOWN_COUNT UINT64_MAX

typedef struct {
    uint8_t kind, flags, depth, nop, gated;
    uint8_t slot[S_N]; /* slot of operand j */
    int32_t tbl;       /* table index when gated */
    uint64_t poff;     /* offset into the coefficient pool */
} Op;

typedef struct {
    uint8_t is_loop;
    uint32_t op;      /* op index when !is_loop */
    uint64_t count;   /* loop count */
    uint64_t child0;  /* first child in the child-index array */
    uint32_t nchild;
    uint8_t gated_sub; /* subtree holds a gated op */
    uint64_t it_ops, it_tof, it_cliff; /* per-iteration analytic totals (ungated) */
} Item;

typedef struct {
    uint8_t kind;
    uint32_t nw;
    uint32_t *w;
} Reg;

typedef struct {
    uint32_t id;
    uint64_t nbits;
    const uint8_t *bits;
} Table;

typedef struct {
    /* source */
    uint8_t *buf;
    size_t len, pos;
    char sha[65];
    /* parsed */
    Reg *regs;
    uint32_t nregs;
    int saw_regs, saw_prog;
    Table *tbl;
    uint32_t ntbl, captbl;
    Op *ops;
    uint64_t nops, capops;
    int64_t *pool;
    uint64_t npool, cappool;
    Item *items;
    uint64_t nitems, capitems;
    uint64_t *child;
    uint64_t nchild, capchild;
    uint64_t *top; /* top-level items */
    uint64_t ntop, captop;
    uint64_t end_count;
    /* census from REGS */
    uint64_t reg_q, reg_b;
} Prog;

static const char *g_err_code = NULL;

_Noreturn static void perr(const char *code, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    printf("{\"ok\": false, \"fail_code\": \"%s\", \"reason\": \"", code);
    vprintf(fmt, ap);
    printf("\"}\n");
    va_end(ap);
    g_err_code = code;
    exit(1);
}

static void need(Prog *p, size_t n, const char *what)
{
    if (p->len - p->pos < n) perr("load/truncated", "stream truncated reading %s", what);
}
static uint8_t rd8(Prog *p, const char *w) { need(p, 1, w); return p->buf[p->pos++]; }
static uint16_t rd16(Prog *p, const char *w)
{
    uint16_t v;
    need(p, 2, w);
    v = (uint16_t)(p->buf[p->pos] | (p->buf[p->pos + 1] << 8));
    p->pos += 2;
    return v;
}
static uint32_t rd32(Prog *p, const char *w)
{
    uint32_t v = 0;
    int i;
    need(p, 4, w);
    for (i = 3; i >= 0; i--) v = (v << 8) | p->buf[p->pos + (size_t)i];
    p->pos += 4;
    return v;
}
static uint64_t rd64(Prog *p, const char *w)
{
    uint64_t v = 0;
    int i;
    need(p, 8, w);
    for (i = 7; i >= 0; i--) v = (v << 8) | p->buf[p->pos + (size_t)i];
    p->pos += 8;
    return v;
}

#define GROW(arr, n, cap)                                                   \
    do {                                                                    \
        if ((n) >= (cap)) {                                                 \
            (cap) = (cap) ? (cap) * 2 : 64;                                 \
            (arr) = xrealloc((arr), (size_t)(cap) * sizeof *(arr));         \
        }                                                                   \
    } while (0)

static void pool_push(Prog *p, int64_t v)
{
    GROW(p->pool, p->npool, p->cappool);
    p->pool[p->npool++] = v;
}

static uint64_t parse_op(Prog *p, int depth)
{
    Op o;
    int k, flags, j, n = 0;
    memset(&o, 0, sizeof o);
    k = rd8(p, "op kind");
    flags = rd8(p, "op flags");
    if (k >= K_NKINDS) perr("load/op-invalid", "op: unknown kind %d", k);
    if (flags & ~0x07) perr("load/op-invalid", "op: reserved flag bits set (flags=0x%02x)", flags);
    if ((flags & 1) && !oc_allowed(k))
        perr("load/op-invalid", "op: kind %d sets flag bit 0, which only Z, CZ, NEG may carry", k);
    if ((flags & 4) && !cc_allowed(k))
        perr("load/op-invalid", "op: kind %d sets flag bit 2, which it may not carry", k);
    o.kind = (uint8_t)k;
    o.flags = (uint8_t)flags;
    o.depth = (uint8_t)depth;
    o.gated = (uint8_t)((flags & 2) != 0);
    o.poff = p->npool;
    o.tbl = -1;
    if (o.gated) {
        uint32_t tid = rd32(p, "gate table_id");
        uint64_t off = rd64(p, "gate offset");
        uint32_t t;
        pool_push(p, (int64_t)off);
        for (j = 0; j < depth; j++) pool_push(p, (int64_t)rd64(p, "gate stride"));
        for (t = 0; t < p->ntbl; t++)
            if (p->tbl[t].id == tid) break;
        if (t == p->ntbl)
            perr("load/op-invalid", "op: gate references table %u with no preceding DATA", tid);
        o.tbl = (int32_t)t;
    }
    for (j = 0; REQ[k][j] >= 0; j++) o.slot[n++] = (uint8_t)REQ[k][j];
    if (flags & 1) o.slot[n++] = S_OC;
    if (flags & 4) o.slot[n++] = S_CC;
    o.nop = (uint8_t)n;
    for (j = 0; j < n; j++) {
        int d;
        pool_push(p, (int64_t)rd32(p, "operand base"));
        for (d = 0; d < depth; d++) pool_push(p, (int64_t)rd64(p, "operand stride"));
    }
    GROW(p->ops, p->nops, p->capops);
    p->ops[p->nops] = o;
    return p->nops++;
}

static uint64_t new_item(Prog *p)
{
    GROW(p->items, p->nitems, p->capitems);
    memset(&p->items[p->nitems], 0, sizeof(Item));
    return p->nitems++;
}

static uint64_t parse_loop(Prog *p, int enclosing)
{
    uint64_t it, count, *tmp;
    uint32_t n, i;
    if (enclosing + 1 > MAX_DEPTH)
        perr("load/segment", "stream: LOOP nesting depth exceeds %d", MAX_DEPTH);
    count = rd64(p, "LOOP count");
    n = rd16(p, "LOOP n_items");
    if (n == 0) perr("load/segment", "stream: LOOP with n_items == 0");
    tmp = xmalloc(n * sizeof *tmp);
    for (i = 0; i < n; i++) {
        uint8_t ik = rd8(p, "LOOP item kind");
        if (ik == 0) {
            uint64_t o = parse_op(p, enclosing + 1);
            uint64_t c = new_item(p);
            p->items[c].op = (uint32_t)o;
            p->items[c].gated_sub = p->ops[o].gated;
            tmp[i] = c;
        } else if (ik == 1) {
            tmp[i] = parse_loop(p, enclosing + 1);
        } else {
            perr("load/segment", "stream: unknown LOOP item kind 0x%02x", ik);
        }
    }
    it = new_item(p);
    p->items[it].is_loop = 1;
    p->items[it].count = count;
    p->items[it].nchild = n;
    p->items[it].child0 = p->nchild;
    for (i = 0; i < n; i++) {
        GROW(p->child, p->nchild, p->capchild);
        p->child[p->nchild++] = tmp[i];
        if (p->items[tmp[i]].gated_sub) p->items[it].gated_sub = 1;
    }
    free(tmp);
    return it;
}

static void add_top(Prog *p, uint64_t it)
{
    GROW(p->top, p->ntop, p->captop);
    p->top[p->ntop++] = it;
}

static void parse(Prog *p)
{
    if (p->len < 8 || memcmp(p->buf, "OPSTRM01", 8) != 0)
        perr("load/bad-magic", "stream does not start with OPSTRM01");
    p->pos = 8;
    for (;;) {
        uint8_t seg;
        if (p->pos >= p->len) perr("load/truncated", "stream: EOF before END segment");
        seg = p->buf[p->pos++];
        if (seg == 0x01) {
            uint32_t r;
            if (p->saw_regs) perr("load/segment", "stream: duplicate REGS segment");
            if (p->saw_prog) perr("load/segment", "stream: REGS must precede every FLAT/LOOP");
            p->saw_regs = 1;
            p->nregs = rd32(p, "REGS n_regs");
            if ((uint64_t)p->nregs * 5u > p->len - p->pos) perr("load/truncated", "REGS truncated");
            p->regs = xcalloc(p->nregs, sizeof(Reg));
            for (r = 0; r < p->nregs; r++) {
                uint32_t w;
                uint8_t k = rd8(p, "REGS reg_kind");
                if (k > 1) perr("load/segment", "stream: unknown REGS reg_kind %u for register %u", k, r);
                p->regs[r].kind = k;
                p->regs[r].nw = rd32(p, "REGS n_wires");
                if ((uint64_t)p->regs[r].nw * 4u > p->len - p->pos) perr("load/truncated", "REGS truncated");
                p->regs[r].w = xmalloc((size_t)p->regs[r].nw * 4u);
                for (w = 0; w < p->regs[r].nw; w++) {
                    uint32_t id = rd32(p, "REGS wire id");
                    p->regs[r].w[w] = id;
                    if (k == 0) {
                        if ((uint64_t)id + 1 > p->reg_q) p->reg_q = (uint64_t)id + 1;
                    } else if ((uint64_t)id + 1 > p->reg_b) {
                        p->reg_b = (uint64_t)id + 1;
                    }
                }
            }
            if (p->reg_q > MAX_QUBIT_ADDR || p->reg_b > MAX_BIT_ADDR)
                perr("caps/address", "address space too large in REGS");
        } else if (seg == 0x02) {
            uint32_t id = rd32(p, "DATA table_id"), t;
            uint64_t nb = rd64(p, "DATA n_bits"), nbytes;
            for (t = 0; t < p->ntbl; t++)
                if (p->tbl[t].id == id) perr("load/segment", "stream: duplicate DATA table_id %u", id);
            nbytes = nb / 8u + ((nb % 8u) != 0);
            if (nbytes > (uint64_t)(p->len - p->pos)) /* before the size_t cast */
                perr("load/truncated", "stream truncated reading DATA bits");
            GROW(p->tbl, p->ntbl, p->captbl);
            p->tbl[p->ntbl].id = id;
            p->tbl[p->ntbl].nbits = nb;
            p->tbl[p->ntbl].bits = p->buf + p->pos;
            p->ntbl++;
            p->pos += (size_t)nbytes;
        } else if (seg == 0x03) {
            uint32_t n, i;
            p->saw_prog = 1;
            n = rd32(p, "FLAT n_ops");
            for (i = 0; i < n; i++) {
                uint64_t o = parse_op(p, 0), c = new_item(p);
                p->items[c].op = (uint32_t)o;
                p->items[c].gated_sub = p->ops[o].gated;
                add_top(p, c);
            }
        } else if (seg == 0x04) {
            p->saw_prog = 1;
            add_top(p, parse_loop(p, 0));
        } else if (seg == 0xFF) {
            p->end_count = rd64(p, "END expanded_op_count");
            if (p->pos != p->len) perr("load/segment", "stream: trailing bytes after END segment");
            return;
        } else {
            perr("load/segment", "stream: unknown segment type 0x%02x", seg);
        }
    }
}

/* saturating u64 arithmetic: UINT64_MAX marks a total that does not fit */
static uint64_t sat_mul(uint64_t a, uint64_t b)
{
    uint64_t r;
    return __builtin_mul_overflow(a, b, &r) ? UINT64_MAX : r;
}
static uint64_t sat_add(uint64_t a, uint64_t b)
{
    uint64_t r;
    return __builtin_add_overflow(a, b, &r) ? UINT64_MAX : r;
}

/* per-iteration analytic totals for ungated subtrees */
static void analyse(Prog *p, uint64_t it)
{
    Item *I = &p->items[it];
    uint32_t c;
    if (!I->is_loop) {
        const Op *o = &p->ops[I->op];
        int cls = bill_class(o->kind, (o->flags & 4) != 0);
        I->it_ops = 1;
        I->it_tof = cls == 2;
        I->it_cliff = cls == 1;
        return;
    }
    for (c = 0; c < I->nchild; c++) {
        uint64_t ch = p->child[I->child0 + c];
        Item *C;
        analyse(p, ch);
        C = &p->items[ch];
        I = &p->items[it];
        if (C->is_loop) {
            I->it_ops = sat_add(I->it_ops, sat_mul(C->it_ops, C->count));
            I->it_tof = sat_add(I->it_tof, sat_mul(C->it_tof, C->count));
            I->it_cliff = sat_add(I->it_cliff, sat_mul(C->it_cliff, C->count));
        } else {
            I->it_ops = sat_add(I->it_ops, C->it_ops);
            I->it_tof = sat_add(I->it_tof, C->it_tof);
            I->it_cliff = sat_add(I->it_cliff, C->it_cliff);
        }
    }
}

/* -------------------------------------------------------------- metrics */

typedef struct {
    uint64_t ops, tof, cliff;
    uint64_t nq, no, nb; /* census: max id + 1 */
} Census;

static void census_note(Census *c, int space, int64_t v)
{
    uint64_t u = (uint64_t)v + 1u;
    if (space == SP_QUBIT) {
        if (u > c->nq) c->nq = u;
    } else if (space == SP_OUT) {
        if (u > c->no) c->no = u;
    } else if (u > c->nb) {
        c->nb = u;
    }
}

/* *acc += c * i, exactly; returns 1 if the result does not fit in int64 */
static int mul_add(int64_t *acc, int64_t c, uint64_t i)
{
    int64_t t;
    if (c == 0 || i == 0) return 0;
    if (i > (uint64_t)INT64_MAX || __builtin_mul_overflow(c, (int64_t)i, &t)) return 1;
    return __builtin_add_overflow(*acc, t, acc);
}

/* base + sum coef_j * idx_j; returns 1 on int64 overflow (the exact value is then far
 * outside every id and table range, which is how the callers report it) */
static int aff_val(const int64_t *coef, int depth, const uint64_t *idx, int64_t *out)
{
    int64_t v = coef[0];
    int j;
    for (j = 0; j < depth; j++)
        if (mul_add(&v, coef[1 + j], idx[j])) return 1;
    *out = v;
    return 0;
}

/* the operand-distinctness rule (validate/aliasing); v[] is -1 for an unused slot */
static int operands_alias(const int64_t *v)
{
    return (v[S_QT] >= 0 && (v[S_QT] == v[S_QB] || v[S_QT] == v[S_QA])) ||
           (v[S_QB] >= 0 && v[S_QB] == v[S_QA]) || (v[S_OT] >= 0 && v[S_OT] == v[S_OC]) ||
           (v[S_CT] >= 0 && v[S_CT] == v[S_CC]);
}

static int gate_on(const Prog *p, const Op *o, const uint64_t *idx, uint64_t opi)
{
    const Table *t = &p->tbl[o->tbl];
    int64_t b;
    if (aff_val(&p->pool[o->poff], o->depth, idx, &b))
        perr("validate/table-index", "data-gate bit index overflows int64, outside [0, %" PRIu64
             ") (table %u) at expanded op %" PRIu64, t->nbits, t->id, opi);
    if (b < 0 || (uint64_t)b >= t->nbits)
        perr("validate/table-index", "data-gate bit index %" PRId64 " out of [0, %" PRIu64
             ") (table %u) at expanded op %" PRIu64, b, t->nbits, t->id, opi);
    return (t->bits[(uint64_t)b >> 3] >> ((uint64_t)b & 7u)) & 1;
}

/* Analytic census of an ungated subtree: idx[0..fix) fixed, deeper loops
 * range over [0, count). cnt[j] are the loop counts on the current path. */
static void census_sub(const Prog *p, uint64_t it, int depth, int fix, uint64_t *idx, uint64_t *cnt,
                       Census *c)
{
    const Item *I = &p->items[it];
    if (I->is_loop) {
        uint32_t k;
        if (I->count == 0) return;
        cnt[depth] = I->count;
        for (k = 0; k < I->nchild; k++)
            census_sub(p, p->child[I->child0 + k], depth + 1, fix, idx, cnt, c);
        return;
    } else {
        const Op *o = &p->ops[I->op];
        const int64_t *cf = &p->pool[o->poff];
        int j, d;
        for (j = 0; j < o->nop; j++) {
            const int64_t *a = cf + (size_t)j * (1u + o->depth);
            int64_t lo = a[0], hi = a[0];
            int ovf = 0;
            for (d = 0; d < o->depth && !ovf; d++) {
                if (d < fix) {
                    ovf = mul_add(&lo, a[1 + d], idx[d]) || mul_add(&hi, a[1 + d], idx[d]);
                } else {
                    int64_t span = 0;
                    ovf = mul_add(&span, a[1 + d], cnt[d] - 1u) ||
                          (span > 0 ? __builtin_add_overflow(hi, span, &hi)
                                    : __builtin_add_overflow(lo, span, &lo));
                }
            }
            if (ovf)
                perr("validate/id-range", "operand of %s resolves outside [0, 2^32) (int64 overflow)",
                     KIND_NAME[o->kind]);
            if (lo < 0 || hi >= ID_LIMIT)
                perr("validate/id-range", "operand of %s resolves outside [0, 2^32) (range %"
                     PRId64 "..%" PRId64 ")", KIND_NAME[o->kind], lo, hi);
            census_note(c, SLOT_SPACE[o->slot[j]], hi);
        }
    }
}

static void count_items(const Prog *p, const uint64_t *list, uint64_t n, int depth, uint64_t *idx,
                        uint64_t *cnt, Census *c);

static void count_item(const Prog *p, uint64_t it, int depth, uint64_t *idx, uint64_t *cnt, Census *c)
{
    const Item *I = &p->items[it];
    if (I->is_loop) {
        uint64_t i;
        if (I->count == 0) return;
        if (!I->gated_sub) {
            c->ops = sat_add(c->ops, sat_mul(I->it_ops, I->count));
            c->tof = sat_add(c->tof, sat_mul(I->it_tof, I->count));
            c->cliff = sat_add(c->cliff, sat_mul(I->it_cliff, I->count));
            if (c->ops == UINT64_MAX)
                die("expanded op count reaches 2^64 - 1; ucverify cannot count this stream");
            census_sub(p, it, depth, depth, idx, cnt, c);
            return;
        }
        cnt[depth] = I->count;
        for (i = 0; i < I->count; i++) {
            idx[depth] = i;
            count_items(p, &p->child[I->child0], I->nchild, depth + 1, idx, cnt, c);
        }
        return;
    } else {
        const Op *o = &p->ops[I->op];
        const int64_t *cf = &p->pool[o->poff];
        int64_t sv[S_N];
        int j, cls;
        if (o->gated) {
            if (!gate_on(p, o, idx, c->ops)) return;
            cf += 1 + o->depth;
        }
        for (j = 0; j < S_N; j++) sv[j] = -1;
        for (j = 0; j < o->nop; j++) {
            int64_t v;
            if (aff_val(cf + (size_t)j * (1u + o->depth), o->depth, idx, &v))
                perr("validate/id-range", "operand overflows int64, outside [0, 2^32), at"
                     " expanded op %" PRIu64, c->ops);
            if (v < 0 || v >= ID_LIMIT)
                perr("validate/id-range", "operand resolves to %" PRId64 ", outside [0, 2^32), at"
                     " expanded op %" PRIu64, v, c->ops);
            census_note(c, SLOT_SPACE[o->slot[j]], v);
            sv[o->slot[j]] = v;
        }
        if (operands_alias(sv))
            perr("validate/aliasing", "%s: operands not distinct at expanded op %" PRIu64,
                 KIND_NAME[o->kind], c->ops);
        cls = bill_class(o->kind, (o->flags & 4) != 0);
        c->ops++;
        if (cls == 2) c->tof++;
        else if (cls == 1) c->cliff++;
    }
}

static void count_items(const Prog *p, const uint64_t *list, uint64_t n, int depth, uint64_t *idx,
                        uint64_t *cnt, Census *c)
{
    uint64_t i;
    for (i = 0; i < n; i++) count_item(p, list[i], depth, idx, cnt, c);
}

static void metrics(const Prog *p, Census *c)
{
    uint64_t idx[MAX_DEPTH + 1] = {0}, cnt[MAX_DEPTH + 1] = {0};
    memset(c, 0, sizeof *c);
    c->nq = p->reg_q;
    c->nb = p->reg_b;
    count_items(p, p->top, p->ntop, 0, idx, cnt, c);
    if (c->nq > MAX_QUBIT_ADDR || c->no > MAX_OUT_ADDR || c->nb > MAX_BIT_ADDR)
        perr("caps/address", "address space too large: %" PRIu64 " qubits / %" PRIu64
             " outcomes / %" PRIu64 " bits", c->nq, c->no, c->nb);
    if (p->end_count != UNKNOWN_COUNT && p->end_count != c->ops)
        perr("load/count-mismatch", "expanded op count mismatch: got %" PRIu64 ", END declared %"
             PRIu64, c->ops, p->end_count);
}

/* ------------------------------------------------------------ simulation */

typedef struct {
    uint32_t n, cap;
    uint32_t one;
    uint32_t *v;
} DSet; /* an outcome slot: GF(2) sum of draw ids */

typedef struct {
    const Prog *p;
    int W; /* words per wire */
    uint64_t *q, *b, *live, *phase, *outdep, *rdirty;
    DSet *slot;
    uint64_t nq, nb, no;
    /* draws */
    uint64_t *res;
    uint32_t *refc, *freel;
    uint32_t ndraw, capdraw, nfree;
    /* counters */
    uint64_t ops, tof, cliff;
    /* mutation: skip the drop_nth-th expanded op of kind drop_kind */
    int drop_kind;
    uint64_t drop_nth, kind_seen, dropped_at;
    int dropped;
} Sim;

static uint32_t *dset_ptr(DSet *s) { return s->cap ? s->v : &s->one; }

static uint32_t draw_new(Sim *s)
{
    uint32_t d;
    if (s->nfree) {
        d = s->freel[--s->nfree];
    } else {
        if (s->ndraw == s->capdraw) {
            s->capdraw = s->capdraw ? s->capdraw * 2 : 1024;
            s->res = xrealloc(s->res, (size_t)s->capdraw * (size_t)s->W * 8u);
            s->refc = xrealloc(s->refc, (size_t)s->capdraw * 4u);
            s->freel = xrealloc(s->freel, (size_t)s->capdraw * 4u);
        }
        d = s->ndraw++;
    }
    s->refc[d] = 0;
    return d;
}

static void draw_release(Sim *s, uint32_t d)
{
    uint64_t *r = s->res + (size_t)d * (size_t)s->W;
    int w;
    for (w = 0; w < s->W; w++) s->outdep[w] |= r[w];
    s->freel[s->nfree++] = d;
}

static void dset_clear(Sim *s, DSet *st)
{
    uint32_t *v = dset_ptr(st), i;
    for (i = 0; i < st->n; i++)
        if (--s->refc[v[i]] == 0) draw_release(s, v[i]);
    st->n = 0;
}

static void dset_toggle(Sim *s, DSet *st, uint32_t d)
{
    uint32_t *v = dset_ptr(st), i;
    for (i = 0; i < st->n; i++)
        if (v[i] == d) {
            v[i] = v[--st->n];
            s->refc[d]--; /* cannot reach 0: the source slot still carries d */
            return;
        }
    if (st->n == (st->cap ? st->cap : 1u)) {
        uint32_t ncap = st->cap ? st->cap * 2 : 4;
        uint32_t *nv = xmalloc(ncap * 4u);
        memcpy(nv, v, st->n * 4u);
        if (st->cap) free(st->v);
        st->v = nv;
        st->cap = ncap;
        v = nv;
    }
    v[st->n++] = d;
    s->refc[d]++;
}

static void sim_exec_op(Sim *s, const Op *o, const uint64_t *idx)
{
    const Prog *p = s->p;
    const int64_t *cf = &p->pool[o->poff];
    int64_t v[S_N];
    int64_t val[S_N];
    int j, W = s->W, w, cls;
    uint64_t opi = s->ops;
    if (o->gated) {
        if (!gate_on(p, o, idx, opi)) return;
        cf += 1 + o->depth;
    }
    for (j = 0; j < S_N; j++) v[j] = -1;
    for (j = 0; j < o->nop; j++) {
        if (aff_val(cf + (size_t)j * (1u + o->depth), o->depth, idx, &val[j]))
            perr("validate/id-range", "operand overflows int64, outside [0, 2^32), at "
                 "expanded op %" PRIu64, opi);
        if (val[j] < 0 || val[j] >= ID_LIMIT)
            perr("validate/id-range", "operand resolves to %" PRId64 ", outside [0, 2^32), at "
                 "expanded op %" PRIu64, val[j], opi);
        v[o->slot[j]] = val[j];
    }
    if (operands_alias(v))
        perr("validate/aliasing", "%s: operands not distinct at expanded op %" PRIu64,
             KIND_NAME[o->kind], opi);
    s->ops++;
    cls = bill_class(o->kind, (o->flags & 4) != 0);
    if (cls == 2) s->tof++;
    else if (cls == 1) s->cliff++;

    if (s->drop_kind == o->kind) {
        if (s->kind_seen++ == s->drop_nth) {
            s->dropped = 1;
            s->dropped_at = opi;
            return; /* mutation: this op does not act (it is still billed) */
        }
    }

    {
        const uint64_t *live = s->live;
        const uint64_t *cc = v[S_CC] >= 0 ? s->b + (size_t)v[S_CC] * W : NULL;
        uint64_t *qt = v[S_QT] >= 0 ? s->q + (size_t)v[S_QT] * W : NULL;
        const uint64_t *qa = v[S_QA] >= 0 ? s->q + (size_t)v[S_QA] * W : NULL;
        uint64_t *qb = v[S_QB] >= 0 ? s->q + (size_t)v[S_QB] * W : NULL;
        switch (o->kind) {
        case K_X:
            for (w = 0; w < W; w++) qt[w] ^= live[w] & (cc ? cc[w] : ~0ull);
            break;
        case K_CX:
            for (w = 0; w < W; w++) qt[w] ^= live[w] & (cc ? cc[w] : ~0ull) & qb[w];
            break;
        case K_CCX:
            for (w = 0; w < W; w++) qt[w] ^= live[w] & qa[w] & qb[w];
            break;
        case K_SWAP:
            for (w = 0; w < W; w++) {
                uint64_t d = (qb[w] ^ qt[w]) & live[w];
                qb[w] ^= d;
                qt[w] ^= d;
            }
            break;
        case K_NEG: case K_Z: case K_CZ: case K_CCZ: {
            uint64_t *tgt = s->phase;
            uint32_t *ds = NULL, nd = 0, i;
            if (v[S_OC] >= 0) {
                DSet *st = &s->slot[v[S_OC]];
                ds = dset_ptr(st);
                nd = st->n;
                if (nd == 0) break; /* an outcome never written is 0: no flip */
            }
            for (w = 0; w < W; w++) {
                uint64_t m = live[w] & (cc ? cc[w] : ~0ull);
                if (qt) m &= qt[w];
                if (qb) m &= qb[w];
                if (qa) m &= qa[w];
                if (!m) continue;
                if (v[S_OC] < 0) {
                    tgt[w] ^= m;
                } else {
                    for (i = 0; i < nd; i++) s->res[(size_t)ds[i] * W + (size_t)w] ^= m;
                }
            }
            break;
        }
        case K_HMR: {
            DSet *st = &s->slot[v[S_OT]];
            uint32_t d;
            uint64_t *r;
            dset_clear(s, st); /* the slot's previous draws lose this carrier */
            d = draw_new(s);
            r = s->res + (size_t)d * W;
            for (w = 0; w < W; w++) {
                r[w] = qt[w] & live[w];
                qt[w] &= ~live[w];
            }
            dset_ptr(st)[0] = d;
            st->n = 1;
            s->refc[d] = 1;
            break;
        }
        case K_R:
            for (w = 0; w < W; w++) {
                uint64_t x = qt[w] & live[w];
                s->outdep[w] |= x;
                s->rdirty[w] |= x;
                qt[w] &= ~live[w];
            }
            break;
        case K_OXOR: {
            DSet *src = &s->slot[v[S_OC]], *dst = &s->slot[v[S_OT]];
            uint32_t n = src->n, i;
            uint32_t *tmp = xmalloc((n ? n : 1u) * 4u);
            memcpy(tmp, dset_ptr(src), n * 4u);
            for (i = 0; i < n; i++) dset_toggle(s, dst, tmp[i]);
            free(tmp);
            break;
        }
        case K_BIT_INVERT: {
            uint64_t *ct = s->b + (size_t)v[S_CT] * W;
            for (w = 0; w < W; w++) ct[w] ^= live[w] & (cc ? cc[w] : ~0ull);
            break;
        }
        case K_BIT_STORE0: {
            uint64_t *ct = s->b + (size_t)v[S_CT] * W;
            for (w = 0; w < W; w++) ct[w] &= ~(live[w] & (cc ? cc[w] : ~0ull));
            break;
        }
        case K_BIT_STORE1: {
            uint64_t *ct = s->b + (size_t)v[S_CT] * W;
            for (w = 0; w < W; w++) ct[w] |= live[w] & (cc ? cc[w] : ~0ull);
            break;
        }
        default:
            die("internal: kind %d", o->kind);
        }
    }
}

static void sim_items(Sim *s, const uint64_t *list, uint64_t n, int depth, uint64_t *idx)
{
    const Prog *p = s->p;
    uint64_t i, r;
    for (i = 0; i < n; i++) {
        const Item *I = &p->items[list[i]];
        if (I->is_loop) {
            for (r = 0; r < I->count; r++) {
                idx[depth] = r;
                sim_items(s, &p->child[I->child0], I->nchild, depth + 1, idx);
            }
        } else {
            sim_exec_op(s, &p->ops[I->op], idx);
        }
    }
}

/* ------------------------------------------------------------- adapters */

#define MAXREG 512

typedef struct {
    int nregs;
    uint32_t width[MAXREG];
    uint8_t kind[MAXREG];
    uint64_t band[MAXREG]; /* centered band on the expected value (widths <= 64) */
} Schema;

typedef struct {
    uint8_t has_in[MAXREG], has_exp[MAXREG];
    uint8_t *in[MAXREG], *exp[MAXREG]; /* little-endian bytes */
    int exp_phase;
} TC;

typedef struct Adapter {
    const char *name;
    Schema sc;
    uint64_t n_lanes;
    int (*gen)(struct Adapter *a, uint64_t lane, TC *tc); /* 0 = dead lane */
    void (*describe)(struct Adapter *a, uint64_t lane, const TC *tc, uint8_t *const *rd, FILE *f);
    void *ctx;
} Adapter;

static uint64_t le_get(const uint8_t *b, uint32_t width)
{
    uint64_t v = 0;
    uint32_t i, nb = (width + 7u) / 8u;
    if (nb > 8) nb = 8;
    for (i = 0; i < nb; i++) v |= (uint64_t)b[i] << (8 * i);
    if (width < 64) v &= (((uint64_t)1) << width) - 1u;
    return v;
}
static void le_put(uint8_t *b, uint32_t width, uint64_t v)
{
    uint32_t i, nb = (width + 7u) / 8u;
    for (i = 0; i < nb; i++) b[i] = i < 8 ? (uint8_t)(v >> (8 * i)) : 0;
}

/* k patterns, fixed so that lane counts are reproducible. splitmix is the
 * public SplitMix64 finalizer (Steele, Lea, Flood 2014) and xormix is
 * Fibonacci hashing by the 64-bit golden ratio. kneg:D sets k = D - f(X), which makes
 * the ideal result equal D, and kpow:J sets k = 2^J - f(X). */
typedef struct {
    int id; /* 0 zero, 1 ones, 2 xormix, 3 splitmix, 4 kneg, 5 random */
    uint64_t seed;
    char text[64];
} KMode;

static int kmode_parse(const char *t, KMode *m)
{
    memset(m, 0, sizeof *m);
    snprintf(m->text, sizeof m->text, "%s", t);
    if (!strcmp(t, "zero")) m->id = 0;
    else if (!strcmp(t, "ones")) m->id = 1;
    else if (!strcmp(t, "xormix")) m->id = 2;
    else if (!strncmp(t, "splitmix:", 9)) { m->id = 3; m->seed = strtoull(t + 9, NULL, 0); }
    else if (!strncmp(t, "kneg:", 5)) { m->id = 4; m->seed = (uint64_t)strtoll(t + 5, NULL, 0); }
    else if (!strncmp(t, "random:", 7)) { m->id = 5; m->seed = strtoull(t + 7, NULL, 0); }
    else return -1;
    return 0;
}

static uint64_t splitmix(uint64_t z)
{
    z += 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

/* f = the ideal addend for this input (used by kneg). */
static uint64_t kmode_value(const KMode *m, uint64_t x, uint32_t width, uint64_t f)
{
    uint64_t mask = width >= 64 ? ~0ull : (((uint64_t)1) << width) - 1u;
    switch (m->id) {
    case 1: return mask;
    case 2: return ((x * 0x9E3779B97F4A7C15ull) >> (64 - width)) & mask;
    case 3: return splitmix((m->seed << 32) ^ x) & mask;
    case 4: return (m->seed - f) & mask;
    case 5: return splitmix(splitmix(m->seed) ^ x) & mask;
    default: return 0;
    }
}

/* --- hhl-angle: |X>|k> -> |X>|k + round(2^32 asin(2^15/X)) mod 2^33> */
typedef struct {
    KMode km;
    int sampled; /* 0 = exhaustive over X in [2^16, 2^21) */
    uint64_t seed;
} HhlCtx;

static uint64_t hhl_f(uint64_t x) { return (uint64_t)round(asin(32768.0 / (double)x) * 4294967296.0); }

static int hhl_gen(Adapter *a, uint64_t lane, TC *tc)
{
    HhlCtx *c = a->ctx;
    uint64_t x, f, k;
    if (c->sampled) x = 65536u + splitmix(c->seed ^ splitmix(lane)) % 2031616u;
    else x = 65536u + lane;
    f = hhl_f(x);
    k = kmode_value(&c->km, x, 33, f);
    tc->has_in[0] = tc->has_in[1] = tc->has_exp[0] = tc->has_exp[1] = 1;
    le_put(tc->in[0], 21, x);
    le_put(tc->in[1], 33, k);
    le_put(tc->exp[0], 21, x);
    le_put(tc->exp[1], 33, (k + f) & ((1ull << 33) - 1u));
    tc->exp_phase = 0;
    return 1;
}

static void hhl_desc(Adapter *a, uint64_t lane, const TC *tc, uint8_t *const *rd, FILE *f)
{
    (void)a; (void)lane;
    fprintf(f, "\"X\": %" PRIu64 ", \"k\": %" PRIu64 ", \"k_out\": %" PRIu64 ", \"k_ideal\": %" PRIu64
            ", \"X_out\": %" PRIu64,
            le_get(tc->in[0], 21), le_get(tc->in[1], 33), le_get(rd[1], 33), le_get(tc->exp[1], 33),
            le_get(rd[0], 21));
}

/* --- coulomb-potential: exhaustive over all 2^24 r_j for a fixed r_i */
typedef struct {
    KMode km;
    uint8_t ri[3];
} CouCtx;

static uint64_t cou_F(uint64_t n2) { return (uint64_t)round(16777216.0 / sqrt((double)n2)); }

static int cou_gen(Adapter *a, uint64_t lane, TC *tc)
{
    CouCtx *c = a->ctx;
    uint8_t rj[3];
    int i;
    int64_t n2 = 0;
    uint64_t f, k;
    rj[0] = (uint8_t)lane;
    rj[1] = (uint8_t)(lane >> 8);
    rj[2] = (uint8_t)(lane >> 16);
    for (i = 0; i < 3; i++) n2 += ((int64_t)c->ri[i] - rj[i]) * ((int64_t)c->ri[i] - rj[i]);
    if (n2 == 0) return 0; /* r_i = r_j is outside the domain */
    f = cou_F((uint64_t)n2);
    k = kmode_value(&c->km, lane ^ ((uint64_t)c->ri[0] << 24 | (uint64_t)c->ri[1] << 32 |
                                    (uint64_t)c->ri[2] << 40), 25, f);
    for (i = 0; i < 7; i++) tc->has_in[i] = tc->has_exp[i] = 1;
    for (i = 0; i < 3; i++) {
        le_put(tc->in[i], 8, c->ri[i]);
        le_put(tc->exp[i], 8, c->ri[i]);
        le_put(tc->in[3 + i], 8, rj[i]);
        le_put(tc->exp[3 + i], 8, rj[i]);
    }
    le_put(tc->in[6], 25, k);
    le_put(tc->exp[6], 25, (k + f) & ((1ull << 25) - 1u));
    tc->exp_phase = 0;
    return 1;
}

static void cou_desc(Adapter *a, uint64_t lane, const TC *tc, uint8_t *const *rd, FILE *f)
{
    int i;
    (void)a; (void)lane;
    fprintf(f, "\"ri\": [%u,%u,%u], \"rj\": [%u,%u,%u], \"k\": %" PRIu64 ", \"k_out\": %" PRIu64
            ", \"k_ideal\": %" PRIu64 ", \"coords_ok\": ",
            tc->in[0][0], tc->in[1][0], tc->in[2][0], tc->in[3][0], tc->in[4][0], tc->in[5][0],
            le_get(tc->in[6], 25), le_get(rd[6], 25), le_get(tc->exp[6], 25));
    for (i = 0; i < 6; i++)
        if (rd[i][0] != tc->in[i][0]) break;
    fprintf(f, "%s", i == 6 ? "true" : "false");
}

/* --- gaussian: |X>|k> -> |X>|k + round(2^32 exp(-(X/2^32)^2)) mod 2^39>,
 * X a 39-bit two's-complement pattern with X/2^32 in [-8, 8). Sampled. */
typedef struct {
    KMode km;
    uint64_t seed;
    int sweep; /* 1 = deterministic strided sweep over the 2^36 domain */
} GauCtx;

static uint64_t gau_f(int64_t xs)
{
    double x = (double)xs / 4294967296.0;
    return (uint64_t)round(exp(-(x * x)) * 4294967296.0);
}

static int gau_gen(Adapter *a, uint64_t lane, TC *tc)
{
    GauCtx *c = a->ctx;
    uint64_t u, pat, f, k, m39 = (1ull << 39) - 1u;
    int64_t xs;
    if (c->sweep) u = (lane * 0x9E3779B97F4A7C15ull) >> 28; /* 36-bit low-discrepancy walk */
    else u = splitmix(c->seed ^ splitmix(lane)) >> 28;
    xs = (int64_t)u - ((int64_t)8 << 32); /* [-2^35, 2^35) */
    pat = (uint64_t)xs & m39;
    f = gau_f(xs);
    k = kmode_value(&c->km, pat, 39, f);
    tc->has_in[0] = tc->has_in[1] = tc->has_exp[0] = tc->has_exp[1] = 1;
    le_put(tc->in[0], 39, pat);
    le_put(tc->in[1], 39, k);
    le_put(tc->exp[0], 39, pat);
    le_put(tc->exp[1], 39, (k + f) & m39);
    tc->exp_phase = 0;
    return 1;
}

static void gau_desc(Adapter *a, uint64_t lane, const TC *tc, uint8_t *const *rd, FILE *f)
{
    (void)a; (void)lane;
    fprintf(f, "\"X\": %" PRIu64 ", \"k\": %" PRIu64 ", \"k_out\": %" PRIu64 ", \"k_ideal\": %" PRIu64,
            le_get(tc->in[0], 39), le_get(tc->in[1], 39), le_get(rd[1], 39), le_get(tc->exp[1], 39));
}

/* --- generic vectors: UCVEC001 file (see README) */
typedef struct {
    uint8_t *buf;
    size_t len;
    uint64_t *off; /* lane record offsets */
} VecCtx;

static int vec_gen(Adapter *a, uint64_t lane, TC *tc)
{
    VecCtx *c = a->ctx;
    const uint8_t *p = c->buf + c->off[lane];
    int r;
    if (!*p++) return 0;
    tc->exp_phase = *p++;
    for (r = 0; r < a->sc.nregs; r++) {
        uint32_t nb = (a->sc.width[r] + 7u) / 8u;
        uint8_t fl = *p++;
        tc->has_in[r] = fl & 1;
        tc->has_exp[r] = (fl >> 1) & 1;
        if (fl & 1) { memcpy(tc->in[r], p, nb); p += nb; }
        if (fl & 2) { memcpy(tc->exp[r], p, nb); p += nb; }
    }
    return 1;
}

static void vec_desc(Adapter *a, uint64_t lane, const TC *tc, uint8_t *const *rd, FILE *f)
{
    int r, nbad = 0;
    (void)lane;
    fprintf(f, "\"bad_regs\": [");
    for (r = 0; r < a->sc.nregs; r++) {
        if (!tc->has_exp[r]) continue;
        if (memcmp(rd[r], tc->exp[r], (a->sc.width[r] + 7u) / 8u)) {
            if (nbad++ < 8) fprintf(f, "%s%d", nbad > 1 ? "," : "", r);
        }
    }
    fprintf(f, "]");
}

static void load_vectors(Adapter *a, const char *path)
{
    VecCtx *c = xcalloc(1, sizeof *c);
    FILE *f = fopen(path, "rb");
    size_t pos, cap = 1 << 20;
    uint64_t L, i;
    int r;
    if (!f) die("cannot open %s", path);
    c->buf = xmalloc(cap);
    for (;;) {
        size_t got = fread(c->buf + c->len, 1, cap - c->len, f);
        c->len += got;
        if (c->len < cap) break;
        cap *= 2;
        c->buf = xrealloc(c->buf, cap);
    }
    fclose(f);
    if (c->len < 20 || memcmp(c->buf, "UCVEC001", 8)) die("%s: not a UCVEC001 file", path);
    pos = 8;
#define VGET(nbytes, out)                                                    \
    do {                                                                     \
        uint64_t _v = 0;                                                     \
        int _i;                                                              \
        if (pos + (nbytes) > c->len) die("%s: truncated", path);             \
        for (_i = (nbytes) - 1; _i >= 0; _i--) _v = (_v << 8) | c->buf[pos + (size_t)_i]; \
        pos += (nbytes);                                                     \
        (out) = _v;                                                          \
    } while (0)
    {
        uint64_t nr;
        VGET(4, nr);
        if (nr > MAXREG) die("too many registers");
        a->sc.nregs = (int)nr;
    }
    for (r = 0; r < a->sc.nregs; r++) {
        uint64_t w, k, b;
        VGET(4, w);
        VGET(1, k);
        VGET(8, b);
        a->sc.width[r] = (uint32_t)w;
        a->sc.kind[r] = (uint8_t)k;
        a->sc.band[r] = b;
    }
    VGET(8, L);
    /* every lane takes at least one byte: bound L before allocating */
    if (L > c->len - pos || L > SIZE_MAX / 8u) die("%s: truncated (%" PRIu64 " lanes declared)", path, L);
    c->off = xmalloc((size_t)(L ? L : 1) * 8u);
    for (i = 0; i < L; i++) {
        if (pos >= c->len) die("%s: truncated lane %" PRIu64, path, i);
        c->off[i] = pos;
        if (c->buf[pos++]) {
            pos++;
            for (r = 0; r < a->sc.nregs; r++) {
                uint32_t nb = (a->sc.width[r] + 7u) / 8u;
                uint8_t fl;
                if (pos >= c->len) die("%s: truncated", path);
                fl = c->buf[pos++];
                if (fl & 1) pos += nb;
                if (fl & 2) pos += nb;
            }
        }
        if (pos > c->len) die("%s: truncated lane %" PRIu64, path, i);
    }
#undef VGET
    a->n_lanes = L;
    a->ctx = c;
    a->gen = vec_gen;
    a->describe = vec_desc;
    a->name = "vectors";
}

/* ------------------------------------------------------------ check run */

typedef struct {
    uint64_t dirty_qubits;
    uint64_t lanes, live, wrong_output, sign_const, sign_outdep, dirty_lanes, reset_dirty, failed;
    uint64_t *dirty_q; /* per qubit: lanes on which it ends nonzero */
    int listed;
} Result;

static int band_ok(const uint8_t *rd, const uint8_t *ex, uint32_t width, uint64_t band)
{
    uint32_t nb = (width + 7u) / 8u;
    if (band == 0 || width == 0 || width > 64) { /* width 0: nothing to compare */
        uint32_t i;
        for (i = 0; i < nb; i++) {
            uint8_t m = 0xFF;
            if (i == nb - 1 && width % 8u) m = (uint8_t)((1u << (width % 8u)) - 1u);
            if ((rd[i] ^ ex[i]) & m) return 0;
        }
        return 1;
    } else {
        uint64_t mod_mask = width == 64 ? ~0ull : (((uint64_t)1) << width) - 1u;
        uint64_t d = (le_get(rd, width) - le_get(ex, width)) & mod_mask;
        uint64_t half = ((uint64_t)1) << (width - 1);
        /* centered residue in (-2^(w-1), 2^(w-1)] */
        if (d > half) d = (mod_mask - d) + 1u; /* magnitude of the negative representative */
        return d <= band;
    }
}

static void run_check(const Prog *p, const Census *cs, Adapter *a, int W, int list_max,
                      int drop_kind, uint64_t drop_nth, Result *res, FILE *out)
{
    Sim s;
    TC tc;
    uint64_t L = (uint64_t)W * 64u, base, idx[MAX_DEPTH + 1];
    uint8_t *rd[MAXREG];
    uint8_t *isreg_q;
    uint64_t *dirtyw;
    int r;
    uint32_t i;

    /* schema: the stream must declare exactly the task's registers */
    if ((int)p->nregs != a->sc.nregs) {
        fprintf(out, "{\"ok\": false, \"fail_code\": \"schema/count\", \"reason\": \"expected %d registers, got %u\"}\n",
                a->sc.nregs, p->nregs);
        exit(1);
    }
    for (r = 0; r < a->sc.nregs; r++) {
        if (p->regs[r].kind != a->sc.kind[r] || p->regs[r].nw != a->sc.width[r]) {
            fprintf(out, "{\"ok\": false, \"fail_code\": \"schema/width\", \"reason\": \"register %d is kind %u width %u, task wants kind %u width %u\"}\n",
                    r, p->regs[r].kind, p->regs[r].nw, a->sc.kind[r], a->sc.width[r]);
            exit(1);
        }
    }
    memset(res, 0, sizeof *res);
    memset(&s, 0, sizeof s);
    s.p = p;
    s.W = W;
    s.nq = cs->nq;
    s.nb = cs->nb;
    s.no = cs->no;
    if ((uint64_t)s.nq * (uint64_t)W >= SIZE_MAX / 8u || (uint64_t)s.nb * (uint64_t)W >= SIZE_MAX / 8u)
        die("simulation state (%" PRIu64 " qubits, %" PRIu64 " bits, %d words) exceeds this"
            " build's address space", s.nq, s.nb, W);
    s.q = xcalloc((size_t)s.nq * W + 1, 8);
    s.b = xcalloc((size_t)s.nb * W + 1, 8);
    s.slot = xcalloc((size_t)s.no + 1, sizeof(DSet));
    s.live = xcalloc((size_t)W, 8);
    s.phase = xcalloc((size_t)W, 8);
    s.outdep = xcalloc((size_t)W, 8);
    s.rdirty = xcalloc((size_t)W, 8);
    s.drop_kind = drop_kind;
    s.drop_nth = drop_nth;
    res->dirty_q = xcalloc((size_t)s.nq + 1, 8);
    isreg_q = xcalloc((size_t)s.nq + 1, 1);
    dirtyw = xcalloc((size_t)W, 8);
    for (r = 0; r < (int)p->nregs; r++)
        if (p->regs[r].kind == 0)
            for (i = 0; i < p->regs[r].nw; i++) isreg_q[p->regs[r].w[i]] = 1;
    for (r = 0; r < a->sc.nregs; r++) {
        size_t nb = (a->sc.width[r] + 7u) / 8u + 8u;
        tc.in[r] = xcalloc(nb, 1);
        tc.exp[r] = xcalloc(nb, 1);
        rd[r] = xcalloc(nb, 1);
    }

    for (base = 0; base < a->n_lanes; base += L) {
        uint64_t n = a->n_lanes - base < L ? a->n_lanes - base : L, l;
        uint64_t q;
        memset(s.q, 0, (size_t)s.nq * W * 8u);
        memset(s.b, 0, (size_t)s.nb * W * 8u);
        memset(s.live, 0, (size_t)W * 8u);
        memset(s.phase, 0, (size_t)W * 8u);
        memset(s.outdep, 0, (size_t)W * 8u);
        memset(s.rdirty, 0, (size_t)W * 8u);
        for (q = 0; q < s.no; q++) s.slot[q].n = 0;
        s.ndraw = s.nfree = 0;
        s.ops = s.tof = s.cliff = 0;
        s.kind_seen = 0;
        s.dropped = 0;
        /* load inputs (normative order: wire i <- bit i, last write wins) */
        for (l = 0; l < n; l++) {
            uint64_t bit = 1ull << (l & 63u);
            size_t wd = (size_t)(l >> 6);
            memset(tc.has_in, 0, sizeof tc.has_in);
            memset(tc.has_exp, 0, sizeof tc.has_exp);
            if (!a->gen(a, base + l, &tc)) continue;
            s.live[wd] |= bit;
            for (r = 0; r < a->sc.nregs; r++) {
                const Reg *R = &p->regs[r];
                uint64_t *arr = R->kind == 0 ? s.q : s.b;
                if (!tc.has_in[r]) continue;
                for (i = 0; i < R->nw; i++) {
                    uint64_t *wp = arr + (size_t)R->w[i] * W + wd;
                    if ((tc.in[r][i >> 3] >> (i & 7u)) & 1u) *wp |= bit;
                    else *wp &= ~bit;
                }
            }
        }
        memset(idx, 0, sizeof idx);
        sim_items(&s, p->top, p->ntop, 0, idx);
        /* close every draw still carried by an outcome slot */
        for (q = 0; q < s.no; q++) dset_clear(&s, &s.slot[q]);
        if (s.ops != cs->ops || s.tof != cs->tof || s.cliff != cs->cliff)
            die("internal: simulated counts differ from analytic counts");
        if (drop_kind >= 0 && !s.dropped) die("mutation: fewer than %" PRIu64 "+1 %s ops", drop_nth, KIND_NAME[drop_kind]);
        if (drop_kind >= 0 && base == 0)
            fprintf(stderr, "ucverify: mutation dropped %s #%" PRIu64 " (expanded op %" PRIu64 ")\n",
                    KIND_NAME[drop_kind], drop_nth, s.dropped_at);
        /* dirty ancillas: non-register qubits nonzero at the end */
        memset(dirtyw, 0, (size_t)W * 8u);
        for (q = 0; q < s.nq; q++) {
            int w;
            if (isreg_q[q]) continue;
            for (w = 0; w < W; w++) {
                uint64_t x = s.q[q * W + (uint64_t)w] & s.live[w];
                if (x) {
                    dirtyw[w] |= x;
                    res->dirty_q[q] += (uint64_t)popc64(x);
                }
            }
        }
        /* verdicts */
        for (l = 0; l < n; l++) {
            uint64_t bit = 1ull << (l & 63u);
            size_t wd = (size_t)(l >> 6);
            int wo = 0, sc_ = 0, so = 0, dq = 0;
            res->lanes++;
            if (!(s.live[wd] & bit)) continue;
            res->live++;
            memset(tc.has_in, 0, sizeof tc.has_in);
            memset(tc.has_exp, 0, sizeof tc.has_exp);
            a->gen(a, base + l, &tc);
            for (r = 0; r < a->sc.nregs; r++) {
                const Reg *R = &p->regs[r];
                size_t nb = (R->nw + 7u) / 8u;
                if (!tc.has_exp[r]) continue;
                if (R->kind != 0)
                    die("register %d is Bit-kind but a test case carries an expected output for it"
                        " (Bit registers are input-only)", r);
                memset(rd[r], 0, nb);
                for (i = 0; i < R->nw; i++)
                    if ((s.q[(size_t)R->w[i] * W + wd] >> (l & 63u)) & 1u)
                        rd[r][i >> 3] |= (uint8_t)(1u << (i & 7u));
                if (!band_ok(rd[r], tc.exp[r], R->nw, a->sc.band[r])) wo = 1;
            }
            if (s.outdep[wd] & bit) so = 1;
            else if (((s.phase[wd] >> (l & 63u)) & 1u) != (uint64_t)(tc.exp_phase != 0)) sc_ = 1;
            if (s.rdirty[wd] & bit) res->reset_dirty++;
            if (dirtyw[wd] & bit) dq = 1;
            res->wrong_output += (uint64_t)wo;
            res->sign_const += (uint64_t)sc_;
            res->sign_outdep += (uint64_t)so;
            res->dirty_lanes += (uint64_t)dq;
            if (wo || sc_ || so || dq) {
                res->failed++;
                if (res->listed < list_max) {
                    for (r = 0; r < a->sc.nregs; r++)
                        if (!tc.has_exp[r]) {
                            const Reg *R = &p->regs[r];
                            memset(rd[r], 0, (R->nw + 7u) / 8u);
                        }
                    fprintf(out, "  {\"lane\": %" PRIu64 ", \"wrong_output\": %d, \"wrong_sign_const\": %d, "
                            "\"outcome_dependent_sign\": %d, \"dirty\": %d, ",
                            base + l, wo, sc_, so, dq);
                    a->describe(a, base + l, &tc, rd, out);
                    fprintf(out, "},\n");
                    res->listed++;
                }
            }
        }
    }
    for (i = 0; i < s.nq; i++)
        if (res->dirty_q[i]) res->dirty_qubits++;
    free(s.q); free(s.b); free(s.live); free(s.phase); free(s.outdep); free(s.rdirty);
    for (i = 0; i < s.no; i++)
        if (s.slot[i].cap) free(s.slot[i].v);
    free(s.slot); free(s.res); free(s.refc); free(s.freel); free(isreg_q); free(dirtyw);
    for (r = 0; r < a->sc.nregs; r++) { free(tc.in[r]); free(tc.exp[r]); free(rd[r]); }
}

/* ------------------------------------------------------------------ main */

static void read_stream(Prog *p, const char *path)
{
    FILE *f = strcmp(path, "-") ? fopen(path, "rb") : stdin;
    size_t cap = 1 << 20;
    Sha256 h;
    if (!f) die("cannot open %s", path);
    p->buf = xmalloc(cap);
    for (;;) {
        size_t got = fread(p->buf + p->len, 1, cap - p->len, f);
        p->len += got;
        if (p->len < cap) break;
        cap *= 2;
        p->buf = xrealloc(p->buf, cap);
    }
    if (f != stdin) fclose(f);
    sha_init(&h);
    sha_update(&h, p->buf, p->len);
    sha_final(&h, p->sha);
}

static void print_metrics(const Prog *p, const Census *c)
{
    double tq = (double)c->tof * (double)c->nq;
    printf("{\"h_ops\": \"%s\", \"metrics\": {\"toffoli\": %" PRIu64 ", \"clifford\": %" PRIu64
           ", \"qubits\": %" PRIu64 ", \"outcomes\": %" PRIu64 ", \"bits\": %" PRIu64 ", \"ops\": %"
           PRIu64 ", \"tq_cost\": %.0f}}\n",
           p->sha, c->tof, c->cliff, c->nq, c->no, c->nb, c->ops, tq);
}

/* Independent reference for reported lanes. Reads "X k k_out" (hhl-angle)
 * or "rix riy riz rjx rjy rjz k k_out" (coulomb-potential) per line and
 * recomputes the ideal from the README formula in extended precision
 * (long double asinl / sqrtl, a different code path from the f64 oracle). */
_Noreturn static void usage(void);
static int ref_main(int argc, char **argv)
{
    char line[512];
    int bad = 0;
    if (argc < 3) usage();
    while (fgets(line, sizeof line, stdin)) {
        if (!strcmp(argv[2], "hhl-angle")) {
            unsigned long long X, k, ko;
            long double fl;
            uint64_t f64v, fld, ideal, d, M = (1ull << 33) - 1u;
            int64_t dc;
            if (sscanf(line, "%llu %llu %llu", &X, &k, &ko) != 3) continue;
            if (X < 65536u || X > 2097151u) { printf("X=%llu out of domain\n", X); continue; }
            fl = asinl(32768.0L / (long double)X) * 4294967296.0L;
            fld = (uint64_t)roundl(fl);
            f64v = hhl_f(X);
            ideal = ((uint64_t)k + fld) & M;
            d = ((uint64_t)ko - ideal) & M;
            dc = d > (1ull << 32) ? (int64_t)d - (int64_t)(1ull << 33) : (int64_t)d;
            printf("X=%llu k=%llu k_out=%llu  f(X): extended=%.6Lf -> %" PRIu64 ", f64=%" PRIu64
                   "  delta=%" PRId64 "  %s\n", X, k, ko, fl, fld, f64v, dc,
                   (dc <= 429 && dc >= -429) ? "within band" : "OUTSIDE the 429-unit band");
            bad |= !(dc <= 429 && dc >= -429);
        } else if (!strcmp(argv[2], "coulomb-potential")) {
            unsigned a[6];
            unsigned long long k, ko;
            long double fl;
            int64_t n2 = 0, dc;
            uint64_t fld, d, M = (1ull << 25) - 1u;
            int i;
            if (sscanf(line, "%u %u %u %u %u %u %llu %llu", &a[0], &a[1], &a[2], &a[3], &a[4], &a[5], &k, &ko) != 8) continue;
            for (i = 0; i < 3; i++) n2 += ((int64_t)a[i] - a[3 + i]) * ((int64_t)a[i] - a[3 + i]);
            if (n2 == 0) { printf("r_i = r_j out of domain\n"); continue; }
            fl = 16777216.0L / sqrtl((long double)n2);
            fld = (uint64_t)roundl(fl);
            d = ((uint64_t)ko - (uint64_t)k - fld) & M;
            dc = d > (1ull << 24) ? (int64_t)d - (int64_t)(1ull << 25) : (int64_t)d;
            printf("n2=%" PRId64 " F: extended=%.6Lf -> %" PRIu64 "  delta=%" PRId64 "  %s\n", n2, fl, fld, dc,
                   (dc <= 32 && dc >= -32) ? "within band" : "OUTSIDE the 32-unit band");
            bad |= !(dc <= 32 && dc >= -32);
        } else {
            die("ref: unsupported task %s", argv[2]);
        }
    }
    return bad;
}

_Noreturn static void usage(void)
{
    fputs("usage:\n"
          "  ucverify count STREAM\n"
          "  ucverify check TASK STREAM [options]\n"
          "  ucverify ref TASK < lanes.txt   (independent recomputation of reported lanes)\n"
          "      TASK: hhl-angle | coulomb-potential | gaussian | vectors:FILE\n"
          "      --k MODE        accumulator pattern: zero ones xormix splitmix:S kneg:D random:S\n"
          "                      (repeatable; default zero ones xormix splitmix:1 splitmix:5)\n"
          "      --strict        the strict k suite (structured, 4 uniform seeds, carry boundaries)\n"
          "      --ri A,B,C      coulomb: fixed r_i (repeatable; default 4 corners/centre)\n"
          "      --lanes N       sampled tasks: number of lanes (gaussian default 2^22)\n"
          "      --seed S        sampled tasks: seed (default 1)\n"
          "      --sample        hhl-angle: sample X instead of the exhaustive sweep\n"
          "      --sweep         gaussian: deterministic low-discrepancy sweep\n"
          "      --words W       64-lane words per pass (default 512)\n"
          "      --list N        print at most N failing lanes per run (default 32)\n"
          "      --drop KIND:N   negative control: the N-th expanded op of KIND does not act\n"
          "  STREAM is a raw OPSTRM01 file, or - for stdin (pipe .xz through xz -dc).\n",
          stderr);
    exit(2);
}

int main(int argc, char **argv)
{
    Prog P;
    Census C;
    const char *task, *path;
    KMode modes[64];
    int nmodes = 0, strict = 0, W = 512, list_max = 32, sample = 0, sweep = 0, nri = 0, ai, drop_kind = -1;
    uint8_t ris[16][3];
    uint64_t lanes = 0, seed = 1, drop_nth = 0;
    int any_fail = 0, mi, ri;

    if (argc < 3) usage();
    memset(&P, 0, sizeof P);
    if (!strcmp(argv[1], "count")) {
        read_stream(&P, argv[2]);
        parse(&P);
        {
            uint64_t t;
            for (t = 0; t < P.ntop; t++) analyse(&P, P.top[t]);
        }
        metrics(&P, &C);
        print_metrics(&P, &C);
        return 0;
    }
    if (!strcmp(argv[1], "ref")) return ref_main(argc, argv);
    if (strcmp(argv[1], "check") || argc < 4) usage();
    task = argv[2];
    path = argv[3];
    for (ai = 4; ai < argc; ai++) {
        const char *o = argv[ai];
        const char *v = ai + 1 < argc ? argv[ai + 1] : NULL;
        if (!strcmp(o, "--sample")) { sample = 1; continue; }
        if (!strcmp(o, "--strict")) { strict = 1; continue; }
        if (!strcmp(o, "--sweep")) { sweep = 1; continue; }
        if (!v) usage();
        ai++;
        if (!strcmp(o, "--k")) {
            if (nmodes == 64 || kmode_parse(v, &modes[nmodes++])) die("bad --k %s", v);
        } else if (!strcmp(o, "--ri")) {
            unsigned a0, a1, a2;
            if (nri == 16 || sscanf(v, "%u,%u,%u", &a0, &a1, &a2) != 3 || a0 > 255 || a1 > 255 || a2 > 255)
                die("bad --ri %s", v);
            ris[nri][0] = (uint8_t)a0; ris[nri][1] = (uint8_t)a1; ris[nri][2] = (uint8_t)a2;
            nri++;
        } else if (!strcmp(o, "--lanes")) lanes = strtoull(v, NULL, 0);
        else if (!strcmp(o, "--seed")) seed = strtoull(v, NULL, 0);
        else if (!strcmp(o, "--words")) W = atoi(v);
        else if (!strcmp(o, "--list")) list_max = atoi(v);
        else if (!strcmp(o, "--drop")) {
            char kn[32];
            const char *c = strchr(v, ':');
            int k;
            if (!c || (size_t)(c - v) >= sizeof kn) die("bad --drop %s", v);
            memcpy(kn, v, (size_t)(c - v));
            kn[c - v] = 0;
            for (k = 0; k < K_NKINDS; k++)
                if (!strcmp(kn, KIND_NAME[k])) break;
            if (k == K_NKINDS) die("bad --drop kind %s", kn);
            drop_kind = k;
            drop_nth = strtoull(c + 1, NULL, 0);
        } else usage();
    }
    if (W < 1 || W > 65536) die("bad --words");
    if (nmodes == 0 && strict) {
        /* the strict suite: the structured patterns, four independent
         * uniform-k seeds, and carry-boundary patterns k* = D for
         * D = 0 and D = 2^j - 1, 2^j across the accumulator's upper bits */
        const char *def[] = {"zero", "ones", "xormix", "splitmix:1", "splitmix:5",
                             "random:1", "random:2", "random:3", "random:4", "kneg:0"};
        uint32_t width = !strcmp(task, "coulomb-potential") ? 25u : !strcmp(task, "gaussian") ? 39u : 33u;
        uint32_t j, j0 = width - (!strcmp(task, "coulomb-potential") ? 8u : 17u);
        char t[64];
        for (mi = 0; mi < 10; mi++) kmode_parse(def[mi], &modes[nmodes++]);
        for (j = j0; j <= width && nmodes + 2 <= 64; j++) {
            snprintf(t, sizeof t, "kneg:%" PRIu64, (((uint64_t)1) << j) - 1u);
            kmode_parse(t, &modes[nmodes++]);
            if (j < width) {
                snprintf(t, sizeof t, "kneg:%" PRIu64, ((uint64_t)1) << j);
                kmode_parse(t, &modes[nmodes++]);
            }
        }
    }
    if (nmodes == 0) {
        const char *def[] = {"zero", "ones", "xormix", "splitmix:1", "splitmix:5"};
        for (mi = 0; mi < 5; mi++) kmode_parse(def[mi], &modes[nmodes++]);
    }
    if (nri == 0) {
        static const uint8_t dri[4][3] = {{0, 0, 0}, {255, 255, 255}, {128, 128, 128}, {0, 255, 128}};
        for (ri = 0; ri < 4; ri++) memcpy(ris[nri++], dri[ri], 3);
    }

    read_stream(&P, path);
    parse(&P);
    {
        uint64_t t;
        for (t = 0; t < P.ntop; t++) analyse(&P, P.top[t]);
    }
    metrics(&P, &C);
    print_metrics(&P, &C);

    if (!strncmp(task, "vectors:", 8)) {
        Adapter a;
        Result R;
        memset(&a, 0, sizeof a);
        load_vectors(&a, task + 8);
        printf("{\"task\": \"vectors\", \"file\": \"%s\", \"failing\": [\n", task + 8);
        run_check(&P, &C, &a, W, list_max, drop_kind, drop_nth, &R, stdout);
        printf("  null], \"lanes\": %" PRIu64 ", \"live\": %" PRIu64 ", \"wrong_output\": %" PRIu64
               ", \"wrong_sign_const\": %" PRIu64 ", \"outcome_dependent_sign\": %" PRIu64
               ", \"reset_of_nonzero\": %" PRIu64 ", \"dirty_lanes\": %" PRIu64 ", \"dirty_qubits\": %" PRIu64 ", \"failed\": %" PRIu64 "}\n",
               R.lanes, R.live, R.wrong_output, R.sign_const, R.sign_outdep, R.reset_dirty, R.dirty_lanes, R.dirty_qubits, R.failed);
        any_fail |= R.failed != 0;
    } else {
        int nruns = !strcmp(task, "coulomb-potential") ? nri : 1;
        for (mi = 0; mi < nmodes; mi++) {
            for (ri = 0; ri < nruns; ri++) {
                Adapter a;
                Result R;
                HhlCtx hc;
                CouCtx cc;
                GauCtx gc;
                memset(&a, 0, sizeof a);
                if (!strcmp(task, "hhl-angle")) {
                    hc.km = modes[mi];
                    hc.sampled = sample;
                    hc.seed = seed;
                    a.name = task;
                    a.sc.nregs = 2;
                    a.sc.width[0] = 21; a.sc.width[1] = 33;
                    a.sc.band[1] = 429;
                    a.n_lanes = sample ? (lanes ? lanes : 1u << 20) : 2031616u;
                    a.gen = hhl_gen; a.describe = hhl_desc; a.ctx = &hc;
                } else if (!strcmp(task, "coulomb-potential")) {
                    int r;
                    cc.km = modes[mi];
                    memcpy(cc.ri, ris[ri], 3);
                    a.name = task;
                    a.sc.nregs = 7;
                    for (r = 0; r < 6; r++) a.sc.width[r] = 8;
                    a.sc.width[6] = 25;
                    a.sc.band[6] = 32;
                    a.n_lanes = 1u << 24;
                    a.gen = cou_gen; a.describe = cou_desc; a.ctx = &cc;
                } else if (!strcmp(task, "gaussian")) {
                    gc.km = modes[mi];
                    gc.seed = seed;
                    gc.sweep = sweep;
                    a.name = task;
                    a.sc.nregs = 2;
                    a.sc.width[0] = 39; a.sc.width[1] = 39;
                    a.sc.band[1] = 429;
                    a.n_lanes = lanes ? lanes : 1u << 22;
                    a.gen = gau_gen; a.describe = gau_desc; a.ctx = &gc;
                } else {
                    die("unknown task %s", task);
                }
                printf("{\"task\": \"%s\", \"k\": \"%s\"", task, modes[mi].text);
                if (a.gen == cou_gen) printf(", \"ri\": [%u,%u,%u]", cc.ri[0], cc.ri[1], cc.ri[2]);
                printf(", \"failing\": [\n");
                run_check(&P, &C, &a, W, list_max, drop_kind, drop_nth, &R, stdout);
                printf("  null], \"lanes\": %" PRIu64 ", \"live\": %" PRIu64 ", \"wrong_output\": %" PRIu64
                       ", \"wrong_sign_const\": %" PRIu64 ", \"outcome_dependent_sign\": %" PRIu64
                       ", \"reset_of_nonzero\": %" PRIu64 ", \"dirty_lanes\": %" PRIu64 ", \"dirty_qubits\": %" PRIu64 ", \"failed\": %" PRIu64 "}\n",
                       R.lanes, R.live, R.wrong_output, R.sign_const, R.sign_outdep, R.reset_dirty,
                       R.dirty_lanes, R.dirty_qubits, R.failed);
                fflush(stdout);
                free(R.dirty_q);
                any_fail |= R.failed != 0;
            }
        }
    }
    printf("{\"verdict\": \"%s\"}\n", any_fail ? "FAIL" : "PASS");
    return any_fail ? 1 : 0;
}
