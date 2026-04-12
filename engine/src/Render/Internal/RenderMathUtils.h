#pragma once

#include <Manro/Core/Types.h>
#include <Manro/Render/MeshManager.h>
#include "RendererTypes.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <cmath>

namespace Manro {
    inline void ComputeNormalMatrix(MeshInstance &inst, const Mat4 &model) {
        Vec3 s0 = Vec3(model[0]), s1 = Vec3(model[1]), s2 = Vec3(model[2]);
        float l0 = glm::dot(s0, s0), l1 = glm::dot(s1, s1), l2 = glm::dot(s2, s2);

        if (std::abs(l0 - 1.f) < 1e-4f && std::abs(l1 - 1.f) < 1e-4f && std::abs(l2 - 1.f) < 1e-4f) {
            for (int i = 0; i < 3; ++i) {
                inst.normalMatrix[i][0] = model[i][0];
                inst.normalMatrix[i][1] = model[i][1];
                inst.normalMatrix[i][2] = model[i][2];
                inst.normalMatrix[i][3] = 0.f;
            }
        } else if (std::abs(l0 - l1) < 1e-4f && std::abs(l0 - l2) < 1e-4f) {
            float invS2 = 1.f / l0;
            for (int i = 0; i < 3; ++i) {
                inst.normalMatrix[i][0] = model[i][0] * invS2;
                inst.normalMatrix[i][1] = model[i][1] * invS2;
                inst.normalMatrix[i][2] = model[i][2] * invS2;
                inst.normalMatrix[i][3] = 0.f;
            }
        } else {
            glm::mat3 n3 = glm::transpose(glm::inverse(glm::mat3(model)));
            for (int i = 0; i < 3; ++i) {
                inst.normalMatrix[i][0] = n3[i][0];
                inst.normalMatrix[i][1] = n3[i][1];
                inst.normalMatrix[i][2] = n3[i][2];
                inst.normalMatrix[i][3] = 0.f;
            }
        }
    }

    inline void BuildCullData(CullData &out, const LoadedMesh *mesh, const Mat4 &model, u32 instanceId) {
        Vec3 s0 = Vec3(model[0]), s1 = Vec3(model[1]), s2 = Vec3(model[2]);
        float l0 = glm::dot(s0, s0), l1 = glm::dot(s1, s1), l2 = glm::dot(s2, s2);
        Vec3 worldCenter = Vec3(model * Vec4(mesh->center, 1.f));
        float scaleSq = std::max(l0, std::max(l1, l2));
        out.center[0] = worldCenter.x;
        out.center[1] = worldCenter.y;
        out.center[2] = worldCenter.z;
        out.radius = mesh->radius * std::sqrt(scaleSq);
        out.instanceId = instanceId;
    }

    inline void ExtractFrustumPlanes(const Mat4 &viewProj, Vec4 planes[6]) {
        Mat4 m = glm::transpose(viewProj);
        Vec4 r0 = m[0], r1 = m[1], r2 = m[2], r3 = m[3];
        planes[0] = r3 + r0;
        planes[1] = r3 - r0;
        planes[2] = r3 + r1;
        planes[3] = r3 - r1;
        planes[4] = r3 + r2;
        planes[5] = r3 - r2;
        for (int i = 0; i < 6; ++i) {
            float len = glm::length(Vec3(planes[i]));
            planes[i] /= len;
        }
    }
} // namespace Manro
