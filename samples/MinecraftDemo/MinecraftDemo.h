#pragma once

#include <Manro/Interfaces/IApplication.h>
#include <Manro/Core/JobSystem.h>
#include <Manro/Input/InputManager.h>
#include <Manro/Platform/Input/SDL3InputBackend.h>
#include <Manro/Render/Renderer.h>
#include <Manro/Physics/PhysicsWorld.h>

#include <array>
#include <optional>
#include <unordered_map>
#include <vector>

namespace Manro {
    class Renderer;
    class JobSystem;
}

class MinecraftDemo final : public Manro::IApplication {
public:
    MinecraftDemo() = default;

    ~MinecraftDemo() override = default;

    Manro::WindowDesc GetWindowDesc() const override;
    void OnStartup(const Manro::InitContext &ctx) override;
    void OnShutdown() override;
    bool OnUpdate(const Manro::FrameContext &ctx, const Manro::UserCmd &cmd) override;
    void OnRender(Manro::FrameContext &frame) override;
    Manro::InputManager *GetInputManager() override { return &m_InputManager; }

private:
    enum class BlockType : Manro::u8 {
        Grass = 0,
        Dirt,
        Stone,
        Wood,
        Leaf,
        Water,
        Count
    };

    struct BlockCoord {
        int x{};
        int y{};
        int z{};

        bool operator==(const BlockCoord &other) const = default;
    };

    struct BlockCoordHash {
        size_t operator()(const BlockCoord &coord) const;
    };

    struct ChunkCoord {
        int x{};
        int y{};
        int z{};

        bool operator==(const ChunkCoord &other) const = default;
    };

    struct ChunkCoordHash {
        size_t operator()(const ChunkCoord &coord) const;
    };

    struct LocalBlockCoord {
        int x{};
        int y{};
        int z{};
    };

    struct BlockProperties {
        bool solid{false};
        bool opaque{false};
        bool collidable{false};
        bool transparentPass{false};
    };

    struct PendingBlock {
        BlockCoord coord{};
        BlockType type{BlockType::Grass};
    };

    struct GeneratedColumn {
        std::vector<PendingBlock> blocks;
    };

    struct TargetBlock {
        bool valid{false};
        BlockCoord coord{};
        Manro::Vec3 hitPosition{0.f};
        Manro::Vec3 hitNormal{0.f};
    };

    struct MergedBox {
        BlockCoord min{};
        BlockCoord max{};
    };

    struct Chunk {
        ChunkCoord coord{};
        std::vector<Manro::u8> blocks; // BlockType storage
        bool renderDirty{true};
        bool collisionDirty{true};

        std::vector<Manro::PhysicsBodyHandle> collisionBodies;
        bool uploaded{false};
    };

    struct WorldDebugStats {
        size_t solidBlockCount{0};
        size_t waterBlockCount{0};
        size_t leafBlockCount{0};
        size_t physicsBodyCount{0};
        size_t chunkCount{0};
        size_t dirtyRenderChunks{0};
        size_t dirtyCollisionChunks{0};
        float physicsStepMs{0.f};
        float chunkMeshBuildMs{0.f};
        float collisionBuildMs{0.f};
        float raycastMs{0.f};
    };

    static constexpr float kBlockSize = 100.f;
    static constexpr float kHalfBlock = kBlockSize * 0.5f;
    static constexpr int kWorldMin = -9;
    static constexpr int kWorldMax = 8;
    static constexpr int kMaxBuildHeight = 12;
    static constexpr int kChunkSize = 16;
    static constexpr int kChunkVolume = kChunkSize * kChunkSize * kChunkSize;

    void LoadAssets();
    void GenerateWorld();

    static GeneratedColumn BuildTerrainColumn(int x, int z);

    static void TrySpawnTree(int x, int z, int groundHeight, GeneratedColumn &column);

    static void AppendBlock(GeneratedColumn &column, const BlockCoord &coord, BlockType type);

    ChunkCoord WorldToChunkCoord(const BlockCoord &coord) const;

    LocalBlockCoord WorldToLocalBlockCoord(const BlockCoord &coord) const;

    BlockCoord WorldToBlockCoord(const Manro::Vec3 &pos) const;

    size_t ChunkIndex(int lx, int ly, int lz) const;

