#pragma once

#include <Manro/Render/Vulkan/VulkanContext.h>
#include <Manro/Render/Vulkan/VulkanHelpers.h>
#include <Manro/Render/Vulkan/Swapchain.h>
#include <Manro/Render/Vulkan/Pipeline.h>
#include <Manro/Render/TextureManager.h>
#include <Manro/Render/MeshManager.h>
#include <Manro/Render/Material/Material.h>
#include <Manro/Render/Material/MaterialInstance.h>
#include <Manro/Core/Types.h>
#include <vector>
#include <Manro/Render/Material/MaterialData.h>

namespace Manro {
    class IWindow;

    struct LightData {
        Vec4 position;
        Vec4 color;
        Mat4 lightSpaceMatrix;
        Vec4 direction;
        int lightType;
        float range;
        float innerConeAngle;
        float outerConeAngle;
    };

    struct UniformBufferObject {
        Mat4 model;
        Mat4 view;
        Mat4 proj;
        Vec4 camPos;
        float exposure{1.0f};
        float gamma{2.2f};
        float prefilteredCubeMipLevels{1.0f};
        float scaleIBLAmbient{1.0f};
        int lightCount{0};
        int padding0{0};
        float padding1{0.0f};
        float padding2{0.0f};
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
        int geometryInfoCount{0};
        int _padGeo[3]{};
        Vec4 _rqReservedWorldPos;
        int materialCount{0};
        int _padMat[3]{};
    };

    struct PbrPushConstants {
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
        float _pad0[3]; // Pad to 64 bytes for Vec3 emissiveFactor alignment
        Vec3 emissiveFactor{0.f, 0.f, 0.f};
        float emissiveStrength{1.f};
        float transmissionFactor{0.f};
        int useSpecGlossWorkflow{0};
        float glossinessFactor{1.f};
        float _pad1; // Pad to 96 bytes for Vec3 specularFactor alignment
        Vec3 specularFactor{1.f, 1.f, 1.f};
        float ior{1.5f};
        int hasEmissiveStrengthExt{0};
        float _pad2; // Ensure total size matches shader (116 or 120 bytes depending on compiler)
    };

    struct CompositePushConstants {
        float exposure{1.0f};
        float gamma{2.2f};
        int outputIsSRGB{0};
        float _pad{0.f};
    };

    struct GpuMeshInstance {
        Mat4 modelMatrix;
        float normalMatrix[3][4];
        u32 materialIndex;
        u32 firstVertex;
        u32 firstIndex;
        u32 indexCount;
        u32 flags;
    };

    struct GpuDrawCommand {
        u32 indexCount;
        u32 instanceCount;
        u32 firstIndex;
        int vertexOffset;
        u32 firstInstance;
    };

    class Renderer {
    public:
        Renderer(IWindow &window, u32 width, u32 height,
                 VkSampleCountFlagBits msaaSamples = VK_SAMPLE_COUNT_1_BIT);

        ~Renderer();

        Renderer(const Renderer &) = delete;

        Renderer &operator=(const Renderer &) = delete;

        void OnResize(u32 width, u32 height);

        bool BeginFrame();

        void EndFrameAndPresent();

        void BeginRendering(Vec4 clearColor = {0.02f, 0.02f, 0.05f, 1.0f});

        void EndRendering();

        void RenderQueue();

        MeshHandle UploadMesh(const ModelData &data) { return m_Meshes.Upload(data); }
        TextureHandle UploadTexture(const TextureData &data) { return m_Textures.Upload(data); }

        void SetViewProjection(const Mat4 &view, const Mat4 &proj) {
            m_ViewMatrix = view;
            m_ProjectionMatrix = proj;
        }

        void SetCameraPosition(const Vec3 &pos) { m_CameraPosition = pos; }

        void AddLight(const LightData &light);

        void ClearLights();

        Scope<MaterialInstance> CreateMaterialInstance(Ref<Material> material);

        void DrawMesh(MeshHandle meshId, MaterialInstance &material, const Mat4 &model);

        void DrawModel(const class Model &model, const Mat4 &transform);

        float GetAspectRatio() const {
            if (!m_Swapchain) return 16.f / 9.f;
            auto ext = m_Swapchain->GetExtent();
            return (ext.height > 0)
                       ? static_cast<float>(ext.width) / static_cast<float>(ext.height)
                       : 16.f / 9.f;
        }

