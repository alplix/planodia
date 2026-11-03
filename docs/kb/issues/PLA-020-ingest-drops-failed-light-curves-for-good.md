---
id: PLA-020
title: ingest.py writes a shard even when downloads failed, so those light curves are never fetched again; a cut target-list download is reused as complete
type: bug
status: confirmed
severity: medium
platforms: [all]
area: data-pipeline
next: In ingest.py, tell a 404 from a failed fetch, do not write a shard after a transient failure, download the target list to a temp name
pattern: PAT-023
aliases: []
reported: 2026-10-27
source: audit 2026-10-27 (pattern PAT-023 check)
where: tools/ingest.py:58-70, 140-176 @ 3f6c1b7
fixed_in:
---

## Symptom

Not reported. Expected: after a network problem (MAST timeouts, server errors, a dropped connection), some stars of a
sector are missing from its shards for good, and nothing says which ones. The sector's `DONE` file and the status file
only give a count (`missing`), and that count covers only the shards written in the last run (`missing` starts at 0
in every run, `ingest.py:150`). No work unit ever searches those stars. Results are not wrong; coverage is lower than
it looks.

## Cause

The unit of resume is the shard file (250 targets, `SHARD_N`): a shard that exists is skipped (`ingest.py:154-156`).

* `fetch()` (`:58-70`) tries a light curve 4 times and then returns `None`, the same value as for a 404 (light curve
  does not exist). The loop counts both as `missing` and leaves the star out (`:160-168`), then writes the shard
  (`:169`). Transient failures are therefore treated like permanent ones (PAT-023 case a, per light curve).
* The target list is downloaded with `urllib.request.urlretrieve(LISTS % sector, listp)` straight to its final name
  (`:141-142`) and reused whenever the file exists. A run stopped during that download leaves a partial CSV that the
  next run takes as the whole list: shard boundaries come from row positions, so the targets after the cut are never
  ingested; a cut inside a row gives a row with too few fields (the script stops with an `IndexError`) or a shortened
  number.
* Not affected (case b): each shard is written to `shard_NNNNN.skt.tmp` and moved with `os.replace()` (`:112-118`), so
  the shard file is its own done mark and nothing is appended twice. There is no `flush`/`fsync` before the rename,
  so a power cut can still leave a short shard (the BOINC app then stops with "truncated shard?", `main:201`).

Verified by reading `tools/ingest.py` at 3f6c1b7. Not run (it needs network access and its data path, PLA-008).

## Fix

In `fetch()`, return a different value for "does not exist" (404/400) and "failed"; when any light curve of a shard
failed for a transient reason, do not write the shard (the next run retries it), or record the failed TICs in a
per-sector file and retry them before the sector is marked `DONE`. Download the target list to a temporary name and
rename it. `f.flush(); os.fsync(f.fileno())` before `os.replace()`. Count `missing` per shard (store it next to the
shard) so `DONE` reports the whole sector. Existing shards already used in work units should not be rewritten;
re-ingest only what is missing.

## Test

Mock `urllib.request.urlopen` to fail one TIC every time: no shard containing that TIC's position may be written as
complete, and the run must report the TIC. Make `urlretrieve` raise half way: the next run must download the list
again.

## Log

- 2026-10-27: found while checking pattern PAT-023 at 3f6c1b7, verified by reading tools/ingest.py.
