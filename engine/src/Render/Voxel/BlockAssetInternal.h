#pragma once

#include "VoxelBlockAssets.h"

#include <nlohmann/json.hpp>

#include <array>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Manro {
    using json = nlohmann::json;

    namespace blockassets {
        struct ModelRef {
            std::string model;
            int x{0};
            int y{0};
        };

        std::vector<ModelRef> CollectModels(const json &blockJson,
                                            const std::map<std::string, std::string> &props);

        std::string StripPrefix(const std::string &s);

        std::string StripState(const std::string &s);

        const json *LoadModelJson(const std::string &modelsDir,
                                  std::unordered_map<std::string, json> &modelCache,
                                  const std::string &rel);

        struct FaceTextures {
            std::array<std::string, 6> tex{};
            std::array<std::array < float, 4>
            ,
            6
            >
            uv {
            };
            bool any{false};

            FaceTextures() {
                for (auto &r: uv)
                    r = {0.f, 0.f, 16.f, 16.f};
            }
        };

        void RotateFaces(FaceTextures &ft, int rotX, int rotY);

        FaceTextures ResolveModel(const std::string &modelsDir,
                                  std::unordered_map<std::string, json> &modelCache,
                                  const std::string &modelPath);

        struct Box3 {
            double mn[3]{0.0, 0.0, 0.0};
            double mx[3]{16.0, 16.0, 16.0};
        };

        struct StateShape {
            double mn[3]{0.0, 0.0, 0.0};
            double mx[3]{16.0, 16.0, 16.0};
            bool hasElements{false};
        };

        StateShape CollectStateShape(const std::string &modelsDir,
                                     std::unordered_map<std::string, json> &modelCache,
                                     const std::vector<ModelRef> &refs);

        std::vector<Box3> CollectSolidBoxes(const std::string &modelsDir,
                                            std::unordered_map<std::string, json> &modelCache,
                                            const std::vector<ModelRef> &refs);

        u32 TintForName(const std::string &name);

        bool IsAlwaysAir(std::string_view name);

        bool IsExplicitSkip(std::string_view name);

        bool IsChainName(std::string_view name);

        bool IsFenceName(std::string_view name);

        bool IsSignName(std::string_view name);

        bool IsSmallDecor(std::string_view name);

        bool IsCutout(std::string_view name);

        bool IsLeavesName(std::string_view name);

        bool IsTrapdoorName(std::string_view name);

        bool IsInnerFaces(std::string_view name);

        bool IsFluidName(std::string_view name);

        u8 EmissionForState(std::string_view bname,
                            const std::map<std::string, std::string> &props);

        u8 SkyOpacityForState(std::string_view bname, u32 flags);

        bool IsOpaqueOccluder(std::string_view name);

        std::string OverrideTexture(std::string_view name);
    }
}
