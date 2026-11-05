# Benchmarking Planodia (`scripts/bench/`)

How to measure whether a change makes Planodia faster and whether it keeps the results, the scripts that do it, and
where to record the results. The method is the one the sibling repository kampylos wrote down on 2026-11-04 after its
first speed-ups, adapted to a BLS search that runs on one CPU core or on an OpenCL GPU and is validated by comparing two
hosts' results (quorum 2). Written 2026-11-04.

**Status (2026-11-04):** written on an Apple M1 and first run on an Apple M4 Pro the same day, where it built and ran
without a fix, CPU and Apple OpenCL. It now also runs Linux arm64 in a container (`linux_ab.sh`) and Windows and Linux
x86_64 builds under emulation (`cross_check.sh`). The first same-results speed-up (candidate 1, the multi-period fold)
is measured in Results, with where the time goes. The "expected" sections further down are kept as they were written;
Results says what they got right.

What is built and run: `planodia_cli` (`src/planodia_cli.cpp`), the standalone search. It contains the whole search
(`planodia_core.h`, and with `-DPL_OPENCL` `planodia_opencl.cpp`) and prints the same result lines as the app writes to
`out`, without the app's `# planodia` header line. The BOINC apps are not built: they need the BOINC libraries, and
this repository has no stub for them (AGENTS.md, "Tests"). The inputs are synthetic shards written by
`scripts/bench/make_shards.py`, because the repository holds no light-curve data.

## Method

### Two kinds of speed change

