#include "MinecraftDemo.h"

#include <Manro/Resource/Primitives.h>
#include <Manro/Core/VirtualFS.h>
#include <Manro/Interfaces/IWindow.h>
#include <Manro/Core/Logger.h>
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <string>
#include <string_view>

namespace {
    constexpr float kFov = 95.f;
    constexpr float kNearZ = 1.f;
    constexpr float kFarZ = 50000.f;
    constexpr Manro::u32 kWindowWidth = 1920;
    constexpr Manro::u32 kWindowHeight = 1080;

    constexpr float kPlayerWidth = 58.f;
    constexpr float kPlayerHeight = 180.f;
    constexpr float kPlayerHalfWidth = kPlayerWidth * 0.5f;
    constexpr float kPlayerHalfHeight = kPlayerHeight * 0.5f;
    constexpr float kPlayerEyeHeight = 162.f;

    constexpr float kTPSDistance = 360.f;
    constexpr float kTPSPivotOffset = 132.f;

    constexpr float kReachDistance = 825.f;
    constexpr float kInteractInterval = 0.18f;

    constexpr float kGroundDrag = 0.68f;
    constexpr float kAirDrag = 0.91f;

    constexpr float kMaxFallSpeed = 4000.f;

    constexpr Manro::u8 kFacePosZ = 1u << 0;
    constexpr Manro::u8 kFaceNegZ = 1u << 1;
    constexpr Manro::u8 kFaceNegX = 1u << 2;
    constexpr Manro::u8 kFacePosX = 1u << 3;
    constexpr Manro::u8 kFacePosY = 1u << 4;
    constexpr Manro::u8 kFaceNegY = 1u << 5;

    struct BlockMaterialSpec {
        std::string_view baseColorTexture;
        std::string_view normalTexture;
        std::string_view metallicRoughnessTexture;
        Manro::Vec4 texturedBaseColorFactor;
        float metallicFactor;
        float roughnessFactor;
        int alphaMode;
        float alphaCutoff;
        bool doubleSided;
    };

    constexpr std::array<BlockMaterialSpec, 6> kBlockMaterialSpecs{
        {
            {
                "minecraft/textures/block/grass_block_top.png", "", "", {1.f, 1.f, 1.f, 1.f},
                0.f, 0.92f, shaderio::eAlphaModeOpaque, 0.5f, false
            },
            {
                "minecraft/textures/block/dirt.png", "", "", {1.f, 1.f, 1.f, 1.f},
                0.f, 0.98f, shaderio::eAlphaModeOpaque, 0.5f, false
            },
            {
                "minecraft/textures/block/stone.png", "", "", {1.f, 1.f, 1.f, 1.f},
                0.f, 0.84f, shaderio::eAlphaModeOpaque, 0.5f, false
            },
            {
                "minecraft/textures/block/oak_log.png", "", "", {1.f, 1.f, 1.f, 1.f},
                0.f, 0.76f, shaderio::eAlphaModeOpaque, 0.5f, false
            },
            {
                "minecraft/textures/block/leaves_oak.png", "", "", {0.56f, 0.86f, 0.49f, 1.f},
                0.f, 0.93f, shaderio::eAlphaModeMask, 0.5f, true
            },
            {
                "minecraft/textures/block/water_still.png", "", "", {0.78f, 0.90f, 1.f, 0.6f},
                0.f, 0.04f, shaderio::eAlphaModeBlend, 0.5f, true
            },
        }
    };

    std::string TexturePathWithSuffix(std::string_view path, std::string_view suffix) {
        const size_t dotPos = path.find_last_of('.');
        if (dotPos == std::string_view::npos) return std::string(path) + std::string(suffix);

        std::string result;
        result.reserve(path.size() + suffix.size());
        result.append(path.substr(0, dotPos));
        result.append(suffix);
        result.append(path.substr(dotPos));
        return result;
    }

    template<size_t N>
    std::string ResolveTextureVariant(std::string_view basePath,
                                      const std::array<std::string_view, N> &suffixes) {
        auto &vfs = Manro::VirtualFS::Get();
        for (const std::string_view suffix: suffixes) {
            const std::string candidate = TexturePathWithSuffix(basePath, suffix);
            if (vfs.FileExists(candidate)) return candidate;
        }
        return {};
    }

    uint16_t ToMaterialTextureSlot(Manro::TextureHandle texture) {
        return texture.IsValid() ? static_cast<uint16_t>(texture.Index() + 1) : 0;
    }

    Manro::ModelData CreateMaskedCubeModel(const Manro::ModelData &baseCube, Manro::u8 visibleMask) {
        constexpr size_t kFaceCount = 6;
        Manro::ModelData masked = baseCube;
        masked.indices.clear();
        masked.indices.reserve(baseCube.indices.size());

        for (size_t face = 0; face < kFaceCount; ++face) {
            constexpr size_t kIndicesPerFace = 6;
            if ((visibleMask & (1u << face)) == 0) continue;
            const size_t offset = face * kIndicesPerFace;
            masked.indices.insert(masked.indices.end(),
                                  baseCube.indices.begin() + offset,
                                  baseCube.indices.begin() + offset + kIndicesPerFace);
        }
        return masked;
    }

    class ScopedTimer {
    public:
        explicit ScopedTimer(float &outMs)
            : m_OutMs(outMs), m_Start(std::chrono::steady_clock::now()) {
        }

        ~ScopedTimer() {
            const auto end = std::chrono::steady_clock::now();
            m_OutMs = std::chrono::duration<float, std::milli>(end - m_Start).count();
        }

    private:
        float &m_OutMs;
        std::chrono::steady_clock::time_point m_Start;
    };

    int FloorDiv(int a, int b) {
        int q = a / b;
        int r = a % b;
        if ((r != 0) && ((r < 0) != (b < 0))) --q;
        return q;
    }

    int PositiveMod(int a, int b) {
        int r = a % b;
        if (r < 0) r += b;
        return r;
    }
}

size_t MinecraftDemo::BlockCoordHash::operator()(const BlockCoord &coord) const {
    size_t h = std::hash<int>{}(coord.x);
    h ^= std::hash<int>{}(coord.y) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<int>{}(coord.z) + 0x9e3779b9 + (h << 6) + (h >> 2);
    return h;
}

size_t MinecraftDemo::ChunkCoordHash::operator()(const ChunkCoord &coord) const {
    size_t h = std::hash<int>{}(coord.x);
    h ^= std::hash<int>{}(coord.y) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<int>{}(coord.z) + 0x9e3779b9 + (h << 6) + (h >> 2);
    return h;
}

