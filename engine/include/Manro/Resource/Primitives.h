#pragma once

#include <Manro/Resource/ModelLoader.h>
#include <Manro/Core/Types.h>

namespace Manro {
    class CPrimitives {
    public:
        static ModelData_t CreateCube(float size = 1.0f);
    };
} // namespace Manro