#include "planodia_opencl.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" {
#include "cl_dynload.h"
}

#ifndef EXPECTED_GPU_VENDOR
#define EXPECTED_GPU_VENDOR ""
#endif

namespace pl {

// One work group (64 work items) = one trial period. The phase histogram lives in local memory: counts as ints and the
// flux sums as 16.16 fixed point ints (so plain local atomic_add works on every OpenCL 1.1+ device, no float atomics).
// A flux of up to +-0.5 and 12000 points stays far below 2^31 in this scale; the quantisation (1.5e-5) is well below
// the photometric noise. The best box per period is found by a parallel scan over the start bins and a reduction that
// breaks ties exactly like scan_cpu (lowest duration, then lowest start bin).
#define PL_MAXBINS 1024
static const char *kSource = R"CLC(
#define MAXB 1024
#define WG 64
#define SCALE 65536.0f
__kernel __attribute__((reqd_work_group_size(64, 1, 1)))
void bls_scan(__global const float *tt, __global const float *y, const int n,
              __global const float *periods, const float binw, const float sigma, const int ndur,
              const int m0, const int m1, const int m2, const int m3,
              __global float *outp, __global int *outj, __global int *outb)
{
    __local int cnt[MAXB];
    __local int sum[MAXB];
    __local float rsnr[WG];
    __local int rj[WG];
    __local int rb[WG];
    int k = get_group_id(0);
    int lid = get_local_id(0);
    float per = periods[k];
    int nb = (int)ceil(per / binw);
    if (nb > MAXB) nb = MAXB;
    for (int b = lid; b < nb; b += WG) { cnt[b] = 0; sum[b] = 0; }
    barrier(CLK_LOCAL_MEM_FENCE);
    for (int i = lid; i < n; i += WG) {
        float ph = tt[i] / per;
        ph -= floor(ph);
        int b = (int)(ph * (float)nb);
        if (b >= nb) b = nb - 1;
        atomic_inc(&cnt[b]);
        atomic_add(&sum[b], (int)rint(y[i] * SCALE));
    }
    barrier(CLK_LOCAL_MEM_FENCE);
    if (lid == 0) {
        for (int b = 1; b < nb; b++) { cnt[b] += cnt[b - 1]; sum[b] += sum[b - 1]; }
    }
    barrier(CLK_LOCAL_MEM_FENCE);
    int ctot = cnt[nb - 1], stot_i = sum[nb - 1];
    float N = (float)n;
    float stot = (float)stot_i / SCALE;
    float best = 0.0f;
    int bestj = 0, bestb = 0;
    for (int j = 0; j < ndur; j++) {
        int m = (j == 0) ? m0 : (j == 1) ? m1 : (j == 2) ? m2 : m3;
        for (int b = lid; b < nb; b += WG) {
            int e = b + m - 1;
            int c0 = b > 0 ? cnt[b - 1] : 0, s0 = b > 0 ? sum[b - 1] : 0;
            int cin_i, sin_i;
            if (e < nb) { cin_i = cnt[e] - c0; sin_i = sum[e] - s0; }
            else { cin_i = (ctot - c0) + cnt[e - nb]; sin_i = (stot_i - s0) + sum[e - nb]; }
            float cin = (float)cin_i, cout = N - cin;
            if (cin < 3.0f || cout < 10.0f) continue;
            float sin_ = (float)sin_i / SCALE;
            float delta = (stot - sin_) / cout - sin_ / cin;
            if (delta <= 0.0f) continue;
            float snr = delta / sigma * sqrt(cin * cout / N);
            if (snr > best) { best = snr; bestj = j; bestb = b; }
        }
    }
    rsnr[lid] = best; rj[lid] = bestj; rb[lid] = bestb;
    barrier(CLK_LOCAL_MEM_FENCE);
    if (lid == 0) {
        float bs = rsnr[0]; int bj = rj[0], bb = rb[0];
        for (int t = 1; t < WG; t++) {
            if (rsnr[t] > bs || (rsnr[t] == bs && bs > 0.0f && (rj[t] < bj || (rj[t] == bj && rb[t] < bb)))) {
                bs = rsnr[t]; bj = rj[t]; bb = rb[t];
            }
        }
        outp[k] = bs; outj[k] = bj; outb[k] = bb;
    }
}
)CLC";

