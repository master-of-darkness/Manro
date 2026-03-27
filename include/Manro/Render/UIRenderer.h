#pragma once

#include <Manro/Render/RHI/IRenderDevice.h>

namespace Manro {

    class UIRenderer {
    public:
        UIRenderer(RHI::Format targetFormat);

        ~UIRenderer();

        void NewFrame();

        void Render(RHI::ICommandList &cmd);

        bool IsEnabled() const { return m_Enabled; }

        void SetEnabled(bool e) { m_Enabled = e; }

    private:
        bool m_Enabled{true};
    };

} // namespace Manro