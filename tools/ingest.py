#!/usr/bin/env python3
"""TESS QLP ingest for the planned transit project (see memory tess-transit-project).

For each sector (27..55 by default): fetch the QLP target list, download the light curve FITS files in small parallel
batches (MAST API), keep only what a transit search needs and write compact shards of SHARD_N light curves:

    /mnt/aidata/tess/data/s0027/shard_00000.skt ...   (+ s0027/DONE when the sector is complete)

Shard format "SKT1" (little endian), documented in tess_format.md next to the data:
    header : magic 'SKT1', u32 version=1, u32 n_lc, u32 sector
    record : u64 tic, f32 tmag, f32 ra, f32 dec, f32 t0 (BTJD of cadence offset 0), f32 dt (days per cadence),
             u32 n, n x u16 cadence offsets, n x f16 (SAP flux / median - 1) , 2 bytes pad when n is odd
Quality-flagged and non-finite points are dropped. FITS are deleted after conversion.

Safety: resumable (finished shards are skipped), stops when the data disk has less than MIN_FREE_GB free, pauses while
the load average is high, fixed small parallelism (WORKERS), one process, nice/ionice are applied by the caller.
"""
import csv, os, struct, sys, time, json, io, shutil, urllib.request, urllib.error
from concurrent.futures import ThreadPoolExecutor
import numpy as np
from astropy.io import fits

ROOT = "/mnt/aidata/tess"
DATA = ROOT + "/data"
TMP = ROOT + "/tmp"
STATUS = ROOT + "/status.json"
LOG = ROOT + "/ingest.log"
SECTORS = [int(x) for x in os.environ.get("SECTORS", ",".join(str(s) for s in range(27, 56))).split(",")]
WORKERS = int(os.environ.get("WORKERS", "10"))
SHARD_N = 250
MIN_FREE_GB = 120
MAX_LOAD = 7.0
BASE = "https://mast.stsci.edu/api/v0.1/Download/file/?uri=mast:HLSP/qlp/"
LISTS = "https://archive.stsci.edu/hlsps/qlp/target_lists/s%04d.csv"

def log(msg):
    with open(LOG, "a") as f:
        f.write(time.strftime("%Y-%m-%d %H:%M:%S ") + msg + "\n")

def free_gb():
    st = os.statvfs(DATA)
    return st.f_bavail * st.f_frsize / 1e9

def wait_if_busy():
    while True:
        if free_gb() < MIN_FREE_GB:
            log("STOP: less than %d GB free" % MIN_FREE_GB)
            sys.exit(3)
        if os.getloadavg()[0] > MAX_LOAD:
            time.sleep(30)
            continue
        return

def lc_path(sector, tic):
    t = "%016d" % tic
    return "s%04d/%s/%s/%s/%s/hlsp_qlp_tess_ffi_s%04d-%s_tess_v01_llc.fits" % (sector, t[0:4], t[4:8], t[8:12], t[12:16], sector, t)

def fetch(sector, tic):
    url = BASE + lc_path(sector, tic)
    for attempt in range(4):
        try:
            with urllib.request.urlopen(url, timeout=60) as r:
                return tic, r.read()
        except urllib.error.HTTPError as e:
            if e.code in (404, 400):
                return tic, None
            time.sleep(2 + 3 * attempt)
        except Exception:
            time.sleep(2 + 3 * attempt)
    return tic, None

