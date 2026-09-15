/*
 * Copyright (c) 2017-2026 The Forge Interactive Inc.
 *
 * This file is part of The-Forge
 * (see https://github.com/ConfettiFX/The-Forge).
 *
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

// Unit Test for testing wave intrinsic operations

// Interfaces
#include "../../../../Common_3/Application/Interfaces/IApp.h"
#include "../../../../Common_3/Application/Interfaces/ICamera.h"
#include "../../../../Common_3/Application/Interfaces/IFont.h"
#include "../../../../Common_3/OS/Interfaces/IInput.h"
#include "../../../../Common_3/Application/Interfaces/IScreenshot.h"
#include "../../../../Common_3/Application/Interfaces/IUI.h"
#include "../../../../Common_3/Game/Interfaces/IScripting.h"
#include "../../../../Common_3/Graphics/Interfaces/IGraphics.h"
#include "../../../../Common_3/Resources/ResourceLoader/Interfaces/IResourceLoader.h"
#include "../../../../Common_3/Utilities/Interfaces/IFileSystem.h"
#include "../../../../Common_3/Utilities/Interfaces/ILog.h"
#include "../../../../Common_3/Utilities/Interfaces/ITime.h"

#include "../../../../Common_3/Utilities/RingBuffer.h"

// Math
#include "../../../../Common_3/Application/Interfaces/IProfiler.h"

#include "../../../../Common_3/Utilities/Interfaces/IMath.h"

#include "../../../../Common_3/Utilities/Interfaces/IMemory.h"

// fsl
#include "../../../../Common_3/Graphics/FSL/defaults.h"
#include "./Shaders/FSL/Global.srt.h"

/// Demo structures
struct SceneConstantBuffer
{
    mat4   orthProjMatrix;
    float2 mousePosition;
    float2 resolution;
    float  time;
    uint   renderMode;
    uint   laneSize;
    uint   padding;
};

// #NOTE: Two sets of resources (one in flight and one being used on CPU)
const uint32_t gDataBufferCount = 2;

TFRenderer* pRenderer = NULL;

TFQueue*   pGraphicsQueue = NULL;
GpuCmdRing gGraphicsCmdRing = {};

TFSwapChain* pSwapChain = NULL;
TFSemaphore* pImageAcquiredSemaphore[gDataBufferCount] = { NULL };

TFRenderTarget* pRenderTargetIntermediate = NULL;

TFShader*   pShaderWave = NULL;
TFPipeline* pPipelineWave = NULL;
TFShader*   pShaderMagnify = NULL;
TFPipeline* pPipelineMagnify = NULL;

TFDescriptorSet* pDescriptorSetPerFrame = NULL;
TFDescriptorSet* pDescriptorSetPersistent = NULL;

TFSampler* pSamplerPointWrap = NULL;

TFBuffer* pUniformBuffer[gDataBufferCount] = { NULL };
TFBuffer* pVertexBufferTriangle = NULL;
TFBuffer* pVertexBufferQuad = NULL;

SceneConstantBuffer gSceneData;
float2              gMovePosition = {};

ProfileToken gGpuProfileToken;

/// UI
TFUIWindowDesc gGuiDesc;

enum RenderMode
{
    RenderMode1,
    RenderMode2,
    RenderMode3,
    RenderMode4,
    RenderMode5,
    RenderMode6,
    RenderMode7,
    RenderMode8,
    RenderMode9,
    RenderModeCount,
};
int32_t gRenderModeToggles = 0;

const char* gTestScripts[] = {
    "Test_WaveGetLaneIndex.lua",
    "Test_WaveIsFirstLane.lua",
    "Test_WaveGetMaxActiveIndex.lua",
    "Test_WaveActiveBallot.lua",
    "Test_WaveReadLaneFirst.lua",
    "Test_WaveActiveSum.lua",
    "Test_WavePrefixSum.lua",
    "Test_QuadReadAcross.lua",
    // Test_Normal.lua is in the end so that Test_Default outputs identical screenshots for all APIs.
    "Test_Normal.lua",
};

const char*           gLabels[RenderModeCount] = {};
TFWaveOpsSupportFlags gRequiredWaveFlags[RenderModeCount] = {};

TFFontDrawDesc gFrameTimeDraw;
TFFont*        gFont;

struct Vertex
{
    float3 position;
    float4 color;
};

struct Vertex2
{
    float3 position;
    float2 uv;
};

class WaveIntrinsics: public IApp
{
public:
    bool Init()
    {
        TFRendererDesc settings;
        memset(&settings, 0, sizeof(settings));
        settings.mShaderTarget = TF_SHADER_TARGET_6_0;
        initGPUConfig(settings.pExtendedSettings);
        initRenderer(GetName(), &settings, &pRenderer);
        // Check for init success
        if (!pRenderer)
        {
            ShowUnsupportedMessage(getUnsupportedGPUMsg());
            return false;
        }
        setGPUConfig(pRenderer->pContext->mGpus, pRenderer->pContext->mGpuCount, (uint32_t)(pRenderer->pGpu - pRenderer->pContext->mGpus),
                     settings.pExtendedSettings);

        mSettings.mFrameMaxCount = gDataBufferCount;

        TFQueueDesc queueDesc = {};
        queueDesc.mType = TF_QUEUE_TYPE_GRAPHICS;
        queueDesc.mFlag = TF_QUEUE_FLAG_INIT_MICROPROFILE;
        initQueue(pRenderer, &queueDesc, &pGraphicsQueue);

        GpuCmdRingDesc cmdRingDesc = {};
        cmdRingDesc.pQueue = pGraphicsQueue;
        cmdRingDesc.mPoolCount = gDataBufferCount;
        cmdRingDesc.mCmdPerPoolCount = 1;
        cmdRingDesc.mAddSyncPrimitives = true;
        initGpuCmdRing(pRenderer, &cmdRingDesc, &gGraphicsCmdRing);

        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            initSemaphore(pRenderer, &pImageAcquiredSemaphore[i]);
        }

        initResourceLoaderInterface(pRenderer);

        TFRootSignatureDesc rootDesc = {};
        INIT_RS_DESC(rootDesc, "default.rootsig", "compute.rootsig");
        initRootSignature(pRenderer, &rootDesc);

        TFSamplerDesc samplerDesc = { TF_FILTER_NEAREST,      TF_FILTER_NEAREST,      TF_MIPMAP_MODE_NEAREST,
                                      TF_ADDRESS_MODE_REPEAT, TF_ADDRESS_MODE_REPEAT, TF_ADDRESS_MODE_CLAMP_TO_BORDER };
        addSampler(pRenderer, &samplerDesc, &pSamplerPointWrap);

        // Define the geometry for a triangle.
        Vertex triangleVertices[] = { { { 0.0f, 0.5f, 0.0f }, { 0.8f, 0.8f, 0.0f, 1.0f } },
                                      { { 0.5f, -0.5f, 0.0f }, { 0.0f, 0.8f, 0.8f, 1.0f } },
                                      { { -0.5f, -0.5f, 0.0f }, { 0.8f, 0.0f, 0.8f, 1.0f } } };

        TFBufferLoadDesc triangleColorDesc = {};
        triangleColorDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_VERTEX_BUFFER;
        triangleColorDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_GPU_ONLY;
        triangleColorDesc.mDesc.mSize = sizeof(triangleVertices);
        triangleColorDesc.pData = triangleVertices;
        triangleColorDesc.ppBuffer = &pVertexBufferTriangle;
        addResource(&triangleColorDesc, NULL);

        // Define the geometry for a rectangle.
        Vertex2 quadVertices[] = { { { -1.0f, -1.0f, 0.0f }, { 0.0f, 1.0f } }, { { -1.0f, 1.0f, 0.0f }, { 0.0f, 0.0f } },
                                   { { 1.0f, 1.0f, 0.0f }, { 1.0f, 0.0f } },

                                   { { -1.0f, -1.0f, 0.0f }, { 0.0f, 1.0f } }, { { 1.0f, 1.0f, 0.0f }, { 1.0f, 0.0f } },
                                   { { 1.0f, -1.0f, 0.0f }, { 1.0f, 1.0f } } };

        TFBufferLoadDesc quadUVDesc = {};
        quadUVDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_VERTEX_BUFFER;
        quadUVDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_GPU_ONLY;
        quadUVDesc.mDesc.mSize = sizeof(quadVertices);
        quadUVDesc.pData = quadVertices;
        quadUVDesc.ppBuffer = &pVertexBufferQuad;
        addResource(&quadUVDesc, NULL);

        TFBufferLoadDesc ubDesc = {};
        ubDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        ubDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
        ubDesc.mDesc.mSize = sizeof(SceneConstantBuffer);
        ubDesc.mDesc.mFlags = TF_BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
        ubDesc.pData = NULL;
        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            ubDesc.ppBuffer = &pUniformBuffer[i];
            addResource(&ubDesc, NULL);
        }

        // Load fonts
        TFFontSystemDesc fontSystemDesc{};
        fontSystemDesc.pRenderer = pRenderer;
        fontSystemDesc.mFrameMaxCount = mSettings.mFrameMaxCount;
        fontSystemDesc.pFrameIdx = &mSettings.mFrameIdx;
        initFontSystem(&fontSystemDesc);

        TFFontDesc fontDesc = {};
        fontDesc.pFontPath = "Selawk/selawk.msdf";
        fontDesc.pFontName = "Selawk";
        fontDesc.mFlags |= TF_FONT_ASCII;
        fontDesc.mFlags |= TF_FONT_ATLAS_AUTO_RESOLUTION_ON_INIT;
        gFont = addFont(&fontDesc);
        if (gFont == NULL)
            return false;

        // Initialize Forge User Interface Rendering
        TFUserInterfaceDesc uiRenderDesc = {};
        uiRenderDesc.pRenderer = pRenderer;
        uiRenderDesc.mFrameMaxCount = mSettings.mFrameMaxCount;
        uiRenderDesc.pFrameIdx = &mSettings.mFrameIdx;
        uiRenderDesc.pFont = gFont;
        initUserInterface(&uiRenderDesc);

        const char* ppGpuProfilerName[1] = { "Graphics" };

        // Initialize micro profiler and its UI.
        TFProfilerDesc profiler = {};
        profiler.pRenderer = pRenderer;
        profiler.ppQueues = &pGraphicsQueue;
        profiler.ppProfilerNames = ppGpuProfilerName;
        profiler.pProfileTokens = &gGpuProfileToken;
        profiler.mGpuProfilerCount = 1;
        initProfiler(&profiler);

        gLabels[RenderMode1] = "1. Normal render.\n";
        gRequiredWaveFlags[RenderMode1] = TF_WAVE_OPS_SUPPORT_FLAG_NONE;
        gLabels[RenderMode2] = "2. Color pixels by lane indices.\n";
        gRequiredWaveFlags[RenderMode2] = TF_WAVE_OPS_SUPPORT_FLAG_BASIC_BIT;
        gLabels[RenderMode3] = "3. Show first lane (white dot) in each wave.\n";
        gRequiredWaveFlags[RenderMode3] = TF_WAVE_OPS_SUPPORT_FLAG_BASIC_BIT;
        gLabels[RenderMode4] = "4. Show first(white dot) and last(red dot) lanes in each wave.\n";
        gRequiredWaveFlags[RenderMode4] = TF_WAVE_OPS_SUPPORT_FLAG_BASIC_BIT | TF_WAVE_OPS_SUPPORT_FLAG_ARITHMETIC_BIT;
        gLabels[RenderMode5] = "5. Color pixels by active lane ratio (white = 100%; black = 0%).\n";
        gRequiredWaveFlags[RenderMode5] = TF_WAVE_OPS_SUPPORT_FLAG_BASIC_BIT | TF_WAVE_OPS_SUPPORT_FLAG_BALLOT_BIT;
        gLabels[RenderMode6] = "6. Broadcast the color of the first active lane to the wave.\n";
        gRequiredWaveFlags[RenderMode6] = TF_WAVE_OPS_SUPPORT_FLAG_BASIC_BIT | TF_WAVE_OPS_SUPPORT_FLAG_BALLOT_BIT;
        gLabels[RenderMode7] = "7. Average the color in a wave.\n";
        gRequiredWaveFlags[RenderMode7] =
            TF_WAVE_OPS_SUPPORT_FLAG_BASIC_BIT | TF_WAVE_OPS_SUPPORT_FLAG_ARITHMETIC_BIT | TF_WAVE_OPS_SUPPORT_FLAG_BALLOT_BIT;
        gLabels[RenderMode8] = "8. Color pixels by prefix sum of distance between current and first lane.\n";
        gRequiredWaveFlags[RenderMode8] =
            TF_WAVE_OPS_SUPPORT_FLAG_BASIC_BIT | TF_WAVE_OPS_SUPPORT_FLAG_ARITHMETIC_BIT | TF_WAVE_OPS_SUPPORT_FLAG_BALLOT_BIT;
        gLabels[RenderMode9] = "9. Color pixels by their quad id.\n";
        gRequiredWaveFlags[RenderMode9] = TF_WAVE_OPS_SUPPORT_FLAG_BASIC_BIT | TF_WAVE_OPS_SUPPORT_FLAG_QUAD_BIT;

        const uint32_t numMaxScripts = sizeof(gTestScripts) / sizeof(gTestScripts[0]);
        const char*    testScripts[numMaxScripts] = {};
        uint32_t       numScripts = 0;

        // Skip normal .. will be added last
        for (uint32_t i = 1; i < numMaxScripts; ++i)
        {
            if ((pRenderer->pGpu->mWaveOpsSupportFlags & gRequiredWaveFlags[i]) == gRequiredWaveFlags[i])
            {
                testScripts[numScripts++] = gTestScripts[i - 1];
            }
        }

        testScripts[numScripts++] = gTestScripts[numMaxScripts - 1];
        TFLuaScriptDesc scriptDescs[numMaxScripts] = {};
        for (uint32_t i = 0; i < numScripts; ++i)
            scriptDescs[i].pScriptFileName = testScripts[i];
        luaDefineScripts(scriptDescs, numScripts);

        AddCustomInputBindings();
        initScreenshotCapturer(pRenderer, pGraphicsQueue, GetName());
        waitForAllResourceLoads();

        return true;
    }

    void Exit()
    {
        exitScreenshotCapturer();
        exitProfiler();

        exitUserInterface();

        removeFont(gFont);
        exitFontSystem();

        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            removeResource(pUniformBuffer[i]);
        }
        removeResource(pVertexBufferQuad);
        removeResource(pVertexBufferTriangle);

        removeSampler(pRenderer, pSamplerPointWrap);

        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            exitSemaphore(pRenderer, pImageAcquiredSemaphore[i]);
        }
        exitGpuCmdRing(pRenderer, &gGraphicsCmdRing);
        exitRootSignature(pRenderer);
        exitResourceLoaderInterface(pRenderer);
        exitQueue(pRenderer, pGraphicsQueue);
        exitRenderer(pRenderer);
        exitGPUConfig();
        pRenderer = NULL;
    }

    bool Load(TFReloadDesc* pReloadDesc)
    {
        UNREF_PARAM(pReloadDesc);

        gSceneData.mousePosition.x = mSettings.mWidth * 0.5f;
        gSceneData.mousePosition.y = mSettings.mHeight * 0.5f;

        addShaders();
        addDescriptorSets();

        loadProfilerUI(mSettings.mWidth, mSettings.mHeight);

        gGuiDesc = {};
        gGuiDesc.mStartPos = vec2(mSettings.mWidth * 0.01f, mSettings.mHeight * 0.2f);
        gGuiDesc.mStartSize = vec2(450, 400);
        gGuiDesc.pWindowTitle = "Render Modes";
        gGuiDesc.mFlags = TF_UI_WINDOW_TITLE | TF_UI_WINDOW_SCALABLE | TF_UI_WINDOW_MOVABLE | TF_UI_WINDOW_MINIMIZABLE |
                          TF_UI_WINDOW_BORDER | TF_UI_WINDOW_INIT_HEIGHT_FIT;

        TFLuaWidgetVariableDesc luaVarDesc = {};
        luaVarDesc.mWidgetType = TF_WIDGET_TYPE_DROPDOWN;
        luaVarDesc.pLabel = "Render Mode";
        luaVarDesc.pUint = (uint32_t*)&gRenderModeToggles;
        luaRegisterWidgetVariable(&luaVarDesc);

        if (!addSwapChain())
            return false;

        if (!addIntermediateRenderTarget())
            return false;

        addPipelines();

        updateDescriptorSets();

        TFUserInterfaceLoadDesc uiLoad = {};
        uiLoad.mColorFormat = pSwapChain->ppRenderTargets[0]->mFormat;
        uiLoad.mHeight = mSettings.mHeight;
        uiLoad.mWidth = mSettings.mWidth;
        loadUserInterface(&uiLoad);

        TFFontSystemLoadDesc fontLoad = {};
        fontLoad.mColorFormat = pSwapChain->ppRenderTargets[0]->mFormat;
        fontLoad.mHeight = mSettings.mHeight;
        fontLoad.mWidth = mSettings.mWidth;
        loadFontSystem(&fontLoad);

        gMovePosition = float2(mSettings.mWidth * 0.5f, mSettings.mHeight * 0.5f);

        return true;
    }

    void Unload(TFReloadDesc* pReloadDesc)
    {
        UNREF_PARAM(pReloadDesc);

        waitQueueIdle(pGraphicsQueue);

        unloadFontSystem();
        unloadUserInterface();

        removePipelines();

        removeSwapChain(pRenderer, pSwapChain);
        removeRenderTarget(pRenderer, pRenderTargetIntermediate);
        unloadProfilerUI();

        removeDescriptorSets();
        removeShaders();
    }

    void Update(float deltaTime)
    {
        updateGui();

        if (!uiIsFocused())
        {
            if (inputGetValue(0, CUSTOM_PT_DOWN))
            {
                gMovePosition = { inputGetValue(0, CUSTOM_PT_X), inputGetValue(0, CUSTOM_PT_Y) };
            }
            if (inputGetValue(0, CUSTOM_TOGGLE_FULLSCREEN))
            {
                toggleFullscreen(pWindow);
            }
            if (inputGetValue(0, CUSTOM_DUMP_PROFILE))
            {
                dumpProfileData(GetName());
            }
            if (inputGetValue(0, CUSTOM_EXIT))
            {
                requestShutdown();
            }
            gMovePosition += float2{ inputGetValue(0, CUSTOM_MOVE_X), -inputGetValue(0, CUSTOM_MOVE_Y) } * 0.25f;
        }
        /************************************************************************/
        // Uniforms
        /************************************************************************/
        static float currentTime = 0.0f;
        currentTime += deltaTime * 1000.0f;

        float aspectRatio = (float)mSettings.mWidth / mSettings.mHeight;
        gSceneData.orthProjMatrix = mat4::orthographicLH(-1.0f * aspectRatio, 1.0f * aspectRatio, -1.0f, 1.0f, 0.0f, 1.0f);
        gSceneData.laneSize = pRenderer->pGpu->mWaveLaneCount;
        gSceneData.time = currentTime;
        gSceneData.resolution.x = (float)(mSettings.mWidth);
        gSceneData.resolution.y = (float)(mSettings.mHeight);
        gSceneData.renderMode = (gRenderModeToggles + 1);
        gSceneData.mousePosition = gMovePosition;
    }

    void Draw()
    {
        if ((bool)pSwapChain->mEnableVsync != mSettings.mVSyncEnabled)
        {
            waitQueueIdle(pGraphicsQueue);
            ::toggleVSync(pRenderer, &pSwapChain);
        }

        uint32_t swapchainImageIndex;
        acquireNextImage(pRenderer, pSwapChain, pImageAcquiredSemaphore[mSettings.mFrameIdx], NULL, &swapchainImageIndex);

        // Stall if CPU is running "gDataBufferCount" frames ahead of GPU
        GpuCmdRingElement elem = getNextGpuCmdRingElement(&gGraphicsCmdRing, true, 1);
        TFFenceStatus     fenceStatus;
        getFenceStatus(pRenderer, elem.pFence, &fenceStatus);
        if (fenceStatus == TF_FENCE_STATUS_INCOMPLETE)
            waitForFences(pRenderer, 1, &elem.pFence);

        resetCmdPool(pRenderer, elem.pCmdPool);

        /************************************************************************/
        // Update uniform buffers
        /************************************************************************/
        TFBufferUpdateDesc viewProjCbv = { pUniformBuffer[mSettings.mFrameIdx] };
        beginUpdateResource(&viewProjCbv);
        memcpy(viewProjCbv.pMappedData, &gSceneData, sizeof(gSceneData));
        endUpdateResource(&viewProjCbv);

        /************************************************************************/
        // Rendering
        /************************************************************************/
        TFRenderTarget* pRenderTarget = pRenderTargetIntermediate;
        TFRenderTarget* pScreenRenderTarget = pSwapChain->ppRenderTargets[swapchainImageIndex];

        TFCmd* cmd = elem.pCmds[0];
        beginCmd(cmd);
        cmdBindDescriptorSet(cmd, 0, pDescriptorSetPersistent);
        cmdBindDescriptorSet(cmd, mSettings.mFrameIdx, pDescriptorSetPerFrame);
        cmdBeginGpuFrameProfile(cmd, gGpuProfileToken);

        // Resource Transition
        TFRenderTargetBarrier rtBarrier[] = {
            { pRenderTarget, TF_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, TF_RESOURCE_STATE_RENDER_TARGET },
            { pScreenRenderTarget, TF_RESOURCE_STATE_PRESENT, TF_RESOURCE_STATE_RENDER_TARGET },
        };
        cmdResourceBarrier(cmd, 0, NULL, 0, NULL, 2, rtBarrier);

        // Wave Shader pass
        {
            cmdBeginGpuTimestampQuery(cmd, gGpuProfileToken, "Wave Shader");
            cmdBeginDebugMarker(cmd, 0, 0, 1, "Wave Shader");

            // Bind Render Targets
            {
                TFBindRenderTargetsDesc bindRenderTargets = {};
                bindRenderTargets.mRenderTargetCount = 1;
                bindRenderTargets.mRenderTargets[0] = { pRenderTarget, TF_LOAD_ACTION_CLEAR };
                cmdBindRenderTargets(cmd, &bindRenderTargets);
                cmdSetViewport(cmd, 0.0f, 0.0f, (float)pRenderTarget->mWidth, (float)pRenderTarget->mHeight, 0.0f, 1.0f);
                cmdSetScissor(cmd, 0, 0, pRenderTarget->mWidth, pRenderTarget->mHeight);
            }
            // Draw
            {
                const uint32_t triangleStride = sizeof(Vertex);
                cmdBindPipeline(cmd, pPipelineWave);
                cmdBindVertexBuffer(cmd, 1, &pVertexBufferTriangle, &triangleStride, NULL);
                cmdDraw(cmd, 3, 0);
            }

            cmdBindRenderTargets(cmd, NULL);
            cmdEndDebugMarker(cmd);
            cmdEndGpuTimestampQuery(cmd, gGpuProfileToken);
        }

        // Magnify pass
        {
            cmdBeginGpuTimestampQuery(cmd, gGpuProfileToken, "Magnify");
            cmdBeginDebugMarker(cmd, 1, 0, 1, "Magnify");

            // Resource Transition
            {
                TFRenderTargetBarrier srvBarrier[] = {
                    { pRenderTarget, TF_RESOURCE_STATE_RENDER_TARGET, TF_RESOURCE_STATE_PIXEL_SHADER_RESOURCE },
                };
                cmdResourceBarrier(cmd, 0, NULL, 0, NULL, 1, srvBarrier);
            }
            // Bind Render Targets
            {
                TFBindRenderTargetsDesc bindRenderTargets = {};
                bindRenderTargets.mRenderTargetCount = 1;
                bindRenderTargets.mRenderTargets[0] = { pScreenRenderTarget, TF_LOAD_ACTION_CLEAR };
                cmdBindRenderTargets(cmd, &bindRenderTargets);
            }
            // Draw
            {
                const uint32_t quadStride = sizeof(Vertex2);
                cmdBindPipeline(cmd, pPipelineMagnify);
                cmdBindVertexBuffer(cmd, 1, &pVertexBufferQuad, &quadStride, NULL);
                cmdDrawInstanced(cmd, 6, 0, 2, 0);
            }

            cmdEndDebugMarker(cmd);
            cmdEndGpuTimestampQuery(cmd, gGpuProfileToken);
        }

        // UI pass
        {
            cmdBeginDebugMarker(cmd, 0, 1, 0, "Draw UI");

            TFFontDrawDesc frameTimeDraw;
            frameTimeDraw.mFontColor = 0xff0080ff;
            frameTimeDraw.mFontSize = 18.0f;
            frameTimeDraw.pFont = gFont;
            float2 txtSize = cmdDrawCpuProfile(cmd, float2(8.0f, 15.0f), &frameTimeDraw);
            cmdDrawGpuProfile(cmd, float2(8.f, txtSize.y + 75.f), gGpuProfileToken, &frameTimeDraw);

            uiCmdDrawUserInterface(cmd, pSwapChain, pScreenRenderTarget, gGpuProfileToken);

            cmdBindRenderTargets(cmd, NULL);
            cmdEndDebugMarker(cmd);
        }

        TFRenderTargetBarrier presentBarrier = { pScreenRenderTarget, TF_RESOURCE_STATE_RENDER_TARGET, TF_RESOURCE_STATE_PRESENT };
        cmdResourceBarrier(cmd, 0, NULL, 0, NULL, 1, &presentBarrier);

        cmdEndGpuFrameProfile(cmd, gGpuProfileToken);
        endCmd(cmd);

        FlushResourceUpdateDesc flushUpdateDesc = {};
        flushUpdateDesc.mNodeIndex = 0;
        flushResourceUpdates(&flushUpdateDesc);
        TFSemaphore* waitSemaphores[2] = { flushUpdateDesc.pOutSubmittedSemaphore, pImageAcquiredSemaphore[mSettings.mFrameIdx] };

        TFQueueSubmitDesc submitDesc = {};
        submitDesc.mCmdCount = 1;
        submitDesc.mSignalSemaphoreCount = 1;
        submitDesc.mWaitSemaphoreCount = TF_ARRAY_COUNT(waitSemaphores);
        submitDesc.ppCmds = &cmd;
        submitDesc.ppSignalSemaphores = &elem.pSemaphore;
        submitDesc.ppWaitSemaphores = waitSemaphores;
        submitDesc.pSignalFence = elem.pFence;
        queueSubmit(pGraphicsQueue, &submitDesc);

        TFQueuePresentDesc presentDesc = {};
        presentDesc.mIndex = (uint8_t)swapchainImageIndex;
        presentDesc.mWaitSemaphoreCount = 1;
        presentDesc.ppWaitSemaphores = &elem.pSemaphore;
        presentDesc.pSwapChain = pSwapChain;
        presentDesc.mSubmitDone = true;
        queuePresent(pGraphicsQueue, &presentDesc);

        flipProfiler();
    }

    const char* GetName() { return "14_WaveIntrinsics"; }

