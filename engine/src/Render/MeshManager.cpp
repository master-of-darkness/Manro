#include <Manro/Render/MeshManager.h>
#include <Manro/Resource/ModelLoader.h>
#include "Backend/Vulkan/VulkanContext.h"
#include "Backend/Vulkan/Buffer.h"
#include "Backend/Vulkan/VulkanHelpers.h"
#include <Manro/Core/Logger.h>
#include <cstring>

namespace Manro {
    MeshManager::MeshManager(const VulkanContext &ctx) : m_Context(ctx) {
        m_VertexBuffer = CreateScope<Buffer>(m_Context, sizeof(Vertex) * kMaxVertices,
                                             VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                             VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                                             VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                             VMA_MEMORY_USAGE_GPU_ONLY);
        m_IndexBuffer = CreateScope<Buffer>(m_Context, sizeof(u32) * kMaxIndices,
                                            VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                                            VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                            VMA_MEMORY_USAGE_GPU_ONLY);
    }

    MeshManager::~MeshManager() = default;

    MeshHandle MeshManager::Upload(const ModelData &data) {
        if (data.vertices.empty() || data.indices.empty()) return kInvalidMesh;

        if (m_CurrentVertexOffset + data.vertices.size() > kMaxVertices ||
            m_CurrentIndexOffset + data.indices.size() > kMaxIndices) {
            LOG_ERROR("[MeshManager] Mesh mega-buffer overflow!");
            return kInvalidMesh;
        }

        u32 firstVertex = m_CurrentVertexOffset;
        u32 firstIndex = m_CurrentIndexOffset;
        u32 indexCount = static_cast<u32>(data.indices.size());

        const VkDeviceSize vertexBytes = sizeof(Vertex) * data.vertices.size();
        const VkDeviceSize indexBytes = sizeof(u32) * data.indices.size();
        const VkDeviceSize stagingSize = vertexBytes + indexBytes;

        VkBuffer stagingBuffer = VK_NULL_HANDLE;
        VmaAllocation stagingAllocation = nullptr;
        VmaAllocationInfo stagingAllocationInfo{};

        VkBufferCreateInfo stagingCI{};
        stagingCI.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        stagingCI.size = stagingSize;
        stagingCI.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        stagingCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo stagingAllocCI{};
        stagingAllocCI.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
        stagingAllocCI.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

        if (vmaCreateBuffer(m_Context.GetAllocator(), &stagingCI, &stagingAllocCI,
                            &stagingBuffer, &stagingAllocation, &stagingAllocationInfo) != VK_SUCCESS) {
            LOG_ERROR("[MeshManager] Failed to create staging buffer for mesh upload");
            return kInvalidMesh;
        }

        auto *mapped = static_cast<u8 *>(stagingAllocationInfo.pMappedData);
        std::memcpy(mapped, data.vertices.data(), static_cast<size_t>(vertexBytes));
        std::memcpy(mapped + static_cast<size_t>(vertexBytes), data.indices.data(), static_cast<size_t>(indexBytes));
        vmaFlushAllocation(m_Context.GetAllocator(), stagingAllocation, 0, stagingSize);

        ExecuteOneShot(m_Context, [&](VkCommandBuffer cmd) {
            VkBufferCopy vertexCopy{};
            vertexCopy.srcOffset = 0;
            vertexCopy.dstOffset = sizeof(Vertex) * firstVertex;
            vertexCopy.size = vertexBytes;
            vkCmdCopyBuffer(cmd, stagingBuffer, m_VertexBuffer->GetHandle(), 1, &vertexCopy);

            VkBufferCopy indexCopy{};
            indexCopy.srcOffset = vertexBytes;
            indexCopy.dstOffset = sizeof(u32) * firstIndex;
            indexCopy.size = indexBytes;
            vkCmdCopyBuffer(cmd, stagingBuffer, m_IndexBuffer->GetHandle(), 1, &indexCopy);
        });

        vmaDestroyBuffer(m_Context.GetAllocator(), stagingBuffer, stagingAllocation);

        m_CurrentVertexOffset += static_cast<u32>(data.vertices.size());
        m_CurrentIndexOffset += static_cast<u32>(data.indices.size());

        MeshHandle id = m_NextId++;
        m_Meshes.emplace(id, LoadedMesh{firstVertex, firstIndex, indexCount, data.center, data.radius});
        return id;
    }

    const LoadedMesh *MeshManager::Get(MeshHandle handle) const {
        auto it = m_Meshes.find(handle);
        return it != m_Meshes.end() ? &it->second : nullptr;
    }
} // namespace Manro