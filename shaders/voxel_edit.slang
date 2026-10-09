struct VoxelEditCmd {
    float3 pos;
    float radius;
    uint material;
    uint op;
};

struct VoxelEditPush {
    uint64_t brickBufferAddr;
    uint64_t headerAddr;
    uint64_t pageTableAddr;
    uint64_t editAddr;
    float3 worldMin;
    float brickSize;
    uint editCount;
    uint virtualDim;
};

[[vk::push_constant]]
VoxelEditPush g_Edit;

void SetMatAtomic(uint* mats, uint i, uint material) {
    uint matWord = i >> 1u;
    uint m = material & 0xFFFFu;
    for (uint t = 0u; t < 8u; ++t) {
        uint prev = mats[matWord];
        uint next = ((i & 1u) == 0u)
            ? (prev & 0xFFFF0000u) | m
            : (prev & 0x0000FFFFu) | (m << 16u);
        if (next == prev)
            return;
        uint orig = 0u;
        InterlockedCompareExchange(mats[matWord], prev, next, orig);
        if (orig == prev)
            return;
    }
    uint prev2 = mats[matWord];
    uint next2 = ((i & 1u) == 0u)
        ? (prev2 & 0xFFFF0000u) | m
        : (prev2 & 0x0000FFFFu) | (m << 16u);
    mats[matWord] = next2;
}

[shader("compute")]
[numthreads(64, 1, 1)]
void EditMain(uint3 dtid : SV_DispatchThreadID, uint3 gid : SV_GroupID, uint gidx : SV_GroupIndex) {
    uint editIdx = gid.x;
    if (editIdx >= g_Edit.editCount)
        return;

    vk::BufferPointer<VoxelEditCmd> edits = vk::BufferPointer<VoxelEditCmd>(g_Edit.editAddr);
    VoxelEditCmd cmd = edits.getPtr()[editIdx];

    vk::BufferPointer<uint> bricks = vk::BufferPointer<uint>(g_Edit.brickBufferAddr);
    uint* bstore = bricks.getPtr();
    vk::BufferPointer<int> pageTable = vk::BufferPointer<int>(g_Edit.pageTableAddr);
    int* pages = pageTable.getPtr();
    vk::BufferPointer<uint> headers = vk::BufferPointer<uint>(g_Edit.headerAddr);
    uint* hwords = headers.getPtr();

    float voxelSize = g_Edit.brickSize / 16.0;
    int dim = int(g_Edit.virtualDim);

    if (cmd.radius < voxelSize * 0.5) {
        if (gidx == 0) {
            float3 local = (cmd.pos - g_Edit.worldMin) / g_Edit.brickSize;
            int3 brickCoord = int3(floor(local));
            int3 wrapped = brickCoord % dim;
            if (wrapped.x < 0) wrapped.x += dim;
            if (wrapped.y < 0) wrapped.y += dim;
            if (wrapped.z < 0) wrapped.z += dim;
            uint slot = uint(wrapped.x + wrapped.y * dim + wrapped.z * dim * dim);
            int prb = pages[slot];
            if (prb >= 0) {
                uint rBrick = uint(prb);
                float3 brickMin = g_Edit.worldMin + float3(brickCoord) * g_Edit.brickSize;
                int3 lv = int3(floor((cmd.pos - brickMin) / voxelSize));
                if (lv.x >= 0 && lv.x < 16 && lv.y >= 0 && lv.y < 16 && lv.z >= 0 && lv.z < 16) {
                    uint* mats = bstore + rBrick * 2176u;
                    uint* occupancy = mats + 2048u;
                    uint i = uint(lv.x) + uint(lv.y) * 16u + uint(lv.z) * 256u;
                    uint word = i >> 5u;
                    uint bit = 1u << (i & 31u);
                    if (cmd.op == 0u) {
                        InterlockedAnd(occupancy[word], ~bit);
                    } else {
                        InterlockedOr(occupancy[word], bit);
                        SetMatAtomic(mats, i, cmd.material);
                    }
                }
                InterlockedOr(hwords[rBrick * 8u + 4u], 2u);
            }
        }
        return;
    }

    float3 rel0 = (cmd.pos - cmd.radius - g_Edit.worldMin) / g_Edit.brickSize;
    float3 rel1 = (cmd.pos + cmd.radius - g_Edit.worldMin) / g_Edit.brickSize;
    int3 b0 = int3(floor(rel0));
    int3 b1 = int3(floor(rel1));
    int3 bc = int3(floor((cmd.pos - g_Edit.worldMin) / g_Edit.brickSize));
    b0 = max(b0, bc - 2);
    b1 = min(b1, bc + 2);

    float rExt = cmd.radius + voxelSize * 0.5;
    for (int bz = b0.z; bz <= b1.z; ++bz) {
        for (int by = b0.y; by <= b1.y; ++by) {
            for (int bx = b0.x; bx <= b1.x; ++bx) {
                int3 wrapped = int3(bx, by, bz) % dim;
                if (wrapped.x < 0) wrapped.x += dim;
                if (wrapped.y < 0) wrapped.y += dim;
                if (wrapped.z < 0) wrapped.z += dim;
                uint slot = uint(wrapped.x + wrapped.y * dim + wrapped.z * dim * dim);
                int prb = pages[slot];
                if (prb < 0)
                    continue;
                uint rBrick = uint(prb);
                uint* mats = bstore + rBrick * 2176u;
                uint* occupancy = mats + 2048u;
                float3 brickMin = g_Edit.worldMin + float3(float(bx), float(by), float(bz)) * g_Edit.brickSize;
                bool touched = false;
                for (uint i = gidx; i < 4096u; i += 64u) {
                    uint x = i & 15u;
                    uint y = (i >> 4u) & 15u;
                    uint z = (i >> 8u) & 15u;
                    float3 c = brickMin + (float3(float(x), float(y), float(z)) + 0.5) * voxelSize;
                    float3 d = c - cmd.pos;
                    if (dot(d, d) > rExt * rExt)
                        continue;
                    touched = true;
                    uint word = i >> 5u;
                    uint bit = 1u << (i & 31u);
                    if (cmd.op == 0u) {
                        InterlockedAnd(occupancy[word], ~bit);
                    } else {
                        InterlockedOr(occupancy[word], bit);
                        SetMatAtomic(mats, i, cmd.material);
                    }
                }
                if (touched) {
                    InterlockedOr(hwords[rBrick * 8u + 4u], 2u);
                }
            }
        }
    }
}
