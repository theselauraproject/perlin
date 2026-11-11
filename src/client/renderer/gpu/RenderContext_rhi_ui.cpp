#include "client/renderer/gpu/RenderContext_rhi.hpp"

#include "client/renderer/MaterialDb.hpp"

#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include "util/Log.hpp"

namespace perlin {
    namespace {
        conduit::blend_factor ToRhi(BlendFactor f) {
            switch (f) {
            case BlendFactor::Zero:
                return conduit::blend_factor::zero;
            case BlendFactor::One:
                return conduit::blend_factor::one;
            case BlendFactor::SrcColor:
                return conduit::blend_factor::src_color;
            case BlendFactor::InvSrcColor:
                return conduit::blend_factor::inv_src_color;
            case BlendFactor::SrcAlpha:
                return conduit::blend_factor::src_alpha;
            case BlendFactor::InvSrcAlpha:
                return conduit::blend_factor::inv_src_alpha;
            case BlendFactor::DstColor:
                return conduit::blend_factor::dst_color;
            case BlendFactor::InvDstColor:
                return conduit::blend_factor::inv_dst_color;
            case BlendFactor::DstAlpha:
                return conduit::blend_factor::dst_alpha;
            case BlendFactor::InvDstAlpha:
                return conduit::blend_factor::inv_dst_alpha;
            case BlendFactor::SrcAlphaSaturate:
                return conduit::blend_factor::src_alpha_saturate;
            }
            return conduit::blend_factor::one;
        }

        conduit::compare_op ToRhi(DepthFunc f) {
            switch (f) {
            case DepthFunc::Less:
                return conduit::compare_op::less;
            case DepthFunc::Equal:
                return conduit::compare_op::equal;
            case DepthFunc::Always:
                return conduit::compare_op::always;
            case DepthFunc::Greater:
                return conduit::compare_op::greater;
            case DepthFunc::GreaterEqual:
                return conduit::compare_op::greater_equal;
            case DepthFunc::NotEqual:
                return conduit::compare_op::not_equal;
            case DepthFunc::Never:
                return conduit::compare_op::never;
            case DepthFunc::LessEqual:
                break;
            }
            return conduit::compare_op::less_equal;
        }

        constexpr uint32_t kUiPageVerts = 1u << 18;
        constexpr uint32_t kUiPageIndices = 1u << 20;
    }

    RenderContext_rhi::DrawState RenderContext_rhi::Base(Program program, bool depth, bool depthWrite,
                                                         conduit::compare_op op) const {
        DrawState s;
        s.program = program;
        s.layout = programLayouts[static_cast<size_t>(program)];
        s.depthTest = depth;
        s.depthWrite = depth && depthWrite;
        s.depthOp = op;
        s.cull = conduit::cull_mode::none;
        s.front = frontFace;
        s.mask = conduit::color_mask::none;
        return s;
    }

    void RenderContext_rhi::ApplyMaterial(DrawState& state, const char* name) const {
        const material::Def* def = material::Registry::Instance().Get().Find(name);
        if (!def)
            return;
        const bool hadDepthWrite = state.depthWrite;
        state.mask = conduit::color_mask::none;
        if (def->ColorWrite())
            state.mask = state.mask | conduit::color_mask::rgb;
        if (def->AlphaWrite())
            state.mask = state.mask | conduit::color_mask::a;
        state.depthWrite = def->DepthWrite() && hadDepthWrite;
        state.blend = def->Blending();
        if (state.blend) {
            state.srcColor = ToRhi(def->blendSrc);
            state.dstColor = ToRhi(def->blendDst);
            state.srcAlpha = ToRhi(def->blendSrc == BlendFactor::SrcAlpha ? BlendFactor::One : def->blendSrc);
            state.dstAlpha = ToRhi(def->blendDst);
        }
    }

