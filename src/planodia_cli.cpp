// Standalone Planodia search on one or more shard files (no BOINC); used for tests and benchmarks.
//   planodia_cli [--time] shard1.skt [shard2.skt ...]  > results.txt
#include <chrono>
#include <cstdio>
#include <cstring>
#include "planodia_core.h"
#ifdef PL_OPENCL
#include "planodia_opencl.h"
#endif

int main(int argc, char **argv) {
    bool timing = false, use_gpu = false;
    pl::Params P;
    long done = 0;
    double scan_s = 0, fin_s = 0, prep_s = 0;
    for (int a = 1; a < argc; a++) {
        if (!std::strcmp(argv[a], "--time")) { timing = true; continue; }
        if (!std::strcmp(argv[a], "--gpu")) {
#ifdef PL_OPENCL
            std::string err;
            if (!pl::gpu_init(0, &err)) { std::fprintf(stderr, "GPU init failed: %s\n", err.c_str()); return 1; }
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
            std::vector<double> periods;
            pl::make_periods(pr, P, periods);
            auto t1 = std::chrono::steady_clock::now();
            pl::ScanOut so;
#ifdef PL_OPENCL
            if (use_gpu) { if (!pl::gpu_scan(pr, P, periods, so)) { std::fprintf(stderr, "GPU scan failed\n"); return 1; } }
            else
#endif
            pl::scan_cpu(pr, P, periods, so);
            auto t2 = std::chrono::steady_clock::now();
            pl::Result r = pl::finish(pr, P, periods, so);
            auto t3 = std::chrono::steady_clock::now();
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
