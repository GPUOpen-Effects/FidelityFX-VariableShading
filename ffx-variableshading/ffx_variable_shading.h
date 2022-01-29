// FFX_VariableShading.h
//
// Copyright (c) 2020 Advanced Micro Devices, Inc. All rights reserved.
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

//////////////////////////////////////////////////////////////////////////
// VariableShading constant buffer parameters:
//
// Resolution The resolution of the surface a VRSImage is to be generated for
// TileSize Hardware dependent tile size (query from API; 8 on AMD RDNA2 based GPUs)
// VarianceCutoff Maximum luminance variance acceptable to accept reduced shading rate
// MotionFactor Length of the motion vector * MotionFactor gets deducted from luminance variance
// to allow lower VS rates on fast moving objects
//
//////////////////////////////////////////////////////////////////////////

#if defined(FFX_CPP)
struct FFX_VariableShading_CB
{
    uint32_t    width, height;
    uint32_t    tileSize;
    float       varianceCutoff;
    float       motionFactor;
};

static uint32_t FFX_VariableShading_DivideRoundingUp(uint32_t a, uint32_t b)
{
    return (a + b - 1) / b;
}

// return the resolution
static void FFX_VariableShading_GetVrsImageResourceDesc(const uint32_t rtWidth, const uint32_t rtHeight, const uint32_t tileSize, CD3DX12_RESOURCE_DESC& VRSImageDesc)
{
    uint32_t vrsImageWidth = FFX_VariableShading_DivideRoundingUp(rtWidth, tileSize);
    uint32_t vrsImageHeight = FFX_VariableShading_DivideRoundingUp(rtHeight, tileSize);

    VRSImageDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8_UINT, vrsImageWidth, vrsImageHeight, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
}

static void FFX_VariableShading_GetDispatchInfo(const FFX_VariableShading_CB* cb, const bool useAditionalShadingRates, uint32_t& numThreadGroupsX, uint32_t& numThreadGroupsY)
{
    uint32_t vrsImageWidth = FFX_VariableShading_DivideRoundingUp(cb->width, cb->tileSize);
    uint32_t vrsImageHeight = FFX_VariableShading_DivideRoundingUp(cb->height, cb->tileSize);

    if (useAditionalShadingRates)
    {
        // coarse tiles are potentially 4x4, so each thread computes 4x4 pixels
        // as a result an 8x8 threadgroup computes 32x32 pixels
        numThreadGroupsX = FFX_VariableShading_DivideRoundingUp(vrsImageWidth * cb->tileSize, 32);
        numThreadGroupsY = FFX_VariableShading_DivideRoundingUp(vrsImageHeight * cb->tileSize, 32);
    }
    else
    {
        // coarse tiles are potentially 2x2, so each thread computes 2x2 pixels
        if (cb->tileSize == 8)
        {
            //each threadgroup computes 4 VRS tiles
            numThreadGroupsX = FFX_VariableShading_DivideRoundingUp(vrsImageWidth, 2);
            numThreadGroupsY = FFX_VariableShading_DivideRoundingUp(vrsImageHeight, 2);
        }
        else
        {
            //each threadgroup computes one VRS tile
            numThreadGroupsX = vrsImageWidth;
            numThreadGroupsY = vrsImageHeight;
        }
    }
}
#elif defined(FFX_HLSL)
    // Constant Buffer
cbuffer FFX_VariableShading_CB0
{
    int2 g_Resolution;
    uint g_TileSize;
    float g_VarianceCutoff;
    float g_MotionFactor;
}

// Forward declaration of functions that need to be implemented by shader code using this technique
float   FFX_VariableShading_ReadLuminance(int2 pos);
float2  FFX_VariableShading_ReadMotionVec2D(int2 pos);
void    FFX_VariableShading_WriteVrsImage(int2 pos, uint value);

