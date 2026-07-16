#include "engine.h"
#include <filesystem>

namespace {

void BootLog(const char* msg)
{
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::filesystem::path logPath = std::filesystem::path(path).parent_path() / L"deepframe_boot.log";
    FILE* f = nullptr;
    if (_wfopen_s(&f, logPath.wstring().c_str(), L"a") == 0 && f)
    {
        SYSTEMTIME st{};
        GetLocalTime(&st);
        fprintf(f, "%04d-%02d-%02d %02d:%02d:%02d.%03d  %s\n",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, msg);
        fclose(f);
    }
    OutputDebugStringA(msg);
    OutputDebugStringA("\n");
}

LONG WINAPI DeepFrameUnhandled(EXCEPTION_POINTERS* ep)
{
    char buf[160];
    snprintf(buf, sizeof(buf), "UNHANDLED exception code=0x%08X at %p",
        ep && ep->ExceptionRecord ? (unsigned)ep->ExceptionRecord->ExceptionCode : 0u,
        ep && ep->ExceptionRecord ? ep->ExceptionRecord->ExceptionAddress : nullptr);
    BootLog(buf);
    MessageBoxA(nullptr, buf, "DeepFrame crash", MB_ICONERROR);
    return EXCEPTION_EXECUTE_HANDLER;
}

} // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int)
{
    SetUnhandledExceptionFilter(DeepFrameUnhandled);
    BootLog("=== DeepFrame start ===");

    // Single-instance: if already running, focus existing and exit
    HWND existing = FindWindowW(L"DeepFrame.ControlPanel", L"DeepFrame");
    if (existing)
    {
        BootLog("Existing instance found — activating");
        ShowWindow(existing, SW_RESTORE);
        SetForegroundWindow(existing);
        return 0;
    }

    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_WIN95_CLASSES | ICC_STANDARD_CLASSES };
    if (!InitCommonControlsEx(&icc))
    {
        char buf[80];
        snprintf(buf, sizeof(buf), "InitCommonControlsEx failed err=%lu (non-fatal)", GetLastError());
        BootLog(buf);
    }

    try
    {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        BootLog("WinRT apartment OK");
    }
    catch (const winrt::hresult_error& e)
    {
        char buf[96];
        snprintf(buf, sizeof(buf), "WinRT init failed 0x%08X", (unsigned)e.code());
        BootLog(buf);
        MessageBoxA(nullptr, buf, "DeepFrame", MB_ICONERROR);
        return 1;
    }

    // DPI awareness so overlay aligns with window bounds
    if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
        BootLog("DPI awareness set failed (non-fatal)");

    df::Engine engine;
    int code = 1;
    try
    {
        BootLog("Engine::Init…");
        if (!engine.Init(hInstance))
        {
            BootLog("Engine::Init returned false");
            winrt::uninit_apartment();
            return 1;
        }
        BootLog("Engine::Init OK — entering Run()");
        code = engine.Run();
        BootLog("Engine::Run returned");
        engine.Shutdown();
    }
    catch (const winrt::hresult_error& e)
    {
        char buf[128];
        snprintf(buf, sizeof(buf), "Fatal COM error: 0x%08X", static_cast<unsigned>(e.code()));
        BootLog(buf);
        MessageBoxA(nullptr, buf, "DeepFrame", MB_ICONERROR);
    }
    catch (const std::exception& e)
    {
        char buf[256];
        snprintf(buf, sizeof(buf), "Fatal std::exception: %s", e.what());
        BootLog(buf);
        MessageBoxA(nullptr, buf, "DeepFrame", MB_ICONERROR);
    }
    catch (...)
    {
        BootLog("Fatal unexpected error");
        MessageBoxW(nullptr, L"Fatal unexpected error.", L"DeepFrame", MB_ICONERROR);
    }

    winrt::uninit_apartment();
    BootLog("=== DeepFrame exit ===");
    return code;
}
