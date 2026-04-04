#include <Manro/Render/Model.h>
#include <Manro/Render/Renderer.h>
#include <Manro/Resource/ModelLoader.h>
#include <Manro/Resource/TextureLoader.h>
#include <Manro/Core/Logger.h>
#include <Manro/Core/JobSystem.h>
#include <unordered_map>
#include <set>

namespace Manro {
    Model::PreparedAssets Model::Prepare(const std::vector<std::string> &paths, JobSystem &jobs) {
        PreparedAssets prepared;
        prepared.subMeshes = ModelLoader::LoadSubMeshes(paths, jobs);
        std::set<std::string> uniqueTexturePaths;
        for (const auto &modelSubMeshes: prepared.subMeshes) {
            for (const auto &sm: modelSubMeshes) {
                if (!sm.diffuseTexturePath.empty()) {
                    uniqueTexturePaths.insert(sm.diffuseTexturePath);
                }
                if (!sm.normalTexturePath.empty()) {
                    uniqueTexturePaths.insert(sm.normalTexturePath);
                }
            }
        }

        prepared.texturePaths.assign(uniqueTexturePaths.begin(), uniqueTexturePaths.end());
        prepared.textures = TextureLoader::Load(prepared.texturePaths, jobs);
        return prepared;
    }

    std::vector<Scope<Model> > Model::CommitPrepared(PreparedAssets prepared, Renderer &renderer) {
        std::vector<Scope<Model> > results;
        results.reserve(prepared.subMeshes.size());
        std::unordered_map<std::string, TextureHandle> textureCache;
        for (size_t i = 0; i < prepared.texturePaths.size(); ++i) {
            if (!prepared.textures[i].pixels.empty()) {
                TextureHandle tex = renderer.UploadTexture(prepared.textures[i]);
                textureCache[prepared.texturePaths[i]] = tex;
            }
        }

        for (size_t i = 0; i < prepared.subMeshes.size(); ++i) {
            auto model = CreateScope<Model>();
            for (auto &sd: prepared.subMeshes[i]) {
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
        return results;
    }

    std::vector<Scope<Model> > Model::Load(const std::vector<std::string> &paths,
                                           Renderer &renderer,
                                           JobSystem &jobs) {
        auto prepared = Prepare(paths, jobs);
        auto results = CommitPrepared(std::move(prepared), renderer);
        LOG_INFO("[Model] Load of {} models completed", paths.size());
        return results;
    }
} // namespace Manro