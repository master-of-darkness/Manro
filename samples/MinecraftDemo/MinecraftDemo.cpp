#include "MinecraftDemo.h"

#include <Manro/Resource/Primitives.h>
#include <Manro/Render/DebugDraw.h>

#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <cmath>

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
}

size_t MinecraftDemo::BlockCoordHash::operator()(const BlockCoord &coord) const {
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
    Manro::VirtualFS::Get().SetBaseDir(MANRO_ASSETS_DIR);
    m_InputManager.SetBackend(&m_InputBackend);

    m_PhysicsWorld = Manro::CreateScope<Manro::PhysicsWorld>();
    LoadAssets();
    GenerateWorld();

    const float spawnY = static_cast<float>(TerrainHeight(0, 0)) * kBlockSize + kPlayerHalfHeight + 20.f;
    m_PlayerPosition = {0.f, spawnY, 0.f};
    Manro::PhysicsWorld::DynamicBodyDesc playerBodyDesc{};
    playerBodyDesc.friction = 0.f;
    playerBodyDesc.restitution = 0.f;
    playerBodyDesc.convexRadius = 2.5f;
    playerBodyDesc.allowSleeping = false;
    playerBodyDesc.lockRotation = true;
    playerBodyDesc.maxLinearVelocity = 50000.f;
    playerBodyDesc.maxAngularVelocity = 0.f;
    m_PlayerBody = m_PhysicsWorld->AddDynamicBox(
        m_PlayerPosition, {kPlayerHalfWidth, kPlayerHalfHeight, kPlayerHalfWidth}, playerBodyDesc);
    if (m_PlayerBody == Manro::kInvalidBodyHandle) {
        LOG_ERROR("[MinecraftDemo] Failed to create player physics body.");
    }

    LOG_INFO(
        "[MinecraftDemo] Ready. WASD move, mouse look, LMB break, RMB place, 1-5 select block, Space jump.");
}

void MinecraftDemo::OnShutdown() {
    m_BlockMaterials = {};
    m_WorldBlocks.clear();
    m_BodyToCoord.clear();
    m_PhysicsWorld.reset();
}

bool MinecraftDemo::OnUpdate(const Manro::FrameContext &ctx, const Manro::UserCmd & /*cmd*/) {
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
        const Manro::Vec3 forwardFlat = glm::normalize(Manro::Vec3{cosf(yawRad), 0.f, sinf(yawRad)});
        const Manro::Vec3 right = glm::normalize(Manro::Vec3{-sinf(yawRad), 0.f, cosf(yawRad)});
        const Manro::Vec3 up = {0.f, 1.f, 0.f};

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
            Manro::Vec3 moveDir{0.f};
            if (m_InputManager.IsKeyDown(K::W)) moveDir += forwardFlat;
            if (m_InputManager.IsKeyDown(K::S)) moveDir -= forwardFlat;
            if (m_InputManager.IsKeyDown(K::D)) moveDir += right;
            if (m_InputManager.IsKeyDown(K::A)) moveDir -= right;
            if (glm::length(moveDir) > 0.001f) moveDir = glm::normalize(moveDir);

            m_IsGrounded = m_PhysicsWorld->IsGrounded(m_PlayerBody);
            float targetVy = m_PhysicsWorld->GetBodyLinearVelocity(m_PlayerBody).y;
            if (m_IsGrounded && targetVy < 0.f) targetVy = 0.f;

            if (m_IsGrounded && jumpPressed) {
                targetVy = m_JumpVelocity;
                m_IsGrounded = false;
            }

            m_PhysicsWorld->SetLinearVelocity(m_PlayerBody, {moveDir.x * speed, targetVy, moveDir.z * speed});
        }

        m_SpaceWasDown = spaceDown;
    } else {
        m_InputManager.ConsumeMouseDelta();
        m_SpaceWasDown = false;
    }

    m_PhysicsWorld->Step(dt);
    if (m_PlayerBody != Manro::kInvalidBodyHandle)
        m_PlayerPosition = m_PhysicsWorld->GetBodyPosition(m_PlayerBody);

    UpdateSelectionInput();
    RefreshTargetBlock(GetEyePosition(), GetForwardVector());
    UpdateInteraction(GetEyePosition(), GetForwardVector(), dt);
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
    const Manro::Mat4 proj = glm::perspective(glm::radians(kFov), m_Renderer->GetAspectRatio(), kNearZ, kFarZ);
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

    for (const auto &[coord, block]: m_WorldBlocks) {
        glm::mat4 model = glm::translate(glm::mat4(1.f), BlockCenter(coord));
        model = glm::scale(model, Manro::Vec3(kHalfBlock));
        m_Renderer->DrawMesh(m_CubeMesh, *m_BlockMaterials[static_cast<size_t>(block.type)], model);
    }

    if (m_TargetBlock.valid) {
        const Manro::Vec3 center = BlockCenter(m_TargetBlock.coord);
        glm::mat4 model = glm::translate(glm::mat4(1.f), center);
        model = glm::scale(model, Manro::Vec3(kHalfBlock + 3.f));
        m_Renderer->DebugBox({0.f, 0.f, 0.f}, {1.f, 1.f, 1.f}, model, 0xFFF7E27Du);
    }

    if (m_ShowPhysics) m_PhysicsWorld->DrawDebug(*m_Renderer);

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
    auto skyFaces = Manro::TextureLoader::LoadCubemap("skyboxes/cubemap_sky.png");
    if (!skyFaces.empty()) {
        const auto h = m_Renderer->UploadCubemap(skyFaces);
        m_Renderer->SetSkybox(h);
    }

    m_CubeMesh = m_Renderer->UploadMesh(Manro::Primitives::CreateCube(2.0f));
    for (size_t i = 0; i < m_BlockMaterials.size(); ++i) {
        auto material = m_Renderer->CreateMaterialInstance(m_Renderer->GetDefaultMaterial());
        material->ModifyData().pbrBaseColorFactor = BlockColor(static_cast<BlockType>(i));
        m_BlockMaterials[i] = std::move(material);
    }
}

