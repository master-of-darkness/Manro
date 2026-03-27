#include <Manro/Render/UIRenderer.h>

namespace Manro { // TODO: implement

    UIRenderer::UIRenderer(RHI::Format targetFormat) {
        (void) targetFormat;
    }

    UIRenderer::~UIRenderer() = default;

    void UIRenderer::NewFrame() {}

    void UIRenderer::Render(RHI::ICommandList &cmd) {
        (void) cmd;
    }

} // namespace Manro