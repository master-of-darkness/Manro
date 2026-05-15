#include <Manro/Resource/Primitives.h>

#include <array>
#include <cmath>

namespace Manro {
    namespace {
        struct CubeFace_t {
            Vec3 normal;
            Vec3 tangentDir;
            Vec3 bitangentDir;
        };
    }

    ModelData_t CPrimitives::CreateCube(float size) {
        ModelData_t data;
        const float h = size * 0.5f;

        constexpr std::array<CubeFace_t, 6> faces{
            {
                {{0.f, 0.f, 1.f}, {1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}},
                {{0.f, 0.f, -1.f}, {-1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}},
                {{-1.f, 0.f, 0.f}, {0.f, 0.f, 1.f}, {0.f, 1.f, 0.f}},
                {{1.f, 0.f, 0.f}, {0.f, 0.f, -1.f}, {0.f, 1.f, 0.f}},
                {{0.f, 1.f, 0.f}, {1.f, 0.f, 0.f}, {0.f, 0.f, -1.f}},
                {{0.f, -1.f, 0.f}, {1.f, 0.f, 0.f}, {0.f, 0.f, 1.f}},
            }
        };

        data.vertices.reserve(24);
        data.indices.reserve(36);

        for (const CubeFace_t &face: faces) {
            const u32 baseIdx = static_cast<u32>(data.vertices.size());
            const Vec3 center = face.normal * h;
            const Vec3 tangent = face.tangentDir * h;
            const Vec3 bitangent = face.bitangentDir * h;

            const std::array<Vec3, 4> positions{
                {
                    center - tangent - bitangent,
                    center + tangent - bitangent,
                    center + tangent + bitangent,
                    center - tangent + bitangent,
                }
            };

            for (size_t i = 0; i < positions.size(); ++i) {
                constexpr std::array<Vec2, 4> uvs{
                    {
                        {0.f, 0.f},
                        {1.f, 0.f},
                        {1.f, 1.f},
                        {0.f, 1.f},
                    }
                };
                Vertex_t vert{};
                vert.position = positions[i];
                vert.normal = face.normal;
                vert.uv = uvs[i];
                vert.tangent = Vec4(face.tangentDir, 1.f);
                data.vertices.push_back(vert);
            }

            data.indices.push_back(baseIdx + 0);
            data.indices.push_back(baseIdx + 1);
            data.indices.push_back(baseIdx + 2);
            data.indices.push_back(baseIdx + 0);
            data.indices.push_back(baseIdx + 2);
            data.indices.push_back(baseIdx + 3);
        }

        data.center = Vec3(0.f);
        data.radius = std::sqrt(3.f * h * h);
        data.baseColorFactor = Vec4(1.f);
        data.metallicFactor = 0.5f;
        data.roughnessFactor = 0.5f;

        return data;
    }
} // namespace Manro