#!/bin/sh
# caps_court.sh -- ucverify handles any number of registers, k patterns,
# r_i points, words per pass, and any line length, with no fixed limit.
#
#   sh caps_court.sh [./ucverify]
#
# Each case fails if the fixed limit it names comes back. It needs no grader
# repository: the streams and vector files are written here.
set -u
U=${1:-./ucverify}
T=$(mktemp -d "${TMPDIR:-/tmp}/ucverify-caps.XXXXXX") || exit 2
trap 'rm -rf "$T"' EXIT INT TERM
fails=0
pass() { echo "ok  $1"; }
fail() { echo "FAIL $1"; fails=$((fails + 1)); }

# little-endian integer: le VALUE NBYTES
le() {
    le_v=$1; le_n=$2; le_i=0
    while [ "$le_i" -lt "$le_n" ]; do
        printf "\\$(printf %03o $((le_v & 255)))"
        le_v=$((le_v >> 8)); le_i=$((le_i + 1))
    done
}

# stream: NREGS qubit registers of WIDTH wires each, no ops, END 0
stream() {
    nregs=$1; width=$2; id=0
    printf 'OPSTRM01'; printf '\001'; le "$nregs" 4
    r=0
    while [ "$r" -lt "$nregs" ]; do
        printf '\000'; le "$width" 4
        w=0
        while [ "$w" -lt "$width" ]; do le "$id" 4; id=$((id + 1)); w=$((w + 1)); done
        r=$((r + 1))
    done
    printf '\377'; le 0 8
}

# UCVEC001: NREGS 1-wire qubit registers, one live lane expecting every
# register to read 1 (the empty stream leaves them 0, so every one is bad)
vectors() {
    nregs=$1
    printf 'UCVEC001'; le "$nregs" 4
    r=0
    while [ "$r" -lt "$nregs" ]; do le 1 4; printf '\000'; le 0 8; r=$((r + 1)); done
    le 1 8
    printf '\001\000'
    r=0
    while [ "$r" -lt "$nregs" ]; do printf '\002\001'; r=$((r + 1)); done
}

# bad_regs entries on the first failing lane
nbad() { sed -n 's/.*"bad_regs": \[\([0-9,]*\)\].*/\1/p' "$1" | head -1 | tr ',' '\n' | grep -c .; }

# 1. registers: 600 registers (was refused above 512, "too many registers")
stream 600 1 > "$T/s600"; vectors 600 > "$T/v600"
"$U" check "vectors:$T/v600" "$T/s600" --list 1 > "$T/o600" 2> "$T/e600"
n=$(nbad "$T/o600")
if [ "$n" = 600 ]; then pass "600 registers checked, all 600 bad registers named"
else fail "600 registers: bad_regs named $n ($(cat "$T/e600"))"; fi

# 2. bad_regs: every bad register is named (was the first 8)
stream 10 1 > "$T/s10"; vectors 10 > "$T/v10"
"$U" check "vectors:$T/v10" "$T/s10" --list 1 > "$T/o10" 2> "$T/e10"
n=$(nbad "$T/o10")
if [ "$n" = 10 ]; then pass "10 bad registers, all 10 named"
else fail "10 bad registers: bad_regs named $n"; fi

# gaussian-shaped stream (two 39-wire registers) for the option cases
stream 2 39 > "$T/g"

# 3. --k: 65 patterns (was refused above 64, "bad --k")
args=""; i=0
while [ "$i" -lt 65 ]; do args="$args --k splitmix:$i"; i=$((i + 1)); done
# shellcheck disable=SC2086
"$U" check gaussian "$T/g" --lanes 64 --list 0 $args > "$T/ok" 2> "$T/ek"
n=$(grep -c '"task": "gaussian", "k"' "$T/ok")
if [ "$n" = 65 ]; then pass "65 --k patterns, 65 runs"
else fail "65 --k patterns: $n runs ($(cat "$T/ek"))"; fi

# 4. k pattern text: printed whole (was cut to 63 characters)
long="splitmix:0000000000000000000000000000000000000000000000000000000000000000000001"
"$U" check gaussian "$T/g" --lanes 64 --list 0 --k "$long" > "$T/ot" 2> "$T/et"
if grep -q "\"k\": \"$long\"" "$T/ot"; then pass "k pattern of ${#long} characters printed whole"
else fail "k pattern text cut: $(grep -o '"k": "[^"]*"' "$T/ot" | head -1)"; fi

# 5. --words: 70000 words per pass (was refused above 65536, "bad --words")
"$U" check gaussian "$T/g" --lanes 64 --list 0 --k zero --words 70000 > "$T/ow" 2> "$T/ew"
if grep -q '"task": "gaussian"' "$T/ow"; then pass "--words 70000 accepted and run"
else fail "--words 70000: $(cat "$T/ew")"; fi

# 6. --ri: 17 points (was refused above 16, "bad --ri"); a stream with a bad
#    magic shows the options were all taken and the stream was reached
printf 'NOTASTRM' > "$T/bad"
args=""; i=0
while [ "$i" -lt 17 ]; do args="$args --ri $i,$i,$i"; i=$((i + 1)); done
# shellcheck disable=SC2086
"$U" check coulomb-potential "$T/bad" $args > "$T/or" 2> "$T/er"
if grep -q 'load/bad-magic' "$T/or"; then pass "17 --ri points accepted"
else fail "17 --ri points: $(cat "$T/er")"; fi

# 7. ref: a line of any length is read whole (was split at 511 bytes)
pad=$(printf '%600s' '')
printf '65536%s 0 0\n' "$pad" | "$U" ref hhl-angle > "$T/oref" 2>&1
if grep -q '^X=65536 ' "$T/oref"; then pass "ref reads a 615-byte line whole"
else fail "ref split a long line: $(cat "$T/oref")"; fi

# 8. --drop: a kind name is matched by its whole length (no 31-byte buffer)
name=CCXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
"$U" check gaussian "$T/g" --lanes 64 --drop "$name:1" > /dev/null 2> "$T/ed"
if grep -q "bad --drop kind $name" "$T/ed"; then pass "--drop names a ${#name}-byte kind back whole"
else fail "--drop long kind: $(cat "$T/ed")"; fi

if [ "$fails" -ne 0 ]; then echo "caps court: $fails failed"; exit 1; fi
echo "caps court: all cases passed"
