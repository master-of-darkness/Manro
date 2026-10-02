#include "VoxelRenderer.h"
#include "VoxelWorld.h"
#include "VoxelStreamWorld.h"
#include "../Vulkan/VulkanContext.h"
#include "../Vulkan/VulkanHelpers.h"
#include "../Vulkan/Pipeline.h"
#include "../Vulkan/PipelineCache.h"
#include "../Vulkan/Buffer.h"

#include <Manro/Core/Logger.h>
#include <Manro/Core/VirtualFS.h>
#include <algorithm>
#include <cmath>
#include <vector>

namespace Manro {
    CVoxelRenderer::CVoxelRenderer(CVulkanContext &ctx, CVirtualFS &vfs)
        : m_Context(ctx), m_Vfs(vfs) {
        m_World = CreateScope<CVoxelWorld>(ctx);
    }

    CVoxelRenderer::~CVoxelRenderer() { Shutdown(); }

    void CVoxelRenderer::Init(CPipelineCache &cache, u32 width, u32 height) {
        VoxelWorldDesc_t desc{};
        desc.virtualDim = 64;
        desc.voxelSize = 1.f;
        desc.maxResidentBricks = 8192;
        m_World->Init(desc);

        // Tile descriptor set layout must exist before the task/mesh PSO
        // bakes it into its pipeline layout.
        CreateTileDescriptor();
        BuildPipelines(cache);

        // Sorted visible ordinals (uint per brick), host-written every frame.
        // Ringed per flight slot (see header): sharing one buffer across
        // in-flight frames tears task reads.
        for (u32 i = 0; i < CVoxelRenderer::kFlightSlots; ++i) {
            m_VisibilityRing[i] = CreateScope<CBuffer>(
                m_Context, sizeof(u32) * 2 * desc.maxResidentBricks,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                VMA_MEMORY_USAGE_CPU_TO_GPU);
        }
        // VkDispatchIndirectCommand layout for pass-2 re-dispatch.
        m_TaskIndirectBuffer = CreateScope<CBuffer>(
            m_Context, sizeof(VkDispatchIndirectCommand),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
            VMA_MEMORY_USAGE_CPU_TO_GPU);
        m_TaskCountBuffer = CreateScope<CBuffer>(
            m_Context, sizeof(u32),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
                VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY);
        m_PaletteBuffer = CreateScope<CBuffer>(
            m_Context, sizeof(Vec4) * 512,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VMA_MEMORY_USAGE_CPU_TO_GPU);
        m_SunBuffer = CreateScope<CBuffer>(
            m_Context, sizeof(Vec4) * 2,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VMA_MEMORY_USAGE_CPU_TO_GPU);
        const u32 cascadeRes = 64;
        m_CascadeBuffer = CreateScope<CBuffer>(
            m_Context, sizeof(Vec4) * cascadeRes * cascadeRes * cascadeRes * 3,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY);
        m_EditStaging = CreateScope<CBuffer>(
            m_Context, sizeof(VoxelEditCmd_t) * 1024,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VMA_MEMORY_USAGE_CPU_TO_GPU);
        m_FrameParamsRing[0] = CreateScope<CBuffer>(
            m_Context, sizeof(VoxelFrameParams_t),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VMA_MEMORY_USAGE_CPU_TO_GPU);
        for (u32 i = 1; i < CVoxelRenderer::kFlightSlots; ++i) {
            m_FrameParamsRing[i] = CreateScope<CBuffer>(
                m_Context, sizeof(VoxelFrameParams_t),
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                VMA_MEMORY_USAGE_CPU_TO_GPU);
        }
        // Face cache: uint[2049] per resident brick (count + packed faces).
        // Written by task on first sight / edits, read by task (count) +
        // mesh (faces) every frame. 8192 bricks x 2049 x 4B = 64MB GPU-only.
        m_FaceCache = CreateScope<CBuffer>(
            m_Context, sizeof(u32) * 2049 * desc.maxResidentBricks,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY);
        // DEBUG counters: uint[8], device-cleared when capture is enabled,
        // atomically incremented by task/mesh. Host-visible for diagnosis.
        m_DebugReadback = CreateScope<CBuffer>(
            m_Context, sizeof(u32) * 8,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VMA_MEMORY_USAGE_CPU_TO_GPU);
        // Block tables: tile layer per (state, face) + flags per state.
        // Sized 32768 so any uint16 state id indexes safely. Host-written
        // once per StreamInit.
        m_BlockTileTable = CreateScope<CBuffer>(
            m_Context, sizeof(u32) * 32768 * 6,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VMA_MEMORY_USAGE_CPU_TO_GPU);
        m_BlockFlagsTable = CreateScope<CBuffer>(
            m_Context, sizeof(u32) * 32768,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VMA_MEMORY_USAGE_CPU_TO_GPU);
        // Fallback 1-magenta-tile array: the descriptor set is always valid,
        // even before StreamInit uploads the real pack.
        {
            BlockAssetPack_t fallback{};
            fallback.tiles.emplace_back(16 * 16 * 4, 0);
            for (int i = 0; i < 16 * 16; ++i) {
                fallback.tiles[0][i * 4 + 0] = 255;
                fallback.tiles[0][i * 4 + 2] = 255;
                fallback.tiles[0][i * 4 + 3] = 255;
            }
            fallback.tilePaths.emplace_back();
            fallback.states.resize(32768);
            CreateBlockTiles(fallback);
            UploadBlockTables();
        }

        // Sun dir normalized once here so the fragment shader (SM-bound at
        // fullscreen) can use it directly with no per-pixel normalize.
        // Intensity 1.0: block albedo is authored for ~1x daylight; the old
        // 3.0 blew sand/water to white through the tonemapper.
        Vec4 sun[2] = {Vec4(0.3f, -1.f, 0.2f, 0.f), Vec4(1.f, 0.98f, 0.9f, 1.f)};
        {
            const float len =
                std::sqrt(sun[0].x * sun[0].x + sun[0].y * sun[0].y + sun[0].z * sun[0].z);
            if (len > 1e-6f) {
                sun[0].x /= len;
                sun[0].y /= len;
                sun[0].z /= len;
            }
        }
        m_SunBuffer->LoadData(sun, sizeof(sun));

        // Distinct palette entries so the checkerboard is visible: 0 = warm grey,
        // 1 = teal. Remaining entries default mid-grey.
        {
            std::vector<Vec4> palette(512, Vec4(0.6f, 0.6f, 0.6f, 1.f));
            palette[0] = Vec4(0.85f, 0.25f, 0.15f, 1.f);
            palette[1] = Vec4(0.1f, 0.7f, 0.65f, 1.f);
            m_PaletteBuffer->LoadData(palette.data(), sizeof(Vec4) * palette.size());
        }

        m_bInitialized = true;
        LOG_INFO("[CVoxelRenderer] Initialized (sparse bricks, task/mesh PSO, {}x{})", width, height);
    }

