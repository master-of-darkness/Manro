#pragma once

#include <Manro/Core/Types.h>
#include <Manro/Core/Handle.h>
#include <Manro/Core/Logger.h>

#include <volk.h>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>

namespace Manro {
    struct RGTextureTag {
    };

    using RGTextureHandle = Handle<RGTextureTag>;

    struct RGBufferTag {
    };

    using RGBufferHandle = Handle<RGBufferTag>;

    enum class QueueType : u8 {
        Graphics, Compute, Transfer
    };

    enum RGTextureUsage : u32 {
        RGTextureUsage_None = 0,
        RGTextureUsage_ColorAttachment = 1 << 0,
        RGTextureUsage_DepthAttachment = 1 << 1,
        RGTextureUsage_ShaderRead = 1 << 2,
        RGTextureUsage_ShaderWrite = 1 << 3,
        RGTextureUsage_TransferSrc = 1 << 4,
        RGTextureUsage_TransferDst = 1 << 5,
        RGTextureUsage_Present = 1 << 6,
    };

    enum RGBufferUsage : u32 {
        RGBufferUsage_None = 0,
        RGBufferUsage_Uniform = 1 << 0,
        RGBufferUsage_Storage = 1 << 1,
        RGBufferUsage_Indirect = 1 << 2,
        RGBufferUsage_TransferSrc = 1 << 3,
        RGBufferUsage_TransferDst = 1 << 4,
    };

    struct RGTextureDesc {
        std::string name;
        u32 width = 0; // 0 = match swapchain
        u32 height = 0;
        VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
        u32 usageHint = RGTextureUsage_None;
        bool external = false; // true = we don't own the VkImage
    };

    struct RGBufferDesc {
        std::string name;
        VkDeviceSize size = 0;
        u32 usageHint = RGBufferUsage_None;
        bool external = false;
    };

    struct RGResolvedTexture {
        VkImage image = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkFormat format = VK_FORMAT_UNDEFINED;
        u32 width = 0;
        u32 height = 0;
    };

    struct RGResolvedBuffer {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceSize size = 0;
    };

    class RGResources {
    public:
        void BindTexture(RGTextureHandle h, RGResolvedTexture tex) {
            m_Textures[h.packed] = tex;
        }

        void BindBuffer(RGBufferHandle h, RGResolvedBuffer buf) {
            m_Buffers[h.packed] = buf;
        }

        const RGResolvedTexture &GetTexture(RGTextureHandle h) const {
            return m_Textures.at(h.packed);
        }

        const RGResolvedBuffer &GetBuffer(RGBufferHandle h) const {
            return m_Buffers.at(h.packed);
        }

    private:
        std::unordered_map<u32, RGResolvedTexture> m_Textures;
        std::unordered_map<u32, RGResolvedBuffer> m_Buffers;
    };

    struct RGTextureAccess {
        RGTextureHandle handle;
        VkPipelineStageFlags2 stageMask;
        VkAccessFlags2 accessMask;
        VkImageLayout requiredLayout;
        VkAttachmentLoadOp loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        bool isAttachment = false;
        bool isDepth = false;
    };

    struct RGBufferAccess {
        RGBufferHandle handle;
        VkPipelineStageFlags2 stageMask;
        VkAccessFlags2 accessMask;
    };

    using PassExecuteFn = std::function<void(VkCommandBuffer, RGResources &)>;

    class RGPassBuilder {
    public:
        void WriteColor(RGTextureHandle h,
                        VkAttachmentLoadOp loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                        VkPipelineStageFlags2 stage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);

        void WriteDepth(RGTextureHandle h,
                        VkAttachmentLoadOp loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                        VkPipelineStageFlags2 stage = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT
                                                      | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT);

        void ReadTexture(RGTextureHandle h,
                         VkPipelineStageFlags2 stage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);

        void WriteStorageImage(RGTextureHandle h,
                               VkPipelineStageFlags2 stage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);

        void ReadBuffer(RGBufferHandle h,
                        VkPipelineStageFlags2 stage,
                        VkAccessFlags2 access);

        void WriteBuffer(RGBufferHandle h,
                         VkPipelineStageFlags2 stage,
                         VkAccessFlags2 access);

        void SetExecute(PassExecuteFn fn) { m_Execute = std::move(fn); }

        const std::vector<RGTextureAccess> &TextureAccesses() const { return m_TextureAccesses; }

        const std::vector<RGBufferAccess> &BufferAccesses() const { return m_BufferAccesses; }

        const PassExecuteFn &Execute() const { return m_Execute; }

    private:
        std::vector<RGTextureAccess> m_TextureAccesses;
        std::vector<RGBufferAccess> m_BufferAccesses;
        PassExecuteFn m_Execute;
    };

    struct CompiledPass {
        std::string name;
        QueueType queue;
        PassExecuteFn execute;

        std::vector<VkImageMemoryBarrier2> imageBarriers;
        std::vector<VkBufferMemoryBarrier2> bufferBarriers;

        std::vector<RGTextureAccess> colorAttachments;
        std::vector<RGTextureAccess> depthAttachments; // at most 1
    };

    class RenderGraph {
    public:
        RGTextureHandle DeclareTexture(RGTextureDesc desc);

        RGBufferHandle DeclareBuffer(RGBufferDesc desc);

        void ImportTexture(RGTextureHandle handle, RGResolvedTexture resolved);

        void ImportBuffer(RGBufferHandle handle, RGResolvedBuffer resolved);

        using PassSetupFn = std::function<void(RGPassBuilder &)>;

        void AddPass(std::string name, QueueType queue, PassSetupFn setup);

        void Compile();

        void Execute(VkCommandBuffer cmd, VkExtent2D swapchainExtent);

        void ResetImports();

        void Reset();

    private:
        struct PassDesc {
            std::string name;
            QueueType queue;
            std::vector<RGTextureAccess> textureAccesses;
            std::vector<RGBufferAccess> bufferAccesses;
            PassExecuteFn execute;
        };

        struct TextureState {
            VkPipelineStageFlags2 stage = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
            VkAccessFlags2 access = VK_ACCESS_2_NONE;
            VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
        };

        struct BufferState {
            VkPipelineStageFlags2 stage = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
            VkAccessFlags2 access = VK_ACCESS_2_NONE;
        };

        std::vector<RGTextureDesc> m_TextureDescs;
        std::vector<RGBufferDesc> m_BufferDescs;
        u32 m_NextTextureIdx = 1; // 0 = null
        u32 m_NextBufferIdx = 1;

        RGResources m_Resources;

        std::vector<PassDesc> m_Passes;
        std::vector<CompiledPass> m_Compiled;
        bool m_NeedsCompile = true;

        VkExtent2D m_SwapchainExtent{};

        void BuildBarriers();

        VkImageMemoryBarrier2 MakeImageBarrier(
                VkImage image,
                const TextureState &src, const TextureState &dst,
                VkImageLayout srcLayout, VkImageLayout dstLayout,
                VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT) const;

        VkBufferMemoryBarrier2 MakeBufferBarrier(
                VkBuffer buffer, VkDeviceSize size,
                const BufferState &src, const BufferState &dst) const;

        static VkImageLayout LayoutForAccess(const RGTextureAccess &acc);

        static VkImageAspectFlags AspectForFormat(VkFormat fmt);
    };
} // namespace Manro
