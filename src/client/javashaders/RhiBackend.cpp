#include <cctype>
#include <cstdio>
#include <sstream>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include "client/javashaders/RhiBackend.hpp"

#include "client/javashaders/GlslangBridge.hpp"

#include <spirv_hlsl.hpp>
#include <spirv_msl.hpp>
#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#include <algorithm>
#include <cstring>
#include <map>
#include <set>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3dcompiler.h>
#endif

namespace perlin::jshader {
    void MatchStageInterface(std::string& vs, std::string& ps);
    namespace {
        constexpr uint32_t kMslImageBase = 96, kMslStorageBufferBase = 10;

        bool CrossHlsl(const std::vector<uint32_t>& spirv, Stage stage, std::string& out,
                       std::string& error) {
            try {
                spirv_cross::CompilerHLSL c(spirv);
                const spirv_cross::ShaderResources res = c.get_shader_resources();
                if (stage == Stage::Vertex) {
                    for (const auto& in : res.stage_inputs) {
                        const uint32_t loc = c.get_decoration(in.id, spv::DecorationLocation);
                        spirv_cross::HLSLVertexAttributeRemap remap;
                        remap.location = loc;
                        remap.semantic = "TEXCOORD" + std::to_string(loc);
                        c.add_vertex_attribute_remap(remap);
                    }
                }
                spirv_cross::CompilerHLSL::Options h;

                h.shader_model = 51;
                h.point_size_compat = true;
                h.point_coord_compat = true;
                h.flatten_matrix_vertex_input_semantics = false;
                c.set_hlsl_options(h);
                out = c.compile();
                return true;
            } catch (const std::exception& e) {
                error = e.what();
                return false;
            }
        }

        bool CrossMsl(const std::vector<uint32_t>& spirv, Stage stage, std::string& out, std::string& error) {
            try {
                spirv_cross::CompilerMSL c(spirv);
                spirv_cross::CompilerMSL::Options m;
#if defined(__APPLE__) && TARGET_OS_IPHONE
                m.platform = spirv_cross::CompilerMSL::Options::iOS;
#else
                m.platform = spirv_cross::CompilerMSL::Options::macOS;
#endif
                m.set_msl_version(2, 3);
                m.enable_decoration_binding = true;
                m.pad_fragment_output_components = true;
                m.force_native_arrays = true;
                c.set_msl_options(m);
                const spv::ExecutionModel model = stage == Stage::Vertex    ? spv::ExecutionModelVertex
                                                  : stage == Stage::Compute ? spv::ExecutionModelGLCompute
                                                                            : spv::ExecutionModelFragment;
                const spirv_cross::ShaderResources res = c.get_shader_resources();

                const auto bind = [&](const spirv_cross::Resource& r, bool buffer) {
                    spirv_cross::MSLResourceBinding b;
                    b.stage = model;
                    b.desc_set = c.get_decoration(r.id, spv::DecorationDescriptorSet);
                    b.binding = c.get_decoration(r.id, spv::DecorationBinding);
                    const uint32_t textureIndex = b.desc_set == 2 ? kMslImageBase + b.binding : b.binding;
                    const uint32_t bufferIndex =
                        b.desc_set == 3 ? kMslStorageBufferBase + b.binding : b.binding;
                    b.msl_buffer = buffer ? bufferIndex : 0;
                    b.msl_texture = buffer ? 0 : textureIndex;
                    b.msl_sampler = buffer ? 0 : b.binding;
                    c.add_msl_resource_binding(b);
                };
                for (const auto& r : res.uniform_buffers)
                    bind(r, true);
                for (const auto& r : res.storage_buffers)
                    bind(r, true);
                for (const auto& r : res.separate_images)
                    bind(r, false);
                for (const auto& r : res.separate_samplers)
                    bind(r, false);
                for (const auto& r : res.sampled_images)
                    bind(r, false);
                for (const auto& r : res.storage_images)
                    bind(r, false);
                out = "// conduit:msl-sets texture " + std::to_string(kMslImageBase) + " 2 buffer " +
                      std::to_string(kMslStorageBufferBase) + " 3\n";
                if (stage == Stage::Compute) {
                    const spirv_cross::SPIREntryPoint& ep = c.get_entry_point("main", model);
                    out += "// conduit:threads " + std::to_string(std::max(1u, ep.workgroup_size.x)) + " " +
                           std::to_string(std::max(1u, ep.workgroup_size.y)) + " " +
                           std::to_string(std::max(1u, ep.workgroup_size.z)) + "\n";
                }
                out += c.compile();
                return true;
            } catch (const std::exception& e) {
                error = e.what();
                return false;
            }
        }

