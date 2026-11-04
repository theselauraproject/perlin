#include "client/PerlinInternal.hpp"
#include "client/Perlin.hpp"

#include "client/mods/ModManager.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_set>

namespace perlin {
    void Perlin::DrawModGfx(int fbW, int fbH, bool hudBand) {
        auto& mm = mods::ModManager::Instance();
        if (!renderer)
            return;

        if (hudBand) {
            std::unordered_set<std::string> seen;
            for (const mods::GfxTexture& t : mm.GfxTextures()) {
                if (!t.rgba ||
                    t.rgba->size() != static_cast<size_t>(t.width) * static_cast<size_t>(t.height) * 4)
                    continue;
                const std::string key = t.plugin + "/" + std::to_string(t.id);
                seen.insert(key);
                ModGfxTexture& slot = modGfxTextures[key];
                if (slot.gpu == 0 || slot.version != t.version) {
                    if (slot.gpu)
                        renderer->UiDestroyTexture(slot.gpu);
                    slot.gpu = renderer->UiCreateTexture(t.width, t.height, t.rgba->data(), t.linear);
                    slot.version = t.version;
                }
            }
            for (auto it = modGfxTextures.begin(); it != modGfxTextures.end();) {
                if (seen.contains(it->first)) {
                    ++it;
                    continue;
                }
                if (it->second.gpu)
                    renderer->UiDestroyTexture(it->second.gpu);
                it = modGfxTextures.erase(it);
            }
        }
        if (fbW <= 0 || fbH <= 0 || (replayCtl.Playing() && replayUi.hideHud))
            return;

        static std::vector<UiVertex> vertices;
        static std::vector<uint32_t> indices;
        for (const mods::GfxLayer& layer : mm.GfxLayers()) {
            if ((layer.z < abi::kGfxScreenZ) != hudBand)
                continue;
            if (!layer.data || layer.data->size() < sizeof(abi::GfxHeader))
                continue;
            const uint8_t* base = layer.data->data();
            abi::GfxHeader h;
            std::memcpy(&h, base, sizeof(h));
            const bool wide = (h.flags & abi::kGfxIndex32) != 0;
            const uint8_t* vtx = base + sizeof(h);
            const uint8_t* idx = vtx + static_cast<size_t>(h.vertexCount) * sizeof(abi::GfxVertex);
            const uint8_t* cmds = idx + static_cast<size_t>(h.indexCount) * (wide ? 4 : 2);
            const auto indexAt = [&](uint32_t i) {
                if (wide) {
                    uint32_t v;
                    std::memcpy(&v, idx + static_cast<size_t>(i) * 4, 4);
                    return v;
                }
                uint16_t v;
                std::memcpy(&v, idx + static_cast<size_t>(i) * 2, 2);
                return static_cast<uint32_t>(v);
            };
            for (uint32_t c = 0; c < h.commandCount; c++) {
                abi::GfxCmd cmd;
                std::memcpy(&cmd, cmds + static_cast<size_t>(c) * sizeof(cmd), sizeof(cmd));
                if (cmd.indexCount == 0)
                    continue;
                const int x0 = std::clamp(static_cast<int>(std::floor(cmd.clip[0])), 0, fbW);
                const int y0 = std::clamp(static_cast<int>(std::floor(cmd.clip[1])), 0, fbH);
                const int x1 = std::clamp(static_cast<int>(std::ceil(cmd.clip[2])), 0, fbW);
                const int y1 = std::clamp(static_cast<int>(std::ceil(cmd.clip[3])), 0, fbH);
                if (x1 <= x0 || y1 <= y0)
                    continue;
                uint32_t lo = UINT32_MAX, hi = 0;
                for (uint32_t i = 0; i < cmd.indexCount; i++) {
                    const uint32_t v = indexAt(cmd.indexOffset + i);
                    lo = std::min(lo, v);
                    hi = std::max(hi, v);
                }
                vertices.resize(static_cast<size_t>(hi - lo) + 1);
                for (uint32_t v = lo; v <= hi; v++) {
                    abi::GfxVertex in;
                    std::memcpy(&in, vtx + (static_cast<size_t>(cmd.vertexOffset) + v) * sizeof(in),
                                sizeof(in));
                    vertices[v - lo] = {in.x, in.y, 0.0f, in.color, in.u, in.v};
                }
                indices.resize(cmd.indexCount);
                for (uint32_t i = 0; i < cmd.indexCount; i++)
                    indices[i] = indexAt(cmd.indexOffset + i) - lo;
                const uint32_t mesh =
                    renderer->UiCreateFrameMesh(vertices.data(), static_cast<uint32_t>(vertices.size()),
                                                indices.data(), static_cast<uint32_t>(indices.size()));
                if (!mesh)
                    continue;
                UiDraw draw;
                draw.mesh = mesh;
                if (cmd.texture) {
                    const auto it = modGfxTextures.find(layer.plugin + "/" + std::to_string(cmd.texture));
                    if (it != modGfxTextures.end())
                        draw.texture = it->second.gpu;
                }
                draw.premultiplied = false;
                draw.blend = 1;
                draw.scissor = true;
                draw.scissorRect[0] = x0;
                draw.scissorRect[1] = y0;
                draw.scissorRect[2] = x1 - x0;
                draw.scissorRect[3] = y1 - y0;
                renderer->UiSubmit(hudBand ? UiLayer::Gui : UiLayer::Top, draw);
                renderer->UiDestroyMesh(mesh);
            }
        }
    }
}
