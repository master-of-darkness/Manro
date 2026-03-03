#pragma once

#include <Core/Types.h>
#include <bitset>

namespace Engine {
    using Entity = u32;

    constexpr Entity NULL_ENTITY = 0xFFFFFFFF;
    const u32 MAX_ENTITIES = 5000;
    const u32 MAX_COMPONENTS = 32;

    using Signature = std::bitset<MAX_COMPONENTS>;
} // namespace Engine