cmake_minimum_required(VERSION 3.20)

add_executable(SponzaTest
        main.cpp
        Sponza.cpp
)

target_compile_definitions(SponzaTest PRIVATE VK_NO_PROTOTYPES)
target_compile_options(SponzaTest PRIVATE ${ENGINE_FLAGS})
target_link_libraries(SponzaTest PRIVATE Engine)

add_custom_command(TARGET SponzaTest POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/assets"
        "$<TARGET_FILE_DIR:SponzaTest>/assets"
        COMMENT "Copying assets -> SponzaTest output dir"
)