#!/bin/sh
# All QUARTZ tests: C and C++ unit tests (tests/unit), then the host simulator scripts (app/tests/*.txt).
# Stops at the first failure.
set -e
cd "$(dirname "$0")/.."
OUT=app/build/tests
mkdir -p "$OUT"

for t in tests/unit/*_test.c; do
    [ -e "$t" ] || continue
    cc -std=gnu11 -O1 -Iapp -Ibase/include "$t" -o "$OUT/$(basename "$t" .c)" -lm
    echo "run $t"
    "$OUT/$(basename "$t" .c)"
done

# mod.c is plain C: the DSP's mod_voice_block comes from it
cc -std=gnu11 -O1 -Iapp -Ibase/include -c app/mod.c -o "$OUT/mod.o"

# the DSP is compiled as on the device, minus the runtime shims
DSP="$(ls app/dsp/*.cc 2>/dev/null | grep -v cxx_runtime.cc || true)"
for t in tests/unit/*_test.cc; do
    [ -e "$t" ] || continue
    c++ -std=gnu++11 -O1 -DTEST -Iapp -Ibase/include -Iapp/third_party -Iapp/dsp "$t" $DSP "$OUT/mod.o" \
        $(find app/third_party/plaits app/third_party/stmlib -name '*.cc') \
        -o "$OUT/$(basename "$t" .cc)" -lm
    echo "run $t"
    "$OUT/$(basename "$t" .cc)"
done

./build.sh --host
for s in app/tests/*.txt; do
    echo "run $s"
    app/build/host/quartz_host "$s" app/build/host
done
echo "all tests ok"