Manro::WindowDesc MinecraftDemo::GetWindowDesc() const {
    Manro::WindowDesc d;
    d.Title = "Minecraft Demo";
    d.Width = kWindowWidth;
    d.Height = kWindowHeight;
    d.Fullscreen = false;
    d.Resizable = true;
    return d;
}

void MinecraftDemo::OnStartup(const Manro::InitContext &ctx) {
    m_Window = &ctx.Window;
    m_Jobs = &ctx.Jobs;
    m_Renderer = &ctx.Renderer;

    m_Renderer->SetDebugUIEnabled(false);
    auto settings = m_Renderer->GetSettings();
    settings.enableVSync = false;
    settings.aaMode = Manro::AntiAliasingMode::None;
    settings.msaaSamples = Manro::MSAASampleCount::MSAA_1X;
    settings.shadows.enabled = false;
    m_Renderer->SetSettings(settings);

    Manro::VirtualFS::Get().SetBaseDir(MANRO_ASSETS_DIR);
    m_InputManager.SetBackend(&m_InputBackend);

    m_PhysicsWorld = Manro::CreateScope<Manro::PhysicsWorld>();

    LoadAssets();
    GenerateWorld();
    RebuildDirtyChunks();

    const float spawnY = static_cast<float>(TerrainHeight(0, 0)) * kBlockSize
                         + kPlayerHalfHeight + 20.f;
    m_PlayerPosition = {0.f, spawnY, 0.f};

    Manro::PhysicsWorld::DynamicBodyDesc playerBodyDesc{};
    playerBodyDesc.friction = 0.f;
    playerBodyDesc.restitution = 0.f;
    playerBodyDesc.convexRadius = 0.f;
    playerBodyDesc.allowSleeping = false;
    playerBodyDesc.lockRotation = true;
    playerBodyDesc.maxLinearVelocity = kMaxFallSpeed;
    playerBodyDesc.maxAngularVelocity = 0.f;

    m_PlayerBody = m_PhysicsWorld->AddDynamicBox(
        m_PlayerPosition,
        {kPlayerHalfWidth, kPlayerHalfHeight, kPlayerHalfWidth},
        playerBodyDesc);

    if (m_PlayerBody == Manro::kInvalidBodyHandle)
        LOG_ERROR("Failed to create player physics body.");
}

void MinecraftDemo::OnShutdown() {
    for (auto &[cc, chunk]: m_Chunks) {
        for (auto body: chunk.collisionBodies) {
            if (body != Manro::kInvalidBodyHandle)
                m_PhysicsWorld->RemoveBody(body);
        }
        chunk.collisionBodies.clear();
    }

    m_BlockMaterials = {};
    m_CubeMeshesByMask.fill(Manro::kInvalidMesh);
    m_Chunks.clear();
    m_PhysicsWorld.reset();
}

bool MinecraftDemo::OnUpdate(const Manro::FrameContext &ctx, const Manro::UserCmd &) {
    if (!m_IsRunning) return false;

    const float dt = ctx.DeltaTime;
    m_TimeOfDay += dt * 0.15f;
    if (m_TimeOfDay >= 24.f) m_TimeOfDay -= 24.f;
    m_InteractionCooldown = std::max(0.f, m_InteractionCooldown - dt);

    const bool ctrlDown = m_InputManager.IsKeyDown(Manro::Key::LeftCtrl);
    const bool f11Down = m_InputManager.IsKeyDown(Manro::Key::F11);
    const bool f1Down = m_InputManager.IsKeyDown(Manro::Key::F1);
    const bool f2Down = m_InputManager.IsKeyDown(Manro::Key::F2);
    const bool f3Down = m_InputManager.IsKeyDown(Manro::Key::F3);

    if (ctrlDown && !m_CtrlWasDown) m_InputCaptured = !m_InputCaptured;
    if (f11Down && !m_F11WasDown) m_Window->ToggleFullscreen();
    if (f1Down && !m_F1WasDown) {
        m_NoClip = !m_NoClip;
        m_PhysicsWorld->SetLinearVelocity(m_PlayerBody, {0.f, 0.f, 0.f});
        m_PhysicsWorld->SetBodyMotionType(m_PlayerBody, m_NoClip);
    }
    if (f2Down && !m_F2WasDown) m_ShowPhysics = !m_ShowPhysics;
    if (f3Down && !m_F3WasDown) m_ThirdPerson = !m_ThirdPerson;

    m_CtrlWasDown = ctrlDown;
    m_F11WasDown = f11Down;
    m_F1WasDown = f1Down;
    m_F2WasDown = f2Down;
    m_F3WasDown = f3Down;

    if (m_InputManager.IsKeyDown(Manro::Key::Escape)) return false;

    if (m_InputCaptured) {
        auto [dx, dy] = m_InputManager.ConsumeMouseDelta();
        m_Yaw += dx * m_MouseSensitivity;
        m_Pitch = std::clamp(m_Pitch - dy * m_MouseSensitivity, -89.f, 89.f);

        const float yawRad = glm::radians(m_Yaw);
        const float pitchRad = glm::radians(m_Pitch);

        const Manro::Vec3 forwardFlat = glm::normalize(
            Manro::Vec3{cosf(yawRad), 0.f, sinf(yawRad)});
        const Manro::Vec3 right = glm::normalize(
            Manro::Vec3{-sinf(yawRad), 0.f, cosf(yawRad)});
        constexpr Manro::Vec3 up = {0.f, 1.f, 0.f};

        using K = Manro::Key;
        float speed = m_MoveSpeed;
        if (m_InputManager.IsKeyDown(K::LeftShift)) speed *= m_SprintMultiplier;

        const bool spaceDown = m_InputManager.IsKeyDown(K::Space);
        const bool jumpPressed = spaceDown && !m_SpaceWasDown;

        if (m_NoClip) {
            const Manro::Vec3 lookDir = glm::normalize(Manro::Vec3{
                cosf(pitchRad) * cosf(yawRad),
                sinf(pitchRad),
                cosf(pitchRad) * sinf(yawRad)
            });

            Manro::Vec3 moveDir{0.f};
            if (m_InputManager.IsKeyDown(K::W)) moveDir += lookDir;
            if (m_InputManager.IsKeyDown(K::S)) moveDir -= lookDir;
            if (m_InputManager.IsKeyDown(K::D)) moveDir += right;
            if (m_InputManager.IsKeyDown(K::A)) moveDir -= right;
            if (spaceDown) moveDir += up;
            if (m_InputManager.IsKeyDown(K::LeftCtrl)) moveDir -= up;

            if (glm::length(moveDir) > 0.001f) moveDir = glm::normalize(moveDir);
            m_PhysicsWorld->SetKinematicVelocity(m_PlayerBody, moveDir * speed);
        } else {
            const bool grounded = m_PhysicsWorld->IsGrounded(m_PlayerBody);
            m_IsGrounded = grounded;

            Manro::Vec3 vel = m_PhysicsWorld->GetBodyLinearVelocity(m_PlayerBody);
            vel.y = std::max(vel.y, -kMaxFallSpeed);

            Manro::Vec3 moveDir{0.f};
            if (m_InputManager.IsKeyDown(K::W)) moveDir += forwardFlat;
            if (m_InputManager.IsKeyDown(K::S)) moveDir -= forwardFlat;
            if (m_InputManager.IsKeyDown(K::D)) moveDir += right;
            if (m_InputManager.IsKeyDown(K::A)) moveDir -= right;

            const bool hasInput = glm::length(moveDir) > 0.001f;
            if (hasInput) moveDir = glm::normalize(moveDir);

            float targetVx, targetVz;
            if (grounded) {
                if (hasInput) {
                    targetVx = moveDir.x * speed;
                    targetVz = moveDir.z * speed;
                } else {
                    targetVx = vel.x * kGroundDrag;
                    targetVz = vel.z * kGroundDrag;
                }
            } else {
                if (hasInput) {
                    constexpr float airControl = 0.18f;
                    targetVx = vel.x + (moveDir.x * speed - vel.x) * airControl;
                    targetVz = vel.z + (moveDir.z * speed - vel.z) * airControl;
                } else {
                    targetVx = vel.x * kAirDrag;
                    targetVz = vel.z * kAirDrag;
                }
            }

            float targetVy = vel.y;
            if (grounded) {
                if (vel.y < 0.f) targetVy = 0.f;
                if (jumpPressed) {
                    targetVy = m_JumpVelocity;
                    m_IsGrounded = false;
                }
            }

            m_PhysicsWorld->SetLinearVelocity(m_PlayerBody, {targetVx, targetVy, targetVz});
        }

        m_SpaceWasDown = spaceDown;
    } else {
        m_InputManager.ConsumeMouseDelta();
        m_SpaceWasDown = false;
    }

    {
        ScopedTimer t(m_DebugStats.physicsStepMs);
        m_PhysicsWorld->Step(dt);
    }

    if (!m_NoClip && m_PlayerBody != Manro::kInvalidBodyHandle)
        m_PlayerPosition = m_PhysicsWorld->GetBodyPosition(m_PlayerBody);
    else if (m_PlayerBody != Manro::kInvalidBodyHandle)
        m_PlayerPosition = m_PhysicsWorld->GetBodyPosition(m_PlayerBody);

    UpdateSelectionInput();
    RefreshTargetBlock(GetEyePosition(), GetForwardVector());
    UpdateInteraction(GetEyePosition(), GetForwardVector(), dt);

    RebuildDirtyChunks();

    return true;
}

