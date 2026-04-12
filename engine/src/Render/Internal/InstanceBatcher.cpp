#include "InstanceBatcher.h"
#include "MaterialSystem.h"
#include "RenderMathUtils.h"
#include "RendererTypes.h"

#include <Manro/Render/MeshManager.h>
#include <Manro/Render/Material/MaterialInstance.h>
#include <Manro/Render/Model.h>

namespace Manro {
    void InstanceBatcher::Init(u32 maxInstances) {
        m_CurrentFrameInstances.reserve(maxInstances);
        m_CurrentFrameCullData.reserve(maxInstances);
    }

    void InstanceBatcher::DrawMesh(MeshHandle meshId, MaterialInstance &material, const Mat4 &model,
                                   MeshManager &meshes, MaterialSystem &matSys, FrameStats &stats) {
        const auto *mesh = meshes.Get(meshId);
        if (!mesh) return;

        u32 matIndex = matSys.ResolveMaterialIndex(material);

        stats.drawCalls++;
        stats.instanceCount++;
        stats.triangleCount += mesh->indexCount / 3;

        MeshInstance inst{};
        inst.modelMatrix = model;
        ComputeNormalMatrix(inst, model);
        inst.firstVertex = mesh->firstVertex;
        inst.firstIndex = mesh->firstIndex;
        inst.indexCount = mesh->indexCount;
        inst.materialIndex = matIndex;
        inst.center[0] = mesh->center.x;
        inst.center[1] = mesh->center.y;
        inst.center[2] = mesh->center.z;
        inst.radius = mesh->radius;
        inst.flags = 0;

        CullData cullData{};
        BuildCullData(cullData, mesh, model, static_cast<u32>(m_CurrentFrameInstances.size()));

        m_CurrentFrameInstances.push_back(inst);
        m_CurrentFrameCullData.push_back(cullData);
    }

    void InstanceBatcher::DrawMeshStatic(MeshHandle meshId, MaterialInstance &material, const Mat4 &model,
                                         MeshManager &meshes, MaterialSystem &matSys) {
        const auto *mesh = meshes.Get(meshId);
        if (!mesh) return;

        u32 matIndex = matSys.ResolveMaterialIndex(material);

        MeshInstance inst{};
        inst.modelMatrix = model;
        ComputeNormalMatrix(inst, model);
        inst.firstVertex = mesh->firstVertex;
        inst.firstIndex = mesh->firstIndex;
        inst.indexCount = mesh->indexCount;
        inst.materialIndex = matIndex;
        inst.center[0] = mesh->center.x;
        inst.center[1] = mesh->center.y;
        inst.center[2] = mesh->center.z;
        inst.radius = mesh->radius;
        inst.flags = 0;

        CullData cullData{};
        BuildCullData(cullData, mesh, model,
                      static_cast<u32>(m_StaticInstances.size() + m_CurrentFrameInstances.size()));

        m_StaticInstances.push_back(inst);
        m_StaticCullData.push_back(cullData);
        m_StaticTriangleCount += inst.indexCount / 3;
    }

    void InstanceBatcher::DrawModel(const Model &model, const Mat4 &transform,
                                    MeshManager &meshes, MaterialSystem &matSys, FrameStats &stats) {
        for (const auto &sm: model.GetSubMeshes())
            DrawMesh(sm.meshId, *sm.material, transform, meshes, matSys, stats);
    }

    void InstanceBatcher::DrawModelStatic(const Model &model, const Mat4 &transform,
                                          MeshManager &meshes, MaterialSystem &matSys) {
        for (const auto &sm: model.GetSubMeshes())
            DrawMeshStatic(sm.meshId, *sm.material, transform, meshes, matSys);
    }

    void InstanceBatcher::ClearStaticDraws() {
        m_StaticInstances.clear();
        m_StaticCullData.clear();
        m_StaticTriangleCount = 0;
    }

    void InstanceBatcher::ClearFrameInstances() {
        m_CurrentFrameInstances.clear();
        m_CurrentFrameCullData.clear();
    }

    void InstanceBatcher::UploadToGpu(FrameData &frame) {
        u32 staticInstCount = static_cast<u32>(m_StaticInstances.size());
        u32 dynamicInstCount = static_cast<u32>(m_CurrentFrameInstances.size());

        if (!frame.staticUploaded && staticInstCount > 0) {
            frame.instanceBuffer->LoadData(m_StaticInstances.data(), sizeof(MeshInstance) * staticInstCount, 0);
            frame.cullDataBuffer->LoadData(m_StaticCullData.data(), sizeof(CullData) * staticInstCount, 0);
            frame.staticUploaded = true;
        }
        if (dynamicInstCount > 0) {
            frame.instanceBuffer->LoadData(m_CurrentFrameInstances.data(),
                                           sizeof(MeshInstance) * dynamicInstCount,
                                           sizeof(MeshInstance) * staticInstCount);
            frame.cullDataBuffer->LoadData(m_CurrentFrameCullData.data(), sizeof(CullData) * dynamicInstCount,
                                           sizeof(CullData) * staticInstCount);
        }
    }

    void InstanceBatcher::InvalidateStaticUpload(std::vector<FrameData> &frames) {
        for (auto &frame: frames) {
            frame.staticUploaded = false;
        }
    }
} // namespace Manro