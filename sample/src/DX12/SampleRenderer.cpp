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

#include "SampleRenderer.h"
#include "base\\SaveTexture.h"


//--------------------------------------------------------------------------------------
//
// OnCreate
//
//--------------------------------------------------------------------------------------
void SampleRenderer::OnCreate(Device* pDevice, SwapChain* pSwapChain)
{
    m_device = pDevice;

    // Initialize helpers

    // Create all the heaps for the resources views
    const uint32_t cbvDescriptorCount = 4000;
    const uint32_t srvDescriptorCount = 8000;
    const uint32_t uavDescriptorCount = 10;
    const uint32_t dsvDescriptorCount = 10;
    const uint32_t rtvDescriptorCount = 60;
    const uint32_t samplerDescriptorCount = 20;
    m_resourceViewHeaps.OnCreate(pDevice, cbvDescriptorCount, srvDescriptorCount, uavDescriptorCount, dsvDescriptorCount, rtvDescriptorCount, samplerDescriptorCount);

    // Create a commandlist ring for the Direct queue
    uint32_t commandListsPerBackBuffer = 8;
    m_commandListRing.OnCreate(pDevice, backBufferCount, commandListsPerBackBuffer, pDevice->GetGraphicsQueue()->GetDesc());

    // Create a 'dynamic' constant buffer
    const uint32_t constantBuffersMemSize = 200 * 1024 * 1024;
    m_constantBufferRing.OnCreate(pDevice, backBufferCount, constantBuffersMemSize, &m_resourceViewHeaps);

    // Create a 'static' pool for vertices, indices and constant buffers
    const uint32_t staticGeometryMemSize = (5 * 128) * 1024 * 1024;
    m_vidMemBufferPool.OnCreate(pDevice, staticGeometryMemSize, USE_VID_MEM, "StaticGeom");

    // initialize the GPU time stamps module
    m_gpuTimer.OnCreate(pDevice, backBufferCount);

    // Quick helper to upload resources, it has it's own commandList and uses suballocation.
    // for 4K textures we'll need 100Megs
    const uint32_t uploadHeapMemSize = 1000 * 1024 * 1024;
    m_uploadHeap.OnCreate(pDevice, uploadHeapMemSize);    // initialize an upload heap (uses suballocation for faster results)

    // Create GBuffer and render passes
    //
    {
        m_gBuffer.OnCreate(
            pDevice,
            &m_resourceViewHeaps,
            {
                { GBUFFER_DEPTH, DXGI_FORMAT_R32_TYPELESS},
                { GBUFFER_FORWARD, DXGI_FORMAT_R16G16B16A16_FLOAT},
                { GBUFFER_MOTION_VECTORS, DXGI_FORMAT_R16G16_FLOAT},
            },
            1
            );

        GBufferFlags fullGBuffer = GBUFFER_DEPTH | GBUFFER_FORWARD | GBUFFER_MOTION_VECTORS;// | GBUFFER_NORMAL_BUFFER | GBUFFER_DIFFUSE | GBUFFER_SPECULAR_ROUGHNESS;
        m_renderPassDepthAndMotion.OnCreate(&m_gBuffer, GBUFFER_DEPTH | GBUFFER_MOTION_VECTORS);
        m_renderPassForward.OnCreate(&m_gBuffer, GBUFFER_DEPTH | GBUFFER_FORWARD | GBUFFER_MOTION_VECTORS);
        m_renderPassJustDepthAndHdr.OnCreate(&m_gBuffer, GBUFFER_DEPTH | GBUFFER_FORWARD);
    }

    // Create a Shadowmap atlas to hold 4 cascades/spotlights
    m_shadowMap.InitDepthStencil(pDevice, "m_pShadowMap", &CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R32_TYPELESS, 2 * 1024, 2 * 1024, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL));
    m_resourceViewHeaps.AllocDSVDescriptor(1, &m_shadowMapDSV);
    m_resourceViewHeaps.AllocCBV_SRV_UAVDescriptor(1, &m_shadowMapSRV);
    m_shadowMap.CreateDSV(0, &m_shadowMapDSV);
    m_shadowMap.CreateSRV(0, &m_shadowMapSRV);

    m_skyDome.OnCreate(pDevice, &m_uploadHeap, &m_resourceViewHeaps, &m_constantBufferRing, &m_vidMemBufferPool, "..\\Media\\Cauldron-Media\\envmaps\\papermill\\diffuse.dds", "..\\Media\\Cauldron-Media\\envmaps\\papermill\\specular.dds", DXGI_FORMAT_R16G16B16A16_FLOAT, 4);
    m_skyDomeProc.OnCreate(pDevice, &m_resourceViewHeaps, &m_constantBufferRing, &m_vidMemBufferPool, DXGI_FORMAT_R16G16B16A16_FLOAT, 1);
    m_wireframe.OnCreate(pDevice, &m_resourceViewHeaps, &m_constantBufferRing, &m_vidMemBufferPool, DXGI_FORMAT_R16G16B16A16_FLOAT, 1);
    m_wireframeBox.OnCreate(pDevice, &m_resourceViewHeaps, &m_constantBufferRing, &m_vidMemBufferPool);
    m_downSample.OnCreate(pDevice, &m_resourceViewHeaps, &m_constantBufferRing, &m_vidMemBufferPool, DXGI_FORMAT_R16G16B16A16_FLOAT);
    m_bloom.OnCreate(pDevice, &m_resourceViewHeaps, &m_constantBufferRing, &m_vidMemBufferPool, DXGI_FORMAT_R16G16B16A16_FLOAT);
    m_taa.OnCreate(pDevice, &m_resourceViewHeaps, &m_vidMemBufferPool);

    // Create tonemapping pass
    m_toneMappingPS.OnCreate(pDevice, &m_resourceViewHeaps, &m_constantBufferRing, &m_vidMemBufferPool, pSwapChain->GetFormat());
    m_toneMappingCS.OnCreate(pDevice, &m_resourceViewHeaps, &m_constantBufferRing);
    m_colorConversionPS.OnCreate(pDevice, &m_resourceViewHeaps, &m_constantBufferRing, &m_vidMemBufferPool, pSwapChain->GetFormat());

    // Initialize UI rendering resources
    m_imGUI.OnCreate(pDevice, &m_uploadHeap, &m_resourceViewHeaps, &m_constantBufferRing, pSwapChain->GetFormat());

    // initialize VRS generation CS
    m_variableShadingCode.OnCreate(pDevice, &m_resourceViewHeaps, &m_constantBufferRing, &m_vidMemBufferPool, pSwapChain->GetFormat());
    m_resourceViewHeaps.AllocCBV_SRV_UAVDescriptor(2, &m_variableShadingInputsSRV);
    m_resourceViewHeaps.AllocRTVDescriptor(1, &m_oldBackBufferRTV);
    m_resourceViewHeaps.AllocCBV_SRV_UAVDescriptor(1, &m_oldBackBufferSRV);

    // Make sure upload heap has finished uploading before continuing
