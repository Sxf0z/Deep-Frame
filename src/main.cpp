// DeepFrame — DirectComposition overlay + WGC Zero-Copy capture + Frame-Paced render loop.
//
// Architecture:
//   Main thread  → Win32 message pump only (window messages, ESC to quit)
//   Render thread → WGC frame acquisition, GPU presentation, high-res timer pacing
//   WGC worker   → CreateFreeThreaded callback signals a Win32 Event to wake render thread
//
// No compute shaders dispatched yet. The pacing skeleton is complete.

#include "pch.h"

#include <winrt/base.h>
#include <thread>
#include <vector>

namespace wgc   = winrt::Windows::Graphics::Capture;
namespace wgdx  = winrt::Windows::Graphics::DirectX;
namespace wgd3d = winrt::Windows::Graphics::DirectX::Direct3D11;

// ─────────────────────────────────────────────────────────────────────────────
// IDirect3DDxgiInterfaceAccess — COM interface for WinRT→D3D11 zero-copy bridge
// ─────────────────────────────────────────────────────────────────────────────
struct __declspec(uuid("A9B3D012-3DF2-4EE3-B8D1-8695F457D3C1"))
IDirect3DDxgiInterfaceAccess : ::IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE GetInterface(REFIID iid, void** p) = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// Utility: HRESULT enforcement
// ─────────────────────────────────────────────────────────────────────────────
static void ThrowIfFailed(HRESULT hr, const char* context)
{
    if (FAILED(hr))
    {
        char buf[256];
        snprintf(buf, sizeof(buf), "[DeepFrame] FATAL — %s failed: 0x%08lX\n", context, hr);
        OutputDebugStringA(buf);
        winrt::throw_hresult(hr);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Globals — statically pre-allocated graphics context
// ─────────────────────────────────────────────────────────────────────────────
static HWND                                     g_hwnd              = nullptr;
static winrt::com_ptr<ID3D11Device>             g_d3dDevice;
static winrt::com_ptr<ID3D11DeviceContext>      g_d3dContext;
static winrt::com_ptr<IDXGISwapChain1>          g_swapChain;
static winrt::com_ptr<IDCompositionDevice>      g_dcompDevice;
static winrt::com_ptr<IDCompositionTarget>      g_dcompTarget;
static winrt::com_ptr<IDCompositionVisual>      g_rootVisual;
static std::atomic<bool>                        g_running{ true };

// Compute Pipeline State
static winrt::com_ptr<ID3D11ComputeShader>      g_computeShader;
static winrt::com_ptr<ID3D11ComputeShader>      g_csLumaExtract;
static winrt::com_ptr<ID3D11ComputeShader>      g_csBlockMatching;
static winrt::com_ptr<ID3D11ComputeShader>      g_csInterpolate;
static winrt::com_ptr<ID3D11SamplerState>       g_linearSampler;
static winrt::com_ptr<ID3D11Buffer>             g_frameConstantsBuffer;

// Frame Generation Resources (Static VRAM Allocation)
static winrt::com_ptr<ID3D11Texture2D>          g_previousFrameLuma;
static winrt::com_ptr<ID3D11Texture2D>          g_currentFrameLuma;
static winrt::com_ptr<ID3D11Texture2D>          g_motionVectorField;
static winrt::com_ptr<ID3D11Texture2D>          g_interpolatedFrame;

static winrt::com_ptr<ID3D11ShaderResourceView> g_srvPreviousLuma;
static winrt::com_ptr<ID3D11ShaderResourceView> g_srvCurrentLuma;
static winrt::com_ptr<ID3D11ShaderResourceView> g_srvMotionVector;
static winrt::com_ptr<ID3D11ShaderResourceView> g_srvInterpolated;

static winrt::com_ptr<ID3D11UnorderedAccessView> g_uavCurrentLuma;
static winrt::com_ptr<ID3D11UnorderedAccessView> g_uavMotionVector;
static winrt::com_ptr<ID3D11UnorderedAccessView> g_uavInterpolated;

struct CBParams
{
    float InvOutputWidth;
    float InvOutputHeight;
    float Pad[2];
};

// WGC capture state
static wgd3d::IDirect3DDevice                   g_winrtDevice{ nullptr };
static wgc::Direct3D11CaptureFramePool          g_framePool{ nullptr };
static wgc::GraphicsCaptureSession              g_captureSession{ nullptr };
static wgc::GraphicsCaptureItem                 g_captureItem{ nullptr };
static wgc::Direct3D11CaptureFramePool::FrameArrived_revoker g_frameArrivedRevoker;
static std::atomic<uint64_t>                    g_capturedFrameCount{ 0 };

// Inter-thread synchronization: WGC callback → render thread
// Auto-reset event: WGC worker sets it, render thread consumes it.
static HANDLE                                   g_frameReadyEvent   = nullptr;

// Render thread handle
static std::thread                              g_renderThread;

// High-resolution performance counter frequency (ticks per second).
// Initialized once at startup, read-only thereafter.
static LARGE_INTEGER                            g_perfFreq;

// ─────────────────────────────────────────────────────────────────────────────
// Window procedure
// ─────────────────────────────────────────────────────────────────────────────
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_DESTROY:
        g_running.store(false, std::memory_order_release);
        // Wake the render thread so it can observe the shutdown flag
        if (g_frameReadyEvent) SetEvent(g_frameReadyEvent);
        PostQuitMessage(0);
        return 0;

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE)
        {
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ─────────────────────────────────────────────────────────────────────────────
// Step 1: Create the transparent click-through overlay window
// ─────────────────────────────────────────────────────────────────────────────
static HWND CreateOverlayWindow(HINSTANCE hInstance)
{
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(WNDCLASSEXW);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = L"DeepFrameOverlay";

    RegisterClassExW(&wc);

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);

    constexpr DWORD exStyle = WS_EX_LAYERED
                            | WS_EX_TRANSPARENT
                            | WS_EX_TOPMOST
                            | WS_EX_NOREDIRECTIONBITMAP;

    HWND hwnd = CreateWindowExW(
        exStyle,
        L"DeepFrameOverlay",
        L"DeepFrame",
        WS_POPUP,
        0, 0,
        screenW, screenH,
        nullptr,
        nullptr,
        hInstance,
        nullptr
    );

    if (!hwnd)
    {
        OutputDebugStringA("[DeepFrame] FATAL — CreateWindowExW failed\n");
        ExitProcess(1);
    }

    SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    UpdateWindow(hwnd);

    return hwnd;
}

// ─────────────────────────────────────────────────────────────────────────────
// Step 2: Initialize D3D11 device + DXGI SwapChain for Composition
// ─────────────────────────────────────────────────────────────────────────────
static void InitD3D11AndSwapChain()
{
    D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
    D3D_FEATURE_LEVEL achievedLevel{};

    UINT deviceFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
    deviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    winrt::com_ptr<ID3D11Device> baseDevice;
    winrt::com_ptr<ID3D11DeviceContext> baseContext;

    HRESULT hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        deviceFlags,
        featureLevels,
        _countof(featureLevels),
        D3D11_SDK_VERSION,
        baseDevice.put(),
        &achievedLevel,
        baseContext.put()
    );
    ThrowIfFailed(hr, "D3D11CreateDevice");

    g_d3dDevice = baseDevice;
    g_d3dContext = baseContext;

    winrt::com_ptr<IDXGIDevice> dxgiDevice;
    hr = g_d3dDevice->QueryInterface(__uuidof(IDXGIDevice), dxgiDevice.put_void());
    ThrowIfFailed(hr, "QueryInterface(IDXGIDevice)");

    winrt::com_ptr<IDXGIAdapter> dxgiAdapter;
    hr = dxgiDevice->GetAdapter(dxgiAdapter.put());
    ThrowIfFailed(hr, "GetAdapter");

    winrt::com_ptr<IDXGIFactory2> dxgiFactory;
    hr = dxgiAdapter->GetParent(__uuidof(IDXGIFactory2), dxgiFactory.put_void());
    ThrowIfFailed(hr, "GetParent(IDXGIFactory2)");

    DXGI_SWAP_CHAIN_DESC1 scDesc{};
    scDesc.Width              = static_cast<UINT>(GetSystemMetrics(SM_CXSCREEN));
    scDesc.Height             = static_cast<UINT>(GetSystemMetrics(SM_CYSCREEN));
    scDesc.Format             = DXGI_FORMAT_B8G8R8A8_UNORM;
    scDesc.Stereo             = FALSE;
    scDesc.SampleDesc.Count   = 1;
    scDesc.SampleDesc.Quality = 0;
    scDesc.BufferUsage        = DXGI_USAGE_RENDER_TARGET_OUTPUT | DXGI_USAGE_UNORDERED_ACCESS;
    scDesc.BufferCount        = 2;
    scDesc.Scaling            = DXGI_SCALING_STRETCH;
    scDesc.SwapEffect         = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    scDesc.AlphaMode          = DXGI_ALPHA_MODE_PREMULTIPLIED;
    scDesc.Flags              = 0;

    hr = dxgiFactory->CreateSwapChainForComposition(
        g_d3dDevice.get(),
        &scDesc,
        nullptr,
        g_swapChain.put()
    );
    ThrowIfFailed(hr, "CreateSwapChainForComposition");
}

