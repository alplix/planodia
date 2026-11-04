---
id: PLA-010
title: What does the Planodia validator compare, and are CPU, GPU and different CPU platforms mixed in one quorum?
type: question
status: new
severity: medium
platforms: [all]
area: determinism
next: Decide: does the Planodia validator compare bytes, lines or numbers with a tolerance, and can CPU and GPU results meet in one quorum?
pattern: PAT-018
aliases: []
reported: 2026-10-21
source: audit 2026-10-21 (plan section 8, validator tolerance)
where: tools/build_cpu.sh:17-32 @ 3f6c1b7
fixed_in:
---

## Symptom

Open question for the maintainers; the validator is not in this repository. Planodia runs with quorum 2
(project description). Results differ between backends and can differ between platforms:

* CPU against GPU: 45 of 50 lines differ, 5 of 50 in the peak list (PLA-009, measured 2026-10-27).
* libstdc++ builds against libc++ builds: tied powers are ordered differently (PLA-011, measured on the sort order).
* FMA contraction: see Cause.

Questions:

1. What does the validator compare for Planodia and Planodia Deep: bytes, lines, peak periods, or numbers with a
   tolerance? How are peaks matched when the lists differ in one weak peak or in order?
2. Can a CPU result and a GPU result (or results of different GPU vendors) end up in one quorum, or is
   homogeneous app version (or homogeneous redundancy) set for these apps?
3. Is a result with a different weak peak (SDE below about 7) acceptable as a match?

## Cause

FMA contraction is allowed by every build: no script sets `-ffp-contract` (`git grep ffp-contract` is empty at
3f6c1b7; the i686 and win32 builds use SSE2, `tools/build_cpu.sh:9`).

* GCC fuses `a*b+c` across statements in C++ even with `-std=c++17` (its ISO-mode exception is for C only).
  Checked on 2026-10-27 with GCC 13.2 for arm64 (macOS; same front end as Linux aarch64):
  `double t = a*b; return t + c;` compiles to `fmadd`, and the same function in C with `-std=c11` does not. So the
  Linux aarch64, ppc64le and riscv64 CPU builds (`build_cpu.sh:26, 28, 29`) contract; x86_64, i686 and the MinGW x86
  builds do not (no `-mfma`, no `-march`); armhf has no fused multiply-add with the default Debian/Ubuntu FPU (not
  checked).
* Apple clang 14.0.0 did not contract by default in the same test; upstream Clang 14 and later contract within one
  expression, so the NDK r27c and llvm-mingw builds probably do (not checked: no NDK or llvm-mingw here).
* FMA instructions in `planodia_cli.cpp` (which contains the whole search) at `-O2 -std=c++17` for arm64: GCC 13.2
  5 `fmadd` (in `pl::finish()` and in `main`, where `make_periods()` is inlined), Apple clang 14.0.0 none.
* Expressions that can be fused: the period grid `fmin + k * df` (`src/planodia_core.h:105`), the spectrum variance
  (`:225`), the transit time (`:246-247`).

Measured on 2026-10-27 on an Apple M1, 50 synthetic light curves, format 1: `planodia_cli` built with GCC 13.2
(libstdc++, contraction) and with Apple clang 14 (libc++, no contraction), each also with `-ffp-contract=off`: all
four outputs byte-identical. So FMA contraction made no visible difference on this set; real TESS light curves and
format 2 were not tried.

## Fix

Depends on the answers. If CPU and GPU results can meet in one quorum, the validator needs tolerances (PLA-009). If
the validator compares bytes or exact peak periods across CPU platforms, add the tie-break of PLA-011 and measure
`-ffp-contract=off` on aarch64 against x86_64 (PAT-018 "How to fix") before deciding.

## Test

The answers go into this issue. For a measurement: build `planodia_cli` for two platforms (for example Linux x86_64
and aarch64 with GCC, in containers) and compare outputs on the same real shards.

## Log

- 2026-10-21: asked in the audit plan (section 8, validator tolerance).
- 2026-10-27: recorded at 3f6c1b7 with the FMA flag check and the measurements above; no answer yet.
- 2026-10-27: re-checked after PAT-018 was corrected (count FMA instructions): counts added; verdict unchanged.
