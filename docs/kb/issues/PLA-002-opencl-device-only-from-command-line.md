---
id: PLA-002
title: The OpenCL device comes only from --device; init_data's gpu_opencl_dev_index is never read
type: bug
status: confirmed
severity: medium
platforms: [opencl]
area: device-selection
next: In main(), read APP_INIT_DATA and use gpu_opencl_dev_index when >= 0, --device only as fallback; fix together with PLA-003
pattern: PAT-011
aliases: [PL2]
reported: 2026-10-21
source: audit 2026-10-21 (plan section 1 and 6.1, PL2)
where: src/main_planodia_boinc.cpp:85-86 @ 3f6c1b7
fixed_in:
---

## Symptom

Not reported for planodia yet. Expected on hosts with more than one GPU of the build's vendor: the task runs on a
different GPU than the one the client assigned, so two tasks can share one GPU while another idles. kampylos had
the same bug, reported on the forum on 2026-10-05 (pattern PAT-011; its fix comment mentions an AMD discrete plus
integrated pair).

## Cause

`main()` sets `device_id = 0` and overrides it only with `--device N` from the command line
(`src/main_planodia_boinc.cpp:85-86`), then passes it to `pl::gpu_init()` (`:130`). There is no
`boinc_get_init_data()` call anywhere in `src/` (`git grep -n 'gpu_opencl_dev_index\|boinc_get_init_data'` finds
nothing). The client's `--device` carries BOINC's own device number, which is not always the OpenCL index of that
device; `APP_INIT_DATA::gpu_opencl_dev_index` is the field made for this
([boinc-api.md](../shared/boinc-api.md#gpu-assignment-init_dataxml)).

Verified by reading the code at 3f6c1b7. Not reproduced (needs a host with two GPUs of one vendor).

## Fix

Proposed (plan PL2): read `APP_INIT_DATA` after `boinc_init_options()` and use `gpu_opencl_dev_index` when it is
>= 0, keep `--device` only as the fallback for standalone runs, and log the chosen index and device name (the name
is already logged, `main:131`). Same as phygas `src/main_phygas_boinc.cpp:346-356` and kampylos b5f5ef2. The index
must then be applied to the list BOINC counts, which `gpu_init()` does not build today (PLA-003); fix both together.
No effect on single-GPU hosts.

## Test

Stub BOINC library or stub `init_data.xml` with `gpu_opencl_dev_index = 1` and `--device 0` on the command line:
stderr must show device 1. On real hardware: a host with two GPUs of one vendor and two tasks; each GPU must be busy.

## Log

- 2026-10-21: found in the audit at 4edb8b0 (plan PL2, marked verified).
- 2026-10-27: recorded from the 2026-10-21 audit, verified at 3f6c1b7 by reading src/main_planodia_boinc.cpp:85-86, 130.
