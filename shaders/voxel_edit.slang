// GPU-local voxel edits: mutates brick bitmasks + material words in VRAM.
// CPU only appends EditCmd records; no mesh re-bake, no re-upload.
struct VoxelEditCmd {
    float3 pos;    // world-space center
    float radius;  // world units (0 = single voxel)
    uint material; // material to write (ignored for erase)
    uint op;       // 0 = erase, 1 = write
};

struct VoxelEditPush {
    uint64_t brickBufferAddr;
    uint64_t headerAddr;
    uint64_t pageTableAddr;
    uint64_t editAddr; // VoxelEditCmd[editCount]
    float3 worldMin;
    float brickSize;
    uint editCount;
    uint virtualDim; // bricks per axis in the page table
};

[[vk::push_constant]]
VoxelEditPush g_Edit;

[shader("compute")]
[numthreads(64, 1, 1)]
void EditMain(uint3 dtid : SV_DispatchThreadID) {
    if (dtid.x >= g_Edit.editCount)
        return;

    vk::BufferPointer<VoxelEditCmd> edits = vk::BufferPointer<VoxelEditCmd>(g_Edit.editAddr);
    VoxelEditCmd* cmds = edits.getPtr();
    VoxelEditCmd cmd = cmds[dtid.x];

    // Map world pos -> brick index via the virtual page table.
    float3 local = (cmd.pos - g_Edit.worldMin) / g_Edit.brickSize;
    int3 brickCoord = int3(floor(local));
    int dim = int(g_Edit.virtualDim);
    if (brickCoord.x < 0 || brickCoord.y < 0 || brickCoord.z < 0 ||
        brickCoord.x >= dim || brickCoord.y >= dim || brickCoord.z >= dim)
        return;
    uint brickIdx = uint(brickCoord.x + brickCoord.y * dim + brickCoord.z * dim * dim);

    vk::BufferPointer<int> pageTable = vk::BufferPointer<int>(g_Edit.pageTableAddr);
    int* pages = pageTable.getPtr();
    if (pages[brickIdx] < 0)
        return; // sparse page not resident; streaming pass binds it first

    vk::BufferPointer<uint> bricks = vk::BufferPointer<uint>(g_Edit.brickBufferAddr);
    uint* base = bricks.getPtr();
    uint* mats = base + brickIdx * 2176u;
    uint* occupancy = mats + 2048u;

    // Sphere edit in brick-local voxel coords.
    float voxelSize = g_Edit.brickSize / 16.0;
    float3 brickMin = g_Edit.worldMin + float3(brickCoord) * g_Edit.brickSize;
    for (uint i = 0u; i < 4096u; ++i) {
        uint x = i & 15u;
        uint y = (i >> 4u) & 15u;
        uint z = (i >> 8u) & 15u;
        float3 c = brickMin + (float3(float(x), float(y), float(z)) + 0.5) * voxelSize;
        if (distance(c, cmd.pos) > cmd.radius + voxelSize * 0.5)
            continue;
        uint word = i >> 5u;
        uint bit = 1u << (i & 31u);
        if (cmd.op == 0u) {
            occupancy[word] &= ~bit;
        } else {
            occupancy[word] |= bit;
            uint matWord = i >> 1u;
            // 2x uint16 packed per uint: read-modify-write via address arithmetic.
            // NOTE: concurrent edits to sibling voxels in the same word race;
            // the edit queue serializes overlapping bricks on the CPU side.
            uint cur = mats[matWord];
            if ((i & 1u) == 0u)
                cur = (cur & 0xFFFF0000u) | (cmd.material & 0xFFFFu);
            else
                cur = (cur & 0x0000FFFFu) | ((cmd.material & 0xFFFFu) << 16u);
            mats[matWord] = cur;
        }
    }

    // Mark dirty so the BLAS refit + HiZ invalidate passes pick it up.
    vk::BufferPointer<uint> headers = vk::BufferPointer<uint>(g_Edit.headerAddr);
    uint* h = headers.getPtr();
    h[brickIdx * 8u + 1u] |= 2u; // flags word, bit 1 = dirty
}
