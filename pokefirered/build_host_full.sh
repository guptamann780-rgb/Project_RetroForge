#!/bin/bash
# Full host build for RetroForge/pokefirered.
# Run from the repo root: bash build_host_full.sh
#
# Every failure gets its FULL error output saved to its own file under
# build_host/logs/, and a summary list at the end -- nothing scrolls
# away unseen, unlike piping straight to the terminal.

set -u  # treat unset variables as errors, catches typos in this script itself
cd "$(dirname "$0")" || exit 1

# ---------------------------------------------------------------------
# STEP 0: refuse to build against an ambiguous/duplicate header.
# Our -iquote order is "include" before "host_src", so if a stray copy
# of gba_memory_host.h exists under include/, it silently wins over the
# real one in host_src/ -- exactly the bug from last session. Building
# 283 files against the wrong copy wastes real time, so we hard-stop.
# ---------------------------------------------------------------------
echo "=== Step 0: checking for duplicate gba_memory_host.h ==="
DUPES=$(find . -name "gba_memory_host.h")
DUPE_COUNT=$(echo "$DUPES" | grep -c .)
echo "$DUPES"
if [ "$DUPE_COUNT" -ne 1 ]; then
    echo ""
    echo "ABORT: expected exactly 1 copy of gba_memory_host.h (in host_src/),"
    echo "found $DUPE_COUNT. Delete the extra copy/copies above, keeping only"
    echo "the one in host_src/, then re-run this script."
    exit 1
fi
if ! echo "$DUPES" | grep -q "^\./host_src/gba_memory_host.h$"; then
    echo ""
    echo "ABORT: the one copy found is NOT at ./host_src/gba_memory_host.h"
    echo "(found: $DUPES). Move it there, then re-run."
    exit 1
fi
echo "OK: exactly one copy, in the right place."
echo ""

# Clean slate -- stale .o files from before the HOST_BUILD/duplicate-header
# fixes could otherwise sit around and give false confidence.
rm -rf build_host
mkdir -p build_host/data build_host/logs

FAIL_TOTAL=0

# ---------------------------------------------------------------------
# STEP 1: src/*.c
# ---------------------------------------------------------------------
echo "=== Step 1: building src/*.c ==="
src_total=0
src_fail=0
: > /tmp/src_fail_list.txt
for f in src/*.c; do
    [ "$f" = "src/host_test.c" ] && continue
    src_total=$((src_total + 1))
    name=$(basename "$f" .c)
    logfile="build_host/logs/src_${name}.log"

    cpp -iquote include -iquote host_src -iquote . -DHOST_BUILD -DFIRERED -DENGLISH "$f" 2> "$logfile" \
      | ./tools/preproc/preproc -i "$f" charmap.txt 2>> "$logfile" \
      | gcc -m32 -x cpp-output -c -o "build_host/${name}.o" - 2>> "$logfile"
    status=("${PIPESTATUS[@]}")

    if [ "${status[0]}" -ne 0 ] || [ "${status[1]}" -ne 0 ] || [ "${status[2]}" -ne 0 ]; then
        echo "FAIL: $f  (see $logfile)"
        echo "$f" >> /tmp/src_fail_list.txt
        src_fail=$((src_fail + 1))
    else
        rm -f "$logfile"   # only keep logs for actual failures
    fi
done
echo "src/*.c: $((src_total - src_fail))/$src_total succeeded, $src_fail failed"
FAIL_TOTAL=$((FAIL_TOTAL + src_fail))
echo ""

# ---------------------------------------------------------------------
# STEP 2: data/*.s
# ---------------------------------------------------------------------
echo "=== Step 2: building data/*.s ==="
data_total=0
data_fail=0
: > /tmp/data_fail_list.txt
for f in data/*.s; do
    data_total=$((data_total + 1))
    name=$(basename "$f" .s)
    logfile="build_host/logs/data_${name}.log"

    if [ "$name" = "event_scripts" ]; then
        ./tools/preproc/preproc "$f" charmap.txt -I asm -I asm/macros 2> "$logfile" \
          | cpp -iquote host_src/data_patch -iquote include -iquote asm -iquote asm/macros -iquote . - 2>> "$logfile" \
          | ./tools/preproc/preproc -ie "$f" charmap.txt -ie asm -ie asm/macros 2>> "$logfile" \
          | as --32 --defsym FIRERED=1 --defsym REVISION=0 --defsym ENGLISH=1 --defsym MODERN=0 \
            -o "build_host/data/${name}_x86.o" - 2>> "$logfile"
    else
        ./tools/preproc/preproc "$f" charmap.txt 2> "$logfile" \
          | cpp -iquote include -iquote asm -iquote . - 2>> "$logfile" \
          | ./tools/preproc/preproc -ie "$f" charmap.txt 2>> "$logfile" \
          | as --32 --defsym FIRERED=1 --defsym REVISION=0 --defsym ENGLISH=1 --defsym MODERN=0 \
            -o "build_host/data/${name}_x86.o" - 2>> "$logfile"
    fi
    status=("${PIPESTATUS[@]}")
    ok=1
    for s in "${status[@]}"; do
        [ "$s" -ne 0 ] && ok=0
    done

    if [ "$ok" -ne 1 ]; then
        echo "FAIL: $f  (see $logfile)"
        echo "$f" >> /tmp/data_fail_list.txt
        data_fail=$((data_fail + 1))
    else
        rm -f "$logfile"
    fi
done
echo "data/*.s: $((data_total - data_fail))/$data_total succeeded, $data_fail failed"
FAIL_TOTAL=$((FAIL_TOTAL + data_fail))
echo ""

# ---------------------------------------------------------------------
# STEP 3: host_src/*.c
# ---------------------------------------------------------------------
echo "=== Step 3: building host_src/*.c ==="
host_total=0
host_fail=0
: > /tmp/host_fail_list.txt
for f in host_src/*.c; do
    [ -f "$f" ] || continue
    host_total=$((host_total + 1))
    name=$(basename "$f" .c)
    logfile="build_host/logs/host_${name}.log"

    gcc -m32 -DHOST_BUILD -iquote include -iquote host_src -iquote . -c "$f" -o "build_host/${name}.o" 2> "$logfile"
    status=$?

    if [ "$status" -ne 0 ]; then
        echo "FAIL: $f  (see $logfile)"
        echo "$f" >> /tmp/host_fail_list.txt
        host_fail=$((host_fail + 1))
    else
        rm -f "$logfile"
    fi
done
echo "host_src/*.c: $((host_total - host_fail))/$host_total succeeded, $host_fail failed"
FAIL_TOTAL=$((FAIL_TOTAL + host_fail))
echo ""

# ---------------------------------------------------------------------
# SUMMARY
# ---------------------------------------------------------------------
echo "=== SUMMARY ==="
echo "Total object files: $(ls build_host/*.o build_host/data/*.o 2>/dev/null | wc -l)"
echo "Total failures: $FAIL_TOTAL"
if [ "$FAIL_TOTAL" -gt 0 ]; then
    echo ""
    echo "Failed files:"
    cat /tmp/src_fail_list.txt /tmp/data_fail_list.txt /tmp/host_fail_list.txt 2>/dev/null
    echo ""
    echo "Full error text for each failure is saved under build_host/logs/"
    echo "e.g.: cat build_host/logs/src_<name>.log"
fi

exit 0
