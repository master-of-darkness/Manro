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
    vk::BufferPointer<VoxelFragParams> frp = vk::BufferPointer<VoxelFragParams>(g_FragRoot.fragParamsAddr);
    VoxelFragParams fr = frp.getPtr()[0];

    uint state = (input.faceState >> 3u) & 0x7FFFu;
    uint f = input.faceState & 0x7u;

    vk::BufferPointer<uint> tiles = vk::BufferPointer<uint>(g_FragRoot.tileTableAddr);
    vk::BufferPointer<uint> flags = vk::BufferPointer<uint>(g_FragRoot.blockFlagsAddr);
    uint tile = tiles.getPtr()[state * 6u + f];
    uint mflags = flags.getPtr()[state];

    float4 albedo = g_Tiles.Sample(float3(input.uv, float(tile)));

    if ((mflags & 2u) != 0u && albedo.a < 0.5) {
        discard;
    }

    float3 nrm = kFaceNormal[f];
    float3 toSun = -fr.sunDir.xyz;
    float ndl = saturate(dot(nrm, toSun));
    float an = abs(nrm.y) > 0.5 ? (nrm.y > 0.0 ? 1.0 : 0.5)
             : (abs(nrm.x) > 0.5 ? 0.8 : 0.65);
    float3 lit = albedo.rgb * (0.15 + fr.sunColor.rgb * (ndl * fr.sunColor.a)) * an;

    if (fr.giEnabled != 0u) {
        vk::BufferPointer<float4> gi = vk::BufferPointer<float4>(fr.giAddr);
        float4* cascades = gi.getPtr();
        lit += cascades[0].rgb;
    }
    return float4(lit, 1.0);
}
