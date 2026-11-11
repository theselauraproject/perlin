#include "util/SystemInfo.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <thread>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define PSAPI_VERSION 2
#include <windows.h>
#include <psapi.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#elif defined(__FreeBSD__)
#include <sys/sysctl.h>
#include <sys/types.h>
#endif

namespace perlin::sysinfo {
    namespace {
        std::string Trim(std::string s) {
            const auto notSpace = [](unsigned char c) { return !std::isspace(c); };
            s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
            s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
            std::string out;
            for (char c : s) {
                if (c == ' ' && !out.empty() && out.back() == ' ')
                    continue;
                out += c;
            }
            return out;
        }

#if defined(__linux__)
        uint64_t StatusKb(const char* key) {
            std::ifstream f("/proc/self/status");
            std::string line;
            const std::string k = std::string(key) + ":";
            while (std::getline(f, line))
                if (line.compare(0, k.size(), k) == 0)
                    return std::strtoull(line.c_str() + k.size(), nullptr, 10);
            return 0;
        }
#endif
    }

    int CpuThreads() {
        return static_cast<int>(std::max(1u, std::thread::hardware_concurrency()));
    }

    std::string CpuName() {
#if defined(__linux__)
        std::ifstream f("/proc/cpuinfo");
        std::string line;
        while (std::getline(f, line)) {
            if (line.compare(0, 10, "model name") == 0 || line.compare(0, 8, "Hardware") == 0) {
                const size_t colon = line.find(':');
                if (colon != std::string::npos)
                    return Trim(line.substr(colon + 1));
            }
        }
        return {};
#elif defined(_WIN32)
        char buffer[256] = {};
        DWORD size = sizeof(buffer);
        if (RegGetValueA(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                         "ProcessorNameString", RRF_RT_REG_SZ, nullptr, buffer, &size) == ERROR_SUCCESS)
            return Trim(buffer);
        return {};
#elif defined(__APPLE__)
        char buffer[256] = {};
        size_t size = sizeof(buffer);
        if (sysctlbyname("machdep.cpu.brand_string", buffer, &size, nullptr, 0) == 0)
            return Trim(buffer);
        return {};
#elif defined(__FreeBSD__)
        char buffer[256] = {};
        size_t size = sizeof(buffer);
        if (sysctlbyname("hw.model", buffer, &size, nullptr, 0) == 0)
            return Trim(buffer);
        return {};
#else
        return {};
#endif
    }

    uint64_t TotalMemory() {
#if defined(__linux__)
        return [] {
            std::ifstream f("/proc/meminfo");
            std::string key;
            uint64_t kb = 0;
            while (f >> key >> kb) {
                if (key == "MemTotal:")
                    return kb * 1024;
                f.ignore(256, '\n');
            }
            return uint64_t{0};
        }();
#elif defined(_WIN32)
        MEMORYSTATUSEX m{};
        m.dwLength = sizeof(m);
        return GlobalMemoryStatusEx(&m) ? m.ullTotalPhys : 0;
#elif defined(__APPLE__)
        uint64_t mem = 0;
        size_t size = sizeof(mem);
        return sysctlbyname("hw.memsize", &mem, &size, nullptr, 0) == 0 ? mem : 0;
#elif defined(__FreeBSD__)
        unsigned long mem = 0;
        size_t size = sizeof(mem);
        return sysctlbyname("hw.physmem", &mem, &size, nullptr, 0) == 0 ? mem : 0;
#else
        return 0;
#endif
    }

    uint64_t ProcessMemory() {
#if defined(__linux__)
        return StatusKb("VmRSS") * 1024;
#elif defined(_WIN32)
        PROCESS_MEMORY_COUNTERS c{};
        return K32GetProcessMemoryInfo(GetCurrentProcess(), &c, sizeof(c)) ? c.WorkingSetSize : 0;
#elif defined(__APPLE__)
        mach_task_basic_info info{};
        mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
        return task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info),
                         &count) == KERN_SUCCESS
                   ? info.resident_size
                   : 0;
#else
        return 0;
