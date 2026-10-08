import voxel_common;

[[vk::push_constant]]
VoxelFrameRoot g_TaskRoot;

groupshared uint s_faceStaging[4096];
groupshared uint s_hasContent;

VoxelFrameParams LoadFrame() {
    vk::BufferPointer<VoxelFrameParams> fp = vk::BufferPointer<VoxelFrameParams>(g_TaskRoot.frameAddr);
    VoxelFrameParams* p = fp.getPtr();
    return p[0];
}

bool BrickResidentAndNear(uint brickIdx, VoxelFrameParams frame) {

    if (brickIdx >= frame.brickCount)
        return false;

    vk::BufferPointer<VoxelBrickHeader> headers = vk::BufferPointer<VoxelBrickHeader>(g_TaskRoot.headerAddr);
    VoxelBrickHeader* h = headers.getPtr();
    VoxelBrickHeader hdr = h[brickIdx];
    if ((hdr.flags & 1u) == 0u)
        return false;

    float3 center = hdr.origin + float3(frame.brickSize * 0.5);
    float radius = length(float3(frame.brickSize * 0.5));

    float3 toCenter = center - frame.cameraPos;
    float maxDist = float(frame.maxDrawDistance) + radius;
    if (dot(toCenter, toCenter) > maxDist * maxDist)
        return false;

    return true;
}

static const bool kDebugSyntheticFaces = false;

