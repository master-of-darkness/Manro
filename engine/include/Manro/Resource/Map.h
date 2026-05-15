#pragma once

#include <Manro/Core/Types.h>
#include <filesystem>
#include <string>
#include <vector>

namespace Manro {
    class CVirtualFS;
    namespace Pack {
        struct PackResult;
    }

    struct MapEntity_t {
        std::string name;
        std::string modelPath; // VFS path
        Vec3 position{0.f};
        Vec3 rotation{0.f}; // Euler degrees
        Vec3 scale{1.f};
    };

    struct MapLight_t {
        std::string name;
        int type{0}; // 0 = directional, 1 = point
        Vec3 position{0.f};
        Vec3 direction{0.f, -1.f, 0.f};
        Vec3 color{1.f};
        f32 intensity{1.f};
        f32 range{1000.f};
    };

    class CMap {
    public:
        CMap() = default;

        std::vector<MapEntity_t> &Entities() { return m_Entities; }
        const std::vector<MapEntity_t> &Entities() const { return m_Entities; }
        std::vector<MapLight_t> &Lights() { return m_Lights; }
        const std::vector<MapLight_t> &Lights() const { return m_Lights; }
        const std::string &SkyboxPath() const { return m_SkyboxPath; }
        void SetSkyboxPath(std::string p) { m_SkyboxPath = std::move(p); }

        void Clear() { *this = CMap{}; }

        std::string Serialize() const;

        bool Deserialize(const std::string &json);

        bool LoadFromFile(const std::filesystem::path &path);

        bool SaveToFile(const std::filesystem::path &path) const;

        bool LoadFromVfs(const std::string &vfsPath, CVirtualFS &vfs);

        Pack::PackResult PackToRres(const std::filesystem::path &outputRres,
                                    const std::filesystem::path &projectDir) const;

        // Matches ImGuizmo::RecomposeMatrixFromComponents
        static Mat4 ComposeEntityTransform(const MapEntity_t &e);

    private:
        std::vector<MapEntity_t> m_Entities;
        std::vector<MapLight_t> m_Lights;
        std::string m_SkyboxPath;
    };
} // namespace Manro
