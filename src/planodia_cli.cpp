// Standalone Planodia search on one or more shard files (no BOINC); used for tests and benchmarks.
//   planodia_cli [--time] shard1.skt [shard2.skt ...]  > results.txt
#include <chrono>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include "planodia_core.h"
#ifdef PL_OPENCL
#include "planodia_opencl.h"
#endif

int main(int argc, char **argv) {
    bool timing = false, use_gpu = false;
    pl::Params P;
    // experiment overrides (not used by the apps): PL_FLATTEN days, PL_FF frequency factor, PL_BINW days, PL_DURS "d1,d2,d3,d4"
    if (const char *e = std::getenv("PL_FLATTEN")) P.flatten_days = std::atof(e);
    if (const char *e = std::getenv("PL_FF")) P.freq_factor = std::atof(e);
    if (const char *e = std::getenv("PL_BINW")) P.binw = std::atof(e);
    if (const char *e = std::getenv("PL_DURS")) {   // "d1,d2,..." up to 8 box durations in days
        int k = 0;
        for (const char *q = e; *q && k < PL_MAXDUR; k++) { P.durs[k] = std::atof(q); const char *c = std::strchr(q, ','); if (!c) { k++; break; } q = c + 1; }
        P.ndur = k;
    }
    if (const char *e = std::getenv("PL_PMIN")) P.pmin = std::atof(e);
    if (const char *e = std::getenv("PL_PMAX")) P.pmax = std::atof(e);
    if (const char *e = std::getenv("PL_MAXP")) P.max_periods = std::atoi(e);
    if (const char *e = std::getenv("PL_PASSES")) { P.passes = std::atoi(e); if (P.passes > 1) P.topn = 5 - (P.passes - 1); }
    if (const char *e = std::getenv("PL_FLOORWIN")) P.floor_win = std::atoi(e) | 1;
    long done = 0;
    double scan_s = 0, fin_s = 0, prep_s = 0;
    for (int a = 1; a < argc; a++) {
        if (!std::strcmp(argv[a], "--time")) { timing = true; continue; }
        if (!std::strcmp(argv[a], "--gpu")) {
#ifdef PL_OPENCL
            std::string err;
            if (!pl::gpu_init(0, (int)std::ceil(P.pmax / P.binw), &err)) { std::fprintf(stderr, "GPU init failed: %s\n", err.c_str()); return 1; }
            std::fprintf(stderr, "device: %s\n", pl::gpu_device_name());
            use_gpu = true;
#else
            std::fprintf(stderr, "built without OpenCL\n"); return 1;
#endif
            continue;
        }
        pl::ShardReader rd;
        if (!rd.open(argv[a])) { std::fprintf(stderr, "cannot read %s\n", argv[a]); return 1; }
        pl::LC lc;
        while (rd.next(lc)) {
            auto t0 = std::chrono::steady_clock::now();
            pl::Prep pr;
            if (!pl::prepare(lc, P, pr)) { std::puts(pl::format_skipped(lc.tic).c_str()); done++; continue; }
            auto t1 = std::chrono::steady_clock::now();
            pl::Result r;
            auto scan = [&](const pl::Prep &q, const std::vector<double> &per, pl::ScanOut &so) {
#ifdef PL_OPENCL
                if (use_gpu) return pl::gpu_scan(q, P, per, so);
#endif
                pl::scan_cpu(q, P, per, so);
                return true;
            };
            if (!pl::search_passes(pr, P, scan, r)) { std::fprintf(stderr, "GPU scan failed\n"); return 1; }
            auto t2 = std::chrono::steady_clock::now();
            auto t3 = t2;
            std::puts(pl::format_result(r).c_str());
            prep_s += std::chrono::duration<double>(t1 - t0).count();
            scan_s += std::chrono::duration<double>(t2 - t1).count();
            fin_s += std::chrono::duration<double>(t3 - t2).count();
            done++;
        }
    }
    if (timing && done) std::fprintf(stderr, "%ld light curves: prepare %.3f s, scan %.3f s, finish %.3f s per light curve\n", done, prep_s / done, scan_s / done, fin_s / done);
    return 0;
}
