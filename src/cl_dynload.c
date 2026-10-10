// Implementation of cl_dynload.h's runtime OpenCL loader -- see that
// header's top comment for why this project dlopen()s/LoadLibrary()s
// the ICD instead of linking it at build time.
#include "cl_dynload.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

cl_api_t cl_api;

#if defined(_WIN32)

#include <windows.h>

int cl_dynload_init(void) {
    HMODULE lib = LoadLibraryA("OpenCL.dll");
    if (!lib) {
        fprintf(stderr, "cl_dynload_init: OpenCL.dll not found on this host\n");
        return 0;
    }
#define LOAD(name) do { \
        cl_api.name = (PFN_cl##name)(void *)GetProcAddress(lib, "cl" #name); \
        if (!cl_api.name) { fprintf(stderr, "cl_dynload_init: missing symbol cl%s\n", #name); return 0; } \
    } while (0)

#else // POSIX: Linux, Android

#include <dlfcn.h>

int cl_dynload_init(void) {
    // Try the common ICD loader names in order -- Linux distros mostly
    // register "libOpenCL.so.1" (via ocl-icd or the vendor driver
    // package), Android vendor drivers commonly ship exactly
    // "libOpenCL.so" under /vendor/lib64 or /system/lib64 (already on
    // the default linker search path there), so the plain name is tried
    // first for that case and as a general fallback. macOS never has any
    // of these -- it ships OpenCL as a framework, not a dlopen-by-soname
    // shared object, and the framework binary has no ".so" extension, so
    // it lives at a fixed absolute path instead (still a real dlopen()
    // call, not a build-time -framework link, matching this project's
    // usual runtime-loading convention -- deprecated since macOS 10.14
    // but still shipped and functional, confirmed working end-to-end
    // with a real kernel compile+dispatch on both arm64 and x86_64 Mach-O
    // builds before this was added).
    static const char *candidates[] = {
        "libOpenCL.so", "libOpenCL.so.1", "libmali.so",
#ifdef __ANDROID__
        // vendor partition fallbacks (Adreno / Mali): not every phone lists libOpenCL.so as a public library
        "/vendor/lib64/libOpenCL.so", "/system/vendor/lib64/libOpenCL.so", "/system/lib64/libOpenCL.so",
        "/vendor/lib64/egl/libGLES_mali.so",
#endif
#ifdef __APPLE__
        "/System/Library/Frameworks/OpenCL.framework/OpenCL",
#endif
        NULL
    };
    void *lib = NULL;
    for (int i = 0; candidates[i]; i++) {
        lib = dlopen(candidates[i], RTLD_NOW | RTLD_GLOBAL);
        if (lib) break;
    }
    if (!lib) {
        fprintf(stderr, "cl_dynload_init: no OpenCL ICD found on this host (tried libOpenCL.so[.1], libmali.so)\n");
        return 0;
    }
#define LOAD(name) do { \
        cl_api.name = (PFN_cl##name)(void *)dlsym(lib, "cl" #name); \
        if (!cl_api.name) { fprintf(stderr, "cl_dynload_init: missing symbol cl%s\n", #name); return 0; } \
    } while (0)

#endif

    LOAD(GetPlatformIDs);
    LOAD(GetPlatformInfo);
    LOAD(GetDeviceIDs);
    LOAD(GetDeviceInfo);
    LOAD(CreateContext);
    LOAD(CreateCommandQueue);
    LOAD(CreateProgramWithSource);
    LOAD(BuildProgram);
    LOAD(GetProgramBuildInfo);
    LOAD(CreateKernel);
    LOAD(CreateBuffer);
    LOAD(EnqueueWriteBuffer);
    LOAD(SetKernelArg);
    LOAD(EnqueueNDRangeKernel);
    LOAD(Finish);
    LOAD(EnqueueReadBuffer);
    LOAD(ReleaseMemObject);
    LOAD(ReleaseKernel);
    LOAD(ReleaseProgram);
    LOAD(ReleaseCommandQueue);
    LOAD(ReleaseContext);
    LOAD(Flush);
    LOAD(WaitForEvents);
    LOAD(ReleaseEvent);
    LOAD(GetEventProfilingInfo);
    LOAD(GetKernelWorkGroupInfo);

#undef LOAD
    return 1;
}

// 2026-10-07, forum threads 73/74: the first version of the vendor filter (2026-10-06) did a
// case-insensitive strstr(CL_PLATFORM_VENDOR, expected). That works for Intel ("Intel(R)
// Corporation") and NVIDIA ("NVIDIA Corporation"), but AMD's ICD reports "Advanced Micro
// Devices, Inc." -- no "amd" in it -- so every opencl_ati task on every AMD host failed with
// 'no "amd" OpenCL devices found'. Mesa (rusticl/Clover) reports "Mesa/X.org" for any GPU.
// So: first check the PCI vendor ID of the platform's GPU devices (independent of how the
// driver spells its name), then fall back to the vendor AND platform-name strings with
// known aliases.
static void lowercase_copy(char *dst, size_t n, const char *src) {
    size_t i;
    for (i = 0; src[i] && i + 1 < n; i++) dst[i] = (char)tolower((unsigned char)src[i]);
    dst[i] = '\0';
}

int cl_platform_matches_vendor(cl_platform_id p, const char *expected) {
    if (!expected || expected[0] == '\0') return 1;

    cl_uint want_id = 0, want_id2 = 0;
    const char *alias = NULL;
    const char *device_alias = NULL;
    if (strcmp(expected, "amd") == 0) { want_id = 0x1002; alias = "advanced micro devices"; }
    else if (strcmp(expected, "intel") == 0) want_id = 0x8086;
    else if (strcmp(expected, "nvidia") == 0) want_id = 0x10de;
    else if (strcmp(expected, "qualcomm") == 0) {
        // Android Adreno reports 0x5143; on Windows on ARM (Snapdragon X etc.) the ID comes
        // from DXGI, which spells Qualcomm as 0x4D4F4351 ("QCOM"). Microsoft's OpenCLOn12
        // compatibility layer also reports platform vendor "Microsoft", so match the
        // device name too.
        want_id = 0x5143; want_id2 = 0x4D4F4351; device_alias = "adreno";
    }

    if (want_id) {
        cl_uint n = 0;
        if (cl_api.GetDeviceIDs(p, CL_DEVICE_TYPE_GPU, 0, NULL, &n) == CL_SUCCESS && n > 0) {
            cl_device_id devs[16];
            if (n > 16) n = 16;
            if (cl_api.GetDeviceIDs(p, CL_DEVICE_TYPE_GPU, n, devs, NULL) == CL_SUCCESS) {
                for (cl_uint i = 0; i < n; i++) {
                    cl_uint vid = 0;
                    if (cl_api.GetDeviceInfo(devs[i], CL_DEVICE_VENDOR_ID, sizeof(vid), &vid, NULL) == CL_SUCCESS
                        && (vid == want_id || (want_id2 && vid == want_id2))) return 1;
                    if (device_alias) {
                        char draw[256], dtext[256];
                        draw[0] = '\0';
                        cl_api.GetDeviceInfo(devs[i], CL_DEVICE_NAME, sizeof(draw), draw, NULL);
                        draw[sizeof(draw) - 1] = '\0';
                        lowercase_copy(dtext, sizeof(dtext), draw);
                        if (strstr(dtext, device_alias)) return 1;
                    }
                }
            }
        }
    }

    char raw[256], text[256];
    raw[0] = '\0';
    cl_api.GetPlatformInfo(p, CL_PLATFORM_VENDOR, sizeof(raw), raw, NULL);
    raw[sizeof(raw) - 1] = '\0';
    lowercase_copy(text, sizeof(text), raw);
    if (strstr(text, expected) || (alias && strstr(text, alias))) return 1;

    raw[0] = '\0';
    cl_api.GetPlatformInfo(p, CL_PLATFORM_NAME, sizeof(raw), raw, NULL);
    raw[sizeof(raw) - 1] = '\0';
    lowercase_copy(text, sizeof(text), raw);
    return strstr(text, expected) || (alias && strstr(text, alias));
}

int cl_pick_vendor_device(const cl_device_id *devs, cl_uint n, int idx, const char *expected) {
    if (!expected || strcmp(expected, "qualcomm") != 0 || n == 0) return idx;
    if (idx < 0 || (cl_uint)idx >= n) idx = 0;
    for (int pass = 0; pass < 2; pass++) {
        for (cl_uint i = 0; i < n; i++) {
            cl_uint j = pass == 0 ? (cl_uint)idx : i;
            char raw[256], text[256];
            raw[0] = '\0';
            cl_api.GetDeviceInfo(devs[j], CL_DEVICE_NAME, sizeof(raw), raw, NULL);
            raw[sizeof(raw) - 1] = '\0';
            lowercase_copy(text, sizeof(text), raw);
            if (strstr(text, "adreno")) return (int)j;
            if (pass == 0) break;
        }
    }
    return idx;
}
