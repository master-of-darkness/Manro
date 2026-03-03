#pragma once

#include <Core/Types.h>
#include <string>
#include <vector>

namespace Engine {
    struct ModelVertex {
        Vec3 position;
        Vec3 color;
        Vec2 uv;
    };

    struct SubMeshData {
        std::vector<ModelVertex> vertices;
        std::vector<u32> indices;
        std::string diffuseTexturePath;
    };

    struct ModelData {
        std::vector<ModelVertex> vertices;
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
} // namespace Engine