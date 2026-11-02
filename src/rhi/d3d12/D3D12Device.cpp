#include "rhi/d3d12/D3D12Device.hpp"

#include "rhi/d3d12/D3D12Convert.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace rhi::d3d12 {
    namespace {
        uint64_t Fnv(const void* data, size_t size, uint64_t h = 1469598103934665603ull) {
            const auto* p = static_cast<const uint8_t*>(data);
            for (size_t i = 0; i < size; i++)
                h = (h ^ p[i]) * 1099511628211ull;
            return h;
        }

        std::wstring HashName(const char* prefix, uint64_t hash) {
            wchar_t buf[40];
            swprintf(buf, 40, L"%hs%016llx", prefix, static_cast<unsigned long long>(hash));
            return buf;
        }

        std::wstring Widen(const char* s) {
            std::wstring out;
            for (; s && *s; s++)
                out.push_back(static_cast<unsigned char>(*s));
            return out;
        }

        D3D12_SHADER_RESOURCE_VIEW_DESC BufferSrv(uint64_t offset, uint64_t size, uint32_t stride) {
            D3D12_SHADER_RESOURCE_VIEW_DESC d{};
            d.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
            d.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            if (stride) {
                d.Format = DXGI_FORMAT_UNKNOWN;
                d.Buffer.FirstElement = offset / stride;
                d.Buffer.NumElements = static_cast<UINT>(size / stride);
                d.Buffer.StructureByteStride = stride;
            } else {
                d.Format = DXGI_FORMAT_R32_TYPELESS;
                d.Buffer.FirstElement = offset / 4;
                d.Buffer.NumElements = static_cast<UINT>(size / 4);
                d.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
            }
            return d;
        }

        D3D12_UNORDERED_ACCESS_VIEW_DESC BufferUav(uint64_t offset, uint64_t size, uint32_t stride) {
            D3D12_UNORDERED_ACCESS_VIEW_DESC d{};
            d.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
            if (stride) {
                d.Format = DXGI_FORMAT_UNKNOWN;
                d.Buffer.FirstElement = offset / stride;
                d.Buffer.NumElements = static_cast<UINT>(size / stride);
                d.Buffer.StructureByteStride = stride;
            } else {
                d.Format = DXGI_FORMAT_R32_TYPELESS;
                d.Buffer.FirstElement = offset / 4;
                d.Buffer.NumElements = static_cast<UINT>(size / 4);
                d.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
            }
            return d;
        }
    }

    BufferHandle Device::CreateBuffer(const BufferDesc& desc) {
        if (desc.size == 0) {
            ReportError("CreateBuffer: zero size");
            return {};
        }
        BufferRec rec;
        rec.desc = desc;
        rec.name = desc.name ? desc.name : "";

        // constant buffer views must span a multiple of 256 bytes
        uint64_t size = desc.size;
        if (Has(desc.usage, BufferUsage::Uniform))
            size = AlignUp(size, 256);

        D3D12_RESOURCE_DESC rd{};
        rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        rd.Width = size;
        rd.Height = rd.DepthOrArraySize = rd.MipLevels = 1;
        rd.SampleDesc.Count = 1;
        rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (Has(desc.usage, BufferUsage::Storage))
            rd.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        MemoryClass memory = desc.memory;
        if (memory == MemoryClass::GpuMappable)
            memory = MemoryClass::Upload;
        D3D12MA::ALLOCATION_DESC ad{};
        D3D12_RESOURCE_STATES initial = D3D12_RESOURCE_STATE_COMMON;
        switch (memory) {
        case MemoryClass::Upload:
            ad.HeapType = D3D12_HEAP_TYPE_UPLOAD;
            initial = D3D12_RESOURCE_STATE_GENERIC_READ;
            break;
        case MemoryClass::Readback:
            ad.HeapType = D3D12_HEAP_TYPE_READBACK;
            initial = D3D12_RESOURCE_STATE_COPY_DEST;
            break;
        default:
            ad.HeapType = D3D12_HEAP_TYPE_DEFAULT;
            break;
        }
        rec.state = initial;
        if (FAILED(allocator->CreateResource(&ad, &rd, initial, nullptr, rec.alloc.put(),
                                             winrt::guid_of<ID3D12Resource>(), rec.resource.put_void()))) {
            ReportError("CreateBuffer '" + rec.name + "' failed");
            return {};
        }
        if (memory != MemoryClass::GpuOnly) {
            D3D12_RANGE none{0, 0};
            void* ptr = nullptr;
            if (SUCCEEDED(rec.resource->Map(0, memory == MemoryClass::Readback ? nullptr : &none, &ptr)))
                rec.mapped = static_cast<uint8_t*>(ptr);
        }
        rec.address = rec.resource->GetGPUVirtualAddress();
        if (!rec.name.empty())
            rec.resource->SetName(Widen(rec.name.c_str()).c_str());
        bufferBytes += static_cast<int64_t>(desc.size);
        liveBuffers++;

        BufferHandle h = buffers.Alloc(std::move(rec));
        BufferRec* stored = buffers.Get(h);
        stored->desc.name = stored->name.c_str();
        stored->desc.memory = memory;
        if (useValidator)
            validator.OnCreate(h, stored->desc);
        return h;
    }

    TextureHandle Device::CreateTexture(const TextureDesc& desc) {
        if (desc.format == Format::Unknown || desc.width == 0 || desc.height == 0) {
            ReportError("CreateTexture: invalid description");
            return {};
        }
        TextureRec rec;
        rec.desc = desc;
        rec.desc.mips = (std::max)(1u, desc.mips);
        rec.name = desc.name ? desc.name : "";
        const FormatMap fm = MapFormat(desc.format);
        rec.resourceFormat = fm.resource;
        rec.viewFormat = fm.view;
        rec.dsvFormat = fm.dsv;
        rec.planes = fm.planes;
        rec.mips = rec.desc.mips;
        rec.layers = LayerCountOf(desc);

        D3D12_RESOURCE_DESC rd{};
        rd.Dimension = desc.dim == TextureDim::Tex3D ? D3D12_RESOURCE_DIMENSION_TEXTURE3D
                                                     : D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        rd.Width = desc.width;
        rd.Height = desc.height;
        rd.DepthOrArraySize = static_cast<UINT16>(desc.dim == TextureDim::Tex3D ? DepthOf(desc) : rec.layers);
        rd.MipLevels = static_cast<UINT16>(rec.mips);
        rd.Format = fm.resource;
        rd.SampleDesc.Count = (std::max)(1u, desc.samples);
        rd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        const bool depth = fm.dsv != DXGI_FORMAT_UNKNOWN;
        if (Has(desc.usage, TextureUsage::ColorTarget))
            rd.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        if (Has(desc.usage, TextureUsage::DepthTarget))
            rd.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        if (Has(desc.usage, TextureUsage::Storage))
            rd.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        if (desc.crossQueue && !depth && desc.samples <= 1) {
            rd.Flags |= D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;
            rec.simultaneous = true;
        }
        if (depth && Has(desc.usage, TextureUsage::DepthTarget) && !Has(desc.usage, TextureUsage::Sampled))
            rd.Flags |= D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;

        D3D12_CLEAR_VALUE clear{};
        const D3D12_CLEAR_VALUE* pClear = nullptr;
        if (Has(desc.usage, TextureUsage::DepthTarget)) {
            clear.Format = fm.dsv;
            clear.DepthStencil = {1.0f, 0};
            pClear = &clear;
        } else if (Has(desc.usage, TextureUsage::ColorTarget)) {
            clear.Format = fm.view;
            std::memcpy(clear.Color, desc.optimizedClear.data(), sizeof(float) * 4);
            pClear = &clear;
        }

        D3D12MA::ALLOCATION_DESC ad{};
        ad.HeapType = D3D12_HEAP_TYPE_DEFAULT;
        // render targets get their own heap: drivers place them for compression and the allocator never fragments them
        if (pClear)
            ad.Flags = D3D12MA::ALLOCATION_FLAG_COMMITTED;
        if (FAILED(allocator->CreateResource(&ad, &rd, D3D12_RESOURCE_STATE_COMMON, pClear, rec.alloc.put(),
                                             winrt::guid_of<ID3D12Resource>(), rec.resource.put_void()))) {
            ReportError("CreateTexture '" + rec.name + "' failed");
            return {};
        }
        if (!rec.name.empty())
            rec.resource->SetName(Widen(rec.name.c_str()).c_str());
        rec.states.assign(static_cast<size_t>(rec.mips) * (desc.dim == TextureDim::Tex3D ? 1 : rec.layers) *
                              rec.planes,
                          D3D12_RESOURCE_STATE_COMMON);

        uint64_t bytes = 0;
        for (uint32_t m = 0; m < rec.mips; m++)
            bytes += static_cast<uint64_t>(RowBytes(desc.format, MipExtent(desc.width, m))) *
                     RowBlocks(desc.format, MipExtent(desc.height, m)) * DepthOf(desc) * rec.layers *
                     (std::max)(1u, desc.samples);
        textureBytes += static_cast<int64_t>(bytes);
        liveTextures++;

        TextureHandle h = textures.Alloc(std::move(rec));
        TextureRec* stored = textures.Get(h);
        stored->desc.name = stored->name.c_str();
        if (useValidator)
            validator.OnCreate(h, stored->desc);
        return h;
    }

    TextureHandle Device::WrapSwapchainImage(ID3D12Resource* resource, const TextureDesc& desc) {
        TextureRec rec;
        rec.resource.copy_from(resource);
        rec.desc = desc;
        rec.name = "backbuffer";
        const FormatMap fm = MapFormat(desc.format);
        rec.resourceFormat = fm.resource;
        rec.viewFormat = fm.view;
        rec.mips = rec.layers = rec.planes = 1;
        rec.external = true;
        rec.states.assign(1, D3D12_RESOURCE_STATE_COMMON);
        TextureHandle h = textures.Alloc(std::move(rec));
        TextureRec* stored = textures.Get(h);
        stored->desc.name = stored->name.c_str();
        if (useValidator)
            validator.OnCreate(h, stored->desc);
        return h;
    }

    void Device::DestroyWrapped(TextureHandle h) {
        TextureRec rec;
        if (!textures.Free(h, &rec))
            return;
        if (useValidator)
            validator.OnDestroy(h);
        for (auto& [k, v] : rec.views.rtv)
            rtvPool.Free(v);
    }

    D3D12_CPU_DESCRIPTOR_HANDLE Device::RenderView(TextureRec& tex, uint32_t mip, uint32_t layer, bool depth,
                                                   bool readOnly) {
        const uint64_t key = mip | (static_cast<uint64_t>(layer) << 8) | (readOnly ? 1ull << 41 : 0);
        std::lock_guard lock(viewMutex);
        auto& map = depth ? tex.views.dsv : tex.views.rtv;
        auto it = map.find(key);
        if (it != map.end())
            return depth ? dsvPool.Handle(it->second) : rtvPool.Handle(it->second);
        const bool ms = tex.desc.samples > 1;
        if (depth) {
            const uint32_t index = dsvPool.Alloc();
            D3D12_DEPTH_STENCIL_VIEW_DESC dv{};
            dv.Format = tex.dsvFormat;
            if (readOnly)
                dv.Flags = D3D12_DSV_FLAG_READ_ONLY_DEPTH |
                           (tex.planes > 1 ? D3D12_DSV_FLAG_READ_ONLY_STENCIL : D3D12_DSV_FLAG_NONE);
            dv.ViewDimension = ms ? D3D12_DSV_DIMENSION_TEXTURE2DMSARRAY : D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
            if (ms) {
                dv.Texture2DMSArray.FirstArraySlice = layer;
                dv.Texture2DMSArray.ArraySize = 1;
            } else {
                dv.Texture2DArray.MipSlice = mip;
                dv.Texture2DArray.FirstArraySlice = layer;
                dv.Texture2DArray.ArraySize = 1;
            }
            device->CreateDepthStencilView(tex.resource.get(), &dv, dsvPool.Handle(index));
            map.emplace(key, index);
            return dsvPool.Handle(index);
        }
        const uint32_t index = rtvPool.Alloc();
        D3D12_RENDER_TARGET_VIEW_DESC rv{};
        rv.Format = tex.viewFormat;
        rv.ViewDimension = ms ? D3D12_RTV_DIMENSION_TEXTURE2DMSARRAY : D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
        if (ms) {
            rv.Texture2DMSArray.FirstArraySlice = layer;
            rv.Texture2DMSArray.ArraySize = 1;
        } else {
            rv.Texture2DArray.MipSlice = mip;
            rv.Texture2DArray.FirstArraySlice = layer;
            rv.Texture2DArray.ArraySize = 1;
        }
        device->CreateRenderTargetView(tex.resource.get(), &rv, rtvPool.Handle(index));
        map.emplace(key, index);
        return rtvPool.Handle(index);
    }

    SamplerHandle Device::CreateSampler(const SamplerDesc& desc) {
        std::lock_guard lock(samplerMutex);
        for (auto& [d, h] : samplerCache)
            if (d == desc)
                return h;
        SamplerRec rec;
        rec.desc = desc;
        SamplerHandle h = samplers.Alloc(std::move(rec));
        samplerCache.emplace_back(desc, h);
        return h;
    }

    void Device::WriteSamplerDesc(D3D12_CPU_DESCRIPTOR_HANDLE dst, const SamplerDesc& d) {
        D3D12_SAMPLER_DESC sd{};
        sd.Filter = ToD3D(d);
        sd.AddressU = ToD3D(d.u);
        sd.AddressV = ToD3D(d.v);
        sd.AddressW = ToD3D(d.w);
        sd.MipLODBias = d.lodBias;
        sd.MaxAnisotropy = static_cast<UINT>(std::clamp(d.maxAnisotropy, 1.0f, 16.0f));
        sd.ComparisonFunc = d.compare ? ToD3D(d.compareOp) : D3D12_COMPARISON_FUNC_NEVER;
        std::memcpy(sd.BorderColor, d.border.data(), sizeof(float) * 4);
        sd.MinLOD = d.minLod;
        sd.MaxLOD = d.maxLod;
        device->CreateSampler(&sd, dst);
    }

    int Device::AcquireSamplerTable(const std::vector<uint32_t>& key, const std::vector<SamplerDesc>& descs) {
        std::lock_guard lock(samplerMutex);
        for (size_t i = 0; i < samplerTables.size(); i++)
            if (samplerTables[i].refs > 0 && samplerTables[i].key == key) {
                samplerTables[i].refs++;
                return static_cast<int>(i);
            }
        uint32_t offset = 0;
        if (!samplerHeap.Alloc(static_cast<uint32_t>(descs.size()), offset)) {
            ReportError("sampler descriptor heap exhausted");
            return -1;
        }
        for (size_t i = 0; i < descs.size(); i++)
            WriteSamplerDesc(samplerHeap.Cpu(offset + static_cast<uint32_t>(i)), descs[i]);
        SamplerTable table;
        table.offset = offset;
        table.count = static_cast<uint32_t>(descs.size());
        table.refs = 1;
        table.key = key;
        for (size_t i = 0; i < samplerTables.size(); i++)
            if (samplerTables[i].refs == 0) {
                samplerTables[i] = std::move(table);
                return static_cast<int>(i);
            }
        samplerTables.push_back(std::move(table));
        return static_cast<int>(samplerTables.size() - 1);
    }

    void Device::ReleaseSamplerTable(int index) {
        if (index < 0)
            return;
        std::lock_guard lock(samplerMutex);
        SamplerTable& t = samplerTables[static_cast<size_t>(index)];
        if (--t.refs == 0) {
            samplerHeap.Free(t.offset, t.count);
            t.key.clear();
        }
    }

    ShaderHandle Device::CreateShader(const ShaderDesc& desc) {
        if (desc.format != ShaderFormat::Dxil && desc.format != ShaderFormat::Dxbc) {
            ReportError("CreateShader: expects DXIL");
            return {};
        }
        ShaderRec rec;
        rec.code.assign(desc.code.begin(), desc.code.end());
        rec.stage = desc.stage;
        return shaders.Alloc(std::move(rec));
    }

    BindLayoutHandle Device::CreateBindLayout(const BindLayoutDesc& desc) {
        BindLayoutRec rec;
        rec.entries.assign(desc.entries.begin(), desc.entries.end());
        for (const BindLayoutEntry& e : rec.entries) {
            const uint32_t count = (std::max)(1u, e.count);
            switch (e.type) {
            case BindingType::UniformBufferDynamic:
                rec.dynamicBindings.push_back(e.binding);
                rec.tableOffset.push_back(0);
                break;
            case BindingType::Sampler:
                rec.tableOffset.push_back(rec.samplerCount);
                rec.samplerCount += count;
                break;
            default:
                rec.tableOffset.push_back(rec.tableCount);
                rec.tableCount += count;
                break;
            }
        }
        std::sort(rec.dynamicBindings.begin(), rec.dynamicBindings.end());
        rec.dynamicCount = static_cast<uint32_t>(rec.dynamicBindings.size());
        if (rec.dynamicCount > kMaxDynamicOffsets) {
            ReportError("CreateBindLayout: too many dynamic uniform buffers");
            return {};
        }
        return bindLayouts.Alloc(std::move(rec));
    }

    BindGroupHandle Device::CreateBindGroup(const BindGroupDesc& desc) {
        BindLayoutRec* layout = bindLayouts.Get(desc.layout);
        if (!layout) {
            ReportError("CreateBindGroup: unknown layout");
            return {};
        }
        BindGroupRec rec;
        rec.layout = desc.layout;
        rec.perFrame = desc.perFrame;
        rec.dynamicCount = layout->dynamicCount;
        rec.tableCount = layout->tableCount;
        if (rec.tableCount && !resourceHeap.Alloc(rec.tableCount, rec.tableOffset)) {
            ReportError("CreateBindGroup: descriptor heap exhausted");
            return {};
        }

        std::vector<uint32_t> samplerKey(layout->samplerCount, 0);
        std::vector<SamplerDesc> samplerDescs(layout->samplerCount);

        for (const BindGroupEntry& e : desc.entries) {
            size_t index = SIZE_MAX;
            for (size_t i = 0; i < layout->entries.size(); i++)
                if (layout->entries[i].binding == e.binding)
                    index = i;
            if (index == SIZE_MAX) {
                ReportError("CreateBindGroup: binding " + std::to_string(e.binding) +
                            " is not in the layout");
                continue;
            }
            const BindLayoutEntry& le = layout->entries[index];
            const uint32_t slotIndex = layout->tableOffset[index] + e.arrayIndex;

            switch (le.type) {
            case BindingType::UniformBuffer:
            case BindingType::UniformBufferDynamic:
            case BindingType::StorageBuffer:
            case BindingType::StorageBufferRW: {
                BufferRec* b = buffers.Get(e.buffer.buffer);
                if (!b) {
                    ReportError("CreateBindGroup: binding " + std::to_string(e.binding) + " has no buffer");
                    continue;
                }
                if (useValidator) {
                    StateValidator::BoundResource br;
                    br.buffer = e.buffer.buffer;
                    br.want = le.type == BindingType::StorageBufferRW ? State::ShaderWrite
                              : le.type == BindingType::StorageBuffer ? State::ShaderRead
                                                                      : State::UniformBuffer;
                    rec.bound.push_back(br);
                }
                const uint64_t size = e.buffer.size ? e.buffer.size : b->desc.size - e.buffer.offset;
                if (le.type == BindingType::UniformBufferDynamic) {
                    const auto it =
                        std::find(layout->dynamicBindings.begin(), layout->dynamicBindings.end(), le.binding);
                    rec.dynamicBase[static_cast<size_t>(it - layout->dynamicBindings.begin())] =
                        b->address + e.buffer.offset;
                } else if (le.type == BindingType::UniformBuffer) {
                    D3D12_CONSTANT_BUFFER_VIEW_DESC cv{b->address + e.buffer.offset,
                                                       static_cast<UINT>(AlignUp(size, 256))};
                    device->CreateConstantBufferView(&cv, resourceHeap.Cpu(rec.tableOffset + slotIndex));
                } else if (le.type == BindingType::StorageBuffer) {
                    const auto sv = BufferSrv(e.buffer.offset, size, b->desc.stride);
                    device->CreateShaderResourceView(b->resource.get(), &sv,
                                                     resourceHeap.Cpu(rec.tableOffset + slotIndex));
                } else {
                    const auto uv = BufferUav(e.buffer.offset, size, b->desc.stride);
                    device->CreateUnorderedAccessView(b->resource.get(), nullptr, &uv,
                                                      resourceHeap.Cpu(rec.tableOffset + slotIndex));
                }
                break;
            }
            case BindingType::SampledTexture:
            case BindingType::StorageTextureRW: {
                TextureRec* t = textures.Get(e.texture.texture);
                if (!t) {
                    ReportError("CreateBindGroup: binding " + std::to_string(e.binding) + " has no texture");
                    continue;
                }
                if (useValidator) {
                    StateValidator::BoundResource br;
                    br.texture = e.texture.texture;
                    br.range = e.texture.range;
                    br.want = le.type == BindingType::StorageTextureRW ? State::ShaderWrite
                                                                       : State::DepthReadSampled;
                    rec.bound.push_back(br);
                }
                const SubresourceRange r = ResolveRange(e.texture.range, t->desc);
                const TextureDim dim =
                    e.texture.viewDim != TextureDim::Tex2D ? e.texture.viewDim : le.viewDim;
                const FormatMap override = MapFormat(e.texture.format);
                const DXGI_FORMAT fmt = e.texture.stencil ? MapFormat(t->desc.format).stencilView
                                        : e.texture.format != Format::Unknown ? override.view
                                                                              : t->viewFormat;
                if (le.type == BindingType::SampledTexture) {
                    D3D12_SHADER_RESOURCE_VIEW_DESC sv{};
                    sv.Format = fmt;
                    sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                    const bool ms = t->desc.samples > 1;
                    switch (dim) {
                    case TextureDim::Tex2D:
                        if (ms) {
                            sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
                        } else {
                            sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                            sv.Texture2D = {r.baseMip, r.mipCount, e.texture.stencil ? 1u : 0u, 0.0f};
                        }
                        break;
                    case TextureDim::Tex2DArray:
                        sv.ViewDimension =
                            ms ? D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY : D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
                        if (ms)
                            sv.Texture2DMSArray = {r.baseLayer, r.layerCount};
                        else
                            sv.Texture2DArray = {
                                r.baseMip, r.mipCount, r.baseLayer, r.layerCount, e.texture.stencil ? 1u : 0u,
                                0.0f};
                        break;
                    case TextureDim::Cube:
                        sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
                        sv.TextureCube = {r.baseMip, r.mipCount, 0.0f};
                        break;
                    case TextureDim::Tex3D:
                        sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
                        sv.Texture3D = {r.baseMip, r.mipCount, 0.0f};
                        break;
                    }
                    device->CreateShaderResourceView(t->resource.get(), &sv,
                                                     resourceHeap.Cpu(rec.tableOffset + slotIndex));
                } else {
                    D3D12_UNORDERED_ACCESS_VIEW_DESC uv{};
                    uv.Format = fmt;
                    switch (dim) {
                    case TextureDim::Tex2D:
                        uv.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
                        uv.Texture2D = {r.baseMip, 0};
                        break;
                    case TextureDim::Tex3D:
                        uv.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
                        uv.Texture3D = {r.baseMip, 0,
                                        DepthOf(t->desc) >> r.baseMip
                                            ? (std::max)(1u, DepthOf(t->desc) >> r.baseMip)
                                            : 1u};
                        break;
                    default:
                        uv.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
                        uv.Texture2DArray = {r.baseMip, r.baseLayer, r.layerCount, 0};
                        break;
                    }
                    device->CreateUnorderedAccessView(t->resource.get(), nullptr, &uv,
                                                      resourceHeap.Cpu(rec.tableOffset + slotIndex));
                }
                break;
            }
            case BindingType::Sampler: {
                SamplerRec* s = samplers.Get(e.sampler);
                if (!s) {
                    ReportError("CreateBindGroup: binding " + std::to_string(e.binding) + " has no sampler");
                    continue;
                }
                const uint32_t at = layout->tableOffset[index] + e.arrayIndex;
                samplerKey[at] = e.sampler.value;
                samplerDescs[at] = s->desc;
                break;
            }
            }
        }

        if (layout->samplerCount) {
            rec.samplerTable = AcquireSamplerTable(samplerKey, samplerDescs);
            if (rec.samplerTable >= 0) {
                std::lock_guard lock(samplerMutex);
                rec.samplerOffset = samplerTables[static_cast<size_t>(rec.samplerTable)].offset;
            }
        }
        liveGroups++;
        BindGroupHandle h = bindGroups.Alloc(std::move(rec));
        if (desc.perFrame) {
            std::lock_guard lock(frameGroupMutex);
            frameGroups[slot].groups.push_back(h);
        }
        return h;
    }

    PipelineLayoutHandle Device::CreatePipelineLayout(const PipelineLayoutDesc& desc) {
        PipelineLayoutRec rec;
        rec.sets = desc.sets;
        rec.tableParam.fill(-1);
        rec.samplerParam.fill(-1);
        for (auto& row : rec.dynamicParam)
            row.fill(-1);
        rec.pushBytes = desc.pushConstantBytes;

        // ranges must not move once params point into them
        std::array<std::vector<D3D12_DESCRIPTOR_RANGE1>, kMaxBindGroups> tableRanges;
        std::array<std::vector<D3D12_DESCRIPTOR_RANGE1>, kMaxBindGroups> samplerRanges;
        for (uint32_t s = 0; s < kMaxBindGroups; s++) {
            BindLayoutRec* l = bindLayouts.Get(desc.sets[s]);
            if (!l)
                continue;
            tableRanges[s].reserve(l->entries.size());
            samplerRanges[s].reserve(l->entries.size());
            for (size_t i = 0; i < l->entries.size(); i++) {
                const BindLayoutEntry& e = l->entries[i];
                D3D12_DESCRIPTOR_RANGE1 r{};
                r.NumDescriptors = (std::max)(1u, e.count);
                r.BaseShaderRegister = e.binding;
                r.RegisterSpace = s;
                r.OffsetInDescriptorsFromTableStart = l->tableOffset[i];
                switch (e.type) {
                case BindingType::UniformBuffer:
                    r.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
                    break;
                case BindingType::StorageBuffer:
                case BindingType::SampledTexture:
                    r.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
                    break;
                case BindingType::StorageBufferRW:
                case BindingType::StorageTextureRW:
                    r.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
                    break;
                case BindingType::Sampler:
                    r.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
                    break;
                case BindingType::UniformBufferDynamic:
                    continue;
                }
                (e.type == BindingType::Sampler ? samplerRanges[s] : tableRanges[s]).push_back(r);
            }
        }

        std::vector<D3D12_ROOT_PARAMETER1> params;
        params.reserve(2 + kMaxBindGroups * (2 + kMaxDynamicOffsets));
        if (desc.pushConstantBytes) {
            D3D12_ROOT_PARAMETER1 p{};
            p.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
            p.Constants = {0, 15, desc.pushConstantBytes / 4};
            p.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            rec.pushParam = static_cast<int>(params.size());
            params.push_back(p);
        }
        for (uint32_t s = 0; s < kMaxBindGroups; s++) {
            BindLayoutRec* l = bindLayouts.Get(desc.sets[s]);
            if (!l)
                continue;
            if (!tableRanges[s].empty()) {
                D3D12_ROOT_PARAMETER1 p{};
                p.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
                p.DescriptorTable = {static_cast<UINT>(tableRanges[s].size()), tableRanges[s].data()};
                p.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
                rec.tableParam[s] = static_cast<int>(params.size());
                params.push_back(p);
            }
            if (!samplerRanges[s].empty()) {
                D3D12_ROOT_PARAMETER1 p{};
                p.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
                p.DescriptorTable = {static_cast<UINT>(samplerRanges[s].size()), samplerRanges[s].data()};
                p.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
                rec.samplerParam[s] = static_cast<int>(params.size());
                params.push_back(p);
            }
            for (size_t k = 0; k < l->dynamicBindings.size(); k++) {
                D3D12_ROOT_PARAMETER1 p{};
                p.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
                p.Descriptor = {l->dynamicBindings[k], s, D3D12_ROOT_DESCRIPTOR_FLAG_NONE};
                p.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
                rec.dynamicParam[s][k] = static_cast<int>(params.size());
                params.push_back(p);
            }
        }

        D3D12_VERSIONED_ROOT_SIGNATURE_DESC vd{};
        vd.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
        vd.Desc_1_1.NumParameters = static_cast<UINT>(params.size());
        vd.Desc_1_1.pParameters = params.data();
        vd.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        winrt::com_ptr<ID3DBlob> blob, error;
        if (FAILED(inst->lib.serializeRootSignature(&vd, blob.put(), error.put()))) {
            ReportError(std::string("root signature serialization failed: ") +
                        (error ? static_cast<const char*>(error->GetBufferPointer()) : "unknown"));
            return {};
        }
        if (FAILED(device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
                                               winrt::guid_of<ID3D12RootSignature>(),
                                               rec.rootSignature.put_void()))) {
            ReportError("CreateRootSignature failed (more than 64 root dwords?)");
            return {};
        }
        rec.hash = Fnv(blob->GetBufferPointer(), blob->GetBufferSize());
        if (desc.name)
            rec.rootSignature->SetName(Widen(desc.name).c_str());
        return pipelineLayouts.Alloc(std::move(rec));
    }

    PipelineHandle Device::CreateGraphicsPipeline(const GraphicsPipelineDesc& desc) {
        PipelineLayoutRec* layout = pipelineLayouts.Get(desc.layout);
        ShaderRec* vs = shaders.Get(desc.vertex);
        ShaderRec* fs = shaders.Get(desc.fragment);
        if (!layout || !vs) {
            ReportError("CreateGraphicsPipeline: missing layout or vertex shader");
            return {};
        }

        D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};
        pd.pRootSignature = layout->rootSignature.get();
        pd.VS = {vs->code.data(), vs->code.size()};
        if (fs)
            pd.PS = {fs->code.data(), fs->code.size()};

        pd.BlendState.AlphaToCoverageEnable = desc.blend.alphaToCoverage;
        pd.BlendState.IndependentBlendEnable = TRUE;
        for (uint32_t i = 0; i < kMaxColorTargets; i++) {
            const BlendTarget& t = desc.blend.targets[i];
            D3D12_RENDER_TARGET_BLEND_DESC& rt = pd.BlendState.RenderTarget[i];
            rt.BlendEnable = t.enable;
            rt.SrcBlend = ToD3D(t.srcColor, false);
            rt.DestBlend = ToD3D(t.dstColor, false);
            rt.BlendOp = ToD3D(t.colorOp);
            rt.SrcBlendAlpha = ToD3D(t.srcAlpha, true);
            rt.DestBlendAlpha = ToD3D(t.dstAlpha, true);
            rt.BlendOpAlpha = ToD3D(t.alphaOp);
            rt.RenderTargetWriteMask = static_cast<UINT8>(t.writeMask);
            rt.LogicOp = D3D12_LOGIC_OP_NOOP;
        }
        pd.SampleMask = UINT_MAX;

        D3D12_RASTERIZER_DESC& rs = pd.RasterizerState;
        rs.FillMode = desc.raster.wireframe ? D3D12_FILL_MODE_WIREFRAME : D3D12_FILL_MODE_SOLID;
        rs.CullMode = desc.raster.cull == CullMode::None   ? D3D12_CULL_MODE_NONE
                      : desc.raster.cull == CullMode::Back ? D3D12_CULL_MODE_BACK
                                                           : D3D12_CULL_MODE_FRONT;
        rs.FrontCounterClockwise = desc.raster.frontFace == FrontFace::CounterClockwise;
        rs.DepthBias = static_cast<INT>(desc.raster.depthBiasConstant);
        rs.DepthBiasClamp = desc.raster.depthBiasClamp;
        rs.SlopeScaledDepthBias = desc.raster.depthBiasSlope;
        rs.DepthClipEnable = desc.raster.depthClip;
        rs.MultisampleEnable = desc.samples > 1;

        D3D12_DEPTH_STENCIL_DESC& ds = pd.DepthStencilState;
        ds.DepthEnable = desc.depthStencil.depthTest;
        ds.DepthWriteMask =
            desc.depthStencil.depthWrite ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
        ds.DepthFunc = ToD3D(desc.depthStencil.depthCompare);
        ds.StencilEnable = desc.depthStencil.stencilTest;
        ds.StencilReadMask = desc.depthStencil.stencilReadMask;
        ds.StencilWriteMask = desc.depthStencil.stencilWriteMask;
        auto face = [](const StencilFace& f) {
            return D3D12_DEPTH_STENCILOP_DESC{ToD3D(f.fail), ToD3D(f.depthFail), ToD3D(f.pass),
                                              ToD3D(f.compare)};
        };
        ds.FrontFace = face(desc.depthStencil.front);
        ds.BackFace = face(desc.depthStencil.back);

        D3D12_INPUT_ELEMENT_DESC elements[kMaxVertexAttributes];
        for (uint32_t i = 0; i < desc.attributeCount; i++) {
            const VertexAttributeDesc& a = desc.attributes[i];
            const bool instance =
                a.stream < desc.streamCount && desc.streams[a.stream].step == VertexStep::Instance;
            elements[i] = {"TEXCOORD",
                           a.location,
                           MapFormat(a.format).view,
                           a.stream,
                           a.offset,
                           instance ? D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA
                                    : D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
                           instance ? 1u : 0u};
        }
        pd.InputLayout = {elements, desc.attributeCount};
        pd.PrimitiveTopologyType = ToD3DType(desc.topology);
        pd.NumRenderTargets = desc.colorCount;
        for (uint32_t i = 0; i < desc.colorCount; i++)
            pd.RTVFormats[i] = MapFormat(desc.colorFormats[i]).view;
        if (desc.depthFormat != Format::Unknown)
            pd.DSVFormat = MapFormat(desc.depthFormat).dsv;
        pd.SampleDesc = {(std::max)(1u, desc.samples), 0};

        // key for the on-disk library: everything that changes the compiled result, nothing pointer-valued
        uint64_t key = Fnv(vs->code.data(), vs->code.size());
        if (fs)
            key = Fnv(fs->code.data(), fs->code.size(), key);
        key = Fnv(&pd.BlendState, sizeof(pd.BlendState), key);
        key = Fnv(&pd.RasterizerState, sizeof(pd.RasterizerState), key);
        key = Fnv(&pd.DepthStencilState, sizeof(pd.DepthStencilState), key);
        for (uint32_t i = 0; i < desc.attributeCount; i++) {
            const uint32_t v[] = {elements[i].SemanticIndex, static_cast<uint32_t>(elements[i].Format),
                                  elements[i].InputSlot, elements[i].AlignedByteOffset,
                                  static_cast<uint32_t>(elements[i].InputSlotClass)};
            key = Fnv(v, sizeof(v), key);
        }
        key = Fnv(pd.RTVFormats, sizeof(pd.RTVFormats), key);
        const uint32_t tail[] = {static_cast<uint32_t>(pd.DSVFormat),
                                 static_cast<uint32_t>(pd.PrimitiveTopologyType), pd.SampleDesc.Count,
                                 pd.NumRenderTargets};
        key = Fnv(tail, sizeof(tail), key);
        key = Fnv(&layout->hash, sizeof(layout->hash), key);
        const std::wstring name = HashName("g", key);

        PipelineRec rec;
        HRESULT hr = E_FAIL;
        if (pipelineLibrary) {
            std::lock_guard lock(libraryMutex);
            hr = pipelineLibrary->LoadGraphicsPipeline(
                name.c_str(), &pd, winrt::guid_of<ID3D12PipelineState>(), rec.pso.put_void());
        }
        if (FAILED(hr)) {
            hr = device->CreateGraphicsPipelineState(&pd, winrt::guid_of<ID3D12PipelineState>(),
                                                     rec.pso.put_void());
            if (SUCCEEDED(hr) && pipelineLibrary) {
                std::lock_guard lock(libraryMutex);
                pipelineLibrary->StorePipeline(name.c_str(), rec.pso.get());
            }
        }
        if (FAILED(hr)) {
            {
                char code[32];
                std::snprintf(code, sizeof(code), "0x%08X", static_cast<unsigned>(hr));
                ReportError("CreateGraphicsPipelineState failed (" + std::string(code) + ") for '" +
                            std::string(desc.name ? desc.name : "") +
                            "' rt=" + std::to_string(desc.colorCount) +
                            " attrs=" + std::to_string(desc.attributeCount));
            }
            return {};
        }
        if (desc.name)
            rec.pso->SetName(Widen(desc.name).c_str());
        rec.rootSignature = layout->rootSignature;
        rec.layoutHandle = desc.layout;
        rec.topology = ToD3D(desc.topology);
        for (uint32_t i = 0; i < desc.streamCount; i++)
            rec.strides[i] = desc.streams[i].stride;
        rec.streamCount = desc.streamCount;
        rec.colorCount = desc.colorCount;
        rec.colorFormats = desc.colorFormats;
        rec.depthFormat = desc.depthFormat;
        rec.pushBytes = layout->pushBytes;
        livePipelines++;
        return pipelines.Alloc(std::move(rec));
    }

    PipelineHandle Device::CreateComputePipeline(const ComputePipelineDesc& desc) {
        PipelineLayoutRec* layout = pipelineLayouts.Get(desc.layout);
        ShaderRec* cs = shaders.Get(desc.compute);
        if (!layout || !cs) {
            ReportError("CreateComputePipeline: missing layout or shader");
            return {};
        }
        D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};
        pd.pRootSignature = layout->rootSignature.get();
        pd.CS = {cs->code.data(), cs->code.size()};
        uint64_t key = Fnv(cs->code.data(), cs->code.size());
        key = Fnv(&layout->hash, sizeof(layout->hash), key);
        const std::wstring name = HashName("c", key);

        PipelineRec rec;
        HRESULT hr = E_FAIL;
        if (pipelineLibrary) {
            std::lock_guard lock(libraryMutex);
            hr = pipelineLibrary->LoadComputePipeline(
                name.c_str(), &pd, winrt::guid_of<ID3D12PipelineState>(), rec.pso.put_void());
        }
        if (FAILED(hr)) {
            hr = device->CreateComputePipelineState(&pd, winrt::guid_of<ID3D12PipelineState>(),
                                                    rec.pso.put_void());
            if (SUCCEEDED(hr) && pipelineLibrary) {
                std::lock_guard lock(libraryMutex);
                pipelineLibrary->StorePipeline(name.c_str(), rec.pso.get());
            }
        }
        if (FAILED(hr)) {
            ReportError("CreateComputePipelineState failed");
            return {};
        }
        if (desc.name)
            rec.pso->SetName(Widen(desc.name).c_str());
        rec.compute = true;
        rec.rootSignature = layout->rootSignature;
        rec.layoutHandle = desc.layout;
        rec.pushBytes = layout->pushBytes;
        livePipelines++;
        return pipelines.Alloc(std::move(rec));
    }

    QuerySetHandle Device::CreateQuerySet(const QuerySetDesc& desc) {
        D3D12_QUERY_HEAP_DESC qd{D3D12_QUERY_HEAP_TYPE_TIMESTAMP, desc.count, 0};
        QueryRec rec;
        rec.count = desc.count;
        if (FAILED(device->CreateQueryHeap(&qd, winrt::guid_of<ID3D12QueryHeap>(), rec.heap.put_void()))) {
            ReportError("CreateQueryHeap failed");
            return {};
        }
        return queries.Alloc(std::move(rec));
    }

    ID3D12CommandSignature* Device::Signature(bool dispatch, uint32_t stride) {
        const uint64_t key = (dispatch ? 1ull << 32 : 0) | stride;
        std::lock_guard lock(signatureMutex);
        auto it = signatures.find(key);
        if (it != signatures.end())
            return it->second.get();
        D3D12_INDIRECT_ARGUMENT_DESC arg{};
        arg.Type =
            dispatch ? D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH : D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;
        D3D12_COMMAND_SIGNATURE_DESC sd{};
        sd.ByteStride = dispatch ? sizeof(D3D12_DISPATCH_ARGUMENTS) : stride;
        sd.NumArgumentDescs = 1;
        sd.pArgumentDescs = &arg;
        winrt::com_ptr<ID3D12CommandSignature> sig;
        if (FAILED(device->CreateCommandSignature(&sd, nullptr, winrt::guid_of<ID3D12CommandSignature>(),
                                                  sig.put_void())))
            return nullptr;
        auto* raw = sig.get();
        signatures.emplace(key, std::move(sig));
        return raw;
    }

    void Device::Retire(std::function<void()> release) {
        QueueSerials snapshot;
        for (size_t i = 0; i < kQueueCount; i++)
            snapshot[i] = queues[i].serial.load();
        retire.Push(snapshot, std::move(release));
    }

    void Device::Destroy(BufferHandle h) {
        BufferRec rec;
        if (!buffers.Free(h, &rec))
            return;
        bufferBytes -= static_cast<int64_t>(rec.desc.size);
        liveBuffers--;
        if (useValidator)
            validator.OnDestroy(h);
        Retire([rec = std::move(rec)]() mutable { rec = BufferRec{}; });
    }

    void Device::Destroy(TextureHandle h) {
        TextureRec rec;
        if (!textures.Free(h, &rec))
            return;
        liveTextures--;
        if (useValidator)
            validator.OnDestroy(h);
        Retire([this, rec = std::move(rec)]() mutable {
            for (auto& [k, v] : rec.views.rtv)
                rtvPool.Free(v);
            for (auto& [k, v] : rec.views.dsv)
                dsvPool.Free(v);
            rec = TextureRec{};
        });
    }

    // samplers are cached and shared by description: they live until the device does
    void Device::Destroy(SamplerHandle) {}
    void Device::Destroy(ShaderHandle h) {
        shaders.Free(h);
    }
    void Device::Destroy(BindLayoutHandle h) {
        bindLayouts.Free(h);
    }

    void Device::Destroy(BindGroupHandle h) {
        BindGroupRec* g = bindGroups.Get(h);
        if (!g || g->perFrame)
            return;
        BindGroupRec rec;
        bindGroups.Free(h, &rec);
        liveGroups--;
        Retire([this, rec] {
            resourceHeap.Free(rec.tableOffset, rec.tableCount);
            ReleaseSamplerTable(rec.samplerTable);
        });
    }

    void Device::Destroy(PipelineLayoutHandle h) {
        PipelineLayoutRec rec;
        if (!pipelineLayouts.Free(h, &rec))
            return;
        Retire([rec = std::move(rec)]() mutable { rec = PipelineLayoutRec{}; });
    }

    void Device::Destroy(PipelineHandle h) {
        PipelineRec rec;
        if (!pipelines.Free(h, &rec))
            return;
        livePipelines--;
        Retire([rec = std::move(rec)]() mutable { rec = PipelineRec{}; });
    }

    void Device::Destroy(QuerySetHandle h) {
        QueryRec rec;
        if (!queries.Free(h, &rec))
            return;
        Retire([rec = std::move(rec)]() mutable { rec = QueryRec{}; });
    }

    void Device::Destroy(FenceHandle h) {
        FenceRec rec;
        if (!fences.Free(h, &rec))
            return;
        Retire([rec = std::move(rec)]() mutable { rec = FenceRec{}; });
    }

    const BufferDesc* Device::GetDesc(BufferHandle h) const {
        BufferRec* r = buffers.Get(h);
        return r ? &r->desc : nullptr;
    }

    const TextureDesc* Device::GetDesc(TextureHandle h) const {
        TextureRec* r = textures.Get(h);
        return r ? &r->desc : nullptr;
    }

    uint8_t* Device::Map(BufferHandle h) {
        BufferRec* r = buffers.Get(h);
        return r ? r->mapped : nullptr;
    }

    uint64_t Device::GpuAddress(BufferHandle h) const {
        BufferRec* r = buffers.Get(h);
        return r ? r->address : 0;
    }

    FenceHandle Device::CreateFence(uint64_t initial) {
        FenceRec rec;
        if (FAILED(device->CreateFence(initial, D3D12_FENCE_FLAG_SHARED, winrt::guid_of<ID3D12Fence>(),
                                       rec.fence.put_void())) &&
            FAILED(device->CreateFence(initial, D3D12_FENCE_FLAG_NONE, winrt::guid_of<ID3D12Fence>(),
                                       rec.fence.put_void())))
            return {};
        return fences.Alloc(std::move(rec));
    }

    uint64_t Device::FenceValue(FenceHandle h) {
        FenceRec* f = fences.Get(h);
        return f ? f->fence->GetCompletedValue() : 0;
    }

    bool Device::WaitFence(FenceHandle h, uint64_t value, uint64_t timeoutNs) {
        FenceRec* f = fences.Get(h);
        if (!f)
            return false;
        if (f->fence->GetCompletedValue() >= value)
            return true;
        std::lock_guard lock(waitMutex);
        if (FAILED(f->fence->SetEventOnCompletion(value, waitEvent.get())))
            return false;
        const DWORD ms =
            timeoutNs == ~0ull
                ? INFINITE
                : static_cast<DWORD>(std::min<uint64_t>(timeoutNs / 1'000'000ull, 0xFFFFFFFEull));
        return WaitForSingleObject(waitEvent.get(), ms) == WAIT_OBJECT_0;
    }

    void Device::SignalFence(FenceHandle h, uint64_t value) {
        if (FenceRec* f = fences.Get(h))
            f->fence->Signal(value);
    }

    QueueSerials Device::Completed() {
        QueueSerials out{};
        for (size_t i = 0; i < kQueueCount; i++) {
            if (!queues[i].fence) {
                // async compute off: that queue never signals, treat it as caught up
                out[i] = ~0ull;
                continue;
            }
            out[i] = queues[i].fence->GetCompletedValue();
        }
        return out;
    }

    ThreadPools& Device::PoolsForThisThread() {
        std::lock_guard lock(poolsMutex);
        auto& entry = threadPools[std::this_thread::get_id()];
        if (!entry) {
            entry = std::make_unique<ThreadPools>();
            entry->slots.resize(options.framesInFlight);
        }
        return *entry;
    }

    CommandList* Device::AcquireCommandList(QueueType requested) {
        // without a separate compute queue the work runs on the direct queue
        const QueueType queue =
            (requested == QueueType::Compute && !queues[1].queue) ? QueueType::Graphics : requested;
        ThreadPools& tp = PoolsForThisThread();
        auto& per = tp.slots[slot][static_cast<size_t>(queue)];
        if (per.used == per.lists.size()) {
            const D3D12_COMMAND_LIST_TYPE types[kQueueCount] = {D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                                D3D12_COMMAND_LIST_TYPE_COMPUTE,
                                                                D3D12_COMMAND_LIST_TYPE_COPY};
            winrt::com_ptr<ID3D12CommandAllocator> alloc;
            winrt::com_ptr<ID3D12GraphicsCommandList> list;
            if (FAILED(device->CreateCommandAllocator(types[static_cast<size_t>(queue)],
                                                      winrt::guid_of<ID3D12CommandAllocator>(),
                                                      alloc.put_void())) ||
                FAILED(device->CreateCommandList(0, types[static_cast<size_t>(queue)], alloc.get(), nullptr,
                                                 winrt::guid_of<ID3D12GraphicsCommandList>(),
                                                 list.put_void()))) {
                ReportError("command list creation failed");
                return nullptr;
            }
            // created open; Begin() resets it, so close it first
            list->Close();
            per.lists.push_back(std::make_unique<CmdList>(*this, queue, std::move(alloc), std::move(list)));
        }
        CmdList* list = per.lists[per.used++].get();
        list->Begin();
        return list;
    }

    void Device::Submit(QueueType requested, const SubmitDesc& desc) {
        const QueueType queueType =
            (requested == QueueType::Compute && !queues[1].queue) ? QueueType::Graphics : requested;
        QueueInfo& q = queues[static_cast<size_t>(queueType)];
        std::vector<ID3D12CommandList*> native;
        native.reserve(desc.lists.size());
        for (CommandList* l : desc.lists) {
            auto* list = static_cast<CmdList*>(l);
            if (list->openLabels != 0)
                ReportError("command list submitted with an open debug label");
            list->End();
            native.push_back(list->Native());
        }
        std::lock_guard lock(q.mutex);
        for (const FenceOp& w : desc.waits)
            if (FenceRec* f = fences.Get(w.fence))
                q.queue->Wait(f->fence.get(), w.value);
        if (!native.empty())
            q.queue->ExecuteCommandLists(static_cast<UINT>(native.size()), native.data());
        for (const FenceOp& s : desc.signals)
            if (FenceRec* f = fences.Get(s.fence))
                q.queue->Signal(f->fence.get(), s.value);
        const uint64_t serial = q.serial.load() + 1;
        q.queue->Signal(q.fence.get(), serial);
        q.serial.store(serial);
        submitCount++;
    }

    void Device::SignalSerial(QueueType) {}

    std::unique_ptr<rhi::Swapchain> Device::CreateSwapchain(const SwapchainDesc& desc) {
        return MakeSwapchain(*this, desc);
    }

    void Device::ResetSlot(uint32_t s) {
        {
            std::lock_guard lock(poolsMutex);
            for (auto& [id, tp] : threadPools)
                for (auto& per : tp->slots[s]) {
                    for (size_t i = 0; i < per.used; i++)
                        per.lists[i]->Allocator()->Reset();
                    per.used = 0;
                }
        }
        std::vector<BindGroupHandle> groups;
        {
            std::lock_guard lock(frameGroupMutex);
            groups.swap(frameGroups[s].groups);
        }
        for (BindGroupHandle g : groups) {
            BindGroupRec rec;
            if (bindGroups.Free(g, &rec)) {
                liveGroups--;
                resourceHeap.Free(rec.tableOffset, rec.tableCount);
                ReleaseSamplerTable(rec.samplerTable);
            }
        }
    }

    void Device::BeginFrame() {
        frame++;
        slot = static_cast<uint32_t>(frame % options.framesInFlight);
        const QueueSerials& need = slotSerials[slot];
        for (size_t i = 0; i < kQueueCount; i++) {
            if (!need[i] || !queues[i].fence)
                continue;
            if (queues[i].fence->GetCompletedValue() >= need[i])
                continue;
            std::lock_guard lock(waitMutex);
            queues[i].fence->SetEventOnCompletion(need[i], waitEvent.get());
            // never reuse a slot whose frame is still running: wait it out unless the device is gone
            DWORD w = WaitForSingleObject(waitEvent.get(), 10'000);
            while (w == WAIT_TIMEOUT && device && device->GetDeviceRemovedReason() == S_OK &&
                   queues[i].fence->GetCompletedValue() < need[i])
                w = WaitForSingleObject(waitEvent.get(), 10'000);
            if (w != WAIT_OBJECT_0 && queues[i].fence->GetCompletedValue() < need[i])
                ReportError("frame wait did not complete");
        }
        ResetSlot(slot);
        retire.Collect(Completed());
    }

    void Device::EndFrame() {
        for (size_t i = 0; i < kQueueCount; i++)
            slotSerials[slot][i] = queues[i].serial.load();
        retire.Collect(Completed());
    }

    void Device::WaitIdle() {
        for (size_t i = 0; i < kQueueCount; i++) {
            if (!queues[i].fence)
                continue;
            const uint64_t target = queues[i].serial.load();
            if (queues[i].fence->GetCompletedValue() >= target)
                continue;
            std::lock_guard lock(waitMutex);
            queues[i].fence->SetEventOnCompletion(target, waitEvent.get());
            WaitForSingleObject(waitEvent.get(), INFINITE);
        }
        retire.Collect(Completed());
    }

    DeviceStats Device::Stats() const {
        DeviceStats s;
        s.draws = drawCount;
        s.dispatches = dispatchCount;
        s.indirectDraws = indirectCount;
        s.barriers = barrierCount;
        s.pipelineBinds = pipelineBindCount;
        s.bindGroupBinds = groupBindCount;
        s.submits = submitCount;
        s.bufferBytes = static_cast<uint64_t>(std::max<int64_t>(0, bufferBytes));
        s.textureBytes = static_cast<uint64_t>(std::max<int64_t>(0, textureBytes));
        s.liveBuffers = static_cast<uint32_t>(liveBuffers.load());
        s.liveTextures = static_cast<uint32_t>(liveTextures.load());
        s.livePipelines = static_cast<uint32_t>(livePipelines.load());
        s.liveBindGroups = static_cast<uint32_t>(liveGroups.load());
        return s;
    }

    std::vector<uint8_t> Device::SerializePipelineCache() {
        if (!pipelineLibrary)
            return {};
        std::lock_guard lock(libraryMutex);
        const SIZE_T size = pipelineLibrary->GetSerializedSize();
        if (!size)
            return {};
        std::vector<uint8_t> data(size);
        if (FAILED(pipelineLibrary->Serialize(data.data(), size)))
            return {};
        return data;
    }
}