void MinecraftDemo::OnRender(Manro::FrameContext &frame) {
    const float dt = frame.DeltaTime;
    m_Window->CaptureMouse(m_InputCaptured);
    m_Window->ShowCursor(!m_InputCaptured);

    const Manro::Vec3 forward = GetForwardVector();
    Manro::Vec3 eyePos = GetEyePosition();
    if (m_ThirdPerson) {
        const Manro::Vec3 pivot = m_PlayerPosition + Manro::Vec3(0.f, kTPSPivotOffset, 0.f);
        eyePos = pivot - forward * kTPSDistance;
    }

    const Manro::Mat4 view = glm::lookAt(eyePos, eyePos + forward, {0.f, 1.f, 0.f});
    const Manro::Mat4 proj = glm::perspective(
        glm::radians(kFov), m_Renderer->GetAspectRatio(), kNearZ, kFarZ);
    m_Renderer->SetViewProjection(view, proj);
    m_Renderer->SetCameraPosition(eyePos);
    m_Renderer->ClearLights();

    const float dayTau = (m_TimeOfDay / 24.f) * 2.f * 3.14159265f;
    const float sunAltitude = sinf(dayTau - 1.5707963f);
    const float sunAzimuth = cosf(dayTau - 1.5707963f);

    Manro::LightData sun{};
    sun.type = shaderio::eLightTypeDirectional;
    sun.direction = glm::normalize(Manro::Vec3{sunAzimuth, -sunAltitude, 0.25f});
    if (sunAltitude > 0.1f) {
        sun.color = {1.0f, 0.96f, 0.88f};
        sun.intensity = 2.8f * sunAltitude + 0.4f;
    } else if (sunAltitude > -0.2f) {
        const float t = (sunAltitude + 0.2f) / 0.3f;
        sun.color = glm::mix(Manro::Vec3{0.95f, 0.35f, 0.12f},
                             Manro::Vec3{1.0f, 0.96f, 0.88f}, t);
        sun.intensity = 0.9f;
    } else {
        sun.color = {0.18f, 0.25f, 0.44f};
        sun.intensity = 0.25f;
    }
    m_Renderer->AddLight(sun);

    if (m_TargetBlock.valid) {
        const Manro::Vec3 center = BlockCenter(m_TargetBlock.coord);
        glm::mat4 model = glm::translate(glm::mat4(1.f), center);
        model = glm::scale(model, Manro::Vec3(kHalfBlock));
        m_Renderer->DrawBox({0.f, 0.f, 0.f}, {1.f, 1.f, 1.f}, model, 0);
    }

    if (m_ShowPhysics) m_PhysicsWorld->DrawPhysics(*m_Renderer);

    m_Renderer->BeginRendering();
    m_Renderer->RenderQueue();
    DrawGui(dt);
    m_Renderer->EndRendering();

    m_LastStats = m_Renderer->GetLastFrameStats();
    const float frameMs = dt * 1000.f;
    m_FrameTimeHistory[m_FrameTimeOffset] = frameMs;
    m_FrameTimeOffset = (m_FrameTimeOffset + 1) % kHistoryLen;
}

