


#define NAPI_VERSION 8
#include "../pipeline/FramePipeline.h"
#include <napi.h>
#include <string>


static std::string WideToUtf8(const std::wstring &wstr) {
  if (wstr.empty())
    return "";
  int size = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(),
                                 static_cast<int>(wstr.length()), nullptr, 0,
                                 nullptr, nullptr);
  std::string result(size, 0);
  WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), static_cast<int>(wstr.length()),
                      &result[0], size, nullptr, nullptr);
  return result;
}


struct WindowInfo {
  HWND hwnd;
  std::wstring title;
  std::wstring className;
};

static BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
  auto *windows = reinterpret_cast<std::vector<WindowInfo> *>(lParam);

  if (!IsWindowVisible(hwnd))
    return TRUE;

  wchar_t title[256] = {};
  wchar_t className[256] = {};
  GetWindowTextW(hwnd, title, 256);
  GetClassNameW(hwnd, className, 256);

  if (wcslen(title) == 0)
    return TRUE;
  if (wcscmp(className, L"Windows.UI.Core.CoreWindow") == 0)
    return TRUE;
  if (wcscmp(className, L"Shell_TrayWnd") == 0)
    return TRUE;
  if (wcscmp(className, L"Progman") == 0)
    return TRUE;

  windows->push_back({hwnd, title, className});
  return TRUE;
}

class DeepFrameAddon : public Napi::ObjectWrap<DeepFrameAddon> {
public:
  static Napi::Object Init(Napi::Env env, Napi::Object exports) {
    Napi::Function func = DefineClass(
        env, "DeepFrame",
        {
            InstanceMethod("initialize", &DeepFrameAddon::Initialize),
            InstanceMethod("start", &DeepFrameAddon::Start),
            InstanceMethod("stop", &DeepFrameAddon::Stop),
            InstanceMethod("getStats", &DeepFrameAddon::GetStats),
            InstanceMethod("isRunning", &DeepFrameAddon::IsRunning),
            InstanceMethod("setTargetWindow", &DeepFrameAddon::SetTargetWindow),
            InstanceMethod("getOpenWindows", &DeepFrameAddon::GetOpenWindows),
            InstanceMethod("setShowStats", &DeepFrameAddon::SetShowStats),
            InstanceMethod("setMode", &DeepFrameAddon::SetMode),
        });

    Napi::FunctionReference *constructor = new Napi::FunctionReference();
    *constructor = Napi::Persistent(func);
    env.SetInstanceData(constructor);

    exports.Set("DeepFrame", func);
    return exports;
  }

  DeepFrameAddon(const Napi::CallbackInfo &info)
      : Napi::ObjectWrap<DeepFrameAddon>(info) {}

  ~DeepFrameAddon() { pipeline_.Shutdown(); }

  
  Napi::Value Initialize(const Napi::CallbackInfo &info) {
    Napi::Env env = info.Env();

    if (pipeline_.IsInitialized()) {
      return Napi::Boolean::New(env, true);
    }

    DeepFrame::PipelineConfig config;
    config.mode = DeepFrame::InterpolationMode::FAST;
    config.showStats = true;
    config.targetWindow = nullptr;

    
    
    config.modelPath = L""; 

    if (!pipeline_.Initialize(config)) {
      return Napi::Boolean::New(env, false);
    }

    return Napi::Boolean::New(env, true);
  }

  
  Napi::Value Start(const Napi::CallbackInfo &info) {
    Napi::Env env = info.Env();

    if (!pipeline_.IsInitialized()) {
      Napi::Error::New(env, "Not initialized").ThrowAsJavaScriptException();
      return env.Undefined();
    }

    if (pipeline_.IsRunning()) {
      return Napi::Boolean::New(env, true);
    }

    
    if (info.Length() > 0 && info[0].IsObject()) {
      Napi::Object config = info[0].As<Napi::Object>();

      if (config.Has("showStats")) {
        bool show = config.Get("showStats").As<Napi::Boolean>().Value();
        pipeline_.SetShowStats(show);
      }
    }

    if (!pipeline_.Start()) {
      return Napi::Boolean::New(env, false);
    }

    return Napi::Boolean::New(env, true);
  }

  
  Napi::Value Stop(const Napi::CallbackInfo &info) {
    pipeline_.Stop();
    return Napi::Boolean::New(info.Env(), true);
  }

  
  Napi::Value GetStats(const Napi::CallbackInfo &info) {
    Napi::Env env = info.Env();

    auto stats = pipeline_.GetStats();

    Napi::Object result = Napi::Object::New(env);
    result.Set("captureFps", Napi::Number::New(env, stats.captureFps));
    result.Set("presentFps", Napi::Number::New(env, stats.presentFps));
    result.Set("inferenceTimeMs",
               Napi::Number::New(env, stats.inferenceTimeMs));
    result.Set(
        "droppedFrames",
        Napi::Number::New(env, static_cast<double>(stats.droppedFrames)));
    result.Set("vramUsageMB",
               Napi::Number::New(env, static_cast<double>(stats.vramUsageMB)));
    result.Set("e2eLatencyMs", Napi::Number::New(env, stats.e2eLatencyMs));
    
    result.Set("fps", Napi::Number::New(env, stats.presentFps));
    result.Set("latencyMs", Napi::Number::New(env, stats.inferenceTimeMs));

    return result;
  }

