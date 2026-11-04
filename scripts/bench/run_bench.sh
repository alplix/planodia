#!/bin/bash
# Interleaved speed benchmark of planodia_cli builds (build_variant.sh) on a fixed synthetic work unit.
# usage: run_bench.sh <profile> <rounds> <label>=<binary> [<label>=<binary> ...]
#   profile: a file in scripts/bench/profiles/ without .wu, or a path to a .wu file (the shards to generate and the
#   lines of the work unit's `in` file). The shards come from make_shards.py and are cached in $PLANODIA_WORK/data.
#   Runs A B C ... A B C ... (rounds) in fresh run directories, so that load drift hits every binary alike. A binary
#   named "opencl" gets --gpu; a label may name a wrapper script that sets environment variables and execs a build.
#   env: BENCH_OUT (default $PLANODIA_WORK/bench-runs, PLANODIA_WORK default ~/planodia-work), RUN_PREFIX (e.g.
#        "taskpolicy -b" or "nice -n 19"), RUN_ARGS (more planodia_cli arguments for every binary)
#   macOS and Linux (GNU time); the GPU-utilisation column only on macOS and only roughly (whole GPU, sampled every 0.5 s)
# Output: one line per run (label, round, wall s, user s, sys s, max RSS MB, exit code, hash of the result lines, mean GPU
# busy %, sum of the SDE column, number of lines that differ from the first run of the first label), then the summary of
# summarize_bench.py. The result lines are planodia_cli's standard output: one line per light curve, no time, no host.
set -u
here="$(cd "$(dirname "$0")" && pwd)"
W=${PLANODIA_WORK:-$HOME/planodia-work}; case $W in /*) ;; *) W=$PWD/$W ;; esac   # the runs cd into their directory
[ $# -ge 3 ] || { sed -n 2,13p "$0"; exit 2; }
PROFILE=$1; ROUNDS=$2; shift 2
WU=$PROFILE; [ -f "$WU" ] || WU=$here/profiles/$PROFILE.wu
[ -f "$WU" ] || { echo "no profile $PROFILE"; exit 2; }
case $ROUNDS in ''|*[!0-9]*) echo "rounds must be a number"; exit 2 ;; esac
for lb in "$@"; do
  case $lb in *=*) ;; *) echo "'$lb' is not label=binary"; exit 2 ;; esac
  [ -x "${lb#*=}" ] || { echo "${lb#*=} is not an executable file"; exit 2; }
done
sha12() { if command -v shasum > /dev/null 2>&1; then shasum; else sha1sum; fi | cut -c1-12; }

# --- the profile: `shard <light curves> <cadence s> <seed>` lines and `in <text>` lines
SHARDS=$(awk '$1=="shard"{print $2, $3, $4}' "$WU")
INTEXT=$(awk '$1=="in"{sub(/^in[ \t]+/, ""); print}' "$WU")
[ -n "$SHARDS" ] && [ -n "$INTEXT" ] || { echo "profile $WU needs 'shard <light curves> <cadence s> <seed>' and 'in <line>' lines"; exit 2; }
NS=$(printf '%s\n' "$SHARDS" | wc -l | tr -d ' ')
WANT=$(printf '%s\n' "$INTEXT" | awk 'NR==2{print $5}')
[ "$NS" = "$WANT" ] || { echo "profile $WU: $NS shard lines, but its input line says $WANT shards"; exit 2; }
EXPECT=$(printf '%s\n' "$SHARDS" | awk '{s += $1} END {print s}')
FMT=$(printf '%s\n' "$INTEXT" | awk 'NR==1{print $2}')
# planodia_cli takes the search parameters from PL_* variables, the app from the `in` file: the profile keeps the `in` text
# of a real work unit and the variables are made from it here (planodia_cli.cpp and main_planodia_boinc.cpp set the same
# Params; the app's range checks are not repeated, the profiles keep to them)
INENV=$(printf '%s\n' "$INTEXT" | awk '
  NR==2 { printf "PL_PMIN=%s PL_PMAX=%s PL_FF=%s PL_FLATTEN=%s ", $1, $2, $3, $4 }
  NR==3 { d = $3; for (i = 4; i <= 2 + $2; i++) d = d "," $i
          printf "PL_BINW=%s PL_DURS=%s PL_MAXP=%s PL_PASSES=%s PL_FLOORWIN=%s ", $1, d, $(3 + $2), $(4 + $2), $(5 + $2) }')
case $FMT in
  1) ;;
  2) case $INENV in *PL_BINW=*) ;; *) echo "profile $WU: input format 2 needs the third line"; exit 2 ;; esac ;;
  *) echo "profile $WU: the first input line must be 'planodia 1' or 'planodia 2'"; exit 2 ;;
esac
# a PL_* variable left in the caller's environment must not change a profile
ENVCLEAN="-u PL_PMIN -u PL_PMAX -u PL_FF -u PL_FLATTEN -u PL_BINW -u PL_DURS -u PL_MAXP -u PL_PASSES -u PL_FLOORWIN"

# --- the shards, generated once (python3 only, no numpy) and kept in the work directory, outside the repository
DATA=$W/data; mkdir -p "$DATA"
GEN=$(python3 -B "$here/make_shards.py" --version) || { echo "python3 is needed to generate the shards"; exit 2; }
SHARDFILES=(); nsf=0
while read -r nlc cad seed; do
  f=$DATA/shard-g$GEN-n$nlc-c$cad-s$seed.skt
  if [ ! -s "$f" ]; then
    echo "generating $f" >&2
    python3 -B "$here/make_shards.py" "$f" "$nlc" "$cad" "$seed" || { echo "make_shards.py failed"; exit 2; }
  fi
  SHARDFILES[$nsf]=$f; nsf=$((nsf + 1))
done <<SHARD_LINES
$SHARDS
SHARD_LINES

STAMP=$(date +%Y%m%d-%H%M%S)
OUT=${BENCH_OUT:-$W/bench-runs}/$STAMP-$(basename "$WU" .wu)
mkdir -p "$OUT"
RESULTS=$OUT/results.tsv
if [ "$(uname)" = Darwin ]; then
  TIMECMD=(/usr/bin/time -l); RSSDIV=1048576                                   # bytes
else
  [ -x /usr/bin/time ] || { echo "no /usr/bin/time (Debian/Ubuntu: apt install time)"; exit 2; }
  TIMECMD=(/usr/bin/time -f "%e real %U user %S sys
%M maximum resident set size"); RSSDIV=1024                                    # GNU time, KiB
fi
FIRST=${1%%=*}
{
  echo "# $(date '+%Y-%m-%d %H:%M:%S') profile $WU rounds $ROUNDS format $FMT expected result lines $EXPECT"
  echo "# host: $(sysctl -n machdep.cpu.brand_string 2>/dev/null || uname -m); $(uname -sr)"
  echo "# input: $(printf '%s' "$INTEXT" | tr '\n' '|')   env: $INENV"
  echo "# load: $(uptime | sed 's/.*load/load/')"
  i=0; while [ $i -lt $nsf ]; do echo "# shard ${SHARDFILES[$i]} sha1 $(sha12 < "${SHARDFILES[$i]}")"; i=$((i + 1)); done
  for lb in "$@"; do
    b=${lb#*=}
    echo "# ${lb%%=*} = $b sha1 $(sha12 < "$b")"
    info=$(dirname "$b")/build-info.txt; if [ -f "$info" ]; then echo "#   $(sed -n 1p "$info")"; fi
  done
} | tee "$RESULTS"

run_one() { # label binary round
  local label=$1 bin=$2 round=$3 dir=$OUT/$1-$3 args=${RUN_ARGS:-}
  case $bin in /*) ;; *) bin=$PWD/$bin ;; esac
  if [ "$(basename "$bin")" = opencl ]; then args="$args --gpu"; fi
  mkdir -p "$dir"
  local sampler=""
  if command -v ioreg > /dev/null; then
    ( while :; do ioreg -r -d 1 -c IOAccelerator | grep -o '"Device Utilization %"=[0-9]*' | head -1 | cut -d= -f2; sleep 0.5; done ) > "$dir/gpu_util.txt" 2> /dev/null &
    sampler=$!
  fi
  # --time adds a line to stderr only (prepare and scan seconds per light curve); stdout is the result and nothing else
  ( cd "$dir" && env $ENVCLEAN $INENV "${TIMECMD[@]}" ${RUN_PREFIX:-} sh -c 'exec "$@" > stdout.txt 2> stderr.txt' sh "$bin" --time $args "${SHARDFILES[@]}" ) 2> "$dir/time.txt"
  echo $? > "$dir/exit_code"
  [ -n "$sampler" ] && { kill $sampler; wait $sampler; } 2> /dev/null
  local real user sys rss gpu=- h=none key=- ndiff=- lines=0 code tm
  code=$(cat "$dir/exit_code")
  real=$(awk '/ real /{print $1}' "$dir/time.txt"); user=$(awk '/ real /{print $3}' "$dir/time.txt"); sys=$(awk '/ real /{print $5}' "$dir/time.txt")
  rss=$(awk -v d=$RSSDIV '/maximum resident set size/{printf "%.0f", $1/d}' "$dir/time.txt")
  [ -s "$dir/gpu_util.txt" ] && gpu=$(awk '{s+=$1; n++} END {printf "%.0f", s/n}' "$dir/gpu_util.txt")
  if [ -s "$dir/stdout.txt" ]; then
    lines=$(wc -l < "$dir/stdout.txt" | tr -d ' ')
    h=$(sha12 < "$dir/stdout.txt")
    key=$(awk '{s += $4} END {printf "%.2f", s}' "$dir/stdout.txt")           # column 4 of a result line is the SDE
    if [ -s "$OUT/$FIRST-1/stdout.txt" ]; then ndiff=$(diff "$OUT/$FIRST-1/stdout.txt" "$dir/stdout.txt" | grep -c '^>'); fi
  fi
  # a run that ended early without an error code would otherwise hash a short file and look like a result change
  if [ "$code" = 0 ] && [ "$lines" != "$EXPECT" ]; then
    echo "# $label $round: $lines result lines, expected $EXPECT" | tee -a "$RESULTS"; code=98
  fi
  printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$label" "$round" "$real" "$user" "$sys" "$rss" "$code" "$h" "$gpu" "$key" "$ndiff" | tee -a "$RESULTS"
  tm=$(grep 'per light curve' "$dir/stderr.txt" 2> /dev/null | tail -1)
  if [ -n "$tm" ]; then echo "# $label $round: $tm" >> "$RESULTS"; fi
}
r=1
while [ "$r" -le "$ROUNDS" ]; do
  for lb in "$@"; do run_one "${lb%%=*}" "${lb#*=}" "$r"; done
  r=$((r + 1))
done
python3 -B "$here/summarize_bench.py" "$RESULTS" | tee "$OUT/summary.txt"