static const uint FFX_VARIABLESHADING_RATE1D_1X = 0x0;
static const uint FFX_VARIABLESHADING_RATE1D_2X = 0x1;
static const uint FFX_VARIABLESHADING_RATE1D_4X = 0x2;
#define FFX_VARIABLESHADING_MAKE_SHADING_RATE(x,y) ((x << 2) | (y))

static const uint FFX_VARIABLESHADING_RATE_1X1 = FFX_VARIABLESHADING_MAKE_SHADING_RATE(FFX_VARIABLESHADING_RATE1D_1X, FFX_VARIABLESHADING_RATE1D_1X); // 0;
static const uint FFX_VARIABLESHADING_RATE_1X2 = FFX_VARIABLESHADING_MAKE_SHADING_RATE(FFX_VARIABLESHADING_RATE1D_1X, FFX_VARIABLESHADING_RATE1D_2X); // 0x1;
static const uint FFX_VARIABLESHADING_RATE_2X1 = FFX_VARIABLESHADING_MAKE_SHADING_RATE(FFX_VARIABLESHADING_RATE1D_2X, FFX_VARIABLESHADING_RATE1D_1X); // 0x4;
static const uint FFX_VARIABLESHADING_RATE_2X2 = FFX_VARIABLESHADING_MAKE_SHADING_RATE(FFX_VARIABLESHADING_RATE1D_2X, FFX_VARIABLESHADING_RATE1D_2X); // 0x5;
static const uint FFX_VARIABLESHADING_RATE_2X4 = FFX_VARIABLESHADING_MAKE_SHADING_RATE(FFX_VARIABLESHADING_RATE1D_2X, FFX_VARIABLESHADING_RATE1D_4X); // 0x6;
static const uint FFX_VARIABLESHADING_RATE_4X2 = FFX_VARIABLESHADING_MAKE_SHADING_RATE(FFX_VARIABLESHADING_RATE1D_4X, FFX_VARIABLESHADING_RATE1D_2X); // 0x9;
static const uint FFX_VARIABLESHADING_RATE_4X4 = FFX_VARIABLESHADING_MAKE_SHADING_RATE(FFX_VARIABLESHADING_RATE1D_4X, FFX_VARIABLESHADING_RATE1D_4X); // 0xa;

#if !defined FFX_VARIABLESHADING_ADDITIONALSHADINGRATES
#if FFX_VARIABLESHADING_TILESIZE == 8
static const uint FFX_VariableShading_ThreadCount1D = 8;
static const uint FFX_VariableShading_NumBlocks1D = 2;
#elif FFX_VARIABLESHADING_TILESIZE == 16
static const uint FFX_VariableShading_ThreadCount1D = 8;
static const uint FFX_VariableShading_NumBlocks1D = 1;
#else // FFX_VARIABLESHADING_TILESIZE == 32
static const uint FFX_VariableShading_ThreadCount1D = 16;
static const uint FFX_VariableShading_NumBlocks1D = 1;
#endif
static const uint FFX_VariableShading_SampleCount1D = FFX_VariableShading_ThreadCount1D + 2;

groupshared uint FFX_VariableShading_LdsGroupReduce;

static const uint FFX_VariableShading_ThreadCount = FFX_VariableShading_ThreadCount1D * FFX_VariableShading_ThreadCount1D;
static const uint FFX_VariableShading_SampleCount = FFX_VariableShading_SampleCount1D * FFX_VariableShading_SampleCount1D;
static const uint FFX_VariableShading_NumBlocks = FFX_VariableShading_NumBlocks1D * FFX_VariableShading_NumBlocks1D;

groupshared float3 FFX_VariableShading_LdsVariance[FFX_VariableShading_SampleCount];
groupshared float FFX_VariableShading_LdsMin[FFX_VariableShading_SampleCount];
groupshared float FFX_VariableShading_LdsMax[FFX_VariableShading_SampleCount];

