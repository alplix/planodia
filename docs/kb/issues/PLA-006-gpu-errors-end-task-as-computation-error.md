---
id: PLA-006
title: GPU initialisation and kernel failures end the task with boinc_finish(1) instead of a temporary exit
type: bug
status: confirmed
severity: medium
platforms: [opencl]
area: gpu-runtime
action: decide
next: Decide (PLA-019): may GPU init and launch failures end in boinc_temporary_exit() with a retry cap instead of boinc_finish(1)?
blocked_by: [PLA-019]
pattern: PAT-013
aliases: [PL5]
reported: 2026-10-21
source: audit 2026-10-21 (plan section 6.1, PL5)
where: src/main_planodia_boinc.cpp:130, 187 @ 3f6c1b7
fixed_in:
---

## Symptom

A GPU that is busy, reset by the driver (Windows TDR), missing after a driver update or a wake from sleep, or out
of memory ends the task as a computation error. stderr shows `no usable GPU: <reason>` or
`planodia_opencl: clEnqueueNDRangeKernel failed (<code>)` / `clFinish failed (<code>)` /
`buffer allocation failed`, followed by `GPU scan failed`. The work done since the start is lost for this host and
the host's error count goes up.

## Cause

Every GPU error path calls `boinc_finish(1)`:

* `main:130`: `pl::gpu_init()` failed. Reasons (`planodia_opencl.cpp:115-174`): no OpenCL library, no platform, no
  GPU of the build's vendor, context, queue or program creation failed (class 1, may go away), kernel build failed
  (driver problem), or too little local memory or work-group size for this work unit (class 3 for this device,
  see PLA-016).
* `main:187`: `pl::search_passes()` returned false because `pl::gpu_scan()` failed: buffer allocation, kernel
  launch or `clFinish` (`planodia_opencl.cpp:193, 227, 229`; class 2, often transient). The bin-table check at
  `:179` is class 3 but cannot trigger after a successful `gpu_init()`.

There is no `boinc_temporary_exit()` anywhere in `src/`. A checkpoint exists at most 20 light curves back
(`main:194`), so a retry would lose little.

Verified by reading `src/main_planodia_boinc.cpp:127-132, 187` and `src/planodia_opencl.cpp:115-236` at 3f6c1b7.

## Fix

Proposed (plan PL5), waiting for the maintainers' decision (PLA-019): `boinc_temporary_exit(delay, reason, false)`
for class 1 and 2, with a retry counter in a small file in the slot directory that is reset when a light curve is
finished; after about five temporary exits without progress, `boinc_finish(1)` with a clear message (PAT-013 "How to
fix"; kampylos 5a65505 for the exit). Keep `boinc_finish(1)` for class 3 (local memory too small, kernel build
failure on a supported driver needs a look at the log). Checked return codes for buffer transfers (PLA-015) belong
to the same change. For volunteers: transient GPU trouble no longer costs the task.

## Test

Stub or environment switch that makes the Nth `clEnqueueNDRangeKernel` fail: the run must end in a temporary exit
with the checkpoint intact; repeated failures without progress must end in `boinc_finish(1)` after the limit.

## Log

- 2026-10-21: found in the audit at 4edb8b0 (plan PL5).
- 2026-10-27: recorded from the 2026-10-21 audit, verified at 3f6c1b7 by reading the exits listed above.
