#pragma once

#include <Manro/Core/Types.h>
#include <atomic>

namespace Manro {
    namespace detail {
        inline std::atomic<u32> g_NextComponentTypeId{0};
    }

    template<typename T>
    u32 ComponentTypeId() noexcept {
        static const u32 id = detail::g_NextComponentTypeId.fetch_add(1, std::memory_order_relaxed);
        return id;
    }
} // namespace Manro