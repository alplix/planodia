---
id: PLA-013
title: A checkpoint or out that cannot be opened is treated as a fresh start, and out is truncated
type: bug
status: confirmed
severity: low
platforms: [all]
area: checkpoint
next: In read_checkpoint() and trim_output(), treat only !boinc_file_exists() as a fresh start; other open failures -> boinc_temporary_exit(300, ...)
pattern: PAT-005
aliases: []
reported: 2026-10-27
source: audit 2026-10-27 (knowledge-base pattern check)
where: src/main_planodia_boinc.cpp:34-43, 57-59, 153-161 @ 3f6c1b7
fixed_in:
---

## Symptom

Not reported. Expected mainly on Windows: when antivirus, an indexer or a backup tool holds `planodia_checkpoint` or
`out` at the moment the task restarts, the task starts again from light curve 0 and the finished lines are lost.
The task still ends with a consistent result; only time is lost.

## Cause

`read_checkpoint()` returns 0 when `boinc_fopen(p, "r")` fails (`main:37-38`), and `trim_output()` returns false
when `boinc_fopen(path, "rb")` fails (`main:58-59`). Either way `main()` sets `done = 0` and opens `out` with `"wb"`
(`main:158-160`), which truncates it. `boinc_fopen()` returns NULL both for a missing file and for one that could not
be opened, on Windows after retrying for 5 s
([boinc-api.md](../shared/boinc-api.md#files)); the code does not tell the two apart.

Verified by reading the code at 3f6c1b7. Not reproduced.

## Fix

Treat only a missing file as a fresh start (`!boinc_file_exists(path)`); when the file exists but cannot be opened,
`boinc_temporary_exit(300, "cannot open the checkpoint or the output file, will retry", false)` and leave the files
alone (PAT-005 "How to fix"). A checkpoint that exists but does not parse may still mean "start from 0", because
`trim_output()` then rewrites `out` consistently.

## Test

On Unix: run until a checkpoint, `chmod 000 out` (or the checkpoint), restart: `out` must not be truncated and the
task must ask for a retry. On Windows: hold the file open without sharing from another process during the restart.

## Log

- 2026-10-27: found while checking pattern PAT-005, verified at 3f6c1b7 by reading the code.
