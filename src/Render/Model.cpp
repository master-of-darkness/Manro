#include <Manro/Render/Model.h>
#include <Manro/Render/Renderer.h>
#include <Manro/Resource/ModelLoader.h>
#include <Manro/Resource/TextureLoader.h>
#include <Manro/Core/Logger.h>
#include <unordered_map>

namespace Manro {
    Scope<Model> Model::Load(const std::string& path, Renderer& renderer) {
        std::vector<SubMeshData> subMeshData;
        if (!ModelLoader::LoadSubMeshes(path.c_str(), subMeshData)) {
            LOG_ERROR("[Model] Failed to load '{}'.", path);
            return nullptr;
        }

        auto model = CreateScope<Model>();
        std::unordered_map<std::string, TextureHandle> textureCache;

        for (auto& sd : subMeshData) {
            if (sd.vertices.empty()) continue;

            ModelData md;
            md.vertices = std::move(sd.vertices);
            md.indices = std::move(sd.indices);
            md.diffuseTexturePath = sd.diffuseTexturePath;
            md.center = sd.center;
            md.radius = sd.radius;

            MeshHandle meshId = renderer.UploadMesh(md);
            auto material = renderer.CreateMaterialInstance(renderer.GetDefaultMaterial());

            if (!sd.diffuseTexturePath.empty()) {
                auto cacheIt = textureCache.find(sd.diffuseTexturePath);
                if (cacheIt != textureCache.end()) {
                    material->SetTexture(cacheIt->second);
                } else {
                    TextureData td;
                    if (TextureLoader::Load(sd.diffuseTexturePath, td)) {
                        TextureHandle tex = renderer.UploadTexture(td);
                        material->SetTexture(tex);
                        textureCache[sd.diffuseTexturePath] = tex;
                    }
                }
            }

            model->AddSubMesh(meshId, std::move(material));
        }

        LOG_INFO("[Model] Loaded '{}' - {} sub-meshes, {} unique textures",
                 path, model->GetSubMeshes().size(), textureCache.size());

        return model;
    }
} // namespace Manro
