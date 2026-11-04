---
id: PLA-008
title: The Android build takes the OpenCL headers from /mnt/c/temp, and every build script uses fixed machine paths
type: bug
status: confirmed
severity: low
platforms: [all]
area: build
next: In tools/build_android.sh replace -idirafter /mnt/c/temp and the /root paths by variables (CL_INC, NDK, BOINC_SRC) with today's values as defaults
pattern: PAT-021
aliases: []
reported: 2026-10-21
source: audit 2026-10-21 (plan section 3.3)
where: tools/build_android.sh:33-35 @ 3f6c1b7
fixed_in:
---

## Symptom

`tools/build_android.sh` works only on the machine it was written on: elsewhere the OpenCL part stops with
`fatal error: 'CL/cl.h' file not found` (expected; not run here), and the toolchain and BOINC paths do not exist.
Nobody else can reproduce a release build from a clean clone.

## Cause

Paths that cannot be overridden, at 3f6c1b7:

* `tools/build_android.sh:33-35`: `-idirafter /mnt/c/temp` supplies `CL/cl.h` for the OpenCL objects (the NDK has no
  OpenCL headers). `/mnt/c/temp` is a Windows directory seen from WSL. If it holds stale headers, a build silently
  uses them.
* `tools/build_android.sh:9, 12-13`: NDK, BOINC headers and the prebuilt BOINC-for-Android libraries under `/root/`
  of a WSL distribution. The usage comment (line 4) points at a Windows user's directories; `OUT` itself can be
  overridden.
* `tools/build_mac.sh:29-30`: the two BOINC trees under `$HOME` of the build Mac.
* `tools/build_cpu.sh:8, 12` and `tools/build_gpu_server.sh:13, 41, 53-54`: `/opt/bitboinc-src/boinc-<target>` and
  `/opt/llvm-mingw` of the build server (the layout of that machine, documented in the script header).
* `tools/ingest.py:23`: data root `/mnt/aidata/tess` for shards, status and log.

The PAT-021 grep finds `build_android.sh:4` and `:33-35`; the others were found by reading the scripts.

## Fix

Proposed: an environment variable with today's value as the default for each location (`CL_INC`, `NDK`,
`BOINC_SRC`, `BOINC_LIBS`, `TESS_ROOT`), and a line in AGENTS.md "Building". For the OpenCL headers, either
vendor the Khronos `CL/` headers into the repository (they are Apache-2.0) or take them from the `opencl-headers`
package as `build_gpu_server.sh:19` does. No effect on the binaries.

## Test

Run each script in a fresh container or on a machine without those paths, with the variables set: it must build.

## Log

- 2026-10-21: listed in the audit at 4edb8b0 (plan section 3.3).
- 2026-10-27: recorded from the 2026-10-21 audit, verified at 3f6c1b7 by reading the scripts and running the
  PAT-021 grep; origin of pattern PAT-021.
- 2026-10-27: re-checked after PAT-021 was corrected (the grep no longer matches URLs): same hits, verdict unchanged.
