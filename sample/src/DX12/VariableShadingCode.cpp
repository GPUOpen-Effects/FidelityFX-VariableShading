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



#include "stdafx.h"
#include "base\Device.h"
#include "base\DynamicBufferRing.h"
#include "base\StaticBufferPool.h"
#include "base\UploadHeap.h"
#include "base\Texture.h"
#include "base\Imgui.h"
#include "base\Helper.h"

#include "PostProc\PostProcCS.h"

#include "VariableShadingCode.h"

void VariableShadingCode::OnCreate(
    Device* pDevice,
    ResourceViewHeaps* pResourceViewHeaps,
    DynamicBufferRing* pConstantBufferRing,
    StaticBufferPool* pStaticBufferPool,
    DXGI_FORMAT overlayOutputFormat
)
{
    TRACED;
    const UINT uavDescriptorCount = 3;
    m_cpuVisibleHeap.OnCreate(pDevice, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, uavDescriptorCount, true);

    m_pDevice = pDevice;
    m_staticBufferPool = pStaticBufferPool;
    m_resourceViewHeaps = pResourceViewHeaps;
    m_constantBufferRing = pConstantBufferRing;

    {
        if (!SUCCEEDED(m_pDevice->GetDevice()->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS6, &m_vrsInfo, sizeof(m_vrsInfo))))
        {
            Trace("Selected device does not support Variable Rate Shading", NULL);
        }
        else
        {
            if (m_vrsInfo.VariableShadingRateTier < D3D12_VARIABLE_SHADING_RATE_TIER_1)
            {
                Trace("Selected device does not support Variable Rate Shading");
            }
            else if (m_vrsInfo.VariableShadingRateTier < D3D12_VARIABLE_SHADING_RATE_TIER_2)
            {
                Trace("Selected device does not support Variable Rate Shading Tier 2", NULL);
            }
        }

        if (m_vrsInfo.VariableShadingRateTier > D3D12_VARIABLE_SHADING_RATE_TIER_1)
        {
            CreateVRSImageGenerationPipeline();

            CreateOverlayPipeline(overlayOutputFormat);
        }
    }

    m_cpuVisibleHeap.AllocDescriptor(1, &m_vrsImageUavCpuVisible);
    m_resourceViewHeaps->AllocCBV_SRV_UAVDescriptor(1, &m_vrsImageUav);
    m_resourceViewHeaps->AllocCBV_SRV_UAVDescriptor(1, &m_vrsImageSrv);
}

void VariableShadingCode::OnCreateWindowSizeDependentResources(uint32_t Width, uint32_t Height)
{
    TRACED;
    m_width = Width;
    m_height = Height;

    if (SupportedTier() > D3D12_VARIABLE_SHADING_RATE_TIER_1)
    {
        // Recreate VRS image
        m_vrsImageWidth = FFX_VariableShading_DivideRoundingUp(m_width, TileSize());
        m_vrsImageHeight = FFX_VariableShading_DivideRoundingUp(m_height, TileSize());

        CD3DX12_RESOURCE_DESC RDescVrsImage = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8_UINT, m_vrsImageWidth, m_vrsImageHeight, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        m_vrsImageState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        m_vrsImage.InitRenderTarget(m_pDevice, "VRSImage", &RDescVrsImage, m_vrsImageState);
        m_vrsImage.CreateUAV(0, &m_vrsImageUavCpuVisible);
        m_vrsImage.CreateUAV(0, &m_vrsImageUav);
        m_vrsImage.CreateSRV(0, &m_vrsImageSrv);
    }
}

void VariableShadingCode::OnDestroyWindowSizeDependentResources()
{
    TRACED;
    if (SupportedTier() > D3D12_VARIABLE_SHADING_RATE_TIER_1)
    {
        m_vrsImage.OnDestroy();
    }
}

void VariableShadingCode::OnDestroy()
{
    TRACED;

    if (m_vrsImageGenerationRootSignature)
    {
        m_vrsImageGenerationRootSignature->Release();
        m_vrsImageGenerationRootSignature = NULL;
    }

    for (int i = 0; i < 2; ++i)
    {
        if (m_vrsImageGenerationPipelines[i])
        {
            m_vrsImageGenerationPipelines[i]->Release();;
            m_vrsImageGenerationPipelines[i] = NULL;
        }
    }

    if (m_vrsOverlayRootSignature)
    {
        m_vrsOverlayRootSignature->Release();
        m_vrsOverlayRootSignature = NULL;
    }

    if (m_vrsOverlayPipeline)
    {
        m_vrsOverlayPipeline->Release();
        m_vrsOverlayPipeline = NULL;
    }

    m_cpuVisibleHeap.OnDestroy();
}

