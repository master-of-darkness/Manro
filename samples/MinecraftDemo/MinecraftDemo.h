#pragma once

#include <Manro/Interfaces/IApplication.h>
#include <Manro/Core/JobSystem.h>
#include <Manro/Input/InputManager.h>
#include <Manro/Platform/Input/SDL3InputBackend.h>
#include <Manro/Render/Renderer.h>
#include <Manro/Physics/PhysicsWorld.h>

#include <array>
#include <unordered_map>
#include <vector>

namespace Manro {
    class Renderer;
    class JobSystem;
}

class MinecraftDemo final : public Manro::IApplication {
public:
    MinecraftDemo() = default;

    ~MinecraftDemo() = default;

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
        int x;
        int y;
        int z;

        bool operator==(const BlockCoord &other) const = default;
    };

    struct BlockCoordHash {
        size_t operator()(const BlockCoord &coord) const;
    };

    struct BlockData {
        BlockType type;
        Manro::PhysicsBodyHandle body{Manro::kInvalidBodyHandle};
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

    void LoadAssets();

    void GenerateWorld();

    GeneratedColumn BuildTerrainColumn(int x, int z) const;

    void TrySpawnTree(int x, int z, int groundHeight, GeneratedColumn &column) const;

    void AppendBlock(GeneratedColumn &column, const BlockCoord &coord, BlockType type) const;

    bool SetBlock(const BlockCoord &coord, BlockType type);

    bool RemoveBlock(const BlockCoord &coord);

    bool IsOccupied(const BlockCoord &coord) const;

    void UpdateSelectionInput();

    void UpdateInteraction(const Manro::Vec3 &eyePos, const Manro::Vec3 &forward, float dt);

    void RefreshTargetBlock(const Manro::Vec3 &eyePos, const Manro::Vec3 &forward);

    bool TryGetPlacementCoord(BlockCoord &outCoord) const;

    bool CanPlaceBlock(const BlockCoord &coord) const;

    void ApplyGroundClearance();

    void DrawGui(float dt) const;

    Manro::Vec3 GetForwardVector() const;

    Manro::Vec3 GetEyePosition() const;

    Manro::Vec3 BlockCenter(const BlockCoord &coord) const;

    BlockType GetSelectedBlockType() const;

    static const char *BlockName(BlockType type);

    static Manro::Vec4 BlockColor(BlockType type);

    static int TerrainHeight(int x, int z);

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

    static constexpr float kBlockSize = 100.f;
    static constexpr float kHalfBlock = kBlockSize * 0.5f;
    static constexpr int kWorldMin = -9;
    static constexpr int kWorldMax = 8;
    static constexpr int kMaxBuildHeight = 12;

    TargetBlock m_TargetBlock;
    std::unordered_map<BlockCoord, BlockData, BlockCoordHash> m_WorldBlocks;
    std::unordered_map<Manro::u32, BlockCoord> m_BodyToCoord;

    Manro::MeshHandle m_CubeMesh;
    std::array<Manro::Scope<Manro::MaterialInstance>, static_cast<size_t>(BlockType::Count)> m_BlockMaterials;
};