    void CVoxelRenderer::Shutdown() {
        VkDevice device = m_Context.GetDevice();
        if (device) {
            vkDeviceWaitIdle(device);
        }
        DestroyBlockTiles();
        DestroyTileDescriptor();
        m_StreamWorld.reset();
        m_BlockTileTable.reset();
        m_BlockFlagsTable.reset();
        m_TaskMeshPipeline.reset();
        m_EditPipeline.reset();
        m_GiInjectPipeline.reset();
        m_GiPropagatePipeline.reset();
        for (auto &slot : m_VisibilityRing)
            slot.reset();
        m_TaskIndirectBuffer.reset();
        m_TaskCountBuffer.reset();
        m_PaletteBuffer.reset();
        m_SunBuffer.reset();
        m_CascadeBuffer.reset();
        m_EditStaging.reset();
        for (auto &slot : m_FrameParamsRing)
            slot.reset();
        m_DebugReadback.reset();
        m_FaceCache.reset();
        if (m_World)
            m_World->Shutdown();
        m_bInitialized = false;
    }

    void CVoxelRenderer::BuildPipelines(CPipelineCache &cache) {
        auto taskSpv = m_Vfs.ReadFile("shaders://voxel_task.task.spv");
        auto meshSpv = m_Vfs.ReadFile("shaders://voxel_mesh.mesh.spv");
        auto fragSpv = m_Vfs.ReadFile("shaders://voxel_shade.frag.spv");
        auto editSpv = m_Vfs.ReadFile("shaders://voxel_edit.comp.spv");
        auto giInjectSpv = m_Vfs.ReadFile("shaders://voxel_gi_inject.comp.spv");
        auto giPropSpv = m_Vfs.ReadFile("shaders://voxel_gi_propagate.comp.spv");

        if (taskSpv.empty() || meshSpv.empty())
            LOG_ERROR("[CVoxelRenderer] Voxel task/mesh shaders missing");
        if (fragSpv.empty())
            LOG_ERROR("[CVoxelRenderer] Voxel shade fragment missing");
        if (editSpv.empty() || giInjectSpv.empty() || giPropSpv.empty())
            LOG_ERROR("[CVoxelRenderer] Voxel compute shaders missing");

        PipelineConfigParams_t meshCfg{};
        meshCfg.vertexEntryPoint = "main";
        meshCfg.fragmentEntryPoint = "main";
        meshCfg.computeEntryPoint = "main";
        meshCfg.colorAttachmentFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
        meshCfg.depthAttachmentFormat = VK_FORMAT_D32_SFLOAT;
        meshCfg.msaaSamples = VK_SAMPLE_COUNT_1_BIT; // voxel path is MSAA-free
        meshCfg.pushConstantStages = VK_SHADER_STAGE_TASK_BIT_EXT | VK_SHADER_STAGE_MESH_BIT_EXT |
                                      VK_SHADER_STAGE_FRAGMENT_BIT;
        meshCfg.pushConstantSize = sizeof(VoxelFrameRoot_t);
        // Block tile array (fragment set 0): images can't travel by BDA.
        if (m_TileSetLayout != VK_NULL_HANDLE)
            meshCfg.descriptorSetLayouts = {m_TileSetLayout};
        meshCfg.depthWriteEnable = VK_TRUE;
        meshCfg.depthCompareOp = VK_COMPARE_OP_LESS;

        PipelineKey_t meshKey{};
        meshKey.taskHash = CPipelineCache::HashSpirV(taskSpv);
        meshKey.meshHash = CPipelineCache::HashSpirV(meshSpv);
        meshKey.fragHash = CPipelineCache::HashSpirV(fragSpv);
        meshKey.variants = PipelineVariant_MeshTask;
        meshKey.colorFmt = meshCfg.colorAttachmentFormat;
        meshKey.depthFmt = meshCfg.depthAttachmentFormat;
        meshKey.pushConstantSize = meshCfg.pushConstantSize;
        if (m_TileSetLayout != VK_NULL_HANDLE) {
            meshKey.setLayoutCount = 1;
            // Stable id for the single tile-array set (FNV-1a of
            // "voxel_tiles_v1", precomputed).
            meshKey.setLayoutHash = 0x7B9B2F4A8C1D3E55ull;
        }

        m_TaskMeshPipeline = CreateScope<CPipeline>(m_Context);
        cache.GetGraphics(meshKey, [&](VkPipelineCache) -> VkPipeline {
            m_TaskMeshPipeline->BuildMeshTask(taskSpv, meshSpv, fragSpv, meshCfg);
            return m_TaskMeshPipeline->GetHandle();
        });

        auto buildCompute = [&](Scope<CPipeline> &pipe, const std::vector<u8> &spv, u32 pushSize,
                                const char *name) {
            PipelineConfigParams_t cfg{};
            cfg.computeEntryPoint = "main";
            cfg.pushConstantSize = pushSize;
            PipelineKey_t key{};
            key.compHash = CPipelineCache::HashSpirV(spv);
            key.variants = PipelineVariant_Compute;
            key.pushConstantSize = pushSize;
            pipe = CreateScope<CPipeline>(m_Context);
            cache.GetCompute(key, [&](VkPipelineCache) -> VkPipeline {
                pipe->BuildCompute(spv, cfg);
                return pipe->GetHandle();
            });
            if (!pipe->GetHandle())
                LOG_ERROR("[CVoxelRenderer] Compute pipeline build failed: {}", name);
        };

        buildCompute(m_EditPipeline, editSpv, sizeof(VoxelEditPushConstants_t), "voxel_edit");
        buildCompute(m_GiInjectPipeline, giInjectSpv, sizeof(VoxelGiPushConstants_t), "voxel_gi_inject");
        buildCompute(m_GiPropagatePipeline, giPropSpv, sizeof(VoxelGiPushConstants_t), "voxel_gi_propagate");
    }

