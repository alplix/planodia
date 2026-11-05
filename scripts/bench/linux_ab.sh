#!/bin/bash
# Linux A/B of planodia_cli (CPU): BASE_REF against this working tree, both with GCC and -static as tools/build_cpu.sh
# builds the Linux apps, natively on Linux or in a Debian 12 container on a Mac (docker, or DOCKER=podman). On Apple
# Silicon the container is arm64 and runs at native speed, but it shares the Mac's cores and load: it gives ratios
# between Linux builds, not Linux-against-macOS numbers.
# usage: scripts/bench/linux_ab.sh [rounds, default 3] [profiles, default "quick dense deep"]
#   env: PLANODIA_WORK (default ~/planodia-work; from a Mac the container works in $PLANODIA_WORK/linux), BASE_REF
#        (default upstream/master, else origin/master), CLANG=1 also builds both sides with clang, DOCKER (default docker)
# Labels: gcc-base, gcc-new (and clang-base, clang-new). Builds of one compiler must write the same lines; GCC and clang
# may differ at rounding level (PLA-010).
# Needs on Linux: g++, git, python3, GNU time (and clang). On a Mac: docker or podman, network once for the image.
set -e -o pipefail
here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/../.." && pwd)"
ROUNDS=${1:-3}
PROFILES=${2:-quick dense deep}
if [ -z "${BASE_REF:-}" ]; then
  for r in upstream/master upstream/main origin/master origin/main; do
    if git -C "$repo" rev-parse -q --verify "$r^{commit}" > /dev/null; then BASE_REF=$r; break; fi
  done
fi
if [ "$(uname)" = Darwin ]; then
  D=${DOCKER:-docker}
  W=${PLANODIA_WORK:-$HOME/planodia-work}/linux
  IMAGE=bitboinc-bench:linux-1      # bump the tag when the package list changes
  mkdir -p "$W/image"
  if ! $D image inspect $IMAGE > /dev/null 2>&1; then
    cat > "$W/image/Dockerfile" <<'EOF'
FROM python:3.12-slim-bookworm
RUN apt-get update && apt-get install -y --no-install-recommends gcc g++ clang git patch time libc6-dev \
    libjemalloc-dev libmimalloc-dev && rm -rf /var/lib/apt/lists/* && git config --global --add safe.directory '*'
EOF
    $D build -q -t $IMAGE "$W/image"
  fi
  exec $D run --rm -v "$repo":/src:ro -v "$W":/work -e PLANODIA_WORK=/work -e BASE_REF="$BASE_REF" -e CLANG="${CLANG:-0}" \
    $IMAGE bash /src/scripts/bench/linux_ab.sh "$ROUNDS" "$PROFILES"
fi

W=${PLANODIA_WORK:-$HOME/planodia-work}
export PLANODIA_WORK=$W
B=$W/bins
echo "== $(g++ --version | head -1), $(ldd --version 2>&1 | head -1), $(uname -m)"
OPT=-O2 REF=$BASE_REF CXX=g++ CC=gcc BACKENDS=cpu LDFLAGS=-static bash "$here/build_variant.sh" lx-base
env -u REF OPT=-O2 CXX=g++ CC=gcc BACKENDS=cpu LDFLAGS=-static bash "$here/build_variant.sh" lx-new
LABELS="gcc-base=$B/lx-base/cpu gcc-new=$B/lx-new/cpu"
if [ "${CLANG:-0}" = 1 ]; then
  echo "== $(clang++ --version | head -1)"
  OPT=-O2 REF=$BASE_REF CXX=clang++ CC=clang BACKENDS=cpu LDFLAGS=-static bash "$here/build_variant.sh" lx-clang-base
  env -u REF OPT=-O2 CXX=clang++ CC=clang BACKENDS=cpu LDFLAGS=-static bash "$here/build_variant.sh" lx-clang-new
  LABELS="$LABELS clang-base=$B/lx-clang-base/cpu clang-new=$B/lx-clang-new/cpu"
fi
for prof in $PROFILES; do
  echo "== $prof, $ROUNDS rounds, BASE_REF $BASE_REF"
  bash "$here/run_bench.sh" "$prof" "$ROUNDS" $LABELS
done
