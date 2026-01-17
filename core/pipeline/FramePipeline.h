#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "../capture/DxgiCapture.h"
#include "../inference/OnnxInference.h"
#include "../present/FramePresenter.h"
#include "RingBuffer.h"
#include <atomic>
#include <mutex>
#include <string>
#include <thread>

namespace DeepFrame {

struct PipelineStats {
  float captureFps = 0.f;
  float presentFps = 0.f;
  float inferenceTimeMs = 0.f;
  uint64_t droppedFrames = 0;
  size_t vramUsageMB = 0;
  float e2eLatencyMs = 0.f;
};

struct PipelineConfig {
  InterpolationMode mode = InterpolationMode::FAST;
  std::wstring modelPath;
  bool showStats = true;
  HWND targetWindow = nullptr;
};

class FramePipeline {
public:
  FramePipeline() noexcept = default;
  ~FramePipeline() noexcept;

  FramePipeline(const FramePipeline &) = delete;
  FramePipeline &operator=(const FramePipeline &) = delete;

  
  [[nodiscard]] bool Initialize(const PipelineConfig &config) noexcept;
  void Shutdown() noexcept;

  
  [[nodiscard]] bool Start() noexcept;
  void Stop() noexcept;

  
  void SetTargetWindow(HWND target) noexcept;
  void SetShowStats(bool show) noexcept;
  [[nodiscard]] bool SetMode(InterpolationMode mode,
                             const std::wstring &modelPath) noexcept;

  
  [[nodiscard]] PipelineStats GetStats() const noexcept;
  [[nodiscard]] bool IsRunning() const noexcept { return running_.load(); }
  [[nodiscard]] bool IsInitialized() const noexcept { return initialized_; }

private:
  
  void CaptureThread() noexcept;
  void InferenceThread() noexcept;
  void PresentThread() noexcept;
  void WorkerLoop() noexcept; 

  
  DxgiCapture capture_;
  OnnxInference inference_;
  FramePresenter presenter_;

  
  RingBuffer<3> captureBuffer_;      
  RingBuffer<3> interpolatedBuffer_; 

  
  ComPtr<ID3D11Texture2D> interpolatedFrame_;

  
  std::thread workerThread_; 
  std::thread captureThread_;
  std::thread inferenceThread_;
  std::thread presentThread_;

  
  std::atomic<bool> running_{false};
  std::atomic<bool> inferenceReady_{false};
  std::atomic<uint64_t> lastInferenceFrame_{0};

  
  mutable std::mutex statsMutex_;
  PipelineStats stats_;

  
  std::atomic<uint64_t> capturedFrames_{0};
  std::atomic<uint64_t> presentedFrames_{0};

  
  PipelineConfig config_;
  bool initialized_ = false;
};

} 