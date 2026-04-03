cmake_minimum_required(VERSION 3.20)

add_executable(MinecraftDemo
        main.cpp
        MinecraftDemo.cpp
)

target_compile_definitions(MinecraftDemo PRIVATE
        VK_NO_PROTOTYPES
        MANRO_ASSETS_DIR="${MANRO_ASSETS_DIR}"
)
target_compile_options(MinecraftDemo PRIVATE ${ENGINE_FLAGS})
target_link_libraries(MinecraftDemo PRIVATE Engine)
