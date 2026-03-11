#include "FBXLoader.h"

#include <ufbx.h>
#include <Core/Logger.h>
#include <algorithm>
#include <unordered_map>

namespace Engine {
    static std::string NormalisePath(const std::string &p) {
        std::string out = p;
        std::replace(out.begin(), out.end(), '\\', '/');
        return out;
    }

    static std::string GetBaseDir(const std::string &filepath) {
        auto slash = filepath.find_last_of("/\\");
        if (slash != std::string::npos)
            return filepath.substr(0, slash + 1);
        return {};
    }

    static std::string ResolveTexturePath(const ufbx_texture *tex, const std::string &baseDir) {
        if (!tex) return {};

        if (tex->relative_filename.length > 0) {
            return NormalisePath(baseDir + std::string(
                                     tex->relative_filename.data, tex->relative_filename.length));
        }

        if (tex->filename.length > 0) {
            return NormalisePath(std::string(
                tex->filename.data, tex->filename.length));
        }

        return {};
    }

    static std::string ResolveDiffuseTexture(const ufbx_material *mat, const std::string &baseDir) {
        if (!mat) return {};

        ufbx_material_map diffuseMap = mat->pbr.base_color;
        if (diffuseMap.texture) {
            std::string path = ResolveTexturePath(diffuseMap.texture, baseDir);
            if (!path.empty()) return path;
        }

        diffuseMap = mat->fbx.diffuse_color;
        if (diffuseMap.texture) {
            std::string path = ResolveTexturePath(diffuseMap.texture, baseDir);
            if (!path.empty()) return path;
        }

        return {};
    }

    static Vec3 GetMaterialDiffuseColor(const ufbx_material *mat) {
        if (!mat) return Vec3{0.85f, 0.82f, 0.78f};

        ufbx_material_map diffuseMap = mat->fbx.diffuse_color;
        if (diffuseMap.has_value) {
            return Vec3{
                static_cast<float>(diffuseMap.value_vec4.x),
                static_cast<float>(diffuseMap.value_vec4.y),
                static_cast<float>(diffuseMap.value_vec4.z)
            };
        }
        return Vec3{0.85f, 0.82f, 0.78f};
    }

    static void TriangulateMesh(const ufbx_mesh *mesh, int matIdx,
                                SubMeshData &out, const AxisRemap &remap) {
        struct VertKey {
            uint32_t posIdx;
            uint32_t uvIdx;
            int matId;

            bool operator==(const VertKey &o) const {
                return posIdx == o.posIdx && uvIdx == o.uvIdx && matId == o.matId;
            }
        };

        struct VertKeyHash {
            size_t operator()(const VertKey &k) const {
                size_t h = std::hash<uint32_t>{}(k.posIdx);
                h ^= std::hash<uint32_t>{}(k.uvIdx) + 0x9e3779b9 + (h << 6) + (h >> 2);
                h ^= std::hash<int>{}(k.matId) + 0x9e3779b9 + (h << 6) + (h >> 2);
                return h;
            }
        };

        const ufbx_material *mat = (matIdx >= 0 && matIdx < static_cast<int>(mesh->materials.count))
                                       ? mesh->materials.data[matIdx]
                                       : nullptr;
        Vec3 diffuseColor = GetMaterialDiffuseColor(mat);
        bool hasUV = mesh->vertex_uv.exists;

        std::unordered_map<VertKey, u32, VertKeyHash> indexMap;

        // Iterate all faces, filter by material, triangulate
        for (size_t fi = 0; fi < mesh->faces.count; ++fi) {
            ufbx_face face = mesh->faces.data[fi];

            // Check material
            if (mesh->face_material.count > 0) {
                uint32_t faceMat = mesh->face_material.data[fi];
                if (matIdx >= 0 && faceMat != static_cast<uint32_t>(matIdx))
                    continue;
            }

            // Triangulate the face (fan triangulation)
            for (uint32_t t = 1; t + 1 < face.num_indices; ++t) {
                uint32_t triIndices[3] = {
                    face.index_begin,
                    face.index_begin + t,
                    face.index_begin + t + 1
                };

                for (int vi = 0; vi < 3; ++vi) {
                    uint32_t idx = triIndices[vi];
                    uint32_t posIdx = mesh->vertex_indices.data[idx];
                    uint32_t uvIdx = hasUV ? mesh->vertex_uv.indices.data[idx] : 0;

                    VertKey key{posIdx, uvIdx, matIdx};
                    auto it = indexMap.find(key);
                    if (it != indexMap.end()) {
                        out.indices.push_back(it->second);
                    } else {
                        Vertex vert{};
                        ufbx_vec3 p = mesh->vertices.data[posIdx];
                        vert.position = remap.Apply(Vec3{
                            static_cast<float>(p.x),
                            static_cast<float>(p.y),
                            static_cast<float>(p.z)
                        });
                        vert.color = diffuseColor;
                        if (hasUV) {
                            ufbx_vec2 uv = mesh->vertex_uv.values.data[uvIdx];
                            vert.uv = Vec2{
                                static_cast<float>(uv.x),
                                static_cast<float>(uv.y)
                            };
                        }
                        u32 outIdx = static_cast<u32>(out.vertices.size());
                        out.vertices.push_back(vert);
                        indexMap[key] = outIdx;
                        out.indices.push_back(outIdx);
                    }
                }
            }
        }
    }

