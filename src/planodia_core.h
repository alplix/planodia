// Planodia core: light-curve preparation, the binned BLS (box least squares) scan, peak picking and the result line.
// The scan itself (scan_cpu here, planodia_opencl.cpp on a GPU) is the only part that differs between backends;
// everything else is shared so CPU and GPU results agree to float rounding.
//
// Search (all numbers are the defaults of Params):
//  1. flux = 1 + x; divide by a running median of about `flatten_days` (index window), take the robust scatter
//     sigma = 1.4826 * MAD, drop upward outliers beyond 5 sigma.
//  2. periods: uniform in frequency, df = freq_factor * dmin / baseline^2, from 1/pmax to 1/pmin.
//  3. for every period fold the points into bins of `binw` days of phase (nb = ceil(P / binw)), then for every
//     box duration (in bins) and every start bin compute the dip significance
//         delta = mean(out) - mean(in),   snr = delta / sigma * sqrt(n_in * n_out / N)      (dips only)
//     and keep the best box per period.
//  4. the power spectrum minus its running median (301 periods) gives the signal detection efficiency
//     SDE = (peak - mean) / std; the five strongest peaks, at least 2 % apart in period, are reported.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include "skt.h"

namespace pl {

struct Params {
    double pmin = 0.5, pmax = 14.0, freq_factor = 1.5, flatten_days = 1.0;
    double binw = 0.02;                       // phase bin width in days
    int ndur = 4;
    double durs[4] = {0.04, 0.08, 0.16, 0.30};
    int topn = 5;
    int max_periods = 80000;
};

// the prepared light curve
struct Prep {
    std::vector<double> t, y;                 // t in days since the first point, y = flattened flux - 1
    double sigma = 0, baseline = 0;
    uint64_t tic = 0;
    float tmag = 0, t0 = 0;
};

inline double median_of(std::vector<double> &v) {
    size_t k = v.size() / 2;
    std::nth_element(v.begin(), v.begin() + k, v.end());
    return v[k];
}

// returns false when the light curve is unusable
inline bool prepare(const LC &lc, const Params &P, Prep &out) {
    size_t n = lc.off.size();
    if (n < 200 || lc.dt <= 0) return false;
    std::vector<double> f(n), tt(n);
    for (size_t i = 0; i < n; i++) {
        f[i] = 1.0 + lc.x[i];
        tt[i] = (double)lc.off[i] * lc.dt;
    }
    int w = (int)(P.flatten_days / lc.dt);
    if (w < 5) w = 5;
    w |= 1;
    int h = w / 2;
    std::vector<double> flat(n), win;
    win.reserve(w);
    for (size_t i = 0; i < n; i++) {
        long lo = (long)i - h, hi = (long)i + h;
        if (lo < 0) lo = 0;
        if (hi > (long)n - 1) hi = (long)n - 1;
        win.assign(f.begin() + lo, f.begin() + hi + 1);
        double m = median_of(win);
        flat[i] = m > 0 ? f[i] / m - 1.0 : 0.0;
    }
    std::vector<double> tmp = flat;
    double med = median_of(tmp);
    for (size_t i = 0; i < n; i++) tmp[i] = std::fabs(flat[i] - med);
    double sigma = 1.4826 * median_of(tmp);
    if (!(sigma > 0)) return false;
    out.t.clear();
    out.y.clear();
    for (size_t i = 0; i < n; i++) {
        if (flat[i] < 5 * sigma) {                // drop upward outliers only (transits are dips)
            out.t.push_back(tt[i] - tt[0]);
            out.y.push_back(flat[i]);
        }
    }
    if (out.t.size() < 200) return false;
    out.sigma = sigma;
    out.baseline = out.t.back() - out.t.front();
    out.tic = lc.tic;
    out.tmag = lc.tmag;
    out.t0 = lc.t0;
    return out.baseline > 3.0 * P.pmin;
}

inline void make_periods(const Prep &p, const Params &P, std::vector<double> &periods) {
    double T = p.baseline;
    double df = P.freq_factor * P.durs[0] / (T * T);
    double fmin = 1.0 / P.pmax, fmax = 1.0 / P.pmin;
    long nf = (long)((fmax - fmin) / df) + 1;
    if (nf > P.max_periods) nf = P.max_periods;
    periods.resize(nf);
    for (long k = 0; k < nf; k++) periods[k] = 1.0 / (fmin + k * df);
}

inline int dur_bins(const Params &P, int j) {
    int m = (int)std::lround(P.durs[j] / P.binw);
    return m < 1 ? 1 : m;
}

struct ScanOut {
    std::vector<float> power;                 // best dip significance per period
    std::vector<int> bj, bb;                  // best duration index and start bin per period
};

// CPU reference scan (double precision). The OpenCL kernel computes exactly the same thing in float.
inline void scan_cpu(const Prep &p, const Params &P, const std::vector<double> &periods, ScanOut &o) {
    size_t np = periods.size(), n = p.t.size();
    o.power.assign(np, 0.f);
    o.bj.assign(np, 0);
    o.bb.assign(np, 0);
    std::vector<double> cnt, sum;
    double N = (double)n;
    for (size_t k = 0; k < np; k++) {
        double per = periods[k];
        int nb = (int)std::ceil(per / P.binw);
        cnt.assign(nb, 0.0);
        sum.assign(nb, 0.0);
        for (size_t i = 0; i < n; i++) {
            double ph = p.t[i] / per;
            ph -= std::floor(ph);
            int b = (int)(ph * nb);
            if (b >= nb) b = nb - 1;
            cnt[b] += 1.0;
            sum[b] += p.y[i];
        }
        for (int b = 1; b < nb; b++) {            // inclusive prefix sums
            cnt[b] += cnt[b - 1];
            sum[b] += sum[b - 1];
        }
        double ctot = cnt[nb - 1], stot = sum[nb - 1];
        double best = 0;
        int bestj = 0, bestb = 0;
        for (int j = 0; j < P.ndur; j++) {
            int m = dur_bins(P, j);
            for (int b = 0; b < nb; b++) {
                int e = b + m - 1;
                double cin, sin_;
                double c0 = b > 0 ? cnt[b - 1] : 0.0, s0 = b > 0 ? sum[b - 1] : 0.0;
                if (e < nb) {
                    cin = cnt[e] - c0;
                    sin_ = sum[e] - s0;
                } else {
                    cin = (ctot - c0) + cnt[e - nb];
                    sin_ = (stot - s0) + sum[e - nb];
                }
                double cout = N - cin;
                if (cin < 3 || cout < 10) continue;
                double delta = (stot - sin_) / cout - sin_ / cin;   // out - in: positive for a dip
                if (delta <= 0) continue;
                double snr = delta / p.sigma * std::sqrt(cin * cout / N);
                if (snr > best) {
                    best = snr;
                    bestj = j;
                    bestb = b;
                }
            }
        }
        o.power[k] = (float)best;
        o.bj[k] = bestj;
        o.bb[k] = bestb;
    }
}

struct Peak {
    double period = 0, dur = 0, depth_ppm = 0, snr = 0, tc = 0;
    int ntr = 0;
};

struct Result {
    uint64_t tic = 0;
    float tmag = 0;
    int npts = 0;
    double sde = 0;
    std::vector<Peak> peaks;
};

// noise floor: the running median of the power spectrum over `win` periods (edge values repeated).
// Sorted-window implementation: O(n * win) memmoves instead of a selection per period.
inline void floor_subtract(const std::vector<float> &pw, std::vector<double> &out, int win = 301) {
    long n = (long)pw.size();
    out.resize(n);
    int h = win / 2;
    auto at = [&](long j) { return pw[j < 0 ? 0 : (j >= n ? n - 1 : j)]; };
    std::vector<float> w;
    w.reserve(win);
    for (long j = -h; j <= h; j++) w.push_back(at(j));
    std::sort(w.begin(), w.end());
    for (long i = 0; i < n; i++) {
        out[i] = pw[i] - w[win / 2];
        if (i + 1 < n) {
            float gone = at(i - h), came = at(i + 1 + h);
            if (gone != came) {
                w.erase(std::lower_bound(w.begin(), w.end(), gone));
                w.insert(std::upper_bound(w.begin(), w.end(), came), came);
            }
        }
    }
}

inline Result finish(const Prep &p, const Params &P, const std::vector<double> &periods, const ScanOut &o) {
    Result r;
    r.tic = p.tic;
    r.tmag = p.tmag;
    r.npts = (int)p.t.size();
    size_t np = periods.size();
    std::vector<double> d;
    floor_subtract(o.power, d);
    double mean = 0;
    for (double v : d) mean += v;
    mean /= (double)np;
    double var = 0;
    for (double v : d) var += (v - mean) * (v - mean);
    double sd = std::sqrt(var / (double)np);
    std::vector<size_t> idx(np);
    for (size_t i = 0; i < np; i++) idx[i] = i;
    size_t keep = std::min<size_t>(np, 4000);        // the top of the spectrum is enough to find 5 separate peaks
    std::partial_sort(idx.begin(), idx.begin() + keep, idx.end(), [&](size_t a, size_t b) { return d[a] > d[b]; });
    r.sde = sd > 0 ? (d[idx[0]] - mean) / sd : 0;
    for (size_t q = 0; q < keep && (int)r.peaks.size() < P.topn; q++) {
        size_t k = idx[q];
        double per = periods[k];
        bool sep = true;
        for (const Peak &pk : r.peaks) if (std::fabs(per - pk.period) / pk.period < 0.02) { sep = false; break; }
        if (!sep) continue;
        Peak pk;
        pk.period = per;
        int nb = (int)std::ceil(per / P.binw);
        int m = dur_bins(P, o.bj[k]);
        int b0 = o.bb[k];
        double bw_t = per / nb;                      // width of a phase bin in days
        pk.dur = m * bw_t;
        pk.snr = o.power[k];
        double tc = (b0 + 0.5 * m) * bw_t;           // transit centre in the first cycle (days since first point)
        pk.tc = (double)p.t0 + tc;
        // depth and number of cycles with in-transit points
        double sin_ = 0, cin = 0, stot = 0;
        std::vector<char> seen;
        long maxc = (long)(p.baseline / per) + 3;
        seen.assign(maxc, 0);
        for (size_t i = 0; i < p.t.size(); i++) {
            double ph = p.t[i] / per;
            ph -= std::floor(ph);
            int b = (int)(ph * nb);
            if (b >= nb) b = nb - 1;
            int rel = b - b0;
            if (rel < 0) rel += nb;
            stot += p.y[i];
            if (rel < m) {
                sin_ += p.y[i];
                cin += 1;
                long cyc = (long)std::floor(p.t[i] / per);
                if (cyc >= 0 && cyc < maxc) seen[cyc] = 1;
            }
        }
        double cout = (double)p.t.size() - cin;
        pk.depth_ppm = (cin > 0 && cout > 0) ? ((stot - sin_) / cout - sin_ / cin) * 1e6 : 0;
        for (char c : seen) pk.ntr += c ? 1 : 0;
        r.peaks.push_back(pk);
    }
    return r;
}

// one text line per light curve:  tic tmag npts sde npeaks  then per peak: period dur depth_ppm snr tc ntr
inline std::string format_result(const Result &r) {
    char buf[256];
    std::string s;
    std::snprintf(buf, sizeof(buf), "%llu %.3f %d %.2f %d", (unsigned long long)r.tic, r.tmag, r.npts, r.sde, (int)r.peaks.size());
    s += buf;
    for (const Peak &p : r.peaks) {
        std::snprintf(buf, sizeof(buf), " %.5f %.3f %.0f %.2f %.4f %d", p.period, p.dur, p.depth_ppm, p.snr, p.tc, p.ntr);
        s += buf;
    }
    return s;
}

// "skipped" line for a light curve that cannot be searched (keeps one output line per input light curve)
inline std::string format_skipped(uint64_t tic) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%llu 0 0 0 0", (unsigned long long)tic);
    return buf;
}

}  // namespace pl