def convert(tic, raw, ra, dec):
    try:
        with fits.open(io.BytesIO(raw)) as h:
            d = h[1].data
            hdr = h[0].header
            t = np.array(d["TIME"], dtype=np.float64)
            f = np.array(d["SAP_FLUX"], dtype=np.float64)
            c = np.array(d["CADENCENO"], dtype=np.int64)
            q = np.array(d["QUALITY"])
            tmag = float(hdr.get("TESSMAG", 99.0) or 99.0)
    except Exception:
        return None
    m = np.isfinite(t) & np.isfinite(f) & (q == 0)
    if m.sum() < 200:
        return None
    t, f, c = t[m], f[m], c[m]
    med = np.median(f)
    if not np.isfinite(med) or med <= 0:
        return None
    flux = (f / med - 1.0).astype(np.float16)
    off = c - c[0]
    if off.max() > 65535 or off.min() < 0:
        return None
    # linear time model: t = t0 + off * dt (dt from the median spacing of consecutive cadences)
    dcad = np.diff(c)
    dtime = np.diff(t)
    ok = dcad > 0
    if not ok.any():
        return None
    dt = float(np.median(dtime[ok] / dcad[ok]))
    t0 = float(t[0])  # BTJD of offset 0 (off[0] == 0)
    n = len(t)
    rec = struct.pack("<QffffIf", tic, tmag, float(ra), float(dec), t0, n, dt)  # placeholder order fixed below
    # explicit layout: tic u64, tmag, ra, dec, t0, dt (f32 each), n u32
    rec = struct.pack("<Q5fI", tic, tmag, float(ra), float(dec), t0, dt, n)
    rec += off.astype("<u2").tobytes() + flux.astype("<f2").tobytes()
    if n % 2:
        rec += b"\0\0"
    return rec

def write_shard(path, sector, recs):
    tmp = path + ".tmp"
    with open(tmp, "wb") as f:
        f.write(b"SKT1" + struct.pack("<III", 1, len(recs), sector))
        for r in recs:
            f.write(r)
    os.replace(tmp, path)

def status(**kw):
    try:
        s = json.load(open(STATUS))
    except Exception:
        s = {}
    s.update(kw)
    s["updated"] = time.strftime("%Y-%m-%d %H:%M:%S")
    s["free_gb"] = round(free_gb(), 1)
    json.dump(s, open(STATUS, "w"))

def main():
    os.makedirs(DATA, exist_ok=True); os.makedirs(TMP, exist_ok=True)
    with open(ROOT + "/tess_format.md", "w") as f:
        f.write(__doc__)
    pool = ThreadPoolExecutor(max_workers=WORKERS)
    for sector in SECTORS:
        sdir = "%s/s%04d" % (DATA, sector)
        if os.path.exists(sdir + "/DONE"):
            continue
        os.makedirs(sdir, exist_ok=True)
        listp = "%s/s%04d.csv" % (TMP, sector)
        if not os.path.exists(listp):
            urllib.request.urlretrieve(LISTS % sector, listp)
        targets = []
        with open(listp) as f:
            for row in csv.reader(f):
                if row and not row[0].startswith("#"):
                    targets.append((int(row[0]), float(row[1]), float(row[2])))
        nshards = (len(targets) + SHARD_N - 1) // SHARD_N
        log("sector %d: %d targets, %d shards" % (sector, len(targets), nshards))
        t_sector = time.time(); done_lc = 0; missing = 0
        for si in range(nshards):
            spath = "%s/shard_%05d.skt" % (sdir, si)
            batch = targets[si * SHARD_N:(si + 1) * SHARD_N]
            if os.path.exists(spath):
                done_lc += len(batch)
                continue
            wait_if_busy()
            meta = {t: (ra, dec) for t, ra, dec in batch}
            recs = []
            for tic, raw in pool.map(lambda t: fetch(sector, t[0]), batch):
                if raw is None:
                    missing += 1
                    continue
                r = convert(tic, raw, *meta[tic])
                if r is None:
                    missing += 1
                    continue
                recs.append(r)
            write_shard(spath, sector, recs)
            done_lc += len(batch)
            if si % 10 == 0:
                el = time.time() - t_sector
                status(sector=sector, shard=si, shards=nshards, lc_done=done_lc, missing=missing,
                       rate_lc_per_s=round(done_lc / max(el, 1), 2))
        with open(sdir + "/DONE", "w") as f:
            f.write("%d targets, %d shards, %d missing\n" % (len(targets), nshards, missing))
        log("sector %d DONE in %.0f s, missing %d" % (sector, time.time() - t_sector, missing))
        status(last_done_sector=sector)
        try:
            os.remove(listp)
        except OSError:
            pass
    log("ALL DONE")

if __name__ == "__main__":
    main()