        bool SamplerNames(const std::vector<uint32_t>& spirv, std::set<std::string>& declared,
                          std::set<std::string>& used) {
            try {
                spirv_cross::Compiler c(spirv);
                const auto collect = [&](const spirv_cross::ShaderResources& res, std::set<std::string>& out) {
                    for (const auto* list : {&res.sampled_images, &res.separate_images, &res.separate_samplers})
                        for (const spirv_cross::Resource& r : *list) {
                            out.insert(r.name);
                            out.insert(c.get_name(r.id));
                        }
                };
                collect(c.get_shader_resources(), declared);
                collect(c.get_shader_resources(c.get_active_interface_variables()), used);
                return true;
            } catch (const std::exception&) {
                return false;
            }
        }

        bool NamedIn(const std::set<std::string>& names, const std::string& n) {
            for (const std::string& x : names)
                if (x == n || (x.size() > n.size() && x.compare(0, n.size(), n) == 0 && x[n.size()] == '_'))
                    return true;
            return false;
        }

        std::vector<uint8_t> Bytes(const std::vector<uint32_t>& words) {
            std::vector<uint8_t> out(words.size() * 4);
            std::memcpy(out.data(), words.data(), out.size());
            return out;
        }
    }

    RhiProgram BuildRhiProgram(const ProgramResult& program, RhiShaderKind kind, const DxbcCompiler& dxbc) {
        RhiProgram result;
        result.kind = kind;
        result.uniformRegs = program.uniformRegs;
        if (!program.ok()) {
            result.error = "program did not transform";
            return result;
        }
        if (kind == RhiShaderKind::Dxbc && !dxbc) {
            result.error = "no HLSL compiler available";
            return result;
        }
        std::map<std::string, RhiSampler> samplers;
        for (const StageResult* st : {&program.vertex, &program.fragment})
            for (const SamplerDecl& s : st->samplers) {
                if (s.image) {

                    if (std::find(result.images.begin(), result.images.end(), s.name) == result.images.end())
                        result.images.push_back(s.name);
                    continue;
                }
                RhiSampler r;
                r.name = s.name;
                r.slot = s.binding;
                r.shadow = s.type.contains("Shadow");
                samplers[s.name] = r;
            }
        for (const auto& kv : samplers)
            result.samplers.push_back(kv.second);

        const StageResult* stages[2] = {&program.vertex, &program.fragment};
        std::vector<uint8_t>* outputs[2] = {&result.vertex, &result.fragment};
        std::string* sources[2] = {&result.vertexSource, &result.fragmentSource};
        std::set<std::string> declaredNames, usedNames;
        bool namesKnown = true;
        for (int i = 0; i < 2; i++) {
            const bool fragment = i == 1;
            std::vector<uint32_t> spirv;
            std::string log;
            if (!CompileToSpirv(stages[i]->glsl, stages[i]->stage, spirv, log)) {
                result.error =
                    std::string(fragment ? "fragment" : "vertex") + " shader failed to compile: " + log;
                return result;
            }
            if (!SamplerNames(spirv, declaredNames, usedNames))
                namesKnown = false;
            if (kind == RhiShaderKind::SpirV) {
                *outputs[i] = Bytes(spirv);
                continue;
            }
            if (kind == RhiShaderKind::Msl) {
                std::string msl, err;
                if (!CrossMsl(spirv, stages[i]->stage, msl, err)) {
                    result.error = std::string(fragment ? "fragment" : "vertex") +
                                   " shader conversion to MSL failed: " + err;
                    return result;
                }
                *sources[i] = msl;
                outputs[i]->assign(msl.begin(), msl.end());
                continue;
            }
            std::string hlsl, err;
            if (!CrossHlsl(spirv, stages[i]->stage, hlsl, err)) {
                result.error =
                    std::string(fragment ? "fragment" : "vertex") + " shader conversion failed: " + err;
                return result;
            }

            int unrolled = 0;
            for (size_t at = hlsl.find("[unroll]"); at != std::string::npos; at = hlsl.find("[unroll]", at)) {
                hlsl.replace(at, 8, "");
                unrolled++;
            }
            if (unrolled && std::getenv("JSHADER_TRACE"))
                std::fprintf(stderr, "[jshader] dropped %d unroll attributes from the %s shader\n", unrolled,
                             fragment ? "pixel" : "vertex");

            for (size_t at = hlsl.find(".SampleCmp("); at != std::string::npos;
                 at = hlsl.find(".SampleCmp(", at))
                hlsl.replace(at, 11, ".SampleCmpLevelZero(");
            *sources[i] = hlsl;
        }
        if (std::getenv("JSHADER_SAMPLER_DEBUG")) {
            std::string d, u;
            for (const auto& x : declaredNames) d += x + " ";
            for (const auto& x : usedNames) u += x + " ";
            std::fprintf(stderr, "[jshader] samplers known=%d declared: %s\n  used: %s\n", namesKnown ? 1 : 0,
                         d.c_str(), u.c_str());
        }
        if (namesKnown && !std::getenv("JSHADER_KEEP_UNUSED_SAMPLERS"))
            result.samplers.erase(std::remove_if(result.samplers.begin(), result.samplers.end(),
                                                 [&](const RhiSampler& r) { return !NamedIn(usedNames, r.name); }),
                                  result.samplers.end());
        if (kind == RhiShaderKind::Hlsl || kind == RhiShaderKind::Dxbc) {
            MatchStageInterface(result.vertexSource, result.fragmentSource);
            for (int i = 0; i < 2; i++) {
                const bool fragment = i == 1;
                if (kind == RhiShaderKind::Hlsl) {
                    outputs[i]->assign(sources[i]->begin(), sources[i]->end());
                    continue;
                }
                std::string cerr;
                if (!dxbc(*sources[i], fragment ? Stage::Fragment : Stage::Vertex, *outputs[i], cerr)) {
                    result.error =
                        std::string(fragment ? "pixel" : "vertex") + " shader: HLSL compile failed: " + cerr;
                    return result;
                }
            }
        }
        result.ok = true;
        return result;
    }

