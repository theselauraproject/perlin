#pragma once
#include <cstdint>
#include <string>

namespace perlin::sysinfo {
    std::string CpuName();
    int CpuThreads();
    uint64_t TotalMemory();
    uint64_t ProcessMemory();
    uint64_t ProcessPeakMemory();
    std::string Compiler();
    std::string GpuName(unsigned vendorId, unsigned deviceId);
    std::string VendorName(unsigned vendorId);
}