// ─────────────────────────────────────────────────────────────────────────────
// Step 2.5: Initialize Compute Pipeline
// ─────────────────────────────────────────────────────────────────────────────
static void InitComputePipeline()
{
    auto LoadShader = [&](const wchar_t* filename, const char* entryPoint, winrt::com_ptr<ID3D11ComputeShader>& outShader)
    {
        std::vector<char> shaderData;
        FILE* f = nullptr;
        _wfopen_s(&f, filename, L"rb");
        if (f)
        {
            fseek(f, 0, SEEK_END);
            long size = ftell(f);
            fseek(f, 0, SEEK_SET);
            shaderData.resize(size);
            fread(shaderData.data(), 1, size, f);
            fclose(f);
        }
        else
        {
            winrt::com_ptr<ID3DBlob> blob;
            winrt::com_ptr<ID3DBlob> errorBlob;
            const wchar_t* hlslFile = (wcscmp(filename, L"compute_pass.cso") == 0) ? L"src/compute_pass.hlsl" : L"src/optical_flow.hlsl";
            HRESULT hr = D3DCompileFromFile(
                hlslFile, nullptr, nullptr, entryPoint, "cs_5_0",
                D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, blob.put(), errorBlob.put()
            );
            if (FAILED(hr))
            {
                if (errorBlob) OutputDebugStringA((const char*)errorBlob->GetBufferPointer());
                ThrowIfFailed(hr, "D3DCompileFromFile");
            }
            shaderData.assign((char*)blob->GetBufferPointer(), (char*)blob->GetBufferPointer() + blob->GetBufferSize());
        }

        HRESULT hr = g_d3dDevice->CreateComputeShader(
            shaderData.data(), shaderData.size(), nullptr, outShader.put()
        );
        ThrowIfFailed(hr, "CreateComputeShader");
    };

    LoadShader(L"compute_pass.cso", "CSMain", g_computeShader);
    LoadShader(L"luma_extract.cso", "CSLumaExtract", g_csLumaExtract);
    LoadShader(L"block_matching.cso", "CSBlockMatching", g_csBlockMatching);
    LoadShader(L"interpolate.cso", "CSInterpolate", g_csInterpolate);

    D3D11_SAMPLER_DESC sampDesc{};
    sampDesc.Filter         = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampDesc.AddressU       = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressV       = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressW       = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sampDesc.MinLOD         = 0;
    sampDesc.MaxLOD         = D3D11_FLOAT32_MAX;

    HRESULT hr = g_d3dDevice->CreateSamplerState(&sampDesc, g_linearSampler.put());
    ThrowIfFailed(hr, "CreateSamplerState");

    D3D11_BUFFER_DESC cbDesc{};
    cbDesc.ByteWidth      = sizeof(CBParams);
    cbDesc.Usage          = D3D11_USAGE_DYNAMIC;
    cbDesc.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    hr = g_d3dDevice->CreateBuffer(&cbDesc, nullptr, g_frameConstantsBuffer.put());
    ThrowIfFailed(hr, "CreateConstantBuffer");
}