    void CVoxelRenderer::CreateTileDescriptor() {
        DestroyTileDescriptor();
        VkDevice device = m_Context.GetDevice();
        VkDescriptorSetLayoutBinding b{};
        b.binding = 0;
        b.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        b.descriptorCount = 1;
        b.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo li{};
        li.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        li.bindingCount = 1;
        li.pBindings = &b;
        if (vkCreateDescriptorSetLayout(device, &li, nullptr, &m_TileSetLayout) != VK_SUCCESS)
            throw std::runtime_error("[CVoxelRenderer] Tile set layout failed");
        VkDescriptorPoolSize ps{};
        ps.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        ps.descriptorCount = 1;
        VkDescriptorPoolCreateInfo pi{};
        pi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pi.maxSets = 1;
        pi.poolSizeCount = 1;
        pi.pPoolSizes = &ps;
        if (vkCreateDescriptorPool(device, &pi, nullptr, &m_TilePool) != VK_SUCCESS)
            throw std::runtime_error("[CVoxelRenderer] Tile pool failed");
        VkDescriptorSetAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool = m_TilePool;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts = &m_TileSetLayout;
        if (vkAllocateDescriptorSets(device, &ai, &m_TileSet) != VK_SUCCESS)
            throw std::runtime_error("[CVoxelRenderer] Tile set alloc failed");
    }

    void CVoxelRenderer::DestroyTileDescriptor() {
        VkDevice device = m_Context.GetDevice();
        if (!device)
            return;
        // Destroying the pool frees the set.
        m_TileSet = VK_NULL_HANDLE;
        if (m_TilePool) {
            vkDestroyDescriptorPool(device, m_TilePool, nullptr);
            m_TilePool = VK_NULL_HANDLE;
        }
        if (m_TileSetLayout) {
            vkDestroyDescriptorSetLayout(device, m_TileSetLayout, nullptr);
            m_TileSetLayout = VK_NULL_HANDLE;
        }
    }

