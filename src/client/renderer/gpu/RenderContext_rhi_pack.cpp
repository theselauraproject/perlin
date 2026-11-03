#include <algorithm>
#include <cstdlib>
#include <map>
#include <functional>
#include <set>
#include "util/Env.hpp"
#include "client/renderer/gpu/RenderContext_rhi.hpp"

#include <iostream>
#include "util/Log.hpp"

namespace perlin {
    int RenderContext_rhi::PackShadowLimit() const {
        if (const char* forced = PERLIN_ENV("PERLIN_PACK_SHADOW_RES"))
            return std::atoi(forced);
        switch (jpackShadowQuality) {
        case 0:
            return 512;
        case 1:
            return 1024;
        case 2:
            return deviceTier <= rhi::Tier::Low ? 1024 : 0;
        default:
            return 0;
        }
    }

    int RenderContext_rhi::PackShadowInterval() const {
        if (const char* forced = PERLIN_ENV("PERLIN_PACK_SHADOW_INTERVAL"))
            return std::max(1, std::atoi(forced));
        return 1;
    }

    bool RenderContext_rhi::AttachJavaPack() {
        if (!jpack)
            return false;
        jpack->SetFluidTiles(static_cast<int>(frame.waterTile), static_cast<int>(frame.waterFlowTile),
                             static_cast<int>(frame.lavaTile), -1);
        // the pack's vertex stream is built next to the normal one whenever a chunk mesh uploads
        jpackStamp++;
        meshes.SetExtension(
            gpu::kPackVertexBytes, [this](const Tessellator& tess, std::vector<uint8_t>& out) {
                // usually converted already on the meshing thread
                if (tess.packStamp == jpackStamp.load() &&
                    tess.packVertices.size() == tess.Vertices().size() * gpu::kPackVertexBytes) {
                    out = tess.packVertices;
                    return;
                }
                if (jpack)
                    jpack->Convert(tess, out);
            });
        Tessellator::SetCaptureQuadInfo(true);
        jpackReady = false;
        return true;
    }

    bool RenderContext_rhi::SetJavaShaderPack(const std::string& path,
                                              const std::map<std::string, std::string>& options,
                                              std::string& error) {
        ClearJavaShaderPack();
        // the hand-made approximation of complementary is only a debugging aid now: every pack runs for real
        if (PERLIN_ENV("PERLIN_JAVA_BAKED"))
            return SetBakedJavaShaderPack(path, options, error);
        if (!initialized) {
            error = "renderer is not ready";
            return false;
        }
        if (atlasSource.empty() || atlasTiles <= 0) {
            error = "the block atlas is not loaded yet (join a world first, then enable the pack)";
            jpackError = error;
            return false;
        }
        ClearJavaShaderPack();
        jpackPath = path;
        jpackOptions = options;
        jpackError.clear();
        auto pack = std::make_unique<gpu::JavaPack>(gpu, meshes);
        gpu::JavaPack::Config config;
        config.path = path;
        for (const auto& kv : options)
            config.options[kv.first] = kv.second;
        config.dimension = jpackDimension;
        config.blockName = blockNames;
        config.blockEmission = blockEmission;
        config.shadowResolutionLimit = PackShadowLimit();
        config.shadowInterval = PackShadowInterval();
        config.shadowCache = !PERLIN_ENV("PERLIN_PACK_NO_SHADOW_CACHE");
        if (!pack->Load(config, error)) {
            jpackError = error;
            jpackPath.clear();
            return false;
        }
        pack->SetAtlas(atlasSource.data(), tileSize, atlasTiles);
        {
            std::unique_lock jpackLock(jpackMutex);
            jpack = std::move(pack);
        }
        AttachJavaPack();
        return true;
    }

    void RenderContext_rhi::ClearJavaShaderPack() {
        ClearBakedJavaShaderPack();
        if (!jpack && jpackPath.empty())
            return;
        const bool had = jpack != nullptr;
        {
            std::unique_lock jpackLock(jpackMutex);
            jpack.reset();
        }
        jpackReady = false;
        jpackGathered = false;
        jpackPath.clear();
        Tessellator::SetCaptureQuadInfo(false);
        // drops every mesh: the caller re-meshes without the extra stream
        if (initialized && had)
            meshes.SetExtension(0, {});
    }

    void RenderContext_rhi::SetJavaShaderPackDimension(const std::string& folder) {
        if (folder == jpackDimension)
            return;
        jpackDimension = folder;
        if (jpack && !jpackPath.empty()) {
            // the pack's programs depend on the dimension folder: load it again
            std::string error;
            const std::string path = jpackPath;
            const auto options = jpackOptions;
            if (!SetJavaShaderPack(path, options, error))
                Log("javapack", "{}", error);
        }
    }

    std::vector<JavaPackOption> RenderContext_rhi::JavaShaderPackOptions() const {
        std::vector<JavaPackOption> out;
        if (!jpack || !jpack->Instance())
            return out;
        const jshader::ShaderPackInstance& inst = *jpack->Instance();
        std::map<std::string, const jshader::ShaderOption*> byName;
        for (const jshader::ShaderOption& o : inst.Options())
            if (o.kind != jshader::ShaderOption::Kind::Const &&
                (o.kind == jshader::ShaderOption::Kind::Toggle || !o.values.empty()))
                byName[o.name] = &o;
        std::set<std::string> done;
        auto emit = [&](const std::string& name, const std::string& group) {
            const auto it = byName.find(name);
            if (it == byName.end() || !done.insert(name).second)
                return;
            const jshader::ShaderOption& o = *it->second;
            JavaPackOption r;
            r.name = o.name;
            r.group = group;
            const std::string label = inst.Lang("option." + o.name);
            r.label = label.empty() ? o.name : label;
            const std::string tip = inst.Lang("option." + o.name + ".comment");
            r.comment = tip.empty() ? o.comment : tip;
            r.toggle = o.kind == jshader::ShaderOption::Kind::Toggle;
            const auto ov = inst.Overrides().find(o.name);
            if (r.toggle) {
                bool on = o.defaultOn;
                if (ov != inst.Overrides().end())
                    on = ov->second == "true" || ov->second == "1" || ov->second == "on";
                r.current = on ? "true" : "false";
                r.values = {"false", "true"};
            } else {
                r.current = ov != inst.Overrides().end() ? ov->second : o.defaultValue;
                r.values = o.values;
            }
            out.push_back(std::move(r));
        };
        // screen layout from shaders.properties: "screen" lists options and [sub screens]
        std::set<std::string> visiting;
        std::function<void(const std::string&, const std::string&)> walk = [&](const std::string& key,
                                                                               const std::string& title) {
            if (!visiting.insert(key).second)
                return;
            for (const std::string& w : jshader::SplitWords(inst.RawProps().Get(key))) {
                if (w.size() > 2 && w.front() == '[' && w.back() == ']') {
                    const std::string sub = w.substr(1, w.size() - 2);
                    const std::string t = inst.Lang("screen." + sub);
                    walk("screen." + sub, t.empty() ? sub : t);
                } else if (w != "<empty>" && w != "<profile>" && w != "*" &&
                           w.compare(0, 8, "<profile") != 0) {
                    emit(w, title);
                }
            }
            visiting.erase(key);
        };
        walk("screen", "");
        if (byName.size() != done.size()) {
            for (const auto& kv : byName)
                emit(kv.first, "Other");
        }
        return out;
    }
}
