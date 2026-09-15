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

#define _USE_MATH_DEFINES

// Interfaces
#include "../../../../Common_3/Application/Interfaces/IApp.h"
#include "../../../../Common_3/Application/Interfaces/ICamera.h"
#include "../../../../Common_3/Application/Interfaces/IFont.h"
#include "../../../../Common_3/Application/Interfaces/IProfiler.h"
#include "../../../../Common_3/Application/Interfaces/IScreenshot.h"
#include "../../../../Common_3/Application/Interfaces/IUI.h"
#include "../../../../Common_3/Game/Interfaces/IScripting.h"
#include "../../../../Common_3/OS/Interfaces/IInput.h"
#include "../../../../Common_3/Utilities/Interfaces/IFileSystem.h"
#include "../../../../Common_3/Utilities/Interfaces/ILog.h"
#include "../../../../Common_3/Utilities/Interfaces/IMath.h"
#include "../../../../Common_3/Utilities/Interfaces/IThread.h"
#include "../../../../Common_3/Utilities/Interfaces/ITime.h"

#include "../../../../Common_3/Utilities/RingBuffer.h"
#include "../../../../Common_3/Utilities/Threading/ThreadSystem.h"

// for cpu usage query
#if defined(NX64)
#endif

#include "../../../../Common_3/Graphics/Interfaces/IGraphics.h"
#include "../../../../Common_3/Resources/ResourceLoader/Interfaces/IResourceLoader.h"

#include "../../../../Common_3/Utilities/Interfaces/IMemory.h"

// fsl
#include "../../../../Common_3/Graphics/FSL/defaults.h"
#include "./Shaders/FSL/Global.srt.h"

// startdust hash function, use this to generate all the seed and update the position of all particles
#define RND_GEN(x)                  ((x) = (x)*196314165 + 907633515)

#define MAX_CORES                   64
#define MAX_GPU_PROFILE_NAME_LENGTH 256

struct ThreadData
{
    TFCmdPool*      pCmdPool;
    TFCmd*          pCmd;
    TFRenderTarget* pRenderTarget;
    int             mStartPoint;
    int             mDrawCount;
    int             mThreadIndex;
    ThreadID        mThreadID;
    uint32_t        mFrameIndex;
};

struct ObjectProperty
{
    float mRotX = 0, mRotY = 0;
} gObjSettings;

const uint32_t gSampleCount = 60;

// #NOTE: Two sets of resources (one in flight and one being used on CPU)
const uint32_t gDataBufferCount = 2;

struct CpuGraphData
{
    int   mSampleIdx;
    float mSample[gSampleCount];
    float mSampley[gSampleCount];
    float mScale;
    int   mEmptyFlag;
};

struct ViewPortState
{
    float mOffsetX;
    float mOffsetY;
    float mWidth;
    float mHeight;
};

struct GraphVertex
{
    vec2 mPosition;
    vec4 mColor;
};

struct CpuGraph
{
    TFBuffer*     mVertexBuffer[gDataBufferCount]; // vetex buffer for cpu sample
    ViewPortState mViewPort;                       // view port for different core
};

struct UniformBlock
{
    TFCameraMatrix mProjectView;
    TFCameraMatrix mSkyProjectView;
};

int      gTotalParticleCount = 2000000;
uint32_t gGraphWidth = 200;
uint32_t gGraphHeight = 100;

// VR 2D layer transform (positioned at -1 along the Z axis, default rotation, default scale)
TFVR2DLayerDesc gVR2DLayer{ { 0.0f, 0.0f, -1.0f }, { 0.0f, 0.0f, 0.0f, 1.0f }, 1.0f };

TFRenderer* pRenderer = NULL;

TFQueue*   pGraphicsQueue = NULL;
GpuCmdRing gGraphicsCmdRing = {};

static FORGE_CONSTEXPR const uint32_t gMaxThreadCount = MAX_GPU_CMD_POOLS_PER_RING / gDataBufferCount;
GpuCmdRing                            gThreadCmdRing = {};

TFSemaphore* pImageAcquiredSemaphore[gDataBufferCount] = { NULL };
TFSwapChain* pSwapChain = NULL;

TFShader*        pShader = NULL;
TFShader*        pSkyBoxDrawShader = NULL;
TFShader*        pGraphShader = NULL;
TFBuffer*        pParticleVertexBuffer = NULL;
TFBuffer*        pUniformBuffer[gDataBufferCount] = { NULL };
TFBuffer*        pSkyBoxVertexBuffer = NULL;
TFBuffer*        pBackGroundVertexBuffer[gDataBufferCount] = { NULL };
TFBuffer*        pPerDrawBuffers[gDataBufferCount][gMaxThreadCount];
TFPipeline*      pPipeline = NULL;
TFPipeline*      pSkyBoxDrawPipeline = NULL;
TFPipeline*      pGraphLinePipeline = NULL;
TFPipeline*      pGraphLineListPipeline = NULL;
TFPipeline*      pGraphTrianglePipeline = NULL;
TFDescriptorSet* pDescriptorSetPersistent = NULL;
TFDescriptorSet* pDescriptorSetPerFrame = NULL;
TFDescriptorSet* pDescriptorSetPerDraw = NULL;
TFTexture*       pTextures[5];
TFTexture*       pSkyBoxTextures[6];
TFSampler*       pSampler = NULL;
bool             bShowThreadsPlot = true;

uint   gCoresCount;
float* pCoresLoadData;

uint32_t       gThreadCount = 0;
ThreadData     gThreadData[gMaxThreadCount] = {};
TFCameraMatrix gProjectView;
TFCameraMatrix gSkyboxProjectView;
ParticleData   gParticleData;
uint32_t       gParticleRootConstantIndex;
uint32_t       gSeed;
float          gPaletteFactor;
uint           gTextureIndex;

char gMainThreadTxt[64] = { 0 };
char gParticleThreadText[64] = { 0 };

TFUIWindowDesc gGuiWindowDesc;
TFICamera*     pCamera = NULL;

static ThreadSystem gThreadSystem;

ProfileToken gGpuProfiletokens[gMaxThreadCount + 1] = {};

CpuGraphData* pCpuData;
CpuGraph*     pCpuGraph;
bool          gPerformanceStatsInited = false;

const char* pImageFileNames[] = {
    "Palette_Fire.tex", "Palette_Purple.tex", "Palette_Muted.tex", "Palette_Rainbow.tex", "Palette_Sky.tex",
};
const char* pSkyBoxImageFileNames[] = {
    "Skybox_right1.tex", "Skybox_left2.tex", "Skybox_top3.tex", "Skybox_bottom4.tex", "Skybox_front5.tex", "Skybox_back6.tex",
};

TFFontDrawDesc gFrameTimeDraw;
static TFFont* gFont = NULL;

uint32_t* gSeedArray = NULL;
uint64_t  gParDataSize = 0;

ThreadID initialThread;

const char* gTestScripts[] = { "Test_TurnOffPlots.lua" };
uint32_t    gCurrentScriptIndex = 0;

void RunScript(void* pUserData)
{
    UNREF_PARAM(pUserData);
    TFLuaScriptDesc runDesc = {};
    runDesc.pScriptFileName = gTestScripts[gCurrentScriptIndex];
    luaQueueScriptToRun(&runDesc);
}

class MultiThread: public IApp
{
public:
    MultiThread() //-V832
    {
#ifdef TARGET_IOS
        mSettings.mContentScaleFactor = 1.f;
#endif
#ifdef ANDROID
        // We reduce particles quantity for Android in order to keep 30fps
        gTotalParticleCount = 750000;
        bShowThreadsPlot = false;
#endif
    }

