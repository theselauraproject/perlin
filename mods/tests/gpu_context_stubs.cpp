// the user shader pack reader is part of the wider client; the headless gpu test runs without packs
#include "client/resources/ShaderPack.hpp"

namespace perlin {
    struct ShaderPack::Impl {};
    ShaderPack::ShaderPack() = default;
    ShaderPack::~ShaderPack() = default;
    ShaderPack& ShaderPack::Instance() {
        static ShaderPack pack;
        return pack;
    }
    namespace {
        bool gBundled = false;
    }
    void TestSetBundledPack(bool on) {
        gBundled = on;
    }
    bool ShaderPack::Active() const {
        return gBundled;
    }
    bool ShaderPack::Bundled() const {
        return gBundled;
    }
    bool ShaderPack::Flag(const std::string& name, bool fallback) const {
        return name == "sceneCopy" && gBundled ? true : fallback;
    }
}
