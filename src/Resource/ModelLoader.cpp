#include <Manro/Resource/ModelLoader.h>

#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

#include <mikktspace.h>
#include <Manro/Core/Logger.h>
#include <algorithm>
#include <unordered_map>

namespace Manro {
    // MikkTSpace interface
    struct MikkContext {
        std::vector<Vertex>* vertices;
        const std::vector<u32>* indices;
    };

    static int MikkGetNumFaces(const SMikkTSpaceContext* context) {
        MikkContext* ctx = (MikkContext*)context->m_pUserData;
        return (int)(ctx->indices->size() / 3);
    }

    static int MikkGetNumVerticesOfFace(const SMikkTSpaceContext* context, const int iFace) {
        return 3;
    }

    static void MikkGetPosition(const SMikkTSpaceContext* context, float fvPosOut[], const int iFace, const int iVert) {
        MikkContext* ctx = (MikkContext*)context->m_pUserData;
        u32 index = (*ctx->indices)[iFace * 3 + iVert];
        const Vec3& pos = (*ctx->vertices)[index].position;
        fvPosOut[0] = pos.x; fvPosOut[1] = pos.y; fvPosOut[2] = pos.z;
    }

    static void MikkGetNormal(const SMikkTSpaceContext* context, float fvNormOut[], const int iFace, const int iVert) {
        MikkContext* ctx = (MikkContext*)context->m_pUserData;
        u32 index = (*ctx->indices)[iFace * 3 + iVert];
        const Vec3& norm = (*ctx->vertices)[index].normal;
        fvNormOut[0] = norm.x; fvNormOut[1] = norm.y; fvNormOut[2] = norm.z;
    }

    static void MikkGetTexCoord(const SMikkTSpaceContext* context, float fvTexcOut[], const int iFace, const int iVert) {
        MikkContext* ctx = (MikkContext*)context->m_pUserData;
        u32 index = (*ctx->indices)[iFace * 3 + iVert];
        const Vec2& uv = (*ctx->vertices)[index].uv;
        fvTexcOut[0] = uv.x; fvTexcOut[1] = uv.y;
    }

    static void MikkSetTSpaceBasic(const SMikkTSpaceContext* context, const float fvTangent[], const float fSign, const int iFace, const int iVert) {
        MikkContext* ctx = (MikkContext*)context->m_pUserData;
        u32 index = (*ctx->indices)[iFace * 3 + iVert];
        Vertex& v = (*ctx->vertices)[index];
        v.tangent = { fvTangent[0], fvTangent[1], fvTangent[2], fSign };
    }

    static void GenerateTangents(std::vector<Vertex>& vertices, const std::vector<u32>& indices) {
        MikkContext mikkCtx = { &vertices, &indices };
        SMikkTSpaceInterface mikkInterface = {};
        mikkInterface.m_getNumFaces = MikkGetNumFaces;
        mikkInterface.m_getNumVerticesOfFace = MikkGetNumVerticesOfFace;
        mikkInterface.m_getPosition = MikkGetPosition;
        mikkInterface.m_getNormal = MikkGetNormal;
        mikkInterface.m_getTexCoord = MikkGetTexCoord;
        mikkInterface.m_setTSpaceBasic = MikkSetTSpaceBasic;

        SMikkTSpaceContext context = {};
        context.m_pInterface = &mikkInterface;
        context.m_pUserData = &mikkCtx;

        genTangSpaceDefault(&context);
    }

    std::string ModelLoader::NormalisePath(const std::string &p) {
        std::string out = p;
        std::replace(out.begin(), out.end(), '\\', '/');
        return out;
    }

    struct VertKey {
        int vi, ni, ti, matId;

        bool operator==(const VertKey &o) const {
            return vi == o.vi && ni == o.ni && ti == o.ti && matId == o.matId;
        }
    };

    struct VertKeyHash {
        size_t operator()(const VertKey &k) const {
            size_t h = std::hash<int>{}(k.vi);
            h ^= std::hash<int>{}(k.ni) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<int>{}(k.ti) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<int>{}(k.matId) + 0x9e3779b9 + (h << 6) + (h >> 2);
            return h;
        }
    };