        VulkanContext &GetContext() { return m_Context; }
        TextureManager &GetTextureManager() { return m_Textures; }
        MeshManager &GetMeshManager() { return m_Meshes; }
        Ref<Material> GetDefaultMaterial() { return m_DefaultMaterial; }
        VkDescriptorPool GetDescriptorPool() { return m_DescriptorPool; }

    private:
        void CreateDepthResources(u32 width, u32 height);

        void CreateColorResources(u32 width, u32 height);

        void CreateOffscreenResources(u32 width, u32 height);

        void RecreateSwapchain();

        void CreateCommandBuffers();

        void CreateSyncObjects();

        void BuildPbrPipeline();

        void BuildCompositePipeline();

        void CreateDescriptorLayouts();

        void CreateDescriptorPool();

        void CreateGpuBuffers();

        void UpdatePbrDescriptorSet(u32 frameIndex);

        void UpdateCompositeDescriptorSet(u32 frameIndex);

        void UploadLights(u32 frameIndex);

        VulkanContext m_Context;
        Scope<Swapchain> m_Swapchain;

        Ref<Material> m_DefaultMaterial;
        TextureManager m_Textures;
        MeshManager m_Meshes;

        AllocatedImage m_DepthImage{};
        VkFormat m_DepthFormat{VK_FORMAT_D32_SFLOAT};
        VkSampleCountFlagBits m_MsaaSamples{VK_SAMPLE_COUNT_1_BIT};
        AllocatedImage m_MsaaColorImage{};

        AllocatedImage m_OffscreenColor{};
        VkFormat m_OffscreenFormat{VK_FORMAT_R16G16B16A16_SFLOAT};
        VkSampler m_OffscreenSampler{VK_NULL_HANDLE};

        Mat4 m_ViewMatrix{1.0f};
        Mat4 m_ProjectionMatrix{1.0f};
        Vec3 m_CameraPosition{0.f, 0.f, 0.f};

        std::vector<LightData> m_PendingLights;

        struct FrameData {
            VkCommandPool commandPool{VK_NULL_HANDLE};
            VkCommandBuffer commandBuffer{VK_NULL_HANDLE};
            Scope<Buffer> uboBuffer;
            Scope<Buffer> lightBuffer;
            Scope<Buffer> tileHeaderBuffer;
            Scope<Buffer> tileLightIndexBuffer;
            Scope<Buffer> instanceBuffer;
            Scope<Buffer> indirectBuffer;
            Scope<Buffer> countBuffer;
            VkDescriptorSet pbrSet{VK_NULL_HANDLE};
            VkDescriptorSet compositeSet{VK_NULL_HANDLE};
        };

        static constexpr int MAX_FRAMES_IN_FLIGHT = 2;
        static constexpr u32 MAX_INSTANCES = 100000;
        static constexpr u32 MAX_LIGHTS = 256;
        static constexpr u32 TILE_SIZE = 16u;
        static constexpr u32 MAX_LIGHTS_PER_TILE = 128u;

        std::vector<FrameData> m_Frames;
        u32 m_CurrentImageIndex{0};
        u32 m_CurrentFrame{0};

        std::vector<VkSemaphore> m_ImageAvailableSemaphores;
        VkSemaphore m_TimelineSemaphore{VK_NULL_HANDLE};
        u64 m_TimelineValue{0};
        u64 m_FrameBaseValue[MAX_FRAMES_IN_FLIGHT]{0, 0};
        std::vector<VkSemaphore> m_PresentSemaphores;

        VkDescriptorPool m_DescriptorPool{VK_NULL_HANDLE};

        VkDescriptorSetLayout m_PbrSetLayout{VK_NULL_HANDLE};
        VkDescriptorSetLayout m_CompositeSetLayout{VK_NULL_HANDLE};

        Scope<Buffer> m_MaterialBuffer;
        std::vector<MaterialData> m_Materials;

        std::vector<GpuMeshInstance> m_CurrentFrameInstances;
        std::vector<PbrPushConstants> m_CurrentFramePushConstants;

        Scope<Pipeline> m_PbrPipeline;
        Scope<Pipeline> m_CompositePipeline;

        u32 m_PendingWidth{0};
        u32 m_PendingHeight{0};
        bool m_PendingResize{false};
    };
} // namespace Manro