#if (USE_VID_MEM==true)
    m_vidMemBufferPool.UploadData(m_uploadHeap.GetCommandList());
    m_uploadHeap.FlushAndFinish();
#endif
}

//--------------------------------------------------------------------------------------
//
// OnDestroy 
//
//--------------------------------------------------------------------------------------
void SampleRenderer::OnDestroy()
{
    TRACED;
    m_variableShadingCode.OnDestroy();

    m_imGUI.OnDestroy();
    m_colorConversionPS.OnDestroy();
    m_toneMappingCS.OnDestroy();
    m_toneMappingPS.OnDestroy();
    m_taa.OnDestroy();
    m_bloom.OnDestroy();
    m_downSample.OnDestroy();
    m_wireframeBox.OnDestroy();
    m_wireframe.OnDestroy();
    m_skyDomeProc.OnDestroy();
    m_skyDome.OnDestroy();
    m_shadowMap.OnDestroy();
#if USE_SHADOWMASK
    m_shadowResolve.OnDestroy();
#endif
    m_gBuffer.OnDestroy();

    m_uploadHeap.OnDestroy();
    m_gpuTimer.OnDestroy();
    m_vidMemBufferPool.OnDestroy();
    m_constantBufferRing.OnDestroy();
    m_resourceViewHeaps.OnDestroy();
    m_commandListRing.OnDestroy();
}