#else //if defined FFX_VARIABLESHADING_ADDITIONALSHADINGRATES
static const uint FFX_VariableShading_ThreadCount1D = 8;
static const uint FFX_VariableShading_NumBlocks1D = 32 / FFX_VARIABLESHADING_TILESIZE;
static const uint FFX_VariableShading_TilesPerGroup = FFX_VariableShading_NumBlocks1D * FFX_VariableShading_NumBlocks1D;
static const uint FFX_VariableShading_SampleCount1D = FFX_VariableShading_ThreadCount1D + 2;

groupshared uint FFX_VariableShading_LdsGroupReduce[FFX_VariableShading_TilesPerGroup];

static const uint FFX_VariableShading_ThreadCount = FFX_VariableShading_ThreadCount1D * FFX_VariableShading_ThreadCount1D;
static const uint FFX_VariableShading_SampleCount = FFX_VariableShading_SampleCount1D * FFX_VariableShading_SampleCount1D;
static const uint FFX_VariableShading_NumBlocks = FFX_VariableShading_NumBlocks1D * FFX_VariableShading_NumBlocks1D;

// load and compute variance for 1x2, 2x1, 2x2, 2x4, 4x2, 4x4 for 8x8 coarse pixels
groupshared uint FFX_VariableShading_LdsShadingRate[FFX_VariableShading_SampleCount];
#endif

float FFX_VariableShading_GetLuminance(int2 pos)
{
    float2 v = FFX_VariableShading_ReadMotionVec2D(pos);
    pos = pos - round(v);
    // clamp to screen
    if (pos.x < 0) pos.x = 0;
    if (pos.y < 0) pos.y = 0;
    if (pos.x >= g_Resolution.x) pos.x = g_Resolution.x - 1;
    if (pos.y >= g_Resolution.y) pos.y = g_Resolution.y - 1;

    return FFX_VariableShading_ReadLuminance(pos);
}

int FFX_VariableShading_FlattenLdsOffset(int2 coord)
{
    coord += 1;
    return coord.y * FFX_VariableShading_SampleCount1D + coord.x;
}

#if !defined FFX_VARIABLESHADING_ADDITIONALSHADINGRATES

