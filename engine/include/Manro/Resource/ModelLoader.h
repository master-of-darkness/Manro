#pragma once

#include <Manro/Core/Types.h>
#include <string>
#include <vector>

namespace Manro {
    struct Vertex_t {
        Vec3 position;
        Vec3 normal;
        Vec2 uv;
        Vec4 tangent;
    };

    struct SubMeshData_t {
        std::vector<Vertex_t> vertices;
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

    struct ModelData_t {
        std::vector<Vertex_t> vertices;
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

    class CJobSystem;
    class CVirtualFS;

    class CModelLoader {
    public:
        static std::vector<ModelData_t> Load(const std::vector<std::string> &filepaths,
                                             CJobSystem &jobs, CVirtualFS &vfs);

        static std::vector<std::vector<SubMeshData_t> > LoadSubMeshes(const std::vector<std::string> &filepaths,
                                                                      CJobSystem &jobs, CVirtualFS &vfs);

        static std::vector<SubMeshData_t> LoadSubMeshes(const std::string &filepath, CVirtualFS &vfs);

        static std::string NormalisePath(const std::string &p);
    };
} // namespace Manro