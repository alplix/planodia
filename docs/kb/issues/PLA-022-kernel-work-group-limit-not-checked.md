---
id: PLA-022
title: The required work-group size of 64 is checked against the device limit but not against the compiled kernel's limit
type: bug
status: confirmed
severity: info
platforms: [opencl]
area: gpu-runtime
next: In gpu_init(), query CL_KERNEL_WORK_GROUP_SIZE after clCreateKernel and refuse the device with a clear message when it is below 64
pattern: PAT-015
aliases: []
reported: 2026-10-27
source: audit 2026-10-27 (pattern PAT-015 re-check after its correction)
where: src/planodia_opencl.cpp:142-150, 171-173 @ 3f6c1b7
fixed_in:
---

## Symptom

None known. On a device whose compiled kernel cannot run 64 work items per group, every launch would fail with
`planodia_opencl: clEnqueueNDRangeKernel failed (-54)` (`CL_INVALID_WORK_GROUP_SIZE`) or `-5` and the task would end
as a computation error, instead of a clear message at start.

## Cause

The kernel is written for exactly 64 work items (`reqd_work_group_size(64, 1, 1)`, `#define WG 64`,
`planodia_opencl.cpp:24-26`; launch with `local = 64`, `:225`). `gpu_init()` refuses a device with
`CL_DEVICE_MAX_WORK_GROUP_SIZE < 64` with the message "device has too little work-group size or local memory"
(`:142-150`), but it does not query `CL_KERNEL_WORK_GROUP_SIZE` after `clCreateKernel` (`:171-173`), although
`cl_dynload` loads `clGetKernelWorkGroupInfo`. The kernel is small, so a kernel limit below 64 is unlikely; no device
with one is known.

Verified by reading the code at 3f6c1b7. The corrected PAT-015 counts a required size as fine only when both the
device and the kernel limit are checked.

## Fix

After `clCreateKernel`, query `CL_KERNEL_WORK_GROUP_SIZE` (and `CL_KERNEL_LOCAL_MEM_SIZE` against the device's local
memory) with `clGetKernelWorkGroupInfo` and refuse the device with a clear message when it is below 64. With PLA-006,
that refusal is a deterministic error, not a temporary exit.

## Test

Hard to reproduce on real hardware. With a stub `clGetKernelWorkGroupInfo` (the `cl_api` table) returning 32,
`gpu_init()` must fail with the message.

## Log

- 2026-10-27: recorded after the re-check of the corrected PAT-015 at 3f6c1b7 (the first round had marked PAT-015
  not-affected because the device limit is checked).
