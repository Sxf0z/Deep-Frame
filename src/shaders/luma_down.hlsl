// Box-downsample R8 → half (or successive halves for flow scale).
Texture2D<unorm float> InLuma : register(t0);
RWTexture2D<unorm float> OutLuma : register(u0);

[numthreads(8, 8, 1)]
void CSMain(uint3 tid : SV_DispatchThreadID)
{
    uint ow, oh;
    OutLuma.GetDimensions(ow, oh);
    if (tid.x >= ow || tid.y >= oh) return;

    uint iw, ih;
    InLuma.GetDimensions(iw, ih);
    // Map out pixel → src using scale (supports non-exact half)
    float2 scale = float2((float)iw / (float)ow, (float)ih / (float)oh);
    int2 s = int2(float2(tid.xy) * scale);
    s = clamp(s, int2(0, 0), int2(iw, ih) - 2);
    float v =
        InLuma[s + int2(0, 0)] +
        InLuma[s + int2(1, 0)] +
        InLuma[s + int2(0, 1)] +
        InLuma[s + int2(1, 1)];
    OutLuma[tid.xy] = v * 0.25;
}
