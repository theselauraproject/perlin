#include "client/PerlinModsGpu.hpp"

#include "client/mods/ModManager.hpp"
#include "client/renderer/gpu/ModGpuHub.hpp"

#include <algorithm>
#include <chrono>
#include <set>

namespace perlin {
    namespace {
        using abi::Status;
        using gpu::HubStatus;
        using gpu::ModGpuHub;
        using mods::CallContext;

        Status ToStatus(HubStatus s) {
            switch (s) {
            case HubStatus::Ok:
                return Status::Ok;
            case HubStatus::BadArguments:
                return Status::BadArguments;
            case HubStatus::Unavailable:
                return Status::Unavailable;
            case HubStatus::NotFound:
                return Status::NotFound;
            case HubStatus::Limit:
                return Status::Failed;
            }
            return Status::Failed;
        }

        uint8_t B(auto v) {
            return static_cast<uint8_t>(v);
        }

        gpu::ModBlendDesc Blend(const abi::GpuBlendDesc& b) {
            gpu::ModBlendDesc o;
            o.enabled = b.enabled;
            o.srcColor = B(b.srcColor);
            o.dstColor = B(b.dstColor);
            o.colorOp = B(b.colorOp);
            o.srcAlpha = B(b.srcAlpha);
            o.dstAlpha = B(b.dstAlpha);
            o.alphaOp = B(b.alphaOp);
            o.writeMask = b.writeMask;
            return o;
        }

        std::set<std::string>& Owners() {
            static std::set<std::string> owners;
            return owners;
        }
    }

