#pragma once

#include <Manro/Core/Types.h>
#include <Manro/Render/RenderSettings.h>

#include <volk.h>
#include <algorithm>

namespace Manro {
    struct UniformBufferObject_t {
        Mat4 model;
        Mat4 view;
        Mat4 proj;
        Vec4 camPos;
        float exposure{1.0f};
        float gamma{2.2f};
        float prefilteredCubeMipLevels{1.0f};
        float scaleIBLAmbient{1.0f};
        int lightCount{0};
        int shadowsEnabled{1};
        float aoIntensity{0.0f};
        float aoRadius{0.5f};
        Vec2 screenDimensions;
        float nearZ{0.1f};
        float farZ{1000.0f};
        float slicesZ{1.0f};
        float _pad3{0.0f};
        Mat4 reflectionVP;
        int reflectionEnabled{0};
        int reflectionPass{0};
        Vec2 _reflectPad0;
        Vec4 clipPlaneWS;
        float reflectionIntensity{1.0f};
        int enableRayQueryReflections{0};
        int enableRayQueryTransparency{0};
        float _padReflect[1]{};
        int rayMaxBounces{1};
        int _padGeo[3]{};
        Vec4 _rqReservedWorldPos;
        int materialCount{0};
        int _padMat[3]{};
    };

    struct PBRPushConstants_t {
        Vec4 baseColorFactor{1.f, 1.f, 1.f, 1.f};
        float metallicFactor{1.f};
        float roughnessFactor{1.f};
        int baseColorTextureSet{-1};
        int physicalDescriptorTextureSet{-1};
        int normalTextureSet{-1};
        int occlusionTextureSet{-1};
        int emissiveTextureSet{-1};
        float alphaMask{0.f};
        float alphaMaskCutoff{0.5f};
        float _pad0[3];
        Vec3 emissiveFactor{0.f, 0.f, 0.f};
        float emissiveStrength{1.f};
        float transmissionFactor{0.f};
        int useSpecGlossWorkflow{0};
        float glossinessFactor{1.f};
        float _pad1;
        Vec3 specularFactor{1.f, 1.f, 1.f};
        float ior{1.5f};
        int hasEmissiveStrengthExt{0};
        float _pad2;
    };

    struct MeshCullPushConstants_t {
        Vec4 planes[6];
        Vec4 cameraPos;
        u32 instanceCount;
        float maxDrawDistance;
        u32 enableFrustumCulling;
        u32 _pad;
    };

    struct CullData_t {
        float center[3];
        float radius;
        u32 instanceId;
        u32 _pad[3];
    };

    struct MeshInstance_t {
        Mat4 modelMatrix;
        float normalMatrix[3][4];
        u32 materialIndex;
        u32 firstVertex;
        u32 firstIndex;
        u32 indexCount;
        float center[3];
        float radius;
        u32 flags;
        u32 _pad[3];
    };

    struct DrawCommand_t {
        u32 indexCount;
        u32 instanceCount;
        u32 firstIndex;
        int vertexOffset;
        u32 firstInstance;
    };

    static inline VkSampleCountFlagBits ToVulkanSampleCount(MSAASampleCount samples) {
        switch (samples) {
            case MSAASampleCount::MSAA_1X: return VK_SAMPLE_COUNT_1_BIT;
            case MSAASampleCount::MSAA_2X: return VK_SAMPLE_COUNT_2_BIT;
            case MSAASampleCount::MSAA_4X: return VK_SAMPLE_COUNT_4_BIT;
            case MSAASampleCount::MSAA_8X: return VK_SAMPLE_COUNT_8_BIT;
            case MSAASampleCount::MSAA_16X: return VK_SAMPLE_COUNT_16_BIT;
            case MSAASampleCount::MSAA_32X: return VK_SAMPLE_COUNT_32_BIT;
            case MSAASampleCount::MSAA_64X: return VK_SAMPLE_COUNT_64_BIT;
            default: return VK_SAMPLE_COUNT_1_BIT;
        }
    }

    static inline MSAASampleCount FromVulkanSampleCount(VkSampleCountFlagBits samples) {
        switch (samples) {
            case VK_SAMPLE_COUNT_1_BIT: return MSAASampleCount::MSAA_1X;
            case VK_SAMPLE_COUNT_2_BIT: return MSAASampleCount::MSAA_2X;
            case VK_SAMPLE_COUNT_4_BIT: return MSAASampleCount::MSAA_4X;
            case VK_SAMPLE_COUNT_8_BIT: return MSAASampleCount::MSAA_8X;
            case VK_SAMPLE_COUNT_16_BIT: return MSAASampleCount::MSAA_16X;
            case VK_SAMPLE_COUNT_32_BIT: return MSAASampleCount::MSAA_32X;
            case VK_SAMPLE_COUNT_64_BIT: return MSAASampleCount::MSAA_64X;
            default: return MSAASampleCount::MSAA_1X;
        }
    }

    static inline void NormalizeRenderSettings(RenderSettings_t &settings, VkSampleCountFlagBits maxSamples) {
        settings.resolutionScale = std::clamp(settings.resolutionScale, 0.1f, 2.0f);

        if (settings.aaMode != AntiAliasingMode::MSAA) {
            settings.msaaSamples = MSAASampleCount::MSAA_1X;
        } else if (static_cast<u32>(ToVulkanSampleCount(settings.msaaSamples)) > static_cast<u32>(maxSamples)) {
            settings.msaaSamples = FromVulkanSampleCount(maxSamples);
        }

        settings.nearZ = std::max(settings.nearZ, 0.001f);
        settings.farZ = std::max(settings.farZ, settings.nearZ + 0.001f);
        if (settings.maxDrawDistance <= 0.0f) {
            settings.maxDrawDistance = settings.farZ;
        } else {
            settings.maxDrawDistance = std::max(settings.maxDrawDistance, settings.nearZ);
        }

        settings.shadows.resolution = std::clamp(settings.shadows.resolution, 128, 8192);
        settings.shadows.bias = std::max(settings.shadows.bias, 0.0f);
        settings.shadows.slopeBias = std::max(settings.shadows.slopeBias, 0.0f);
        settings.shadows.softShadows = std::max(settings.shadows.softShadows, 0.0f);

        settings.lighting.iblIntensity = std::max(settings.lighting.iblIntensity, 0.0f);
        settings.lighting.gamma = std::clamp(settings.lighting.gamma, 1.0f, 3.0f);
        settings.lighting.aoIntensity = std::max(settings.lighting.aoIntensity, 0.0f);
        settings.lighting.aoRadius = std::max(settings.lighting.aoRadius, 0.0f);

        settings.postProcess.bloomIntensity = std::max(settings.postProcess.bloomIntensity, 0.0f);
        settings.postProcess.bloomThreshold = std::max(settings.postProcess.bloomThreshold, 0.0f);

        settings.rayTracing.maxBounces = std::clamp(settings.rayTracing.maxBounces, 1, 8);
    }
} // namespace Manro
