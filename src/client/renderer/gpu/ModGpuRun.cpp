#include "client/renderer/gpu/ModGpu.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace perlin::gpu {
    namespace {
        using stream::Cmd;

        constexpr uint32_t kUniformMax = 4096;
        constexpr uint32_t kUsageSampled = 1u << 0, kUsageStorage = 1u << 1, kUsageColor = 1u << 2,
                           kUsageDepth = 1u << 3;
        constexpr uint32_t kBufVertex = 1u << 0, kBufIndex = 1u << 1, kBufStorage = 1u << 2, kBufIndirect = 1u << 3;
        constexpr uint32_t kMaxLayerErrors = 6;

        bool Finite(float v) {
            return std::isfinite(v);
        }
    }

    struct ModGpu::Uses {
        std::set<uint32_t> sampled, storageTex, storageBufRw, storageBufRo, vertex, index, indirect;
    };

    struct ModGpu::Run {
        struct PassInfo {
            uint32_t colorCount = 0;
            conduit::format colors[4]{};
            conduit::format depth = conduit::format::unknown;
            uint32_t samples = 1;
            uint32_t width = 0, height = 0;
            std::set<uint32_t> attach;
        };
        struct Slot {
            uint32_t texture = 0, sampler = 0, layer = 0;
        };
        struct Vb {
            uint32_t buffer = 0;
            uint64_t offset = 0;
        };

        ModGpu& g;
        PluginResources& res;
        std::string plugin;
        std::string layerName;
        conduit::command_list* list = nullptr;
        const Screen* screen = nullptr;
        Uses* collect = nullptr;
        uint32_t errors = 0;
        uint32_t labels = 0;

        uint32_t pipelineId = 0;
        Pipeline* pipeline = nullptr;
        std::array<uint8_t, kUniformMax> uniforms{};
        std::array<Slot, 48> slots{};
        std::map<uint32_t, uint32_t> storage;
        std::array<Vb, 8> vbs{};
        Vb ib;
        bool ibWide = false;
        bool inPass = false, skip = false;
        PassInfo pass;

        Run(ModGpu& gpuIn, PluginResources& resIn, const ModLayer& layer, conduit::command_list* l, const Screen* s)
            : g(gpuIn), res(resIn), plugin(layer.plugin), layerName(layer.name), list(l), screen(s) {}

        void Err(const std::string& message) {
            if (collect || errors >= kMaxLayerErrors)
                return;
            errors++;
            g.Report(plugin, "layer " + layerName + ": " + message);
        }

        Texture* Tex(uint32_t id) {
            const auto it = res.textures.find(id);
            return it == res.textures.end() || !it->second.handle ? nullptr : &it->second;
        }
        Buffer* Buf(uint32_t id) {
            const auto it = res.buffers.find(id);
            return it == res.buffers.end() ? nullptr : &it->second;
        }

        void ApplyUses(const Uses& u) {
            for (uint32_t id : u.sampled) {
                Texture* t = Tex(id);
                if (!t || pass.attach.contains(id) || !(t->desc.usage & kUsageSampled))
                    continue;
                g.gpu.Transition(*list, t->handle,
                                 conduit::is_depth_format(t->format) ? conduit::state::depth_read_sampled
                                                                     : conduit::state::shader_read);
            }
            for (uint32_t id : u.storageTex) {
                Texture* t = Tex(id);
                if (!t || !(t->desc.usage & kUsageStorage))
                    continue;
                if (g.gpu.StateOf(t->handle) == conduit::state::shader_write)
                    list->uav_barrier(t->handle);
                else
                    g.gpu.Transition(*list, t->handle, conduit::state::shader_write);
            }
            for (uint32_t id : u.storageBufRw)
                if (Buffer* b = Buf(id); b && (b->usage & kBufStorage))
                    g.UseBuffer(*list, plugin, id, *b, conduit::state::shader_write);
            for (uint32_t id : u.storageBufRo)
                if (Buffer* b = Buf(id); b && (b->usage & kBufStorage))
                    g.UseBuffer(*list, plugin, id, *b, conduit::state::shader_read);
            for (uint32_t id : u.vertex)
                if (Buffer* b = Buf(id); b && (b->usage & kBufVertex))
                    g.UseBuffer(*list, plugin, id, *b, conduit::state::vertex_buffer);
            for (uint32_t id : u.index)
                if (Buffer* b = Buf(id); b && (b->usage & kBufIndex))
                    g.UseBuffer(*list, plugin, id, *b, conduit::state::index_buffer);
            for (uint32_t id : u.indirect)
                if (Buffer* b = Buf(id); b && (b->usage & kBufIndirect))
                    g.UseBuffer(*list, plugin, id, *b, conduit::state::indirect_argument);
        }

        void Gather(Uses& u, bool indexed, uint32_t indirectBuffer) {
            if (!pipeline || !pipeline->handle)
                return;
            const auto packIt = res.packs.find(pipeline->desc.pack);
            if (packIt != res.packs.end())
                for (const conduit::texture_slot& s : packIt->second.library.texture_slots())
                    if (slots[s.slot].texture)
                        u.sampled.insert(slots[s.slot].texture);
            for (const ModStorageBinding& s : pipeline->desc.storage) {
                const auto it = storage.find(s.binding);
                if (it == storage.end())
                    continue;
                if (s.kind == 2)
                    u.storageTex.insert(it->second);
                else if (s.kind == 1)
                    u.storageBufRw.insert(it->second);
                else
                    u.storageBufRo.insert(it->second);
            }
            if (!pipeline->compute) {
                for (uint32_t i = 0; i < pipeline->streamCount && i < vbs.size(); i++)
                    if (vbs[i].buffer)
                        u.vertex.insert(vbs[i].buffer);
                if (indexed && ib.buffer)
                    u.index.insert(ib.buffer);
                if (indirectBuffer)
                    u.indirect.insert(indirectBuffer);
            }
        }

        bool PassCompatible() {
            if (pipeline->colorCount != pass.colorCount) {
                Err("the pipeline writes " + std::to_string(pipeline->colorCount) + " colour targets but the pass has " +
                    std::to_string(pass.colorCount));
                return false;
            }
            for (uint32_t i = 0; i < pass.colorCount; i++)
                if (pipeline->colors[i] != pass.colors[i]) {
                    Err("a pipeline colour format does not match the pass target");
                    return false;
                }
            if (pipeline->depth != pass.depth) {
                Err("the pipeline depth format does not match the pass");
                return false;
            }
            if (pipeline->desc.samples != pass.samples) {
                Err("the pipeline sample count does not match the pass");
                return false;
            }
            return true;
        }

        bool BindCommon(bool compute) {
            conduit::device& dev = g.gpu.Dev();
            const auto packIt = res.packs.find(pipeline->desc.pack);
            if (packIt == res.packs.end()) {
                Err("the shader pack of the pipeline was destroyed");
                return false;
            }
            Pack& pack = packIt->second;

            const uint64_t ringBytes = std::max<uint64_t>(256, (pipeline->uniformBytes + 255) / 256 * 256);
            const RingSlice ring = g.gpu.Ring(ringBytes, 256);
            if (!ring.data) {
                Err("out of uniform memory");
                return false;
            }
            std::memset(ring.data, 0, ringBytes);
            std::memcpy(ring.data, uniforms.data(), std::min<uint32_t>(pipeline->uniformBytes, kUniformMax));
            const uint32_t offset = static_cast<uint32_t>(ring.offset);

            GroupKey key;
            key.values.push_back(pack.textureLayout.value);
            std::vector<conduit::bind_group_entry> entries;
            for (const conduit::texture_slot& s : pack.library.texture_slots()) {
                const Slot& b = slots[s.slot];
                conduit::texture_handle handle = s.array ? g.gpu.WhiteArray() : g.gpu.White2D();
                uint32_t layer = 0;
                if (b.texture) {
                    Texture* t = Tex(b.texture);
                    const bool attached = pass.attach.contains(b.texture) && inPass;
                    if (!t)
                        Err("a bound texture does not exist");
                    else if (attached)
                        Err("a texture is sampled while it is a target of the same pass");
                    else if (!(t->desc.usage & kUsageSampled))
                        Err("a bound texture was not created with the Sampled usage");
                    else if (t->desc.samples > 1)
                        Err("a multisampled texture cannot be sampled; resolve it first");
                    else if (s.array && t->desc.dim != 1)
                        Err("slot " + s.name + " needs an array texture");
                    else if (!s.array && t->desc.dim != 0 && !(t->desc.dim == 1 && b.layer < t->desc.layers))
                        Err("slot " + s.name + " needs a 2D texture");
                    else {
                        handle = t->handle;
                        layer = (!s.array && t->desc.dim == 1) ? b.layer : 0;
                    }
                }
                conduit::sampler_handle sampler =
                    g.gpu.Sampler(s.unfilterable ? SamplerKind::PointClamp : SamplerKind::LinearClamp);
                if (b.sampler) {
                    const auto it = res.samplers.find(b.sampler);
                    if (it != res.samplers.end())
                        sampler = it->second;
                    else
                        Err("a bound sampler does not exist");
                }
                key.values.push_back(handle.value);
                key.values.push_back(sampler.value);
                key.values.push_back(layer);
                conduit::bind_group_entry t;
                t.binding = s.slot * 2;
                t.texture.texture = handle;
                t.texture.view_dim = s.array ? conduit::texture_dim::tex2d_array : conduit::texture_dim::tex2d;
                if (layer) {
                    t.texture.range.base_layer = layer;
                    t.texture.range.layer_count = 1;
                }
                entries.push_back(t);
                conduit::bind_group_entry smp;
                smp.binding = s.slot * 2 + 1;
                smp.sampler = sampler;
                entries.push_back(smp);
            }
            conduit::bind_group_handle textures;
            if (const auto it = g.textureGroups.find(key); it != g.textureGroups.end()) {
                textures = it->second;
            } else {
                conduit::bind_group_desc gd;
                gd.layout = pack.textureLayout;
                gd.entries = entries;
                gd.name = "mod textures";
                textures = dev.create_bind_group(gd);
                if (!textures) {
                    Err("could not create the texture bind group");
                    return false;
                }
                g.textureGroups.emplace(std::move(key), textures);
            }

            conduit::bind_group_handle storageGroup;
            if (!pipeline->desc.storage.empty()) {
                GroupKey sk;
                sk.values.push_back(pipeline->storageLayout.value);
                std::vector<conduit::bind_group_entry> se;
                for (const ModStorageBinding& s : pipeline->desc.storage) {
                    const auto it = storage.find(s.binding);
                    if (it == storage.end()) {
                        Err("storage binding " + std::to_string(s.binding) + " has nothing bound");
                        return false;
                    }
                    conduit::bind_group_entry e;
                    e.binding = s.binding;
                    if (s.kind == 2) {
                        Texture* t = Tex(it->second);
                        if (!t || !(t->desc.usage & kUsageStorage)) {
                            Err("storage binding " + std::to_string(s.binding) + " needs a texture with the Storage usage");
                            return false;
                        }
                        e.texture.texture = t->handle;
                        sk.values.push_back(s.binding);
                        sk.values.push_back(t->handle.value);
                    } else {
                        Buffer* b = Buf(it->second);
                        if (!b || !(b->usage & kBufStorage)) {
                            Err("storage binding " + std::to_string(s.binding) + " needs a buffer with the Storage usage");
                            return false;
                        }
                        e.buffer = {b->handle, 0, 0};
                        sk.values.push_back(s.binding);
                        sk.values.push_back(b->handle.value);
                    }
                    se.push_back(e);
                }
                if (const auto it = g.storageGroups.find(sk); it != g.storageGroups.end()) {
                    storageGroup = it->second;
                } else {
                    conduit::bind_group_desc gd;
                    gd.layout = pipeline->storageLayout;
                    gd.entries = se;
                    gd.per_frame = true;
                    gd.name = "mod storage";
                    storageGroup = dev.create_bind_group(gd);
                    if (!storageGroup) {
                        Err("could not create the storage bind group");
                        return false;
                    }
                    g.storageGroups.emplace(std::move(sk), storageGroup);
                }
            }
            (void)compute;
            list->set_pipeline(pipeline->handle);
            list->set_bind_group(0, g.gpu.UniformGroup(ring.chunk), std::span<const uint32_t>(&offset, 1));
            list->set_bind_group(1, textures);
            if (storageGroup)
                list->set_bind_group(2, storageGroup);
            return true;
        }

        void Draw(bool indexed, uint32_t count, uint32_t instances, uint32_t first, int32_t vertexOffset,
                  uint32_t firstInstance, uint32_t indirectBuffer, uint64_t indirectOffset, uint32_t indirectDraws,
                  uint32_t indirectStride) {
            if (collect) {
                Gather(*collect, indexed, indirectBuffer);
                return;
            }
            if (!inPass) {
                Err("draw outside of a pass");
                return;
            }
            if (skip)
                return;
            if (!pipeline || !pipeline->handle) {
                Err("draw without a valid pipeline");
                return;
            }
            if (pipeline->compute) {
                Err("a compute pipeline cannot draw");
                return;
            }
            if (!PassCompatible())
                return;
            for (uint32_t i = 0; i < pipeline->streamCount; i++) {
                Buffer* b = Buf(vbs[i].buffer);
                if (!b || !(b->usage & kBufVertex) || vbs[i].offset >= b->size) {
                    Err("vertex buffer slot " + std::to_string(i) + " is missing or invalid");
                    return;
                }
            }
            Buffer* indexBuffer = nullptr;
            if (indexed) {
                indexBuffer = Buf(ib.buffer);
                if (!indexBuffer || !(indexBuffer->usage & kBufIndex) || ib.offset >= indexBuffer->size) {
                    Err("indexed draw without a valid index buffer");
                    return;
                }
            }
            Buffer* indirect = nullptr;
            if (indirectBuffer) {
                indirect = Buf(indirectBuffer);
                if (!indirect || !(indirect->usage & kBufIndirect) || indirectDraws == 0 ||
                    indirectStride < sizeof(conduit::draw_indexed_indirect_command) ||
                    indirectOffset + static_cast<uint64_t>(indirectDraws) * indirectStride > indirect->size) {
                    Err("indirect draw needs a buffer with the Indirect usage and a range that fits");
                    return;
                }
                if (!g.gpu.Caps().features.multi_draw_indirect && indirectDraws > 1) {
                    Err("this device cannot run more than one indirect draw at a time");
                    return;
                }
            }
            if (!BindCommon(false))
                return;
            for (uint32_t i = 0; i < pipeline->streamCount; i++)
                list->set_vertex_buffer(i, Buf(vbs[i].buffer)->handle, vbs[i].offset);
            if (indexed)
                list->set_index_buffer(indexBuffer->handle, ibWide ? conduit::index_type::u32 : conduit::index_type::u16,
                                       ib.offset);
            if (indirect)
                list->draw_indexed_indirect(indirect->handle, indirectOffset, indirectDraws, indirectStride);
            else if (indexed)
                list->draw_indexed(count, instances, first, vertexOffset, firstInstance);
            else
                list->draw(count, instances, first, firstInstance);
        }

        void Dispatch(const stream::Dispatch& d) {
            if (collect)
                return;
            if (inPass) {
                Err("dispatch inside a render pass");
                return;
            }
            if (!pipeline || !pipeline->handle || !pipeline->compute) {
                Err("dispatch without a valid compute pipeline");
                return;
            }
            if (d.x == 0 || d.y == 0 || d.z == 0 || d.x > 65535 || d.y > 65535 || d.z > 65535) {
                Err("dispatch size is out of range");
                return;
            }
            Uses u;
            Gather(u, false, 0);
            ApplyUses(u);
            if (!BindCommon(true))
                return;
            list->dispatch(d.x, d.y, d.z);
        }

        void BeginPass(const stream::Command& c, const std::vector<stream::Command>& cmds, size_t index) {
            if (inPass) {
                Err("BeginPass inside a pass");
                return;
            }
            stream::BeginPass bp;
            if (!c.read(bp) || bp.colorCount > stream::MaxColors || (bp.colorCount == 0 && !bp.hasDepth)) {
                Err("malformed BeginPass");
                inPass = skip = true;
                return;
            }
            inPass = true;
            skip = true;
            PassInfo info;
            conduit::rendering_desc rd;
            std::vector<conduit::texture_handle> toColor, toResolve;
            conduit::texture_handle depthHandle;
            bool depthReadOnly = false;
            bool sizeSet = false, sampleSet = false;
            const auto sized = [&](uint32_t w, uint32_t h, uint32_t samples) {
                if (!sizeSet) {
                    info.width = w;
                    info.height = h;
                    sizeSet = true;
                } else if (info.width != w || info.height != h) {
                    Err("pass attachments have different sizes");
                    return false;
                }
                if (!sampleSet) {
                    info.samples = samples;
                    sampleSet = true;
                } else if (info.samples != samples) {
                    Err("pass attachments have different sample counts");
                    return false;
                }
                return true;
            };
            const auto load = [](uint32_t v) {
                return v == 1 ? conduit::load_op::clear : v == 2 ? conduit::load_op::dont_care : conduit::load_op::load;
            };
            const auto store = [](uint32_t v) {
                return v == 1 ? conduit::store_op::dont_care : conduit::store_op::store;
            };
            if (bp.colorCount > g.gpu.Caps().limits.max_color_targets) {
                Err("more colour targets than the device supports");
                return;
            }
            for (uint32_t i = 0; i < bp.colorCount; i++) {
                const stream::Color& col = bp.colors[i];
                conduit::color_attachment& a = rd.colors[i];
                if (col.texture == 0) {
                    if (!screen || !screen->color) {
                        Err("the screen is not a target in the Pre stage");
                        return;
                    }
                    if (!sized(screen->width, screen->height, 1))
                        return;
                    a.texture = screen->color;
                    info.colors[i] = screen->format;
                    toColor.push_back(screen->color);
                } else {
                    Texture* t = Tex(col.texture);
                    if (!t || !(t->desc.usage & kUsageColor)) {
                        Err("a colour target is missing or was not created with the ColorTarget usage");
                        return;
                    }
                    if (col.mip >= t->desc.mips || col.layer >= t->desc.layers) {
                        Err("colour target mip or layer is out of range");
                        return;
                    }
                    if (!sized(std::max(1u, t->width >> col.mip), std::max(1u, t->height >> col.mip), t->desc.samples))
                        return;
                    a.texture = t->handle;
                    a.mip = col.mip;
                    a.layer = col.layer;
                    info.colors[i] = t->format;
                    info.attach.insert(col.texture);
                    toColor.push_back(t->handle);
                }
                a.load = load(col.load);
                a.store = store(col.store);
                a.clear = {col.clear[0], col.clear[1], col.clear[2], col.clear[3]};
                if (col.resolve) {
                    Texture* r = Tex(col.resolve);
                    if (!r || !(r->desc.usage & kUsageColor) || r->desc.samples != 1 || r->format != info.colors[i] ||
                        info.samples <= 1) {
                        Err("a resolve target must be a single-sample colour texture of the same format");
                        return;
                    }
                    a.resolve = r->handle;
                    info.attach.insert(col.resolve);
                    toResolve.push_back(r->handle);
                }
            }
            info.colorCount = bp.colorCount;
            if (bp.hasDepth) {
                Texture* t = Tex(bp.depth.texture);
                if (!t || !(t->desc.usage & kUsageDepth)) {
                    Err("the depth target is missing or was not created with the DepthTarget usage");
                    return;
                }
                if (!sized(t->width, t->height, t->desc.samples))
                    return;
                rd.depth.texture = t->handle;
                rd.depth.load = load(bp.depth.load);
                rd.depth.store = store(bp.depth.store);
                rd.depth.clear_depth = bp.depth.clearDepth;
                rd.depth.clear_stencil = static_cast<uint8_t>(bp.depth.clearStencil);
                rd.depth.read_only = bp.depth.readOnly != 0;
                rd.depth.layer = bp.depth.layer;
                depthHandle = t->handle;
                depthReadOnly = rd.depth.read_only;
                info.depth = t->format;
                info.attach.insert(bp.depth.texture);
            }
            rd.color_count = bp.colorCount;
            rd.name = "mod pass";
            pass = info;

            Uses uses;
            {
                Run scan(*this);
                scan.collect = &uses;
                scan.inPass = true;
                scan.skip = false;
                for (size_t k = index + 1; k < cmds.size() && cmds[k].op != Cmd::EndPass; k++)
                    scan.Exec(cmds[k]);
            }
            ApplyUses(uses);
            for (conduit::texture_handle h : toColor)
                g.gpu.Transition(*list, h, conduit::state::color_target);
            for (conduit::texture_handle h : toResolve)
                g.gpu.Transition(*list, h, conduit::state::resolve_dst);
            if (depthHandle)
                g.gpu.Transition(*list, depthHandle,
                                 depthReadOnly ? conduit::state::depth_read : conduit::state::depth_write);
            list->begin_rendering(rd);
            list->set_viewport({0, 0, static_cast<float>(info.width), static_cast<float>(info.height), 0.0f, 1.0f});
            list->set_scissor({0, 0, info.width, info.height});
            skip = false;
        }

        void EndPass() {
            if (!inPass) {
                Err("EndPass without BeginPass");
                return;
            }
            if (!skip)
                list->end_rendering();
            inPass = false;
            skip = false;
            pass = {};
        }

        void CopyBuffer(const stream::CopyBuffer& c) {
            if (collect)
                return;
            if (inPass) {
                Err("copies are not allowed inside a pass");
                return;
            }
            Buffer* dst = Buf(c.dst);
            Buffer* src = Buf(c.src);
            const uint64_t dOff = stream::join(c.dstOffsetLo, c.dstOffsetHi), sOff = stream::join(c.srcOffsetLo, c.srcOffsetHi),
                           size = stream::join(c.sizeLo, c.sizeHi);
            if (!dst || !src || size == 0 || dOff + size > dst->size || sOff + size > src->size) {
                Err("CopyBuffer: bad buffer or range");
                return;
            }
            if (c.dst == c.src && dOff < sOff + size && sOff < dOff + size) {
                Err("CopyBuffer: ranges overlap");
                return;
            }
            g.UseBuffer(*list, plugin, c.src, *src, conduit::state::copy_src);
            g.UseBuffer(*list, plugin, c.dst, *dst, conduit::state::copy_dst);
            list->copy_buffer(dst->handle, dOff, src->handle, sOff, size);
        }

        void FillBuffer(const stream::FillBuffer& f) {
            if (collect)
                return;
            if (inPass) {
                Err("fills are not allowed inside a pass");
                return;
            }
            Buffer* dst = Buf(f.dst);
            const uint64_t off = stream::join(f.offsetLo, f.offsetHi), size = stream::join(f.sizeLo, f.sizeHi);
            if (!dst || size == 0 || off % 4 || size % 4 || off + size > dst->size) {
                Err("FillBuffer: bad buffer, range or alignment (multiples of 4)");
                return;
            }
            g.UseBuffer(*list, plugin, f.dst, *dst, conduit::state::copy_dst);
            list->fill_buffer(dst->handle, off, size, f.value);
        }

        void CopyTexture(const stream::CopyTexture& c) {
            if (collect)
                return;
            if (inPass) {
                Err("copies are not allowed inside a pass");
                return;
            }
            Texture* dst = Tex(c.dst);
            Texture* src = Tex(c.src);
            if (!dst || !src || c.dst == c.src || dst->format != src->format || dst->desc.samples != 1 ||
                src->desc.samples != 1 || c.width == 0 || c.height == 0) {
                Err("CopyTexture: textures are missing, the same, multisampled or differ in format");
                return;
            }
            if (c.srcMip >= src->desc.mips || c.dstMip >= dst->desc.mips || c.srcLayer >= src->desc.layers ||
                c.dstLayer >= dst->desc.layers ||
                c.srcX + c.width > std::max(1u, src->width >> c.srcMip) ||
                c.srcY + c.height > std::max(1u, src->height >> c.srcMip) ||
                c.dstX + c.width > std::max(1u, dst->width >> c.dstMip) ||
                c.dstY + c.height > std::max(1u, dst->height >> c.dstMip)) {
                Err("CopyTexture: region is out of range");
                return;
            }
            g.gpu.Transition(*list, src->handle, conduit::state::copy_src);
            g.gpu.Transition(*list, dst->handle, conduit::state::copy_dst);
            conduit::texture_copy tc;
            tc.src_mip = c.srcMip;
            tc.src_layer = c.srcLayer;
            tc.src_x = c.srcX;
            tc.src_y = c.srcY;
            tc.dst_mip = c.dstMip;
            tc.dst_layer = c.dstLayer;
            tc.dst_x = c.dstX;
            tc.dst_y = c.dstY;
            tc.width = c.width;
            tc.height = c.height;
            list->copy_texture(dst->handle, src->handle, std::span<const conduit::texture_copy>(&tc, 1));
        }

        void CopyScreen(const stream::CopyScreen& c) {
            if (collect)
                return;
            if (inPass) {
                Err("copies are not allowed inside a pass");
                return;
            }
            if (!screen || !screen->color) {
                Err("CopyScreen is not available in the Pre stage");
                return;
            }
            Texture* dst = Tex(c.dst);
            if (!dst || dst->format != screen->format || dst->width != screen->width || dst->height != screen->height ||
                dst->desc.samples != 1 || dst->desc.layers != 1) {
                Err("CopyScreen: the destination must be a screen-sized texture (scale 1) in the Screen format");
                return;
            }
            g.gpu.Transition(*list, screen->color, conduit::state::copy_src);
            g.gpu.Transition(*list, dst->handle, conduit::state::copy_dst);
            conduit::texture_copy tc;
            tc.width = screen->width;
            tc.height = screen->height;
            list->copy_texture(dst->handle, screen->color, std::span<const conduit::texture_copy>(&tc, 1));
        }

        void Exec(const stream::Command& c) {
            switch (c.op) {
            case Cmd::SetPipeline: {
                stream::SetPipeline p;
                if (!c.read(p)) {
                    Err("malformed SetPipeline");
                    return;
                }
                const auto it = res.pipelines.find(p.pipeline);
                if (it == res.pipelines.end() || !it->second.handle) {
                    Err("SetPipeline: pipeline " + std::to_string(p.pipeline) + " does not exist or failed to build");
                    pipeline = nullptr;
                    return;
                }
                pipeline = &it->second;
                pipelineId = p.pipeline;
                return;
            }
            case Cmd::SetViewport: {
                stream::SetViewport v;
                if (!c.read(v) || !Finite(v.x) || !Finite(v.y) || !Finite(v.width) || !Finite(v.height) ||
                    !Finite(v.minDepth) || !Finite(v.maxDepth) || v.width <= 0 || v.height <= 0) {
                    Err("bad viewport");
                    return;
                }
                if (collect || skip)
                    return;
                if (!inPass) {
                    Err("SetViewport outside of a pass");
                    return;
                }
                list->set_viewport({v.x, v.y, v.width, v.height, std::clamp(v.minDepth, 0.0f, 1.0f),
                                    std::clamp(v.maxDepth, 0.0f, 1.0f)});
                return;
            }
            case Cmd::SetScissor: {
                stream::SetScissor s;
                if (!c.read(s)) {
                    Err("malformed SetScissor");
                    return;
                }
                if (collect || skip)
                    return;
                if (!inPass) {
                    Err("SetScissor outside of a pass");
                    return;
                }
                const int64_t x0 = std::clamp<int64_t>(s.x, 0, pass.width);
                const int64_t y0 = std::clamp<int64_t>(s.y, 0, pass.height);
                const int64_t x1 = std::clamp<int64_t>(static_cast<int64_t>(s.x) + s.width, 0, pass.width);
                const int64_t y1 = std::clamp<int64_t>(static_cast<int64_t>(s.y) + s.height, 0, pass.height);
                list->set_scissor({static_cast<int32_t>(x0), static_cast<int32_t>(y0),
                                   static_cast<uint32_t>(std::max<int64_t>(0, x1 - x0)),
                                   static_cast<uint32_t>(std::max<int64_t>(0, y1 - y0))});
                return;
            }
            case Cmd::SetVertexBuffer: {
                stream::SetVertexBuffer v;
                if (!c.read(v) || v.slot >= vbs.size()) {
                    Err("bad SetVertexBuffer");
                    return;
                }
                vbs[v.slot] = {v.buffer, stream::join(v.offsetLo, v.offsetHi)};
                return;
            }
            case Cmd::SetIndexBuffer: {
                stream::SetIndexBuffer v;
                if (!c.read(v)) {
                    Err("bad SetIndexBuffer");
                    return;
                }
                ib = {v.buffer, stream::join(v.offsetLo, v.offsetHi)};
                ibWide = v.wide != 0;
                return;
            }
            case Cmd::SetUniforms: {
                stream::SetUniforms u;
                if (!c.read(u)) {
                    Err("malformed SetUniforms");
                    return;
                }
                const auto data = c.tail(sizeof(u));
                if (u.bytes > data.size() || static_cast<uint64_t>(u.offset) + u.bytes > kUniformMax) {
                    Err("SetUniforms: data does not fit the 4096 byte uniform block");
                    return;
                }
                std::memcpy(uniforms.data() + u.offset, data.data(), u.bytes);
                return;
            }
            case Cmd::BindTexture: {
                stream::BindTexture b;
                if (!c.read(b) || b.slot >= slots.size()) {
                    Err("bad BindTexture");
                    return;
                }
                slots[b.slot] = {b.texture, b.sampler, b.layer};
                return;
            }
            case Cmd::BindStorage: {
                stream::BindStorage b;
                if (!c.read(b) || b.binding > 15) {
                    Err("bad BindStorage");
                    return;
                }
                storage[b.binding] = b.resource;
                return;
            }
            case Cmd::Draw: {
                stream::Draw d;
                if (!c.read(d)) {
                    Err("malformed Draw");
                    return;
                }
                Draw(false, d.vertexCount, d.instanceCount, d.firstVertex, 0, d.firstInstance, 0, 0, 0, 0);
                return;
            }
            case Cmd::DrawIndexed: {
                stream::DrawIndexed d;
                if (!c.read(d)) {
                    Err("malformed DrawIndexed");
                    return;
                }
                Draw(true, d.indexCount, d.instanceCount, d.firstIndex, d.vertexOffset, d.firstInstance, 0, 0, 0, 0);
                return;
            }
            case Cmd::DrawIndexedIndirect: {
                stream::DrawIndexedIndirect d;
                if (!c.read(d)) {
                    Err("malformed DrawIndexedIndirect");
                    return;
                }
                Draw(true, 0, 0, 0, 0, 0, d.buffer, stream::join(d.offsetLo, d.offsetHi), d.maxDraws, d.stride);
                return;
            }
            case Cmd::Dispatch: {
                stream::Dispatch d;
                if (!c.read(d)) {
                    Err("malformed Dispatch");
                    return;
                }
                Dispatch(d);
                return;
            }
            case Cmd::CopyBuffer: {
                stream::CopyBuffer d;
                if (c.read(d))
                    CopyBuffer(d);
                else
                    Err("malformed CopyBuffer");
                return;
            }
            case Cmd::CopyTexture: {
                stream::CopyTexture d;
                if (c.read(d))
                    CopyTexture(d);
                else
                    Err("malformed CopyTexture");
                return;
            }
            case Cmd::CopyScreen: {
                stream::CopyScreen d;
                if (c.read(d))
                    CopyScreen(d);
                else
                    Err("malformed CopyScreen");
                return;
            }
            case Cmd::FillBuffer: {
                stream::FillBuffer d;
                if (c.read(d))
                    FillBuffer(d);
                else
                    Err("malformed FillBuffer");
                return;
            }
            case Cmd::PushLabel: {
                if (collect)
                    return;
                stream::PushLabel l;
                if (!c.read(l)) {
                    Err("malformed PushLabel");
                    return;
                }
                const auto text = c.tail(sizeof(l));
                if (l.length > text.size() || l.length > 128) {
                    Err("malformed PushLabel");
                    return;
                }
                const std::string name(reinterpret_cast<const char*>(text.data()), l.length);
                list->begin_debug_label(name.c_str());
                labels++;
                return;
            }
            case Cmd::PopLabel:
                if (!collect && labels) {
                    list->end_debug_label();
                    labels--;
                }
                return;
            case Cmd::BeginPass:
            case Cmd::EndPass:
                return;
            default:
                Err("unknown command " + std::to_string(static_cast<unsigned>(c.op)));
                return;
            }
        }
    };

    void ModGpu::UseBuffer(conduit::command_list& list, const std::string& plugin, uint32_t id, Buffer& b,
                           conduit::state want) {
        if (b.current == want) {
            if (want == conduit::state::shader_write)
                list.uav_barrier(b.handle);
            return;
        }
        const bool wantWrite = conduit::is_write_state(want);
        if (!wantWrite && b.current != conduit::state::undefined && !conduit::is_write_state(b.current) &&
            (static_cast<uint32_t>(b.current) & static_cast<uint32_t>(want)) == static_cast<uint32_t>(want))
            return;
        conduit::buffer_barrier bb;
        bb.buffer = b.handle;
        bb.before = b.current;
        bb.after = want;
        list.barrier(bb);
        b.current = want;
        const auto key = std::make_pair(plugin, id);
        if (std::find(touched.begin(), touched.end(), key) == touched.end())
            touched.push_back(key);
    }

    void ModGpu::RestoreBuffers(conduit::command_list& list) {
        for (const auto& [plugin, id] : touched) {
            const auto p = plugins.find(plugin);
            if (p == plugins.end())
                continue;
            const auto it = p->second.buffers.find(id);
            if (it == p->second.buffers.end() || it->second.current == it->second.resting)
                continue;
            conduit::buffer_barrier bb;
            bb.buffer = it->second.handle;
            bb.before = it->second.current;
            bb.after = it->second.resting;
            list.barrier(bb);
            it->second.current = it->second.resting;
        }
        touched.clear();
    }

    void ModGpu::FlushWrites(conduit::command_list& list) {
        std::vector<PendingWrite> work;
        work.swap(pending);
        for (const PendingWrite& w : work) {
            const auto p = plugins.find(w.plugin);
            if (p == plugins.end())
                continue;
            const auto it = p->second.buffers.find(w.id);
            if (it == p->second.buffers.end() || !w.bytes || w.offset + w.bytes->size() > it->second.size)
                continue;
            const RingSlice staging = gpu.Ring(w.bytes->size(), 16);
            if (!staging.data) {
                Report(w.plugin, "out of upload memory for a buffer write");
                continue;
            }
            std::memcpy(staging.data, w.bytes->data(), w.bytes->size());
            UseBuffer(list, w.plugin, w.id, it->second, conduit::state::copy_dst);
            list.copy_buffer(it->second.handle, w.offset, staging.buffer, staging.offset, w.bytes->size());
        }
    }

    void ModGpu::FlushReads(conduit::command_list& list) {
        for (PendingRead& r : reads) {
            if (r.copied)
                continue;
            const auto p = plugins.find(r.plugin);
            if (p == plugins.end())
                continue;
            const auto it = p->second.buffers.find(r.id);
            if (it == p->second.buffers.end())
                continue;
            UseBuffer(list, r.plugin, r.id, it->second, conduit::state::copy_src);
            list.copy_buffer(r.staging, 0, it->second.handle, r.offset, r.size);
            r.copied = true;
            r.issuedFrame = gpu.Dev().frame_number();
        }
    }

    void ModGpu::RunLayer(conduit::command_list& list, const ModLayer& layer, const Screen* screen) {
        if (!layer.stream)
            return;
        PluginResources& res = plugins[layer.plugin];
        stream::Reader reader(std::span<const uint8_t>(layer.stream->data(), layer.stream->size()));
        std::vector<stream::Command> cmds;
        stream::Command c;
        while (reader.next(c))
            cmds.push_back(c);
        if (!reader.complete()) {
            Report(layer.plugin, "layer " + layer.name + ": malformed command stream");
            return;
        }
        Run run(*this, res, layer, &list, screen);
        for (size_t i = 0; i < cmds.size(); i++) {
            if (cmds[i].op == Cmd::BeginPass)
                run.BeginPass(cmds[i], cmds, i);
            else if (cmds[i].op == Cmd::EndPass)
                run.EndPass();
            else if (!run.skip || (cmds[i].op != Cmd::Draw && cmds[i].op != Cmd::DrawIndexed &&
                                   cmds[i].op != Cmd::DrawIndexedIndirect))
                run.Exec(cmds[i]);
        }
        if (run.inPass) {
            run.Err("a pass was not closed with EndPass");
            if (!run.skip)
                list.end_rendering();
        }
        for (; run.labels; run.labels--)
            list.end_debug_label();
    }

    void ModGpu::Record(conduit::command_list& list, ModStage stage, const Screen* screen) {
        if (!started)
            return;
        if (stage == ModStage::Pre) {
            FlushWrites(list);
        }
        std::vector<ModLayer> layers;
        ModGpuHub::Instance().Layers(stage, layers);
        for (const ModLayer& layer : layers)
            RunLayer(list, layer, screen);
        if (stage == ModStage::Overlay)
            FlushReads(list);
        RestoreBuffers(list);
    }
}