    BlockCoord ChunkLocalToWorld(const ChunkCoord &cc, const LocalBlockCoord &lc) const;

    Chunk *GetChunk(const ChunkCoord &coord);

    const Chunk *GetChunk(const ChunkCoord &coord) const;

    Chunk &GetOrCreateChunk(const ChunkCoord &coord);

    std::optional<BlockType> GetBlock(const BlockCoord &coord) const;
    bool SetBlock(const BlockCoord &coord, BlockType type);
    bool RemoveBlock(const BlockCoord &coord);
    bool IsOccupied(const BlockCoord &coord) const;

    const BlockProperties &GetBlockProperties(BlockType type) const;

    bool IsSolidBlock(BlockType type) const;

    bool IsOpaqueBlock(BlockType type) const;

    bool IsCollidableBlock(BlockType type) const;

    void MarkChunkDirtyForBlock(const BlockCoord &coord);

    void RebuildDirtyChunks();

    void RebuildChunkCollision(Chunk &chunk);

    void RebuildChunkRender(Chunk &chunk);

    void UpdateSelectionInput();
    void UpdateInteraction(const Manro::Vec3 &eyePos, const Manro::Vec3 &forward, float dt);
    void RefreshTargetBlock(const Manro::Vec3 &eyePos, const Manro::Vec3 &forward);

    bool RaycastVoxel(const Manro::Vec3 &origin, const Manro::Vec3 &dir, float maxDistance, TargetBlock &outHit) const;
    bool TryGetPlacementCoord(BlockCoord &outCoord) const;
    bool CanPlaceBlock(const BlockCoord &coord) const;

    void DrawGui(float dt) const;
    Manro::Vec3 GetForwardVector() const;
    Manro::Vec3 GetEyePosition() const;

    static Manro::Vec3 BlockCenter(const BlockCoord &coord);
    BlockType GetSelectedBlockType() const;
    static const char *BlockName(BlockType type);
    static int TerrainHeight(int x, int z);

    std::vector<MergedBox> BuildMergedBoxesForChunk(const Chunk &chunk) const;

private:
    Manro::IWindow *m_Window{nullptr};
    Manro::JobSystem *m_Jobs{nullptr};
    Manro::Renderer *m_Renderer{nullptr};

    Manro::SDL3InputBackend m_InputBackend;
    Manro::InputManager m_InputManager;
    Manro::Scope<Manro::PhysicsWorld> m_PhysicsWorld;
    Manro::PhysicsBodyHandle m_PlayerBody{Manro::kInvalidBodyHandle};

    Manro::Vec3 m_PlayerPosition{0.f};
    float m_Yaw{0.f};
    float m_Pitch{-15.f};
    float m_MouseSensitivity{0.14f};
    float m_MoveSpeed{431.7f};
    float m_SprintMultiplier{1.3f};
    float m_JumpVelocity{495.f};
    bool m_IsGrounded{false};

    bool m_NoClip{false};
    bool m_InputCaptured{true};
    bool m_ShowPhysics{false};
    bool m_ThirdPerson{false};
    bool m_IsRunning{true};

    bool m_CtrlWasDown{false};
    bool m_F11WasDown{false};
    bool m_F1WasDown{false};
    bool m_F2WasDown{false};
    bool m_F3WasDown{false};
    bool m_LeftMouseWasDown{false};
    bool m_RightMouseWasDown{false};
    bool m_SpaceWasDown{false};
    std::array<bool, 6> m_NumberWasDown{};

    float m_InteractionCooldown{0.f};
    int m_SelectedBlockIndex{0};
    float m_TimeOfDay{9.5f};

    Manro::FrameStats m_LastStats{};
    static constexpr int kHistoryLen = 120;
    float m_FrameTimeHistory[kHistoryLen]{};
    int m_FrameTimeOffset{0};

    TargetBlock m_TargetBlock;
    std::unordered_map<ChunkCoord, Chunk, ChunkCoordHash> m_Chunks;

    std::array<Manro::MeshHandle, 64> m_CubeMeshesByMask{};
    std::array<Manro::Scope<Manro::MaterialInstance>, static_cast<size_t>(BlockType::Count)> m_BlockMaterials;

    mutable WorldDebugStats m_DebugStats{};
};