void MinecraftDemo::GenerateWorld() {
    for (int x = kWorldMin; x <= kWorldMax; ++x)
        for (int z = kWorldMin; z <= kWorldMax; ++z)
            BuildTerrainColumn(x, z);

    LOG_INFO("[MinecraftDemo] Generated {} voxel blocks.", m_WorldBlocks.size());
}

void MinecraftDemo::BuildTerrainColumn(int x, int z) {
    const int height = TerrainHeight(x, z);
    for (int y = 0; y < height; ++y) {
        BlockType type = BlockType::Stone;
        if (y == height - 1) type = BlockType::Grass;
        else if (y >= height - 3) type = BlockType::Dirt;
        SetBlock({x, y, z}, type);
    }

    TrySpawnTree(x, z, height);
}

void MinecraftDemo::TrySpawnTree(int x, int z, int groundHeight) {
    const int hash = std::abs(x * 7349 + z * 9151 + x * z * 193);
    if (groundHeight < 3 || (hash % 11) != 0) return;
    if (std::abs(x) < 2 && std::abs(z) < 2) return;

    const int trunkHeight = 3 + (hash % 2);
    for (int i = 0; i < trunkHeight; ++i)
        SetBlock({x, groundHeight + i, z}, BlockType::Wood);

    const int leafBase = groundHeight + trunkHeight - 1;
    for (int lx = -2; lx <= 2; ++lx) {
        for (int lz = -2; lz <= 2; ++lz) {
            for (int ly = 0; ly <= 2; ++ly) {
                const int dist = std::abs(lx) + std::abs(lz) + ly;
                if (dist > 4) continue;
                if (lx == 0 && lz == 0 && ly < 2) continue;
                SetBlock({x + lx, leafBase + ly, z + lz}, BlockType::Leaf);
            }
        }
    }
}

bool MinecraftDemo::SetBlock(const BlockCoord &coord, BlockType type) {
    if (coord.y < 0 || coord.y >= kMaxBuildHeight) return false;
    if (IsOccupied(coord)) return false;

    const Manro::Vec3 center = BlockCenter(coord);
    Manro::PhysicsWorld::StaticBodyDesc blockBodyDesc{};
    blockBodyDesc.friction = 0.f;
    blockBodyDesc.restitution = 0.f;
    blockBodyDesc.convexRadius = 0.01f;
    const auto body = m_PhysicsWorld->AddStaticBox(center, {kHalfBlock, kHalfBlock, kHalfBlock}, blockBodyDesc);
    if (body == Manro::kInvalidBodyHandle) {
        LOG_WARN("[MinecraftDemo] Failed to create collider for block at ({}, {}, {}).", coord.x, coord.y, coord.z);
    } else {
        m_BodyToCoord[body.packed] = coord;
    }
    m_WorldBlocks.emplace(coord, BlockData{type, body});
    return true;
}

