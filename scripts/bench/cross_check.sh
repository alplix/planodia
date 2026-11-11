#!/bin/bash
# Builds of planodia_cli (CPU) for the x86_64 platforms a Mac cannot run natively, run under emulation to check that
# they compile, link, run to the end and write the lines we expect: Windows (cross-compiled with MinGW-w64 GCC, static,
# run under Wine) and Linux x86_64 (GCC, -static as tools/build_cpu.sh), both in an x86_64 Debian 12 container, for
# BASE_REF and this working tree. On Apple Silicon the container runs under Rosetta, so the times are only a rough ratio
# between builds of one platform, never a speed claim (docs/BENCHMARK.md). The x86_64 builds use the compiler's default
# instruction set (SSE2, no FMA), as tools/build_cpu.sh does.
# usage: scripts/bench/cross_check.sh [profile, default quick] [rounds, default 1]
#   env: PLANODIA_WORK (default ~/planodia-work; the container works in $PLANODIA_WORK/x86), BASE_REF, MINGW_PREFIX
#        (default x86_64-w64-mingw32-, found on the host: brew install mingw-w64), DOCKER (default docker)
# Labels: win-base, win-new, lx64-base, lx64-new. Each platform's two builds must write the same lines (unless this tree
# changes results); between platforms they may differ at rounding level (PLA-010), and the table shows whether they do.
# The hash ignores the CRs the Windows build writes.
set -e -o pipefail
here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/../.." && pwd)"
PROFILE=${1:-quick}
ROUNDS=${2:-1}
IMAGE=bitboinc-bench:x86-1      # bump the tag when the package list changes
if [ -z "${BASE_REF:-}" ]; then
  for r in upstream/master upstream/main origin/master origin/main; do
    if git -C "$repo" rev-parse -q --verify "$r^{commit}" > /dev/null; then BASE_REF=$r; break; fi
  done
fi

if [ -z "${CROSS_CHECK_INSIDE:-}" ]; then
  D=${DOCKER:-docker}
  W=${PLANODIA_WORK:-$HOME/planodia-work}/x86
  M=${MINGW_PREFIX:-x86_64-w64-mingw32-}
  command -v "${M}g++" > /dev/null || { echo "no ${M}g++: brew install mingw-w64 (or set MINGW_PREFIX)"; exit 2; }
  echo "== Windows builds: $(${M}g++ --version | head -1)"
  # PLAT empty: no -mmacosx-version-min for the cross compiler; -static: no libstdc++ or libgcc DLLs to find under Wine
  OPT=-O2 REF=$BASE_REF PLANODIA_WORK=$W CXX=${M}g++ CC=${M}gcc PLAT= BACKENDS=cpu LDFLAGS=-static \
    bash "$here/build_variant.sh" win-base
  env -u REF OPT=-O2 PLANODIA_WORK=$W CXX=${M}g++ CC=${M}gcc PLAT= BACKENDS=cpu LDFLAGS=-static \
    bash "$here/build_variant.sh" win-new
  mkdir -p "$W/image"
  if ! $D image inspect $IMAGE > /dev/null 2>&1; then
    cat > "$W/image/Dockerfile" <<'EOF'
FROM --platform=linux/amd64 debian:12-slim
RUN apt-get update && apt-get install -y --no-install-recommends gcc g++ libc6-dev git patch time python3 wine wine64 \
    ca-certificates && rm -rf /var/lib/apt/lists/* && git config --global --add safe.directory '*'
ENV WINEDEBUG=-all
RUN wineboot -i > /dev/null 2>&1 || true
EOF
    $D build --platform linux/amd64 -q -t $IMAGE "$W/image"
  fi
  exec $D run --rm --platform linux/amd64 -v "$repo":/src:ro -v "$W":/work -e PLANODIA_WORK=/work -e BASE_REF="$BASE_REF" \
    -e CROSS_CHECK_INSIDE=1 $IMAGE bash /src/scripts/bench/cross_check.sh "$PROFILE" "$ROUNDS"
fi

# Inside the container (or on an x86_64 Linux machine with Wine and the win-* builds already made)
W=${PLANODIA_WORK:-$HOME/planodia-work}
export PLANODIA_WORK=$W
B=$W/bins
echo "== Linux builds: $(g++ --version | head -1), $(ldd --version 2>&1 | head -1), $(uname -m); $(wine --version 2>/dev/null)"
OPT=-O2 REF=$BASE_REF CXX=g++ CC=gcc BACKENDS=cpu LDFLAGS=-static bash "$here/build_variant.sh" lx64-base
env -u REF OPT=-O2 CXX=g++ CC=gcc BACKENDS=cpu LDFLAGS=-static bash "$here/build_variant.sh" lx64-new
mkdir -p "$W/wrap"
LABELS=""
for v in base new; do
  # run_bench.sh runs a label's file directly; Wine needs the .exe name and its own launcher
  cp "$B/win-$v/cpu" "$B/win-$v/cpu.exe"
  printf '#!/bin/sh\nexec wine %s "$@"\n' "$B/win-$v/cpu.exe" > "$W/wrap/win-$v"
  chmod +x "$W/wrap/win-$v"
  LABELS="$LABELS win-$v=$W/wrap/win-$v"
done
bash "$here/run_bench.sh" "$PROFILE" "$ROUNDS" $LABELS lx64-base=$B/lx64-base/cpu lx64-new=$B/lx64-new/cpu
