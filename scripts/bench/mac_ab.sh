#!/bin/bash
# One-command A/B on a Mac (Apple Silicon or Intel): planodia_cli built from BASE_REF the documented way (-O2, as
# tools/build_mac.sh builds the apps), against this working tree, on the profiles in scripts/bench/profiles.
# usage: scripts/bench/mac_ab.sh [rounds per profile, default 3] [profiles, default "quick dense deep"]
#   env: BASE_REF     the baseline (default upstream/master if that remote branch exists, else origin/master)
#        PLANODIA_WORK  (default ~/planodia-work): builds in build/ and bins/, shards in data/, results in
#                     bench-runs/<date>-<profile>/
#        NEW_OPT      compiler flags of the working-tree build (default -O2, the baseline's), e.g. "-O3"
#        NEW_PATCHES  patch files (git diff format, -p1) applied to the working-tree build only
#        OPENCL=0     skip the Apple OpenCL labels (no GPU, or the GPU is busy)
#        RUN_PREFIX   "taskpolicy -b" or "nice -n 19" runs every binary through it (see docs/BENCHMARK.md)
# Needs the Xcode command line tools, git and python3 (the shards are generated, once). With no NEW_OPT and no
# NEW_PATCHES and a clean branch the two sides are the same code: the "new" rows must read "no clear gain; same output",
# which is also the measure of this machine's noise. About 25 minutes for 3 rounds of the default profiles on an M1
# (estimated from the documented timings, not measured); `prod` (about 4 minutes per run) only when named.
#
# Binaries (labels):
#   cpu-base   BASE_REF, planodia_cli, CPU          cpu-new   working tree, CPU
#   ocl-base   BASE_REF, planodia_cli --gpu, Apple OpenCL   ocl-new   working tree, Apple OpenCL
# CPU and OpenCL labels are run in separate calls: the OpenCL scan differs from the CPU scan at rounding level (PLA-009),
# so each pair needs its own baseline.
set -e -o pipefail
here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/../.." && pwd)"
ROUNDS=${1:-3}
PROFILES=${2:-quick dense deep}
W=${PLANODIA_WORK:-$HOME/planodia-work}
export PLANODIA_WORK=$W
if [ -z "${BASE_REF:-}" ]; then
  for r in upstream/master upstream/main origin/master origin/main; do
    if git -C "$repo" rev-parse -q --verify "$r^{commit}" > /dev/null; then BASE_REF=$r; break; fi
  done
fi
[ -n "${BASE_REF:-}" ] || { echo "no upstream/master or origin/master here: set BASE_REF"; exit 2; }
BK="cpu opencl"
if [ "${OPENCL:-1}" = 0 ]; then BK=cpu; fi
B=$W/bins

# env -u REF: a REF left in the caller's environment must not turn the working-tree build into a second baseline
OPT=-O2 REF=$BASE_REF BACKENDS="$BK" bash "$here/build_variant.sh" base
env -u REF BACKENDS="$BK" OPT="${NEW_OPT:--O2}" bash "$here/build_variant.sh" new ${NEW_PATCHES:-}

for prof in $PROFILES; do
  echo "== $prof"
  bash "$here/run_bench.sh" "$prof" "$ROUNDS" cpu-base=$B/base/cpu cpu-new=$B/new/cpu | tail -3
  if [ "${OPENCL:-1}" != 0 ]; then
    bash "$here/run_bench.sh" "$prof" "$ROUNDS" ocl-base=$B/base/opencl ocl-new=$B/new/opencl | tail -3
  fi
done
echo "details of every run: $W/bench-runs/"