bool MinecraftDemo::RemoveBlock(const BlockCoord &coord) {
    const auto it = m_WorldBlocks.find(coord);
    if (it == m_WorldBlocks.end()) return false;

    if (it->second.body != Manro::kInvalidBodyHandle) {
        m_PhysicsWorld->RemoveBody(it->second.body);
        m_BodyToCoord.erase(it->second.body.packed);
    }
    m_WorldBlocks.erase(it);
    return true;
}

bool MinecraftDemo::IsOccupied(const BlockCoord &coord) const {
    return m_WorldBlocks.contains(coord);
}

void MinecraftDemo::UpdateSelectionInput() {
    const std::array<Manro::Key, 5> keys{
        Manro::Key::Num1, Manro::Key::Num2, Manro::Key::Num3, Manro::Key::Num4, Manro::Key::Num5
    };

    for (size_t i = 0; i < keys.size(); ++i) {
        const bool down = m_InputManager.IsKeyDown(keys[i]);
        if (down && !m_NumberWasDown[i]) m_SelectedBlockIndex = static_cast<int>(i);
        m_NumberWasDown[i] = down;
    }
}

void MinecraftDemo::UpdateInteraction(const Manro::Vec3 &eyePos, const Manro::Vec3 &forward, float /*dt*/) {
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
    m_TargetBlock = {};

    Manro::PhysicsWorld::RaycastHit hit{};
    if (!m_PhysicsWorld->RaycastClosest(eyePos, forward, kReachDistance, hit, m_PlayerBody)) return;

    const auto it = m_BodyToCoord.find(hit.body.packed);
    if (it == m_BodyToCoord.end()) return;

    m_TargetBlock.valid = true;
    m_TargetBlock.coord = it->second;
    m_TargetBlock.hitPosition = hit.position;
}

bool MinecraftDemo::TryGetPlacementCoord(BlockCoord &outCoord) const {
    if (!m_TargetBlock.valid) return false;

    const Manro::Vec3 center = BlockCenter(m_TargetBlock.coord);
    const Manro::Vec3 local = m_TargetBlock.hitPosition - center;

    const float ax = std::abs(local.x);
    const float ay = std::abs(local.y);
    const float az = std::abs(local.z);

    outCoord = m_TargetBlock.coord;
    if (ax >= ay && ax >= az) outCoord.x += (local.x >= 0.f) ? 1 : -1;
    else if (ay >= ax && ay >= az) outCoord.y += (local.y >= 0.f) ? 1 : -1;
    else outCoord.z += (local.z >= 0.f) ? 1 : -1;

    return true;
}

bool MinecraftDemo::CanPlaceBlock(const BlockCoord &coord) const {
    if (coord.y < 0 || coord.y >= kMaxBuildHeight) return false;
    if (IsOccupied(coord)) return false;

    const Manro::Vec3 center = BlockCenter(coord);
    const float dx = std::abs(center.x - m_PlayerPosition.x);
    const float dy = std::abs(center.y - m_PlayerPosition.y);
    const float dz = std::abs(center.z - m_PlayerPosition.z);

    return !(dx < (kHalfBlock + kPlayerHalfWidth) &&
             dy < (kHalfBlock + kPlayerHalfHeight) &&
             dz < (kHalfBlock + kPlayerHalfWidth));
}

