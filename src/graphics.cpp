#include "graphics.h"
#include <filesystem>
#include <cwctype>

namespace df {
namespace {

struct alignas(16) CBData
{
    float invOutW, invOutH;
    float invInW, invInH;
    float sharpness;
    float pad[3];
};

bool ReadFileBytes(const std::wstring& path, std::vector<char>& out)
{
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || !f)
        return false;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); return false; }
    out.resize(static_cast<size_t>(sz));
    size_t n = fread(out.data(), 1, out.size(), f);
    fclose(f);
    return n == out.size();
}

} // namespace

std::wstring Graphics::ExeDirectory()
{
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    return std::filesystem::path(path).parent_path().wstring() + L"\\";
}

std::vector<GpuInfo> EnumerateGpus()
{
    std::vector<GpuInfo> list;
    winrt::com_ptr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), factory.put_void())))
        return list;

    for (UINT i = 0; ; ++i)
    {
        winrt::com_ptr<IDXGIAdapter1> adapter;
        if (factory->EnumAdapters1(i, adapter.put()) == DXGI_ERROR_NOT_FOUND)
            break;

        DXGI_ADAPTER_DESC1 desc{};
        adapter->GetDesc1(&desc);

        // Skip software / remote WARP adapters
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
            continue;

        GpuInfo g;
        g.index = (int)i;
        g.name = desc.Description;
        g.vendorId = desc.VendorId;
        g.deviceId = desc.DeviceId;
        g.dedicatedBytes = (size_t)desc.DedicatedVideoMemory;
        g.isNvidia = (desc.VendorId == 0x10DE);
        g.isIntel  = (desc.VendorId == 0x8086);
        g.isAmd    = (desc.VendorId == 0x1002);
        list.push_back(std::move(g));
    }
    return list;
}

int FindPreferredGpuIndex(const std::vector<GpuInfo>& gpus)
{
    if (gpus.empty()) return 0;

    auto containsCI = [](const std::wstring& s, const wchar_t* sub) -> bool
    {
        std::wstring a = s, b = sub;
        for (auto& c : a) c = (wchar_t)towlower(c);
        for (auto& c : b) c = (wchar_t)towlower(c);
        return a.find(b) != std::wstring::npos;
    };

    // 1) Exact-ish match: RTX 3050 Ti (or 3050)
    for (auto& g : gpus)
        if (g.isNvidia && (containsCI(g.name, L"3050 ti") || containsCI(g.name, L"3050")))
            return g.index;

    // 2) Any NVIDIA with real VRAM (discrete)
    for (auto& g : gpus)
        if (g.isNvidia && g.dedicatedBytes > (512ull << 20))
            return g.index;

    // 3) Any non-Intel with dedicated VRAM
    for (auto& g : gpus)
        if (!g.isIntel && g.dedicatedBytes > (512ull << 20))
            return g.index;

    // 4) First NVIDIA even if low VRAM reporting
    for (auto& g : gpus)
        if (g.isNvidia)
            return g.index;

    return gpus.front().index;
}

bool Graphics::Init(int adapterIndex)
{
    auto gpus = EnumerateGpus();
    if (adapterIndex < 0)
        adapterIndex = FindPreferredGpuIndex(gpus);
    return CreateDevice(adapterIndex);
}

bool Graphics::RecreateDevice(int adapterIndex)
{
    std::lock_guard lock(m_ctxMutex);
    DestroyOverlayTarget();
    ReleaseDeviceResources();
    return CreateDevice(adapterIndex);
}

void Graphics::ReleaseDeviceResources()
{
    m_csLuma = nullptr;
    m_csLumaScaled = nullptr;
    m_csLumaDown = nullptr;
    m_csFlow = nullptr;
    m_csFlowFilter = nullptr;
    m_csInterp = nullptr;
    m_csUpscale = nullptr;
    m_csBlit = nullptr;
    m_linear = nullptr;
    m_cb = nullptr;
    m_factory = nullptr;
    m_ctx = nullptr;
    m_device = nullptr;
    m_adapterName.clear();
}

void Graphics::Shutdown()
{
    std::lock_guard lock(m_ctxMutex);
    DestroyOverlayTarget();
    ReleaseDeviceResources();
}

