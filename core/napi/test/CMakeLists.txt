cmake_minimum_required(VERSION 3.20)
project(test_native LANGUAGES CXX)

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

# Define the minimal test native addon
add_library(${PROJECT_NAME} SHARED
    test_native.cpp
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
        ${CMAKE_JS_LIB}
    )
endif()

# Set output name and extension for Node.js
set_target_properties(${PROJECT_NAME} PROPERTIES
    PREFIX ""
    SUFFIX ".node"
)
