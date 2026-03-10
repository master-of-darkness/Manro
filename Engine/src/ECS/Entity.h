#pragma once

#include <Core/Types.h>
#include <bitset>

namespace Engine {
    using Entity = u32;

    constexpr Entity NULL_ENTITY = 0xFFFFFFFF;
    constexpr u32 MAX_ENTITIES = 5000;
    constexpr u32 MAX_COMPONENTS = 32;

    using Signature = std::bitset<MAX_COMPONENTS>;
} // namespace Engine