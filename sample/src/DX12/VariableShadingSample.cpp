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

#include "VariableShadingSample.h"

VariableShadingSample::VariableShadingSample(LPCSTR name) : FrameworkWindows(name)
{
    m_state = {};

    m_lastFrameTime = MillisecondsNow();
    m_time = 0;
    m_play = true;

    m_gltfLoader = NULL;
    m_currentDisplayMode = DISPLAYMODE_SDR;
}

//--------------------------------------------------------------------------------------
//
// OnParseCommandLine
//
//--------------------------------------------------------------------------------------
void VariableShadingSample::OnParseCommandLine(LPSTR lpCmdLine, uint32_t* pWidth, uint32_t* pHeight, bool* pbFullScreen)
{
    // set some default values
    *pWidth = 1920;
    *pHeight = 1080;
    m_activeScene = 0; //load the first one by default
    *pbFullScreen = false;
    m_state.m_isBenchmarking = false;
    m_isCpuValidationLayerEnabled = false;
    m_isGpuValidationLayerEnabled = false;
    m_activeCamera = 0;
    m_stablePowerState = false;

    //read globals
    auto process = [&](json jData)
    {
        *pWidth = jData.value("width", *pWidth);
        *pHeight = jData.value("height", *pHeight);
        *pbFullScreen = jData.value("fullScreen", *pbFullScreen);
        m_activeScene = jData.value("activeScene", m_activeScene);
        m_activeCamera = jData.value("activeCamera", m_activeCamera);
        m_isCpuValidationLayerEnabled = jData.value("CpuValidationLayerEnabled", m_isCpuValidationLayerEnabled);
        m_isGpuValidationLayerEnabled = jData.value("GpuValidationLayerEnabled", m_isGpuValidationLayerEnabled);
        m_state.m_isBenchmarking = jData.value("benchmark", m_state.m_isBenchmarking);
        m_stablePowerState = jData.value("stablePowerState", m_stablePowerState);
    };

    //read json globals from commandline
    //
    try
    {
        if (strlen(lpCmdLine) > 0)
        {
            auto j3 = json::parse(lpCmdLine);
            process(j3);
        }
    }
    catch (json::parse_error)
    {
        Trace("Error parsing commandline\n");
        exit(0);
    }

    // read config file (and override values from commandline if so)
    //
    {
        std::ifstream f("VariableShadingSample.json");
        if (!f)
        {
            MessageBox(NULL, "Config file not found!\n", "Cauldron Panic!", MB_ICONERROR);
            exit(0);
        }

        try
        {
            f >> m_jsonConfigFile;
        }
        catch (json::parse_error)
        {
            MessageBox(NULL, "Error parsing VariableShadingSample.json!\n", "Cauldron Panic!", MB_ICONERROR);
            exit(0);
        }
    }


    json globals = m_jsonConfigFile["globals"];
    process(globals);

    // get the list of scenes
    for (const auto& scene : m_jsonConfigFile["scenes"])
        m_sceneNames.push_back(scene["name"]);
}

//--------------------------------------------------------------------------------------
//
// OnCreate
//
//--------------------------------------------------------------------------------------
void VariableShadingSample::OnCreate(HWND hWnd)
{
    // Create Device
    //
    m_device.OnCreate("Variable Shading Sample", "Cauldron", m_isCpuValidationLayerEnabled, m_isGpuValidationLayerEnabled, hWnd);
    m_device.CreatePipelineCache();

    // set stable power state
    if (m_stablePowerState)
        m_device.GetDevice()->SetStablePowerState(TRUE);

    //init the shader compiler
    InitDirectXCompiler();
    CreateShaderCache();

    // Create Swapchain
    //
    uint32_t dwNumberOfBackBuffers = 2;
    m_swapChain.OnCreate(&m_device, dwNumberOfBackBuffers, hWnd);

    // Create a instance of the renderer and initialize it, we need to do that for each GPU
    //
    m_node = new SampleRenderer();
    m_node->OnCreate(&m_device, &m_swapChain);

    // init GUI (non gfx stuff)
    //
    ImGUI_Init((void*)hWnd);

    // Init Camera, looking at the origin
    //
    m_roll = 0.0f;
    m_pitch = 0.0f;
    m_distance = 3.5f;

    // init GUI state
    m_state.m_toneMapper = 0;
    m_state.m_useTAA = true;
    m_state.m_skyDomeType = 0;
    m_state.m_exposure = 1.0f;
    m_state.m_iblFactor = 2.0f;
    m_state.m_emisiveFactor = 1.0f;
    m_state.m_drawLightFrustum = false;
    m_state.m_drawBoundingBoxes = false;
    m_state.m_camera.LookAt(m_roll, m_pitch, m_distance, XMVectorSet(0, 0, 0, 0));

    m_state.m_allowAdditionalVrsRates = true;
    m_state.m_enableShadingRateImage = false;
    m_state.m_vrsImageCombiner = 0;
    m_state.m_showVRSMap = false;
    m_state.m_vrsVarianceThreshold = 0.05f;
    m_state.m_vrsMotionFactor = 0.05f;
    m_state.m_hideUI = false;

    LoadScene(0);
}