//--------------------------------------------------------------------------------------//
// Main function (without additional shading rates) */                                  //
//--------------------------------------------------------------------------------------//
void FFX_VariableShading_GenerateVrsImage(uint3 Gid, uint3 Gtid, uint Gidx)
{
    int2 tileOffset = Gid.xy * FFX_VariableShading_ThreadCount1D * 2;
    int2 baseOffset = tileOffset + int2(-2, -2);
    uint index = Gidx;

#if FFX_VARIABLESHADING_TILESIZE > 8
    if (index == 0)
    {
        FFX_VariableShading_LdsGroupReduce = FFX_VARIABLESHADING_RATE_2X2;
    }
#endif

    // sample source texture (using motion vectors)
    while (index < FFX_VariableShading_SampleCount)
    {
        int2 index2D = 2 * int2(index % FFX_VariableShading_SampleCount1D, index / FFX_VariableShading_SampleCount1D);
        float4 lum = 0;
        lum.x = FFX_VariableShading_GetLuminance(baseOffset + index2D + int2(0, 0));
        lum.y = FFX_VariableShading_GetLuminance(baseOffset + index2D + int2(1, 0));
        lum.z = FFX_VariableShading_GetLuminance(baseOffset + index2D + int2(0, 1));
        lum.w = FFX_VariableShading_GetLuminance(baseOffset + index2D + int2(1, 1));

        // compute the 2x1, 1x2 and 2x2 variance inside the 2x2 coarse pixel region
        float3 delta;
        delta.x = max(abs(lum.x - lum.y), abs(lum.z - lum.w));
        delta.y = max(abs(lum.x - lum.z), abs(lum.y - lum.w));
        float2 minmax = float2(min(min(min(lum.x, lum.y), lum.z), lum.w), max(max(max(lum.x, lum.y), lum.z), lum.w));
        delta.z = minmax.y - minmax.x;

        // reduce variance value for fast moving pixels
        float v = length(FFX_VariableShading_ReadMotionVec2D(baseOffset + index2D));
        v *= g_MotionFactor;
        delta -= v;
        minmax.y -= v;

        // store variance as well as min/max luminance
        FFX_VariableShading_LdsVariance[index] = delta;
        FFX_VariableShading_LdsMin[index] = minmax.x;
        FFX_VariableShading_LdsMax[index] = minmax.y;

        index += FFX_VariableShading_ThreadCount;
    }

    GroupMemoryBarrierWithGroupSync();

    // upper left coordinate in LDS
    int2 threadUV = Gtid.xy;

    // look at neighbouring coarse pixels, to combat burn in effect due to frame dependence
    float3 delta = FFX_VariableShading_LdsVariance[FFX_VariableShading_FlattenLdsOffset(threadUV + int2(0, 0))];

    // read the minimum luminance for neighbouring coarse pixels
    float minNeighbour = FFX_VariableShading_LdsMin[FFX_VariableShading_FlattenLdsOffset(threadUV + int2(0, -1))];
    minNeighbour = min(minNeighbour, FFX_VariableShading_LdsMin[FFX_VariableShading_FlattenLdsOffset(threadUV + int2(-1, 0))]);
    minNeighbour = min(minNeighbour, FFX_VariableShading_LdsMin[FFX_VariableShading_FlattenLdsOffset(threadUV + int2(0, 1))]);
    minNeighbour = min(minNeighbour, FFX_VariableShading_LdsMin[FFX_VariableShading_FlattenLdsOffset(threadUV + int2(1, 0))]);
    float dMin = max(0, FFX_VariableShading_LdsMin[FFX_VariableShading_FlattenLdsOffset(threadUV + int2(0, 0))] - minNeighbour);

    // read the maximum luminance for neighbouring coarse pixels
    float maxNeighbour = FFX_VariableShading_LdsMax[FFX_VariableShading_FlattenLdsOffset(threadUV + int2(0, -1))];
    maxNeighbour = max(maxNeighbour, FFX_VariableShading_LdsMax[FFX_VariableShading_FlattenLdsOffset(threadUV + int2(-1, 0))]);
    maxNeighbour = max(maxNeighbour, FFX_VariableShading_LdsMax[FFX_VariableShading_FlattenLdsOffset(threadUV + int2(0, 1))]);
    maxNeighbour = max(maxNeighbour, FFX_VariableShading_LdsMax[FFX_VariableShading_FlattenLdsOffset(threadUV + int2(1, 0))]);
    float dMax = max(0, maxNeighbour - FFX_VariableShading_LdsMax[FFX_VariableShading_FlattenLdsOffset(threadUV + int2(0, 0))]);

    // assume higher luminance based on min & max values gathered from neighbouring pixels
    delta = max(0, delta + dMin + dMax);

    // Reduction: find maximum variance within VRS tile
#if FFX_VARIABLESHADING_TILESIZE > 8
    // with tilesize=16 we compute 1 tile in one 8x8 threadgroup, in wave32 mode we'll need LDS to compute the per tile max
    // similar for tilesize=32: 1 tile is computed in a 16x16 threadgroup, so we definitely need LDS
    delta = WaveActiveMax(delta);

    if (WaveIsFirstLane())
    {
        uint shadingRate = FFX_VARIABLESHADING_MAKE_SHADING_RATE(FFX_VARIABLESHADING_RATE1D_1X, FFX_VARIABLESHADING_RATE1D_1X);

        if (delta.z < g_VarianceCutoff)
        {
            shadingRate = FFX_VARIABLESHADING_MAKE_SHADING_RATE(FFX_VARIABLESHADING_RATE1D_2X, FFX_VARIABLESHADING_RATE1D_2X);
        }
        else
        {
            if (delta.x > delta.y)
            {
                shadingRate = FFX_VARIABLESHADING_MAKE_SHADING_RATE(FFX_VARIABLESHADING_RATE1D_1X, (delta.y > g_VarianceCutoff) ? FFX_VARIABLESHADING_RATE1D_1X : FFX_VARIABLESHADING_RATE1D_2X);
            }
            else
            {
                shadingRate = FFX_VARIABLESHADING_MAKE_SHADING_RATE((delta.x > g_VarianceCutoff) ? FFX_VARIABLESHADING_RATE1D_1X : FFX_VARIABLESHADING_RATE1D_2X, FFX_VARIABLESHADING_RATE1D_1X);
            }
        }

        InterlockedAnd(FFX_VariableShading_LdsGroupReduce, shadingRate);
    }
    GroupMemoryBarrierWithGroupSync();

    if (Gidx == 0)
    {
        // Store
        FFX_VariableShading_WriteVrsImage(Gid.xy, FFX_VariableShading_LdsGroupReduce);
    }
#else
    // with tilesize=8 we compute 2x2 tiles in one 8x8 threadgroup
    // even in wave32 mode wave intrinsics are sufficient
    float4 diffX = 0;
    float4 diffY = 0;
    float4 diffZ = 0;
    unsigned int idx = (Gtid.y & (FFX_VariableShading_NumBlocks1D - 1)) * FFX_VariableShading_NumBlocks1D + (Gtid.x & (FFX_VariableShading_NumBlocks1D - 1));
    diffX[idx] = delta.x;
    diffY[idx] = delta.y;
    diffZ[idx] = delta.z;
    diffX = WaveActiveMax(diffX);
    diffY = WaveActiveMax(diffY);
    diffZ = WaveActiveMax(diffZ);

    // write out shading rates to VRS image
    if (Gidx < FFX_VariableShading_NumBlocks)
    {
        float varH = diffX[Gidx];
        float varV = diffY[Gidx];
        float var = diffZ[Gidx];;
        uint shadingRate = FFX_VARIABLESHADING_MAKE_SHADING_RATE(FFX_VARIABLESHADING_RATE1D_1X, FFX_VARIABLESHADING_RATE1D_1X);

        if (var < g_VarianceCutoff)
        {
            shadingRate = FFX_VARIABLESHADING_MAKE_SHADING_RATE(FFX_VARIABLESHADING_RATE1D_2X, FFX_VARIABLESHADING_RATE1D_2X);
        }
        else
        {
            if (varH > varV)
            {
                shadingRate = FFX_VARIABLESHADING_MAKE_SHADING_RATE(FFX_VARIABLESHADING_RATE1D_1X, (varV > g_VarianceCutoff) ? FFX_VARIABLESHADING_RATE1D_1X : FFX_VARIABLESHADING_RATE1D_2X);
            }
            else
            {
                shadingRate = FFX_VARIABLESHADING_MAKE_SHADING_RATE((varH > g_VarianceCutoff) ? FFX_VARIABLESHADING_RATE1D_1X : FFX_VARIABLESHADING_RATE1D_2X, FFX_VARIABLESHADING_RATE1D_1X);
            }
        }
        // Store
        FFX_VariableShading_WriteVrsImage(Gid.xy* FFX_VariableShading_NumBlocks1D + uint2(Gidx / FFX_VariableShading_NumBlocks1D, Gidx % FFX_VariableShading_NumBlocks1D), shadingRate);
    }
#endif
}

