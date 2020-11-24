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

// We are queuing (backBufferCount + 0.5) frames, so we need to triple buffer the resources that get modified each frame
static const int backBufferCount = 3;

#define USE_VID_MEM true

using namespace CAULDRON_DX12;

#include "VariableShadingCode.h"

//
// This class deals with the GPU side of the sample.
//
class SampleRenderer
{
public:
    struct Spotlight
    {
        Camera              m_light;
        XMVECTOR            m_color;
        float               m_intensity;
    };

    struct State
    {
        float               m_time;
        Camera              m_camera;

        float               m_exposure;
        float               m_iblFactor;
        float               m_emisiveFactor;

        int                 m_toneMapper;
        int                 m_skyDomeType;
        bool                m_drawBoundingBoxes;

        bool                m_useTAA;

        bool                m_isBenchmarking;

        bool                m_drawLightFrustum;

        const std::string*  m_screenShotName = NULL;

        int                 m_vrsBaseRate;
        int                 m_vrsImageCombiner;
        int                 m_vrsShaderRate;

        bool                m_enableShaderVRS;
        bool                m_enableShadingRateImage;
        float               m_vrsVarianceThreshold;
        float               m_vrsMotionFactor;

        bool                m_showVRSMap;
        bool                m_allowAdditionalVrsRates;
        int                 m_hideUI;
    };

    void OnCreate(Device* pDevice, SwapChain* pSwapChain);
    void OnDestroy();

    void OnCreateWindowSizeDependentResources(SwapChain* pSwapChain, uint32_t Width, uint32_t Height);
    void OnDestroyWindowSizeDependentResources();

    int LoadScene(GLTFCommon* pGLTFCommon, int stage = 0);
    void UnloadScene();

    bool GetHasTAA() const { return m_hasTAA; }
    void SetHasTAA(bool hasTAA) { m_hasTAA = hasTAA; }

    const std::vector<TimeStamp>& GetTimingValues() { return m_timeStamps; }

    void OnRender(State* pState, SwapChain* pSwapChain);

    D3D12_VARIABLE_SHADING_RATE_TIER GetVrsTier() { return m_variableShadingCode.SupportedTier(); }
    bool AdditionalShadingRates() { return m_variableShadingCode.AdditionalShadingRates(); }
    bool AdditionalShadingRatesSupported() { return m_variableShadingCode.AdditionalShadingRatesSupported(); }

private:
    Device*                         m_device;

    uint32_t                        m_width;
    uint32_t                        m_height;
    D3D12_VIEWPORT                  m_viewport;
    D3D12_RECT                      m_rectScissor;
    bool                            m_hasTAA = false;

    // Initialize helper classes
    ResourceViewHeaps               m_resourceViewHeaps;
    UploadHeap                      m_uploadHeap;
    DynamicBufferRing               m_constantBufferRing;
    StaticBufferPool                m_vidMemBufferPool;
    CommandListRing                 m_commandListRing;
    GPUTimestamps                   m_gpuTimer;

    //gltf passes
    GltfMotionVectorsPass*          m_gltfMotionVectors;
    GltfPbrPass*                    m_gltfPBR;
    GltfBBoxPass*                   m_gltfBBox;
    GltfDepthPass*                  m_gltfDepth;
    GLTFTexturesAndBuffers*         m_gltfTexturesAndBuffers;

    // effects
    Bloom                           m_bloom;
    SkyDome                         m_skyDome;
    DownSamplePS                    m_downSample;
    SkyDomeProc                     m_skyDomeProc;
    ToneMapping                     m_toneMappingPS;
    ToneMappingCS                   m_toneMappingCS;
    ColorConversionPS               m_colorConversionPS;
    TAA                             m_taa;

    // GUI
    ImGUI                           m_imGUI;

    // Temporary render targets
    GBuffer                         m_gBuffer;
    GBufferRenderPass               m_renderPassDepthAndMotion;
    GBufferRenderPass               m_renderPassForward;
    GBufferRenderPass               m_renderPassJustDepthAndHdr;

    // shadowmaps
    Texture                         m_shadowMap;
    DSV                             m_shadowMapDSV;
    CBV_SRV_UAV                     m_shadowMapSRV;

    // VRS resources
    int                             m_lastVrsImageCombiner = -1;
    VariableShadingCode             m_variableShadingCode;
    CBV_SRV_UAV                     m_variableShadingInputsSRV;

    Texture                         m_oldBackBuffer;
    CBV_SRV_UAV                     m_oldBackBufferSRV;
    RTV                             m_oldBackBufferRTV;

    // widgets
    Wireframe                       m_wireframe;
    WireframeBox                    m_wireframeBox;

    std::vector<TimeStamp>          m_timeStamps;

    SaveTexture                     m_saveTexture;
    AsyncPool                       m_asyncPool;
};
