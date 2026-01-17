{
  "targets": [{
    "target_name": "deepframe_native",
    "cflags!": ["-fno-exceptions"],
    "cflags_cc!": ["-fno-exceptions"],
    "sources": [
      "deepframe_native.cpp",
      "../capture/DxgiCapture.cpp",
      "../framegen/FrameBuffer.cpp",
      "../framegen/FrameGenerator.cpp",
      "../present/FramePresenter.cpp"
    ],
    "include_dirs": [
      "<!@(node -p \"require('node-addon-api').include\")",
      "../capture",
      "../framegen",
      "../present"
    ],
    "libraries": [
      "d3d11.lib",
      "dxgi.lib",
      "d3dcompiler.lib"
    ],
    "defines": ["NAPI_VERSION=8"],
    "msvs_settings": {
      "VCCLCompilerTool": {
        "ExceptionHandling": 1,
        "AdditionalOptions": ["/std:c++17"]
      }
    },
    "conditions": [
      ["OS=='win'", {
        "defines": ["WIN32", "_WINDOWS", "NOMINMAX", "WIN32_LEAN_AND_MEAN"]
      }]
    ],
    "copies": [{
      "destination": "<(PRODUCT_DIR)/shaders",
      "files": [
        "../shaders/diff_mask.hlsl",
        "../shaders/predict_track.hlsl",
        "../shaders/warp_blend.hlsl"
      ]
    }]
  }]
}