    bool Init() override
    {
        gPerformanceStatsInited = initCpuUsage();

        // gThreadCount is the amount of secondary threads: the amount of physical cores except the main thread
        gThreadCount = min(gMaxThreadCount, max(4u, gCoresCount) - 2);

        initialThread = getCurrentThreadID();

        // Initial needed data for each thread
        for (uint32_t i = 0; i < gThreadCount; ++i)
        {
            // Fill up the data for drawing point
            gThreadData[i].mStartPoint = i * (gTotalParticleCount / gThreadCount);
            gThreadData[i].mDrawCount = (gTotalParticleCount / gThreadCount);
            gThreadData[i].mThreadIndex = i;
            gThreadData[i].mThreadID = initialThread;
        }

        bool threadSystemInitialized = threadSystemInit(&gThreadSystem, &gThreadSystemInitDescDefault);
        ASSERT(threadSystemInitialized);

        // Generate particle data
        unsigned int particleSeed = 23232323; // we have gseed as global declaration, pick a name that is not gseed
        for (int i = 0; i < 6 * 9; ++i)
        {
            RND_GEN(particleSeed);
        }

        gSeedArray = (uint32_t*)tf_malloc(gTotalParticleCount * sizeof(uint32_t));
        for (int i = 0; i < gTotalParticleCount; ++i)
        {
            RND_GEN(particleSeed);
            gSeedArray[i] = particleSeed;
        }

        gParDataSize = sizeof(uint32_t) * (uint64_t)gTotalParticleCount;

        char gpuProfileNames[MAX_CORES][MAX_GPU_PROFILE_NAME_LENGTH];

        const char* constGpuProfileNames[gMaxThreadCount + 1] = {};
        TFQueue*    queues[gMaxThreadCount + 1] = {};

        // DirectX 11 not supported on this unit test
        TFRendererDesc settings;
        memset(&settings, 0, sizeof(settings));
        initGPUConfig(settings.pExtendedSettings);
        initRenderer(GetName(), &settings, &pRenderer);
        // check for init success
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

        // initial Gpu profilers for each core
        for (uint32_t i = 0; i < gThreadCount + 1; ++i)
        {
            if (i == 0)
                snprintf(gpuProfileNames[i], 64, "Graphics");
            else
                snprintf(gpuProfileNames[i], 64, "Gpu Particle cmd %u", i - 1);

            constGpuProfileNames[i] = gpuProfileNames[i]; //-V507
            queues[i] = pGraphicsQueue;
        }

        GpuCmdRingDesc cmdRingDesc = {};
        cmdRingDesc.pQueue = pGraphicsQueue;
        cmdRingDesc.mPoolCount = gDataBufferCount;
        cmdRingDesc.mCmdPerPoolCount = 2;
        cmdRingDesc.mAddSyncPrimitives = true;
        initGpuCmdRing(pRenderer, &cmdRingDesc, &gGraphicsCmdRing);

        GpuCmdRingDesc threadCmdRingDesc = {};
        threadCmdRingDesc.pQueue = pGraphicsQueue;
        threadCmdRingDesc.mPoolCount = gThreadCount * gDataBufferCount;
        threadCmdRingDesc.mCmdPerPoolCount = 1;
        threadCmdRingDesc.mAddSyncPrimitives = false;
        initGpuCmdRing(pRenderer, &threadCmdRingDesc, &gThreadCmdRing);

        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            initSemaphore(pRenderer, &pImageAcquiredSemaphore[i]);
        }

        TFHiresTimer timer;
        initHiresTimer(&timer);
        initResourceLoaderInterface(pRenderer);

        TFRootSignatureDesc rootDesc = {};
        INIT_RS_DESC(rootDesc, "default.rootsig", "compute.rootsig");
        initRootSignature(pRenderer, &rootDesc);

        // load all image to GPU
        for (int i = 0; i < 5; ++i)
        {
            TFTextureLoadDesc textureDesc = {};
            textureDesc.pFileName = pImageFileNames[i];
            textureDesc.ppTexture = &pTextures[i];
            // Textures representing color should be stored in SRGB or HDR format
            textureDesc.mCreationFlag = TF_TEXTURE_CREATION_FLAG_SRGB;
            addResource(&textureDesc, NULL);
        }

        for (int i = 0; i < 6; ++i)
        {
            TFTextureLoadDesc textureDesc = {};
            textureDesc.pFileName = pSkyBoxImageFileNames[i];
            textureDesc.ppTexture = &pSkyBoxTextures[i];
            // Textures representing color should be stored in SRGB or HDR format
            textureDesc.mCreationFlag = TF_TEXTURE_CREATION_FLAG_SRGB;
            addResource(&textureDesc, NULL);
        }
        TFSamplerDesc samplerDesc = { TF_FILTER_LINEAR,       TF_FILTER_LINEAR,       TF_MIPMAP_MODE_NEAREST,
                                      TF_ADDRESS_MODE_REPEAT, TF_ADDRESS_MODE_REPEAT, TF_ADDRESS_MODE_REPEAT };
        addSampler(pRenderer, &samplerDesc, &pSampler);
        gTextureIndex = 0;

        // #ifdef _WINDOWS
        //	  SYSTEM_INFO sysinfo;
        //	  GetSystemInfo(&sysinfo);
        //	  gCPUCoreCount = sysinfo.dwNumberOfProcessors;
        // #elif defined(__APPLE__)
        //	  gCPUCoreCount = (unsigned int)[[NSProcessInfo processInfo] processorCount];
        // #endif

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
        skyboxVbDesc.ppBuffer = &pSkyBoxVertexBuffer;
        addResource(&skyboxVbDesc, NULL);