    bool FBXLoader::Load(const std::string &filepath, ModelData &out,
                         const AxisRemap &remap) {
        ufbx_load_opts opts = {};
        ufbx_error error;
        ufbx_scene *scene = ufbx_load_file(filepath.c_str(), &opts, &error);

        if (!scene) {
            LOG_ERROR("[FBXLoader] Failed to load '{}': {}",
                      filepath, std::string(error.description.data, error.description.length));
            return false;
        }

        std::string baseDir = GetBaseDir(filepath);

        out.vertices.clear();
        out.indices.clear();
        out.diffuseTexturePath.clear();

        // Find first diffuse texture
        for (size_t mi = 0; mi < scene->materials.count && out.diffuseTexturePath.empty(); ++mi) {
            out.diffuseTexturePath = ResolveDiffuseTexture(scene->materials.data[mi], baseDir);
        }

        SubMeshData combined;
        for (size_t ni = 0; ni < scene->meshes.count; ++ni) {
            const ufbx_mesh *mesh = scene->meshes.data[ni];
            TriangulateMesh(mesh, -1, combined, remap);
        }

        out.vertices = std::move(combined.vertices);
        out.indices = std::move(combined.indices);

        LOG_INFO("[FBXLoader] Loaded '{}' - {} vertices, {} indices",
                 filepath, out.vertices.size(), out.indices.size());

        ufbx_free_scene(scene);
        return true;
    }

    bool FBXLoader::LoadSubMeshes(const std::string &filepath,
                                  std::vector<SubMeshData> &out,
                                  const AxisRemap &remap) {
        ufbx_load_opts opts = {};
        ufbx_error error;
        ufbx_scene *scene = ufbx_load_file(filepath.c_str(), &opts, &error);

        if (!scene) {
            LOG_ERROR("[FBXLoader] Failed to load '{}': {}",
                      filepath, std::string(error.description.data, error.description.length));
            return false;
        }

        std::string baseDir = GetBaseDir(filepath);

        // Build one bucket per material + one for no-material faces
        size_t numMats = scene->materials.count;
        std::vector<SubMeshData> buckets(numMats + 1);

        for (size_t mi = 0; mi < numMats; ++mi) {
            buckets[mi].diffuseTexturePath = ResolveDiffuseTexture(
                scene->materials.data[mi], baseDir);
        }

        for (size_t ni = 0; ni < scene->meshes.count; ++ni) {
            const ufbx_mesh *mesh = scene->meshes.data[ni];

            if (mesh->materials.count > 0 && mesh->face_material.count > 0) {
                // Build a map from mesh-local material index to scene-global bucket index
                std::vector<size_t> localToGlobal(mesh->materials.count, numMats);
                for (size_t li = 0; li < mesh->materials.count; ++li) {
                    const ufbx_material *localMat = mesh->materials.data[li];
                    for (size_t gi = 0; gi < numMats; ++gi) {
                        if (scene->materials.data[gi] == localMat) {
                            localToGlobal[li] = gi;
                            break;
                        }
                    }
                }

                for (size_t li = 0; li < mesh->materials.count; ++li) {
                    TriangulateMesh(mesh, static_cast<int>(li), buckets[localToGlobal[li]], remap);
                }
            } else {
                TriangulateMesh(mesh, -1, buckets[numMats], remap);
            }
        }

        out.clear();
        for (auto &b: buckets) {
            if (!b.vertices.empty())
                out.push_back(std::move(b));
        }

        LOG_INFO("[FBXLoader] LoadSubMeshes '{}' -> {} sub-meshes", filepath, out.size());

        ufbx_free_scene(scene);
        return true;
    }
} // namespace Engine