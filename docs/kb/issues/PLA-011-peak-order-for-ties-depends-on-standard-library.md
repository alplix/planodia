---
id: PLA-011
title: The order of periods with equal power depends on the C++ standard library (std::partial_sort without a tie-break)
type: bug
status: confirmed
severity: low
platforms: [macos, android, clang]
area: determinism
action: decide
next: Decide: may finish() break power ties by index (d[a] == d[b] && a < b)? Same peaks on every library; changes results only at ties
blocked_by: [PLA-010]
pattern: PAT-026
aliases: []
reported: 2026-10-21
source: audit 2026-10-21 (plan section 3.3), measured 2026-10-27
where: src/planodia_core.h:229-237 @ 3f6c1b7
fixed_in:
---

## Symptom

Builds that use libc++ (macOS, Android NDK, Windows arm64 with llvm-mingw) and builds that use libstdc++ (Linux GCC,
MinGW-w64 GCC) can report different peaks for the same light curve when two trial periods have exactly the same
noise-floor-subtracted power: another period is taken, or the order of two peaks changes. Measured on 2026-10-27 on
an Apple M1 with 50 synthetic light curves, format 1 (CPU): every light curve had tied values among its 4,000
strongest periods, 6 of 50 among the 50 strongest; the order of the 60 strongest periods differed between a libc++
build (Apple clang) and a libstdc++ build (GCC 13.2) in 3 of 50 light curves (two tied neighbours swapped). The
printed result lines were still identical on this set, because the swapped periods were not the ones reported.

## Cause

`finish()` sorts the period indices by power with
`std::partial_sort(..., [&](size_t a, size_t b) { return d[a] > d[b]; })` (`src/planodia_core.h:230`). The order of
equal elements is unspecified, and libc++ and libstdc++ implement the heap selection differently. The peak loop
then takes the first index of each 2 % period window (`:232-237`), so with a tie at a reported position the chosen
period depends on the library. The power is a float (`ScanOut::power`), and neighbouring trial periods often fold
the points into the same bins, so exact ties are common. `r.sde` uses only the value and is not affected.

Verified by reading `src/planodia_core.h:213-237` at 3f6c1b7 and by the measurement above (scratch programs that
call `finish()`'s sort on the scan output; not part of the repository).

## Fix

Proposed, not applied because it can change reported peaks (plan section 10 lists it as a recommendation only):
break ties by index in the comparator, `d[a] > d[b] || (d[a] == d[b] && a < b)`, which makes the order the same with
every library. It changes results only where a tie sits at a reported position. Needs the maintainers' agreement and
a note of the change in the results (PLA-010 decides whether it matters for validation).

## Test

Build `planodia_cli` with clang++ (libc++) and with GCC (libstdc++) and run both on the same shards; with the
tie-break the outputs must be identical. A unit test can feed `finish()` a power spectrum with deliberate ties.

## Log

- 2026-10-21: listed in the audit at 4edb8b0 (plan section 3.3).
- 2026-10-27: verified at 3f6c1b7 by reading planodia_core.h:229-237 and measured on an Apple M1 (numbers above).
- 2026-10-27: origin of pattern PAT-026 (linked). The other sorts in the code (`std::nth_element` in `median_of()`,
  `std::sort` in `floor_subtract()`) use only the values, so their tie order does not reach the output.
