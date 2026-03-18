cmake_minimum_required(VERSION 3.20)

add_executable(SponzaTest
        main.cpp
        Sponza.cpp
)

target_compile_definitions(SponzaTest PRIVATE
        VK_NO_PROTOTYPES
        MANRO_ASSETS_DIR="${CMAKE_SOURCE_DIR}"
)
target_compile_options(SponzaTest PRIVATE ${ENGINE_FLAGS})
target_link_libraries(SponzaTest PRIVATE Engine)
