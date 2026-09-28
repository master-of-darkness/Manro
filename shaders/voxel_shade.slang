// Voxel shade fragment: passthrough for the per-face flat color computed
// in the mesh shader (lighting inputs are all face-uniform, so per-pixel
// evaluation was redundant). GI/ray-query hooks land here in Phase 6.
import voxel_common;

[[vk::push_constant]]
VoxelFrameRoot g_FragRoot;

struct VoxelFragIn {
    float4 pos : SV_Position;
    nointerpolation float3 color : COLOR;
};

float4 PSMain(VoxelFragIn input) : SV_Target {
    vk::BufferPointer<VoxelFrameParams> fp = vk::BufferPointer<VoxelFrameParams>(g_FragRoot.frameAddr);
    VoxelFrameParams* fpp = fp.getPtr();
    VoxelFrameParams frame = fpp[0];

    float3 lit = input.color;

    if (frame.giEnabled != 0u) {
        vk::BufferPointer<float4> gi = vk::BufferPointer<float4>(frame.giAddr);
        float4* cascades = gi.getPtr();
        lit += cascades[0].rgb;
    }
    return float4(lit, 1.0);
}