void MinecraftDemo::LoadAssets() {
    if (Manro::VirtualFS::Get().FileExists("skyboxes/cubemap_sky.png")) {
        auto skyFaces = Manro::TextureLoader::LoadCubemap("skyboxes/cubemap_sky.png");
        if (!skyFaces.empty()) {
            const auto h = m_Renderer->UploadCubemap(skyFaces);
            m_Renderer->SetSkybox(h);
        }
    }

    const Manro::ModelData baseCube = Manro::Primitives::CreateCube(2.0f);
    m_CubeMeshesByMask.fill(Manro::kInvalidMesh);
    for (int mask = 1; mask < 64; ++mask) {
        Manro::ModelData maskedCube = CreateMaskedCubeModel(baseCube, static_cast<Manro::u8>(mask));
        if (maskedCube.indices.empty()) continue;
        m_CubeMeshesByMask[mask] = m_Renderer->UploadMesh(maskedCube);
    }

    std::unordered_map<std::string, Manro::TextureHandle> textureCache;
    textureCache.reserve(m_BlockMaterials.size() * 3);

    auto uploadTexture = [&](const std::string &path) -> Manro::TextureHandle {
        if (path.empty()) return Manro::kInvalidTexture;
        if (const auto it = textureCache.find(path); it != textureCache.end()) {
            return it->second;
        }
        if (!Manro::VirtualFS::Get().FileExists(path)) {
            textureCache.emplace(path, Manro::kInvalidTexture);
            return Manro::kInvalidTexture;
        }

        auto textureData = Manro::TextureLoader::LoadOne(path);
        if (textureData.pixels.empty()) {
            LOG_WARN("[MinecraftDemo] Failed to load texture '{}'.", path);
            textureCache.emplace(path, Manro::kInvalidTexture);
            return Manro::kInvalidTexture;
        }

        const Manro::TextureHandle handle = m_Renderer->UploadTexture(textureData);
        textureCache.emplace(path, handle);
        return handle;
    };

    for (size_t i = 0; i < m_BlockMaterials.size(); ++i) {
        constexpr std::array<std::string_view, 3> kNormalSuffixes{"_n", "_normal", "_nor"};
        constexpr std::array<std::string_view, 5> kMetallicRoughnessSuffixes{
            "_orm", "_mr", "_mra", "_metalrough", "_s"
        };

        const BlockType type = static_cast<BlockType>(i);
        const BlockMaterialSpec &spec = kBlockMaterialSpecs[i];
        auto material = m_Renderer->CreateMaterialInstance(m_Renderer->GetDefaultMaterial());
        auto &matData = material->ModifyData();

        matData.pbrBaseColorFactor = spec.texturedBaseColorFactor;
        matData.pbrMetallicFactor = spec.metallicFactor;
        matData.pbrRoughnessFactor = spec.roughnessFactor;
        matData.alphaMode = spec.alphaMode;
        matData.alphaCutoff = spec.alphaCutoff;
        matData.doubleSided = spec.doubleSided ? 1 : 0;

        const std::string baseColorPath(spec.baseColorTexture);
        const Manro::TextureHandle baseColorTexture = uploadTexture(baseColorPath);
        if (baseColorTexture.IsValid()) {
            material->SetTexture(baseColorTexture);
            matData.pbrBaseColorFactor = spec.texturedBaseColorFactor;
        } else {
            LOG_WARN("[MinecraftDemo] Missing block texture '{}' for {}.", baseColorPath, BlockName(type));
        }

        const std::string normalPath = !spec.normalTexture.empty()
                                           ? std::string(spec.normalTexture)
                                           : ResolveTextureVariant(baseColorPath, kNormalSuffixes);
        if (const Manro::TextureHandle normalTexture = uploadTexture(normalPath); normalTexture.IsValid()) {
            matData.normalTexture = ToMaterialTextureSlot(normalTexture);
        }

        const std::string metallicRoughnessPath = !spec.metallicRoughnessTexture.empty()
                                                      ? std::string(spec.metallicRoughnessTexture)
                                                      : ResolveTextureVariant(
                                                          baseColorPath, kMetallicRoughnessSuffixes);
        if (const Manro::TextureHandle metallicRoughnessTexture = uploadTexture(metallicRoughnessPath);
            metallicRoughnessTexture.IsValid()) {
            matData.pbrMetallicRoughnessTexture = ToMaterialTextureSlot(metallicRoughnessTexture);
        }

        m_BlockMaterials[i] = std::move(material);
    }
}

void MinecraftDemo::GenerateWorld() {
    constexpr int worldWidth = kWorldMax - kWorldMin + 1;
    constexpr int worldDepth = kWorldMax - kWorldMin + 1;
    constexpr Manro::u32 columnCount = static_cast<Manro::u32>(worldWidth * worldDepth);

    std::vector<GeneratedColumn> generatedColumns(columnCount);
    const auto handle = m_Jobs->CreateHandle();
    m_Jobs->Dispatch(handle, columnCount, [&](Manro::u32 index) {
        const int xOffset = static_cast<int>(index) / worldDepth;
        const int zOffset = static_cast<int>(index) % worldDepth;
        const int x = kWorldMin + xOffset;
        const int z = kWorldMin + zOffset;
        generatedColumns[index] = BuildTerrainColumn(x, z);
    });
    m_Jobs->Wait(handle);

    for (const auto &column: generatedColumns) {
        for (const auto &block: column.blocks) {
            SetBlock(block.coord, block.type);
        }
    }

    LOG_INFO("[MinecraftDemo] Generated voxel world using chunked storage.");
}

MinecraftDemo::GeneratedColumn MinecraftDemo::BuildTerrainColumn(int x, int z) {
    GeneratedColumn column;
    const int height = TerrainHeight(x, z);

    for (int y = 0; y < height; ++y) {
        BlockType type;
        if (y == 0) type = BlockType::Stone;
        else if (y == height - 1) type = BlockType::Grass;
        else if (y >= height - 4) type = BlockType::Dirt;
        else type = BlockType::Stone;
        AppendBlock(column, {x, y, z}, type);
    }

    constexpr int waterLevel = 3;
    if (height <= waterLevel) {
        for (int y = height; y <= waterLevel; ++y)
            AppendBlock(column, {x, y, z}, BlockType::Water);
    }

    TrySpawnTree(x, z, height, column);
    return column;
}

void MinecraftDemo::TrySpawnTree(int x, int z, int groundHeight, GeneratedColumn &column) {
    const int hash = std::abs(x * 7349 + z * 9151 + x * z * 193);
    if (groundHeight <= 4) return;
    if ((hash % 13) != 0) return;
    if (std::abs(x) < 3 && std::abs(z) < 3) return;

    const int trunkHeight = 4 + (hash % 3);
    for (int i = 0; i < trunkHeight; ++i)
        AppendBlock(column, {x, groundHeight + i, z}, BlockType::Wood);

    const int leafBase = groundHeight + trunkHeight - 1;
    for (int lx = -2; lx <= 2; ++lx) {
        for (int lz = -2; lz <= 2; ++lz) {
            for (int ly = 0; ly <= 3; ++ly) {
                const int dist = std::abs(lx) + std::abs(lz) + ly;
                if (dist > 4) continue;
                if (lx == 0 && lz == 0 && ly < 2) continue;
                AppendBlock(column, {x + lx, leafBase + ly, z + lz}, BlockType::Leaf);
            }
        }
    }
}

