#include <Manro/Render/UIRenderer.h>

namespace Manro { // TODO: implement

    UIRenderer::UIRenderer(RHI::IRenderDevice &device, RHI::Format targetFormat)
            : m_Device(device) {
        (void) targetFormat;
    }

    UIRenderer::~UIRenderer() = default;

    void UIRenderer::NewFrame() {}

    void UIRenderer::Render(RHI::ICommandList &cmd) {
        (void) cmd;
    }

} // namespace Manro