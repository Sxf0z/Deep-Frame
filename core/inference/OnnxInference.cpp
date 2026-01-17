

#include "OnnxInference.h"
#include <algorithm>
#include <chrono>
#include <cstdio>


namespace DeepFrame {

OnnxInference::~OnnxInference() noexcept { Shutdown(); }

#ifdef HAS_ONNX


bool OnnxInference::Initialize(ID3D11Device *device,
                               const std::wstring &modelPath,
                               InterpolationMode mode) noexcept {
  if (initialized_)
    return true;
  if (modelPath.empty())
    return false;

  device_ = device;
  device_->GetImmediateContext(&context_);
  mode_ = mode;

  try {
    env_ = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "DeepFrame");

    Ort::SessionOptions opts;
    opts.SetIntraOpNumThreads(1);
    opts.SetGraphOptimizationLevel(ORT_ENABLE_ALL);

    
    
    OrtDmlDeviceOptions dmlOpts = {};
    dmlOpts.device_id = 0;
    opts.AppendExecutionProvider_DML(dmlOpts);

    
    OrtCUDAProviderOptions cudaOpts{};
    cudaOpts.device_id = 0;
    cudaOpts.arena_extend_strategy = 0;
    cudaOpts.gpu_mem_limit = 512 * 1024 * 1024; 
    cudaOpts.cudnn_conv_algo_search = OrtCudnnConvAlgoSearchExhaustive;

    try {
      opts.AppendExecutionProvider_CUDA(cudaOpts);
    } catch (...) {
      
    }

    session_ = std::make_unique<Ort::Session>(*env_, modelPath.c_str(), opts);

    
    auto inputInfo = session_->GetInputTypeInfo(0);
    auto tensorInfo = inputInfo.GetTensorTypeAndShapeInfo();
    auto shape = tensorInfo.GetShape();

    if (shape.size() >= 4) {
      height_ = static_cast<uint32_t>(shape[2] > 0 ? shape[2] : 1080);
      width_ = static_cast<uint32_t>(shape[3] > 0 ? shape[3] : 1920);
    } else {
      height_ = 1080;
      width_ = 1920;
    }

    
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width_;
    desc.Height = height_;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT; 
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

    if (FAILED(device_->CreateTexture2D(&desc, nullptr, &gpuInputA_)) ||
        FAILED(device_->CreateTexture2D(&desc, nullptr, &gpuInputB_)) ||
        FAILED(device_->CreateTexture2D(&desc, nullptr, &gpuOutput_))) {
      printf("[OnnxInference] Failed to create GPU textures\n");
      return false;
    }

    initialized_ = true;
    printf("[OnnxInference] Initialized with GPU-resident tensors\n");
    return true;

  } catch (const Ort::Exception &e) {
    printf("[OnnxInference] ONNX Error: %s\n", e.what());
    return false;
  } catch (...) {
    printf("[OnnxInference] Unknown error\n");
    return false;
  }
}

void OnnxInference::Shutdown() noexcept {
  session_.reset();
  env_.reset();
  gpuInputA_.Reset();
  gpuInputB_.Reset();
  gpuOutput_.Reset();
  stagingTextureA_.Reset();
  stagingTextureB_.Reset();
  stagingOutput_.Reset();
  inputTensorA_.clear();
  inputTensorB_.clear();
  outputTensor_.clear();
  initialized_ = false;
}

bool OnnxInference::Interpolate(ID3D11Texture2D *frameA,
                                ID3D11Texture2D *frameB,
                                ID3D11Texture2D *output, float t) noexcept {
  if (!initialized_ || !session_ || !frameA || !frameB || !output) {
    return false;
  }

  auto startTime = std::chrono::high_resolution_clock::now();

  try {
    
    context_->CopyResource(gpuInputA_.Get(), frameA);
    context_->CopyResource(gpuInputB_.Get(), frameB);
    context_->Flush(); 

    
    
    if (!TextureToTensor(frameA, inputTensorA_))
      return false;
    if (!TextureToTensor(frameB, inputTensorB_))
      return false;

    std::array<int64_t, 4> inputShape = {1, 3, static_cast<int64_t>(height_),
                                         static_cast<int64_t>(width_)};
    auto memoryInfo =
        Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

    Ort::Value inputTensorOrtA = Ort::Value::CreateTensor<float>(
        memoryInfo, inputTensorA_.data(), inputTensorA_.size(),
        inputShape.data(), inputShape.size());
    Ort::Value inputTensorOrtB = Ort::Value::CreateTensor<float>(
        memoryInfo, inputTensorB_.data(), inputTensorB_.size(),
        inputShape.data(), inputShape.size());

    auto inputName0 = session_->GetInputNameAllocated(0, allocator_);
    auto inputName1 = session_->GetInputNameAllocated(1, allocator_);
    auto outputName = session_->GetOutputNameAllocated(0, allocator_);

    const char *inputNames[] = {inputName0.get(), inputName1.get()};
    const char *outputNames[] = {outputName.get()};

    std::vector<Ort::Value> inputTensors;
    inputTensors.push_back(std::move(inputTensorOrtA));
    inputTensors.push_back(std::move(inputTensorOrtB));

    auto outputTensors =
        session_->Run(Ort::RunOptions{nullptr}, inputNames, inputTensors.data(),
                      inputTensors.size(), outputNames, 1);

    float *outputData = outputTensors[0].GetTensorMutableData<float>();
    size_t outputSize = 3 * height_ * width_;
    std::copy(outputData, outputData + outputSize, outputTensor_.begin());

    if (!TensorToTexture(outputTensor_, output))
      return false;

    auto endTime = std::chrono::high_resolution_clock::now();
    stats_.lastInferenceMs =
        std::chrono::duration<float, std::milli>(endTime - startTime).count();
    stats_.totalFrames++;

    if (stats_.lastInferenceMs > GetTimeBudgetMs()) {
      stats_.droppedFrames++;
      return false;
    }

    return true;
  } catch (...) {
    stats_.droppedFrames++;
    return false;
  }
}

