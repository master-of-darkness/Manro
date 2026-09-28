cmake_minimum_required(VERSION 3.20)

add_executable(VoxelTest
        main.cpp
)

target_include_directories(VoxelTest SYSTEM PRIVATE
        ${MANRO_THIRD_PARTY_DIR}/imgui
)
target_link_libraries(VoxelTest PRIVATE Manro)
