#include <Manro/Render/RHI/IRenderDevice.h>
#include <Manro/Render/RHI/VulkanCommandList.h>

#include <Manro/Core/Handle.h>
#include <Manro/Platform/Window/IWindow.h>
#include <algorithm>
#include <cstring>

namespace Manro::RHI {

    namespace {
        struct BufferRecord {
            BufferDesc desc{};
            std::vector<u8> bytes;
        };

        struct TextureRecord {
            TextureDesc desc{};
        };

        struct PipelineRecord {
            PipelineDesc desc{};
            bool isCompute{false};
        };

        class StubRenderDevice final : public IRenderDevice {
        public:
            StubRenderDevice(u32 width, u32 height)
                    : m_Width(width), m_Height(height) {
                TextureDesc scDesc{};
                scDesc.width = width;
                scDesc.height = height;
                scDesc.format = Format::B8G8R8A8_Unorm;
                m_SwapchainTexture = m_Textures.Insert(TextureRecord{scDesc});
            }

            BufferHandle CreateBuffer(const BufferDesc &desc) override {
                BufferRecord rec{};
                rec.desc = desc;
                rec.bytes.resize(static_cast<size_t>(desc.size));
                return m_Buffers.Insert(std::move(rec));
            }

            void DestroyBuffer(BufferHandle handle) override { m_Buffers.Remove(handle); }

            void WriteBuffer(BufferHandle handle, const void *data, u64 size, u64 offset) override {
                BufferRecord *rec = m_Buffers.Get(handle);
                if (!rec || !data || size == 0) return;
                if (offset >= rec->bytes.size()) return;

                const u64 writable = std::min<u64>(size, static_cast<u64>(rec->bytes.size()) - offset);
                std::memcpy(rec->bytes.data() + offset, data, static_cast<size_t>(writable));
            }

            TextureHandle CreateTexture(const TextureDesc &desc) override {
                return m_Textures.Insert(TextureRecord{desc});
            }

            void DestroyTexture(TextureHandle handle) override {
                if (handle == m_SwapchainTexture) return;
                m_Textures.Remove(handle);
            }

            PipelineHandle CreateGraphicsPipeline(const PipelineDesc &desc) override {
                return m_Pipelines.Insert(PipelineRecord{desc, false});
            }

            PipelineHandle CreateComputePipeline(const PipelineDesc &desc) override {
                return m_Pipelines.Insert(PipelineRecord{desc, true});
            }

            void DestroyPipeline(PipelineHandle handle) override { m_Pipelines.Remove(handle); }

            bool BeginFrame() override { return true; }

            ICommandList &GetCommandList() override { return m_CommandList; }

            void EndFrame() override {}

            TextureHandle GetSwapchainTexture() const override { return m_SwapchainTexture; }

            Format GetSwapchainFormat() const override { return Format::B8G8R8A8_Unorm; }

            void OnResize(u32 w, u32 h) override {
                m_Width = w;
                m_Height = h;
                if (TextureRecord *rec = m_Textures.Get(m_SwapchainTexture)) {
                    rec->desc.width = w;
                    rec->desc.height = h;
                }
            }

            AdapterInfo GetAdapterInfo() const override {
                AdapterInfo info{};
                const char *name = "StubRHI (migration in progress)";
                std::memcpy(info.name, name, std::strlen(name));
                return info;
            }

        private:
            u32 m_Width{0};
            u32 m_Height{0};
            TextureHandle m_SwapchainTexture{};
            VulkanCommandList m_CommandList;

            SlotMap<BufferRecord, BufferHandle> m_Buffers;
            SlotMap<TextureRecord, TextureHandle> m_Textures;
            SlotMap<PipelineRecord, PipelineHandle> m_Pipelines;
        };
    } // namespace

    Scope<IRenderDevice> IRenderDevice::CreateVulkan(::Manro::IWindow &window, u32 width, u32 height, bool vsync) {
        (void) window;
        (void) vsync;
        return CreateScope<StubRenderDevice>(width, height);
    }

} // namespace Manro::RHI

