// Reader for the Planodia light-curve shards ("SKT1", written by tools/ingest.py).
//
//   header : magic 'SKT1', u32 version = 1, u32 n_lc, u32 sector
//   record : u64 tic, f32 tmag, f32 ra, f32 dec, f32 t0 (BTJD of cadence offset 0), f32 dt (days per cadence),
//            u32 n, n x u16 cadence offsets, n x f16 (flux / median - 1), 2 bytes of padding when n is odd
//
// Quality-flagged and non-finite points were dropped at ingest, so offsets are not contiguous.
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>

namespace pl {

struct LC {
    uint64_t tic = 0;
    float tmag = 0, ra = 0, dec = 0, t0 = 0, dt = 0;
    std::vector<uint16_t> off;   // cadence offsets
    std::vector<float> x;        // flux / median - 1
};

inline float half_to_float(uint16_t h) {
    uint32_t s = (h >> 15) & 1, e = (h >> 10) & 31, m = h & 1023, f;
    if (e == 0) {
        if (m == 0) f = s << 31;
        else {                                   // subnormal half
            e = 1;
            while (!(m & 1024)) { m <<= 1; e--; }
            m &= 1023;
            f = (s << 31) | ((e + 112) << 23) | (m << 13);
        }
    } else if (e == 31) {
        f = (s << 31) | 0x7f800000 | (m << 13);
    } else {
        f = (s << 31) | ((e + 112) << 23) | (m << 13);
    }
    float r;
    std::memcpy(&r, &f, 4);
    return r;
}

class ShardReader {
public:
    ShardReader() {}
    ~ShardReader() { close(); }
    bool open(const char *path) {
        close();
        f_ = std::fopen(path, "rb");
        if (!f_) return false;
        char magic[4];
        uint32_t hdr[3];
        if (std::fread(magic, 1, 4, f_) != 4 || std::memcmp(magic, "SKT1", 4) != 0 || std::fread(hdr, 4, 3, f_) != 3 || hdr[0] != 1) {
            close();
            return false;
        }
        n_lc_ = hdr[1];
        sector_ = hdr[2];
        read_ = 0;
        return true;
    }
    uint32_t count() const { return n_lc_; }
    uint32_t sector() const { return sector_; }
    // false at the end of the shard or on a truncated/corrupt file
    bool next(LC &lc) {
        if (!f_ || read_ >= n_lc_) return false;
        unsigned char rec[8 + 5 * 4 + 4];
        if (std::fread(rec, 1, sizeof(rec), f_) != sizeof(rec)) return false;
        uint32_t n;
        std::memcpy(&lc.tic, rec, 8);
        std::memcpy(&lc.tmag, rec + 8, 4);
        std::memcpy(&lc.ra, rec + 12, 4);
        std::memcpy(&lc.dec, rec + 16, 4);
        std::memcpy(&lc.t0, rec + 20, 4);
        std::memcpy(&lc.dt, rec + 24, 4);
        std::memcpy(&n, rec + 28, 4);
        if (n < 10 || n > 70000) return false;
        lc.off.resize(n);
        std::vector<uint16_t> h(n);
        if (std::fread(lc.off.data(), 2, n, f_) != n || std::fread(h.data(), 2, n, f_) != n) return false;
        if (n % 2) {
            uint16_t pad;
            if (std::fread(&pad, 2, 1, f_) != 1) return false;
        }
        lc.x.resize(n);
        for (uint32_t i = 0; i < n; i++) lc.x[i] = half_to_float(h[i]);
        read_++;
        return true;
    }
    void close() {
        if (f_) std::fclose(f_);
        f_ = nullptr;
    }
private:
    FILE *f_ = nullptr;
    uint32_t n_lc_ = 0, sector_ = 0, read_ = 0;
};

}  // namespace pl