#else // if defined FFX_VARIABLESHADING_ADDITIONALSHADINGRATES

//--------------------------------------------------------------------------------------//
// Main function (with support for additional shading rates)                            //
//--------------------------------------------------------------------------------------//
void FFX_VariableShading_GenerateVrsImage(uint3 Gid, uint3 Gtid, uint Gidx)
{
    int2 tileOffset = Gid.xy * FFX_VariableShading_ThreadCount1D * 4;
    int2 baseOffset = tileOffset;
    uint index = Gidx;

    while (index < FFX_VariableShading_SampleCount)
    {
        int2 index2D = 4 * int2(index % FFX_VariableShading_SampleCount1D, index / FFX_VariableShading_SampleCount1D);

        // reduce shading rate for fast moving pixels
        float v = length(FFX_VariableShading_ReadMotionVec2D(baseOffset + index2D));
        v *= g_MotionFactor;

        // compute variance for one 4x4 region
        float var2x1 = 0;
        float var1x2 = 0;
        float var2x2 = 0;
        float2 minmax4x2[2] = { float2(g_VarianceCutoff, 0.f), float2(g_VarianceCutoff, 0.f) };
        float2 minmax2x4[2] = { float2(g_VarianceCutoff, 0.f), float2(g_VarianceCutoff, 0.f) };
        float2 minmax4x4 = float2(g_VarianceCutoff, 0.f);

        // computes variance for 2x2 tiles
        // also we need min/max for 2x4, 4x2 & 4x4 
        for (uint y = 0; y < 2; y += 1)
        {
            float tmpVar4x2 = 0;
            for (uint x = 0; x < 2; x += 1)
            {
                int2 index2D = 4 * int2(index % FFX_VariableShading_SampleCount1D, index / FFX_VariableShading_SampleCount1D) + int2(2 * x, 2 * y);
                float4 lum = 0;
                lum.x = FFX_VariableShading_GetLuminance(baseOffset + index2D + int2(0, 0));
                lum.y = FFX_VariableShading_GetLuminance(baseOffset + index2D + int2(1, 0));
                lum.z = FFX_VariableShading_GetLuminance(baseOffset + index2D + int2(0, 1));
                lum.w = FFX_VariableShading_GetLuminance(baseOffset + index2D + int2(1, 1));

                float2 minmax = float2(min(min(lum.x, lum.y), min(lum.z, lum.w)), max(max(lum.x, lum.y), max(lum.z, lum.w)));
                float3 delta;
                delta.x = max(abs(lum.x - lum.y), abs(lum.z - lum.w));
                delta.y = max(abs(lum.x - lum.z), abs(lum.y - lum.w));
                delta.z = minmax.y - minmax.x;

                // reduce shading rate for fast moving pixels
                delta = max(0, delta - v);

                var2x1 = max(var2x1, delta.x);
                var1x2 = max(var1x2, delta.y);
                var2x2 = max(var2x2, delta.z);

                minmax4x2[y].x = min(minmax4x2[y].x, minmax.x);
                minmax4x2[y].y = max(minmax4x2[y].y, minmax.y);

                minmax2x4[x].x = min(minmax2x4[x].x, minmax.x);
                minmax2x4[x].y = max(minmax2x4[x].y, minmax.y);

                minmax4x4.x = min(minmax4x4.x, minmax.x);
                minmax4x4.y = max(minmax4x4.y, minmax.y);
            }
        }

        float var4x2 = max(0, max(minmax4x2[0].y - minmax4x2[0].x, minmax4x2[1].y - minmax4x2[1].x) - v);
        float var2x4 = max(0, max(minmax2x4[0].y - minmax2x4[0].x, minmax2x4[1].y - minmax2x4[1].x) - v);
        float var4x4 = max(0, minmax4x4.y - minmax4x4.x - v);

        uint shadingRate = FFX_VARIABLESHADING_RATE_1X1;
        if (var4x4 < g_VarianceCutoff) shadingRate = FFX_VARIABLESHADING_RATE_4X4;
        else if (var4x2 < g_VarianceCutoff) shadingRate = FFX_VARIABLESHADING_RATE_4X2;
        else if (var2x4 < g_VarianceCutoff) shadingRate = FFX_VARIABLESHADING_RATE_2X4;
        else if (var2x2 < g_VarianceCutoff) shadingRate = FFX_VARIABLESHADING_RATE_2X2;
        else if (var2x1 < g_VarianceCutoff) shadingRate = FFX_VARIABLESHADING_RATE_2X1;
        else if (var1x2 < g_VarianceCutoff) shadingRate = FFX_VARIABLESHADING_RATE_1X2;

        FFX_VariableShading_LdsShadingRate[index] = shadingRate;

        index += FFX_VariableShading_ThreadCount;
    }

    if (Gidx < FFX_VariableShading_TilesPerGroup)
    {
        FFX_VariableShading_LdsGroupReduce[Gidx] = 0;
    }
    GroupMemoryBarrierWithGroupSync();

    int i = 0;
    int2 threadUV = Gtid.xy;

    uint shadingRate[FFX_VariableShading_TilesPerGroup];
    for (i = 0; i < FFX_VariableShading_TilesPerGroup; ++i)
    {
        shadingRate[i] = FFX_VARIABLESHADING_RATE_4X4;
    }
    uint idx = (Gtid.y & (FFX_VariableShading_NumBlocks1D - 1)) * FFX_VariableShading_NumBlocks1D + (Gtid.x & (FFX_VariableShading_NumBlocks1D - 1));
    shadingRate[idx] = FFX_VariableShading_LdsShadingRate[FFX_VariableShading_FlattenLdsOffset(threadUV + int2(0, 0))];
    shadingRate[idx] = min(shadingRate[idx], FFX_VariableShading_LdsShadingRate[FFX_VariableShading_FlattenLdsOffset(threadUV + int2(0, -1))]);
    shadingRate[idx] = min(shadingRate[idx], FFX_VariableShading_LdsShadingRate[FFX_VariableShading_FlattenLdsOffset(threadUV + int2(-1, 0))]);
    shadingRate[idx] = min(shadingRate[idx], FFX_VariableShading_LdsShadingRate[FFX_VariableShading_FlattenLdsOffset(threadUV + int2(1, 0))]);
    shadingRate[idx] = min(shadingRate[idx], FFX_VariableShading_LdsShadingRate[FFX_VariableShading_FlattenLdsOffset(threadUV + int2(0, 1))]);

    // wave-reduce
    for (i = 0; i < FFX_VariableShading_TilesPerGroup; ++i)
    {
        shadingRate[i] = WaveActiveMin(shadingRate[i]);
    }

    // threadgroup-reduce
#if FFX_VARIABLESHADING_TILESIZE<16
    if (WaveIsFirstLane())
    {
        for (i = 0; i < FFX_VariableShading_TilesPerGroup; ++i)
        {

            InterlockedAnd(FFX_VariableShading_LdsGroupReduce[i], shadingRate[i]);
        }
    }
    GroupMemoryBarrierWithGroupSync();

    // write out final rates
    if (Gidx < FFX_VariableShading_TilesPerGroup)
    {
        FFX_VariableShading_WriteVrsImage( Gid.xy * FFX_VariableShading_NumBlocks1D + uint2(Gidx / FFX_VariableShading_NumBlocks1D, Gidx % FFX_VariableShading_NumBlocks1D), FFX_VariableShading_LdsGroupReduce[Gidx] );
    }
#else
    // write out final rates
    if (Gidx < FFX_VariableShading_TilesPerGroup)
    {
        FFX_VariableShading_WriteVrsImage( Gid.xy * FFX_VariableShading_NumBlocks1D + uint2(Gidx / FFX_VariableShading_NumBlocks1D, Gidx % FFX_VariableShading_NumBlocks1D), shadingRate[Gidx] );
    }
#endif


}
#endif // FFX_VARIABLESHADING_ADDITIONALSHADINGRATES
#endif // FFX_CPP|FFX_HLSL
