#include "rhi/vulkan/VulkanDevice.hpp"

#include "rhi/vulkan/VulkanConvert.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <set>

namespace rhi::vk {
    namespace {
        constexpr uint32_t kMaxFramesInFlight = 4;

        template <class... Ts> void Chain(void** tail, Ts*... structs) {
            ((*tail = structs,
              tail = reinterpret_cast<void**>(&reinterpret_cast<VkBaseOutStructure*>(structs)->pNext)),
             ...);
        }
    }

    Device::Device(std::shared_ptr<InstanceState> instance, const PhysicalInfo& phys,
                   const AdapterInfo& adapter, const DeviceOptions& opts)
        : inst(std::move(instance)), phys(phys), options(opts) {
        physical = phys.device;
        caps.adapter = adapter;
        options.framesInFlight = std::clamp(options.framesInFlight, 1u, kMaxFramesInFlight);
        useValidator = options.stateValidation;
    }

    Device::~Device() {
        if (!device)
            return;
        fn.vkDeviceWaitIdle(device);
        retire.FlushAll();

        // anything the owner forgot to destroy
        buffers.ForEach([&](BufferHandle, BufferRec& r) { vmaDestroyBuffer(allocator, r.buffer, r.alloc); });
        textures.ForEach([&](TextureHandle, TextureRec& r) {
            for (auto& [key, view] : r.views)
                fn.vkDestroyImageView(device, view, nullptr);
            if (!r.external)
                vmaDestroyImage(allocator, r.image, r.alloc);
        });
        samplers.ForEach(
            [&](SamplerHandle, SamplerRec& r) { fn.vkDestroySampler(device, r.sampler, nullptr); });
        shaders.ForEach(
            [&](ShaderHandle, ShaderRec& r) { fn.vkDestroyShaderModule(device, r.module, nullptr); });
        bindLayouts.ForEach([&](BindLayoutHandle, BindLayoutRec& r) {
            fn.vkDestroyDescriptorSetLayout(device, r.layout, nullptr);
        });
        pipelineLayouts.ForEach([&](PipelineLayoutHandle, PipelineLayoutRec& r) {
            fn.vkDestroyPipelineLayout(device, r.layout, nullptr);
        });
        pipelines.ForEach(
            [&](PipelineHandle, PipelineRec& r) { fn.vkDestroyPipeline(device, r.pipeline, nullptr); });
        queries.ForEach([&](QuerySetHandle, QueryRec& r) { fn.vkDestroyQueryPool(device, r.pool, nullptr); });
        fences.ForEach(
            [&](FenceHandle, FenceRec& r) { fn.vkDestroySemaphore(device, r.semaphore, nullptr); });

        for (VkDescriptorPool p : persistentPools)
            fn.vkDestroyDescriptorPool(device, p, nullptr);
        for (auto& f : frameDescriptors)
            for (VkDescriptorPool p : f.pools)
                fn.vkDestroyDescriptorPool(device, p, nullptr);
        for (auto& [id, tp] : threadPools)
            for (auto& slotPools : tp->slots)
                for (auto& q : slotPools)
                    if (q.pool)
                        fn.vkDestroyCommandPool(device, q.pool, nullptr);
        for (auto& q : queues)
            if (q.timeline)
                fn.vkDestroySemaphore(device, q.timeline, nullptr);
        if (emptySetLayout)
            fn.vkDestroyDescriptorSetLayout(device, emptySetLayout, nullptr);
        if (pipelineCache)
            fn.vkDestroyPipelineCache(device, pipelineCache, nullptr);
        if (allocator)
            vmaDestroyAllocator(allocator);
        fn.vkDestroyDevice(device, nullptr);
    }

