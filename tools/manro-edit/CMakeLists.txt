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

target_link_libraries(manro-edit PRIVATE Manro)