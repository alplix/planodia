// OpenCL backend of the Planodia BLS scan (any OpenCL 1.2 GPU: NVIDIA, AMD, Intel, Qualcomm Adreno, Apple).
// Single precision only -- no FP64 needed. The kernel computes exactly what pl::scan_cpu computes.
#pragma once
#include <string>
#include <vector>
#include "planodia_core.h"

namespace pl {
// device_id: index within the matching platform (BOINC --device); returns false and fills *err on failure
// maxbins: the largest phase-bin table the kernel needs (pmax / binw rounded up); more bins need more local memory
bool gpu_init(int device_id, int maxbins, std::string *err);
// same contract as scan_cpu(); false on any OpenCL error
bool gpu_scan(const Prep &p, const Params &P, const std::vector<double> &periods, ScanOut &o);
void gpu_shutdown();
const char *gpu_device_name();
}  // namespace pl
