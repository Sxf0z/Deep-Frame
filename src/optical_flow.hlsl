// optical_flow.hlsl — Core Frame Generation Logic
// Pass 1: Luma Extraction
// Pass 2: Block Matching (Optical Flow via msad4)
// Pass 3: Temporal Interpolation + Disocclusion Masking

// ─── PASS 1: Luma Extraction ───
Texture2D<unorm float4> InputRGB          : register(t0);
RWTexture2D<unorm float> CurrentLumaUAV   : register(u0);

[numthreads(8, 8, 1)]
void CSLumaExtract(uint3 tid : SV_DispatchThreadID)
{
    float2 dims;
    InputRGB.GetDimensions(dims.x, dims.y);
    if (tid.x >= dims.x || tid.y >= dims.y) return;

    float3 color = InputRGB[tid.xy].rgb;
    // Standard Rec.601 luma for motion tracking
    CurrentLumaUAV[tid.xy] = dot(color, float3(0.299f, 0.587f, 0.114f));
}

// ─── PASS 2: Block Matching (msad4) ───
Texture2D<unorm float> CurrentLuma        : register(t0);
Texture2D<unorm float> PreviousLuma       : register(t1);
RWTexture2D<float2> MotionVectorUAV       : register(u0);

// Helper to pack 4 horizontal pixels into a 32-bit uint for msad4
uint Pack4(int2 pos, Texture2D<unorm float> tex)
{
    uint p = 0;
    p |= (uint(tex[pos + int2(0, 0)] * 255.0f) & 0xFF);
    p |= (uint(tex[pos + int2(1, 0)] * 255.0f) & 0xFF) << 8;
    p |= (uint(tex[pos + int2(2, 0)] * 255.0f) & 0xFF) << 16;
    p |= (uint(tex[pos + int2(3, 0)] * 255.0f) & 0xFF) << 24;
    return p;
}

// Helper to pack 8 horizontal pixels into two 32-bit uints for msad4
uint2 Pack8(int2 pos, Texture2D<unorm float> tex)
{
    return uint2(Pack4(pos, tex), Pack4(pos + int2(4, 0), tex));
}

[numthreads(8, 8, 1)]
void CSBlockMatching(uint3 tid : SV_DispatchThreadID)
{
    uint2 dims;
    CurrentLuma.GetDimensions(dims.x, dims.y);
    if (tid.x >= dims.x || tid.y >= dims.y) return;

    int2 pos = int2(tid.xy);
    
    // Reference block (Current Frame): 4x1 pixels
    uint ref = Pack4(pos, CurrentLuma);

    uint bestSAD = 0xFFFFFFFF;
    int2 bestVector = int2(0, 0);

    // Hardware accelerated search window: -4 to +4 (9x9 area)
    for (int y = -4; y <= 4; y++)
    {
        int2 basePos = pos + int2(-4, y);
        
        // Pack source blocks from Previous Frame
        uint2 src1 = Pack8(basePos, PreviousLuma);              // X: -4 to +3
        uint2 src2 = Pack8(basePos + int2(4, 0), PreviousLuma); // X:  0 to +7
        uint2 src3 = Pack8(basePos + int2(8, 0), PreviousLuma); // X: +4 to +11
        
        // Execute msad4 primitives (computes 4 adjacent displacements simultaneously)
        uint4 sad1 = msad4(ref, src1, uint4(0,0,0,0));
        uint4 sad2 = msad4(ref, src2, uint4(0,0,0,0));
        uint4 sad3 = msad4(ref, src3, uint4(0,0,0,0));
        
        // Evaluate Block 1 (-4, -3, -2, -1)
        if (sad1.x < bestSAD) { bestSAD = sad1.x; bestVector = int2(-4, y); }
        if (sad1.y < bestSAD) { bestSAD = sad1.y; bestVector = int2(-3, y); }
        if (sad1.z < bestSAD) { bestSAD = sad1.z; bestVector = int2(-2, y); }
        if (sad1.w < bestSAD) { bestSAD = sad1.w; bestVector = int2(-1, y); }

        // Evaluate Block 2 (0, 1, 2, 3)
        if (sad2.x < bestSAD) { bestSAD = sad2.x; bestVector = int2(0, y); }
        if (sad2.y < bestSAD) { bestSAD = sad2.y; bestVector = int2(1, y); }
        if (sad2.z < bestSAD) { bestSAD = sad2.z; bestVector = int2(2, y); }
        if (sad2.w < bestSAD) { bestSAD = sad2.w; bestVector = int2(3, y); }

        // Evaluate Block 3 (+4)
        if (sad3.x < bestSAD) { bestSAD = sad3.x; bestVector = int2(4, y); }
    }

    // Disocclusion Confidence Mask: Threshold SAD at ~35 per pixel (140 total)
    if (bestSAD > 140)
    {
        // Mark as unreliable vector (Fallback trigger)
        MotionVectorUAV[pos] = float2(999.0f, 999.0f);
    }
    else
    {
        MotionVectorUAV[pos] = float2(bestVector);
    }
}

// ─── PASS 3: Temporal Interpolation ───
Texture2D<unorm float4> NativeRGB       : register(t0); // Current frame
Texture2D<float2>       MotionVectorTex : register(t1);
RWTexture2D<unorm float4> InterpolatedUAV : register(u0);
SamplerState            LinearSampler   : register(s0);

[numthreads(8, 8, 1)]
void CSInterpolate(uint3 tid : SV_DispatchThreadID)
{
    float2 dims;
    NativeRGB.GetDimensions(dims.x, dims.y);
    if (tid.x >= dims.x || tid.y >= dims.y) return;

    uint2 pos = tid.xy;
    float2 mv = MotionVectorTex[pos];

    if (mv.x > 900.0f) 
    {
        // Disocclusion Fallback: Use native pixel to prevent tearing/ghosting
        InterpolatedUAV[pos] = NativeRGB[pos]; 
    }
    else
    {
        // Interpolate T+0.5: Push the current native frame backwards along half the vector
        float2 uv = (float2(pos) + 0.5f - (mv * 0.5f)) / dims;
        
        // Bilinear sample the shifted pixel
        InterpolatedUAV[pos] = NativeRGB.SampleLevel(LinearSampler, uv, 0.0f);
    }
}
