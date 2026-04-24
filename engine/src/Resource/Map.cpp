#include <Manro/Resource/Map.h>
#include <Manro/Core/Logger.h>
#include <Manro/Core/VirtualFS.h>
#include <Manro/Resource/Pack.h>

#include <imgui.h>
#include <ImGuizmo.h>
#include <glm/gtc/type_ptr.hpp>
#include <nlohmann/json.hpp>

#include <fstream>
#include <sstream>
#include <unordered_set>

namespace fs = std::filesystem;

namespace Manro {
    using nlohmann::json;

    namespace {
        json V3(const Vec3 &v) { return json::array({v.x, v.y, v.z}); }

        Vec3 V3from(const json &j, const Vec3 &def = {}) {
            if (!j.is_array() || j.size() != 3) return def;
            return {j[0].get<float>(), j[1].get<float>(), j[2].get<float>()};
        }
    } // namespace

    std::string CMap::Serialize() const {
        json root;
        root["version"] = 1;
        root["skyboxPath"] = m_SkyboxPath;

        json &ents = root["entities"] = json::array();
        for (const auto &e: m_Entities) {
            ents.push_back({
                {"name", e.name},
                {"modelPath", e.modelPath},
                {"position", V3(e.position)},
                {"rotation", V3(e.rotation)},
                {"scale", V3(e.scale)},
            });
        }

        json &lights = root["lights"] = json::array();
        for (const auto &l: m_Lights) {
            lights.push_back({
                {"name", l.name},
                {"type", l.type},
                {"position", V3(l.position)},
                {"direction", V3(l.direction)},
                {"color", V3(l.color)},
                {"intensity", l.intensity},
                {"range", l.range},
            });
        }
        return root.dump(2);
    }

    bool CMap::Deserialize(const std::string &text) {
        try {
            json root = json::parse(text);
            Clear();
            m_SkyboxPath = root.value("skyboxPath", std::string{});

            if (root.contains("entities")) {
                for (const auto &je: root["entities"]) {
                    MapEntity_t e;
                    e.name = je.value("name", std::string{});
                    e.modelPath = je.value("modelPath", std::string{});
                    e.position = V3from(je.value("position", json{}), {});
                    e.rotation = V3from(je.value("rotation", json{}), {});
                    e.scale = V3from(je.value("scale", json{}), Vec3{1.f});
                    m_Entities.push_back(std::move(e));
                }
            }
            if (root.contains("lights")) {
                for (const auto &jl: root["lights"]) {
                    MapLight_t l;
                    l.name = jl.value("name", std::string{});
                    l.type = jl.value("type", 0);
                    l.position = V3from(jl.value("position", json{}), {});
                    l.direction = V3from(jl.value("direction", json{}),
                                         Vec3{0, -1, 0});
                    l.color = V3from(jl.value("color", json{}), Vec3{1.f});
                    l.intensity = jl.value("intensity", 1.f);
                    l.range = jl.value("range", 1000.f);
                    m_Lights.push_back(std::move(l));
                }
            }
            return true;
        } catch (const std::exception &) {
            return false;
        }
    }

    bool CMap::SaveToFile(const fs::path &path) const {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        const std::string s = Serialize();
        out.write(s.data(), static_cast<std::streamsize>(s.size()));
        return static_cast<bool>(out);
    }

    bool CMap::LoadFromFile(const fs::path &path) {
        std::ifstream in(path, std::ios::binary);
        if (!in) return false;
        std::stringstream ss;
        ss << in.rdbuf();
        return Deserialize(ss.str());
    }

    bool CMap::LoadFromVfs(const std::string &vfsPath) {
        auto bytes = CVirtualFS::Get().ReadFile(vfsPath);
        if (bytes.empty()) return false;
        std::string text(reinterpret_cast<const char *>(bytes.data()),
                         bytes.size());
        return Deserialize(text);
    }

