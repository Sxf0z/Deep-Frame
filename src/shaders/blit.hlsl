// Fast opaque blit (forces alpha=1 for DComp premultiplied).
Texture2D<unorm float4> Src : register(t0);
RWTexture2D<unorm float4> Dst : register(u0);
SamplerState LinearS : register(s0);

cbuffer Params : register(b0)
{
    float2 InvDstSize;
    float2 Pad;
};

[numthreads(8, 8, 1)]
void CSMain(uint3 tid : SV_DispatchThreadID)
{
    uint w, h;
    Dst.GetDimensions(w, h);
    if (tid.x >= w || tid.y >= h) return;
    float2 uv = (float2(tid.xy) + 0.5) * InvDstSize;
    float4 c = Src.SampleLevel(LinearS, uv, 0);
    c.a = 1.0;
    Dst[tid.xy] = c;
}
