cmake_minimum_required(VERSION 3.20)

add_executable(DemoShooterTest
        main.cpp
        DemoShooter.cpp
)

target_compile_definitions(DemoShooterTest PRIVATE
        VK_NO_PROTOTYPES
        MANRO_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets"
)
target_compile_options(DemoShooterTest PRIVATE ${ENGINE_FLAGS})
target_link_libraries(DemoShooterTest PRIVATE Engine)
