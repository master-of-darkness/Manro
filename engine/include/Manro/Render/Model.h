#pragma once

#include <Manro/Core/Types.h>
#include <Manro/Resource/ModelLoader.h>
#include <Manro/Resource/TextureLoader.h>
#include <Manro/Render/MeshManager.h>
#include <Manro/Render/Material/MaterialInstance.h>
#include <vector>
#include <string>

namespace Manro {
    class Renderer;

    class JobSystem;

    struct ModelSubMesh {
        MeshHandle meshId;
        Scope<MaterialInstance> material;
    };

    class Model {
    public:
        struct PreparedAssets {
            std::vector<std::vector<SubMeshData> > subMeshes;
            std::vector<std::string> texturePaths;
            std::vector<TextureData> textures;
        };

        Model() = default;

        ~Model() = default;

        static PreparedAssets Prepare(const std::vector<std::string> &paths, JobSystem &jobs);

        static std::vector<Scope<Model> > CommitPrepared(PreparedAssets prepared, Renderer &renderer);

        static std::vector<Scope<Model> > Load(const std::vector<std::string> &paths,
                                               Renderer &renderer,
                                               JobSystem &jobs);

        void AddSubMesh(MeshHandle meshId, Scope<MaterialInstance> material) {
            m_SubMeshes.push_back({meshId, std::move(material)});
        }

        const std::vector<ModelSubMesh> &GetSubMeshes() const { return m_SubMeshes; }

    private:
        std::vector<ModelSubMesh> m_SubMeshes;
    };
} // namespace Manro