#endif
    }

    uint64_t ProcessPeakMemory() {
#if defined(__linux__)
        return StatusKb("VmHWM") * 1024;
#elif defined(_WIN32)
        PROCESS_MEMORY_COUNTERS c{};
        return K32GetProcessMemoryInfo(GetCurrentProcess(), &c, sizeof(c)) ? c.PeakWorkingSetSize : 0;
#elif defined(__APPLE__)
        mach_task_basic_info info{};
        mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
        return task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info),
                         &count) == KERN_SUCCESS
                   ? info.resident_size_max
                   : 0;
#else
        return 0;
#endif
    }

    std::string Compiler() {
#if defined(__clang__)
        return "Clang " + std::to_string(__clang_major__) + "." + std::to_string(__clang_minor__) + "." +
               std::to_string(__clang_patchlevel__);
#elif defined(__GNUC__)
        return "GCC " + std::to_string(__GNUC__) + "." + std::to_string(__GNUC_MINOR__) + "." +
               std::to_string(__GNUC_PATCHLEVEL__);
#elif defined(_MSC_VER)
        return "MSVC " + std::to_string(_MSC_VER / 100) + "." + std::to_string(_MSC_VER % 100);
#else
        return "C++";
#endif
    }

    std::string VendorName(unsigned id) {
        switch (id) {
        case 0x10DE:
            return "NVIDIA Corporation";
        case 0x1002:
        case 0x1022:
            return "Advanced Micro Devices, Inc.";
        case 0x8086:
            return "Intel";
        case 0x106B:
            return "Apple";
        case 0x13B5:
            return "ARM";
        case 0x5143:
            return "Qualcomm";
        case 0x1414:
            return "Microsoft";
        case 0x10005:
            return "Mesa";
        default:
            return {};
        }
    }

    std::string GpuName(unsigned vendorId, unsigned deviceId) {
#if defined(_WIN32)
        char want[32];
        std::snprintf(want, sizeof(want), "VEN_%04X&DEV_%04X", vendorId, deviceId);
        HKEY key = nullptr;
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                          "SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e968-e325-11ce-bfc1-08002be10318}",
                          0, KEY_READ, &key) != ERROR_SUCCESS)
            return {};
        std::string found;
        for (DWORD i = 0; found.empty(); i++) {
            char name[64];
            DWORD len = sizeof(name);
            if (RegEnumKeyExA(key, i, name, &len, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
                break;
            char id[512] = {};
            DWORD size = sizeof(id);
            std::string sub = std::string(name);
            if (RegGetValueA(key, sub.c_str(), "MatchingDeviceId", RRF_RT_REG_SZ, nullptr, id, &size) !=
                ERROR_SUCCESS)
                continue;
            std::string upper = id;
            for (char& c : upper)
                c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            if (!upper.contains(want))
                continue;
            char desc[256] = {};
            size = sizeof(desc);
            if (RegGetValueA(key, sub.c_str(), "DriverDesc", RRF_RT_REG_SZ, nullptr, desc, &size) ==
                ERROR_SUCCESS)
                found = Trim(desc);
        }
        RegCloseKey(key);
        return found;
#elif defined(__linux__)
        char vendor[8], device[8];
        std::snprintf(vendor, sizeof(vendor), "%04x", vendorId);
        std::snprintf(device, sizeof(device), "%04x", deviceId);
        for (const char* path :
             {"/usr/share/hwdata/pci.ids", "/usr/share/misc/pci.ids", "/usr/share/pci.ids"}) {
            std::ifstream f(path);
            if (!f.good())
                continue;
            std::string line;
            bool inVendor = false;
            while (std::getline(f, line)) {
                if (line.empty() || line[0] == '#')
                    continue;
                if (line[0] != '\t') {
                    inVendor = line.compare(0, 4, vendor) == 0;
                    continue;
                }
                if (inVendor && line.size() > 6 && line[1] != '\t' && line.compare(1, 4, device) == 0) {
                    std::string name = Trim(line.substr(5));
                    const size_t open = name.find('[');
                    const size_t close = name.rfind(']');
                    if (open != std::string::npos && close != std::string::npos && close > open)
                        name = name.substr(open + 1, close - open - 1);
                    return name;
                }
            }
        }
        return {};
#elif defined(__APPLE__)
        if (vendorId == 0x106B) {
            std::string cpu = CpuName();
            if (cpu.compare(0, 5, "Apple") == 0)
                return cpu;
        }
        return {};
#else
        return {};
#endif
    }
}
