#!/bin/sh
# QUARTZ build: ./build.sh [--host|--web|--release X.Y]  ->  app/build/quartz.fwsc, app/build/host/quartz_host
set -e
cd "$(dirname "$0")"
export JIELI_TOOLCHAIN="${JIELI_TOOLCHAIN:-$PWD/.tools/toolchain}"
export AC79_SDK="${AC79_SDK:-$PWD/.tools/sdk}"

if [ ! -d "$JIELI_TOOLCHAIN/pi32v2/include" ]; then
    echo "build.sh: toolchain missing at $JIELI_TOOLCHAIN (fetch it with base/tools/get_toolchain.sh)" >&2
    exit 1
fi
if [ ! -f "$AC79_SDK/cpu/wl82/tools/uboot.boot" ]; then
    echo "build.sh: $AC79_SDK/cpu/wl82/tools/uboot.boot missing: take the 3 SDK files from the amitv87/fw-AC79_AIoT_SDK mirror, tag AC79NN_SDK_V1.2.1_2023-12-13" >&2
    exit 1
fi

# fm1-base patches: libm and compiler-rt for the C++ DSP on the device; the DSP's place in the flash (o2_first)
for p in patches/*.patch; do
    git -C base apply --reverse --check "../$p" 2>/dev/null ||
        git -C base apply "../$p" || {
            echo "build.sh: $p does not apply to base (fm1-base $(git -C base rev-parse --short HEAD), pinned at $(git ls-tree HEAD base | awk '{print $3}')): check that base is at the pinned commit" >&2
            exit 1
        }
done
ln -sfn "$JIELI_TOOLCHAIN/pi32v2/include" app/libc_include

cd base && exec python3 tools/build.py ../app "$@"
