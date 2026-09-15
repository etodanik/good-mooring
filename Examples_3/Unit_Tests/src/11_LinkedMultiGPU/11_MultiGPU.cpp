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

// Unit Test for distributing heavy gpu workload such as Split Frame Rendering to Multiple Identical GPUs
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

// #NOTE: Two sets of resources (one in flight and one being used on CPU)
const uint32_t gDataBufferCount = 2;

const uint32_t gViewCount = 2;
// Simulate heavy gpu workload by rendering high resolution spheres
const int      gSphereResolution = 1024; // Increase for higher resolution spheres
const float    gSphereDiameter = 0.5f;
const uint     gNumPlanets = 11;     // Sun, Mercury -> Neptune, Pluto, Moon
const uint     gTimeOffset = 600000; // For visually better starting locations
const float    gRotSelfScale = 0.0004f;
const float    gRotOrbitYScale = 0.001f;
const float    gRotOrbitZScale = 0.00001f;

TFRenderer* pRenderer = NULL;

TFQueue*        pGraphicsQueue[gViewCount] = { NULL };
GpuCmdRing      gGraphicsCmdRing[gViewCount] = {};
TFBuffer*       pSphereVertexBuffer[gViewCount] = { NULL };
TFBuffer*       pSkyBoxVertexBuffer[gViewCount] = { NULL };
TFTexture*      pSkyBoxTextures[gViewCount][6];
TFRenderTarget* pRenderTargets[gDataBufferCount][gViewCount] = { { NULL } };
TFRenderTarget* pDepthBuffers[gViewCount] = { NULL };

TFSemaphore* pImageAcquiredSemaphore = NULL;
TFSwapChain* pSwapChain = NULL;

TFShader*   pSphereShader = NULL;
TFPipeline* pSpherePipeline = NULL;

TFShader*        pSkyBoxDrawShader = NULL;
TFPipeline*      pSkyBoxDrawPipeline = NULL;
TFShader*        pCopyQuadShader = NULL;
TFPipeline*      pCopyQuadPipeline = NULL;
TFSampler*       pSamplerSkyBox = NULL;
TFDescriptorSet* pDescriptorSetPersistent[gViewCount] = { NULL };
TFDescriptorSet* pDescriptorSetPerFrame[gViewCount] = { NULL };
TFDescriptorSet* pDescriptorSetCopyQuad = { NULL };

TFBuffer* pProjViewUniformBuffer[gDataBufferCount] = { NULL };
TFBuffer* pQuadVertexBuffer = NULL;

int              gNumberOfSpherePoints;
UniformBlock     gUniformData;
PlanetInfoStruct gPlanetInfoData[gNumPlanets];

TFICamera* pCamera = NULL;

/// UI
TFUIWindowDesc gGuiWindowDesc;

const char* pSkyBoxImageFileNames[] = {
    "Skybox_right1.tex", "Skybox_left2.tex", "Skybox_top3.tex", "Skybox_bottom4.tex", "Skybox_front5.tex", "Skybox_back6.tex",
};
const char*  pGpuProfilerNames[gViewCount] = { NULL };
char         gGpuProfilerNames[gViewCount][MAX_GPU_PROFILER_NAME_LENGTH]{};
ProfileToken gGpuProfilerTokens[gViewCount];

TFFontDrawDesc gFrameTimeDraw;
TFFont*        gFont;

TFClearValue gClearColor; // initialization in Init
TFClearValue gClearDepth;
bool         gMultiGPU = true;
bool         gMultiGpuUi = true;
bool         gMultiGPURestart = false;
bool         gMultiGpuAvailable = false;
float*       pSpherePoints;

const char* gTestScripts[] = { "Test0.lua" };
uint32_t    gCurrentScriptIndex = 0;

float gFoVH = 90.0f;

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

