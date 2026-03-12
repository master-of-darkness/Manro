#include <Manro/Render/MeshManager.h>
#include <Manro/Resource/ModelLoader.h>
#include <Manro/Render/Vulkan/VulkanContext.h>

#include <Manro/Resource/ModelLoader.h>

namespace Manro {
    MeshManager::MeshManager(const VulkanContext &ctx) : m_Context(ctx) {
    }

    MeshHandle MeshManager::Upload(const ModelData &data) {
        if (data.vertices.empty() || data.indices.empty()) return kInvalidMesh;

        LoadedMesh mesh;
        mesh.indexCount = static_cast<u32>(data.indices.size());

        mesh.vertexBuffer = CreateScope<Buffer>(m_Context,
                                                sizeof(Vertex) * data.vertices.size(),
                                                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
        mesh.vertexBuffer->LoadData(data.vertices.data(), sizeof(Vertex) * data.vertices.size());

        mesh.indexBuffer = CreateScope<Buffer>(m_Context,
                                               sizeof(u32) * data.indices.size(),
                                               VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
        mesh.indexBuffer->LoadData(data.indices.data(), sizeof(u32) * data.indices.size());

        MeshHandle id = m_NextId++;
        m_Meshes.emplace(id, std::move(mesh));
        return id;
    }

    const MeshManager::LoadedMesh *MeshManager::Get(MeshHandle handle) const {
        auto it = m_Meshes.find(handle);
        return it != m_Meshes.end() ? &it->second : nullptr;
    }
} // namespace Manro