[shader("amplification")]
[numthreads(32, 1, 1)]
void TaskMain(uint gidx : SV_GroupIndex, uint3 gid : SV_GroupID) {
    VoxelFrameParams frame = LoadFrame();

    vk::BufferPointer<uint> vis = vk::BufferPointer<uint>(g_TaskRoot.visibilityAddr);
    uint brickIdx = vis.getPtr()[gid.x];

    vk::BufferPointer<uint> dbg = vk::BufferPointer<uint>(g_TaskRoot.debugAddr);
    uint* debug = dbg.getPtr();
    bool wantDbg = frame.debugEnabled != 0u;
    if (gidx == 0 && wantDbg) {
        InterlockedAdd(debug[0], 1u);
    }

    if (frame.useFrustum != 0u && !BrickResidentAndNear(brickIdx, frame)) {
        if (gidx == 0 && wantDbg) {
            InterlockedAdd(debug[3], 1u);
        }
        return;
    }

    vk::BufferPointer<uint> bricks = vk::BufferPointer<uint>(g_TaskRoot.brickBufferAddr);
    uint* base = bricks.getPtr();
    uint* mats = base + brickIdx * 2176u;
    uint* occupancy = mats + 2048u;

    vk::BufferPointer<VoxelBrickHeader> bheaders =
        vk::BufferPointer<VoxelBrickHeader>(g_TaskRoot.headerAddr);
    VoxelBrickHeader bhdr = bheaders.getPtr()[brickIdx];

    if (gidx == 0 && wantDbg) {
        InterlockedAdd(debug[1], 1u);
    }
    vk::BufferPointer<uint> fcache = vk::BufferPointer<uint>(g_TaskRoot.faceCacheAddr);
    uint* fcb = fcache.getPtr() + brickIdx * kVoxelFaceCacheStride;

    bool needsScan = kDebugSyntheticFaces ||
                     ((bhdr.flags & 4u) == 0u) || ((bhdr.flags & 2u) != 0u);
    uint faceCount = 0u;
    if (!needsScan) {
        faceCount = fcb[0];
    } else {
        if (kDebugSyntheticFaces) {

            for (uint i = gidx; i < 256u; i += 32u) {
                uint x = i & 15u;
                uint z = (i >> 4u) & 15u;
                uint iter = x + 7u * 16u + z * 256u;
                uint mat = (x + z) & 1u;
                fcb[1u + i] = (iter & 0xFFFu) | (2u << 12u) | ((mat & 0x1FFu) << 15u);
            }
            if (gidx == 0) {
                fcb[0] = 256u;
            }
            faceCount = 256u;
        } else {

            if (gidx == 0) {
                s_faceStaging[0] = 0u;
                s_hasContent = 0u;
            }
            GroupMemoryBarrierWithGroupSync();

            for (uint w = gidx; w < 128u; w += 32u) {
                if (occupancy[w] != 0u) {
                    InterlockedOr(s_hasContent, 1u);
                }
            }
            GroupMemoryBarrierWithGroupSync();
            if (s_hasContent != 0u) {

                vk::BufferPointer<uint> flagPtr = vk::BufferPointer<uint>(g_TaskRoot.blockFlagsAddr);
                uint* blockFlags = flagPtr.getPtr();
                vk::BufferPointer<uint> shapePtr = vk::BufferPointer<uint>(g_TaskRoot.shapeTableAddr);
                uint* blockShapes = shapePtr.getPtr();
                vk::BufferPointer<int> nbpt = vk::BufferPointer<int>(g_TaskRoot.pageTableAddr);
                int* pageTable = nbpt.getPtr();
                vk::BufferPointer<uint> nbr = vk::BufferPointer<uint>(g_TaskRoot.brickBufferAddr);
                uint* nstore = nbr.getPtr();
                float voxelSize = frame.brickSize / 16.0;
                uint vdim = frame.virtualDim;
                for (uint iter = gidx; iter < 4096u; iter += 32u) {
                    uint x = iter & 15u;
                    uint y = (iter >> 4u) & 15u;
                    uint z = (iter >> 8u) & 15u;
                    if (!VoxelSolid(occupancy, x, y, z))
                        continue;
                    uint mat = VoxelMaterial(mats, x, y, z);
                    uint vflags = (mat < kBlockStateMax) ? blockFlags[mat] : 0u;
                    if ((vflags & 4u) != 0u)
                        continue;
                    for (uint f = 0u; f < 6u; ++f) {
                        int3 n = int3(x, y, z) + kFaceOffset[f];
                        uint nstate = 0u;
                        if (n.x >= 0 && n.x < 16 && n.y >= 0 && n.y < 16 && n.z >= 0 &&
                            n.z < 16) {
                            if (VoxelSolid(occupancy, uint(n.x), uint(n.y), uint(n.z)))
                                nstate = VoxelMaterial(mats, uint(n.x), uint(n.y), uint(n.z));
                        } else {

                            float3 nwc = bhdr.origin +
                                         (float3(float(n.x), float(n.y), float(n.z)) + 0.5) *
                                             voxelSize;
                            int3 nbc = int3(floor((nwc - frame.worldMin) / frame.brickSize));

                            int3 wrapped =
                                ((nbc % int(vdim)) + int(vdim)) % int(vdim);
                            uint nslot = uint(wrapped.x) + uint(wrapped.y) * vdim +
                                         uint(wrapped.z) * vdim * vdim;
                            int rnb = pageTable[nslot];
                            if (rnb >= 0) {
                                uint* nnbase = nstore + uint(rnb) * 2176u;
                                uint* nnmats = nnbase;
                                uint* nnocc = nnbase + 2048u;
                                uint nx = uint(n.x) & 15u;
                                uint ny = uint(n.y) & 15u;
                                uint nz = uint(n.z) & 15u;
                                if (VoxelSolid(nnocc, nx, ny, nz))
                                    nstate = VoxelMaterial(nnmats, nx, ny, nz);
                            }
                        }

                        bool emit;
                        if (nstate >= kBlockStateMax) {
                            emit = true;
                        } else {
                            uint nflags = blockFlags[nstate];
                            if ((nflags & 4u) != 0u) {
                                emit = true;
                            } else if (nstate == mat) {
                                if ((vflags & 8u) != 0u) {
                                    emit = true;
                                } else {

                                    uint vs = min(mat, kBlockStateMax - 1u);
                                    int3 vMin = VoxelShapeMin(blockShapes, vs);
                                    int3 vMax = VoxelShapeMax(blockShapes, vs);
                                    int3 nMin = VoxelShapeMin(blockShapes, nstate);
                                    int3 nMax = VoxelShapeMax(blockShapes, nstate);
                                    emit = !VoxelFaceCovered(vMin, vMax, nMin, nMax, f);
                                }
                            } else {
                                emit = ((nflags & 1u) == 0u);
                            }
                        }
                        if (!emit)
                            continue;
                        uint slot;
                        InterlockedAdd(s_faceStaging[0], 1u, slot);
                        if (slot < kVoxelFaceCacheCap) {
                            s_faceStaging[1u + slot] =
                                (iter & 0xFFFu) | ((f & 0x7u) << 12u) | ((mat & 0x7FFFu) << 15u);
                        }
                    }
                }
    GroupMemoryBarrierWithGroupSync();

                faceCount = min(s_faceStaging[0], kVoxelFaceCacheCap);

                if (gidx == 0) {
                    fcb[0] = faceCount;
                }
                for (uint i = gidx; i < faceCount; i += 32u) {
                    fcb[1u + i] = s_faceStaging[1u + i];
                }
            } else {

                if (gidx == 0) {
                    fcb[0] = 0u;
                }
                faceCount = 0u;
            }
            GroupMemoryBarrierWithGroupSync();

            DeviceMemoryBarrier();
            if (gidx == 0) {

                vk::BufferPointer<uint> hraw = vk::BufferPointer<uint>(g_TaskRoot.headerAddr);
                uint* hw = hraw.getPtr();
                InterlockedAnd(hw[brickIdx * 8u + 4u], ~2u);
                InterlockedOr(hw[brickIdx * 8u + 4u], 4u);
            }
        }
    }

    if (faceCount == 0u)
        return;
    if (gidx == 0 && wantDbg) {
        InterlockedAdd(debug[2], faceCount);
    }
    VoxelTaskPayload p;
    p.brickIdx = brickIdx;
    p.faceCount = faceCount;
    uint meshGroups = (faceCount + kVoxelFacesPerMeshGroup - 1u) / kVoxelFacesPerMeshGroup;
    DispatchMesh(meshGroups, 1, 1, p);
}
