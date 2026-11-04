---
id: PLA-005
title: Progress is not reported after a resume until the next light curve is finished
type: bug
status: confirmed
severity: low
platforms: [all]
area: boinc-lifecycle
next: In main(), call boinc_fraction_done((double)done / total) right after out is opened on resume (after main:163)
pattern: PAT-007
aliases: [PL4]
reported: 2026-10-21
source: audit 2026-10-21 (plan section 6.1, PL4 second part)
where: src/main_planodia_boinc.cpp:153-193 @ 3f6c1b7
fixed_in:
---

## Symptom

After a restart from a checkpoint, the application reports no progress until the first new light curve is done.
In format 1 that is short: one light curve takes about 0.08 s on a CPU core and 4 ms on an RTX 5070 Ti (README);
measured on one Apple M1 core on 2026-10-27 (synthetic 27-day light curves): 0.5 s, 1.3 s with three passes. Format 2
can be much longer on a CPU: 19 s per light curve on the M1 core for a heavy setting (11,146 points, `binw` 0.004,
8 durations, 140,569 periods, 3 passes), so more than a minute on a CPU core four or more times slower. Which
format-2 parameters the server uses is not in this repository. What the BOINC manager displays meanwhile was not
checked.

## Cause

`boinc_fraction_done()` is called only after a light curve is finished (`main:193`). Between reading the checkpoint
(`main:153`) and that call, the code opens the GPU, the shards and `out`, and skips the `done` light curves
(`main:172`) without reporting `done / total`.

Verified by reading `src/main_planodia_boinc.cpp:153-193` at 3f6c1b7.

## Fix

Proposed (plan PL4): call `boinc_fraction_done((double)done / total)` right after the resume position is known
(after `main:163`). No effect on results.

## Test

Stub BOINC library that logs `boinc_fraction_done()`: after a resume with checkpoint `done`, the first logged value
must be `done / total`.

## Log

- 2026-10-21: found in the audit at 4edb8b0 (plan PL4).
- 2026-10-27: recorded from the 2026-10-21 audit, verified at 3f6c1b7 by reading src/main_planodia_boinc.cpp:153-193;
  origin of pattern PAT-007.
- 2026-10-27: re-checked after PAT-007 was corrected (affected only when the first report can take more than about a
  minute): still affected for format-2 work units on slow CPUs (measurement in Symptom), not for format 1.
