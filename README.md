# QUARTZ

A groovebox firmware for the M-VAVE FM-1, built on [fm1-base](https://github.com/charlesvestal/fm1-base)
(submodule `base/`) with Mutable Instruments Plaits vendored in `app/third_party/`.

## Build

    git submodule update --init --recursive
    ./build.sh              # app/build/quartz.fwsc
    ./build.sh --host       # app/build/host/quartz_host, the host simulator
    tests/run_tests.sh      # unit tests, then the simulator scripts in app/tests/

`build.sh` expects the Jieli toolchain in `.tools/toolchain` (`base/tools/get_toolchain.sh`) and the AC79 SDK
files in `.tools/sdk` (from the `amitv87/fw-AC79_AIoT_SDK` mirror, tag `AC79NN_SDK_V1.2.1_2023-12-13`);
`JIELI_TOOLCHAIN` and `AC79_SDK` override both. It applies `patches/fm1-base-device-libs.patch` to `base/`
(links libm and compiler-rt for the C++ DSP).

## License

QUARTZ is GPL-3.0-or-later (`LICENSE`). It includes Mutable Instruments Grids (GPL-3.0, the drum map in
`app/third_party/grids/`), Plaits and Marbles (MIT) and builds on fm1-base (MPL-2.0).
