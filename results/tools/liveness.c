/* liveness -- wire-liveness lower bound for an OPSTRM01 op stream.
 *
 *   xz -dc stream.ops.xz | liveness
 *
 * C port of results/tools/liveness.py (same parse, same counting, same output
 * line), so the Clifford/qubit notes can cite a committed tool. Wire intervals
 * run from a wire's first to its last touch; a reset (op kinds 8 and 9, slot 2)
 * of a non-register wire that was touched before splits its interval. Register
 * qubit wires (segment 1, kind 0) are counted apart. The bound printed is
 * register wires + the peak number of concurrently live ancilla intervals.
 *
 * Credit: op-stream format from the uncompute evaluator, Gu, Kemeny, Yelin,
 * Lukin, arXiv:2610.22367, https://arxiv.org/abs/2610.22367, andigu/uncompute
 * at commit 64b5a13aa249f1360846657392e6c66817b26d0f
 * (https://github.com/andigu/uncompute). Techniques: HereLiesScarlet SpaceTime,
 * Michael David Brown, DOI 10.5281/zenodo.23486661. See CREDITS.md.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned char *d;
static size_t dn, pos = 8;

static void die(const char *m) { fprintf(stderr, "liveness: %s\n", m); exit(2); }

static uint64_t u(int n)
{
    uint64_t v = 0;
    int i;
    if (pos + (size_t)n > dn) die("truncated stream");
    for (i = 0; i < n; ++i) v |= (uint64_t)d[pos + i] << (8 * i);
    pos += (size_t)n;
    return v;
}
static int64_t s64(void) { return (int64_t)u(8); }

/* required slots per op kind, as REQ in liveness.py */
static const int req_n[14] = {1, 1, 0, 2, 2, 2, 3, 3, 1, 2, 2, 1, 1, 1};
static const int req_s[14][3] = {{2}, {2}, {0}, {1, 2}, {1, 2}, {1, 2}, {0, 1, 2},
                                 {0, 1, 2}, {2}, {2, 3}, {3, 4}, {5}, {5}, {5}};

typedef struct { int slot; uint32_t base; int64_t *st; } operand;
typedef struct node {
    int is_loop, kind, nops, nch;
    operand *ops;
    uint64_t cnt;
    struct node **ch;
} node;

static node *pop(int depth)
{
    node *x = calloc(1, sizeof *x);
    int k, fl, slots[5], ns = 0, i, j;
    if (!x) die("out of memory");
    k = (int)u(1); fl = (int)u(1);
    if (k < 0 || k > 13) die("unknown op kind");
    if (fl & 2) { u(4); u(8); for (i = 0; i < depth; ++i) u(8); }
    for (i = 0; i < req_n[k]; ++i) slots[ns++] = req_s[k][i];
    if (fl & 1) slots[ns++] = 4;
    if (fl & 4) slots[ns++] = 6;
    x->kind = k; x->nops = ns;
    x->ops = calloc((size_t)ns + 1, sizeof *x->ops);
    if (!x->ops) die("out of memory");
    for (i = 0; i < ns; ++i) {
        x->ops[i].slot = slots[i];
        x->ops[i].base = (uint32_t)u(4);
        x->ops[i].st = calloc((size_t)depth + 1, sizeof(int64_t));
        if (!x->ops[i].st) die("out of memory");
        for (j = 0; j < depth; ++j) x->ops[i].st[j] = s64();
    }
    return x;
}

static node *ploop(int enc)
{
    node *x = calloc(1, sizeof *x);
    int i;
    if (!x) die("out of memory");
    x->is_loop = 1; x->cnt = u(8); x->nch = (int)u(2);
    x->ch = calloc((size_t)x->nch + 1, sizeof *x->ch);
    if (!x->ch) die("out of memory");
    for (i = 0; i < x->nch; ++i) {
        int ik = (int)u(1);
        x->ch[i] = ik == 0 ? pop(enc + 1) : ploop(enc + 1);
    }
    return x;
}

/* wire tables, indexed by wire id */
static size_t wcap;
static unsigned char *isreg, *seen;
static uint64_t *first, *last;
static uint64_t t;
typedef struct { uint64_t a, b; } span;
static span *segs;
static size_t nseg, segcap;

static void grow(size_t w)
{
    size_t n = wcap ? wcap : 1024;
    if (w < wcap) return;
    while (n <= w) n *= 2;
    isreg = realloc(isreg, n); seen = realloc(seen, n);
    first = realloc(first, n * sizeof *first); last = realloc(last, n * sizeof *last);
    if (!isreg || !seen || !first || !last) die("out of memory");
    memset(isreg + wcap, 0, n - wcap); memset(seen + wcap, 0, n - wcap);
    wcap = n;
}

