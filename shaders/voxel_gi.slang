struct VoxelGiPush {
    uint64_t brickBufferAddr;
    uint64_t headerAddr;
    uint64_t cascadeAddr;
    uint64_t sunAddr;
    uint brickCount;
    uint cascadeRes;
    uint cascadeCount;
    float brickSize;
    float3 worldMin;
};

[[vk::push_constant]]
VoxelGiPush g_Gi;

[shader("compute")]
[numthreads(8, 8, 8)]
void GiInjectMain(uint3 dtid : SV_DispatchThreadID) {
    if (dtid.x >= g_Gi.cascadeRes || dtid.y >= g_Gi.cascadeRes || dtid.z >= g_Gi.cascadeRes)
        return;

    vk::BufferPointer<float4> cascades = vk::BufferPointer<float4>(g_Gi.cascadeAddr);
    float4* vols = cascades.getPtr();
    uint idx = dtid.x + dtid.y * g_Gi.cascadeRes + dtid.z * g_Gi.cascadeRes * g_Gi.cascadeRes;
    vols[idx] = float4(0, 0, 0, 1);
}

[shader("compute")]
[numthreads(8, 8, 8)]
void GiPropagateMain(uint3 dtid : SV_DispatchThreadID) {
    if (dtid.x >= g_Gi.cascadeRes || dtid.y >= g_Gi.cascadeRes || dtid.z >= g_Gi.cascadeRes)
        return;
    vk::BufferPointer<float4> cascades = vk::BufferPointer<float4>(g_Gi.cascadeAddr);
    float4* vols = cascades.getPtr();
    uint R = g_Gi.cascadeRes;
    uint idx = dtid.x + dtid.y * R + dtid.z * R * R;

    float4 acc = float4(0, 0, 0, 0);
    acc += vols[dtid.x > 0   ? idx - 1   : idx];
    acc += vols[dtid.x + 1 < R ? idx + 1   : idx];
    acc += vols[dtid.y > 0   ? idx - R   : idx];
    acc += vols[dtid.y + 1 < R ? idx + R   : idx];
    acc += vols[dtid.z > 0   ? idx - R*R : idx];
    acc += vols[dtid.z + 1 < R ? idx + R*R : idx];
    vols[idx] = lerp(vols[idx], acc / 6.0, 0.5);
}
