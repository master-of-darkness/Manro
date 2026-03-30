#pragma once

#include <Manro/Resource/ModelLoader.h>
#include <Manro/Core/Handles.h>
#include <Manro/Core/Types.h>
#include <unordered_map>

namespace Manro {
    class VulkanContext;
    class Buffer;

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
        ~MeshManager();

        MeshManager(const MeshManager &) = delete;
        MeshManager &operator=(const MeshManager &) = delete;

        MeshHandle Upload(const ModelData &data);
        const LoadedMesh *Get(MeshHandle handle) const;

        Buffer *GetVertexBuffer() const { return m_VertexBuffer.get(); }

        Buffer *GetIndexBuffer() const { return m_IndexBuffer.get(); }

    private:
        const VulkanContext &m_Context;
        std::unordered_map<MeshHandle, LoadedMesh> m_Meshes;
        MeshHandle m_NextId{MeshHandle::Make(1, 0)};

        Scope<Buffer> m_VertexBuffer;
        Scope<Buffer> m_IndexBuffer;

        u32 m_CurrentVertexOffset{0};
        u32 m_CurrentIndexOffset{0};

        static constexpr u32 kMaxVertices = 10'000'000;
        static constexpr u32 kMaxIndices = 20'000'000;
    };
} // namespace Manro