#!/bin/sh
# rw_court.sh -- rw rejects malformed OPSTRM01 streams cleanly and takes LOOP
# nesting of any depth.
#
#   sh rw_court.sh [rw.c]
#
# Builds rw.c with AddressSanitizer and UndefinedBehaviorSanitizer (plain
# build when the compiler has none) and feeds it crafted streams. Each case
# fails if the defect it names comes back: a DATA length past the end of the
# input, a 32-bit FLAT count wrap, an UNKNOWN (UINT64_MAX) END count turned
# into a finite one, an unchecked write, an unchecked allocation, an
# overflowing 64-bit accumulator, recursion that dies at depth, and
# valid streams whose output changed.
set -u
SRC=${1:-$(dirname "$0")/rw.c}
T=$(mktemp -d "${TMPDIR:-/tmp}/rw-court.XXXXXX") || exit 2
trap 'rm -rf "$T"' EXIT INT TERM
CC=${CC:-cc}
fails=0
ASAN_OPTIONS=detect_leaks=0; export ASAN_OPTIONS   # rw exits right after use; leaks are not what is judged here
pass() { echo "ok  $1"; }
fail() { echo "FAIL $1"; fails=$((fails + 1)); }

if printf 'int main(void){return 0;}\n' > "$T/a.c" && $CC -fsanitize=address,undefined "$T/a.c" -o "$T/a" 2>/dev/null && "$T/a"; then
    SAN=-fsanitize=address,undefined
else
    SAN=; echo "note: no sanitizers here; plain build"
fi
$CC -std=c11 -O1 -g $SAN -fno-sanitize-recover=undefined -Wall -Wextra -Werror -o "$T/rw" "$SRC" 2>/dev/null ||
$CC -std=c11 -O1 -g $SAN -Wall -Wextra -Werror -o "$T/rw" "$SRC" || { echo "FAIL: build"; exit 1; }
$CC -std=c11 -O1 -Wall -Wextra -Werror -o "$T/rwplain" "$SRC" || { echo "FAIL: plain build"; exit 1; }
RW=$T/rw

big63() { printf '\000\000\000\000\000\000\000\200'; }   # 2^63, little-endian
le() {
    le_v=$1; le_n=$2; le_i=0
    while [ "$le_i" -lt "$le_n" ]; do
        printf "\\$(printf %03o $((le_v & 255)))"
        le_v=$((le_v >> 8)); le_i=$((le_i + 1))
    done
}
# one operand of an op at nesting depth D: wire id, then D 8-byte indices
operand() { le "$1" 4; od=0; while [ "$od" -lt "$2" ]; do le 0 8; od=$((od + 1)); done; }
# ungated CX (kind 3, flags 0) on wires 0,1 at nesting depth D
cx() { printf '\003\000'; operand 0 "$1"; operand 1 "$1"; }
cz() { printf '\004\000'; operand 0 "$1"; operand 1 "$1"; }
# UINT64_MAX
unknown() { printf '\377\377\377\377\377\377\377\377'; }

# run NAME EXPECT_EXIT: run rw on $T/in -> $T/out; EXPECT_EXIT is "ok" or "die"
# (exit 2 with a plain "rw:" message, and no sanitizer report)
run() {
    "$RW" "$T/in" "$T/out" > /dev/null 2> "$T/err"; rc=$?
}
expect_die() {   # NAME MESSAGE-SUBSTRING
    run
    if [ "$rc" = 2 ] && grep -q "^rw: .*$2" "$T/err" && ! grep -q "Sanitizer\|runtime error" "$T/err"; then pass "$1"
    else fail "$1 (exit $rc: $(head -c 200 "$T/err" | tr '\n' ' '))"; fi
}
expect_ok() {
    run
    if [ "$rc" = 0 ] && ! grep -q "Sanitizer\|runtime error" "$T/err"; then pass "$1"
    else fail "$1 (exit $rc: $(head -c 200 "$T/err" | tr '\n' ' '))"; fi
}
hdr() { printf 'OPSTRM01'; }

