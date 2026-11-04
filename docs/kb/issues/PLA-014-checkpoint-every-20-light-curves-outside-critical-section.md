---
id: PLA-014
title: The checkpoint is written every 20 light curves, outside a critical section and regardless of the volunteer's checkpoint interval
type: bug
status: confirmed
severity: low
platforms: [all]
area: checkpoint
action: decide
next: Decide: drop the every-20-light-curves checkpoint (timer only), or keep a time-based minimum inside a critical section?
blocked_by: [PLA-001]
pattern: PAT-004
aliases: []
reported: 2026-10-27
source: audit 2026-10-27 (knowledge-base pattern check)
where: src/main_planodia_boinc.cpp:194-195 @ 3f6c1b7
fixed_in:
---

## Symptom

The app writes and renames its checkpoint file far more often than the volunteer's "request tasks to checkpoint at
most every N seconds" allows: at 0.08 s per light curve (README, one CPU core) every 1.6 s, at 4 ms per light curve
(RTX 5070 Ti) about 12 times a second. Each write is a new file plus a rename. On phones and laptops this is
avoidable flash wear and disk wake-ups; nothing is lost or wrong.

## Cause

`if (idx % 20 == 0 || boinc_time_to_checkpoint())` (`main:194`) writes the checkpoint every 20 light curves whether
or not the timer asked. Those writes are not inside a critical section, so a quit can stop the process in the middle
of one; that is harmless here because the write goes to a temporary file that is renamed (`main:48-53`, but see
PLA-004 for the unchecked results). `boinc_checkpoint_completed()` is called only when the timer asked
(`main:196`), so the client's bookkeeping is not disturbed (apart from PLA-001).

Verified by reading `src/main_planodia_boinc.cpp:45-54, 194-197` at 3f6c1b7; the rates are computed from the README
timings, not measured.

## Fix

Two options for the maintainers, both after PLA-001: drop the own cadence and checkpoint only when
`boinc_time_to_checkpoint()` returns 1 (a resume then repeats at most one checkpoint interval of work, which the
volunteer chose), or keep a time-based minimum of the app's own (for example at most once every few seconds) and put
the write inside `boinc_begin_critical_section()` / `boinc_end_critical_section()`. The comment at `main:194` ("the
checkpoint is a few bytes: write it often") gives the reason for the current choice; with GPU speeds "often" became
several times a second. No effect on results.

## Test

Stub BOINC library that logs every checkpoint write and every `boinc_time_to_checkpoint()` result; with the timer
never firing, only the final checkpoint (`main:202`) may be written.

## Log

- 2026-10-27: found while checking pattern PAT-004, verified at 3f6c1b7 by reading the code.