    RhiCompute BuildRhiCompute(const ComputeResult& program, RhiShaderKind kind, const DxbcCompiler& dxbc) {
        RhiCompute result;
        result.kind = kind;
        result.uniformRegs = program.uniformRegs;
        if (!program.ok()) {
            result.error = program.stage.error.empty() ? "program did not transform" : program.stage.error;
            return result;
        }
        if (kind == RhiShaderKind::Dxbc && !dxbc) {
            result.error = "no HLSL compiler available";
            return result;
        }
        for (const SamplerDecl& s : program.stage.samplers) {
            if (s.image) {
                result.images.push_back(s.name);
                continue;
            }
            RhiSampler r;
            r.name = s.name;
            r.slot = s.binding;
            r.shadow = s.type.contains("Shadow");
            result.samplers.push_back(r);
        }
        std::vector<uint32_t> spirv;
        std::string log;
        if (!CompileToSpirv(program.stage.glsl, Stage::Compute, spirv, log)) {
            result.error = "compute shader failed to compile: " + log;
            return result;
        }
        if (kind == RhiShaderKind::SpirV) {
            result.code = Bytes(spirv);
            result.ok = true;
            return result;
        }
        std::string text, err;
        const bool converted = kind == RhiShaderKind::Msl ? CrossMsl(spirv, Stage::Compute, text, err)
                                                          : CrossHlsl(spirv, Stage::Compute, text, err);
        if (!converted) {
            result.error = "compute shader conversion failed: " + err;
            return result;
        }
        result.source = text;
        if (kind == RhiShaderKind::Dxbc) {
            if (!dxbc(text, Stage::Compute, result.code, err)) {
                result.error = "compute shader: HLSL compile failed: " + err;
                return result;
            }
        } else {
            result.code.assign(text.begin(), text.end());
        }
        result.ok = true;
        return result;
    }