//--------------------------------------------------------------------------------------
//
// OnDestroy
//
//--------------------------------------------------------------------------------------
void VariableShadingSample::OnDestroy()
{
    ImGUI_Shutdown();

    m_device.GPUFlush();

    // Fullscreen state should always be false before exiting the app.
    m_swapChain.SetFullScreen(false);

    m_node->UnloadScene();
    m_node->OnDestroyWindowSizeDependentResources();
    m_node->OnDestroy();

    delete m_node;

    m_swapChain.OnDestroyWindowSizeDependentResources();
    m_swapChain.OnDestroy();

    //shut down the shader compiler 
    DestroyShaderCache(&m_device);

    if (m_gltfLoader)
    {
        delete m_gltfLoader;
        m_gltfLoader = NULL;
    }

    m_device.OnDestroy();
}

//--------------------------------------------------------------------------------------
//
// OnEvent, win32 sends us events and we forward them to ImGUI
//
//--------------------------------------------------------------------------------------
bool VariableShadingSample::OnEvent(MSG msg)
{
    if (ImGUI_WndProcHandler(msg.hwnd, msg.message, msg.wParam, msg.lParam))
        return true;

    return true;
}

//--------------------------------------------------------------------------------------
//
// SetFullScreen
//
//--------------------------------------------------------------------------------------
void VariableShadingSample::SetFullScreen(bool fullscreen)
{
    m_device.GPUFlush();

    // when going to windowed make sure we are always using SDR
    if ((fullscreen == false) && (m_currentDisplayMode != DISPLAYMODE_SDR))
        m_currentDisplayMode = DISPLAYMODE_SDR;

    m_swapChain.SetFullScreen(fullscreen);
}

//--------------------------------------------------------------------------------------
//
// OnResize
//
//--------------------------------------------------------------------------------------
void VariableShadingSample::OnResize(uint32_t width, uint32_t height, DisplayModes displayMode)
{
    if (m_Width != width || m_Height != height || m_currentDisplayMode != displayMode)
    {
        // Flush GPU
        //
        m_device.GPUFlush();

        // destroy resources (if were not minimized)
        //
        if (m_Width > 0 && m_Height > 0)
        {
            if (m_node != NULL)
            {
                m_node->OnDestroyWindowSizeDependentResources();
            }
            m_swapChain.OnDestroyWindowSizeDependentResources();
        }

        m_Width = width;
        m_Height = height;
        m_currentDisplayMode = displayMode;

        // if resizing but not minimizing the recreate it with the new size
        //
        if (m_Width > 0 && m_Height > 0)
        {
            m_swapChain.OnCreateWindowSizeDependentResources(m_Width, m_Height, false, m_currentDisplayMode);
            if (m_node != NULL)
            {
                m_node->OnCreateWindowSizeDependentResources(&m_swapChain, m_Width, m_Height);
            }
        }
    }
    m_state.m_camera.SetFov(XM_PI / 4, m_Width, m_Height, 1.0f, 1000.0f);
}

