#pragma once

#include "VulkanContext.h"
#include "Swapchain.h"
#include <Core/Types.h>
#include "Pipeline.h"
#include "SlangCompiler.h"
#include "Buffer.h"
#include "ModelLoader.h"
#include "TextureLoader.h"
#include <vector>
#include <unordered_map>

namespace Engine {
    class IWindow;

    struct Vertex {
        Vec3 position;
        Vec3 color;
        Vec2 uv;
    };

    class Renderer {
    public:
        Renderer();

        ~Renderer();

        void Initialize(IWindow *window, u32 width, u32 height);

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

        float GetAspectRatio() const {
            if (!m_Swapchain) return 16.f / 9.f;
            auto ext = m_Swapchain->GetExtent();
            return (ext.height > 0)
                       ? static_cast<float>(ext.width) / static_cast<float>(ext.height)
                       : 16.f / 9.f;
        }

        void SetTintColor(const Vec3 &color) { m_TintColor = color; }

        VulkanContext &GetContext() { return m_Context; }

    private:
        void CreateDepthResources(u32 width, u32 height);

        void DestroyDepthResources();

        void RecreateSwapchain();

        void CreateCommandBuffers();

        void CreateSyncObjects();

        void CreateDescriptorPool();

        void CreateDescriptorSetLayout();

        void CreateDefaultSampler();

        void CreateWhiteTexture();

        void LoadShadersAndPipeline();

        u32 UploadTextureInternal(const u8 *pixels, int width, int height);

        VulkanContext m_Context;
        Scope<Swapchain> m_Swapchain;

        Scope<SlangCompiler> m_ShaderCompiler;
        Scope<Pipeline> m_Pipeline;

        Scope<Buffer> m_VertexBuffer;
        Scope<Buffer> m_IndexBuffer;

        struct LoadedMesh {
            Scope<Buffer> vertexBuffer;
            Scope<Buffer> indexBuffer;
            u32 indexCount{0};
        };

        std::unordered_map<u32, LoadedMesh> m_Meshes;
        u32 m_NextMeshId{1};

        struct LoadedTexture {
            VkImage image{VK_NULL_HANDLE};
            VkImageView view{VK_NULL_HANDLE};
            VmaAllocation allocation{nullptr};
            VkDescriptorSet descriptorSet{VK_NULL_HANDLE};
        };

        std::unordered_map<u32, LoadedTexture> m_Textures;
        u32 m_NextTextureId{1};

        VkDescriptorSet m_ActiveDescriptorSet{VK_NULL_HANDLE};

        VkImage m_DepthImage{VK_NULL_HANDLE};
        VkImageView m_DepthImageView{VK_NULL_HANDLE};
        VmaAllocation m_DepthAllocation{nullptr};
        VkFormat m_DepthFormat{VK_FORMAT_D32_SFLOAT};
        VkSampleCountFlagBits m_MsaaSamples{VK_SAMPLE_COUNT_1_BIT};

        VkImage m_ColorImage{VK_NULL_HANDLE};
        VkImageView m_ColorImageView{VK_NULL_HANDLE};
        VmaAllocation m_ColorAllocation{nullptr};

        VkDescriptorPool m_DescriptorPool{VK_NULL_HANDLE};
        VkDescriptorSetLayout m_DescriptorSetLayout{VK_NULL_HANDLE};
        VkSampler m_Sampler{VK_NULL_HANDLE};

        u32 m_WhiteTextureId{0};

        Vec3 m_TintColor{1.0f, 1.0f, 1.0f};

        struct FrameData {
            VkCommandPool commandPool{VK_NULL_HANDLE};
            VkCommandBuffer commandBuffer{VK_NULL_HANDLE};
            VkSemaphore imageAvailableSemaphore{VK_NULL_HANDLE};
            VkSemaphore renderFinishedSemaphore{VK_NULL_HANDLE};
            VkFence inFlightFence{VK_NULL_HANDLE};
        };

        static constexpr int MAX_FRAMES_IN_FLIGHT = 2;
        std::vector<FrameData> m_Frames;

        u32 m_CurrentImageIndex{0};
        u32 m_CurrentFrame{0};

        u32 m_PendingWidth{0};
        u32 m_PendingHeight{0};
        bool m_PendingResize{false};
    };
} // namespace Engine