    RenderContext_rhi::TexRec* RenderContext_rhi::LookupUiTexture(uint32_t id) {
        const uint32_t slot = id & kUiTexSlotMask;
        if (slot == 0 || slot > uiTextures.size() || !uiTextures[slot - 1].used)
            return nullptr;
        if (slot <= uiTextureGen.size() && uiTextureGen[slot - 1] != (id >> kUiTexSlotBits))
            return nullptr;
        return &uiTextures[slot - 1];
    }

    uint32_t RenderContext_rhi::StoreUiTexture(const TexRec& rec) {
        uiTextureGen.resize(uiTextures.size(), 0);
        constexpr uint32_t kGenMask = (1u << (32 - kUiTexSlotBits)) - 1u;
        for (size_t i = 0; i < uiTextures.size(); i++)
            if (!uiTextures[i].used) {
                uiTextures[i] = rec;
                uiTextureGen[i] = (uiTextureGen[i] + 1) & kGenMask;
                return static_cast<uint32_t>(i + 1) | (uiTextureGen[i] << kUiTexSlotBits);
            }
        uiTextures.push_back(rec);
        uiTextureGen.push_back(0);
        return static_cast<uint32_t>(uiTextures.size());
    }

    uint32_t RenderContext_rhi::UiCreateTextureMipped(int w, int h, const std::vector<const uint8_t*>& levels) {
        if (!initialized || w <= 0 || h <= 0 || levels.empty() || !levels[0])
            return 0;
        TexRec rec;
        if (!MakeTexture(rec, static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1,
                         static_cast<uint32_t>(levels.size()), conduit::format::rgba8_unorm,
                         gpu::SamplerKind::LinearMipClamp, "ui texture (mipped)"))
            return 0;
        for (size_t l = 0; l < levels.size(); l++)
            if (levels[l])
                gpu.WriteTexture(rec.handle, 0, 0, 0, static_cast<uint32_t>(std::max(1, w >> l)),
                                 static_cast<uint32_t>(std::max(1, h >> l)), levels[l], 0, static_cast<uint32_t>(l));
        return StoreUiTexture(rec);
    }

    uint32_t RenderContext_rhi::UiCreateTexture(int w, int h, const uint8_t* rgba, bool linear, bool repeat) {
        if (!initialized || w <= 0 || h <= 0 || !rgba)
            return 0;
        TexRec rec;
        const gpu::SamplerKind sampler =
            linear ? (repeat ? gpu::SamplerKind::LinearRepeat : gpu::SamplerKind::LinearClamp)
                   : (repeat ? gpu::SamplerKind::PointRepeat : gpu::SamplerKind::PointClamp);
        if (!MakeTexture(rec, static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1, 1,
                         conduit::format::rgba8_unorm, sampler, "ui texture"))
            return 0;
        gpu.WriteTexture(rec.handle, 0, 0, 0, static_cast<uint32_t>(w), static_cast<uint32_t>(h), rgba);
        return StoreUiTexture(rec);
    }

    bool RenderContext_rhi::UiUpdateTexture(uint32_t id, int x, int y, int w, int h, const uint8_t* rgba,
                                            int rowBytes) {
        TexRec* found = initialized ? LookupUiTexture(id) : nullptr;
        if (!found || !rgba || w <= 0 || h <= 0 || x < 0 || y < 0)
            return false;
        TexRec& rec = *found;
        if (static_cast<uint32_t>(x + w) > rec.w || static_cast<uint32_t>(y + h) > rec.h || rec.mips != 1)
            return false;
        const size_t pitch = rowBytes > 0 ? static_cast<size_t>(rowBytes) : static_cast<size_t>(w) * 4;
        gpu.WriteTexture(rec.handle, 0, static_cast<uint32_t>(x), static_cast<uint32_t>(y), static_cast<uint32_t>(w),
                         static_cast<uint32_t>(h), rgba, static_cast<uint32_t>(pitch));
        return true;
    }

    void RenderContext_rhi::UiDestroyTexture(uint32_t id) {
        if (TexRec* rec = LookupUiTexture(id))
            FreeTexture(*rec);
    }

