Texture2D<unorm float4> InputRGB : register(t0);
RWTexture2D<unorm float> OutLuma : register(u0);

[numthreads(8, 8, 1)]
void CSMain(uint3 tid : SV_DispatchThreadID)
{
    uint w, h;
    OutLuma.GetDimensions(w, h);
    if (tid.x >= w || tid.y >= h) return;
    float3 c = InputRGB[tid.xy].rgb;
    OutLuma[tid.xy] = dot(c, float3(0.299, 0.587, 0.114));
}
