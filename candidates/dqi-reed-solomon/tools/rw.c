/* rw: cancel adjacent identical ungated Clifford involutions (CX, CZ, SWAP) in an
 * OPSTRM01 stream. Usage: rw IN OUT   (IN and OUT are raw streams).
 * Prints the removed-op census to stderr. Scratch tool, not part of the repo. */
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define die(...) do { fprintf(stderr, "rw: " __VA_ARGS__); fputc('\n', stderr); exit(2); } while (0)

enum { K_X = 0, K_Z, K_NEG, K_CX, K_CZ, K_SWAP, K_CCX, K_CCZ, K_R, K_HMR, K_OXOR,
       K_BIT_INVERT, K_BIT_STORE0, K_BIT_STORE1, K_NKINDS };
static const int8_t REQ[K_NKINDS][4] = {
    {2, -1}, {2, -1}, {-1}, {1, 2, -1}, {1, 2, -1}, {1, 2, -1},
    {0, 1, 2, -1}, {0, 1, 2, -1}, {2, -1}, {2, 3, -1}, {3, 4, -1},
    {5, -1}, {5, -1}, {5, -1},
};

static uint8_t *buf;
static size_t len, pos;

typedef struct Node {
    int is_loop;
    uint64_t count;
    size_t s, e; /* op: raw bytes [s, e) starting at kind byte */
    int kind, flags;
    int cancellable;
    uint32_t nch, cap;
    struct Node **ch;
} Node;

static uint64_t rd(int n)
{
    uint64_t v = 0;
    int i;
    if (len - pos < (size_t)n) die("truncated");
    for (i = n - 1; i >= 0; i--) v = (v << 8) | buf[pos + i];
    pos += n;
    return v;
}

static Node *node_new(int is_loop)
{
    Node *n = calloc(1, sizeof *n);
    if (!n) die("oom");
    n->is_loop = is_loop;
    return n;
}

static void push_child(Node *L, Node *c)
{
    if (L->nch == L->cap) {
        uint32_t ncap = L->cap ? L->cap * 2 : 8;
        Node **t = realloc(L->ch, (size_t)ncap * sizeof *L->ch);
        if (!t) die("oom");
        L->ch = t;
        L->cap = ncap;
    }
    L->ch[L->nch++] = c;
}

/* Re-do parse_op operand loop correctly (kept separate for clarity). */
static Node *parse_op2(int depth)
{
    Node *n = node_new(0);
    int k, flags, nops, j, d;
    n->s = pos;
    k = (int)rd(1);
    flags = (int)rd(1);
    if (k >= K_NKINDS) die("bad kind %d", k);
    n->kind = k;
    n->flags = flags;
    if (flags & 2) {
        rd(4);
        rd(8);
        for (d = 0; d < depth; d++) rd(8);
    }
    nops = 0;
    while (REQ[k][nops] >= 0) nops++;
    if (flags & 1) nops++;
    if (flags & 4) nops++;
    for (j = 0; j < nops; j++) {
        rd(4);
        for (d = 0; d < depth; d++) rd(8);
    }
    n->e = pos;
    n->cancellable = (flags == 0) && (k == K_CX || k == K_CZ || k == K_SWAP);
    return n;
}

/* LOOP parsing is iterative: an explicit stack of open loops, grown from the
 * input, so nesting depth is limited only by memory. */
typedef struct PFrame {
    Node *L;
    uint32_t rem;
} PFrame;

static Node *parse_loop_header(void)
{
    Node *L = node_new(1);
    uint32_t n;
    L->count = rd(8);
    n = (uint32_t)rd(2);
    if (n == 0) die("LOOP with 0 items");
    L->cap = n; /* remaining items, consumed by the caller's frame */
    return L;
}

/* Parse a LOOP body (after the segment/item kind byte); enclosing = 0 at top level. */
static Node *parse_loop_body(int enclosing)
{
    PFrame *st = NULL;
    size_t sp = 0, cap = 0;
    Node *root = parse_loop_header();
    (void)enclosing; /* depth is the number of open loops (enclosing is 0 here) */
    cap = 16;
    st = malloc(cap * sizeof *st);
    if (!st) die("oom");
    st[sp].L = root;
    st[sp].rem = root->cap;
    root->cap = 0;
    sp++;
    while (sp > 0) {
        PFrame *f = &st[sp - 1];
        uint8_t ik;
        if (f->rem == 0) {
            sp--;
            continue;
        }
        f->rem--;
        ik = (uint8_t)rd(1);
        if (ik == 0) {
            push_child(f->L, parse_op2((int)sp));
        } else if (ik == 1) {
            Node *c = parse_loop_header();
            uint32_t crem = c->cap;
            push_child(f->L, c);
            c->cap = 0;
            if (sp == cap) {
                PFrame *t = realloc(st, cap * 2 * sizeof *st);
                if (!t) die("oom");
                st = t;
                cap *= 2;
            }
            st[sp].L = c;
            st[sp].rem = crem;
            sp++;
        } else {
            die("bad item kind");
        }
    }
    free(st);
    return root;
}

