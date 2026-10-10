import voxel_common;

[[vk::push_constant]]
VoxelFrameRoot g_TaskRoot;

groupshared uint s_faceStaging[4096];
groupshared uint s_hasContent;
groupshared uint s_occ[128];
groupshared uint s_mats[2048];
groupshared uint s_brickIdx;
groupshared float3 s_origin;
groupshared uint s_hdrFlags;
groupshared uint s_brickCount;
groupshared uint s_debugOn;
groupshared float s_brickSize;
groupshared float3 s_worldMin;
groupshared uint s_virtualDim;

bool SolidLDS(uint x, uint y, uint z) {
    uint bit = x + y * 16u + z * 256u;
    return ((s_occ[bit >> 5u] >> (bit & 31u)) & 1u) != 0u;
}

uint MaterialLDS(uint x, uint y, uint z) {
    uint bit = x + y * 16u + z * 256u;
    uint word = s_mats[bit >> 1u];
    return ((bit & 1u) == 0u) ? (word & 0xFFFFu) : (word >> 16u);
}

static const bool kDebugSyntheticFaces = false;

[shader("amplification")]
[numthreads(32, 1, 1)]
void TaskMain(uint gidx : SV_GroupIndex, uint3 gid : SV_GroupID) {
    if (gidx == 0) {
        vk::BufferPointer<VoxelFrameParams> fp =
            vk::BufferPointer<VoxelFrameParams>(g_TaskRoot.frameAddr);
        VoxelFrameParams frame = fp.getPtr()[0];
        s_brickCount = frame.brickCount;
        s_debugOn = frame.debugEnabled;
        s_brickSize = frame.brickSize;
        s_worldMin = frame.worldMin;
        s_virtualDim = frame.virtualDim;

        vk::BufferPointer<uint> vis = vk::BufferPointer<uint>(g_TaskRoot.visibilityAddr);
        uint bidx = vis.getPtr()[gid.x];
        s_brickIdx = bidx;

        uint flags = 0u;
        float3 org = float3(0.0);
        if (bidx < frame.brickCount) {
            vk::BufferPointer<VoxelBrickHeader> headers =
                vk::BufferPointer<VoxelBrickHeader>(g_TaskRoot.headerAddr);
            VoxelBrickHeader hdr = headers.getPtr()[bidx];
            flags = hdr.flags;
            org = hdr.origin;
        }
        s_hdrFlags = flags;
        s_origin = org;
        s_faceStaging[0] = 0u;
        s_hasContent = 0u;
    }
    GroupMemoryBarrierWithGroupSync();

    uint brickIdx = s_brickIdx;
    bool wantDbg = s_debugOn != 0u;

    vk::BufferPointer<uint> dbg = vk::BufferPointer<uint>(g_TaskRoot.debugAddr);
    uint* debug = dbg.getPtr();
    if (gidx == 0 && wantDbg) {
        InterlockedAdd(debug[0], 1u);
    }

    if (brickIdx >= s_brickCount || (s_hdrFlags & 1u) == 0u) {
        if (gidx == 0 && wantDbg) {
            InterlockedAdd(debug[3], 1u);
        }
        return;
    }

    vk::BufferPointer<uint> bricks = vk::BufferPointer<uint>(g_TaskRoot.brickBufferAddr);
    uint* base = bricks.getPtr();
    uint* mats = base + brickIdx * 2176u;
    uint* occupancy = mats + 2048u;

    if (gidx == 0 && wantDbg) {
        InterlockedAdd(debug[1], 1u);
    }
    vk::BufferPointer<uint> fcache = vk::BufferPointer<uint>(g_TaskRoot.faceCacheAddr);
    uint* fcb = fcache.getPtr() + brickIdx * kVoxelFaceCacheStride;

    bool needsScan = kDebugSyntheticFaces ||
                     ((s_hdrFlags & 4u) == 0u) || ((s_hdrFlags & 2u) != 0u);
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
            for (uint w = gidx; w < 128u; w += 32u) {
                s_occ[w] = occupancy[w];
            }
            GroupMemoryBarrierWithGroupSync();

            uint localHas = 0u;
            for (uint w = gidx; w < 128u; w += 32u) {
                if (s_occ[w] != 0u) {
                    localHas = 1u;
                    break;
                }
            }
            if (localHas != 0u) {
                InterlockedOr(s_hasContent, 1u);
            }
            GroupMemoryBarrierWithGroupSync();
            if (s_hasContent != 0u) {
                for (uint w = gidx; w < 2048u; w += 32u) {
                    s_mats[w] = mats[w];
                }
                GroupMemoryBarrierWithGroupSync();

                vk::BufferPointer<uint> flagPtr = vk::BufferPointer<uint>(g_TaskRoot.blockFlagsAddr);
                uint* blockFlags = flagPtr.getPtr();
                vk::BufferPointer<uint> shapePtr = vk::BufferPointer<uint>(g_TaskRoot.shapeTableAddr);
                uint* blockShapes = shapePtr.getPtr();
                vk::BufferPointer<int> nbpt = vk::BufferPointer<int>(g_TaskRoot.pageTableAddr);
                int* pageTable = nbpt.getPtr();
                float voxelSize = s_brickSize / 16.0;
                uint vdim = s_virtualDim;
                float3 bOrigin = s_origin;
                float3 wMin = s_worldMin;

                uint localBuf[32];
                uint localN = 0u;

                for (uint iter = gidx; iter < 4096u; iter += 32u) {
                    uint x = iter & 15u;
                    uint y = (iter >> 4u) & 15u;
                    uint z = (iter >> 8u) & 15u;
                    if (!SolidLDS(x, y, z))
                        continue;
                    uint mat = MaterialLDS(x, y, z);
                    uint vflags = (mat < kBlockStateMax) ? blockFlags[mat] : 0u;
                    if ((vflags & 4u) != 0u)
                        continue;
                    for (uint f = 0u; f < 6u; ++f) {
                        int3 n = int3(x, y, z) + kFaceOffset[f];
                        uint nstate = 0u;
                        if (n.x >= 0 && n.x < 16 && n.y >= 0 && n.y < 16 && n.z >= 0 &&
                            n.z < 16) {
                            if (SolidLDS(uint(n.x), uint(n.y), uint(n.z)))
                                nstate = MaterialLDS(uint(n.x), uint(n.y), uint(n.z));
                        } else {
                            float3 nwc = bOrigin +
                                         (float3(float(n.x), float(n.y), float(n.z)) + 0.5) *
                                             voxelSize;
                            int3 nbc = int3(floor((nwc - wMin) / s_brickSize));
                            int vd = int(vdim);
                            int3 wrapped = nbc % vd;
                            if (wrapped.x < 0) wrapped.x += vd;
                            if (wrapped.y < 0) wrapped.y += vd;
                            if (wrapped.z < 0) wrapped.z += vd;
                            uint nslot = uint(wrapped.x) + uint(wrapped.y) * vdim +
                                         uint(wrapped.z) * vdim * vdim;
                            int rnb = pageTable[nslot];
                            if (rnb >= 0) {
                                uint* nnbase = base + uint(rnb) * 2176u;
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
                                if ((nflags & 1u) == 0u) {
                                    emit = true;
                                } else {
                                    uint vs = min(mat, kBlockStateMax - 1u);
                                    int3 vMin = VoxelShapeMin(blockShapes, vs);
                                    int3 vMax = VoxelShapeMax(blockShapes, vs);
                                    int3 nMin = VoxelShapeMin(blockShapes, nstate);
                                    int3 nMax = VoxelShapeMax(blockShapes, nstate);
                                    emit = !VoxelFaceCovered(vMin, vMax, nMin, nMax, f);
                                }
                            }
                        }
                        if (!emit)
                            continue;
                        localBuf[localN++] =
                            (iter & 0xFFFu) | ((f & 0x7u) << 12u) | ((mat & 0x7FFFu) << 15u);
                        if (localN == 32u) {
                            uint slot;
                            InterlockedAdd(s_faceStaging[0], 32u, slot);
                            for (uint k = 0u; k < 32u; ++k) {
                                if (slot + k < kVoxelFaceCacheCap) {
                                    s_faceStaging[1u + slot + k] = localBuf[k];
                                }
                            }
                            localN = 0u;
                        }
                    }
                }
                if (localN != 0u) {
                    uint slot;
                    InterlockedAdd(s_faceStaging[0], localN, slot);
                    for (uint k = 0u; k < localN; ++k) {
                        if (slot + k < kVoxelFaceCacheCap) {
                            s_faceStaging[1u + slot + k] = localBuf[k];
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
                uint wi = brickIdx * 8u + 4u;
                hw[wi] = (hw[wi] & ~2u) | 4u;
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
