// Shared BDA types for the GPU-driven voxel path (Slang-only).
// All voxel buffers are traversed pointerless via device addresses
// published in VoxelPush — no descriptor binding per brick.

struct VoxelBrickHeader {
    float3 origin;   // world-space min corner of the 16^3 brick
    uint pageIndex;  // index into sparse page residency table
    uint flags;      // bit 0: resident, bit 1: dirty (needs BLAS refit + HiZ invalidate)
    uint paletteBase;// offset into global material palette
    uint _pad0;
    uint _pad1;
};

// Frame root: the ONLY push constant (~112B). Everything else is
// reached pointerless via BDA — no descriptor binding per brick.
struct VoxelFrameRoot {
    uint64_t brickBufferAddr; // Brick voxel words: uint16 matID[4096] + uint64 occupancy[64]
    uint64_t headerAddr;      // VoxelBrickHeader[brickCount]
    uint64_t pageTableAddr;   // int32 pageTable[virtualBrickCount], -1 = unbound
    uint64_t visibilityAddr;  // uint2 visibility[brickCount] (brickID, packedQuadBase)
    uint64_t frameAddr;       // VoxelFrameParams (viewProj, camera, world, HiZ, palette, sun, GI)
    uint64_t debugAddr;       // DEBUG: uint counters[8] (taskRuns, visible, faces, meshRuns, ...)
};

struct VoxelFrameParams {
    float4x4 viewProj;
    float4x4 prevViewProj;
    float3 cameraPos;
    uint brickCount;
    float3 worldMin;          // virtual world min corner (for hash -> page table)
    float brickSize;          // world units per brick edge (16 * voxelSize)
    uint maxDrawDistance;
    uint enableHiZ;
    uint64_t paletteAddr;     // float4 albedo[512]
    uint64_t sunAddr;         // float4 sunDir + float4 sunColor+intensity
    uint64_t giAddr;          // radiance cascade buffer (0 = disabled)
    uint giEnabled;
    uint shadowsEnabled;
    uint64_t hizAddr;         // Hi-Z pyramid sampler payload (image desc via descriptors)
    uint _pad0;
};

// Task->mesh payload: compacted exposed-face list for one brick.
// faces[] packs (voxelIdx:12 | face:3 | mat:9). Worst-case surface of a
// dense 16^3 brick is ~1536 faces; cap 2048 fits the 16KB payload limit.
struct VoxelTaskPayload {
    uint brickIdx;
    uint faceCount;
    uint faces[2048];
};

static const uint kVoxelFacesPerMeshGroup = 16u;

bool VoxelSolid(uint* occupancy, uint x, uint y, uint z) {
    if (x >= 16u || y >= 16u || z >= 16u)
        return false;
    uint bit = x + y * 16u + z * 256u;
    return ((occupancy[bit >> 5u] >> (bit & 31u)) & 1u) != 0u;
}

uint VoxelMaterial(uint* mats, uint x, uint y, uint z) {
    uint bit = x + y * 16u + z * 256u;
    uint word = mats[bit >> 1u];
    // mats[] packs 2x uint16 LE: even voxel = low half, odd = high half.
    return ((bit & 1u) == 0u) ? (word & 0xFFFFu) : (word >> 16u);
}

// Face tables: +X,-X,+Y,-Y,+Z,-Z neighbour offsets and outward normals.
static const int3 kFaceOffset[6] = {
    int3(1, 0, 0), int3(-1, 0, 0),
    int3(0, 1, 0), int3(0, -1, 0),
    int3(0, 0, 1), int3(0, 0, -1),
};

static const float3 kFaceNormal[6] = {
    float3(1, 0, 0), float3(-1, 0, 0),
    float3(0, 1, 0), float3(0, -1, 0),
    float3(0, 0, 1), float3(0, 0, -1),
};

// Packed micro-quad vertex: X:6 Y:6 Z:6 (brick-local voxel coords)
// Normal:3 AO:2 Material:9 — single 32-bit word per vertex.
static const uint kVoxelVertMaskX = 0x3Fu;
static const uint kVoxelVertShiftN = 18u;
static const uint kVoxelVertShiftAO = 21u;
static const uint kVoxelVertShiftMat = 23u;

uint PackVoxelVertex(uint x, uint y, uint z, uint n, uint ao, uint mat) {
    return (x & 0x3Fu)
         | ((y & 0x3Fu) << 6u)
         | ((z & 0x3Fu) << 12u)
         | ((n & 0x7u) << kVoxelVertShiftN)
         | ((ao & 0x3u) << kVoxelVertShiftAO)
         | ((mat & 0x1FFu) << kVoxelVertShiftMat);
}