// ─────────────────────────────────────────────────────────────────────────────
// Step 3: Build the DirectComposition visual tree
// ─────────────────────────────────────────────────────────────────────────────
// ─────────────────────────────────────────────────────────────────────────────
// Step 2.6: Initialize Frame Generation VRAM Resources
// ─────────────────────────────────────────────────────────────────────────────
static void InitFrameGenerationResources()
{
    UINT width = GetSystemMetrics(SM_CXSCREEN);
    UINT height = GetSystemMetrics(SM_CYSCREEN);

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;

    // Luma (R8_UNORM)
    desc.Format = DXGI_FORMAT_R8_UNORM;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    HRESULT hr = g_d3dDevice->CreateTexture2D(&desc, nullptr, g_currentFrameLuma.put());
    ThrowIfFailed(hr, "CreateTexture2D(CurrentLuma)");
    hr = g_d3dDevice->CreateTexture2D(&desc, nullptr, g_previousFrameLuma.put());
    ThrowIfFailed(hr, "CreateTexture2D(PreviousLuma)");

    // Motion Vector (R16G16_FLOAT)
    desc.Format = DXGI_FORMAT_R16G16_FLOAT;
    hr = g_d3dDevice->CreateTexture2D(&desc, nullptr, g_motionVectorField.put());
    ThrowIfFailed(hr, "CreateTexture2D(MotionVector)");

    // Interpolated Frame (B8G8R8A8_UNORM)
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    hr = g_d3dDevice->CreateTexture2D(&desc, nullptr, g_interpolatedFrame.put());
    ThrowIfFailed(hr, "CreateTexture2D(Interpolated)");

    // Create Views
    g_d3dDevice->CreateShaderResourceView(g_previousFrameLuma.get(), nullptr, g_srvPreviousLuma.put());
    g_d3dDevice->CreateShaderResourceView(g_currentFrameLuma.get(), nullptr, g_srvCurrentLuma.put());
    g_d3dDevice->CreateShaderResourceView(g_motionVectorField.get(), nullptr, g_srvMotionVector.put());
    g_d3dDevice->CreateShaderResourceView(g_interpolatedFrame.get(), nullptr, g_srvInterpolated.put());

    g_d3dDevice->CreateUnorderedAccessView(g_currentFrameLuma.get(), nullptr, g_uavCurrentLuma.put());
    g_d3dDevice->CreateUnorderedAccessView(g_motionVectorField.get(), nullptr, g_uavMotionVector.put());
    g_d3dDevice->CreateUnorderedAccessView(g_interpolatedFrame.get(), nullptr, g_uavInterpolated.put());
}

