---
id: PLA-023
title: OpenCL app: prepare(), the kernel launches and finish() run one after the other with blocking calls
type: bug
status: confirmed
severity: low
platforms: [opencl]
area: performance
action:
next: Measure prepare/scan/finish with --time on a fast GPU, then overlap the next light curve's prepare() with the current scan and drop the blocking reads
blocked_by: []
pattern: PAT-033
aliases: []
reported: 2026-11-04
source: read 2026-11-04 (docs/BENCHMARK.md, candidate 6)
where: src/planodia_opencl.cpp:200-235 @ 089cbd3
fixed_in:
---

## Symptom

Wasted time, no wrong result, not measured on a fast GPU: a light curve is `prepare()` on the CPU, up to six launches of
4,096 periods, each a blocking write, `clFinish` and three blocking reads, then `finish()` on the CPU, one after the
other. On an RTX 5070 Ti the README gives 4 ms per light curve in total, so the host parts are likely a large share
there; on the Apple M4 Pro the OpenCL scan took about 1-2 s per profile against 7-33 s on one CPU core (2026-11-04).

## Cause

`gpu_scan()` and its caller (`src/planodia_opencl.cpp`, `src/main_planodia_boinc.cpp`) use blocking transfers and
`clFinish` per launch and do the CPU work of the next light curve only after the current one is read back (PAT-033).

## Fix

Proposed, not done: `--time` already splits prepare and scan per light curve; measure on a fast GPU, then prepare the
next light curve while the current one scans (and finish the previous one), with events instead of blocking calls. The
result lines and their order must stay the same, and the checkpoint counts only lines already written.

## Test

`scripts/bench/run_bench.sh quick 3 ocl-base=... ocl-new=...` on the GPU in question: the same lines (compare OpenCL
only with OpenCL, PLA-009), and `--time` before and after.

## Log

- 2026-11-04: recorded with pattern PAT-033.
