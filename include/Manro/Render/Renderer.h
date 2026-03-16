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

    struct GpuMeshInstance {
        Mat4 modelMatrix;
        float normalMatrix[3][4]; // 3 columns of vec4 (xyz used)
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
        int padding0;
        float padding1;
        float padding2;
        Vec2 screenDimensions;
        float nearZ{0.1f};
        float farZ{1000.0f};
        float slicesZ{16.0f};
        float _pad3;
        Mat4 reflectionVP;
        int reflectionEnabled{0};
        int reflectionPass{0};
        Vec2 _reflectPad0;
        Vec4 clipPlaneWS;
        float reflectionIntensity{1.0f};
        int enableRayQueryReflections{0};
        int enableRayQueryTransparency{0};
        float _padReflect[1];
        int geometryInfoCount{0};
        int _padGeo[3];
        Vec4 _rqReservedWorldPos;
        int materialCount{0};
        int _padMat[3];
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
        void BindTexture(TextureHandle id);
        void SetTintColor(const Vec3 &color) { m_TintColor = color; }
        void SetViewProjection(const Mat4 &view, const Mat4 &proj) {
            m_ViewMatrix = view;
            m_ProjectionMatrix = proj;
        }

        Scope<MaterialInstance> CreateMaterialInstance(Ref<Material> material);

        void DrawMesh(MeshHandle meshId, const Mat4 &model);

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
        void RecreateSwapchain();
        void CreateCommandBuffers();
        void CreateSyncObjects();
        void LoadShadersAndPipeline();
        void CreateDescriptorPool();
        void CreateGpuBuffers();
        void UpdateGlobalDescriptorSet(VkCommandBuffer cb);

        VulkanContext m_Context;
        Scope<Swapchain> m_Swapchain;

        Ref<Material> m_DefaultMaterial;
        TextureManager m_Textures;
        MeshManager m_Meshes;

        AllocatedImage m_DepthImage{};
        VkFormat m_DepthFormat{VK_FORMAT_D32_SFLOAT};
        VkSampleCountFlagBits m_MsaaSamples{VK_SAMPLE_COUNT_1_BIT};

        AllocatedImage m_ColorImage{};

        Vec3 m_TintColor{1.0f, 1.0f, 1.0f};

        Mat4 m_ViewMatrix{1.0f};
        Mat4 m_ProjectionMatrix{1.0f};

        Scope<MaterialInstance> m_DefaultMaterialInstance;

        struct FrameData {
            VkCommandPool commandPool{VK_NULL_HANDLE};
            VkCommandBuffer commandBuffer{VK_NULL_HANDLE};
            VkFence inFlightFence{VK_NULL_HANDLE};
            Scope<Buffer> uboBuffer;
            Scope<Buffer> instanceBuffer;
            Scope<Buffer> indirectBuffer;
            Scope<Buffer> countBuffer;
            Scope<Buffer> statsBuffer;
            VkDescriptorSet globalSet{VK_NULL_HANDLE};
        };

        static constexpr int MAX_FRAMES_IN_FLIGHT = 2;
        static constexpr u32 MAX_INSTANCES = 100000;
        std::vector<FrameData> m_Frames;

        u32 m_CurrentImageIndex{0};
        u32 m_CurrentFrame{0};

        std::vector<VkSemaphore> m_ImageAvailableSemaphores;
        std::vector<VkSemaphore> m_RenderFinishedSemaphores;

        VkDescriptorPool m_DescriptorPool{VK_NULL_HANDLE};
        VkDescriptorSetLayout m_GlobalSetLayout{VK_NULL_HANDLE};

        Scope<Buffer> m_MaterialBuffer;
        std::vector<MaterialData> m_Materials;
        std::vector<GpuMeshInstance> m_CurrentFrameInstances;

        Scope<Pipeline> m_CullPipeline;
        Scope<Pipeline> m_IndirectPipeline;

        u32 m_PendingWidth{0};
        u32 m_PendingHeight{0};
        bool m_PendingResize{false};
    };
} // namespace Manro