static void InitDirectComposition()
{
    winrt::com_ptr<IDXGIDevice> dxgiDevice;
    HRESULT hr = g_d3dDevice->QueryInterface(__uuidof(IDXGIDevice), dxgiDevice.put_void());
    ThrowIfFailed(hr, "QueryInterface(IDXGIDevice) for DComp");

    hr = DCompositionCreateDevice(
        dxgiDevice.get(),
        __uuidof(IDCompositionDevice),
        g_dcompDevice.put_void()
    );
    ThrowIfFailed(hr, "DCompositionCreateDevice");

    hr = g_dcompDevice->CreateTargetForHwnd(g_hwnd, TRUE, g_dcompTarget.put());
    ThrowIfFailed(hr, "CreateTargetForHwnd");

    hr = g_dcompDevice->CreateVisual(g_rootVisual.put());
    ThrowIfFailed(hr, "CreateVisual");

    hr = g_rootVisual->SetContent(g_swapChain.get());
    ThrowIfFailed(hr, "SetContent(SwapChain)");

    hr = g_dcompTarget->SetRoot(g_rootVisual.get());
    ThrowIfFailed(hr, "SetRoot");

    hr = g_dcompDevice->Commit();
    ThrowIfFailed(hr, "DComp Commit");
}

// ─────────────────────────────────────────────────────────────────────────────
// Step 4: WGC Zero-Copy capture — primary monitor
// ─────────────────────────────────────────────────────────────────────────────

static wgd3d::IDirect3DDevice CreateWinRTDevice(ID3D11Device* d3dDevice)
{
    winrt::com_ptr<IDXGIDevice> dxgiDevice;
    HRESULT hr = d3dDevice->QueryInterface(__uuidof(IDXGIDevice), dxgiDevice.put_void());
    ThrowIfFailed(hr, "QI(IDXGIDevice) for WinRT bridge");

    winrt::com_ptr<::IInspectable> inspectable;
    hr = CreateDirect3D11DeviceFromDXGIDevice(dxgiDevice.get(), inspectable.put());
    ThrowIfFailed(hr, "CreateDirect3D11DeviceFromDXGIDevice");

    return inspectable.as<wgd3d::IDirect3DDevice>();
}

static HMONITOR GetPrimaryMonitor()
{
    POINT origin{ 0, 0 };
    HMONITOR hMon = MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY);
    if (!hMon)
    {
        OutputDebugStringA("[DeepFrame] FATAL — Could not resolve primary monitor\n");
        ExitProcess(1);
    }
    return hMon;
}

static wgc::GraphicsCaptureItem CreateCaptureItemForMonitor(HMONITOR hMonitor)
{
    auto interopFactory = winrt::get_activation_factory<
        wgc::GraphicsCaptureItem, IGraphicsCaptureItemInterop>();

    wgc::GraphicsCaptureItem item{ nullptr };
    HRESULT hr = interopFactory->CreateForMonitor(
        hMonitor,
        winrt::guid_of<wgc::GraphicsCaptureItem>(),
        winrt::put_abi(item)
    );
    ThrowIfFailed(hr, "CreateForMonitor");

    return item;
}

