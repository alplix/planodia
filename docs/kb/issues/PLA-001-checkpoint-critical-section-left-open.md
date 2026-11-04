---
id: PLA-001
title: boinc_time_to_checkpoint() is called twice per checkpoint, so a critical section stays open and suspend, quit and abort stop working
type: bug
status: confirmed
severity: high
platforms: [all]
area: boinc-lifecycle
next: In main() call boinc_time_to_checkpoint() once per light curve, keep it in a bool, pair each 1 with one boinc_checkpoint_completed() (main:194-196)
pattern: PAT-001
aliases: [PL1]
reported: 2026-10-21
source: audit 2026-10-21 (plan section 1 and 6.1, PL1)
where: src/main_planodia_boinc.cpp:194-196 @ 3f6c1b7
fixed_in:
---

## Symptom

Not reported by a volunteer yet; found by reading the code. Expected effect on every host, CPU and GPU builds alike:
from the first checkpoint interval on (60 s by default, the volunteer's "checkpoint at most every N seconds"), the
task no longer suspends when the client says so (it keeps using the CPU or GPU while the BOINC manager shows it
suspended), ignores quit and abort, and does not exit when the client dies. A quit at a client exit or reboot is
deferred until the task ends.

## Cause

`src/main_planodia_boinc.cpp:194-197` at 3f6c1b7:

```
if (idx % 20 == 0 || boinc_time_to_checkpoint()) {
    write_checkpoint(idx);
    if (boinc_time_to_checkpoint()) boinc_checkpoint_completed();
}
```

Each call that returns 1 enters a critical section; `boinc_checkpoint_completed()` leaves one and clears the flag
([boinc-api.md](../shared/boinc-api.md#checkpoints-and-critical-sections)). The four cases of one loop iteration:

| `idx % 20 == 0` | timer flag set | calls returning 1 | `checkpoint_completed` | counter change |
|---|---|---|---|---|
| no | no | 0 (line 194 returns 0, body skipped) | no | 0 |
| no | yes | 2 (line 194, then line 196) | once | **+1** |
| yes | no | 0 (line 194 short-circuits, line 196 returns 0) | no | 0 |
| yes | yes | 1 (line 194 short-circuits, line 196) | once | 0 |

The timer sets the flag asynchronously, so the first iteration that sees it has `idx % 20 != 0` in 19 of 20 cases.
Every such checkpoint adds one level that is never left; the counter only grows. With the counter above 0 the BOINC
library records suspend, quit, abort and "client dead" but does not act on them.

Verified by reading `src/main_planodia_boinc.cpp:194-197` at 3f6c1b7 against `docs/kb/shared/boinc-api.md`
(BOINC `api/boinc_api.cpp:1646-1664` at 4d1529d). Not reproduced: the repository has no stub BOINC library. The
code is unchanged since the plan's commit 4edb8b0 (`git diff 4edb8b0 3f6c1b7` touches only `src/cl_dynload.c`).

## Fix

Proposed (plan PL1), not written yet: call the timer once per opportunity and pair it.

```
bool due = boinc_time_to_checkpoint();
if (idx % 20 == 0 || due) {
    bool ok = write_checkpoint(idx);          // write_checkpoint() must return its result (PLA-004)
    if (due) {
        if (ok) boinc_checkpoint_completed();
        else boinc_end_critical_section();
    }
}
```

Whether the own 20-light-curve cadence should stay is a separate question (PLA-014). For volunteers the fix
restores suspend, quit and abort; results and checkpoints do not change.

## Test

Stub BOINC library (PAT-001 "How to test") whose `boinc_time_to_checkpoint()` returns 1 on a chosen call and which
counts begin and end of critical sections; it aborts when the counter is not 0 after
`boinc_checkpoint_completed()` or at `boinc_finish()`. Build `main_planodia_boinc.cpp` against it and run a small
synthetic work unit with the timer firing when `idx % 20 != 0`. At 3f6c1b7 this must fail; after the fix it must
pass. No such test exists in the repository yet.

## Log

- 2026-10-21: found in the audit at 4edb8b0 (plan PL1, marked verified).
- 2026-10-27: recorded from the 2026-10-21 audit, verified at 3f6c1b7 by reading src/main_planodia_boinc.cpp:194-197
  and walking through the four short-circuit cases; origin of pattern PAT-001.
