#!/bin/sh
# liveness_court.sh -- liveness takes LOOP nesting of any depth.
#
#   sh results/tools/liveness_court.sh [liveness.c]
#
# Builds liveness.c with AddressSanitizer (plain build when the compiler has
# none) and feeds it a stream nested 70 LOOPs deep. The loop-index array is
# sized from the stream, so this passes; with a fixed 64-entry array back it
# writes past the array and the sanitizer stops the run.
set -u
SRC=${1:-$(dirname "$0")/liveness.c}
T=$(mktemp -d "${TMPDIR:-/tmp}/liveness-court.XXXXXX") || exit 2
trap 'rm -rf "$T"' EXIT INT TERM
CC=${CC:-cc}
if printf 'int main(void){return 0;}\n' > "$T/a.c" && $CC -fsanitize=address "$T/a.c" -o "$T/a" 2>/dev/null && "$T/a"; then
    SAN=-fsanitize=address
else
    SAN=; echo "note: no AddressSanitizer here; plain build"
fi
$CC -std=c11 -O1 -g $SAN -Wall -Wextra -Werror -o "$T/liveness" "$SRC" || { echo "FAIL: build"; exit 1; }

le() {
    le_v=$1; le_n=$2; le_i=0
    while [ "$le_i" -lt "$le_n" ]; do
        printf "\\$(printf %03o $((le_v & 255)))"
        le_v=$((le_v >> 8)); le_i=$((le_i + 1))
    done
}

# OPSTRM01, one register of one qubit wire (id 0), DEPTH nested LOOPs of
# count 1, one X on wire 0 innermost (base 0, DEPTH zero strides), END 1
depth=70
{
    printf 'OPSTRM01'
    printf '\001'; le 1 4; printf '\000'; le 1 4; le 0 4
    printf '\004'
    i=0
    while [ "$i" -lt "$depth" ]; do
        le 1 8; le 1 2
        if [ $((i + 1)) -lt "$depth" ]; then printf '\001'; else printf '\000'; fi
        i=$((i + 1))
    done
    printf '\000\000'; le 0 4
    i=0
    while [ "$i" -lt "$depth" ]; do le 0 8; i=$((i + 1)); done
    printf '\377'; le 1 8
} > "$T/deep.ops"

out=$(ASAN_OPTIONS=detect_leaks=0 "$T/liveness" < "$T/deep.ops" 2> "$T/err"); rc=$?
case "$out" in
"reg qubit wires 1 touched 1 max id+1 1 ancilla wires 0 max ancilla overlap 0 => lower bound on relocation 1")
    if [ "$rc" -eq 0 ]; then echo "ok  $depth nested LOOPs: $out"; echo "liveness court: passed"; exit 0; fi ;;
esac
echo "FAIL: $depth nested LOOPs (exit $rc): $out"
head -5 "$T/err"
exit 1
