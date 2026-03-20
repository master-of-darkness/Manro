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
        Vec3 center;
        float radius;
    };

    struct ModelData {
        std::vector<Vertex> vertices;
        std::vector<u32> indices;
        std::string diffuseTexturePath;
        Vec3 center;
        float radius;
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
