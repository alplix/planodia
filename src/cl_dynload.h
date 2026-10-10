// Runtime dynamic loader for the OpenCL ICD library -- used instead of
// build-time `-lOpenCL` linking so opencl_search.c is the EXACT same
// source and needs no OpenCL import library at build time on any of
// Linux/Android/Windows:
//  - Android has no NDK-provided libOpenCL.so to link against at all
//    (OpenCL is vendor-specific -- Mali/Adreno/PowerVR each ship their
//    own in /vendor/lib64 or /system/lib64 on-device, never at build
//    time), so dlopen is the ONLY way to use OpenCL on Android.
//  - On Linux and Windows a build-time import library usually does
//    exist, but not every VOLUNTEER host is guaranteed to have one
//    registered under the exact same name/path this project's build
//    machine has -- hard-linking risks the whole binary failing to even
//    start ("cannot open shared object file") on hosts whose ICD loader
//    is present but differently named. Loading at runtime instead lets
//    gpu_search_init() fail gracefully (return 1, BOINC app exits
//    cleanly) exactly like "no usable device" already does, instead of
//    the process refusing to launch.
// Declares function pointers for exactly the cl* calls opencl_search.c
// uses; cl_dynload_init() resolves them all or returns 0 (failure).
#ifndef BITBOINC_CL_DYNLOAD_H
#define BITBOINC_CL_DYNLOAD_H

#ifdef __APPLE__
#include <OpenCL/cl.h>
#else
#include <CL/cl.h>
#endif

typedef cl_int (*PFN_clGetPlatformIDs)(cl_uint, cl_platform_id *, cl_uint *);
typedef cl_int (*PFN_clGetPlatformInfo)(cl_platform_id, cl_platform_info, size_t, void *, size_t *);
typedef cl_int (*PFN_clGetDeviceIDs)(cl_platform_id, cl_device_type, cl_uint, cl_device_id *, cl_uint *);
typedef cl_int (*PFN_clGetDeviceInfo)(cl_device_id, cl_device_info, size_t, void *, size_t *);
typedef cl_context (*PFN_clCreateContext)(const cl_context_properties *, cl_uint, const cl_device_id *,
                                           void (*)(const char *, const void *, size_t, void *), void *, cl_int *);
typedef cl_command_queue (*PFN_clCreateCommandQueue)(cl_context, cl_device_id, cl_command_queue_properties, cl_int *);
typedef cl_program (*PFN_clCreateProgramWithSource)(cl_context, cl_uint, const char **, const size_t *, cl_int *);
typedef cl_int (*PFN_clBuildProgram)(cl_program, cl_uint, const cl_device_id *, const char *,
                                      void (*)(cl_program, void *), void *);
typedef cl_int (*PFN_clGetProgramBuildInfo)(cl_program, cl_device_id, cl_program_build_info, size_t, void *, size_t *);
typedef cl_kernel (*PFN_clCreateKernel)(cl_program, const char *, cl_int *);
typedef cl_mem (*PFN_clCreateBuffer)(cl_context, cl_mem_flags, size_t, void *, cl_int *);
typedef cl_int (*PFN_clEnqueueWriteBuffer)(cl_command_queue, cl_mem, cl_bool, size_t, size_t, const void *,
                                            cl_uint, const cl_event *, cl_event *);
typedef cl_int (*PFN_clSetKernelArg)(cl_kernel, cl_uint, size_t, const void *);
typedef cl_int (*PFN_clEnqueueNDRangeKernel)(cl_command_queue, cl_kernel, cl_uint, const size_t *, const size_t *,
                                              const size_t *, cl_uint, const cl_event *, cl_event *);
typedef cl_int (*PFN_clFinish)(cl_command_queue);
typedef cl_int (*PFN_clEnqueueReadBuffer)(cl_command_queue, cl_mem, cl_bool, size_t, size_t, void *,
                                           cl_uint, const cl_event *, cl_event *);
