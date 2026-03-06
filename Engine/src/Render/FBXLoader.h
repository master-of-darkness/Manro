#pragma once

#include "ModelLoader.h"
#include <Core/Types.h>
#include <string>
#include <vector>

namespace Engine {
    struct AxisRemap {
        int src[3]  = {0, 1, 2};
        float sign[3] = {1.f, 1.f, 1.f};

        Vec3 Apply(const Vec3 &v) const {
            float in[3] = {v.x, v.y, v.z};
            return Vec3{in[src[0]] * sign[0], in[src[1]] * sign[1], in[src[2]] * sign[2]};
        }

        static AxisRemap Identity() { return {}; }

        // NOTE: Some engines like Amazon's use different coordinate system
        static AxisRemap ZUpToYUp() {
            AxisRemap r;
            r.src[0] = 0; r.sign[0] =  1.f; // x -> x
            r.src[1] = 2; r.sign[1] =  1.f; // y -> z
            r.src[2] = 1; r.sign[2] = -1.f; // z -> -y
            return r;
        }
    };

    class FBXLoader {
    public:
        static bool Load(const std::string &filepath, ModelData &out,
                         const AxisRemap &remap = AxisRemap::Identity());

        static bool LoadSubMeshes(const std::string &filepath,
                                  std::vector<SubMeshData> &out,
                                  const AxisRemap &remap = AxisRemap::Identity());

        static bool LoadPBRSubMeshes(const std::string &filepath,
                                     std::vector<PBRSubMeshData> &out,
                                     const AxisRemap &remap = AxisRemap::Identity());
    };
} // namespace Engine
