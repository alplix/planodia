---
id: PLA-007
title: Android: the OpenCL library is not found under the app's linker namespace, so every GPU task fails
type: bug
status: fixed
severity: high
platforms: [android, opencl]
area: device-selection
pattern: PAT-017
aliases: []
reported: 2026-10-12
source: maintainer, Android task failures found 2026-10-12 (per the comment in commit 3f6c1b7)
where: src/cl_dynload.c:29-121 @ 3f6c1b7
fixed_in: 3f6c1b7
---

## Symptom

On Android phones whose vendor does not list `libOpenCL.so` as a public library (many Adreno and Mali phones),
every task of the OpenCL app failed at start. stderr before the fix:

```
cl_dynload_init: no OpenCL ICD found on this host (tried libOpenCL.so[.1], libmali.so)
no usable GPU: no OpenCL library found
```

and the task ended with `boinc_finish(1)` (computation error). Found on 2026-10-12 according to the comment the fix
added (`src/cl_dynload.c:33-37`). The same binary can work from `adb shell`, which runs in another linker
namespace.

## Cause

Since Android 7, an app process loads native libraries through a linker namespace that does not include the vendor
partition. A plain `dlopen("libOpenCL.so")` works only when the vendor lists the library as public; the absolute
`/vendor/lib64/...` paths that were already in the candidate list (`cl_dynload.c:104-108`, since 4cc0df4) are
refused in that namespace too. Pattern PAT-017.

Cause and fix read from the diff of 3f6c1b7 (`git show 3f6c1b7`); not reproduced or tested on a device here.

## Fix

3f6c1b7 (2026-10-12) adds `cl_android_namespace_open()` (`src/cl_dynload.c:38-84`), used when every plain
`dlopen()` failed (`:119-121`): it looks up `android_dlopen_ext` and `android_get_exported_namespace` (or
`__loader_android_get_exported_namespace`) with `dlsym` at run time, so the binary still needs only libc, libm and
libdl (`tools/build_android.sh` `check()`), and tries the library names in the `sphal`, `vndk` and `default`
namespaces; as a last resort it creates its own namespace with `android_create_namespace` that may search
`/system/`, `/data/` and `/vendor/`. The BOINC client does the same (`client/gpu_opencl.cpp`, per the commit's
comment). The failure message now includes `dlerror()`. The exported-namespace calls are not public NDK API; a
future Android version may refuse them.

The copies of `cl_dynload.c` in phygas (0a3eda2) and nostos (eb7029e) have neither this fallback nor the vendor
path list (compared on 2026-10-27); they need it if they ship an Android OpenCL build (`kb.py status`, PAT-017).
Library paths are 64-bit only (PLA-018).

## Test

Manual, on an Adreno or Mali phone under the BOINC client (not from a shell): stderr must show
`cl_dynload_init: opened <library> through the Android <namespace> namespace` (or `a private Android namespace`)
and `planodia_opencl: using OpenCL device "<name>"`. No automated test.

## Log

- 2026-10-12: fixed upstream in 3f6c1b7.
- 2026-10-27: recorded from commit 3f6c1b7 (diff and source comment read at HEAD 3f6c1b7); origin of pattern
  PAT-017. Not tested on a device.
