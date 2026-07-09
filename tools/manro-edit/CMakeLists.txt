add_executable(manro-edit
        main.cpp
        Editor.cpp
        FlyCamera.cpp
)

target_include_directories(manro-edit PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${MANRO_THIRD_PARTY_DIR}/imguizmo
        ${MANRO_THIRD_PARTY_DIR}/json
)
target_include_directories(manro-edit SYSTEM PRIVATE
        ${MANRO_THIRD_PARTY_DIR}/imgui
        ${MANRO_THIRD_PARTY_DIR}/imgui/backends
        ${MANRO_THIRD_PARTY_DIR}/imgui/misc/freetype
)

target_link_libraries(manro-edit PRIVATE Manro)

add_custom_command(TARGET manro-edit POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets"
        "$<TARGET_FILE_DIR:manro-edit>/assets"
)