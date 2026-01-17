cmake_minimum_required(VERSION 3.20)
project(DeepFrame LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ONNX Runtime path (set via environment or command line)
if(NOT DEFINED ONNXRUNTIME_DIR)
    set(ONNXRUNTIME_DIR "C:/onnxruntime" CACHE PATH "Path to ONNX Runtime")
endif()

# -----------------------------------------------------------------------------
# Capture Library (DXGI Desktop Duplication)
# -----------------------------------------------------------------------------
add_library(dxgi_capture STATIC
    capture/DxgiCapture.cpp
    capture/DxgiCapture.h
)

target_link_libraries(dxgi_capture PRIVATE d3d11 dxgi)
target_include_directories(dxgi_capture PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/capture)

# -----------------------------------------------------------------------------
# Presenter Library (Overlay Window)
# -----------------------------------------------------------------------------
add_library(frame_presenter STATIC
    present/FramePresenter.cpp
    present/FramePresenter.h
)

target_link_libraries(frame_presenter PRIVATE d3d11 d2d1 dwrite)
target_include_directories(frame_presenter PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/present)

# -----------------------------------------------------------------------------
# Inference Library (ONNX Runtime AI Models)
# -----------------------------------------------------------------------------
add_library(onnx_inference STATIC
    inference/OnnxInference.h
    inference/OnnxInference.cpp
)

target_include_directories(onnx_inference PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}/inference
    ${ONNXRUNTIME_DIR}/include
)

target_link_libraries(onnx_inference PRIVATE 
    d3d11
    ${ONNXRUNTIME_DIR}/lib/onnxruntime.lib
)

# -----------------------------------------------------------------------------
# Pipeline Library (Async Frame Processing)
# -----------------------------------------------------------------------------
add_library(frame_pipeline STATIC
    pipeline/FramePipeline.h
    pipeline/FramePipeline.cpp
)

target_include_directories(frame_pipeline PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/pipeline)
target_link_libraries(frame_pipeline PRIVATE dxgi_capture onnx_inference frame_presenter)

# -----------------------------------------------------------------------------
# Copy ONNX Runtime DLLs to output
# -----------------------------------------------------------------------------
if(EXISTS ${ONNXRUNTIME_DIR}/lib/onnxruntime.dll)
    add_custom_command(TARGET onnx_inference POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${ONNXRUNTIME_DIR}/lib/onnxruntime.dll
        $<TARGET_FILE_DIR:onnx_inference>
    )
endif()