    RenderContext_rhi::UiMeshGpu* RenderContext_rhi::LookupUiMesh(uint32_t id) {
        const uint32_t slot = id & kUiMeshSlotMask;
        if (slot == 0 || slot > uiMeshes.size())
            return nullptr;
        UiMeshGpu& mesh = uiMeshes[slot - 1];
        if (!mesh.used || mesh.generation != (id >> kUiMeshSlotBits))
            return nullptr;
        return &mesh;
    }

    uint32_t RenderContext_rhi::StoreUiMesh(const UiMeshGpu& mesh) {
        uint32_t slot;
        if (!freeUiMeshes.empty()) {
            slot = freeUiMeshes.back();
            freeUiMeshes.pop_back();
        } else {
            if (uiMeshes.size() >= kUiMeshSlotMask)
                return 0;
            slot = static_cast<uint32_t>(uiMeshes.size());
            uiMeshes.emplace_back();
        }
        const uint32_t generation = uiMeshes[slot].generation;
        uiMeshes[slot] = mesh;
        uiMeshes[slot].generation = generation;
        uiMeshes[slot].used = true;
        if (!mesh.transient)
            staticUiMeshes++;
        return (generation << kUiMeshSlotBits) | (slot + 1);
    }

    void RenderContext_rhi::ReleaseUiMeshSlot(uint32_t slot) {
        UiMeshGpu& mesh = uiMeshes[slot];
        if (!mesh.used)
            return;
        if (!mesh.transient) {
            if (mesh.page >= 0 && mesh.page < static_cast<int>(uiPages.size()) &&
                uiPages[static_cast<size_t>(mesh.page)].live) {
                uiPages[static_cast<size_t>(mesh.page)].vb.Free(mesh.vFirst, mesh.vCount);
                uiPages[static_cast<size_t>(mesh.page)].ib.Free(mesh.iFirst, mesh.iCount);
            }
            staticUiMeshes--;
        }
        const uint32_t next = (mesh.generation + 1) & ((1u << (32 - kUiMeshSlotBits)) - 1u);
        mesh = {};
        mesh.generation = next;
        freeUiMeshes.push_back(slot);
    }

    bool RenderContext_rhi::AllocUiMesh(uint32_t vertices, uint32_t indices, UiMeshGpu& out) {
        for (size_t i = 0; i < uiPages.size(); i++) {
            UiPage& p = uiPages[i];
            if (!p.live)
                continue;
            uint32_t v = 0, ix = 0;
            if (!p.vb.Alloc(vertices, v))
                continue;
            if (!p.ib.Alloc(indices, ix)) {
                p.vb.Free(v, vertices);
                continue;
            }
            out.page = static_cast<int>(i);
            out.vFirst = v;
            out.iFirst = ix;
            return true;
        }
        UiPage page;
        const uint32_t vCap = std::max(kUiPageVerts, vertices), iCap = std::max(kUiPageIndices, indices);
        if (!page.vb.Create(gpu.Dev(), vCap, sizeof(UiVertex), conduit::buffer_usage::vertex, "ui vertices") ||
            !page.ib.Create(gpu.Dev(), iCap, sizeof(uint32_t), conduit::buffer_usage::index, "ui indices")) {
            page.vb.Destroy(gpu.Dev());
            page.ib.Destroy(gpu.Dev());
            return false;
        }
        page.live = true;
        size_t at = uiPages.size();
        for (size_t i = 0; i < uiPages.size(); i++)
            if (!uiPages[i].live) {
                at = i;
                break;
            }
        if (at == uiPages.size())
            uiPages.push_back(std::move(page));
        else
            uiPages[at] = std::move(page);
        UiPage& p = uiPages[at];
        uint32_t v = 0, ix = 0;
        p.vb.Alloc(vertices, v);
        p.ib.Alloc(indices, ix);
        out.page = static_cast<int>(at);
        out.vFirst = v;
        out.iFirst = ix;
        return true;
    }