    void MatchStageInterface(std::string& vs, std::string& ps) {
        auto body = [](const std::string& src, const char* structName, size_t& b, size_t& e) {
            b = src.find(std::string("struct ") + structName);
            if (b == std::string::npos)
                return false;
            b = src.find('{', b);
            e = src.find("};", b);
            return b != std::string::npos && e != std::string::npos;
        };
        size_t pb, pe, vb, ve;
        if (!body(ps, "SPIRV_Cross_Input", pb, pe) || !body(vs, "SPIRV_Cross_Output", vb, ve))
            return;
        std::string vsBody = vs.substr(vb, ve - vb);
        std::string add, init;
        std::istringstream in(ps.substr(pb + 1, pe - pb - 1));
        std::string line;
        while (std::getline(in, line)) {
            size_t colon = line.find(" : TEXCOORD");
            if (colon == std::string::npos)
                continue;
            std::string sem = line.substr(colon + 3);
            while (!sem.empty() && (sem.back() == ';' || sem.back() == '\r' || sem.back() == ' '))
                sem.pop_back();
            if (vsBody.contains(" : " + sem + ";"))
                continue;
            size_t t0 = line.find_first_not_of(" \t");
            std::string decl = line.substr(t0, colon - t0);
            size_t sp = decl.rfind(' ');
            if (sp == std::string::npos)
                continue;
            add += "    " + decl + " : " + sem + ";\n";
            init += "    stage_output." + decl.substr(sp + 1) + " = (" + decl.substr(0, sp) + ")0;\n";
        }
        if (!add.empty()) {
            vs.insert(ve, add);
            size_t ret = vs.rfind("return stage_output;");
            if (ret == std::string::npos)
                ret = vs.rfind("return ");
            if (ret != std::string::npos)
                vs.insert(ret, init);
            if (std::getenv("JSHADER_TRACE"))
                std::fprintf(stderr, "[jshader] vertex shader padded with missing pixel inputs\n");
        }

        if (!body(ps, "SPIRV_Cross_Input", pb, pe) || !body(vs, "SPIRV_Cross_Output", vb, ve))
            return;
        struct Member {
            std::string line, name, semantic;
            size_t nameAt = 0;
        };
        const auto parse = [](const std::string& text) {
            std::vector<Member> out;
            std::istringstream lines(text);
            std::string l;
            while (std::getline(lines, l)) {
                while (!l.empty() && (l.back() == '\r' || l.back() == ' '))
                    l.pop_back();
                const size_t colon = l.rfind(" : ");
                if (colon == std::string::npos || l.empty() || l.back() != ';')
                    continue;
                Member m;
                m.line = l;
                m.semantic = l.substr(colon + 3, l.size() - colon - 4);
                size_t nameEnd = colon;
                while (nameEnd > 0 && l[nameEnd - 1] == ' ')
                    nameEnd--;
                size_t nameBegin = nameEnd;
                while (nameBegin > 0 && l[nameBegin - 1] != ' ' && l[nameBegin - 1] != '\t')
                    nameBegin--;
                m.name = l.substr(nameBegin, nameEnd - nameBegin);
                m.nameAt = nameBegin;

                out.push_back(std::move(m));
            }
            return out;
        };
        const std::vector<Member> outs = parse(vs.substr(vb + 1, ve - vb - 1));
        const std::vector<Member> ins = parse(ps.substr(pb + 1, pe - pb - 1));
        const auto upper = [](std::string t) {
            for (char& c : t)
                c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            return t;
        };
        const auto find = [&](const std::vector<Member>& list, const std::string& sem) -> const Member* {
            for (const Member& m : list)
                if (upper(m.semantic) == upper(sem))
                    return &m;
            return nullptr;
        };
        const auto renamed = [](const Member& m, const std::string& name) {
            std::string base = m.name;
            const size_t bracket = base.find('[');
            const std::string suffix = bracket == std::string::npos ? "" : base.substr(bracket);
            std::string n = name;
            if (n.find('[') == std::string::npos)
                n += suffix;
            return m.line.substr(0, m.nameAt) + n + m.line.substr(m.nameAt + m.name.size());
        };
        std::string rebuilt = "\n";
        int unused = 0;
        for (const Member& o : outs) {
            if (const Member* in = find(ins, o.semantic)) {
                const std::string inName = in->name.substr(0, in->name.find('['));
                rebuilt += renamed(o, inName) + "\n";
            } else {
                rebuilt += renamed(o, "perlin_link_" + std::to_string(unused++)) + "\n";
            }
        }

        for (const Member& in : ins)
            if (!find(outs, in.semantic))
                rebuilt += in.line + "\n";
        if (std::getenv("JSHADER_TRACE") && rebuilt != ps.substr(pb + 1, pe - pb - 1))
            std::fprintf(stderr, "[jshader] pixel inputs reordered to the vertex outputs\n");
        ps.replace(pb + 1, pe - pb - 1, rebuilt);
    }