* **Same results**: build flags that leave floating point alone, a restructured loop that keeps every floating-point
  operation and its order, integer instead of floating-point counters, scheduling of host and GPU work, reordering of
  integer sums in the kernel. The result lines (`planodia_cli`'s standard output) must stay byte-identical to the old
  build's on the same toolchain, backend and device. Such a change needs a measured gain and the output check, nothing
  more; the repository has no tests, so the byte comparison is the test.
* **Result-changing**: anything that moves a printed digit or a peak: another rounding (a reciprocal instead of a
  division, float instead of double, fused multiply-add through `-march=` or `-ffast-math`), the tie-break of the peak
  sort (PLA-011), and every change to the search itself (period grid, phase bins, box durations, number of passes,
  stopping early). Treat these as science or server changes that the maintainers decide on. Planodia runs with quorum 2;
  the validator that compares two hosts' results is not in this repository and what it tolerates is an open question
  (PLA-010). Even a last-digit change can flip a tied peak (PLA-011) or an SDE at its printed precision. Measure such a
  change the same way, report how many result lines differ (the summary prints it), and keep it behind a compile-time
  switch that is off by default until the maintainers decide.

### Rules for every measurement

1. **Build both sides the same way, from clean copies.** The baseline is a git ref (`REF=upstream/master`) built with
   the documented line (`-O2 -std=c++17`, plus `-mmacosx-version-min=11.0` on a Mac, as `tools/build_mac.sh`); the
   candidate is the working tree, or the baseline plus a patch (`build_variant.sh <name> <patch>`). The script copies the
   tracked files, so an object file or an untracked edit in the working tree cannot leak in. `build-info.txt` next to
   each binary records the source, flags and compiler, and `run_bench.sh` copies that line into its results.
2. **Find out where the time goes first.** `planodia_cli --time` prints the seconds per light curve spent in
   `prepare()` and in the scan plus `finish()` (`run_bench.sh` keeps that line of every run as a `#` comment in
   `results.tsv`). Inside the scan, sample a run (`sample <pid> 10` on macOS, `perf record -g` on Linux). For an OpenCL
   build separate the host's time from the kernel's. A change saves at most the share of time it touches: a change to
   10 % of the time cannot gain more than 1.11x.
3. **Use a profile that exercises the change.** A change to the fold loop shows on `quick` and `dense`; a change to
   `prepare()` has about nine times the absolute weight on `dense` (433-point running median) than on `quick`; a change per pass
   shows on `deep`; a kernel change on the OpenCL labels. Confirm the final number on `prod`.
4. **Interleave and repeat.** `run_bench.sh` runs A B C A B C ... in fresh directories; use at least 3 rounds. The load
   from other programs changes over minutes, and interleaving makes it hit every build alike. Compare medians.
5. **Accept by the rule, not by eye.** A candidate is faster when its median is at least 5 % below the baseline's and
   its slowest run beats the baseline's fastest, or when its median is at least 10 % below. Anything else is "no clear
   gain" and is reported as that.
6. **Check the output of every run.** Each run's standard output is hashed; `same output` means every run of a label
   matches the baseline. A run that wrote the wrong number of lines counts as failed (exit 98). Compare like with
   like: builds with libc++ and with libstdc++ can order tied peaks differently (PLA-011), builds that fuse
   multiply-adds and builds that do not can differ (PLA-010), and the OpenCL scan differs from the CPU scan at rounding
   level (PLA-009). So a CPU build is compared with a CPU build of the same compiler and standard library, an OpenCL
   build with an OpenCL build on the same device.
7. **Measure on a quiet machine.** The app is single-threaded, so one busy core is enough, but other programs still
   change the clock and the cache. Pause the BOINC client (CPU and GPU tasks) and other heavy programs, keep a laptop on
   power, look at `uptime` (the scripts record the load). Never run two benchmark processes at once. On a loaded machine
   an interleaved comparison is still fair, but its absolute times and spreads are not worth quoting.
8. **Write it down.** Date, machine, OS, compiler, load, profile, rounds, medians and spread, verdict and output go
   into the Results section below. A number in a commit message, a README or a code comment comes from there, and a
   platform that was not measured is called unmeasured.

### Steps for one change

1. Run the current build on `quick` with `--time`, sample it, and note where the time goes (rule 2).
2. Make the change in the working tree, or as a patch (`build_variant.sh <name> <patch>`); build the baseline with
   `REF=`.
3. Run one round (`run_bench.sh quick 1 ...`) to see that the output stays the same.
4. Run 3 or more rounds on a quiet machine on each profile the change touches, and `prod` for the final number.
5. Checks (there are no tests): for a same-results change, byte-identical output on every profile, and for a change that
   touches sorting, ties or the order of arithmetic also on a second standard library (GCC on Linux, libc++ on a Mac).
   For the kernel: OpenCL output identical to the old OpenCL build on the same device, and CPU against OpenCL compared
   field by field as in PLA-009. A change to `main_planodia_boinc.cpp` (checkpoint, lifecycle, device choice) is not
   covered by this suite; AGENTS.md says there is no harness for it.
6. Measure other platforms where possible (Linux x86_64 first, see below) and name the ones that were not measured.
   A change that can alter results is checked across platforms as PAT-018 describes.
7. Record the results here, and update the numbers in AGENTS.md and in comments that the change moves.

### Traps specific to Planodia

These come from the code and the knowledge base; none has been observed with this suite yet.

* **Quorum 2 means cross-host agreement.** Two hosts, possibly of different platforms, compute the same work unit and
  the validator compares them. Byte-identical output on the machine that measures is necessary for a same-results
  change but not sufficient: the change must not make the result depend on anything that differs between hosts, such as
  the SIMD width (the order of a floating-point reduction), fused multiply-add, a libm function, `-march=`, or a
  thread count. Today the CPU builds already can differ between platforms (PLA-010, PLA-011); a speed change must not
  widen that.
* **The order of arithmetic is part of the result.** In `scan_cpu()` each bin's flux sum is added in point-index order,
  starting from 0.0, the prefix sums run in bin order, and the phase is `t / per` (a division, not a multiplication by a
  reciprocal). Counts are exact in any form. Changing any of the others changes bits, and a bit can move a point into
  the neighbouring bin or break a tie between two periods.
* **Ties.** `finish()` sorts by power with `std::partial_sort` and no tie-break (PLA-011); a change near that code can
  change which of two equal peaks is reported. Run the output check with both standard libraries before calling a
  change there "same results". The synthetic profiles have ties as well (PLA-011 measured them in similar synthetic
  data, not in these files).
* **A result line depends only on its light curve and `Params`** (AGENTS.md, "Determinism per light curve"). That is
  why a resumed task reproduces the cut lines. A speed change that carries state from one light curve to the next
  (a warm start, a cache of results) breaks it; reusing a buffer is fine if it is fully re-initialised.
* **Checkpoints and the wrapper are outside the suite.** The app flushes after every light curve and writes a
  checkpoint file every 20 light curves and whenever the client asks (PLA-014). On a fast GPU (4 ms per light curve,
  README) that is about 12 renames a second; the CLI does none of it, so a gain here cannot be measured with
  `planodia_cli`, and a change that checkpoints less changes the resume behaviour (PLA-001, PLA-004, PLA-014).
* **Thread counts.** Planodia uses one thread; there is no `ncpus` to set, and the suite runs one process at a time. A
  threaded scan could keep the results identical (each period is independent), but it changes what a task asks of the
  volunteer's machine, which the server's plan class decides, and it adds the suspend problem of PAT-025.
* **Efficiency cores.** On Apple Silicon `taskpolicy -b` (background QoS) puts a process on the efficiency cores, `nice`
  does not, and the BOINC client's way of lowering priority decides which one a volunteer's Mac gets. Compare
  `RUN_PREFIX="taskpolicy -b"` and `RUN_PREFIX="nice -n 19"` before trusting a gain.
* **OpenCL fixed costs.** Every process loads the OpenCL library, finds the device and compiles the kernel before the
  first light curve; a task pays that once. In the short profiles it is a larger share than in a production task
  (expected, not measured).
* **Apple OpenCL is opt-in** in `tools/build_mac.sh` because it is about as fast as one CPU core. The OpenCL labels
  therefore measure the kernel on a GPU that is slow for it; a kernel gain on an M1 need not carry over to NVIDIA, AMD,
  Intel or Adreno (local memory, wave size and atomics differ) and the reverse. Measured on 2026-11-04: on an M4 Pro
  (16-core GPU) the Apple OpenCL scan took 1.0-2.1 s per profile against 6.7-33 s on one CPU core, 5.5-9x faster than
  even the faster CPU build of Results. "About as fast as one CPU core" holds for the M1, not for newer Macs: whether
  the Apple OpenCL app should stay opt-in is worth asking again (Results, plan).
* **Other programs on the GPU.** The display and any BOINC GPU task share the Apple GPU. Pause them for the OpenCL runs.

## One command on a Mac

```
scripts/bench/mac_ab.sh              # 3 rounds of the profiles quick, dense and deep, CPU and Apple OpenCL
scripts/bench/mac_ab.sh 5 quick      # 5 rounds of one profile
scripts/bench/mac_ab.sh 3 prod       # the long profile (about 4 minutes per run, expected)
NEW_OPT="-O3" scripts/bench/mac_ab.sh 3 quick       # the working tree with other compiler flags
NEW_PATCHES="$PWD/my.patch" scripts/bench/mac_ab.sh # the working tree plus a patch
OPENCL=0 scripts/bench/mac_ab.sh     # CPU only
scripts/bench/linux_ab.sh            # GCC (CLANG=1: also clang) on Linux, in a Debian 12 container on a Mac
scripts/bench/cross_check.sh         # Windows (MinGW-w64, Wine) and Linux x86_64 builds, emulated: compile, run, which lines
```

On an M4 Pro the default `mac_ab.sh` took about 6 minutes (2026-11-04).

It needs the Xcode command line tools, git and python3 (the shards are generated once, with the standard library only).
It builds, in `~/planodia-work`, the baseline `upstream/master` (or `origin/master`; `BASE_REF=` picks another ref) with
the documented line and this working tree with `NEW_OPT` and `NEW_PATCHES` (default: the same line, no patch), then runs
each profile with the CPU pair and with the OpenCL pair. With neither variable set and a clean branch the two sides are
the same code: every "new" row must read `no clear gain; same output`, which is also the measure of the machine's
noise, so run it once before trusting any other comparison. About 25 minutes for 3 rounds of the default profiles
(expected, from the documented timings; not measured). Each profile ends with a table like this one
(`~/planodia-work/bench-runs/<date>-<profile>/summary.txt`; the values are placeholders):

```
label        runs  median s    min s    max s    cpu s  RSS MB  GPU%   vs 1st  verdict / output
cpu-base        3     <s>      <s>      <s>      <s>     <MB>     -   1.000x  baseline; same output
cpu-new         3     <s>      <s>      <s>      <s>     <MB>     -   <x>     <verdict>; <output>
```

`verdict` applies the rule of this file (Method, rule 5). `same output` means every run of that label wrote the same
result lines as the baseline's, `same output as X` the same as an earlier label; otherwise the column shows the change of
the sum of the SDE column over all light curves and the largest number of result lines that differ from the baseline's
first run. `GPU%` is a rough sample of the whole GPU (`ioreg`, twice a second); do not base decisions on it.

On Linux (CPU; OpenCL only with `BACKENDS="cpu opencl"`, which needs `CL/cl.h` and an OpenCL driver) the same pieces are
used directly, and `/usr/bin/time` must be GNU time (`apt install time`):

```
REF=upstream/master scripts/bench/build_variant.sh base
scripts/bench/build_variant.sh new                 # LDFLAGS=-static matches tools/build_cpu.sh
scripts/bench/run_bench.sh quick 3 base=$HOME/planodia-work/bins/base/cpu new=$HOME/planodia-work/bins/new/cpu
```

## The pieces

| file | what it does |
|---|---|
| `build_variant.sh <name> [patch ...]` | copies the tracked files (or `REF=<git ref>`), applies patches, builds `bins/<name>/cpu` and, on macOS, `bins/<name>/opencl` (`BACKENDS=` changes that); `OPT`, `CXX`, `PLAT`, `VENDOR`, `LDFLAGS` set flags and compilers; writes `build-info.txt`; stops with the compiler's errors if a binary is missing. There is no CUDA backend: NVIDIA GPUs run the OpenCL app, so nothing needs `nvcc` |
| `run_bench.sh <profile> <rounds> label=binary ...` | generates the profile's shards once, runs the binaries interleaved in fresh directories, records wall, user and system time, peak memory, a hash of the result lines, the sum of the SDE column and the number of lines that differ from the first run; a binary named `opencl` gets `--gpu`. `RUN_PREFIX` runs every binary through a command (`taskpolicy -b`, `nice -n 19`) |
| `summarize_bench.py <results.tsv>` | the table above |
| `make_shards.py <out> <light curves> <cadence s> <seed>` | writes synthetic `SKT1` shards, byte-identical on every platform (integer random numbers, no libm calls); see its header for the model |
| `profiles/*.wu` | `shard` lines (light curves, cadence in seconds, seed) and `in` lines (the text of the work unit's `in` file; `run_bench.sh` turns it into the `PL_*` variables `planodia_cli` reads) |
| `mac_ab.sh` | everything above in one command |

What is compared is the standard output of `planodia_cli`: one line per light curve, with no time, host or other
variable content, so the whole output is hashed. `run_bench.sh` keeps `stdout.txt`, `stderr.txt` (with the `--time` line)
and `time.txt` of every run in its directory.

| profile | work unit | expected time on an M1 | why |
|---|---|---|---|
| `quick` | format 1, default `Params`, 60 light curves of about 3,700 points (10-minute cadence), 1 shard | about 30 s | the usual case: the fold loop is nearly all of it |
| `dense` | format 1, 40 light curves of about 11,000 points (200-second cadence) | about 1 minute | three times the points, a running median window of 433 points: weighs `prepare()` and everything per point |
| `deep` | format 2, three passes, binw 0.01, six durations, 16 light curves of about 3,700 points | 40 to 60 s | work per pass: `finish()`, `mask_transits()`, the box scan with more bins and durations, the OpenCL kernel at 1,400 bins |
| `prod` | format 1, 2 shards of 250 light curves (the shard size of `tools/ingest.py`) | about 4 minutes | production-like length for the final number of a change |

The expected times come from the one documented measurement on an M1 core: 0.5 s per light curve in format 1 and 1.3 s
with three passes (PLA-005, 2026-10-27, synthetic 27-day light curves). `deep`'s parameters are an assumption inside the
app's range checks: the server's real Deep parameters, the `Params` values it sends and the number of shards per work
unit are not in this repository (open questions at the end).

## Where the time is expected to go (expected, not measured)

For one pass over a light curve of n points, with the default `Params`:

* The period grid is uniform in frequency with `df = freq_factor * durs[0] / T^2`: about 23,500 periods for T = 27 days
  (the ledger's PAT-016 row has about 23,400), about 32,000 for `deep`.
* **The fold loop of `scan_cpu()`** does for every period and every point a division `t / per`, a `floor`, a multiply, a
  conversion and two read-modify-write additions (`cnt[b] += 1.0; sum[b] += y[i]`). That is 23,500 x 3,700 = 87 million
  point-folds per light curve at 10 minutes, 260 million at 200 s. With the documented 0.5 s per light curve, if the
  loop were all of the time, a point-fold would cost about 6 ns, which is more than a division's throughput; the likely
  reason is a dependency chain through memory: consecutive points are `dt` apart, a bin is `binw` wide, so a point
  lands in the same bin as its predecessor in `1 - dt/binw` = 65 % of the cases at 10 minutes (88 % at 200 s), and the
  addition then waits for the previous store to the same bin.
* **The box scan and prefix sums**: per period `ndur * nb` boxes with `nb = ceil(P / binw)`, about 86 bins on average
  over the grid: 23,500 x 4 x 86 = 8 million boxes per light curve, each with a `sqrt` and two divisions when it
  passes the early tests. For `deep` 6 durations and 173 bins: about 33 million boxes per pass.
* **`prepare()`**: the running median does an `assign` and an `nth_element` per point over 145 points (10 minutes) or
  433 (200 s). **`finish()`**: `floor_subtract()` moves a sorted window of 301 values per period, `partial_sort` selects
  4,000 of 23,500, then five peaks take one pass over the points each; it runs once per pass.

| share of one CPU run | `quick` | `dense` | `deep` |
|---|---|---|---|
| fold loop | 80-90 % | 85-90 % | 65-75 % |
| box scan and prefix sums | 8-15 % | 5-8 % | 20-30 % |
| `prepare()` | 1-2 % | 2-4 % | under 1 % |
| `finish()` and `mask_transits()` | under 1 % | under 1 % | 1-2 % |

**OpenCL** (Apple, expected shares unknown; the documented figure is 0.23 s per light curve on an M1, `tools/build_mac.sh`):

* In the kernel one work group (64 work items) takes one period. Points go to the local histogram with two atomics each;
  the 32 neighbours of a SIMD group span about 11 bins, so about three lanes meet in one bin. The prefix sum is one
  lane's serial loop over `nb` bins, the other 63 wait. For 63 % of the periods `nb` is at most 64, so the box loop
  gives most lanes less than one bin to look at. The final reduction over 64 results is again one lane.
* On the host, a light curve is `prepare()`, up to six launches of 4,096 periods (each a blocking write, `clFinish` and
  three blocking reads), and `finish()`, one after the other. On a fast GPU, where the README gives 4 ms per light curve
  in total, `prepare()` and `finish()` on the CPU are likely a large part of it; on the M1 the kernel probably
  dominates. `--time` shows the split; measure before choosing.

## Candidate speed-ups, in the order to measure them

Ranked by expected gain against risk. "Same" means the output must stay byte-identical (check it); "changes" needs the
maintainers' decision (Method, two kinds of change). All gains are guesses until measured.

| # | change | results | why it should pay, and what to check |
|---|---|---|---|
| 1 | In the fold loop take 2 to 4 trial periods per pass over the points, each with its own `cnt` and `sum` arrays, each still added in point order | same | The additions of different periods are independent, so the memory dependency chain above overlaps; the loads of `t[i]` and `y[i]` are shared. Up to 1.5-2x on the fold if it is latency-bound, 1.3-1.8x overall. Keep the division `t / per` and each bin's order of additions. Measure on `quick` and `dense` (the chain is worse there: 88 % same-bin) |
| 2 | Smaller fold changes: integer counters (counts are exact either way), a truncating cast instead of `std::floor` (times are not negative, same value), bin indices computed in a separate vectorisable pass before the scatter | same | The `floor` is one instruction on arm64, but in an x86_64 `-O2` build without SSE4.1 (what `tools/build_cpu.sh` ships) it is likely a libm call per point (check the assembly): expect little on an M1 and more on Linux x86_64, so measure there too |
| 3 | Compiler flags that leave floating point alone: `-O3`, `-fno-math-errno`, `-mcpu=apple-m1` (arm64 Macs only) through `NEW_OPT` | same if the bytes are equal (check, including `-ffp-contract=off`) | The cheapest to try: one command per flag set. Kampylos measured 0-8 % from flags like these, on another workload and the same compiler. `-march=native` or an x86-64 level above the baseline raises the minimum CPU of the volunteers (a plan-class question) and enables FMA: see #9 |
| 4 | `prepare()`: the running median from a sorted sliding window, as `floor_subtract()` already does, instead of `assign` and `nth_element` per point | same (a median is a selection: no arithmetic; take element `size / 2` of the sorted window as `median_of()` does, also at the clipped edges) | 1-4 % on the CPU profiles, but on a fast GPU the host's `prepare()` is a large part of a 4 ms light curve. Measure on `dense` and with `--time` on the OpenCL label |
| 5 | The box scan: skip the `sqrt` and the divisions for boxes that cannot beat `best`, with a bound that has a safe margin | same only if the bound cannot change which box wins, ties included | 5-25 % of the CPU time, most on `deep`. Test with the output hash on all four profiles |
| 6 | OpenCL kernel and host: a parallel prefix sum, spreading the box loop over (duration, bin) pairs, fewer blocking calls and the next light curve's `prepare()` overlapped with the current scan | same (the sums are integers, the winner rule "lowest duration, then lowest start bin" does not depend on how lanes share the work; check the compiler's float contraction) | The serial prefix and the single-lane reduction leave most lanes idle (see above). Compare the OpenCL output with the old OpenCL build on the same device, never with the CPU's (PLA-009). Measure on the OpenCL labels of `quick`, `dense`, `deep` |
| 7 | `finish()`: cheaper `floor_subtract()`; leave `partial_sort` as it is | same, as long as the tie order stays (PLA-011) | Under 1 % on the CPU, noticeable on a fast GPU. Replacing the sort changes which tied peak is reported: that is a different change (#9) |
| 8 | Format 2: stop the later passes when a pass finds nothing (for example its SDE is below 5, the limit for adding a peak) | changes (rarely: a later pass could still reach 5) | Most light curves have no planet, so up to 2-3x on `deep`, where two thirds of the light curves have none. Needs the number of differing lines on real light curves and the maintainers' agreement |
| 9 | Rounding-level changes: a reciprocal of the period, adding the points of a run of equal bins in a register first, comparing squares in the box scan, float32 in the fold, FMA through `-march=` or `-ffp-contract=fast`, `-ffast-math`, a tie-break in the peak sort | changes (last digits; can move a point across a bin edge or reorder tied peaks) | Perhaps 10-40 % of the fold, but the results then differ from hosts that run the old build. Wait for the answer to PLA-010, and do the tie-break (PLA-011) first and separately |
| 10 | A coarser period grid (`freq_factor`), fewer box durations, a narrower period range | changes (science) | The cost falls in proportion, the detection rate moves (README: 28 of 40 known planets in the top three peaks, 3 of 200 random stars above SDE 9). Never silently (AGENTS.md, "Conventions") |
| 11 | Threads over periods or over light curves | same is possible | BOINC runs one task per core, so a gain needs idle cores; the CPU budget is a plan-class setting on the server; adds PAT-025. Not worth measuring before the server side decides |

## What the suite does not cover

* **The BOINC apps.** Input parsing, the checkpoint and resume logic, `trim_output()`, the flush after each light curve,
  the checkpoint every 20 light curves, progress reports, device choice. No stub exists, and AGENTS.md says no harness
  exists for them either. Anything that is only in `main_planodia_boinc.cpp` is not measured.
* **Real light curves.** The repository holds none and `tools/ingest.py` needs the network. The synthetic ones fix the run
  time (it depends on the point count, the period grid and the bin counts) but their peaks and ties are not those of
  TESS data. Before claiming "same results" for a change, also compare outputs on real shards (a machine with the data
  can run `planodia_cli` on them with both builds and `cmp` the outputs, AGENTS.md "Tests").
* **The server's real parameters.** `Params` defaults are used for format 1; the format-2 setting of `deep` is an
  assumption; the number of shards of a work unit is unknown.
* **GPUs other than the Apple one.** The OpenCL build runs on any OpenCL 1.2 GPU in principle; here only an Apple GPU is
  planned. NVIDIA, AMD, Intel and Adreno are unmeasured. There is no CUDA backend.
* **Platforms.** Linux x86_64 and aarch64 builds work through the same scripts (the CPU build), but only a Mac is planned.
  Windows (MinGW-w64, llvm-mingw), Android, i686, armhf, ppc64le and riscv64 are not built; the shipped Linux builds
  are static (`LDFLAGS=-static`) and the Mac builds target macOS 11.
* **Load from several tasks at once.** A volunteer's host often runs one task per core; shared caches and memory
  bandwidth can make a gain smaller or larger there. The suite runs one process at a time.
* **Power, heat, and low-priority scheduling** beyond what `RUN_PREFIX` can imitate.

## Open questions only the maintainers can answer

* What do the validator and the assimilator compare, and can CPU and GPU results meet in one quorum (PLA-010)? The
  answer decides how much of the "changes" column can ever ship.
* Which `Params` and Deep settings does the server send, and how many shards does a work unit carry? Edit the profiles
  when they are known.
* Is there a standing set of real shards that benchmarks may use (kept outside the repository)?

## Results

### Candidate 1: the fold takes 4 trial periods per pass (2026-11-04)

Machine: Apple M4 Pro (8 performance + 4 efficiency cores, 16-core GPU, 24 GB), macOS 27.0, Apple clang 21.0.0
(Command Line Tools), load 2.2-3.5 (the benchmark itself 1, the window server and BOINC Manager about 0.7), BOINC client's
tasks suspended. Baseline: `upstream/master` (4a9df85), `-O2`. Candidate: this tree, `src/planodia_core.h`: `scan_cpu()`
folds `SCAN_FOLD_PERIODS` = 4 trial periods per pass over the points, each bin still filled in point order; the box
search moved unchanged into `scan_folded()`. 3 interleaved rounds, medians.

| profile | build | macOS CPU | Apple OpenCL (M4 GPU) | Linux arm64 GCC 12 | Linux arm64 clang 14 | output |
|---|---|---|---|---|---|---|
| `quick` | base | 11.60 s | 1.04 s | 12.03 s | 12.57 s | |
| | new | 6.68 s (**1.74x**) | 0.96 s (A/A: no clear gain) | 7.63 s (**1.58x**) | 6.56 s (**1.92x**) | same |
| `dense` | base | 33.34 s | 2.10 s | 33.31 s | 33.24 s | |
| | new | 11.56 s (**2.88x**) | 2.11 s | 13.54 s (**2.46x**) | 11.20 s (**2.97x**) | same |
| `deep` | base | 10.51 s | 1.13 s | 10.94 s | 11.53 s | |
| | new | 9.90 s (1.06x) | 1.12 s | 10.87 s (1.01x, no clear gain) | 9.35 s (1.23x) | same |

The OpenCL path does not use `scan_cpu()`, so its two labels are an A/A check of the machine: no clear gain, as it
should be. The CPU result lines are the same on every platform tried (hash 0a5d045733d2 on `quick`): macOS clang 21,
Linux arm64 GCC 12 and clang 14, and, under emulation (`cross_check.sh`, 1 round each), Linux x86_64 GCC 12 (16.8 s ->
14.1 s) and Windows x86_64 MinGW-w64 GCC 16.2 under Wine (15.6 s -> 12.2 s). The emulated times only show that the
builds run.

**Where the time goes** (`sample` of `deep` with the new build): the fold 49 %, the box scan (`scan_folded()`) 48 %,
`prepare()` and `finish()` under 2 %. The estimate got the order of the fold and box shares right for `deep` and the
dependency chain right for the fold: on `dense` (200 s cadence, 88 % of consecutive points in the same bin) the fold
gained 2.9x, on `quick` and `deep` (600 s) less.

**Tried and dropped: a vectorisable box scan** (each duration's SNR of every start bin computed in branch-free loops,
then the winner picked in the old order; bit-identical results). With Apple clang 21 it gained 1.24x more on `deep`
and 1.14x on `quick`, with Debian clang 14 1.2x on `deep`, but with GCC 12 at `-O2`, which does not vectorise it, it made
`deep` 8.9 % slower than the baseline (it computes the divisions of boxes the old loop skipped). The Linux and Windows
CPU apps are built with GCC, so it is not in this change. It is worth having for the clang builds (macOS, Android,
llvm-mingw) behind `#if defined(__clang__)`, or for all with GCC `-O3`, measured first.

**Compilers.** On Linux arm64 clang 14 ran this tree 1.16-1.21x faster than GCC 12 with the same result lines. The Linux
CPU apps are built with GCC (`tools/build_cpu.sh`): measure clang natively on x86_64 before choosing.

### Next steps by platform (plan of 2026-11-04)

| platform | app | next step | expected | evidence |
|---|---|---|---|---|
| every CPU build | CPU | release candidate 1 | 1.06-2.9x by profile, same lines | above |
| clang builds (macOS, Android, Windows ARM64) | CPU | the vectorisable box scan behind `__clang__` | 1.14-1.24x more | above |
| Linux, Windows | CPU | measure clang against GCC natively (x86_64), and GCC `-O3` | up to 1.2x on arm64 | above |
| every CPU build | CPU | candidate 2 (integer counters, truncating cast; x86_64 builds without SSE4.1 call `floor`), then candidate 4 (`prepare()`) | small on arm64; x86_64 unmeasured | the candidates list |
| OpenCL | GPU | overlap `prepare()`, the launches and `finish()` (PLA-023), then the kernel's serial prefix and reduction (candidate 6) | unknown on fast GPUs | `--time` on a fast GPU first |
| macOS | OpenCL | reconsider the opt-in of the Apple OpenCL app: on an M4 Pro it was 5.5-9x faster than the faster CPU build | a decision (plan classes) | above, "Traps" |
| all | all | candidate 8 (stop later passes that find nothing), candidates 9-10, decide (PLA-010) | up to 2-3x on `deep`; results change | not measured |

One block per later change or question, newest first, in this form:

```
### <change or question> (<date>)

Machine: <chip, cores, RAM>, <OS version>, <compiler and version from build-info.txt>, load <uptime line>.
Baseline: <ref and flags>. Candidate: <working tree or patch, flags>.
Profiles and rounds: <list>. RUN_PREFIX: <none or the prefix>.

| profile | label | runs | median s | min-max s | cpu s | vs 1st | verdict | output |
|---|---|---|---|---|---|---|---|---|

Where the time went (the --time line and any sample): <...>
Output: <same on every run, or how many lines differ and by how much>.
Not measured: <platforms, GPUs, real light curves>.
```
