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

// Unit Test for distributing heavy gpu workload such as Split Frame Rendering to Multiple GPUs (not necessarily identical)
// GPU 0 Renders Left Eye and finally does a composition pass to present Left and Right eye textures to screen
// GPU 1 Renders Right Eye

#define MAX_PLANETS                  20 // Does not affect test, just for allocating space in uniform block. Must match with shader.
#define MAX_GPU_PROFILER_NAME_LENGTH 128

// Interfaces
#include "../../../../Common_3/Application/Interfaces/IApp.h"
#include "../../../../Common_3/Application/Interfaces/ICamera.h"
#include "../../../../Common_3/Application/Interfaces/IFont.h"
#include "../../../../Common_3/OS/Interfaces/IInput.h"
#include "../../../../Common_3/Application/Interfaces/IProfiler.h"
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
#include "../../../../Common_3/Resources/ResourceLoader/TextureContainers.h"
#include "../../../../Common_3/Utilities/Interfaces/IMath.h"

#include "../../../../Common_3/Utilities/Interfaces/IMemory.h"

// fsl
#include "../../../../Common_3/Graphics/FSL/defaults.h"
#include "./Shaders/FSL/Global.srt.h"

/// Demo structures
struct PlanetInfoStruct
{
    mat4  mTranslationMat;
    mat4  mScaleMat;
    mat4  mSharedMat; // Matrix to pass down to children
    vec4  mColor;
    uint  mParentIndex;
    float mYOrbitSpeed; // Rotation speed around parent
    float mZOrbitSpeed;
    float mRotationSpeed; // Rotation speed around self
};

struct UniformBlock
{
    mat4 mProjectView;
    mat4 mSkyProjectView;
    mat4 mToWorldMat[MAX_PLANETS];
    vec4 mColor[MAX_PLANETS];

    // Point Light Information
    vec3 mLightPosition;
    vec3 mLightColor;
};

const uint32_t gDataBufferCount = 3;

const uint32_t gViewCount = 2;

// Number of frames to use between rendering and merging/presentation. Must be < gDataBufferCount
uint32_t gFrameLatency = 2;
uint32_t gBufferedFrames = 0;

// Simulate heavy gpu workload by rendering high resolution spheres
const int   gSphereResolution = 1024; // Increase for higher resolution spheres
const float gSphereDiameter = 0.5f;
const uint  gNumPlanets = 11;     // Sun, Mercury -> Neptune, Pluto, Moon
const uint  gTimeOffset = 600000; // For visually better starting locations
const float gRotSelfScale = 0.0004f;
const float gRotOrbitYScale = 0.001f;
const float gRotOrbitZScale = 0.00001f;

TFRendererContext* pContext = NULL;
TFRenderer*        pRenderer[gViewCount] = { NULL };

TFQueue*        pGraphicsQueue[gViewCount] = { NULL };
GpuCmdRing      gGraphicsCmdRing[gViewCount] = {};
TFBuffer*       pSphereVertexBuffer[gViewCount] = { NULL };
TFBuffer*       pSkyBoxVertexBuffer[gViewCount] = { NULL };
TFTexture*      pSkyBoxTextures[gViewCount][6];
TFRenderTarget* pRenderTargets[gDataBufferCount][gViewCount] = { { NULL } };
TFRenderTarget* pDepthBuffers[gViewCount] = { NULL };

TFBuffer*   pTransferBuffer[gDataBufferCount][gViewCount] = { { NULL } };
TFTexture*  pRenderResult[gDataBufferCount][gViewCount - 1] = { { NULL } };
TFSyncToken gReadbackSyncTokens[gDataBufferCount][gViewCount - 1] = { { NULL } };

TFSemaphore* pImageAcquiredSemaphore = NULL;
TFSwapChain* pSwapChain = NULL;

TFShader*   pSphereShader[gViewCount] = { NULL };
TFPipeline* pSpherePipeline[gViewCount] = { NULL };

TFShader*        pCopyQuadShader = NULL;
TFPipeline*      pCopyQuadPipeline = NULL;
TFBuffer*        pQuadVertexBuffer = NULL;
TFDescriptorSet* pDescriptorSetCopyQuad = NULL;

TFShader*        pSkyBoxDrawShader[gViewCount] = { NULL };
TFPipeline*      pSkyBoxDrawPipeline[gViewCount] = { NULL };
TFSampler*       pSamplerSkyBox[gViewCount] = { NULL };
TFDescriptorSet* pDescriptorSetPersistent[gViewCount] = { NULL };
TFDescriptorSet* pDescriptorSetUniforms[gViewCount] = { NULL };

TFBuffer* pProjViewUniformBuffer[gDataBufferCount][gViewCount] = { { NULL } };

int              gNumberOfSpherePoints;
UniformBlock     gUniformData;
PlanetInfoStruct gPlanetInfoData[gNumPlanets];

TFICamera* pCamera = NULL;

/// UI
TFUIWindowDesc gGuiWindowDesc;
char           gRendererLabels[gViewCount][20] = {};

const char* pSkyBoxImageFileNames[] = {
    "Skybox_right1.tex", "Skybox_left2.tex", "Skybox_top3.tex", "Skybox_bottom4.tex", "Skybox_front5.tex", "Skybox_back6.tex",
};
char         gGpuProfilerNames[gViewCount + 1][MAX_GPU_PROFILER_NAME_LENGTH]{};
ProfileToken gGpuProfilerTokens[gViewCount + 1];
char         gGpuNames[TF_MAX_MULTIPLE_GPUS][MAX_GPU_PROFILER_NAME_LENGTH]{};
const char*  gGpuNamePtrs[] = {
    gGpuNames[0],
    gGpuNames[1],
    gGpuNames[2],
    gGpuNames[3],
};
COMPILE_ASSERT(sizeof(gGpuNamePtrs) / sizeof(gGpuNamePtrs[0]) == TF_MAX_MULTIPLE_GPUS);

ProfileToken gReadBackCpuToken;
ProfileToken gUploadCpuToken;
ProfileToken gGpuExecCpuToken;

TFFontDrawDesc gFrameTimeDraw;
TFFont*        gFont;
TFClearValue   gClearColor; // initialization in Init
TFClearValue   gClearDepth;
bool           gMultiGPU = true;
bool           gMultiGPURestart = false;
bool           gMultiGPUCurrent = true;
uint32_t       gSelectedGpuIndices[gViewCount] = { 0, 1 };
float*         pSpherePoints;

uint32_t    gCurrentScriptIndex = 0;
const char* gTestScripts[] = { "Test0.lua" };

float gFoVH = 90.0f;

#ifdef AUTOMATED_TESTING
// For this UT the test ordering is done manually..
// It is done based of the number of times we reset the APP..
const char*    gSwapRendererTestScriptFileName = "Test_SwapRenderers_Swap.lua";
const char*    gSwapRendererScreenshotTestScriptFileName = "Test_SwapRenderers_Screenshot.lua";
int32_t        gNumResetsFromTestScripts = 0; // number of times we have reset the app..
const uint32_t gNumScripts = (sizeof(gTestScripts) / sizeof(gTestScripts[0])) + 1;
#else
const uint32_t gNumScripts = (sizeof(gTestScripts) / sizeof(gTestScripts[0]));
#endif
uint32_t gNumScriptsToDefine = gNumScripts;

void RunScript(void* pUserData)
{
    UNREF_PARAM(pUserData);
    TFLuaScriptDesc runDesc = {};
    runDesc.pScriptFileName = gTestScripts[gCurrentScriptIndex];
    luaQueueScriptToRun(&runDesc);
}

void SwitchGpuMode(void* pUserData)
{
    UNREF_PARAM(pUserData);
    gMultiGPURestart = true;
    TFResetDesc resetDescriptor;
    resetDescriptor.mType = TF_RESET_TYPE_GPU_MODE_SWITCH;
    requestReset(&resetDescriptor);
}

void RequestResetGPUSwitch(void* pUserData)
{
    UNREF_PARAM(pUserData);
    uint32_t temporary = gSelectedGpuIndices[0];
    gSelectedGpuIndices[0] = gSelectedGpuIndices[1];
    gSelectedGpuIndices[1] = temporary;

    TFResetDesc resetDescriptor;
    resetDescriptor.mType = TF_RESET_TYPE_GRAPHIC_CARD_SWITCH;
    requestReset(&resetDescriptor);
}

