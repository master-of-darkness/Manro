#include "InstanceBatcher.h"
#include "../Pipelines/MaterialSystem.h"
#include "../Math/RenderMathUtils.h"
#include "../Internal/ShaderTypes.h"

#include <Manro/Render/MeshManager.h>
#include <Manro/Render/Material/MaterialInstance.h>
#include <Manro/Render/Model.h>

namespace Manro {
    void CInstanceBatcher::Init(u32 maxInstances) {
        m_CurrentFrameInstances.reserve(maxInstances);
        m_CurrentFrameCullData.reserve(maxInstances);
    }

    void CInstanceBatcher::DrawMesh(MeshHandle meshId, CMaterialInstance &material, const Mat4 &model,
                                    CMeshManager &meshes, CMaterialSystem &matSys, FrameStats_t &stats) {
        const auto *mesh = meshes.Get(meshId);
        if (!mesh) return;

        u32 matIndex = matSys.ResolveMaterialIndex(material);

        stats.drawCalls++;
        stats.instanceCount++;
        stats.triangleCount += mesh->indexCount / 3;

        MeshInstance_t inst{};
        inst.modelMatrix = model;
        inst.firstVertex = mesh->firstVertex;
        inst.firstIndex = mesh->firstIndex;
        inst.indexCount = mesh->indexCount;
        inst.materialIndex = matIndex;
        inst.center[0] = mesh->center.x;
        inst.center[1] = mesh->center.y;
        inst.center[2] = mesh->center.z;
        inst.radius = mesh->radius;
        inst.flags = 0;

        CullData_t cullData{};
        BuildCullData(cullData, mesh, model, static_cast<u32>(m_CurrentFrameInstances.size()));

        m_CurrentFrameInstances.push_back(inst);
        m_CurrentFrameCullData.push_back(cullData);
    }

    void CInstanceBatcher::DrawMeshStatic(MeshHandle meshId, CMaterialInstance &material, const Mat4 &model,
                                          CMeshManager &meshes, CMaterialSystem &matSys) {
        const auto *mesh = meshes.Get(meshId);
        if (!mesh) return;

        u32 matIndex = matSys.ResolveMaterialIndex(material);

        MeshInstance_t inst{};
        inst.modelMatrix = model;
        inst.firstVertex = mesh->firstVertex;
        inst.firstIndex = mesh->firstIndex;
        inst.indexCount = mesh->indexCount;
        inst.materialIndex = matIndex;
        inst.center[0] = mesh->center.x;
        inst.center[1] = mesh->center.y;
        inst.center[2] = mesh->center.z;
        inst.radius = mesh->radius;
        inst.flags = 0;

        CullData_t cullData{};
        BuildCullData(cullData, mesh, model,
                      static_cast<u32>(m_StaticInstances.size() + m_CurrentFrameInstances.size()));

        m_StaticInstances.push_back(inst);
        m_StaticCullData.push_back(cullData);
        m_unStaticTriangleCount += inst.indexCount / 3;
        ++m_unStaticGeneration;
    }

    void CInstanceBatcher::DrawModel(const CModel &model, const Mat4 &transform,
                                     CMeshManager &meshes, CMaterialSystem &matSys, FrameStats_t &stats) {
        for (const auto &sm: model.GetSubMeshes())
            DrawMesh(sm.meshId, *sm.material, transform, meshes, matSys, stats);
    }

    void CInstanceBatcher::DrawModelStatic(const CModel &model, const Mat4 &transform,
                                           CMeshManager &meshes, CMaterialSystem &matSys) {
        for (const auto &sm: model.GetSubMeshes())
            DrawMeshStatic(sm.meshId, *sm.material, transform, meshes, matSys);
    }

    void CInstanceBatcher::ClearStaticDraws() {
        m_StaticInstances.clear();
        m_StaticCullData.clear();
        m_unStaticTriangleCount = 0;
        ++m_unStaticGeneration;
    }

    void CInstanceBatcher::ClearFrameInstances() {
        m_CurrentFrameInstances.clear();
        m_CurrentFrameCullData.clear();
    }

    void CInstanceBatcher::UploadToGpu(FrameData_t &frame) {
        u32 staticInstCount = static_cast<u32>(m_StaticInstances.size());
        u32 dynamicInstCount = static_cast<u32>(m_CurrentFrameInstances.size());

        if (frame.staticUploadedGeneration != m_unStaticGeneration && staticInstCount > 0) {
            frame.instanceBuffer->LoadData(m_StaticInstances.data(), sizeof(MeshInstance_t) * staticInstCount, 0);
            frame.cullDataBuffer->LoadData(m_StaticCullData.data(), sizeof(CullData_t) * staticInstCount, 0);
            frame.staticUploadedGeneration = m_unStaticGeneration;
        }
        if (dynamicInstCount > 0) {
            frame.instanceBuffer->LoadData(m_CurrentFrameInstances.data(),
                                           sizeof(MeshInstance_t) * dynamicInstCount,
                                           sizeof(MeshInstance_t) * staticInstCount);
            frame.cullDataBuffer->LoadData(m_CurrentFrameCullData.data(), sizeof(CullData_t) * dynamicInstCount,
                                           sizeof(CullData_t) * staticInstCount);
        }
    }
} // namespace Manro