// This function creates the VRS image generation pipeline(s)
// m_vrsImageGenerationPipelines[0] does not support additional shading rates.
// If the hardware supports additional shading rates, then
// m_vrsImageGenerationPipelines[1] generates a VRS image using them
void VariableShadingCode::CreateVRSImageGenerationPipeline()
{
    // generate root Signature
    {
        uint32_t UAVTableSize = 1;
        uint32_t SRVTableSize = 2; // color + motionvectors

        CD3DX12_DESCRIPTOR_RANGE DescRange[3];
        CD3DX12_ROOT_PARAMETER RTSlot[3];

        // we'll always have a constant buffer
        int parameterCount = 0;
        DescRange[parameterCount].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);
        RTSlot[parameterCount++].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL);

        DescRange[parameterCount].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, UAVTableSize, 0);
        RTSlot[parameterCount].InitAsDescriptorTable(1, &DescRange[parameterCount], D3D12_SHADER_VISIBILITY_ALL);
        ++parameterCount;

        DescRange[parameterCount].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, SRVTableSize, 0);
        RTSlot[parameterCount].InitAsDescriptorTable(1, &DescRange[parameterCount], D3D12_SHADER_VISIBILITY_ALL);
        ++parameterCount;

        // the root signature contains 3 slots to be used
        CD3DX12_ROOT_SIGNATURE_DESC descRootSignature = CD3DX12_ROOT_SIGNATURE_DESC();
        descRootSignature.NumParameters = parameterCount;
        descRootSignature.pParameters = RTSlot;
        descRootSignature.NumStaticSamplers = 0;
        descRootSignature.pStaticSamplers = nullptr;

        // deny uneccessary access to certain pipeline stages   
        descRootSignature.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

        HRESULT hr = S_OK;
        ID3DBlob* pOutBlob, * pErrorBlob = NULL;

        hr = D3D12SerializeRootSignature(&descRootSignature, D3D_ROOT_SIGNATURE_VERSION_1, &pOutBlob, &pErrorBlob);
        if (FAILED(hr))
        {
            Trace("Compilation failed with errors:\n%hs\n", (const char*)pErrorBlob->GetBufferPointer());
        }

        ThrowIfFailed(
            m_pDevice->GetDevice()->CreateRootSignature(0, pOutBlob->GetBufferPointer(), pOutBlob->GetBufferSize(), IID_PPV_ARGS(&m_vrsImageGenerationRootSignature))
        );
        SetName(m_vrsImageGenerationRootSignature, std::string("VRSRootSignature"));

        pOutBlob->Release();
        if (pErrorBlob)
            pErrorBlob->Release();
    }

    for (int i = 0; i < (AdditionalShadingRatesSupported() ? 2 : 1); ++i)
    {
        // Tile size is fixed (queried from the device)
        DefineList defines;

        char szTileSize[3];
        _itoa_s(m_vrsInfo.ShadingRateImageTileSize, szTileSize, 10);
        defines["FFX_VARIABLESHADING_TILESIZE"] = szTileSize;

        if (i & 1)
        {
            defines["FFX_VARIABLESHADING_ADDITIONALSHADINGRATES"] = "1";
        }

        D3D12_SHADER_BYTECODE shaderByteCode;
        CompileShaderFromFile("VRSImageGenCS.hlsl", &defines, "mainCS", "-T cs_6_0", &shaderByteCode);

        D3D12_COMPUTE_PIPELINE_STATE_DESC descPso = {};
        descPso.CS = shaderByteCode;
        descPso.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
        descPso.pRootSignature = m_vrsImageGenerationRootSignature;
        descPso.NodeMask = 0;

        m_pDevice->GetDevice()->CreateComputePipelineState(&descPso, IID_PPV_ARGS(&m_vrsImageGenerationPipelines[i]));
        m_vrsImageGenerationPipelines[i]->SetName(L"VRSImageGenerationPipeline");
    }
}

