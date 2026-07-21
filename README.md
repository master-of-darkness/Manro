# Manro Engine

![Vulkan](https://img.shields.io/badge/Vulkan-1.4+-AC162C?logo=vulkan&logoColor=white)
![Platform](https://img.shields.io/badge/Platform-Linux%20%7C%20Windows-0078D4?logo=windows&logoColor=white)

The engine is designed with a focus on rendering through the Vulkan API.

## Features

- Renderer: Vulkan API, Slang shaders, and GPU-driven system
- Virtual File System with URI scheme support (e.g., shaders://)
- Jolt physics
- Linux and Windows support with SDL3

## Getting Started
### Prerequisites

- Clang compiler
- Windows or Ubuntu (tested on 25.10 and 26.04)
- CMake
- Vulkan SDK (1.4+)

> [!NOTE]  
> It's not tested under X11 but the engine uses SDL3 so it should work.

### Building

Make sure submodules are fetched first:

```bash
git submodule update --init --recursive
```

Configure and build with Ninja:

```bash
cmake -S . -B build -G Ninja
cmake --build build -j$(nproc)
```

The engine builds as a static library (`libManro.a`); samples and tools link
against it.

### CMake Options

| Option                   | Default | Description                                |
|--------------------------|---------|--------------------------------------------|
| `MANRO_ENABLE_PROFILING` | `OFF`   | Enable Tracy                               |
| `MANRO_BUILD_SAMPLES`    | `ON`    | Build sample applications under `samples/` |

### Simple App

An app is a class that implements `Manro::IApplication` and is handed to
`Manro::CEngineLoop::Run`. The loop owns the window, renderer, VFS, job system
and world, and pumps `OnUpdate` / `OnRender` every frame.

`MyApp.h`:

```cpp
#pragma once
#include <Manro/Interfaces/IApplication.h>

class CMyApp final : public Manro::IApplication {
public:
    Manro::WindowDesc_t GetWindowDesc() const override;
    void OnStartup(const Manro::InitContext_t &ctx) override;
    void OnShutdown() override;
    bool OnUpdate(const Manro::FrameContext_t &ctx, const Manro::UserCmd_t &cmd) override;
    void OnRender(Manro::FrameContext_t &frame) override;
};
```

`MyApp.cpp`:

```cpp
#include "MyApp.h"
#include <Manro/Platform/Window/Window.h>

Manro::WindowDesc_t CMyApp::GetWindowDesc() const {
    Manro::WindowDesc_t d;
    d.Title = "MyApp";
    d.Width = 1280;
    d.Height = 720;
    return d;
}

void CMyApp::OnStartup(const Manro::InitContext_t &ctx) {
    // ctx gives you the window, renderer, VFS, job system and world.
    // Load assets, build your scene here.
}

void CMyApp::OnShutdown() {
    // Release anything you allocated.
}

bool CMyApp::OnUpdate(const Manro::FrameContext_t &ctx, const Manro::UserCmd_t &) {
    // Game logic. Return false to quit.
    return true;
}

void CMyApp::OnRender(Manro::FrameContext_t &frame) {
    // Draw via ctx renderer (capture it in OnStartup).
}
```

`main.cpp`:

```cpp
#include "MyApp.h"
#include <Manro/Core/EngineLoop.h>

int main() {
    CMyApp app;
    Manro::CEngineLoop::Run(app);
}
```

The `InitContext_t` members you receive in `OnStartup`:

| Member       | Purpose|
|--------------|--------|
| `CWindow`    | The SDL3 window|
| `CJobSystem` | Thread pool for async work |
| `CRenderer`  | Vulkan renderer|
| `CVirtualFS` | Asset filesystem|
| `CWorld`     | ECS world |

### CMake target

```cmake
add_executable(MyApp main.cpp MyApp.cpp)
target_link_libraries(MyApp PRIVATE Manro)
```

## Examples

The `samples/` directory (e.g. Sponza) is a full app built on the same
`IApplication` interface. Use it as a reference for renderer/VFS/physics usage.
