---
id: PLA-004
title: trim_output() rewrites out in place, and the checkpoint and trim writes are not checked
type: bug
status: confirmed
severity: low
platforms: [all]
area: checkpoint
next: Add one helper (temp file, checked writes and close, boinc_rename) for trim_output() and write_checkpoint(); report only on success
pattern: PAT-003
aliases: [PL4]
reported: 2026-10-21
source: audit 2026-10-21 (plan section 6.1, PL4 first part)
where: src/main_planodia_boinc.cpp:45-82 @ 3f6c1b7
fixed_in:
---

## Symptom

Not reported. Expected, rarely: a task that is killed or quit while it trims `out` at start-up starts again from
light curve 0 (lost work); with a full disk during the trim, `out` can contain a broken line that is uploaded as a
successful result.

## Cause

At 3f6c1b7, against the four forms of PAT-003:

* (a) `trim_output()` opens the result file itself with `boinc_fopen(path, "wb")` (`main:77`), which truncates it,
  and then writes the kept part (`main:79`). A kill or quit between those lines leaves `out` empty or partial. The
  next start then finds no header or too few lines, `trim_output()` returns false and the task restarts from 0
  (`main:155-161`): work is lost, the output stays consistent. The trim runs at start-up, outside any critical
  section.
* (b) the results of `fwrite()` and `fclose()` in `trim_output()` (`main:79-80`) and of `fprintf()` and `fclose()` in
  `write_checkpoint()` (`main:51-52`) are not checked. A short `fwrite()` on a full disk leaves `out` ending in a
  torn line, `trim_output()` still returns true, and the main loop appends to it with `"ab"` (`main:157`): the torn
  line and the next result line run together. A failed checkpoint write renames an empty file over the good one;
  `read_checkpoint()` then returns 0 and the task restarts (consistent, work lost).
* (c) not affected: `write_checkpoint()` writes `planodia_checkpoint.tmp` and uses `boinc_rename()` (`main:48-53`);
  its result is not checked.
* (d) `write_checkpoint()` returns nothing, so `boinc_checkpoint_completed()` (`main:196`) is called whether or not
  the write worked.

Verified by reading `src/main_planodia_boinc.cpp:45-82, 153-163, 194-197` at 3f6c1b7. The disk-full and kill cases
were not reproduced.

## Fix

Proposed (plan PL4): one helper for both files that writes a temporary file, checks every write, `fflush` and
`fclose`, and moves it with `boinc_rename()`, returning success (PAT-003 "How to fix"). `trim_output()` writes the
kept part to `out.tmp` and renames it over `out`; `write_checkpoint()` returns the result, and the caller reports the
checkpoint only on success (see PLA-001 for the critical section). If the trim fails, end with a temporary exit
rather than appending to an unknown file. Results do not change.

## Test

On Linux: make the temporary file `/dev/full` (symbolic link) in the slot directory of a standalone run; the old
`out` and checkpoint must stay unchanged and no checkpoint may be reported. Kill the process in a loop during
start-up with a large `out`; every restart must give the same final `out` as an uninterrupted run (`cmp`).

## Log

- 2026-10-21: found in the audit at 4edb8b0 (plan PL4).
- 2026-10-27: recorded from the 2026-10-21 audit, verified at 3f6c1b7 by reading src/main_planodia_boinc.cpp:45-82;
  the torn-line case after a short trim write added.
