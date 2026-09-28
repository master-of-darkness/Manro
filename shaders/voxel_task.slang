// Task (amplification) shader: per-brick cull + face compaction.
// One task group per resident brick. Dirty/unseen bricks scan occupancy and
// store the FULL exposed-face list into the face cache (view-independent —
// never view-culled here); clean bricks just load the count and dispatch.
// Mesh fans out to ceil(faceCount/16) groups and backface-filters per frame.
// Dense-brick worst case ~1536 faces -> 96 mesh groups.
import voxel_common;

[[vk::push_constant]]
VoxelFrameRoot g_TaskRoot;

// Module-scope shared staging for face compaction (32 lanes cooperate).
// s_faceStaging layout: [0] = count, [1..2048] = packed faces.
groupshared uint s_faceStaging[2049];
groupshared uint s_hasContent;

VoxelFrameParams LoadFrame() {
    vk::BufferPointer<VoxelFrameParams> fp = vk::BufferPointer<VoxelFrameParams>(g_TaskRoot.frameAddr);
    VoxelFrameParams* p = fp.getPtr();
    return p[0];
}

bool BrickResidentAndNear(uint brickIdx, VoxelFrameParams frame) {
    // Residency + distance only. Frustum testing lives in TaskMain as an
    // 8-corner NDC test using the same mul() as the mesh shader, so culling
    // can never disagree with what rasterization would draw (analytic plane
    // extraction is one convention slip away from false culls).
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

// BISECT DEBUG: synthetic full top-plane faces, bypasses ALL brick-data
// BDA reads (occupancy/materials). If the full 16x16 top plane appears,
// task->mesh->raster->composite is proven good and the bug is in brick
// data reads (sparse upload / BDA / occupancy). Flip to false to revert.
static const bool kDebugSyntheticFaces = false;

[shader("amplification")]
[numthreads(32, 1, 1)]
void TaskMain(uint gidx : SV_GroupIndex, uint3 gid : SV_GroupID) {
    VoxelFrameParams frame = LoadFrame();
    // Dispatch covers the CPU-compacted front-to-back visible list exactly
    // (no empty groups); remap group -> resident brick ordinal here.
    // frame.brickCount stays the total for bounds checks below.
    vk::BufferPointer<uint> vis = vk::BufferPointer<uint>(g_TaskRoot.visibilityAddr);
    uint brickIdx = vis.getPtr()[gid.x];

    // DEBUG counter helper: debugAddr[0] = task runs, [1] = visible bricks,
    // [2] = total compacted faces. Skipped unless frame.debugEnabled.
    vk::BufferPointer<uint> dbg = vk::BufferPointer<uint>(g_TaskRoot.debugAddr);
    uint* debug = dbg.getPtr();
    bool wantDbg = frame.debugEnabled != 0u;
    if (gidx == 0 && wantDbg) {
        InterlockedAdd(debug[0], 1u);
    }

    // useFrustum=0 disables the residency + distance check here (the CPU
    // compact + sort that feeds this dispatch is gated by the same flag).
    // Dense append-only allocation keeps every slot < brickCount resident.
    if (frame.useFrustum != 0u && !BrickResidentAndNear(brickIdx, frame)) {
        if (gidx == 0 && wantDbg) {
            InterlockedAdd(debug[3], 1u); // culled
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

    // NOTE: no GPU frustum re-test here on purpose. The CPU already
    // compacted the dispatch to frustum-visible bricks (exact: same NDC test
    // against the same matrix), so a lane-cooperative re-test with its two
    // group barriers + shared atomics measured SLOWER than nothing when the
    // scene is mostly visible. Residency + distance above stay as cheap,
    // barrier-free belt-and-braces (and cover future sparse eviction).
    // Survived all culling: this brick will dispatch mesh groups.
    if (gidx == 0 && wantDbg) {
        InterlockedAdd(debug[1], 1u);
    }
    vk::BufferPointer<uint> fcache = vk::BufferPointer<uint>(g_TaskRoot.faceCacheAddr);
    uint* fcb = fcache.getPtr() + brickIdx * kVoxelFaceCacheStride;

    // Face-cache fast path (Minecraft rule: static bricks cost ~nothing).
    // needsScan is uniform — same header for all lanes.
    // bit1 = dirty (a GPU edit touched this brick), bit2 = cache valid.
    // The scan path consumes dirty after storing (see below), so an edited
    // brick rescans exactly the frames it needs — no permanent rescan.
    bool needsScan = kDebugSyntheticFaces ||
                     ((bhdr.flags & 4u) == 0u) || ((bhdr.flags & 2u) != 0u);
    uint faceCount = 0u;
    if (!needsScan) {
        faceCount = fcb[0];
    } else {
        if (kDebugSyntheticFaces) {
            // 16x16 top faces (+Y, f=2), checkerboard mats, stored straight
            // into the cache (mesh reads the cache, never the payload).
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
            // Empty-brick early-out: 128 occupancy words, 4 per lane.
            // Uniform exit after the barrier — all lanes observe s_hasContent.
            // Empty bricks cache count 0 so they stay on the fast path.
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
                // State-aware scan: every exposed face is stored, with NO
                // view-dependent culling. The cache must stay valid for all
                // viewpoints (backface rejection runs per-frame in mesh);
                // caching a culled list freezes one viewpoint's visibility.
                // mats[] holds vanilla protocol state ids; mcFlags (root)
                // carries opaque/cutout/skip/inner bits per state.
                // Emit rule per face of V against neighbor N:
                // - N skipped/air -> emit; N == V -> emit only for inner
                //   faces (leaves); opaque N -> cull; else emit.
                // Neighbors outside this brick resolve through the page table
                // into the adjacent resident brick (missing/unfilled reads as
                // air, so the face emits and the neighbor emits its own side
                // when IT fills + dirties us for a re-evaluation). This kills
                // the doubled hidden boundary faces that otherwise rasterize
                // inside solid rock on every frame.
                vk::BufferPointer<uint> mcfl = vk::BufferPointer<uint>(g_TaskRoot.mcFlagsAddr);
                uint* mcFlags = mcfl.getPtr();
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
                    uint vflags = (mat < kMcStateMax) ? mcFlags[mat] : 0u;
                    if ((vflags & 4u) != 0u)
                        continue; // skip-states never emit (flora/air)
                    for (uint f = 0u; f < 6u; ++f) {
                        int3 n = int3(x, y, z) + kFaceOffset[f];
                        uint nstate = 0u;
                        if (n.x >= 0 && n.x < 16 && n.y >= 0 && n.y < 16 && n.z >= 0 &&
                            n.z < 16) {
                            if (VoxelSolid(occupancy, uint(n.x), uint(n.y), uint(n.z)))
                                nstate = VoxelMaterial(mats, uint(n.x), uint(n.y), uint(n.z));
                        } else {
                            // Cross-brick neighbor voxel: world pos -> page
                            // slot -> resident ordinal -> occupancy/material.
                            float3 nwc = bhdr.origin +
                                         (float3(float(n.x), float(n.y), float(n.z)) + 0.5) *
                                             voxelSize;
                            int3 nbc = int3(floor((nwc - frame.worldMin) / frame.brickSize));
                            if (nbc.x >= 0 && nbc.y >= 0 && nbc.z >= 0 &&
                                uint(nbc.x) < vdim && uint(nbc.y) < vdim &&
                                uint(nbc.z) < vdim) {
                                uint nslot = uint(nbc.x) + uint(nbc.y) * vdim +
                                             uint(nbc.z) * vdim * vdim;
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
                        }
                        bool emit;
                        if (nstate == mat)
                            emit = (vflags & 8u) != 0u; // inner faces only
                        else
                            emit = (nstate >= kMcStateMax) ||
                                   ((mcFlags[nstate] & 1u) == 0u);
                        if (!emit)
                            continue;
                        uint slot;
                        InterlockedAdd(s_faceStaging[0], 1u, slot);
                        if (slot < 2048u) {
                            s_faceStaging[1u + slot] =
                                (iter & 0xFFFu) | ((f & 0x7u) << 12u) | ((mat & 0x7FFFu) << 15u);
                        }
                    }
                }
    GroupMemoryBarrierWithGroupSync();

                faceCount = min(s_faceStaging[0], 2048u);
                // Cooperative store into the face cache (~12 faces/lane
                // worst case) — replaces the old per-lane 8KB payload copy.
                if (gidx == 0) {
                    fcb[0] = faceCount;
                }
                for (uint i = gidx; i < faceCount; i += 32u) {
                    fcb[1u + i] = s_faceStaging[1u + i];
                }
            } else {
                // Air brick: cache count 0 so it stays on the fast path.
                if (gidx == 0) {
                    fcb[0] = 0u;
                }
                faceCount = 0u;
            }
            GroupMemoryBarrierWithGroupSync();
            // Task stores -> mesh reads within this draw.
            DeviceMemoryBarrier();
            if (gidx == 0) {
                // Consume dirty + mark cache valid. Header uint layout:
                // origin(0-2), pageIndex(3), flags(4).
                // Clearing dirty here (instead of never) stops edited bricks
                // rescanning forever — the fps death seen after ~50 edits.
                // Residual race: an edit landing in the same in-flight window
                // as this clear can lose one dirty flag; the next edit or
                // alloc re-marks it, so it self-heals. Strictly better than
                // permanent rescan.
                vk::BufferPointer<uint> hraw = vk::BufferPointer<uint>(g_TaskRoot.headerAddr);
                uint* hw = hraw.getPtr();
                InterlockedAnd(hw[brickIdx * 8u + 4u], ~2u);
                InterlockedOr(hw[brickIdx * 8u + 4u], 4u);
            }
        }
    }
    // Common dispatch: fast path and scan path rejoin here with a uniform
    // faceCount. Divergent DispatchMesh is undefined behavior — every lane
    // reaches this call with identical args.
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
