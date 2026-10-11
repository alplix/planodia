#!/bin/bash
# Planodia for macOS (arm64 and Intel): CPU app and OpenCL app (Apple's OpenCL framework, single precision, no FP64).
# Run on the build Mac in a directory that contains src/. BOINC libraries: arm64 ~/wisteria-work/boinc-src-fresh,
# x86_64 ~/wisteria-work/boinc-src-x86_64-min11 (deployment target macOS 11).
# Output: out/planodia_v<V>_macos_<arch>, out/planodia_opencl_apple_v<V>_macos_<arch>
set -e
cd "$(dirname "$0")/.."
V=${V:-1}
OUT=${OUT:-$PWD/out}; mkdir -p "$OUT"
MIN=11.0

build_arch() {  # arch boincdir
    local arch=$1 B=$2 o=$OUT/obj_$1
    rm -rf "$o"; mkdir -p "$o"
    lipo -archs $B/api/libboinc_api.a | grep -q $arch || { echo "ERROR: $B/api/libboinc_api.a is not $arch"; return 1; }
    local CF="-arch $arch -mmacosx-version-min=$MIN -O2 -std=c++17 -I src -I $B/api -I $B/lib"
    local LIBS="$B/api/libboinc_api.a $B/lib/libboinc.a"
    local FW="-framework CoreFoundation -framework IOKit"
    clang++ $CF -c src/main_planodia_boinc.cpp -o $o/main_cpu.o
    clang++ -arch $arch -mmacosx-version-min=$MIN $o/main_cpu.o $LIBS $FW -o $OUT/planodia_v${V}_macos_$arch
    [ -n "$OPENCL" ] || return 0   # Apple OpenCL is about as fast as one CPU core (M1: 0.23 s per light curve): opt-in with OPENCL=1
    # OpenCL: the framework is dlopen()ed by cl_dynload.c, nothing to link
    clang -arch $arch -mmacosx-version-min=$MIN -O2 -std=c11 -I src -DCL_TARGET_OPENCL_VERSION=120 '-DEXPECTED_GPU_VENDOR="apple"' -c src/cl_dynload.c -o $o/cl_dynload.o
    clang++ $CF -DPL_OPENCL -DCL_TARGET_OPENCL_VERSION=120 '-DEXPECTED_GPU_VENDOR="apple"' -c src/planodia_opencl.cpp -o $o/pl_opencl.o
    clang++ $CF -DPL_OPENCL -DCL_TARGET_OPENCL_VERSION=120 '-DEXPECTED_GPU_VENDOR="apple"' -c src/main_planodia_boinc.cpp -o $o/main_ocl.o
    clang++ -arch $arch -mmacosx-version-min=$MIN $o/main_ocl.o $o/pl_opencl.o $o/cl_dynload.o $LIBS $FW -o $OUT/planodia_opencl_apple_v${V}_macos_$arch
}

build_arch arm64 "$HOME/wisteria-work/boinc-src-fresh" || echo "arm64 FAILED"
build_arch x86_64 "$HOME/wisteria-work/boinc-src-x86_64-min11" || echo "x86_64 FAILED"
ls -la "$OUT" | grep -E 'macos'
file "$OUT"/*macos* | cut -c1-150
