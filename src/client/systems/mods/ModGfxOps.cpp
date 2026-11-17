#include "client/systems/mods/ModInternal.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <random>
#include <set>
#include "util/Log.hpp"

namespace perlin::mods {

    namespace {
        bool ValidGfx(const std::vector<uint8_t>& blob) {
            if (blob.size() < sizeof(abi::GfxHeader) || blob.size() > abi::kGfxMaxBytes)
                return false;
            abi::GfxHeader h;
            std::memcpy(&h, blob.data(), sizeof(h));
            const bool wide = (h.flags & abi::kGfxIndex32) != 0;
            if (h.vertexCount > abi::kGfxMaxVertices || h.indexCount > 4u * abi::kGfxMaxVertices ||
                h.commandCount > 65536)
                return false;
            const uint64_t need = sizeof(h) + static_cast<uint64_t>(h.vertexCount) * sizeof(abi::GfxVertex) +
                                  static_cast<uint64_t>(h.indexCount) * (wide ? 4 : 2) +
                                  static_cast<uint64_t>(h.commandCount) * sizeof(abi::GfxCmd);
            if (need != blob.size())
                return false;
            const uint8_t* idx =
                blob.data() + sizeof(h) + static_cast<size_t>(h.vertexCount) * sizeof(abi::GfxVertex);
            const uint8_t* cmds = idx + static_cast<size_t>(h.indexCount) * (wide ? 4 : 2);
            for (uint32_t i = 0; i < h.commandCount; i++) {
                abi::GfxCmd c;
                std::memcpy(&c, cmds + static_cast<size_t>(i) * sizeof(c), sizeof(c));
                if (c.indexCount % 3 != 0 ||
                    static_cast<uint64_t>(c.indexOffset) + c.indexCount > h.indexCount ||
                    static_cast<uint64_t>(c.vertexOffset) > h.vertexCount)
                    return false;
                for (float v : c.clip)
                    if (!std::isfinite(v))
                        return false;
                for (uint32_t k = 0; k < c.indexCount; k++) {
                    uint32_t at = 0;
                    if (wide) {
                        std::memcpy(&at, idx + (static_cast<size_t>(c.indexOffset) + k) * 4, 4);
                    } else {
                        uint16_t s = 0;
                        std::memcpy(&s, idx + (static_cast<size_t>(c.indexOffset) + k) * 2, 2);
                        at = s;
                    }
                    if (static_cast<uint64_t>(at) + c.vertexOffset >= h.vertexCount)
                        return false;
                }
            }
            return true;
        }
    }

