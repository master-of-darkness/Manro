#pragma once

#include <Manro/Render/Vulkan/Buffer.h>
#include <Manro/Resource/ModelLoader.h>
#include <Manro/Core/Types.h>
#include <unordered_map>

namespace Manro {
    class VulkanContext;

    using MeshHandle = u32;
    inline constexpr MeshHandle kInvalidMesh = 0;

    struct LoadedMesh {
        u32 firstVertex;
        u32 firstIndex;
        u32 indexCount;
        Vec3 center;
        float radius;
    };

    class MeshManager {
    public:
        explicit MeshManager(const VulkanContext &ctx);

        ~MeshManager() = default;

        MeshManager(const MeshManager &) = delete;

        MeshManager &operator=(const MeshManager &) = delete;

        MeshHandle Upload(const ModelData &data);

        const LoadedMesh *Get(MeshHandle handle) const;

        Buffer *GetVertexBuffer() const { return m_VertexBuffer.get(); }
        Buffer *GetIndexBuffer() const { return m_IndexBuffer.get(); }

    private:
        const VulkanContext &m_Context;
        std::unordered_map<MeshHandle, LoadedMesh> m_Meshes;
        MeshHandle m_NextId{1};

        Scope<Buffer> m_VertexBuffer;
        Scope<Buffer> m_IndexBuffer;

        u32 m_CurrentVertexOffset{0};
        u32 m_CurrentIndexOffset{0};

        static constexpr u32 kMaxVertices = 10000000; // 10M vertices
        static constexpr u32 kMaxIndices = 20000000;  // 20M indices
    };
} // namespace Manro