class UnlinkedMultiGPU: public IApp
{
public:
    bool Init()
    {
        gMultiGPUCurrent = gMultiGPU;

        gClearColor.r = 0.0f;
        gClearColor.g = 0.0f;
        gClearColor.b = 0.0f;
        gClearColor.a = 0.0f;

        gClearDepth.depth = 0.0f;
        gClearDepth.stencil = 0;

        // Generate sphere vertex buffer
        if (!pSpherePoints)
        {
            gNumberOfSpherePoints = 0;
            generateSpherePoints(NULL, &gNumberOfSpherePoints, gSphereResolution, gSphereDiameter);
            pSpherePoints = (float*)tf_malloc(sizeof(float) * gNumberOfSpherePoints);
            generateSpherePoints(pSpherePoints, &gNumberOfSpherePoints, gSphereResolution, gSphereDiameter);
        }

        // Setup planets (Rotation speeds are relative to Earth's, some values randomly given)
        // Sun
        gPlanetInfoData[0].mParentIndex = 0;
        gPlanetInfoData[0].mYOrbitSpeed = 0; // Earth years for one orbit
        gPlanetInfoData[0].mZOrbitSpeed = 0;
        gPlanetInfoData[0].mRotationSpeed = 24.0f; // Earth days for one rotation
        gPlanetInfoData[0].mTranslationMat = mat4::identity();
        gPlanetInfoData[0].mScaleMat = mat4::scale(vec3(10.0f));
        gPlanetInfoData[0].mColor = vec4(0.97f, 0.38f, 0.09f, 0.0f);

        // Mercury
        gPlanetInfoData[1].mParentIndex = 0;
        gPlanetInfoData[1].mYOrbitSpeed = 0.5f;
        gPlanetInfoData[1].mZOrbitSpeed = 0.0f;
        gPlanetInfoData[1].mRotationSpeed = 58.7f;
        gPlanetInfoData[1].mTranslationMat = mat4::translation(vec3(10.0f, 0, 0));
        gPlanetInfoData[1].mScaleMat = mat4::scale(vec3(1.0f));
        gPlanetInfoData[1].mColor = vec4(0.45f, 0.07f, 0.006f, 1.0f);

        // Venus
        gPlanetInfoData[2].mParentIndex = 0;
        gPlanetInfoData[2].mYOrbitSpeed = 0.8f;
        gPlanetInfoData[2].mZOrbitSpeed = 0.0f;
        gPlanetInfoData[2].mRotationSpeed = 243.0f;
        gPlanetInfoData[2].mTranslationMat = mat4::translation(vec3(20.0f, 0, 5));
        gPlanetInfoData[2].mScaleMat = mat4::scale(vec3(2));
        gPlanetInfoData[2].mColor = vec4(0.6f, 0.32f, 0.006f, 1.0f);

        // Earth
        gPlanetInfoData[3].mParentIndex = 0;
        gPlanetInfoData[3].mYOrbitSpeed = 1.0f;
        gPlanetInfoData[3].mZOrbitSpeed = 0.0f;
        gPlanetInfoData[3].mRotationSpeed = 1.0f;
        gPlanetInfoData[3].mTranslationMat = mat4::translation(vec3(30.0f, 0, 0));
        gPlanetInfoData[3].mScaleMat = mat4::scale(vec3(4));
        gPlanetInfoData[3].mColor = vec4(0.07f, 0.028f, 0.61f, 1.0f);

        // Mars
        gPlanetInfoData[4].mParentIndex = 0;
        gPlanetInfoData[4].mYOrbitSpeed = 2.0f;
        gPlanetInfoData[4].mZOrbitSpeed = 0.0f;
        gPlanetInfoData[4].mRotationSpeed = 1.1f;
        gPlanetInfoData[4].mTranslationMat = mat4::translation(vec3(40.0f, 0, 0));
        gPlanetInfoData[4].mScaleMat = mat4::scale(vec3(3));
        gPlanetInfoData[4].mColor = vec4(0.79f, 0.07f, 0.006f, 1.0f);

        // Jupiter
        gPlanetInfoData[5].mParentIndex = 0;
        gPlanetInfoData[5].mYOrbitSpeed = 11.0f;
        gPlanetInfoData[5].mZOrbitSpeed = 0.0f;
        gPlanetInfoData[5].mRotationSpeed = 0.4f;
        gPlanetInfoData[5].mTranslationMat = mat4::translation(vec3(50.0f, 0, 0));
        gPlanetInfoData[5].mScaleMat = mat4::scale(vec3(8));
        gPlanetInfoData[5].mColor = vec4(0.32f, 0.13f, 0.13f, 1);

        // Saturn
        gPlanetInfoData[6].mParentIndex = 0;
        gPlanetInfoData[6].mYOrbitSpeed = 29.4f;
        gPlanetInfoData[6].mZOrbitSpeed = 0.0f;
        gPlanetInfoData[6].mRotationSpeed = 0.5f;
        gPlanetInfoData[6].mTranslationMat = mat4::translation(vec3(60.0f, 0, 0));
        gPlanetInfoData[6].mScaleMat = mat4::scale(vec3(6));
        gPlanetInfoData[6].mColor = vec4(0.45f, 0.45f, 0.21f, 1.0f);

        // Uranus
        gPlanetInfoData[7].mParentIndex = 0;
        gPlanetInfoData[7].mYOrbitSpeed = 84.07f;
        gPlanetInfoData[7].mZOrbitSpeed = 0.0f;
        gPlanetInfoData[7].mRotationSpeed = 0.8f;
        gPlanetInfoData[7].mTranslationMat = mat4::translation(vec3(70.0f, 0, 0));
        gPlanetInfoData[7].mScaleMat = mat4::scale(vec3(7));
        gPlanetInfoData[7].mColor = vec4(0.13f, 0.13f, 0.32f, 1.0f);

        // Neptune
        gPlanetInfoData[8].mParentIndex = 0;
        gPlanetInfoData[8].mYOrbitSpeed = 164.81f;
        gPlanetInfoData[8].mZOrbitSpeed = 0.0f;
        gPlanetInfoData[8].mRotationSpeed = 0.9f;
        gPlanetInfoData[8].mTranslationMat = mat4::translation(vec3(80.0f, 0, 0));
        gPlanetInfoData[8].mScaleMat = mat4::scale(vec3(8));
        gPlanetInfoData[8].mColor = vec4(0.21f, 0.028f, 0.79f, 1.0f);

        // Pluto - Not a planet XDD
        gPlanetInfoData[9].mParentIndex = 0;
        gPlanetInfoData[9].mYOrbitSpeed = 247.7f;
        gPlanetInfoData[9].mZOrbitSpeed = 1.0f;
        gPlanetInfoData[9].mRotationSpeed = 7.0f;
        gPlanetInfoData[9].mTranslationMat = mat4::translation(vec3(90.0f, 0, 0));
        gPlanetInfoData[9].mScaleMat = mat4::scale(vec3(1.0f));
        gPlanetInfoData[9].mColor = vec4(0.45f, 0.21f, 0.21f, 1.0f);

        // Moon
        gPlanetInfoData[10].mParentIndex = 3;
        gPlanetInfoData[10].mYOrbitSpeed = 1.0f;
        gPlanetInfoData[10].mZOrbitSpeed = 200.0f;
        gPlanetInfoData[10].mRotationSpeed = 27.0f;
        gPlanetInfoData[10].mTranslationMat = mat4::translation(vec3(5.0f, 0, 0));
        gPlanetInfoData[10].mScaleMat = mat4::scale(vec3(1));
        gPlanetInfoData[10].mColor = vec4(0.07f, 0.07f, 0.13f, 1.0f);

        TFRendererContextDesc contextSettings;
        memset(&contextSettings, 0, sizeof(contextSettings));
        TFExtendedSettings* extendedSettings = NULL;
        initGPUConfig(extendedSettings);
        initRendererContext(GetName(), &contextSettings, &pContext);
        mSettings.mFrameMaxCount = gDataBufferCount;

        TFRootSignatureDesc rootDesc = {};
        INIT_RS_DESC(rootDesc, "default.rootsig", "compute.rootsig");

        if (!pContext || pContext->mGpuCount < 1)
        {
            LOGF(LogLevel::eWARNING, "Unlinked multi GPU is not supported with the selected API Falling back to single GPU mode.");

            TFRendererDesc settings;
            memset(&settings, 0, sizeof(settings));
            settings.mGpuMode = TF_GPU_MODE_SINGLE;

            initGPUConfig(settings.pExtendedSettings);
            initRenderer(GetName(), &settings, &pRenderer[0]);
            // Check for init success
            if (!pRenderer[0])
            {
                ShowUnsupportedMessage(getUnsupportedGPUMsg());
                return false;
            }
            initRootSignature(pRenderer[0], &rootDesc);
            setGPUConfig(pRenderer[0]->pContext->mGpus, pRenderer[0]->pContext->mGpuCount,
                         (uint32_t)(pRenderer[0]->pGpu - pRenderer[0]->pContext->mGpus), settings.pExtendedSettings);
            for (uint32_t i = 1; i < gViewCount; ++i)
                pRenderer[i] = pRenderer[0];

            gMultiGPUCurrent = false;
        }
        else
        {
            if (pContext->mGpuCount < 2 && gMultiGPUCurrent)
            {
                LOGF(LogLevel::eWARNING, "The system has only one GPU, Renderers will be created on the same GPU");
            }
            for (uint32_t i = 0; i < gViewCount; ++i)
            {
                if (gSelectedGpuIndices[i] >= pContext->mGpuCount)
                    gSelectedGpuIndices[i] = 0;
            }

            TFRendererDesc settings;
            memset(&settings, 0, sizeof(settings));
            settings.mGpuMode = gMultiGPUCurrent ? TF_GPU_MODE_UNLINKED : TF_GPU_MODE_SINGLE;
            settings.pContext = pContext;
            for (uint32_t i = 0; i < gViewCount; ++i)
            {
                if (!gMultiGPUCurrent && i > 0)
                {
                    pRenderer[i] = pRenderer[0];
                }
                else
                {
                    settings.mGpuIndex = gSelectedGpuIndices[i];
                    initRenderer(GetName(), &settings, &pRenderer[i]);
                    // check for init success
                    if (!pRenderer[i])
                    {
                        ShowUnsupportedMessage(getUnsupportedGPUMsg());
                        return false;
                    }
                    initRootSignature(pRenderer[i], &rootDesc);
                    setGPUConfig(pRenderer[i]->pContext->mGpus, pRenderer[i]->pContext->mGpuCount,
                                 (uint32_t)(pRenderer[i]->pGpu - pRenderer[i]->pContext->mGpus), settings.pExtendedSettings);
                }
            }

            // Fill GPU names to be used later by UI
            for (uint32_t i = 0; i < pContext->mGpuCount; ++i)
            {
                snprintf(gGpuNames[i], sizeof(gGpuNames), "GPU %u : %s", i, //-V512
                         pContext->mGpus[i].mGpuVendorPreset.mGpuName);
            }
        }

        const uint32_t rendererCount = gMultiGPUCurrent ? gViewCount : 1;

        initResourceLoaderInterface(pRenderer, rendererCount);

        for (uint32_t i = 0; i < gViewCount; ++i)
        {
            TFQueueDesc queueDesc = {};
            queueDesc.mType = TF_QUEUE_TYPE_GRAPHICS;
            queueDesc.mFlag = TF_QUEUE_FLAG_INIT_MICROPROFILE;
            queueDesc.mNodeIndex = i;

            if (gMultiGPUCurrent || i == 0)
                initQueue(pRenderer[i], &queueDesc, &pGraphicsQueue[i]);
            else
                pGraphicsQueue[i] = pGraphicsQueue[0];

            uint32_t rendererIndex = gMultiGPUCurrent ? i : 0;
            snprintf(gGpuProfilerNames[i], sizeof(gGpuProfilerNames[gViewCount]), "Graphics %d: GPU %u (%s)", i,
                     gSelectedGpuIndices[rendererIndex], pRenderer[i]->pGpu->mGpuVendorPreset.mGpuName);
        }

        snprintf(gGpuProfilerNames[gViewCount], sizeof(gGpuProfilerNames[gViewCount]), "Merge (GPU %u)", gSelectedGpuIndices[0]);

        // Load fonts
        TFFontSystemDesc fontSystemDesc{};
        fontSystemDesc.pRenderer = pRenderer[0];
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
        uiRenderDesc.pRenderer = pRenderer[0];
        uiRenderDesc.mFrameMaxCount = mSettings.mFrameMaxCount;
        uiRenderDesc.pFrameIdx = &mSettings.mFrameIdx;
        uiRenderDesc.pFont = gFont;
        initUserInterface(&uiRenderDesc);

        TFLuaScriptDesc scriptDescs[gNumScripts] = {};

#ifdef AUTOMATED_TESTING
        if (gNumResetsFromTestScripts == 0)
        {
            // Add all non swap render scripts
            for (uint32_t i = 0; i < gNumScripts - 1; ++i)
                scriptDescs[i].pScriptFileName = gTestScripts[i];

            // Last slot is for render swapping
            scriptDescs[gNumScripts - 1].pScriptFileName = gSwapRendererTestScriptFileName;
        }
        else if (gNumResetsFromTestScripts == 1) // we only want to take a screenshot for swapped renderer
        {
            scriptDescs[0].pScriptFileName = gSwapRendererScreenshotTestScriptFileName;
            scriptDescs[1].pScriptFileName = gSwapRendererTestScriptFileName;
        }
        else if (gNumResetsFromTestScripts == 2) // just take a default screenshot again (since first one gets overriden)..
        {
            gNumScriptsToDefine = 0;
            gNumResetsFromTestScripts = -1;
        }
#endif
        luaDefineScripts(scriptDescs, gNumScriptsToDefine);

        // Initialize micro profiler and its UI.
        TFProfilerDesc profiler = {};
        profiler.pRenderer = pRenderer[0];
        initProfiler(&profiler);

        for (uint32_t i = 0; i < gViewCount; ++i)
        {
            gGpuProfilerTokens[i] = initGpuProfiler(pRenderer[i], pGraphicsQueue[i], gGpuProfilerNames[i]);
        }

        gGpuProfilerTokens[gViewCount] = initGpuProfiler(pRenderer[0], pGraphicsQueue[0], gGpuProfilerNames[gViewCount]);
        gReadBackCpuToken = getCpuProfileToken("Stalls", "Reading results", 0xff00ffff);
        gUploadCpuToken = getCpuProfileToken("Stalls", "Upload results", 0xff00ffff);
        gGpuExecCpuToken = getCpuProfileToken("Stalls", "Gpu execution", 0xff00ffff);

        for (uint32_t j = 0; j < gViewCount; ++j)
        {
            GpuCmdRingDesc cmdRingDesc = {};
            cmdRingDesc.pQueue = pGraphicsQueue[j];
            cmdRingDesc.mPoolCount = gDataBufferCount;
            cmdRingDesc.mCmdPerPoolCount = j == 0 ? 2 : 1;
            cmdRingDesc.mAddSyncPrimitives = true;
            initGpuCmdRing(pRenderer[j], &cmdRingDesc, &gGraphicsCmdRing[j]);
        }

        initSemaphore(pRenderer[0], &pImageAcquiredSemaphore);

        for (uint32_t i = 0; i < gViewCount; ++i)
        {
            if (!gMultiGPUCurrent && i > 0)
            {
                pSamplerSkyBox[i] = pSamplerSkyBox[0];
            }
            else
            {
                TFSamplerDesc samplerDesc = { TF_FILTER_LINEAR,
                                              TF_FILTER_LINEAR,
                                              TF_MIPMAP_MODE_NEAREST,
                                              TF_ADDRESS_MODE_CLAMP_TO_EDGE,
                                              TF_ADDRESS_MODE_CLAMP_TO_EDGE,
                                              TF_ADDRESS_MODE_CLAMP_TO_EDGE };
                addSampler(pRenderer[i], &samplerDesc, &pSamplerSkyBox[i]);

                samplerDesc.mMagFilter = TF_FILTER_NEAREST;
                samplerDesc.mMinFilter = TF_FILTER_NEAREST;
            }
        }
        uint64_t         sphereDataSize = gNumberOfSpherePoints * sizeof(float);
        TFBufferLoadDesc sphereVbDesc = {};
        sphereVbDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_VERTEX_BUFFER;
        sphereVbDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_GPU_ONLY;
        sphereVbDesc.mDesc.mSize = sphereDataSize;
        sphereVbDesc.pData = pSpherePoints;

        // Generate sky box vertex buffer
        float skyBoxPoints[] = {
            10.0f,  -10.0f, -10.0f, 6.0f, // -z
            -10.0f, -10.0f, -10.0f, 6.0f,   -10.0f, 10.0f,  -10.0f, 6.0f,   -10.0f, 10.0f,
            -10.0f, 6.0f,   10.0f,  10.0f,  -10.0f, 6.0f,   10.0f,  -10.0f, -10.0f, 6.0f,

            -10.0f, -10.0f, 10.0f,  2.0f, //-x
            -10.0f, -10.0f, -10.0f, 2.0f,   -10.0f, 10.0f,  -10.0f, 2.0f,   -10.0f, 10.0f,
            -10.0f, 2.0f,   -10.0f, 10.0f,  10.0f,  2.0f,   -10.0f, -10.0f, 10.0f,  2.0f,

            10.0f,  -10.0f, -10.0f, 1.0f, //+x
            10.0f,  -10.0f, 10.0f,  1.0f,   10.0f,  10.0f,  10.0f,  1.0f,   10.0f,  10.0f,
            10.0f,  1.0f,   10.0f,  10.0f,  -10.0f, 1.0f,   10.0f,  -10.0f, -10.0f, 1.0f,

            -10.0f, -10.0f, 10.0f,  5.0f, // +z
            -10.0f, 10.0f,  10.0f,  5.0f,   10.0f,  10.0f,  10.0f,  5.0f,   10.0f,  10.0f,
            10.0f,  5.0f,   10.0f,  -10.0f, 10.0f,  5.0f,   -10.0f, -10.0f, 10.0f,  5.0f,

            -10.0f, 10.0f,  -10.0f, 3.0f, //+y
            10.0f,  10.0f,  -10.0f, 3.0f,   10.0f,  10.0f,  10.0f,  3.0f,   10.0f,  10.0f,
            10.0f,  3.0f,   -10.0f, 10.0f,  10.0f,  3.0f,   -10.0f, 10.0f,  -10.0f, 3.0f,

            10.0f,  -10.0f, 10.0f,  4.0f, //-y
            10.0f,  -10.0f, -10.0f, 4.0f,   -10.0f, -10.0f, -10.0f, 4.0f,   -10.0f, -10.0f,
            -10.0f, 4.0f,   -10.0f, -10.0f, 10.0f,  4.0f,   10.0f,  -10.0f, 10.0f,  4.0f,
        };

        uint64_t         skyBoxDataSize = 4 * 6 * 6 * sizeof(float);
        TFBufferLoadDesc skyboxVbDesc = {};
        skyboxVbDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_VERTEX_BUFFER;
        skyboxVbDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_GPU_ONLY;
        skyboxVbDesc.mDesc.mSize = skyBoxDataSize;
        skyboxVbDesc.pData = skyBoxPoints;

        TFTextureLoadDesc textureDesc = {};

        for (uint32_t view = 0; view < rendererCount; ++view)
        {
            textureDesc.mNodeIndex = view;

            for (int i = 0; i < 6; ++i)
            {
                textureDesc.pFileName = pSkyBoxImageFileNames[i];
                textureDesc.ppTexture = &pSkyBoxTextures[view][i];
                // Textures representing color should be stored in SRGB or HDR format
                textureDesc.mCreationFlag = TF_TEXTURE_CREATION_FLAG_SRGB;

                addResource(&textureDesc, NULL);
            }

            sphereVbDesc.mDesc.mNodeIndex = view;
            sphereVbDesc.ppBuffer = &pSphereVertexBuffer[view];

            skyboxVbDesc.mDesc.mNodeIndex = view;
            skyboxVbDesc.ppBuffer = &pSkyBoxVertexBuffer[view];

            addResource(&sphereVbDesc, NULL);
            addResource(&skyboxVbDesc, NULL);
        }
        const float quadVertices[6 * 6] = {
            1.f, 1.f, 0.f, 1.f, 1.f, 0.f, -1.f, 1.f,  0.f, 1.f, 0.f, 0.f, -1.f, -1.f, 0.f, 1.f, 0.f, 1.f,
            1.f, 1.f, 0.f, 1.f, 1.f, 0.f, -1.f, -1.f, 0.f, 1.f, 0.f, 1.f, 1.f,  -1.f, 0.f, 1.f, 1.f, 1.f,
        };

        TFBufferLoadDesc quadDesc = {};
        quadDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_VERTEX_BUFFER;
        quadDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_GPU_ONLY;
        quadDesc.mDesc.mSize = sizeof(quadVertices);
        quadDesc.pData = quadVertices;
        quadDesc.ppBuffer = &pQuadVertexBuffer;
        quadDesc.mDesc.mNodeIndex = 0;
        addResource(&quadDesc, NULL);

        TFBufferLoadDesc ubDesc = {};
        ubDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        ubDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
        ubDesc.mDesc.mSize = sizeof(UniformBlock);
        ubDesc.mDesc.mFlags = TF_BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
        ubDesc.pData = NULL;
        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            for (uint32_t j = 0; j < rendererCount; ++j)
            {
                ubDesc.mDesc.mNodeIndex = j;
                ubDesc.ppBuffer = &pProjViewUniformBuffer[i][j];
                addResource(&ubDesc, NULL);
            }
        }