    bool Device::PickQueues(std::string& error) {
        uint32_t count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, nullptr);
        std::vector<VkQueueFamilyProperties> fams(count);
        vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, fams.data());

        int gfx = -1, comp = -1, copy = -1;
        for (uint32_t i = 0; i < count; i++) {
            const VkQueueFlags f = fams[i].queueFlags;
            if (gfx < 0 && (f & VK_QUEUE_GRAPHICS_BIT) && (f & VK_QUEUE_COMPUTE_BIT))
                gfx = static_cast<int>(i);
            if (comp < 0 && (f & VK_QUEUE_COMPUTE_BIT) && !(f & VK_QUEUE_GRAPHICS_BIT))
                comp = static_cast<int>(i);
            if (copy < 0 && (f & VK_QUEUE_TRANSFER_BIT) &&
                !(f & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)))
                copy = static_cast<int>(i);
        }
        if (gfx < 0) {
            error = "no graphics+compute queue family";
            return false;
        }

        struct Slot {
            uint32_t family, index;
        };
        Slot slots[kQueueCount] = {{static_cast<uint32_t>(gfx), 0},
                                   {static_cast<uint32_t>(gfx), 0},
                                   {static_cast<uint32_t>(gfx), 0}};
        if (options.allowAsyncCompute) {
            if (comp >= 0)
                slots[1] = {static_cast<uint32_t>(comp), 0};
            else if (fams[gfx].queueCount > 1)
                slots[1] = {static_cast<uint32_t>(gfx), 1};
        }
        if (copy >= 0)
            slots[2] = {static_cast<uint32_t>(copy), 0};
        else if (slots[1].family != static_cast<uint32_t>(gfx) || slots[1].index != 0)
            slots[2] = slots[1];

        queuePriorities.fill(1.0f);
        std::vector<VkDeviceQueueCreateInfo> infos;
        std::vector<Slot> unique;
        for (const Slot& s : slots) {
            bool seen = false;
            for (const Slot& u : unique)
                seen |= (u.family == s.family && u.index == s.index);
            if (!seen)
                unique.push_back(s);
        }
        std::set<uint32_t> families;
        for (const Slot& u : unique)
            families.insert(u.family);
        for (uint32_t fam : families) {
            uint32_t need = 1;
            for (const Slot& u : unique)
                if (u.family == fam)
                    need = (std::max)(need, u.index + 1);
            VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
            qi.queueFamilyIndex = fam;
            qi.queueCount = need;
            qi.pQueuePriorities = queuePriorities.data();
            infos.push_back(qi);
        }
        queueCreateInfos = std::move(infos);
        for (size_t i = 0; i < kQueueCount; i++) {
            queues[i].family = slots[i].family;
            queueIndex[i] = slots[i].index;
        }
        return true;
    }

    bool Device::Init(std::string& error) {
        if (!PickQueues(error))
            return false;

        const bool api13 = phys.props.apiVersion >= VK_API_VERSION_1_3;
        vkGetPhysicalDeviceMemoryProperties(physical, &memProps);

        VkPhysicalDeviceFeatures2 sup2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        VkPhysicalDeviceVulkan11Features sup11{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
        VkPhysicalDeviceVulkan12Features sup12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
        VkPhysicalDeviceVulkan13Features sup13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
        VkPhysicalDeviceDynamicRenderingFeaturesKHR supDr{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR};
        VkPhysicalDeviceSynchronization2FeaturesKHR supS2{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES_KHR};
        VkPhysicalDeviceDepthClipEnableFeaturesEXT supClip{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_ENABLE_FEATURES_EXT};
        {
            void** tail = &sup2.pNext;
            Chain(tail, &sup11);
            tail = &sup11.pNext;
            Chain(tail, &sup12);
            tail = &sup12.pNext;
            if (api13) {
                Chain(tail, &sup13);
                tail = &sup13.pNext;
            } else {
                Chain(tail, &supDr, &supS2);
                tail = &supS2.pNext;
            }
            if (phys.Has(VK_EXT_DEPTH_CLIP_ENABLE_EXTENSION_NAME))
                Chain(tail, &supClip);
            vkGetPhysicalDeviceFeatures2(physical, &sup2);
        }
        const VkPhysicalDeviceFeatures& sf = sup2.features;

        VkPhysicalDeviceFeatures2 en2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        VkPhysicalDeviceVulkan11Features en11{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
        VkPhysicalDeviceVulkan12Features en12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
        VkPhysicalDeviceVulkan13Features en13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
        VkPhysicalDeviceDynamicRenderingFeaturesKHR enDr{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR};
        VkPhysicalDeviceSynchronization2FeaturesKHR enS2{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES_KHR};
        VkPhysicalDeviceDepthClipEnableFeaturesEXT enClip{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_ENABLE_FEATURES_EXT};

        VkPhysicalDeviceFeatures& ef = en2.features;
        // robustBufferAccess stays off: bounds checks cost shader time and the renderer never indexes out of range
        ef.samplerAnisotropy = sf.samplerAnisotropy;
        ef.multiDrawIndirect = sf.multiDrawIndirect;
        ef.drawIndirectFirstInstance = sf.drawIndirectFirstInstance;
        ef.textureCompressionBC = sf.textureCompressionBC;
        ef.independentBlend = sf.independentBlend;
        ef.fillModeNonSolid = sf.fillModeNonSolid;
        ef.depthClamp = sf.depthClamp;
        ef.depthBiasClamp = sf.depthBiasClamp;
        ef.shaderInt64 = sf.shaderInt64;
        ef.shaderFloat64 = sf.shaderFloat64;
        ef.imageCubeArray = sf.imageCubeArray;
        ef.sampleRateShading = sf.sampleRateShading;
        ef.fragmentStoresAndAtomics = sf.fragmentStoresAndAtomics;
        ef.vertexPipelineStoresAndAtomics = sf.vertexPipelineStoresAndAtomics;
        ef.shaderStorageImageReadWithoutFormat = sf.shaderStorageImageReadWithoutFormat;
        ef.shaderStorageImageWriteWithoutFormat = sf.shaderStorageImageWriteWithoutFormat;
        ef.shaderStorageImageExtendedFormats = sf.shaderStorageImageExtendedFormats;

        en11.shaderDrawParameters = sup11.shaderDrawParameters;
        en11.storageBuffer16BitAccess = sup11.storageBuffer16BitAccess;
        en11.uniformAndStorageBuffer16BitAccess = sup11.uniformAndStorageBuffer16BitAccess;

        en12.timelineSemaphore = VK_TRUE;
        en12.bufferDeviceAddress = sup12.bufferDeviceAddress;
        en12.drawIndirectCount = sup12.drawIndirectCount;
        en12.hostQueryReset = sup12.hostQueryReset;
        en12.shaderFloat16 = sup12.shaderFloat16;
        en12.samplerMirrorClampToEdge = sup12.samplerMirrorClampToEdge;
        en12.runtimeDescriptorArray = sup12.runtimeDescriptorArray;
        en12.descriptorBindingPartiallyBound = sup12.descriptorBindingPartiallyBound;
        en12.shaderSampledImageArrayNonUniformIndexing = sup12.shaderSampledImageArrayNonUniformIndexing;

        std::vector<const char*> extensions;
        auto enable = [&](const char* name) {
            if (!phys.Has(name))
                return false;
            if (std::find_if(extensions.begin(), extensions.end(),
                             [&](const char* e) { return std::strcmp(e, name) == 0; }) == extensions.end())
                extensions.push_back(name);
            return true;
        };
        swapchainExt = enable(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
        if (api13) {
            en13.dynamicRendering = VK_TRUE;
            en13.synchronization2 = VK_TRUE;
            en13.maintenance4 = sup13.maintenance4;
        } else {
            enable(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
            enable(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);
            enDr.dynamicRendering = VK_TRUE;
            enS2.synchronization2 = VK_TRUE;
            dynamicRenderingExt = true;
        }
        const bool memoryBudget = enable(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
        if (supClip.depthClipEnable && enable(VK_EXT_DEPTH_CLIP_ENABLE_EXTENSION_NAME)) {
            enClip.depthClipEnable = VK_TRUE;
            depthClipEnable = true;
        }
        enable("VK_KHR_portability_subset");
        for (const char* e : options.hooks.deviceExtensions)
            enable(e);

        {
            void** tail = &en2.pNext;
            Chain(tail, &en11);
            tail = &en11.pNext;
            Chain(tail, &en12);
            tail = &en12.pNext;
            if (api13) {
                Chain(tail, &en13);
                tail = &en13.pNext;
            } else {
                Chain(tail, &enDr, &enS2);
                tail = &enS2.pNext;
            }
            if (depthClipEnable) {
                Chain(tail, &enClip);
                tail = &enClip.pNext;
            }
            *tail = options.hooks.deviceFeatureChain;
        }

        VkDeviceCreateInfo ci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        ci.pNext = &en2;
        ci.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
        ci.pQueueCreateInfos = queueCreateInfos.data();
        ci.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        ci.ppEnabledExtensionNames = extensions.data();
        const VkResult created = vkCreateDevice(physical, &ci, nullptr, &device);
        if (created != VK_SUCCESS) {
            error = "vkCreateDevice failed (" + std::to_string(created) + ")";
            device = VK_NULL_HANDLE;
            return false;
        }
        volkLoadDeviceTable(&fn, device);
        // the khr names stand in on 1.2 devices that got the features through extensions
        auto alias = [](auto& core, auto ext) {
            if (!core)
                core = ext;
        };
        alias(fn.vkCmdBeginRendering, fn.vkCmdBeginRenderingKHR);
        alias(fn.vkCmdEndRendering, fn.vkCmdEndRenderingKHR);
        alias(fn.vkCmdPipelineBarrier2, fn.vkCmdPipelineBarrier2KHR);
        alias(fn.vkQueueSubmit2, fn.vkQueueSubmit2KHR);
        alias(fn.vkCmdWriteTimestamp2, fn.vkCmdWriteTimestamp2KHR);
        alias(fn.vkCmdDrawIndexedIndirectCount, fn.vkCmdDrawIndexedIndirectCountKHR);
        alias(fn.vkWaitSemaphores, fn.vkWaitSemaphoresKHR);
        alias(fn.vkGetSemaphoreCounterValue, fn.vkGetSemaphoreCounterValueKHR);
        alias(fn.vkSignalSemaphore, fn.vkSignalSemaphoreKHR);
        alias(fn.vkGetBufferDeviceAddress, fn.vkGetBufferDeviceAddressKHR);
        alias(fn.vkResetQueryPool, fn.vkResetQueryPoolEXT);
        hostQueryReset = en12.hostQueryReset && fn.vkResetQueryPool;

        std::mutex* mutexes[kQueueCount] = {};
        for (size_t i = 0; i < kQueueCount; i++) {
            fn.vkGetDeviceQueue(device, queues[i].family, queueIndex[i], &queues[i].queue);
            for (size_t j = 0; j < i; j++)
                if (queues[j].queue == queues[i].queue) {
                    queues[i].shared = queues[j].shared = true;
                    mutexes[i] = mutexes[j];
                }
            if (!mutexes[i])
                mutexes[i] = &queueMutexes[i];
            queues[i].mutex = mutexes[i];

            VkSemaphoreTypeCreateInfo type{VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
            type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
            VkSemaphoreCreateInfo sci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, &type};
            if (fn.vkCreateSemaphore(device, &sci, nullptr, &queues[i].timeline) != VK_SUCCESS) {
                error = "timeline semaphore creation failed";
                return false;
            }
        }

        VmaVulkanFunctions vf{};
        vf.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
        vf.vkGetDeviceProcAddr = vkGetDeviceProcAddr;
        VmaAllocatorCreateInfo ai{};
        ai.physicalDevice = physical;
        ai.device = device;
        ai.instance = inst->instance;
        ai.vulkanApiVersion = (std::min)(phys.props.apiVersion, inst->apiVersion);
        // the 256 MB default block wastes most of itself on a game that needs a few hundred in total
        ai.preferredLargeHeapBlockSize = 32ull << 20;
        ai.pVulkanFunctions = &vf;
        if (en12.bufferDeviceAddress)
            ai.flags |= VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
        if (memoryBudget)
            ai.flags |= VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT;
        if (vmaCreateAllocator(&ai, &allocator) != VK_SUCCESS) {
            error = "VMA allocator creation failed";
            return false;
        }

        VkPipelineCacheCreateInfo pc{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
        pc.initialDataSize = options.pipelineCacheData.size();
        pc.pInitialData = options.pipelineCacheData.empty() ? nullptr : options.pipelineCacheData.data();
        if (fn.vkCreatePipelineCache(device, &pc, nullptr, &pipelineCache) != VK_SUCCESS &&
            pc.initialDataSize) {
            // stale or foreign blob: start empty
            pc.initialDataSize = 0;
            pc.pInitialData = nullptr;
            fn.vkCreatePipelineCache(device, &pc, nullptr, &pipelineCache);
        }

        VkDescriptorSetLayoutCreateInfo empty{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        fn.vkCreateDescriptorSetLayout(device, &empty, nullptr, &emptySetLayout);

        frameDescriptors.resize(options.framesInFlight);
        slotSerials.assign(options.framesInFlight, QueueSerials{});

        caps.features.bufferDeviceAddress = en12.bufferDeviceAddress;
        FillCaps(caps.adapter);
        caps.features.multiDrawIndirect = ef.multiDrawIndirect && ef.drawIndirectFirstInstance;
        caps.features.drawIndirectFirstInstance = ef.drawIndirectFirstInstance;
        caps.features.drawIndirectCount = en12.drawIndirectCount;
        caps.features.samplerAnisotropy = ef.samplerAnisotropy;
        caps.features.textureCompressionBC = ef.textureCompressionBC;
        caps.features.shaderFloat16 = en12.shaderFloat16;
        caps.features.shaderInt64 = ef.shaderInt64;
        caps.features.shaderFloat64 = ef.shaderFloat64;
        caps.features.depthClipControl = depthClipEnable;
        caps.features.bindless = en12.runtimeDescriptorArray && en12.descriptorBindingPartiallyBound &&
                                 en12.shaderSampledImageArrayNonUniformIndexing;
        caps.features.enhancedBarriers = true;
        caps.features.timestampQueries = phys.props.limits.timestampComputeAndGraphics != 0;
        caps.features.pipelineCacheBlob = true;
        caps.features.asyncCompute = queues[1].queue != queues[0].queue;
        caps.features.dedicatedCopyQueue =
            queues[2].queue != queues[0].queue && queues[2].queue != queues[1].queue;
        caps.features.meshShaders = phys.Has("VK_EXT_mesh_shader");
        caps.features.rayTracing = phys.Has("VK_KHR_ray_query") && phys.Has("VK_KHR_acceleration_structure");
        caps.features.variableRateShading = phys.Has("VK_KHR_fragment_shading_rate");
        caps.features.cooperativeVector = phys.Has("VK_NV_cooperative_vector");
        caps.features.cooperativeMatrix = phys.Has("VK_KHR_cooperative_matrix");
        caps.features.hdrOutput = inst->colorSpaceExt && swapchainExt;
        caps.features.tearing = swapchainExt;
        return true;
    }

    void Device::FillCaps(const AdapterInfo&) {
        const VkPhysicalDeviceLimits& l = phys.props.limits;
        caps.limits.maxTexture2D = l.maxImageDimension2D;
        caps.limits.maxTextureLayers = l.maxImageArrayLayers;
        caps.limits.maxPushConstantBytes = std::min<uint32_t>(l.maxPushConstantsSize, kMaxPushConstantBytes);
        caps.limits.maxSampledTexturesPerStage = l.maxPerStageDescriptorSampledImages;
        caps.limits.maxColorTargets = std::min<uint32_t>(l.maxColorAttachments, kMaxColorTargets);
        const VkSampleCountFlags msaa = l.framebufferColorSampleCounts & l.framebufferDepthSampleCounts;
        caps.limits.maxMsaaSamples = (msaa & VK_SAMPLE_COUNT_8_BIT)   ? 8
                                     : (msaa & VK_SAMPLE_COUNT_4_BIT) ? 4
                                     : (msaa & VK_SAMPLE_COUNT_2_BIT) ? 2
                                                                      : 1;
        caps.limits.uniformBufferAlignment = static_cast<uint32_t>(l.minUniformBufferOffsetAlignment);
        caps.limits.storageBufferAlignment = static_cast<uint32_t>(l.minStorageBufferOffsetAlignment);
        caps.limits.copyRowPitchAlignment =
            static_cast<uint32_t>(std::max<VkDeviceSize>(1, l.optimalBufferCopyRowPitchAlignment));
        caps.limits.copyOffsetAlignment =
            static_cast<uint32_t>(std::max<VkDeviceSize>(4, l.optimalBufferCopyOffsetAlignment));
        caps.limits.maxAnisotropy = l.maxSamplerAnisotropy;
        caps.limits.timestampPeriodNs = l.timestampPeriod;
        caps.limits.maxBufferSize = 1ull << 30;

        VkPhysicalDeviceSubgroupProperties sub{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES};
        VkPhysicalDeviceProperties2 p2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &sub};
        vkGetPhysicalDeviceProperties2(physical, &p2);
        caps.features.subgroupOps = (sub.supportedOperations & VK_SUBGROUP_FEATURE_BASIC_BIT) &&
                                    (sub.supportedStages & VK_SHADER_STAGE_COMPUTE_BIT);
        caps.limits.minSubgroupSize = caps.limits.maxSubgroupSize = sub.subgroupSize;

        // resizable bar: a device-local heap with host-visible memory bigger than the classic 256mb window
        for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
            const auto flags = memProps.memoryTypes[i].propertyFlags;
            const auto& heap = memProps.memoryHeaps[memProps.memoryTypes[i].heapIndex];
            if ((flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) &&
                (flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
                if (caps.adapter.type == AdapterType::Integrated || caps.adapter.type == AdapterType::Cpu)
                    caps.features.unifiedMemory = true;
                else if (heap.size > (256ull << 20) + (64ull << 20))
                    caps.features.resizableBar = true;
            }
        }

        VkFormatProperties d24{};
        vkGetPhysicalDeviceFormatProperties(physical, VK_FORMAT_D24_UNORM_S8_UINT, &d24);
        d24Supported = (d24.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0;

        for (size_t i = 1; i < static_cast<size_t>(Format::Count); i++) {
            const Format f = static_cast<Format>(i);
            VkFormatProperties props{};
            vkGetPhysicalDeviceFormatProperties(physical, FormatToVk(f), &props);
            const VkFormatFeatureFlags o = props.optimalTilingFeatures;
            FormatUse use = FormatUse::None;
            if (o & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)
                use = use | FormatUse::Sampled;
            if (o & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)
                use = use | FormatUse::SampledLinear;
            if (o & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT)
                use = use | FormatUse::ColorTarget | FormatUse::Msaa;
            if (o & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT)
                use = use | FormatUse::Blendable;
            if (o & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
                use = use | FormatUse::DepthTarget | FormatUse::Msaa;
            if (o & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT)
                use = use | FormatUse::Storage;
            caps.formats[i] = use;
        }
    }

    VkFormat Device::FormatToVk(Format f) const {
        if (f == Format::D24UnormS8Uint && !d24Supported)
            return VK_FORMAT_D32_SFLOAT_S8_UINT;
        return BaseFormat(f);
    }

    NativeHandles Device::Native() const {
        NativeHandles n;
        n.api = Api::Vulkan;
        n.instance = inst->instance;
        n.physicalDevice = physical;
        n.device = device;
        n.graphicsQueue = queues[0].queue;
        n.computeQueue = queues[1].queue;
        n.graphicsFamily = queues[0].family;
        n.computeFamily = queues[1].family;
        n.getInstanceProcAddr = reinterpret_cast<void*>(vkGetInstanceProcAddr);
        n.getDeviceProcAddr = reinterpret_cast<void*>(vkGetDeviceProcAddr);
        return n;
    }

    NativeTexture Device::NativeOf(TextureHandle h, State state) {
        TextureRec* t = textures.Get(h);
        if (!t)
            return {};
        NativeTexture n;
        n.resource = reinterpret_cast<void*>(t->image);
        if (t->alloc) {
            VmaAllocationInfo info{};
            vmaGetAllocationInfo(allocator, t->alloc, &info);
            n.memory = reinterpret_cast<void*>(info.deviceMemory);
        }
        n.view = reinterpret_cast<void*>(View(*t, {}, TextureDim::Tex2D, Format::Unknown, false));
        n.state = UseOf(state, QueueType::Graphics, (t->aspect & VK_IMAGE_ASPECT_DEPTH_BIT) != 0).layout;
        n.nativeFormat = t->format;
        n.width = t->desc.width;
        n.height = t->desc.height;
        n.mips = t->desc.mips;
        n.layers = t->layers;
        n.usage = t->usage;
        n.flags = t->createFlags;
        n.aspect = t->aspect;
        return n;
    }

    void Device::NameObject(VkObjectType type, uint64_t handle, const char* name) {
        if (!inst->debugUtils || !name || !*name || !vkSetDebugUtilsObjectNameEXT)
            return;
        VkDebugUtilsObjectNameInfoEXT info{VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT};
        info.objectType = type;
        info.objectHandle = handle;
        info.pObjectName = name;
        vkSetDebugUtilsObjectNameEXT(device, &info);
    }

    void Device::ReportError(const std::string& message) {
        {
            std::lock_guard lock(inst->messageMutex);
            inst->errorCount++;
            if (inst->messages.size() < 256)
                inst->messages.push_back(message);
        }
        std::fprintf(stderr, "[rhi/vulkan] %s\n", message.c_str());
    }

    QueueSerials Device::Completed() {
        QueueSerials out{};
        for (size_t i = 0; i < kQueueCount; i++) {
            uint64_t value = 0;
            fn.vkGetSemaphoreCounterValue(device, queues[i].timeline, &value);
            out[i] = value;
        }
        return out;
    }

    void Device::Retire(std::function<void()> release) {
        QueueSerials snapshot;
        for (size_t i = 0; i < kQueueCount; i++)
            snapshot[i] = queues[i].serial.load();
        retire.Push(snapshot, std::move(release));
    }

    BufferHandle Device::CreateBuffer(const BufferDesc& desc) {
        if (desc.size == 0) {
            ReportError("CreateBuffer: zero size");
            return {};
        }
        VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bi.size = desc.size;
        VkBufferUsageFlags usage = 0;
        if (Has(desc.usage, BufferUsage::Vertex))
            usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        if (Has(desc.usage, BufferUsage::Index))
            usage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        if (Has(desc.usage, BufferUsage::Uniform))
            usage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        if (Has(desc.usage, BufferUsage::Storage))
            usage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        if (Has(desc.usage, BufferUsage::Indirect))
            usage |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
        if (Has(desc.usage, BufferUsage::CopySrc))
            usage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        if (Has(desc.usage, BufferUsage::CopyDst))
            usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        if (caps.features.bufferDeviceAddress &&
            (usage & (VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT)))
            usage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        bi.usage = usage;

        uint32_t familyIndices[kQueueCount];
        uint32_t familyCount = 0;
        if (desc.crossQueue) {
            for (auto& q : queues) {
                bool seen = false;
                for (uint32_t i = 0; i < familyCount; i++)
                    seen |= familyIndices[i] == q.family;
                if (!seen)
                    familyIndices[familyCount++] = q.family;
            }
        }
        if (familyCount > 1) {
            bi.sharingMode = VK_SHARING_MODE_CONCURRENT;
            bi.queueFamilyIndexCount = familyCount;
            bi.pQueueFamilyIndices = familyIndices;
        }

        VmaAllocationCreateInfo ai{};
        auto host = [&](bool random) {
            ai.usage = VMA_MEMORY_USAGE_AUTO;
            ai.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT |
                       (random ? VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT
                               : VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
        };
        MemoryClass memory = desc.memory;
        switch (memory) {
        case MemoryClass::GpuOnly:
            ai.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
            break;
        case MemoryClass::Upload:
            host(false);
            break;
        case MemoryClass::Readback:
            host(true);
            break;
        case MemoryClass::GpuMappable:
            if (caps.features.resizableBar || caps.features.unifiedMemory) {
                host(false);
                ai.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
                ai.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
            } else {
                host(false);
                memory = MemoryClass::Upload;
            }
            break;
        }

        BufferRec rec;
        VmaAllocationInfo info{};
        VkResult r = vmaCreateBuffer(allocator, &bi, &ai, &rec.buffer, &rec.alloc, &info);
        if (r != VK_SUCCESS && desc.memory == MemoryClass::GpuMappable) {
            ai.requiredFlags = 0;
            host(false);
            r = vmaCreateBuffer(allocator, &bi, &ai, &rec.buffer, &rec.alloc, &info);
        }
        if (r != VK_SUCCESS) {
            ReportError("CreateBuffer '" + std::string(desc.name ? desc.name : "") + "' failed (" +
                        std::to_string(r) + ")");
            return {};
        }
        rec.desc = desc;
        rec.name = desc.name ? desc.name : "";
        rec.mapped = static_cast<uint8_t*>(info.pMappedData);
        VkMemoryPropertyFlags flags = 0;
        vmaGetAllocationMemoryProperties(allocator, rec.alloc, &flags);
        rec.coherent = (flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
        if (caps.features.bufferDeviceAddress && (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)) {
            VkBufferDeviceAddressInfo ba{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
            ba.buffer = rec.buffer;
            rec.address = fn.vkGetBufferDeviceAddress(device, &ba);
        }
        NameObject(VK_OBJECT_TYPE_BUFFER, reinterpret_cast<uint64_t>(rec.buffer), desc.name);
        bufferBytes += static_cast<int64_t>(desc.size);
        liveBuffers++;

        BufferHandle h = buffers.Alloc(std::move(rec));
        BufferRec* stored = buffers.Get(h);
        // short strings move by copy, so the name pointer has to be set on the final object
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
        rec.format = FormatToVk(desc.format);
        rec.aspect = AspectOf(desc.format);
        rec.layers = LayerCountOf(desc);

        VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        ii.imageType = desc.dim == TextureDim::Tex3D ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D;
        ii.format = rec.format;
        ii.extent = {desc.width, desc.height, DepthOf(desc)};
        ii.mipLevels = rec.desc.mips;
        ii.arrayLayers = rec.layers;
        ii.samples = SamplesOf(desc.samples);
        ii.tiling = VK_IMAGE_TILING_OPTIMAL;
        ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (desc.dim == TextureDim::Cube)
            ii.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
        if (!GetFormatInfo(desc.format).depth)
            ii.flags |= VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
        VkImageUsageFlags usage = 0;
        if (Has(desc.usage, TextureUsage::Sampled))
            usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
        if (Has(desc.usage, TextureUsage::Storage))
            usage |= VK_IMAGE_USAGE_STORAGE_BIT;
        if (Has(desc.usage, TextureUsage::ColorTarget))
            usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        if (Has(desc.usage, TextureUsage::DepthTarget))
            usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        if (Has(desc.usage, TextureUsage::CopySrc))
            usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        if (Has(desc.usage, TextureUsage::CopyDst))
            usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        ii.usage = usage;
        rec.usage = usage;
        rec.createFlags = ii.flags;

        uint32_t familyIndices[kQueueCount];
        uint32_t familyCount = 0;
        if (desc.crossQueue) {
            for (auto& q : queues) {
                bool seen = false;
                for (uint32_t i = 0; i < familyCount; i++)
                    seen |= familyIndices[i] == q.family;
                if (!seen)
                    familyIndices[familyCount++] = q.family;
            }
        }
        if (familyCount > 1) {
            ii.sharingMode = VK_SHARING_MODE_CONCURRENT;
            ii.queueFamilyIndexCount = familyCount;
            ii.pQueueFamilyIndices = familyIndices;
        }

        VmaAllocationCreateInfo ai{};
        ai.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        if (usage & (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT))
            ai.flags |= VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
        const VkResult r = vmaCreateImage(allocator, &ii, &ai, &rec.image, &rec.alloc, nullptr);
        if (r != VK_SUCCESS) {
            ReportError("CreateTexture '" + rec.name + "' failed (" + std::to_string(r) + ")");
            return {};
        }
        NameObject(VK_OBJECT_TYPE_IMAGE, reinterpret_cast<uint64_t>(rec.image), desc.name);

        uint64_t bytes = 0;
        for (uint32_t m = 0; m < rec.desc.mips; m++)
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

    TextureHandle Device::WrapSwapchainImage(VkImage image, const TextureDesc& desc, VkFormat format) {
        TextureRec rec;
        rec.image = image;
        rec.desc = desc;
        rec.name = desc.name ? desc.name : "backbuffer";
        rec.format = format;
        rec.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
        rec.layers = 1;
        rec.external = true;
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
        for (auto& [key, view] : rec.views)
            fn.vkDestroyImageView(device, view, nullptr);
    }

    VkImageView Device::MakeView(TextureRec& tex, const SubresourceRange& range, VkImageViewType type,
                                 VkFormat format, VkImageAspectFlags aspect) {
        const SubresourceRange r = ResolveRange(range, tex.desc);
        const uint64_t key =
            (static_cast<uint64_t>(r.baseMip) & 0xF) | (static_cast<uint64_t>(r.mipCount) & 0xF) << 4 |
            (static_cast<uint64_t>(r.baseLayer) & 0xFFF) << 8 |
            (static_cast<uint64_t>(r.layerCount) & 0xFFF) << 20 | static_cast<uint64_t>(type) << 32 |
            static_cast<uint64_t>(format) << 36 | static_cast<uint64_t>(aspect) << 56;
        std::lock_guard lock(viewMutex);
        auto it = tex.views.find(key);
        if (it != tex.views.end())
            return it->second;
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image = tex.image;
        vi.viewType = type;
        vi.format = format;
        vi.subresourceRange = {
            aspect, r.baseMip, r.mipCount, r.baseLayer,
            (type == VK_IMAGE_VIEW_TYPE_2D || type == VK_IMAGE_VIEW_TYPE_3D) ? 1u : r.layerCount};
        if (type == VK_IMAGE_VIEW_TYPE_CUBE)
            vi.subresourceRange.layerCount = 6;
        VkImageView view = VK_NULL_HANDLE;
        if (fn.vkCreateImageView(device, &vi, nullptr, &view) != VK_SUCCESS) {
            ReportError("vkCreateImageView failed");
            return VK_NULL_HANDLE;
        }
        tex.views.emplace(key, view);
        return view;
    }

    VkImageView Device::View(TextureRec& tex, const SubresourceRange& range, TextureDim dim, Format format,
                             bool stencil) {
        VkFormat vkFormat = format == Format::Unknown ? tex.format : FormatToVk(format);
        VkImageAspectFlags aspect = tex.aspect;
        if (tex.aspect & VK_IMAGE_ASPECT_DEPTH_BIT)
            aspect = stencil ? VK_IMAGE_ASPECT_STENCIL_BIT : VK_IMAGE_ASPECT_DEPTH_BIT;
        return MakeView(tex, range, ViewTypeOf(dim), vkFormat, aspect);
    }

    VkImageView Device::AttachmentView(TextureHandle h, uint32_t mip, uint32_t layer) {
        TextureRec* tex = textures.Get(h);
        if (!tex)
            return VK_NULL_HANDLE;
        return MakeView(*tex, {mip, 1, layer, 1}, VK_IMAGE_VIEW_TYPE_2D, tex->format, tex->aspect);
    }

    SamplerHandle Device::CreateSampler(const SamplerDesc& desc) {
        std::lock_guard lock(samplerMutex);
        for (auto& [d, h] : samplerCache)
            if (d == desc)
                return h;
        VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        si.magFilter = ToVk(desc.mag);
        si.minFilter = ToVk(desc.min);
        si.mipmapMode =
            desc.mip == Filter::Nearest ? VK_SAMPLER_MIPMAP_MODE_NEAREST : VK_SAMPLER_MIPMAP_MODE_LINEAR;
        si.addressModeU = ToVk(desc.u);
        si.addressModeV = ToVk(desc.v);
        si.addressModeW = ToVk(desc.w);
        si.mipLodBias = desc.lodBias;
        si.anisotropyEnable = desc.maxAnisotropy > 1.0f && caps.features.samplerAnisotropy;
        si.maxAnisotropy = (std::min)(desc.maxAnisotropy, caps.limits.maxAnisotropy);
        si.compareEnable = desc.compare;
        si.compareOp = ToVk(desc.compareOp);
        si.minLod = desc.minLod;
        si.maxLod = desc.maxLod;
        const bool opaque = desc.border[3] > 0.5f;
        const bool white = desc.border[0] > 0.5f;
        si.borderColor =
            opaque ? (white ? VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE : VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK)
                   : VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
        SamplerRec rec;
        rec.desc = desc;
        if (fn.vkCreateSampler(device, &si, nullptr, &rec.sampler) != VK_SUCCESS) {
            ReportError("vkCreateSampler failed");
            return {};
        }
        SamplerHandle h = samplers.Alloc(rec);
        samplerCache.emplace_back(desc, h);
        return h;
    }

    ShaderHandle Device::CreateShader(const ShaderDesc& desc) {
        if (desc.format != ShaderFormat::SpirV || desc.code.size() < 20 || desc.code.size() % 4 != 0) {
            ReportError("CreateShader: expects SPIR-V");
            return {};
        }
        VkShaderModuleCreateInfo si{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        si.codeSize = desc.code.size();
        // the span may not be 4-byte aligned
        std::vector<uint32_t> words(desc.code.size() / 4);
        std::memcpy(words.data(), desc.code.data(), desc.code.size());
        si.pCode = words.data();
        ShaderRec rec;
        rec.stage = desc.stage;
        rec.entry = desc.entry ? desc.entry : "main";
        if (fn.vkCreateShaderModule(device, &si, nullptr, &rec.module) != VK_SUCCESS) {
            ReportError("vkCreateShaderModule failed");
            return {};
        }
        NameObject(VK_OBJECT_TYPE_SHADER_MODULE, reinterpret_cast<uint64_t>(rec.module), desc.name);
        return shaders.Alloc(std::move(rec));
    }

    BindLayoutHandle Device::CreateBindLayout(const BindLayoutDesc& desc) {
        std::vector<VkDescriptorSetLayoutBinding> bindings;
        BindLayoutRec rec;
        for (const BindLayoutEntry& e : desc.entries) {
            VkDescriptorSetLayoutBinding b{};
            b.binding = e.binding;
            b.descriptorCount = (std::max)(1u, e.count);
            b.stageFlags = ToVk(e.stages);
            switch (e.type) {
            case BindingType::UniformBuffer:
                b.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                break;
            case BindingType::UniformBufferDynamic:
                b.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
                rec.dynamicCount++;
                break;
            case BindingType::StorageBuffer:
            case BindingType::StorageBufferRW:
                b.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                break;
            case BindingType::SampledTexture:
                b.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
                break;
            case BindingType::StorageTextureRW:
                b.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                break;
            case BindingType::Sampler:
                b.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
                break;
            }
            bindings.push_back(b);
            rec.entries.push_back(e);
        }
        VkDescriptorSetLayoutCreateInfo li{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        li.bindingCount = static_cast<uint32_t>(bindings.size());
        li.pBindings = bindings.data();
        if (fn.vkCreateDescriptorSetLayout(device, &li, nullptr, &rec.layout) != VK_SUCCESS) {
            ReportError("vkCreateDescriptorSetLayout failed");
            return {};
        }
        NameObject(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, reinterpret_cast<uint64_t>(rec.layout), desc.name);
        return bindLayouts.Alloc(std::move(rec));
    }

    VkDescriptorPool Device::NewDescriptorPool(bool freeable) {
        const VkDescriptorPoolSize sizes[] = {
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2048}, {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1024},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2048}, {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 8192},
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1024},  {VK_DESCRIPTOR_TYPE_SAMPLER, 4096},
        };
        VkDescriptorPoolCreateInfo pi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pi.flags = freeable ? VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT : 0;
        pi.maxSets = 1024;
        pi.poolSizeCount = static_cast<uint32_t>(std::size(sizes));
        pi.pPoolSizes = sizes;
        VkDescriptorPool pool = VK_NULL_HANDLE;
        fn.vkCreateDescriptorPool(device, &pi, nullptr, &pool);
        return pool;
    }

    VkDescriptorSet Device::AllocateSet(VkDescriptorSetLayout layout, bool perFrame,
                                        VkDescriptorPool& poolOut) {
        std::lock_guard lock(descriptorMutex);
        VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        ai.descriptorSetCount = 1;
        ai.pSetLayouts = &layout;
        VkDescriptorSet set = VK_NULL_HANDLE;

        if (perFrame) {
            FrameDescriptors& fd = frameDescriptors[slot];
            while (true) {
                if (fd.used >= fd.pools.size())
                    fd.pools.push_back(NewDescriptorPool(false));
                ai.descriptorPool = fd.pools[fd.used];
                const VkResult r = fn.vkAllocateDescriptorSets(device, &ai, &set);
                if (r == VK_SUCCESS) {
                    poolOut = ai.descriptorPool;
                    return set;
                }
                if (r != VK_ERROR_OUT_OF_POOL_MEMORY && r != VK_ERROR_FRAGMENTED_POOL)
                    return VK_NULL_HANDLE;
                fd.used++;
            }
        }

        for (int attempt = 0; attempt < 2; attempt++) {
            if (persistentPools.empty() || attempt == 1)
                persistentPools.push_back(NewDescriptorPool(true));
            ai.descriptorPool = persistentPools.back();
            const VkResult r = fn.vkAllocateDescriptorSets(device, &ai, &set);
            if (r == VK_SUCCESS) {
                poolOut = ai.descriptorPool;
                return set;
            }
            if (r != VK_ERROR_OUT_OF_POOL_MEMORY && r != VK_ERROR_FRAGMENTED_POOL)
                break;
        }
        return VK_NULL_HANDLE;
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
        rec.set = AllocateSet(layout->layout, desc.perFrame, rec.pool);
        if (!rec.set) {
            ReportError("CreateBindGroup: descriptor allocation failed");
            return {};
        }

        std::vector<VkWriteDescriptorSet> writes;
        std::vector<VkDescriptorBufferInfo> bufferInfos;
        std::vector<VkDescriptorImageInfo> imageInfos;
        writes.reserve(desc.entries.size());
        bufferInfos.reserve(desc.entries.size());
        imageInfos.reserve(desc.entries.size());

        for (const BindGroupEntry& e : desc.entries) {
            const BindLayoutEntry* le = nullptr;
            for (const BindLayoutEntry& cand : layout->entries)
                if (cand.binding == e.binding)
                    le = &cand;
            if (!le) {
                ReportError("CreateBindGroup: binding " + std::to_string(e.binding) +
                            " is not in the layout");
                continue;
            }
            VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            w.dstSet = rec.set;
            w.dstBinding = e.binding;
            w.dstArrayElement = e.arrayIndex;
            w.descriptorCount = 1;
            switch (le->type) {
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
                    br.want = le->type == BindingType::StorageBufferRW ? State::ShaderWrite
                              : le->type == BindingType::StorageBuffer ? State::ShaderRead
                                                                       : State::UniformBuffer;
                    rec.bound.push_back(br);
                }
                bufferInfos.push_back(
                    {b->buffer, e.buffer.offset, e.buffer.size ? e.buffer.size : VK_WHOLE_SIZE});
                w.pBufferInfo = &bufferInfos.back();
                w.descriptorType = le->type == BindingType::UniformBuffer ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                                   : le->type == BindingType::UniformBufferDynamic
                                       ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC
                                       : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
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
                    br.want = le->type == BindingType::StorageTextureRW ? State::ShaderWrite
                                                                        : State::DepthReadSampled;
                    rec.bound.push_back(br);
                }
                const TextureDim dim =
                    e.texture.viewDim != TextureDim::Tex2D ? e.texture.viewDim : le->viewDim;
                VkImageView view = View(*t, e.texture.range, dim, e.texture.format, e.texture.stencil);
                const bool depth = (t->aspect & VK_IMAGE_ASPECT_DEPTH_BIT) != 0;
                const bool storage = le->type == BindingType::StorageTextureRW;
                imageInfos.push_back({VK_NULL_HANDLE, view,
                                      storage ? VK_IMAGE_LAYOUT_GENERAL
                                              : (depth ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
                                                       : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)});
                w.pImageInfo = &imageInfos.back();
                w.descriptorType =
                    storage ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
                break;
            }
            case BindingType::Sampler: {
                SamplerRec* s = samplers.Get(e.sampler);
                if (!s) {
                    ReportError("CreateBindGroup: binding " + std::to_string(e.binding) + " has no sampler");
                    continue;
                }
                imageInfos.push_back({s->sampler, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED});
                w.pImageInfo = &imageInfos.back();
                w.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
                break;
            }
            }
            writes.push_back(w);
        }
        // pointers into the info vectors were taken after reserve, so they stay valid
        fn.vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
        liveGroups++;

        BindGroupHandle h = bindGroups.Alloc(std::move(rec));
        if (desc.perFrame) {
            std::lock_guard lock(descriptorMutex);
            frameDescriptors[slot].groups.push_back(h);
        }
        return h;
    }

    PipelineLayoutHandle Device::CreatePipelineLayout(const PipelineLayoutDesc& desc) {
        PipelineLayoutRec rec;
        rec.sets = desc.sets;
        std::vector<VkDescriptorSetLayout> sets;
        uint32_t used = 0;
        for (uint32_t i = 0; i < kMaxBindGroups; i++)
            if (desc.sets[i])
                used = i + 1;
        for (uint32_t i = 0; i < used; i++) {
            BindLayoutRec* l = bindLayouts.Get(desc.sets[i]);
            sets.push_back(l ? l->layout : emptySetLayout);
        }
        VkPushConstantRange pcr{};
        VkPipelineLayoutCreateInfo li{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        li.setLayoutCount = used;
        li.pSetLayouts = sets.data();
        if (desc.pushConstantBytes) {
            pcr.stageFlags = ToVk(desc.pushConstantStages);
            pcr.size = desc.pushConstantBytes;
            li.pushConstantRangeCount = 1;
            li.pPushConstantRanges = &pcr;
            rec.pushStages = pcr.stageFlags;
            rec.pushBytes = desc.pushConstantBytes;
        }
        if (fn.vkCreatePipelineLayout(device, &li, nullptr, &rec.layout) != VK_SUCCESS) {
            ReportError("vkCreatePipelineLayout failed");
            return {};
        }
        NameObject(VK_OBJECT_TYPE_PIPELINE_LAYOUT, reinterpret_cast<uint64_t>(rec.layout), desc.name);
        return pipelineLayouts.Alloc(rec);
    }

    PipelineHandle Device::CreateGraphicsPipeline(const GraphicsPipelineDesc& desc) {
        PipelineLayoutRec* layout = pipelineLayouts.Get(desc.layout);
        ShaderRec* vs = shaders.Get(desc.vertex);
        ShaderRec* fs = shaders.Get(desc.fragment);
        if (!layout || !vs) {
            ReportError("CreateGraphicsPipeline: missing layout or vertex shader");
            return {};
        }
        VkPipelineShaderStageCreateInfo stages[2]{};
        uint32_t stageCount = 0;
        stages[stageCount++] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                                nullptr,
                                0,
                                VK_SHADER_STAGE_VERTEX_BIT,
                                vs->module,
                                vs->entry.c_str(),
                                nullptr};
        if (fs)
            stages[stageCount++] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                                    nullptr,
                                    0,
                                    VK_SHADER_STAGE_FRAGMENT_BIT,
                                    fs->module,
                                    fs->entry.c_str(),
                                    nullptr};

        VkVertexInputBindingDescription bindings[kMaxVertexStreams];
        for (uint32_t i = 0; i < desc.streamCount; i++)
            bindings[i] = {i, desc.streams[i].stride,
                           desc.streams[i].step == VertexStep::Instance ? VK_VERTEX_INPUT_RATE_INSTANCE
                                                                        : VK_VERTEX_INPUT_RATE_VERTEX};
        VkVertexInputAttributeDescription attrs[kMaxVertexAttributes];
        for (uint32_t i = 0; i < desc.attributeCount; i++)
            attrs[i] = {desc.attributes[i].location, desc.attributes[i].stream,
                        BaseFormat(desc.attributes[i].format), desc.attributes[i].offset};
        VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        vi.vertexBindingDescriptionCount = desc.streamCount;
        vi.pVertexBindingDescriptions = bindings;
        vi.vertexAttributeDescriptionCount = desc.attributeCount;
        vi.pVertexAttributeDescriptions = attrs;

        VkPipelineInputAssemblyStateCreateInfo ia{
            VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        switch (desc.topology) {
        case Topology::TriangleList:
            ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            break;
        case Topology::TriangleStrip:
            ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
            break;
        case Topology::LineList:
            ia.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
            break;
        case Topology::PointList:
            ia.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
            break;
        }

        VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        vp.viewportCount = vp.scissorCount = 1;

        VkPipelineRasterizationDepthClipStateCreateInfoEXT clip{
            VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_DEPTH_CLIP_STATE_CREATE_INFO_EXT};
        VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        rs.polygonMode = desc.raster.wireframe ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
        rs.cullMode = desc.raster.cull == CullMode::None   ? VK_CULL_MODE_NONE
                      : desc.raster.cull == CullMode::Back ? VK_CULL_MODE_BACK_BIT
                                                           : VK_CULL_MODE_FRONT_BIT;
        rs.frontFace = desc.raster.frontFace == FrontFace::CounterClockwise ? VK_FRONT_FACE_COUNTER_CLOCKWISE
                                                                            : VK_FRONT_FACE_CLOCKWISE;
        rs.lineWidth = 1.0f;
        rs.depthBiasEnable = desc.raster.depthBiasConstant != 0.0f || desc.raster.depthBiasSlope != 0.0f;
        rs.depthBiasConstantFactor = desc.raster.depthBiasConstant;
        rs.depthBiasSlopeFactor = desc.raster.depthBiasSlope;
        rs.depthBiasClamp = desc.raster.depthBiasClamp;
        if (!desc.raster.depthClip) {
            if (depthClipEnable) {
                clip.depthClipEnable = VK_FALSE;
                rs.pNext = &clip;
            } else {
                rs.depthClampEnable = VK_TRUE;
            }
        }

        VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        ms.rasterizationSamples = SamplesOf(desc.samples);
        ms.alphaToCoverageEnable = desc.blend.alphaToCoverage;

        VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        ds.depthTestEnable = desc.depthStencil.depthTest;
        ds.depthWriteEnable = desc.depthStencil.depthWrite;
        ds.depthCompareOp = ToVk(desc.depthStencil.depthCompare);
        ds.stencilTestEnable = desc.depthStencil.stencilTest;
        auto face = [&](const StencilFace& f) {
            VkStencilOpState s{};
            s.failOp = ToVk(f.fail);
            s.depthFailOp = ToVk(f.depthFail);
            s.passOp = ToVk(f.pass);
            s.compareOp = ToVk(f.compare);
            s.compareMask = desc.depthStencil.stencilReadMask;
            s.writeMask = desc.depthStencil.stencilWriteMask;
            return s;
        };
        ds.front = face(desc.depthStencil.front);
        ds.back = face(desc.depthStencil.back);

        VkPipelineColorBlendAttachmentState blendStates[kMaxColorTargets];
        for (uint32_t i = 0; i < desc.colorCount; i++) {
            const BlendTarget& t = desc.blend.targets[i];
            VkPipelineColorBlendAttachmentState& s = blendStates[i];
            s = {};
            s.blendEnable = t.enable;
            s.srcColorBlendFactor = ToVk(t.srcColor);
            s.dstColorBlendFactor = ToVk(t.dstColor);
            s.colorBlendOp = ToVk(t.colorOp);
            s.srcAlphaBlendFactor = ToVk(t.srcAlpha);
            s.dstAlphaBlendFactor = ToVk(t.dstAlpha);
            s.alphaBlendOp = ToVk(t.alphaOp);
            s.colorWriteMask = (Has(t.writeMask, ColorMask::R) ? VK_COLOR_COMPONENT_R_BIT : 0) |
                               (Has(t.writeMask, ColorMask::G) ? VK_COLOR_COMPONENT_G_BIT : 0) |
                               (Has(t.writeMask, ColorMask::B) ? VK_COLOR_COMPONENT_B_BIT : 0) |
                               (Has(t.writeMask, ColorMask::A) ? VK_COLOR_COMPONENT_A_BIT : 0);
        }
        VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        cb.attachmentCount = desc.colorCount;
        cb.pAttachments = blendStates;

        const VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR,
                                                VK_DYNAMIC_STATE_STENCIL_REFERENCE};
        VkPipelineDynamicStateCreateInfo dyn{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dyn.dynamicStateCount = static_cast<uint32_t>(std::size(dynamicStates));
        dyn.pDynamicStates = dynamicStates;

        VkFormat colorFormats[kMaxColorTargets];
        for (uint32_t i = 0; i < desc.colorCount; i++)
            colorFormats[i] = FormatToVk(desc.colorFormats[i]);
        VkPipelineRenderingCreateInfo ri{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
        ri.colorAttachmentCount = desc.colorCount;
        ri.pColorAttachmentFormats = colorFormats;
        if (desc.depthFormat != Format::Unknown) {
            ri.depthAttachmentFormat = FormatToVk(desc.depthFormat);
            if (HasStencil(desc.depthFormat))
                ri.stencilAttachmentFormat = ri.depthAttachmentFormat;
        }

        VkGraphicsPipelineCreateInfo pi{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        pi.pNext = &ri;
        pi.stageCount = stageCount;
        pi.pStages = stages;
        pi.pVertexInputState = &vi;
        pi.pInputAssemblyState = &ia;
        pi.pViewportState = &vp;
        pi.pRasterizationState = &rs;
        pi.pMultisampleState = &ms;
        pi.pDepthStencilState = &ds;
        pi.pColorBlendState = &cb;
        pi.pDynamicState = &dyn;
        pi.layout = layout->layout;

        PipelineRec rec;
        if (fn.vkCreateGraphicsPipelines(device, pipelineCache, 1, &pi, nullptr, &rec.pipeline) !=
            VK_SUCCESS) {
            ReportError("vkCreateGraphicsPipelines failed for '" + std::string(desc.name ? desc.name : "") +
                        "'");
            return {};
        }
        NameObject(VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<uint64_t>(rec.pipeline), desc.name);
        rec.bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        rec.layoutHandle = desc.layout;
        rec.layout = layout->layout;
        rec.pushStages = layout->pushStages;
        rec.pushBytes = layout->pushBytes;
        rec.colorCount = desc.colorCount;
        rec.colorFormats = desc.colorFormats;
        rec.depthFormat = desc.depthFormat;
        rec.streamCount = desc.streamCount;
        livePipelines++;
        return pipelines.Alloc(rec);
    }

    PipelineHandle Device::CreateComputePipeline(const ComputePipelineDesc& desc) {
        PipelineLayoutRec* layout = pipelineLayouts.Get(desc.layout);
        ShaderRec* cs = shaders.Get(desc.compute);
        if (!layout || !cs) {
            ReportError("CreateComputePipeline: missing layout or shader");
            return {};
        }
        VkComputePipelineCreateInfo pi{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        pi.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                    nullptr,
                    0,
                    VK_SHADER_STAGE_COMPUTE_BIT,
                    cs->module,
                    cs->entry.c_str(),
                    nullptr};
        pi.layout = layout->layout;
        PipelineRec rec;
        if (fn.vkCreateComputePipelines(device, pipelineCache, 1, &pi, nullptr, &rec.pipeline) !=
            VK_SUCCESS) {
            ReportError("vkCreateComputePipelines failed");
            return {};
        }
        NameObject(VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<uint64_t>(rec.pipeline), desc.name);
        rec.bindPoint = VK_PIPELINE_BIND_POINT_COMPUTE;
        rec.compute = true;
        rec.layoutHandle = desc.layout;
        rec.layout = layout->layout;
        rec.pushStages = layout->pushStages;
        rec.pushBytes = layout->pushBytes;
        livePipelines++;
        return pipelines.Alloc(rec);
    }

    QuerySetHandle Device::CreateQuerySet(const QuerySetDesc& desc) {
        VkQueryPoolCreateInfo qi{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
        qi.queryType = VK_QUERY_TYPE_TIMESTAMP;
        qi.queryCount = desc.count;
        QueryRec rec;
        rec.count = desc.count;
        if (fn.vkCreateQueryPool(device, &qi, nullptr, &rec.pool) != VK_SUCCESS) {
            ReportError("vkCreateQueryPool failed");
            return {};
        }
        if (hostQueryReset)
            fn.vkResetQueryPool(device, rec.pool, 0, desc.count);
        return queries.Alloc(rec);
    }

    void Device::Destroy(BufferHandle h) {
        BufferRec rec;
        if (!buffers.Free(h, &rec))
            return;
        bufferBytes -= static_cast<int64_t>(rec.desc.size);
        liveBuffers--;
        if (useValidator)
            validator.OnDestroy(h);
        Retire([this, rec] { vmaDestroyBuffer(allocator, rec.buffer, rec.alloc); });
    }

    void Device::Destroy(TextureHandle h) {
        TextureRec rec;
        if (!textures.Free(h, &rec))
            return;
        liveTextures--;
        if (useValidator)
            validator.OnDestroy(h);
        Retire([this, rec = std::move(rec)] {
            for (auto& [key, view] : rec.views)
                fn.vkDestroyImageView(device, view, nullptr);
            if (!rec.external)
                vmaDestroyImage(allocator, rec.image, rec.alloc);
        });
    }

    // samplers are cached and shared by description: they live until the device does
    void Device::Destroy(SamplerHandle) {}

    void Device::Destroy(ShaderHandle h) {
        ShaderRec rec;
        if (!shaders.Free(h, &rec))
            return;
        // pipelines copy what they need at creation, so the module can go as soon as the gpu is past any use
        Retire([this, m = rec.module] { fn.vkDestroyShaderModule(device, m, nullptr); });
    }

    void Device::Destroy(BindLayoutHandle h) {
        BindLayoutRec rec;
        if (!bindLayouts.Free(h, &rec))
            return;
        Retire([this, l = rec.layout] { fn.vkDestroyDescriptorSetLayout(device, l, nullptr); });
    }

    void Device::Destroy(BindGroupHandle h) {
        BindGroupRec* g = bindGroups.Get(h);
        if (!g || g->perFrame)
            return;
        BindGroupRec rec;
        bindGroups.Free(h, &rec);
        liveGroups--;
        Retire([this, rec] {
            std::lock_guard lock(descriptorMutex);
            fn.vkFreeDescriptorSets(device, rec.pool, 1, &rec.set);
        });
    }

    void Device::Destroy(PipelineLayoutHandle h) {
        PipelineLayoutRec rec;
        if (!pipelineLayouts.Free(h, &rec))
            return;
        Retire([this, l = rec.layout] { fn.vkDestroyPipelineLayout(device, l, nullptr); });
    }

    void Device::Destroy(PipelineHandle h) {
        PipelineRec rec;
        if (!pipelines.Free(h, &rec))
            return;
        livePipelines--;
        Retire([this, p = rec.pipeline] { fn.vkDestroyPipeline(device, p, nullptr); });
    }

    void Device::Destroy(QuerySetHandle h) {
        QueryRec rec;
        if (!queries.Free(h, &rec))
            return;
        Retire([this, p = rec.pool] { fn.vkDestroyQueryPool(device, p, nullptr); });
    }

    void Device::Destroy(FenceHandle h) {
        FenceRec rec;
        if (!fences.Free(h, &rec))
            return;
        Retire([this, s = rec.semaphore] { fn.vkDestroySemaphore(device, s, nullptr); });
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

    void Device::Flush(BufferHandle h, uint64_t offset, uint64_t size) {
        BufferRec* r = buffers.Get(h);
        if (r && !r->coherent)
            vmaFlushAllocation(allocator, r->alloc, offset, size ? size : VK_WHOLE_SIZE);
    }

    void Device::Invalidate(BufferHandle h, uint64_t offset, uint64_t size) {
        BufferRec* r = buffers.Get(h);
        if (r && !r->coherent)
            vmaInvalidateAllocation(allocator, r->alloc, offset, size ? size : VK_WHOLE_SIZE);
    }

    uint64_t Device::GpuAddress(BufferHandle h) const {
        BufferRec* r = buffers.Get(h);
        return r ? r->address : 0;
    }

    FenceHandle Device::CreateFence(uint64_t initial) {
        VkSemaphoreTypeCreateInfo type{VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
        type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
        type.initialValue = initial;
        VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, &type};
        FenceRec rec;
        if (fn.vkCreateSemaphore(device, &si, nullptr, &rec.semaphore) != VK_SUCCESS)
            return {};
        return fences.Alloc(rec);
    }

    uint64_t Device::FenceValue(FenceHandle h) {
        FenceRec* f = fences.Get(h);
        uint64_t value = 0;
        if (f)
            fn.vkGetSemaphoreCounterValue(device, f->semaphore, &value);
        return value;
    }

    bool Device::WaitFence(FenceHandle h, uint64_t value, uint64_t timeoutNs) {
        FenceRec* f = fences.Get(h);
        if (!f)
            return false;
        VkSemaphoreWaitInfo wi{VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO};
        wi.semaphoreCount = 1;
        wi.pSemaphores = &f->semaphore;
        wi.pValues = &value;
        return fn.vkWaitSemaphores(device, &wi, timeoutNs) == VK_SUCCESS;
    }

    void Device::SignalFence(FenceHandle h, uint64_t value) {
        FenceRec* f = fences.Get(h);
        if (!f)
            return;
        VkSemaphoreSignalInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO};
        si.semaphore = f->semaphore;
        si.value = value;
        fn.vkSignalSemaphore(device, &si);
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

    CommandList* Device::AcquireCommandList(QueueType queue) {
        ThreadPools& tp = PoolsForThisThread();
        const uint32_t s = slot;
        auto& per = tp.slots[s][static_cast<size_t>(queue)];
        if (!per.pool) {
            VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
            pi.queueFamilyIndex = queues[static_cast<size_t>(queue)].family;
            pi.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
            if (fn.vkCreateCommandPool(device, &pi, nullptr, &per.pool) != VK_SUCCESS) {
                ReportError("vkCreateCommandPool failed");
                return nullptr;
            }
        }
        if (per.used == per.lists.size()) {
            VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
            ai.commandPool = per.pool;
            ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            ai.commandBufferCount = 1;
            VkCommandBuffer cmd = VK_NULL_HANDLE;
            if (fn.vkAllocateCommandBuffers(device, &ai, &cmd) != VK_SUCCESS) {
                ReportError("vkAllocateCommandBuffers failed");
                return nullptr;
            }
            per.lists.push_back(std::make_unique<CmdList>(*this, queue, cmd));
        }
        CmdList* list = per.lists[per.used++].get();
        list->Begin();
        return list;
    }

    void Device::Submit(QueueType queueType, const SubmitDesc& desc) {
        QueueInfo& q = queues[static_cast<size_t>(queueType)];
        std::vector<VkCommandBufferSubmitInfo> cmds;
        cmds.reserve(desc.lists.size());
        for (CommandList* list : desc.lists) {
            auto* cmdList = static_cast<CmdList*>(list);
            if (cmdList->openLabels != 0)
                ReportError("command list submitted with an open debug label");
            cmdList->End();
            VkCommandBufferSubmitInfo ci{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
            ci.commandBuffer = cmdList->Cmd();
            cmds.push_back(ci);
        }

        std::vector<VkSemaphoreSubmitInfo> waits, signals;
        auto semInfo = [](VkSemaphore s, uint64_t value) {
            VkSemaphoreSubmitInfo i{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
            i.semaphore = s;
            i.value = value;
            i.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            return i;
        };
        for (const FenceOp& w : desc.waits)
            if (FenceRec* f = fences.Get(w.fence))
                waits.push_back(semInfo(f->semaphore, w.value));
        if (queueType == QueueType::Graphics)
            if (VkSemaphore acquire = pendingAcquire.exchange(VK_NULL_HANDLE))
                waits.push_back(semInfo(acquire, 0));
        for (const FenceOp& s : desc.signals)
            if (FenceRec* f = fences.Get(s.fence))
                signals.push_back(semInfo(f->semaphore, s.value));

        std::lock_guard lock(*q.mutex);
        const uint64_t serial = q.serial.load() + 1;
        signals.push_back(semInfo(q.timeline, serial));

        VkSubmitInfo2 si{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
        si.waitSemaphoreInfoCount = static_cast<uint32_t>(waits.size());
        si.pWaitSemaphoreInfos = waits.data();
        si.commandBufferInfoCount = static_cast<uint32_t>(cmds.size());
        si.pCommandBufferInfos = cmds.data();
        si.signalSemaphoreInfoCount = static_cast<uint32_t>(signals.size());
        si.pSignalSemaphoreInfos = signals.data();
        const VkResult r = fn.vkQueueSubmit2(q.queue, 1, &si, VK_NULL_HANDLE);
        if (r != VK_SUCCESS) {
            ReportError("vkQueueSubmit2 failed (" + std::to_string(r) + ")");
            lost = (r == VK_ERROR_DEVICE_LOST);
            return;
        }
        q.serial.store(serial);
        submitCount++;
    }

    // empty submit that signals the binary semaphore a present waits on; it covers every earlier graphics submit
    void Device::SubmitPresent(VkSemaphore renderDone) {
        QueueInfo& q = queues[0];
        VkSemaphoreSubmitInfo signals[2]{};
        signals[0] = {VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, nullptr, renderDone, 0,
                      VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,    0};
        VkSemaphoreSubmitInfo waits[1]{};
        uint32_t waitCount = 0;
        if (VkSemaphore acquire = pendingAcquire.exchange(VK_NULL_HANDLE)) {
            waits[waitCount++] = {VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, nullptr, acquire, 0,
                                  VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,    0};
        }
        std::lock_guard lock(*q.mutex);
        const uint64_t serial = q.serial.load() + 1;
        signals[1] = {VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, nullptr, q.timeline, serial,
                      VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,    0};
        VkSubmitInfo2 si{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
        si.waitSemaphoreInfoCount = waitCount;
        si.pWaitSemaphoreInfos = waits;
        si.signalSemaphoreInfoCount = 2;
        si.pSignalSemaphoreInfos = signals;
        if (fn.vkQueueSubmit2(q.queue, 1, &si, VK_NULL_HANDLE) == VK_SUCCESS)
            q.serial.store(serial);
        else
            ReportError("present submit failed");
    }

    std::unique_ptr<rhi::Swapchain> Device::CreateSwapchain(const SwapchainDesc& desc) {
        return MakeSwapchain(*this, desc);
    }

    void Device::ResetSlot(uint32_t s) {
        {
            std::lock_guard lock(poolsMutex);
            for (auto& [id, tp] : threadPools)
                for (auto& per : tp->slots[s]) {
                    if (per.pool)
                        fn.vkResetCommandPool(device, per.pool, 0);
                    per.used = 0;
                }
        }
        std::lock_guard lock(descriptorMutex);
        FrameDescriptors& fd = frameDescriptors[s];
        for (VkDescriptorPool p : fd.pools)
            fn.vkResetDescriptorPool(device, p, 0);
        fd.used = 0;
        for (BindGroupHandle g : fd.groups) {
            if (bindGroups.Free(g))
                liveGroups--;
        }
        fd.groups.clear();
    }

    void Device::BeginFrame() {
        frame++;
        slot = static_cast<uint32_t>(frame % options.framesInFlight);

        const QueueSerials& need = slotSerials[slot];
        VkSemaphore sems[kQueueCount];
        uint64_t values[kQueueCount];
        uint32_t n = 0;
        for (size_t i = 0; i < kQueueCount; i++)
            if (need[i]) {
                sems[n] = queues[i].timeline;
                values[n] = need[i];
                n++;
            }
        if (n) {
            VkSemaphoreWaitInfo wi{VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO};
            wi.semaphoreCount = n;
            wi.pSemaphores = sems;
            wi.pValues = values;
            // a slow frame (software rasterisers, a driver compiling a huge pipeline) can take longer than any
            // fixed timeout; reusing its resources early would corrupt the frame still in flight, so keep waiting
            // until it finishes or the device is lost
            VkResult r = fn.vkWaitSemaphores(device, &wi, 10'000'000'000ull);
            while (r == VK_TIMEOUT)
                r = fn.vkWaitSemaphores(device, &wi, 10'000'000'000ull);
            if (r != VK_SUCCESS)
                ReportError("frame wait did not complete (" + std::to_string(r) + ")");
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
        fn.vkDeviceWaitIdle(device);
        retire.Collect(Completed());
    }

    MemoryBudget Device::QueryMemoryBudget() {
        MemoryBudget out;
        VmaBudget budgets[VK_MAX_MEMORY_HEAPS]{};
        vmaGetHeapBudgets(allocator, budgets);
        for (uint32_t i = 0; i < memProps.memoryHeapCount; i++) {
            const bool local = (memProps.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0;
            (local ? out.deviceBudget : out.hostBudget) += budgets[i].budget;
            (local ? out.deviceUsage : out.hostUsage) += budgets[i].usage;
        }
        out.valid = true;
        return out;
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
        size_t size = 0;
        if (!pipelineCache ||
            fn.vkGetPipelineCacheData(device, pipelineCache, &size, nullptr) != VK_SUCCESS || !size)
            return {};
        std::vector<uint8_t> data(size);
        if (fn.vkGetPipelineCacheData(device, pipelineCache, &size, data.data()) != VK_SUCCESS)
            return {};
        data.resize(size);
        return data;
    }

    // pending validation-layer, rhi and state-tracker messages; reading clears them
    std::string Device::DebugReport() {
        std::string out;
        {
            std::lock_guard lock(inst->messageMutex);
            for (auto& m : inst->messages)
                out += m + "\n";
            inst->messages.clear();
            inst->errorCount = 0;
        }
        if (useValidator)
            for (auto& m : validator.TakeErrors())
                out += "state: " + m + "\n";
        return out;
    }
}