static cl_context g_ctx = NULL;
static cl_command_queue g_queue = NULL;
static cl_program g_prog = NULL;
static cl_kernel g_kernel = NULL;
static cl_device_id g_dev = NULL;
static char g_name[256] = "";
static cl_mem g_tt = NULL, g_y = NULL, g_per = NULL, g_op = NULL, g_oj = NULL, g_ob = NULL;
static size_t g_cap_n = 0, g_cap_chunk = 0;
static const int kChunk = 4096;   // periods per launch: short launches stay far below any watchdog limit

const char *gpu_device_name() { return g_name; }

static void release_buffers() {
    cl_mem *all[] = {&g_tt, &g_y, &g_per, &g_op, &g_oj, &g_ob};
    for (cl_mem *m : all) {
        if (*m) cl_api.ReleaseMemObject(*m);
        *m = NULL;
    }
    g_cap_n = g_cap_chunk = 0;
}

bool gpu_init(int device_id, std::string *err) {
    if (!cl_dynload_init()) { *err = "no OpenCL library found"; return false; }
    cl_uint np = 0;
    if (cl_api.GetPlatformIDs(0, NULL, &np) != CL_SUCCESS || np == 0) { *err = "no OpenCL platform"; return false; }
    std::vector<cl_platform_id> plats(np);
    cl_api.GetPlatformIDs(np, plats.data(), NULL);
    cl_platform_id platform = 0;
    cl_uint ndev = 0;
    for (cl_uint i = 0; i < np; i++) {
        if (!cl_platform_matches_vendor(plats[i], EXPECTED_GPU_VENDOR)) continue;
        cl_uint n = 0;
        if (cl_api.GetDeviceIDs(plats[i], CL_DEVICE_TYPE_GPU, 0, NULL, &n) == CL_SUCCESS && n > 0) { platform = plats[i]; ndev = n; break; }
    }
    if (!platform) {
        for (cl_uint i = 0; i < np && !platform; i++) {     // fall back to any device type (some drivers report "default")
            if (!cl_platform_matches_vendor(plats[i], EXPECTED_GPU_VENDOR)) continue;
            cl_uint n = 0;
            if (cl_api.GetDeviceIDs(plats[i], CL_DEVICE_TYPE_ALL, 0, NULL, &n) == CL_SUCCESS && n > 0) { platform = plats[i]; ndev = n; }
        }
    }
    if (!platform) { *err = std::string("no usable OpenCL GPU") + (EXPECTED_GPU_VENDOR[0] ? " (expected vendor " EXPECTED_GPU_VENDOR ")" : ""); return false; }
    std::vector<cl_device_id> devs(ndev);
    cl_api.GetDeviceIDs(platform, CL_DEVICE_TYPE_ALL, ndev, devs.data(), NULL);
    if (device_id < 0 || (cl_uint)device_id >= ndev) device_id = 0;
    device_id = cl_pick_vendor_device(devs.data(), ndev, device_id, EXPECTED_GPU_VENDOR);
    g_dev = devs[device_id];
    cl_api.GetDeviceInfo(g_dev, CL_DEVICE_NAME, sizeof(g_name), g_name, NULL);
    size_t max_wg = 0;
    cl_api.GetDeviceInfo(g_dev, CL_DEVICE_MAX_WORK_GROUP_SIZE, sizeof(max_wg), &max_wg, NULL);
    cl_ulong lmem = 0;
    cl_api.GetDeviceInfo(g_dev, CL_DEVICE_LOCAL_MEM_SIZE, sizeof(lmem), &lmem, NULL);
    if (max_wg < 64 || lmem < 12 * 1024) { *err = "device has too little work-group size or local memory"; return false; }
    cl_int e = 0;
    g_ctx = cl_api.CreateContext(NULL, 1, &g_dev, NULL, NULL, &e);
    if (!g_ctx) { *err = "clCreateContext failed"; return false; }
    g_queue = cl_api.CreateCommandQueue(g_ctx, g_dev, 0, &e);
    if (!g_queue) { *err = "clCreateCommandQueue failed"; return false; }
    const char *src = kSource;
    size_t len = std::strlen(src);
    g_prog = cl_api.CreateProgramWithSource(g_ctx, 1, &src, &len, &e);
    if (!g_prog) { *err = "clCreateProgramWithSource failed"; return false; }
    e = cl_api.BuildProgram(g_prog, 1, &g_dev, "-cl-std=CL1.2", NULL, NULL);
    if (e != CL_SUCCESS) {
        size_t ls = 0;
        cl_api.GetProgramBuildInfo(g_prog, g_dev, CL_PROGRAM_BUILD_LOG, 0, NULL, &ls);
        std::string log(ls + 1, 0);
        cl_api.GetProgramBuildInfo(g_prog, g_dev, CL_PROGRAM_BUILD_LOG, ls, &log[0], NULL);
        *err = "kernel build failed: " + log;
        return false;
    }
    g_kernel = cl_api.CreateKernel(g_prog, "bls_scan", &e);
    if (!g_kernel) { *err = "clCreateKernel failed"; return false; }
    return true;
}