#else


bool OnnxInference::Initialize(ID3D11Device *, const std::wstring &,
                               InterpolationMode) noexcept {
  printf("[OnnxInference] ONNX Runtime not available - AI interpolation "
         "disabled\n");
  return false;
}

void OnnxInference::Shutdown() noexcept { initialized_ = false; }

bool OnnxInference::Interpolate(ID3D11Texture2D *, ID3D11Texture2D *,
                                ID3D11Texture2D *, float) noexcept {
  return false;
}

#endif 



float OnnxInference::GetTimeBudgetMs() const noexcept {
  switch (mode_) {
  case InterpolationMode::FAST:
    return 8.0f;
  case InterpolationMode::BALANCED:
    return 12.0f;
  case InterpolationMode::QUALITY:
    return 20.0f;
  default:
    return 8.0f;
  }
}

bool OnnxInference::SetMode(InterpolationMode mode,
                            const std::wstring &modelPath) noexcept {
  Shutdown();
  return Initialize(device_, modelPath, mode);
}

bool OnnxInference::TextureToTensor(ID3D11Texture2D *texture,
                                    std::vector<float> &tensorData) noexcept {
  if (!texture || !context_)
    return false;

  
  if (!stagingTextureA_) {
    D3D11_TEXTURE2D_DESC stagingDesc = {};
    stagingDesc.Width = width_;
    stagingDesc.Height = height_;
    stagingDesc.MipLevels = 1;
    stagingDesc.ArraySize = 1;
    stagingDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    stagingDesc.SampleDesc.Count = 1;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (FAILED(device_->CreateTexture2D(&stagingDesc, nullptr,
                                        &stagingTextureA_))) {
      return false;
    }

    size_t tensorSize = 3 * height_ * width_;
    inputTensorA_.resize(tensorSize);
    inputTensorB_.resize(tensorSize);
    outputTensor_.resize(tensorSize);
  }

  context_->CopyResource(stagingTextureA_.Get(), texture);

  D3D11_MAPPED_SUBRESOURCE mapped;
  if (FAILED(context_->Map(stagingTextureA_.Get(), 0, D3D11_MAP_READ, 0,
                           &mapped))) {
    return false;
  }

  const uint8_t *src = static_cast<const uint8_t *>(mapped.pData);
  size_t channelSize = height_ * width_;

  for (uint32_t y = 0; y < height_; y++) {
    const uint8_t *row = src + y * mapped.RowPitch;
    for (uint32_t x = 0; x < width_; x++) {
      size_t pixelIdx = y * width_ + x;
      tensorData[0 * channelSize + pixelIdx] = row[x * 4 + 2] / 255.0f;
      tensorData[1 * channelSize + pixelIdx] = row[x * 4 + 1] / 255.0f;
      tensorData[2 * channelSize + pixelIdx] = row[x * 4 + 0] / 255.0f;
    }
  }

  context_->Unmap(stagingTextureA_.Get(), 0);
  return true;
}

bool OnnxInference::TensorToTexture(const std::vector<float> &tensorData,
                                    ID3D11Texture2D *texture) noexcept {
  if (!texture || !context_)
    return false;

  
  if (!stagingOutput_) {
    D3D11_TEXTURE2D_DESC stagingDesc = {};
    stagingDesc.Width = width_;
    stagingDesc.Height = height_;
    stagingDesc.MipLevels = 1;
    stagingDesc.ArraySize = 1;
    stagingDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    stagingDesc.SampleDesc.Count = 1;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(
            device_->CreateTexture2D(&stagingDesc, nullptr, &stagingOutput_))) {
      return false;
    }
  }

  D3D11_MAPPED_SUBRESOURCE mapped;
  if (FAILED(context_->Map(stagingOutput_.Get(), 0, D3D11_MAP_WRITE, 0,
                           &mapped))) {
    return false;
  }

  uint8_t *dst = static_cast<uint8_t *>(mapped.pData);
  size_t channelSize = height_ * width_;

  for (uint32_t y = 0; y < height_; y++) {
    uint8_t *row = dst + y * mapped.RowPitch;
    for (uint32_t x = 0; x < width_; x++) {
      size_t pixelIdx = y * width_ + x;
      float r = tensorData[0 * channelSize + pixelIdx];
      float g = tensorData[1 * channelSize + pixelIdx];
      float b = tensorData[2 * channelSize + pixelIdx];

      row[x * 4 + 0] =
          static_cast<uint8_t>(std::min(255.0f, std::max(0.0f, b * 255.0f)));
      row[x * 4 + 1] =
          static_cast<uint8_t>(std::min(255.0f, std::max(0.0f, g * 255.0f)));
      row[x * 4 + 2] =
          static_cast<uint8_t>(std::min(255.0f, std::max(0.0f, r * 255.0f)));
      row[x * 4 + 3] = 255;
    }
  }

  context_->Unmap(stagingOutput_.Get(), 0);
  context_->CopyResource(texture, stagingOutput_.Get());
  return true;
}

} 