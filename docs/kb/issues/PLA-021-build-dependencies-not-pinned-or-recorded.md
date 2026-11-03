---
id: PLA-021
title: The build image, packages and BOINC library versions are not pinned or recorded, so a release cannot be rebuilt as it was
type: bug
status: confirmed
severity: low
platforms: [all]
area: build
next: Print image digest, package versions, compiler --version and the BOINC tree's git commit into each build log; pin ubuntu:20.04 by digest
pattern: PAT-029
aliases: []
reported: 2026-10-27
source: audit 2026-10-27 (pattern PAT-029 check)
where: tools/build_gpu_server.sh:19, 36 @ 3f6c1b7
fixed_in:
---

## Symptom

Nothing visible today. Two builds of the same commit can differ (another compiler release, another BOINC library),
and when a shipped binary misbehaves nobody can tell which BOINC version or compiler it was made with.

## Cause

At 3f6c1b7:

* `tools/build_gpu_server.sh:36` runs the Linux OpenCL build in `ubuntu:20.04` by tag (no digest), and `:19` installs
  `build-essential m4 pkg-config file opencl-headers` without versions. The compiler, the static libstdc++ and libgcc
  (`:32`) and the OpenCL headers from that image end up in the shipped Linux binaries.
* The BOINC library is built from, or linked out of, source trees on the build machines (`/opt/bitboinc-src/...`,
  two trees under the build Mac's home directory, `/root/boincsrc` and prebuilt Android libraries, see PLA-008). No
  script records their commit, and the app does not print it.
* Host toolchains (MinGW-w64, llvm-mingw in `/opt/llvm-mingw`, Xcode's clang) are whatever is installed; only the
  Android NDK is fixed by its path (r27c).
* `tools/ingest.py` imports `numpy` and `astropy` without a requirements file; they shape the shard contents (FITS
  reading, float16 conversion).

Verified by reading the four build scripts and `tools/ingest.py` at 3f6c1b7 and running the PAT-029 grep.

## Fix

Pin the image by digest (or a dated tag) and the packages by version, or at least print the versions each build used
(`docker image inspect`, `dpkg -l`, `<cc> --version`, `git -C <boinc> rev-parse HEAD`) into the build log next to the
binaries. Print the BOINC library version in the app's stderr if the library exposes it. A `requirements.txt` with
versions for `tools/ingest.py`. No effect on today's binaries.

## Test

Build the same commit twice in clean containers a week apart and compare the recorded versions and the result of a
small work unit.

## Log

- 2026-10-27: found while checking pattern PAT-029 at 3f6c1b7, verified by reading the build scripts.
