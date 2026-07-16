cmake_minimum_required(VERSION 3.24)
project(DeepFrame LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/permissive- /fp:fast /W4 /EHsc /utf-8)
    add_compile_options("$<$<CONFIG:Release>:/O2;/Oi;/GL>")
    add_link_options("$<$<CONFIG:Release>:/LTCG>")
endif()

add_executable(DeepFrame WIN32
    src/main.cpp
    src/engine.cpp
    src/graphics.cpp
    src/capture.cpp
    src/frame_gen.cpp
    src/ui.cpp
    src/fps_hud.cpp
)

target_include_directories(DeepFrame PRIVATE src)
target_compile_definitions(DeepFrame PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE WINRT_LEAN_AND_MEAN)

target_link_libraries(DeepFrame PRIVATE
    d3d11.lib dxgi.lib dcomp.lib dwmapi.lib d3dcompiler.lib windowsapp.lib comctl32.lib)

# Enable Common Controls v6 (buttons/listboxes look correct; InitCommonControlsEx succeeds)
if(MSVC)
    target_sources(DeepFrame PRIVATE src/app.manifest)
endif()

find_program(FXC_EXECUTABLE fxc
    PATHS
        "$ENV{WindowsSdkDir}/bin/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/x64"
        "$ENV{WindowsSdkVerBinPath}/x64"
        "C:/Program Files (x86)/Windows Kits/10/bin/*/x64"
)

set(SHADER_DIR "${CMAKE_SOURCE_DIR}/src/shaders")
set(SHADER_OUT_DIR "${CMAKE_BINARY_DIR}/shaders_cso")
file(MAKE_DIRECTORY "${SHADER_OUT_DIR}")

set(SHADER_LIST
    luma_extract
    luma_scaled
    luma_down
    optical_flow
    flow_filter
    interpolate
    upscale
    blit
)

set(CSO_FILES "")
if(FXC_EXECUTABLE)
    foreach(name IN LISTS SHADER_LIST)
        set(SRC "${SHADER_DIR}/${name}.hlsl")
        set(OUT "${SHADER_OUT_DIR}/${name}.cso")
        add_custom_command(
            OUTPUT "${OUT}"
            COMMAND "${FXC_EXECUTABLE}" /T cs_5_0 /E CSMain /O3 /Fo "${OUT}" "${SRC}"
            DEPENDS "${SRC}"
            COMMENT "FXC ${name}.hlsl"
            VERBATIM
        )
        list(APPEND CSO_FILES "${OUT}")
    endforeach()
    add_custom_target(CompileShaders ALL DEPENDS ${CSO_FILES})
    add_dependencies(DeepFrame CompileShaders)
    add_custom_command(TARGET DeepFrame POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CSO_FILES} "$<TARGET_FILE_DIR:DeepFrame>/"
        COMMENT "Copy shaders next to DeepFrame.exe")
else()
    message(WARNING "fxc.exe not found — runtime HLSL fallback")
endif()