class MultiGPU: public IApp
{
public:
    bool Init()
    {
        gMultiGPU = gMultiGpuUi;

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

        TFRendererDesc settings;
        memset(&settings, 0, sizeof(settings));
        settings.mGpuMode = gMultiGPU ? TF_GPU_MODE_LINKED : TF_GPU_MODE_SINGLE;
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

        initResourceLoaderInterface(pRenderer);

        TFRootSignatureDesc rootDesc = {};
        INIT_RS_DESC(rootDesc, "default.rootsig", "compute.rootsig");
        initRootSignature(pRenderer, &rootDesc);

        if (!gMultiGPURestart)
        {
            gMultiGpuAvailable = gMultiGpuAvailable || pRenderer->mGpuMode == TF_GPU_MODE_LINKED;
        }

        if (pRenderer->mGpuMode == TF_GPU_MODE_SINGLE && gMultiGPU)
        {
            LOGF(LogLevel::eWARNING, "Multi GPU will be disabled since the system only has one GPU");
            gMultiGPU = false;
        }
        for (uint32_t i = 0; i < gViewCount; ++i)
        {
            TFQueueDesc queueDesc = {};
            queueDesc.mType = TF_QUEUE_TYPE_GRAPHICS;
            queueDesc.mFlag = TF_QUEUE_FLAG_INIT_MICROPROFILE;
            queueDesc.mNodeIndex = i;

            if (!gMultiGPU && i > 0)
                pGraphicsQueue[i] = pGraphicsQueue[0];
            else
                initQueue(pRenderer, &queueDesc, &pGraphicsQueue[i]);

            snprintf(gGpuProfilerNames[i], MAX_GPU_PROFILER_NAME_LENGTH, "Graphics");

            char index[10];
            snprintf(index, 10, "%u", i);
            strcat(gGpuProfilerNames[i], index);

            pGpuProfilerNames[i] = gGpuProfilerNames[i];
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

        const uint32_t  numScripts = sizeof(gTestScripts) / sizeof(gTestScripts[0]);
        TFLuaScriptDesc scriptDescs[numScripts] = {};
        for (uint32_t i = 0; i < numScripts; ++i)
            scriptDescs[i].pScriptFileName = gTestScripts[i];
        luaDefineScripts(scriptDescs, numScripts);

        // Initialize micro profiler and its UI.
        TFProfilerDesc profiler = {};
        profiler.pRenderer = pRenderer;
        profiler.ppQueues = pGraphicsQueue;
        profiler.ppProfilerNames = pGpuProfilerNames;
        profiler.pProfileTokens = gGpuProfilerTokens;
        profiler.mGpuProfilerCount = gViewCount;
        initProfiler(&profiler);

        for (uint32_t j = 0; j < gViewCount; ++j)
        {
            GpuCmdRingDesc cmdRingDesc = {};
            cmdRingDesc.pQueue = pGraphicsQueue[j];
            cmdRingDesc.mPoolCount = gDataBufferCount;
            cmdRingDesc.mCmdPerPoolCount = 1;
            cmdRingDesc.mAddSyncPrimitives = true;
            initGpuCmdRing(pRenderer, &cmdRingDesc, &gGraphicsCmdRing[j]);
        }

        initSemaphore(pRenderer, &pImageAcquiredSemaphore);
        TFSamplerDesc samplerDesc = { TF_FILTER_LINEAR,
                                      TF_FILTER_LINEAR,
                                      TF_MIPMAP_MODE_NEAREST,
                                      TF_ADDRESS_MODE_CLAMP_TO_EDGE,
                                      TF_ADDRESS_MODE_CLAMP_TO_EDGE,
                                      TF_ADDRESS_MODE_CLAMP_TO_EDGE };
        addSampler(pRenderer, &samplerDesc, &pSamplerSkyBox);

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

        for (uint32_t view = 0; view < gViewCount; ++view)
        {
            textureDesc.mNodeIndex = view;

            for (int i = 0; i < 6; ++i)
            {
                textureDesc.pFileName = pSkyBoxImageFileNames[i];
                textureDesc.ppTexture = &pSkyBoxTextures[view][i];
                // Textures representing color should be stored in SRGB or HDR format
                textureDesc.mCreationFlag = TF_TEXTURE_CREATION_FLAG_SRGB;

                if (!gMultiGPU && view > 0)
                    pSkyBoxTextures[view][i] = pSkyBoxTextures[0][i];
                else
                    addResource(&textureDesc, NULL);
            }

            sphereVbDesc.mDesc.mNodeIndex = view;
            sphereVbDesc.ppBuffer = &pSphereVertexBuffer[view];

            skyboxVbDesc.mDesc.mNodeIndex = view;
            skyboxVbDesc.ppBuffer = &pSkyBoxVertexBuffer[view];

            if (!gMultiGPU && view > 0)
            {
                pSphereVertexBuffer[view] = pSphereVertexBuffer[0];
                pSkyBoxVertexBuffer[view] = pSkyBoxVertexBuffer[0];
            }
            else
            {
                addResource(&sphereVbDesc, NULL);
                addResource(&skyboxVbDesc, NULL);
            }
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
        addResource(&quadDesc, NULL);

        TFBufferLoadDesc ubDesc = {};
        ubDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        ubDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
        ubDesc.mDesc.mSize = sizeof(UniformBlock);
        ubDesc.mDesc.mFlags = TF_BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
        ubDesc.pData = NULL;
        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            ubDesc.ppBuffer = &pProjViewUniformBuffer[i];
            addResource(&ubDesc, NULL);
        }

        TFCameraMotionParameters cmp{ 160.0f, 600.0f, 600.0f };
        vec3                     camPos{ 48.0f, 48.0f, 20.0f };
        vec3                     lookAt{ 0 };

        pCamera = initFpsCamera(camPos, lookAt);
        pCamera->setMotionParameters(cmp);

        AddCustomInputBindings();
        initScreenshotCapturer(pRenderer, pGraphicsQueue[0], GetName());
        gMultiGPURestart = false;

        waitForAllResourceLoads();

        return true;
    }

    void Exit()
    {
        exitScreenshotCapturer();

        exitCamera(pCamera);

        exitProfiler();

        exitUserInterface();

        removeFont(gFont);
        exitFontSystem();

        removeResource(pQuadVertexBuffer);

        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            removeResource(pProjViewUniformBuffer[i]);
        }
        removeSampler(pRenderer, pSamplerSkyBox);

        for (uint32_t view = 0; view < gViewCount; ++view)
        {
            if (!gMultiGPU && view > 0)
                continue;
            removeResource(pSphereVertexBuffer[view]);
            removeResource(pSkyBoxVertexBuffer[view]);

            for (uint i = 0; i < 6; ++i)
                removeResource(pSkyBoxTextures[view][i]);
        }

        for (uint32_t j = 0; j < gViewCount; ++j)
        {
            exitGpuCmdRing(pRenderer, &gGraphicsCmdRing[j]);
        }

        for (uint32_t view = 0; view < gViewCount; ++view)
        {
            if (!gMultiGPU && view > 0)
                break;
            exitQueue(pRenderer, pGraphicsQueue[view]);
        }

        exitSemaphore(pRenderer, pImageAcquiredSemaphore);
        exitRootSignature(pRenderer);
        exitResourceLoaderInterface(pRenderer);

        exitRenderer(pRenderer);
        exitGPUConfig();
        pRenderer = NULL;

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

        mSettings.mFrameIdx = 0;

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

            if (gMultiGpuAvailable)
            {
                luaVarDesc.mWidgetType = TF_WIDGET_TYPE_CHECKBOX;
                luaVarDesc.pLabel = "Enable Multi GPU";
                luaVarDesc.pBool = &gMultiGPU;
                luaRegisterWidgetVariable(&luaVarDesc);
                luaFuncDesc.mType = TF_LUA_WIDGET_FUNCTION_ON_EDITED;
                luaFuncDesc.pLabel = "Enable Multi GPU";
                luaFuncDesc.pFuncData = NULL;
                luaFuncDesc.pFunc = SwitchGpuMode;
                luaRegisterWidgetFunction(&luaFuncDesc);
            }

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

        return true;
    }

