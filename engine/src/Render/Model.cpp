#include <Manro/Render/Model.h>
#include <Manro/Render/Renderer.h>
#include <Manro/Resource/ModelLoader.h>
#include <Manro/Resource/TextureLoader.h>
#include <Manro/Core/Logger.h>
#include <Manro/Core/JobSystem.h>
#include <unordered_map>
#include <set>

namespace Manro {
    std::vector<Scope<Model>> Model::Load(const std::vector<std::string> &paths,
                                          Renderer &renderer,
                                          JobSystem &jobs) {
        auto allSubMeshes = ModelLoader::LoadSubMeshes(paths, jobs);
        std::vector<Scope<Model>> results;
        results.reserve(paths.size());

        std::set<std::string> uniqueTexturePaths;
        for (const auto &modelSubMeshes: allSubMeshes) {
            for (const auto &sm: modelSubMeshes) {
                if (!sm.diffuseTexturePath.empty()) {
                    uniqueTexturePaths.insert(sm.diffuseTexturePath);
                }
                if (!sm.normalTexturePath.empty()) {
                    uniqueTexturePaths.insert(sm.normalTexturePath);
                }
            }
        }

        std::vector<std::string> texturePathList(uniqueTexturePaths.begin(), uniqueTexturePaths.end());
        auto loadedTextures = TextureLoader::Load(texturePathList, jobs);

        std::unordered_map<std::string, TextureHandle> textureCache;
        for (size_t i = 0; i < texturePathList.size(); ++i) {
            if (!loadedTextures[i].pixels.empty()) {
                TextureHandle tex = renderer.UploadTexture(loadedTextures[i]);
                textureCache[texturePathList[i]] = tex;
            }
        }

        for (size_t i = 0; i < paths.size(); ++i) {
            auto model = CreateScope<Model>();
            for (auto &sd: allSubMeshes[i]) {
                if (sd.vertices.empty()) continue;

                ModelData md;
                md.vertices = std::move(sd.vertices);
                md.indices = std::move(sd.indices);
                md.diffuseTexturePath = sd.diffuseTexturePath;
                md.normalTexturePath = sd.normalTexturePath;
                md.center = sd.center;
                md.radius = sd.radius;

                MeshHandle meshId = renderer.UploadMesh(md);
                auto material = renderer.CreateMaterialInstance(renderer.GetDefaultMaterial());

                if (!sd.diffuseTexturePath.empty()) {
                    auto cacheIt = textureCache.find(sd.diffuseTexturePath);
                    if (cacheIt != textureCache.end()) {
                        material->SetTexture(cacheIt->second);
                    }
                }

                if (!sd.normalTexturePath.empty()) {
                    auto cacheIt = textureCache.find(sd.normalTexturePath);
                    if (cacheIt != textureCache.end()) {
                        material->ModifyData().normalTexture = static_cast<uint16_t>(cacheIt->second + 1);
                    }
                }

                auto &matData = material->ModifyData();
                matData.pbrBaseColorFactor = sd.baseColorFactor;
                matData.pbrMetallicFactor = sd.metallicFactor;
                matData.pbrRoughnessFactor = sd.roughnessFactor;
                matData.alphaMode = sd.alphaMode;
                matData.alphaCutoff = sd.alphaCutoff;
                matData.doubleSided = sd.doubleSided ? 1 : 0;

                model->AddSubMesh(meshId, std::move(material));
            }
            results.push_back(std::move(model));
        }

        LOG_INFO("[Model] Load of {} models completed", paths.size());
        return results;
    }
} // namespace Manro
