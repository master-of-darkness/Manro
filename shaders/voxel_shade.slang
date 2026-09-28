// Voxel shade fragment: unpacks mesh-shader varyings, applies
// palette albedo + sun lambert + face AO. GI/ray-query hooks land here
// in Phase 6 (radiance cascades + BLAS soft shadows).
import voxel_common;

[[vk::push_constant]]
VoxelFrameRoot g_FragRoot;

struct VoxelFragIn {
    float4 pos : SV_Position;
    float3 normal : NORMAL;
    uint packed : PACKED;
    uint material : MATERIAL;
};

float4 PSMain(VoxelFragIn input) : SV_Target {
    vk::BufferPointer<VoxelFrameParams> fp = vk::BufferPointer<VoxelFrameParams>(g_FragRoot.frameAddr);
    VoxelFrameParams* fpp = fp.getPtr();
    VoxelFrameParams frame = fpp[0];

    vk::BufferPointer<float4> palette = vk::BufferPointer<float4>(frame.paletteAddr);
    float4* albedo = palette.getPtr();
    float4 base = albedo[input.material & 511u];

    float ao = float((input.packed >> kVoxelVertShiftAO) & 0x3u) / 3.0;

    // Minecraft-style directional face shading: gives every cube 3D
    // definition independent of sun angle. Top full bright, bottom dark,
    // sides stepped (X brighter than Z) so adjacent cubes never blend flat.
    float3 fn = normalize(input.normal);
    float3 an = abs(fn);
    float faceShade = (an.y > 0.5) ? (fn.y > 0.0 ? 1.0 : 0.5) : (an.x > 0.5 ? 0.8 : 0.65);

    vk::BufferPointer<float4> sun = vk::BufferPointer<float4>(frame.sunAddr);
    float4* sunData = sun.getPtr();
    float3 sunDir = normalize(sunData[0].xyz);
    float3 sunColor = sunData[1].rgb;
    float sunI = sunData[1].a;

    float ndl = saturate(dot(fn, -sunDir));
    float3 lit = base.rgb * (0.25 + 0.75 * ao) * (0.15 + sunColor * (ndl * sunI)) * faceShade;

    if (frame.giEnabled != 0u) {
        vk::BufferPointer<float4> gi = vk::BufferPointer<float4>(frame.giAddr);
        float4* cascades = gi.getPtr();
        lit += cascades[0].rgb * base.rgb;
    }
    return float4(lit, 1.0);
}
