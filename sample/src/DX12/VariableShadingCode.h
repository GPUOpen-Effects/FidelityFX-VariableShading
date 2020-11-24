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


#pragma once

#include "PostProc\PostProcCS.h"
#include "base\Texture.h"

#define FFX_CPP
#include "ffx_variable_shading.h"

class VariableShadingCode
{
public:
    void OnCreate(Device* pDevice, ResourceViewHeaps* pResourceViewHeaps, DynamicBufferRing* pConstantBufferRing, StaticBufferPool* pStaticBufferPool, DXGI_FORMAT overlayOutputFormat);
    void OnDestroy();

    void OnCreateWindowSizeDependentResources(uint32_t Width, uint32_t Height);
    void OnDestroyWindowSizeDependentResources();

    void ClearVrsMap(ID3D12GraphicsCommandList* pCmdLst);
    void ComputeVrsMap(ID3D12GraphicsCommandList* pCmdLst, CBV_SRV_UAV* srvs);
    void DrawOverlay(ID3D12GraphicsCommandList* pCmdLst);
    Texture* GetTexture() { return &m_vrsImage; }

    void StartVrsRendering(ID3D12GraphicsCommandList* pCmdLst);
    void EndVrsRendering(ID3D12GraphicsCommandList* pCmdLst);
    void SetShadingRate(D3D12_SHADING_RATE baseShadingRate, const D3D12_SHADING_RATE_COMBINER* combiners, ID3D12GraphicsCommandList* pCmdLst = nullptr);
    void SetVarianceThreshold(float value) { m_vrsThreshold = value; }
    void SetMotionFactor(float value) { m_vrsMotionFactor = value; }

    void SetAdditionalShadingRatesAllowed(bool value) { m_additionalShadingRatesAllowed = value; }
    D3D12_VARIABLE_SHADING_RATE_TIER    SupportedTier() { return m_vrsInfo.VariableShadingRateTier; }
    uint32_t    TileSize() { return m_vrsInfo.ShadingRateImageTileSize; }
    bool AdditionalShadingRates() { return AdditionalShadingRatesSupported() && m_additionalShadingRatesAllowed; }
    bool AdditionalShadingRatesSupported() { return m_vrsInfo.AdditionalShadingRatesSupported; }
    bool UseMotionVectors() { return m_useMotionVectors; }

private:
    void CreateVRSImageGenerationPipeline();
    void CreateOverlayPipeline(DXGI_FORMAT outputFormat);
    void VrsMapStateBarrier(ID3D12GraphicsCommandList* pCmdLst, D3D12_RESOURCE_STATES state);

private:
    Device* m_pDevice = nullptr;

    D3D12_SHADING_RATE                  m_baseShadingRate = D3D12_SHADING_RATE_1X1;
    D3D12_SHADING_RATE_COMBINER         m_combiners[D3D12_RS_SET_SHADING_RATE_COMBINER_COUNT] = {
        D3D12_SHADING_RATE_COMBINER_PASSTHROUGH,
        D3D12_SHADING_RATE_COMBINER_PASSTHROUGH
    };

    StaticResourceViewHeap              m_cpuVisibleHeap;

    StaticBufferPool*                   m_staticBufferPool;
    ResourceViewHeaps*                  m_resourceViewHeaps;
    DynamicBufferRing*                  m_constantBufferRing;

    uint32_t                            m_width;
    uint32_t                            m_height;

    // VRS Resources
    uint32_t                            m_vrsImageWidth;
    uint32_t                            m_vrsImageHeight;
    Texture                             m_vrsImage;
    CBV_SRV_UAV                         m_vrsImageUavCpuVisible;
    CBV_SRV_UAV                         m_vrsImageUav;
    CBV_SRV_UAV                         m_vrsImageSrv;
    D3D12_RESOURCE_STATES               m_vrsImageState;

    bool                                m_vrsImageBound = false;
    bool                                m_vrsEnabled = false;

    // VRS configuration
    float                               m_vrsThreshold = 0.015f;
    float                               m_vrsMotionFactor = 0.01f;
    bool                                m_additionalShadingRatesAllowed = true;
    bool                                m_useMotionVectors = true;

    // The Direct3D12 device
    D3D12_FEATURE_DATA_D3D12_OPTIONS6   m_vrsInfo = {};

    // The compiled pipelines:
    // for this sample we'll create 2 pipeline variants if additional shading rates are supported by the hardware
    ID3D12RootSignature*                m_vrsImageGenerationRootSignature = nullptr;
    ID3D12PipelineState*                m_vrsImageGenerationPipelines[2] = {};
    ID3D12RootSignature*                m_vrsOverlayRootSignature = nullptr;
    ID3D12PipelineState*                m_vrsOverlayPipeline = nullptr;
};
