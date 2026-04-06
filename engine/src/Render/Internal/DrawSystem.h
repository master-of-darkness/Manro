#pragma once

#include <Manro/Core/Types.h>
#include <volk.h>

namespace Manro {
    class VulkanContext;
    class Buffer;
    class Pipeline;

    struct DrawLineCmd {
        Vec3 start;
        u32 color;
        Vec3 end;
        u32 depthTest;
    };

    struct DrawBoxCmd {
        Vec3 center;
        u32 color;
        Vec3 halfExtents;
        u32 depthTest;
        Mat4 transform;
    };

    struct DrawSphereCmd {
        Vec3 center;
        float radius;
        u32 color;
        u32 segments;
        u32 depthTest;
        u32 _pad0;
    };

    struct DrawFrustumCmd {
        Mat4 invViewProj;
        u32 color;
        u32 depthTest;
        u32 _pad0;
        u32 _pad1;
    };

    struct DrawCrossCmd {
        Vec3 center;
        float size;
        u32 color;
        u32 depthTest;
    };

    struct LineVertex {
        Vec3 position;
        u32 color;
    };

    struct DrawIndirectCmd {
        u32 vertexCount;
        u32 instanceCount;
        u32 firstVertex;
        u32 firstInstance;
    };

    class DrawSystem {
    public:
        explicit DrawSystem(const VulkanContext &context);

        ~DrawSystem();

        DrawSystem(const DrawSystem &) = delete;

        DrawSystem &operator=(const DrawSystem &) = delete;

        void Init(VkFormat colorFormat, VkFormat depthFormat, VkSampleCountFlagBits msaaSamples);

        void Shutdown();

        void BeginFrame();

        void DispatchExpand(VkCommandBuffer cmd);

        void Draw(VkCommandBuffer cmd, const Mat4 &viewProj,
                  VkImageView colorTarget,
                  VkImageView resolveColorTarget,
                  VkImageView depthTarget,
                  u32 width, u32 height,
                  bool useMsaaResolve);

        void SubmitLine(const Vec3 &a, const Vec3 &b, u32 color, bool depthTest);

        void SubmitBox(const Vec3 &center, const Vec3 &halfExtents, const Mat4 &transform, u32 color, bool depthTest);

        void SubmitSphere(const Vec3 &center, float radius, u32 color, int segments, bool depthTest);

        void SubmitFrustum(const Mat4 &invViewProj, u32 color, bool depthTest);

        void SubmitCross(const Vec3 &center, float size, u32 color, bool depthTest);

    private:
        void CreateBuffers();

        void CreateComputePipeline();

        void CreateRenderPipelines(VkFormat colorFormat, VkFormat depthFormat, VkSampleCountFlagBits msaaSamples);

        const VulkanContext &m_Context;

        static constexpr u32 kMaxLines = 65536;
        static constexpr u32 kMaxBoxes = 8192;
        static constexpr u32 kMaxSpheres = 4096;
        static constexpr u32 kMaxFrustums = 256;
        static constexpr u32 kMaxCrosses = 4096;
        static constexpr u32 kMaxVertices = 1024 * 1024;

        Scope<Buffer> m_LineCommandBuffer;
        Scope<Buffer> m_BoxCommandBuffer;
        Scope<Buffer> m_SphereCommandBuffer;
        Scope<Buffer> m_FrustumCommandBuffer;
        Scope<Buffer> m_CrossCommandBuffer;

        Scope<Buffer> m_VertexBuffer;
        Scope<Buffer> m_VertexBufferNoDepth;
        Scope<Buffer> m_IndirectBuffer;
        Scope<Buffer> m_IndirectBufferNoDepth;
        Scope<Buffer> m_CounterBuffer;

        Scope<Pipeline> m_ExpandPipeline;
        Scope<Pipeline> m_RenderPipeline;
        Scope<Pipeline> m_RenderPipelineNoDepth;

        VkDescriptorPool m_DescriptorPool{VK_NULL_HANDLE};
        VkDescriptorSetLayout m_ComputeSetLayout{VK_NULL_HANDLE};
        VkDescriptorSet m_ComputeDescriptorSet{VK_NULL_HANDLE};
        VkDescriptorSet m_ComputeDescriptorSetNoDepth{VK_NULL_HANDLE};

        u32 m_LineCount{0};
        u32 m_BoxCount{0};
        u32 m_SphereCount{0};
        u32 m_FrustumCount{0};
        u32 m_CrossCount{0};
        u32 m_DepthVertexCount{0};
        u32 m_NoDepthVertexCount{0};
    };
} // namespace Manro