bool Graphics::CreateDevice(int adapterIndex)
{
    D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0
    };
    D3D_FEATURE_LEVEL got{};
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;

    winrt::com_ptr<IDXGIFactory1> factory1;
    ThrowIfFailed(CreateDXGIFactory1(__uuidof(IDXGIFactory1), factory1.put_void()), "CreateDXGIFactory1");

    winrt::com_ptr<IDXGIAdapter1> chosen;
    DXGI_ADAPTER_DESC1 chosenDesc{};
    {
        winrt::com_ptr<IDXGIAdapter1> a;
        if (SUCCEEDED(factory1->EnumAdapters1((UINT)adapterIndex, a.put())))
        {
            chosen = a;
            chosen->GetDesc1(&chosenDesc);
        }
        else
        {
            // Fallback to first hardware adapter
            for (UINT i = 0; factory1->EnumAdapters1(i, a.put()) != DXGI_ERROR_NOT_FOUND; ++i, a = nullptr)
            {
                DXGI_ADAPTER_DESC1 d{};
                a->GetDesc1(&d);
                if (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
                chosen = a;
                chosenDesc = d;
                adapterIndex = (int)i;
                break;
            }
        }
    }

    HRESULT hr = E_FAIL;
    if (chosen)
    {
        // When passing an adapter, driver type MUST be UNKNOWN
        hr = D3D11CreateDevice(
            chosen.get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, flags,
            levels, _countof(levels), D3D11_SDK_VERSION,
            m_device.put(), &got, m_ctx.put());
    }

    if (FAILED(hr))
    {
        Log("Adapter %d create failed 0x%08X — falling back to default HW", adapterIndex, (unsigned)hr);
        hr = D3D11CreateDevice(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
            levels, _countof(levels), D3D11_SDK_VERSION,
            m_device.put(), &got, m_ctx.put());
        if (FAILED(hr))
        {
            hr = D3D11CreateDevice(
                nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags,
                levels, _countof(levels), D3D11_SDK_VERSION,
                m_device.put(), &got, m_ctx.put());
            ThrowIfFailed(hr, "D3D11CreateDevice");
            m_adapterName = L"WARP (software)";
            m_adapterIndex = -1;
        }
        else
        {
            winrt::com_ptr<IDXGIDevice> dxgiDev;
            m_device->QueryInterface(__uuidof(IDXGIDevice), dxgiDev.put_void());
            winrt::com_ptr<IDXGIAdapter> ad;
            dxgiDev->GetAdapter(ad.put());
            DXGI_ADAPTER_DESC desc{};
            ad->GetDesc(&desc);
            m_adapterName = desc.Description;
            m_adapterIndex = adapterIndex;
        }
    }
    else
    {
        m_adapterIndex = adapterIndex;
        m_adapterName = chosenDesc.Description;
    }

    {
        char buf[256];
        WideCharToMultiByte(CP_UTF8, 0, m_adapterName.c_str(), -1, buf, 256, nullptr, nullptr);
        Log("GPU[%d] %s  FL=0x%04X", m_adapterIndex, buf, (unsigned)got);
    }

    winrt::com_ptr<IDXGIDevice1> dxgiDev1;
    if (SUCCEEDED(m_device->QueryInterface(__uuidof(IDXGIDevice1), dxgiDev1.put_void())))
        dxgiDev1->SetMaximumFrameLatency(1);

    winrt::com_ptr<IDXGIDevice> dxgiDev;
    ThrowIfFailed(m_device->QueryInterface(__uuidof(IDXGIDevice), dxgiDev.put_void()), "QI");
    winrt::com_ptr<IDXGIAdapter> adapter;
    ThrowIfFailed(dxgiDev->GetAdapter(adapter.put()), "GetAdapter");
    ThrowIfFailed(adapter->GetParent(__uuidof(IDXGIFactory2), m_factory.put_void()), "Factory");

    D3D11_SAMPLER_DESC sd{};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    ThrowIfFailed(m_device->CreateSamplerState(&sd, m_linear.put()), "sampler");

    D3D11_BUFFER_DESC cbd{};
    cbd.ByteWidth = 64;
    cbd.Usage = D3D11_USAGE_DYNAMIC;
    cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    ThrowIfFailed(m_device->CreateBuffer(&cbd, nullptr, m_cb.put()), "cb");
    return true;
}

bool Graphics::LoadOneShader(const std::wstring& csoPath, const std::wstring& hlslPath,
                             const char* entry, winrt::com_ptr<ID3D11ComputeShader>& out)
{
    std::vector<char> bytes;
    if (!ReadFileBytes(csoPath, bytes))
    {
        winrt::com_ptr<ID3DBlob> blob, err;
        HRESULT hr = D3DCompileFromFile(
            hlslPath.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
            entry, "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
            blob.put(), err.put());
        if (FAILED(hr))
        {
            if (err) Log("Shader %s: %s", entry, (const char*)err->GetBufferPointer());
            return false;
        }
        bytes.assign((char*)blob->GetBufferPointer(),
                     (char*)blob->GetBufferPointer() + blob->GetBufferSize());
    }
    return SUCCEEDED(m_device->CreateComputeShader(bytes.data(), bytes.size(), nullptr, out.put()));
}

