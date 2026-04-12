#pragma once

#include <Manro/Core/Types.h>
#include <Manro/Render/Renderer.h>
#include "RendererTypes.h"

#include <vector>

namespace Manro {
    class MeshManager;
    class MaterialInstance;
    class MaterialSystem;
    class Model;

    class InstanceBatcher {
    public:
        void Init(u32 maxInstances);

        void DrawMesh(MeshHandle meshId, MaterialInstance &material, const Mat4 &model,
                      MeshManager &meshes, MaterialSystem &matSys, FrameStats &stats);

        void DrawMeshStatic(MeshHandle meshId, MaterialInstance &material, const Mat4 &model,
                            MeshManager &meshes, MaterialSystem &matSys);

        void DrawModel(const Model &model, const Mat4 &transform,
                       MeshManager &meshes, MaterialSystem &matSys, FrameStats &stats);

        void DrawModelStatic(const Model &model, const Mat4 &transform,
                             MeshManager &meshes, MaterialSystem &matSys);

        void ClearStaticDraws();

        void ClearFrameInstances();

        void UploadToGpu(FrameData &frame);

        void InvalidateStaticUpload(std::vector<FrameData> &frames);

        [[nodiscard]] u32 GetTotalInstanceCount() const {
            return static_cast<u32>(m_StaticInstances.size() + m_CurrentFrameInstances.size());
        }

        [[nodiscard]] u32 GetStaticInstanceCount() const {
            return static_cast<u32>(m_StaticInstances.size());
        }

        [[nodiscard]] u32 GetStaticTriangleCount() const { return m_StaticTriangleCount; }

        [[nodiscard]] const std::vector<MeshInstance> &GetStaticInstances() const { return m_StaticInstances; }

        [[nodiscard]] const std::vector<CullData> &GetStaticCullData() const { return m_StaticCullData; }

        [[nodiscard]] const std::vector<MeshInstance> &GetFrameInstances() const { return m_CurrentFrameInstances; }

        [[nodiscard]] const std::vector<CullData> &GetFrameCullData() const { return m_CurrentFrameCullData; }

    private:
        std::vector<MeshInstance> m_CurrentFrameInstances;
        std::vector<CullData> m_CurrentFrameCullData;
        std::vector<MeshInstance> m_StaticInstances;
        std::vector<CullData> m_StaticCullData;
        u32 m_StaticTriangleCount = 0;
    };
} // namespace Manro