    void ModManager::RegisterGfxOps() {
        Register(abi::Op::GfxImageLoad, [this](CallContext&, abi::Reader& r, abi::Writer& w) {
            const std::string path = r.str();
            if (!r.Ok() || path.empty() || path.contains(".."))
                return abi::Status::BadArguments;
            ImageLoader loader;
            {
                std::lock_guard lock(mutex);
                loader = imageLoader;
            }
            if (!loader)
                return abi::Status::Unavailable;
            int width = 0, height = 0;
            std::vector<uint8_t> rgba;
            if (!loader(path, width, height, rgba) || width <= 0 || height <= 0 ||
                width > static_cast<int32_t>(abi::kGfxMaxTextureSide) ||
                height > static_cast<int32_t>(abi::kGfxMaxTextureSide) ||
                rgba.size() != static_cast<size_t>(width) * static_cast<size_t>(height) * 4)
                return abi::Status::NotFound;
            w.put<int32_t>(width).put<int32_t>(height).bytes(rgba.data(), rgba.size());
            return abi::Status::Ok;
        });

        Register(abi::Op::GfxTextureCreate, [this](CallContext& ctx, abi::Reader& r, abi::Writer& w) {
            const int32_t width = r.get<int32_t>(), height = r.get<int32_t>();
            const bool linear = r.boolean();
            std::vector<uint8_t> rgba = r.bytes();
            if (!r.Ok() || width <= 0 || height <= 0 ||
                width > static_cast<int32_t>(abi::kGfxMaxTextureSide) ||
                height > static_cast<int32_t>(abi::kGfxMaxTextureSide) ||
                rgba.size() != static_cast<size_t>(width) * static_cast<size_t>(height) * 4)
                return abi::Status::BadArguments;
            std::lock_guard lock(mutex);
            auto* mod = static_cast<Plugin*>(ctx.user);
            if (mod->reg.textures.size() >= abi::kGfxMaxTextures ||
                mod->reg.textureBytes + rgba.size() > abi::kGfxMaxTextureBytes)
                return abi::Status::Failed;
            const uint32_t id = mod->reg.nextTexture++;
            Plugin::Registrations::Tex tex;
            tex.w = width;
            tex.h = height;
            tex.linear = linear;
            tex.version = 1;
            mod->reg.textureBytes += rgba.size();
            tex.rgba = std::make_shared<const std::vector<uint8_t>>(std::move(rgba));
            mod->reg.textures[id] = std::move(tex);
            w.put<uint32_t>(id);
            return abi::Status::Ok;
        });

        Register(abi::Op::GfxTextureUpdate, [this](CallContext& ctx, abi::Reader& r, abi::Writer&) {
            const uint32_t id = r.get<uint32_t>();
            const int32_t x = r.get<int32_t>(), y = r.get<int32_t>(), width = r.get<int32_t>(),
                          height = r.get<int32_t>();
            std::vector<uint8_t> rgba = r.bytes();
            if (!r.Ok() || width <= 0 || height <= 0 || x < 0 || y < 0 ||
                rgba.size() != static_cast<size_t>(width) * static_cast<size_t>(height) * 4)
                return abi::Status::BadArguments;
            std::lock_guard lock(mutex);
            auto* mod = static_cast<Plugin*>(ctx.user);
            const auto it = mod->reg.textures.find(id);
            if (it == mod->reg.textures.end())
                return abi::Status::NotFound;
            Plugin::Registrations::Tex& tex = it->second;
            if (static_cast<int64_t>(x) + width > tex.w || static_cast<int64_t>(y) + height > tex.h)
                return abi::Status::BadArguments;
            std::vector<uint8_t>* target = nullptr;
            if (tex.rgba && tex.rgba.use_count() == 1) {
                target = const_cast<std::vector<uint8_t>*>(tex.rgba.get());
            } else {
                auto copy = std::make_shared<std::vector<uint8_t>>(*tex.rgba);
                target = copy.get();
                tex.rgba = std::move(copy);
            }
            for (int32_t row = 0; row < height; row++)
                std::memcpy(target->data() + (static_cast<size_t>(y + row) * static_cast<size_t>(tex.w) +
                                              static_cast<size_t>(x)) *
                                                 4,
                            rgba.data() + static_cast<size_t>(row) * static_cast<size_t>(width) * 4,
                            static_cast<size_t>(width) * 4);
            if (tex.rx1 <= tex.rx0 || tex.ry1 <= tex.ry0) {
                tex.rx0 = x;
                tex.ry0 = y;
                tex.rx1 = x + width;
                tex.ry1 = y + height;
            } else {
                tex.rx0 = std::min(tex.rx0, x);
                tex.ry0 = std::min(tex.ry0, y);
                tex.rx1 = std::max(tex.rx1, x + width);
                tex.ry1 = std::max(tex.ry1, y + height);
            }
            tex.version++;
            return abi::Status::Ok;
        });

        Register(abi::Op::GfxTextureDestroy, [this](CallContext& ctx, abi::Reader& r, abi::Writer&) {
            const uint32_t id = r.get<uint32_t>();
            if (!r.Ok())
                return abi::Status::BadArguments;
            std::lock_guard lock(mutex);
            auto* mod = static_cast<Plugin*>(ctx.user);
            const auto it = mod->reg.textures.find(id);
            if (it == mod->reg.textures.end())
                return abi::Status::NotFound;
            mod->reg.textureBytes -=
                std::min<uint64_t>(mod->reg.textureBytes, it->second.rgba ? it->second.rgba->size() : 0);
            mod->reg.textures.erase(it);
            return abi::Status::Ok;
        });

        Register(abi::Op::GfxSubmit, [this](CallContext& ctx, abi::Reader& r, abi::Writer&) {
            const std::string name = r.str();
            const int32_t z = r.get<int32_t>();
            std::vector<uint8_t> blob = r.bytes();
            if (!r.Ok() || name.empty() || name.size() > 64 || !ValidGfx(blob))
                return abi::Status::BadArguments;
            std::lock_guard lock(mutex);
            auto* mod = static_cast<Plugin*>(ctx.user);
            if (mod->reg.gfxLayers.size() >= abi::kGfxMaxLayers && !mod->reg.gfxLayers.contains(name))
                return abi::Status::Failed;
            GfxLayer& layer = mod->reg.gfxLayers[name];
            layer.plugin = ctx.plugin;
            layer.name = name;
            layer.z = z;
            layer.data = std::make_shared<const std::vector<uint8_t>>(std::move(blob));
            layer.serial = ++mod->serial;
            return abi::Status::Ok;
        });

        Register(abi::Op::GfxClear, [this](CallContext& ctx, abi::Reader& r, abi::Writer&) {
            const std::string name = r.str();
            if (!r.Ok())
                return abi::Status::BadArguments;
            std::lock_guard lock(mutex);
            static_cast<Plugin*>(ctx.user)->reg.gfxLayers.erase(name);
            return abi::Status::Ok;
        });

        Register(abi::Op::InputGrab, [this](CallContext& ctx, abi::Reader& r, abi::Writer&) {
            const bool mouse = r.boolean(), keyboard = r.boolean();
            if (!r.Ok())
                return abi::Status::BadArguments;
            std::lock_guard lock(mutex);
            static_cast<Plugin*>(ctx.user)->reg.grab = static_cast<uint8_t>((mouse ? 1 : 0) | (keyboard ? 2 : 0));
            RefreshGrabs();
            return abi::Status::Ok;
        });
    }
}
