#pragma once

#include "VulkanContext.h"
#include "Swapchain.h"
#include <Core/Types.h>
#include "Pipeline.h"
#include "SlangCompiler.h"
#include "Buffer.h"
#include "ModelLoader.h"
#include "TextureLoader.h"
#include "FrameManager.h"
#include "ResourceManager.h"
#include "DescriptorManager.h"
#include "RenderTargets.h"
#include <vector>
#include <unordered_map>

namespace Engine {
    class IWindow;

    struct Vertex {
        Vec3 position;
        Vec3 color;
        Vec2 uv;
    };

    static constexpr int MAX_POINT_LIGHTS = 4;

    struct PointLightGPU {
        Vec3 position;
        float radius;
        Vec3 color;
        float intensity;
    };

    struct LightDataGPU {
        Vec3 dirLightDirection;
        float _pad0;
        Vec3 dirLightColor;
        float dirLightIntensity;
        Vec3 cameraPos;
        float _pad1;
        Vec3 ambientColor;
        float ambientIntensity;
        int numPointLights;
        Vec3 _pad2;
        PointLightGPU pointLights[MAX_POINT_LIGHTS];
    };

    struct PBRMaterial {
        u32 albedoTextureId{0};
        u32 normalTextureId{0};
        u32 specularTextureId{0};
        VkDescriptorSet descriptorSet{VK_NULL_HANDLE};
    };

    class Renderer {
    public:
        Renderer();

        ~Renderer();

        void Initialize(IWindow *window, u32 width, u32 height,
                        VkSampleCountFlagBits msaaSamples = VK_SAMPLE_COUNT_1_BIT);

        void Shutdown();

        void OnResize(u32 width, u32 height);

        bool BeginFrame();

        void EndFrameAndPresent();

        void DrawCube(const Mat4 &mvp);

        void BeginRenderPass(Vec4 clearColor = {0.02f, 0.02f, 0.05f, 1.0f});

        void EndRenderPass();

        u32 UploadMesh(const ModelData &data);

        void DrawMesh(u32 meshId, const Mat4 &mvp);

        u32 UploadTexture(const TextureData &data);

        void BindTexture(u32 textureId);

        // PBR pipeline
        u32 UploadPBRMesh(const PBRSubMeshData &data);

        void UpdateLights(const LightDataGPU &lightData);

        void BeginPBRPass();

        void BindPBRMaterial(const PBRMaterial &mat);

        void BuildPBRMaterialDescriptor(PBRMaterial &mat);

        void DrawPBRMesh(u32 meshId, const Mat4 &mvp, const Mat4 &model);

        float GetAspectRatio() const {
            if (!m_Swapchain) return 16.f / 9.f;
            auto ext = m_Swapchain->GetExtent();
            return (ext.height > 0)
                       ? static_cast<float>(ext.width) / static_cast<float>(ext.height)
                       : 16.f / 9.f;
        }

        void SetTintColor(const Vec3 &color) { m_TintColor = color; }

        VulkanContext &GetContext() { return m_Context; }

        u32 GetNeutralNormalTextureId() const { return m_Resources.GetNeutralNormalTextureId(); }
        u32 GetWhiteTextureId() const { return m_Resources.GetWhiteTextureId(); }

    private:
        void RecreateSwapchain();

        void LoadShadersAndPipeline();

        void CreatePBRPipeline();

        VulkanContext m_Context;
        Scope<Swapchain> m_Swapchain;

        FrameManager m_FrameManager;
        ResourceManager m_Resources;
        DescriptorManager m_Descriptors;
        RenderTargets m_RenderTargets;

        Scope<SlangCompiler> m_ShaderCompiler;
        Scope<Pipeline> m_Pipeline;

        Scope<Buffer> m_VertexBuffer;
        Scope<Buffer> m_IndexBuffer;

        VkDescriptorSet m_ActiveDescriptorSet{VK_NULL_HANDLE};

        Vec3 m_TintColor{1.0f, 1.0f, 1.0f};

        Scope<Pipeline> m_PBRPipeline;
        Scope<Buffer> m_LightUBO;

        u32 m_CurrentImageIndex{0};

        u32 m_PendingWidth{0};
        u32 m_PendingHeight{0};
        bool m_PendingResize{false};
    };
} // namespace Engine