// FrameArrived callback — thin signaler only.
// All extraction and GPU work happens on the render thread.
// This callback fires on the WGC free-threaded worker and must return fast.
static void OnFrameArrived(
    wgc::Direct3D11CaptureFramePool const& /*sender*/,
    winrt::Windows::Foundation::IInspectable const& /*args*/)
{
    SetEvent(g_frameReadyEvent);
}

static void InitWGCCapture()
{
    g_winrtDevice = CreateWinRTDevice(g_d3dDevice.get());

    HMONITOR hPrimary = GetPrimaryMonitor();
    g_captureItem = CreateCaptureItemForMonitor(hPrimary);

    auto itemSize = g_captureItem.Size();
    {
        char buf[128];
        snprintf(buf, sizeof(buf),
            "[DeepFrame] Capture target: %dx%d (primary monitor)\n",
            itemSize.Width, itemSize.Height);
        OutputDebugStringA(buf);
    }

    g_framePool = wgc::Direct3D11CaptureFramePool::CreateFreeThreaded(
        g_winrtDevice,
        wgdx::DirectXPixelFormat::B8G8R8A8UIntNormalized,
        2,
        itemSize
    );

    g_frameArrivedRevoker = g_framePool.FrameArrived(
        winrt::auto_revoke, OnFrameArrived);

    g_captureSession = g_framePool.CreateCaptureSession(g_captureItem);

    try { g_captureSession.IsBorderRequired(false); }
    catch (...) {}

    try { g_captureSession.IsCursorCaptureEnabled(false); }
    catch (...) {}

    g_captureSession.StartCapture();

    OutputDebugStringA("[DeepFrame] WGC capture session started (free-threaded)\n");
}

// ─────────────────────────────────────────────────────────────────────────────
// Step 5: WGC teardown
// ─────────────────────────────────────────────────────────────────────────────
static void ShutdownWGCCapture()
{
    g_frameArrivedRevoker.revoke();

    if (g_captureSession)
    {
        g_captureSession.Close();
        g_captureSession = nullptr;
    }
    if (g_framePool)
    {
        g_framePool.Close();
        g_framePool = nullptr;
    }

    g_captureItem   = nullptr;
    g_winrtDevice   = nullptr;

    char buf[96];
    snprintf(buf, sizeof(buf),
        "[DeepFrame] WGC shutdown. Total frames captured: %llu\n",
        static_cast<unsigned long long>(g_capturedFrameCount.load()));
    OutputDebugStringA(buf);
}