    bool ModelLoader::Load(const std::string &filepath, ModelData &out) {
        tinyobj::attrib_t attrib;
        std::vector<tinyobj::shape_t> shapes;
        std::vector<tinyobj::material_t> materials;
        std::string warn, err;

        std::string baseDir;
        auto slash = filepath.find_last_of("/\\");
        if (slash != std::string::npos)
            baseDir = filepath.substr(0, slash + 1);

        bool ok = tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err,
                                   filepath.c_str(),
                                   baseDir.empty() ? nullptr : baseDir.c_str());

        if (!warn.empty())
            LOG_WARN("[ModelLoader] Warning ({}): {}", filepath, warn);
        if (!err.empty())
            LOG_ERROR("[ModelLoader] Error ({}): {}", filepath, err);
        if (!ok) return false;

        out.vertices.clear();
        out.indices.clear();
        out.diffuseTexturePath.clear();

        for (const auto &mat: materials) {
            if (!mat.diffuse_texname.empty()) {
                out.diffuseTexturePath = NormalisePath(baseDir + mat.diffuse_texname);
                break;
            }
        }

        std::unordered_map<VertKey, u32, VertKeyHash> indexMap;

        for (const auto &shape: shapes) {
            size_t indexOffset = 0;
            for (size_t f = 0; f < shape.mesh.num_face_vertices.size(); ++f) {
                int faceVerts = shape.mesh.num_face_vertices[f];
                int matId = shape.mesh.material_ids.empty() ? -1 : shape.mesh.material_ids[f];

                for (int v = 0; v < faceVerts; ++v) {
                    tinyobj::index_t idx = shape.mesh.indices[indexOffset + v];
                    VertKey key{idx.vertex_index, idx.normal_index, idx.texcoord_index, matId};

                    auto it = indexMap.find(key);
                    if (it != indexMap.end()) {
                        out.indices.push_back(it->second);
                    } else {
                        Vertex vert{};
                        vert.position = {
                            attrib.vertices[3 * idx.vertex_index + 0],
                            attrib.vertices[3 * idx.vertex_index + 1],
                            attrib.vertices[3 * idx.vertex_index + 2],
                        };
                        
                        if (idx.normal_index >= 0 &&
                            3 * idx.normal_index + 2 < static_cast<int>(attrib.normals.size())) {
                            vert.normal = {
                                attrib.normals[3 * idx.normal_index + 0],
                                attrib.normals[3 * idx.normal_index + 1],
                                attrib.normals[3 * idx.normal_index + 2],
                            };
                        } else {
                            vert.normal = {0.0f, 1.0f, 0.0f};
                        }

                        if (idx.texcoord_index >= 0 &&
                            2 * idx.texcoord_index + 1 < static_cast<int>(attrib.texcoords.size())) {
                            vert.uv = {
                                attrib.texcoords[2 * idx.texcoord_index + 0],
                                attrib.texcoords[2 * idx.texcoord_index + 1],
                            };
                        }
                        
                        vert.tangent = {1.0f, 0.0f, 0.0f, 1.0f};

                        u32 outIdx = static_cast<u32>(out.vertices.size());
                        out.vertices.push_back(vert);
                        indexMap[key] = outIdx;
                        out.indices.push_back(outIdx);
                    }
                }
                indexOffset += faceVerts;
            }
        }

        GenerateTangents(out.vertices, out.indices);

