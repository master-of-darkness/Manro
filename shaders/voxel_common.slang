// Shared BDA types for the GPU-driven voxel path (Slang-only).
// All voxel buffers are traversed pointerless via device addresses
// published in VoxelPush — no descriptor binding per brick.

struct VoxelBrickHeader {
    float3 origin;   // world-space min corner of the 16^3 brick
    uint pageIndex;  // index into sparse page residency table
    uint flags;      // bit 0: resident, bit 1: dirty (needs BLAS refit)
    uint paletteBase;// offset into global material palette
    uint _pad0;
    uint _pad1;
};

// Frame root: the ONLY push constant (~112B). Everything else is
// reached pointerless via BDA — no descriptor binding per brick.
// (The tile texture array IS descriptor-bound: images can't travel by BDA.)
struct VoxelFrameRoot {
    uint64_t brickBufferAddr; // Brick voxel words: uint16 matID[4096] + uint64 occupancy[64]
    uint64_t headerAddr;      // VoxelBrickHeader[brickCount]
    uint64_t pageTableAddr;   // int32 pageTable[virtualBrickCount], -1 = unbound
    uint64_t visibilityAddr;  // uint2 visibility[brickCount] (brickID, packedQuadBase)
    uint64_t frameAddr;       // VoxelFrameParams (viewProj, camera, world, palette, sun, GI)
    uint64_t debugAddr;       // DEBUG: uint counters[8] (taskRuns, visible, faces, meshRuns, ...)
    uint64_t faceCacheAddr;   // uint[4096] per resident brick: [0]=faceCount, [1..4095]=packed faces
    uint64_t tileTableAddr;   // uint32[32768*6]: tile layer per (state, face)
    uint64_t blockFlagsAddr;  // uint32[32768]: kBlockFlag* per block state id
    uint64_t shapeTableAddr;  // uint32[32768*2]: shapeMin/shapeMax per state (0..16 bytes packed)
    uint64_t uvTableAddr;     // uint32[32768*6]: uv sub-rect per (state, face) (u0|v0<<8|u1<<16|v1<<24)
};

struct VoxelFrameParams {
    float4x4 viewProj;
    float4x4 prevViewProj;
    float3 cameraPos;
    uint brickCount;
    float3 worldMin;          // virtual world min corner (for hash -> page table)
    float brickSize;          // world units per brick edge (16 * voxelSize)
    uint maxDrawDistance;
    uint enableHiZ;           // reserved: software HiZ removed (ZCULL handles it)
    uint64_t paletteAddr;     // float4 albedo[512]
    uint64_t sunAddr;         // float4 sunDir + float4 sunColor+intensity
    uint64_t giAddr;          // radiance cascade buffer (0 = disabled)
    uint giEnabled;
    uint shadowsEnabled;
    uint debugEnabled;        // 0 = skip debug atomics + host readback path
    uint useBackface;         // mesh per-face backface cull (degenerate quads)
    uint useFrustum;          // task brick frustum + distance cull
    uint virtualDim;          // bricks per axis: task cross-brick page lookups
};

// Task->mesh payload: tiny — mesh loads faces from the face cache.
// brickIdx addresses headers, brick data AND face cache (all resident-ordinal).
// Kept small on purpose: the old 8KB faces[] copy ran per lane per brick.
struct VoxelTaskPayload {
    uint brickIdx;
    uint faceCount;
};

// Face-cache stride: [0] = faceCount, [1..4095] = packed faces
// (voxelIdx:12 | face:3 | mat:9). Dense thin-block bricks (slab floors,
// trapdoor linings, fence rows in rock, mineshaft/trial-chamber timber)
// emit more than the old 2048 cap: overflow silently drops arbitrary faces
// = rectangular holes near dense builds. Measured 3501 worst in a real
// temple/trial world; 4095 covers that with margin (stride 4096 = exactly
// 16 KiB task shared memory, the guaranteed minimum). Only pathological
// densities (alternating solid/thin every voxel) still truncate.
static const uint kVoxelFaceCacheCap = 4095u;
static const uint kVoxelFaceCacheStride = 4096u;

static const uint kVoxelFacesPerMeshGroup = 32u;

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

// Per-state baked shape: union AABB of the resolved model elements in
// 0..16 voxel units (trapdoor slabs, door panels, bamboo stalks, fence
// posts+arms, ...; full cube = 0,0,0..16,16,16). Baked on CPU (see
// CollectStateShape); thin states render shrunken quads and never occlude.
int3 VoxelShapeMin(uint* shapes, uint state) {
    uint p = shapes[state * 2u];
    return int3(p & 0xFFu, (p >> 8u) & 0xFFu, (p >> 16u) & 0xFFu);
}

int3 VoxelShapeMax(uint* shapes, uint state) {
    uint p = shapes[state * 2u + 1u];
    return int3(p & 0xFFu, (p >> 8u) & 0xFFu, (p >> 16u) & 0xFFu);
}

