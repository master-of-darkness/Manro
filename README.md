# Manro Engine

Manro is a game engine developed as a **research project**.

The engine is designed with a focus on embedded network support and rendering through the Vulkan API.

## Features

- Renderer: Vulkan API, Slang shaders, and GPU-driven bindless system
- Virtual File System (VFS) with URI scheme support (e.g., shaders://)
- Entity-Component System (ECS)
- Basic UDP game network layer
- Jolt physics
- Linux and Windows support with SDL3

## Getting Started
### Prerequisites

- C++20 compatible compiler (Tested with GCC and MSVC)
- Windows or Ubuntu (tested on 25.10)
- CMake
- Vulkan SDK (1.4+)
### Building

To build the project, use `CMakeLists.txt` in root.

This will fetch and build dependencies. They are gonna be linked statically to libEngine.
## Examples

See `tests`
