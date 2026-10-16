import voxel_common;

[[vk::push_constant]]
VoxelFrameRoot g_TaskRoot;

groupshared uint s_swap[4096];
groupshared uint s_partCount[3];
groupshared uint s_cutoutPass;
groupshared uint s_hasContent;
groupshared uint s_occ[128];
groupshared uint s_mats[2048];
groupshared uint s_light[1024];
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

uint FlatLightLDS(uint x, uint y, uint z) {
    uint bit = x + y * 16u + z * 256u;
    return (s_light[bit >> 2u] >> ((bit & 3u) * 8u)) & 0xFFu;
}

bool OpaqueFullLDS(uint x, uint y, uint z, uint* flags, uint* shapes) {
    uint bit = x + y * 16u + z * 256u;
    uint word = s_mats[bit >> 1u];
    uint state = ((bit & 1u) == 0u) ? (word & 0xFFFFu) : (word >> 16u);
    if (state >= kBlockStateMax)
        return false;
    if ((flags[state] & 1u) == 0u)
        return false;
    return shapes[state * 2u] == 0u &&
           shapes[state * 2u + 1u] == (16u | (16u << 8u) | (16u << 16u));
}




uint FlatLightAt(int gx, int gy, int gz, int3 brickAbs,
                 uint* bricksEx, int* pages, int minBX, int minBZ, uint vdim) {
    int3 rel = int3(gx, gy, gz) - brickAbs;
    if (rel.x >= 0 && rel.x < 16 && rel.y >= 0 && rel.y < 16 && rel.z >= 0 && rel.z < 16)
        return FlatLightLDS(uint(rel.x), uint(rel.y), uint(rel.z));
    return VoxelSampleFlatLight(gx, gy, gz, bricksEx, pages, minBX, minBZ, vdim);
}

