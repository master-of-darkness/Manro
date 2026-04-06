#pragma once

#include <Manro/Resource/ModelLoader.h>
#include <Manro/Core/Handles.h>
#include <Manro/Core/Types.h>
#include <unordered_map>

namespace Manro {
    class Buffer;
    struct MeshManagerImpl;

    struct LoadedMesh {
        u32 firstVertex;
        u32 firstIndex;
        u32 indexCount;
        Vec3 center;
        float radius;
    };

    class MeshManager {
    public:
        explicit MeshManager(MeshManagerImpl *impl);

        ~MeshManager();

        MeshManager(const MeshManager &) = delete;

        MeshManager &operator=(const MeshManager &) = delete;

        MeshHandle Upload(const ModelData &data);

        const LoadedMesh *Get(MeshHandle handle) const;

        Buffer *GetVertexBuffer() const;

        Buffer *GetIndexBuffer() const;

    private:
        Scope<MeshManagerImpl> m_Impl;
    };
} // namespace Manro