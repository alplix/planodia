---
id: PLA-024
title: CPU scan: the fold's additions wait on each other, because consecutive points land in the same bin
type: bug
status: fix-proposed
severity: low
platforms: [all]
area: performance
action: review
next: Review the multi-period fold (scan_cpu); measure the vectorisable box scan for clang builds
blocked_by: []
pattern:
aliases: []
reported: 2026-11-04
source: M4 benchmark 2026-11-04 (docs/BENCHMARK.md, Results)
where: src/planodia_core.h:119-169 @ 089cbd3
fixed_in:
---

## Symptom

Wasted time, no wrong result: the CPU app's fold loop ran at the speed of a chain of dependent memory updates, not of
its arithmetic. With the fix the CPU scan is 1.74x (600 s cadence) to 2.9x (200 s cadence) faster on an Apple M4 Pro and
1.6-2.5x on Linux arm64 with GCC 12, with the same result lines.

## Cause

`scan_cpu()` (`src/planodia_core.h:119-169` at 089cbd3) folds one trial period per pass over the points. Consecutive
points are `dt` apart and a bin is `binw` wide, so a point lands in its predecessor's bin in 65 % (600 s cadence) to
88 % (200 s) of the cases, and each `cnt[b] += 1.0; sum[b] += y[i]` waits for the store of the one before.

## Fix

This branch: `scan_cpu()` folds `SCAN_FOLD_PERIODS` = 4 periods per pass, each with its own bins, so the CPU overlaps
four independent chains; each bin still receives its points in index order with the same expressions, so every sum
and every result line is bit-identical. The box search moved unchanged into `scan_folded()`. The OpenCL kernel is a
different algorithm and needs no change. A vectorisable form of the box search gained 1.14-1.24x more with clang but
lost 9 % with GCC 12 at `-O2` and was left out (docs/BENCHMARK.md).

## Test

`scripts/bench/mac_ab.sh 3 "quick dense deep"`: same output on every run; `linux_ab.sh` (GCC 12, clang 14) and
`cross_check.sh` (Linux x86_64 GCC, Windows MinGW-w64 under Wine): the same result lines on every platform.

## Log

- 2026-11-04: created; fix written and measured on an Apple M4 Pro and Linux arm64.
