#pragma once

#include <Manro/Core/Types.h>
#include <vector>

namespace Manro {

    struct DebugVertex {
        Vec3 position;
        u32 color;
    };

    struct DebugLine {
        Vec3 start;
        Vec3 end;
        u32 color;
        bool depthTest{true};
    };

    namespace DebugDraw {

        void Line(std::vector<DebugVertex> &out,
                  const Vec3 &a, const Vec3 &b,
                  u32 color, bool depthTest = true);

        void AABB(std::vector<DebugVertex> &out,
                  const Vec3 &min, const Vec3 &max,
                  u32 color, bool depthTest = true);

        void Box(std::vector<DebugVertex> &out,
                 const Vec3 &center, const Vec3 &halfExtents,
                 const Mat4 &transform,
                 u32 color, bool depthTest = true);

        void Sphere(std::vector<DebugVertex> &out,
                    const Vec3 &center, float radius,
                    u32 color, int segments = 16, bool depthTest = true);

        void Frustum(std::vector<DebugVertex> &out,
                     const Mat4 &invViewProj,
                     u32 color, bool depthTest = true);

        void Cross(std::vector<DebugVertex> &out,
                   const Vec3 &center, float size,
                   u32 color, bool depthTest = true);

        void Axes(std::vector<DebugVertex> &out,
                  const Mat4 &transform, float size);

        constexpr u32 Color(u8 r, u8 g, u8 b, u8 a = 255) {
            return (u32(r)) | (u32(g) << 8) | (u32(b) << 16) | (u32(a) << 24);
        }

        namespace Colors {
            inline constexpr u32 Red = Color(255, 0, 0);
            inline constexpr u32 Green = Color(0, 255, 0);
            inline constexpr u32 Blue = Color(0, 0, 255);
            inline constexpr u32 Yellow = Color(255, 255, 0);
            inline constexpr u32 Cyan = Color(0, 255, 255);
            inline constexpr u32 Magenta = Color(255, 0, 255);
            inline constexpr u32 White = Color(255, 255, 255);
            inline constexpr u32 Orange = Color(255, 128, 0);
            inline constexpr u32 Gray = Color(128, 128, 128);
        }

    } // namespace DebugDraw
} // namespace Manro