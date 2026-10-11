// Planodia (transit search in TESS light curves) as a BOINC application, CPU backend.
//
// Work unit files:
//   in      one text file:  line 1 "planodia 1" or "planodia 2"
//                           line 2 "<pmin> <pmax> <freq_factor> <flatten_days> <nshards>"
//                           line 3 (format 2 only, the finer "deep" search):
//                                  "<binw> <ndur> <dur_1> ... <dur_ndur> <max_periods> <passes> <floor_win>"
//                                  (days; ndur <= 8; passes 1..4 = multi-planet search, see planodia_core.h)
//   shard0 .. shard<nshards-1>   light-curve shards (skt.h), a few hundred light curves each
// Result file "out": a header line "# planodia 1 <nlc> light curves" then one line per light curve
// (planodia_core.h format_result). One line per light curve is written and flushed as soon as it is done; the
// checkpoint is the number of finished light curves, so a restart resumes exactly where it stopped.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "boinc_api.h"
#include "filesys.h"
#include "planodia_core.h"
#ifdef PL_OPENCL
#include "planodia_opencl.h"
#endif

#ifdef PL_OPENCL
#define PLANODIA_VERSION "1-opencl"
#else
#define PLANODIA_VERSION "1"
#endif
static const char *CKPT = "planodia_checkpoint";

static long read_checkpoint() {
    char p[512];
    if (boinc_resolve_filename(CKPT, p, sizeof(p))) return 0;
    FILE *f = boinc_fopen(p, "r");
    if (!f) return 0;
    long c = 0;
    if (fscanf(f, "%ld", &c) != 1) c = 0;
    fclose(f);
    return c < 0 ? 0 : c;
}

static void write_checkpoint(long c) {
    char p[512];
    if (boinc_resolve_filename(CKPT, p, sizeof(p))) return;
    std::string tmp = std::string(p) + ".tmp";
    FILE *f = boinc_fopen(tmp.c_str(), "w");
    if (!f) return;
    fprintf(f, "%ld\n", c);
    fclose(f);
    boinc_rename(tmp.c_str(), p);
}

// Keep the header line and the first `keep` data lines of an existing result file (a torn last line is dropped).
static bool trim_output(const char *path, long keep) {
    FILE *f = boinc_fopen(path, "rb");
    if (!f) return false;
    std::string content, kept;
    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) content.append(buf, n);
    fclose(f);
    if (content.compare(0, 11, "# planodia ") != 0) return false;
    size_t pos = 0;
    long lines = 0;
    bool header = true;
    while (pos < content.size()) {
        size_t eol = content.find('\n', pos);
        if (eol == std::string::npos) break;
        if (header) { kept += content.substr(pos, eol - pos + 1); header = false; }
        else if (lines < keep) { kept += content.substr(pos, eol - pos + 1); lines++; }
        pos = eol + 1;
    }
    if (lines < keep) return false;
    f = boinc_fopen(path, "wb");
    if (!f) return false;
    fwrite(kept.data(), 1, kept.size(), f);
    fclose(f);
    return true;
}

