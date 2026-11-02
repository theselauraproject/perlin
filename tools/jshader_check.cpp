// Offline check of an OptiFine / Iris shader pack: every program of every dimension goes through the same path the
// game uses (include expansion, option macros, Transform, glslang to SPIR-V) and on to each backend's language
// (SPIR-V for Vulkan, HLSL for Direct3D, MSL for Metal). Compute programs are compiled too.
//   jshader_check <pack folder or zip> [-d world0,world-1,world1] [-b spirv,hlsl,msl] [-o NAME=VALUE]... [-v] [-p prog]
#include "client/javashaders/GlslangBridge.hpp"
#include "client/javashaders/PackSource.hpp"
#include "client/javashaders/PipelineConfig.hpp"
#include "client/javashaders/ProgramSet.hpp"
#include "client/javashaders/RhiBackend.hpp"
#include "client/javashaders/ShaderScreens.hpp"
#include "client/javashaders/Transform.hpp"

#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace perlin::jshader;

namespace {
    std::vector<std::string> Split(const std::string& s, char sep) {
        std::vector<std::string> out;
        std::stringstream in(s);
        std::string item;
        while (std::getline(in, item, sep))
            if (!item.empty())
                out.push_back(item);
        return out;
    }

    // glslang's HLSL front end stands in for fxc / dxc: it catches what SPIRV-Cross emits that HLSL rejects
    bool ValidateHlsl(const std::string& text, bool fragment, std::string& log) {
        const EShLanguage lang = fragment ? EShLangFragment : EShLangVertex;
        glslang::TShader shader(lang);
        const char* src = text.c_str();
        shader.setStrings(&src, 1);
        shader.setEnvInput(glslang::EShSourceHlsl, lang, glslang::EShClientVulkan, 100);
        shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_1);
        shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_3);
        shader.setEntryPoint("main");
        shader.setSourceEntryPoint("main");
        if (!shader.parse(GetDefaultResources(), 450, ENoProfile, false, false,
                          static_cast<EShMessages>(EShMsgSpvRules | EShMsgVulkanRules | EShMsgReadHlsl))) {
            log = shader.getInfoLog();
            return false;
        }
        return true;
    }

    std::string FirstError(const std::string& log) {
        size_t a = log.find("ERROR");
        if (a == std::string::npos)
            a = 0;
        const size_t b = log.find('\n', a);
        return log.substr(a, b == std::string::npos ? std::string::npos : b - a).substr(0, 300);
    }

    // metal links stages by [[user(locnN)]]: every fragment input needs a vertex output of the same type
    std::map<std::string, std::string> MslVaryings(const std::string& src, const char* structName) {
        std::map<std::string, std::string> out;
        const size_t a = src.find(std::string("struct ") + structName);
        if (a == std::string::npos)
            return out;
        const size_t e = src.find("};", a);
        std::stringstream body(src.substr(a, e - a));
        std::string line;
        while (std::getline(body, line)) {
            const size_t u = line.find("[[user(");
            if (u == std::string::npos)
                continue;
            const size_t close = line.find(')', u);
            const std::string loc = line.substr(u + 7, close - u - 7);
            std::stringstream words(line.substr(0, u));
            std::string type, name;
            words >> type >> name;
            out[loc] = type;
        }
        return out;
    }

    bool MslInterfaceMatches(const std::string& vs, const std::string& fs, std::string& why) {
        const auto outs = MslVaryings(vs, "main0_out");
        for (const auto& [loc, type] : MslVaryings(fs, "main0_in")) {
            const auto it = outs.find(loc);
            if (it == outs.end()) {
                why = loc + " (" + type + ") is not written by the vertex shader";
                return false;
            }
            if (it->second != type) {
                why =
                    loc + " is " + it->second + " in the vertex shader, " + type + " in the fragment shader";
                return false;
            }
        }
        return true;
    }

    void Dump(const std::string& name, const std::string& text) {
        std::ofstream("/tmp/jshader_" + name) << text;
    }
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(
            stderr,
            "usage: jshader_check <pack> [-d dims] [-b spirv,hlsl,msl] [-o NAME=VALUE] [-v] [-p program]\n");
        return 2;
    }
    std::vector<std::string> dims = {"world0", "world-1", "world1"};
    std::vector<std::string> backends = {"spirv", "hlsl", "msl"};
    OptionValues overrides;
    bool verbose = false;
    std::string only, profileArg;
    int randomConfigs = 0;
    for (int i = 2; i < argc; i++) {
        const std::string a = argv[i];
        if (a == "-P" && i + 1 < argc)
            profileArg = argv[++i];
        else if (a == "-r" && i + 1 < argc)
            randomConfigs = std::atoi(argv[++i]);
        else if (a == "-v")
            verbose = true;
        else if (a == "-p" && i + 1 < argc)
            only = argv[++i];
        else if (a == "-d" && i + 1 < argc)
            dims = Split(argv[++i], ',');
        else if (a == "-b" && i + 1 < argc)
            backends = Split(argv[++i], ',');
        else if (a == "-o" && i + 1 < argc) {
            const std::string kv = argv[++i];
            const size_t eq = kv.find('=');
            if (eq != std::string::npos)
                overrides[kv.substr(0, eq)] = kv.substr(eq + 1);
        }
    }
    std::shared_ptr<PackSource> pack = OpenPack(argv[1]);
    if (!pack) {
        std::fprintf(stderr, "cannot open pack %s\n", argv[1]);
        return 2;
    }
    // the features a vulkan / d3d12 / metal device offers
    EngineEnvironment env;
    env.features = BaseFeatureFlags();
    for (const char* f : {"CUSTOM_IMAGES", "COMPUTE_SHADERS", "SSBO"})
        env.features.insert(f);
    // -P NAME applies a profile of the pack, -P all checks every profile in turn
    std::vector<std::pair<std::string, OptionValues>> configs;
    if (profileArg.empty()) {
        configs.emplace_back("", overrides);
    } else {
        const ShaderPackInstance probe(pack, overrides, EngineMacros(260300, false, env));
        for (const auto& [name, values] : Profiles(probe)) {
            if (profileArg != "all" && profileArg != name)
                continue;
            OptionValues v = values;
            for (const auto& kv : overrides)
                v[kv.first] = kv.second;
            configs.emplace_back(name, v);
        }
        if (configs.empty()) {
            std::fprintf(stderr, "no profile %s\n", profileArg.c_str());
            return 2;
        }
    }
    // -r N: N configurations with every option at a random value of its list (seeded, reproducible)
    if (randomConfigs > 0) {
        const ShaderPackInstance probe(pack, overrides, EngineMacros(260300, false, env));
        uint64_t seed = 0x9E3779B97F4A7C15ull;
        const auto next = [&] {
            seed ^= seed << 13;
            seed ^= seed >> 7;
            seed ^= seed << 17;
            return seed;
        };
        configs.clear();
        for (int r = 0; r < randomConfigs; r++) {
            OptionValues v;
            // only options a screen shows: the rest (section guards like sildur's #define final) are not
            // meant to change, and iris would let a player reach them through a screen only
            std::set<std::string> shown;
            std::function<void(const std::string&)> walk = [&](const std::string& screen) {
                for (const ScreenEntry& e : BuildScreen(probe, screen).entries) {
                    if (e.kind == ScreenEntry::Kind::Option)
                        shown.insert(e.name);
                    else if (e.kind == ScreenEntry::Kind::Screen && shown.insert("[" + e.name).second)
                        walk(e.name);
                }
            };
            walk("");
            for (const ShaderOption& o : probe.Options()) {
                if (!shown.contains(o.name))
                    continue;
                if (o.kind == ShaderOption::Kind::Toggle)
                    v[o.name] = next() % 2 ? "true" : "false";
                else if (o.kind == ShaderOption::Kind::Value && !o.values.empty())
                    v[o.name] = o.values[next() % o.values.size()];
            }
            for (const auto& kv : overrides)
                v[kv.first] = kv.second;
            configs.emplace_back("rand" + std::to_string(r), v);
        }
    }
    int ok = 0, bad = 0;
    std::map<std::string, int> reasons;
    for (const auto& [configName, configValues] : configs)
        for (const std::string& dim : dims) {
            ShaderPackInstance instance(pack, configValues, EngineMacros(260300, false, env));
            instance.SetDimension(dim);
            std::printf("== %s %s %s: %zu options\n", instance.Name().c_str(), configName.c_str(),
                        dim.c_str(), instance.Options().size());
            const PipelineConfig pipeline = ParsePipelineConfig(instance);
            TransformOptions baseOpts;
            baseOpts.macros = instance.EngineMacros();
            baseOpts.preprocess = PreprocessGlsl;
            baseOpts.zeroToOneDepth = true;
            baseOpts.splitSamplers = true;
            for (size_t i = 0; i < pipeline.images.size(); i++) {
                baseOpts.imageSlots[pipeline.images[i].name] = static_cast<int>(i);
                baseOpts.imageFormats[pipeline.images[i].name] =
                    ImageFormatQualifier(pipeline.images[i].internalFormat);
            }
            std::vector<std::string> names;
            std::set<std::string> seen;
            const auto add = [&](const std::string& n) {
                if (seen.insert(n).second)
                    names.push_back(n);
            };
            for (const ProgramInfo& p : StandardPrograms())
                add(p.name);
            for (const char* base : {"setup", "begin", "prepare", "deferred", "composite", "shadowcomp"})
                for (const std::string& n : instance.PassChain(base))
                    add(n);
            add("final");
            for (const std::string& name : names) {
                if (!only.empty() && name != only)
                    continue;
                if (!instance.ProgramExists(name) || !instance.ProgramEnabled(name) ||
                    name.compare(0, 3, "dh_") == 0)
                    continue;
                if (name.compare(0, 10, "shadowcomp") == 0 && !pipeline.shadowEnabled)
                    continue; // iris runs shadowcomp only with a shadow pass
                const auto src = instance.Program(name);
                if (!src || !src->found)
                    continue;
                const std::string tag = (configName.empty() ? "" : configName + ":") + dim + "/" + name;
                // compute: through the same transform and backends the game uses
                for (size_t c = 0; c < src->compute.size(); c++) {
                    TransformOptions copts = baseOpts;
                    const ComputeResult cr = TransformCompute(src->compute[c], copts);
                    if (!cr.ok()) {
                        bad++;
                        reasons["compute-transform"]++;
                        std::printf("FAIL  %-40s compute %zu transform: %s\n", tag.c_str(), c,
                                    cr.stage.error.c_str());
                        continue;
                    }
                    bool pass = true;
                    for (const std::string& b : backends) {
                        const RhiShaderKind kind = b == "msl"    ? RhiShaderKind::Msl
                                                   : b == "hlsl" ? RhiShaderKind::Hlsl
                                                                 : RhiShaderKind::SpirV;
                        const RhiCompute rc = BuildRhiCompute(cr, kind, {});
                        if (!rc.ok) {
                            pass = false;
                            reasons["compute-" + b]++;
                            std::printf("FAIL  %-40s compute %zu %s: %s\n", tag.c_str(), c, b.c_str(),
                                        FirstError(rc.error).c_str());
                            if (verbose)
                                Dump(name + ".comp", cr.stage.glsl);
                            continue;
                        }
                        if (verbose)
                            Dump(name + "." + std::to_string(c) + "." + b + ".cs", rc.source);
                    }
                    (pass ? ok : bad)++;
                    if (pass && verbose)
                        std::printf("ok    %s compute %zu: local %ux%ux%u groups %s %ux%ux%u\n", tag.c_str(),
                                    c, cr.localSize[0], cr.localSize[1], cr.localSize[2],
                                    cr.fixedGroups ? "fixed" : "render", cr.workGroups[0], cr.workGroups[1],
                                    cr.workGroups[2]);
                }
                if (src->vertex.text.empty() || src->fragment.text.empty())
                    continue;
                for (const bool toBack : {false, true}) {
                    if (toBack && name != "final")
                        continue;
                    TransformOptions opts = baseOpts;
                    opts.flipVertexY = !toBack;
                    opts.flipFragCoordY = toBack;
                    opts.explicitLod =
                        name.compare(0, 8, "gbuffers") != 0 && name.compare(0, 6, "shadow") != 0;
                    const ProgramResult result = TransformProgram(src->vertex, src->fragment, opts);
                    if (!result.ok()) {
                        bad++;
                        reasons["transform"]++;
                        std::printf("FAIL  %-40s transform: %s%s\n", tag.c_str(), result.vertex.error.c_str(),
                                    result.fragment.error.c_str());
                        continue;
                    }
                    bool pass = true;
                    for (const std::string& b : backends) {
                        const RhiShaderKind kind = b == "msl"    ? RhiShaderKind::Msl
                                                   : b == "hlsl" ? RhiShaderKind::Hlsl
                                                                 : RhiShaderKind::SpirV;
                        const RhiProgram rp = BuildRhiProgram(result, kind, {});
                        if (!rp.ok) {
                            pass = false;
                            reasons[b]++;
                            std::printf("FAIL  %-40s %s: %s\n", tag.c_str(), b.c_str(),
                                        FirstError(rp.error).c_str());
                            if (verbose) {
                                Dump(name + ".vert", result.vertex.glsl);
                                Dump(name + ".frag", result.fragment.glsl);
                            }
                            continue;
                        }
                        if (kind == RhiShaderKind::Msl) {
                            std::string why;
                            if (!MslInterfaceMatches(rp.vertexSource, rp.fragmentSource, why)) {
                                pass = false;
                                reasons["msl-interface"]++;
                                std::printf("FAIL  %-40s msl interface: %s\n", tag.c_str(), why.c_str());
                            }
                        }
                        if (kind == RhiShaderKind::Hlsl) {
                            std::string log;
                            if (!ValidateHlsl(rp.vertexSource, false, log) ||
                                !ValidateHlsl(rp.fragmentSource, true, log)) {
                                pass = false;
                                reasons["hlsl-validate"]++;
                                std::printf("FAIL  %-40s hlsl validate: %s\n", tag.c_str(),
                                            FirstError(log).c_str());
                            }
                        }
                        if (verbose) {
                            Dump(name + "." + b + ".vs", rp.vertexSource);
                            Dump(name + "." + b + ".fs", rp.fragmentSource);
                        }
                    }
                    (pass ? ok : bad)++;
                    if (pass && verbose)
                        std::printf("ok    %s (%d uniform registers)\n", tag.c_str(), result.uniformRegs);
                }
            }
        }
    std::printf("summary: %d ok, %d failed", ok, bad);
    for (const auto& [k, n] : reasons)
        std::printf(", %s %d", k.c_str(), n);
    std::printf("\n");
    return bad ? 1 : 0;
}
