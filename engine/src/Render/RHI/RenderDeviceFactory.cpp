#include <Manro/Interfaces/IRenderDevice.h>
#include "Backend/Vulkan/VulkanContext.h"
#include "VulkanRenderDevice.h"

namespace Manro::RHI {
    Scope<IRenderDevice> IRenderDevice::CreateVulkan(::Manro::IWindow &window, u32 width, u32 height, bool vsync,
                                                     const AdapterInfo *pAdapterInfo) {
        (void) pAdapterInfo; // Not used in this overload - creates its own context

        // This overload creates its own VulkanContext (for standalone use)
        throw std::runtime_error("Standalone CreateVulkan not implemented - use overload with VulkanContext");
    }

    Scope<IRenderDevice> IRenderDevice::CreateVulkan(::Manro::VulkanContext &context, u32 width, u32 height,
                                                     bool vsync, bool manageSwapchain, u32 maxFramesInFlight) {
        return CreateScope<VulkanRenderDevice>(context, width, height, vsync, manageSwapchain, maxFramesInFlight);
    }
} // namespace Manro::RHI