    DxbcCompiler SystemDxbcCompiler() {
#ifdef _WIN32
        return [](const std::string& hlsl, Stage stage, std::vector<uint8_t>& dxbc,
                  std::string& error) -> bool {
            const bool fragment = stage == Stage::Fragment;
            static HMODULE module = LoadLibraryA("d3dcompiler_47.dll");
            static pD3DCompile compile =
                module ? reinterpret_cast<pD3DCompile>(GetProcAddress(module, "D3DCompile")) : nullptr;
            if (!compile) {
                error = "d3dcompiler_47.dll is not available";
                return false;
            }

            std::filesystem::path cacheFile;
            try {
                uint64_t h = 1469598103934665603ull;
                for (unsigned char c : hlsl) {
                    h ^= c;
                    h *= 1099511628211ull;
                }
                h ^= stage == Stage::Compute ? 0x2545f4914f6cdd1dull
                     : fragment              ? 0x9e3779b97f4a7c15ull
                                             : 0x517cc1b727220a95ull;
                char name[48];
                std::snprintf(name, sizeof(name), "%016llx_%zx.dxbc", static_cast<unsigned long long>(h),
                              hlsl.size());
                const std::filesystem::path dir =
                    std::filesystem::temp_directory_path() / "perlin_dxbc_cache2";
                std::filesystem::create_directories(dir);
                cacheFile = dir / name;
                std::ifstream in(cacheFile, std::ios::binary);
                if (in) {
                    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                                               std::istreambuf_iterator<char>());
                    if (bytes.size() > 32) {
                        dxbc = std::move(bytes);
                        return true;
                    }
                }
            } catch (...) {
                cacheFile.clear();
            }
            const auto store = [&]() {
                if (cacheFile.empty())
                    return;
                try {
                    std::ofstream out(cacheFile, std::ios::binary | std::ios::trunc);
                    out.write(reinterpret_cast<const char*>(dxbc.data()),
                              static_cast<std::streamsize>(dxbc.size()));
                } catch (...) {}
            };
            const auto run = [&](const std::string& text, UINT flags, std::string& err) -> bool {
                ID3DBlob* code = nullptr;
                ID3DBlob* messages = nullptr;
                const HRESULT hr = compile(text.data(), text.size(), "javapack", nullptr, nullptr, "main",
                                           stage == Stage::Compute ? "cs_5_1"
                                           : fragment              ? "ps_5_1"
                                                                   : "vs_5_1",
                                           flags, 0, &code, &messages);
                if (FAILED(hr) || !code) {
                    err = messages ? std::string(static_cast<const char*>(messages->GetBufferPointer()),
                                                 messages->GetBufferSize())
                                   : "D3DCompile failed";

                    std::string errors;
                    size_t at = 0;
                    while (at < err.size()) {
                        size_t end = err.find('\n', at);
                        if (end == std::string::npos)
                            end = err.size();
                        const std::string line = err.substr(at, end - at);
                        if (line.contains("error") && errors.size() < 1200)
                            errors += line + "\n";
                        at = end + 1;
                    }
                    if (!errors.empty())
                        err = errors;
                    if (err.size() > 1200)
                        err.resize(1200);
                    if (messages)
                        messages->Release();
                    if (code)
                        code->Release();
                    return false;
                }
                const uint8_t* p = static_cast<const uint8_t*>(code->GetBufferPointer());
                dxbc.assign(p, p + code->GetBufferSize());
                code->Release();
                if (messages)
                    messages->Release();
                return true;
            };

            std::string looped;
            looped.reserve(hlsl.size() + 4096);
            for (size_t at = 0; at < hlsl.size();) {
                const size_t nl = hlsl.find('\n', at);
                const size_t end = nl == std::string::npos ? hlsl.size() : nl + 1;
                const size_t first = hlsl.find_first_not_of(" \t", at);
                if (first != std::string::npos && first < end &&
                    (hlsl.compare(first, 5, "for (") == 0 || hlsl.compare(first, 7, "while (") == 0))
                    looped.append(hlsl, at, first - at).append("[loop] ");
                else
                    looped.append(hlsl, at,
                                  first == std::string::npos || first >= end ? end - at : first - at);
                if (first != std::string::npos && first < end)
                    looped.append(hlsl, first, end - first);
                at = end;
            }
            std::string first, second, third;

            if (run(looped, D3DCOMPILE_OPTIMIZATION_LEVEL3, first) ||
                run(looped, D3DCOMPILE_OPTIMIZATION_LEVEL1, second) ||
                run(hlsl, D3DCOMPILE_OPTIMIZATION_LEVEL3, third)) {
                store();
                return true;
            }

            std::string fourth;
            if (run(hlsl, D3DCOMPILE_SKIP_OPTIMIZATION, fourth)) {
                store();
                return true;
            }
            error = first + (second.empty() || second == first ? "" : "\n(level 1) " + second) +
                    (third.empty() ? "" : "\n(unrolled) " + third) +
                    (fourth.empty() ? "" : "\n(without optimization) " + fourth);
            return false;
        };
#else
        return {};
#endif
    }

    std::string MergeHlslSamplers(const std::string& src,
                                  const std::function<std::string(const std::string& texture, bool comparison)>& keyOf) {
        struct Decl {
            size_t begin, end;
            std::string var, type;
        };
        const auto word = [](char ch) { return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_'; };
        std::vector<Decl> decls;
        for (const char* type : {"SamplerState ", "SamplerComparisonState "}) {
            const std::string t = type;
            for (size_t at = src.find(t); at != std::string::npos; at = src.find(t, at + 1)) {
                if (at > 0 && src[at - 1] != '\n')
                    continue;
                const size_t nameAt = at + t.size();
                size_t nameEnd = nameAt;
                while (nameEnd < src.size() && word(src[nameEnd]))
                    nameEnd++;
                const size_t lineEnd = src.find('\n', nameEnd);
                if (lineEnd == std::string::npos || src.find("register(", nameEnd) > lineEnd)
                    continue;
                decls.push_back({at, lineEnd + 1, src.substr(nameAt, nameEnd - nameAt), t});
            }
        }
        if (decls.size() <= 16)
            return src;
        std::map<std::string, std::string> renameTo, groupHead;
        std::vector<std::pair<size_t, size_t>> drop;
        for (const Decl& d : decls) {
            const size_t suffix = d.var.rfind("_iris_s");
            const std::string texture = suffix == std::string::npos ? d.var : d.var.substr(0, suffix);
            const std::string key = keyOf(texture, d.type == "SamplerComparisonState ");
            if (key.empty())
                continue;
            const auto [it, first] = groupHead.emplace(d.type + key, d.var);
            if (!first) {
                renameTo[d.var] = it->second;
                drop.emplace_back(d.begin, d.end);
            }
        }
        std::string out = src;
        std::sort(drop.begin(), drop.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        for (const auto& [b, e] : drop)
            out.erase(b, e - b);
        for (const auto& [from, to] : renameTo)
            for (size_t at = out.find(from); at != std::string::npos; at = out.find(from, at)) {
                const size_t end = at + from.size();
                if ((at > 0 && word(out[at - 1])) || (end < out.size() && word(out[end]))) {
                    at = end;
                    continue;
                }
                out.replace(at, from.size(), to);
                at += to.size();
            }
        return out;
    }
}
