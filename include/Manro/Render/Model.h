#pragma once

#include <Manro/Core/Types.h>
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
        Model() = default;
        ~Model() = default;

        static std::vector<Scope<Model>> Load(const std::vector<std::string>& paths,
                                               Renderer& renderer,
                                               JobSystem& jobs);

        void AddSubMesh(MeshHandle meshId, Scope<MaterialInstance> material) {
            m_SubMeshes.push_back({meshId, std::move(material)});
        }

        const std::vector<ModelSubMesh>& GetSubMeshes() const { return m_SubMeshes; }

    private:
        std::vector<ModelSubMesh> m_SubMeshes;
    };
} // namespace Manro