bool Graphics::LoadShaders(const std::wstring& baseDir)
{
    auto tryLoad = [&](const wchar_t* csoName, const wchar_t* hlslName,
                       winrt::com_ptr<ID3D11ComputeShader>& out) -> bool
    {
        const std::wstring paths[] = {
            baseDir + L"shaders\\" + hlslName,
            baseDir + L"..\\..\\src\\shaders\\" + hlslName,
            L"src\\shaders\\" + std::wstring(hlslName),
            baseDir + hlslName
        };
        for (auto& hp : paths)
        {
            out = nullptr;
            if (LoadOneShader(baseDir + csoName, hp, "CSMain", out))
                return true;
        }
        return false;
    };

    tryLoad(L"luma_extract.cso", L"luma_extract.hlsl", m_csLuma);
    tryLoad(L"luma_scaled.cso",  L"luma_scaled.hlsl",  m_csLumaScaled);
    tryLoad(L"luma_down.cso",    L"luma_down.hlsl",    m_csLumaDown);
    tryLoad(L"optical_flow.cso", L"optical_flow.hlsl", m_csFlow);
    tryLoad(L"flow_filter.cso",  L"flow_filter.hlsl",  m_csFlowFilter);
    tryLoad(L"interpolate.cso",  L"interpolate.hlsl",  m_csInterp);
    tryLoad(L"upscale.cso",      L"upscale.hlsl",      m_csUpscale);
    tryLoad(L"blit.cso",         L"blit.hlsl",         m_csBlit);

    if (!m_csLumaScaled) m_csLumaScaled = m_csLuma;
    return m_csFlow && m_csInterp && m_csBlit;
}

winrt::com_ptr<ID3D11Texture2D> Graphics::CreateTex2D(
    UINT w, UINT h, DXGI_FORMAT fmt, UINT bindFlags)
{
    D3D11_TEXTURE2D_DESC d{};
    d.Width = w; d.Height = h; d.MipLevels = 1; d.ArraySize = 1;
    d.Format = fmt; d.SampleDesc.Count = 1;
    d.Usage = D3D11_USAGE_DEFAULT; d.BindFlags = bindFlags;
    winrt::com_ptr<ID3D11Texture2D> tex;
    ThrowIfFailed(m_device->CreateTexture2D(&d, nullptr, tex.put()), "CreateTexture2D");
    return tex;
}

bool Graphics::CreateOverlayTarget(HWND overlayHwnd, UINT width, UINT height)
{
    std::lock_guard lock(m_ctxMutex);
    DestroyOverlayTarget();
    if (!overlayHwnd || !width || !height) return false;
    width = std::min(width, 3840u);
    height = std::min(height, 2160u);
    m_overlayHwnd = overlayHwnd;
    m_overlayW = width;
    m_overlayH = height;
    m_bufferCount = 3;

    DXGI_SWAP_CHAIN_DESC1 sc{};
    sc.Width = width;
    sc.Height = height;
    sc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sc.SampleDesc.Count = 1;
    sc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sc.BufferCount = m_bufferCount;
    sc.Scaling = DXGI_SCALING_STRETCH;
    sc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    sc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;

    ThrowIfFailed(m_factory->CreateSwapChainForComposition(
        m_device.get(), &sc, nullptr, m_swap.put()), "SwapChain");

    // One staging for opaque alpha fix
    m_presentStaging = CreateTex2D(width, height, DXGI_FORMAT_B8G8R8A8_UNORM,
        D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS);
    ThrowIfFailed(m_device->CreateUnorderedAccessView(
        m_presentStaging.get(), nullptr, m_presentUav.put()), "UAV");
    ThrowIfFailed(m_device->CreateShaderResourceView(
        m_presentStaging.get(), nullptr, m_presentSrv.put()), "SRV");

    winrt::com_ptr<IDXGIDevice> dxgiDev;
    ThrowIfFailed(m_device->QueryInterface(__uuidof(IDXGIDevice), dxgiDev.put_void()), "QI");
    ThrowIfFailed(DCompositionCreateDevice(dxgiDev.get(), __uuidof(IDCompositionDevice),
                                           m_dcomp.put_void()), "DComp");
    ThrowIfFailed(m_dcomp->CreateTargetForHwnd(overlayHwnd, TRUE, m_dcompTarget.put()), "Target");
    ThrowIfFailed(m_dcomp->CreateVisual(m_rootVisual.put()), "Visual");
    ThrowIfFailed(m_rootVisual->SetContent(m_swap.get()), "Content");
    ThrowIfFailed(m_dcompTarget->SetRoot(m_rootVisual.get()), "Root");
    ThrowIfFailed(m_dcomp->Commit(), "Commit");
    return true;
}

