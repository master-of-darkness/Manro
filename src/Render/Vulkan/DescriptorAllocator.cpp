#include <Manro/Render/Vulkan/DescriptorAllocator.h>
#include <Manro/Core/Logger.h>
#include <stdexcept>

namespace Manro {
    void PerFrameAllocator::Init(VkDevice device, u32 maxSets,
                                 const PoolSizeRatio *ratios, u32 ratioCount) {
        m_Device = device;

        std::vector<VkDescriptorPoolSize> sizes(ratioCount);
        for (u32 i = 0; i < ratioCount; ++i) {
            sizes[i].type = ratios[i].type;
            sizes[i].descriptorCount = static_cast<u32>(maxSets * ratios[i].ratio);
        }

        VkDescriptorPoolCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        ci.maxSets = maxSets;
        ci.poolSizeCount = ratioCount;
        ci.pPoolSizes = sizes.data();

        if (vkCreateDescriptorPool(m_Device, &ci, nullptr, &m_Pool) != VK_SUCCESS)
            throw std::runtime_error("[PerFrameAllocator] Failed to create descriptor pool");
    }

    void PerFrameAllocator::Shutdown() {
        if (m_Pool) {
            vkDestroyDescriptorPool(m_Device, m_Pool, nullptr);
            m_Pool = VK_NULL_HANDLE;
        }
    }

    void PerFrameAllocator::Reset() {
        vkResetDescriptorPool(m_Device, m_Pool, 0);
    }

    VkDescriptorSet PerFrameAllocator::Allocate(VkDescriptorSetLayout layout) {
        VkDescriptorSetAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool = m_Pool;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts = &layout;

        VkDescriptorSet set = VK_NULL_HANDLE;
        VkResult result = vkAllocateDescriptorSets(m_Device, &ai, &set);
        if (result != VK_SUCCESS) {
            LOG_ERROR("[PerFrameAllocator] vkAllocateDescriptorSets failed: {}", (int)result);
            return VK_NULL_HANDLE;
        }
        return set;
    }

    void PersistentAllocator::Init(VkDevice device, u32 initialPoolSets,
                                   const PoolSizeRatio *ratios, u32 ratioCount) {
        m_Device = device;
        m_SetsPerPool = initialPoolSets;
        m_Ratios.assign(ratios, ratios + ratioCount);
        m_Current = GrowPool();
    }

    void PersistentAllocator::Shutdown() {
        for (auto pool: m_Pools)
            vkDestroyDescriptorPool(m_Device, pool, nullptr);
        m_Pools.clear();
        m_Current = VK_NULL_HANDLE;
    }

    VkDescriptorSet PersistentAllocator::Allocate(VkDescriptorSetLayout layout) {
        VkDescriptorSetAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool = m_Current;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts = &layout;

        VkDescriptorSet set = VK_NULL_HANDLE;
        VkResult result = vkAllocateDescriptorSets(m_Device, &ai, &set);

        if (result == VK_ERROR_OUT_OF_POOL_MEMORY || result == VK_ERROR_FRAGMENTED_POOL) {
            // Grow and retry once
            m_Current = GrowPool();
            ai.descriptorPool = m_Current;
            result = vkAllocateDescriptorSets(m_Device, &ai, &set);
        }

        if (result != VK_SUCCESS) {
            LOG_ERROR("[PersistentAllocator] vkAllocateDescriptorSets failed: {}", (int)result);
            return VK_NULL_HANDLE;
        }
        return set;
    }

    void PersistentAllocator::Free(VkDescriptorSet set) {
        vkFreeDescriptorSets(m_Device, m_Current, 1, &set);
    }

