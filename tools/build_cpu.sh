#!/bin/bash
# Planodia CPU apps for every platform bitboinc serves, built on the bitboinc build server against the per-target
# BOINC libraries in /opt/bitboinc-src/boinc-<target>. Run from a directory that contains src/ (this repo's src/).
# Fully static Linux builds (no glibc dependency), mingw / llvm-mingw for Windows. Each target is independent.
# Output: out/planodia_v<V>_<os>_<arch>[.exe]
V=${V:-1}
mkdir -p out
LLVM=/opt/llvm-mingw/bin
X86_32="-msse2 -mfpmath=sse"      # 32-bit x86: SSE2 arithmetic instead of x87

build() {  # tag  cxx  boinc-dir  output  link-flags  [extra-compile-flags]
    local tag=$1 cxx="$2 $6" B=/opt/bitboinc-src/$3 outf=out/$4 libs=$5
    echo "=== $tag ==="
    local AP=$B/api LP=$B/lib
    [ -f $B/api/.libs/libboinc_api.a ] && AP=$B/api/.libs
    [ -f $B/lib/.libs/libboinc.a ] && LP=$B/lib/.libs
    if $cxx -O2 -std=c++17 -I src -I $B/api -I $B/lib src/main_planodia_boinc.cpp -static $AP/libboinc_api.a $LP/libboinc.a $libs -o $outf; then
        file $outf | cut -c1-120
    else
        echo "$tag FAILED"
    fi
}

build x86_64  g++                          boinc          planodia_v${V}_linux_x86_64          "-lpthread"
build i686    i686-linux-gnu-g++           boinc-i686     planodia_v${V}_linux_i686            "-lpthread" "$X86_32"
build aarch64 aarch64-linux-gnu-g++        boinc-aarch64  planodia_v${V}_linux_aarch64         "-lpthread"
build armhf   arm-linux-gnueabihf-g++      boinc-armhf    planodia_v${V}_linux_armhf           "-lpthread -lrt"
build ppc64le powerpc64le-linux-gnu-g++    boinc-ppc64le  planodia_v${V}_linux_ppc64le         "-lpthread"
build riscv64 riscv64-linux-gnu-g++        boinc-riscv64  planodia_v${V}_linux_riscv64         "-lpthread"
build win64   /usr/bin/x86_64-w64-mingw32-g++ boinc-mingw64 planodia_v${V}_windows_x86_64.exe  "-lpsapi"
build win32   /usr/bin/i686-w64-mingw32-g++   boinc-mingw32 planodia_v${V}_windows_x86.exe     "-lpsapi" "$X86_32"
build winarm64 $LLVM/aarch64-w64-mingw32-clang++ boinc-winarm64 planodia_v${V}_windows_arm64.exe "-lpsapi"
ls -la out/
