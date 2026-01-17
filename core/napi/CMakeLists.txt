cmake_minimum_required(VERSION 3.20)
project(deepframe_native LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find Node.js includes via cmake-js
include_directories(${CMAKE_JS_INC})

# Find node-addon-api
execute_process(
    COMMAND node -p "require('node-addon-api').include"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE NODE_ADDON_API_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
string(REPLACE "\"" "" NODE_ADDON_API_DIR ${NODE_ADDON_API_DIR})
include_directories(${NODE_ADDON_API_DIR})

# Include our backend headers
include_directories(${CMAKE_SOURCE_DIR}/../capture)
include_directories(${CMAKE_SOURCE_DIR}/../present)
include_directories(${CMAKE_SOURCE_DIR}/../inference)
include_directories(${CMAKE_SOURCE_DIR}/../pipeline)

# ONNX Runtime (optional - will compile without it if not found)
set(ONNXRUNTIME_DIR "" CACHE PATH "Path to ONNX Runtime")
if(ONNXRUNTIME_DIR)
    include_directories(${ONNXRUNTIME_DIR}/include)
    link_directories(${ONNXRUNTIME_DIR}/lib)
    set(ONNX_LIBS onnxruntime)
else()
    message(WARNING "ONNX Runtime not configured - AI interpolation will be disabled")
    set(ONNX_LIBS "")
endif()

# Define the native addon
add_library(${PROJECT_NAME} SHARED
    deepframe_native.cpp
    ../capture/DxgiCapture.cpp
    ../present/FramePresenter.cpp
    ../inference/OnnxInference.cpp
    ../pipeline/FramePipeline.cpp
    ${CMAKE_JS_SRC}
)

# Windows-specific settings
if(WIN32)
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        WIN32
        _WINDOWS
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        NAPI_VERSION=8
    )
    target_link_libraries(${PROJECT_NAME} PRIVATE
        d3d11
        dxgi
        d3dcompiler
        d2d1
        dwrite
        ${ONNX_LIBS}
        ${CMAKE_JS_LIB}
    )
endif()

# Set output name and extension for Node.js
set_target_properties(${PROJECT_NAME} PROPERTIES
    PREFIX ""
    SUFFIX ".node"
)

# Copy ONNX Runtime DLLs if available
if(ONNXRUNTIME_DIR)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${ONNXRUNTIME_DIR}/lib/onnxruntime.dll"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
        COMMENT "Copying ONNX Runtime DLL"
    )
endif()
