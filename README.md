# Manro Engine

![Vulkan](https://img.shields.io/badge/Vulkan-1.4+-AC162C?logo=vulkan&logoColor=white)
![Platform](https://img.shields.io/badge/Platform-Linux%20%7C%20Windows-0078D4?logo=windows&logoColor=white)
![CMake](https://img.shields.io/badge/Build-CMake-064F8C?logo=cmake&logoColor=white)
![Compiler](https://img.shields.io/badge/Compiler-Clang-F05033?logo=llvm&logoColor=white)
![SDL3](https://img.shields.io/badge/Windowing-SDL3-1C2333?logo=sdl&logoColor=white)

Manro is a game engine developed as a **research project**.

The engine is designed with a focus on embedded network support and rendering through the Vulkan API.

## Features

- Renderer: Vulkan API, Slang shaders, and GPU-driven system
- Virtual File System with URI scheme support (e.g., shaders://)
- Entity-Component System (ECS)
- Basic UDP game network layer
- Jolt physics
- Linux and Windows support with SDL3

## Getting Started
### Prerequisites

- Clang compiler
- Windows or Ubuntu (tested on 25.10)
- CMake
- Vulkan SDK (1.4+)
### Building

To build the project, use `CMakeLists.txt` in root.
Make sure you fetched submodules before.
## Examples

See `samples` directory.
