# ucverify -- standalone OPSTRM01 verifier.  See README.md.
#
#   make                build ucverify
#   make check          cross-check counts against the published numbers and
#                       run the negative controls (needs UNCOMPUTE)
#   make check STORE=d  also cross-check every d/**/stream.ops.xz against the
#                       grade.json beside it
#   make reproduce REPRO_CARRY30=f REPRO_BLIP=g   list the reproducers' failing lanes
#
# Streams are read through `xz -dc | ucverify ... -`, so nothing is
# decompressed to disk.

CC       ?= cc
CFLAGS   ?= -std=c11 -O2 -Wall -Wextra -Werror
LDLIBS    = -lm
UNCOMPUTE ?= ../uncompute
STORE    ?=
REPRO_CARRY30 ?=
REPRO_BLIP ?=

ucverify: ucverify.c
	$(CC) $(CFLAGS) -o $@ ucverify.c $(LDLIBS)

# the number after "FIELD": in a JSON line (a trailing .0 is dropped)
field = sed -n 's/.*"$(1)": \([0-9.]*\).*/\1/p' | head -1 | sed 's/\.0$$//'

check: ucverify check-counts check-store check-negative
	@echo "make check: all checks passed"

# 1. T/Q/TQ of every paper circuit equals its certified row in results.jsonl
check-counts: ucverify
	@set -e; n=0; \
	for f in $(UNCOMPUTE)/tasks/*/circuits/*/circuit.ops.xz; do \
	  d=$$(dirname $$f); t=$$(basename $$(dirname $$(dirname $$d))); \
	  out=$$(xz -dc $$f | ./ucverify count -); \
	  h=$$(echo "$$out" | sed -n 's/.*"h_ops": "\([0-9a-f]*\)".*/\1/p'); \
	  row=$$(grep "\"$$h\"" $(UNCOMPUTE)/tasks/$$t/results.jsonl || true); \
	  if [ -z "$$row" ]; then echo "FAIL $$t $$d: h_ops $$h not in results.jsonl"; exit 1; fi; \
	  for k in toffoli qubits tq_cost; do \
	    a=$$(echo "$$out" | $(call field,$$k)); b=$$(echo "$$row" | $(call field,$$k)); \
	    if [ "$$a" != "$$b" ]; then echo "FAIL $$t $$(basename $$d) $$k: ucverify $$a, published $$b"; exit 1; fi; \
	  done; \
	  echo "ok  $$t/$$(basename $$d)  T=$$(echo "$$out" | $(call field,toffoli)) Q=$$(echo "$$out" | $(call field,qubits)) TQ=$$(echo "$$out" | $(call field,tq_cost))"; \
	  n=$$((n+1)); \
	done; echo "check-counts: $$n paper circuits match results.jsonl"

# 2. (optional) every stored stream matches the grade.json beside it
check-store: ucverify
	@set -e; [ -n "$(STORE)" ] || { echo "check-store: STORE not set, skipped"; exit 0; }; n=0; \
	for f in $$(find $(STORE) -name stream.ops.xz | sort); do \
	  g=$$(dirname $$f)/grade.json; [ -f $$g ] || continue; \
	  out=$$(xz -dc $$f | ./ucverify count -); \
	  for k in toffoli qubits tq_cost outcomes ops clifford bits; do \
	    a=$$(echo "$$out" | $(call field,$$k)); b=$$(cat $$g | $(call field,$$k)); \
	    if [ "$$a" != "$$b" ]; then echo "FAIL $$f $$k: ucverify $$a, grade.json $$b"; exit 1; fi; \
	  done; \
	  echo "ok  $$f  T=$$(echo "$$out" | $(call field,toffoli)) Q=$$(echo "$$out" | $(call field,qubits))"; n=$$((n+1)); \
	done; echo "check-store: $$n stored streams match their grade.json"

# 3. negative controls on the paper's hhl-angle circuit: the unmodified
#    stream passes; dropping one Toffoli or one phase op must be flagged.
HHL = $(UNCOMPUTE)/tasks/hhl-angle/circuits/discovered/circuit.ops.xz
check-negative: ucverify
	@set -e; \
	xz -dc $(HHL) | ./ucverify check hhl-angle - --k zero --k xormix --list 0 > /dev/null \
	  && echo "ok  control: unmodified hhl-angle/discovered passes (k = zero, xormix)" \
	  || { echo "FAIL: unmodified circuit rejected"; exit 1; }; \
	if xz -dc $(HHL) | ./ucverify check hhl-angle - --k zero --list 0 --drop CCX:700 > tmp_neg.txt 2>/dev/null; \
	  then echo "FAIL: dropped Toffoli not detected"; exit 1; fi; \
	echo "ok  negative: CCX #700 dropped -> FAIL, wrong_output lanes $$(sed -n 2,9p tmp_neg.txt | $(call field,wrong_output))"; \
	if xz -dc $(HHL) | ./ucverify check hhl-angle - --k zero --list 0 --drop CZ:100 > tmp_neg.txt 2>/dev/null; \
	  then echo "FAIL: dropped phase op not detected"; exit 1; fi; \
	o=$$(sed -n 2,9p tmp_neg.txt | $(call field,outcome_dependent_sign)); w=$$(sed -n 2,9p tmp_neg.txt | $(call field,wrong_output)); \
	[ "$$o" -gt 0 ] || { echo "FAIL: dropped phase op gave no sign failure"; exit 1; }; \
	echo "ok  negative: CZ #100 dropped -> FAIL, outcome-dependent sign on $$o lanes, wrong_output $$w"; \
	rm -f tmp_neg.txt

reproduce: ucverify
	@if [ -n "$(REPRO_CARRY30)" ]; then ./ucverify check hhl-angle $(REPRO_CARRY30) --list 100; fi; \
	if [ -n "$(REPRO_BLIP)" ]; then ./ucverify check hhl-angle $(REPRO_BLIP) --list 100; fi; true

clean:
	rm -f ucverify tmp_neg.txt

.PHONY: check check-counts check-store check-negative reproduce clean