static void addseg(uint64_t a, uint64_t b)
{
    if (nseg == segcap) {
        segcap = segcap ? segcap * 2 : 1024;
        segs = realloc(segs, segcap * sizeof *segs);
        if (!segs) die("out of memory");
    }
    segs[nseg].a = a; segs[nseg].b = b; ++nseg;
}

static void run(const node *x, int64_t *idx, int depth)
{
    int i;
    if (!x->is_loop) {
        for (i = 0; i < x->nops; ++i) {
            const operand *o = &x->ops[i];
            int64_t w = o->base;
            int j;
            if (o->slot > 2) continue;
            for (j = 0; j < depth; ++j) w += o->st[j] * idx[j];
            if (w < 0) die("negative wire id");
            grow((size_t)w);
            if ((x->kind == 8 || x->kind == 9) && o->slot == 2 && seen[w] && !isreg[w]) {
                addseg(first[w], last[w]);
                first[w] = t;
            }
            if (!seen[w]) { seen[w] = 1; first[w] = t; }
            last[w] = t;
        }
        ++t;
        return;
    }
    {
        uint64_t k;
        for (k = 0; k < x->cnt; ++k) {
            idx[depth] = (int64_t)k;
            for (i = 0; i < x->nch; ++i) run(x->ch[i], idx, depth + 1);
        }
    }
}

typedef struct { uint64_t at; int e; } event;
static int evcmp(const void *p, const void *q)
{
    const event *a = p, *b = q;
    if (a->at != b->at) return a->at < b->at ? -1 : 1;
    return a->e - b->e;
}

int main(void)
{
    size_t cap = 1 << 20, ntop = 0, topcap = 64, w, nreg = 0, touched = 0, maxid = 0, nanc, i, ne = 0;
    node **top = malloc(topcap * sizeof *top);
    int64_t idx[64];
    event *ev;
    long cur = 0, mx = 0;
    d = malloc(cap);
    if (!d || !top) die("out of memory");
    for (;;) {
        size_t r = fread(d + dn, 1, cap - dn, stdin);
        dn += r;
        if (dn < cap) break;
        cap *= 2; d = realloc(d, cap);
        if (!d) die("out of memory");
    }
    if (dn < 8 || memcmp(d, "OPSTRM01", 8) != 0) die("not an OPSTRM01 stream on stdin");
    for (;;) {
        int seg = (int)u(1);
        if (seg == 1) {
            uint64_t nr = u(4), a, b;
            for (a = 0; a < nr; ++a) {
                int k = (int)u(1);
                uint64_t nw = u(4);
                for (b = 0; b < nw; ++b) {
                    uint64_t wi = u(4);
                    if (k == 0) { grow((size_t)wi); isreg[wi] = 1; }
                }
            }
        } else if (seg == 2) {
            uint64_t nb;
            u(4); nb = u(8); pos += (size_t)((nb + 7) / 8);
        } else if (seg == 3 || seg == 4) {
            uint64_t n = seg == 3 ? u(4) : 1, a;
            for (a = 0; a < n; ++a) {
                if (ntop == topcap) { topcap *= 2; top = realloc(top, topcap * sizeof *top); if (!top) die("out of memory"); }
                top[ntop++] = seg == 3 ? pop(0) : ploop(0);
            }
        } else if (seg == 0xFF) {
            break;
        } else {
            die("unknown segment");
        }
    }
    for (i = 0; i < ntop; ++i) run(top[i], idx, 0);
    for (w = 0; w < wcap; ++w) {
        if (isreg[w]) ++nreg;
        if (seen[w]) { ++touched; maxid = w; }
    }
    nanc = nseg;
    for (w = 0; w < wcap; ++w) if (seen[w] && !isreg[w]) ++nanc;
    ev = malloc((2 * nanc + 1) * sizeof *ev);
    if (!ev) die("out of memory");
    for (i = 0; i < nseg; ++i) { ev[ne].at = segs[i].a; ev[ne++].e = 1; ev[ne].at = segs[i].b + 1; ev[ne++].e = -1; }
    for (w = 0; w < wcap; ++w)
        if (seen[w] && !isreg[w]) { ev[ne].at = first[w]; ev[ne++].e = 1; ev[ne].at = last[w] + 1; ev[ne++].e = -1; }
    qsort(ev, ne, sizeof *ev, evcmp);
    for (i = 0; i < ne; ++i) { cur += ev[i].e; if (cur > mx) mx = cur; }
    printf("reg qubit wires %zu touched %zu max id+1 %zu ancilla wires %zu max ancilla overlap %ld => lower bound on relocation %zu\n",
           nreg, touched, maxid + 1, nanc, mx, nreg + (size_t)mx);
    return 0;
}