    Mat4 CMap::ComposeEntityTransform(const MapEntity_t &e) {
        Mat4 m;
        const float t[3] = {e.position.x, e.position.y, e.position.z};
        const float r[3] = {e.rotation.x, e.rotation.y, e.rotation.z};
        const float s[3] = {e.scale.x, e.scale.y, e.scale.z};
        ImGuizmo::RecomposeMatrixFromComponents(t, r, s, glm::value_ptr(m));
        return m;
    }

    std::string CMap::PackToRres(const fs::path &outputRres,
                                 const fs::path &projectDir) const {
        if (projectDir.empty() || !fs::is_directory(projectDir))
            return "project dir does not exist: " + projectDir.string();

        LOG_INFO("[CMap::PackToRres] {} entities, project={}, out={}",
                 m_Entities.size(), projectDir.string(), outputRres.string());

        const fs::path stage = fs::temp_directory_path() /
                               ("manro-map-pack-" + std::to_string(
                                    std::hash<std::string>{}(outputRres.string()) ^
                                    std::hash<std::size_t>{}(fs::file_time_type::clock::now()
                                        .time_since_epoch().count())));
        std::error_code ec;
        fs::create_directories(stage, ec);
        if (ec) return "create temp: " + ec.message();

        if (!SaveToFile(stage / "map.mmap"))
            return "write map.mmap";

        std::unordered_set<std::string> copiedDirs;
        size_t copiedFiles = 0;
        for (const auto &e: m_Entities) {
            if (e.modelPath.empty()) {
                LOG_WARN("[CMap::PackToRres] entity '{}' has empty modelPath", e.name);
                continue;
            }

            // Resolve the on disk source
            const fs::path raw = e.modelPath;
            const fs::path src = raw.is_absolute() ? raw : projectDir / raw;
            if (!fs::is_regular_file(src, ec)) {
                fs::remove_all(stage, ec);
                return "model not found: " + src.string();
            }

            fs::path rel = fs::relative(src, projectDir, ec);
            if (ec || rel.empty() || rel.native().rfind("..", 0) == 0) {
                fs::remove_all(stage, ec);
                return "model lives outside project root, can't pack: "
                       + src.string();
            }

            const fs::path srcDir = src.parent_path();
            const fs::path dstDir = stage / rel.parent_path();
            fs::create_directories(dstDir, ec);

            const fs::path dstFile = dstDir / src.filename();
            if (fs::copy_file(src, dstFile, fs::copy_options::overwrite_existing, ec))
                ++copiedFiles;
            else if (ec)
                LOG_WARN("[CMap::PackToRres] copy_file({} -> {}) failed: {}",
                         src.string(), dstFile.string(), ec.message());

            if (!copiedDirs.insert(srcDir.string()).second) continue;

            std::error_code itEc;
            auto it = fs::recursive_directory_iterator(
                srcDir, fs::directory_options::skip_permission_denied, itEc);
            if (itEc) {
                LOG_WARN("[CMap::PackToRres] iterate {} failed: {}",
                         srcDir.string(), itEc.message());
                continue;
            }
            for (; it != fs::recursive_directory_iterator(); it.increment(itEc)) {
                if (itEc) break;
                std::error_code regEc;
                if (!fs::is_regular_file(it->path(), regEc)) continue;
                if (fs::equivalent(it->path(), src, regEc)) continue;
                const fs::path sibRel = fs::relative(it->path(), srcDir, regEc);
                if (regEc || sibRel.empty()) continue;
                const fs::path dst = dstDir / sibRel;
                fs::create_directories(dst.parent_path(), regEc);
                if (fs::copy_file(it->path(), dst,
                                  fs::copy_options::overwrite_existing, regEc))
                    ++copiedFiles;
            }
        }
        LOG_INFO("[CMap::PackToRres] staged {} files across {} dir(s)",
                 copiedFiles, copiedDirs.size());

        const auto res = Pack::PackDirectory(stage, outputRres, {});
        fs::remove_all(stage, ec);
        if (!res.ok) return res.error;
        LOG_INFO("[CMap::PackToRres] wrote {} ({} entries)",
                 outputRres.string(), res.entryCount);
        return {};
    }
} // namespace Manro
