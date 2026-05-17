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
> It's not tested under X11 but the enngine uses SDL3 so it should work.

## Building

To build the project, use `CMakeLists.txt` in root.
Make sure you fetched submodules before.
### CMake Options

| Option                   | Default | Description                                |
|--------------------------|---------|--------------------------------------------|
| `MANRO_ENABLE_PROFILING` | `OFF`   | Enable Tracy                               |
| `MANRO_BUILD_SAMPLES`    | `ON`    | Build sample applications under `samples/` |

## Examples

See `samples` directory.