    void Unload(TFReloadDesc* pReloadDesc)
    {
        UNREF_PARAM(pReloadDesc);

        for (uint32_t i = 0; i < gViewCount; ++i)
            waitQueueIdle(pGraphicsQueue[i]);

        unloadFontSystem();
        unloadUserInterface();

        removePipelines();

        removeSwapChain(pRenderer, pSwapChain);
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
            ::toggleVSync(pRenderer, &pSwapChain);
        }

        uint32_t swapchainImageIndex;
        acquireNextImage(pRenderer, pSwapChain, pImageAcquiredSemaphore, NULL, &swapchainImageIndex);

        GpuCmdRingElement elem[gViewCount] = {};
        // Stall if CPU is running "gDataBufferCount" frames ahead of GPU
        for (uint32_t i = 0; i < gViewCount; ++i)
        {
            elem[i] = getNextGpuCmdRingElement(&gGraphicsCmdRing[i], true, 1);
            TFFenceStatus fenceStatus;
            getFenceStatus(pRenderer, elem[i].pFence, &fenceStatus);
            if (fenceStatus == TF_FENCE_STATUS_INCOMPLETE)
            {
                waitForFences(pRenderer, 1, &elem[i].pFence);
            }

            resetCmdPool(pRenderer, elem[i].pCmdPool);
        }

