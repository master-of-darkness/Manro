#pragma once

#include "Buffer.h"
#include "ModelLoader.h"
#include <Core/Types.h>
#include <unordered_map>

namespace Engine {
    class VulkanContext;

    using MeshHandle = u32;
    inline constexpr MeshHandle kInvalidMesh = 0;

    class MeshManager {
    public:
        explicit MeshManager(const VulkanContext &ctx);

        ~MeshManager() = default;

        MeshManager(const MeshManager &) = delete;

        MeshManager &operator=(const MeshManager &) = delete;

        MeshHandle Upload(const ModelData &data);

        struct LoadedMesh {
            Scope<Buffer> vertexBuffer;
            Scope<Buffer> indexBuffer;
            u32 indexCount{0};
        };

        const LoadedMesh *Get(MeshHandle handle) const;

    private:
        const VulkanContext &m_Context;
        std::unordered_map<MeshHandle, LoadedMesh> m_Meshes;
        MeshHandle m_NextId{1};
    };
} // namespace Engine