// ─────────────────────────────────────────────────────────────────────────────
// Step 6: Dispatch Compute Shader to Swapchain UAV
// ─────────────────────────────────────────────────────────────────────────────
static void DispatchComputeToSwapChain(ID3D11Texture2D* srcTexture)
{
    winrt::com_ptr<ID3D11Texture2D> backBuffer;
    HRESULT hr = g_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), backBuffer.put_void());
    if (FAILED(hr)) return;

    D3D11_TEXTURE2D_DESC bbDesc{};
    backBuffer->GetDesc(&bbDesc);

    winrt::com_ptr<ID3D11ShaderResourceView> srv;
    hr = g_d3dDevice->CreateShaderResourceView(srcTexture, nullptr, srv.put());
    if (FAILED(hr)) return;

    winrt::com_ptr<ID3D11UnorderedAccessView> uav;
    hr = g_d3dDevice->CreateUnorderedAccessView(backBuffer.get(), nullptr, uav.put());
    if (FAILED(hr)) return;

    D3D11_MAPPED_SUBRESOURCE mapped{};
    hr = g_d3dContext->Map(g_frameConstantsBuffer.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (SUCCEEDED(hr))
    {
        CBParams* constants = static_cast<CBParams*>(mapped.pData);
        constants->InvOutputWidth  = 1.0f / static_cast<float>(bbDesc.Width);
        constants->InvOutputHeight = 1.0f / static_cast<float>(bbDesc.Height);
        constants->Pad[0] = 0.0f;
        constants->Pad[1] = 0.0f;
        g_d3dContext->Unmap(g_frameConstantsBuffer.get(), 0);
    }

    g_d3dContext->CSSetShader(g_computeShader.get(), nullptr, 0);

    ID3D11ShaderResourceView* srvs[] = { srv.get() };
    g_d3dContext->CSSetShaderResources(0, 1, srvs);

    ID3D11SamplerState* samplers[] = { g_linearSampler.get() };
    g_d3dContext->CSSetSamplers(0, 1, samplers);

    ID3D11UnorderedAccessView* uavs[] = { uav.get() };
    g_d3dContext->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);

    ID3D11Buffer* cbs[] = { g_frameConstantsBuffer.get() };
    g_d3dContext->CSSetConstantBuffers(0, 1, cbs);

    // 1D Threadgroup distribution matching [numthreads(64, 1, 1)]
    UINT dispatchX = (bbDesc.Width + 63) / 64;
    UINT dispatchY = bbDesc.Height;
    g_d3dContext->Dispatch(dispatchX, dispatchY, 1);

    // CRITICAL: Unbind SRV and UAV
    ID3D11ShaderResourceView* nullSRV[] = { nullptr };
    g_d3dContext->CSSetShaderResources(0, 1, nullSRV);

    ID3D11UnorderedAccessView* nullUAV[] = { nullptr };
    g_d3dContext->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);

    // Present immediately
    g_swapChain->Present(0, 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// Step 7: Render loop — runs on a dedicated worker thread
//
// Pipeline per iteration:
//   A. Wait for WGC FrameArrived signal
//   B. Extract zero-copy ID3D11Texture2D
//   C. (Future) Dispatch compute shaders
//   D. Present native frame to DComp swapchain
//   E. Calculate predictive half-frame delay
//   F. Sleep via high-resolution waitable timer
//   G. Present interpolated frame (currently echoes the native frame)
// ─────────────────────────────────────────────────────────────────────────────
static void RenderLoopThread()
{
    // The render thread needs its own COM apartment for WinRT interop
    winrt::init_apartment(winrt::apartment_type::multi_threaded);

    // ── Initialize NT high-resolution waitable timer ──
    // CREATE_WAITABLE_TIMER_HIGH_RESOLUTION bypasses the standard ~15.6ms
    // coalescing clock and aligns to hardware counters (~0.1ms precision).
    HANDLE hTimer = CreateWaitableTimerExW(
        nullptr,
        nullptr,
        CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
        TIMER_ALL_ACCESS
    );
    if (!hTimer)
    {
        // Fallback for pre-1803 (should never hit given our NTDDI target)
        OutputDebugStringA("[DeepFrame] WARNING — High-res timer unavailable, falling back\n");
        hTimer = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);
    }

    // ── Frame cadence tracking via QPC ──
    LARGE_INTEGER lastFrameTime{};
    QueryPerformanceCounter(&lastFrameTime);

    // Exponential moving average of the source frame interval.
    // Initialized to 16.67ms (60Hz assumption), adapts dynamically.
    double avgFrameIntervalMs = 16.667;
    constexpr double kEmaAlpha = 0.15;  // Smoothing factor — dampens transient spikes

    uint64_t presentCount = 0;

    OutputDebugStringA("[DeepFrame] Render thread started. High-res timer active.\n");

    while (g_running.load(std::memory_order_acquire))
    {
        // ─── Step A: Wait for WGC FrameArrived signal ───
        // The WGC free-threaded worker calls SetEvent(g_frameReadyEvent)
        // when a new captured frame is available. We block here with a
        // 100ms timeout to periodically check the shutdown flag.
        DWORD waitResult = WaitForSingleObject(g_frameReadyEvent, 100);
        if (!g_running.load(std::memory_order_acquire)) break;
        if (waitResult == WAIT_TIMEOUT) continue;

        // ── Measure inter-frame interval for dynamic pacing ──
        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        double deltaMs = static_cast<double>(now.QuadPart - lastFrameTime.QuadPart) * 1000.0
                       / static_cast<double>(g_perfFreq.QuadPart);
        lastFrameTime = now;

        // Clamp to sane range before feeding EMA (reject startup transients)
        if (deltaMs > 1.0 && deltaMs < 200.0)
        {
            avgFrameIntervalMs = kEmaAlpha * deltaMs + (1.0 - kEmaAlpha) * avgFrameIntervalMs;
        }

        // ─── Step B: Extract zero-copy ID3D11Texture2D ───
        auto frame = g_framePool.TryGetNextFrame();
        if (!frame) continue;

        auto surface = frame.Surface();
        auto dxgiAccess = surface.as<IDirect3DDxgiInterfaceAccess>();

        winrt::com_ptr<ID3D11Texture2D> capturedTexture;
        HRESULT hr = dxgiAccess->GetInterface(
            __uuidof(ID3D11Texture2D),
            capturedTexture.put_void()
        );

        if (FAILED(hr) || !capturedTexture)
        {
            frame.Close();
            continue;
        }

        uint64_t frameNum = g_capturedFrameCount.fetch_add(1, std::memory_order_relaxed) + 1;

        // ─── Step C: Frame Generation Pipeline ───
        D3D11_TEXTURE2D_DESC nativeDesc{};
        capturedTexture->GetDesc(&nativeDesc);
        UINT dimX = (nativeDesc.Width + 7) / 8;
        UINT dimY = (nativeDesc.Height + 7) / 8;

        winrt::com_ptr<ID3D11ShaderResourceView> srvCaptured;
        g_d3dDevice->CreateShaderResourceView(capturedTexture.get(), nullptr, srvCaptured.put());

        // Nullify binds helper
        auto UnbindAll = []() {
            ID3D11ShaderResourceView* nullSRV[3] = { nullptr, nullptr, nullptr };
            ID3D11UnorderedAccessView* nullUAV[2] = { nullptr, nullptr };
            g_d3dContext->CSSetShaderResources(0, 3, nullSRV);
            g_d3dContext->CSSetUnorderedAccessViews(0, 2, nullUAV, nullptr);
        };

        // 1. CSLumaExtract
        g_d3dContext->CSSetShader(g_csLumaExtract.get(), nullptr, 0);
        ID3D11ShaderResourceView* srvsLuma[] = { srvCaptured.get() };
        g_d3dContext->CSSetShaderResources(0, 1, srvsLuma);
        ID3D11UnorderedAccessView* uavsLuma[] = { g_uavCurrentLuma.get() };
        g_d3dContext->CSSetUnorderedAccessViews(0, 1, uavsLuma, nullptr);
        g_d3dContext->Dispatch(dimX, dimY, 1);
        UnbindAll();

        bool hasPreviousFrame = (frameNum > 1);
        if (hasPreviousFrame)
        {
            // 2. CSBlockMatching
            g_d3dContext->CSSetShader(g_csBlockMatching.get(), nullptr, 0);
            ID3D11ShaderResourceView* srvsMatch[] = { g_srvCurrentLuma.get(), g_srvPreviousLuma.get() };
            g_d3dContext->CSSetShaderResources(0, 2, srvsMatch);
            ID3D11UnorderedAccessView* uavsMatch[] = { g_uavMotionVector.get() };
            g_d3dContext->CSSetUnorderedAccessViews(0, 1, uavsMatch, nullptr);
            g_d3dContext->Dispatch(dimX, dimY, 1);
            UnbindAll();

            // 3. CSInterpolate
            g_d3dContext->CSSetShader(g_csInterpolate.get(), nullptr, 0);
            ID3D11ShaderResourceView* srvsInterp[] = { srvCaptured.get(), g_srvMotionVector.get() };
            g_d3dContext->CSSetShaderResources(0, 2, srvsInterp);
            ID3D11SamplerState* samps[] = { g_linearSampler.get() };
            g_d3dContext->CSSetSamplers(0, 1, samps);
            ID3D11UnorderedAccessView* uavsInterp[] = { g_uavInterpolated.get() };
            g_d3dContext->CSSetUnorderedAccessViews(0, 1, uavsInterp, nullptr);
            g_d3dContext->Dispatch(dimX, dimY, 1);
            UnbindAll();
        }
        else
        {
            g_d3dContext->CopyResource(g_interpolatedFrame.get(), capturedTexture.get());
        }

        // ─── Step D: Present native frame (T) to DComp swapchain ───
        DispatchComputeToSwapChain(capturedTexture.get());

        // ─── Step E: Calculate predictive half-frame delay ───
        // For frame generation, the interpolated frame must land at the
        // temporal midpoint (T + 0.5) between two native frames.
        // Half the estimated frame interval gives us the target sleep.
        double halfFrameMs = avgFrameIntervalMs * 0.5;

        // Convert to 100-nanosecond units (NT timer granularity).
        // Negative value = relative delay from SetWaitableTimerEx call.
        LARGE_INTEGER dueTime{};
        dueTime.QuadPart = -static_cast<LONGLONG>(halfFrameMs * 10000.0);

        // ─── Step F: High-resolution sleep ───
        // SetWaitableTimerEx arms the timer; WaitForSingleObject blocks
        // until the NT kernel's hardware counter fires (~0.1ms precision).
        // CPU is fully yielded — zero spin, zero power waste.
        SetWaitableTimerEx(hTimer, &dueTime, 0, nullptr, nullptr, nullptr, 0);
        WaitForSingleObject(hTimer, INFINITE);

        if (!g_running.load(std::memory_order_acquire)) 
        {
            frame.Close();
            break;
        }

        // ─── Step G: Present interpolated frame (T+0.5) ───
        DispatchComputeToSwapChain(g_interpolatedFrame.get());

        // End of Frame: Copy Current Luma to Previous Luma
        g_d3dContext->CopyResource(g_previousFrameLuma.get(), g_currentFrameLuma.get());

        frame.Close();

        presentCount += 2;  // Two presents per captured frame (native + interpolated)

        // Periodic diagnostics
        if ((frameNum % 120) == 1)
        {
            D3D11_TEXTURE2D_DESC desc{};
            capturedTexture->GetDesc(&desc);

            char diagBuf[320];
            snprintf(diagBuf, sizeof(diagBuf),
                "[DeepFrame] Frame #%llu | %ux%u | Fmt:%u | AvgInterval:%.2fms | "
                "HalfFrame:%.2fms | Presents:%llu\n",
                static_cast<unsigned long long>(frameNum),
                desc.Width, desc.Height,
                static_cast<unsigned>(desc.Format),
                avgFrameIntervalMs,
                halfFrameMs,
                static_cast<unsigned long long>(presentCount));
            OutputDebugStringA(diagBuf);
        }
    }

    // Cleanup
    if (hTimer)
    {
        CancelWaitableTimer(hTimer);
        CloseHandle(hTimer);
    }

    winrt::uninit_apartment();
    OutputDebugStringA("[DeepFrame] Render thread exited.\n");
}

