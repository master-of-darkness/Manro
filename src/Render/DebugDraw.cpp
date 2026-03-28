#include <Manro/Render/DebugDraw.h>
#include <cmath>
#include <numbers>

namespace Manro::DebugDraw {

    static void Push(std::vector<DebugVertex> &out, const Vec3 &p, u32 c) {
        out.push_back({p, c});
    }

    void Line(std::vector<DebugVertex> &out,
              const Vec3 &a, const Vec3 &b, u32 color, bool) {
        Push(out, a, color);
        Push(out, b, color);
    }

    void AABB(std::vector<DebugVertex> &out,
              const Vec3 &mn, const Vec3 &mx, u32 color, bool depthTest) {
        Vec3 corners[8] = {
                {mn.x, mn.y, mn.z},
                {mx.x, mn.y, mn.z},
                {mx.x, mx.y, mn.z},
                {mn.x, mx.y, mn.z},
                {mn.x, mn.y, mx.z},
                {mx.x, mn.y, mx.z},
                {mx.x, mx.y, mx.z},
                {mn.x, mx.y, mx.z},
        };
        static const int edges[12][2] = {
                {0, 1},
                {1, 2},
                {2, 3},
                {3, 0}, // bottom
                {4, 5},
                {5, 6},
                {6, 7},
                {7, 4}, // top
                {0, 4},
                {1, 5},
                {2, 6},
                {3, 7}  // verticals
        };
        for (auto &e: edges)
            Line(out, corners[e[0]], corners[e[1]], color, depthTest);
    }

    void Box(std::vector<DebugVertex> &out,
             const Vec3 &center, const Vec3 &half,
             const Mat4 &transform, u32 color, bool depthTest) {
        Vec3 corners[8] = {
                {-half.x, -half.y, -half.z},
                {half.x,  -half.y, -half.z},
                {half.x,  half.y,  -half.z},
                {-half.x, half.y,  -half.z},
                {-half.x, -half.y, half.z},
                {half.x,  -half.y, half.z},
                {half.x,  half.y,  half.z},
                {-half.x, half.y,  half.z},
        };
        for (auto &v: corners) {
            Vec4 t = transform * Vec4(center + v, 1.f);
            v = Vec3(t);
        }
        static const int edges[12][2] = {
                {0, 1},
                {1, 2},
                {2, 3},
                {3, 0},
                {4, 5},
                {5, 6},
                {6, 7},
                {7, 4},
                {0, 4},
                {1, 5},
                {2, 6},
                {3, 7}
        };
        for (auto &e: edges)
            Line(out, corners[e[0]], corners[e[1]], color, depthTest);
    }

    void Sphere(std::vector<DebugVertex> &out,
                const Vec3 &center, float radius,
                u32 color, int segs, bool depthTest) {
        const float step = 2.f * std::numbers::pi_v<float> / static_cast<float>(segs);
        for (int i = 0; i < segs; ++i) {
            float a0 = step * i, a1 = step * (i + 1);
            float c0 = cosf(a0), s0 = sinf(a0);
            float c1 = cosf(a1), s1 = sinf(a1);
            // XY
            Line(out, center + Vec3(c0, s0, 0) * radius,
                 center + Vec3(c1, s1, 0) * radius, color, depthTest);
            // XZ
            Line(out, center + Vec3(c0, 0, s0) * radius,
                 center + Vec3(c1, 0, s1) * radius, color, depthTest);
            // YZ
            Line(out, center + Vec3(0, c0, s0) * radius,
                 center + Vec3(0, c1, s1) * radius, color, depthTest);
        }
    }

    void Frustum(std::vector<DebugVertex> &out,
                 const Mat4 &invViewProj, u32 color, bool depthTest) {
        static const Vec4 ndc[8] = {
                {-1, -1, -1, 1},
                {1,  -1, -1, 1},
                {1,  1,  -1, 1},
                {-1, 1,  -1, 1},
                {-1, -1, 1,  1},
                {1,  -1, 1,  1},
                {1,  1,  1,  1},
                {-1, 1,  1,  1},
        };
        Vec3 corners[8];
        for (int i = 0; i < 8; ++i) {
            Vec4 w = invViewProj * ndc[i];
            corners[i] = Vec3(w) / w.w;
        }
        static const int edges[12][2] = {
                {0, 1},
                {1, 2},
                {2, 3},
                {3, 0},
                {4, 5},
                {5, 6},
                {6, 7},
                {7, 4},
                {0, 4},
                {1, 5},
                {2, 6},
                {3, 7}
        };
        for (auto &e: edges)
            Line(out, corners[e[0]], corners[e[1]], color, depthTest);
    }

    void Cross(std::vector<DebugVertex> &out,
               const Vec3 &center, float size, u32 color, bool depthTest) {
        Line(out, center - Vec3(size, 0, 0), center + Vec3(size, 0, 0), color, depthTest);
        Line(out, center - Vec3(0, size, 0), center + Vec3(0, size, 0), color, depthTest);
        Line(out, center - Vec3(0, 0, size), center + Vec3(0, 0, size), color, depthTest);
    }

    void Axes(std::vector<DebugVertex> &out, const Mat4 &transform, float size) {
        Vec3 o = Vec3(transform[3]);
        Vec3 rx = Vec3(transform[0]) * size;
        Vec3 ry = Vec3(transform[1]) * size;
        Vec3 rz = Vec3(transform[2]) * size;
        Line(out, o, o + rx, Colors::Red, true);
        Line(out, o, o + ry, Colors::Green, true);
        Line(out, o, o + rz, Colors::Blue, true);
    }

} // namespace Manro::DebugDraw