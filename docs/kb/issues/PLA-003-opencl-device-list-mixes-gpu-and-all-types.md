---
id: PLA-003
title: OpenCL devices are counted as CL_DEVICE_TYPE_GPU but fetched as CL_DEVICE_TYPE_ALL, and the ALL fallback can pick a CPU device
type: bug
status: confirmed
severity: medium
platforms: [opencl]
area: device-selection
next: In gpu_init(), use CL_DEVICE_TYPE_GPU for both the count and the list, drop the CL_DEVICE_TYPE_ALL fallback, log vendor, type and name
pattern: PAT-012
aliases: [PL3]
reported: 2026-10-21
source: audit 2026-10-21 (plan section 1 and 6.1, PL3)
where: src/planodia_opencl.cpp:123-140 @ 3f6c1b7
fixed_in:
---

## Symptom

Not reported yet. Expected: on a platform that lists a CPU device before its GPU(s), the GPU task runs on the CPU
OpenCL device (slow, and it competes with the CPU tasks); the stderr line
`planodia_opencl: using OpenCL device "<name>"` then names a processor.

## Cause

`pl::gpu_init()` at 3f6c1b7:

* (b) line 126 counts the platform's devices with `CL_DEVICE_TYPE_GPU` (`ndev`), but line 137 fetches the first
  `ndev` devices with `CL_DEVICE_TYPE_ALL`. When the platform lists a CPU device first, `devs[0]` is the CPU and a
  GPU can be cut off the list. `cl_pick_vendor_device()` (`cl_dynload.c:233-249`) corrects this only for
  `qualcomm` builds (Adreno by name).
* (c) when no matching platform has a GPU, lines 128-134 fall back to `CL_DEVICE_TYPE_ALL` and accept any device,
  for example the Intel CPU runtime, whose vendor string "Intel(R) Corporation" passes
  `cl_platform_matches_vendor(p, "intel")` (`cl_dynload.c:219-224`). A GPU task then runs on the CPU without a
  message saying so.
* (a) the platform is the first one that matches the build's vendor (`EXPECTED_GPU_VENDOR`, set per build), not
  `aid.gpu_type` from `init_data`. That is equivalent while the plan classes send each vendor build only to that
  vendor's GPUs (server side, not checked here). Matching by PCI vendor id can also accept a second platform for the
  same GPU (for example Mesa rusticl next to the vendor driver); BOINC's index refers to one platform only.

BOINC counts the `CL_DEVICE_TYPE_GPU` devices of the matching platform
([boinc-api.md](../shared/boinc-api.md#gpu-assignment-init_dataxml)), so even with PLA-002 fixed the index would be
applied to a different list in case (b).

Verified by reading `src/planodia_opencl.cpp:115-140` and `src/cl_dynload.c:179-249` at 3f6c1b7. On an Apple M1
(2026-10-27) the Apple platform lists only the GPU, so the two lists agree there. Which platforms list a CPU device
first was not checked on hardware here.

## Fix

Proposed (plan PL3): use `CL_DEVICE_TYPE_GPU` for both the count and the list; remove the `CL_DEVICE_TYPE_ALL`
fallback, or keep it only for a documented driver that reports its GPU as `DEFAULT` and then check
`CL_DEVICE_TYPE` of the chosen device; fail with a clear message (or a temporary exit, PLA-006) instead of running
on a CPU. Log platform vendor, device type and name. kampylos d7f9ef7 (`platform_vendor_matches()`) is the
reference for matching the platform by `gpu_type`.

## Test

Needs a platform that lists a CPU device next to a GPU (POCL in one ICD with a GPU driver, or an old AMD APP SDK),
or a stub OpenCL loader that returns such lists. Manual: the stderr line must name a GPU.

## Log

- 2026-10-21: found in the audit at 4edb8b0 (plan PL3, marked verified).
- 2026-10-27: recorded from the 2026-10-21 audit, verified at 3f6c1b7 by reading src/planodia_opencl.cpp:123-140
  and src/cl_dynload.c:179-249; the platform-vendor point (a) added.