// ─────────────────────────────────────────────────────────────────────────────
// Entry point
// ─────────────────────────────────────────────────────────────────────────────
int WINAPI wWinMain(
    _In_     HINSTANCE hInstance,
    _In_opt_ HINSTANCE /*hPrevInstance*/,
    _In_     LPWSTR    /*lpCmdLine*/,
    _In_     int       /*nCmdShow*/)
{
    winrt::init_apartment(winrt::apartment_type::multi_threaded);

    // Initialize QPC frequency — immutable after this point
    QueryPerformanceFrequency(&g_perfFreq);

    // Create the inter-thread signaling event (auto-reset)
    g_frameReadyEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!g_frameReadyEvent)
    {
        OutputDebugStringA("[DeepFrame] FATAL — CreateEventW failed\n");
        return 1;
    }

    g_hwnd = CreateOverlayWindow(hInstance);
    InitD3D11AndSwapChain();
    InitComputePipeline();
    InitFrameGenerationResources();
    InitDirectComposition();
    InitWGCCapture();

    // Launch the render loop on a dedicated worker thread.
    // The D3D11 immediate context is now exclusively owned by this thread.
    g_renderThread = std::thread(RenderLoopThread);

    OutputDebugStringA("[DeepFrame] All systems initialized. Render thread active.\n");

    // ─── Main thread: Win32 message pump only ───
    // The render loop runs independently on g_renderThread.
    // This pump handles window messages (WM_DESTROY, WM_KEYDOWN, etc.)
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    // ─── Shutdown sequence ───
    g_running.store(false, std::memory_order_release);
    SetEvent(g_frameReadyEvent);  // Unblock render thread if waiting

    if (g_renderThread.joinable())
        g_renderThread.join();

    ShutdownWGCCapture();

    g_frameConstantsBuffer = nullptr;
    g_linearSampler = nullptr;
    g_computeShader = nullptr;
    g_csLumaExtract = nullptr;
    g_csBlockMatching = nullptr;
    g_csInterpolate = nullptr;

    g_srvPreviousLuma = nullptr;
    g_srvCurrentLuma = nullptr;
    g_srvMotionVector = nullptr;
    g_srvInterpolated = nullptr;

    g_uavCurrentLuma = nullptr;
    g_uavMotionVector = nullptr;
    g_uavInterpolated = nullptr;

    g_previousFrameLuma = nullptr;
    g_currentFrameLuma = nullptr;
    g_motionVectorField = nullptr;
    g_interpolatedFrame = nullptr;

    g_rootVisual    = nullptr;
    g_dcompTarget   = nullptr;
    g_dcompDevice   = nullptr;
    g_swapChain     = nullptr;
    g_d3dContext    = nullptr;
    g_d3dDevice     = nullptr;

    CloseHandle(g_frameReadyEvent);
    g_frameReadyEvent = nullptr;

    winrt::uninit_apartment();
    return 0;
}
