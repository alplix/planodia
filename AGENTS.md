# Working on Planodia (notes for AI agents and new contributors)

Planodia is the planet-transit search of the bitboinc BOINC project: a box-least-squares (BLS) search over TESS
light curves. One source tree gives the CPU application and, with `-DPL_OPENCL`, the OpenCL GPU application; both
must write the same result format. [README.md](README.md) explains the science and the measured detection rates.
Written 2026-10-27 against commit 3f6c1b7. File:line references below refer to that commit.

## What one work unit is

Input files: `in` (text) and `shard0` .. `shard<nshards-1>` (binary light-curve shards, format `SKT1` in
`src/skt.h`, a few hundred light curves each). `in` has `planodia 1` or `planodia 2` on line 1,
`<pmin> <pmax> <freq_factor> <flatten_days> <nshards>` on line 2, and for format 2 ("Planodia Deep") a third line
`<binw> <ndur> <dur_1..dur_ndur> <max_periods> <passes> <floor_win>`; the allowed ranges are checked in
`src/main_planodia_boinc.cpp:105-118`. For every light curve the app flattens the flux with a running median, builds
a period grid uniform in frequency, folds and bins the points for each trial period, keeps the best box (duration,
start bin) per period, subtracts a running-median noise floor from the power spectrum, computes the SDE and reports
up to five separated peaks (`src/planodia_core.h`). With `passes` > 1 the strongest peak's transits are masked and
the scan is repeated. Output: `out`, a header line `# planodia 1 <nlc> light curves` and then exactly one line per
input light curve, in shard order. A light curve takes about 0.08 s on one CPU core and 4 ms on an RTX 5070 Ti
(README, sector 26); format 2 with three passes about 0.1-0.2 s per light curve on that GPU.

## Layout

| Path | What |
|---|---|
| `src/main_planodia_boinc.cpp` | the BOINC application (CPU, and GPU with `-DPL_OPENCL`): input parsing, checkpoint, `trim_output()`, main loop |
| `src/planodia_core.h` | the search: `Params` (defaults), `prepare()`, `make_periods()`, `scan_cpu()` (double), `finish()` (noise floor, SDE, peaks), `search_passes()`, `format_result()`, `format_skipped()` |
| `src/planodia_opencl.cpp`, `.h` | the OpenCL backend: kernel source (`kSource`), `gpu_init()` (platform and device choice), `gpu_scan()` (same contract as `scan_cpu`, in float) |
| `src/cl_dynload.c`, `.h` | run-time OpenCL loader (dlopen / LoadLibrary, Android linker namespaces) and the vendor filters `cl_platform_matches_vendor()`, `cl_pick_vendor_device()`; a copy shared with other bitboinc apps (`docs/kb/kb.conf`). Its comments name files of other apps (`opencl_search.c`). |
| `src/skt.h` | `ShardReader`: the only reader of the `SKT1` shard format |
| `src/planodia_cli.cpp` | standalone search over shard files without BOINC (`--time`, `--gpu`), parameter overrides through `PL_*` environment variables |
| `tools/ingest.py` | downloads TESS QLP light curves from MAST and writes the shards (the only `SKT1` writer); resumes per shard, drops light curves whose download failed (PLA-020) |
| `tools/build_cpu.sh` | CPU apps for Linux (x86_64, i686, aarch64, armhf, ppc64le, riscv64) and Windows (x86_64, x86, arm64) |
| `tools/build_gpu_server.sh` | OpenCL apps: Linux x86_64 and Windows x86_64 per vendor (nvidia, amd, intel), Windows arm64 (qualcomm) |
| `tools/build_mac.sh` | macOS arm64 and x86_64, CPU app and (with `OPENCL=1`) the Apple OpenCL app |
| `tools/build_android.sh` | Android aarch64, CPU app and the Adreno OpenCL app |
| `docs/kb/` | knowledge base (below) |

No vendored third-party code. The BOINC library and the OpenCL headers come from the build machines.

## Invariants (break one and results become silently wrong)

