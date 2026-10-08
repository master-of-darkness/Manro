struct VoxelBrickHeader {
    float3 origin;
    uint pageIndex;
    uint flags;
    uint paletteBase;
    uint _pad0;
    uint _pad1;
};

struct VoxelFragParams {
    float4 sunDir;
    float4 sunColor;
    uint64_t giAddr;
    uint giEnabled;
    uint _pad0;
};

struct VoxelFrameRoot {
    uint64_t brickBufferAddr;
    uint64_t headerAddr;
    uint64_t pageTableAddr;
    uint64_t visibilityAddr;
    uint64_t frameAddr;
    uint64_t debugAddr;
    uint64_t faceCacheAddr;
    uint64_t tileTableAddr;
    uint64_t blockFlagsAddr;
    uint64_t shapeTableAddr;
    uint64_t uvTableAddr;
    uint64_t fragParamsAddr;
};

struct VoxelFrameParams {
    float4x4 viewProj;
    float4x4 prevViewProj;
    float3 cameraPos;
    uint brickCount;
    float3 worldMin;
    float brickSize;
    uint maxDrawDistance;
    uint enableHiZ;
    uint64_t paletteAddr;
    uint64_t sunAddr;
    uint64_t giAddr;
    uint giEnabled;
    uint shadowsEnabled;
    uint debugEnabled;
    uint useBackface;
    uint useFrustum;
    uint virtualDim;
};

struct VoxelTaskPayload {
    uint brickIdx;
    uint faceCount;
};

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

    return ((bit & 1u) == 0u) ? (word & 0xFFFFu) : (word >> 16u);
}

int3 VoxelShapeMin(uint* shapes, uint state) {
    uint p = shapes[state * 2u];
    return int3(p & 0xFFu, (p >> 8u) & 0xFFu, (p >> 16u) & 0xFFu);
}

int3 VoxelShapeMax(uint* shapes, uint state) {
    uint p = shapes[state * 2u + 1u];
    return int3(p & 0xFFu, (p >> 8u) & 0xFFu, (p >> 16u) & 0xFFu);
}

float4 VoxelFaceUv(uint* uvs, uint state, uint f) {
    uint p = uvs[state * 6u + f];
    return float4(float(p & 0xFFu), float((p >> 8u) & 0xFFu),
                  float((p >> 16u) & 0xFFu), float((p >> 24u) & 0xFFu)) / 255.0;
}

bool VoxelFaceCovered(int3 vMin, int3 vMax, int3 nMin, int3 nMax, uint f) {
    uint ax = f >> 1u;
    if ((f & 1u) == 0u) {
        if ((ax == 0u && (vMax.x != 16 || nMin.x != 0)) ||
            (ax == 1u && (vMax.y != 16 || nMin.y != 0)) ||
            (ax == 2u && (vMax.z != 16 || nMin.z != 0)))
            return false;
    } else {
        if ((ax == 0u && (vMin.x != 0 || nMax.x != 16)) ||
            (ax == 1u && (vMin.y != 0 || nMax.y != 16)) ||
            (ax == 2u && (vMin.z != 0 || nMax.z != 16)))
            return false;
    }

    if (ax != 0u && (nMin.x > vMin.x || nMax.x < vMax.x))
        return false;
    if (ax != 1u && (nMin.y > vMin.y || nMax.y < vMax.y))
        return false;
    if (ax != 2u && (nMin.z > vMin.z || nMax.z < vMax.z))
        return false;
    return true;
}

void VoxelFaceQuad(uint x, uint y, uint z, uint f, int3 sMin, int3 sMax,
                   out float3 b, out float3 e1, out float3 e2) {
    float3 mn = float3(float(x), float(y), float(z)) + float3(sMin) / 16.0;
    float3 mx = float3(float(x), float(y), float(z)) + float3(sMax) / 16.0;
    float3 ex = mx - mn;
    if (f == 0u) {
        b = float3(mx.x, mn.y, mn.z); e1 = float3(0.0, ex.y, 0.0); e2 = float3(0.0, 0.0, ex.z);
    } else if (f == 1u) {
        b = float3(mn.x, mn.y, mn.z); e1 = float3(0.0, ex.y, 0.0); e2 = float3(0.0, 0.0, ex.z);
    } else if (f == 2u) {
        b = float3(mn.x, mx.y, mn.z); e1 = float3(ex.x, 0.0, 0.0); e2 = float3(0.0, 0.0, ex.z);
    } else if (f == 3u) {
        b = float3(mn.x, mn.y, mn.z); e1 = float3(ex.x, 0.0, 0.0); e2 = float3(0.0, 0.0, ex.z);
    } else if (f == 4u) {
        b = float3(mn.x, mn.y, mx.z); e1 = float3(ex.x, 0.0, 0.0); e2 = float3(0.0, ex.y, 0.0);
    } else {
        b = float3(mn.x, mn.y, mn.z); e1 = float3(ex.x, 0.0, 0.0); e2 = float3(0.0, ex.y, 0.0);
    }
}

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
