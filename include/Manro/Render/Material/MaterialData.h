#pragma once
#include <Manro/Core/Types.h>
#include <cstring>

namespace Manro {
    struct alignas(16) MaterialData {
        Vec3 albedo{1.0f, 1.0f, 1.0f};
        float metallic{0.0f};           // Offs: 12
        Vec3 emissive{0.0f, 0.0f, 0.0f};
        float roughness{0.5f};          // Offs: 28
        float ao{1.0f};                 // Offs: 32
        float ior{1.5f};                // Offs: 36
        float emissiveStrength{1.0f};   // Offs: 40
        float alpha{1.0f};              // Offs: 44
        float transmissionFactor{0.0f}; // Offs: 48
        float alphaCutoff{0.5f};        // Offs: 52
        int alphaMode{0};               // Offs: 56
        u32 isGlass{0};                 // Offs: 60
        u32 isLiquid{0};                // Offs: 64
        float _pad0[3];                 // Offs: 68 -> Align 80
        Vec3 absorptionColor{1.0f, 1.0f, 1.0f};
        float absorptionDistance{1.0f}; // Offs: 92
        u32 thinWalled{0};              // Offs: 96
        int baseColorTextureSet{-1};    // Offs: 100
        int physicalDescriptorTextureSet{-1}; // Offs: 104
        int normalTextureSet{-1};       // Offs: 108
        int occlusionTextureSet{-1};    // Offs: 112
        int emissiveTextureSet{-1};     // Offs: 116
        int baseColorTexIndex{-1};      // Offs: 120
        int normalTexIndex{-1};         // Offs: 124
        int physicalTexIndex{-1};       // Offs: 128
        int occlusionTexIndex{-1};      // Offs: 132
        int emissiveTexIndex{-1};       // Offs: 136
        int useSpecGlossWorkflow{0};    // Offs: 140
        float glossinessFactor{1.0f};   // Offs: 144
        float _pad1[3];                 // Offs: 148 -> Align 160
        Vec3 specularFactor{1.0f, 1.0f, 1.0f};
        int hasEmissiveStrengthExt{0};  // Offs: 172
        u32 _padMat[4];                 // Offs: 176 -> Total 192

        bool operator==(const MaterialData& other) const {
            return std::memcmp(this, &other, sizeof(MaterialData)) == 0;
        }
    };

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