void VariableShadingCode::CreateOverlayPipeline(DXGI_FORMAT outputFormat)
{
    // generate root Signature
    {
        CD3DX12_DESCRIPTOR_RANGE DescRange[2];
        CD3DX12_ROOT_PARAMETER RTSlot[2];

        // we'll have a constant buffer
        int parameterCount = 0;
        DescRange[parameterCount].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);
        RTSlot[parameterCount++].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL);

        // and we have a SRV table
        DescRange[parameterCount].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
        RTSlot[parameterCount].InitAsDescriptorTable(1, &DescRange[parameterCount], D3D12_SHADER_VISIBILITY_ALL);
        ++parameterCount;

        // generate the root signature
        CD3DX12_ROOT_SIGNATURE_DESC descRootSignature = CD3DX12_ROOT_SIGNATURE_DESC();
        descRootSignature.NumParameters = parameterCount;
        descRootSignature.pParameters = RTSlot;
        descRootSignature.NumStaticSamplers = 0;
        descRootSignature.pStaticSamplers = nullptr;

        // deny uneccessary access to certain pipeline stages   
        descRootSignature.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

        ID3DBlob* pOutBlob, * pErrorBlob = NULL;

        HRESULT hr = S_OK;
        hr = D3D12SerializeRootSignature(&descRootSignature, D3D_ROOT_SIGNATURE_VERSION_1, &pOutBlob, &pErrorBlob);
        if (FAILED(hr))
        {
            Trace("Compilation failed with errors:\n%hs\n", (const char*)pErrorBlob->GetBufferPointer());
        }
        ThrowIfFailed(
            m_pDevice->GetDevice()->CreateRootSignature(0, pOutBlob->GetBufferPointer(), pOutBlob->GetBufferSize(), IID_PPV_ARGS(&m_vrsOverlayRootSignature))
        );
        SetName(m_vrsOverlayRootSignature, std::string("VRSOverlayRootSignature"));

        pOutBlob->Release();
        if (pErrorBlob)
            pErrorBlob->Release();
    }

    D3D12_SHADER_BYTECODE vsCode;
    D3D12_SHADER_BYTECODE psCode;
    CompileShaderFromFile("VrsOverlay.hlsl", NULL, "mainVS", "-T vs_6_0", &vsCode);
    CompileShaderFromFile("VrsOverlay.hlsl", NULL, "mainPS", "-T ps_6_0", &psCode);

    // generate Graphics Pipeline State Object
    if (m_vrsOverlayPipeline != NULL)
    {
        m_vrsOverlayPipeline->Release();
        m_vrsOverlayPipeline = NULL;
    }

    //we need to cache the referenced data so the lambda function can get a copy
    D3D12_BLEND_DESC blendDesc = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    blendDesc.IndependentBlendEnable = true;
    blendDesc.RenderTarget[0].BlendEnable = true;
    blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_SRC_ALPHA;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;

    D3D12_DEPTH_STENCIL_DESC depthStencilBlankDesc = {};

    D3D12_GRAPHICS_PIPELINE_STATE_DESC descPso = {};
    descPso.InputLayout = { NULL, 0 };
    descPso.pRootSignature = m_vrsOverlayRootSignature;
    descPso.VS = vsCode;
    descPso.PS = psCode;
    descPso.DepthStencilState = depthStencilBlankDesc;
    descPso.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    descPso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    descPso.BlendState = blendDesc;
    descPso.SampleMask = UINT_MAX;
    descPso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    descPso.NumRenderTargets = 1;
    descPso.RTVFormats[0] = outputFormat;
    descPso.SampleDesc.Count = 1;
    descPso.NodeMask = 0;
    ThrowIfFailed(
        m_pDevice->GetDevice()->CreateGraphicsPipelineState(&descPso, IID_PPV_ARGS(&m_vrsOverlayPipeline))
    );
    SetName(m_vrsOverlayPipeline, "VRSOverlayPipeline");
}