# 1. DATA length far past the end of the input
{ hdr; printf '\002'; le 0 4; le 100000000000 8; printf '\377'; le 0 8; } > "$T/in"
expect_die "DATA length past the end of the input is rejected" truncated
# 1b. DATA that exactly fits still passes through unchanged
{ hdr; printf '\002'; le 0 4; le 12 8; printf 'ab'; printf '\377'; le 0 8; } > "$T/in"
expect_ok "DATA that fits is accepted"
cmp -s "$T/in" "$T/out" && pass "DATA that fits is copied unchanged" || fail "DATA that fits is copied unchanged"

# 2. FLAT count 0xFFFFFFFF (n + 1 wrapped to 0 in 32 bits)
{ hdr; printf '\003'; le 4294967295 4; cx 0; printf '\377'; le 0 8; } > "$T/in"
expect_die "FLAT count 0xFFFFFFFF is rejected" truncated
# 2b. FLAT count larger than the bytes left
{ hdr; printf '\003'; le 1000 4; cx 0; printf '\377'; le 0 8; } > "$T/in"
expect_die "FLAT count larger than the remaining input is rejected" truncated

# 3. END: UNKNOWN stays UNKNOWN when ops are removed
{ hdr; printf '\003'; le 2 4; cx 0; cx 0; printf '\377'; unknown; } > "$T/in"
expect_ok "UNKNOWN END with a cancelling pair is accepted"
{ hdr; printf '\003'; le 0 4; printf '\377'; unknown; } > "$T/exp"
cmp -s "$T/out" "$T/exp" && pass "UNKNOWN END count is kept as UNKNOWN" || fail "UNKNOWN END count is kept as UNKNOWN"
# 3b. a finite END smaller than the removed ops
{ hdr; printf '\003'; le 2 4; cx 0; cx 0; printf '\377'; le 1 8; } > "$T/in"
expect_die "finite END smaller than the removed op count is rejected" "END smaller"
# 3c. a finite END is reduced by the removed ops
{ hdr; printf '\003'; le 2 4; cx 0; cx 0; printf '\377'; le 5 8; } > "$T/in"
expect_ok "finite END with a cancelling pair is accepted"
{ hdr; printf '\003'; le 0 4; printf '\377'; le 3 8; } > "$T/exp"
cmp -s "$T/out" "$T/exp" && pass "finite END is reduced by the removed ops" || fail "finite END is reduced by the removed ops"
{ hdr; printf '\003'; le 2 4; cx 0; cx 0; printf '\377'; le 5 8; } > "$T/in"

# 4. a write that fails is reported
if [ -w /dev/full ]; then
    { hdr; printf '\377'; le 0 8; } > "$T/in"
    "$RW" "$T/in" /dev/full > /dev/null 2> "$T/err"; rc=$?
    if [ "$rc" = 2 ] && grep -q "^rw: write failed" "$T/err"; then pass "output to a full device fails with a message"
    else fail "output to a full device fails with a message (exit $rc)"; fi
else
    echo "skip output to a full device (no /dev/full)"
fi

# 5. 64-bit accumulators: loop count 2^63 makes the weighted op count wrap
{ hdr; printf '\004'; big63; le 2 2; printf '\000'; cx 1; printf '\000'; cx 1
  printf '\377'; unknown; } > "$T/in"
expect_die "removed op count overflowing 64 bits is rejected" overflow
# 5b. nested counts whose product overflows, with nothing cancelled: still fine
{ hdr; printf '\004'; le 4 8; le 1 2; printf '\001'; big63; le 2 2
  printf '\000'; cx 2; printf '\000'; cz 2; printf '\377'; unknown; } > "$T/in"
expect_ok "overflowing loop product with no cancel is accepted"
cmp -s "$T/in" "$T/out" && pass "overflowing loop product with no cancel is unchanged" || fail "overflowing loop product with no cancel is unchanged"
# 5c. the same product with a cancel in the inner loop
{ hdr; printf '\004'; le 4 8; le 1 2; printf '\001'; big63; le 2 2
  printf '\000'; cx 2; printf '\000'; cx 2; printf '\377'; unknown; } > "$T/in"