        waitForAllResourceLoads();

        if (!gMultiGPUCurrent)
        {
            for (uint32_t view = 1; view < gViewCount; ++view)
            {
                for (int i = 0; i < 6; ++i)
                    pSkyBoxTextures[view][i] = pSkyBoxTextures[0][i];
                pSphereVertexBuffer[view] = pSphereVertexBuffer[0];
                pSkyBoxVertexBuffer[view] = pSkyBoxVertexBuffer[0];
                for (uint32_t i = 0; i < gDataBufferCount; ++i)
                {
                    pProjViewUniformBuffer[i][view] = pProjViewUniformBuffer[i][0];
                }
            }
        }

        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            for (uint32_t j = 0; j < gViewCount - 1; ++j)
                gReadbackSyncTokens[i][j] = 0;
        }

        TFCameraMotionParameters cmp{ 160.0f, 600.0f, 600.0f };
        vec3                     camPos{ 48.0f, 48.0f, 20.0f };
        vec3                     lookAt{ 0 };

        pCamera = initFpsCamera(camPos, lookAt);
        pCamera->setMotionParameters(cmp);

        AddCustomInputBindings();
        initScreenshotCapturer(pRenderer[0], pGraphicsQueue[0], GetName());
        gMultiGPURestart = false;

        return true;
    }

    void Exit()
    {
#ifdef AUTOMATED_TESTING
        // Exit is only called whenever SwapRenderer.lua script gets executed or API gets switched
        // In case of API switch we can ignore (it will already be -1)
        // In case if SwapRenderer, this decides what scripts get executed...
        gNumResetsFromTestScripts++;
#endif
        exitScreenshotCapturer();
        const uint32_t rendererCount = gMultiGPUCurrent ? gViewCount : 1;

        exitCamera(pCamera);

        exitProfiler();

        exitUserInterface();

        removeFont(gFont);
        exitFontSystem();

        removeResource(pQuadVertexBuffer);

        for (uint32_t view = 0; view < rendererCount; ++view)
        {
            for (uint32_t i = 0; i < gDataBufferCount; ++i)
            {
                removeResource(pProjViewUniformBuffer[i][view]);
            }

            removeResource(pSphereVertexBuffer[view]);
            removeResource(pSkyBoxVertexBuffer[view]);

            for (uint i = 0; i < 6; ++i)
                removeResource(pSkyBoxTextures[view][i]);
            removeSampler(pRenderer[view], pSamplerSkyBox[view]);
        }

        for (uint32_t j = 0; j < gViewCount; ++j)
        {
            exitGpuCmdRing(pRenderer[j], &gGraphicsCmdRing[j]);
        }

        for (uint32_t view = 0; view < rendererCount; ++view)
        {
            exitQueue(pRenderer[view], pGraphicsQueue[view]);
        }

        exitSemaphore(pRenderer[0], pImageAcquiredSemaphore);

        exitResourceLoaderInterface(pRenderer, rendererCount);
        for (uint32_t view = 0; view < gViewCount; ++view)
        {
            if (view < rendererCount)
            {
                exitRootSignature(pRenderer[view]);
                exitRenderer(pRenderer[view]);
            }

            pRenderer[view] = NULL;
        }
        removeGPUConfigurationRules();
        if (pContext)
        {
            exitRendererContext(pContext);
            pContext = NULL;
        }

        gBufferedFrames = 0;

        if (!gMultiGPURestart)
        {
            // Need to free memory;
            tf_free(pSpherePoints);
            pSpherePoints = 0;
        }
    }

    bool Load(TFReloadDesc* pReloadDesc)
    {
        UNREF_PARAM(pReloadDesc);

        addShaders();
        addDescriptorSets();

        loadProfilerUI(mSettings.mWidth, mSettings.mHeight);

        gGuiWindowDesc = {};
        gGuiWindowDesc.mStartPos = vec2(mSettings.mWidth * 0.01f, mSettings.mHeight * 0.2f);
        gGuiWindowDesc.mStartSize = vec2(600.0f, 550.0f);
        gGuiWindowDesc.pWindowTitle = GetName();
        gGuiWindowDesc.mFlags = TF_UI_WINDOW_TITLE | TF_UI_WINDOW_SCALABLE | TF_UI_WINDOW_MOVABLE | TF_UI_WINDOW_MINIMIZABLE |
                                TF_UI_WINDOW_BORDER | TF_UI_WINDOW_INIT_HEIGHT_FIT;

        // Register UI functionality for Lua
        {
            TFLuaWidgetVariableDesc luaVarDesc = {};
            TFLuaWidgetFunctionDesc luaFuncDesc = {};

            if (pContext)
            {
                luaVarDesc.mWidgetType = TF_WIDGET_TYPE_CHECKBOX;
                luaVarDesc.pLabel = "Enable Multi Renderer";
                luaVarDesc.pBool = &gMultiGPU;
                luaRegisterWidgetVariable(&luaVarDesc);
                luaFuncDesc.mType = TF_LUA_WIDGET_FUNCTION_ON_EDITED;
                luaFuncDesc.pLabel = "Enable Multi Renderer";
                luaFuncDesc.pFuncData = NULL;
                luaFuncDesc.pFunc = SwitchGpuMode;
                luaRegisterWidgetFunction(&luaFuncDesc);

                const uint32_t rendererCount = gMultiGPUCurrent ? gViewCount : 1;
                luaVarDesc.mWidgetType = TF_WIDGET_TYPE_DROPDOWN;
                for (uint32_t i = 0; i < rendererCount; ++i)
                {
                    snprintf(gRendererLabels[i], 20, "Renderer %d", i);
                    luaVarDesc.pLabel = gRendererLabels[i];
                    luaVarDesc.pUint = &gSelectedGpuIndices[i];
                    luaRegisterWidgetVariable(&luaVarDesc);
                    luaFuncDesc.mType = TF_LUA_WIDGET_FUNCTION_ON_EDITED;
                    luaFuncDesc.pLabel = gRendererLabels[i];
                    luaFuncDesc.pFuncData = NULL;
                    luaFuncDesc.pFunc = SwitchGpuMode;
                    luaRegisterWidgetFunction(&luaFuncDesc);
                }

#ifdef AUTOMATED_TESTING
                luaFuncDesc.mType = TF_LUA_WIDGET_FUNCTION_ON_EDITED;
                luaFuncDesc.pLabel = "Swap Renderers";
                luaFuncDesc.pFuncData = this;
                luaFuncDesc.pFunc = RequestResetGPUSwitch;
                luaRegisterWidgetFunction(&luaFuncDesc);
#endif
            }

            luaVarDesc.mWidgetType = TF_WIDGET_TYPE_SLIDER_UINT;
            luaVarDesc.pLabel = "Frame Latency";
            luaVarDesc.pUint = &gFrameLatency;
            luaRegisterWidgetVariable(&luaVarDesc);
            luaVarDesc.mWidgetType = TF_WIDGET_TYPE_SLIDER_FLOAT;
            luaVarDesc.pLabel = "Camera Horizontal FoV";
            luaVarDesc.pFloat = &gFoVH;
            luaRegisterWidgetVariable(&luaVarDesc);
            luaVarDesc.mWidgetType = TF_WIDGET_TYPE_DROPDOWN;
            luaVarDesc.pLabel = "Test Scripts";
            luaVarDesc.pUint = &gCurrentScriptIndex;
            luaRegisterWidgetVariable(&luaVarDesc);

            luaFuncDesc.mType = TF_LUA_WIDGET_FUNCTION_ON_EDITED;
            luaFuncDesc.pLabel = "Run";
            luaFuncDesc.pFuncData = NULL;
            luaFuncDesc.pFunc = RunScript;
            luaRegisterWidgetFunction(&luaFuncDesc);
        }

        if (!addSwapChain())
            return false;

        if (!addRenderTargets())
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

        memset(gReadbackSyncTokens, 0, sizeof(gReadbackSyncTokens));

        return true;
    }

    void Unload(TFReloadDesc* pReloadDesc)
    {
        UNREF_PARAM(pReloadDesc);

        const uint32_t rendererCount = gMultiGPUCurrent ? gViewCount : 1;

        for (uint32_t i = 0; i < rendererCount; ++i)
            waitQueueIdle(pGraphicsQueue[i]);

        unloadFontSystem();
        unloadUserInterface();

        // Must flush resource loader queues as well.
        waitForAllResourceLoads();
        waitCopyQueueIdle();

        removePipelines();

        removeSwapChain(pRenderer[0], pSwapChain);

        removeRenderTargets();

        unloadProfilerUI();

        removeDescriptorSets();
        removeShaders();
    }

    void Update(float deltaTime)
    {
        updateGui();

        if (!uiIsFocused())
        {
            pCamera->onMove({ inputGetValue(0, CUSTOM_MOVE_X), inputGetValue(0, CUSTOM_MOVE_Y) });
            pCamera->onRotate({ inputGetValue(0, CUSTOM_LOOK_X), inputGetValue(0, CUSTOM_LOOK_Y) });
            pCamera->onMoveY(inputGetValue(0, CUSTOM_MOVE_UP));
            if (inputGetValue(0, CUSTOM_RESET_VIEW))
            {
                pCamera->resetView();
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
        }

        /************************************************************************/
        // Update frame latency
        /************************************************************************/
        static uint32_t frameLatencyCurrent = gFrameLatency;
        if (gFrameLatency != frameLatencyCurrent)
        {
            resetFrameLatency();
            frameLatencyCurrent = gFrameLatency;
        }

        pCamera->update(deltaTime);

        /************************************************************************/
        // Scene Update
        /************************************************************************/
        static float currentTime = 0.0f;
        currentTime += deltaTime * 1000.0f;

        // Update camera with time
        TFCameraMatrix viewMat = pCamera->getViewMatrix();
        const float    aspectInverse = (float)mSettings.mHeight / ((float)mSettings.mWidth * 0.5f);
        const float    horizontalFov = gFoVH * PI / 180.0f;
        const float    nearPlane = 0.1f;
        const float    farPlane = 1000.0f;
        TFCameraMatrix projMat = camMatPerspectiveReverseZ(horizontalFov, aspectInverse, nearPlane, farPlane);
        gUniformData.mProjectView = camMatMul(&projMat, &viewMat).mMatrices[MONO_CAMERA_VIEW_INDEX];

        // Point light parameters
        gUniformData.mLightPosition = vec3(0, 0, 0);
        gUniformData.mLightColor = vec3(0.9f, 0.9f, 0.7f); // Pale Yellow

        // Update planet transformations
        for (int i = 0; i < gNumPlanets; i++)
        {
            mat4 rotSelf, rotOrbitY, rotOrbitZ, trans, scale, parentMat;
            rotSelf = rotOrbitY = rotOrbitZ = parentMat = mat4::identity();
            if (gPlanetInfoData[i].mRotationSpeed > 0.0f)
                rotSelf = mat4::rotationY(gRotSelfScale * (currentTime + gTimeOffset) / gPlanetInfoData[i].mRotationSpeed);
            if (gPlanetInfoData[i].mYOrbitSpeed > 0.0f)
                rotOrbitY = mat4::rotationY(gRotOrbitYScale * (currentTime + gTimeOffset) / gPlanetInfoData[i].mYOrbitSpeed);
            if (gPlanetInfoData[i].mZOrbitSpeed > 0.0f)
                rotOrbitZ = mat4::rotationZ(gRotOrbitZScale * (currentTime + gTimeOffset) / gPlanetInfoData[i].mZOrbitSpeed);
            if (gPlanetInfoData[i].mParentIndex > 0)
                parentMat = gPlanetInfoData[gPlanetInfoData[i].mParentIndex].mSharedMat;

            trans = gPlanetInfoData[i].mTranslationMat;
            scale = gPlanetInfoData[i].mScaleMat;

            gPlanetInfoData[i].mSharedMat = parentMat * rotOrbitY * trans;
            gUniformData.mToWorldMat[i] = parentMat * rotOrbitY * rotOrbitZ * trans * rotSelf * scale;
            gUniformData.mColor[i] = gPlanetInfoData[i].mColor;
        }

        viewMat = camMatSetTranslation(&viewMat, vec3(0));
        gUniformData.mSkyProjectView = camMatMul(&projMat, &viewMat).mMatrices[MONO_CAMERA_VIEW_INDEX];
    }

    void Draw()
    {
        if ((bool)pSwapChain->mEnableVsync != mSettings.mVSyncEnabled)
        {
            waitQueueIdle(pGraphicsQueue[0]);
            ::toggleVSync(pRenderer[0], &pSwapChain);
        }

        // Update uniform buffers
        for (int i = gViewCount - 1; i >= 0; --i)
        {
            if (!gMultiGPUCurrent && i > 0)
                continue;

            TFBufferUpdateDesc viewProjCbv = { pProjViewUniformBuffer[mSettings.mFrameIdx][i] };
            beginUpdateResource(&viewProjCbv);
            memcpy(viewProjCbv.pMappedData, &gUniformData, sizeof(gUniformData));
            endUpdateResource(&viewProjCbv);
        }

        for (int i = gViewCount - 1; i >= 0; --i)
        {
            /************************************************************************/
            // View render pass
            /************************************************************************/
            const uint32_t  nextFrameIndex = (mSettings.mFrameIdx + gFrameLatency) % gDataBufferCount;
            TFRenderTarget* pRenderTarget = pRenderTargets[nextFrameIndex][i];
            TFRenderTarget* pDepthBuffer = pDepthBuffers[i];
            TFSemaphore*    pRenderCompleteSemaphore = gGraphicsCmdRing[i].pSemaphores[mSettings.mFrameIdx][0];
            TFFence*        pRenderCompleteFence = gGraphicsCmdRing[i].pFences[mSettings.mFrameIdx][0];
            TFCmd*          cmd = gGraphicsCmdRing[i].pCmds[mSettings.mFrameIdx][0];

            // Simply record the screen cleaning command
            beginCmd(cmd);
            cmdBindDescriptorSet(cmd, 0, pDescriptorSetPersistent[i]);
            cmdBeginGpuFrameProfile(cmd, gGpuProfilerTokens[i]);

            if (i == 0 || !gMultiGPUCurrent)
            {
                TFRenderTargetBarrier barrier = { pRenderTarget, TF_RESOURCE_STATE_SHADER_RESOURCE, TF_RESOURCE_STATE_RENDER_TARGET };
                cmdResourceBarrier(cmd, 0, NULL, 0, NULL, 1, &barrier);
            }
            else
            {
                TFRenderTargetBarrier barrier = { pRenderTarget, TF_RESOURCE_STATE_COPY_SOURCE, TF_RESOURCE_STATE_COPY_SOURCE };
                // Barrier - Acquire from Transfer queue to be used on Graphics queue as RT
                // Conditional barrier as if the transfer queue never acquired the RT, no need to reacquire
                if (gReadbackSyncTokens[nextFrameIndex][i - 1] && isTokenCompleted(&gReadbackSyncTokens[nextFrameIndex][i - 1]))
                {
                    barrier.mAcquire = true;
                    barrier.mQueueType = TF_QUEUE_TYPE_TRANSFER;
                    cmdResourceBarrier(cmd, 0, NULL, 0, NULL, 1, &barrier);
                }
                barrier = { pRenderTarget, TF_RESOURCE_STATE_COPY_SOURCE, TF_RESOURCE_STATE_RENDER_TARGET };
                cmdResourceBarrier(cmd, 0, NULL, 0, NULL, 1, &barrier);
            }

            // Bind Render Targets
            TFBindRenderTargetsDesc bindRenderTargets = {};
            bindRenderTargets.mRenderTargetCount = 1;
            bindRenderTargets.mRenderTargets[0] = { pRenderTarget, TF_LOAD_ACTION_CLEAR };
            bindRenderTargets.mDepthStencil = { pDepthBuffer, TF_LOAD_ACTION_CLEAR };
            cmdBindRenderTargets(cmd, &bindRenderTargets);

            cmdSetViewport(cmd, 0.0f, 0.0f, (float)pRenderTarget->mWidth, (float)pRenderTarget->mHeight, 0.0f, 1.0f);
            cmdSetScissor(cmd, 0, 0, pRenderTarget->mWidth, pRenderTarget->mHeight);

            // Skybox pass
            {
                const uint32_t skyboxStride = sizeof(float) * 4;
                cmdBeginGpuTimestampQuery(cmd, gGpuProfilerTokens[i], "Draw skybox");
                cmdBindPipeline(cmd, pSkyBoxDrawPipeline[i]);
                cmdBindDescriptorSet(cmd, mSettings.mFrameIdx * 2 + 0, pDescriptorSetUniforms[i]);
                cmdBindVertexBuffer(cmd, 1, &pSkyBoxVertexBuffer[i], &skyboxStride, NULL);
                cmdDraw(cmd, 36, 0);
                cmdEndGpuTimestampQuery(cmd, gGpuProfilerTokens[i]);
            }

            // Planets pass
            {
                const uint32_t sphereStride = sizeof(float) * 6;
                cmdBeginGpuTimestampQuery(cmd, gGpuProfilerTokens[i], "Draw Planets");
                cmdBindPipeline(cmd, pSpherePipeline[i]);
                cmdBindDescriptorSet(cmd, mSettings.mFrameIdx * 2 + 1, pDescriptorSetUniforms[i]);
                cmdBindVertexBuffer(cmd, 1, &pSphereVertexBuffer[i], &sphereStride, NULL);
                cmdDrawInstanced(cmd, gNumberOfSpherePoints / 6, 0, gNumPlanets, 0);
                cmdEndGpuTimestampQuery(cmd, gGpuProfilerTokens[i]);
            }

            cmdBindRenderTargets(cmd, NULL);

            if (i == 0 || !gMultiGPUCurrent)
            {
                TFRenderTargetBarrier barrier = { pRenderTarget, TF_RESOURCE_STATE_RENDER_TARGET, TF_RESOURCE_STATE_SHADER_RESOURCE };
                cmdResourceBarrier(cmd, 0, NULL, 0, NULL, 1, &barrier);
            }
            else
            {
                TFRenderTargetBarrier barrier = { pRenderTarget, TF_RESOURCE_STATE_RENDER_TARGET, TF_RESOURCE_STATE_COPY_SOURCE };
                cmdResourceBarrier(cmd, 0, NULL, 0, NULL, 1, &barrier);

                // Barrier - Release from Graphics queue to be used on Transfer queue as CopySource
                barrier = { pRenderTarget, TF_RESOURCE_STATE_COPY_SOURCE, TF_RESOURCE_STATE_COPY_SOURCE };
                barrier.mRelease = true;
                barrier.mQueueType = TF_QUEUE_TYPE_TRANSFER;
                cmdResourceBarrier(cmd, 0, NULL, 0, NULL, 1, &barrier);
            }

            cmdEndGpuFrameProfile(cmd, gGpuProfilerTokens[i]);
            endCmd(cmd);

            // Submit graphics workload
            if (i == 0)
            {
                TFQueueSubmitDesc submitDesc = {};
                submitDesc.mCmdCount = 1;
                submitDesc.ppCmds = &cmd;
                queueSubmit(pGraphicsQueue[i], &submitDesc);
            }
            else
            {
                TFQueueSubmitDesc submitDesc = {};
                submitDesc.mCmdCount = 1;
                submitDesc.ppCmds = &cmd;
                submitDesc.pSignalFence = pRenderCompleteFence;
                submitDesc.mSignalSemaphoreCount = 1;
                submitDesc.ppSignalSemaphores = &pRenderCompleteSemaphore;
                queueSubmit(pGraphicsQueue[i], &submitDesc);
            }
        }

        // Copy outputs from secondary GPUs to CPU
        for (int i = 1; i < gViewCount && gMultiGPUCurrent; ++i)
        {
            const uint32_t  nextFrameIndex = (mSettings.mFrameIdx + gFrameLatency) % gDataBufferCount;
            TFRenderTarget* pRenderTarget = pRenderTargets[nextFrameIndex][i];
            TFSemaphore*    pRenderCompleteSemaphore = gGraphicsCmdRing[i].pSemaphores[mSettings.mFrameIdx][0];

            TFTextureCopyDesc copyDesc = {};
            copyDesc.pTexture = pRenderTarget->pTexture;
            copyDesc.pBuffer = pTransferBuffer[nextFrameIndex][i];
            copyDesc.pWaitSemaphore = pRenderCompleteSemaphore;
            copyDesc.mTextureState = TF_RESOURCE_STATE_COPY_SOURCE;
            // Barrier - Info to copy engine that the resource was last acquired by Graphics queue
            // Copy engine will use this as the queue type for acquire and release barrier on Transfer queue
            copyDesc.mQueueType = TF_QUEUE_TYPE_GRAPHICS;
            copyResource(&copyDesc, &gReadbackSyncTokens[nextFrameIndex][i - 1]);
        }

        // Upload results from secondary GPUs to primary GPU
        if (gMultiGPUCurrent)
        {
            if (gBufferedFrames == gFrameLatency)
            {
                for (uint32_t i = 1; i < gViewCount; ++i)
                {
                    uint64_t tick = cpuProfileEnter(gReadBackCpuToken);
                    waitForToken(&gReadbackSyncTokens[mSettings.mFrameIdx][i - 1]);
                    cpuProfileLeave(gReadBackCpuToken, tick);

                    TFTextureUpdateDesc updateDesc = { pRenderResult[mSettings.mFrameIdx][i - 1] };
                    updateDesc.mCurrentState = TF_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
                    TFBuffer* stagingBuffer = pTransferBuffer[mSettings.mFrameIdx][i];
                    beginUpdateResource(&updateDesc);
                    TFTextureSubresourceUpdate subresource = updateDesc.getSubresourceUpdateDesc(0, 0);
                    memcpy(subresource.pMappedData, stagingBuffer->pCpuMappedAddress, stagingBuffer->mSize);
                    endUpdateResource(&updateDesc);
                }
            }
            else
            {
                for (uint32_t i = 1; i < gViewCount; ++i)
                {
                    // Clear the result texture, to have consistent behavior between the first frames and the normal frames
                    TFTextureUpdateDesc updateDesc = { pRenderResult[mSettings.mFrameIdx][i - 1] };
                    updateDesc.mCurrentState = TF_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
                    TFBuffer* stagingBuffer = pTransferBuffer[mSettings.mFrameIdx][i];
                    beginUpdateResource(&updateDesc);
                    TFTextureSubresourceUpdate subresource = updateDesc.getSubresourceUpdateDesc(0, 0);
                    memset(subresource.pMappedData, 0, stagingBuffer->mSize);
                    endUpdateResource(&updateDesc);
                }
                ++gBufferedFrames;
            }
        }

        /************************************************************************/
        // Merge results from previous frame and present
        /************************************************************************/
        {
            uint32_t swapchainImageIndex;
            acquireNextImage(pRenderer[0], pSwapChain, pImageAcquiredSemaphore, NULL, &swapchainImageIndex);

            TFCmd*          cmd = gGraphicsCmdRing[0].pCmds[mSettings.mFrameIdx][1];
            TFRenderTarget* pRenderTarget = pSwapChain->ppRenderTargets[swapchainImageIndex];

            beginCmd(cmd);
            cmdBeginGpuFrameProfile(cmd, gGpuProfilerTokens[gViewCount]);
            // Draw Results pass
            {
                cmdBeginGpuTimestampQuery(cmd, gGpuProfilerTokens[gViewCount], "Draw Results");

                // Resource Transition
                TFRenderTargetBarrier barriers[] = { { pRenderTarget, TF_RESOURCE_STATE_PRESENT, TF_RESOURCE_STATE_RENDER_TARGET } };
                cmdResourceBarrier(cmd, 0, NULL, 0, NULL, 1, barriers);

                // Bind Render Targets
                TFBindRenderTargetsDesc bindRenderTargets = {};
                bindRenderTargets.mRenderTargetCount = 1;
                bindRenderTargets.mRenderTargets[0] = { pRenderTarget, TF_LOAD_ACTION_CLEAR };
                cmdBindRenderTargets(cmd, &bindRenderTargets);

                // Copy results pass
                {
                    cmdBeginGpuTimestampQuery(cmd, gGpuProfilerTokens[gViewCount], "Copy Results");

                    const uint32_t quadStride = sizeof(float) * 6;
                    cmdBindPipeline(cmd, pCopyQuadPipeline);

                    cmdBindDescriptorSet(cmd, mSettings.mFrameIdx * 2 + 0, pDescriptorSetCopyQuad);
                    cmdSetViewport(cmd, 0.0f, 0.0f, (float)mSettings.mWidth * 0.5f, (float)mSettings.mHeight, 0.0f, 1.0f);
                    cmdSetScissor(cmd, 0, 0, mSettings.mWidth, mSettings.mHeight);
                    cmdBindVertexBuffer(cmd, 1, &pQuadVertexBuffer, &quadStride, NULL);
                    cmdDraw(cmd, 6, 0);

                    cmdBindDescriptorSet(cmd, mSettings.mFrameIdx * 2 + 1, pDescriptorSetCopyQuad);
                    cmdSetViewport(cmd, (float)mSettings.mWidth * 0.5f, 0.0f, (float)mSettings.mWidth * 0.5f, (float)mSettings.mHeight,
                                   0.0f, 1.0f);
                    cmdSetScissor(cmd, 0, 0, mSettings.mWidth, mSettings.mHeight);
                    cmdBindVertexBuffer(cmd, 1, &pQuadVertexBuffer, &quadStride, NULL);
                    cmdDraw(cmd, 6, 0);

                    cmdEndGpuTimestampQuery(cmd, gGpuProfilerTokens[gViewCount]);
                }

                // Profiler and UI pass
                {
                    cmdSetViewport(cmd, 0.0f, 0.0f, (float)mSettings.mWidth, (float)mSettings.mHeight, 0.0f, 1.0f);

                    const float txtIndentY = 15.f;
                    float       txtOrigY = txtIndentY;
                    float2      screenCoords = float2(8.0f, txtOrigY);

                    gFrameTimeDraw.mFontColor = 0xff00ffff;
                    gFrameTimeDraw.mFontSize = 18.0f;
                    gFrameTimeDraw.pFont = gFont;
                    float2 txtSize = cmdDrawCpuProfile(cmd, screenCoords, &gFrameTimeDraw);

                    txtOrigY += txtSize.y + 4 * txtIndentY;
                    for (uint32_t j = 0; j < gViewCount; ++j)
                    {
                        screenCoords = float2(8.f, txtOrigY);
                        txtSize = cmdDrawGpuProfile(cmd, screenCoords, gGpuProfilerTokens[j], &gFrameTimeDraw);
                        txtOrigY += txtSize.y + txtIndentY;
                    }
                    screenCoords = float2(8.f, txtOrigY);
                    txtSize = cmdDrawGpuProfile(cmd, screenCoords, gGpuProfilerTokens[gViewCount], &gFrameTimeDraw);

                    uiCmdDrawUserInterface(cmd, pSwapChain, pRenderTarget, gGpuProfilerTokens[gViewCount]);
                }

                cmdBindRenderTargets(cmd, NULL);

                // Resource Transition
                barriers[0] = { pRenderTarget, TF_RESOURCE_STATE_RENDER_TARGET, TF_RESOURCE_STATE_PRESENT };
                cmdResourceBarrier(cmd, 0, NULL, 0, NULL, 1, barriers);
                cmdEndGpuTimestampQuery(cmd, gGpuProfilerTokens[gViewCount]);
            }
            cmdEndGpuFrameProfile(cmd, gGpuProfilerTokens[gViewCount]);
            endCmd(cmd);

            // Avoid submitting graphics work before the resource loader submits the copy.
            FlushResourceUpdateDesc flushUpdateDesc[gViewCount] = {};
            for (uint32_t i = 0; i < gViewCount; ++i)
            {
                if (i && !gMultiGPUCurrent)
                {
                    flushUpdateDesc[i] = flushUpdateDesc[0];
                    continue;
                }
                flushUpdateDesc[i].mNodeIndex = i;
                flushResourceUpdates(&flushUpdateDesc[i]);
            }

            const uint32_t previousFrameIndex = (mSettings.mFrameIdx + gDataBufferCount - gFrameLatency) % gDataBufferCount;
            TFSemaphore*   uploadCompleteSemaphore =
                gMultiGPUCurrent ? getLastSemaphoreSubmitted(0) : gGraphicsCmdRing[1].pSemaphores[previousFrameIndex][0];
            TFSemaphore* pRenderCompleteSemaphore = gGraphicsCmdRing[0].pSemaphores[mSettings.mFrameIdx][0];
            TFSemaphore* ppWaitSemaphores[] = { flushUpdateDesc[0].pOutSubmittedSemaphore, pImageAcquiredSemaphore,
                                                uploadCompleteSemaphore };
            TFFence*     pRenderCompleteFence = gGraphicsCmdRing[0].pFences[mSettings.mFrameIdx][0];

            TFQueueSubmitDesc submitDesc = {};
            submitDesc.mCmdCount = 1;
            submitDesc.ppCmds = &cmd;
            submitDesc.pSignalFence = pRenderCompleteFence;
            submitDesc.ppWaitSemaphores = ppWaitSemaphores;
            submitDesc.mWaitSemaphoreCount = uploadCompleteSemaphore ? 3 : 2;
            submitDesc.mSignalSemaphoreCount = 1;
            submitDesc.ppSignalSemaphores = &pRenderCompleteSemaphore;
            queueSubmit(pGraphicsQueue[0], &submitDesc);

            TFQueuePresentDesc presentDesc = {};
            presentDesc.mIndex = (uint8_t)swapchainImageIndex;
            presentDesc.mWaitSemaphoreCount = 1;
            presentDesc.ppWaitSemaphores = &pRenderCompleteSemaphore;
            presentDesc.pSwapChain = pSwapChain;
            presentDesc.mSubmitDone = true;
            queuePresent(pGraphicsQueue[0], &presentDesc);
        }

        // Stall if CPU is running "gDataBufferCount" frames ahead of GPU
        for (uint32_t i = 0; i < gViewCount; ++i)
        {
            TFFence*      pNextFence = gGraphicsCmdRing[i].pFences[(mSettings.mFrameIdx + 1) % gDataBufferCount][0];
            TFFenceStatus fenceStatus;
            getFenceStatus(pRenderer[i], pNextFence, &fenceStatus);
            if (fenceStatus == TF_FENCE_STATUS_INCOMPLETE)
            {
                uint64_t tick = cpuProfileEnter(gGpuExecCpuToken);
                waitForFences(pRenderer[i], 1, &pNextFence);
                cpuProfileLeave(gGpuExecCpuToken, tick);
            }

            resetCmdPool(pRenderer[i], gGraphicsCmdRing[i].pCmdPools[(mSettings.mFrameIdx + 1) % gDataBufferCount]);
        }
        flipProfiler();
    }

    const char* GetName() { return "11a_UnlinkedMultiGPU"; }

private:
    void updateGui()
    {
        if (UI_WINDOW_IS_VISIBLE(uiBeginWidgetWindow(&gGuiWindowDesc)))
        {
            if (pContext)
            {
                uiLayoutAutoRows(1);
                if (UI_WIDGET_IS_CHANGED(uiCheckbox("Enable Multi Renderer", &gMultiGPU)))
                {
                    SwitchGpuMode(NULL);
                }

                uiLayoutAutoRows(2);
                const uint32_t rendererCount = gMultiGPUCurrent ? gViewCount : 1;
                for (uint32_t i = 0; i < rendererCount; ++i)
                {
                    uiLabel(gRendererLabels[i], TF_ALIGN_LEFT);
                    int selected = UI_WIDGET_GET_SELECTED(uiDropdown(gGpuNamePtrs, pContext->mGpuCount, gSelectedGpuIndices[i]));
                    if (selected != (int)gSelectedGpuIndices[i])
                    {
                        gSelectedGpuIndices[i] = selected;
                        SwitchGpuMode(NULL);
                    }
                }

#ifdef AUTOMATED_TESTING
                uiLayoutAutoRows(1);
                if (UI_WIDGET_IS_PRESSED(uiButton("Swap Renderers")))
                {
                    RequestResetGPUSwitch(NULL);
                }
#endif
            }

            uiLayoutAutoRows(2);
            uiLabel("Frame Latency", TF_ALIGN_LEFT);
            uiSliderUint(&gFrameLatency, 0, gDataBufferCount - 1, 1);
            uiLabel("Camera Horizontal FoV", TF_ALIGN_LEFT);
            uiSliderFloat(&gFoVH, 30.0f, 179.0f, 1.0f);

            uiLabel("Test Scripts", TF_ALIGN_LEFT);
            gCurrentScriptIndex = UI_WIDGET_GET_SELECTED(uiDropdown(gTestScripts, gNumScriptsToDefine, gCurrentScriptIndex));
            uiLayoutAutoRows(1);
            if (UI_WIDGET_IS_PRESSED(uiButton("Run")))
            {
                RunScript(NULL);
            }
        }

        uiEndWidgetWindow();
    }

    bool addSwapChain()
    {
        TFSwapChainDesc swapChainDesc = {};
        swapChainDesc.mWindowHandle = pWindow->handle;
        swapChainDesc.mPresentQueueCount = 1;
        swapChainDesc.ppPresentQueues = &pGraphicsQueue[0];
        swapChainDesc.mWidth = mSettings.mWidth;
        swapChainDesc.mHeight = mSettings.mHeight;
        swapChainDesc.mImageCount = getRecommendedSwapchainImageCount(pRenderer[0], &pWindow->handle);
        swapChainDesc.mColorFormat = getSupportedSwapchainFormat(pRenderer[0], &swapChainDesc, TF_COLOR_SPACE_SDR_SRGB);
        swapChainDesc.mColorSpace = TF_COLOR_SPACE_SDR_SRGB;
        swapChainDesc.mEnableVsync = mSettings.mVSyncEnabled;
        ::addSwapChain(pRenderer[0], &swapChainDesc, &pSwapChain);

        return pSwapChain != NULL;
    }

    void addDescriptorSets()
    {
        for (uint32_t i = 0; i < gViewCount; ++i)
        {
            if (!gMultiGPUCurrent && i > 0)
            {
                pDescriptorSetPersistent[i] = pDescriptorSetPersistent[0];
                pDescriptorSetUniforms[i] = pDescriptorSetUniforms[0];
            }
            else
            {
                TFDescriptorSetDesc setDesc = SRT_SET_DESC(SrtData, Persistent, 1, 0);
                addDescriptorSet(pRenderer[i], &setDesc, &pDescriptorSetPersistent[i]);
                setDesc = SRT_SET_DESC(SrtData, PerBatch, gDataBufferCount * 2, 0);
                addDescriptorSet(pRenderer[i], &setDesc, &pDescriptorSetUniforms[i]);
            }
        }
        TFDescriptorSetDesc setDesc = SRT_SET_DESC(SrtData, Persistent, gDataBufferCount * gViewCount, 0);
        addDescriptorSet(pRenderer[0], &setDesc, &pDescriptorSetCopyQuad);
    }

    void removeDescriptorSets()
    {
        const uint32_t rendererCount = gMultiGPUCurrent ? gViewCount : 1;

        for (uint32_t view = 0; view < rendererCount; ++view)
        {
            removeDescriptorSet(pRenderer[view], pDescriptorSetPersistent[view]);
            removeDescriptorSet(pRenderer[view], pDescriptorSetUniforms[view]);
        }
        removeDescriptorSet(pRenderer[0], pDescriptorSetCopyQuad);
    }

    void addShaders()
    {
        for (uint32_t i = 0; i < gViewCount; ++i)
        {
            if (!gMultiGPUCurrent && i > 0)
            {
                pSkyBoxDrawShader[i] = pSkyBoxDrawShader[0];
                pSphereShader[i] = pSphereShader[0];
            }
            else
            {
                TFShaderLoadDesc skyShader = {};
                skyShader.mVert.pFileName = "skybox.vert";
                skyShader.mFrag.pFileName = "skybox.frag";
                TFShaderLoadDesc basicShader = {};
                basicShader.mVert.pFileName = "basic.vert";
                basicShader.mFrag.pFileName = "basic.frag";

                addShader(pRenderer[i], &skyShader, &pSkyBoxDrawShader[i]);
                addShader(pRenderer[i], &basicShader, &pSphereShader[i]);
            }
        }
        TFShaderLoadDesc copyQuadShader = {};
        copyQuadShader.mVert.pFileName = "copyQuad.vert";
        copyQuadShader.mFrag.pFileName = "copyQuad.frag";
        addShader(pRenderer[0], &copyQuadShader, &pCopyQuadShader);
    }

    void removeShaders()
    {
        const uint32_t rendererCount = gMultiGPUCurrent ? gViewCount : 1;

        for (uint32_t view = 0; view < rendererCount; ++view)
        {
            removeShader(pRenderer[view], pSphereShader[view]);
            removeShader(pRenderer[view], pSkyBoxDrawShader[view]);
        }
        removeShader(pRenderer[0], pCopyQuadShader);
    }

    void addPipelines()
    {
        for (uint32_t i = 0; i < gViewCount; ++i)
        {
            if (!gMultiGPUCurrent && i > 0)
            {
                pSpherePipeline[i] = pSpherePipeline[0];
                pSkyBoxDrawPipeline[i] = pSkyBoxDrawPipeline[0];
                continue;
            }
            // Layout and pipeline for sphere draw
            TFVertexLayout vertexLayout = {};
            vertexLayout.mBindingCount = 1;
            vertexLayout.mAttribCount = 2;
            vertexLayout.mAttribs[0].mSemantic = TF_SEMANTIC_POSITION;
            vertexLayout.mAttribs[0].mFormat = TinyImageFormat_R32G32B32_SFLOAT;
            vertexLayout.mAttribs[0].mBinding = 0;
            vertexLayout.mAttribs[0].mLocation = 0;
            vertexLayout.mAttribs[0].mOffset = 0;
            vertexLayout.mAttribs[1].mSemantic = TF_SEMANTIC_NORMAL;
            vertexLayout.mAttribs[1].mFormat = TinyImageFormat_R32G32B32_SFLOAT;
            vertexLayout.mAttribs[1].mBinding = 0;
            vertexLayout.mAttribs[1].mLocation = 1;
            vertexLayout.mAttribs[1].mOffset = 3 * sizeof(float);

            TFRasterizerStateDesc rasterizerStateDesc = {};
            rasterizerStateDesc.mCullMode = TF_CULL_MODE_NONE;

            TFDepthStateDesc depthStateDesc = {};
            depthStateDesc.mDepthTest = true;
            depthStateDesc.mDepthWrite = true;
            depthStateDesc.mDepthFunc = TF_CMP_GEQUAL;

            TFPipelineDesc desc = {};
            desc.mType = TF_PIPELINE_TYPE_GRAPHICS;
            TFGraphicsPipelineDesc& pipelineSettings = desc.mGraphicsDesc;
            pipelineSettings.mPrimitiveTopo = TF_PRIMITIVE_TOPO_TRI_LIST;
            pipelineSettings.mRenderTargetCount = 1;
            pipelineSettings.pDepthState = &depthStateDesc;
            pipelineSettings.pColorFormats = &pSwapChain->ppRenderTargets[0]->mFormat;
            pipelineSettings.mSampleCount = pSwapChain->ppRenderTargets[0]->mSampleCount;
            pipelineSettings.mSampleQuality = pSwapChain->ppRenderTargets[0]->mSampleQuality;
            pipelineSettings.mDepthStencilFormat = pDepthBuffers[0]->mFormat;
            pipelineSettings.pVertexLayout = &vertexLayout;
            pipelineSettings.pRasterizerState = &rasterizerStateDesc;

            pipelineSettings.pShaderProgram = pSphereShader[i];
            addPipeline(pRenderer[i], &desc, &pSpherePipeline[i]);

            // Layout and pipeline for skybox draw
            vertexLayout = {};
            vertexLayout.mBindingCount = 1;
            vertexLayout.mAttribCount = 1;
            vertexLayout.mAttribs[0].mSemantic = TF_SEMANTIC_POSITION;
            vertexLayout.mAttribs[0].mFormat = TinyImageFormat_R32G32B32A32_SFLOAT;
            vertexLayout.mAttribs[0].mBinding = 0;
            vertexLayout.mAttribs[0].mLocation = 0;
            vertexLayout.mAttribs[0].mOffset = 0;

            pipelineSettings.pDepthState = NULL;
            pipelineSettings.pRasterizerState = &rasterizerStateDesc;
            pipelineSettings.pShaderProgram = pSkyBoxDrawShader[i];
            addPipeline(pRenderer[i], &desc, &pSkyBoxDrawPipeline[i]);
        }

        TFVertexLayout quadVertexLayout = {};
        quadVertexLayout.mBindingCount = 1;
        quadVertexLayout.mAttribCount = 2;
        quadVertexLayout.mAttribs[0].mSemantic = TF_SEMANTIC_POSITION;
        quadVertexLayout.mAttribs[0].mFormat = TinyImageFormat_R32G32B32A32_SFLOAT;
        quadVertexLayout.mAttribs[0].mBinding = 0;
        quadVertexLayout.mAttribs[0].mLocation = 0;
        quadVertexLayout.mAttribs[0].mOffset = 0;
        quadVertexLayout.mAttribs[1].mSemantic = TF_SEMANTIC_TEXCOORD0;
        quadVertexLayout.mAttribs[1].mFormat = TinyImageFormat_R32G32_SFLOAT;
        quadVertexLayout.mAttribs[1].mBinding = 0;
        quadVertexLayout.mAttribs[1].mLocation = 1;
        quadVertexLayout.mAttribs[1].mOffset = 4 * sizeof(float);

        TFPipelineDesc        desc = {};
        TFDepthStateDesc      depthStateDesc = {};
        TFRasterizerStateDesc rasterizerStateDesc = {};
        rasterizerStateDesc.mCullMode = TF_CULL_MODE_NONE;

        desc.mType = TF_PIPELINE_TYPE_GRAPHICS;
        TFGraphicsPipelineDesc& pipelineSettings = desc.mGraphicsDesc;
        pipelineSettings.mPrimitiveTopo = TF_PRIMITIVE_TOPO_TRI_LIST;
        pipelineSettings.mRenderTargetCount = 1;
        pipelineSettings.pDepthState = &depthStateDesc;
        pipelineSettings.pColorFormats = &pSwapChain->ppRenderTargets[0]->mFormat;
        pipelineSettings.mSampleCount = pSwapChain->ppRenderTargets[0]->mSampleCount;
        pipelineSettings.mSampleQuality = pSwapChain->ppRenderTargets[0]->mSampleQuality;
        pipelineSettings.mDepthStencilFormat = pDepthBuffers[0]->mFormat;
        pipelineSettings.pVertexLayout = &quadVertexLayout;
        pipelineSettings.pRasterizerState = &rasterizerStateDesc;

        desc.mGraphicsDesc.mDepthStencilFormat = TinyImageFormat_UNDEFINED;
        desc.mGraphicsDesc.pDepthState = &depthStateDesc;
        desc.mGraphicsDesc.pVertexLayout = &quadVertexLayout;
        desc.mGraphicsDesc.pShaderProgram = pCopyQuadShader;
        addPipeline(pRenderer[0], &desc, &pCopyQuadPipeline);
    }

    void removePipelines()
    {
        const uint32_t rendererCount = gMultiGPUCurrent ? gViewCount : 1;
        for (uint32_t i = 0; i < rendererCount; ++i)
        {
            removePipeline(pRenderer[i], pSkyBoxDrawPipeline[i]);
            removePipeline(pRenderer[i], pSpherePipeline[i]);
        }
        removePipeline(pRenderer[0], pCopyQuadPipeline);
    }

    void updateDescriptorSets()
    {
        for (uint32_t i = 0; i < gViewCount; ++i)
        {
            if (i > 0 && pDescriptorSetPersistent[i] == pDescriptorSetPersistent[0])
                continue;

            TFDescriptorData params[7] = {};
            params[0].mIndex = SRT_RES_IDX(SrtData, Persistent, gRightText);
            params[0].ppTextures = &pSkyBoxTextures[i][0];
            params[1].mIndex = SRT_RES_IDX(SrtData, Persistent, gLeftText);
            params[1].ppTextures = &pSkyBoxTextures[i][1];
            params[2].mIndex = SRT_RES_IDX(SrtData, Persistent, gTopText);
            params[2].ppTextures = &pSkyBoxTextures[i][2];
            params[3].mIndex = SRT_RES_IDX(SrtData, Persistent, gBotText);
            params[3].ppTextures = &pSkyBoxTextures[i][3];
            params[4].mIndex = SRT_RES_IDX(SrtData, Persistent, gFrontText);
            params[4].ppTextures = &pSkyBoxTextures[i][4];
            params[5].mIndex = SRT_RES_IDX(SrtData, Persistent, gBackText);
            params[5].ppTextures = &pSkyBoxTextures[i][5];
            params[6].mIndex = SRT_RES_IDX(SrtData, Persistent, gSamplerSkybox);
            params[6].ppSamplers = &pSamplerSkyBox[i];
            updateDescriptorSet(pRenderer[i], 0, pDescriptorSetPersistent[i], 7, params);

            for (uint32_t f = 0; f < gDataBufferCount; ++f)
            {
                TFDescriptorData uParams[1] = {};
                uParams[0].mIndex = SRT_RES_IDX(SrtData, PerBatch, gUniformBlock);
                uParams[0].ppBuffers = &pProjViewUniformBuffer[f][i];
                updateDescriptorSet(pRenderer[i], f * 2 + 0, pDescriptorSetUniforms[i], 1, uParams);
                updateDescriptorSet(pRenderer[i], f * 2 + 1, pDescriptorSetUniforms[i], 1, uParams);
            }
        }

        for (uint32_t f = 0; f < gDataBufferCount; ++f)
        {
            for (uint32_t view = 0; view < gViewCount; ++view)
            {
                TFDescriptorData copyQuadParams[1] = {};
                copyQuadParams[0].mIndex = SRT_RES_IDX(SrtData, Persistent, gTex);
                copyQuadParams[0].ppTextures =
                    (view == 0 || !gMultiGPUCurrent) ? &pRenderTargets[f][view]->pTexture : &pRenderResult[f][view - 1];
                updateDescriptorSet(pRenderer[0], f * gViewCount + view, pDescriptorSetCopyQuad, 1, copyQuadParams);
            }
        }
    }

    bool addRenderTargets()
    {
        // Add color buffer
        TFRenderTargetDesc colorRT = {};
        colorRT.mArraySize = 1;
        colorRT.mClearValue = gClearColor;
        colorRT.mDepth = 1;
        colorRT.mFormat = pSwapChain->mFormat;
        colorRT.mStartState = TF_RESOURCE_STATE_SHADER_RESOURCE;
        colorRT.mHeight = mSettings.mHeight;
        colorRT.mSampleCount = TF_SAMPLE_COUNT_1;
        colorRT.mSampleQuality = 0;
        colorRT.mWidth = mSettings.mWidth / 2;
        colorRT.pName = "Color RT";

        // Add depth buffer
        TFRenderTargetDesc depthRT = {};
        depthRT.mArraySize = 1;
        depthRT.mClearValue = gClearDepth;
        depthRT.mDepth = 1;
        depthRT.mFormat = TinyImageFormat_D32_SFLOAT;
        depthRT.mStartState = TF_RESOURCE_STATE_DEPTH_WRITE;
        depthRT.mHeight = mSettings.mHeight;
        depthRT.mSampleCount = TF_SAMPLE_COUNT_1;
        depthRT.mSampleQuality = 0;
        depthRT.mWidth = mSettings.mWidth / 2;

        TFTextureDesc colorResult = {};
        colorResult.mArraySize = colorRT.mArraySize;
        colorResult.mMipLevels = 1;
        colorResult.mClearValue = colorRT.mClearValue;
        colorResult.mDepth = colorRT.mDepth;
        colorResult.mFormat = colorRT.mFormat;
        colorResult.mStartState = TF_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        colorResult.mHeight = colorRT.mHeight;
        colorResult.mSampleCount = colorRT.mSampleCount;
        colorResult.mSampleQuality = colorRT.mSampleQuality;
        colorResult.mWidth = colorRT.mWidth;
        colorResult.mDescriptors = TF_DESCRIPTOR_TYPE_TEXTURE;
        colorResult.pName = "Color result";

        for (uint32_t i = 0; i < gViewCount; ++i)
        {
            if (gMultiGPUCurrent)
            {
                colorRT.mNodeIndex = i;
                depthRT.mNodeIndex = i;
            }

            addRenderTarget(pRenderer[i], &depthRT, &pDepthBuffers[i]);

            for (uint32_t frameIdx = 0; frameIdx < gDataBufferCount; ++frameIdx)
            {
                if (gMultiGPUCurrent && i > 0)
                {
                    colorRT.mStartState = TF_RESOURCE_STATE_COPY_SOURCE;
                    addRenderTarget(pRenderer[i], &colorRT, &pRenderTargets[frameIdx][i]);

                    TFTextureLoadDesc colorTexture = { &pRenderResult[frameIdx][i - 1], &colorResult };
                    addResource(&colorTexture, NULL);

                    const uint32_t rowAlignment = max(1u, pRenderer[i]->pGpu->mUploadBufferTextureRowAlignment);
                    const uint32_t blockSize = max(1u, TinyImageFormat_BitSizeOfBlock(colorResult.mFormat));
                    const uint32_t sliceAlignment =
                        round_up(round_up(pRenderer[i]->pGpu->mUploadBufferTextureAlignment, blockSize), rowAlignment);
                    TFBufferLoadDesc bufferDesc = {};
                    bufferDesc.mDesc.mSize = util_get_surface_size(colorResult.mFormat, colorResult.mWidth, colorResult.mHeight, 1,
                                                                   rowAlignment, sliceAlignment, 0, 1, 0, 1);
                    bufferDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_GPU_TO_CPU;
                    bufferDesc.mDesc.mFlags = TF_BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
                    bufferDesc.mDesc.mQueueType = TF_QUEUE_TYPE_TRANSFER;
                    bufferDesc.mDesc.mNodeIndex = i;
                    bufferDesc.ppBuffer = &pTransferBuffer[frameIdx][i];

                    addResource(&bufferDesc, NULL);
                }
                else
                {
                    colorRT.mStartState = TF_RESOURCE_STATE_SHADER_RESOURCE;
                    addRenderTarget(pRenderer[i], &colorRT, &pRenderTargets[frameIdx][i]);
                }
            }
        }

        return pDepthBuffers[0] != NULL;
    }

    void removeRenderTargets()
    {
        for (uint32_t frameIdx = 0; frameIdx < gDataBufferCount; ++frameIdx)
        {
            for (uint32_t view = 0; view < gViewCount; ++view)
            {
                removeRenderTarget(pRenderer[view], pRenderTargets[frameIdx][view]);

                if (gMultiGPUCurrent && view > 0)
                {
                    removeResource(pRenderResult[frameIdx][view - 1]);
                    removeResource(pTransferBuffer[frameIdx][view]);
                }
            }
        }

        for (uint32_t view = 0; view < gViewCount; ++view)
        {
            removeRenderTarget(pRenderer[view], pDepthBuffers[view]);
        }
    }

    void resetFrameLatency()
    {
        const uint32_t rendererCount = gMultiGPUCurrent ? gViewCount : 1;
        for (uint32_t i = 0; i < rendererCount; ++i)
            waitQueueIdle(pGraphicsQueue[i]);

        waitForAllResourceLoads();

        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            for (uint32_t j = 0; j < gViewCount; ++j)
            {
                resetCmdPool(pRenderer[j], gGraphicsCmdRing[j].pCmdPools[i]);

                // Recycle fence and semaphores to reset signaled state.
                exitFence(pRenderer[j], gGraphicsCmdRing[j].pFences[i][0]);
                exitSemaphore(pRenderer[j], gGraphicsCmdRing[j].pSemaphores[i][0]);

                if (j > 0)
                    gReadbackSyncTokens[i][j - 1] = {};

                initFence(pRenderer[j], &gGraphicsCmdRing[j].pFences[i][0]);
                initSemaphore(pRenderer[j], &gGraphicsCmdRing[j].pSemaphores[i][0]);
            }
        }
        gBufferedFrames = 0;
    }
};

DEFINE_APPLICATION_MAIN(UnlinkedMultiGPU)
