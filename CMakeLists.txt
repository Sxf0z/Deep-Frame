cmake_minimum_required(VERSION 3.24)
project(DeepFrame LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# MSVC-specific flags: conformance mode, fast FP, full optimizations in Release
if(MSVC)
    add_compile_options(/permissive- /fp:fast /W4 /EHsc)
    add_compile_options("$<$<CONFIG:Release>:/O2;/Oi;/GL>")
    add_link_options("$<$<CONFIG:Release>:/LTCG>")
endif()

add_executable(DeepFrame WIN32
    src/main.cpp
)

target_compile_definitions(DeepFrame PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE
    WINRT_LEAN_AND_MEAN
)

# Core DirectX, DXGI, DirectComposition, DWM, WinRT, and shader compiler libraries
target_link_libraries(DeepFrame PRIVATE
    d3d11.lib
    dxgi.lib
    dcomp.lib
    dwmapi.lib
    d3dcompiler.lib         # D3DCompileFromFile for runtime HLSL compilation
    windowsapp.lib          # C++/WinRT projection runtime and WGC interop
)

# ─── HLSL Compute Shader compilation ───
# Compile .hlsl → .cso at build time using fxc.exe.
# The CSO is placed next to the executable so LoadCompiledShader can find it.

# Locate fxc.exe from Windows SDK
# CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION is set by the VS generator
find_program(FXC_EXECUTABLE fxc
    PATHS
        "$ENV{WindowsSdkDir}/bin/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/x64"
        "$ENV{WindowsSdkVerBinPath}/x64"
        "C:/Program Files (x86)/Windows Kits/10/bin/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/x64"
)

if(FXC_EXECUTABLE)
    set(HLSL_SOURCE "${CMAKE_SOURCE_DIR}/src/compute_pass.hlsl")
    set(CSO_OUTPUT  "${CMAKE_BINARY_DIR}/$<CONFIG>/compute_pass.cso")

    set(OF_SOURCE   "${CMAKE_SOURCE_DIR}/src/optical_flow.hlsl")

    add_custom_command(
        OUTPUT  "${CMAKE_BINARY_DIR}/compute_pass.cso"
        COMMAND "${FXC_EXECUTABLE}" /T cs_5_0 /E CSMain /O3 /Fo "${CMAKE_BINARY_DIR}/compute_pass.cso" "$<$<CONFIG:Debug>:/Zi>" "${HLSL_SOURCE}"
        DEPENDS "${HLSL_SOURCE}"
    )
    add_custom_command(
        OUTPUT  "${CMAKE_BINARY_DIR}/luma_extract.cso"
        COMMAND "${FXC_EXECUTABLE}" /T cs_5_0 /E CSLumaExtract /O3 /Fo "${CMAKE_BINARY_DIR}/luma_extract.cso" "$<$<CONFIG:Debug>:/Zi>" "${OF_SOURCE}"
        DEPENDS "${OF_SOURCE}"
    )
    add_custom_command(
        OUTPUT  "${CMAKE_BINARY_DIR}/block_matching.cso"
        COMMAND "${FXC_EXECUTABLE}" /T cs_5_0 /E CSBlockMatching /O3 /Fo "${CMAKE_BINARY_DIR}/block_matching.cso" "$<$<CONFIG:Debug>:/Zi>" "${OF_SOURCE}"
        DEPENDS "${OF_SOURCE}"
    )
    add_custom_command(
        OUTPUT  "${CMAKE_BINARY_DIR}/interpolate.cso"
        COMMAND "${FXC_EXECUTABLE}" /T cs_5_0 /E CSInterpolate /O3 /Fo "${CMAKE_BINARY_DIR}/interpolate.cso" "$<$<CONFIG:Debug>:/Zi>" "${OF_SOURCE}"
        DEPENDS "${OF_SOURCE}"
    )

    add_custom_target(CompileShaders ALL
        DEPENDS "${CMAKE_BINARY_DIR}/compute_pass.cso"
                "${CMAKE_BINARY_DIR}/luma_extract.cso"
                "${CMAKE_BINARY_DIR}/block_matching.cso"
                "${CMAKE_BINARY_DIR}/interpolate.cso"
    )
    add_dependencies(DeepFrame CompileShaders)

    # Copy CSOs next to the executable for each config
    add_custom_command(TARGET DeepFrame POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_BINARY_DIR}/compute_pass.cso"
            "${CMAKE_BINARY_DIR}/luma_extract.cso"
            "${CMAKE_BINARY_DIR}/block_matching.cso"
            "${CMAKE_BINARY_DIR}/interpolate.cso"
            "$<TARGET_FILE_DIR:DeepFrame>/"
    )
else()
    message(WARNING "fxc.exe not found — HLSL will be compiled at runtime via D3DCompileFromFile fallback")
endif()