        // Update uniform buffers
        TFBufferUpdateDesc viewProjCbv = { pProjViewUniformBuffer[mSettings.mFrameIdx] };
        beginUpdateResource(&viewProjCbv);
        memcpy(viewProjCbv.pMappedData, &gUniformData, sizeof(gUniformData));
        endUpdateResource(&viewProjCbv);

        for (int i = gViewCount - 1; i >= 0; --i)
        {
            /************************************************************************/
            // View render pass
            /************************************************************************/
            TFRenderTarget* pRenderTarget = pRenderTargets[mSettings.mFrameIdx][i];
            TFRenderTarget* pDepthBuffer = pDepthBuffers[i];
            TFCmd*          cmd = elem[i].pCmds[0];

            beginCmd(cmd);
            cmdBindDescriptorSet(cmd, 0, pDescriptorSetPersistent[i]);
            cmdBindDescriptorSet(cmd, mSettings.mFrameIdx, pDescriptorSetPerFrame[i]);
            cmdBeginGpuFrameProfile(cmd, gGpuProfilerTokens[i]);

            // Resource Transition
            TFRenderTargetBarrier barriers[] = {
                { pRenderTarget, TF_RESOURCE_STATE_SHADER_RESOURCE, TF_RESOURCE_STATE_RENDER_TARGET },
            };
            cmdResourceBarrier(cmd, 0, NULL, 0, NULL, 1, barriers);

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
                cmdBeginGpuTimestampQuery(cmd, gGpuProfilerTokens[i], "Draw Skybox");
                cmdBindPipeline(cmd, pSkyBoxDrawPipeline);
                cmdBindVertexBuffer(cmd, 1, &pSkyBoxVertexBuffer[i], &skyboxStride, NULL);
                cmdDraw(cmd, 36, 0);
                cmdEndGpuTimestampQuery(cmd, gGpuProfilerTokens[i]);
            }

            // Draw Planets pass
            {
                const uint32_t sphereStride = sizeof(float) * 6;
                cmdBeginGpuTimestampQuery(cmd, gGpuProfilerTokens[i], "Draw Planets");
                cmdBindPipeline(cmd, pSpherePipeline);
                cmdBindVertexBuffer(cmd, 1, &pSphereVertexBuffer[i], &sphereStride, NULL);
                cmdDrawInstanced(cmd, gNumberOfSpherePoints / 6, 0, gNumPlanets, 0);
                cmdEndGpuTimestampQuery(cmd, gGpuProfilerTokens[i]);
            }

            cmdBindRenderTargets(cmd, NULL);

