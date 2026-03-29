#include <Manro/Resource/Primitives.h>

namespace Manro {

    ModelData Primitives::CreateCube(float size) {
        ModelData data;
        const float h = size * 0.5f;

        Vec3 normals[6] = {
                {0,  0,  1}, // Front
                {0,  0,  -1}, // Back
                {-1, 0,  0}, // Left
                {1,  0,  0}, // Right
                {0,  1,  0}, // Top
                {0,  -1, 0}  // Bottom
        };

        Vec4 tangents[6] = {
                {1,  0, 0,  1}, // Front
                {-1, 0, 0,  1}, // Back
                {0,  0, 1,  1}, // Left
                {0,  0, -1, 1}, // Right
                {1,  0, 0,  1}, // Top
                {1,  0, 0,  1}  // Bottom
        };

        // Positions for each face
        // Front
        Vec3 p0{-h, -h, h}, p1{h, -h, h}, p2{h, h, h}, p3{-h, h, h};
        // Back
        Vec3 p4{h, -h, -h}, p5{-h, -h, -h}, p6{-h, h, -h}, p7{h, h, -h};
        // Left
        Vec3 p8{-h, -h, -h}, p9{-h, -h, h}, p10{-h, h, h}, p11{-h, h, -h};
        // Right
        Vec3 p12{h, -h, h}, p13{h, -h, -h}, p14{h, h, -h}, p15{h, h, h};
        // Top
        Vec3 p16{-h, h, h}, p17{h, h, h}, p18{h, h, -h}, p19{-h, h, -h};
        // Bottom
        Vec3 p20{-h, -h, -h}, p21{h, -h, -h}, p22{h, -h, h}, p23{-h, -h, h};

        Vec3 *facePos[6] = {
                new Vec3[4]{p0, p1, p2, p3},
                new Vec3[4]{p4, p5, p6, p7},
                new Vec3[4]{p8, p9, p10, p11},
                new Vec3[4]{p12, p13, p14, p15},
                new Vec3[4]{p16, p17, p18, p19},
                new Vec3[4]{p20, p21, p22, p23}
        };

        Vec2 uvs[4] = {
                {0.0f, 0.0f},
                {1.0f, 0.0f},
                {1.0f, 1.0f},
                {0.0f, 1.0f}
        };

        for (int i = 0; i < 6; ++i) {
            u32 baseIdx = static_cast<u32>(data.vertices.size());
            for (int v = 0; v < 4; ++v) {
                Vertex vert;
                vert.position = facePos[i][v];
                vert.normal = normals[i];
                vert.tangent = tangents[i];
                vert.uv = uvs[v];
                data.vertices.push_back(vert);
            }

            data.indices.push_back(baseIdx + 0);
            data.indices.push_back(baseIdx + 1);
            data.indices.push_back(baseIdx + 2);
            data.indices.push_back(baseIdx + 0);
            data.indices.push_back(baseIdx + 2);
            data.indices.push_back(baseIdx + 3);

            delete[] facePos[i];
        }

        data.center = Vec3(0.0f);
        data.radius = std::sqrt(h * h + h * h + h * h);

        data.baseColorFactor = Vec4(1.0f);
        data.metallicFactor = 0.5f;
        data.roughnessFactor = 0.5f;

        return data;
    }

} // namespace Manro