- **Shard format `SKT1`**: one writer (`tools/ingest.py` `convert()`, `write_shard()`) and one reader (`src/skt.h`).
  Header `SKT1`, u32 version 1, u32 n_lc, u32 sector; record u64 tic, f32 tmag, ra, dec, t0, dt, u32 n, n u16
  cadence offsets, n f16 flux, 2 bytes of padding when n is odd. Little endian. `ingest.py:104` packs a wrong order
  and is overwritten by line 106; only line 106 counts. `t0` is f32 (PLA-017).
- **Result line**: `format_result()` (`planodia_core.h:317-327`): `tic tmag npts sde npeaks` and per peak
  `period dur depth_ppm snr tc ntr` with fixed printf formats; an unusable light curve gives `tic 0 0 0 0`. The
  server's validator and assimilator parse these lines (not in this repository). The header says result format 1
  for both input formats; it is not the app version. Changing a field or a format is a server change too.
- **One line per light curve, in shard order.** `idx != total` at the end is an error (`main:201`). The resume
  logic counts lines.
- **`out` is binary** (`"wb"`/`"ab"`): the same bytes on every OS, `\n` line ends. `trim_output()` relies on that
  and on the header prefix `# planodia `.
- **`scan_cpu()` and the kernel compute the same thing.** Both keep the best box per period with the same tie rule
  (lowest duration index, then lowest start bin: `planodia_core.h:146-169`, `planodia_opencl.cpp:76-87`). A change
  in one needs the same change in the other. They are not bit-identical (float and 16.16 fixed-point sums on the
  GPU, PLA-009); expect last-digit differences between CPU and GPU results.
- **Determinism per light curve**: no random numbers, no threads; a line depends only on the light curve and
  `Params`. That is what makes a resume reproduce the same lines with the same binary. Across platforms the peak
  order can still differ where powers tie (`std::partial_sort`, PLA-011), and FMA contraction is allowed by every
  build (no `-ffp-contract` anywhere; PLA-010).
- **GPU local memory**: the kernel needs `8 * maxbins + 1280` bytes, `maxbins = ceil(pmax / binw)` rounded up to
  64. A work unit above the device's limit fails on that device (PLA-016).

## Checkpoint/resume contract

What the code does at 3f6c1b7:

- Files: `planodia_checkpoint` (one decimal number: finished light curves; written to `planodia_checkpoint.tmp`
  and moved with `boinc_rename()`, `main:45-54`) and `out`.
- Authority: the checkpoint gives the count; `out` is cut back to the header plus that many complete lines
  (`trim_output()`, `main:57-82`). If `out` has no header or fewer complete lines, the task starts again from 0 with
  a new `out`. A torn last line is dropped. This design is sound (PAT-002 not affected).
- Order: every line is written and `fflush`ed before a checkpoint can count it (`main:190-195`).
- Cadence: every 20 light curves and whenever `boinc_time_to_checkpoint()` returns 1 (`main:194-197`), plus a final
  write before `boinc_finish(0)` (`main:202`). The 20-light-curve writes ignore the volunteer's interval and are
  outside a critical section (PLA-014).
- Critical sections: only through `boinc_time_to_checkpoint()`. It is called twice in one opportunity, which leaves
  the critical section open for the rest of the task: suspend, quit and abort stop working (PLA-001, high).
- Known gaps:
  - `trim_output()` rewrites `out` in place, and no write or rename result is checked (PLA-004).
  - A failed open of the checkpoint or of `out` is treated as a fresh start and truncates `out` (PLA-013).
  - Write errors on `out` are not checked before `boinc_finish(0)` (PLA-012).
  - Progress is not reported at resume (PLA-005).
- A resume must reproduce exactly the lines that were cut away. It does with the same binary on the same device.
  A GPU task that restarts on another GPU of the same host mixes lines from two devices (rounding level, PLA-009).

## Building

Each script names its machine; the BOINC `api/` and `lib/` headers and static libraries for the target must exist
there. `V` (default 1) is the version in the file names; `PLANODIA_VERSION` in `main_planodia_boinc.cpp` ("1",
"1-opencl") is printed to stderr.