void  VariableShadingCode::ClearVrsMap(ID3D12GraphicsCommandList* pCommandList)
{
    assert(pCommandList != nullptr);

    if (m_vrsImage.GetResource())
    {
        VrsMapStateBarrier(pCommandList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        const UINT ClearColor[4] = {};
        pCommandList->ClearUnorderedAccessViewUint(m_vrsImageUav.GetGPU(), m_vrsImageUavCpuVisible.GetCPU(), m_vrsImage.GetResource(), ClearColor, 0, NULL);
    }
}

void VariableShadingCode::ComputeVrsMap(ID3D12GraphicsCommandList* pCmdLst, CBV_SRV_UAV* srvs)
{
    TRACED;
    assert(pCmdLst != nullptr);

    if (m_vrsInfo.VariableShadingRateTier > D3D12_VARIABLE_SHADING_RATE_TIER_1)
    {

        UserMarker marker(pCmdLst, "VariableShadingCodeCS");

        FFX_VariableShading_CB* data;
        D3D12_GPU_VIRTUAL_ADDRESS constantBuffer;
        m_constantBufferRing->AllocConstantBuffer(sizeof(FFX_VariableShading_CB), (void**)&data, &constantBuffer);
        data->width = m_width;
        data->height = m_height;
        data->varianceCutoff = m_vrsThreshold;
        data->tileSize = TileSize();
        data->motionFactor = m_vrsMotionFactor;

        VrsMapStateBarrier(pCmdLst, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        // Bind Descriptor heaps and the root signature
        ID3D12DescriptorHeap* pSrvHeap = m_resourceViewHeaps->GetCBV_SRV_UAVHeap();
        pCmdLst->SetDescriptorHeaps(1, &pSrvHeap);
        pCmdLst->SetComputeRootSignature(m_vrsImageGenerationRootSignature);

        // Bind Descriptor the descriptor sets
        //                
        int params = 0;
        pCmdLst->SetComputeRootConstantBufferView(params++, constantBuffer);
        pCmdLst->SetComputeRootDescriptorTable(params++, m_vrsImageUav.GetGPU());
        pCmdLst->SetComputeRootDescriptorTable(params++, srvs->GetGPU());

        // Bind Pipeline
        //
        uint32_t shaderIndex = AdditionalShadingRates() ? 1 : 0;
        pCmdLst->SetPipelineState(m_vrsImageGenerationPipelines[shaderIndex]);

        // Dispatch: compute VRS image
        //
        uint32_t w = 0;
        uint32_t h = 0;
        FFX_VariableShading_GetDispatchInfo(data, AdditionalShadingRates(), w, h);
        pCmdLst->Dispatch(w, h, 1);

    }
}

void VariableShadingCode::StartVrsRendering(ID3D12GraphicsCommandList* pCmdLst)
{
    TRACED;
    assert(pCmdLst != nullptr);
    assert(m_vrsEnabled == false);
    if (SupportedTier() > D3D12_VARIABLE_SHADING_RATE_TIER_NOT_SUPPORTED)
    {
        ID3D12GraphicsCommandList5* pCommandList5;
        ThrowIfFailed(pCmdLst->QueryInterface(__uuidof(ID3D12GraphicsCommandList5), (void**)&pCommandList5));

        if (SupportedTier() > D3D12_VARIABLE_SHADING_RATE_TIER_1)
        {
            if ((!m_vrsImageBound) && (m_combiners[1] != D3D12_SHADING_RATE_COMBINER_PASSTHROUGH))
            {
                VrsMapStateBarrier(pCmdLst, D3D12_RESOURCE_STATE_SHADING_RATE_SOURCE);

                pCommandList5->RSSetShadingRateImage(m_vrsImage.GetResource());
                m_vrsImageBound = true;
            }
        }

        // set the VRS settings into the device
        pCommandList5->RSSetShadingRate(
            m_baseShadingRate,
            m_combiners
        );

        m_vrsEnabled = true;

        pCommandList5->Release();
    }
}

void VariableShadingCode::SetShadingRate(D3D12_SHADING_RATE baseShadingRate, const D3D12_SHADING_RATE_COMBINER* combiners, ID3D12GraphicsCommandList* pCmdLst)
{
    TRACED;
    m_baseShadingRate = baseShadingRate;
    memcpy(m_combiners, combiners, sizeof(m_combiners));

    if (SupportedTier() == D3D12_VARIABLE_SHADING_RATE_TIER_NOT_SUPPORTED)
        return;

    // if VRS is enabled we need a pCmdLst to pass the new state to the device
    if (m_vrsEnabled)
    {
        assert(pCmdLst != nullptr);
        ID3D12GraphicsCommandList5* pCommandList5;
        ThrowIfFailed(pCmdLst->QueryInterface(__uuidof(ID3D12GraphicsCommandList5), (void**)&pCommandList5));

        if (SupportedTier() > D3D12_VARIABLE_SHADING_RATE_TIER_1)
        {
            pCommandList5->RSSetShadingRateImage(m_vrsImage.GetResource());
        }

        if (m_combiners)
        {
            pCommandList5->RSSetShadingRate(
                m_baseShadingRate,
                m_combiners
            );
        }
        else
        {
            D3D12_SHADING_RATE_COMBINER combiners[D3D12_RS_SET_SHADING_RATE_COMBINER_COUNT] = {
                D3D12_SHADING_RATE_COMBINER_PASSTHROUGH,
                m_vrsImage.GetResource() ? D3D12_SHADING_RATE_COMBINER_OVERRIDE : D3D12_SHADING_RATE_COMBINER_PASSTHROUGH
            };
            pCommandList5->RSSetShadingRate(
                m_baseShadingRate,
                combiners
            );
        }

        pCommandList5->Release();
    }
}

void VariableShadingCode::EndVrsRendering(ID3D12GraphicsCommandList* pCmdLst)
{
    TRACED;
    assert(pCmdLst != nullptr);

    if (SupportedTier() == D3D12_VARIABLE_SHADING_RATE_TIER_NOT_SUPPORTED)
        return;

    // disable VRS
    ID3D12GraphicsCommandList5* pCommandList5;
    ThrowIfFailed(pCmdLst->QueryInterface(__uuidof(ID3D12GraphicsCommandList5), (void**)&pCommandList5));

    {
        if (SupportedTier() > D3D12_VARIABLE_SHADING_RATE_TIER_1)
        {
            pCommandList5->RSSetShadingRateImage(nullptr);
        }

        if (m_combiners)
        {
            pCommandList5->RSSetShadingRate(
                D3D12_SHADING_RATE_1X1,
                m_combiners
            );
        }
        else
        {
            D3D12_SHADING_RATE_COMBINER combiners[D3D12_RS_SET_SHADING_RATE_COMBINER_COUNT] = {
                D3D12_SHADING_RATE_COMBINER_PASSTHROUGH,
                m_vrsImage.GetResource() ? D3D12_SHADING_RATE_COMBINER_OVERRIDE : D3D12_SHADING_RATE_COMBINER_PASSTHROUGH
            };
            pCommandList5->RSSetShadingRate(
                D3D12_SHADING_RATE_1X1,
                combiners
            );
        }

    }

    m_vrsImageBound = false;
    m_vrsEnabled = false;

    pCommandList5->Release();
}

void VariableShadingCode::DrawOverlay(ID3D12GraphicsCommandList* pCmdLst)
{
    TRACED;
    assert(pCmdLst != nullptr);

    if (SupportedTier() > D3D12_VARIABLE_SHADING_RATE_TIER_1)
    {
        UserMarker marker(pCmdLst, "VrsDrawOverlay");

        FFX_VariableShading_CB* data;
        D3D12_GPU_VIRTUAL_ADDRESS constantBuffer;
        m_constantBufferRing->AllocConstantBuffer(sizeof(FFX_VariableShading_CB), (void**)&data, &constantBuffer);
        data->width = m_width;
        data->height = m_height;
        data->tileSize = TileSize();

        VrsMapStateBarrier(pCmdLst, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

        {
            // Bind vertices 
            //
            pCmdLst->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

            // Bind Descriptor heaps, root signatures and descriptor sets
            //                
            ID3D12DescriptorHeap* pSrvHeap = m_resourceViewHeaps->GetCBV_SRV_UAVHeap();
            pCmdLst->SetDescriptorHeaps(1, &pSrvHeap);
            pCmdLst->SetGraphicsRootSignature(m_vrsOverlayRootSignature);
            pCmdLst->SetGraphicsRootConstantBufferView(0, constantBuffer);
            pCmdLst->SetGraphicsRootDescriptorTable(1, m_vrsImageSrv.GetGPU());

            // Bind Pipeline
            //
            pCmdLst->SetPipelineState(m_vrsOverlayPipeline);

            // Draw
            //
            pCmdLst->DrawInstanced(3, 1, 0, 0);
        }
    }
}


void VariableShadingCode::VrsMapStateBarrier(ID3D12GraphicsCommandList* pCmdLst, D3D12_RESOURCE_STATES state)
{
    TRACED;
    assert(pCmdLst != nullptr);
    assert(m_vrsImageBound == false);

    if (SupportedTier() > D3D12_VARIABLE_SHADING_RATE_TIER_1)
    {
        if (m_vrsImageState != state)
        {
            pCmdLst->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_vrsImage.GetResource(), m_vrsImageState, state));
            m_vrsImageState = state;
        }
    }
}