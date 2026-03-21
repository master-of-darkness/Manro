#pragma once
#include <Manro/Core/Types.h>
#include <cstring>

#include "nvshaders/gltf_scene_io.h.slang"

namespace shaderio {
    inline bool operator==(const GltfShadeMaterial& a, const GltfShadeMaterial& b) {
        return std::memcmp(&a, &b, sizeof(GltfShadeMaterial)) == 0;
    }
}

namespace Manro {
    using MaterialData = shaderio::GltfShadeMaterial;

    struct MaterialDataHash {
        size_t operator()(const MaterialData& md) const {
            const uint64_t* p = reinterpret_cast<const uint64_t*>(&md);
            size_t h = 0;
            for (size_t i = 0; i < sizeof(MaterialData) / 8; ++i) {
                h ^= std::hash<uint64_t>{}(p[i]) + 0x9e3779b9 + (h << 6) + (h >> 2);
            }
            return h;
        }
    };
} // namespace Manro
