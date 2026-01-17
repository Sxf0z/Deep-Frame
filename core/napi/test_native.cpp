
#define NAPI_VERSION 8
#include <napi.h>

Napi::String HelloWorld(const Napi::CallbackInfo &info) {
  return Napi::String::New(info.Env(), "Hello from native module!");
}

Napi::Object Init(Napi::Env env, Napi::Object exports) {
  exports.Set("hello", Napi::Function::New(env, HelloWorld));
  return exports;
}

NODE_API_MODULE(test_native, Init)