/* ---------- reduction: stack-cancel adjacent identical cancellable ops ---------- */

static uint64_t removed_pairs_w; /* weighted by loop multiplicity */
static uint64_t removed_ops_w;
static uint64_t removed_pairs_plain;

static int same_bytes(const Node *a, const Node *b)
{
    return a->e - a->s == b->e - b->s && memcmp(buf + a->s, buf + b->s, a->e - a->s) == 0;
}

typedef struct RFrame {
    Node **list;
    uint32_t *pn;   /* where the reduced length is stored */
    Node *owner;    /* the loop this list belongs to (NULL at the root) */
    Node **out;
    uint32_t n, i, top;
    uint64_t mult;  /* product of enclosing loop counts */
    int ovf;        /* mult overflowed 64 bits: fatal only if a cancel uses it */
} RFrame;

static void rframe_open(RFrame *f, Node **list, uint32_t *pn, Node *owner, uint64_t mult, int ovf)
{
    f->list = list;
    f->pn = pn;
    f->owner = owner;
    f->n = *pn;
    f->i = 0;
    f->top = 0;
    f->mult = mult;
    f->ovf = ovf;
    f->out = malloc(((size_t)f->n + 1) * sizeof *f->out);
    if (!f->out) die("oom");
}

/* reduce list in place; mult = product of enclosing loop counts. Iterative. */
static void reduce_list(Node **list, uint32_t *nlist, uint64_t mult)
{
    RFrame *st;
    size_t sp = 0, cap = 16;
    st = malloc(cap * sizeof *st);
    if (!st) die("oom");
    rframe_open(&st[sp++], list, nlist, NULL, mult, 0);
    while (sp > 0) {
        RFrame *f = &st[sp - 1];
        if (f->i < f->n) {
            Node *c = f->list[f->i++];
            if (c->is_loop) {
                uint64_t m2;
                int ovf = f->ovf | __builtin_mul_overflow(f->mult, c->count, &m2);
                if (sp == cap) {
                    RFrame *t = realloc(st, cap * 2 * sizeof *st);
                    if (!t) die("oom");
                    st = t;
                    cap *= 2;
                    f = &st[sp - 1];
                }
                rframe_open(&st[sp++], c->ch, &c->nch, c, m2, ovf);
                continue;
            }
            if (c->cancellable && f->top > 0 && f->out[f->top - 1]->cancellable &&
                same_bytes(f->out[f->top - 1], c)) {
                /* the top op and c cancel; each removed op is counted at its multiplicity */
                uint64_t m2;
                if (f->ovf || __builtin_mul_overflow(f->mult, (uint64_t)2, &m2) ||
                    __builtin_add_overflow(removed_pairs_w, f->mult, &removed_pairs_w) ||
                    __builtin_add_overflow(removed_ops_w, m2, &removed_ops_w))
                    die("removed op count overflows 64 bits");
                removed_pairs_plain++;
                f->top--;
                continue;
            }
            f->out[f->top++] = c;
        } else {
            /* list done: write back, free, pop, and hand the loop to its parent */
            Node *owner = f->owner;
            memcpy(f->list, f->out, (size_t)f->top * sizeof *f->out);
            free(f->out);
            *f->pn = f->top;
            sp--;
            if (sp > 0 && f->top != 0) {
                RFrame *p = &st[sp - 1];
                p->out[p->top++] = owner; /* empty body: the loop is dropped */
            }
        }
    }
    free(st);
}

/* ---------- output ---------- */

static uint8_t *obuf;
static size_t olen, ocap;

static void emit(const void *p, size_t n)
{
    if (olen + n > ocap) {
        size_t ncap = (olen + n) * 2 + 4096;
        uint8_t *t = realloc(obuf, ncap);
        if (!t) die("oom");
        obuf = t;
        ocap = ncap;
    }
    memcpy(obuf + olen, p, n);
    olen += n;
}
static void emit_u(uint64_t v, int n)
{
    uint8_t b[8];
    int i;
    for (i = 0; i < n; i++) b[i] = (uint8_t)(v >> (8 * i));
    emit(b, n);
}

typedef struct EFrame {
    const Node *L;
    uint32_t i;
} EFrame;

