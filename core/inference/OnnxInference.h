#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <d3d11.h>
#include <memory>
#include <string>
#include <vector>
#include <wrl/client.h>


#if __has_include(<onnxruntime_cxx_api.h>)
#define HAS_ONNX 1
#include <onnxruntime_cxx_api.h>
#endif

namespace DeepFrame {

using Microsoft::WRL::ComPtr;

enum class InterpolationMode {
  FAST,     
  BALANCED, 
  QUALITY   
};

struct InferenceStats {
  float lastInferenceMs = 0.f;
  uint64_t totalFrames = 0;
  uint64_t droppedFrames = 0;
  size_t vramUsageMB = 0;
};

class OnnxInference {
public:
  OnnxInference() noexcept = default;
  ~OnnxInference() noexcept;

  OnnxInference(const OnnxInference &) = delete;
  OnnxInference &operator=(const OnnxInference &) = delete;

  
  
  [[nodiscard]] bool
  Initialize(ID3D11Device *device, const std::wstring &modelPath,
             InterpolationMode mode = InterpolationMode::FAST) noexcept;

  void Shutdown() noexcept;

  
  
  [[nodiscard]] bool Interpolate(ID3D11Texture2D *frameA,
                                 ID3D11Texture2D *frameB,
                                 ID3D11Texture2D *output,
                                 float t = 0.5f) noexcept;

  [[nodiscard]] float GetTimeBudgetMs() const noexcept;
  [[nodiscard]] const InferenceStats &GetStats() const noexcept {
    return stats_;
  }
  [[nodiscard]] bool IsInitialized() const noexcept { return initialized_; }
  [[nodiscard]] InterpolationMode GetMode() const noexcept { return mode_; }

  [[nodiscard]] bool SetMode(InterpolationMode mode,
                             const std::wstring &modelPath) noexcept;

private:
  [[nodiscard]] bool TextureToTensor(ID3D11Texture2D *texture,
                                     std::vector<float> &tensorData) noexcept;
  [[nodiscard]] bool TensorToTexture(const std::vector<float> &tensorData,
                                     ID3D11Texture2D *texture) noexcept;

  ID3D11Device *device_ = nullptr;
  ID3D11DeviceContext *context_ = nullptr;

#ifdef HAS_ONNX
  std::unique_ptr<Ort::Env> env_;
  std::unique_ptr<Ort::Session> session_;
  Ort::AllocatorWithDefaultOptions allocator_;
#endif

  
  ComPtr<ID3D11Texture2D> gpuInputA_;
  ComPtr<ID3D11Texture2D> gpuInputB_;
  ComPtr<ID3D11Texture2D> gpuOutput_;

  
  ComPtr<ID3D11Texture2D> stagingTextureA_;
  ComPtr<ID3D11Texture2D> stagingTextureB_;
  ComPtr<ID3D11Texture2D> stagingOutput_;

  std::vector<float> inputTensorA_;
  std::vector<float> inputTensorB_;
  std::vector<float> outputTensor_;

  InterpolationMode mode_ = InterpolationMode::FAST;
  InferenceStats stats_;

  uint32_t width_ = 0;
  uint32_t height_ = 0;
  bool initialized_ = false;
};

} 