void Graphics::ResizeOverlay(UINT width, UINT height)
{
    std::lock_guard lock(m_ctxMutex);
    if (!m_swap) return;
    width = std::min(width, 3840u);
    height = std::min(height, 2160u);
    if (width == m_overlayW && height == m_overlayH) return;

    m_presentStaging = nullptr;
    m_presentUav = nullptr;
    m_presentSrv = nullptr;
    if (FAILED(m_swap->ResizeBuffers(m_bufferCount, width, height, DXGI_FORMAT_UNKNOWN, 0)))
        return;
    m_overlayW = width;
    m_overlayH = height;
    m_presentStaging = CreateTex2D(width, height, DXGI_FORMAT_B8G8R8A8_UNORM,
        D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS);
    m_device->CreateUnorderedAccessView(m_presentStaging.get(), nullptr, m_presentUav.put());
    m_device->CreateShaderResourceView(m_presentStaging.get(), nullptr, m_presentSrv.put());
}

void Graphics::DestroyOverlayTarget()
{
    m_presentStaging = nullptr;
    m_presentUav = nullptr;
    m_presentSrv = nullptr;
    m_rootVisual = nullptr;
    m_dcompTarget = nullptr;
    m_dcomp = nullptr;
    m_swap = nullptr;
    m_overlayHwnd = nullptr;
    m_overlayW = m_overlayH = 0;
}

void Graphics::UnbindCS()
{
    ID3D11ShaderResourceView* nsrv[4] = {};
    ID3D11UnorderedAccessView* nuav[2] = {};
    m_ctx->CSSetShaderResources(0, 4, nsrv);
    m_ctx->CSSetUnorderedAccessViews(0, 2, nuav, nullptr);
}

bool Graphics::BlitToCurrentBackbuffer(ID3D11ShaderResourceView* srcSrv, bool upscale, float sharpness)
{
    if (!m_swap || !srcSrv || !m_presentUav) return false;

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (SUCCEEDED(m_ctx->Map(m_cb.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        auto* cb = reinterpret_cast<CBData*>(mapped.pData);
        cb->invOutW = 1.0f / float(m_overlayW);
        cb->invOutH = 1.0f / float(m_overlayH);
        cb->invInW = cb->invOutW;
        cb->invInH = cb->invOutH;
        cb->sharpness = sharpness;
        m_ctx->Unmap(m_cb.get(), 0);
    }

    m_ctx->CSSetShader((upscale && m_csUpscale) ? m_csUpscale.get() : m_csBlit.get(), nullptr, 0);
    ID3D11ShaderResourceView* srvs[] = { srcSrv };
    m_ctx->CSSetShaderResources(0, 1, srvs);
    ID3D11SamplerState* samp[] = { m_linear.get() };
    m_ctx->CSSetSamplers(0, 1, samp);
    ID3D11UnorderedAccessView* uavs[] = { m_presentUav.get() };
    m_ctx->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
    ID3D11Buffer* cbs[] = { m_cb.get() };
    m_ctx->CSSetConstantBuffers(0, 1, cbs);
    m_ctx->Dispatch((m_overlayW + 7) / 8, (m_overlayH + 7) / 8, 1);
    UnbindCS();

    // Flip model: must GetBuffer(0) every present
    winrt::com_ptr<ID3D11Texture2D> bb;
    if (FAILED(m_swap->GetBuffer(0, __uuidof(ID3D11Texture2D), bb.put_void())) || !bb)
        return false;
    m_ctx->CopyResource(bb.get(), m_presentStaging.get());
    return true;
}

bool Graphics::PresentSrv(ID3D11ShaderResourceView* srv, bool useUpscale, float sharpness)
{
    if (!m_swap || !srv) return false;
    std::lock_guard lock(m_ctxMutex);
    if (!BlitToCurrentBackbuffer(srv, useUpscale, sharpness))
        return false;
    HRESULT hr = m_swap->Present(0, 0);
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
        return false;
    return SUCCEEDED(hr) || hr == DXGI_STATUS_OCCLUDED;
}

bool Graphics::PresentTexture(ID3D11Texture2D* tex, bool useUpscale, float sharpness)
{
    if (!tex) return false;
    winrt::com_ptr<ID3D11ShaderResourceView> srv;
    if (FAILED(m_device->CreateShaderResourceView(tex, nullptr, srv.put())))
        return false;
    return PresentSrv(srv.get(), useUpscale, sharpness);
}

} // namespace df