```
# Linux and Windows CPU apps (build server; run from a directory that contains src/; BOINC in /opt/bitboinc-src/boinc-<target>)
V=1 bash tools/build_cpu.sh
#   per target: <cxx> -O2 -std=c++17 -I src -I $B/api -I $B/lib src/main_planodia_boinc.cpp -static \
#               $B/api/libboinc_api.a $B/lib/libboinc.a <-lpthread | -lpsapi> -o out/planodia_v1_<os>_<arch>[.exe]
#   i686 and win32 add -msse2 -mfpmath=sse; winarm64 uses llvm-mingw from /opt/llvm-mingw

# OpenCL apps (build server; uses sudo docker with ubuntu:20.04 for Linux, host mingw-w64 and llvm-mingw for Windows)
V=1 bash tools/build_gpu_server.sh
#   per vendor: -DPL_OPENCL -DCL_TARGET_OPENCL_VERSION=120 -DEXPECTED_GPU_VENDOR="nvidia|amd|intel|qualcomm"
#   cl_dynload.c as C (gcc -std=c99 / -std=c11); Linux links -lpthread -ldl -lrt -static-libstdc++ -static-libgcc
#   and prints the newest GLIBC symbol version the binary needs

# macOS arm64 and x86_64 (build Mac; two BOINC trees under $HOME, paths in the script), -mmacosx-version-min=11.0
bash tools/build_mac.sh            # CPU apps
OPENCL=1 bash tools/build_mac.sh   # also planodia_opencl_apple_* (Apple OpenCL is opt-in, see the script comment)

# Android aarch64 (WSL Ubuntu as root, NDK r27c, API 24, libc++ static, PIE; see the usage line in the script)
OUT=<dir> bash tools/build_android.sh
#   check() fails the build if a binary NEEDs anything but libc, libm, libdl, liblog
```

OpenCL is never linked: `cl_dynload.c` loads it at run time, so only the headers are needed to build. The build
scripts and `tools/ingest.py` use fixed paths of the build machines (PLA-008), and no script records the image,
package, compiler or BOINC versions it used (PLA-021).

The standalone tool has no build script. It needs only a C++17 compiler; checked on 2026-10-27 on macOS arm64:

```
clang++ -O2 -std=c++17 -I src src/planodia_cli.cpp -o planodia_cli
planodia_cli --time shard_00000.skt > results.txt
```

## Tests

There are none: no test directory, no stub BOINC library, no reference outputs. How to check a change today:

- Search code (`planodia_core.h`, `planodia_opencl.cpp`): build `planodia_cli` before and after the change, run
  both on the same shards, `cmp` the outputs. A change that is meant to keep results must give identical bytes on
  the CPU. For the kernel, compare `--gpu` with the CPU output by fields with a tolerance (last-digit differences
  are normal, PLA-009) and look at the peak periods.
- `main_planodia_boinc.cpp` (checkpoint, lifecycle, device choice): no harness exists. The knowledge-base patterns
  describe the stub BOINC library to build (PAT-001 "How to test"; kampylos has one in `tests/boinc_stub/`).
- Build scripts: a change to flags can change results (PAT-018); compare CLI outputs of two builds.
- Shards for tests: `tools/ingest.py` needs network access and its fixed data path. A small synthetic `SKT1` file
  can be written in a few lines of Python following `src/skt.h`; keep it outside the repository.

## Environment notes

- `build_cpu.sh` and `build_gpu_server.sh` do not change directory: run them from a scratch directory that contains
  `src/`. They create `out/`, `obj_*`, `inc/` and `inner_ocl.sh` there; `.gitignore` covers only `build/` and
  `stage/`. `build_mac.sh` and `build_android.sh` change to the repository root.
- The Android OpenCL build needs the Khronos OpenCL headers; the NDK has none. The script takes them from a fixed
  directory (PLA-008).
- Android: only aarch64 is built; the vendor library paths in `cl_dynload.c` are 64-bit only (PLA-018). App processes
  cannot see the vendor OpenCL library without the linker-namespace fallback of 3f6c1b7 (PLA-007); test under the
  BOINC client, not from `adb shell`.
