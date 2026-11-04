---
id: PLA-015
title: OpenCL buffer writes, reads and kernel arguments are not checked, so a failed transfer silently reuses old data
type: bug
status: confirmed
severity: low
platforms: [opencl]
area: gpu-runtime
next: In gpu_scan(), check every clEnqueueWriteBuffer, clSetKernelArg and clEnqueueReadBuffer, return false on error; clear host results first
pattern: PAT-027
aliases: []
reported: 2026-10-27
source: audit 2026-10-27 (knowledge-base read-through)
where: src/planodia_opencl.cpp:197-232 @ 3f6c1b7
fixed_in:
---

## Symptom

Not reported. Expected, rarely (a driver that fails a transfer but not the launch, for example out of resources):
a result line that belongs to wrong data is reported as a success. No message on stderr.

## Cause

In `gpu_scan()` only `clEnqueueNDRangeKernel` and `clFinish` are checked (`planodia_opencl.cpp:226-229`). The return
codes of the blocking `clEnqueueWriteBuffer` calls (times and fluxes `:197-198`, duration table `:205`, periods
`:212`), of `clSetKernelArg` (`:214-224`) and of the blocking `clEnqueueReadBuffer` calls (`:230-232`) are ignored:

* a failed write of `g_tt` / `g_y` leaves the previous light curve's points (or, after a reallocation at
  `:182-194`, uninitialised memory) in the device buffers, so the scan runs on wrong data and the result is written
  under the current TIC;
* a failed write of `g_per` scans the previous chunk's periods;
* a failed read leaves the host vectors with zeros (first chunk) or the previous chunk's values.

`gpu_init()` also ignores the results of `clGetPlatformIDs` (`:120`) and of the device list (`:137`); there a failure
leads to a refused or invalid device rather than wrong data.

Verified by reading `src/planodia_opencl.cpp:115-236` at 3f6c1b7. Not reproduced.

## Fix

Check every call and return false from `gpu_scan()` on any error, with the call's name and code on stderr, so the
error goes the way of other GPU errors (PLA-006). No effect on results when the driver works.

## Test

A stub OpenCL loader (the function table `cl_api` in `cl_dynload.h` makes this easy) whose `EnqueueReadBuffer` or
`EnqueueWriteBuffer` fails on the Nth call: the run must stop with an error and must not write a line for that
light curve.

## Log

- 2026-10-27: found while reading the OpenCL backend at 3f6c1b7.
- 2026-10-27: origin of pattern PAT-027 (linked). Its fix adds: clear the host result vectors before each read, so a
  failed read cannot pass old values.