        TFBufferLoadDesc ubDesc = {};
        ubDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        ubDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
        ubDesc.mDesc.mSize = sizeof(UniformBlock);
        ubDesc.mDesc.mFlags = TF_BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
        ubDesc.pData = NULL;
        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            ubDesc.ppBuffer = &pUniformBuffer[i];
            addResource(&ubDesc, NULL);
        }

        ubDesc.mDesc.mSize = sizeof(ParticleData);
        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            for (uint32_t j = 0; j < gMaxThreadCount; ++j)
            {
                ubDesc.ppBuffer = &pPerDrawBuffers[i][j];
                addResource(&ubDesc, NULL);
            }
        }

        TFBufferLoadDesc particleVbDesc = {};
        particleVbDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_VERTEX_BUFFER;
        particleVbDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_GPU_ONLY;
        particleVbDesc.mDesc.mSize = gParDataSize;
        particleVbDesc.pData = gSeedArray;
        particleVbDesc.ppBuffer = &pParticleVertexBuffer;
        addResource(&particleVbDesc, NULL);

        uint32_t graphDataSize = sizeof(GraphVertex) * gSampleCount * 3; // 2 vertex for tri, 1 vertex for line strip

        // generate vertex buffer for all cores to draw cpu graph and setting up view port for each graph
        pCpuGraph = (CpuGraph*)tf_malloc(sizeof(CpuGraph) * gCoresCount);
        for (uint i = 0; i < gCoresCount; ++i)
        {
            pCpuGraph[i].mViewPort.mOffsetX = mSettings.mWidth - 10.0f - gGraphWidth;
            pCpuGraph[i].mViewPort.mWidth = (float)gGraphWidth;
            pCpuGraph[i].mViewPort.mOffsetY = 36 + i * (gGraphHeight + 4.0f);
            pCpuGraph[i].mViewPort.mHeight = (float)gGraphHeight;
            // create vertex buffer for each swapchain
            for (uint j = 0; j < gDataBufferCount; ++j)
            {
                TFBufferLoadDesc vbDesc = {};
                vbDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_VERTEX_BUFFER;
                vbDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_GPU_ONLY;
                vbDesc.mDesc.mFlags = TF_BUFFER_CREATION_FLAG_NONE;
                vbDesc.mDesc.mStartState = TF_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
                vbDesc.mDesc.mSize = graphDataSize;
                vbDesc.pData = NULL;
                vbDesc.ppBuffer = &pCpuGraph[i].mVertexBuffer[j];
                addResource(&vbDesc, NULL);
            }
        }
        uint32_t s = sizeof(GraphVertex);
        graphDataSize = s * gSampleCount;
        for (uint i = 0; i < gDataBufferCount; ++i)
        {
            TFBufferLoadDesc vbDesc = {};
            vbDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_VERTEX_BUFFER;
            vbDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_GPU_ONLY;
            vbDesc.mDesc.mFlags = TF_BUFFER_CREATION_FLAG_NONE;
            vbDesc.mDesc.mStartState = TF_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
            vbDesc.mDesc.mSize = graphDataSize;
            vbDesc.pData = NULL;
            vbDesc.ppBuffer = &pBackGroundVertexBuffer[i];
            addResource(&vbDesc, NULL);
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

        // Initialize profiler
        TFProfilerDesc profiler = {};
        profiler.pRenderer = pRenderer;
        profiler.ppQueues = queues;
        profiler.ppProfilerNames = constGpuProfileNames;
        profiler.pProfileTokens = gGpuProfiletokens;
        profiler.mGpuProfilerCount = gThreadCount + 1;
        initProfiler(&profiler);

        waitForAllResourceLoads();
        LOGF(LogLevel::eINFO, "Load Time %lld", getHiresTimerUSec(&timer, false) / 1000);

        TFCameraMotionParameters cmp{ 100.0f, 800.0f, 1000.0f };
        vec3                     camPos{ 24.0f, 24.0f, 10.0f };
        vec3                     lookAt{ 0 };

        pCamera = initFpsCamera(camPos, lookAt);

        pCamera->setMotionParameters(cmp);

        AddCustomInputBindings();
        initScreenshotCapturer(pRenderer, pGraphicsQueue, GetName());

        return true;
    }

    void Exit() override
    {
        exitScreenshotCapturer();
        threadSystemExit(&gThreadSystem, &gThreadSystemExitDescDefault);
        exitCamera(pCamera);

        exitCpuUsage();

        tf_free(gSeedArray);

        exitProfiler();

        exitUserInterface();

        removeFont(gFont);
        exitFontSystem();

        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            removeResource(pUniformBuffer[i]);
        }

        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            for (uint32_t j = 0; j < gMaxThreadCount; ++j)
            {
                removeResource(pPerDrawBuffers[i][j]);
            }
        }
        removeResource(pParticleVertexBuffer);
        removeResource(pSkyBoxVertexBuffer);

        for (uint i = 0; i < gDataBufferCount; ++i)
            removeResource(pBackGroundVertexBuffer[i]);

        for (uint i = 0; i < gCoresCount; ++i)
        {
            // remove all vertex buffer belongs to graph
            for (uint j = 0; j < gDataBufferCount; ++j)
                removeResource(pCpuGraph[i].mVertexBuffer[j]);
        }

        tf_free(pCpuGraph);

        for (uint i = 0; i < 5; ++i)
            removeResource(pTextures[i]);
        for (uint i = 0; i < 6; ++i)
            removeResource(pSkyBoxTextures[i]);

        removeSampler(pRenderer, pSampler);
        exitGpuCmdRing(pRenderer, &gGraphicsCmdRing);
        exitGpuCmdRing(pRenderer, &gThreadCmdRing);

        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            exitSemaphore(pRenderer, pImageAcquiredSemaphore[i]);
        }
        exitQueue(pRenderer, pGraphicsQueue);
        exitRootSignature(pRenderer);
        exitResourceLoaderInterface(pRenderer);
        exitRenderer(pRenderer);
        exitGPUConfig();

        pRenderer = NULL;
    }

    bool Load(TFReloadDesc* pReloadDesc) override
    {
        UNREF_PARAM(pReloadDesc);

        addShaders();
        addDescriptorSets();

        gGraphWidth = mSettings.mWidth / 6; // 200;
        gGraphHeight = gCoresCount ? (mSettings.mHeight - 30 - gCoresCount * 10) / gCoresCount : 0;
        loadProfilerUI(mSettings.mWidth, mSettings.mHeight);

        gGuiWindowDesc = {};
        gGuiWindowDesc.mStartPos = vec2(mSettings.mWidth * 0.01f, mSettings.mHeight * 0.2f);
        gGuiWindowDesc.mStartSize = vec2(600.0f, 550.0f);
        gGuiWindowDesc.pWindowTitle = GetName();
        gGuiWindowDesc.mFlags = TF_UI_WINDOW_TITLE | TF_UI_WINDOW_SCALABLE | TF_UI_WINDOW_MOVABLE | TF_UI_WINDOW_MINIMIZABLE |
                                TF_UI_WINDOW_BORDER | TF_UI_WINDOW_INIT_HEIGHT_FIT;

        TFLuaWidgetVariableDesc luaVarDesc = {};
#if !defined(TARGET_IOS) && !defined(DURANGO) && !defined(ANDROID)
        luaVarDesc.pLabel = "Show threads plot";
        luaVarDesc.mWidgetType = TF_WIDGET_TYPE_CHECKBOX;
        luaVarDesc.pBool = &bShowThreadsPlot;
        luaRegisterWidgetVariable(&luaVarDesc);
#endif
        luaVarDesc.pLabel = "Test Scripts";
        luaVarDesc.mWidgetType = TF_WIDGET_TYPE_DROPDOWN;
        luaVarDesc.pUint = &gCurrentScriptIndex;
        luaRegisterWidgetVariable(&luaVarDesc);

        TFLuaWidgetFunctionDesc luaFuncDesc = {};
        luaFuncDesc.pLabel = "Run";
        luaFuncDesc.mType = TF_LUA_WIDGET_FUNCTION_ON_EDITED;
        luaFuncDesc.pFunc = RunScript;
        luaRegisterWidgetFunction(&luaFuncDesc);

        if (!addSwapChain())
            return false;

        addPipelines();

        updateDescriptorSets();

        TFUserInterfaceLoadDesc uiLoad = {};
        uiLoad.mColorFormat = pSwapChain->ppRenderTargets[0]->mFormat;
        uiLoad.mHeight = mSettings.mHeight;
        uiLoad.mWidth = mSettings.mWidth;
        uiLoad.mVR2DLayer.mPosition = float3(gVR2DLayer.m2DLayerPosition.x, gVR2DLayer.m2DLayerPosition.y, gVR2DLayer.m2DLayerPosition.z);
        uiLoad.mVR2DLayer.mScale = gVR2DLayer.m2DLayerScale;
        loadUserInterface(&uiLoad);

        TFFontSystemLoadDesc fontLoad = {};
        fontLoad.mColorFormat = pSwapChain->ppRenderTargets[0]->mFormat;
        fontLoad.mHeight = mSettings.mHeight;
        fontLoad.mWidth = mSettings.mWidth;
        loadFontSystem(&fontLoad);

        return true;
    }

    void Unload(TFReloadDesc* pReloadDesc) override
    {
        UNREF_PARAM(pReloadDesc);

        waitQueueIdle(pGraphicsQueue);

        unloadFontSystem();
        unloadUserInterface();

        removePipelines();

        removeSwapChain(pRenderer, pSwapChain);
        unloadProfilerUI();

        removeDescriptorSets();
        removeShaders();
    }

    void Update(float deltaTime) override
    {
        updateGui();

        /************************************************************************/
        // Input
        /************************************************************************/
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

        const float k_wrapAround = (float)(M_PI * 2.0);
        if (gObjSettings.mRotX > k_wrapAround)
            gObjSettings.mRotX -= k_wrapAround;
        if (gObjSettings.mRotX < -k_wrapAround)
            gObjSettings.mRotX += k_wrapAround;
        if (gObjSettings.mRotY > k_wrapAround)
            gObjSettings.mRotY -= k_wrapAround;
        if (gObjSettings.mRotY < -k_wrapAround)
            gObjSettings.mRotY += k_wrapAround;
        /************************************************************************/
        // Compute matrices
        /************************************************************************/
        // Update camera with time
        mat4           modelMat = mat4::rotationX(gObjSettings.mRotX) * mat4::rotationY(gObjSettings.mRotY);
        TFCameraMatrix viewMat = pCamera->getViewMatrix();

        const float    aspectInverse = (float)mSettings.mHeight / (float)mSettings.mWidth;
        const float    horizontal_fov = PI / 2.0f;
        TFCameraMatrix projMat = camMatPerspectiveReverseZ(horizontal_fov, aspectInverse, 0.1f, 100.0f);
        TFCameraMatrix viewProj = camMatMul(&projMat, &viewMat);

        gProjectView = camMatMulMat4(&viewProj, &modelMat);

        // Update particle position matrix
        viewMat = camMatSetTranslation(&viewMat, vec3(0));

        gSkyboxProjectView = camMatMul(&projMat, &viewMat);

        gPaletteFactor += deltaTime * 0.25f;
        if (gPaletteFactor > 1.0f)
        {
            for (int i = 0; i < 9; ++i)
            {
                RND_GEN(gSeed);
            }
            gPaletteFactor = 0.0f;

            gTextureIndex = (gTextureIndex + 1) % 5;

            //   gPaletteFactor = 1.0;
        }
        gParticleData.mPaletteFactor = gPaletteFactor * gPaletteFactor * (3.0f - 2.0f * gPaletteFactor);
        gParticleData.mData = gSeed;
        gParticleData.mTextureIndex = gTextureIndex;

        static float currentTime = 0.0f;
        currentTime += deltaTime;

        // Update cpu data graph
        if (currentTime * 1000.0f > 500)
        {
            updateCpuUsage();
            for (uint i = 0; i < gCoresCount; ++i)
            {
                pCpuData[i].mSampley[pCpuData[i].mSampleIdx] = 0.0f;
                pCpuData[i].mSample[pCpuData[i].mSampleIdx] = pCoresLoadData[i] / 100.0f;
                pCpuData[i].mSampleIdx = (pCpuData[i].mSampleIdx + 1) % gSampleCount;
            }

            currentTime = 0.0f;
        }
    }

    void Draw() override
    {
        if ((bool)pSwapChain->mEnableVsync != mSettings.mVSyncEnabled)
        {
            waitQueueIdle(pGraphicsQueue);
            ::toggleVSync(pRenderer, &pSwapChain);
        }

        uint32_t swapchainImageIndex;
        acquireNextImage(pRenderer, pSwapChain, pImageAcquiredSemaphore[mSettings.mFrameIdx], NULL, &swapchainImageIndex);

        TFRenderTarget*   pRenderTarget = pSwapChain->ppRenderTargets[swapchainImageIndex];
        GpuCmdRingElement elem = getNextGpuCmdRingElement(&gGraphicsCmdRing, true, 2);

        // Stall if CPU is running "gDataBufferCount" frames ahead of GPU
        TFFenceStatus fenceStatus;
        getFenceStatus(pRenderer, elem.pFence, &fenceStatus);
        if (fenceStatus == TF_FENCE_STATUS_INCOMPLETE)
            waitForFences(pRenderer, 1, &elem.pFence);

        uint32_t frameIdx = mSettings.mFrameIdx;

        resetCmdPool(pRenderer, elem.pCmdPool);

        /************************************************************************/
        // Update per-frame buffers
        /************************************************************************/
        {
            TFBufferUpdateDesc viewProjCbv = { pUniformBuffer[frameIdx] };
            beginUpdateResource(&viewProjCbv);
            UniformBlock ub = {};
            ub.mProjectView = gProjectView;
            ub.mSkyProjectView = gSkyboxProjectView;
            memcpy(viewProjCbv.pMappedData, &ub, sizeof(UniformBlock));
            endUpdateResource(&viewProjCbv);
        }

        if (gCoresCount)
        {
            for (uint32_t i = 0; i < gCoresCount; ++i)
            {
                // Update vertex buffer for each cpugraph
                updateCpuGraphBuffer(frameIdx, &pCpuData[i], &pCpuGraph[i]);
            }
            // Update vertex buffer for background of the graph (grid)
            updateCpuGraphBackgroundBuffer(frameIdx);
        }

        /************************************************************************/
        // Particle command recording
        /************************************************************************/
        for (uint32_t i = 0; i < gThreadCount; ++i)
        {
            GpuCmdRingElement threadElem = getNextGpuCmdRingElement(&gThreadCmdRing, true, 1);

            gThreadData[i].pRenderTarget = pRenderTarget;
            gThreadData[i].mFrameIndex = frameIdx;
            gThreadData[i].pCmdPool = threadElem.pCmdPool;
            gThreadData[i].pCmd = threadElem.pCmds[0];
        }
        threadSystemAddTaskGroup(gThreadSystem, drawParticlesThreadPass, gThreadCount, gThreadData);

        /************************************************************************/
        // Main graphics command buffer
        /************************************************************************/
        TFCmd* cmd = elem.pCmds[0];
        beginCmd(cmd);
        cmdBindDescriptorSet(cmd, 0, pDescriptorSetPersistent);
        cmdBindDescriptorSet(cmd, frameIdx, pDescriptorSetPerFrame);
        cmdBeginGpuFrameProfile(cmd, gGpuProfiletokens[0]); // gGpuProfiletokens[0] is reserved for the main thread

        // Transition swapchain image to render target
        TFRenderTargetBarrier barrier = { pRenderTarget, TF_RESOURCE_STATE_PRESENT, TF_RESOURCE_STATE_RENDER_TARGET };
        cmdResourceBarrier(cmd, 0, NULL, 0, NULL, 1, &barrier);

        // Skybox pass
        {
            cmdBeginGpuTimestampQuery(cmd, gGpuProfiletokens[0], "Draw Skybox");

            // Bind Render Targets
            {
                TFBindRenderTargetsDesc bindRenderTargets = {};
                bindRenderTargets.mRenderTargetCount = 1;
                bindRenderTargets.mRenderTargets[0] = { pRenderTarget, TF_LOAD_ACTION_CLEAR };
                cmdBindRenderTargets(cmd, &bindRenderTargets);
                cmdSetViewport(cmd, 0.0f, 0.0f, (float)pRenderTarget->mWidth, (float)pRenderTarget->mHeight, 0.0f, 1.0f);
                cmdSetScissor(cmd, 0, 0, pRenderTarget->mWidth, pRenderTarget->mHeight);

                cmdSetViewport(cmd, 0.0f, 0.0f, (float)pRenderTarget->mWidth, (float)pRenderTarget->mHeight, 1.0f, 1.0f);
            }
            // Draw
            {
                cmdBindPipeline(cmd, pSkyBoxDrawPipeline);
                const uint32_t skyboxStride = sizeof(float) * 4;
                cmdBindVertexBuffer(cmd, 1, &pSkyBoxVertexBuffer, &skyboxStride, NULL);
                cmdDraw(cmd, 36, 0);
            }

            cmdEndGpuTimestampQuery(cmd, gGpuProfiletokens[0]);
        }

        cmdEndGpuFrameProfile(cmd, gGpuProfiletokens[0]); // pGpuProfiletokens[0] is reserved for main thread
        endCmd(cmd);

        /************************************************************************/
        // UI command buffer
        /************************************************************************/
        TFCmd* uiCmd = elem.pCmds[1];
        beginCmd(uiCmd);

        TFBindRenderTargetsDesc bindRenderTargets = {};
        bindRenderTargets.mRenderTargetCount = 1;
        bindRenderTargets.mRenderTargets[0] = { pRenderTarget, TF_LOAD_ACTION_LOAD };
        cmdBindRenderTargets(uiCmd, &bindRenderTargets);
        cmdSetViewport(uiCmd, 0.0f, 0.0f, (float)pRenderTarget->mWidth, (float)pRenderTarget->mHeight, 0.0f, 1.0f);
        cmdSetScissor(uiCmd, 0, 0, pRenderTarget->mWidth, pRenderTarget->mHeight);

        cmdBeginDebugMarker(uiCmd, 0, 1, 0, "Draw UI");
        {
            const float yTxtOffset = 12.f;
            const float xTxtOffset = 8.f;
            float       yTxtOrig = yTxtOffset;

            gFrameTimeDraw.mFontColor = 0xff00ffff;
            gFrameTimeDraw.pFont = gFont;

            float2 txtSizePx = cmdDrawCpuProfile(uiCmd, float2(xTxtOffset, yTxtOrig), &gFrameTimeDraw);
            yTxtOrig += txtSizePx.y + 7 * yTxtOffset;

            txtSizePx = cmdDrawGpuProfile(uiCmd, float2(xTxtOffset, yTxtOrig), gGpuProfiletokens[0], &gFrameTimeDraw);
            yTxtOrig += txtSizePx.y + yTxtOffset;

            txtSizePx.y = 15.0f;

            // Disable UI rendering when taking screenshots
            if (getIsProfilerDrawing())
            {
                for (uint32_t i = 0; i < gThreadCount + 1; ++i)
                {
                    if (i == 0)
                    {
                        snprintf(gParticleThreadText, 64, "GPU Main Thread - %f ms", getGpuProfileAvgTime(gGpuProfiletokens[i]));
                    }
                    else
                    {
                        snprintf(gParticleThreadText, 64, "GPU Particle Thread %u - %f ms", i - 1,
                                 getGpuProfileAvgTime(gGpuProfiletokens[i]));
                    }
                    gFrameTimeDraw.pText = gParticleThreadText;
                    cmdDrawText(uiCmd, float2(xTxtOffset, yTxtOrig), &gFrameTimeDraw);
                    yTxtOrig += txtSizePx.y + yTxtOffset;
                }
            }

            uiCmdDrawUserInterface(uiCmd, pSwapChain, pRenderTarget);

            if (getIsProfilerDrawing() && bShowThreadsPlot)
            {
                cmdBeginDebugMarker(uiCmd, 0, 1, 0, "Draw Graph");

                TFBindRenderTargetsDesc bindDesc = {};
                bindDesc.mRenderTargetCount = 1;
                bindDesc.mRenderTargets[0] = { pRenderTarget, TF_LOAD_ACTION_LOAD };
                cmdBindRenderTargets(uiCmd, &bindDesc);

                gGraphWidth = pRenderTarget->mWidth / 6;
                gGraphHeight = (pRenderTarget->mHeight - 30 - gCoresCount * 10) / gCoresCount;

                for (uint i = 0; i < gCoresCount; ++i)
                {
                    pCpuGraph[i].mViewPort.mOffsetX = pRenderTarget->mWidth - 10.0f - gGraphWidth;
                    pCpuGraph[i].mViewPort.mWidth = (float)gGraphWidth;
                    pCpuGraph[i].mViewPort.mOffsetY = 36 + i * (gGraphHeight + 4.0f);
                    pCpuGraph[i].mViewPort.mHeight = (float)gGraphHeight;

                    cmdSetViewport(uiCmd, pCpuGraph[i].mViewPort.mOffsetX, pCpuGraph[i].mViewPort.mOffsetY, pCpuGraph[i].mViewPort.mWidth,
                                   pCpuGraph[i].mViewPort.mHeight, 0.0f, 1.0f);
                    cmdSetScissor(uiCmd, 0, 0, pRenderTarget->mWidth, pRenderTarget->mHeight);

                    const uint32_t graphDataStride = sizeof(GraphVertex); // vec2(position) + vec4(color)

                    cmdBindPipeline(uiCmd, pGraphTrianglePipeline);
                    cmdBindVertexBuffer(uiCmd, 1, &pBackGroundVertexBuffer[frameIdx], &graphDataStride, NULL);
                    cmdDraw(uiCmd, 4, 0);

                    cmdBindPipeline(uiCmd, pGraphLineListPipeline);
                    cmdBindVertexBuffer(uiCmd, 1, &pBackGroundVertexBuffer[frameIdx], &graphDataStride, NULL);
                    cmdDraw(uiCmd, 38, 4);

                    cmdBindPipeline(uiCmd, pGraphTrianglePipeline);
                    cmdBindVertexBuffer(uiCmd, 1, &(pCpuGraph[i].mVertexBuffer[frameIdx]), &graphDataStride, NULL);
                    cmdDraw(uiCmd, 2 * gSampleCount, 0);

                    cmdBindPipeline(uiCmd, pGraphLinePipeline);
                    cmdBindVertexBuffer(uiCmd, 1, &pCpuGraph[i].mVertexBuffer[frameIdx], &graphDataStride, NULL);
                    cmdDraw(uiCmd, gSampleCount, 2 * gSampleCount);
                }
                cmdBindRenderTargets(uiCmd, NULL);

                cmdEndDebugMarker(uiCmd);
            }
        }
        cmdEndDebugMarker(uiCmd);

        cmdBindRenderTargets(uiCmd, NULL);

        barrier = { pRenderTarget, TF_RESOURCE_STATE_RENDER_TARGET, TF_RESOURCE_STATE_PRESENT };
        cmdResourceBarrier(uiCmd, 0, NULL, 0, NULL, 1, &barrier);
        endCmd(uiCmd);

        // Wait until all particle threads done
        threadSystemWaitIdle(gThreadSystem);

        FlushResourceUpdateDesc flushUpdateDesc = {};
        flushUpdateDesc.mNodeIndex = 0;
        flushResourceUpdates(&flushUpdateDesc);
        TFSemaphore* waitSemaphores[2] = { flushUpdateDesc.pOutSubmittedSemaphore, pImageAcquiredSemaphore[mSettings.mFrameIdx] };

        /************************************************************************/
        // Gather command buffers
        /************************************************************************/
        // It is important to keep the screen clean command at the beginning
        uint32_t cmdCount = gThreadCount + 2;
        TFCmd*   allCmds[gMaxThreadCount + 2] = {};
        allCmds[0] = cmd;

        for (uint32_t i = 0; i < gThreadCount; ++i)
        {
            allCmds[i + 1] = gThreadData[i].pCmd;
        }
        allCmds[gThreadCount + 1] = uiCmd;

        /************************************************************************/
        // Submit and present
        /************************************************************************/
        TFQueueSubmitDesc submitDesc = {};
        submitDesc.mCmdCount = cmdCount;
        submitDesc.mSignalSemaphoreCount = 1;
        submitDesc.mWaitSemaphoreCount = TF_ARRAY_COUNT(waitSemaphores);
        submitDesc.ppCmds = allCmds;
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

    const char* GetName() override { return "03_MultiThread"; }

private:
    void updateGui()
    {
        if (UI_WINDOW_IS_VISIBLE(uiBeginWidgetWindow(&gGuiWindowDesc)))
        {
            uiLayoutAutoTextRows(1);
#if !defined(TARGET_IOS) && !defined(DURANGO) && !defined(ANDROID)
            uiCheckbox("Show threads plot", &bShowThreadsPlot);
#endif
            const int scriptCount = sizeof(gTestScripts) / sizeof(gTestScripts[0]);
            gCurrentScriptIndex = UI_WIDGET_GET_SELECTED(uiDropdown(gTestScripts, scriptCount, gCurrentScriptIndex));

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
        swapChainDesc.ppPresentQueues = &pGraphicsQueue;
        swapChainDesc.mWidth = mSettings.mWidth;
        swapChainDesc.mHeight = mSettings.mHeight;
        swapChainDesc.mImageCount = getRecommendedSwapchainImageCount(pRenderer, &pWindow->handle);
        swapChainDesc.mColorFormat = getSupportedSwapchainFormat(pRenderer, &swapChainDesc, TF_COLOR_SPACE_SDR_SRGB);
        swapChainDesc.mColorSpace = TF_COLOR_SPACE_SDR_SRGB;
        swapChainDesc.mEnableVsync = mSettings.mVSyncEnabled;
        swapChainDesc.mFlags = TF_SWAP_CHAIN_CREATION_FLAG_ENABLE_2D_VR_LAYER;
        swapChainDesc.mVR.m2DLayer = gVR2DLayer;

        ::addSwapChain(pRenderer, &swapChainDesc, &pSwapChain);

        return pSwapChain != NULL;
    }

    void addDescriptorSets()
    {
        TFDescriptorSetDesc setDesc = SRT_SET_DESC(SrtData, Persistent, 1, 0);
        addDescriptorSet(pRenderer, &setDesc, &pDescriptorSetPersistent);
        setDesc = SRT_SET_DESC(SrtData, PerFrame, gDataBufferCount, 0);
        addDescriptorSet(pRenderer, &setDesc, &pDescriptorSetPerFrame);
        setDesc = SRT_SET_DESC(SrtData, PerDraw, gDataBufferCount * gMaxThreadCount, 0);
        addDescriptorSet(pRenderer, &setDesc, &pDescriptorSetPerDraw);
    }

    void removeDescriptorSets()
    {
        removeDescriptorSet(pRenderer, pDescriptorSetPerDraw);
        removeDescriptorSet(pRenderer, pDescriptorSetPerFrame);
        removeDescriptorSet(pRenderer, pDescriptorSetPersistent);
    }

    void addShaders()
    {
        TFShaderLoadDesc graphShader = {};
        graphShader.mVert.pFileName = "Graph.vert";
        graphShader.mFrag.pFileName = "Graph.frag";

        TFShaderLoadDesc particleShader = {};
        particleShader.mVert.pFileName = "Particle.vert";
        particleShader.mFrag.pFileName = "Particle.frag";

        TFShaderLoadDesc skyShader = {};
        skyShader.mVert.pFileName = "Skybox.vert";
        skyShader.mFrag.pFileName = "Skybox.frag";

        addShader(pRenderer, &particleShader, &pShader);
        addShader(pRenderer, &skyShader, &pSkyBoxDrawShader);
        addShader(pRenderer, &graphShader, &pGraphShader);
    }

    void removeShaders()
    {
        removeShader(pRenderer, pShader);
        removeShader(pRenderer, pSkyBoxDrawShader);
        removeShader(pRenderer, pGraphShader);
    }

    void addPipelines()
    {
        // vertexlayout and pipeline for particles
        TFVertexLayout vertexLayout = {};
        vertexLayout.mBindingCount = 1;
        vertexLayout.mAttribCount = 1;
        vertexLayout.mAttribs[0].mSemantic = TF_SEMANTIC_POSITION;
        vertexLayout.mAttribs[0].mFormat = TinyImageFormat_R32_UINT;
        vertexLayout.mAttribs[0].mBinding = 0;
        vertexLayout.mAttribs[0].mLocation = 0;
        vertexLayout.mAttribs[0].mOffset = 0;

        TFBlendStateDesc blendStateDesc = {};
        blendStateDesc.mSrcAlphaFactors[0] = TF_BC_ONE;
        blendStateDesc.mDstAlphaFactors[0] = TF_BC_ONE;
        blendStateDesc.mSrcFactors[0] = TF_BC_ONE;
        blendStateDesc.mDstFactors[0] = TF_BC_ONE;
        blendStateDesc.mColorWriteMasks[0] = TF_COLOR_MASK_ALL;
        blendStateDesc.mRenderTargetMask = TF_BLEND_STATE_TARGET_0;
        blendStateDesc.mIndependentBlend = false;

        TFRasterizerStateDesc rasterizerStateDesc = {};
        rasterizerStateDesc.mCullMode = TF_CULL_MODE_NONE;

        TFPipelineDesc graphicsPipelineDesc = {};
        PIPELINE_LAYOUT_DESC(graphicsPipelineDesc, SRT_LAYOUT_DESC(SrtData, Persistent), SRT_LAYOUT_DESC(SrtData, PerFrame), NULL,
                             SRT_LAYOUT_DESC(SrtData, PerDraw));
        graphicsPipelineDesc.mType = TF_PIPELINE_TYPE_GRAPHICS;
        TFGraphicsPipelineDesc& pipelineSettings = graphicsPipelineDesc.mGraphicsDesc;
        pipelineSettings.mPrimitiveTopo = TF_PRIMITIVE_TOPO_POINT_LIST;
        pipelineSettings.mRenderTargetCount = 1;
        pipelineSettings.pBlendState = &blendStateDesc;
        pipelineSettings.pRasterizerState = &rasterizerStateDesc;
        pipelineSettings.pColorFormats = &pSwapChain->ppRenderTargets[0]->mFormat;
        pipelineSettings.mSampleCount = pSwapChain->ppRenderTargets[0]->mSampleCount;
        pipelineSettings.mSampleQuality = pSwapChain->ppRenderTargets[0]->mSampleQuality;
        pipelineSettings.pShaderProgram = pShader;
        pipelineSettings.pVertexLayout = &vertexLayout;
        addPipeline(pRenderer, &graphicsPipelineDesc, &pPipeline);

        // Layout and pipeline for skybox draw
        vertexLayout = {};
        vertexLayout.mBindingCount = 1;
        vertexLayout.mAttribCount = 1;
        vertexLayout.mAttribs[0].mSemantic = TF_SEMANTIC_POSITION;
        vertexLayout.mAttribs[0].mFormat = TinyImageFormat_R32G32B32A32_SFLOAT;
        vertexLayout.mAttribs[0].mBinding = 0;
        vertexLayout.mAttribs[0].mLocation = 0;
        vertexLayout.mAttribs[0].mOffset = 0;

        pipelineSettings = { 0 };
        pipelineSettings.mPrimitiveTopo = TF_PRIMITIVE_TOPO_TRI_LIST;
        pipelineSettings.mRenderTargetCount = 1;
        pipelineSettings.pRasterizerState = &rasterizerStateDesc;
        pipelineSettings.pColorFormats = &pSwapChain->ppRenderTargets[0]->mFormat;
        pipelineSettings.mSampleCount = pSwapChain->ppRenderTargets[0]->mSampleCount;
        pipelineSettings.mSampleQuality = pSwapChain->ppRenderTargets[0]->mSampleQuality;
        pipelineSettings.pShaderProgram = pSkyBoxDrawShader;
        pipelineSettings.pVertexLayout = &vertexLayout;
        addPipeline(pRenderer, &graphicsPipelineDesc, &pSkyBoxDrawPipeline);

        /********** layout and pipeline for graph draw*****************/
        vertexLayout = {};
        vertexLayout.mBindingCount = 1;
        vertexLayout.mAttribCount = 2;
        vertexLayout.mAttribs[0].mSemantic = TF_SEMANTIC_POSITION;
        vertexLayout.mAttribs[0].mFormat = TinyImageFormat_R32G32B32A32_SFLOAT;
        vertexLayout.mAttribs[0].mBinding = 0;
        vertexLayout.mAttribs[0].mLocation = 0;
        vertexLayout.mAttribs[0].mOffset = 0;
        vertexLayout.mAttribs[1].mSemantic = TF_SEMANTIC_COLOR;
        vertexLayout.mAttribs[1].mFormat = TinyImageFormat_R32G32B32A32_SFLOAT;
        vertexLayout.mAttribs[1].mBinding = 0;
        vertexLayout.mAttribs[1].mLocation = 1;
        vertexLayout.mAttribs[1].mOffset = 4 * sizeof(float);

        pipelineSettings = { 0 };
        pipelineSettings.mPrimitiveTopo = TF_PRIMITIVE_TOPO_LINE_STRIP;
        pipelineSettings.mRenderTargetCount = 1;
        pipelineSettings.pColorFormats = &pSwapChain->ppRenderTargets[0]->mFormat;
        pipelineSettings.mSampleCount = pSwapChain->ppRenderTargets[0]->mSampleCount;
        pipelineSettings.mSampleQuality = pSwapChain->ppRenderTargets[0]->mSampleQuality;
        pipelineSettings.pShaderProgram = pGraphShader;
        pipelineSettings.pVertexLayout = &vertexLayout;
        addPipeline(pRenderer, &graphicsPipelineDesc, &pGraphLinePipeline);

        pipelineSettings.mPrimitiveTopo = TF_PRIMITIVE_TOPO_TRI_STRIP;
        addPipeline(pRenderer, &graphicsPipelineDesc, &pGraphTrianglePipeline);

        pipelineSettings.mPrimitiveTopo = TF_PRIMITIVE_TOPO_LINE_LIST;
        addPipeline(pRenderer, &graphicsPipelineDesc, &pGraphLineListPipeline);
        /********************************************************************/
    }

    void removePipelines()
    {
        removePipeline(pRenderer, pPipeline);
        removePipeline(pRenderer, pSkyBoxDrawPipeline);
        removePipeline(pRenderer, pGraphLineListPipeline);
        removePipeline(pRenderer, pGraphLinePipeline);
        removePipeline(pRenderer, pGraphTrianglePipeline);
    }

    void updateDescriptorSets()
    {
        TFDescriptorData params[8] = {};
        params[0].mIndex = SRT_RES_IDX(SrtData, Persistent, gRightText);
        params[0].ppTextures = &pSkyBoxTextures[0];
        params[1].mIndex = SRT_RES_IDX(SrtData, Persistent, gLeftText);
        params[1].ppTextures = &pSkyBoxTextures[1];
        params[2].mIndex = SRT_RES_IDX(SrtData, Persistent, gTopText);
        params[2].ppTextures = &pSkyBoxTextures[2];
        params[3].mIndex = SRT_RES_IDX(SrtData, Persistent, gBotText);
        params[3].ppTextures = &pSkyBoxTextures[3];
        params[4].mIndex = SRT_RES_IDX(SrtData, Persistent, gFrontText);
        params[4].ppTextures = &pSkyBoxTextures[4];
        params[5].mIndex = SRT_RES_IDX(SrtData, Persistent, gBackText);
        params[5].ppTextures = &pSkyBoxTextures[5];
        params[6].mIndex = SRT_RES_IDX(SrtData, Persistent, gSampler);
        params[6].ppSamplers = &pSampler;
        params[7].mIndex = SRT_RES_IDX(SrtData, Persistent, gTexture);
        params[7].mCount = sizeof(pImageFileNames) / sizeof(pImageFileNames[0]);
        params[7].ppTextures = pTextures;
        updateDescriptorSet(pRenderer, 0, pDescriptorSetPersistent, 8, params);

        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            params[0] = {};
            params[0].mIndex = SRT_RES_IDX(SrtData, PerFrame, gUniformBlock);
            params[0].ppBuffers = &pUniformBuffer[i];
            updateDescriptorSet(pRenderer, i, pDescriptorSetPerFrame, 1, params);
        }

        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            params[0] = {};
            params[0].mIndex = SRT_RES_IDX(SrtData, PerDraw, gParticleConstants);

            for (uint32_t j = 0; j < gMaxThreadCount; j++)
            {
                params[0].ppBuffers = &pPerDrawBuffers[i][j];
                updateDescriptorSet(pRenderer, i * gMaxThreadCount + j, pDescriptorSetPerDraw, 1, params);
            }
        }
    }

    void updateCpuUsage()
    {
        if (!gPerformanceStatsInited)
            return;

        TFPerformanceStats cpuStats = getPerformanceStats();
        for (uint32_t i = 0; i < gCoresCount; i++)
        {
            if (cpuStats.mCoreUsagePercentage[i] >= 0.0f && cpuStats.mCoreUsagePercentage[i] <= 100.0f)
                pCoresLoadData[i] = cpuStats.mCoreUsagePercentage[i];
        }
    }

    bool initCpuUsage()
    {
        gCoresCount = getNumCPUCores();

        pCpuData = (CpuGraphData*)tf_malloc(sizeof(CpuGraphData) * gCoresCount);
        for (uint i = 0; i < gCoresCount; ++i)
        {
            pCpuData[i].mSampleIdx = 0;
            pCpuData[i].mScale = 1.0f;
            for (uint j = 0; j < gSampleCount; ++j)
            {
                pCpuData[i].mSample[j] = 0.0f;
                pCpuData[i].mSampley[j] = 0.0f;
            }
        }

        if (gCoresCount)
        {
            pCoresLoadData = (float*)tf_malloc(sizeof(float) * gCoresCount);
            memset(pCoresLoadData, 0, sizeof(float) * gCoresCount);
        }

        if (initPerformanceStats(PERFORMANCE_STATS_FLAG_CORE_USAGE_PERCENTAGE) < 0)
            return false;

        updateCpuUsage();
        return true;
    }

    void exitCpuUsage()
    {
        tf_free(pCpuData);
        tf_free(pCoresLoadData);

        if (gPerformanceStatsInited)
            exitPerformanceStats();
    }

    void updateCpuGraphBackgroundBuffer(uint32_t frameIdx)
    {
        TFBufferUpdateDesc backgroundVbUpdate = { pBackGroundVertexBuffer[frameIdx] };
        backgroundVbUpdate.mCurrentState = TF_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
        beginUpdateResource(&backgroundVbUpdate);
        GraphVertex* backGroundPoints = (GraphVertex*)backgroundVbUpdate.pMappedData;
        memset(backgroundVbUpdate.pMappedData, 0, pBackGroundVertexBuffer[frameIdx]->mSize);

        // background data
        backGroundPoints[0].mPosition = vec2(-1.0f, -1.0f);
        backGroundPoints[0].mColor = vec4(0.0f, 0.0f, 0.0f, 0.3f);
        backGroundPoints[1].mPosition = vec2(1.0f, -1.0f);
        backGroundPoints[1].mColor = vec4(0.0f, 0.0f, 0.0f, 0.3f);
        backGroundPoints[2].mPosition = vec2(-1.0f, 1.0f);
        backGroundPoints[2].mColor = vec4(0.0f, 0.0f, 0.0f, 0.3f);
        backGroundPoints[3].mPosition = vec2(1.0f, 1.0f);
        backGroundPoints[3].mColor = vec4(0.0f, 0.0f, 0.0f, 0.3f);

        const float woff = 2.0f / gGraphWidth;
        const float hoff = 2.0f / gGraphHeight;

        backGroundPoints[4].mPosition = vec2(-1.0f + woff, -1.0f + hoff);
        backGroundPoints[4].mColor = vec4(0.0f, 0.3f, 0.5f, 0.25f);
        backGroundPoints[5].mPosition = vec2(1.0f - woff, -1.0f + hoff);
        backGroundPoints[5].mColor = vec4(0.0f, 0.3f, 0.5f, 0.25f);
        backGroundPoints[6].mPosition = vec2(1.0f - woff, -1.0f + hoff);
        backGroundPoints[6].mColor = vec4(0.0f, 0.3f, 0.5f, 0.25f);
        backGroundPoints[7].mPosition = vec2(1.0f - woff, 1.0f - hoff);
        backGroundPoints[7].mColor = vec4(0.0f, 0.3f, 0.5f, 0.25f);
        backGroundPoints[8].mPosition = vec2(1.0f - woff, 1.0f - hoff);
        backGroundPoints[8].mColor = vec4(0.0f, 0.3f, 0.5f, 0.25f);
        backGroundPoints[9].mPosition = vec2(-1.0f + woff, 1.0f - hoff);
        backGroundPoints[9].mColor = vec4(0.0f, 0.3f, 0.5f, 0.25f);
        backGroundPoints[10].mPosition = vec2(-1.0f + woff, 1.0f - hoff);
        backGroundPoints[10].mColor = vec4(0.0f, 0.3f, 0.5f, 0.25f);
        backGroundPoints[11].mPosition = vec2(-1.0f + woff, -1.0f + hoff);
        backGroundPoints[11].mColor = vec4(0.0f, 0.3f, 0.5f, 0.25f);

        for (int i = 1; i <= 6; ++i)
        {
            backGroundPoints[12 + i * 2].mPosition =
                vec2(-1.0f + i * (2.0f / 6.0f) - 2.0f * ((pCpuData[0].mSampleIdx % (gSampleCount / 6)) / (float)gSampleCount), -1.0f);
            backGroundPoints[12 + i * 2].mColor = vec4(0.0f, 0.1f, 0.2f, 0.25f);
            backGroundPoints[13 + i * 2].mPosition =
                vec2(-1.0f + i * (2.0f / 6.0f) - 2.0f * ((pCpuData[0].mSampleIdx % (gSampleCount / 6)) / (float)gSampleCount), 1.0f);
            backGroundPoints[13 + i * 2].mColor = vec4(0.0f, 0.1f, 0.2f, 0.25f);
        }
        // start from 24

        for (int i = 1; i <= 9; ++i)
        {
            backGroundPoints[24 + i * 2].mPosition = vec2(-1.0f, -1.0f + i * (2.0f / 10.0f));
            backGroundPoints[24 + i * 2].mColor = vec4(0.0f, 0.1f, 0.2f, 0.25f);
            backGroundPoints[25 + i * 2].mPosition = vec2(1.0f, -1.0f + i * (2.0f / 10.0f));
            backGroundPoints[25 + i * 2].mColor = vec4(0.0f, 0.1f, 0.2f, 0.25f);
        }
        // start from 42

        backGroundPoints[42].mPosition = vec2(-1.0f, -1.0f);
        backGroundPoints[42].mColor = vec4(0.85f, 0.0f, 0.0f, 0.25f);
        backGroundPoints[43].mPosition = vec2(1.0f, -1.0f);
        backGroundPoints[43].mColor = vec4(0.85f, 0.0f, 0.0f, 0.25f);
        backGroundPoints[44].mPosition = vec2(-1.0f, 1.0f);
        backGroundPoints[44].mColor = vec4(0.85f, 0.0f, 0.0f, 0.25f);
        backGroundPoints[45].mPosition = vec2(1.0f, 1.0f);
        backGroundPoints[45].mColor = vec4(0.85f, 0.0f, 0.0f, 0.25f);

        endUpdateResource(&backgroundVbUpdate);
    }

    void updateCpuGraphBuffer(uint32_t frameIdx, CpuGraphData* graphData, CpuGraph* graph)
    {
        TFBufferUpdateDesc vbUpdate = { graph->mVertexBuffer[frameIdx] };
        vbUpdate.mCurrentState = TF_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
        beginUpdateResource(&vbUpdate);
        GraphVertex* points = (GraphVertex*)vbUpdate.pMappedData;
        memset(vbUpdate.pMappedData, 0, graph->mVertexBuffer[frameIdx]->mSize);

        int index = graphData->mSampleIdx;
        // fill up tri vertex
        for (uint32_t i = 0; i < gSampleCount; ++i)
        {
            if (--index < 0)
                index = gSampleCount - 1;
            points[i * 2].mPosition = vec2((1.0f - i * (2.0f / gSampleCount)) * 0.999f - 0.02f, -0.97f);
            points[i * 2].mColor = vec4(0.0f, 0.85f, 0.0f, 1.0f);
            points[i * 2 + 1].mPosition =
                vec2((1.0f - i * (2.0f / gSampleCount)) * 0.999f - 0.02f,
                     (2.0f * ((graphData->mSample[index] + graphData->mSampley[index]) * graphData->mScale - 0.5f)) * 0.97f);
            points[i * 2 + 1].mColor = vec4(0.0f, 0.85f, 0.0f, 1.0f);
        }

        // line vertex
        index = graphData->mSampleIdx;
        for (uint32_t i = 0; i < gSampleCount; ++i)
        {
            if (--index < 0)
                index = gSampleCount - 1;
            points[i + 2 * gSampleCount].mPosition =
                vec2((1.0f - i * (2.0f / gSampleCount)) * 0.999f - 0.02f,
                     (2.0f * ((graphData->mSample[index] + graphData->mSampley[index]) * graphData->mScale - 0.5f)) * 0.97f);
            points[i + 2 * gSampleCount].mColor = vec4(0.0f, 0.85f, 0.0f, 1.0f);
        }

        endUpdateResource(&vbUpdate);
    }

    // Records one particle draw command buffer from a worker thread.
    static void drawParticlesThreadPass(void* pData, uint64_t)
    {
        ThreadData& data = *(ThreadData*)pData;
        if (data.mThreadID == initialThread)
            data.mThreadID = getCurrentThreadID();

        // Update buffers
        TFBufferUpdateDesc update = { pPerDrawBuffers[data.mFrameIndex][data.mThreadIndex] };
        beginUpdateResource(&update);
        memcpy(update.pMappedData, &gParticleData, sizeof(gParticleData));
        endUpdateResource(&update);
        // PROFILER_SET_CPU_SCOPE("Threads", "Cpu draw", 0xffffff);
        TFCmd* cmd = data.pCmd;
        resetCmdPool(pRenderer, data.pCmdPool);
        beginCmd(cmd);

        cmdBindDescriptorSet(cmd, 0, pDescriptorSetPersistent);
        cmdBindDescriptorSet(cmd, data.mFrameIndex, pDescriptorSetPerFrame);
        cmdBeginGpuFrameProfile(cmd, gGpuProfiletokens[data.mThreadIndex + 1], false); // pGpuProfiletokens[0] is reserved for main thread
        char buffer[32] = {};
        snprintf(buffer, TF_ARRAY_COUNT(buffer), "Particle Thread Cmd %d", data.mThreadIndex);
        cmdBeginDebugMarker(cmd, 0.6f, 0.7f, 0.8f, buffer);
        // Particle Draw pass
        {
            cmdBeginGpuTimestampQuery(cmd, gGpuProfiletokens[data.mThreadIndex + 1], buffer);

            // Bind Render Targets
            {
                TFBindRenderTargetsDesc bindRenderTargets = {};
                bindRenderTargets.mRenderTargetCount = 1;
                bindRenderTargets.mRenderTargets[0] = { data.pRenderTarget, TF_LOAD_ACTION_LOAD };
                cmdBindRenderTargets(cmd, &bindRenderTargets);
                cmdSetViewport(cmd, 0.0f, 0.0f, (float)data.pRenderTarget->mWidth, (float)data.pRenderTarget->mHeight, 0.0f, 1.0f);
                cmdSetScissor(cmd, 0, 0, data.pRenderTarget->mWidth, data.pRenderTarget->mHeight);
            }
            // Draw
            {
                const uint32_t parDataStride = sizeof(uint32_t);
                cmdBindPipeline(cmd, pPipeline);
                cmdBindDescriptorSet(cmd, data.mFrameIndex * gMaxThreadCount + data.mThreadIndex, pDescriptorSetPerDraw);
                cmdBindVertexBuffer(cmd, 1, &pParticleVertexBuffer, &parDataStride, NULL);
                cmdDrawInstanced(cmd, data.mDrawCount, data.mStartPoint, 1, 0);
            }

            cmdEndGpuTimestampQuery(cmd, gGpuProfiletokens[data.mThreadIndex + 1]);
        }
        cmdEndDebugMarker(cmd);
        cmdEndGpuFrameProfile(cmd, gGpuProfiletokens[data.mThreadIndex + 1]); // pGpuProfiletokens[0] is reserved for main thread
        endCmd(cmd);
        updatePerformanceStats();
    }
};

DEFINE_APPLICATION_MAIN(MultiThread)