- OpenCL device and errors at 3f6c1b7:
  - The device comes only from `--device`; `init_data`'s `gpu_opencl_dev_index` is never read (PLA-002).
  - Devices are counted as `CL_DEVICE_TYPE_GPU` but fetched as `CL_DEVICE_TYPE_ALL`, and the fallback to all device
    types can pick a CPU device (PLA-003).
  - Initialisation and kernel failures end the task with `boinc_finish(1)` instead of a temporary exit (PLA-006;
    question PLA-019).
  - Buffer writes, reads and kernel arguments are not checked, so a failed transfer silently reuses old data
    (PLA-015).
- Apple Silicon OpenCL works (M1 on 2026-10-27: Apple platform with only the GPU device, 32 KB local memory, maximum
  work-group size 256). The kernel is float only; no fp64 is needed anywhere.
- One OpenCL launch covers 4096 trial periods (`kChunk`, `planodia_opencl.cpp:102`): at most 36 ms per launch measured
  on an M1 even for a format-2 setting at the local-memory limit (ledger, PAT-016).
- The CPU builds use libstdc++ (GCC, MinGW-w64 GCC) or libc++ (Apple clang, NDK, llvm-mingw); this matters for ties
  in the peak order (PLA-011).

## Conventions for changes

- Small branches, one concern each. One fix per commit; add the test or the measured comparison that shows the bug
  and the fix. The commit message says what was wrong, how it was reproduced or measured, what the fix changes for
  volunteers (checkpoints, credit, runtime, results), and names the issue (`KB: PLA-NNN`) and the pattern if any
  (`Pattern: PAT-NNN`).
- Keep the backends symmetric: a change to `scan_cpu()` needs the same change in the kernel, and the reverse.
- Do not change a result format, a parameter default or the peak logic silently: such a change alters science
  results and the server's parsing. Record it in the knowledge base with the measured effect.
- Numbers in comments (timings, detection rates) are measurements: date them, and update them when a change moves
  them.
- A fix to `src/cl_dynload.c` or `.h` belongs in the other copies too (`kb.py status ../phygas ../nostos` shows the
  drift).

## Not in this repository

- The server side: work-unit generator (writes `in` and picks the shards), validator (quorum 2), assimilator, plan
  classes (which vendor build goes to which GPU), app version files.
- The light-curve shards (written by `tools/ingest.py` on the data machine) and the TESS target lists.
- The BOINC library sources and builds, the build containers and toolchains, the OpenCL headers for Android.

## Knowledge base

`docs/kb/` holds this repository's known bugs and platform traps, and the bug patterns shared with the other bitboinc
applications. Workflow: [docs/kb/shared/PROCESS.md](docs/kb/shared/PROCESS.md).

- Ready to fix, with next steps: `python3 docs/kb/shared/kb.py list --open --action fix` (also `--area`,
  `--platform`). Then `kb.py show PLA-NNN` for the issue and `kb.py show PAT-NNN` for the pattern it links, with the
  BOINC API facts the pattern cites. In the code, read the functions that `where:` and the Cause name.
- All repositories checked out next to this one: `kb.py todo`, then `kb.py todo --pattern PAT-NNN` for the next step
  and place of each issue; `kb.py check PAT-NNN` checks a pattern everywhere.
- Before debugging a symptom: `grep -ril "<words>" ../*/docs/kb/issues docs/kb/shared/patterns`, then `kb.py show`
  the ids it finds. Read other repositories through the shell (`kb.py show`, `git -C ../<repo> grep -n`, `sed -n`):
  opening one of their files in an editor tool can load their whole AGENTS.md as well.
- New report: `kb.py new issue "symptom in a few words" area=... next=...`.
- After a fix: `kb.py set PLA-NNN status=fix-proposed --log "..."` (`status=fixed fixed_in=<commit>` once merged),
  and update the passages of this file it prints. The commit names the issue (`KB: PLA-NNN`) and the pattern, if any
  (`Pattern: PAT-NNN`).
- Before a pull request: `kb.py index && kb.py lint --strict`.