            TFRenderTargetBarrier srvBarriers[] = {
                { pRenderTarget, TF_RESOURCE_STATE_RENDER_TARGET, TF_RESOURCE_STATE_SHADER_RESOURCE },
            };
            cmdResourceBarrier(cmd, 0, NULL, 0, NULL, 1, srvBarriers);

            if (i == 0)
            {
                // Draw Results pass
                {
                    cmdBeginGpuTimestampQuery(cmd, gGpuProfilerTokens[i], "Draw Results");

                    pRenderTarget = pSwapChain->ppRenderTargets[swapchainImageIndex];
                    barriers[0] = { pRenderTarget, TF_RESOURCE_STATE_PRESENT, TF_RESOURCE_STATE_RENDER_TARGET };
                    cmdResourceBarrier(cmd, 0, NULL, 0, NULL, 1, barriers);

                    bindRenderTargets = {};
                    bindRenderTargets.mRenderTargetCount = 1;
                    bindRenderTargets.mRenderTargets[0] = { pRenderTarget, TF_LOAD_ACTION_CLEAR };
                    cmdBindRenderTargets(cmd, &bindRenderTargets);

                    // Copy Results pass
                    {
                        cmdBeginGpuTimestampQuery(cmd, gGpuProfilerTokens[i], "Copy Results");

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

                        cmdEndGpuTimestampQuery(cmd, gGpuProfilerTokens[i]);
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

                        uiCmdDrawUserInterface(cmd, pSwapChain, pRenderTarget, gGpuProfilerTokens[i]);
                    }
                    cmdBindRenderTargets(cmd, NULL);

                    barriers[0] = { pRenderTarget, TF_RESOURCE_STATE_RENDER_TARGET, TF_RESOURCE_STATE_PRESENT };
                    cmdResourceBarrier(cmd, 0, NULL, 0, NULL, 1, barriers);
                    cmdEndGpuTimestampQuery(cmd, gGpuProfilerTokens[i]);
                }
            }

            cmdEndGpuFrameProfile(cmd, gGpuProfilerTokens[i]);
            endCmd(cmd);

            FlushResourceUpdateDesc flushUpdateDesc = {};
            flushUpdateDesc.mNodeIndex = gMultiGpuAvailable ? i : 0;
            flushResourceUpdates(&flushUpdateDesc);
            TFSemaphore* waitSemaphores[] = { flushUpdateDesc.pOutSubmittedSemaphore, pImageAcquiredSemaphore, elem[1].pSemaphore };

            if (i == 0)
            {
                TFQueueSubmitDesc submitDesc = {};
                submitDesc.mCmdCount = 1;
                submitDesc.ppCmds = &cmd;
                submitDesc.pSignalFence = elem[i].pFence;
                submitDesc.mSignalSemaphoreCount = 1;
                submitDesc.ppSignalSemaphores = &elem[i].pSemaphore;
                submitDesc.mWaitSemaphoreCount = TF_ARRAY_COUNT(waitSemaphores);
                submitDesc.ppWaitSemaphores = waitSemaphores;
                queueSubmit(pGraphicsQueue[i], &submitDesc);
                TFQueuePresentDesc presentDesc = {};
                presentDesc.mIndex = (uint8_t)swapchainImageIndex;
                presentDesc.mWaitSemaphoreCount = 1;
                presentDesc.ppWaitSemaphores = &elem[i].pSemaphore;
                presentDesc.pSwapChain = pSwapChain;
                presentDesc.mSubmitDone = true;
                queuePresent(pGraphicsQueue[i], &presentDesc);
            }
            else
            {
                TFQueueSubmitDesc submitDesc = {};
                submitDesc.mCmdCount = 1;
                submitDesc.ppCmds = &cmd;
                submitDesc.pSignalFence = elem[i].pFence;
                submitDesc.mSignalSemaphoreCount = 1;
                submitDesc.ppSignalSemaphores = &elem[i].pSemaphore;
                submitDesc.mWaitSemaphoreCount = 1;
                submitDesc.ppWaitSemaphores = waitSemaphores;
                queueSubmit(pGraphicsQueue[i], &submitDesc);
            }
        }

        flipProfiler();
    }

    const char* GetName() { return "11_LinkedMultiGPU"; }

