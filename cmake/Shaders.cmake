add_executable(slangc IMPORTED GLOBAL)
add_library(slang SHARED IMPORTED GLOBAL)

if(WIN32)
    set_target_properties(slangc PROPERTIES IMPORTED_LOCATION "${slang_bin_SOURCE_DIR}/bin/slangc.exe")
    set_target_properties(slang PROPERTIES
            IMPORTED_LOCATION "${slang_bin_SOURCE_DIR}/bin/slang.dll"
            IMPORTED_IMPLIB "${slang_bin_SOURCE_DIR}/lib/slang.lib")
    set(SLANG_EXTRA_LIB_DIR "${slang_bin_SOURCE_DIR}/bin")
    set(_SLANG_ENV_VAR "PATH")
    set(_SLANG_ENV_SEP ";")
else()
    set_target_properties(slangc PROPERTIES IMPORTED_LOCATION "${slang_bin_SOURCE_DIR}/bin/slangc")
    set_target_properties(slang PROPERTIES IMPORTED_LOCATION "${slang_bin_SOURCE_DIR}/lib/libslang.so")
    set(SLANG_EXTRA_LIB_DIR "${slang_bin_SOURCE_DIR}/lib")
    set(_SLANG_ENV_VAR "LD_LIBRARY_PATH")
    set(_SLANG_ENV_SEP ":")
endif ()

target_include_directories(slang INTERFACE "${slang_bin_SOURCE_DIR}/include")

set(SHADER_SRC_DIR "${CMAKE_SOURCE_DIR}/shaders/source")
set(SHADER_BIN_DIR "${CMAKE_BINARY_DIR}/shaders/spv")
file(MAKE_DIRECTORY "${SHADER_BIN_DIR}")

set(_EMBED_SCRIPT "${CMAKE_SOURCE_DIR}/cmake/embed_spv.cmake")

macro(compile_shader SHADER_FILE ENTRY_POINT STAGE OUTPUT_FILE)
    add_custom_command(
            OUTPUT "${OUTPUT_FILE}"
            COMMAND ${CMAKE_COMMAND} -E env
            "${_SLANG_ENV_VAR}=$<TARGET_FILE_DIR:slangc>${_SLANG_ENV_SEP}${SLANG_EXTRA_LIB_DIR}"
            $<TARGET_FILE:slangc> "${SHADER_SRC_DIR}/${SHADER_FILE}"
            -entry ${ENTRY_POINT} -stage ${STAGE} -target spirv -I "${SHADER_SRC_DIR}" -o "${OUTPUT_FILE}"
            DEPENDS "${SHADER_SRC_DIR}/${SHADER_FILE}" slangc
            COMMENT "Compiling shader: ${SHADER_FILE} [${ENTRY_POINT}]"
    )
    list(APPEND PRECOMPILED_SHADERS "${OUTPUT_FILE}")
endmacro()

macro(embed_shader SHADER_SPV_FILE VAR_NAME)
    set(_header "${SHADER_BIN_DIR}/${VAR_NAME}.h")
    add_custom_command(
            OUTPUT "${_header}"
            COMMAND ${CMAKE_COMMAND}
            -D INPUT=${SHADER_SPV_FILE}
            -D VAR_NAME=${VAR_NAME}
            -D OUTPUT=${_header}
            -P "${_EMBED_SCRIPT}"
            DEPENDS "${SHADER_SPV_FILE}" "${_EMBED_SCRIPT}"
            COMMENT "Embedding shader: ${VAR_NAME}"
    )
    list(APPEND EMBEDDED_SHADER_HEADERS "${_header}")
endmacro()

