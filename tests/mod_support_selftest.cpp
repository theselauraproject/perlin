#include "client/systems/mods/ModSupport.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

using namespace perlin::mods::support;
namespace fs = std::filesystem;

namespace {
    int failures = 0;

    void Expect(bool ok, const char* what) {
        if (!ok) {
            std::printf("FAIL %s\n", what);
            failures++;
        }
    }

    void Leb(std::string& out, size_t v) {
        do {
            unsigned char byte = v & 0x7F;
            v >>= 7;
            out += static_cast<char>(byte | (v ? 0x80 : 0));
        } while (v);
    }

    std::string CustomSection(const std::string& name, const std::string& payload) {
        std::string body;
        Leb(body, name.size());
        body += name;
        body += payload;
        std::string section(1, '\0');
        Leb(section, body.size());
        return section + body;
    }

    const std::string kHeader("\0asm\x01\0\0\0", 8);

    std::string WriteTemp(const std::string& name, const std::string& bytes) {
        const fs::path path = fs::temp_directory_path() / ("perlin_mod_support_" + name);
        std::ofstream(path, std::ios::binary | std::ios::trunc) << bytes;
        return path.string();
    }
}

int main() {
    fs::path out;
    Expect(SafeRelative("a/b.png", out) && out == fs::path("a/b.png"), "plain relative path");
    Expect(SafeRelative("a/./b", out) && out == fs::path("a/b"), "dot segments are normalised");
    Expect(!SafeRelative("", out), "empty path");
    Expect(!SafeRelative("../x", out), "parent escape");
    Expect(!SafeRelative("a/../../x", out), "parent escape after a segment");
    Expect(!SafeRelative("/etc/passwd", out), "absolute path");
    Expect(!SafeRelative("a\\b", out), "backslash");
    Expect(!SafeRelative(std::string("a\0b", 3), out), "embedded nul");
    Expect(!SafeRelative(std::string(241, 'a'), out), "overlong path");
    Expect(!SafeRelative("a/", out) || out == fs::path("a/"), "trailing separator does not escape");

    const std::string manifest = R"({"name":"demo"})";
    const std::string good =
        kHeader + CustomSection("other", "xx") + CustomSection("perlin_manifest", manifest);
    Expect(EmbeddedManifest(WriteTemp("good.wasm", good)) == manifest,
           "manifest after another custom section");

    Expect(EmbeddedManifest(WriteTemp("none.wasm", kHeader + CustomSection("other", "xx"))).empty(),
           "module without a manifest");
    Expect(EmbeddedManifest(WriteTemp("notwasm.wasm", "hello world, definitely not wasm")).empty(),
           "not a module");
    Expect(EmbeddedManifest(WriteTemp("empty.wasm", "")).empty(), "empty file");
    Expect(EmbeddedManifest("/nonexistent/perlin/none.wasm").empty(), "missing file");

    const std::string truncated = good.substr(0, good.size() - 4);
    Expect(EmbeddedManifest(WriteTemp("trunc.wasm", truncated)).empty(), "truncated section");

    const std::string huge = kHeader + CustomSection("perlin_manifest", std::string(70u << 10, 'x'));
    Expect(EmbeddedManifest(WriteTemp("huge.wasm", huge)).empty(), "manifest over the size cap is ignored");

    std::vector<uint8_t> bytes;
    Expect(!ReadFile("/nonexistent/perlin/none.wasm", bytes), "read of a missing file fails");
    Expect(!ReadFile(WriteTemp("zero.bin", ""), bytes), "read of an empty file fails");
    Expect(ReadFile(WriteTemp("some.bin", "abc"), bytes) && bytes.size() == 3, "read of a file");

    Expect(UnixMs() > 1'600'000'000'000, "unix time is in milliseconds");

    std::printf(failures ? "%d failure(s)\n" : "mod_support_selftest ok\n", failures);
    return failures ? 1 : 0;
}
