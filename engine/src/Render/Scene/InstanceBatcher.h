#pragma once

#include <Manro/Core/Types.h>
#include <Manro/Render/Renderer.h>
#include "../Internal/ShaderTypes.h"
#include "../Internal/FrameData.h"

#include <vector>

namespace Manro {
    class CMeshManager;
    class CMaterialInstance;
    class CMaterialSystem;
    class CModel;

    class CInstanceBatcher {
    public:
        void Init(u32 maxInstances);

        void DrawMesh(MeshHandle meshId, CMaterialInstance &material, const Mat4 &model,
                      CMeshManager &meshes, CMaterialSystem &matSys, FrameStats_t &stats);

        void DrawMeshStatic(MeshHandle meshId, CMaterialInstance &material, const Mat4 &model,
                            CMeshManager &meshes, CMaterialSystem &matSys);

        void DrawModel(const CModel &model, const Mat4 &transform,
                       CMeshManager &meshes, CMaterialSystem &matSys, FrameStats_t &stats);

        void DrawModelStatic(const CModel &model, const Mat4 &transform,
                             CMeshManager &meshes, CMaterialSystem &matSys);

        void ClearStaticDraws();

        void ClearFrameInstances();

        void UploadToGpu(FrameData_t &frame);

        [[nodiscard]] u32 GetStaticGeneration() const { return m_unStaticGeneration; }

        [[nodiscard]] u32 GetTotalInstanceCount() const {
            return static_cast<u32>(m_StaticInstances.size() + m_CurrentFrameInstances.size());
        }

        [[nodiscard]] u32 GetStaticInstanceCount() const {
            return static_cast<u32>(m_StaticInstances.size());
        }

        [[nodiscard]] u32 GetStaticTriangleCount() const { return m_unStaticTriangleCount; }

        [[nodiscard]] const std::vector<MeshInstance_t> &GetStaticInstances() const { return m_StaticInstances; }

        [[nodiscard]] const std::vector<CullData_t> &GetStaticCullData() const { return m_StaticCullData; }

        [[nodiscard]] const std::vector<MeshInstance_t> &GetFrameInstances() const { return m_CurrentFrameInstances; }

        [[nodiscard]] const std::vector<CullData_t> &GetFrameCullData() const { return m_CurrentFrameCullData; }

    private:
        std::vector<MeshInstance_t> m_CurrentFrameInstances;
        std::vector<CullData_t> m_CurrentFrameCullData;
        std::vector<MeshInstance_t> m_StaticInstances;
        std::vector<CullData_t> m_StaticCullData;
        u32 m_unStaticTriangleCount = 0;
        u32 m_unStaticGeneration = 0;
    };
} // namespace Manro