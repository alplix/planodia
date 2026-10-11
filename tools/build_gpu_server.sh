#!/bin/bash
# Planodia OpenCL GPU apps built on the bitboinc build server, from a directory containing src/:
#   Linux x86_64    ubuntu:20.04 container (GLIBC 2.31), one binary per vendor: nvidia, amd, intel
#   Windows x86_64  host mingw-w64, static, per vendor
#   Windows ARM64   llvm-mingw, static, Qualcomm Adreno (Snapdragon X)
# OpenCL itself is loaded at run time (cl_dynload.c), so no OpenCL library is needed to build or to start the app.
# Output: out/planodia_opencl_<vendor>_v<V>_<os>_<arch>[.exe]
set -e
V=${V:-1}
W=$PWD
mkdir -p out inc
[ -e inc/CL ] || ln -s /usr/include/CL inc/CL
BOINC_SRC=/opt/bitboinc-src/boinc

cat > inner_ocl.sh <<'EOF'
#!/bin/bash
set -e
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq && apt-get install -y -qq build-essential m4 pkg-config file opencl-headers >/dev/null
cp -r /boincsrc-orig /boincsrc && cd /boincsrc
for f in aclocal.m4 configure config.h.in Makefile.in Makefile; do find . -name "$f" -exec touch {} +; done
./configure --disable-client --disable-manager --disable-server --enable-apps --no-create --no-recursion --disable-maintainer-mode >/dev/null
make -C api -j4 >/dev/null && make -C lib -j4 >/dev/null
B=/boincsrc
cd /work
mkdir -p obj_lin
for vendor in nvidia amd intel; do
    gcc -O2 -std=c99 -I src -DCL_TARGET_OPENCL_VERSION=120 "-DEXPECTED_GPU_VENDOR=\"$vendor\"" -c src/cl_dynload.c -o obj_lin/cl_dynload_$vendor.o
    g++ -O2 -std=c++17 -DPL_OPENCL -DCL_TARGET_OPENCL_VERSION=120 "-DEXPECTED_GPU_VENDOR=\"$vendor\"" -I src -I $B/api -I $B/lib -c src/planodia_opencl.cpp -o obj_lin/pl_opencl_$vendor.o
    g++ -O2 -std=c++17 -DPL_OPENCL -DCL_TARGET_OPENCL_VERSION=120 "-DEXPECTED_GPU_VENDOR=\"$vendor\"" -I src -I $B/api -I $B/lib -c src/main_planodia_boinc.cpp -o obj_lin/main_$vendor.o
    g++ obj_lin/main_$vendor.o obj_lin/pl_opencl_$vendor.o obj_lin/cl_dynload_$vendor.o $B/api/.libs/libboinc_api.a $B/lib/.libs/libboinc.a \
        -lpthread -ldl -lrt -static-libstdc++ -static-libgcc -o out/planodia_opencl_${vendor}_v${V}_linux_x86_64
done
EOF
echo "=== Linux OpenCL ==="
sudo docker run --rm -v $BOINC_SRC:/boincsrc-orig:ro -v $W:/work -e V=$V ubuntu:20.04 bash /work/inner_ocl.sh
sudo chown -R $(id -u):$(id -g) out obj_lin 2>/dev/null || true
for f in out/planodia_opencl_*_linux_x86_64; do echo "$f: needs GLIBC $(objdump -T $f | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1)"; done

echo "=== Windows x86_64 OpenCL ==="
WB=/opt/bitboinc-src/boinc-mingw64
mkdir -p obj_win
for vendor in nvidia amd intel; do
    D="-DPL_OPENCL -DCL_TARGET_OPENCL_VERSION=120 -DEXPECTED_GPU_VENDOR=\"$vendor\""
    x86_64-w64-mingw32-gcc -O2 -std=c11 -I src -I inc -DCL_TARGET_OPENCL_VERSION=120 "-DEXPECTED_GPU_VENDOR=\"$vendor\"" -c src/cl_dynload.c -o obj_win/cl_dynload_$vendor.o
    x86_64-w64-mingw32-g++ -O2 -std=c++17 $D -I src -I inc -I $WB/api -I $WB/lib -c src/planodia_opencl.cpp -o obj_win/pl_opencl_$vendor.o
    x86_64-w64-mingw32-g++ -O2 -std=c++17 $D -I src -I inc -I $WB/api -I $WB/lib -c src/main_planodia_boinc.cpp -o obj_win/main_$vendor.o
    x86_64-w64-mingw32-g++ -static obj_win/main_$vendor.o obj_win/pl_opencl_$vendor.o obj_win/cl_dynload_$vendor.o \
        $WB/api/libboinc_api.a $WB/lib/libboinc.a -lpsapi -o out/planodia_opencl_${vendor}_v${V}_windows_x86_64.exe
done

echo "=== Windows ARM64 OpenCL (Adreno) ==="
LB=/opt/bitboinc-src/boinc-winarm64
LL=/opt/llvm-mingw/bin
mkdir -p obj_wa
D="-DPL_OPENCL -DCL_TARGET_OPENCL_VERSION=120 -DEXPECTED_GPU_VENDOR=\"qualcomm\""
$LL/aarch64-w64-mingw32-clang -O2 -std=c11 -I src -I inc -DCL_TARGET_OPENCL_VERSION=120 "-DEXPECTED_GPU_VENDOR=\"qualcomm\"" -c src/cl_dynload.c -o obj_wa/cl_dynload.o
$LL/aarch64-w64-mingw32-clang++ -O2 -std=c++17 $D -I src -I inc -I $LB/api -I $LB/lib -c src/planodia_opencl.cpp -o obj_wa/pl_opencl.o
$LL/aarch64-w64-mingw32-clang++ -O2 -std=c++17 $D -I src -I inc -I $LB/api -I $LB/lib -c src/main_planodia_boinc.cpp -o obj_wa/main.o
$LL/aarch64-w64-mingw32-clang++ -static obj_wa/main.o obj_wa/pl_opencl.o obj_wa/cl_dynload.o $LB/api/libboinc_api.a $LB/lib/libboinc.a -lpsapi \
    -o out/planodia_opencl_qualcomm_v${V}_windows_arm64.exe
ls -la out/ | grep opencl