    uint32_t RenderContext_rhi::UiCreateMesh(const UiVertex* vertices, uint32_t vertexCount,
                                             const uint32_t* indices, uint32_t indexCount) {
        if (!initialized || vertexCount == 0 || indexCount == 0 || !vertices || !indices)
            return 0;
        UiMeshGpu mesh;
        if (!AllocUiMesh(vertexCount, indexCount, mesh))
            return 0;
        UiPage& p = uiPages[static_cast<size_t>(mesh.page)];
        mesh.vCount = vertexCount;
        mesh.iCount = indexCount;
        mesh.cpuVertices = std::make_shared<const std::vector<UiVertex>>(vertices, vertices + vertexCount);
        mesh.cpuIndices = std::make_shared<const std::vector<uint32_t>>(indices, indices + indexCount);
        gpu.WriteBuffer(p.vb, static_cast<uint64_t>(mesh.vFirst) * sizeof(UiVertex), vertices,
                        static_cast<uint64_t>(vertexCount) * sizeof(UiVertex));
        gpu.WriteBuffer(p.ib, static_cast<uint64_t>(mesh.iFirst) * sizeof(uint32_t), indices,
                        static_cast<uint64_t>(indexCount) * sizeof(uint32_t));
        const uint32_t id = StoreUiMesh(mesh);
        if (!id) {
            p.vb.Free(mesh.vFirst, vertexCount);
            p.ib.Free(mesh.iFirst, indexCount);
            return 0;
        }
        if (staticUiMeshes >= staticUiMeshWarnAt) {
            Log("render", "{} UI meshes alive", staticUiMeshes);
            staticUiMeshWarnAt *= 2;
        }
        return id;
    }

    uint32_t RenderContext_rhi::UiCreateFrameMesh(const UiVertex* vertices, uint32_t vertexCount,
                                                  const uint32_t* indices, uint32_t indexCount) {
        if (!initialized || !frameOpen || vertexCount == 0 || indexCount == 0 || !vertices || !indices)
            return 0;
        UiMeshGpu mesh;
        mesh.iCount = indexCount;
        mesh.transient = true;
        if (PackRunning()) {
            mesh.cpuVertices =
                std::make_shared<const std::vector<UiVertex>>(vertices, vertices + vertexCount);
            mesh.cpuIndices = std::make_shared<const std::vector<uint32_t>>(indices, indices + indexCount);
            mesh.lazyRing = true;
        } else {
            mesh.tv = gpu.Ring(static_cast<uint64_t>(vertexCount) * sizeof(UiVertex), 16);
            mesh.ti = gpu.Ring(static_cast<uint64_t>(indexCount) * sizeof(uint32_t), 4);
            if (!mesh.tv.data || !mesh.ti.data)
                return 0;
            std::memcpy(mesh.tv.data, vertices, static_cast<size_t>(vertexCount) * sizeof(UiVertex));
            std::memcpy(mesh.ti.data, indices, static_cast<size_t>(indexCount) * sizeof(uint32_t));
        }
        const uint32_t id = StoreUiMesh(mesh);
        if (id)
            frameUiMeshes.push_back(id);
        return id;
    }

    void RenderContext_rhi::UiDestroyMesh(uint32_t id) {
        if (LookupUiMesh(id))
            ReleaseUiMeshSlot((id & kUiMeshSlotMask) - 1);
    }

    void RenderContext_rhi::ReleaseFrameMeshes() {
        for (uint32_t id : frameUiMeshes)
            UiDestroyMesh(id);
        frameUiMeshes.clear();
    }

    void RenderContext_rhi::DestroyUi() {
        for (size_t i = 0; i < uiMeshes.size(); i++)
            ReleaseUiMeshSlot(static_cast<uint32_t>(i));
        frameUiMeshes.clear();
        staticUiMeshes = 0;
        for (TexRec& t : uiTextures)
            if (t.used)
                FreeTexture(t);
        uiMeshes.clear();
        freeUiMeshes.clear();
        uiTextures.clear();
        uiTextureGen.clear();
        for (UiPage& p : uiPages) {
            p.vb.Destroy(gpu.Dev());
            p.ib.Destroy(gpu.Dev());
        }
        uiPages.clear();
    }