void MinecraftDemo::AppendBlock(GeneratedColumn &column, const BlockCoord &coord, BlockType type) {
    if (coord.y < 0 || coord.y >= kMaxBuildHeight) return;
    column.blocks.push_back(PendingBlock{coord, type});
}

MinecraftDemo::ChunkCoord MinecraftDemo::WorldToChunkCoord(const BlockCoord &coord) const {
    return {
        FloorDiv(coord.x, kChunkSize),
        FloorDiv(coord.y, kChunkSize),
        FloorDiv(coord.z, kChunkSize)
    };
}

MinecraftDemo::LocalBlockCoord MinecraftDemo::WorldToLocalBlockCoord(const BlockCoord &coord) const {
    return {
        PositiveMod(coord.x, kChunkSize),
        PositiveMod(coord.y, kChunkSize),
        PositiveMod(coord.z, kChunkSize)
    };
}

MinecraftDemo::BlockCoord MinecraftDemo::WorldToBlockCoord(const Manro::Vec3 &pos) const {
    return {
        static_cast<int>(std::floor(pos.x / kBlockSize)),
        static_cast<int>(std::floor(pos.y / kBlockSize)),
        static_cast<int>(std::floor(pos.z / kBlockSize))
    };
}

size_t MinecraftDemo::ChunkIndex(int lx, int ly, int lz) const {
    return static_cast<size_t>(lx + ly * kChunkSize + lz * kChunkSize * kChunkSize);
}

MinecraftDemo::BlockCoord MinecraftDemo::ChunkLocalToWorld(const ChunkCoord &cc, const LocalBlockCoord &lc) const {
    return {
        cc.x * kChunkSize + lc.x,
        cc.y * kChunkSize + lc.y,
        cc.z * kChunkSize + lc.z
    };
}

MinecraftDemo::Chunk *MinecraftDemo::GetChunk(const ChunkCoord &coord) {
    const auto it = m_Chunks.find(coord);
    return (it != m_Chunks.end()) ? &it->second : nullptr;
}

const MinecraftDemo::Chunk *MinecraftDemo::GetChunk(const ChunkCoord &coord) const {
    const auto it = m_Chunks.find(coord);
    return (it != m_Chunks.end()) ? &it->second : nullptr;
}

MinecraftDemo::Chunk &MinecraftDemo::GetOrCreateChunk(const ChunkCoord &coord) {
    auto [it, inserted] = m_Chunks.try_emplace(coord);
    Chunk &chunk = it->second;
    if (inserted) {
        chunk.coord = coord;
        chunk.blocks.resize(kChunkVolume, 255);
        chunk.renderDirty = true;
        chunk.collisionDirty = true;
    }
    return chunk;
}

std::optional<MinecraftDemo::BlockType> MinecraftDemo::GetBlock(const BlockCoord &coord) const {
    if (coord.y < 0 || coord.y >= kMaxBuildHeight) return std::nullopt;

    const ChunkCoord cc = WorldToChunkCoord(coord);
    const Chunk *chunk = GetChunk(cc);
    if (!chunk) return std::nullopt;

    const LocalBlockCoord lc = WorldToLocalBlockCoord(coord);
    const size_t idx = ChunkIndex(lc.x, lc.y, lc.z);
    const Manro::u8 raw = chunk->blocks[idx];
    if (raw == 255) return std::nullopt;
    return static_cast<BlockType>(raw);
}

bool MinecraftDemo::SetBlock(const BlockCoord &coord, BlockType type) {
    if (coord.y < 0 || coord.y >= kMaxBuildHeight) return false;
    if (IsOccupied(coord)) return false;

    const ChunkCoord cc = WorldToChunkCoord(coord);
    Chunk &chunk = GetOrCreateChunk(cc);
    const LocalBlockCoord lc = WorldToLocalBlockCoord(coord);
    const size_t idx = ChunkIndex(lc.x, lc.y, lc.z);
    chunk.blocks[idx] = static_cast<Manro::u8>(type);

    MarkChunkDirtyForBlock(coord);
    return true;
}

bool MinecraftDemo::RemoveBlock(const BlockCoord &coord) {
    const ChunkCoord cc = WorldToChunkCoord(coord);
    Chunk *chunk = GetChunk(cc);
    if (!chunk) return false;

    const LocalBlockCoord lc = WorldToLocalBlockCoord(coord);
    const size_t idx = ChunkIndex(lc.x, lc.y, lc.z);
    if (chunk->blocks[idx] == 255) return false;

    chunk->blocks[idx] = 255;
    MarkChunkDirtyForBlock(coord);
    return true;
}

bool MinecraftDemo::IsOccupied(const BlockCoord &coord) const {
    return GetBlock(coord).has_value();
}

const MinecraftDemo::BlockProperties &MinecraftDemo::GetBlockProperties(BlockType type) const {
    static const std::array<BlockProperties, static_cast<size_t>(BlockType::Count)> props{
        {
            {true, true, true, false}, // Grass
            {true, true, true, false}, // Dirt
            {true, true, true, false}, // Stone
            {true, true, true, false}, // Wood
            {false, false, false, true}, // Leaf
            {false, false, false, true}, // Water
        }
    };
    return props[static_cast<size_t>(type)];
}

bool MinecraftDemo::IsSolidBlock(BlockType type) const {
    return GetBlockProperties(type).solid;
}

bool MinecraftDemo::IsOpaqueBlock(BlockType type) const {
    return GetBlockProperties(type).opaque;
}

bool MinecraftDemo::IsCollidableBlock(BlockType type) const {
    return GetBlockProperties(type).collidable;
}

void MinecraftDemo::MarkChunkDirtyForBlock(const BlockCoord &coord) {
    const ChunkCoord cc = WorldToChunkCoord(coord);

    auto mark = [&](const ChunkCoord &c) {
        Chunk &chunk = GetOrCreateChunk(c);
        chunk.renderDirty = true;
        chunk.collisionDirty = true;
    };

    mark(cc);

    const LocalBlockCoord lc = WorldToLocalBlockCoord(coord);

    if (lc.x == 0) mark({cc.x - 1, cc.y, cc.z});
    if (lc.x == kChunkSize - 1) mark({cc.x + 1, cc.y, cc.z});
    if (lc.y == 0) mark({cc.x, cc.y - 1, cc.z});
    if (lc.y == kChunkSize - 1) mark({cc.x, cc.y + 1, cc.z});
    if (lc.z == 0) mark({cc.x, cc.y, cc.z - 1});
    if (lc.z == kChunkSize - 1) mark({cc.x, cc.y, cc.z + 1});
}

