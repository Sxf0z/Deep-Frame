// compute_pass.hlsl — Fused EASU (Upsampling) and RCAS (Sharpening) Single-Pass Pipeline
// Optimized for Wavefront execution with [numthreads(64, 1, 1)] topology.

Texture2D<unorm float4>   InputTexture  : register(t0);
RWTexture2D<unorm float4> OutputUAV     : register(u0);
SamplerState              LinearSampler : register(s0);

cbuffer CBParams : register(b0)
{
    float2 InvOutputResolution; // 1.0 / Width, 1.0 / Height
    float2 Pad;                 // Alignment padding
};

// Simplified luminance for contrast detection
float GetLuma(float3 color)
{
    return dot(color, float3(0.299f, 0.587f, 0.114f));
}

[numthreads(64, 1, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint2 pos = dispatchThreadId.xy;

    // Safely reconstruct integer bounds from the inverse float resolution
    uint outW = (uint)(1.0f / InvOutputResolution.x + 0.5f);
    uint outH = (uint)(1.0f / InvOutputResolution.y + 0.5f);

    if (pos.x >= outW || pos.y >= outH)
        return;

    // Normalized UV for the pixel center
    float2 uv = (float2(pos) + 0.5f) * InvOutputResolution;

    // --- Phase 1: EASU (Edge Adaptive Spatial Upsampling) ---
    // Sample a spatial cross pattern
    float2 offsetU = float2(InvOutputResolution.x, 0.0f);
    float2 offsetV = float2(0.0f, InvOutputResolution.y);

    float3 c = InputTexture.SampleLevel(LinearSampler, uv, 0.0f).rgb;
    float3 n = InputTexture.SampleLevel(LinearSampler, uv - offsetV, 0.0f).rgb;
    float3 s = InputTexture.SampleLevel(LinearSampler, uv + offsetV, 0.0f).rgb;
    float3 w = InputTexture.SampleLevel(LinearSampler, uv - offsetU, 0.0f).rgb;
    float3 e = InputTexture.SampleLevel(LinearSampler, uv + offsetU, 0.0f).rgb;

    float lC = GetLuma(c);
    float lN = GetLuma(n);
    float lS = GetLuma(s);
    float lW = GetLuma(w);
    float lE = GetLuma(e);

    // Directional gradient evaluation
    float gradH = abs(lE - lC) + abs(lC - lW);
    float gradV = abs(lN - lC) + abs(lC - lS);
    
    // Weight cross pattern based on gradients to preserve hard edges
    float weightH = saturate(1.0f - (gradH / (gradV + 1e-5f)));
    float weightV = saturate(1.0f - (gradV / (gradH + 1e-5f)));

    float3 colorH = (w + e) * 0.5f;
    float3 colorV = (n + s) * 0.5f;
    
    float3 easuColor = lerp(c, lerp(colorH, colorV, weightV), weightH * weightV * 0.5f);

    // --- Phase 2: RCAS (Robust Contrast Adaptive Sharpening) ---
    // Compute local contrast min/max bounds
    float maxLuma = max(lC, max(max(lN, lS), max(lW, lE)));
    float minLuma = min(lC, min(min(lN, lS), min(lW, lE)));
    float contrast = maxLuma - minLuma;
    
    // Adaptive sharpening: robust against noise by suppressing extreme contrast ringing
    float sharpenAmount = 0.2f + 0.3f * saturate(contrast * 2.0f);
    
    // High-pass laplacian filter fully fused into the local register
    float3 laplacian = (n + s + w + e) * 0.25f;
    float3 rcasColor = easuColor + (easuColor - laplacian) * sharpenAmount;

    // Final VRAM write
    OutputUAV[pos] = float4(saturate(rcasColor), 1.0f);
}