    bool RenderContext_rhi::RouteToJavaPack(UiLayer layer, const UiDraw& draw, const UiMeshGpu& mesh,
                                            conduit::texture_handle texture, bool glint) {
        if (layer != UiLayer::World && layer != UiLayer::WorldLate && layer != UiLayer::Hand)
            return false;
        if (draw.material == DrawMaterial::Nametag || draw.colorMask || !mesh.cpuVertices || !mesh.cpuIndices)
            return false;
        using K = gpu::JavaPack::ExternalDraw::Kind;
        gpu::JavaPack::ExternalDraw d;
        const bool translucent = draw.blend == 1 || draw.premultiplied || draw.customBlend;
        const bool lightning =
            draw.entityType && std::strcmp(draw.entityType, "minecraft:lightning_bolt") == 0;
        constexpr float kTenth = 0.1f, kNonZero = 0.0001f;
        if (glint) {
            d.kind = layer == UiLayer::Hand ? K::HandGlint : K::Glint;
            d.alphaRef = kNonZero;
        } else if (layer == UiLayer::Hand) {
            d.kind = translucent ? K::HandTranslucent : K::Hand;
            d.alphaRef = kTenth;
        } else if (draw.packKind == UiDraw::PackKind::Line) {
            d.kind = K::Line;
        } else if (draw.packKind == UiDraw::PackKind::Clouds) {
            d.kind = K::Clouds;
            d.alphaRef = kTenth;
        } else if (lightning || draw.packKind == UiDraw::PackKind::Lightning) {
            d.kind = K::Lightning;
        } else if (draw.packKind == UiDraw::PackKind::BlockText) {
            d.kind = K::BlockTranslucent;
            d.alphaRef = kTenth;
        } else if (draw.blockEntity) {
            d.kind = translucent ? K::BlockTranslucent : K::Block;
            d.alphaRef = kTenth;
        } else if (draw.material == DrawMaterial::Particle) {
            d.kind = translucent && draw.alphaCutoff < 0.5f ? K::ParticleTranslucent : K::Particle;
            d.alphaRef = kTenth;
        } else if (draw.material == DrawMaterial::Weather) {
            d.kind = K::Weather;
            d.alphaRef = kTenth;
        } else if (draw.material == DrawMaterial::Cracks) {
            d.kind = K::Cracks;
            d.alphaRef = kTenth;
        } else if (draw.emissive == 1 || draw.blend == 2) {
            d.kind = K::Eyes;
            d.alphaRef = kNonZero;
        } else {
            d.kind = translucent ? K::EntityTranslucent : K::Entity;
            d.alphaRef = translucent || draw.alphaCutoff > 0.0f ? kTenth : -1.0f;
        }
        const int blockLevel = draw.blockLight >= 0 ? draw.blockLight : frame.eyeBlockLight;
        const int skyLevel = draw.skyLight >= 0 ? draw.skyLight : frame.eyeSkyLight;
        gpu::JavaPack::BuildVertices(mesh.cpuVertices->data(), mesh.cpuVertices->size(),
                                     mesh.cpuIndices->data(), mesh.cpuIndices->size(), draw.model, blockLevel,
                                     skyLevel, d.vertices, draw.vertexLight, nullptr,
                                     jpackDimension != "world0");
        d.count = static_cast<uint32_t>(d.vertices.size() / gpu::kPackVertexBytes);
        if (d.count == 0)
            return true;
        d.texture = texture;
        d.depthTest = draw.depth || layer == UiLayer::Hand;
        d.depthWrite = draw.depthWrite;
        d.blend = translucent;
        d.additive = draw.blend == 2;
        std::memcpy(d.entityColor, draw.overlay, sizeof(d.entityColor));
        d.castsShadow = layer == UiLayer::World && !d.additive && draw.depthWrite && !glint &&
                        (d.kind == K::Entity || d.kind == K::EntityTranslucent || d.kind == K::Block ||
                         d.kind == K::Lightning);
        d.shadowOnly = draw.shadowOnly;
        d.player = draw.player;
        if (draw.entityType)
            d.entityId = jpack->EntityIdFor(draw.entityType);
        if (draw.blockEntity)
            d.blockEntityId = jpack->BlockIdFor(draw.blockEntity);
        if (draw.itemName)
            d.itemId = jpack->ItemIdFor(draw.itemName);
        if (glint) {
            d.glintBlend = true;
            d.depthWrite = false;
            const float scrollU = draw.glintMatrix[0], scrollV = draw.glintMatrix[1];
            const float scale = draw.glintMatrix[2] > 0.0f ? draw.glintMatrix[2] : 0.5f;
            const float c = std::cos(0.17453292f), sn = std::sin(0.17453292f);
            const float m[16] = {c * scale, sn * scale, 0, 0, -sn * scale, c * scale, 0, 0,
                                 0,         0,          1, 0, scrollU,     scrollV,   0, 1};
            std::memcpy(d.textureMatrix, m, sizeof(m));
        }
        jpack->QueueDraw(std::move(d));
        return true;
    }

