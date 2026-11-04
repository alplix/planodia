---
id: PLA-017
title: The light-curve start time t0 is stored as f32 in the shards, so reported transit times carry up to 1.2e-4 d of rounding
type: design-note
status: documented
severity: info
platforms: [all]
area: data-pipeline
pattern:
aliases: []
reported: 2026-10-21
source: audit 2026-10-21 (plan section 3.3)
where: src/skt.h:4, 20 @ 3f6c1b7
fixed_in:
---

## Symptom

None visible. The transit time `tc` of each peak is printed with four decimals (`%.4f`, `planodia_core.h:323`),
but the last decimal is not meaningful: every `tc` of a light curve is shifted by the same rounding error of its
`t0`, up to 1.2e-4 d (10.5 s).

## Cause

The shard record stores `t0` (BTJD of cadence offset 0) as f32 (`src/skt.h:4`, `LC::t0` at `:20`; written by
`tools/ingest.py:102, 106` with `struct.pack("<Q5fI", ...)`). The default sectors 27 to 55 (`ingest.py:28`) have
BTJD of about 2035 to 2825; f32 values in [2048, 4096) are 2.44e-4 d apart, so the stored value is off by up to
1.22e-4 d (in [1024, 2048): up to 0.61e-4 d). `Prep::t0` stays f32 (`planodia_core.h:44`) and is added to the
in-cycle transit time in double (`:247`). The search itself uses times relative to the first point
(`planodia_core.h:60, 85`) and is not affected; `dt` is also f32, a relative error of about 6e-8, below 2e-6 d over a
sector.

Verified by reading `src/skt.h`, `tools/ingest.py` and `src/planodia_core.h:240-247` at 3f6c1b7; the error bound is
computed, not measured.

## Fix

Nothing needed for the search: transits last 0.04 d or more, and catalogue epochs are known to about 1e-3 d. If
`tc` must be precise to seconds, store `t0` as f64 (a new shard version, reader and writer changed together) or as
an offset from a fixed epoch. Changing it alters the result lines.

## Test

None needed. To see the size: `python3 -c "import numpy as np; print(np.float32(2500.123456) - 2500.123456)"`.

## Log

- 2026-10-21: listed in the audit at 4edb8b0 (plan section 3.3).
- 2026-10-27: verified at 3f6c1b7 by reading the reader, the writer and the use in finish().
