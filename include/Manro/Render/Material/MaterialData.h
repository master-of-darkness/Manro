#pragma once
#include <Manro/Core/Types.h>
#include <cstring>

// Include nvshaders GltfShadeMaterial as the engine's material data type.
// gltf_scene_io.h.slang is a C++/Slang polyglot header — in C++ it puts
// types into the shaderio namespace via slang_types.h / GLM.
#include "nvshaders/gltf_scene_io.h.slang"

namespace shaderio {
    // Equality comparison via raw memory (all fields are POD)
    inline bool operator==(const GltfShadeMaterial& a, const GltfShadeMaterial& b) {
        return std::memcmp(&a, &b, sizeof(GltfShadeMaterial)) == 0;
    }
}

namespace Manro {
    // Use the nvshaders GltfShadeMaterial as the canonical GPU material struct.
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