    namespace {
        u32 FindDeviceLocalMemory(VkPhysicalDevice phys, u32 bits) {
            VkPhysicalDeviceMemoryProperties props{};
            vkGetPhysicalDeviceMemoryProperties(phys, &props);
            for (u32 i = 0; i < props.memoryTypeCount; ++i) {
                if ((bits & (1u << i)) &&
                    (props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
                    return i;
            }
            throw std::runtime_error("[CVoxelRenderer] No device-local memory type");
        }
    } // namespace

    void CVoxelRenderer::CreateBlockTiles(const BlockAssetPack_t &pack) {
        DestroyBlockTiles();
        VkDevice device = m_Context.GetDevice();
        const u32 layers = std::max<u32>(1u, static_cast<u32>(pack.tiles.size()));
        constexpr u32 kMips = 5; // 16 -> 1
        m_TileLayers = layers;

        VkImageCreateInfo ii{};
        ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ii.imageType = VK_IMAGE_TYPE_2D;
        ii.format = VK_FORMAT_R8G8B8A8_SRGB;
        ii.extent = {16, 16, 1};
        ii.mipLevels = kMips;
        ii.arrayLayers = layers;
        ii.samples = VK_SAMPLE_COUNT_1_BIT;
        ii.tiling = VK_IMAGE_TILING_OPTIMAL;
        ii.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vkCreateImage(device, &ii, nullptr, &m_TileImage) != VK_SUCCESS)
            throw std::runtime_error("[CVoxelRenderer] Tile image failed");
        VkMemoryRequirements req{};
        vkGetImageMemoryRequirements(device, m_TileImage, &req);
        VkMemoryAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize = req.size;
        ai.memoryTypeIndex = FindDeviceLocalMemory(m_Context.GetPhysicalDevice(), req.memoryTypeBits);
        if (vkAllocateMemory(device, &ai, nullptr, &m_TileMemory) != VK_SUCCESS)
            throw std::runtime_error("[CVoxelRenderer] Tile memory failed");
        vkBindImageMemory(device, m_TileImage, m_TileMemory, 0);

        // CPU mip chain (box filter in sRGB bytes — invisible at 16px, and
        // it avoids 5k GPU blits at init). One staging buffer, one copy.
        constexpr u32 kLevelPx[5] = {256, 64, 16, 4, 1};
        VkDeviceSize layerBytes = 0;
        for (u32 m = 0; m < kMips; ++m)
            layerBytes += static_cast<VkDeviceSize>(kLevelPx[m]) * 4u;
        std::vector<u8> staging(static_cast<size_t>(layerBytes) * layers);
        std::vector<u32> prev(256), cur(256);
        for (u32 l = 0; l < layers; ++l) {
            const u8 *src = (l < pack.tiles.size() && pack.tiles[l].size() >= 16 * 16 * 4)
                                ? pack.tiles[l].data()
                                : pack.tiles[0].data();
            for (int i = 0; i < 256; ++i)
                prev[static_cast<size_t>(i)] = static_cast<u32>(src[i * 4 + 0]) |
                                               (static_cast<u32>(src[i * 4 + 1]) << 8u) |
                                               (static_cast<u32>(src[i * 4 + 2]) << 16u) |
                                               (static_cast<u32>(src[i * 4 + 3]) << 24u);
            size_t dstOff = static_cast<size_t>(layerBytes) * l;
            u32 dim = 16;
            for (u32 m = 0; m < kMips; ++m) {
                for (u32 i = 0; i < dim * dim; ++i) {
                    const u32 p = prev[i];
                    staging[dstOff + i * 4 + 0] = static_cast<u8>(p & 0xFFu);
                    staging[dstOff + i * 4 + 1] = static_cast<u8>((p >> 8u) & 0xFFu);
                    staging[dstOff + i * 4 + 2] = static_cast<u8>((p >> 16u) & 0xFFu);
                    staging[dstOff + i * 4 + 3] = static_cast<u8>((p >> 24u) & 0xFFu);
                }
                dstOff += static_cast<size_t>(dim) * dim * 4u;
                if (m + 1u < kMips) {
                    const u32 nd = dim / 2u;
                    for (u32 y = 0; y < nd; ++y) {
                        for (u32 x = 0; x < nd; ++x) {
                            u32 acc[4] = {0, 0, 0, 0};
                            for (u32 dy = 0; dy < 2; ++dy) {
                                for (u32 dx = 0; dx < 2; ++dx) {
                                    const u32 p = prev[(y * 2u + dy) * dim + (x * 2u + dx)];
                                    acc[0] += p & 0xFFu;
                                    acc[1] += (p >> 8u) & 0xFFu;
                                    acc[2] += (p >> 16u) & 0xFFu;
                                    acc[3] += (p >> 24u) & 0xFFu;
                                }
                            }
                            cur[y * nd + x] = ((acc[0] + 2u) / 4u) | (((acc[1] + 2u) / 4u) << 8u) |
                                              (((acc[2] + 2u) / 4u) << 16u) |
                                              (((acc[3] + 2u) / 4u) << 24u);
                        }
                    }
                    prev = cur;
                    dim = nd;
                }
            }
        }
        CBuffer stageBuf(m_Context, staging.size(),
                         VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
        stageBuf.LoadData(staging.data(), staging.size());

        std::vector<VkBufferImageCopy> regions;
        regions.reserve(static_cast<size_t>(layers) * kMips);
        for (u32 l = 0; l < layers; ++l) {
            VkDeviceSize off = static_cast<VkDeviceSize>(layerBytes) * l;
            u32 dim = 16;
            for (u32 m = 0; m < kMips; ++m) {
                VkBufferImageCopy r{};
                r.bufferOffset = off;
                r.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, m, l, 1};
                r.imageExtent = {dim, dim, 1};
                regions.push_back(r);
                off += static_cast<VkDeviceSize>(dim) * dim * 4u;
                dim /= 2u;
            }
        }

        VkImageViewCreateInfo vi{};
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = m_TileImage;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        vi.format = VK_FORMAT_R8G8B8A8_SRGB;
        vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, kMips, 0, layers};
        if (vkCreateImageView(device, &vi, nullptr, &m_TileView) != VK_SUCCESS)
            throw std::runtime_error("[CVoxelRenderer] Tile view failed");

        VkSamplerCreateInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        si.magFilter = VK_FILTER_NEAREST;
        si.minFilter = VK_FILTER_LINEAR;
        si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.minLod = 0.f;
        si.maxLod = static_cast<float>(kMips - 1);
        si.maxAnisotropy = 1.f;
        if (vkCreateSampler(device, &si, nullptr, &m_TileSampler) != VK_SUCCESS)
            throw std::runtime_error("[CVoxelRenderer] Tile sampler failed");