    void RegisterModGpuOps(mods::ModManager& mm) {
        using abi::Op;
        ModGpuHub& hub = ModGpuHub::Instance();

        mm.Register(Op::GpuInfo, [](CallContext&, abi::Reader&, abi::Writer& w) {
            const gpu::ModDeviceInfo i = ModGpuHub::Instance().Info();
            abi::GpuInfoData d;
            d.available = i.available;
            d.api = i.api;
            d.adapter = i.adapter;
            d.vendor = i.vendor;
            d.discrete = i.discrete;
            d.targetFormat = static_cast<abi::GpuFormat>(i.targetFormat);
            d.width = i.width;
            d.height = i.height;
            d.maxTexture2d = i.maxTexture2d;
            d.maxTextureLayers = i.maxTextureLayers;
            d.maxMsaaSamples = i.maxMsaa;
            d.maxBufferBytes = i.maxBufferBytes;
            d.maxColorTargets = i.maxColorTargets;
            d.compute = i.compute;
            d.multiDrawIndirect = i.multiDrawIndirect;
            d.float16 = i.float16;
            d.int64 = i.int64;
            d.subgroups = i.subgroups;
            d.sceneCopy = i.sceneCopy;
            d.shaderTarget = i.shaderTarget;
            w.write(d);
            return Status::Ok;
        });

        mm.Register(Op::GpuPackLoad, [](CallContext& ctx, abi::Reader& r, abi::Writer& w) {
            abi::GpuPackLoadRequest q = r.read<abi::GpuPackLoadRequest>();
            if (!r.Ok())
                return Status::BadArguments;
            gpu::ModPackSummary s;
            const HubStatus st = ModGpuHub::Instance().LoadPack(ctx.plugin, q.data, s);
            if (st != HubStatus::Ok)
                return ToStatus(st);
            Owners().insert(ctx.plugin);
            abi::GpuPackInfo o;
            o.pack = s.id;
            for (const gpu::ModProgram& p : s.programs) {
                abi::GpuProgramInfo pi;
                pi.name = p.name;
                pi.uniformBytes = p.uniformBytes;
                for (const gpu::ModUniform& u : p.uniforms)
                    pi.uniforms.push_back({u.name, u.offset, u.bytes, u.elements});
                pi.vertex = p.vertex;
                pi.fragment = p.fragment;
                pi.compute = p.compute;
                o.programs.push_back(std::move(pi));
            }
            for (const gpu::ModSlot& sl : s.slots)
                o.slots.push_back({sl.name, sl.slot, sl.array, sl.unfilterable});
            w.write(o);
            return Status::Ok;
        });

        mm.Register(Op::GpuBufferCreate, [](CallContext& ctx, abi::Reader& r, abi::Writer& w) {
            abi::GpuBufferRequest q = r.read<abi::GpuBufferRequest>();
            if (!r.Ok())
                return Status::BadArguments;
            gpu::ModBufferDesc d;
            d.size = q.size;
            d.usage = q.usage.bits();
            d.data = std::move(q.data);
            d.name = std::move(q.name);
            uint32_t id = 0;
            const HubStatus st = ModGpuHub::Instance().CreateBuffer(ctx.plugin, std::move(d), id);
            if (st == HubStatus::Ok) {
                Owners().insert(ctx.plugin);
                w.put<uint32_t>(id);
            }
            return ToStatus(st);
        });

        mm.Register(Op::GpuBufferWrite, [](CallContext& ctx, abi::Reader& r, abi::Writer&) {
            abi::GpuBufferWriteRequest q = r.read<abi::GpuBufferWriteRequest>();
            if (!r.Ok())
                return Status::BadArguments;
            return ToStatus(ModGpuHub::Instance().WriteBuffer(ctx.plugin, q.id, q.offset, std::move(q.data)));
        });

        mm.Register(Op::GpuBufferRead, [](CallContext& ctx, abi::Reader& r, abi::Writer&) {
            const abi::GpuBufferReadRequest q = r.read<abi::GpuBufferReadRequest>();
            if (!r.Ok())
                return Status::BadArguments;
            return ToStatus(ModGpuHub::Instance().ReadBuffer(ctx.plugin, q.id, q.offset, q.size, q.tag));
        });

        mm.Register(Op::GpuTextureCreate, [](CallContext& ctx, abi::Reader& r, abi::Writer& w) {
            abi::GpuTextureRequest q = r.read<abi::GpuTextureRequest>();
            if (!r.Ok())
                return Status::BadArguments;
            gpu::ModTextureDesc d;
            d.width = q.width;
            d.height = q.height;
            d.layers = q.layers;
            d.mips = q.mips;
            d.samples = q.samples;
            d.format = B(q.format);
            d.usage = q.usage.bits();
            d.dim = B(q.dim);
            d.screenSized = q.screenSized;
            d.scale = q.scale;
            d.data = std::move(q.data);
            d.name = std::move(q.name);
            uint32_t id = 0;
            const HubStatus st = ModGpuHub::Instance().CreateTexture(ctx.plugin, std::move(d), id);
            if (st == HubStatus::Ok) {
                Owners().insert(ctx.plugin);
                w.put<uint32_t>(id);
            }
            return ToStatus(st);
        });

        mm.Register(Op::GpuTextureWrite, [](CallContext& ctx, abi::Reader& r, abi::Writer&) {
            abi::GpuTextureWriteRequest q = r.read<abi::GpuTextureWriteRequest>();
            if (!r.Ok())
                return Status::BadArguments;
            gpu::ModTextureWrite t;
            t.layer = q.layer;
            t.mip = q.mip;
            t.x = q.x;
            t.y = q.y;
            t.width = q.width;
            t.height = q.height;
            t.data = std::move(q.data);
            return ToStatus(ModGpuHub::Instance().WriteTexture(ctx.plugin, q.id, std::move(t)));
        });

        mm.Register(Op::GpuSamplerCreate, [](CallContext& ctx, abi::Reader& r, abi::Writer& w) {
            const abi::GpuSamplerRequest q = r.read<abi::GpuSamplerRequest>();
            if (!r.Ok())
                return Status::BadArguments;
            gpu::ModSamplerDesc d;
            d.min = B(q.min);
            d.mag = B(q.mag);
            d.mip = B(q.mip);
            d.u = B(q.u);
            d.v = B(q.v);
            d.w = B(q.w);
            d.anisotropy = q.anisotropy;
            d.lodBias = q.lodBias;
            d.minLod = q.minLod;
            d.maxLod = q.maxLod;
            d.compare = q.compare;
            d.compareOp = B(q.compareOp);
            d.border = q.border;
            uint32_t id = 0;
            const HubStatus st = ModGpuHub::Instance().CreateSampler(ctx.plugin, d, id);
            if (st == HubStatus::Ok) {
                Owners().insert(ctx.plugin);
                w.put<uint32_t>(id);
            }
            return ToStatus(st);
        });

        mm.Register(Op::GpuPipelineCreate, [](CallContext& ctx, abi::Reader& r, abi::Writer& w) {
            abi::GpuPipelineRequest q = r.read<abi::GpuPipelineRequest>();
            if (!r.Ok())
                return Status::BadArguments;
            gpu::ModPipelineDesc d;
            d.pack = q.pack;
            d.program = std::move(q.program);
            for (const abi::GpuStreamDesc& s : q.streams)
                d.streams.push_back({s.stride, s.instance});
            for (const abi::GpuAttributeDesc& a : q.attributes)
                d.attributes.push_back({a.location, a.stream, B(a.format), a.offset});
            d.topology = B(q.topology);
            d.cull = B(q.cull);
            d.frontFace = B(q.frontFace);
            d.depthBiasConstant = q.depthBiasConstant;
            d.depthBiasSlope = q.depthBiasSlope;
            d.depthTest = q.depthTest;
            d.depthWrite = q.depthWrite;
            d.depthCompare = B(q.depthCompare);
            for (const abi::GpuBlendDesc& b : q.blend)
                d.blend.push_back(Blend(b));
            for (abi::GpuFormat f : q.colorFormats)
                d.colorFormats.push_back(B(f));
            d.depthFormat = B(q.depthFormat);
            d.samples = q.samples;
            for (const abi::GpuStorageBinding& s : q.storage)
                d.storage.push_back({s.binding, B(s.kind)});
            d.name = std::move(q.name);
            uint32_t id = 0;
            const HubStatus st = ModGpuHub::Instance().CreatePipeline(ctx.plugin, std::move(d), id);
            if (st == HubStatus::Ok) {
                Owners().insert(ctx.plugin);
                w.put<uint32_t>(id);
            }
            return ToStatus(st);
        });

        mm.Register(Op::GpuComputePipelineCreate, [](CallContext& ctx, abi::Reader& r, abi::Writer& w) {
            abi::GpuComputePipelineRequest q = r.read<abi::GpuComputePipelineRequest>();
            if (!r.Ok())
                return Status::BadArguments;
            gpu::ModPipelineDesc d;
            d.compute = true;
            d.pack = q.pack;
            d.program = std::move(q.program);
            for (const abi::GpuStorageBinding& s : q.storage)
                d.storage.push_back({s.binding, B(s.kind)});
            d.name = std::move(q.name);
            uint32_t id = 0;
            const HubStatus st = ModGpuHub::Instance().CreatePipeline(ctx.plugin, std::move(d), id);
            if (st == HubStatus::Ok) {
                Owners().insert(ctx.plugin);
                w.put<uint32_t>(id);
            }
            return ToStatus(st);
        });

        mm.Register(Op::GpuDestroy, [](CallContext& ctx, abi::Reader& r, abi::Writer&) {
            const abi::GpuDestroyRequest q = r.read<abi::GpuDestroyRequest>();
            if (!r.Ok())
                return Status::BadArguments;
            return ToStatus(ModGpuHub::Instance().Destroy(ctx.plugin, static_cast<gpu::ModResource>(B(q.kind)), q.id));
        });

        mm.Register(Op::GpuSubmit, [](CallContext& ctx, abi::Reader& r, abi::Writer&) {
            abi::GpuSubmitRequest q = r.read<abi::GpuSubmitRequest>();
            if (!r.Ok())
                return Status::BadArguments;
            const HubStatus st = ModGpuHub::Instance().Submit(ctx.plugin, q.layer, static_cast<gpu::ModStage>(B(q.stage)),
                                                              q.z, q.once, std::move(q.stream));
            if (st == HubStatus::Ok)
                Owners().insert(ctx.plugin);
            return ToStatus(st);
        });

        mm.Register(Op::GpuLayerClear, [](CallContext& ctx, abi::Reader& r, abi::Writer&) {
            const abi::GpuLayerClearRequest q = r.read<abi::GpuLayerClearRequest>();
            if (!r.Ok())
                return Status::BadArguments;
            return ToStatus(ModGpuHub::Instance().ClearLayer(ctx.plugin, q.layer));
        });

        mm.Register(Op::GpuErrors, [](CallContext& ctx, abi::Reader&, abi::Writer& w) {
            abi::GpuErrorsResponse o;
            o.messages = ModGpuHub::Instance().TakeErrors(ctx.plugin);
            w.write(o);
            return Status::Ok;
        });
        (void)hub;
    }