bool OpaqueFullAt(int gx, int gy, int gz, int3 brickAbs,
                  uint* bricksEx, int* pages, uint* flags, uint* shapes,
                  int minBX, int minBZ, uint vdim) {
    int3 rel = int3(gx, gy, gz) - brickAbs;
    if (rel.x >= 0 && rel.x < 16 && rel.y >= 0 && rel.y < 16 && rel.z >= 0 && rel.z < 16)
        return OpaqueFullLDS(uint(rel.x), uint(rel.y), uint(rel.z), flags, shapes);
    if (gy < -64 || gy >= 320)
        return gy < -64;
    int sx = gx >> 4;
    int sy = gy >> 4;
    int sz = gz >> 4;
    int D = int(vdim);
    int bx = (sx - minBX) % D; if (bx < 0) bx += D;
    int by = (sy + 4) % D; if (by < 0) by += D;
    int bz = (sz - minBZ) % D; if (bz < 0) bz += D;
    uint slot = uint(bx) + uint(by) * vdim + uint(bz) * vdim * vdim;
    int brick = pages[slot];
    if (brick < 0)
        return true; 
    uint lx = uint(gx & 15);
    uint ly = uint(gy & 15);
    uint lz = uint(gz & 15);
    uint vi = lx + ly * 16u + lz * 256u;
    uint* mats = bricksEx + uint(brick) * 3200u;
    uint word = mats[vi >> 1u];
    uint state = ((vi & 1u) == 0u) ? (word & 0xFFFFu) : (word >> 16u);
    if (state >= kBlockStateMax)
        return false;
    if ((flags[state] & 1u) == 0u)
        return false;
    return shapes[state * 2u] == 0u &&
           shapes[state * 2u + 1u] == (16u | (16u << 8u) | (16u << 16u));
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
        s_cutoutPass = frame.cutoutPass;

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
        s_partCount[0] = 0u;
        s_partCount[1] = 0u;
        s_partCount[2] = 0u;
        s_hasContent = 0u;
    }
    GroupMemoryBarrierWithGroupSync();

    uint brickIdx = s_brickIdx;
    uint cutoutPass = s_cutoutPass;
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
    uint* mats = base + brickIdx * 3200u;
    uint* occupancy = mats + 2048u;

    if (gidx == 0 && wantDbg) {
        InterlockedAdd(debug[1], 1u);
    }
    vk::BufferPointer<uint> fcache = vk::BufferPointer<uint>(g_TaskRoot.faceCacheAddr);
    uint* fcb = fcache.getPtr() + brickIdx * kVoxelFaceCacheStride;

    bool needsScan = kDebugSyntheticFaces ||
                     ((s_hdrFlags & 4u) == 0u) || ((s_hdrFlags & 2u) != 0u);
    uint faceStart = 0u;
    uint faceCount = 0u;
    if (!needsScan) {
        uint nTotal = fcb[0];
        uint nOp = fcb[kVoxelFaceCacheOpaqueIdx];
        if (nTotal > kVoxelFaceCacheCap)
            nTotal = kVoxelFaceCacheCap;
        if (nOp > nTotal)
            nOp = nTotal;
        if (cutoutPass != 0u) {
            faceStart = nOp;
            faceCount = (nTotal > nOp) ? (nTotal - nOp) : 0u;
        } else {
            faceStart = 0u;
            faceCount = nOp;
        }
    } else {
        if (kDebugSyntheticFaces) {
            for (uint i = gidx; i < 256u; i += 32u) {
                uint x = i & 15u;
                uint z = (i >> 4u) & 15u;
                uint iter = x + 7u * 16u + z * 256u;
                uint mat = (x + z) & 1u;
                fcb[1u + i * 4u] = (iter & 0xFFFu) | (2u << 12u) | ((mat & 0x1FFu) << 15u);
                fcb[1u + i * 4u + 1u] = 0x3FFu | (0x3FFu << 10u) | (0x3FFu << 20u);
                fcb[1u + i * 4u + 2u] = 0x3FFu; 
                fcb[1u + i * 4u + 3u] = 0u; 
            }
            if (gidx == 0) {
                fcb[0] = 256u;
                fcb[kVoxelFaceCacheOpaqueIdx] = 256u;
            }
            if (cutoutPass != 0u) {
                faceStart = 256u;
                faceCount = 0u;
            } else {
                faceStart = 0u;
                faceCount = 256u;
            }
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
                uint* lightSrc = mats + 2176u;
                for (uint w = gidx; w < 1024u; w += 32u) {
                    s_light[w] = lightSrc[w];
                }
                GroupMemoryBarrierWithGroupSync();

                vk::BufferPointer<uint> flagPtr = vk::BufferPointer<uint>(g_TaskRoot.blockFlagsAddr);
                uint* blockFlags = flagPtr.getPtr();
                vk::BufferPointer<uint> shapePtr = vk::BufferPointer<uint>(g_TaskRoot.shapeTableAddr);
                uint* blockShapes = shapePtr.getPtr();
                vk::BufferPointer<uint> boxPtr = vk::BufferPointer<uint>(g_TaskRoot.boxTableAddr);
                uint* blockBoxes = boxPtr.getPtr();
                vk::BufferPointer<int> nbpt = vk::BufferPointer<int>(g_TaskRoot.pageTableAddr);
                int* pageTable = nbpt.getPtr();
                float voxelSize = s_brickSize / 16.0;
                uint vdim = s_virtualDim;
                float3 bOrigin = s_origin;
                float3 wMin = s_worldMin;

                uint localPack[32];
                uint localBox[32];
                uint localCut[32];
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
                                uint* nnbase = base + uint(rnb) * 3200u;
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
                        uint packed =
                            (iter & 0xFFFu) | ((f & 0x7u) << 12u) | ((mat & 0x7FFFu) << 15u);
                        uint msBox = min(mat, kBlockStateMax - 1u);
                        bool isMulti = ((vflags & 16u) != 0u) &&
                                       (VoxelBoxCount(blockBoxes, msBox) > 1u);
                        if (!isMulti) {
                        if (((vflags & 32u) != 0u) && (f == 3u) && (nstate != mat)) {
                            emit = true;
                        } else if (nstate >= kBlockStateMax) {
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
                                    bool bothFull =
                                        all(vMin == int3(0, 0, 0)) &&
                                        all(vMax == int3(16, 16, 16)) &&
                                        all(nMin == int3(0, 0, 0)) &&
                                        all(nMax == int3(16, 16, 16));
                                    if ((vflags & 32u) != 0u && bothFull) {
                                        emit = ((f & 1u) == 0u);
                                    } else {
                                        emit = !VoxelFaceCovered(vMin, vMax, nMin, nMax, f);
                                    }
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
                        localPack[localN] = packed;
                        localBox[localN] = 0u;
                        localCut[localN] = ((vflags & 2u) != 0u) ? 1u : 0u;
                        localN++;
                        if (localN == 32u) {
                            uint nOp = 0u;
                            for (uint kc0 = 0u; kc0 < 32u; ++kc0)
                                nOp += (localCut[kc0] == 0u) ? 1u : 0u;
                            uint oldTotal = 0u;
                            InterlockedAdd(s_partCount[0], 32u, oldTotal);
                            uint keep = 32u;
                            if (oldTotal >= kVoxelFaceCacheCap)
                                keep = 0u;
                            else if (oldTotal + 32u > kVoxelFaceCacheCap)
                                keep = kVoxelFaceCacheCap - oldTotal;
                            nOp = 0u;
                            for (uint kc1 = 0u; kc1 < keep; ++kc1)
                                nOp += (localCut[kc1] == 0u) ? 1u : 0u;
                            uint nCut = keep - nOp;
                            uint oldNop = 0u;
                            uint oldNcut = 0u;
                            InterlockedAdd(s_partCount[1], nOp, oldNop);
                            InterlockedAdd(s_partCount[2], nCut, oldNcut);
                            uint ko = 0u;
                            uint kc = 0u;
                            for (uint kf = 0u; kf < keep; ++kf) {
                                if (localCut[kf] == 0u) {
                                    uint widx = oldNop + ko; ko++;
                                    if (widx < kVoxelFaceCacheCap) {
                                        fcb[1u + widx * 4u] = localPack[kf];
                                        fcb[1u + widx * 4u + 3u] = localBox[kf];
                                    }
                                } else {
                                    uint widx = (kVoxelFaceCacheCap - 1u) - (oldNcut + kc); kc++;
                                    if (widx < kVoxelFaceCacheCap) {
                                        fcb[1u + widx * 4u] = localPack[kf];
                                        fcb[1u + widx * 4u + 3u] = localBox[kf];
                                    }
                                }
                            }
                            localN = 0u;
                        }
                        } else {




                            bool nSkip = true;
                            int3 nMin = int3(0, 0, 0);
                            int3 nMax = int3(16, 16, 16);
                            if (nstate < kBlockStateMax) {
                                uint nflags = blockFlags[nstate];
                                if ((nflags & 4u) == 0u) {
                                    nSkip = false;
                                    nMin = VoxelShapeMin(blockShapes, nstate);
                                    nMax = VoxelShapeMax(blockShapes, nstate);
                                }
                            }
                            uint nBox = VoxelBoxCount(blockBoxes, msBox);
                            for (uint bi = 0u; bi < nBox; ++bi) {
                                if (VoxelBoxFaceHidden(blockBoxes, msBox, bi, f))
                                    continue;
                                if (!nSkip) {
                                    int3 bMin = VoxelBoxMin(blockBoxes, msBox, bi);
                                    int3 bMax = VoxelBoxMax(blockBoxes, msBox, bi);
                                    if (VoxelFaceCovered(bMin, bMax, nMin, nMax, f))
                                        continue;
                                }
                                localPack[localN] = packed;
                                localBox[localN] = bi + 1u;
                                localCut[localN] = ((vflags & 2u) != 0u) ? 1u : 0u;
                                localN++;
                                if (localN == 32u) {
                                    uint nOp = 0u;
                                    for (uint kc0 = 0u; kc0 < 32u; ++kc0)
                                        nOp += (localCut[kc0] == 0u) ? 1u : 0u;
                                    uint oldTotal = 0u;
                                    InterlockedAdd(s_partCount[0], 32u, oldTotal);
                                    uint keep = 32u;
                                    if (oldTotal >= kVoxelFaceCacheCap)
                                        keep = 0u;
                                    else if (oldTotal + 32u > kVoxelFaceCacheCap)
                                        keep = kVoxelFaceCacheCap - oldTotal;
                                    nOp = 0u;
                                    for (uint kc1 = 0u; kc1 < keep; ++kc1)
                                        nOp += (localCut[kc1] == 0u) ? 1u : 0u;
                                    uint nCut = keep - nOp;
                                    uint oldNop = 0u;
                                    uint oldNcut = 0u;
                                    InterlockedAdd(s_partCount[1], nOp, oldNop);
                                    InterlockedAdd(s_partCount[2], nCut, oldNcut);
                                    uint ko = 0u;
                                    uint kc = 0u;
                                    for (uint kf = 0u; kf < keep; ++kf) {
                                        if (localCut[kf] == 0u) {
                                            uint widx = oldNop + ko; ko++;
                                            if (widx < kVoxelFaceCacheCap) {
                                                fcb[1u + widx * 4u] = localPack[kf];
                                                fcb[1u + widx * 4u + 3u] = localBox[kf];
                                            }
                                        } else {
                                            uint widx = (kVoxelFaceCacheCap - 1u) - (oldNcut + kc); kc++;
                                            if (widx < kVoxelFaceCacheCap) {
                                                fcb[1u + widx * 4u] = localPack[kf];
                                                fcb[1u + widx * 4u + 3u] = localBox[kf];
                                            }
                                        }
                                    }
                                    localN = 0u;
                                }
                            }
                        }
                    }
                }
                if (localN != 0u) {
                    uint tailN = localN;
                    uint nOp = 0u;
                    for (uint k = 0u; k < tailN; ++k)
                        nOp += (localCut[k] == 0u) ? 1u : 0u;
                    uint oldTotal = 0u;
                    InterlockedAdd(s_partCount[0], tailN, oldTotal);
                    uint keep = tailN;
                    if (oldTotal >= kVoxelFaceCacheCap)
                        keep = 0u;
                    else if (oldTotal + tailN > kVoxelFaceCacheCap)
                        keep = kVoxelFaceCacheCap - oldTotal;
                    nOp = 0u;
                    for (uint k = 0u; k < keep; ++k)
                        nOp += (localCut[k] == 0u) ? 1u : 0u;
                    uint nCut = keep - nOp;
                    uint oldNop = 0u;
                    uint oldNcut = 0u;
                    InterlockedAdd(s_partCount[1], nOp, oldNop);
                    InterlockedAdd(s_partCount[2], nCut, oldNcut);
                    uint ko = 0u;
                    uint kc = 0u;
                    for (uint k = 0u; k < keep; ++k) {
                        if (localCut[k] == 0u) {
                            uint widx = oldNop + ko; ko++;
                            if (widx < kVoxelFaceCacheCap) {
                                fcb[1u + widx * 4u] = localPack[k];
                                fcb[1u + widx * 4u + 3u] = localBox[k];
                            }
                        } else {
                            uint widx = (kVoxelFaceCacheCap - 1u) - (oldNcut + kc); kc++;
                            if (widx < kVoxelFaceCacheCap) {
                                fcb[1u + widx * 4u] = localPack[k];
                                fcb[1u + widx * 4u + 3u] = localBox[k];
                            }
                        }
                    }
                }
                GroupMemoryBarrierWithGroupSync();

                uint nOpTotal = min(s_partCount[1], kVoxelFaceCacheCap);
                uint nCutTotal = min(s_partCount[2], kVoxelFaceCacheCap - nOpTotal);
                uint nTotal = nOpTotal + nCutTotal;

                if (nCutTotal != 0u) {
                    for (uint i = gidx; i < nCutTotal; i += 32u) {
                        uint src = (kVoxelFaceCacheCap - 1u) - i;
                        s_swap[i] = fcb[1u + src * 4u];
                    }
                    GroupMemoryBarrierWithGroupSync();
                    for (uint i = gidx; i < nCutTotal; i += 32u) {
                        fcb[1u + (nOpTotal + i) * 4u] = s_swap[i];
                    }
                    GroupMemoryBarrierWithGroupSync();
                    for (uint i = gidx; i < nCutTotal; i += 32u) {
                        uint src = (kVoxelFaceCacheCap - 1u) - i;
                        s_swap[i] = fcb[1u + src * 4u + 3u];
                    }
                    GroupMemoryBarrierWithGroupSync();
                    for (uint i = gidx; i < nCutTotal; i += 32u) {
                        fcb[1u + (nOpTotal + i) * 4u + 3u] = s_swap[i];
                    }
                    GroupMemoryBarrierWithGroupSync();
                }

                if (gidx == 0) {
                    fcb[0] = nTotal;
                    fcb[kVoxelFaceCacheOpaqueIdx] = nOpTotal;
                }
                GroupMemoryBarrierWithGroupSync();

                if (cutoutPass != 0u) {
                    faceStart = nOpTotal;
                    faceCount = nCutTotal;
                } else {
                    faceStart = 0u;
                    faceCount = nOpTotal;
                }




                {
                    vk::BufferPointer<uint> brickPtr =
                        vk::BufferPointer<uint>(g_TaskRoot.brickBufferAddr);
                    vk::BufferPointer<int> pagePtr =
                        vk::BufferPointer<int>(g_TaskRoot.pageTableAddr);
                    uint* bricksEx = brickPtr.getPtr();
                    int* pages = pagePtr.getPtr();
                    int minBX = int(s_worldMin.x / 16.0);
                    int minBZ = int(s_worldMin.z / 16.0);
                    uint vdim = s_virtualDim;
                    float voxelSize = s_brickSize / 16.0;
                    int3 brickAbs = int3(s_origin / voxelSize);
                    for (uint i = gidx; i < nTotal; i += 32u) {
                        uint packed = fcb[1u + i * 4u];
                        fcb[1u + i * 4u] = packed;
                        uint iter = packed & 0xFFFu;
                        uint f = (packed >> 12u) & 0x7u;
                        int3 vPos = brickAbs + int3(int(iter & 15u),
                                                    int((iter >> 4u) & 15u),
                                                    int((iter >> 8u) & 15u));
                        int3 nOff = kFaceOffset[f];
                        int3 uVec;
                        int3 vVec;
                        if (f == 0u || f == 1u) {
                            uVec = int3(0, 1, 0);
                            vVec = int3(0, 0, 1);
                        } else if (f == 2u || f == 3u) {
                            uVec = int3(1, 0, 0);
                            vVec = int3(0, 0, 1);
                        } else {
                            uVec = int3(1, 0, 0);
                            vVec = int3(0, 1, 0);
                        }
                        int3 facePos = vPos + nOff;
                        uint fmat = min((packed >> 15u) & 0x7FFFu, kBlockStateMax - 1u);
                        int3 lightBase = facePos;
                        if (f == 3u && ((blockFlags[fmat] & 32u) != 0u) &&
                            OpaqueFullAt(facePos.x, facePos.y, facePos.z, brickAbs,
                                         bricksEx, pages, blockFlags, blockShapes,
                                         minBX, minBZ, vdim)) {
                            lightBase = vPos;
                        }
                        uint faceLP = FlatLightAt(lightBase.x, lightBase.y, lightBase.z,
                                                  brickAbs, bricksEx, pages, minBX, minBZ,
                                                  vdim);
                        uint faceSky = (faceLP >> 4u) & 0xFu;
                        uint faceBlk = faceLP & 0xFu;
                        uint w1 = 0u;
                        uint v3sky = 0u, v3blk = 0u, v3ao = 0u;
                        for (uint ci = 0u; ci < 4u; ++ci) {
                            int du = (ci == 0u || ci == 3u) ? -1 : 1;
                            int dv = (ci == 0u || ci == 1u) ? -1 : 1;
                            int3 s1Pos = lightBase + uVec * du;
                            int3 s2Pos = lightBase + vVec * dv;
                            int3 cPos = lightBase + uVec * du + vVec * dv;
                            uint o1 = OpaqueFullAt(s1Pos.x, s1Pos.y, s1Pos.z, brickAbs,
                                                   bricksEx, pages, blockFlags, blockShapes,
                                                   minBX, minBZ, vdim) ? 1u : 0u;
                            uint o2 = OpaqueFullAt(s2Pos.x, s2Pos.y, s2Pos.z, brickAbs,
                                                   bricksEx, pages, blockFlags, blockShapes,
                                                   minBX, minBZ, vdim) ? 1u : 0u;
                            uint oc = OpaqueFullAt(cPos.x, cPos.y, cPos.z, brickAbs,
                                                   bricksEx, pages, blockFlags, blockShapes,
                                                   minBX, minBZ, vdim) ? 1u : 0u;
                            uint ao = ((o1 != 0u && o2 != 0u)) ? 0u : (3u - (o1 + o2 + oc));
                            uint sky;
                            uint blk;
                            if (o1 != 0u && o2 != 0u) {
                                sky = faceSky;
                                blk = faceBlk;
                            } else {
                                uint s1LP = FlatLightAt(s1Pos.x, s1Pos.y, s1Pos.z,
                                                        brickAbs, bricksEx, pages, minBX,
                                                        minBZ, vdim);
                                uint s2LP = FlatLightAt(s2Pos.x, s2Pos.y, s2Pos.z,
                                                        brickAbs, bricksEx, pages, minBX,
                                                        minBZ, vdim);
                                uint cLP = FlatLightAt(cPos.x, cPos.y, cPos.z,
                                                       brickAbs, bricksEx, pages, minBX,
                                                       minBZ, vdim);
                                sky = min((faceSky + ((s1LP >> 4u) & 0xFu) +
                                           ((s2LP >> 4u) & 0xFu) + ((cLP >> 4u) & 0xFu) +
                                           2u) >> 2u, 15u);
                                blk = min((faceBlk + (s1LP & 0xFu) + (s2LP & 0xFu) +
                                           (cLP & 0xFu) + 2u) >> 2u, 15u);
                            }
                            uint bits = (sky & 0xFu) | ((blk & 0xFu) << 4u) | ((ao & 0x3u) << 8u);
                            if (ci < 3u) {
                                w1 |= bits << (ci * 10u);
                            } else {
                                v3sky = sky & 0xFu;
                                v3blk = blk & 0xFu;
                                v3ao = ao & 0x3u;
                            }
                        }
                        fcb[1u + i * 4u + 1u] = w1;
                        fcb[1u + i * 4u + 2u] = v3sky | (v3blk << 4u) | (v3ao << 8u);
                    }
                }
            } else {
                if (gidx == 0) {
                    fcb[0] = 0u;
                    fcb[kVoxelFaceCacheOpaqueIdx] = 0u;
                }
                faceStart = 0u;
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
    p.faceStart = faceStart;
    p.faceCount = faceCount;
    uint meshGroups = (faceCount + kVoxelFacesPerMeshGroup - 1u) / kVoxelFacesPerMeshGroup;
    DispatchMesh(meshGroups, 1, 1, p);
}
