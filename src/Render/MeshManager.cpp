#include <Manro/Render/MeshManager.h>
#include <Manro/Resource/ModelLoader.h>
#include <Manro/Render/Vulkan/VulkanContext.h>
#include <Manro/Core/Logger.h>

namespace Manro {
    MeshManager::MeshManager(const VulkanContext &ctx) : m_Context(ctx) {
        m_VertexBuffer = CreateScope<Buffer>(m_Context, sizeof(Vertex) * kMaxVertices,
                                              VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                              VMA_MEMORY_USAGE_CPU_TO_GPU);
        m_IndexBuffer = CreateScope<Buffer>(m_Context, sizeof(u32) * kMaxIndices,
                                             VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                             VMA_MEMORY_USAGE_CPU_TO_GPU);
    }

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

        m_VertexBuffer->LoadData(data.vertices.data(), sizeof(Vertex) * data.vertices.size(), sizeof(Vertex) * firstVertex);
        m_IndexBuffer->LoadData(data.indices.data(), sizeof(u32) * data.indices.size(), sizeof(u32) * firstIndex);

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