    void RenderContext_rhi::UiSubmit(UiLayer layer, const UiDraw& draw) {
        if (!initialized || !frameOpen)
            return;
        UiMeshGpu* found = LookupUiMesh(draw.mesh);
        if (!found)
            return;
        UiMeshGpu& mesh = *found;
        const uint32_t count = mesh.iCount;

        const TexRec* texture = draw.texture != 0 ? LookupUiTexture(draw.texture) : nullptr;
        const TexRec* glintRec = draw.glintTexture != 0 ? LookupUiTexture(draw.glintTexture) : nullptr;
        const bool glint = glintRec != nullptr;
        if (PackRunning() && jpackGathered &&
            RouteToJavaPack(layer, draw, mesh,
                            glint ? glintRec->handle
                                  : (texture ? texture->handle : conduit::texture_handle{}),
                            glint))
            return;
        if (mesh.transient && mesh.lazyRing) {
            mesh.lazyRing = false;
            if (!mesh.cpuVertices || !mesh.cpuIndices || mesh.cpuVertices->empty() || mesh.cpuIndices->empty())
                return;
            mesh.tv = gpu.Ring(static_cast<uint64_t>(mesh.cpuVertices->size()) * sizeof(UiVertex), 16);
            mesh.ti = gpu.Ring(static_cast<uint64_t>(mesh.cpuIndices->size()) * sizeof(uint32_t), 4);
            if (!mesh.tv.data || !mesh.ti.data) {
                mesh.tv = mesh.ti = {};
                return;
            }
            std::memcpy(mesh.tv.data, mesh.cpuVertices->data(), mesh.cpuVertices->size() * sizeof(UiVertex));
            std::memcpy(mesh.ti.data, mesh.cpuIndices->data(), mesh.cpuIndices->size() * sizeof(uint32_t));
        }
        if (mesh.transient && !mesh.tv.data)
            return;
        Source vertices, indices;
        if (mesh.transient) {
            vertices = {mesh.tv.buffer, mesh.tv.offset};
            indices = {mesh.ti.buffer, mesh.ti.offset};
        } else {
            const UiPage& page = uiPages[static_cast<size_t>(mesh.page)];
            vertices = {page.vb.Buffer(), static_cast<uint64_t>(mesh.vFirst) * sizeof(UiVertex)};
            indices = {page.ib.Buffer(), static_cast<uint64_t>(mesh.iFirst) * sizeof(uint32_t)};
        }
        const View view = layer == UiLayer::World       ? View::Entity
                          : layer == UiLayer::WorldLate ? View::Late
                          : layer == UiLayer::Hand      ? View::Hand
                          : layer == UiLayer::Doll      ? View::Doll
                          : layer == UiLayer::Top       ? View::Top
                          : layer == UiLayer::Export    ? View::ExportUi
                                                        : View::Gui;

        if (layer == UiLayer::World && shadow.active && shadow.entities && !glint && draw.depth &&
            draw.depthWrite && draw.blend != 2 && ProgramIndex(Program::EntityShadow) >= 0) {
            SetTransform(glm::make_mat4(draw.model));
            BindTexture("s_uiTex", texture ? texture->handle : gpu.White2D(),
                        texture ? texture->sampler : gpu::SamplerKind::LinearRepeat);
            SetMatrix(shadow.rasterE);
            uni.Mat4("u_shadowMtx", shadow.sampleE);
            DrawState ss = Base(Program::EntityShadow, true, true, conduit::compare_op::less);
            ss.layout = gpu::VertexLayout::Ui;
            ss.cull = conduit::cull_mode::none;
            ss.mask = conduit::color_mask::all;
            Submit(View::ShadowEntity, ss, vertices, &indices, conduit::index_type::u32, 0, count);
            SetMatrix(mvp);
        }
        if (draw.shadowOnly)
            return;

        SetTransform(glm::make_mat4(draw.model));
        BindTexture("s_uiTex", texture ? texture->handle : gpu.White2D(),
                    texture ? texture->sampler : gpu::SamplerKind::LinearRepeat);
        if (glint)
            BindTexture("s_uiGlint", glintRec->handle, glintRec->sampler);

        const bool world = layer == UiLayer::World || layer == UiLayer::WorldLate;
        const bool entityView = world || layer == UiLayer::Hand || layer == UiLayer::Doll;
        const DrawMaterial domain = draw.material == DrawMaterial::Auto
                                        ? (entityView ? DrawMaterial::Entity : DrawMaterial::Auto)
                                        : draw.material;
        uni.Vec4("u_rdMaterial", world && domain != DrawMaterial::Nametag ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f);
        uni.Vec4("u_uiParams", draw.alphaCutoff, glint ? 1.0f : 0.0f, draw.glintStrength,
                 static_cast<float>(draw.emissive));
        uni.Set("u_uiOverlay", draw.overlay, 4);
        uni.Vec4("u_uiGlintA", draw.glintMatrix[0], draw.glintMatrix[1], draw.glintMatrix[2],
                 draw.glintMatrix[3]);
        uni.Vec4("u_uiGlintB", draw.glintOffset[0], draw.glintOffset[1], 0.0f, 0.0f);

        Program program = Program::Ui;
        if (glint)
            program = Program::EntityGlint;
        else if (domain == DrawMaterial::Particle)
            program = draw.alphaCutoff >= 0.5f ? Program::ParticleCutout : Program::Particle;
        else if (domain == DrawMaterial::Weather)
            program = Program::Weather;
        else if (domain == DrawMaterial::Cracks)
            program = Program::Cracks;
        else if (domain == DrawMaterial::Nametag)
            program = draw.texture ? Program::Ui : Program::Nametag;
        else if (domain == DrawMaterial::Entity)
            program = draw.colorMask       ? Program::EntityMask
                      : draw.emissive == 1 ? Program::EntityEmissive
                                           : Program::Entity;

        DrawState state = Base(program, draw.depth && !glint, draw.depthWrite, ToRhi(draw.depthFunc));
        state.layout = gpu::VertexLayout::Ui;
        if (draw.colorWrite)
            state.mask = state.mask | conduit::color_mask::rgb;
        if (draw.alphaWrite)
            state.mask = state.mask | conduit::color_mask::a;
        if (glint) {
            state.mask = conduit::color_mask::rgb;
            state.blend = true;
            state.srcColor = state.srcAlpha = conduit::blend_factor::one;
            state.dstColor = state.dstAlpha = conduit::blend_factor::one;
            if (draw.depth) {
                state.depthTest = true;
                state.depthOp = conduit::compare_op::less_equal;
            }
        } else if (draw.premultiplied) {
            state.blend = true;
            state.srcColor = state.srcAlpha = conduit::blend_factor::one;
            state.dstColor = state.dstAlpha = conduit::blend_factor::inv_src_alpha;
        } else if (draw.customBlend) {
            state.blend = true;
            state.srcColor = ToRhi(draw.blendSrc);
            state.dstColor = ToRhi(draw.blendDst);
            state.srcAlpha = ToRhi(draw.blendSrc == BlendFactor::SrcAlpha ? BlendFactor::One : draw.blendSrc);
            state.dstAlpha = ToRhi(draw.blendDst);
        } else if (draw.blend == 1) {
            state.blend = true;
            state.srcColor = conduit::blend_factor::src_alpha;
            state.dstColor = conduit::blend_factor::inv_src_alpha;
            state.srcAlpha = conduit::blend_factor::one;
            state.dstAlpha = conduit::blend_factor::inv_src_alpha;
        } else if (draw.blend == 2) {
            state.blend = true;
            state.srcColor = state.srcAlpha = conduit::blend_factor::src_alpha;
            state.dstColor = state.dstAlpha = conduit::blend_factor::one;
        }
        state.cull = draw.cull == CullMode::Back    ? conduit::cull_mode::back
                     : draw.cull == CullMode::Front ? conduit::cull_mode::front
                                                    : conduit::cull_mode::none;
        state.alphaToCoverage = draw.alphaToCoverage;

        conduit::rect rect;
        const conduit::rect* scissor = nullptr;
        if (draw.scissor) {
            rect.x = std::max(0, draw.scissorRect[0]);
            rect.y = std::max(0, draw.scissorRect[1]);
            rect.width = static_cast<uint32_t>(std::max(0, draw.scissorRect[2]));
            rect.height = static_cast<uint32_t>(std::max(0, draw.scissorRect[3]));
            scissor = &rect;
        }

        Submit(view, state, vertices, &indices, conduit::index_type::u32, 0, count, scissor);
    }