        ExecuteOneShot(m_Context, [&](VkCommandBuffer cmd) {
            VkImageMemoryBarrier b0{};
            b0.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            b0.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            b0.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            b0.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b0.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b0.image = m_TileImage;
            b0.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, kMips, 0, layers};
            b0.srcAccessMask = 0;
            b0.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b0);
            vkCmdCopyBufferToImage(cmd, stageBuf.GetHandle(), m_TileImage,
                                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                   static_cast<u32>(regions.size()), regions.data());
            VkImageMemoryBarrier b1 = b0;
            b1.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            b1.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            b1.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            b1.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr,
                                 1, &b1);
        });

        VkDescriptorImageInfo ii2{};
        ii2.sampler = m_TileSampler;
        ii2.imageView = m_TileView;
        ii2.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkWriteDescriptorSet w{};
        w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w.dstSet = m_TileSet;
        w.dstBinding = 0;
        w.descriptorCount = 1;
        w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w.pImageInfo = &ii2;
        vkUpdateDescriptorSets(device, 1, &w, 0, nullptr);
        LOG_INFO("[CVoxelRenderer] Tile array: {} layers, {} mips", layers, kMips);
    }

    void CVoxelRenderer::DestroyBlockTiles() {
        VkDevice device = m_Context.GetDevice();
        if (!device)
            return;
        if (m_TileSampler) {
            vkDestroySampler(device, m_TileSampler, nullptr);
            m_TileSampler = VK_NULL_HANDLE;
        }
        if (m_TileView) {
            vkDestroyImageView(device, m_TileView, nullptr);
            m_TileView = VK_NULL_HANDLE;
        }
        if (m_TileImage) {
            vkDestroyImage(device, m_TileImage, nullptr);
            m_TileImage = VK_NULL_HANDLE;
        }
        if (m_TileMemory) {
            vkFreeMemory(device, m_TileMemory, nullptr);
            m_TileMemory = VK_NULL_HANDLE;
        }
        m_TileLayers = 0;
    }

    void CVoxelRenderer::UploadBlockTables() {
        std::vector<u32> tiles(32768u * 6u, 0u), flags(32768u, 0u);
        const size_t n = std::min(m_BlockPack.states.size(), static_cast<size_t>(32768u));
        for (size_t s = 0; s < n; ++s) {
            for (int f = 0; f < 6; ++f)
                tiles[s * 6u + static_cast<size_t>(f)] = m_BlockPack.states[s].faces.tile[f];
            flags[s] = m_BlockPack.states[s].flags;
        }
        m_BlockTileTable->LoadData(tiles.data(), tiles.size() * sizeof(u32));
        m_BlockFlagsTable->LoadData(flags.data(), flags.size() * sizeof(u32));
    }

    Vec3 CVoxelRenderer::StreamInit(const std::string &worldDir, const std::string &assetsDir,
                                int radiusSections) {
        // Recreating the tile array while flights may sample it: init-time
        // op, idle is correct and cheap here.
        VkDevice device = m_Context.GetDevice();
        if (device)
            vkDeviceWaitIdle(device);
        // Empty assetsDir falls back to the build-time client-jar assets.
        const std::string dir = assetsDir.empty() ? MANRO_MC_ASSETS_DIR : assetsDir;
        std::string err;
        if (!BuildBlockAssetPack(dir, worldDir, m_BlockPack, err)) {
            LOG_ERROR("[CVoxelRenderer] Block assets failed: {}", err);
            return Vec3(8.f, 80.f, 8.f);
        }
        if (m_BlockPack.maxState >= 32768u) {
            LOG_ERROR("[CVoxelRenderer] State id {} exceeds 15-bit face-cache pack", m_BlockPack.maxState);
            return Vec3(8.f, 80.f, 8.f);
        }
        CreateBlockTiles(m_BlockPack);
        UploadBlockTables();
        m_StreamWorld = std::make_unique<CVoxelStreamWorld>();
        VoxelStreamDesc_t desc{};
        desc.worldDir = worldDir;
        desc.radiusSections = radiusSections;
        return m_StreamWorld->Init(*m_World, m_BlockPack, desc);
    }

    int CVoxelRenderer::StreamUpdate(const Vec3 &cameraPos, u32 flightSlot) {
        if (!m_StreamWorld)
            return 0;
        return m_StreamWorld->Update(*m_World, cameraPos, flightSlot);
    }

    bool CVoxelRenderer::StreamIsSolid(const Vec3 &p) const {
        if (!m_StreamWorld)
            return true;
        return m_StreamWorld->IsSolidAt(static_cast<i64>(std::floor(p.x)),
                                    static_cast<i64>(std::floor(p.y)),
                                    static_cast<i64>(std::floor(p.z)));
    }

    bool CVoxelRenderer::StreamIsFluid(const Vec3 &p) const {
        if (!m_StreamWorld)
            return false;
        return m_StreamWorld->IsFluidAt(static_cast<i64>(std::floor(p.x)),
                                    static_cast<i64>(std::floor(p.y)),
                                    static_cast<i64>(std::floor(p.z)));
    }

    u32 CVoxelRenderer::StreamPlaceState() const {
        if (!m_StreamWorld)
            return 1u;
        const i32 st = m_StreamWorld->PlaceState();
        return st >= 0 ? static_cast<u32>(st) : 1u;
    }

    void CVoxelRenderer::StreamApplyEdit(const Vec3 &pos, float radius, u32 op) {
        if (m_StreamWorld)
            m_StreamWorld->ApplyEdit(pos, radius, op);
    }

    void CVoxelRenderer::DispatchEdits(VkCommandBuffer cb) {
        const auto &edits = m_World->GetPendingEdits();
        if (edits.empty() || !m_EditPipeline->GetHandle())
            return;
        m_EditStaging->LoadData(edits.data(), sizeof(VoxelEditCmd_t) * edits.size());

        VoxelEditPushConstants_t pc{};
        pc.brickBufferAddr = m_World->GetBrickBufferAddr();
        pc.headerAddr = m_World->GetHeaderAddr();
        pc.pageTableAddr = m_World->GetPageTableAddr();
        pc.editAddr = m_EditStaging->GetDeviceAddress();
        pc.worldMin = m_World->GetWorldMin();
        pc.brickSize = m_World->GetBrickSize();
        pc.editCount = static_cast<u32>(edits.size());
        pc.virtualDim = m_World->GetVirtualDim();

        // Host -> compute visibility for the edit ring.
        VkBufferMemoryBarrier2 barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_HOST_WRITE_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
        barrier.buffer = m_EditStaging->GetHandle();
        barrier.size = VK_WHOLE_SIZE;
        VkDependencyInfo dep{};
        dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dep.bufferMemoryBarrierCount = 1;
        dep.pBufferMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(cb, &dep);

        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, m_EditPipeline->GetHandle());
        vkCmdPushConstants(cb, m_EditPipeline->GetLayout(), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc),
                           &pc);
        vkCmdDispatch(cb, (pc.editCount + 63) / 64, 1, 1);

        // Compute writes (brick pages, headers) -> task/mesh/fragment reads.
        // Global memory barrier since the sparse brick store is not a CBuffer.
        VkMemoryBarrier2 post{};
        post.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
        post.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        post.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
        post.dstStageMask = VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT |
                            VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT |
                            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        post.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
        VkDependencyInfo postDep{};
        postDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        postDep.memoryBarrierCount = 1;
        postDep.pMemoryBarriers = &post;
        vkCmdPipelineBarrier2(cb, &postDep);
    }

    void CVoxelRenderer::DispatchGi(VkCommandBuffer cb) {
        // GI cascade volumes are stubs; skip both dispatches + barrier
        // unless explicitly enabled (saves 2x 8x8x8 dispatches/frame).
        if (!m_bGiEnabled)
            return;
        if (!m_GiInjectPipeline->GetHandle() || !m_GiPropagatePipeline->GetHandle())
            return;
        VoxelGiPushConstants_t pc{};
        pc.brickBufferAddr = m_World->GetBrickBufferAddr();
        pc.headerAddr = m_World->GetHeaderAddr();
        pc.cascadeAddr = m_CascadeBuffer->GetDeviceAddress();
        pc.sunAddr = m_SunBuffer->GetDeviceAddress();
        pc.brickCount = m_World->GetBrickCount();
        pc.cascadeRes = 64;
        pc.cascadeCount = 3;
        pc.brickSize = m_World->GetBrickSize();
        pc.worldMin = m_World->GetWorldMin();

        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, m_GiInjectPipeline->GetHandle());
        vkCmdPushConstants(cb, m_GiInjectPipeline->GetLayout(), VK_SHADER_STAGE_COMPUTE_BIT, 0,
                           sizeof(pc), &pc);
        vkCmdDispatch(cb, 8, 8, 8);

        VkMemoryBarrier2 mem{};
        mem.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
        mem.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        mem.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
        mem.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        mem.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
        VkDependencyInfo dep{};
        dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dep.memoryBarrierCount = 1;
        dep.pMemoryBarriers = &mem;
        vkCmdPipelineBarrier2(cb, &dep);

        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, m_GiPropagatePipeline->GetHandle());
        vkCmdPushConstants(cb, m_GiPropagatePipeline->GetLayout(), VK_SHADER_STAGE_COMPUTE_BIT, 0,
                           sizeof(pc), &pc);
        vkCmdDispatch(cb, 8, 8, 8);
    }

    void CVoxelRenderer::Record(VkCommandBuffer cb, VkExtent2D extent, VkImageView colorView,
                                VkImageView depthView, bool clearColor, u32 flightSlot,
                                const Mat4 &viewProj, const Mat4 &prevViewProj,
                                const Vec3 &cameraPos, float nearZ, float farZ) {
        if (!m_bInitialized)
            return;
        (void)nearZ;
        (void)farZ;

        // All compute + uploads happen OUTSIDE any render pass (VUID
        // vkCmdDispatch-None-10672 / vkCmdPipelineBarrier2-None-09553).
        m_World->FlushHeaders();
        DispatchEdits(cb);
        DispatchGi(cb);
        m_World->ClearEdits();
        // Staged streaming fills (buffered by StreamUpdate into this flight
        // slot): device copies + transfer barrier, in-frame, no extra sync.
        m_World->FlushStagedUploads(cb, flightSlot);

        const u32 brickCount = m_World->GetBrickCount();
        m_Stats.brickCount = brickCount;
        m_Stats.editCount = 0;
        if (brickCount == 0 || !m_TaskMeshPipeline->GetHandle())
            return;

        // CPU front-to-back sort + frustum compact (the proper overdraw cut):
        // visible bricks only, nearest first, so hardware ZCULL + early-z
        // kills hidden pixels instead of shading them. Dispatch gets the
        // exact visible count — no empty task groups.
        // - Hidden interior bricks (fully opaque + opaque neighbors) are
        //   skipped before dispatch: no task/mesh/raster cost for solid rock.
        // - Frustum test is 6-plane sphere vs the same viewProj the mesh
        //   shader rasterizes with (plane extraction is exact, unlike an
        //   8-corner NDC test it can never disagree per corner convention).
        u32 visibleCount = brickCount;
        // Visibility + params rings share the flight slot so host uploads
        // can never tear an in-flight frame's task/mesh reads.
        CBuffer &visSlot = *m_VisibilityRing[flightSlot % kFlightSlots];
        {
            const float brickSize = m_World->GetBrickSize();
            const float radius = brickSize * 0.8660254f; // half-diagonal
            const float maxD = 10000.f + radius;
            m_VisibleScratch.clear();
            if (m_bUseFrustum) {
                // Frustum planes from viewProj rows (glm column-major:
                // row r = (m[0][r], m[1][r], m[2][r], m[3][r])).
                Vec4 rows[4];
                for (int r = 0; r < 4; ++r)
                    rows[r] = Vec4(viewProj[0][r], viewProj[1][r], viewProj[2][r], viewProj[3][r]);
                Vec4 planes[6] = {rows[3] + rows[0], rows[3] - rows[0], rows[3] + rows[1],
                                  rows[3] - rows[1], rows[3] + rows[2], rows[3] - rows[2]};
                for (auto &pl : planes) {
                    const float len = std::sqrt(pl.x * pl.x + pl.y * pl.y + pl.z * pl.z);
                    if (len > 1e-6f) {
                        const float inv = 1.f / len;
                        pl.x *= inv;
                        pl.y *= inv;
                        pl.z *= inv;
                        pl.w *= inv;
                    }
                }
                const auto &mirror = m_World->GetHeaderMirror();
                for (u32 i = 0; i < brickCount; ++i) {
                    if (m_World->IsBrickHidden(i))
                        continue;
                    const VoxelBrickHeader_t &h = mirror[i];
                    if ((h.flags & 1u) == 0u)
                        continue;
                    const Vec3 center = h.origin + Vec3(brickSize * 0.5f);
                    const Vec3 toC = center - cameraPos;
                    const float dist2 = glm::dot(toC, toC);
                    if (dist2 > maxD * maxD)
                        continue;
                    bool inside = true;
                    for (const auto &pl : planes) {
                        const float d = pl.x * center.x + pl.y * center.y + pl.z * center.z + pl.w;
                        if (d < -radius) {
                            inside = false;
                            break;
                        }
                    }
                    if (!inside)
                        continue; // culled
                    m_VisibleScratch.emplace_back(dist2, i);
                }
                std::sort(m_VisibleScratch.begin(), m_VisibleScratch.end(),
                          [](const auto &a, const auto &b) { return a.first < b.first; });
                visibleCount = static_cast<u32>(m_VisibleScratch.size());
            } else {
                for (u32 i = 0; i < brickCount; ++i) {
                    if (m_World->IsBrickHidden(i))
                        continue;
                    m_VisibleScratch.emplace_back(0.f, i);
                }
                visibleCount = static_cast<u32>(m_VisibleScratch.size());
            }
            m_VisibleList.resize(m_VisibleScratch.size());
            for (size_t k = 0; k < m_VisibleScratch.size(); ++k)
                m_VisibleList[k] = m_VisibleScratch[k].second;
            if (!m_VisibleList.empty())
                visSlot.LoadData(m_VisibleList.data(), sizeof(u32) * m_VisibleList.size());
        }

        // Push constant is the frame root; frame params live in a BDA buffer.
        VoxelFrameParams_t frame{};
        frame.viewProj = viewProj;
        frame.prevViewProj = prevViewProj;
        frame.cameraPos = cameraPos;
        frame.brickCount = brickCount;
        frame.worldMin = m_World->GetWorldMin();
        frame.brickSize = m_World->GetBrickSize();
        frame.maxDrawDistance = 10000;
        frame.enableHiZ = 0; // reserved: software HiZ removed (ZCULL handles it)
        frame.paletteAddr = m_PaletteBuffer->GetDeviceAddress();
        frame.sunAddr = m_SunBuffer->GetDeviceAddress();
        frame.giAddr = 0; // GI stub adds light on black; v1 visibility = lambert only
        frame.giEnabled = m_bGiEnabled ? 1u : 0u;
        frame.shadowsEnabled = 0;
        frame.debugEnabled = m_bDebugEnabled ? 1u : 0u;
        frame.useBackface = m_bUseBackface ? 1u : 0u;
        frame.useFrustum = m_bUseFrustum ? 1u : 0u;
        frame.virtualDim = m_World->GetVirtualDim();
        CBuffer &paramsSlot = *m_FrameParamsRing[flightSlot % kFlightSlots];
        paramsSlot.LoadData(&frame, sizeof(frame));

        VoxelFrameRoot_t root{};
        root.brickBufferAddr = m_World->GetBrickBufferAddr();
        root.headerAddr = m_World->GetHeaderAddr();
        root.pageTableAddr = m_World->GetPageTableAddr();
        root.visibilityAddr = visSlot.GetDeviceAddress();
        root.frameAddr = paramsSlot.GetDeviceAddress();
        root.faceCacheAddr = m_FaceCache->GetDeviceAddress();
        root.tileTableAddr = m_BlockTileTable ? m_BlockTileTable->GetDeviceAddress() : 0u;
        root.blockFlagsAddr = m_BlockFlagsTable ? m_BlockFlagsTable->GetDeviceAddress() : 0u;
        // DEBUG counters: device-side clear only when capture is enabled.
        // Skipping the Fill + extra barrier saves a full-buffer op/frame.
        if (m_bDebugEnabled)
            vkCmdFillBuffer(cb, m_DebugReadback->GetHandle(), 0, sizeof(u32) * 8, 0);
        root.debugAddr = m_DebugReadback->GetDeviceAddress();

        // Frame params + visibility list (host uploads) -> task/mesh reads.
        // Downloaded buffers are read-only downstream: dst access is READ,
        // not READ|WRITE (the WRITE bit needlessly stalls the pipeline).
        // Debug-clear barrier appended only when capture is enabled. The
        // global memory barrier orders prior in-flight frames' shader
        // writes (face-cache stores, edit writes) before task reads.
        {
            VkBufferMemoryBarrier2 b[3]{};
            u32 barrierCount = 2;
            b[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
            b[0].srcStageMask = VK_PIPELINE_STAGE_2_HOST_BIT | VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            b[0].srcAccessMask = VK_ACCESS_2_HOST_WRITE_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT;
            b[0].dstStageMask = VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT |
                                VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT |
                                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            b[0].dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
            b[0].buffer = paramsSlot.GetHandle();
            b[0].size = VK_WHOLE_SIZE;
            b[1] = b[0];
            b[1].buffer = visSlot.GetHandle();
            if (m_bDebugEnabled) {
                b[2] = b[0];
                b[2].buffer = m_DebugReadback->GetHandle();
                barrierCount = 3;
            }
            VkDependencyInfo dep{};
            dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            dep.bufferMemoryBarrierCount = barrierCount;
            dep.pBufferMemoryBarriers = b;
            // Cross-frame visibility: prior frames' task/mesh/compute writes
            // (face cache, brick edits) -> this frame's task/mesh reads.
            VkMemoryBarrier2 cross{};
            cross.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
            cross.srcStageMask = VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT |
                                 VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT |
                                 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT |
                                 VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            cross.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT;
            cross.dstStageMask = VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT |
                                 VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT |
                                 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            cross.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
            dep.memoryBarrierCount = 1;
            dep.pMemoryBarriers = &cross;
            vkCmdPipelineBarrier2(cb, &dep);
        }

        // Own voxel-only dynamic-rendering pass (shares the Sponza
        // offscreen+depth IMAGES but never runs inside the PBR pass).
        // clearColor=true on the first voxel pass of the frame (nothing drew
        // before us); false when appending after PBR/skybox.
        VkRenderingAttachmentInfo colorAtt{};
        colorAtt.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colorAtt.imageView = colorView;
        colorAtt.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAtt.loadOp = clearColor ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
        colorAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAtt.clearValue.color = {{0.05f, 0.05f, 0.07f, 1.f}};

        VkRenderingAttachmentInfo depthAtt{};
        depthAtt.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        depthAtt.imageView = depthView;
        depthAtt.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depthAtt.loadOp = clearColor ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
        depthAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depthAtt.clearValue.depthStencil = {1.f, 0};

        VkRenderingInfo ri{};
        ri.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        ri.renderArea.extent = extent;
        ri.layerCount = 1;
        ri.colorAttachmentCount = 1;
        ri.pColorAttachments = &colorAtt;
        ri.pDepthAttachment = &depthAtt;
        vkCmdBeginRendering(cb, &ri);

        VkViewport vp{0.f, 0.f, static_cast<float>(extent.width), static_cast<float>(extent.height),
                      0.f, 1.f};
        vkCmdSetViewport(cb, 0, 1, &vp);
        VkRect2D sc{{0, 0}, extent};
        vkCmdSetScissor(cb, 0, 1, &sc);

        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, m_TaskMeshPipeline->GetHandle());
        vkCmdPushConstants(cb, m_TaskMeshPipeline->GetLayout(),
                           VK_SHADER_STAGE_TASK_BIT_EXT | VK_SHADER_STAGE_MESH_BIT_EXT |
                               VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(root), &root);
        // Block tile array (always valid: magenta fallback before StreamInit).
        if (m_TileSet != VK_NULL_HANDLE) {
            vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    m_TaskMeshPipeline->GetLayout(), 0, 1, &m_TileSet, 0, nullptr);
        }

        // Sorted front-to-back compact list: exact dispatch count, no empty
        // task groups, early-z order for hidden-pixel rejection.
        m_Stats.taskGroups = visibleCount;
        if (visibleCount > 0)
            vkCmdDrawMeshTasksEXT(cb, visibleCount, 1, 1);
        vkCmdEndRendering(cb);

        // DEBUG host barrier only when capture is enabled; otherwise skip
        // the extra pipeline stall entirely.
        if (m_bDebugEnabled) {
            VkMemoryBarrier2 mem{};
            mem.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
            mem.srcStageMask = VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT |
                               VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT;
            mem.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
            mem.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
            mem.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
            VkDependencyInfo dep{};
            dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            dep.memoryBarrierCount = 1;
            dep.pMemoryBarriers = &mem;
            vkCmdPipelineBarrier2(cb, &dep);
        }

        m_PrevViewProj = viewProj;
    }

    void CVoxelRenderer::ReadDebugCounters(u32 out[6]) const {
        // Disabled => return last cached values with NO queue stall.
        if (!m_bDebugEnabled || !m_DebugReadback) {
            for (int i = 0; i < 6; ++i)
                out[i] = m_CachedDebug[i];
            return;
        }
        // GPU writes land in device memory; the validation app is the only
        // reader and runs single-frame-in-flight, so a queue idle + mapped
        // read is the correct (if slow) diagnostic path.
        vkQueueWaitIdle(m_Context.GetGraphicsQueue());
        vmaInvalidateAllocation(m_Context.GetAllocator(), m_DebugReadback->GetAllocation(), 0,
                                sizeof(u32) * 6);
        const auto *src = static_cast<const u32 *>(m_DebugReadback->GetMapped());
        for (int i = 0; i < 6; ++i) {
            out[i] = src[i];
            m_CachedDebug[i] = src[i];
        }
    }
} // namespace Manro
