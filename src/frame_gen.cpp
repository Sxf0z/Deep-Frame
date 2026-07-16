#include "frame_gen.h"

namespace df {
namespace {

struct alignas(16) CBFlow
{
    int32_t searchRadius;
    int32_t step;
    float   fullToFlow;
    float   temporalAlpha;
    uint32_t useTemporal;
    uint32_t pad0;
    float    pad1[2];
};

struct alignas(16) CBInterp
{
    float invFullW, invFullH;
    float invFlowW, invFlowH;
    float time;
    float flowScale;
    float confThreshold;
    uint32_t mode;
};

struct alignas(16) CBLuma
{
    float invFlowW, invFlowH;
    float pad[2];
};

} // namespace

FrameGenPipeline::FrameGenPipeline(Graphics& gfx) : m_gfx(gfx) {}

ID3D11ShaderResourceView* FrameGenPipeline::SrvInterp(int index) const
{
    if (index < 0 || index >= 3) return nullptr;
    return m_srvInterp[index].get();
}

ID3D11Texture2D* FrameGenPipeline::ColorInterp(int index) const
{
    if (index < 0 || index >= 3) return nullptr;
    return m_interp[index].get();
}

void FrameGenPipeline::Release()
{
    for (int i = 0; i < 3; ++i)
    {
        m_srvInterp[i] = nullptr;
        m_uavInterp[i] = nullptr;
        m_interp[i] = nullptr;
    }
    m_srvColorCurr = m_srvColorPrev = nullptr;
    m_srvLumaFlowCurr = m_srvLumaFlowPrev = nullptr;
    m_srvFlowA = m_srvFlowB = m_srvFlowTemporal = nullptr;
    m_uavLumaFlowCurr = m_uavFlowA = m_uavFlowB = nullptr;
    m_colorCurr = m_colorPrev = nullptr;
    m_lumaFlowCurr = m_lumaFlowPrev = nullptr;
    m_flowA = m_flowB = m_flowTemporal = nullptr;
    m_w = m_h = m_fw = m_fh = 0;
    m_hasHistory = false;
    m_genCount = 0;
}

bool FrameGenPipeline::EnsureSize(UINT width, UINT height, float flowScale)
{
    if (!width || !height) return false;
    width  = std::min(width,  2560u); // cap work for laptop GPUs
    height = std::min(height, 1440u);
    flowScale = std::clamp(flowScale, 0.25f, 1.0f);

    UINT fw = std::max(16u, (UINT)std::lround(width  * flowScale));
    UINT fh = std::max(16u, (UINT)std::lround(height * flowScale));
    fw &= ~1u; fh &= ~1u;

    if (m_colorCurr && m_w == width && m_h == height && m_fw == fw && m_fh == fh)
        return true;

    Release();
    m_w = width; m_h = height;
    m_fw = fw; m_fh = fh;
    m_flowScale = flowScale;
    m_fullToFlow = float(width) / float(std::max(1u, fw));

    const UINT colorBind = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    const UINT r8Bind    = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    const UINT mvBind    = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

    try
    {
        m_colorCurr = m_gfx.CreateTex2D(m_w, m_h, DXGI_FORMAT_B8G8R8A8_UNORM, colorBind);
        m_colorPrev = m_gfx.CreateTex2D(m_w, m_h, DXGI_FORMAT_B8G8R8A8_UNORM, colorBind);
        for (int i = 0; i < 3; ++i)
            m_interp[i] = m_gfx.CreateTex2D(m_w, m_h, DXGI_FORMAT_B8G8R8A8_UNORM, colorBind);

        m_lumaFlowCurr = m_gfx.CreateTex2D(m_fw, m_fh, DXGI_FORMAT_R8_UNORM, r8Bind);
        m_lumaFlowPrev = m_gfx.CreateTex2D(m_fw, m_fh, DXGI_FORMAT_R8_UNORM, r8Bind);

        // RGBA16F: xy = motion (full px), z = confidence
        m_flowA = m_gfx.CreateTex2D(m_fw, m_fh, DXGI_FORMAT_R16G16B16A16_FLOAT, mvBind);
        m_flowB = m_gfx.CreateTex2D(m_fw, m_fh, DXGI_FORMAT_R16G16B16A16_FLOAT, mvBind);
        m_flowTemporal = m_gfx.CreateTex2D(m_fw, m_fh, DXGI_FORMAT_R16G16B16A16_FLOAT, mvBind);

        auto* dev = m_gfx.Device();
        ThrowIfFailed(dev->CreateShaderResourceView(m_colorCurr.get(), nullptr, m_srvColorCurr.put()), "srv");
        ThrowIfFailed(dev->CreateShaderResourceView(m_colorPrev.get(), nullptr, m_srvColorPrev.put()), "srv");
        for (int i = 0; i < 3; ++i)
        {
            ThrowIfFailed(dev->CreateShaderResourceView(m_interp[i].get(), nullptr, m_srvInterp[i].put()), "srvI");
            ThrowIfFailed(dev->CreateUnorderedAccessView(m_interp[i].get(), nullptr, m_uavInterp[i].put()), "uavI");
        }
        ThrowIfFailed(dev->CreateShaderResourceView(m_lumaFlowCurr.get(), nullptr, m_srvLumaFlowCurr.put()), "srv");
        ThrowIfFailed(dev->CreateShaderResourceView(m_lumaFlowPrev.get(), nullptr, m_srvLumaFlowPrev.put()), "srv");
        ThrowIfFailed(dev->CreateShaderResourceView(m_flowA.get(), nullptr, m_srvFlowA.put()), "srv");
        ThrowIfFailed(dev->CreateShaderResourceView(m_flowB.get(), nullptr, m_srvFlowB.put()), "srv");
        ThrowIfFailed(dev->CreateShaderResourceView(m_flowTemporal.get(), nullptr, m_srvFlowTemporal.put()), "srv");
        ThrowIfFailed(dev->CreateUnorderedAccessView(m_lumaFlowCurr.get(), nullptr, m_uavLumaFlowCurr.put()), "uav");
        ThrowIfFailed(dev->CreateUnorderedAccessView(m_flowA.get(), nullptr, m_uavFlowA.put()), "uav");
        ThrowIfFailed(dev->CreateUnorderedAccessView(m_flowB.get(), nullptr, m_uavFlowB.put()), "uav");
    }
    catch (...)
    {
        Release();
        return false;
    }

    Log("FG %ux%u flow %ux%u (%.0f%%)", m_w, m_h, m_fw, m_fh, m_flowScale * 100.f);
    return true;
}

void FrameGenPipeline::BeginFrame()
{
    if (!m_colorCurr || !m_colorPrev || !m_hasHistory) return;
    m_gfx.Context()->CopyResource(m_colorPrev.get(), m_colorCurr.get());
}

void FrameGenPipeline::Unbind()
{
    ID3D11ShaderResourceView* nsrv[4] = {};
    ID3D11UnorderedAccessView* nuav[2] = {};
    auto* ctx = m_gfx.Context();
    ctx->CSSetShaderResources(0, 4, nsrv);
    ctx->CSSetUnorderedAccessViews(0, 2, nuav, nullptr);
}

void FrameGenPipeline::DispatchFlow(bool performanceMode)
{
    auto* ctx = m_gfx.Context();
    const UINT fx = (m_fw + 7) / 8, fy = (m_fh + 7) / 8;

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (SUCCEEDED(ctx->Map(m_gfx.CB(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        auto* cb = reinterpret_cast<CBLuma*>(mapped.pData);
        cb->invFlowW = 1.0f / float(m_fw);
        cb->invFlowH = 1.0f / float(m_fh);
        ctx->Unmap(m_gfx.CB(), 0);
    }

    ID3D11ComputeShader* lumaCS = m_gfx.CSLumaScaled() ? m_gfx.CSLumaScaled() : m_gfx.CSLuma();
    ctx->CSSetShader(lumaCS, nullptr, 0);
    ID3D11ShaderResourceView* s0[] = { m_srvColorCurr.get() };
    ctx->CSSetShaderResources(0, 1, s0);
    ID3D11SamplerState* samp[] = { m_gfx.LinearSampler() };
    ctx->CSSetSamplers(0, 1, samp);
    ID3D11UnorderedAccessView* u0[] = { m_uavLumaFlowCurr.get() };
    ctx->CSSetUnorderedAccessViews(0, 1, u0, nullptr);
    ID3D11Buffer* cbs[] = { m_gfx.CB() };
    ctx->CSSetConstantBuffers(0, 1, cbs);
    ctx->Dispatch(fx, fy, 1);
    Unbind();

    if (!m_hasHistory)
    {
        ctx->CopyResource(m_lumaFlowPrev.get(), m_lumaFlowCurr.get());
        ctx->CopyResource(m_colorPrev.get(), m_colorCurr.get());
        return;
    }

    if (SUCCEEDED(ctx->Map(m_gfx.CB(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        auto* cb = reinterpret_cast<CBFlow*>(mapped.pData);
        if (performanceMode)
        {
            cb->searchRadius = 4;
            cb->step = 2;
            cb->temporalAlpha = 0.25f;
        }
        else
        {
            cb->searchRadius = 6;
            cb->step = 2;
            cb->temporalAlpha = 0.20f;
        }
        cb->fullToFlow = m_fullToFlow;
        cb->useTemporal = 1u;
        cb->pad0 = 0;
        ctx->Unmap(m_gfx.CB(), 0);
    }

    ctx->CSSetShader(m_gfx.CSFlow(), nullptr, 0);
    ID3D11ShaderResourceView* sf[] = {
        m_srvLumaFlowCurr.get(), m_srvLumaFlowPrev.get(), m_srvFlowTemporal.get()
    };
    ctx->CSSetShaderResources(0, 3, sf);
    ID3D11UnorderedAccessView* uf[] = { m_uavFlowA.get() };
    ctx->CSSetUnorderedAccessViews(0, 1, uf, nullptr);
    ctx->CSSetConstantBuffers(0, 1, cbs);
    ctx->Dispatch(fx, fy, 1);
    Unbind();

    if (!performanceMode && m_gfx.CSFlowFilter())
    {
        ctx->CSSetShader(m_gfx.CSFlowFilter(), nullptr, 0);
        ID3D11ShaderResourceView* sff[] = { m_srvFlowA.get() };
        ctx->CSSetShaderResources(0, 1, sff);
        ID3D11UnorderedAccessView* uff[] = { m_uavFlowB.get() };
        ctx->CSSetUnorderedAccessViews(0, 1, uff, nullptr);
        ctx->Dispatch(fx, fy, 1);
        Unbind();
        ctx->CopyResource(m_flowA.get(), m_flowB.get());
    }

    ctx->CopyResource(m_flowTemporal.get(), m_flowA.get());
    ctx->CopyResource(m_lumaFlowPrev.get(), m_lumaFlowCurr.get());
}

void FrameGenPipeline::DispatchInterpolate(float t, int outIndex)
{
    if (outIndex < 0 || outIndex >= 3) return;
    auto* ctx = m_gfx.Context();
    const UINT gx = (m_w + 7) / 8, gy = (m_h + 7) / 8;

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (SUCCEEDED(ctx->Map(m_gfx.CB(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        auto* cb = reinterpret_cast<CBInterp*>(mapped.pData);
        cb->invFullW = 1.0f / float(m_w);
        cb->invFullH = 1.0f / float(m_h);
        cb->invFlowW = 1.0f / float(m_fw);
        cb->invFlowH = 1.0f / float(m_fh);
        cb->time = t;
        cb->flowScale = m_fullToFlow;
        cb->confThreshold = 0.40f;
        cb->mode = 0; // warp PREV forward — sharp mid frame, no dual blend
        ctx->Unmap(m_gfx.CB(), 0);
    }

    ctx->CSSetShader(m_gfx.CSInterp(), nullptr, 0);
    ID3D11ShaderResourceView* s[] = {
        m_srvColorPrev.get(), m_srvColorCurr.get(), m_srvFlowA.get()
    };
    ctx->CSSetShaderResources(0, 3, s);
    ID3D11SamplerState* samp[] = { m_gfx.LinearSampler() };
    ctx->CSSetSamplers(0, 1, samp);
    ID3D11UnorderedAccessView* u[] = { m_uavInterp[outIndex].get() };
    ctx->CSSetUnorderedAccessViews(0, 1, u, nullptr);
    ID3D11Buffer* cbs[] = { m_gfx.CB() };
    ctx->CSSetConstantBuffers(0, 1, cbs);
    ctx->Dispatch(gx, gy, 1);
    Unbind();
}

void FrameGenPipeline::Process(bool frameGen, bool performanceMode, int multiplier)
{
    m_genCount = 0;
    if (!m_colorCurr) return;

    if (!frameGen)
    {
        // Still need history for next time FG is enabled
        if (!m_hasHistory)
        {
            m_gfx.Context()->CopyResource(m_colorPrev.get(), m_colorCurr.get());
            m_hasHistory = true;
        }
        else
        {
            // keep prev updated via BeginFrame
            m_hasHistory = true;
        }
        return;
    }

    DispatchFlow(performanceMode);

    if (!m_hasHistory)
    {
        m_hasHistory = true;
        return;
    }

    // x2 → one mid at t=0.5; x3 → 1/3,2/3; x4 → 1/4,2/4,3/4
    const int intermediates = std::clamp(multiplier - 1, 1, 3);
    for (int i = 0; i < intermediates; ++i)
    {
        float t = float(i + 1) / float(multiplier);
        DispatchInterpolate(t, i);
    }
    m_genCount = intermediates;
    m_hasHistory = true;
}

} // namespace df
