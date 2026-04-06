#pragma once

#include "../Vulkan/VulkanContext.h"
#include "../Vulkan/Buffer.h"
#include <Manro/Render/MeshManager.h>
#include <Manro/Core/Handles.h>
#include <unordered_map>

namespace Manro {
    struct MeshManagerImpl {
        const VulkanContext &Context;
        std::unordered_map<MeshHandle, LoadedMesh> Meshes;
        MeshHandle NextId{MeshHandle::Make(1, 0)};

        Scope<Buffer> VertexBuffer;
        Scope<Buffer> IndexBuffer;

        u32 CurrentVertexOffset{0};
        u32 CurrentIndexOffset{0};

        static constexpr u32 kMaxVertices = 10'000'000;
        static constexpr u32 kMaxIndices = 20'000'000;

        explicit MeshManagerImpl(const VulkanContext &ctx);
    };
} // namespace Manro
