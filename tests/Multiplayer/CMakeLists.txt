cmake_minimum_required(VERSION 3.20)

add_executable(MultiplayerTest
        main.cpp
        Game.cpp
        Scene.cpp
)

target_compile_definitions(MultiplayerTest PRIVATE VK_NO_PROTOTYPES)
target_compile_options(MultiplayerTest PRIVATE ${ENGINE_FLAGS})
target_link_libraries(MultiplayerTest PRIVATE Engine)

add_custom_command(TARGET MultiplayerTest POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/assets"
        "$<TARGET_FILE_DIR:MultiplayerTest>/assets"
        COMMENT "Copying assets -> MultiplayerTest output dir"
)