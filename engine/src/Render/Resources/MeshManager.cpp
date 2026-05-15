#include "MeshManagerInternal.h"
#include "../Vulkan/VulkanContext.h"
#include "../Vulkan/Buffer.h"
#include "../Vulkan/VulkanHelpers.h"

#include <Manro/Core/Logger.h>
#include <Manro/Render/MeshManager.h>
#include <Manro/Resource/ModelLoader.h>
#include <cstring>

namespace Manro {
    MeshManagerImpl_t::MeshManagerImpl_t(const CVulkanContext &ctx) : Context(ctx) {
        VertexBuffer = CreateScope<CBuffer>(Context, sizeof(Vertex_t) * kMaxVertices,
                                            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                                            VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                            VMA_MEMORY_USAGE_GPU_ONLY);
        IndexBuffer = CreateScope<CBuffer>(Context, sizeof(u32) * kMaxIndices,
                                           VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                           VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                                           VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                           VMA_MEMORY_USAGE_GPU_ONLY);
    }

    CMeshManager::CMeshManager(MeshManagerImpl_t *impl) : m_Impl(impl) {
    }

    CMeshManager::~CMeshManager() = default;

    MeshHandle CMeshManager::Upload(const ModelData_t &data) const {
        if (data.vertices.empty() || data.indices.empty()) return kInvalidMesh;

        if (m_Impl->CurrentVertexOffset + data.vertices.size() > MeshManagerImpl_t::kMaxVertices ||
            m_Impl->CurrentIndexOffset + data.indices.size() > MeshManagerImpl_t::kMaxIndices) {
            LOG_ERROR("[CMeshManager] Mesh mega-buffer overflow!");
            return kInvalidMesh;
        }

        u32 firstVertex = m_Impl->CurrentVertexOffset;
        u32 firstIndex = m_Impl->CurrentIndexOffset;
        u32 indexCount = static_cast<u32>(data.indices.size());

        const VkDeviceSize vertexBytes = sizeof(Vertex_t) * data.vertices.size();
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

        if (vmaCreateBuffer(m_Impl->Context.GetAllocator(), &stagingCI, &stagingAllocCI,
                            &stagingBuffer, &stagingAllocation, &stagingAllocationInfo) != VK_SUCCESS) {
            LOG_ERROR("[CMeshManager] Failed to create staging buffer for mesh upload");
            return kInvalidMesh;
        }

        auto *mapped = static_cast<u8 *>(stagingAllocationInfo.pMappedData);
        std::memcpy(mapped, data.vertices.data(), static_cast<size_t>(vertexBytes));
        std::memcpy(mapped + static_cast<size_t>(vertexBytes), data.indices.data(), static_cast<size_t>(indexBytes));
        vmaFlushAllocation(m_Impl->Context.GetAllocator(), stagingAllocation, 0, stagingSize);

        ExecuteOneShot(m_Impl->Context, [&](VkCommandBuffer cmd) {
            VkBufferCopy vertexCopy{};
            vertexCopy.srcOffset = 0;
            vertexCopy.dstOffset = sizeof(Vertex_t) * firstVertex;
            vertexCopy.size = vertexBytes;
            vkCmdCopyBuffer(cmd, stagingBuffer, m_Impl->VertexBuffer->GetHandle(), 1, &vertexCopy);

            VkBufferCopy indexCopy{};
            indexCopy.srcOffset = vertexBytes;
            indexCopy.dstOffset = sizeof(u32) * firstIndex;
            indexCopy.size = indexBytes;
            vkCmdCopyBuffer(cmd, stagingBuffer, m_Impl->IndexBuffer->GetHandle(), 1, &indexCopy);
        });

        vmaDestroyBuffer(m_Impl->Context.GetAllocator(), stagingBuffer, stagingAllocation);

        m_Impl->CurrentVertexOffset += static_cast<u32>(data.vertices.size());
        m_Impl->CurrentIndexOffset += static_cast<u32>(data.indices.size());

        MeshHandle id = m_Impl->NextId++;
        m_Impl->Meshes.emplace(id, LoadedMesh_t{firstVertex, firstIndex, indexCount, data.center, data.radius});
        return id;
    }

    const LoadedMesh_t *CMeshManager::Get(MeshHandle handle) const {
        auto it = m_Impl->Meshes.find(handle);
        return it != m_Impl->Meshes.end() ? &it->second : nullptr;
    }

    CBuffer *CMeshManager::GetVertexBuffer() const {
        return m_Impl->VertexBuffer.get();
    }

    CBuffer *CMeshManager::GetIndexBuffer() const {
        return m_Impl->IndexBuffer.get();
    }
} // namespace Manro