expect_die "cancel under an overflowing loop product is rejected" overflow

# 6. valid nested loops: everything cancels, loops vanish, END is reduced
#    LOOP x3 { CX CX LOOP x2 { CZ CZ } }: 3 pairs (6 ops) + 6 pairs (12 ops) = 18
{ hdr; printf '\004'; le 3 8; le 3 2
  printf '\000'; cx 1; printf '\000'; cx 1
  printf '\001'; le 2 8; le 2 2; printf '\000'; cz 2; printf '\000'; cz 2
  printf '\377'; le 100 8; } > "$T/in"
expect_ok "nested loops that cancel completely are accepted"
{ hdr; printf '\377'; le 82 8; } > "$T/exp"
cmp -s "$T/out" "$T/exp" && pass "nested loops that cancel completely emit nothing and reduce END" || fail "nested loops that cancel completely emit nothing and reduce END"
#    only the inner loop empties: the outer loop keeps its other op
{ hdr; printf '\004'; le 3 8; le 2 2
  printf '\000'; cx 1
  printf '\001'; le 2 8; le 2 2; printf '\000'; cz 2; printf '\000'; cz 2
  printf '\377'; le 100 8; } > "$T/in"
expect_ok "an inner loop that empties is dropped, the outer kept"
{ hdr; printf '\004'; le 3 8; le 1 2; printf '\000'; cx 1; printf '\377'; le 88 8; } > "$T/exp"
cmp -s "$T/out" "$T/exp" && pass "an emptied inner loop is dropped and its count removed from END" || fail "an emptied inner loop is dropped and its count removed from END"

# 7. unchecked allocation: with the address space capped, a big FLAT of
#    14-byte ops must end with "oom", not a NULL dereference (plain build)
{ hdr; printf '\003'; le 700000 4; head -c 9800000 /dev/zero | tr '\000' '\002'; printf '\377'; le 0 8; } > "$T/in"
( ulimit -v 90000 2>/dev/null; "$T/rwplain" "$T/in" "$T/out" > /dev/null 2> "$T/err"; echo $? > "$T/rc" ) 2>/dev/null
rc=$(cat "$T/rc" 2>/dev/null || echo 99)
if [ "$rc" = 0 ]; then echo "skip allocation failure (address space cap not enforced here)"
elif [ "$rc" = 2 ] && grep -q "^rw: oom" "$T/err"; then pass "allocation failure ends with rw: oom"
else fail "allocation failure ends with rw: oom (exit $rc: $(head -c 120 "$T/err" | tr '\n' ' '))"; fi

# 8. recursion: LOOPs nested 262144 deep (was one stack frame per level)
D=262144
{ printf '\001'; le 1 8; le 1 2; } > "$T/unit"
cp "$T/unit" "$T/n1"; k=1
while [ "$k" -lt "$D" ]; do cat "$T/n1" "$T/n1" > "$T/n2"; mv "$T/n2" "$T/n1"; k=$((k * 2)); done
{ hdr; printf '\004'; le 1 8; le 1 2; cat "$T/n1"; printf '\000\002\000'; printf '\377'; le 7 8; } > "$T/in"
( ulimit -s 2048 2>/dev/null; "$RW" "$T/in" "$T/out" > /dev/null 2> "$T/err"; echo $? > "$T/rc" ) 2>/dev/null
rc=$(cat "$T/rc" 2>/dev/null || echo 99)
if [ "$rc" = 0 ] && ! grep -q "Sanitizer\|runtime error" "$T/err" && cmp -s "$T/in" "$T/out"; then pass "$D nested LOOPs are parsed, reduced and emitted unchanged"
else fail "$D nested LOOPs are parsed, reduced and emitted unchanged (exit $rc: $(head -c 120 "$T/err" | tr '\n' ' '))"; fi

if [ "$fails" = 0 ]; then echo "rw_court: all cases passed"; exit 0; fi
echo "rw_court: $fails failed"; exit 1
