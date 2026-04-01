#pragma once

#include <Manro/Core/Types.h>
#include <string>
#include <vector>

namespace Manro {
    struct Vertex {
        Vec3 position;
        Vec3 normal;
        Vec2 uv;
        Vec4 tangent;
    };

    struct SubMeshData {
        std::vector<Vertex> vertices;
        std::vector<u32> indices;
        std::string diffuseTexturePath;
        std::string normalTexturePath;
        Vec3 center;
        float radius;

        Vec4 baseColorFactor{1.0f};
        float metallicFactor{1.0f};
        float roughnessFactor{1.0f};
        int alphaMode{0};
        float alphaCutoff{0.5f};
        bool doubleSided{false};
    };

    struct ModelData {
        std::vector<Vertex> vertices;
        std::vector<u32> indices;
        std::string diffuseTexturePath;
        std::string normalTexturePath;
        Vec3 center;
        float radius;
        Vec4 baseColorFactor{1.0f};
        float metallicFactor{1.0f};
        float roughnessFactor{1.0f};
        int alphaMode{0};
        float alphaCutoff{0.5f};
        bool doubleSided{false};
    };

    class JobSystem;

    class ModelLoader {
    public:
        static std::vector<ModelData> Load(const std::vector<std::string> &filepaths, JobSystem &jobs);

        static std::vector<std::vector<SubMeshData>> LoadSubMeshes(const std::vector<std::string> &filepaths,
                                                                   JobSystem &jobs);

        static std::string NormalisePath(const std::string &p);
    };
} // namespace Manro
