#pragma once

#include <Manro/Resource/ModelLoader.h>
#include <Manro/Core/Handles.h>
#include <Manro/Core/Types.h>
#include <unordered_map>

namespace Manro {
    class CBuffer;
    struct MeshManagerImpl_t;

    struct LoadedMesh_t {
        u32 firstVertex;
        u32 firstIndex;
        u32 indexCount;
        Vec3 center;
        float radius;
    };

    class CMeshManager {
    public:
        explicit CMeshManager(MeshManagerImpl_t *impl);

        ~CMeshManager();

        CMeshManager(const CMeshManager &) = delete;

        CMeshManager &operator=(const CMeshManager &) = delete;

        [[nodiscard]] MeshHandle Upload(const ModelData_t &data) const;

        [[nodiscard]] const LoadedMesh_t *Get(MeshHandle handle) const;

        CBuffer *GetVertexBuffer() const;

        CBuffer *GetIndexBuffer() const;

    private:
        Scope<MeshManagerImpl_t> m_Impl;
    };
} // namespace Manro