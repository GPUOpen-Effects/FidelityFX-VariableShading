// AMD FidelityFX Variable Shading Sample code
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

// This is the user side integration of ffx_variable shading.h
// The shader needs to implement functions for reading Luminance and Motionvectors
// and a function to write the VRS image to a UAV
// it also needs to provide the compute shader entry function and call FFX_VariableShading_GenerateVrsImage

// Defines required:
// FFX_VARIABLESHADING_TILESIZE
// FFX_VARIABLESHADING_ADDITIONALSHADINGRATES (if additional shading rates should be used)

// Texture definitions
RWTexture2D<uint>    imgDestination     : register(u0);
Texture2D            texColor           : register(t0);
Texture2D            texVelocity        : register(t1);

// must be after the declaration of imgDestination
#define FFX_HLSL 1
#include "ffx_Variable_Shading.h"

// read a value from previous frames color buffer and return luminance
float FFX_VariableShading_ReadLuminance(int2 pos)
{
    float3 color = texColor[pos].xyz;

    // return color value converted to grayscale
    return dot(color, float3(0.30, 0.59, 0.11));

    // in some cases using different weights, linearizing the color values 
    // or multiplying luminance with a value based on specularity or depth
    // may yield better results
}

// read per pixel motion vectors and convert them to pixel-space
float2 FFX_VariableShading_ReadMotionVec2D(int2 pos)
{
    // return 0 to not use motion vectors
    return texVelocity[pos].xy * float2(0.5f, -0.5f) * g_Resolution;
}

void FFX_VariableShading_WriteVrsImage(int2 pos, uint value)
{
    imgDestination[pos] = value;
}

[numthreads(FFX_VariableShading_ThreadCount1D, FFX_VariableShading_ThreadCount1D, 1)]
void mainCS(
    uint3 Gid  : SV_GroupID,
    uint3 Gtid : SV_GroupThreadID,
    uint  Gidx : SV_GroupIndex)
{
    FFX_VariableShading_GenerateVrsImage(Gid, Gtid, Gidx);
}