private:
    void updateGui()
    {
        if (UI_WINDOW_IS_VISIBLE(uiBeginWidgetWindow(&gGuiDesc)))
        {
            uiLayoutAutoTextRows(1);
            for (uint32_t i = 0; i < RenderModeCount; ++i)
            {
                if ((pRenderer->pGpu->mWaveOpsSupportFlags & gRequiredWaveFlags[i]) != gRequiredWaveFlags[i])
                {
                    continue;
                }

                bool isActiveMode = gRenderModeToggles == (int32_t)i;
                if (UI_WIDGET_IS_CHANGED(uiRadioButton(gLabels[i], &isActiveMode)) && isActiveMode)
                {
                    gRenderModeToggles = i;
                }
            }
        }
        uiEndWidgetWindow();
    }

    bool addSwapChain()
    {
        TFSwapChainDesc swapChainDesc = {};
        swapChainDesc.mWindowHandle = pWindow->handle;
        swapChainDesc.mPresentQueueCount = 1;
        swapChainDesc.ppPresentQueues = &pGraphicsQueue;
        swapChainDesc.mWidth = mSettings.mWidth;
        swapChainDesc.mHeight = mSettings.mHeight;
        swapChainDesc.mImageCount = getRecommendedSwapchainImageCount(pRenderer, &pWindow->handle);
        swapChainDesc.mColorFormat = getSupportedSwapchainFormat(pRenderer, &swapChainDesc, TF_COLOR_SPACE_SDR_SRGB);
        swapChainDesc.mColorSpace = TF_COLOR_SPACE_SDR_SRGB;
        swapChainDesc.mEnableVsync = mSettings.mVSyncEnabled;
        ::addSwapChain(pRenderer, &swapChainDesc, &pSwapChain);

        return pSwapChain != NULL;
    }

    void addDescriptorSets()
    {
        TFDescriptorSetDesc setDesc = SRT_SET_DESC(SrtData, Persistent, 1, 0);
        addDescriptorSet(pRenderer, &setDesc, &pDescriptorSetPersistent);
        setDesc = SRT_SET_DESC(SrtData, PerFrame, gDataBufferCount, 0);
        addDescriptorSet(pRenderer, &setDesc, &pDescriptorSetPerFrame);
    }

    void removeDescriptorSets()
    {
        removeDescriptorSet(pRenderer, pDescriptorSetPerFrame);
        removeDescriptorSet(pRenderer, pDescriptorSetPersistent);
    }

    void addShaders()
    {
        TFShaderLoadDesc waveShader = {};
        waveShader.mVert.pFileName = "wave.vert";
        auto waveFlags = pRenderer->pGpu->mWaveOpsSupportFlags;

        if ((waveFlags & TF_WAVE_OPS_SUPPORT_FLAG_BASIC_BIT) && (waveFlags & TF_WAVE_OPS_SUPPORT_FLAG_ARITHMETIC_BIT) &&
            (waveFlags & TF_WAVE_OPS_SUPPORT_FLAG_BALLOT_BIT) && (waveFlags & TF_WAVE_OPS_SUPPORT_FLAG_QUAD_BIT))
        {
            waveShader.mFrag.pFileName = "wave_all.frag";
        }
        else if ((waveFlags & TF_WAVE_OPS_SUPPORT_FLAG_BASIC_BIT) && (waveFlags & TF_WAVE_OPS_SUPPORT_FLAG_ARITHMETIC_BIT) &&
                 (waveFlags & TF_WAVE_OPS_SUPPORT_FLAG_BALLOT_BIT))
        {
            waveShader.mFrag.pFileName = "wave_basic_ballot_arithmetic.frag";
        }
        else if ((waveFlags & TF_WAVE_OPS_SUPPORT_FLAG_BASIC_BIT) && (waveFlags & TF_WAVE_OPS_SUPPORT_FLAG_BALLOT_BIT))
        {
            waveShader.mFrag.pFileName = "wave_basic_ballot.frag";
        }
        else if ((waveFlags & TF_WAVE_OPS_SUPPORT_FLAG_BASIC_BIT) && (waveFlags & TF_WAVE_OPS_SUPPORT_FLAG_ARITHMETIC_BIT))
        {
            waveShader.mFrag.pFileName = "wave_basic_arithmetic.frag";
        }
        else
        {
            waveShader.mFrag.pFileName = "wave_basic.frag";
        }
        addShader(pRenderer, &waveShader, &pShaderWave);

        TFShaderLoadDesc magnifyShader = {};
        magnifyShader.mVert.pFileName = "magnify.vert";
        magnifyShader.mFrag.pFileName = "magnify.frag";
        addShader(pRenderer, &magnifyShader, &pShaderMagnify);
    }

    void removeShaders()
    {
        removeShader(pRenderer, pShaderMagnify);
        removeShader(pRenderer, pShaderWave);
    }

    void addPipelines()
    {
        // Layout and pipeline for sphere draw
        TFVertexLayout vertexLayout = {};
        vertexLayout.mBindingCount = 1;
        vertexLayout.mAttribCount = 2;
        vertexLayout.mAttribs[0].mSemantic = TF_SEMANTIC_POSITION;
        vertexLayout.mAttribs[0].mFormat = TinyImageFormat_R32G32B32_SFLOAT;
        vertexLayout.mAttribs[0].mBinding = 0;
        vertexLayout.mAttribs[0].mLocation = 0;
        vertexLayout.mAttribs[0].mOffset = 0;
        vertexLayout.mAttribs[1].mSemantic = TF_SEMANTIC_COLOR;
        vertexLayout.mAttribs[1].mFormat = TinyImageFormat_R32G32B32A32_SFLOAT;
        vertexLayout.mAttribs[1].mBinding = 0;
        vertexLayout.mAttribs[1].mLocation = 1;
        vertexLayout.mAttribs[1].mOffset = 3 * sizeof(float);

        TFRasterizerStateDesc rasterizerStateDesc = {};
        rasterizerStateDesc.mCullMode = TF_CULL_MODE_NONE;

        TFDepthStateDesc depthStateDesc = {};

        TFPipelineDesc desc = {};
        PIPELINE_LAYOUT_DESC(desc, SRT_LAYOUT_DESC(SrtData, Persistent), SRT_LAYOUT_DESC(SrtData, PerFrame), NULL, NULL);
        desc.mType = TF_PIPELINE_TYPE_GRAPHICS;
        TFGraphicsPipelineDesc& pipelineSettings = desc.mGraphicsDesc;
        pipelineSettings.mPrimitiveTopo = TF_PRIMITIVE_TOPO_TRI_LIST;
        pipelineSettings.mRenderTargetCount = 1;
        pipelineSettings.pDepthState = &depthStateDesc;
        pipelineSettings.pColorFormats = &pSwapChain->ppRenderTargets[0]->mFormat;
        pipelineSettings.mSampleCount = pSwapChain->ppRenderTargets[0]->mSampleCount;
        pipelineSettings.mSampleQuality = pSwapChain->ppRenderTargets[0]->mSampleQuality;
        pipelineSettings.pShaderProgram = pShaderWave;
        pipelineSettings.pVertexLayout = &vertexLayout;
        pipelineSettings.pRasterizerState = &rasterizerStateDesc;
        addPipeline(pRenderer, &desc, &pPipelineWave);

        // Layout and pipeline for magnify draw
        vertexLayout.mAttribs[1].mSemantic = TF_SEMANTIC_TEXCOORD0;
        vertexLayout.mAttribs[1].mFormat = TinyImageFormat_R32G32_SFLOAT;
        vertexLayout.mAttribs[1].mBinding = 0;
        vertexLayout.mAttribs[1].mLocation = 1;
        vertexLayout.mAttribs[1].mOffset = 3 * sizeof(float);

        pipelineSettings.pShaderProgram = pShaderMagnify;
        addPipeline(pRenderer, &desc, &pPipelineMagnify);
    }

    void removePipelines()
    {
        removePipeline(pRenderer, pPipelineMagnify);
        removePipeline(pRenderer, pPipelineWave);
    }

    void updateDescriptorSets()
    {
        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            TFDescriptorData params[1] = {};
            params[0].mIndex = SRT_RES_IDX(SrtData, PerFrame, gSceneConstantBuffer);
            params[0].ppBuffers = &pUniformBuffer[i];
            updateDescriptorSet(pRenderer, i, pDescriptorSetPerFrame, 1, params);
        }

        TFDescriptorData magnifyParams[2] = {};
        magnifyParams[0].mIndex = SRT_RES_IDX(SrtData, Persistent, gTexture);
        magnifyParams[0].ppTextures = &pRenderTargetIntermediate->pTexture;
        magnifyParams[1].mIndex = SRT_RES_IDX(SrtData, Persistent, gSampler);
        magnifyParams[1].ppSamplers = &pSamplerPointWrap;
        updateDescriptorSet(pRenderer, 0, pDescriptorSetPersistent, 2, magnifyParams);
    }

    bool addIntermediateRenderTarget()
    {
        TF_ESRAM_BEGIN_ALLOC(pRenderer, "Intermediate", 0);

        TFRenderTargetDesc rtDesc = {};
        rtDesc.mArraySize = 1;
        rtDesc.mClearValue = {
            { 0.001f, 0.001f, 0.001f, 0.001f }
        }; // This is a temporary workaround for AMD cards on macOS. Setting this to (0,0,0,0) will introduce weird behavior.
        rtDesc.mDepth = 1;
        rtDesc.mDescriptors = TF_DESCRIPTOR_TYPE_TEXTURE;
        rtDesc.mFormat = pSwapChain->mFormat;
        rtDesc.mStartState = TF_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        rtDesc.mHeight = mSettings.mHeight;
        rtDesc.mSampleCount = TF_SAMPLE_COUNT_1;
        rtDesc.mSampleQuality = 0;
        rtDesc.mWidth = mSettings.mWidth;
        rtDesc.mFlags = TF_TEXTURE_CREATION_FLAG_ESRAM | TF_TEXTURE_CREATION_FLAG_VR_MULTIVIEW;
        rtDesc.mFlags |= TF_TEXTURE_CREATION_FLAG_DCC | TF_TEXTURE_CREATION_FLAG_TEXTURE_COMPATABILITY;
        addRenderTarget(pRenderer, &rtDesc, &pRenderTargetIntermediate);

        TF_ESRAM_END_ALLOC(pRenderer);

        return pRenderTargetIntermediate != NULL;
    }
};

DEFINE_APPLICATION_MAIN(WaveIntrinsics)