// Baked per-face uv sub-rect of the block's tile, normalized 0..1
// (u0,v0,u1,v1; u0>u1 / v0>v1 encode flips). Full-rect faces return
// (0,0,1,1), reproducing the old full-tile sampling exactly.
float4 VoxelFaceUv(uint* uvs, uint state, uint f) {
    uint p = uvs[state * 6u + f];
    return float4(float(p & 0xFFu), float((p >> 8u) & 0xFFu),
                  float((p >> 16u) & 0xFFu), float((p >> 24u) & 0xFFu)) / 255.0;
}

// Does neighbor N (shape nMin/nMax) COVER voxel V's face f (shape
// vMin/vMax)? Cover = both shapes reach the shared boundary plane AND N's
// rect contains V's rect on that plane. Thin neighbors set back from the
// plane (fence posts, stalks) never cover; thin V faces set back from the
// plane (trapdoor tops) are never covered. Full-cube pairs cover exactly
// like the old opaque rule.
bool VoxelFaceCovered(int3 vMin, int3 vMax, int3 nMin, int3 nMax, uint f) {
    uint ax = f >> 1u; // face order +X,-X,+Y,-Y,+Z,-Z: pair per axis
    if ((f & 1u) == 0u) { // positive side: V.max touches, N.min touches
        if ((ax == 0u && (vMax.x != 16 || nMin.x != 0)) ||
            (ax == 1u && (vMax.y != 16 || nMin.y != 0)) ||
            (ax == 2u && (vMax.z != 16 || nMin.z != 0)))
            return false;
    } else { // negative side: V.min touches, N.max touches
        if ((ax == 0u && (vMin.x != 0 || nMax.x != 16)) ||
            (ax == 1u && (vMin.y != 0 || nMax.y != 16)) ||
            (ax == 2u && (vMin.z != 0 || nMax.z != 16)))
            return false;
    }
    // Containment on the two tangent axes (partial overlap still emits:
    // the per-frame backface cull hides the coplanar overlap, while
    // culling on partial overlap would punch holes in the exposed part).
    if (ax != 0u && (nMin.x > vMin.x || nMax.x < vMax.x))
        return false;
    if (ax != 1u && (nMin.y > vMin.y || nMax.y < vMax.y))
        return false;
    if (ax != 2u && (nMin.z > vMin.z || nMax.z < vMax.z))
        return false;
    return true;
}

// Base corner + tangent extents of voxel (x,y,z)'s face-f quad for the
// given shape, in brick-local voxel units. Corner ORDER matches the old
// full-cube emission ([b, b+e1, b+e1+e2, b+e2] with e1/e2 along the same
// tangents), so the quad UV mapping below is untouched; full shapes
// reproduce the old quads exactly.
void VoxelFaceQuad(uint x, uint y, uint z, uint f, int3 sMin, int3 sMax,
                   out float3 b, out float3 e1, out float3 e2) {
    float3 mn = float3(float(x), float(y), float(z)) + float3(sMin) / 16.0;
    float3 mx = float3(float(x), float(y), float(z)) + float3(sMax) / 16.0;
    float3 ex = mx - mn;
    if (f == 0u) { // +X
        b = float3(mx.x, mn.y, mn.z); e1 = float3(0.0, ex.y, 0.0); e2 = float3(0.0, 0.0, ex.z);
    } else if (f == 1u) { // -X
        b = float3(mn.x, mn.y, mn.z); e1 = float3(0.0, ex.y, 0.0); e2 = float3(0.0, 0.0, ex.z);
    } else if (f == 2u) { // +Y
        b = float3(mn.x, mx.y, mn.z); e1 = float3(ex.x, 0.0, 0.0); e2 = float3(0.0, 0.0, ex.z);
    } else if (f == 3u) { // -Y
        b = float3(mn.x, mn.y, mn.z); e1 = float3(ex.x, 0.0, 0.0); e2 = float3(0.0, 0.0, ex.z);
    } else if (f == 4u) { // +Z
        b = float3(mn.x, mn.y, mx.z); e1 = float3(ex.x, 0.0, 0.0); e2 = float3(0.0, ex.y, 0.0);
    } else { // -Z
        b = float3(mn.x, mn.y, mn.z); e1 = float3(ex.x, 0.0, 0.0); e2 = float3(0.0, ex.y, 0.0);
    }
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
// NOTE: materials are block state ids; the face cache stores
// (voxelIdx:12 | face:3 | state:15) in one word (state < 32768).
static const uint kVoxelVertMaskX = 0x3Fu;
static const uint kVoxelVertShiftN = 18u;
static const uint kVoxelVertShiftAO = 21u;
static const uint kVoxelVertShiftMat = 23u;
static const uint kBlockStateBits = 15u;
static const uint kBlockStateMax = 32768u;

uint PackVoxelVertex(uint x, uint y, uint z, uint n, uint ao, uint mat) {
    return (x & 0x3Fu)
         | ((y & 0x3Fu) << 6u)
         | ((z & 0x3Fu) << 12u)
         | ((n & 0x7u) << kVoxelVertShiftN)
         | ((ao & 0x3u) << kVoxelVertShiftAO)
         | ((mat & 0x1FFu) << kVoxelVertShiftMat);
}
