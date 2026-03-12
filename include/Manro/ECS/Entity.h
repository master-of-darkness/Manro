#pragma once

#include <Manro/Core/Types.h>
#include <bitset>

namespace Manro {
    using Entity = u32;

    constexpr Entity NULL_ENTITY = 0xFFFFFFFF;
    constexpr u32 MAX_ENTITIES = 5000;
    constexpr u32 MAX_COMPONENTS = 32;

    using Signature = std::bitset<MAX_COMPONENTS>;
} // namespace Manro