    void PumpModGpuEvents(mods::ModManager& mm) {
        ModGpuHub& hub = ModGpuHub::Instance();
        std::vector<gpu::ModReadback> reads;
        hub.TakeReadbacks(reads);
        for (gpu::ModReadback& rb : reads) {
            abi::Writer w;
            abi::EventGpuReadback e;
            e.tag = rb.tag;
            e.id = rb.id;
            e.ok = rb.ok;
            e.data = std::move(rb.data);
            w.write(e);
            mm.DispatchTo(rb.plugin, abi::Event::GpuReadback, w);
        }
        std::vector<std::pair<std::string, uint32_t>> resets;
        hub.TakeResets(resets);
        for (const auto& [plugin, generation] : resets) {
            abi::Writer w;
            w.write(abi::EventGpuReset{generation});
            mm.DispatchTo(plugin, abi::Event::GpuReset, w);
        }
        // Forget plugins that no longer run so their GPU resources are released (once a second is plenty).
        static auto nextSweep = std::chrono::steady_clock::now();
        const auto now = std::chrono::steady_clock::now();
        if (now < nextSweep)
            return;
        nextSweep = now + std::chrono::seconds(1);
        std::set<std::string> running;
        for (const mods::ModInfo& info : mm.List())
            if (info.state == "running")
                running.insert(info.name);
        for (const std::string& owner : hub.Plugins())
            if (!running.contains(owner))
                hub.DropPlugin(owner);
    }

    void DropModGpu(const std::string& plugin) {
        ModGpuHub::Instance().DropPlugin(plugin);
        Owners().erase(plugin);
    }
}
