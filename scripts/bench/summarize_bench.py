#!/usr/bin/env python3
"""Summary of a run_bench.sh results.tsv: per label the median, minimum and maximum wall time, CPU time, memory, GPU
use, speed relative to the first label, the verdict of the acceptance rule below, and whether the label's results are
the same as the first label's (or as an earlier label's: the OpenCL scan differs from the CPU scan at rounding level,
PLA-009, so a CPU and an OpenCL label are best run in separate calls, each with its own first label).

Acceptance rule (as in docs/BENCHMARK.md): a candidate is faster when its median is at least 5 % below the first
label's and its slowest run is faster than the first label's fastest, or when its median is at least 10 % below. With
fewer than 3 runs of either label the verdict is "too few runs".

When the output differs, the table gives the change of the sum of the SDE column over all light curves (the key
number: every light curve contributes) and the largest number of result lines that differ from the first label's first
run. The hash only says "equal or not"; a quorum-2 validator cares how much differs (PLA-010).

usage: summarize_bench.py <results.tsv>
"""
import statistics, sys


def main(path):
    runs, order = {}, []
    for line in open(path):
        if line.startswith("#") or not line.strip():
            continue
        f = line.rstrip("\n").split("\t")
        label = f[0]
        if label not in runs:
            runs[label] = []
            order.append(label)
        runs[label].append(dict(wall=float(f[2] or "nan"), user=float(f[3] or "nan"), sys=float(f[4] or "nan"),
                                rss=f[5], code=f[6], hash=f[7], gpu=f[8], key=f[9],
                                ndiff=f[10] if len(f) > 10 else "-"))
    if not order:
        print("no runs")
        return
    ref = order[0]
    ref_hashes = {r["hash"] for r in runs[ref]}
    ref_key = [float(r["key"]) for r in runs[ref] if r["key"] not in ("-", "")]
    rmed = statistics.median(r["wall"] for r in runs[ref])
    rmin = min(r["wall"] for r in runs[ref])
    print("%-12s %4s %9s %8s %8s %8s %7s %5s  %7s  %s" % ("label", "runs", "median s", "min s", "max s", "cpu s",
                                                         "RSS MB", "GPU%", "vs 1st", "verdict / output"))
    for lb in order:
        rs = runs[lb]
        walls = [r["wall"] for r in rs]
        med, lo, hi = statistics.median(walls), min(walls), max(walls)
        cpu = statistics.median(r["user"] + r["sys"] for r in rs)
        rss = statistics.median(float(r["rss"] or 0) for r in rs)
        gpus = [float(r["gpu"]) for r in rs if r["gpu"] not in ("-", "")]
        gpu = "%.0f" % statistics.median(gpus) if gpus else "-"
        bad = [r["code"] for r in rs if r["code"] != "0"]
        hashes = {r["hash"] for r in rs}
        if bad:
            out = "FAILED (exit %s)" % ",".join(sorted(set(bad)))
        elif len(hashes) > 1:
            out = "results differ between its own runs"
        elif hashes == ref_hashes:
            out = "same output"
        elif any({r["hash"] for r in runs[o]} == hashes for o in order[:order.index(lb)]):
            # e.g. an OpenCL build listed after CPU builds: compare like with like
            out = "same output as " + next(o for o in order if {r["hash"] for r in runs[o]} == hashes)
        else:
            k = [float(r["key"]) for r in rs if r["key"] not in ("-", "")]
            d = (statistics.median(k) - statistics.median(ref_key)) if k and ref_key else float("nan")
            nd = [int(r["ndiff"]) for r in rs if r["ndiff"] not in ("-", "")]
            out = "OTHER output (SDE sum %+.2f, %s result lines differ)" % (d, max(nd) if nd else "?")
        if lb == ref:
            verdict = "baseline"
        else:
            gain = 1 - med / rmed
            if min(len(rs), len(runs[ref])) < 3:
                # one or two runs say nothing about the spread, so the rule cannot be applied
                verdict = "too few runs (%+.1f %%)" % (100 * gain)
            elif (gain >= 0.05 and hi < rmin) or gain >= 0.10:
                verdict = "ACCEPT (%+.1f %%)" % (100 * gain)
            elif gain <= -0.05:
                verdict = "slower (%+.1f %%)" % (100 * gain)
            else:
                verdict = "no clear gain (%+.1f %%)" % (100 * gain)
        print("%-12s %4d %9.2f %8.2f %8.2f %8.1f %7.0f %5s  %6.3fx  %s; %s" % (lb, len(rs), med, lo, hi, cpu, rss, gpu,
                                                                                rmed / med, verdict, out))


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(__doc__)
        sys.exit(2)
    main(sys.argv[1])
