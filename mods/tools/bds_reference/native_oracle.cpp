#if !defined(__x86_64__)
#error "Build the native oracle for x86-64 (use -arch x86_64 on macOS)."
#endif
#include <array>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <immintrin.h>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <sys/mman.h>
#include <unistd.h>
#include <vector>

using SineFunction = __m128(__attribute__((ms_abi)) *)(float);
using MovementFunction = void(__attribute__((ms_abi)) *)(float*, float, float, float, float, float);
using SlowdownFunction = void(__attribute__((ms_abi)) *)(float*, float*, float*);

int main(int argc, char** argv) {
    if (argc != 3)
        return 1;
    std::ifstream image(argv[1], std::ios::binary), fixture(argv[2]);
    uint32_t magic = 0, count = 0;
    image.read(reinterpret_cast<char*>(&magic), 4);
    image.read(reinterpret_cast<char*>(&count), 4);
    if (!image || !fixture || magic != 0x53444250 || count > 10000)
        return 1;
    struct Page {
        uint64_t address;
        std::array<char, 4096> bytes;
    };
    std::vector<Page> pages(count);
    for (auto& page : pages) {
        image.read(reinterpret_cast<char*>(&page.address), 8);
        image.read(page.bytes.data(), page.bytes.size());
        if (!image || page.address % 4096 != 0 || page.address < 0x140000000ull ||
            page.address >= 0x150000000ull)
            return 1;
    }
    const uint64_t pageSize = static_cast<uint64_t>(sysconf(_SC_PAGESIZE));
    if (!pageSize || (pageSize & (pageSize - 1)))
        return 1;
    std::set<uint64_t> regions;
    for (const auto& page : pages)
        regions.insert(page.address & ~(pageSize - 1));
    regions.insert(0x14712EC10ull & ~(pageSize - 1));
    for (uint64_t address : regions) {
        void* wanted = reinterpret_cast<void*>(address);
        void* mapped =
            mmap(wanted, pageSize, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON, -1, 0);
        if (mapped != wanted) {
            std::fprintf(stderr, "Cannot map BDS page at %llx\n", static_cast<unsigned long long>(address));
            return 1;
        }
    }
    for (const auto& page : pages)
        std::memcpy(reinterpret_cast<void*>(page.address), page.bytes.data(), page.bytes.size());
    std::cout << "# Native x86-64 execution of BDS Windows ABI math; MXCSR 0x1f80\n";
    std::string line;
    while (std::getline(fixture, line)) {
        if (line.empty())
            continue;
        if (line[0] == '#') {
            if (line.rfind("# unicorn", 0) != 0)
                std::cout << line << '\n';
            continue;
        }
        std::istringstream row(line);
        char kind;
        row >> kind >> std::hex;
        const int fields = kind == 'S' ? 3 : kind == 'C' ? 5 : kind == 'M' ? 12 : kind == 'D' ? 18 : 0;
        std::array<uint32_t, 18> words{};
        if (!fields)
            return 1;
        for (int i = 0; i < fields; ++i)
            if (!(row >> words[i]))
                return 1;
        const auto f = [&](int index) { return std::bit_cast<float>(words[index]); };
        if (kind == 'S') {
            for (int mode = 0; mode < 2; ++mode) {
                _mm_setcsr(0x1f80);
                const auto fn = reinterpret_cast<SineFunction>(mode ? 0x1462527E0ull : 0x146251EA0ull);
                const auto result = std::bit_cast<std::array<uint32_t, 4>>(fn(f(0)));
                words[mode + 1] = result[0];
            }
        } else if (kind == 'C') {
            for (int mode = 0; mode < 2; ++mode) {
                _mm_setcsr(0x1f80);
                *reinterpret_cast<uint32_t*>(0x14712EC10ull) = mode * 2;
                const auto fn = reinterpret_cast<SineFunction>(0x146251E30ull);
                const auto result = std::bit_cast<std::array<uint32_t, 4>>(fn(f(0)));
                words[mode * 2 + 1] = result[0];
                words[mode * 2 + 2] = result[1];
            }
        } else if (kind == 'D') {
            _mm_setcsr(0x1f80);
            std::array<float, 3> multiplier{f(0), f(1), f(2)};
            std::array<float, 18> request{};
            std::array<float, 9> state{};
            for (int axis = 0; axis < 3; ++axis) {
                request[axis + 15] = f(axis + 3);
                state[axis + 6] = f(axis + 6);
            }
            reinterpret_cast<SlowdownFunction>(0x140C1EF20ull)(multiplier.data(), request.data(), state.data());
            for (int axis = 0; axis < 3; ++axis) {
                words[axis + 9] = std::bit_cast<uint32_t>(multiplier[axis]);
                words[axis + 12] = std::bit_cast<uint32_t>(request[axis + 15]);
                words[axis + 15] = std::bit_cast<uint32_t>(state[axis + 6]);
            }
        } else {
            _mm_setcsr(0x1f80);
            *reinterpret_cast<uint32_t*>(0x14712EC10ull) = words[0];
            std::array<float, 3> velocity{f(6), f(7), f(8)};
            const auto fn = reinterpret_cast<MovementFunction>(0x1432674C0ull);
            fn(velocity.data(), f(1), f(2), f(3), f(4), f(5));
            for (int axis = 0; axis < 3; ++axis)
                words[axis + 9] = std::bit_cast<uint32_t>(velocity[axis]);
        }
        std::cout << kind;
        for (int i = 0; i < fields; ++i)
            std::cout << ' ' << std::hex << std::setfill('0') << std::setw(8) << words[i];
        std::cout << '\n';
    }
    return fixture.eof() ? 0 : 1;
}
