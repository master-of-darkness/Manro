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
    };

    struct ModelData {
        std::vector<Vertex> vertices;
        std::vector<u32> indices;
        std::string diffuseTexturePath;
    };

    class ModelLoader {
    public:
        static bool Load(const std::string &filepath, ModelData &out);

        static bool LoadSubMeshes(const std::string &filepath,
                                  std::vector<SubMeshData> &out);

    private:
        static std::string NormalisePath(const std::string &p);
    };
} // namespace Manro