    VkDescriptorPool PersistentAllocator::GrowPool() {
        std::vector<VkDescriptorPoolSize> sizes(m_Ratios.size());
        for (u32 i = 0; i < m_Ratios.size(); ++i) {
            sizes[i].type = m_Ratios[i].type;
            sizes[i].descriptorCount = static_cast<u32>(m_SetsPerPool * m_Ratios[i].ratio);
        }

        VkDescriptorPoolCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        ci.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        ci.maxSets = m_SetsPerPool;
        ci.poolSizeCount = static_cast<u32>(sizes.size());
        ci.pPoolSizes = sizes.data();

        VkDescriptorPool pool = VK_NULL_HANDLE;
        if (vkCreateDescriptorPool(m_Device, &ci, nullptr, &pool) != VK_SUCCESS)
            throw std::runtime_error("[PersistentAllocator] Failed to grow descriptor pool");

        m_Pools.push_back(pool);

        m_SetsPerPool = std::min(m_SetsPerPool * 2, 4096u);
        return pool;
    }

    void BindlessAllocator::Init(VkDevice device) {
        m_Device = device;

        VkDescriptorPoolSize poolSize{};
        poolSize.type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        poolSize.descriptorCount = kMaxTextures;

        VkDescriptorPoolCreateInfo poolCI{};
        poolCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolCI.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
        poolCI.maxSets = 1;
        poolCI.poolSizeCount = 1;
        poolCI.pPoolSizes = &poolSize;

        if (vkCreateDescriptorPool(m_Device, &poolCI, nullptr, &m_Pool) != VK_SUCCESS)
            throw std::runtime_error("[BindlessAllocator] Failed to create pool");

        // Layout
        VkDescriptorSetLayoutBinding binding{};
        binding.binding = 0;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        binding.descriptorCount = kMaxTextures;
        binding.stageFlags = VK_SHADER_STAGE_ALL;

        VkDescriptorBindingFlags flags =
                VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
                VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
                VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT;

        VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlags{};
        bindingFlags.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
        bindingFlags.bindingCount = 1;
        bindingFlags.pBindingFlags = &flags;

        VkDescriptorSetLayoutCreateInfo layoutCI{};
        layoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutCI.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
        layoutCI.bindingCount = 1;
        layoutCI.pBindings = &binding;
        layoutCI.pNext = &bindingFlags;

        if (vkCreateDescriptorSetLayout(m_Device, &layoutCI, nullptr, &m_Layout) != VK_SUCCESS)
            throw std::runtime_error("[BindlessAllocator] Failed to create layout");

        // Allocate the single set
        u32 varCount = kMaxTextures;
        VkDescriptorSetVariableDescriptorCountAllocateInfo varCI{};
        varCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO;
        varCI.descriptorSetCount = 1;
        varCI.pDescriptorCounts = &varCount;

        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = m_Pool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &m_Layout;
        allocInfo.pNext = &varCI;

        if (vkAllocateDescriptorSets(m_Device, &allocInfo, &m_Set) != VK_SUCCESS)
            throw std::runtime_error("[BindlessAllocator] Failed to allocate set");

        LOG_INFO("[BindlessAllocator] Initialized ({} texture slots)", kMaxTextures);
    }

    void BindlessAllocator::Shutdown() {
        if (m_Layout) {
            vkDestroyDescriptorSetLayout(m_Device, m_Layout, nullptr);
            m_Layout = VK_NULL_HANDLE;
        }
        if (m_Pool) {
            vkDestroyDescriptorPool(m_Device, m_Pool, nullptr);
            m_Pool = VK_NULL_HANDLE;
        }
        m_Set = VK_NULL_HANDLE;
    }

    void BindlessAllocator::UpdateSlot(u32 index, VkImageView view, VkImageLayout layout) {
        VkDescriptorImageInfo imgInfo{};
        imgInfo.imageView = view;
        imgInfo.imageLayout = layout;

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = m_Set;
        write.dstBinding = 0;
        write.dstArrayElement = index;
        write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        write.descriptorCount = 1;
        write.pImageInfo = &imgInfo;

        vkUpdateDescriptorSets(m_Device, 1, &write, 0, nullptr);
    }
} // namespace Manro
