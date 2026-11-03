---
id: PLA-016
title: A format-2 work unit with a large pmax / binw needs more GPU local memory than the device has, and the GPU task fails at start
type: design-note
status: documented
severity: low
platforms: [opencl]
area: gpu-runtime
pattern:
aliases: []
reported: 2026-10-21
source: audit 2026-10-21 (plan section 3.3), measured 2026-10-27
where: src/planodia_opencl.cpp:146-150 @ 3f6c1b7
fixed_in:
---

## Symptom

The OpenCL app stops at start with

```
no usable GPU: device has too little work-group size or local memory
```

and the task ends as a computation error. Reproduced on 2026-10-27 on an Apple M1 (32 KB local memory) with
`planodia_cli --gpu` and `PL_PMAX=60 PL_BINW=0.004` (15,000 bins). The CPU app runs the same work unit normally.

## Cause

The kernel keeps the phase histogram of one trial period in local memory: two int tables of `MAXB` entries plus the
reduction arrays. `gpu_init()` sets `maxbins = ceil(pmax / binw)` (from `main:130`), at least 256 and rounded up to
a multiple of 64, and refuses the device when `CL_DEVICE_LOCAL_MEM_SIZE < 8 * maxbins + 64 * 12 + 512` bytes
(`planodia_opencl.cpp:146-150`). Largest `ceil(pmax / binw)` that fits:

| Local memory | Examples (only the M1 was checked here) | Largest ceil(pmax / binw) |
|---|---|---|
| 32 KB | Apple M1 (measured: 3,897 bins run, 3,922 fail), Adreno | 3,904 |
| 48 KB | NVIDIA | 5,952 |
| 64 KB | AMD, recent Intel | 8,000 |

Format 1 cannot exceed it: `binw` is fixed at 0.02 d and `pmax` <= 60 (`main:107`), at most 3,000 bins. Format 2
accepts `binw` down to 0.001 d with `pmax` up to 60 (`main:107, 110`), up to 60,000 bins. The refusal is the same
on every GPU of that size, so the work unit fails on every such host until it reaches a CPU host.

Verified by reading the code at 3f6c1b7 and by the M1 run above. Which `pmax` and `binw` the server uses for
Planodia Deep is not in this repository.

## Fix

A limit, not a bug. The work generator must keep `ceil(pmax / binw)` at or below 3,904 for GPU work (for example
`pmax` 14 d needs `binw` >= 0.0036 d), or the server must send larger ones to CPU versions only. A kernel that
keeps the histogram in global memory (or tiles it) would lift the limit at some speed cost. Since the failure is
deterministic, it should stay a computation error even after PLA-006, but with a message that names the bin count
and the device's local memory.

## Test

`PL_PMAX=60 PL_BINW=0.004 planodia_cli --gpu <shard>` on a 32 KB device must fail at start; with `PL_BINW=0.0154`
(3,897 bins) it must run, with `PL_BINW=0.0153` (3,922 bins) fail (both checked on the M1, 2026-10-27).

## Log

- 2026-10-21: listed in the audit at 4edb8b0 (plan section 3.3).
- 2026-10-27: verified at 3f6c1b7 by reading planodia_opencl.cpp:146-150 and reproduced on an Apple M1.