std::vector<MinecraftDemo::MergedBox> MinecraftDemo::BuildMergedBoxesForChunk(const Chunk &chunk) const {
    std::vector<MergedBox> boxes;
    std::array<bool, kChunkVolume> visited{};
    visited.fill(false);

    for (int z = 0; z < kChunkSize; ++z) {
        for (int y = 0; y < kChunkSize; ++y) {
            for (int x = 0; x < kChunkSize; ++x) {
                const size_t idx = ChunkIndex(x, y, z);
                if (visited[idx]) continue;

                const Manro::u8 raw = chunk.blocks[idx];
                if (raw == 255) continue;

                const BlockType type = static_cast<BlockType>(raw);
                if (!IsCollidableBlock(type)) continue;

                int runEnd = x;
                while (runEnd + 1 < kChunkSize) {
                    const size_t nextIdx = ChunkIndex(runEnd + 1, y, z);
                    if (visited[nextIdx]) break;
                    const Manro::u8 nextRaw = chunk.blocks[nextIdx];
                    if (nextRaw != raw) break;
                    if (!IsCollidableBlock(static_cast<BlockType>(nextRaw))) break;
                    ++runEnd;
                }

                for (int rx = x; rx <= runEnd; ++rx)
                    visited[ChunkIndex(rx, y, z)] = true;

                const BlockCoord minW = ChunkLocalToWorld(chunk.coord, {x, y, z});
                const BlockCoord maxW = ChunkLocalToWorld(chunk.coord, {runEnd, y, z});
                boxes.push_back({minW, maxW});
            }
        }
    }

    return boxes;
}

void MinecraftDemo::RebuildChunkCollision(Chunk &chunk) {
    ScopedTimer timer(m_DebugStats.collisionBuildMs);

    for (auto body: chunk.collisionBodies) {
        if (body != Manro::kInvalidBodyHandle)
            m_PhysicsWorld->RemoveBody(body);
    }
    chunk.collisionBodies.clear();

    const std::vector<MergedBox> boxes = BuildMergedBoxesForChunk(chunk);

    for (const MergedBox &box: boxes) {
        const float sizeX = static_cast<float>(box.max.x - box.min.x + 1) * kBlockSize;
        const float sizeY = static_cast<float>(box.max.y - box.min.y + 1) * kBlockSize;
        const float sizeZ = static_cast<float>(box.max.z - box.min.z + 1) * kBlockSize;

        const Manro::Vec3 center{
            (static_cast<float>(box.min.x) + static_cast<float>(box.max.x) + 1.f) * 0.5f * kBlockSize,
            (static_cast<float>(box.min.y) + static_cast<float>(box.max.y) + 1.f) * 0.5f * kBlockSize,
            (static_cast<float>(box.min.z) + static_cast<float>(box.max.z) + 1.f) * 0.5f * kBlockSize
        };

        Manro::PhysicsWorld::StaticBodyDesc desc{};
        desc.friction = 0.6f;
        desc.restitution = 0.f;
        desc.convexRadius = 0.f;

        const auto body = m_PhysicsWorld->AddStaticBox(
            center,
            {sizeX * 0.5f, sizeY * 0.5f, sizeZ * 0.5f},
            desc);

        if (body != Manro::kInvalidBodyHandle)
            chunk.collisionBodies.push_back(body);
    }

    chunk.collisionDirty = false;
}

void MinecraftDemo::RebuildChunkRender(Chunk &chunk) {
    ScopedTimer timer(m_DebugStats.chunkMeshBuildMs);

    m_Renderer->ClearStaticDraws();

    const auto shouldRenderFace = [&](BlockType selfType, int x, int y, int z) {
        const auto neighbor = GetBlock({x, y, z});
        if (!neighbor.has_value()) return true;

        const bool selfOpaque = IsOpaqueBlock(selfType);
        const bool neighborOpaque = IsOpaqueBlock(*neighbor);

        if (selfOpaque && neighborOpaque) return false;
        if (!selfOpaque && neighborOpaque) return false;
        if (selfOpaque && !neighborOpaque) return true;
        return false;
    };

    for (const auto &[cc, ch]: m_Chunks) {
        for (int z = 0; z < kChunkSize; ++z) {
            for (int y = 0; y < kChunkSize; ++y) {
                for (int x = 0; x < kChunkSize; ++x) {
                    const size_t idx = ChunkIndex(x, y, z);
                    const Manro::u8 raw = ch.blocks[idx];
                    if (raw == 255) continue;

                    const BlockType type = static_cast<BlockType>(raw);
                    const BlockCoord world = ChunkLocalToWorld(cc, {x, y, z});

                    Manro::u8 visibleMask = 0;
                    if (shouldRenderFace(type, world.x, world.y, world.z + 1)) visibleMask |= kFacePosZ;
                    if (shouldRenderFace(type, world.x, world.y, world.z - 1)) visibleMask |= kFaceNegZ;
                    if (shouldRenderFace(type, world.x - 1, world.y, world.z)) visibleMask |= kFaceNegX;
                    if (shouldRenderFace(type, world.x + 1, world.y, world.z)) visibleMask |= kFacePosX;
                    if (shouldRenderFace(type, world.x, world.y + 1, world.z)) visibleMask |= kFacePosY;
                    if (shouldRenderFace(type, world.x, world.y - 1, world.z)) visibleMask |= kFaceNegY;
                    if (visibleMask == 0) continue;

                    const Manro::MeshHandle mesh = m_CubeMeshesByMask[visibleMask];
                    if (!mesh.IsValid()) continue;

                    glm::mat4 model = glm::translate(glm::mat4(1.f), BlockCenter(world));
                    model = glm::scale(model, Manro::Vec3(kHalfBlock));
                    m_Renderer->DrawMeshStatic(
                        mesh,
                        *m_BlockMaterials[static_cast<size_t>(type)],
                        model);
                }
            }
        }
    }

    for (auto &[cc2, c2]: m_Chunks) {
        c2.renderDirty = false;
        c2.uploaded = true;
    }
}

