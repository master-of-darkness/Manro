// Hi-Z single-pass downsampler (SPD): builds the depth pyramid used by
// the task shader for two-pass occlusion culling.
// NOTE: BDA-only in v1 — no descriptor-bound images (pipeline has no set
// layout). The SPD pass is wired up in the follow-up; this shader is kept
// compiling but unused at runtime.
struct HizSpdPush {
    uint64_t srcAddr;   // reserved: source depth buffer BDA
    uint64_t dstAddr;   // reserved: destination HiZ buffer BDA
    uint srcMip;
    uint dstMip;
    uint width;
    uint height;
    float nearZ;
    float farZ;
};

[[vk::push_constant]]
HizSpdPush g_Spd;

float Linearize(float d) {
    return g_Spd.nearZ * g_Spd.farZ / (g_Spd.farZ - d * (g_Spd.farZ - g_Spd.nearZ));
}

[shader("compute")]
[numthreads(8, 8, 1)]
void SpdMain(uint3 dtid : SV_DispatchThreadID) {
    if (dtid.x >= g_Spd.width || dtid.y >= g_Spd.height)
        return;
    // v1: depth-pyramid write via BDA lands in the follow-up; keep the
    // entry point compiling and side-effect free until descriptors exist.
}