/* Iterative: an explicit stack of open loops, grown as needed. */
static void emit_loop_body(const Node *root)
{
    EFrame *st;
    size_t sp = 0, cap = 16;
    st = malloc(cap * sizeof *st);
    if (!st) die("oom");
    emit_u(root->count, 8);
    emit_u(root->nch, 2);
    st[sp].L = root;
    st[sp].i = 0;
    sp++;
    while (sp > 0) {
        EFrame *f = &st[sp - 1];
        const Node *c;
        if (f->i == f->L->nch) {
            sp--;
            continue;
        }
        c = f->L->ch[f->i++];
        if (c->is_loop) {
            emit_u(1, 1);
            emit_u(c->count, 8);
            emit_u(c->nch, 2);
            if (sp == cap) {
                EFrame *t = realloc(st, cap * 2 * sizeof *st);
                if (!t) die("oom");
                st = t;
                cap *= 2;
            }
            st[sp].L = c;
            st[sp].i = 0;
            sp++;
        } else {
            emit_u(0, 1);
            emit(buf + c->s, c->e - c->s);
        }
    }
    free(st);
}

int main(int argc, char **argv)
{
    FILE *f;
    size_t cap = 0, got;
    uint8_t *tmp;
    uint64_t end_declared = 0;
    uint64_t total_ops_expanded_removed = 0;
    uint64_t end_new = UINT64_MAX;
    size_t regs_s = 0, regs_e = 0;
    int have_regs = 0;
    /* read input */
    if (argc != 3) die("usage: rw IN OUT");
    f = fopen(argv[1], "rb");
    if (!f) die("open");
    while (1) {
        if (len == cap) {
            cap = cap ? cap * 2 : (1 << 26);
            tmp = realloc(buf, cap);
            if (!tmp) die("oom");
            buf = tmp;
        }
        got = fread(buf + len, 1, cap - len, f);
        if (got == 0) break;
        len += got;
    }
    fclose(f);
    if (len < 8 || memcmp(buf, "OPSTRM01", 8) != 0) die("bad magic");
    pos = 8;

    /* segments: keep REGS and DATA raw; FLAT and LOOP re-emitted; END recomputed */
    emit("OPSTRM01", 8);
    {
        for (;;) {
            uint8_t seg = (uint8_t)rd(1);
            if (seg == 0x01) {
                size_t s = pos - 1;
                uint32_t nr = (uint32_t)rd(4), r;
                for (r = 0; r < nr; r++) {
                    uint8_t k = (uint8_t)rd(1);
                    uint32_t nw = (uint32_t)rd(4), w;
                    (void)k;
                    for (w = 0; w < nw; w++) rd(4);
                }
                emit(buf + s, pos - s);
                have_regs = 1;
            } else if (seg == 0x02) {
                size_t s = pos - 1;
                uint64_t nb;
                rd(4);
                nb = rd(8);
                if (nb > (uint64_t)(len - pos) * 8) die("truncated");
                pos += (size_t)(nb / 8 + ((nb % 8) != 0));
                emit(buf + s, pos - s);
            } else if (seg == 0x03) {
                /* FLAT: a top-level list of ops at depth 0 */
                uint32_t n = (uint32_t)rd(4), i;
                size_t cap2 = (size_t)n + 1;
                Node **flat;
                uint32_t nflat = n;
                if (n > (len - pos) / 2) die("truncated");
                flat = malloc(cap2 * sizeof *flat);
                if (!flat) die("oom");
                for (i = 0; i < n; i++) flat[i] = parse_op2(0);
                reduce_list(flat, &nflat, 1);
                emit_u(0x03, 1);
                emit_u(nflat, 4);
                for (i = 0; i < nflat; i++) emit(buf + flat[i]->s, flat[i]->e - flat[i]->s);
                free(flat);
            } else if (seg == 0x04) {
                Node *L = parse_loop_body(0);
                /* top-level loops: reduce body, drop if empty */
                reduce_list(L->ch, &L->nch, L->count);
                if (L->nch > 0) {
                    emit_u(0x04, 1);
                    emit_loop_body(L);
                }
            } else if (seg == 0xFF) {
                end_declared = rd(8);
                if (pos != len) die("trailing bytes after END");
                break;
            } else {
                die("unknown segment 0x%02x", seg);
            }
        }
        (void)have_regs;
        (void)regs_s;
        (void)regs_e;
        /* END: old count minus removed ops */
        total_ops_expanded_removed = removed_ops_w;
        emit_u(0xFF, 1);
        if (end_declared != UINT64_MAX) { /* UINT64_MAX is the format's UNKNOWN count: keep it */
            if (total_ops_expanded_removed > end_declared) die("END smaller than removed ops");
            end_new = end_declared - total_ops_expanded_removed;
        }
        emit_u(end_new, 8);
    }
    f = fopen(argv[2], "wb");
    if (!f) die("open out");
    if (fwrite(obuf, 1, olen, f) != olen) die("write failed");
    if (fclose(f)) die("write failed");
    fprintf(stderr,
            "rw: removed_pairs_plain=%" PRIu64 " removed_pairs_weighted=%" PRIu64
            " removed_ops_weighted=%" PRIu64 " clifford_saved=%" PRIu64 " end_old=%" PRIu64
            " end_new=%" PRIu64 "\n",
            removed_pairs_plain, removed_pairs_w, removed_ops_w, removed_ops_w,
            end_declared, end_new);
    return 0;
}