void MinecraftDemo::RebuildDirtyChunks() {
    m_DebugStats.chunkCount = m_Chunks.size();
    m_DebugStats.physicsBodyCount = 0;
    m_DebugStats.dirtyRenderChunks = 0;
    m_DebugStats.dirtyCollisionChunks = 0;
    m_DebugStats.solidBlockCount = 0;
    m_DebugStats.waterBlockCount = 0;
    m_DebugStats.leafBlockCount = 0;

    bool anyRenderDirty = false;

    for (auto &[cc, chunk]: m_Chunks) {
        if (chunk.renderDirty) {
            anyRenderDirty = true;
            ++m_DebugStats.dirtyRenderChunks;
        }
        if (chunk.collisionDirty) ++m_DebugStats.dirtyCollisionChunks;

        for (auto body: chunk.collisionBodies) {
            if (body != Manro::kInvalidBodyHandle)
                ++m_DebugStats.physicsBodyCount;
        }

        for (Manro::u8 raw: chunk.blocks) {
            if (raw == 255) continue;
            const BlockType t = static_cast<BlockType>(raw);
            if (t == BlockType::Water) ++m_DebugStats.waterBlockCount;
            else if (t == BlockType::Leaf) ++m_DebugStats.leafBlockCount;
            else ++m_DebugStats.solidBlockCount;
        }
    }

    for (auto &[cc, chunk]: m_Chunks) {
        if (chunk.collisionDirty)
            RebuildChunkCollision(chunk);
    }

    if (anyRenderDirty)
        RebuildChunkRender(m_Chunks.begin()->second);
}

void MinecraftDemo::UpdateSelectionInput() {
    constexpr std::array<Manro::Key, 6> keys{
        Manro::Key::Num1, Manro::Key::Num2, Manro::Key::Num3,
        Manro::Key::Num4, Manro::Key::Num5, Manro::Key::Num6
    };

    for (size_t i = 0; i < keys.size(); ++i) {
        const bool down = m_InputManager.IsKeyDown(keys[i]);
        if (down && !m_NumberWasDown[i])
            m_SelectedBlockIndex = static_cast<int>(i);
        m_NumberWasDown[i] = down;
    }
}

void MinecraftDemo::UpdateInteraction(
    const Manro::Vec3 &eyePos, const Manro::Vec3 &forward, float) {
    const bool leftDown = m_InputManager.IsMouseButtonDown(Manro::MouseButton::Left);
    const bool rightDown = m_InputManager.IsMouseButtonDown(Manro::MouseButton::Right);

    if (!m_InputCaptured) {
        m_LeftMouseWasDown = leftDown;
        m_RightMouseWasDown = rightDown;
        return;
    }

    if (m_InteractionCooldown <= 0.f && leftDown && !m_LeftMouseWasDown && m_TargetBlock.valid) {
        if (RemoveBlock(m_TargetBlock.coord)) {
            RefreshTargetBlock(eyePos, forward);
            m_InteractionCooldown = kInteractInterval;
        }
    }

    if (m_InteractionCooldown <= 0.f && rightDown && !m_RightMouseWasDown) {
        BlockCoord placeCoord{};
        if (TryGetPlacementCoord(placeCoord) && CanPlaceBlock(placeCoord)) {
            if (SetBlock(placeCoord, GetSelectedBlockType())) {
                RefreshTargetBlock(eyePos, forward);
                m_InteractionCooldown = kInteractInterval;
            }
        }
    }

    m_LeftMouseWasDown = leftDown;
    m_RightMouseWasDown = rightDown;
}

void MinecraftDemo::RefreshTargetBlock(const Manro::Vec3 &eyePos, const Manro::Vec3 &forward) {
    ScopedTimer timer(m_DebugStats.raycastMs);
    m_TargetBlock = {};
    RaycastVoxel(eyePos, forward, kReachDistance, m_TargetBlock);
}

bool MinecraftDemo::RaycastVoxel(const Manro::Vec3 &origin,
                                 const Manro::Vec3 &dir,
                                 float maxDistance,
                                 TargetBlock &outHit) const {
    if (glm::length(dir) < 0.0001f) return false;
    const Manro::Vec3 rayDir = glm::normalize(dir);

    BlockCoord cell = WorldToBlockCoord(origin);

    const int stepX = (rayDir.x > 0.f) ? 1 : (rayDir.x < 0.f ? -1 : 0);
    const int stepY = (rayDir.y > 0.f) ? 1 : (rayDir.y < 0.f ? -1 : 0);
    const int stepZ = (rayDir.z > 0.f) ? 1 : (rayDir.z < 0.f ? -1 : 0);

    auto nextBoundary = [&](float p, int c, int step) -> float {
        if (step > 0) return (static_cast<float>(c + 1) * kBlockSize - p);
        if (step < 0) return (p - static_cast<float>(c) * kBlockSize);
        return std::numeric_limits<float>::infinity();
    };

    const float invX = (std::abs(rayDir.x) > 1e-6f)
                           ? (1.f / std::abs(rayDir.x))
                           : std::numeric_limits<float>::infinity();
    const float invY = (std::abs(rayDir.y) > 1e-6f)
                           ? (1.f / std::abs(rayDir.y))
                           : std::numeric_limits<float>::infinity();
    const float invZ = (std::abs(rayDir.z) > 1e-6f)
                           ? (1.f / std::abs(rayDir.z))
                           : std::numeric_limits<float>::infinity();

    float tMaxX = nextBoundary(origin.x, cell.x, stepX) * invX;
    float tMaxY = nextBoundary(origin.y, cell.y, stepY) * invY;
    float tMaxZ = nextBoundary(origin.z, cell.z, stepZ) * invZ;

    const float tDeltaX = kBlockSize * invX;
    const float tDeltaY = kBlockSize * invY;
    const float tDeltaZ = kBlockSize * invZ;

    Manro::Vec3 lastNormal{0.f};

    float t = 0.f;
    while (t <= maxDistance) {
        const auto block = GetBlock(cell);
        if (block.has_value() && IsSolidBlock(*block)) {
            outHit.valid = true;
            outHit.coord = cell;
            outHit.hitPosition = origin + rayDir * t;
            outHit.hitNormal = lastNormal;
            return true;
        }

        if (tMaxX < tMaxY && tMaxX < tMaxZ) {
            cell.x += stepX;
            t = tMaxX;
            tMaxX += tDeltaX;
            lastNormal = {-static_cast<float>(stepX), 0.f, 0.f};
        } else if (tMaxY < tMaxZ) {
            cell.y += stepY;
            t = tMaxY;
            tMaxY += tDeltaY;
            lastNormal = {0.f, -static_cast<float>(stepY), 0.f};
        } else {
            cell.z += stepZ;
            t = tMaxZ;
            tMaxZ += tDeltaZ;
            lastNormal = {0.f, 0.f, -static_cast<float>(stepZ)};
        }
    }

    return false;
}

bool MinecraftDemo::TryGetPlacementCoord(BlockCoord &outCoord) const {
    if (!m_TargetBlock.valid) return false;

    const Manro::Vec3 &n = m_TargetBlock.hitNormal;
    const float ax = std::abs(n.x);
    const float ay = std::abs(n.y);
    const float az = std::abs(n.z);

    outCoord = m_TargetBlock.coord;
    if (ax >= ay && ax >= az)
        outCoord.x += (n.x >= 0.f) ? 1 : -1;
    else if (ay >= ax && ay >= az)
        outCoord.y += (n.y >= 0.f) ? 1 : -1;
    else
        outCoord.z += (n.z >= 0.f) ? 1 : -1;

    return true;
}