compile_shader("pbr.slang" "VSMain" "vertex"   "${SHADER_BIN_DIR}/pbr.vert.spv")
compile_shader("pbr.slang" "PSMain" "fragment" "${SHADER_BIN_DIR}/pbr.frag.spv")
compile_shader("forward_plus_cull.slang" "main" "compute" "${SHADER_BIN_DIR}/forward_plus_cull.comp.spv")
compile_shader("composite.slang" "VSMain" "vertex"   "${SHADER_BIN_DIR}/composite.vert.spv")
compile_shader("composite.slang" "PSMain" "fragment" "${SHADER_BIN_DIR}/composite.frag.spv")
compile_shader("gizmo.slang" "VSMain" "vertex"   "${SHADER_BIN_DIR}/gizmo.vert.spv")
compile_shader("gizmo.slang" "PSMain" "fragment" "${SHADER_BIN_DIR}/gizmo.frag.spv")
compile_shader("mesh_cull.slang" "main" "compute" "${SHADER_BIN_DIR}/mesh_cull.comp.spv")
compile_shader("shadow_depth.slang" "VSMain" "vertex" "${SHADER_BIN_DIR}/shadow_depth.vert.spv")
compile_shader("imgui.slang" "VSMain" "vertex"   "${SHADER_BIN_DIR}/imgui.vert.spv")
compile_shader("imgui.slang" "PSMain" "fragment" "${SHADER_BIN_DIR}/imgui.frag.spv")
compile_shader("skybox.slang" "VSMain" "vertex" "${SHADER_BIN_DIR}/skybox.vert.spv")
compile_shader("skybox.slang" "PSMain" "fragment" "${SHADER_BIN_DIR}/skybox.frag.spv")
compile_shader("nvshaders/tonemapper.slang" "Tonemap"      "compute" "${SHADER_BIN_DIR}/tonemapper.comp.spv")
compile_shader("nvshaders/tonemapper.slang" "Histogram"    "compute" "${SHADER_BIN_DIR}/tonemapper_histogram.comp.spv")
compile_shader("nvshaders/tonemapper.slang" "AutoExposure" "compute" "${SHADER_BIN_DIR}/tonemapper_autoexposure.comp.spv")
compile_shader("line_expand.slang" "main" "compute" "${SHADER_BIN_DIR}/line_expand.comp.spv")

add_custom_target(PrecompileShaders DEPENDS ${PRECOMPILED_SHADERS})

embed_shader("${SHADER_BIN_DIR}/pbr.vert.spv" pbr_vert_spv)
embed_shader("${SHADER_BIN_DIR}/pbr.frag.spv" pbr_frag_spv)
embed_shader("${SHADER_BIN_DIR}/forward_plus_cull.comp.spv" forward_plus_cull_comp_spv)
embed_shader("${SHADER_BIN_DIR}/composite.vert.spv" composite_vert_spv)
embed_shader("${SHADER_BIN_DIR}/composite.frag.spv" composite_frag_spv)
embed_shader("${SHADER_BIN_DIR}/tonemapper.comp.spv" tonemapper_comp_spv)
embed_shader("${SHADER_BIN_DIR}/tonemapper_histogram.comp.spv" tonemapper_histogram_comp_spv)
embed_shader("${SHADER_BIN_DIR}/tonemapper_autoexposure.comp.spv" tonemapper_autoexposure_comp_spv)
embed_shader("${SHADER_BIN_DIR}/mesh_cull.comp.spv" mesh_cull_comp_spv)
embed_shader("${SHADER_BIN_DIR}/shadow_depth.vert.spv" shadow_depth_vert_spv)
embed_shader("${SHADER_BIN_DIR}/imgui.vert.spv" imgui_vert_spv)
embed_shader("${SHADER_BIN_DIR}/imgui.frag.spv" imgui_frag_spv)
embed_shader("${SHADER_BIN_DIR}/skybox.vert.spv" skybox_vert_spv)
embed_shader("${SHADER_BIN_DIR}/skybox.frag.spv" skybox_frag_spv)
embed_shader("${SHADER_BIN_DIR}/gizmo.vert.spv" gizmo_vert_spv)
embed_shader("${SHADER_BIN_DIR}/gizmo.frag.spv" gizmo_frag_spv)
embed_shader("${SHADER_BIN_DIR}/line_expand.comp.spv" line_expand_comp_spv)

add_custom_target(EmbedShaders DEPENDS ${EMBEDDED_SHADER_HEADERS})