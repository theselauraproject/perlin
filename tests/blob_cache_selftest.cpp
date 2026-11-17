#include "client/systems/multiplayer/BlobCache.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>

using namespace perlin;

static int fails = 0;
#define CHECK(c)                                                                                             \
    do {                                                                                                     \
        if (!(c)) {                                                                                          \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c);                                               \
            fails++;                                                                                         \
        }                                                                                                    \
    } while (0)

static std::vector<uint8_t> Bytes(size_t n, uint8_t seed) {
    std::vector<uint8_t> v(n);
    for (size_t i = 0; i < n; i++)
        v[i] = static_cast<uint8_t>(seed + i * 7);
    return v;
}

int main() {
    const auto dir = std::filesystem::temp_directory_path() / "perlin_blob_cache_selftest";
    std::filesystem::remove_all(dir);
    const std::string path = (dir / "blobs.bin").string();
    const auto a = Bytes(300, 1), b = Bytes(4096, 9);

    {
        BlobCache cache(path);
        CHECK(cache.Usable());
        std::vector<uint8_t> out;
        CHECK(!cache.Get(1, out));
        cache.Put(1, a.data(), a.size());
        cache.Put(2, b.data(), b.size());
        cache.Put(1, b.data(), b.size());
        CHECK(cache.Get(1, out) && out == a);
        CHECK(cache.Get(2, out) && out == b);
        cache.Flush();
    }

    const auto intact = std::filesystem::file_size(path);
    {
        std::ofstream torn(path, std::ios::binary | std::ios::app);
        const char half[10] = {3, 0, 0, 0, 0, 0, 0, 0, 100, 0};
        torn.write(half, sizeof(half));
    }
    {
        BlobCache cache(path);
        std::vector<uint8_t> out;
        CHECK(cache.Get(1, out) && out == a);
        CHECK(cache.Get(2, out) && out == b);
        CHECK(!cache.Get(3, out));
        CHECK(std::filesystem::file_size(path) == intact);
        const auto c = Bytes(50, 4);
        cache.Put(3, c.data(), c.size());
        CHECK(cache.Get(3, out) && out == c);
    }

    {
        std::fstream f(path, std::ios::in | std::ios::out | std::ios::binary);
        f.seekp(8 + 16 + 10);
        f.put(static_cast<char>(0x5A));
    }
    {
        BlobCache cache(path);
        std::vector<uint8_t> out;
        CHECK(!cache.Get(1, out));
    }

    {
        BlobCache off("");
        std::vector<uint8_t> out;
        CHECK(!off.Usable());
        off.Put(1, a.data(), a.size());
        CHECK(!off.Get(1, out));
    }

    std::filesystem::remove_all(dir);
    if (fails)
        printf("%d failures\n", fails);
    else
        printf("blob_cache_selftest ok\n");
    return fails ? 1 : 0;
}