bool MinecraftDemo::CanPlaceBlock(const BlockCoord &coord) const {
    if (coord.y < 0 || coord.y >= kMaxBuildHeight) return false;
    if (IsOccupied(coord)) return false;

    const Manro::Vec3 center = BlockCenter(coord);

    const float dx = std::abs(center.x - m_PlayerPosition.x);
    const float dy = std::abs(center.y - m_PlayerPosition.y);
    const float dz = std::abs(center.z - m_PlayerPosition.z);

    constexpr float kEpsilon = 2.f;
    const float overlapX = (kHalfBlock + kPlayerHalfWidth - kEpsilon) - dx;
    const float overlapY = (kHalfBlock + kPlayerHalfHeight - kEpsilon) - dy;
    const float overlapZ = (kHalfBlock + kPlayerHalfWidth - kEpsilon) - dz;

    return !(overlapX > 0.f && overlapY > 0.f && overlapZ > 0.f);
}

void MinecraftDemo::DrawGui(float dt) const {
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImDrawList *draw = ImGui::GetBackgroundDrawList();
    const ImVec2 center{display.x * 0.5f, display.y * 0.5f};

    draw->AddLine({center.x - 10.f, center.y}, {center.x + 10.f, center.y},
                  IM_COL32(245, 245, 245, 230), 2.f);
    draw->AddLine({center.x, center.y - 10.f}, {center.x, center.y + 10.f},
                  IM_COL32(245, 245, 245, 230), 2.f);

    ImGui::SetNextWindowPos({16.f, 16.f}, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.72f);
    if (ImGui::Begin("HUD", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                     ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove)) {
        float avgMs = dt * 1000.f;
        float sumMs = 0.f;
        int samples = 0;
        for (float ft: m_FrameTimeHistory) {
            if (ft > 0.f) {
                sumMs += ft;
                ++samples;
            }
        }
        if (samples > 0)
            avgMs = sumMs / static_cast<float>(samples);
        const float fps = avgMs > 0.f ? 1000.f / avgMs : 0.f;

        ImGui::Text("FPS %.1f | Draws %u", fps, m_LastStats.drawCalls);
        ImGui::Text("Mode: %s", m_NoClip ? "noclip" : "survival");
        ImGui::Text("Grounded: %s", m_IsGrounded ? "yes" : "no");
        ImGui::Text("Selected: %s", BlockName(GetSelectedBlockType()));

        const auto vel = m_PhysicsWorld->GetBodyLinearVelocity(m_PlayerBody);
        ImGui::Text("Vel  %.1f  %.1f  %.1f", vel.x, vel.y, vel.z);

        if (m_TargetBlock.valid)
            ImGui::Text("Target: %d %d %d",
                        m_TargetBlock.coord.x, m_TargetBlock.coord.y, m_TargetBlock.coord.z);
        else
            ImGui::TextDisabled("Target: none");

        ImGui::Separator();
        ImGui::Text("Chunks: %zu", m_DebugStats.chunkCount);
        ImGui::Text("Physics bodies: %zu", m_DebugStats.physicsBodyCount);
        ImGui::Text("Solid blocks: %zu", m_DebugStats.solidBlockCount);
        ImGui::Text("Leaves: %zu | Water: %zu", m_DebugStats.leafBlockCount, m_DebugStats.waterBlockCount);
        ImGui::Text("Dirty render chunks: %zu", m_DebugStats.dirtyRenderChunks);
        ImGui::Text("Dirty collision chunks: %zu", m_DebugStats.dirtyCollisionChunks);
        ImGui::Text("Physics step: %.3f ms", m_DebugStats.physicsStepMs);
        ImGui::Text("Mesh rebuild: %.3f ms", m_DebugStats.chunkMeshBuildMs);
        ImGui::Text("Collision rebuild: %.3f ms", m_DebugStats.collisionBuildMs);
        ImGui::Text("Voxel raycast: %.3f ms", m_DebugStats.raycastMs);

        ImGui::TextDisabled("LMB break  RMB place  1-6 blocks  F1 noclip  F3 cam  Ctrl cursor");
    }
    ImGui::End();
}

Manro::Vec3 MinecraftDemo::GetForwardVector() const {
    const float pitchRad = glm::radians(m_Pitch);
    const float yawRad = glm::radians(m_Yaw);
    return glm::normalize(Manro::Vec3{
        cosf(pitchRad) * cosf(yawRad),
        sinf(pitchRad),
        cosf(pitchRad) * sinf(yawRad)
    });
}

Manro::Vec3 MinecraftDemo::GetEyePosition() const {
    return m_PlayerPosition + Manro::Vec3(0.f, kPlayerEyeHeight - kPlayerHalfHeight, 0.f);
}

Manro::Vec3 MinecraftDemo::BlockCenter(const BlockCoord &coord) {
    return {
        (static_cast<float>(coord.x) + 0.5f) * kBlockSize,
        (static_cast<float>(coord.y) + 0.5f) * kBlockSize,
        (static_cast<float>(coord.z) + 0.5f) * kBlockSize
    };
}

MinecraftDemo::BlockType MinecraftDemo::GetSelectedBlockType() const {
    return static_cast<BlockType>(m_SelectedBlockIndex);
}

const char *MinecraftDemo::BlockName(BlockType type) {
    switch (type) {
        case BlockType::Grass: return "Grass";
        case BlockType::Dirt: return "Dirt";
        case BlockType::Stone: return "Stone";
        case BlockType::Wood: return "Wood";
        case BlockType::Leaf: return "Leaf";
        case BlockType::Water: return "Water";
        default: return "Block";
    }
}

int MinecraftDemo::TerrainHeight(int x, int z) {
    const float fx = static_cast<float>(x);
    const float fz = static_cast<float>(z);

    float n = sinf(fx * 0.031f) * cosf(fz * 0.027f);
    n += 0.50f * sinf(fx * 0.071f + 1.3f) * cosf(fz * 0.063f + 0.9f);
    n += 0.25f * sinf(fx * 0.157f + 2.1f) * cosf(fz * 0.149f + 1.7f);
    n += 0.125f * sinf(fx * 0.311f + 0.5f) * cosf(fz * 0.293f + 2.3f);

    const float normalized = (n + 1.875f) / 3.75f;
    return 4 + static_cast<int>(normalized * 48.f);
}