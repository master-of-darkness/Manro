#include <Manro/Resource/ModelLoader.h>
#include <Manro/Core/JobSystem.h>
#include <Manro/Core/Logger.h>
#include <Manro/Core/VirtualFS.h>

#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define TINYGLTF_NO_INCLUDE_STB_IMAGE
#include <stb_image.h>
#include <tiny_gltf.h>

#include <mikktspace.h>
#include <algorithm>
#include <unordered_map>
#include <sstream>
#include <filesystem>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace Manro {
    using Mat3 = glm::mat3;

    class VirtualFSMaterialReader : public tinyobj::MaterialReader {
    public:
        explicit VirtualFSMaterialReader(std::string baseDir) : m_BaseDir(std::move(baseDir)) {}

        bool operator()(const std::string &matId,
                        std::vector<tinyobj::material_t> *materials,
                        std::map<std::string, int> *matMap, std::string *warn,
                        std::string *err) override {
            std::string filepath = m_BaseDir + matId;
            auto &vfs = VirtualFS::Get();
            std::vector<u8> data = vfs.ReadFile(filepath);
            if (data.empty()) {
                if (err) (*err) += "Failed to load material file: " + filepath + "\n";
                return false;
            }

            std::string content(reinterpret_cast<const char *>(data.data()), data.size());
            std::stringstream ss(content);
            tinyobj::LoadMtl(matMap, materials, &ss, warn, err);
            return true;
        }

    private:
        std::string m_BaseDir;
    };

    // tinygltf VirtualFS callbacks
    static bool VfsFileExists(const std::string &abs_filename, void *user_data) {
        return VirtualFS::Get().FileExists(abs_filename);
    }

    static std::string VfsExpandFilePath(const std::string &filepath, void *user_data) {
        return filepath;
    }

    static bool VfsReadWholeFile(std::vector<unsigned char> *out, std::string *err,
                                 const std::string &filepath, void *user_data) {
        auto data = VirtualFS::Get().ReadFile(filepath);
        if (data.empty()) {
            if (err) *err = "Failed to read file: " + filepath;
            return false;
        }
        *out = std::move(data);
        return true;
    }

    static bool VfsWriteWholeFile(std::string *err, const std::string &filepath,
                                  const std::vector<unsigned char> &contents, void *user_data) {
        return false;
    }

    static bool VfsGetFileSizeInBytes(size_t *filesize_out, std::string *err,
                                      const std::string &abs_filename, void *userdata) {
        return VirtualFS::Get().GetFileSize(abs_filename, *filesize_out);
    }

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
        if (indices.empty()) return;
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

    static bool HasExtension(const std::string &path, const std::string &ext) {
        if (path.size() < ext.size()) return false;
        std::string tail = path.substr(path.size() - ext.size());
        std::transform(tail.begin(), tail.end(), tail.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return tail == ext;
    }

    static void LoadObj(const std::string &filepath, std::vector<SubMeshData> &out) {
        tinyobj::attrib_t attrib;
        std::vector<tinyobj::shape_t> shapes;
        std::vector<tinyobj::material_t> materials;
        std::string warn, err;

        std::string baseDir;
        auto slash = filepath.find_last_of("/\\");
        if (slash != std::string::npos)
            baseDir = filepath.substr(0, slash + 1);

        std::vector<u8> objData = VirtualFS::Get().ReadFile(filepath);
        if (objData.empty()) return;
        std::string objContent(reinterpret_cast<const char *>(objData.data()), objData.size());
        std::stringstream ss(objContent);

        VirtualFSMaterialReader matReader(baseDir);
        bool ok = tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, &ss, &matReader);

        if (!ok) {
            LOG_ERROR("[ModelLoader] Failed to load OBJ: {} - {}", filepath, err);
            return;
        }

        const int kNoMat = static_cast<int>(materials.size());
        std::vector<SubMeshData> buckets(materials.size() + 1);

        for (int i = 0; i < static_cast<int>(materials.size()); ++i) {
            if (!materials[i].diffuse_texname.empty())
                buckets[i].diffuseTexturePath = ModelLoader::NormalisePath(baseDir + materials[i].diffuse_texname);
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

        for (auto &b: buckets) {
            if (!b.vertices.empty()) {
                GenerateTangents(b.vertices, b.indices);

                Vec3 min = b.vertices[0].position;
                Vec3 max = b.vertices[0].position;
                for (const auto& v : b.vertices) {
                    min = glm::min(min, v.position);
                    max = glm::max(max, v.position);
                }
                b.center = (min + max) * 0.5f;
                float maxDistSq = 0.0f;
                for (const auto& v : b.vertices) {
                    Vec3 diff = v.position - b.center;
                    maxDistSq = std::max(maxDistSq, glm::dot(diff, diff));
                }
                b.radius = std::sqrt(maxDistSq);

                out.push_back(std::move(b));
            }
        }
    }

    static void TraverseGltfNodes(const tinygltf::Model &model, int nodeIdx, const Mat4 &parentTransform,
                                  const std::string &baseDir, const std::string &modelPath,
                                  std::vector<SubMeshData> &out) {
        if (nodeIdx < 0 || nodeIdx >= static_cast<int>(model.nodes.size())) return;
        const auto &node = model.nodes[nodeIdx];

        Mat4 nodeTransform = parentTransform;
        if (!node.matrix.empty()) {
            Mat4 local;
            for (int i = 0; i < 16; ++i)
                reinterpret_cast<float *>(&local)[i] = static_cast<float>(node.matrix[i]);
            nodeTransform *= local;
        } else {
            if (!node.translation.empty()) {
                nodeTransform = glm::translate(nodeTransform,
                                               Vec3(static_cast<float>(node.translation[0]),
                                                    static_cast<float>(node.translation[1]),
                                                    static_cast<float>(node.translation[2])));
            }
            if (!node.rotation.empty()) {
                Quat q(static_cast<float>(node.rotation[3]), static_cast<float>(node.rotation[0]),
                       static_cast<float>(node.rotation[1]), static_cast<float>(node.rotation[2]));
                nodeTransform *= glm::mat4_cast(q);
            }
            if (!node.scale.empty()) {
                nodeTransform = glm::scale(nodeTransform,
                                           Vec3(static_cast<float>(node.scale[0]), static_cast<float>(node.scale[1]),
                                                static_cast<float>(node.scale[2])));
            }
        }

        if (node.mesh >= 0 && node.mesh < static_cast<int>(model.meshes.size())) {
            const auto &gltfMesh = model.meshes[node.mesh];
            for (const auto &primitive: gltfMesh.primitives) {
                SubMeshData smd;

                // Position
                if (primitive.attributes.count("POSITION") > 0) {
                    const tinygltf::Accessor &accessor = model.accessors[primitive.attributes.at("POSITION")];
                    const tinygltf::BufferView &bufferView = model.bufferViews[accessor.bufferView];
                    const tinygltf::Buffer &buffer = model.buffers[bufferView.buffer];
                    const float *positions = reinterpret_cast<const float *>(&buffer.data[
                        bufferView.byteOffset + accessor.byteOffset]);

                    smd.vertices.resize(accessor.count);
                    for (size_t i = 0; i < accessor.count; ++i) {
                        Vec4 pos = nodeTransform * Vec4(positions[i * 3 + 0], positions[i * 3 + 1],
                                                        positions[i * 3 + 2], 1.0f);
                        smd.vertices[i].position = Vec3(pos);
                    }
                }

                // Normal
                if (primitive.attributes.count("NORMAL") > 0) {
                    const tinygltf::Accessor &accessor = model.accessors[primitive.attributes.at("NORMAL")];
                    const tinygltf::BufferView &bufferView = model.bufferViews[accessor.bufferView];
                    const tinygltf::Buffer &buffer = model.buffers[bufferView.buffer];
                    const float *normals = reinterpret_cast<const float *>(&buffer.data[
                        bufferView.byteOffset + accessor.byteOffset]);

                    Mat3 normalMatrix = glm::transpose(glm::inverse(Mat3(nodeTransform)));
                    for (size_t i = 0; i < accessor.count; ++i) {
                        smd.vertices[i].normal = glm::normalize(
                            normalMatrix * Vec3(normals[i * 3 + 0], normals[i * 3 + 1], normals[i * 3 + 2]));
                    }
                } else {
                    for (auto &v: smd.vertices) v.normal = {0, 1, 0};
                }

                // UV
                if (primitive.attributes.count("TEXCOORD_0") > 0) {
                    const tinygltf::Accessor &accessor = model.accessors[primitive.attributes.at("TEXCOORD_0")];
                    const tinygltf::BufferView &bufferView = model.bufferViews[accessor.bufferView];
                    const tinygltf::Buffer &buffer = model.buffers[bufferView.buffer];
                    const float *uvs = reinterpret_cast<const float *>(&buffer.data[bufferView.byteOffset + accessor.
                        byteOffset]);

                    for (size_t i = 0; i < accessor.count; ++i) {
                        smd.vertices[i].uv = {uvs[i * 2 + 0], 1.0f - uvs[i * 2 + 1]}; // GLM vs GLTF UV flip
                    }
                }

                if (primitive.indices >= 0) {
                    const tinygltf::Accessor &indexAccessor = model.accessors[primitive.indices];
                    const tinygltf::BufferView &indexBufferView = model.bufferViews[indexAccessor.bufferView];
                    const tinygltf::Buffer &indexBuffer = model.buffers[indexBufferView.buffer];

                    smd.indices.resize(indexAccessor.count);
                    if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
                        const u32 *buf = reinterpret_cast<const u32 *>(&indexBuffer.data[
                            indexBufferView.byteOffset + indexAccessor.byteOffset]);
                        for (size_t i = 0; i < indexAccessor.count; ++i) smd.indices[i] = buf[i];
                    } else if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
                        const u16 *buf = reinterpret_cast<const u16 *>(&indexBuffer.data[
                            indexBufferView.byteOffset + indexAccessor.byteOffset]);
                        for (size_t i = 0; i < indexAccessor.count; ++i) smd.indices[i] = buf[i];
                    } else if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
                        const u8 *buf = reinterpret_cast<const u8 *>(&indexBuffer.data[
                            indexBufferView.byteOffset + indexAccessor.byteOffset]);
                        for (size_t i = 0; i < indexAccessor.count; ++i) smd.indices[i] = buf[i];
                    }
                } else {
                    smd.indices.resize(smd.vertices.size());
                    for (size_t i = 0; i < smd.vertices.size(); ++i) smd.indices[i] = static_cast<u32>(i);
                }

                if (primitive.material >= 0) {
                    const auto &mat = model.materials[primitive.material];
                    smd.baseColorFactor = Vec4(
                            static_cast<float>(mat.pbrMetallicRoughness.baseColorFactor[0]),
                            static_cast<float>(mat.pbrMetallicRoughness.baseColorFactor[1]),
                            static_cast<float>(mat.pbrMetallicRoughness.baseColorFactor[2]),
                            static_cast<float>(mat.pbrMetallicRoughness.baseColorFactor[3]));
                    smd.metallicFactor = static_cast<float>(mat.pbrMetallicRoughness.metallicFactor);
                    smd.roughnessFactor = static_cast<float>(mat.pbrMetallicRoughness.roughnessFactor);
                    smd.doubleSided = mat.doubleSided;
                    smd.alphaCutoff = static_cast<float>(mat.alphaCutoff);
                    if (mat.alphaMode == "MASK") smd.alphaMode = 1;
                    else if (mat.alphaMode == "BLEND") smd.alphaMode = 2;
                    else smd.alphaMode = 0;

                    if (mat.pbrMetallicRoughness.baseColorTexture.index >= 0) {
                        const auto &tex = model.textures[mat.pbrMetallicRoughness.baseColorTexture.index];
                        const auto &img = model.images[tex.source];
                        if (!img.uri.empty()) {
                            if (img.uri.substr(0, 5) != "data:") {
                                smd.diffuseTexturePath = ModelLoader::NormalisePath(baseDir + img.uri);
                            }
                        } else if (img.bufferView >= 0) {
                            std::string ext = ".png";
                            if (img.mimeType == "image/jpeg") ext = ".jpg";
                            smd.diffuseTexturePath =
                                    "memory://" + modelPath + "/image_" + std::to_string(tex.source) + ext;
                        }
                    }

                    if (mat.normalTexture.index >= 0) {
                        const auto &tex = model.textures[mat.normalTexture.index];
                        const auto &img = model.images[tex.source];
                        if (!img.uri.empty()) {
                            if (img.uri.substr(0, 5) != "data:") {
                                smd.normalTexturePath = ModelLoader::NormalisePath(baseDir + img.uri);
                            }
                        } else if (img.bufferView >= 0) {
                            std::string ext = ".png";
                            if (img.mimeType == "image/jpeg") ext = ".jpg";
                            smd.normalTexturePath =
                                    "memory://" + modelPath + "/image_" + std::to_string(tex.source) + ext;
                        }
                    }
                }

                GenerateTangents(smd.vertices, smd.indices);

                if (!smd.vertices.empty()) {
                    Vec3 min = smd.vertices[0].position;
                    Vec3 max = smd.vertices[0].position;
                    for (const auto &v: smd.vertices) {
                        min = glm::min(min, v.position);
                        max = glm::max(max, v.position);
                    }
                    smd.center = (min + max) * 0.5f;
                    float maxDistSq = 0.0f;
                    for (const auto &v: smd.vertices) {
                        Vec3 diff = v.position - smd.center;
                        maxDistSq = std::max(maxDistSq, glm::dot(diff, diff));
                    }
                    smd.radius = std::sqrt(maxDistSq);
                    out.push_back(std::move(smd));
                }
            }
        }

        for (int childIdx: node.children) {
            TraverseGltfNodes(model, childIdx, nodeTransform, baseDir, modelPath, out);
        }
    }

    static void LoadGltf(const std::string &filepath, std::vector<SubMeshData> &out) {
        std::string baseDir;
        auto slash = filepath.find_last_of("/\\");
        if (slash != std::string::npos)
            baseDir = filepath.substr(0, slash + 1);

        auto fileData = VirtualFS::Get().ReadFile(filepath);
        if (fileData.empty()) return;

        tinygltf::Model model;
        tinygltf::TinyGLTF loader;
        std::string err, warn;

        tinygltf::FsCallbacks callbacks{};
        callbacks.FileExists = VfsFileExists;
        callbacks.ExpandFilePath = VfsExpandFilePath;
        callbacks.ReadWholeFile = VfsReadWholeFile;
        callbacks.WriteWholeFile = VfsWriteWholeFile;
        callbacks.GetFileSizeInBytes = VfsGetFileSizeInBytes;
        callbacks.user_data = nullptr;
        loader.SetFsCallbacks(callbacks);

        loader.SetImageLoader(nullptr, nullptr);

        bool ret = false;
        if (HasExtension(filepath, ".glb")) {
            ret = loader.LoadBinaryFromMemory(&model, &err, &warn, fileData.data(),
                                              static_cast<unsigned int>(fileData.size()), baseDir);
        } else {
            ret = loader.LoadASCIIFromString(&model, &err, &warn, reinterpret_cast<const char *>(fileData.data()),
                                             static_cast<unsigned int>(fileData.size()), baseDir);
        }

        if (!warn.empty())
            LOG_WARN("[ModelLoader] GLTF Warning ({}): {}", filepath, warn);
        if (!err.empty())
            LOG_ERROR("[ModelLoader] GLTF Error ({}): {}", filepath, err);
        if (!ret) return;

        for (size_t i = 0; i < model.images.size(); ++i) {
            const auto &img = model.images[i];
            if (img.bufferView >= 0) {
                const auto &bv = model.bufferViews[img.bufferView];
                const auto &buf = model.buffers[bv.buffer];
                std::string ext = ".png";
                if (img.mimeType == "image/jpeg") ext = ".jpg";
                std::string virtualPath = "memory://" + filepath + "/image_" + std::to_string(i) + ext;

                if (!VirtualFS::Get().FileExists(virtualPath)) {
                    std::vector<u8> data(buf.data.begin() + bv.byteOffset, buf.data.begin() + bv.byteOffset + bv.byteLength);
                    VirtualFS::Get().MountOwned(virtualPath, std::move(data));
                    LOG_INFO("[ModelLoader] Mounted embedded GLB texture: {}", virtualPath);
                }
            }
        }

        const tinygltf::Scene &scene = model.scenes[model.defaultScene >= 0 ? model.defaultScene : 0];
        for (int nodeIdx : scene.nodes) {
            TraverseGltfNodes(model, nodeIdx, Mat4(1.0f), baseDir, filepath, out);
        }
    }


    std::vector<std::vector<SubMeshData>> ModelLoader::LoadSubMeshes(const std::vector<std::string> &filepaths, JobSystem &jobs) {
        std::vector<std::vector<SubMeshData>> allResults(filepaths.size());

        for (size_t i = 0; i < filepaths.size(); ++i) {
            jobs.Execute([&filepaths, &allResults, i]() {
                const std::string& path = filepaths[i];
                if (HasExtension(path, ".obj")) {
                    LoadObj(path, allResults[i]);
                } else if (HasExtension(path, ".gltf") || HasExtension(path, ".glb")) {
                    LoadGltf(path, allResults[i]);
                } else {
                    LOG_ERROR("[ModelLoader] Unsupported extension: {}", path);
                }
            });
        }
        jobs.WaitAll();
        return allResults;
    }

    std::vector<ModelData> ModelLoader::Load(const std::vector<std::string> &filepaths, JobSystem &jobs) {
        auto subMeshes = LoadSubMeshes(filepaths, jobs);
        std::vector<ModelData> results(filepaths.size());

        for (size_t i = 0; i < filepaths.size(); ++i) {
            ModelData& md = results[i];
            for (const auto& sm : subMeshes[i]) {
                if (sm.vertices.empty()) continue;
                u32 currentVertexOffset = static_cast<u32>(md.vertices.size());
                for (auto v : sm.vertices) {
                    md.vertices.push_back(v);
                }
                for (auto idx : sm.indices) {
                    md.indices.push_back(idx + currentVertexOffset);
                }

                if (!sm.diffuseTexturePath.empty() && md.diffuseTexturePath.empty()) {
                    md.diffuseTexturePath = sm.diffuseTexturePath;
                    md.normalTexturePath = sm.normalTexturePath;
                    md.baseColorFactor = sm.baseColorFactor;
                    md.metallicFactor = sm.metallicFactor;
                    md.roughnessFactor = sm.roughnessFactor;
                    md.alphaMode = sm.alphaMode;
                    md.alphaCutoff = sm.alphaCutoff;
                    md.doubleSided = sm.doubleSided;
                }
            }

            if (md.vertices.empty()) {
                md.center = Vec3(0.0f);
                md.radius = 0.0f;
            } else {
                Vec3 min = md.vertices[0].position;
                Vec3 max = md.vertices[0].position;
                for (const auto& v : md.vertices) {
                    min = glm::min(min, v.position);
                    max = glm::max(max, v.position);
                }
                md.center = (min + max) * 0.5f;
                float maxDistSq = 0.0f;
                for (const auto& v : md.vertices) {
                    Vec3 diff = v.position - md.center;
                    maxDistSq = std::max(maxDistSq, glm::dot(diff, diff));
                }
                md.radius = std::sqrt(maxDistSq);
            }
        }

        return results;
    }

} // namespace Manro
