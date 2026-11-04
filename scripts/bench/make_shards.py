#!/usr/bin/env python3
"""Synthetic light-curve shards in the SKT1 format (src/skt.h) for the speed benchmark in scripts/bench/.

usage: make_shards.py <out.skt> <light curves> <cadence seconds> <seed>
       make_shards.py --version

Why synthetic: the repository holds no real TESS data (tools/ingest.py needs the network and writes to a fixed data
path), and a benchmark needs the same input on every machine. The output depends only on the four arguments. The random
numbers come from an integer generator (SplitMix64) and the model uses only operations that IEEE 754 defines exactly
(+ - * / sqrt floor), no libm function such as sin or pow, so the file is byte-identical on every platform and Python
version. When the model changes, raise GEN_VERSION: run_bench.sh puts it in the shard's file name, so old files are not
reused.

One light curve imitates what src/planodia_core.h receives from a sector of full-frame images, with 27.4 days of
cadences:

* a gap of one day in the middle (the data downlink between the two orbits) and three short gaps (0.1 to 0.5 day,
  momentum dumps and scattered light), so the cadence offsets are not contiguous and the point count varies by light
  curve: about 3,700 points at 600 s, about 11,000 at 200 s;
* white noise of 0.05 % to 1.5 % (brighter stars have less) and two slow variations of a few tenths of a percent, which
  the running median of prepare() has to remove;
* a few upward outliers of 6 to 10 sigma, which prepare() drops;
* a box transit in every third light curve (period 0.8 to 11.8 days, depth 0.3 % to 1.5 %), a second one half as deep in
  every sixth, so the result lines contain real peaks as well as noise peaks, and the multi-pass search of format 2 has a
  second planet to find.

The noise is the sum of twelve 16-bit uniform numbers (nearly normal, no tail beyond 6 sigma). The point is not
astrophysical realism but work that matches the real thing: the number of points, the period grid and the bin counts
decide the run time, and the values decide only which peaks are reported.
"""
import math
import os
import struct
import sys

GEN_VERSION = 1
SECTOR_DAYS = 27.4
MASK = (1 << 64) - 1


class Rng:
    """SplitMix64: integer arithmetic only, so a seed gives the same stream everywhere."""

    def __init__(self, seed):
        self.s = seed & MASK

    def next(self):
        self.s = (self.s + 0x9E3779B97F4A7C15) & MASK
        z = self.s
        z = ((z ^ (z >> 30)) * 0xBF58476D1CE4E5B9) & MASK
        z = ((z ^ (z >> 27)) * 0x94D049BB133111EB) & MASK
        return z ^ (z >> 31)

    def uniform(self):
        # 53 random bits over 2^53: both exact in a double
        return (self.next() >> 11) / 9007199254740992.0

    def normal(self):
        # twelve 16-bit uniforms from three 64-bit draws; the sum is an integer, its mean is 12 * 32767.5 = 393210 and its
        # standard deviation 65536 (to within 1e-6 relative), so one exact division by 2^16 gives mean 0 and standard
        # deviation 1
        s = 0
        for _ in range(3):
            r = self.next()
            s += (r & 0xFFFF) + ((r >> 16) & 0xFFFF) + ((r >> 32) & 0xFFFF) + (r >> 48)
        return (s - 393210) / 65536.0


def f32(x):
    # the shard stores dt as f32, and the search computes its times from that value
    return struct.unpack('<f', struct.pack('<f', x))[0]


def arch(x):
    # periodic parabola with mean zero: a slow variation without sin(), whose last digit differs between libms
    u = x - math.floor(x)
    return 8.0 * u * (1.0 - u) - 4.0 / 3.0


def make_lc(seed, index, cad_s):
    """One record of the shard: (header fields, cadence offsets, f16 flux) packed as in src/skt.h."""
    rng = Rng(seed * 1000003 + index)
    dt = f32(cad_s / 86400.0)
    tmag = 9.0 + 6.0 * rng.uniform()
    ra = 360.0 * rng.uniform()
    dec = 180.0 * rng.uniform() - 90.0
    d = tmag - 9.0
    sigma = (0.0005 + 0.0004 * d * d) * math.sqrt(cad_s / 600.0)

    gaps = [(13.0, 14.0)]
    for _ in range(3):
        lo = 0.5 + 25.0 * rng.uniform()        # not before 0.5 day: cadence offset 0 must be present (skt.h, ingest.py)
        gaps.append((lo, lo + 0.1 + 0.4 * rng.uniform()))
    offs, times = [], []
    for c in range(int(SECTOR_DAYS * 86400.0 / cad_s)):
        t = c * dt
        if any(lo <= t < hi for lo, hi in gaps):
            continue
        offs.append(c)
        times.append(t)
    n = len(offs)

    a1 = 0.0005 + 0.004 * rng.uniform()
    p1 = 2.5 + 8.0 * rng.uniform()
    f1 = rng.uniform()
    a2 = 0.0003 + 0.002 * rng.uniform()
    p2 = 0.7 + 2.0 * rng.uniform()
    f2 = rng.uniform()

    nplanets = 2 if index % 6 == 0 else (1 if index % 3 == 0 else 0)
    planets = []
    for j in range(nplanets):
        period = 0.8 + 11.0 * rng.uniform()
        dur = 0.05 + 0.02 * period
        depth = (0.003 + 0.012 * rng.uniform()) / (1.0 + j)
        epoch = period * rng.uniform()
        planets.append((period, dur, depth, epoch))

    spikes = {}
    for _ in range(n // 400):
        k = rng.next() % n
        spikes[k] = 6.0 + 4.0 * rng.uniform()

    flux = []
    for i in range(n):
        t = times[i]
        x = sigma * rng.normal() + a1 * arch(t / p1 + f1) + a2 * arch(t / p2 + f2)
        for period, dur, depth, epoch in planets:
            ph = (t - epoch) / period
            if (ph - math.floor(ph)) * period < dur:
                x -= depth
        if i in spikes:
            x += spikes[i] * sigma
        flux.append(x)

    tic = 300000000 + seed * 10000 + index
    # record layout of src/skt.h: u64 tic, f32 tmag, ra, dec, t0, dt, u32 n, n x u16 offsets, n x f16, pad to 4 bytes
    rec = struct.pack('<Q5fI', tic, tmag, ra, dec, 2036.5, dt, n)
    rec += struct.pack('<%dH' % n, *offs) + struct.pack('<%de' % n, *flux)
    if n % 2:
        rec += b'\0\0'
    return rec


def main(argv):
    if len(argv) == 2 and argv[1] == '--version':
        print(GEN_VERSION)
        return 0
    if len(argv) != 5:
        print(__doc__)
        return 2
    path = argv[1]
    nlc, cad_s, seed = int(argv[2]), int(argv[3]), int(argv[4])
    # at 60 s the 27.4 days still fit the u16 cadence offsets of the format (65535) and the reader's limit of 70000 points
    if not (1 <= nlc <= 9999 and 60 <= cad_s <= 1800 and seed >= 0):
        print('need 1 <= light curves <= 9999, 60 <= cadence seconds <= 1800, seed >= 0')
        return 2
    tmp = path + '.tmp'
    with open(tmp, 'wb') as f:
        f.write(b'SKT1' + struct.pack('<III', 1, nlc, 27))
        for i in range(nlc):
            f.write(make_lc(seed, i, cad_s))
    os.replace(tmp, path)     # a half-written shard is never mistaken for a finished one
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
