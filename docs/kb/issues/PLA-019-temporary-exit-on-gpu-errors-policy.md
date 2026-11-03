---
id: PLA-019
title: Should transient GPU errors end in a temporary exit with a retry limit instead of a computation error?
type: question
status: new
severity: medium
platforms: [opencl]
area: gpu-runtime
next: Decide: may transient GPU errors end in boinc_temporary_exit() with a retry cap (about 5, reset on progress) instead of a computation error?
pattern: PAT-013
aliases: []
reported: 2026-10-21
source: audit 2026-10-21 (plan section 8, temporary exit on GPU errors)
where: src/main_planodia_boinc.cpp:130, 187 @ 3f6c1b7
fixed_in:
---

## Symptom

Open question for the maintainers, needed before PLA-006 can be fixed. Today every GPU error ends the task with
`boinc_finish(1)`.

## Cause

`boinc_temporary_exit()` keeps the task and its checkpoint and lets the client start it again later. The client
aborts the task only after more than 100 temporary exits, more than 8 hours of retries at a 300 s delay
([boinc-api.md](../shared/boinc-api.md#exit-paths)), so a lower limit of the application's own is a choice. The
proposal (PAT-013, plan PL5):

1. No usable device, context or queue creation failed, buffer allocation, kernel launch or `clFinish` failed:
   `boinc_temporary_exit()` with a delay of some minutes.
2. A counter in a small file in the slot directory, reset when a light curve is finished; after about five temporary
   exits without progress, `boinc_finish(1)` with a clear message.
3. Deterministic failures (local memory too small for the work unit, PLA-016) stay computation errors.

Questions: is this acceptable for Planodia (and the same policy for the other GPU apps)? Which delay and which
retry limit? Should "no OpenCL library" or "no GPU of this vendor" (a driver missing for good) count as transient?

## Fix

The answer goes into this issue; PLA-006 is then fixed accordingly.

## Test

See PLA-006.

## Log

- 2026-10-21: asked in the audit plan (section 8).
- 2026-10-27: recorded at 3f6c1b7; no answer yet.
- 2026-10-27: re-checked after boinc-api.md and PAT-013 were corrected: the client's own limit (more than 100
  temporary exits) added; the question stands.