//--------------------------------------------------------------------------------------
//
// OnCreateWindowSizeDependentResources
//
//--------------------------------------------------------------------------------------
void SampleRenderer::OnCreateWindowSizeDependentResources(SwapChain* pSwapChain, uint32_t Width, uint32_t Height)
{
    m_width = Width;
    m_height = Height;

    // Set the viewport
    //
    m_viewport = { 0.0f, 0.0f, static_cast<float>(Width), static_cast<float>(Height), 0.0f, 1.0f };

    // Create scissor rectangle
    //
    m_rectScissor = { 0, 0, (LONG)Width, (LONG)Height };

    // Create GBuffer
    //
    m_gBuffer.OnCreateWindowSizeDependentResources(pSwapChain, Width, Height);
    m_renderPassDepthAndMotion.OnCreateWindowSizeDependentResources(Width, Height);
    m_renderPassForward.OnCreateWindowSizeDependentResources(Width, Height);
    m_renderPassJustDepthAndHdr.OnCreateWindowSizeDependentResources(Width, Height);

    m_taa.OnCreateWindowSizeDependentResources(Width, Height, &m_gBuffer);

    // update bloom and downscaling effect
    //
    m_downSample.OnCreateWindowSizeDependentResources(m_width, m_height, &m_gBuffer.m_HDR, 5); //downsample the HDR texture 5 times
    m_bloom.OnCreateWindowSizeDependentResources(m_width / 2, m_height / 2, m_downSample.GetTexture(), 5, &m_gBuffer.m_HDR);

    // Update pipelines in case the format of the RTs changed (this happens when going HDR)
    m_colorConversionPS.UpdatePipelines(pSwapChain->GetFormat(), pSwapChain->GetDisplayMode());
    m_toneMappingPS.UpdatePipelines(pSwapChain->GetFormat());
    m_imGUI.UpdatePipeline((pSwapChain->GetDisplayMode() == DISPLAYMODE_SDR) ? pSwapChain->GetFormat() : m_gBuffer.m_HDR.GetFormat());

    m_variableShadingCode.OnCreateWindowSizeDependentResources(Width, Height);

    CD3DX12_RESOURCE_DESC RDesc = CD3DX12_RESOURCE_DESC::Tex2D((pSwapChain->GetDisplayMode() == DISPLAYMODE_SDR) ? DXGI_FORMAT_R8G8B8A8_UNORM : m_gBuffer.m_HDR.GetFormat(), Width, Height, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    m_oldBackBuffer.InitRenderTarget(m_device, "OldBackbuffer", &RDesc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    m_oldBackBuffer.CreateSRV(0, &m_oldBackBufferSRV);
    m_oldBackBuffer.CreateRTV(0, &m_oldBackBufferRTV);

    m_oldBackBuffer.CreateSRV(0, &m_variableShadingInputsSRV);
    m_gBuffer.m_MotionVectors.CreateSRV(1, &m_variableShadingInputsSRV);
}


//--------------------------------------------------------------------------------------
//
// OnDestroyWindowSizeDependentResources
//
//--------------------------------------------------------------------------------------
void SampleRenderer::OnDestroyWindowSizeDependentResources()
{
    TRACED;
    m_variableShadingCode.OnDestroyWindowSizeDependentResources();

    m_bloom.OnDestroyWindowSizeDependentResources();
    m_downSample.OnDestroyWindowSizeDependentResources();

    m_gBuffer.OnDestroyWindowSizeDependentResources();

    m_oldBackBuffer.OnDestroy();

    m_taa.OnDestroyWindowSizeDependentResources();
}

//--------------------------------------------------------------------------------------
//
// LoadScene
//
//--------------------------------------------------------------------------------------
int SampleRenderer::LoadScene(GLTFCommon* pGLTFCommon, int stage)
{
    // show loading progress
    //
    ImGui::OpenPopup("Loading");
    if (ImGui::BeginPopupModal("Loading", NULL, ImGuiWindowFlags_AlwaysAutoResize))
    {
        float progress = (float)stage / 13.0f;
        ImGui::ProgressBar(progress, ImVec2(0.f, 0.f), NULL);
        ImGui::EndPopup();
    }

    // use multithreading
    AsyncPool* pAsyncPool = &m_asyncPool;

    // Loading stages
    //
    if (stage == 0)
    {
    }
    else if (stage == 5)
    {
        Profile p("m_gltfLoader->Load");

        m_gltfTexturesAndBuffers = new GLTFTexturesAndBuffers();
        m_gltfTexturesAndBuffers->OnCreate(m_device, pGLTFCommon, &m_uploadHeap, &m_vidMemBufferPool, &m_constantBufferRing);
    }
    else if (stage == 6)
    {
        Profile p("LoadTextures");

        // here we are loading onto the GPU all the textures and the inverse matrices
        // this data will be used to create the PBR and Depth passes       
        m_gltfTexturesAndBuffers->LoadTextures(pAsyncPool);
    }
    else if (stage == 7)
    {
        Profile p("m_gltfDepth->OnCreate");

        //create the glTF's textures, VBs, IBs, shaders and descriptors for this particular pass
        m_gltfDepth = new GltfDepthPass();
        m_gltfDepth->OnCreate(
            m_device,
            &m_uploadHeap,
            &m_resourceViewHeaps,
            &m_constantBufferRing,
            &m_vidMemBufferPool,
            m_gltfTexturesAndBuffers,
            pAsyncPool
        );
    }
    else if (stage == 8)
    {
        Profile p("m_gltfMotionVectors->OnCreate");

        m_gltfMotionVectors = new GltfMotionVectorsPass();
        m_gltfMotionVectors->OnCreate(
            m_device,
            &m_uploadHeap,
            &m_resourceViewHeaps,
            &m_constantBufferRing,
            &m_vidMemBufferPool,
            m_gltfTexturesAndBuffers,
            m_gBuffer.m_MotionVectors.GetFormat(),
            DXGI_FORMAT_UNKNOWN,
            pAsyncPool
        );
    }
    else if (stage == 9)
    {
        Profile p("m_gltfPBR->OnCreate");

        // same thing as above but for the PBR pass
        m_gltfPBR = new GltfPbrPass();
        m_gltfPBR->OnCreate(
            m_device,
            &m_uploadHeap,
            &m_resourceViewHeaps,
            &m_constantBufferRing,
            m_gltfTexturesAndBuffers,
            &m_skyDome,
            false,                  // use a SSAO mask
            false,
            &m_renderPassForward,
            pAsyncPool
        );

    }
    else if (stage == 10)
    {
        Profile p("m_gltfBBox->OnCreate");

        // just a bounding box pass that will draw boundingboxes instead of the geometry itself
        m_gltfBBox = new GltfBBoxPass();
        m_gltfBBox->OnCreate(
            m_device,
            &m_uploadHeap,
            &m_resourceViewHeaps,
            &m_constantBufferRing,
            &m_vidMemBufferPool,
            m_gltfTexturesAndBuffers,
            &m_wireframe
        );
#if (USE_VID_MEM==true)
        // we are borrowing the upload heap command list for uploading to the GPU the IBs and VBs
        m_vidMemBufferPool.UploadData(m_uploadHeap.GetCommandList());
#endif    
    }
    else if (stage == 11)
    {
        Profile p("Flush");

        m_uploadHeap.FlushAndFinish();

#if (USE_VID_MEM==true)
        //once everything is uploaded we dont need he upload heaps anymore
        m_vidMemBufferPool.FreeUploadHeap();
#endif

        // tell caller that we are done loading the map
        return 0;
    }

    stage++;
    return stage;
}

//--------------------------------------------------------------------------------------
//
// UnloadScene
//
//--------------------------------------------------------------------------------------
void SampleRenderer::UnloadScene()
{
    m_device->GPUFlush();

    if (m_gltfPBR)
    {
        m_gltfPBR->OnDestroy();
        delete m_gltfPBR;
        m_gltfPBR = NULL;
    }

    if (m_gltfMotionVectors)
    {
        m_gltfMotionVectors->OnDestroy();
        delete m_gltfMotionVectors;
        m_gltfMotionVectors = NULL;
    }

    if (m_gltfDepth)
    {
        m_gltfDepth->OnDestroy();
        delete m_gltfDepth;
        m_gltfDepth = NULL;
    }

    if (m_gltfBBox)
    {
        m_gltfBBox->OnDestroy();
        delete m_gltfBBox;
        m_gltfBBox = NULL;
    }

    if (m_gltfTexturesAndBuffers)
    {
        m_gltfTexturesAndBuffers->OnDestroy();
        delete m_gltfTexturesAndBuffers;
        m_gltfTexturesAndBuffers = NULL;
    }

}

D3D12_SHADING_RATE VrsBaseRate_to_D3DShadingRate(UINT vrsBaseRate)
{
    TRACED;
    switch (vrsBaseRate)
    {
    case 1:
        return D3D12_SHADING_RATE_1X2;
    case 2:
        return D3D12_SHADING_RATE_2X1;
    case 3:
        return D3D12_SHADING_RATE_2X2;
    case 4:
        return D3D12_SHADING_RATE_2X4;
    case 5:
        return D3D12_SHADING_RATE_4X2;
    case 6:
        return D3D12_SHADING_RATE_4X4;
    default:
        return D3D12_SHADING_RATE_1X1;
    }
}

//--------------------------------------------------------------------------------------
//
// OnRender
//
//--------------------------------------------------------------------------------------
void SampleRenderer::OnRender(State* pState, SwapChain* pSwapChain)
{
    // Timing values
    //
    UINT64 gpuTicksPerSecond;
    m_device->GetGraphicsQueue()->GetTimestampFrequency(&gpuTicksPerSecond);

    // Let our resource managers do some house keeping
    //
    m_commandListRing.OnBeginFrame();
    m_constantBufferRing.OnBeginFrame();
    m_gpuTimer.OnBeginFrame(gpuTicksPerSecond, &m_timeStamps);

    m_gpuTimer.GetTimeStampUser({ "time (s)", pState->m_time });

    if (pState->m_useTAA)
    {
        static uint32_t Seed;
        pState->m_camera.SetProjectionJitter(m_width, m_height, Seed);
    }

    // Sets the perFrame data 
    //
    per_frame* pPerFrame = NULL;
    if (m_gltfTexturesAndBuffers)
    {
        // fill as much as possible using the GLTF (camera, lights, ...)
        pPerFrame = m_gltfTexturesAndBuffers->m_pGLTFCommon->SetPerFrameData(pState->m_camera);

        // Set some lighting factors
        pPerFrame->iblFactor = pState->m_iblFactor;
        pPerFrame->emmisiveFactor = pState->m_emisiveFactor;
        pPerFrame->invScreenResolution[0] = 1.0f / ((float)m_width);
        pPerFrame->invScreenResolution[1] = 1.0f / ((float)m_height);

        // Set shadowmaps bias and an index that indicates the rectangle of the atlas in which depth will be rendered
        uint32_t shadowMapIndex = 0;
        for (uint32_t i = 0; i < pPerFrame->lightCount; i++)
        {
            if ((shadowMapIndex < 4) && (pPerFrame->lights[i].type == LightType_Spot))
            {
                pPerFrame->lights[i].shadowMapIndex = shadowMapIndex++; // set the shadowmap index
                pPerFrame->lights[i].depthBias = 70.0f / 100000.0f;
            }
            else if ((shadowMapIndex < 4) && (pPerFrame->lights[i].type == LightType_Directional))
            {
                pPerFrame->lights[i].shadowMapIndex = shadowMapIndex++; // set the shadowmap index
                pPerFrame->lights[i].depthBias = 1000.0f / 100000.0f;
            }
            else
            {
                pPerFrame->lights[i].shadowMapIndex = -1;   // no shadow for this light
            }
        }

        m_gltfTexturesAndBuffers->SetPerFrameConstants();
        m_gltfTexturesAndBuffers->SetSkinningMatricesForSkeletons();
    }

    // command buffer calls
    //
    ID3D12GraphicsCommandList* pCmdLst1 = m_commandListRing.GetNewCommandList();

    m_gpuTimer.GetTimeStamp(pCmdLst1, "Begin Frame");

    // Setup VRS
    D3D12_SHADING_RATE_COMBINER combiners[D3D12_RS_SET_SHADING_RATE_COMBINER_COUNT] = {
        D3D12_SHADING_RATE_COMBINER_PASSTHROUGH,            // this sample is not supporting shader based VRS
        (D3D12_SHADING_RATE_COMBINER)pState->m_vrsImageCombiner
    };
    m_variableShadingCode.SetShadingRate(VrsBaseRate_to_D3DShadingRate(pState->m_vrsBaseRate), combiners);


    pCmdLst1->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(pSwapChain->GetCurrentBackBufferResource(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

    // Render spot lights shadow map atlas  ------------------------------------------
    //
    if (m_gltfDepth && pPerFrame != NULL)
    {
        pCmdLst1->ClearDepthStencilView(m_shadowMapDSV.GetCPU(), D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
        m_gpuTimer.GetTimeStamp(pCmdLst1, "Clear shadow map");

        uint32_t shadowMapIndex = 0;
        for (uint32_t i = 0; i < pPerFrame->lightCount; i++)
        {
            if (!(pPerFrame->lights[i].type == LightType_Spot || pPerFrame->lights[i].type == LightType_Directional))
                continue;

            // Set the RT's quadrant where to render the shadomap (these viewport offsets need to match the ones in shadowFiltering.h)
            uint32_t viewportOffsetsX[4] = { 0, 1, 0, 1 };
            uint32_t viewportOffsetsY[4] = { 0, 0, 1, 1 };
            uint32_t viewportWidth = m_shadowMap.GetWidth() / 2;
            uint32_t viewportHeight = m_shadowMap.GetHeight() / 2;
            SetViewportAndScissor(pCmdLst1, viewportOffsetsX[i] * viewportWidth, viewportOffsetsY[i] * viewportHeight, viewportWidth, viewportHeight);
            pCmdLst1->OMSetRenderTargets(0, NULL, false, &m_shadowMapDSV.GetCPU());

            GltfDepthPass::per_frame* cbDepthPerFrame = m_gltfDepth->SetPerFrameConstants();
            cbDepthPerFrame->mViewProj = pPerFrame->lights[i].mLightViewProj;

            m_gltfDepth->Draw(pCmdLst1);

            m_gpuTimer.GetTimeStamp(pCmdLst1, "Shadow map");
            shadowMapIndex++;
        }
    }

    pCmdLst1->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_shadowMap.GetResource(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));

    // Render Scene to the GBuffer ------------------------------------------------
    //
    if (pPerFrame != NULL)
    {
        pCmdLst1->RSSetViewports(1, &m_viewport);
        pCmdLst1->RSSetScissorRects(1, &m_rectScissor);

        if (m_gltfPBR)
        {
            std::vector<GltfPbrPass::BatchList> opaque, transparent;
            m_gltfPBR->BuildBatchLists(&opaque, &transparent);

            // Render depth and motion vectors
            // 
            {
                m_renderPassDepthAndMotion.BeginPass(pCmdLst1, true);

                GltfMotionVectorsPass::per_frame* cbDepthPerFrame = m_gltfMotionVectors->SetPerFrameConstants();
                cbDepthPerFrame->mCurrViewProj = pPerFrame->mCameraCurrViewProj;
                cbDepthPerFrame->mPrevViewProj = pPerFrame->mCameraPrevViewProj;

                m_gltfMotionVectors->Draw(pCmdLst1);

                m_gpuTimer.GetTimeStamp(pCmdLst1, "Motion vectors");
                m_renderPassDepthAndMotion.EndPass();
            }

            // Generate VRS rate image
            {
                m_variableShadingCode.SetAdditionalShadingRatesAllowed(pState->m_allowAdditionalVrsRates);
                m_variableShadingCode.SetVarianceThreshold(pState->m_vrsVarianceThreshold);
                m_variableShadingCode.SetMotionFactor(pState->m_vrsMotionFactor);

                if (pState->m_vrsImageCombiner != 0)
                {
                    UserMarker marker(pCmdLst1, "Generate VRS Image");

                    {
                        CD3DX12_RESOURCE_BARRIER barriers[] = {
                        CD3DX12_RESOURCE_BARRIER::Transition(m_gBuffer.m_HDR.GetResource(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
                        CD3DX12_RESOURCE_BARRIER::Transition(m_gBuffer.m_MotionVectors.GetResource(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
                        };
                        pCmdLst1->ResourceBarrier(ARRAYSIZE(barriers), barriers);
                    }

                    // generate VRS map for the frame:
                    //   analyze blocks for variance
                    //   will result in feedback loop for still images (lower shading rate=> less variance)
                    m_variableShadingCode.ComputeVrsMap(pCmdLst1, &m_variableShadingInputsSRV);

                    {
                        CD3DX12_RESOURCE_BARRIER barriers[] = {
                            CD3DX12_RESOURCE_BARRIER::Transition(m_gBuffer.m_HDR.GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET),
                            CD3DX12_RESOURCE_BARRIER::Transition(m_gBuffer.m_MotionVectors.GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET),
                        };
                        pCmdLst1->ResourceBarrier(ARRAYSIZE(barriers), barriers);
                    }

                    m_gpuTimer.GetTimeStamp(pCmdLst1, "Gen VRSImg");
                }
                else if (pState->m_vrsImageCombiner != m_lastVrsImageCombiner)
                {
                    m_variableShadingCode.ClearVrsMap(pCmdLst1);
                }
                m_lastVrsImageCombiner = pState->m_vrsImageCombiner;
            }

            m_variableShadingCode.StartVrsRendering(pCmdLst1);

            // Render opaque geometry
            // 
            {
                m_renderPassForward.BeginPass(pCmdLst1, true);
                m_gltfPBR->DrawBatchList(pCmdLst1, &m_shadowMapSRV, &opaque);
                m_gpuTimer.GetTimeStamp(pCmdLst1, "PBR Forward");
                m_renderPassForward.EndPass();
            }

            // draw skydome
            // 
            {
                m_renderPassJustDepthAndHdr.BeginPass(pCmdLst1, false);

                // Render skydome
                //
                if (pState->m_skyDomeType == 1)
                {
                    XMMATRIX clipToView = XMMatrixInverse(NULL, pPerFrame->mCameraCurrViewProj);
                    m_skyDome.Draw(pCmdLst1, clipToView);
                    m_gpuTimer.GetTimeStamp(pCmdLst1, "Skydome cube");
                }
                else if (pState->m_skyDomeType == 0)
                {
                    SkyDomeProc::Constants skyDomeConstants;
                    skyDomeConstants.invViewProj = XMMatrixInverse(NULL, pPerFrame->mCameraCurrViewProj);
                    skyDomeConstants.vSunDirection = XMVectorSet(1.0f, 0.05f, 0.0f, 0.0f);
                    skyDomeConstants.turbidity = 10.0f;
                    skyDomeConstants.rayleigh = 2.0f;
                    skyDomeConstants.mieCoefficient = 0.005f;
                    skyDomeConstants.mieDirectionalG = 0.8f;
                    skyDomeConstants.luminance = 1.0f;
                    skyDomeConstants.sun = false;
                    m_skyDomeProc.Draw(pCmdLst1, skyDomeConstants);

                    m_gpuTimer.GetTimeStamp(pCmdLst1, "Skydome proc");
                }

                m_renderPassJustDepthAndHdr.EndPass();
            }

            // draw transparent geometry
            //
            {
                m_renderPassForward.BeginPass(pCmdLst1, false);

                std::sort(transparent.begin(), transparent.end());
                m_gltfPBR->DrawBatchList(pCmdLst1, &m_shadowMapSRV, &transparent);
                m_gpuTimer.GetTimeStamp(pCmdLst1, "PBR Transparent");

                m_renderPassForward.EndPass();
            }
        }

        // draw object's bounding boxes
        //
        if (m_gltfBBox && pPerFrame != NULL)
        {
            if (pState->m_drawBoundingBoxes)
            {
                m_gltfBBox->Draw(pCmdLst1, pPerFrame->mCameraCurrViewProj);

                m_gpuTimer.GetTimeStamp(pCmdLst1, "Bounding Box");
            }
        }

        // draw light's frustums
        //
        if (pState->m_drawLightFrustum && pPerFrame != NULL)
        {
            UserMarker marker(pCmdLst1, "light frustrums");

            XMVECTOR vCenter = XMVectorSet(0.0f, 0.0f, 0.5f, 0.0f);
            XMVECTOR vRadius = XMVectorSet(1.0f, 1.0f, 0.5f, 0.0f);
            XMVECTOR vColor = XMVectorSet(1.0f, 1.0f, 1.0f, 1.0f);
            for (uint32_t i = 0; i < pPerFrame->lightCount; i++)
            {
                XMMATRIX spotlightMatrix = XMMatrixInverse(NULL, pPerFrame->lights[i].mLightViewProj);
                XMMATRIX worldMatrix = spotlightMatrix * pPerFrame->mCameraCurrViewProj;
                m_wireframeBox.Draw(pCmdLst1, &m_wireframe, worldMatrix, vCenter, vRadius, vColor);
            }

            m_gpuTimer.GetTimeStamp(pCmdLst1, "Light's frustum");
        }
        m_variableShadingCode.EndVrsRendering(pCmdLst1);
    }

    D3D12_RESOURCE_BARRIER preResolve[2] = {
        CD3DX12_RESOURCE_BARRIER::Transition(m_shadowMap.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE),
        CD3DX12_RESOURCE_BARRIER::Transition(m_gBuffer.m_HDR.GetResource(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
    };
    pCmdLst1->ResourceBarrier(2, preResolve);

    // Post proc---------------------------------------------------------------------------
    //

    // Bloom, takes HDR as input and applies bloom to it.
    //
    {
        D3D12_CPU_DESCRIPTOR_HANDLE renderTargets[] = { m_gBuffer.m_HDRRTV.GetCPU() };
        pCmdLst1->OMSetRenderTargets(ARRAYSIZE(renderTargets), renderTargets, false, NULL);

        m_downSample.Draw(pCmdLst1);
        //m_downSample.Gui();
        m_gpuTimer.GetTimeStamp(pCmdLst1, "Downsample");

        m_bloom.Draw(pCmdLst1, &m_gBuffer.m_HDR);
        //m_bloom.Gui();
        m_gpuTimer.GetTimeStamp(pCmdLst1, "Bloom");
    }

    // Apply TAA & Sharpen to m_HDR
    //
    if (pState->m_useTAA)
    {
        m_taa.Draw(pCmdLst1, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        m_gpuTimer.GetTimeStamp(pCmdLst1, "TAA");
    }

    // If using FreeSync HDR we need to to the tonemapping in-place and then apply the GUI, later we'll apply the color conversion into the swapchain
    //
    if (pSwapChain->GetDisplayMode() != DISPLAYMODE_SDR)
    {
        // In place Tonemapping ------------------------------------------------------------------------
        //
        {
            D3D12_RESOURCE_BARRIER hdrToUAV = CD3DX12_RESOURCE_BARRIER::Transition(m_gBuffer.m_HDR.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            pCmdLst1->ResourceBarrier(1, &hdrToUAV);

            m_toneMappingCS.Draw(pCmdLst1, &m_gBuffer.m_HDRUAV, pState->m_exposure, pState->m_toneMapper, m_width, m_height);
        }

        // Copy Backbuffer for next frame--------------------------------------------------
        //
        {
            D3D12_RESOURCE_BARRIER preResolve[2] = {
                CD3DX12_RESOURCE_BARRIER::Transition(m_gBuffer.m_HDR.GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE),
                CD3DX12_RESOURCE_BARRIER::Transition(m_oldBackBuffer.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST)
            };
            pCmdLst1->ResourceBarrier(2, preResolve);

            pCmdLst1->CopyResource(m_oldBackBuffer.GetResource(), m_gBuffer.m_HDR.GetResource());

            D3D12_RESOURCE_BARRIER postResolve[2] = {
                CD3DX12_RESOURCE_BARRIER::Transition(m_gBuffer.m_HDR.GetResource(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
                CD3DX12_RESOURCE_BARRIER::Transition(m_oldBackBuffer.GetResource(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_RENDER_TARGET)
            };
            pCmdLst1->ResourceBarrier(2, postResolve);
        }

        // Render VRS Overlay -------------------------------------------------------------
        //
        if (pState->m_showVRSMap)
        {
            m_variableShadingCode.DrawOverlay(pCmdLst1);

            m_gpuTimer.GetTimeStamp(pCmdLst1, "VRS Overlay");
        }

        // Render HUD  ------------------------------------------------------------------------
        //
        static bool bShowUI = true;
        if (bShowUI)
        {
            pCmdLst1->RSSetViewports(1, &m_viewport);
            pCmdLst1->RSSetScissorRects(1, &m_rectScissor);
            pCmdLst1->OMSetRenderTargets(1, &m_gBuffer.m_HDRRTV.GetCPU(), true, NULL);

            m_imGUI.Draw(pCmdLst1);

            D3D12_RESOURCE_BARRIER hdrToSRV = CD3DX12_RESOURCE_BARRIER::Transition(m_gBuffer.m_HDR.GetResource(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            pCmdLst1->ResourceBarrier(1, &hdrToSRV);

            m_gpuTimer.GetTimeStamp(pCmdLst1, "ImGUI Rendering");
        }
    }

    // submit command buffer #1
    ThrowIfFailed(pCmdLst1->Close());
    ID3D12CommandList* CmdListList1[] = { pCmdLst1 };
    m_device->GetGraphicsQueue()->ExecuteCommandLists(1, CmdListList1);

    // Wait for swapchain (we are going to render to it) -----------------------------------
    //
    pSwapChain->WaitForSwapChain();

    ID3D12GraphicsCommandList* pCmdLst2 = m_commandListRing.GetNewCommandList();

    pCmdLst2->RSSetViewports(1, &m_viewport);
    pCmdLst2->RSSetScissorRects(1, &m_rectScissor);
    pCmdLst2->OMSetRenderTargets(1, pSwapChain->GetCurrentBackBufferRTV(), true, NULL);

    if (pSwapChain->GetDisplayMode() != DISPLAYMODE_SDR)
    {
        // FS HDR mode! Apply color conversion now.
        //
        m_colorConversionPS.Draw(pCmdLst2, &m_gBuffer.m_HDRSRV);
        m_gpuTimer.GetTimeStamp(pCmdLst2, "Color conversion");

        pCmdLst2->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_gBuffer.m_HDR.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET));
    }
    else
    {
        // non FS HDR mode, that is SDR, here we apply the tonemapping from the HDR into the swapchain and then we render the GUI
        //

        // Tonemapping ------------------------------------------------------------------------
        //
        {
            m_toneMappingPS.Draw(pCmdLst2, &m_gBuffer.m_HDRSRV, pState->m_exposure, pState->m_toneMapper);
            m_gpuTimer.GetTimeStamp(pCmdLst2, "Tone mapping");

            pCmdLst2->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_gBuffer.m_HDR.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET));
        }

        // Copy Backbuffer for next frame--------------------------------------------------
        //
        {
            D3D12_RESOURCE_BARRIER preResolve[2] = {
                CD3DX12_RESOURCE_BARRIER::Transition(pSwapChain->GetCurrentBackBufferResource(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE),
                CD3DX12_RESOURCE_BARRIER::Transition(m_oldBackBuffer.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST)
            };
            pCmdLst2->ResourceBarrier(2, preResolve);

            pCmdLst2->CopyResource(m_oldBackBuffer.GetResource(), pSwapChain->GetCurrentBackBufferResource());

            D3D12_RESOURCE_BARRIER postResolve[2] = {
                CD3DX12_RESOURCE_BARRIER::Transition(pSwapChain->GetCurrentBackBufferResource(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET),
                CD3DX12_RESOURCE_BARRIER::Transition(m_oldBackBuffer.GetResource(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
            };
            pCmdLst2->ResourceBarrier(2, postResolve);
        }

        // Render VRS Overlay -------------------------------------------------------------
        //
        if (pState->m_showVRSMap)
        {
            m_variableShadingCode.DrawOverlay(pCmdLst2);

            m_gpuTimer.GetTimeStamp(pCmdLst2, "VRS Overlay");
        }

        // Render HUD  ------------------------------------------------------------------------
        //
        {
            m_imGUI.Draw(pCmdLst2);
            m_gpuTimer.GetTimeStamp(pCmdLst2, "ImGUI Rendering");
        }
    }

    if (pState->m_screenShotName != NULL)
    {
        m_saveTexture.CopyRenderTargetIntoStagingTexture(m_device->GetDevice(), pCmdLst2, pSwapChain->GetCurrentBackBufferResource(), D3D12_RESOURCE_STATE_RENDER_TARGET);
    }

    // Transition swapchain into present mode

    pCmdLst2->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(pSwapChain->GetCurrentBackBufferResource(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

    m_gpuTimer.OnEndFrame();

    m_gpuTimer.CollectTimings(pCmdLst2);

    // Close & Submit the command list #2 -------------------------------------------------
    //
    ThrowIfFailed(pCmdLst2->Close());

    ID3D12CommandList* CmdListList2[] = { pCmdLst2 };
    m_device->GetGraphicsQueue()->ExecuteCommandLists(1, CmdListList2);

    if (pState->m_screenShotName != NULL)
    {
        m_saveTexture.SaveStagingTextureAsJpeg(m_device->GetDevice(), m_device->GetGraphicsQueue(), pState->m_screenShotName->c_str());
        pState->m_screenShotName = NULL;
    }

    // Update previous camera matrices
    //
    pState->m_camera.UpdatePreviousMatrices();
}
