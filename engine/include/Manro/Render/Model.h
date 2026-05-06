#pragma once

#include <Manro/Core/Types.h>
#include <Manro/Resource/ModelLoader.h>
#include <Manro/Resource/TextureLoader.h>
#include <Manro/Render/MeshManager.h>
#include <Manro/Render/Material/MaterialInstance.h>
#include <vector>
#include <string>

namespace Manro {
    class CRenderer;

    class CJobSystem;

    class CVirtualFS;

    struct ModelSubMesh_t {
        MeshHandle meshId;
        Scope<CMaterialInstance> material;
    };

    class CModel {
    public:
        struct PreparedAssets_t {
            std::vector<std::vector<SubMeshData_t> > subMeshes;
            std::vector<std::string> texturePaths;
            std::vector<TextureData_t> textures;
        };

        CModel() = default;

        ~CModel() = default;

        static PreparedAssets_t Prepare(const std::vector<std::string> &paths,
                                        CJobSystem &jobs, CVirtualFS &vfs);

        static std::vector<Scope<CModel> > CommitPrepared(PreparedAssets_t prepared, CRenderer &renderer);

        static std::vector<Scope<CModel> > Load(const std::vector<std::string> &paths,
                                                CRenderer &renderer,
                                                CJobSystem &jobs,
                                                CVirtualFS &vfs);

        void AddSubMesh(MeshHandle meshId, Scope<CMaterialInstance> material) {
            m_SubMeshes.push_back({meshId, std::move(material)});
        }

        const std::vector<ModelSubMesh_t> &GetSubMeshes() const { return m_SubMeshes; }

    private:
        std::vector<ModelSubMesh_t> m_SubMeshes;
    };
} // namespace Manro