//--------------------------------------------------------------------------------------
//
// LoadScene
//
//--------------------------------------------------------------------------------------
void VariableShadingSample::LoadScene(int sceneIndex)
{
    json scene = m_jsonConfigFile["scenes"][sceneIndex];

    // release everything and load the GLTF, just the light json data, the rest (textures and geometry) will be done in the main loop
    if (m_gltfLoader != NULL)
    {
        m_node->UnloadScene();
        m_node->OnDestroyWindowSizeDependentResources();
        m_node->OnDestroy();
        m_gltfLoader->Unload();
        m_node->OnCreate(&m_device, &m_swapChain);
        m_node->OnCreateWindowSizeDependentResources(&m_swapChain, m_Width, m_Height);
    }

    delete(m_gltfLoader);
    m_gltfLoader = new GLTFCommon();
    if (m_gltfLoader->Load(scene["directory"], scene["filename"]) == false)
    {
        MessageBox(NULL, "The selected model couldn't be found, please check the documentation", "Cauldron Panic!", MB_ICONERROR);
        exit(0);
    }

    // Load the UI settings, and also some defaults cameras and lights, in case the GLTF has none
    {
#define LOAD(j, key, val) val = j.value(key, val)

        // global settings
        LOAD(scene, "TAA", m_state.m_useTAA);
        LOAD(scene, "toneMapper", m_state.m_toneMapper);
        LOAD(scene, "skyDomeType", m_state.m_skyDomeType);
        LOAD(scene, "exposure", m_state.m_exposure);
        LOAD(scene, "iblFactor", m_state.m_iblFactor);
        LOAD(scene, "emisiveFactor", m_state.m_emisiveFactor);
        LOAD(scene, "skyDomeType", m_state.m_skyDomeType);

        // Add a default light in case there are none
        //
        if (m_gltfLoader->m_lights.size() == 0)
        {
            tfNode n;
            n.m_tranform.LookAt(PolarToVector(XM_PI / 2.0f, 0.58f) * 3.5f, XMVectorSet(0, 0, 0, 0));

            tfLight l;
            l.m_type = tfLight::LIGHT_SPOTLIGHT;
            l.m_intensity = scene.value("intensity", 1.0f);
            l.m_color = XMVectorSet(1.0f, 1.0f, 1.0f, 0.0f);
            l.m_range = 15;
            l.m_outerConeAngle = XM_PI / 4.0f;
            l.m_innerConeAngle = (XM_PI / 4.0f) * 0.9f;

            m_gltfLoader->AddLight(n, l);
        }
        else
        {
            // clamp light intensity
            for (int l = 0; l < m_gltfLoader->m_lights.size(); ++l)
                m_gltfLoader->m_lights[l].m_intensity = m_gltfLoader->m_lights[l].m_intensity > 10.f ? 10.f : m_gltfLoader->m_lights[l].m_intensity;
        }

        // set default camera
        //
        json camera = scene["camera"];
        m_activeCamera = scene.value("activeCamera", m_activeCamera);
        XMVECTOR from = GetVector(GetElementJsonArray(camera, "defaultFrom", { 0.0, 0.0, 10.0 }));
        XMVECTOR to = GetVector(GetElementJsonArray(camera, "defaultTo", { 0.0, 0.0, 0.0 }));
        m_state.m_camera.LookAt(from, to);
        m_roll = m_state.m_camera.GetYaw();
        m_pitch = m_state.m_camera.GetPitch();
        m_distance = m_state.m_camera.GetDistance();

        // set benchmarking state if enabled 
        //
        if (m_state.m_isBenchmarking)
        {
            std::string deviceName;
            std::string driverVersion;
            m_device.GetDeviceInfo(&deviceName, &driverVersion);
            BenchmarkConfig(scene["BenchmarkSettings"], m_activeCamera, m_gltfLoader, deviceName, driverVersion);
        }

        // indicate the mainloop we started loading a GLTF and it needs to load the rest (textures and geometry)
        m_loadingScene = true;
    }
}

//--------------------------------------------------------------------------------------
//
// AddUiButton, helper function to add a color button to the UI
//
//--------------------------------------------------------------------------------------
void AddUiButton(const char* s, ImVec4 col, bool sameLine = true)
{
    ImGui::ColorButton(s, col);
    // set tooltip
    if (ImGui::IsItemHovered())	ImGui::SetTooltip("Color Overlay for VRS rate of %s", s);
    // disable drag and drop
    if (ImGui::IsItemActive() && ImGui::BeginDragDropSource())ImGui::EndDragDropSource();

    // add text in same line
    ImGui::SameLine();
    ImGui::Text(s);

    // convenience in case next item should be in same line, too:
    if (sameLine) ImGui::SameLine();
}

