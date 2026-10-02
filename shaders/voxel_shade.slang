// Voxel shade fragment: samples the block tile array per pixel.
// Varyings carry the quad UV + flat (face, block state); the tile layer
// and cutout flag resolve here via the BDA tables in the frame root.
// Lighting is the classic face-shade + sun lambert over the albedo.
import voxel_common;

[[vk::push_constant]]
VoxelFrameRoot g_FragRoot;

[[vk::binding(0, 0)]]
Sampler2DArray g_Tiles;

struct VoxelFragIn {
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
    nointerpolation uint faceState : TEXCOORD1;
};

float4 PSMain(VoxelFragIn input) : SV_Target {
    vk::BufferPointer<VoxelFrameParams> fp = vk::BufferPointer<VoxelFrameParams>(g_FragRoot.frameAddr);
    VoxelFrameParams* fpp = fp.getPtr();
    VoxelFrameParams frame = fpp[0];

    uint state = (input.faceState >> 3u) & 0x7FFFu;
    uint f = input.faceState & 0x7u;

    vk::BufferPointer<uint> tiles = vk::BufferPointer<uint>(g_FragRoot.tileTableAddr);
    vk::BufferPointer<uint> flags = vk::BufferPointer<uint>(g_FragRoot.blockFlagsAddr);
    uint tile = tiles.getPtr()[state * 6u + f];
    uint mflags = flags.getPtr()[state];

    float4 albedo = g_Tiles.Sample(float3(input.uv, float(tile)));
    // Cutout (leaves): punch holes, keep depth write.
    if ((mflags & 2u) != 0u && albedo.a < 0.5) {
        discard;
    }

    float3 nrm = kFaceNormal[f];
    vk::BufferPointer<float4> sun = vk::BufferPointer<float4>(frame.sunAddr);
    float4* sunData = sun.getPtr();
    float3 toSun = -sunData[0].xyz; // normalized once on upload
    float ndl = saturate(dot(nrm, toSun));
    float an = abs(nrm.y) > 0.5 ? (nrm.y > 0.0 ? 1.0 : 0.5)
             : (abs(nrm.x) > 0.5 ? 0.8 : 0.65);
    float3 lit = albedo.rgb * (0.15 + sunData[1].rgb * (ndl * sunData[1].a)) * an;

    if (frame.giEnabled != 0u) {
        vk::BufferPointer<float4> gi = vk::BufferPointer<float4>(frame.giAddr);
        float4* cascades = gi.getPtr();
        lit += cascades[0].rgb;
    }
    return float4(lit, 1.0);
}
