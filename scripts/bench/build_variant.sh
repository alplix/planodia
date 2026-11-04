#!/bin/bash
# Builds one variant of planodia_cli, the standalone search, for run_bench.sh. The BOINC apps are not built: they need
# the BOINC libraries and the repository has no stub for them, while planodia_cli contains the whole search
# (planodia_core.h, the OpenCL backend) and writes the same result lines as the app's out file.
# usage: build_variant.sh <name> [patch ...]
#   Copies the repository's tracked files to $PLANODIA_WORK/build/<name>, applies the patches (git diff format, -p1)
#   and builds bins/<name>/cpu (and bins/<name>/opencl, the Apple OpenCL build, on macOS); build-info.txt next to them
#   records the source, flags and compiler. Planodia has no CUDA backend (NVIDIA GPUs run the OpenCL app), so nothing
#   here needs nvcc.
#   env: OPT       compiler flags (default -O2, the build lines of tools/build_mac.sh and tools/build_cpu.sh)
#        BACKENDS  default "cpu opencl" on macOS, "cpu" elsewhere (opencl on Linux needs CL/cl.h and an OpenCL driver)
#        CXX, CC   compilers (default clang++ and clang on macOS, c++ and cc elsewhere)
#        PLAT      platform flags (default -mmacosx-version-min=11.0 on macOS, as tools/build_mac.sh, nothing elsewhere)
#        VENDOR    EXPECTED_GPU_VENDOR of the OpenCL build (default apple on macOS, empty elsewhere: any vendor)
#        LDFLAGS   extra link flags (Linux release builds are -static, see tools/build_cpu.sh)
#        REF       build a git ref of this repository (e.g. upstream/master) instead of its working tree
#        PLANODIA_SRC  the repository to copy (default: this one); PLANODIA_WORK (default ~/planodia-work)
set -e -o pipefail
here="$(cd "$(dirname "$0")" && pwd)"
repo=${PLANODIA_SRC:-$(cd "$here/../.." && pwd)}
[ $# -ge 1 ] || { sed -n 2,17p "$0"; exit 2; }
name=$1; shift
case $name in ''|*[!A-Za-z0-9._-]*) echo "name: letters, digits, '.', '_' and '-' only"; exit 2 ;; esac
W=${PLANODIA_WORK:-$HOME/planodia-work}
OPT=${OPT:--O2}; LDFLAGS=${LDFLAGS:-}
if [ "$(uname)" = Darwin ]; then
  BACKENDS=${BACKENDS:-cpu opencl}; CXX=${CXX:-clang++}; CC=${CC:-clang}
  PLAT=${PLAT--mmacosx-version-min=11.0}; VENDOR=${VENDOR-apple}; CSTD=c11; LIBS=""
else
  BACKENDS=${BACKENDS:-cpu}; CXX=${CXX:-c++}; CC=${CC:-cc}
  PLAT=${PLAT-}; VENDOR=${VENDOR-}; CSTD=c99; LIBS="-ldl"      # dlopen of the OpenCL library (cl_dynload.c)
fi
T=$W/build/$name; out=$W/bins/$name
rm -rf "$T" "$out"; mkdir -p "$T" "$out"        # no binary of an older build stays next to this build's build-info.txt

# a clean copy of the tracked files: an object file, a scratch binary or an untracked edit in the working tree cannot leak in
if [ -n "${REF:-}" ]; then
  git -C "$repo" rev-parse -q --verify "$REF^{commit}" > /dev/null || { echo "no such ref: $REF"; exit 2; }
  (cd "$repo" && git archive "$REF") | tar -xf - -C "$T"
  src="$REF ($(git -C "$repo" rev-parse --short "$REF"))"
else
  # a tracked file that was deleted but not staged would make tar fail, so list only the files that exist
  (cd "$repo" && git ls-files -z | while IFS= read -r -d '' f; do if [ -e "$f" ]; then printf '%s\0' "$f"; fi; done \
     | tar --null -T - -cf -) | tar -xf - -C "$T"
  src="working tree on $(git -C "$repo" rev-parse --short HEAD)"
  if [ -n "$(git -C "$repo" status --porcelain --untracked-files=no)" ]; then src="$src plus uncommitted changes"; fi
fi
[ -f "$T/src/planodia_cli.cpp" ] || { echo "$src has no src/planodia_cli.cpp"; exit 1; }
patched=""
for p in "$@"; do
  [ -f "$p" ] || { echo "no patch file $p"; exit 2; }
  a="$(cd "$(dirname "$p")" && pwd)/$(basename "$p")"
  (cd "$T" && patch -p1 -s < "$a") || { echo "patch $p does not apply to $src"; exit 1; }
  patched="$patched $(basename "$p")"
done

log=$T/build.log; : > "$log"
cc_run() { echo "+ $*" >> "$log"; "$@" >> "$log" 2>&1 || true; }       # a failure shows as a missing binary, below
GPU_DEFS="-DPL_OPENCL -DCL_TARGET_OPENCL_VERSION=120"
for b in $BACKENDS; do
  rm -f "$out/$b"
  case $b in
    cpu)
      cc_run $CXX $PLAT $OPT -std=c++17 -I "$T/src" "$T/src/planodia_cli.cpp" $LDFLAGS -o "$out/cpu" ;;
    opencl)
      if [ "$(uname)" != Darwin ] && ! printf '#include <CL/cl.h>\n' | $CXX -x c++ -E - > /dev/null 2>&1; then
        echo "the opencl build needs CL/cl.h (Debian/Ubuntu: apt install opencl-headers)"; exit 1
      fi
      # the same three translation units as tools/build_mac.sh and tools/build_gpu_server.sh, without the BOINC main
      cc_run $CC $PLAT $OPT -std=$CSTD -I "$T/src" -DCL_TARGET_OPENCL_VERSION=120 "-DEXPECTED_GPU_VENDOR=\"$VENDOR\"" \
        -c "$T/src/cl_dynload.c" -o "$T/cl_dynload.o"
      cc_run $CXX $PLAT $OPT -std=c++17 -I "$T/src" $GPU_DEFS "-DEXPECTED_GPU_VENDOR=\"$VENDOR\"" \
        -c "$T/src/planodia_opencl.cpp" -o "$T/pl_opencl.o"
      cc_run $CXX $PLAT $OPT -std=c++17 -I "$T/src" $GPU_DEFS -c "$T/src/planodia_cli.cpp" -o "$T/cli_opencl.o"
      if [ -f "$T/cl_dynload.o" ] && [ -f "$T/pl_opencl.o" ] && [ -f "$T/cli_opencl.o" ]; then
        cc_run $CXX $PLAT "$T/cli_opencl.o" "$T/pl_opencl.o" "$T/cl_dynload.o" $LIBS $LDFLAGS -o "$out/opencl"
      fi ;;
    cuda) echo "planodia has no CUDA backend: NVIDIA GPUs run the OpenCL app (BACKENDS=opencl, VENDOR=nvidia)"; exit 2 ;;
    *) echo "unknown backend $b (cpu or opencl)"; exit 2 ;;
  esac
  [ -x "$out/$b" ] || { grep -E 'error' "$log" | head || true; echo "build of $b failed, see $log"; exit 1; }
done

cxxv=$($CXX --version 2>&1 | sed -n 1p)
echo "source: $src; patches:${patched:- none}; OPT=$OPT PLAT=${PLAT:-none} LDFLAGS=${LDFLAGS:-none}; CXX: $cxxv" > "$out/build-info.txt"
echo "built $out: $(cd "$out" && ls | grep -v build-info | tr '\n' ' ')($src; OPT=$OPT)"