bool gpu_scan(const Prep &p, const Params &P, const std::vector<double> &periods, ScanOut &o) {
    size_t n = p.t.size(), np = periods.size();
    if (!g_kernel || n == 0 || np == 0) return false;
    if ((int)std::ceil(P.pmax / P.binw) > PL_MAXBINS) { std::fprintf(stderr, "planodia_opencl: pmax too large for the kernel's bin table\n"); return false; }
    cl_int e = 0;
    size_t chunk = (np < (size_t)kChunk) ? np : (size_t)kChunk;
    if (n > g_cap_n || chunk > g_cap_chunk) {
        release_buffers();
        g_cap_n = n + n / 4 + 64;
        g_cap_chunk = (size_t)kChunk;
        g_tt = cl_api.CreateBuffer(g_ctx, CL_MEM_READ_ONLY, g_cap_n * 4, NULL, &e);
        g_y = cl_api.CreateBuffer(g_ctx, CL_MEM_READ_ONLY, g_cap_n * 4, NULL, &e);
        g_per = cl_api.CreateBuffer(g_ctx, CL_MEM_READ_ONLY, g_cap_chunk * 4, NULL, &e);
        g_op = cl_api.CreateBuffer(g_ctx, CL_MEM_READ_WRITE, g_cap_chunk * 4, NULL, &e);
        g_oj = cl_api.CreateBuffer(g_ctx, CL_MEM_READ_WRITE, g_cap_chunk * 4, NULL, &e);
        g_ob = cl_api.CreateBuffer(g_ctx, CL_MEM_READ_WRITE, g_cap_chunk * 4, NULL, &e);
        if (!g_tt || !g_y || !g_per || !g_op || !g_oj || !g_ob) { std::fprintf(stderr, "planodia_opencl: buffer allocation failed\n"); release_buffers(); return false; }
    }
    std::vector<float> tf(n), yf(n), pf(chunk);
    for (size_t i = 0; i < n; i++) { tf[i] = (float)p.t[i]; yf[i] = (float)p.y[i]; }
    cl_api.EnqueueWriteBuffer(g_queue, g_tt, CL_TRUE, 0, n * 4, tf.data(), 0, NULL, NULL);
    cl_api.EnqueueWriteBuffer(g_queue, g_y, CL_TRUE, 0, n * 4, yf.data(), 0, NULL, NULL);
    o.power.assign(np, 0.f);
    o.bj.assign(np, 0);
    o.bb.assign(np, 0);
    int ni = (int)n, nd = P.ndur;
    int m[4] = {1, 1, 1, 1};
    for (int j = 0; j < nd && j < 4; j++) m[j] = dur_bins(P, j);
    float binw = (float)P.binw, sigma = (float)p.sigma;
    std::vector<float> opw(chunk);
    std::vector<int> oj(chunk), ob(chunk);
    for (size_t start = 0; start < np; start += chunk) {
        size_t cn = (np - start < chunk) ? (np - start) : chunk;
        for (size_t k = 0; k < cn; k++) pf[k] = (float)periods[start + k];
        cl_api.EnqueueWriteBuffer(g_queue, g_per, CL_TRUE, 0, cn * 4, pf.data(), 0, NULL, NULL);
        int a = 0;
        cl_api.SetKernelArg(g_kernel, a++, sizeof(cl_mem), &g_tt);
        cl_api.SetKernelArg(g_kernel, a++, sizeof(cl_mem), &g_y);
        cl_api.SetKernelArg(g_kernel, a++, sizeof(int), &ni);
        cl_api.SetKernelArg(g_kernel, a++, sizeof(cl_mem), &g_per);
        cl_api.SetKernelArg(g_kernel, a++, sizeof(float), &binw);
        cl_api.SetKernelArg(g_kernel, a++, sizeof(float), &sigma);
        cl_api.SetKernelArg(g_kernel, a++, sizeof(int), &nd);
        cl_api.SetKernelArg(g_kernel, a++, sizeof(int), &m[0]);
        cl_api.SetKernelArg(g_kernel, a++, sizeof(int), &m[1]);
        cl_api.SetKernelArg(g_kernel, a++, sizeof(int), &m[2]);
        cl_api.SetKernelArg(g_kernel, a++, sizeof(int), &m[3]);
        cl_api.SetKernelArg(g_kernel, a++, sizeof(cl_mem), &g_op);
        cl_api.SetKernelArg(g_kernel, a++, sizeof(cl_mem), &g_oj);
        cl_api.SetKernelArg(g_kernel, a++, sizeof(cl_mem), &g_ob);
        size_t global = cn * 64, local = 64;
        e = cl_api.EnqueueNDRangeKernel(g_queue, g_kernel, 1, NULL, &global, &local, 0, NULL, NULL);
        if (e != CL_SUCCESS) { std::fprintf(stderr, "planodia_opencl: clEnqueueNDRangeKernel failed (%d)\n", e); return false; }
        e = cl_api.Finish(g_queue);
        if (e != CL_SUCCESS) { std::fprintf(stderr, "planodia_opencl: clFinish failed (%d)\n", e); return false; }
        cl_api.EnqueueReadBuffer(g_queue, g_op, CL_TRUE, 0, cn * 4, opw.data(), 0, NULL, NULL);
        cl_api.EnqueueReadBuffer(g_queue, g_oj, CL_TRUE, 0, cn * 4, oj.data(), 0, NULL, NULL);
        cl_api.EnqueueReadBuffer(g_queue, g_ob, CL_TRUE, 0, cn * 4, ob.data(), 0, NULL, NULL);
        for (size_t k = 0; k < cn; k++) { o.power[start + k] = opw[k]; o.bj[start + k] = oj[k]; o.bb[start + k] = ob[k]; }
    }
    return true;
}

void gpu_shutdown() {
    release_buffers();
    if (g_kernel) cl_api.ReleaseKernel(g_kernel);
    if (g_prog) cl_api.ReleaseProgram(g_prog);
    if (g_queue) cl_api.ReleaseCommandQueue(g_queue);
    if (g_ctx) cl_api.ReleaseContext(g_ctx);
    g_kernel = NULL; g_prog = NULL; g_queue = NULL; g_ctx = NULL;
}

}  // namespace pl
