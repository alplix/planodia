#!/bin/bash
# Planodia for Android aarch64: CPU app and OpenCL app for Qualcomm Adreno phones. Run in the WSL "Ubuntu" distro as root
# (state is lost between calls: give OUT a /mnt/c path):
#   MSYS_NO_PATHCONV=1 wsl -d Ubuntu -u root -- env OUT=/mnt/c/Users/Alp/AppData/Local/Temp/planodia_android bash /mnt/c/Users/Alp/Documents/planodia/tools/build_android.sh
# NDK r27c, API 24 (matches the prebuilt BOINC-for-Android libs), libc++ static, PIE; the apps may only NEED libc/libm/libdl.
set -e
cd "$(dirname "$0")/.."
V=${V:-1}
NDK=/root/android/android-ndk-r27c/toolchains/llvm/prebuilt/linux-x86_64/bin
CC="$NDK/aarch64-linux-android24-clang"
CXX="$NDK/aarch64-linux-android24-clang++"
BOINC_INC="-I /root/boincsrc/boinc/api -I /root/boincsrc/boinc/lib"
LIBS="/root/android/boinc-android/lib/libboinc_api.a /root/android/boinc-android/lib/libboinc.a"
OUT=${OUT:-/tmp/planodia_android}; O=$OUT/obj
rm -rf "$OUT"; mkdir -p "$O"
CF="-O2 -std=c++17 -fPIE -I src"

check() {
    local needed
    needed=$("$NDK/llvm-readelf" -d "$OUT/$1" | grep NEEDED | sed -E 's/.*\[(.*)\]/\1/' | tr '\n' ' ')
    echo "$1 NEEDED: $needed"
    for lib in $needed; do
        case "$lib" in libc.so|libm.so|libdl.so|liblog.so) ;; *) echo "ERROR: unexpected dependency $lib in $1"; exit 1;; esac
    done
}

$CXX $CF $BOINC_INC -c src/main_planodia_boinc.cpp -o $O/main_cpu.o
$CXX -pie -static-libstdc++ $O/main_cpu.o $LIBS -o $OUT/planodia_v${V}_android_aarch64
"$NDK/llvm-strip" $OUT/planodia_v${V}_android_aarch64
check planodia_v${V}_android_aarch64

DEFS='-DPL_OPENCL -DCL_TARGET_OPENCL_VERSION=120 -DEXPECTED_GPU_VENDOR="qualcomm"'
$CC -O2 -std=c11 -fPIE -I src -idirafter /mnt/c/temp -DCL_TARGET_OPENCL_VERSION=120 '-DEXPECTED_GPU_VENDOR="qualcomm"' -c src/cl_dynload.c -o $O/cl_dynload.o
$CXX $CF $DEFS -idirafter /mnt/c/temp $BOINC_INC -c src/planodia_opencl.cpp -o $O/pl_opencl.o
$CXX $CF $DEFS -idirafter /mnt/c/temp $BOINC_INC -c src/main_planodia_boinc.cpp -o $O/main_ocl.o
$CXX -pie -static-libstdc++ $O/main_ocl.o $O/pl_opencl.o $O/cl_dynload.o $LIBS -o $OUT/planodia_opencl_qualcomm_v${V}_android_aarch64
"$NDK/llvm-strip" $OUT/planodia_opencl_qualcomm_v${V}_android_aarch64
check planodia_opencl_qualcomm_v${V}_android_aarch64
ls -la $OUT | grep android
