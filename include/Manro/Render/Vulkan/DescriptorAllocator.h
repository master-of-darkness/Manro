#pragma once

#include <Manro/Core/Types.h>
#include <Manro/Core/Logger.h>

#include <volk.h>
#include <stdexcept>
#include <vector>

namespace Manro {
    class VulkanContext;

    struct PoolSizeRatio {
        VkDescriptorType type;
        float ratio;
    };

    static constexpr PoolSizeRatio kDefaultPoolSizes[] = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4.f},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 10.f},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4.f},
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 4.f},
        {VK_DESCRIPTOR_TYPE_SAMPLER, 2.f},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2.f},
    };

    class PerFrameAllocator {
    public:
        void Init(VkDevice device, u32 maxSetsPerFrame,
                  const PoolSizeRatio *ratios = kDefaultPoolSizes,
                  u32 ratioCount = static_cast<u32>(std::size(kDefaultPoolSizes)));

        void Shutdown();

        void Reset();

        VkDescriptorSet Allocate(VkDescriptorSetLayout layout);

        VkDevice Device() const { return m_Device; }

    private:
        VkDevice m_Device = VK_NULL_HANDLE;
        VkDescriptorPool m_Pool = VK_NULL_HANDLE;
    };

    class PersistentAllocator {
    public:
        void Init(VkDevice device, u32 initialPoolSets = 256,
                  const PoolSizeRatio *ratios = kDefaultPoolSizes,
                  u32 ratioCount = static_cast<u32>(std::size(kDefaultPoolSizes)));

        void Shutdown();

        VkDescriptorSet Allocate(VkDescriptorSetLayout layout);

        void Free(VkDescriptorSet set);

    private:
        VkDescriptorPool GrowPool();

        VkDevice m_Device = VK_NULL_HANDLE;
        u32 m_SetsPerPool = 256;
        std::vector<PoolSizeRatio> m_Ratios;
        std::vector<VkDescriptorPool> m_Pools;
        VkDescriptorPool m_Current = VK_NULL_HANDLE;
    };

    class BindlessAllocator {
    public:
        static constexpr u32 kMaxTextures = 4096;

        void Init(VkDevice device);

        void Shutdown();

        VkDescriptorSet GetSet() const { return m_Set; }
        VkDescriptorSetLayout GetLayout() const { return m_Layout; }

        void UpdateSlot(u32 index, VkImageView view,
                        VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    private:
        VkDevice m_Device = VK_NULL_HANDLE;
        VkDescriptorPool m_Pool = VK_NULL_HANDLE;
        VkDescriptorSetLayout m_Layout = VK_NULL_HANDLE;
        VkDescriptorSet m_Set = VK_NULL_HANDLE;
    };
} // namespace Manro
