#pragma once
#include <conduit/device.hpp>
#include <conduit/shader_library.hpp>
#include "client/renderer/RangeAllocator.hpp"

#include <array>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace perlin::gpu {
    // UiInstanced: the ui vertex plus one instance per draw (UiInstance) carrying its per-draw values
    enum class VertexLayout : uint8_t { Terrain, Position, Color, Texture, Ui, UiInstanced, Count };

    struct VertexLayoutInfo {
        uint32_t stride;
        std::array<conduit::vertex_attribute_desc, 16> attributes;
        uint32_t attributeCount;
        uint32_t instanceStride = 0;
    };

    // one per ui, entity or particle draw, read as instance attributes 8..15 (see vs_ui in
    // gen_renderdragon_shaders.py); the bounds are only for the gpu culling pass
    struct UiInstance {
        float modelRows[4][4];
        float params[4];
        float overlay[4];
        float glint[4];
        float material[4];
        float bounds[4];
        float pad[4];
    };
    static_assert(sizeof(UiInstance) == 160, "UiInstance must stay a multiple of 16 bytes");
    const VertexLayoutInfo& LayoutInfo(VertexLayout layout);

    struct PipelineKey {
        uint16_t program = 0;
        VertexLayout layout = VertexLayout::Terrain;
        conduit::cull_mode cull = conduit::cull_mode::none;
        conduit::front_face front = conduit::front_face::clockwise;
        bool depthTest = false;
        bool depthWrite = false;
        conduit::compare_op depthOp = conduit::compare_op::less_equal;
        bool blend = false;
        conduit::blend_factor srcColor = conduit::blend_factor::one, dstColor = conduit::blend_factor::zero;
        conduit::blend_factor srcAlpha = conduit::blend_factor::one, dstAlpha = conduit::blend_factor::zero;
        conduit::color_mask mask = conduit::color_mask::all;

        conduit::blend_op alphaOp = conduit::blend_op::add;
        bool alphaToCoverage = false;
        conduit::format color = conduit::format::rgba8_unorm;
        conduit::format depth = conduit::format::d32_float;

        uint8_t colorCount = 1;
        conduit::format color1 = conduit::format::rgba8_unorm;
        conduit::format color2 = conduit::format::rgba8_unorm;

        int16_t biasConstant = 0;
        int16_t biasSlope = 0;

        std::array<uint64_t, 4> Pack() const;
        bool operator==(const PipelineKey& o) const { return Pack() == o.Pack(); }
    };

    struct PipelineKeyHash {
        size_t operator()(const PipelineKey& k) const;
    };

    enum class SamplerKind : uint8_t {
        PointClamp,
        PointRepeat,
        LinearClamp,
        LinearRepeat,
        LinearMipRepeat,
        PointMipClamp,
        LinearMipClamp,
        Count
    };

    constexpr uint32_t kTextureSlots = 48;

    struct TextureSet {
        std::array<conduit::texture_handle, kTextureSlots> texture{};
        std::array<SamplerKind, kTextureSlots> sampler{};
        bool operator==(const TextureSet& o) const { return texture == o.texture && sampler == o.sampler; }
    };

    struct TextureSetHash {
        size_t operator()(const TextureSet& s) const;
    };

    struct RingSlice {
        conduit::buffer_handle buffer;
        uint32_t chunk = 0;
        uint64_t offset = 0;
        uint8_t* data = nullptr;
    };

    class Arena {
    public:
        bool Create(conduit::device& device, uint32_t elements, uint32_t elementBytes, conduit::buffer_usage usage,
                    const char* name);
        void Destroy(conduit::device& device);
        bool Alloc(uint32_t count, uint32_t& first) { return ranges.Alloc(count, first); }
        void Free(uint32_t first, uint32_t count) { ranges.Free(first, count); }
        bool Unused() const { return ranges.Unused(); }
        void Reset() { ranges.Reset(ranges.Capacity()); }
        uint32_t Capacity() const { return ranges.Capacity(); }
        uint32_t FreeTotal() const { return ranges.FreeTotal(); }
        conduit::buffer_handle Buffer() const { return buffer; }
        uint32_t ElementBytes() const { return elementBytes; }
        conduit::state BarrierState() const { return state; }
        uint64_t Bytes() const { return static_cast<uint64_t>(ranges.Capacity()) * elementBytes; }

        void Detach() { buffer = {}; }

        bool Rebuild(conduit::device& device);

    private:
        conduit::buffer_usage usage = conduit::buffer_usage::none;
        const char* name = nullptr;
        conduit::buffer_handle buffer;
        RangeAllocator ranges;
        uint32_t elementBytes = 0;
        conduit::state state = conduit::state::vertex_buffer;
    };

    struct GpuSettings {
        conduit::api api = conduit::api::vulkan;
        uint32_t adapterIndex = 0;
        bool validation = false;
        conduit::native_window window;
        uint32_t width = 0, height = 0;
        conduit::present_mode present = conduit::present_mode::fifo;
        uint32_t framesInFlight = 2;
        bool hdr = false;

        bool offscreen = false;
        conduit::vendor_hooks hooks;
        std::span<const uint8_t> pipelineCache;
    };

    class Gpu {
    public:
        ~Gpu();

        bool Init(const GpuSettings& settings, std::span<const uint8_t> shaderPack, std::string* error);
        void Shutdown();

        conduit::device& Dev() { return *device; }
        const conduit::device_caps& Caps() const { return device->caps(); }
        conduit::api Api() const { return settings.api; }
        const GpuSettings& Settings() const { return settings; }
        conduit::swapchain* Chain() { return swapchain.get(); }
        const conduit::shader_library& Library() const { return library; }
        uint32_t Frames() const { return settings.framesInFlight; }

        int Program(std::string_view name) const;
        const conduit::program_info& ProgramInfo(int program) const {
            return library.programs()[static_cast<size_t>(program)];
        }
        bool PipelineReady(const PipelineKey& key);
        conduit::pipeline_handle Pipeline(const PipelineKey& key);

        std::vector<PipelineKey> PipelineKeys() const;

        void ClearPipelines();

        void BeginFrame();

        bool AcquireImage(conduit::swapchain::image& out);
        void PresentImage();

        bool Resize(uint32_t width, uint32_t height);

        bool RecreateSurface(uint32_t width, uint32_t height);
        void SetWindow(const conduit::native_window& window) { settings.window = window; }
        void SetPresentMode(conduit::present_mode mode);
        conduit::format TargetFormat() const { return targetFormat; }
        uint32_t TargetWidth() const { return targetWidth; }

        uint32_t AcquireFailures() const { return acquireFailures; }
        void ClearAcquireFailures() { acquireFailures = 0; }
        uint32_t TargetHeight() const { return targetHeight; }
        conduit::texture_handle OffscreenTexture() const { return offscreen; }

        uint32_t FrameSlot() const { return device->frame_slot(); }

        RingSlice Ring(uint64_t bytes, uint64_t align);
        conduit::bind_group_handle UniformGroup(uint32_t chunk) const { return ringGroups[FrameSlot()][chunk]; }
        conduit::buffer_handle RingBuffer(uint32_t chunk) const { return rings[FrameSlot()][chunk].buffer; }
        uint32_t uniformRange() const { return uniformRangeBytes; }
        // whether frame ring memory can also be bound as read-only shader storage
        bool RingStorage() const { return ringStorage; }

        conduit::bind_group_handle TextureGroup(const TextureSet& set);
        void ForgetTexture(conduit::texture_handle texture);
        conduit::sampler_handle Sampler(SamplerKind kind) const { return samplers[static_cast<size_t>(kind)]; }
        conduit::pipeline_layout_handle Layout() const { return pipelineLayout; }
        conduit::bind_layout_handle UniformLayout() const { return uniformLayout; }
        conduit::texture_handle White2D() const { return white2d; }
        conduit::texture_handle WhiteArray() const { return whiteArray; }

        void WriteBuffer(const Arena& arena, uint64_t byteOffset, const void* data, uint64_t bytes) {
            WriteBuffer(arena.Buffer(), arena.BarrierState(), byteOffset, data, bytes);
        }
        void WriteBuffer(conduit::buffer_handle buffer, conduit::state resting, uint64_t byteOffset, const void* data,
                         uint64_t bytes);

        void ForgetBuffer(conduit::buffer_handle buffer) { bufferStates.erase(buffer.value); }
        conduit::buffer_handle CreateStatic(const void* data, uint64_t bytes, conduit::buffer_usage usage,
                                       const char* name);
        struct ImageWrite {
            conduit::texture_handle texture;
            uint32_t mip = 0, layer = 0, x = 0, y = 0, width = 0, height = 0;
            uint64_t source = 0;
            uint32_t rowBytes = 0;
        };

        void WriteTexture(conduit::texture_handle texture, uint32_t layer, uint32_t x, uint32_t y, uint32_t width,
                          uint32_t height, const void* data, uint32_t rowBytes = 0, uint32_t mip = 0);

        conduit::texture_handle CreateSampled(conduit::texture_desc desc);
        void DestroyTexture(conduit::texture_handle texture);
        void Flush(conduit::command_list& list);

        void FlushRings();
        bool HasPendingWrites() const { return !bufferWrites.empty() || !imageWrites.empty(); }

        conduit::state StateOf(conduit::texture_handle texture) const;
        void Transition(conduit::command_list& list, conduit::texture_handle texture, conduit::state to);
        void SetState(conduit::texture_handle texture, conduit::state state);

        uint64_t FrameNumber() const { return device->frame_number(); }

        struct BufferSource {
            conduit::buffer_handle buffer;
            conduit::state resting = conduit::state::vertex_buffer;
            uint64_t bytes = 0;
        };

        std::vector<std::vector<uint8_t>> ReadBuffers(std::span<const BufferSource> sources);
        // mip 0, layer 0 of rgba8 textures, rows packed; empty for other formats; stalls the gpu
        std::vector<std::vector<uint8_t>> ReadTextures(std::span<const conduit::texture_handle> textures);

    private:
        struct RingChunk {
            conduit::buffer_handle buffer;
            uint8_t* data = nullptr;
            uint64_t size = 0;
            uint64_t used = 0;
            // frame cycles of this slot without use; extra chunks are released after a while
            uint32_t idle = 0;
        };
        struct BufferWrite {
            conduit::buffer_handle buffer;
            conduit::state state;
            uint64_t offset, bytes, source;
        };

        bool CreateRing(uint32_t slot, uint64_t minimum);
        void ReleaseRings();
        void BuildSamplers();
        bool BuildLayouts();

        GpuSettings settings;
        std::unique_ptr<conduit::instance> instance;
        std::unique_ptr<conduit::device> device;
        std::unique_ptr<conduit::swapchain> swapchain;
        conduit::shader_library library;
        std::vector<uint8_t> packBytes;
        std::vector<conduit::shader_handle> vertexShaders, fragmentShaders;
        conduit::shader_target target = conduit::shader_target::spirv;

        conduit::bind_layout_handle uniformLayout, textureLayout;
        conduit::pipeline_layout_handle pipelineLayout;
        std::array<conduit::sampler_handle, static_cast<size_t>(SamplerKind::Count)> samplers{};
        uint32_t uniformRangeBytes = 4096;
        bool ringStorage = true;

        std::unordered_map<PipelineKey, conduit::pipeline_handle, PipelineKeyHash> pipelines;
        std::unordered_map<TextureSet, conduit::bind_group_handle, TextureSetHash> textureGroups;

        std::vector<std::vector<RingChunk>> rings;
        std::vector<std::vector<conduit::bind_group_handle>> ringGroups;
        std::vector<uint32_t> ringCursor;

        std::vector<uint8_t> pendingBytes;
        std::vector<BufferWrite> bufferWrites;
        std::vector<ImageWrite> imageWrites;
        std::unordered_map<uint32_t, conduit::state> textureStates;
        std::unordered_map<uint32_t, conduit::state> bufferStates;

        conduit::texture_handle white2d, whiteArray;
        conduit::texture_handle offscreen;
        conduit::format targetFormat = conduit::format::bgra8_unorm;
        uint32_t targetWidth = 0, targetHeight = 0;
        uint32_t acquireFailures = 0;
    };

    uint32_t FormatBytes(conduit::format format);
}