    void RenderContext_rhi::DrawHudCursor(float x, float y, float w, float h) {
        if (!initialized || !frameOpen || !crosshair.used || width <= 0 || height <= 0)
            return;
        const gpu::RingSlice tv = gpu.Ring(4 * sizeof(TextureVertex), 16);
        if (!tv.data)
            return;
        const float W = static_cast<float>(width), H = static_cast<float>(height);
        const auto nx = [&](float px) { return px / W * 2.0f - 1.0f; };
        const auto ny = [&](float py) { return 1.0f - py / H * 2.0f; };
        const float x0 = nx(std::round(x)), x1 = nx(std::round(x + w)), y0 = ny(std::round(y)),
                    y1 = ny(std::round(y + h));
        auto* v = reinterpret_cast<TextureVertex*>(tv.data);
        v[0] = {x0, y1, 0.0f, 0.0f, 0.0f};
        v[1] = {x1, y1, 0.0f, 1.0f, 0.0f};
        v[2] = {x1, y0, 0.0f, 1.0f, 1.0f};
        v[3] = {x0, y0, 0.0f, 0.0f, 1.0f};
        static const float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        SetMatrix(identity);
        uni.Vec4("u_modulator", 1.0f, 1.0f, 1.0f, 1.0f);
        BindTexture("s_sky", crosshair.handle, gpu::SamplerKind::PointClamp);
        DrawState state = Base(ProgramIndex(Program::SpriteUi) >= 0 ? Program::SpriteUi : Program::Celestial,
                               false, false, conduit::compare_op::less_equal);
        state.layout = gpu::VertexLayout::Texture;
        state.mask = conduit::color_mask::rgb;
        ApplyMaterial(state, "ui_crosshair");
        const Source vertices{tv.buffer, tv.offset};
        const Source indices{quadIdx, 0};
        Submit(View::Gui, state, vertices, &indices, conduit::index_type::u16, 0, 6);
    }
}
