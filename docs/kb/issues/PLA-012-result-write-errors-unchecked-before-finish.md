---
id: PLA-012
title: Write errors on out are not checked before boinc_finish(0), so a truncated result can be reported as a success
type: bug
status: confirmed
severity: low
platforms: [all]
area: io
next: In main(), check fprintf/fflush/fclose on out (or ferror before fclose); on failure no boinc_finish(0), exit so the resume repairs out
pattern: PAT-006
aliases: []
reported: 2026-10-27
source: audit 2026-10-27 (knowledge-base pattern check)
where: src/main_planodia_boinc.cpp:161-204 @ 3f6c1b7
fixed_in:
---

## Symptom

Not reported. Expected on a full disk or a quota: the task ends normally and uploads an `out` with missing or cut
lines; the validator rejects it, or the server receives fewer lines than light curves.

## Cause

None of the writes to `out` is checked: the header `fprintf` (`main:161`), every result line `fprintf` and `fflush`
(`main:190-191`), and the final `fclose` (`main:200`). After the loop only the light-curve count is compared
(`main:201`), then `boinc_finish(0)` (`main:204`).

Verified by reading `src/main_planodia_boinc.cpp:151-205` at 3f6c1b7.

## Fix

Check the results of `fprintf`, `fflush` and `fclose` (or `ferror(fout)` before the close); on failure do not call
`boinc_finish(0)`: a temporary exit lets the resume repair `out` from the checkpoint (`trim_output()`), or
`boinc_finish(1)` with the `strerror(errno)` text. Together with PLA-004, which has the same problem for the trim.

## Test

On Linux: link `out` to `/dev/full` in the slot directory of a standalone run; the run must not end with status 0.

## Log

- 2026-10-27: found while checking pattern PAT-006, verified at 3f6c1b7 by reading the code.