typedef cl_int (*PFN_clReleaseMemObject)(cl_mem);
typedef cl_int (*PFN_clReleaseKernel)(cl_kernel);
typedef cl_int (*PFN_clReleaseProgram)(cl_program);
typedef cl_int (*PFN_clReleaseCommandQueue)(cl_command_queue);
typedef cl_int (*PFN_clReleaseContext)(cl_context);
/* Added 2026-10 for Keraunos's asynchronous double-buffered launches. All
 * OpenCL 1.0 core entry points, exported by every ICD loader. */
typedef cl_int (*PFN_clFlush)(cl_command_queue);
typedef cl_int (*PFN_clWaitForEvents)(cl_uint, const cl_event *);
typedef cl_int (*PFN_clReleaseEvent)(cl_event);
typedef cl_int (*PFN_clGetEventProfilingInfo)(cl_event, cl_profiling_info, size_t, void *, size_t *);
typedef cl_int (*PFN_clGetKernelWorkGroupInfo)(cl_kernel, cl_device_id, cl_kernel_work_group_info, size_t, void *, size_t *);

typedef struct {
    PFN_clGetPlatformIDs GetPlatformIDs;
    PFN_clGetPlatformInfo GetPlatformInfo;
    PFN_clGetDeviceIDs GetDeviceIDs;
    PFN_clGetDeviceInfo GetDeviceInfo;
    PFN_clCreateContext CreateContext;
    PFN_clCreateCommandQueue CreateCommandQueue;
    PFN_clCreateProgramWithSource CreateProgramWithSource;
    PFN_clBuildProgram BuildProgram;
    PFN_clGetProgramBuildInfo GetProgramBuildInfo;
    PFN_clCreateKernel CreateKernel;
    PFN_clCreateBuffer CreateBuffer;
    PFN_clEnqueueWriteBuffer EnqueueWriteBuffer;
    PFN_clSetKernelArg SetKernelArg;
    PFN_clEnqueueNDRangeKernel EnqueueNDRangeKernel;
    PFN_clFinish Finish;
    PFN_clEnqueueReadBuffer EnqueueReadBuffer;
    PFN_clReleaseMemObject ReleaseMemObject;
    PFN_clReleaseKernel ReleaseKernel;
    PFN_clReleaseProgram ReleaseProgram;
    PFN_clReleaseCommandQueue ReleaseCommandQueue;
    PFN_clReleaseContext ReleaseContext;
    PFN_clFlush Flush;
    PFN_clWaitForEvents WaitForEvents;
    PFN_clReleaseEvent ReleaseEvent;
    PFN_clGetEventProfilingInfo GetEventProfilingInfo;
    PFN_clGetKernelWorkGroupInfo GetKernelWorkGroupInfo;
} cl_api_t;

extern cl_api_t cl_api;

// Returns 1 on success (all symbols resolved), 0 on failure (no OpenCL
// ICD found on this host, or it's missing an expected entry point).
int cl_dynload_init(void);

// Returns 1 if OpenCL platform p belongs to the GPU vendor named by `expected` (lowercase,
// e.g. "amd", "intel", "nvidia", "qualcomm"; empty string = no filtering, always 1).
// Shared by every app's EXPECTED_GPU_VENDOR platform filter -- see cl_dynload.c for why a
// plain substring match on CL_PLATFORM_VENDOR is not enough.
int cl_platform_matches_vendor(cl_platform_id p, const char *expected);

// Returns the device index to use within a platform already accepted by
// cl_platform_matches_vendor(). Only changes anything for "qualcomm": Microsoft's OpenCLOn12
// layer (Windows on ARM) can list the software "Microsoft Basic Render Driver" next to the
// Adreno on the same platform, so if devs[idx] isn't an Adreno, use the first one that is.
int cl_pick_vendor_device(const cl_device_id *devs, cl_uint n, int idx, const char *expected);

#endif