  Napi::Value IsRunning(const Napi::CallbackInfo &info) {
    return Napi::Boolean::New(info.Env(), pipeline_.IsRunning());
  }

  
  Napi::Value SetTargetWindow(const Napi::CallbackInfo &info) {
    Napi::Env env = info.Env();

    if (info.Length() < 1) {
      pipeline_.SetTargetWindow(nullptr);
      return Napi::Boolean::New(env, true);
    }

    if (!info[0].IsNumber()) {
      return Napi::Boolean::New(env, false);
    }

    HWND hwnd = reinterpret_cast<HWND>(info[0].As<Napi::Number>().Int64Value());
    pipeline_.SetTargetWindow(hwnd);

    return Napi::Boolean::New(env, true);
  }

  
  Napi::Value GetOpenWindows(const Napi::CallbackInfo &info) {
    Napi::Env env = info.Env();

    std::vector<WindowInfo> windows;
    EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&windows));

    Napi::Array result = Napi::Array::New(env, windows.size());
    for (size_t i = 0; i < windows.size(); i++) {
      Napi::Object winObj = Napi::Object::New(env);
      winObj.Set("hwnd", Napi::Number::New(
                             env, reinterpret_cast<int64_t>(windows[i].hwnd)));
      winObj.Set("title", Napi::String::New(env, WideToUtf8(windows[i].title)));
      winObj.Set("className",
                 Napi::String::New(env, WideToUtf8(windows[i].className)));
      result[i] = winObj;
    }

    return result;
  }

  
  Napi::Value SetShowStats(const Napi::CallbackInfo &info) {
    Napi::Env env = info.Env();

    if (info.Length() < 1 || !info[0].IsBoolean()) {
      return Napi::Boolean::New(env, false);
    }

    bool show = info[0].As<Napi::Boolean>().Value();
    pipeline_.SetShowStats(show);
    return Napi::Boolean::New(env, true);
  }

  
  Napi::Value SetMode(const Napi::CallbackInfo &info) {
    Napi::Env env = info.Env();

    if (info.Length() < 1 || !info[0].IsString()) {
      return Napi::Boolean::New(env, false);
    }

    std::string modeStr = info[0].As<Napi::String>().Utf8Value();
    DeepFrame::InterpolationMode mode = DeepFrame::InterpolationMode::FAST;

    if (modeStr == "balanced") {
      mode = DeepFrame::InterpolationMode::BALANCED;
    } else if (modeStr == "quality") {
      mode = DeepFrame::InterpolationMode::QUALITY;
    }

    
    std::wstring modelPath = L""; 

    return Napi::Boolean::New(env, pipeline_.SetMode(mode, modelPath));
  }

private:
  DeepFrame::FramePipeline pipeline_;
};

Napi::Object Init(Napi::Env env, Napi::Object exports) {
  return DeepFrameAddon::Init(env, exports);
}

NODE_API_MODULE(deepframe_native, Init)