//--------------------------------------------------------------------------------------
//
// BuildUI, all UI code should be here
//
//--------------------------------------------------------------------------------------
void VariableShadingSample::BuildUI()
{
    ImGuiStyle& style = ImGui::GetStyle();
    style.FrameBorderSize = 1.0f;

    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(250, 700), ImGuiCond_FirstUseEver);

    bool opened = true;
    ImGui::Begin("FidelityFX Variable Shading", &opened);

    if (ImGui::CollapsingHeader("Settings", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Text("Resolution       : %ix%i", m_Width, m_Height);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Current Backbuffer Resolution");

        ImGui::Separator();

        if (m_node->AdditionalShadingRatesSupported())
        {
            ImGui::Checkbox("Allow Additional VRS Rates", &m_state.m_allowAdditionalVrsRates);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Allow/prohibit usage of 2x4, 4x2 and 4x4 shading rate");
        }

        if (m_node->GetVrsTier() > D3D12_VARIABLE_SHADING_RATE_TIER_NOT_SUPPORTED)
        {
            const char* combinersEnabled[] = { "Passthrough", "Override", "Min", "Max", "Sum" };
            const char* combinersDisabled[] = { "Passthrough" };
            {
                const char* baseRate[] = { "1x1", "1x2", "2x1", "2x2", "2x4", "4x2", "4x4" };
                ImGui::Combo("PerDraw VRS", &m_state.m_vrsBaseRate, baseRate, m_node->AdditionalShadingRates() ? _countof(baseRate) : 4);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Set base shading rate (Tier1)");
            }

            if (m_node->GetVrsTier() > D3D12_VARIABLE_SHADING_RATE_TIER_1)
            {
                ImGui::Separator();
                if (ImGui::Checkbox("ShadingRateImage Enabled", &m_state.m_enableShadingRateImage))
                {
                    m_state.m_vrsImageCombiner = m_state.m_enableShadingRateImage ? 1 : 0;
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Enable shading rate image");

                ImGui::Checkbox("ShadingRateImage Overlay", &m_state.m_showVRSMap);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Enable overlay to visualize shading rate image");

                if (m_state.m_showVRSMap)
                {
                    ImGui::Indent();
                    AddUiButton("1x1", ImVec4(1.0f, 0.0f, 0, 0));
                    AddUiButton("1x2", ImVec4(1.0f, 1.0f, 0, 0));
                    AddUiButton("2x1", ImVec4(1.0f, 0.5f, 0, 0));
                    AddUiButton("2x2", ImVec4(0.0f, 1.0f, 0, 0), false);

                    if (m_node->AdditionalShadingRates())
                    {
                        AddUiButton("2x4", ImVec4(0.5f, 0.5f, 1.0f, 0));
                        AddUiButton("4x2", ImVec4(1.0f, 0.5f, 1.0f, 0));
                        AddUiButton("4x4", ImVec4(0.0f, 1.0f, 1.0f, 0), false);
                    }
                    ImGui::Unindent();
                }

                ImGui::SliderFloat("VRS variance Threshold", &m_state.m_vrsVarianceThreshold, 0.0f, 0.1f, "%.3f");
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("This value specifies how much variance in luminance is acceptable to reduce shading rate");

                ImGui::SliderFloat("VRS Motion Factor", &m_state.m_vrsMotionFactor, 0.0f, 0.1f, "%.3f");
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("The lower this value, the faster a pixel has to move to get the shading rate reduced");

                if (m_state.m_enableShadingRateImage)
                    ImGui::Combo("ShadingRateImage Combiner", &m_state.m_vrsImageCombiner, combinersEnabled, _countof(combinersEnabled));
                else
                    ImGui::Combo("ShadingRateImage Combiner", &m_state.m_vrsImageCombiner, combinersDisabled, _countof(combinersDisabled));
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("How to combine shading rate from image with base shading rate");
            }
        }
        else
        {
            ImGui::TextColored( ImVec4(1.0f, 0.25f, 0.25f, 1.0f), "GPU does not support VRS!");
        }
    }

    if (ImGui::CollapsingHeader("Profiler", ImGuiTreeNodeFlags_DefaultOpen))
    {
        std::vector<TimeStamp> timeStamps = m_node->GetTimingValues();
        if (timeStamps.size() > 0)
        {
            for (uint32_t i = 0; i < timeStamps.size(); i++)
            {
                ImGui::Text("%-22s: %7.1f", timeStamps[i].m_label.c_str(), timeStamps[i].m_microseconds);
            }

            //scrolling data and average computing
            static float values[128];
            values[127] = timeStamps.back().m_microseconds;
            for (uint32_t i = 0; i < 128 - 1; i++) { values[i] = values[i + 1]; }
            ImGui::PlotLines("", values, 128, 0, "GPU frame time (us)", 0.0f, 30000.0f, ImVec2(0, 80));
        }
    }

    ImGui::End();

    // Sets Camera based on UI selection (WASD, Orbit or any of the GLTF cameras)
    //
    ImGuiIO& io = ImGui::GetIO();
    {
        //If the mouse was not used by the GUI then it's for the camera
        //
        if (io.WantCaptureMouse)
        {
            io.MouseDelta.x = 0;
            io.MouseDelta.y = 0;
            io.MouseWheel = 0;
        }
        else if ((io.KeyCtrl == false) && (io.MouseDown[0] == true))
        {
            m_roll -= io.MouseDelta.x / 100.f;
            m_pitch += io.MouseDelta.y / 100.f;
        }

        // Choose camera movement depending on setting
        //
        if (m_activeCamera == 0)
        {
            //  Orbiting
            //
            m_distance -= (float)io.MouseWheel / 3.0f;
            m_distance = std::max<float>(m_distance, 0.1f);

            bool panning = (io.KeyCtrl == true) && (io.MouseDown[0] == true);

            m_state.m_camera.UpdateCameraPolar(m_roll, m_pitch, panning ? -io.MouseDelta.x / 100.0f : 0.0f, panning ? io.MouseDelta.y / 100.0f : 0.0f, m_distance);
        }
        else if (m_activeCamera == 1)
        {
            //  WASD
            //
            m_state.m_camera.UpdateCameraWASD(m_roll, m_pitch, io.KeysDown, io.DeltaTime);
        }
        else if (m_activeCamera > 1)
        {
            // Use a camera from the GLTF
            // 
            m_gltfLoader->GetCamera(m_activeCamera - 2, &m_state.m_camera);
            m_roll = m_state.m_camera.GetYaw();
            m_pitch = m_state.m_camera.GetPitch();
        }
    }
}

//--------------------------------------------------------------------------------------
//
// OnRender, updates the state from the UI, animates, transforms and renders the scene
//
//--------------------------------------------------------------------------------------
void VariableShadingSample::OnRender()
{
    // Get timings
    //
    double timeNow = MillisecondsNow();
    float deltaTime = (float)(timeNow - m_lastFrameTime);
    m_lastFrameTime = timeNow;

    ImGUI_UpdateIO();
    ImGui::NewFrame();

    if (m_loadingScene)
    {
        // the scene loads in chuncks, that way we can show a progress bar
        static int loadingStage = 0;
        loadingStage = m_node->LoadScene(m_gltfLoader, loadingStage);
        if (loadingStage == 0)
        {
            m_time = 0;
            m_loadingScene = false;
        }
    }
    else if (m_gltfLoader && m_state.m_isBenchmarking)
    {
        // benchmarking takes control of the time, and exits the app when the animation is done
        std::vector<TimeStamp> timeStamps = m_node->GetTimingValues();

        m_time = BenchmarkLoop(timeStamps, &m_state.m_camera, &m_state.m_screenShotName);
    }
    else
    {
        // Build the UI. Note that the rendering of the UI happens later.
        BuildUI();

        // Set animation time
        //
        if (m_play)
        {
            m_time += (float)deltaTime / 1000.0f;
        }
    }

    // Animate and transform the scene
    //
    if (m_gltfLoader)
    {
        m_gltfLoader->SetAnimationTime(0, m_time);
        m_gltfLoader->TransformScene(0, XMMatrixIdentity());
    }

    m_state.m_time = m_time;

    // Do Render frame using AFR 
    //
    m_node->OnRender(&m_state, &m_swapChain);

    m_swapChain.Present();
}


//--------------------------------------------------------------------------------------
//
// WinMain
//
//--------------------------------------------------------------------------------------
int WINAPI WinMain(HINSTANCE hInstance,
    HINSTANCE hPrevInstance,
    LPSTR lpCmdLine,
    int nCmdShow)
{
    LPCSTR Name = "FidelityFX Variable Shading Sample";

    // create new DX sample
    return RunFramework(hInstance, lpCmdLine, nCmdShow, new VariableShadingSample(Name));
}
