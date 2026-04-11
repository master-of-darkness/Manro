cmake_minimum_required(VERSION 3.20)

set(SPONZA_ASSETS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/assets")

add_executable(SponzaTest
        main.cpp
        Sponza.cpp
)

target_compile_definitions(SponzaTest PRIVATE
        MANRO_ASSETS_DIR="${SPONZA_ASSETS_DIR}"
)
target_compile_options(SponzaTest PRIVATE ${ENGINE_FLAGS})
target_link_libraries(SponzaTest PRIVATE Engine)