private:
    void updateGui()
    {
        if (UI_WINDOW_IS_VISIBLE(uiBeginWidgetWindow(&gGuiWindowDesc)))
        {
            // Reset graphics with a button.
            // Show this checkbox only when multiple GPUs are present
            if (gMultiGpuAvailable)
            {
                uiLayoutAutoRows(1);
                if (UI_WIDGET_IS_CHANGED(uiCheckbox("Enable Multi GPU", &gMultiGpuUi)))
                {
                    SwitchGpuMode(NULL);
                }
            }

            uiLayoutAutoRows(2);
            uiLabel("Camera Horizontal FoV", TF_ALIGN_LEFT);
            uiSliderFloat(&gFoVH, 30.0f, 179.0f, 1.0f);

            int testScriptsCount = sizeof(gTestScripts) / sizeof(gTestScripts[0]);
            uiLabel("Test Scripts", TF_ALIGN_LEFT);
            gCurrentScriptIndex = UI_WIDGET_GET_SELECTED(uiDropdown(gTestScripts, testScriptsCount, gCurrentScriptIndex));
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
        swapChainDesc.mImageCount = getRecommendedSwapchainImageCount(pRenderer, &pWindow->handle);
        swapChainDesc.mColorFormat = getSupportedSwapchainFormat(pRenderer, &swapChainDesc, TF_COLOR_SPACE_SDR_SRGB);
        swapChainDesc.mColorSpace = TF_COLOR_SPACE_SDR_SRGB;
        swapChainDesc.mEnableVsync = mSettings.mVSyncEnabled;
        ::addSwapChain(pRenderer, &swapChainDesc, &pSwapChain);

        return pSwapChain != NULL;
    }

    void addDescriptorSets()
    {
        for (uint32_t i = 0; i < gViewCount; i++)
        {
            if (!gMultiGPU && i > 0)
            {
                pDescriptorSetPersistent[i] = pDescriptorSetPersistent[0];
                pDescriptorSetPerFrame[i] = pDescriptorSetPerFrame[0];
            }
            else
            {
                TFDescriptorSetDesc setDesc = SRT_SET_DESC(SrtData, Persistent, 1, 0);
                addDescriptorSet(pRenderer, &setDesc, &pDescriptorSetPersistent[i]);
                setDesc = SRT_SET_DESC(SrtData, PerFrame, gDataBufferCount, 0);
                addDescriptorSet(pRenderer, &setDesc, &pDescriptorSetPerFrame[i]);
            }
        }
        TFDescriptorSetDesc setDesc = SRT_SET_DESC(SrtData, Persistent, gDataBufferCount * gViewCount, 0);
        addDescriptorSet(pRenderer, &setDesc, &pDescriptorSetCopyQuad);
    }

    void removeDescriptorSets()
    {
        for (uint32_t view = 0; view < gViewCount; ++view)
        {
            if (!gMultiGPU && view > 0)
                continue;

            removeDescriptorSet(pRenderer, pDescriptorSetPersistent[view]);
            removeDescriptorSet(pRenderer, pDescriptorSetPerFrame[view]);
        }
        removeDescriptorSet(pRenderer, pDescriptorSetCopyQuad);
    }

    void addShaders()
    {
        TFShaderLoadDesc skyShader = {};
        skyShader.mVert.pFileName = "skybox.vert";
        skyShader.mFrag.pFileName = "skybox.frag";

        TFShaderLoadDesc basicShader = {};
        basicShader.mVert.pFileName = "basic.vert";
        basicShader.mFrag.pFileName = "basic.frag";

        TFShaderLoadDesc copyQuadShader = {};
        copyQuadShader.mVert.pFileName = "copyQuad.vert";
        copyQuadShader.mFrag.pFileName = "copyQuad.frag";

        addShader(pRenderer, &skyShader, &pSkyBoxDrawShader);
        addShader(pRenderer, &basicShader, &pSphereShader);
        addShader(pRenderer, &copyQuadShader, &pCopyQuadShader);
    }

    void removeShaders()
    {
        removeShader(pRenderer, pSphereShader);
        removeShader(pRenderer, pSkyBoxDrawShader);
        removeShader(pRenderer, pCopyQuadShader);
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
        pipelineSettings.pShaderProgram = pSphereShader;
        pipelineSettings.pVertexLayout = &vertexLayout;
        pipelineSettings.pRasterizerState = &rasterizerStateDesc;
        addPipeline(pRenderer, &desc, &pSpherePipeline);

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
        pipelineSettings.pShaderProgram = pSkyBoxDrawShader;
        addPipeline(pRenderer, &desc, &pSkyBoxDrawPipeline);

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

        depthStateDesc = {};
        desc.mGraphicsDesc.mDepthStencilFormat = TinyImageFormat_UNDEFINED;
        desc.mGraphicsDesc.pDepthState = &depthStateDesc;
        desc.mGraphicsDesc.pVertexLayout = &quadVertexLayout;
        desc.mGraphicsDesc.pShaderProgram = pCopyQuadShader;
        addPipeline(pRenderer, &desc, &pCopyQuadPipeline);
    }

    void removePipelines()
    {
        removePipeline(pRenderer, pSkyBoxDrawPipeline);
        removePipeline(pRenderer, pSpherePipeline);
        removePipeline(pRenderer, pCopyQuadPipeline);
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
            params[6].ppSamplers = &pSamplerSkyBox;
            updateDescriptorSet(pRenderer, 0, pDescriptorSetPersistent[i], 7, params);

            for (uint32_t f = 0; f < gDataBufferCount; ++f)
            {
                TFDescriptorData uParams[1] = {};
                uParams[0].mIndex = SRT_RES_IDX(SrtData, PerFrame, gUniformBlock);
                uParams[0].ppBuffers = &pProjViewUniformBuffer[f];
                updateDescriptorSet(pRenderer, f, pDescriptorSetPerFrame[i], 1, uParams);
            }
        }

        for (uint32_t f = 0; f < gDataBufferCount; ++f)
        {
            for (uint32_t view = 0; view < gViewCount; ++view)
            {
                TFDescriptorData copyQuadParams[1] = {};
                copyQuadParams[0].mIndex = SRT_RES_IDX(SrtData, Persistent, gTex);
                copyQuadParams[0].ppTextures = &pRenderTargets[f][view]->pTexture;
                updateDescriptorSet(pRenderer, f * gViewCount + view, pDescriptorSetCopyQuad, 1, copyQuadParams);
            }
        }
    }

    void removeRenderTargets()
    {
        for (uint32_t frameIdx = 0; frameIdx < gDataBufferCount; ++frameIdx)
        {
            for (uint32_t view = 0; view < gViewCount; ++view)
            {
                removeRenderTarget(pRenderer, pRenderTargets[frameIdx][view]);
            }
        }

        for (uint32_t view = 0; view < gViewCount; ++view)
        {
            removeRenderTarget(pRenderer, pDepthBuffers[view]);
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

        uint32_t sharedIndices[] = { 0 };

        for (uint32_t i = 0; i < gViewCount; ++i)
        {
            if (gMultiGPU)
            {
                colorRT.mNodeIndex = i;
                depthRT.mNodeIndex = i;

                if (i > 0)
                {
                    colorRT.pSharedNodeIndices = sharedIndices;
                    colorRT.mSharedNodeIndexCount = 1;
                }
            }

            addRenderTarget(pRenderer, &depthRT, &pDepthBuffers[i]);

            for (uint32_t frameIdx = 0; frameIdx < gDataBufferCount; ++frameIdx)
            {
                addRenderTarget(pRenderer, &colorRT, &pRenderTargets[frameIdx][i]);
            }
        }

        return pDepthBuffers[0] != NULL;
    }
};

DEFINE_APPLICATION_MAIN(MultiGPU)
