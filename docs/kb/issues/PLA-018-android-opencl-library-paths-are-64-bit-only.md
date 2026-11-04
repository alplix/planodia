---
id: PLA-018
title: The Android OpenCL library paths in cl_dynload.c are 64-bit only
type: design-note
status: documented
severity: info
platforms: [android, opencl]
area: device-selection
pattern:
aliases: []
reported: 2026-10-21
source: audit 2026-10-21 (plan section 3.3)
where: src/cl_dynload.c:43-44, 68, 106-107 @ 3f6c1b7
fixed_in:
---

## Symptom

None today: `tools/build_android.sh` builds only aarch64 (`aarch64-linux-android24-clang`, lines 10-11). A 32-bit
Android OpenCL build (armeabi-v7a) would find no OpenCL library on phones where the plain name is not public.

## Cause

Every absolute path that `cl_dynload.c` tries on Android is a 64-bit directory: the plain `dlopen` candidates
(`/vendor/lib64/libOpenCL.so`, `/system/vendor/lib64/...`, `/system/lib64/...`, `/vendor/lib64/egl/libGLES_mali.so`,
`:106-107`), the same list for the namespace fallback of 3f6c1b7 (`:43-44`) and the search path of the private
namespace (`/system/lib64/`, `:68`). A 32-bit process cannot load those libraries; it needs `/vendor/lib`,
`/system/vendor/lib` and `/system/lib`.

Verified by reading `src/cl_dynload.c` and `tools/build_android.sh` at 3f6c1b7.

## Fix

Only if a 32-bit Android build is added: choose the directory with `#if defined(__LP64__)` (`lib64` or `lib`) for all
three lists. The copies in other repositories (`kb.conf` `shared-code`) would need the same change.

## Test

A 32-bit Android build on a phone whose vendor does not list `libOpenCL.so` as public, under the BOINC client: stderr
must name the OpenCL device.

## Log

- 2026-10-21: listed in the audit at 4edb8b0 (plan section 3.3).
- 2026-10-27: verified at 3f6c1b7 by reading cl_dynload.c and build_android.sh.