        if (!out.diffuseTexturePath.empty())
            LOG_INFO("[ModelLoader] Loaded '{}' - {} vertices, {} indices, texture: {}",
                 filepath, out.vertices.size(), out.indices.size(), out.diffuseTexturePath);
        else
            LOG_INFO("[ModelLoader] Loaded '{}' - {} vertices, {} indices",
                 filepath, out.vertices.size(), out.indices.size());
        return true;
    }

    bool ModelLoader::LoadSubMeshes(const std::string &filepath,
                                    std::vector<SubMeshData> &out) {
        tinyobj::attrib_t attrib;
        std::vector<tinyobj::shape_t> shapes;
        std::vector<tinyobj::material_t> materials;
        std::string warn, err;

        std::string baseDir;
        auto slash = filepath.find_last_of("/\\");
        if (slash != std::string::npos)
            baseDir = filepath.substr(0, slash + 1);

        bool ok = tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err,
                                   filepath.c_str(),
                                   baseDir.empty() ? nullptr : baseDir.c_str());

        if (!warn.empty())
            LOG_WARN("[ModelLoader] Warning ({}): {}", filepath, warn);
        if (!err.empty())
            LOG_ERROR("[ModelLoader] Error ({}): {}", filepath, err);
        if (!ok) return false;

        const int kNoMat = static_cast<int>(materials.size());
        std::vector<SubMeshData> buckets(materials.size() + 1);

        for (int i = 0; i < static_cast<int>(materials.size()); ++i) {
            if (!materials[i].diffuse_texname.empty())
                buckets[i].diffuseTexturePath = NormalisePath(baseDir + materials[i].diffuse_texname);
        }

        std::vector<std::unordered_map<VertKey, u32, VertKeyHash> > indexMaps(buckets.size());

        for (const auto &shape: shapes) {
            size_t indexOffset = 0;
            for (size_t f = 0; f < shape.mesh.num_face_vertices.size(); ++f) {
                int faceVerts = shape.mesh.num_face_vertices[f];
                int matId = shape.mesh.material_ids.empty() ? -1 : shape.mesh.material_ids[f];
                int bucketIdx = (matId >= 0 && matId < kNoMat) ? matId : kNoMat;

                SubMeshData &bucket = buckets[bucketIdx];
                auto &imap = indexMaps[bucketIdx];

                for (int v = 0; v < faceVerts; ++v) {
                    tinyobj::index_t idx = shape.mesh.indices[indexOffset + v];
                    VertKey key{idx.vertex_index, idx.normal_index, idx.texcoord_index, matId};

                    auto it = imap.find(key);
                    if (it != imap.end()) {
                        bucket.indices.push_back(it->second);
                    } else {
                        Vertex vert{};
                        vert.position = {
                            attrib.vertices[3 * idx.vertex_index + 0],
                            attrib.vertices[3 * idx.vertex_index + 1],
                            attrib.vertices[3 * idx.vertex_index + 2],
                        };
                        
                        if (idx.normal_index >= 0 &&
                            3 * idx.normal_index + 2 < static_cast<int>(attrib.normals.size())) {
                            vert.normal = {
                                attrib.normals[3 * idx.normal_index + 0],
                                attrib.normals[3 * idx.normal_index + 1],
                                attrib.normals[3 * idx.normal_index + 2],
                            };
                        } else {
                            vert.normal = {0.0f, 1.0f, 0.0f};
                        }

                        if (idx.texcoord_index >= 0 &&
                            2 * idx.texcoord_index + 1 < static_cast<int>(attrib.texcoords.size())) {
                            vert.uv = {
                                attrib.texcoords[2 * idx.texcoord_index + 0],
                                attrib.texcoords[2 * idx.texcoord_index + 1],
                            };
                        }

                        vert.tangent = {1.0f, 0.0f, 0.0f, 1.0f};

                        u32 outIdx = static_cast<u32>(bucket.vertices.size());
                        bucket.vertices.push_back(vert);
                        imap[key] = outIdx;
                        bucket.indices.push_back(outIdx);
                    }
                }
                indexOffset += faceVerts;
            }
        }

        out.clear();
        for (auto &b: buckets) {
            if (!b.vertices.empty()) {
                GenerateTangents(b.vertices, b.indices);
                out.push_back(std::move(b));
            }
        }

        LOG_INFO("[ModelLoader] LoadSubMeshes '{}' → {} sub-meshes", filepath, out.size());
        return true;
    }
} // namespace Manro