int main(int argc, char **argv) {
    int device_id = 0;
    for (int a = 1; a + 1 < argc; a++) if (!strcmp(argv[a], "--device")) device_id = atoi(argv[a + 1]);
    fprintf(stderr, "Planodia (transit search in TESS light curves) v" PLANODIA_VERSION "\n");
    fprintf(stderr, "bitboinc -- https://bitboinc.athena.org.tr/\n");

    BOINC_OPTIONS options;
    boinc_options_defaults(options);
    options.main_program = true;
    options.check_heartbeat = true;
    options.handle_process_control = true;
    int rc = boinc_init_options(&options);
    if (rc) { fprintf(stderr, "boinc_init_options failed: %d\n", rc); return rc; }

    char path[512];
    if (boinc_resolve_filename("in", path, sizeof(path))) { fprintf(stderr, "can't resolve input file 'in'\n"); boinc_finish(1); return 1; }
    FILE *fin = boinc_fopen(path, "r");
    if (!fin) { fprintf(stderr, "can't open %s\n", path); boinc_finish(1); return 1; }
    char magic[32];
    int version = 0, nshards = 0;
    pl::Params P;
    bool ok = fscanf(fin, "%31s %d", magic, &version) == 2 && strcmp(magic, "planodia") == 0 && (version == 1 || version == 2) &&
              fscanf(fin, "%lf %lf %lf %lf %d", &P.pmin, &P.pmax, &P.freq_factor, &P.flatten_days, &nshards) == 5 &&
              nshards >= 1 && nshards <= 64 && P.pmin > 0.1 && P.pmax > P.pmin && P.pmax <= 60 && P.flatten_days >= 0.2 && P.freq_factor >= 0.05;
    if (ok && version == 2) {
        int nd = 0;
        ok = fscanf(fin, "%lf %d", &P.binw, &nd) == 2 && P.binw >= 0.001 && P.binw <= 0.1 && nd >= 1 && nd <= PL_MAXDUR;
        for (int j = 0; ok && j < nd; j++)
            ok = fscanf(fin, "%lf", &P.durs[j]) == 1 && P.durs[j] >= P.binw && P.durs[j] <= 1.0 && (j == 0 || P.durs[j] > P.durs[j - 1]);
        if (ok) {
            P.ndur = nd;
            ok = fscanf(fin, "%d %d %d", &P.max_periods, &P.passes, &P.floor_win) == 3 && P.max_periods >= 1000 && P.max_periods <= 400000 &&
                 P.passes >= 1 && P.passes <= 4 && P.floor_win >= 101 && P.floor_win <= 2001 && (P.floor_win & 1);
            if (ok && P.passes > 1) P.topn = 5 - (P.passes - 1) > 1 ? 5 - (P.passes - 1) : 1;
        }
    }
    if (!ok) {
        fprintf(stderr, "malformed input file\n");
        fclose(fin);
        boinc_finish(1);
        return 1;
    }
    fclose(fin);
#ifdef PL_OPENCL
    {
        std::string err;
        if (!pl::gpu_init(device_id, (int)std::ceil(P.pmax / P.binw), &err)) { fprintf(stderr, "no usable GPU: %s\n", err.c_str()); boinc_finish(1); return 1; }
        fprintf(stderr, "planodia_opencl: using OpenCL device \"%s\"\n", pl::gpu_device_name());
    }
#else
    (void)device_id;
#endif

    // total number of light curves, for progress and for the header
    std::vector<std::string> shard_paths;
    long total = 0;
    for (int s = 0; s < nshards; s++) {
        char name[32];
        snprintf(name, sizeof(name), "shard%d", s);
        char sp[512];
        if (boinc_resolve_filename(name, sp, sizeof(sp))) { fprintf(stderr, "can't resolve %s\n", name); boinc_finish(1); return 1; }
        pl::ShardReader rd;
        if (!rd.open(sp)) { fprintf(stderr, "can't read %s\n", sp); boinc_finish(1); return 1; }
        total += rd.count();
        shard_paths.push_back(sp);
    }

    char outp[512];
    if (boinc_resolve_filename("out", outp, sizeof(outp))) { fprintf(stderr, "can't resolve 'out'\n"); boinc_finish(1); return 1; }
    long done = read_checkpoint();
    FILE *fout = nullptr;
    if (done > 0 && trim_output(outp, done)) {
        fprintf(stderr, "resuming after %ld of %ld light curves\n", done, total);
        fout = boinc_fopen(outp, "ab");
    } else {
        done = 0;
        fout = boinc_fopen(outp, "wb");
        if (fout) fprintf(fout, "# planodia 1 %ld light curves\n", total);   // result FORMAT version 1 (not the app version)
    }
    if (!fout) { fprintf(stderr, "can't open output\n"); boinc_finish(1); return 1; }
    fflush(fout);

    long idx = 0;
    for (const std::string &sp : shard_paths) {
        pl::ShardReader rd;
        if (!rd.open(sp.c_str())) { fprintf(stderr, "can't re-read %s\n", sp.c_str()); fclose(fout); boinc_finish(1); return 1; }
        pl::LC lc;
        while (rd.next(lc)) {
            if (idx < done) { idx++; continue; }
            pl::Prep pr;
            std::string line;
            if (!pl::prepare(lc, P, pr)) {
                line = pl::format_skipped(lc.tic);
            } else {
                pl::Result res;
                auto scan = [&P](const pl::Prep &q, const std::vector<double> &per, pl::ScanOut &so) {
#ifdef PL_OPENCL
                    return pl::gpu_scan(q, P, per, so);
#else
                    pl::scan_cpu(q, P, per, so);
                    return true;
#endif
                };
                if (!pl::search_passes(pr, P, scan, res)) { fprintf(stderr, "GPU scan failed\n"); fclose(fout); boinc_finish(1); return 1; }
                line = pl::format_result(res);
            }
            fprintf(fout, "%s\n", line.c_str());
            fflush(fout);
            idx++;
            boinc_fraction_done((double)idx / (double)(total ? total : 1));
            if (idx % 20 == 0 || boinc_time_to_checkpoint()) {   // the checkpoint is a few bytes: write it often
                write_checkpoint(idx);
                if (boinc_time_to_checkpoint()) boinc_checkpoint_completed();
            }
        }
    }
    fclose(fout);
    if (idx != total) { fprintf(stderr, "read %ld light curves, expected %ld (truncated shard?)\n", idx, total); boinc_finish(1); return 1; }
    write_checkpoint(idx);
    fprintf(stderr, "done: %ld light curves\n", idx);
    boinc_finish(0);
    return 0;
}
