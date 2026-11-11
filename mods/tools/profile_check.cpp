#include "client/javashaders/GlslangBridge.hpp"
#include "client/javashaders/PackSource.hpp"
#include "client/javashaders/ProgramSet.hpp"
#include "client/javashaders/ShaderScreens.hpp"

#include <cstdio>
#include <memory>

using namespace perlin::jshader;

int main(int argc, char** argv) {
    if (argc < 2)
        return 2;
    auto src = OpenPack(argv[1]);
    if (!src)
        return 1;
    std::shared_ptr<PackSource> pack(std::move(src));
    ShaderPackInstance base(pack, {}, EngineMacros(260300, false, {}));
    const auto profiles = Profiles(base);
    std::printf("%zu profiles\n", profiles.size());
    for (const auto& [name, values] : profiles) {
        ShaderPackInstance inst(pack, values, EngineMacros(260300, false, {}));
        const std::string match = MatchingProfile(inst, inst.Overrides());
        std::string missing;
        for (const auto& [k, v] : values) {
            const ShaderOption* o = FindOption(inst, k);
            if (!o)
                missing += k + " ";
            else if (OptionValue(inst, *o, inst.Overrides()) != v && o->kind != ShaderOption::Kind::Toggle)
                missing += k + "(" + OptionValue(inst, *o, inst.Overrides()) + "!=" + v + ") ";
        }
        std::printf("%-14s matches '%s'  shadow=%d  unknown/odd: %s\n", name.c_str(), match.c_str(),
                    inst.ProgramEnabled("shadow") ? 1 : 0, missing.c_str());
    }
    OptionValues file;
    if (argc > 2)
        file = ReadJavaPackOptions(argv[2]);
    ShaderPackInstance loaded(pack, file, EngineMacros(260300, false, {}));
    OptionValues edits;
    for (int click = 0; click < 10; click++) {
        OptionValues current = loaded.Overrides();
        for (const auto& kv : edits)
            current[kv.first] = kv.second;
        const std::string at = MatchingProfile(loaded, current);
        size_t next = 0;
        for (size_t i = 0; i < profiles.size(); i++)
            if (profiles[i].first == at)
                next = (i + 1) % profiles.size();
        std::printf("click %d: showing '%s' -> applying %s\n", click, at.c_str(), profiles[next].first.c_str());
        for (const auto& [k, v] : profiles[next].second)
            edits[k] = v;
    }
    return 0;
}
