---
id: PLA-009
title: The OpenCL scan (float, 16.16 fixed-point sums) is not bit-identical to scan_cpu (double), so CPU and GPU result lines differ
type: design-note
status: documented
severity: medium
platforms: [opencl]
area: determinism
pattern: PAT-018
aliases: []
reported: 2026-10-21
source: audit 2026-10-21 (plan section 3.3), measured 2026-10-27
where: src/planodia_opencl.cpp:23-91, 195-211 @ 3f6c1b7
fixed_in:
---

## Symptom

The CPU app and the OpenCL app give different result lines for the same work unit: mostly the last printed digit
of SDE and snr, sometimes a different peak. Measured on 2026-10-27 on an Apple M1 with 50 synthetic light curves
(40 with about 3,700 points at 10-minute cadence, 10 with about 11,100 points at 200 s; 25 with an injected box
transit), format 1 defaults, `planodia_cli` CPU (clang++ -O2) against `planodia_cli --gpu` (Apple OpenCL), same
source:

* 45 of 50 lines differ in at least one character; 5 are identical.
* 5 of 50 have a different peak list: one of the five periods differs, or two peaks swap places. All five are light
  curves without a detectable signal (SDE 4.6 to 6.3).
* Where the periods agree: SDE differs by at most 0.06, snr by at most 0.01; depth and tc are equal at print
  precision. The light curves with a detectable injected transit give the same periods.

Whether that matters depends on the validator (PLA-010).

## Cause

The GPU path rounds differently on purpose, to run fast in single precision on every OpenCL 1.2 GPU without fp64:

* times, fluxes, periods, `binw` and `sigma` are converted to float (`planodia_opencl.cpp:196, 206, 211`), so the
  phase of a point near a bin edge can fall into the other bin, and `nb = ceil(per / binw)` is computed in float
  (`planodia_opencl.cpp:40`; the CPU does it in double, `planodia_core.h:128`);
* the per-bin flux sums are 16.16 fixed-point integers (`(int)rint(y * 65536)`, `planodia_opencl.cpp:18-22, 50`): a
  quantisation of 1.5e-5 per point. Integer sums do not depend on the order of the atomics, so one device gives the
  same result every run;
* the box statistic is computed in float with OpenCL's `sqrt` and division, which OpenCL 1.2 does not require to be
  correctly rounded, and OpenCL C may contract `a*b+c` by default: different GPU vendors can also differ from each
  other (not measured);
* `finish()` and everything after it run on the host in double for both backends (`planodia_core.h:213-274`), so
  the differences come only from the per-period power, best duration and start bin.

The comments `planodia_opencl.h:2` and `planodia_core.h:118` ("computes exactly the same thing") describe the
algorithm, not the bits; `planodia_core.h:2-3` says "agree to float rounding", which is what was measured.

## Fix

Nothing to change in the code; this is the price of the single-precision GPU path. Consequences for the server:
either the validator compares with tolerances and lets a weak peak differ, or CPU and GPU results are never
compared with each other (BOINC's homogeneous app version). Question to the maintainers: PLA-010. A change to the
kernel or to `scan_cpu()` should be checked with the same CPU-against-GPU comparison and the numbers above
updated.

## Test

Build `planodia_cli` with and without `-DPL_OPENCL` (flags as in `tools/build_mac.sh`), run both on the same shards
(`--gpu` for the OpenCL build) and compare the lines field by field: count identical lines, lines with the same peak
periods, and the largest SDE and snr differences.

## Log

- 2026-10-21: listed in the audit at 4edb8b0 (plan section 3.3: mixed CPU/GPU quorums need a tolerant validator).
- 2026-10-27: verified at 3f6c1b7 by reading the kernel and gpu_scan(), and measured on an Apple M1 (numbers above).
