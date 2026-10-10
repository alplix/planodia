# Planodia

A BOINC project application that searches NASA TESS full-frame light curves (QLP, MAST) for the repeating dips of
transiting planets. Part of the bitboinc volunteer-computing network (https://bitboinc.athena.org.tr/).

* `tools/ingest.py` downloads a sector's light curves and writes compact shards (format in `src/skt.h`).
* `src/planodia_core.h` is the search: flatten with a running median, binned box-least-squares over a frequency grid,
  noise-floor subtraction, signal detection efficiency (SDE), five strongest peaks per light curve.
* `src/main_planodia_boinc.cpp` the BOINC CPU application; with `-DPL_OPENCL` (`planodia_opencl.cpp`) the same scan runs on
  any OpenCL 1.2 GPU in single precision (NVIDIA, AMD, Intel, Qualcomm Adreno, Apple).
* `src/planodia_cli.cpp` standalone tool (tests, benchmarks); `tools/build_cpu.sh` builds every CPU platform.

Measured on 240 sector-26 light curves (40 of them known TOIs): the C++ search finds 28/40 known planets in its top three
peaks (15/16 with transit depth above 0.6 %), 3 of 200 random stars exceed SDE 9; about 0.08 s per light curve on one CPU
core, 4 ms on an RTX 5070 Ti.