void MinecraftDemo::DrawGui(float dt) const {
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImDrawList *draw = ImGui::GetBackgroundDrawList();
    const ImVec2 center{display.x * 0.5f, display.y * 0.5f};

    draw->AddLine({center.x - 8.f, center.y}, {center.x + 8.f, center.y}, IM_COL32(245, 245, 245, 230), 2.f);
    draw->AddLine({center.x, center.y - 8.f}, {center.x, center.y + 8.f}, IM_COL32(245, 245, 245, 230), 2.f);

    ImGui::SetNextWindowPos({16.f, 16.f}, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.72f);
    if (ImGui::Begin("Minecraft HUD", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                     ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove)) {
        const float fps = dt > 0.f ? 1.f / dt : 0.f;
        ImGui::Text("FPS %.1f | Blocks %zu | Draws %u", fps, m_WorldBlocks.size(), m_LastStats.drawCalls);
        ImGui::Text("%s", m_NoClip ? "Mode: noclip" : "Mode: survival-ish");
        ImGui::Text("Selected: %s", BlockName(GetSelectedBlockType()));
        if (m_TargetBlock.valid)
            ImGui::Text("Target: %d %d %d", m_TargetBlock.coord.x, m_TargetBlock.coord.y, m_TargetBlock.coord.z);
        else
            ImGui::TextDisabled("Target: none");
        ImGui::TextDisabled("LMB break  RMB place  1-5 blocks  F1 noclip  F3 camera  Ctrl cursor");
    }
    ImGui::End();

    const float slotW = 116.f;
    const float slotH = 42.f;
    const float gap = 8.f;
    const float totalW = slotW * 5.f + gap * 4.f;
    const float startX = display.x * 0.5f - totalW * 0.5f;
    const float y = display.y - 72.f;
    ImFont *font = ImGui::GetFont();
    const float labelFontSize = ImGui::GetFontSize() * 0.84f;

    for (int i = 0; i < 5; ++i) {
        const bool selected = (i == m_SelectedBlockIndex);
        const ImVec2 a{startX + i * (slotW + gap), y};
        const ImVec2 b{a.x + slotW, a.y + slotH};
        const auto color = BlockColor(static_cast<BlockType>(i));
        const ImU32 fill = IM_COL32(static_cast<int>(color.r * 255.f),
                                    static_cast<int>(color.g * 255.f),
                                    static_cast<int>(color.b * 255.f), 220);

        draw->AddRectFilled(a, b, IM_COL32(26, 27, 30, 210), 6.f);
        draw->AddRect(a, b, selected ? IM_COL32(255, 241, 166, 255) : IM_COL32(110, 110, 110, 255), 6.f, 0,
                      selected ? 3.f : 1.5f);
        draw->AddRectFilled({a.x + 8.f, a.y + 8.f}, {a.x + 28.f, a.y + 28.f}, fill, 4.f);
        const char *label = BlockName(static_cast<BlockType>(i));
        const ImVec2 labelSize = font->CalcTextSizeA(labelFontSize, FLT_MAX, 0.0f, label);
        draw->AddText(font, labelFontSize,
                      {a.x + 36.f, a.y + 7.f + (20.f - labelSize.y) * 0.5f},
                      IM_COL32(245, 245, 245, 255), label);
        char digit[2] = {static_cast<char>('1' + i), '\0'};
        draw->AddText({a.x + 8.f, a.y + 30.f}, IM_COL32(210, 210, 210, 255), digit);
    }
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

Manro::Vec3 MinecraftDemo::BlockCenter(const BlockCoord &coord) const {
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
        case BlockType::Grass:
            return "Grass";
        case BlockType::Dirt:
            return "Dirt";
        case BlockType::Stone:
            return "Stone";
        case BlockType::Wood:
            return "Wood";
        case BlockType::Leaf:
            return "Leaf";
        default:
            return "Block";
    }
}

Manro::Vec4 MinecraftDemo::BlockColor(BlockType type) {
    switch (type) {
        case BlockType::Grass:
            return {0.42f, 0.69f, 0.28f, 1.f};
        case BlockType::Dirt:
            return {0.50f, 0.32f, 0.18f, 1.f};
        case BlockType::Stone:
            return {0.58f, 0.60f, 0.63f, 1.f};
        case BlockType::Wood:
            return {0.56f, 0.40f, 0.21f, 1.f};
        case BlockType::Leaf:
            return {0.24f, 0.52f, 0.19f, 1.f};
        default:
            return {1.f, 1.f, 1.f, 1.f};
    }
}

int MinecraftDemo::TerrainHeight(int x, int z) {
    const float fx = static_cast<float>(x) * 0.55f;
    const float fz = static_cast<float>(z) * 0.45f;
    const float wave = std::sinf(fx) + std::cosf(fz * 1.2f) + std::sinf((fx + fz) * 0.7f);
    const int height = 3 + static_cast<int>(std::floor((wave + 2.5f) * 0.8f));
    return std::clamp(height, 2, 6);
}
