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
    n->is_loop = is_loop;
    return n;
}

static void push_child(Node *L, Node *c)
{
    if (L->nch == L->cap) {
        L->cap = L->cap ? L->cap * 2 : 8;
        L->ch = realloc(L->ch, L->cap * sizeof *L->ch);
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

static Node *parse_loop_body(int enclosing); /* count, n, items */

static Node *parse_loop_body(int enclosing)
{
    Node *L = node_new(1);
    uint32_t n, i;
    L->count = rd(8);
    n = (uint32_t)rd(2);
    if (n == 0) die("LOOP with 0 items");
    for (i = 0; i < n; i++) {
        uint8_t ik = (uint8_t)rd(1);
        if (ik == 0)
            push_child(L, parse_op2(enclosing + 1));
        else if (ik == 1)
            push_child(L, parse_loop_body(enclosing + 1));
        else
            die("bad item kind");
    }
    return L;
}

/* ---------- reduction: stack-cancel adjacent identical cancellable ops ---------- */

static uint64_t removed_pairs_w; /* weighted by loop multiplicity */
static uint64_t removed_ops_w;
static uint64_t removed_pairs_plain;

static int same_bytes(const Node *a, const Node *b)
{
    return a->e - a->s == b->e - b->s && memcmp(buf + a->s, buf + b->s, a->e - a->s) == 0;
}

/* reduce list in place; mult = product of enclosing loop counts. Returns new length. */
static void reduce_list(Node **list, uint32_t *nlist, uint64_t mult)
{
    uint32_t i, top = 0;
    Node **out = malloc((*nlist + 1) * sizeof *out);
    for (i = 0; i < *nlist; i++) {
        Node *c = list[i];
        if (c->is_loop) {
            uint32_t m = c->nch;
            reduce_list(c->ch, &m, mult * c->count);
            c->nch = m;
            if (m == 0) continue; /* empty body: drop the loop */
            out[top++] = c;
            continue;
        }
        if (c->cancellable && top > 0 && out[top - 1]->cancellable && same_bytes(out[top - 1], c)) {
            /* the top op and c cancel; each removed op is counted at its multiplicity */
            removed_pairs_w += mult;
            removed_ops_w += 2 * mult;
            removed_pairs_plain++;
            top--;
            continue;
        }
        out[top++] = c;
    }
    memcpy(list, out, top * sizeof *out);
    free(out);
    *nlist = top;
}

/* ---------- output ---------- */

static uint8_t *obuf;
static size_t olen, ocap;

static void emit(const void *p, size_t n)
{
    if (olen + n > ocap) {
        ocap = (olen + n) * 2 + 4096;
        obuf = realloc(obuf, ocap);
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

static void emit_loop_body(const Node *L);

static void emit_item(const Node *c)
{
    if (c->is_loop) {
        emit_u(1, 1);
        emit_loop_body(c);
    } else {
        emit_u(0, 1);
        emit(buf + c->s, c->e - c->s);
    }
}

static void emit_loop_body(const Node *L)
{
    uint32_t i;
    emit_u(L->count, 8);
    emit_u(L->nch, 2);
    for (i = 0; i < L->nch; i++) emit_item(L->ch[i]);
}

int main(int argc, char **argv)
{
    FILE *f;
    size_t cap = 0, got;
    uint8_t *tmp;
    uint64_t end_declared = 0;
    uint64_t total_ops_expanded_removed = 0;
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
        Node **top = NULL;
        uint32_t ntop = 0, captop = 0;
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
                pos += (size_t)(nb / 8 + ((nb % 8) != 0));
                emit(buf + s, pos - s);
            } else if (seg == 0x03) {
                /* FLAT: a top-level list of ops at depth 0 */
                uint32_t n = (uint32_t)rd(4), i;
                uint32_t cap2 = n + 1;
                Node **flat = malloc(cap2 * sizeof *flat);
                uint32_t nflat = n;
                for (i = 0; i < n; i++) flat[i] = parse_op2(0);
                reduce_list(flat, &nflat, 1);
                emit_u(0x03, 1);
                emit_u(nflat, 4);
                for (i = 0; i < nflat; i++) emit(buf + flat[i]->s, flat[i]->e - flat[i]->s);
                free(flat);
            } else if (seg == 0x04) {
                Node *L = parse_loop_body(0);
                uint32_t m = 1;
                /* top-level loops: reduce body, drop if empty */
                reduce_list(L->ch, &L->nch, L->count);
                /* when the body is reduced, the counts above used multiplicity count */
                (void)m;
                if (L->nch > 0) {
                    emit_u(0x04, 1);
                    emit_loop_body(L);
                }
                if (ntop == captop) {
                    captop = captop ? captop * 2 : 16;
                    top = realloc(top, captop * sizeof *top);
                }
                top[ntop++] = L;
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
        (void)top;
        /* END: old count minus removed ops */
        total_ops_expanded_removed = removed_ops_w;
        emit_u(0xFF, 1);
        emit_u(end_declared - total_ops_expanded_removed, 8);
    }
    f = fopen(argv[2], "wb");
    if (!f) die("open out");
    fwrite(obuf, 1, olen, f);
    fclose(f);
    fprintf(stderr,
            "rw: removed_pairs_plain=%" PRIu64 " removed_pairs_weighted=%" PRIu64
            " removed_ops_weighted=%" PRIu64 " clifford_saved=%" PRIu64 " end_old=%" PRIu64
            " end_new=%" PRIu64 "\n",
            removed_pairs_plain, removed_pairs_w, removed_ops_w, removed_ops_w,
            end_declared, end_declared - total_ops_expanded_removed);
    return 0;
}
