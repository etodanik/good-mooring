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

#include "../../../Common_3/Application/Interfaces/IApp.h"
#include "../../../Common_3/Application/Interfaces/ICamera.h"
#include "../../../Common_3/Application/Interfaces/IFont.h"
#include "../../../Common_3/Application/Interfaces/IProfiler.h"
#include "../../../Common_3/Application/Interfaces/IScreenshot.h"
#include "../../../Common_3/Application/Interfaces/IUI.h"
#include "../../../Common_3/Game/Interfaces/IScripting.h"
#include "../../../Common_3/Graphics/Interfaces/IGraphics.h"
#include "../../../Common_3/OS/Interfaces/IInput.h"
#include "../../../Common_3/Renderer/Interfaces/IVisibilityBuffer2.h"
#include "../../../Common_3/Utilities/Interfaces/IFileSystem.h"
#include "../../../Common_3/Utilities/Interfaces/ILog.h"
#include "../../../Common_3/Utilities/Interfaces/IThread.h"
#include "../../../Common_3/Utilities/Interfaces/ITime.h"

#include "../../../Common_3/Graphics/FSL/fsl_srt.h"
#include "../../../Common_3/Utilities/RingBuffer.h"
#include "../../../Common_3/Utilities/Threading/ThreadSystem.h"

#include "Shaders/FSL/ShaderDefs.h.fsl"
#include "../../../Common_3/Renderer/VisibilityBuffer2/Shaders/FSL/TriangleBinning.h.fsl"

// fsl
#include "../../../Common_3/Graphics/FSL/defaults.h"
#include "Shaders/FSL/Display.srt.h"
#include "Shaders/FSL/Global.srt.h"
#include "Shaders/FSL/GodrayBlur.srt.h"
#include "Shaders/FSL/LightClusters.srt.h"
#include "Shaders/FSL/Structs.h"
#include "Shaders/FSL/TriangleFiltering.srt.h"

#if defined(XBOX)
#include "../../../Xbox/Common_3/Graphics/Direct3D12/Direct3D12X.h"
#include "../../../Xbox/Common_3/Graphics/IESRAMManager.h"
#define BEGINALLOCATION(X, O) esramBeginAllocation(pRenderer->mDx.pESRAMManager, X, O)
#define ALLOCATIONOFFSET()    esramGetCurrentOffset(pRenderer->mDx.pESRAMManager)
#define ENDALLOCATION(X)      esramEndAllocation(pRenderer->mDx.pESRAMManager)
#else
#define BEGINALLOCATION(X, O)
#define ALLOCATIONOFFSET() 0u
#define ENDALLOCATION(X)
#endif

#include "../../../Common_3/Utilities/Interfaces/IMemory.h"

#define FOREACH_SETTING(X)  \
    X(BindlessSupported, 1) \
    X(DisableAO, 0)         \
    X(DisableGodRays, 0)    \
    X(AddGeometryPassThrough, 0)

#define GENERATE_ENUM(x, y)   x,
#define GENERATE_STRING(x, y) #x,
#define GENERATE_STRUCT(x, y) uint32_t m##x = y;
#define GENERATE_VALUE(x, y)  y,
#define INIT_STRUCT(s)        s = { FOREACH_SETTING(GENERATE_VALUE) }

typedef enum ESettings
{
    FOREACH_SETTING(GENERATE_ENUM) Count
} ESettings;

const char* gSettingNames[] = { FOREACH_SETTING(GENERATE_STRING) };

// Useful for using names directly instead of subscripting an array
struct ConfigSettings
{
    FOREACH_SETTING(GENERATE_STRUCT)
} gGpuSettings;

static ThreadSystem gThreadSystem;

#define SCENE_SCALE 50.0f

typedef enum OutputMode
{
    OUTPUT_MODE_SDR = 0,
    OUTPUT_MODE_P2020,
    OUTPUT_MODE_COUNT
} OutputMode;

struct GodRayConstant
{
    float mScatterFactor;
};

GodRayConstant   gGodRayConstant{ 0.5f };
RenderTargetInfo gShadowRenderTargetInfo;
RenderTargetInfo gDepthRenderTargetInfo;
RenderTargetInfo gClearRenderTargetInfo;

struct GodRayBlurConstant
{
    uint32_t mBlurPassType; // Horizontal or Vertical pass
    uint32_t mFilterRadius;
};

#define MAX_BLUR_KERNEL_SIZE 8

struct BlurWeights
{
    float mBlurWeights[MAX_BLUR_KERNEL_SIZE];
};

GodRayBlurConstant gGodRayBlurConstant;
BlurWeights        gBlurWeightsUniform;
float              gGaussianBlurSigma[2] = { 1.0f, 1.0f };

static float gaussian(float x, float sigma)
{
    x = abs(x) / sigma;
    x *= x;
    return expf(-0.5f * x);
}

enum DisplayColorRange
{
    ColorRange_RGB = 0,
    ColorRange_YCbCr422 = 1,
    ColorRange_YCbCr444 = 2
};

enum DisplaySignalRange
{
    Display_SIGNAL_RANGE_FULL = 0,
    Display_SIGNAL_RANGE_NARROW = 1
};

enum DisplayColorSpace
{
    ColorSpace_Rec709 = 0,
    ColorSpace_Rec2020 = 1,
    ColorSpace_P3D65 = 2
};

struct DisplayChromacities
{
    float RedX;
    float RedY;
    float GreenX;
    float GreenY;
    float BlueX;
    float BlueY;
    float WhiteX;
    float WhiteY;
};

enum BlurPassType
{
    BLUR_PASS_TYPE_HORIZONTAL,
    BLUR_PASS_TYPE_VERTICAL,
    BLUR_PASS_TYPE_COUNT
};

// Camera Walking
static float gCameraWalkingTime = 0.0f;
float3*      gCameraPathData;

uint  gCameraPoints;
float gTotalElpasedTime;
/************************************************************************/
// GUI CONTROLS
/************************************************************************/
#if defined(ANDROID)
#define DEFAULT_ASYNC_COMPUTE false
#else
#define DEFAULT_ASYNC_COMPUTE true
#endif

typedef struct AppSettings
{
    OutputMode mOutputMode = OUTPUT_MODE_SDR;

    bool mSmallScaleRaster = true;
    bool mSeparateAlphaTestRasterization = true;
    bool mAsyncCompute = DEFAULT_ASYNC_COMPUTE;

    // toggle rendering of local point lights
    bool mRenderLocalLights = false;

    bool mDrawDebugTargets = false;

    bool mLargeBinRasterGroups = true;

    float nearPlane = 10.0f;
    float farPlane = 3000.0f;

    float lightNearPlane = 0.1f * SCENE_SCALE;
    float lightFarPlane = 55.0f * SCENE_SCALE;

    // Adjust directional sunlight angle based on time. Values represent a 24 hour clock.
    float mTimeOfDay = 12.0f;
    float mTimeOfDaySunrise = 6.0f;
    float mTimeOfDaySunset = 18.0f;

    // The percentage of the full range of sun angles that the shadowmap will take.
    // For example, a range of 0.8f means that the shadowmap camera lerps from
    // 0.1 * PI radians at sunrise to 0.9 * PI radians at sunset.
    // This does not affect the light direction that BRDFs see, so highlights and contour shadows are unaffected.
    float mShadowRange = 0.985f;

    // The vector pointing to the sunrise direction is given by mat4::rotationY(mSunriseDirection) * vec4::zAxis().
    float mSunriseDirection = 0.58f;

    float mSunSize = 300.0f;

    float4 mLightColor = { 1.0f, 0.8627f, 0.78f, 2.5f };

    bool     mEnableGodray = true;
    uint32_t mFilterRadius = 3;

    float mEsmControl = 200.0f;

    bool mVisualizeGeometry = false;
    bool mVisualizeBinTriangleCount = false;
    bool mVisualizeBinOccupancy = false;

    // AO data
    bool  mEnableAO = true;
    bool  mVisualizeAO = false;
    float mAOIntensity = 3.0f;
    int   mAOQuality = 2;

    float LinearScale = 260.0f;

    DisplayColorSpace  mCurrentSwapChainColorSpace = ColorSpace_Rec2020;
    DisplayColorRange  mDisplayColorRange = ColorRange_RGB;
    DisplaySignalRange mDisplaySignalRange = Display_SIGNAL_RANGE_FULL;

    // Camera Walking
    bool  cameraWalking = false;
    float cameraWalkingSpeed = 1.0f;

    const char* mCameraPresetNames[4] = { "Default", "Bin Rasterizer Alpha Testing", "Large Triangles", "Top-Down" };
    int         mSelectedCameraPreset = 0;

} AppSettings;

/************************************************************************/
// Constants
/************************************************************************/

// #NOTE: Two sets of resources (one in flight and one being used on CPU)
const uint32_t gDataBufferCount = 2;

// Constants
const uint32_t gShadowMapSize = 1024;

struct UniformDataSkybox
{
    mat4 mProjectView;
    vec3 mCamPos;
};

int gGodrayScale = 2;

// Define different geometry sets (opaque and alpha tested geometry)
const uint32_t gNumGeomSets = NUM_GEOMETRY_SETS;

/************************************************************************/
// Per frame staging data
/************************************************************************/
struct PerFrameData
{
    // Stores the camera/eye position in object space for cluster culling
    vec3                  gEyeObjectSpace[NUM_CULLING_VIEWPORTS] = {};
    PerFrameConstantsData gPerFrameUniformData = {};
    VBViewConstantsData   gVBViewUniformData = {};
    UniformDataSkybox     gUniformDataSky;

    // These are just used for statistical information
    uint32_t gTotalClusters = 0;
    uint32_t gCulledClusters = 0;
    uint32_t gDrawCount[gNumGeomSets] = {};
    uint32_t gTotalDrawCount = {};
};

uint32_t gIndexCount = 0;

/************************************************************************/
// Scene
/************************************************************************/
TFPackage*        gPackage = NULL;
/************************************************************************/
// Settings
/************************************************************************/
AppSettings       gAppSettings;
/************************************************************************/
// Profiling
/************************************************************************/
ProfileToken      gGraphicsProfileToken;
ProfileToken      gComputeProfileToken;
/************************************************************************/
// Rendering data
/************************************************************************/
TFRenderer*       pRenderer = NULL;
VisibilityBuffer* pVisibilityBuffer = NULL;
/************************************************************************/
// Queues and Command buffers
/************************************************************************/
TFQueue*          pGraphicsQueue = NULL;
GpuCmdRing        gGraphicsCmdRing = {};

size_t gAllTexturesCount;

TFQueue*   pComputeQueue = NULL;
GpuCmdRing gComputeCmdRing = {};

TFDescriptorSet* pDescriptorSetPersistent = NULL;
TFDescriptorSet* pDescriptorSetPerFrame = NULL;
TFDescriptorSet* pDescriptorSetDisplayPerDraw = NULL;
TFDescriptorSet* pDescriptorSetRenderTargetPerBatch = NULL;
TFDescriptorSet* pDescriptorSetClusterLights = NULL;
/************************************************************************/
// Swapchain
/************************************************************************/
TFSwapChain*     pSwapChain = NULL;
// Signaled by acquireNextImage; waited before rendering to the swapchain image.
TFSemaphore*     pImageAcquiredSemaphore[gDataBufferCount] = { NULL };
// Signaled by graphics submit; waited by present.
TFSemaphore*     pPresentSemaphore = NULL;
// Signaled by graphics submit; waited by next async compute submit before reusing frame resources.
TFSemaphore*     gPrevGraphicsSemaphore = NULL;
// Signaled by async compute submit; waited by graphics before consuming compute output.
TFSemaphore*     gComputeSemaphores[gDataBufferCount] = {};
/************************************************************************/
// Clear buffers pipeline
/************************************************************************/
TFShader*        pShaderClearBuffers = nullptr;
TFPipeline*      pPipelineClearBuffers = nullptr;
/************************************************************************/
// Clear VisibilityBuffer pipeline
/************************************************************************/
TFShader*        pShaderClearRenderTarget = NULL;
TFPipeline*      pPipelineClearRenderTarget = NULL;
uint32_t         gClearRenderTargetRootConstantIndex;
/************************************************************************/
// Triangle filtering pipeline
/************************************************************************/
TFShader*        pShaderTriangleFiltering = nullptr;
TFPipeline*      pPipelineTriangleFiltering = nullptr;
TFDescriptorSet* pDescriptorSetTriangleFilteringPerBatch = NULL;
TFDescriptorSet* pDescriptorSetTriangleFilteringPerDraw = NULL;
TFDescriptorSet* pDescriptorSetTriangleFilteringPerDrawCompute = NULL; // Used in async compute shaders
/************************************************************************/
// Clear light clusters pipeline
/************************************************************************/
TFShader*        pShaderClearLightClusters = nullptr;
TFPipeline*      pPipelineClearLightClusters = nullptr;
/************************************************************************/
// Compute light clusters pipeline
/************************************************************************/
TFShader*        pShaderClusterLights = nullptr;
TFPipeline*      pPipelineClusterLights = nullptr;
/************************************************************************/
// Shadow pass pipeline
/************************************************************************/

/************************************************************************/
// GPU Driven Visibility Buffer Filling
/************************************************************************/
// Below are the GPU resources created for compute based visibility buffer filling.
// Unfortunately, to be able to stay cross platform, we need Root Signatures per each compute shader.
// This is due to Prospero requiring exact same bindings in each shader that shares the same root signature.
// Shaders below do not share the exact bindings with each other.
/************************************************************************/
// GPU Driven Visibility Buffer Filling - Depth/Shadow
/************************************************************************/
// Small triangles
uint32_t    gVisibilityBufferDepthRasterRootConstantIndex = 0;
TFShader*   pShaderVisibilityBufferDepthRaster = nullptr;
TFShader*   pShaderVisibilityBufferDepthRasterSeparateAlpha[2] = { nullptr };
TFPipeline* pPipelineVisibilityBufferDepthRaster = nullptr;
TFPipeline* pPipelineVisibilityBufferDepthRasterSeparateAlpha[2] = { nullptr };
/************************************************************************/
// VB Blit Depth
/************************************************************************/
TFShader*   pShaderBlitDepth = nullptr;
TFPipeline* pPipelineBlitDepth = nullptr;
uint32_t    gBlitDepthRootConstantIndex = 0;
/************************************************************************/
// VB shade pipeline
/************************************************************************/
TFShader*   pShaderVisibilityBufferShade[2] = { nullptr };
TFPipeline* pPipelineVisibilityBufferShadeSrgb[2] = { nullptr };
/************************************************************************/
// Skybox pipeline
/************************************************************************/
TFShader*   pShaderSkybox = nullptr;
TFPipeline* pSkyboxPipeline = nullptr;
TFBuffer*   pSkyboxVertexBuffer = NULL;
TFTexture*  pSkybox = NULL;
/************************************************************************/
// Godray pipeline
/************************************************************************/
TFShader*   pGodRayPass = nullptr;
TFPipeline* pPipelineGodRayPass = nullptr;
TFBuffer*   pBufferGodRayConstant = nullptr;
uint32_t    gGodRayConstantIndex = 0;

TFShader*        pShaderGodRayBlurPass = nullptr;
TFPipeline*      pPipelineGodRayBlurPass = nullptr;
TFDescriptorSet* pDescriptorSetGodRayBlurPassPerDraw = nullptr;
TFBuffer*        pBufferBlurWeights = nullptr;

TFBuffer*   pGodRayBlurBuffer[BLUR_PASS_TYPE_COUNT][gDataBufferCount] = {};
uint32_t    gGodRayBlurConstantIndex = 0;
/************************************************************************/
// Curve Conversion pipeline
/************************************************************************/
TFShader*   pShaderCurveConversion = nullptr;
TFPipeline* pPipelineCurveConversionPass = nullptr;

OutputMode         gWasOutputMode = gAppSettings.mOutputMode;
DisplayColorSpace  gWasColorSpace = gAppSettings.mCurrentSwapChainColorSpace;
DisplayColorRange  gWasDisplayColorRange = gAppSettings.mDisplayColorRange;
DisplaySignalRange gWasDisplaySignalRange = gAppSettings.mDisplaySignalRange;

/************************************************************************/
// Present pipeline
/************************************************************************/
TFShader*       pShaderPresentPass = nullptr;
TFPipeline*     pPipelinePresentPass = nullptr;
uint32_t        gSCurveRootConstantIndex = 0;
/************************************************************************/
// Render targets
/************************************************************************/
TFRenderTarget* pDepthBuffer = NULL;
TFBuffer*       pVBDepthBuffer[2] = { NULL };
TFRenderTarget* pRenderTargetShadow = NULL;
TFRenderTarget* pIntermediateRenderTarget = NULL;
TFRenderTarget* pRenderTargetGodRay[2] = { NULL };
TFRenderTarget* pCurveConversionRenderTarget = NULL;
/************************************************************************/
// Samplers
/************************************************************************/
TFSampler*      pSamplerTrilinearAniso = NULL;
TFSampler*      pSamplerBilinear = NULL;
TFSampler*      pSamplerPointClamp = NULL;
TFSampler*      pSamplerBilinearClamp = NULL;
/************************************************************************/
// Vertex buffers for the scene
/************************************************************************/
TFGeometry*     pGeom = NULL;
/************************************************************************/
// Indirect buffers
/************************************************************************/
TFBuffer*       pMaterialPropertyBuffer = NULL;
TFBuffer*       pPerFrameUniformBuffers[gDataBufferCount] = { NULL };
// used in bin_rasterizer, clear_render_target and visibilityBuffer_blitDepth
TFBuffer*       pRenderTargetInfoConstantsBuffers[gDataBufferCount][3] = { { NULL } };
enum
{
    VB_UB_COMPUTE = 0,
    VB_UB_GRAPHICS,
    VB_UB_COUNT
};
TFBuffer* pPerFrameVBUniformBuffers[VB_UB_COUNT][gDataBufferCount] = {};
// Buffers containing all indirect draw commands per geometry set (no culling)
uint32_t  gDrawCountAll[gNumGeomSets] = {};
TFBuffer* pMeshConstantsBuffer = NULL;

/************************************************************************/
// Other buffers for lighting, point lights,...
/************************************************************************/
TFBuffer*        pLightsBuffer = NULL;
TFBuffer**       gPerBatchUniformBuffers = NULL;
TFBuffer*        pLightClustersCount[gDataBufferCount] = { NULL };
TFBuffer*        pLightClusters[gDataBufferCount] = { NULL };
TFBuffer*        pUniformBufferSky[gDataBufferCount] = { NULL };
FilterContainer* pFilterContainers = NULL;
size_t           gMeshCount = 0;
size_t           gTextureCount = 0;
TFUIWindowDesc   gGuiWindowDesc;
TFUIWindowDesc   gDebugTexturesWindowDesc;
bstring          gOutputSupportsHDRText = bempty();

TFFontDrawDesc gFrameTimeDraw;
TFFont*        gFont;

TFTexture* gNullTextureResource = NULL;

/************************************************************************/
TFICamera* pCamera = NULL;
/************************************************************************/
// CPU staging data
/************************************************************************/
// CPU buffers for light data
LightData  gLightData[LIGHT_COUNT] = {};

PerFrameData    gPerFrame[gDataBufferCount] = {};
TFRenderTarget* pScreenRenderTarget = NULL;

// We are rendering the scene (geometry, skybox, ...) at this resolution, UI at window resolution (mSettings.mWidth, mSettings.mHeight)
// Render scene at gSceneRes
// Render UI into backbuffer
static TFResolution gSceneRes;
/************************************************************************/
// Screen resolution UI data
/************************************************************************/
#if defined(_WINDOWS)
struct ResolutionData
{
    // Buffer for all res name strings
    char*        mResNameContainer;
    // Array of const char*
    const char** mResNamePointers;
};

static ResolutionData gGuiResolution = { NULL, NULL };
#endif

const char*      pPipelineCacheName = "PipelineCache.cache";
TFPipelineCache* pPipelineCache = NULL;

/************************************************************************/
// App implementation
/************************************************************************/
class Visibility_Buffer: public IApp
{
public:
    Visibility_Buffer() { SetContentScaleFactor(2.0f); }
    bool Init()
    {
        threadSystemInit(&gThreadSystem, &gThreadSystemInitDescDefault);

        // Camera Walking
        loadCameraPath("cameraPath.txt", gCameraPoints, &gCameraPathData);
        gCameraPoints = (uint)29084 / 2;
        gTotalElpasedTime = (float)gCameraPoints * 0.00833f;

        /************************************************************************/
        // Initialize the Forge renderer with the appropriate parameters.
        /************************************************************************/
        INIT_STRUCT(gGpuSettings);
        TFExtendedSettings extendedSettings = {};
        extendedSettings.mNumSettings = ESettings::Count;
        extendedSettings.pSettings = (uint32_t*)&gGpuSettings;
        extendedSettings.ppSettingNames = gSettingNames;

        TFRendererDesc settings = {};
        settings.pExtendedSettings = &extendedSettings;
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

        // turn off by default depending on gpu config rules
        gAppSettings.mEnableGodray &= !gGpuSettings.mDisableGodRays;
        gAppSettings.mEnableAO &= !gGpuSettings.mDisableAO;

        TFQueueDesc queueDesc = {};
        queueDesc.mType = TF_QUEUE_TYPE_GRAPHICS;
        queueDesc.mFlag = TF_QUEUE_FLAG_INIT_MICROPROFILE;
        initQueue(pRenderer, &queueDesc, &pGraphicsQueue);

        TFQueueDesc computeQueueDesc = {};
        computeQueueDesc.mType = TF_QUEUE_TYPE_COMPUTE;
        computeQueueDesc.mFlag = TF_QUEUE_FLAG_INIT_MICROPROFILE;
        initQueue(pRenderer, &computeQueueDesc, &pComputeQueue);

        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            initSemaphore(pRenderer, &pImageAcquiredSemaphore[i]);
        }
        initSemaphore(pRenderer, &pPresentSemaphore);

        GpuCmdRingDesc cmdRingDesc = {};
        cmdRingDesc.pQueue = pGraphicsQueue;
        cmdRingDesc.mPoolCount = gDataBufferCount;
        cmdRingDesc.mCmdPerPoolCount = 1;
        cmdRingDesc.mAddSyncPrimitives = true;
        initGpuCmdRing(pRenderer, &cmdRingDesc, &gGraphicsCmdRing);

        cmdRingDesc = {};
        cmdRingDesc.pQueue = pComputeQueue;
        cmdRingDesc.mPoolCount = gDataBufferCount;
        cmdRingDesc.mCmdPerPoolCount = 1;
        cmdRingDesc.mAddSyncPrimitives = true;
        initGpuCmdRing(pRenderer, &cmdRingDesc, &gComputeCmdRing);
        /************************************************************************/
        // Initialize helper interfaces (resource loader, profiler)
        /************************************************************************/
        initResourceLoaderInterface(pRenderer);

        TFRootSignatureDesc rootDesc = {};
        INIT_RS_DESC(rootDesc, "default.rootsig", "compute.rootsig");
        initRootSignature(pRenderer, &rootDesc);

        TFPipelineCacheLoadDesc cacheDesc = {};
        cacheDesc.pFileName = pPipelineCacheName;
        loadPipelineCache(pRenderer, &cacheDesc, &pPipelineCache);

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

        /************************************************************************/
        // Setup the UI components for text rendering, UI controls...
        /************************************************************************/

        // Initialize micro profiler and its UI.
        TFProfilerDesc profiler = {};
        profiler.pRenderer = pRenderer;
        initProfiler(&profiler);

        gGraphicsProfileToken = initGpuProfiler(pRenderer, pGraphicsQueue, "Graphics");
        gComputeProfileToken = initGpuProfiler(pRenderer, pComputeQueue, "Compute");
        /************************************************************************/
        // Start timing the scene load
        /************************************************************************/
        TFHiresTimer totalTimer;
        initHiresTimer(&totalTimer);
        /************************************************************************/
        // Setup sampler states
        /************************************************************************/
        // Create sampler for VB render target
        TFSamplerDesc trilinearDesc = { TF_FILTER_LINEAR,
                                        TF_FILTER_LINEAR,
                                        TF_MIPMAP_MODE_LINEAR,
                                        TF_ADDRESS_MODE_REPEAT,
                                        TF_ADDRESS_MODE_REPEAT,
                                        TF_ADDRESS_MODE_REPEAT,
                                        0.0f,
                                        false,
                                        0.0f,
                                        0.0f,
                                        8.0f };
        TFSamplerDesc bilinearDesc = { TF_FILTER_LINEAR,       TF_FILTER_LINEAR,       TF_MIPMAP_MODE_LINEAR,
                                       TF_ADDRESS_MODE_REPEAT, TF_ADDRESS_MODE_REPEAT, TF_ADDRESS_MODE_REPEAT };
        TFSamplerDesc pointDesc = { TF_FILTER_NEAREST,
                                    TF_FILTER_NEAREST,
                                    TF_MIPMAP_MODE_NEAREST,
                                    TF_ADDRESS_MODE_CLAMP_TO_EDGE,
                                    TF_ADDRESS_MODE_CLAMP_TO_EDGE,
                                    TF_ADDRESS_MODE_CLAMP_TO_EDGE };

        TFSamplerDesc bilinearClampDesc = { TF_FILTER_LINEAR,
                                            TF_FILTER_LINEAR,
                                            TF_MIPMAP_MODE_LINEAR,
                                            TF_ADDRESS_MODE_CLAMP_TO_EDGE,
                                            TF_ADDRESS_MODE_CLAMP_TO_EDGE,
                                            TF_ADDRESS_MODE_CLAMP_TO_EDGE };

        addSampler(pRenderer, &trilinearDesc, &pSamplerTrilinearAniso);
        addSampler(pRenderer, &bilinearDesc, &pSamplerBilinear);
        addSampler(pRenderer, &pointDesc, &pSamplerPointClamp);
        addSampler(pRenderer, &bilinearClampDesc, &pSamplerBilinearClamp);

        TFBufferLoadDesc godrayConstantBufferDesc = {};
        godrayConstantBufferDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        godrayConstantBufferDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
        godrayConstantBufferDesc.mDesc.mFlags = TF_BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
        godrayConstantBufferDesc.mDesc.mSize = sizeof(GodRayConstant);
        godrayConstantBufferDesc.pData = &gGodRayConstant;
        godrayConstantBufferDesc.ppBuffer = &pBufferGodRayConstant;
        addResource(&godrayConstantBufferDesc, NULL);

        TFBufferLoadDesc blitDepthConstantBufferDesc = {};
        blitDepthConstantBufferDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        blitDepthConstantBufferDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
        blitDepthConstantBufferDesc.mDesc.mFlags = TF_BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;

        for (uint32_t i = 0; i < gDataBufferCount; i++)
        {
            gShadowRenderTargetInfo = { 0, (int)gShadowMapSize, (int)gShadowMapSize };
            blitDepthConstantBufferDesc.mDesc.mSize = sizeof(RenderTargetInfo);
            blitDepthConstantBufferDesc.pData = &gShadowRenderTargetInfo;
            blitDepthConstantBufferDesc.ppBuffer = &pRenderTargetInfoConstantsBuffers[i][0];
            addResource(&blitDepthConstantBufferDesc, NULL);

            gDepthRenderTargetInfo = { 1, (int)gSceneRes.mWidth, (int)gSceneRes.mHeight };
            blitDepthConstantBufferDesc.mDesc.mSize = sizeof(RenderTargetInfo);
            blitDepthConstantBufferDesc.pData = &gDepthRenderTargetInfo;
            blitDepthConstantBufferDesc.ppBuffer = &pRenderTargetInfoConstantsBuffers[i][1];
            addResource(&blitDepthConstantBufferDesc, NULL);

            gClearRenderTargetInfo = { 0, (int)gSceneRes.mWidth * (int)gSceneRes.mHeight, 0 };
            blitDepthConstantBufferDesc.mDesc.mSize = sizeof(RenderTargetInfo);
            blitDepthConstantBufferDesc.pData = &gClearRenderTargetInfo;
            blitDepthConstantBufferDesc.ppBuffer = &pRenderTargetInfoConstantsBuffers[i][2];
            addResource(&blitDepthConstantBufferDesc, NULL);
        }

        TFBufferLoadDesc godrayBlurConstantBufferDesc = {};
        godrayBlurConstantBufferDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        godrayBlurConstantBufferDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
        godrayBlurConstantBufferDesc.mDesc.mFlags = TF_BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
        godrayBlurConstantBufferDesc.mDesc.mSize = sizeof(GodRayBlurConstant);
        for (uint32_t frameIdx = 0; frameIdx < gDataBufferCount; ++frameIdx)
        {
            godrayBlurConstantBufferDesc.pData = &gGodRayBlurConstant;
            godrayBlurConstantBufferDesc.ppBuffer = &pGodRayBlurBuffer[BLUR_PASS_TYPE_HORIZONTAL][frameIdx];
            addResource(&godrayBlurConstantBufferDesc, NULL);
            godrayBlurConstantBufferDesc.pData = &gGodRayBlurConstant;
            godrayBlurConstantBufferDesc.ppBuffer = &pGodRayBlurBuffer[BLUR_PASS_TYPE_VERTICAL][frameIdx];
            addResource(&godrayBlurConstantBufferDesc, NULL);
        }

        for (int i = 0; i < MAX_BLUR_KERNEL_SIZE; i++)
        {
            gBlurWeightsUniform.mBlurWeights[i] = gaussian((float)i, gGaussianBlurSigma[0]);
        }

        TFBufferLoadDesc blurWeightsBufferDesc = {};
        blurWeightsBufferDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        blurWeightsBufferDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
        blurWeightsBufferDesc.mDesc.mSize = sizeof(BlurWeights);
        blurWeightsBufferDesc.ppBuffer = &pBufferBlurWeights;
        blurWeightsBufferDesc.pData = &gBlurWeightsUniform;
        blurWeightsBufferDesc.mDesc.mFlags = TF_BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
        addResource(&blurWeightsBufferDesc, NULL);

        /************************************************************************/
        // Load resources for skybox
        /************************************************************************/
        TFTextureLoadDesc skyboxTriDesc = {};
        skyboxTriDesc.pFileName = "SanMiguel_3/daytime_cube.tex";
        skyboxTriDesc.ppTexture = &pSkybox;
        addResource(&skyboxTriDesc, NULL);
        /************************************************************************/
        // Load the scene using the SceneLoader class
        /************************************************************************/
        TFHiresTimer sceneLoadTimer;
        initHiresTimer(&sceneLoadTimer);

        {
            TFSyncToken   token = {};
            TFTextureDesc texDesc = {};
            texDesc.mArraySize = 1;
            texDesc.mDepth = 1;
            texDesc.mFormat = TinyImageFormat_B8G8R8A8_SRGB;
            texDesc.mWidth = 1;
            texDesc.mHeight = 2;
            texDesc.mMipLevels = 1;
            texDesc.mSampleCount = TF_SAMPLE_COUNT_1;
            texDesc.mStartState = TF_RESOURCE_STATE_SHADER_RESOURCE;
            texDesc.mDescriptors = TF_DESCRIPTOR_TYPE_TEXTURE;
            texDesc.pName = "null_texture";

            TFTextureLoadDesc loadDesc = {};
            loadDesc.pDesc = &texDesc;
            loadDesc.ppTexture = &gNullTextureResource;
            addResource(&loadDesc, &token);

            waitForToken(&token);
        }

        TFPackageLoadDesc packageLoadDesc = {};
        packageLoadDesc.packageName = "SanMiguelPak.buny";
        packageLoadDesc.loadGeoData = true;
        packageLoadDesc.loadTexData = true;
        packageLoadDesc.ppOutPackage = &gPackage;

        if (!addResourcesFromPackage(&packageLoadDesc, gNullTextureResource))
        {
            LOGF(LogLevel::eERROR, "Failed to load package");
            return false;
        }

        LOGF(LogLevel::eINFO, "Load scene : %f ms", getHiresTimerUSec(&sceneLoadTimer, true) / 1000.0f);

        gMeshCount = gPackage->pTextureMetadata->mMeshCount;
        gTextureCount = gPackage->pTextureMetadata->mTextureCount;
        pFilterContainers = (FilterContainer*)tf_calloc(gMeshCount, sizeof(FilterContainer));
        pGeom = *(gPackage->pGeoData[0].ppGeometry);

        // Filtered index buffer will be organized by batches. Each batch can have up to 256 triangles.
        // Here we calculate the number of maximum indices we need for the filtered index buffer.
        for (uint32_t i = 0; i < gMeshCount; ++i)
        {
            gIndexCount += (uint32_t)ceil((pGeom->pDrawArgs + i)->mIndexCount / 3.f / RASTERIZE_BATCH_SIZE) * RASTERIZE_BATCH_SIZE * 3;
        }

        gIndexCount = MAX_BATCHES * RASTERIZE_BATCH_SIZE * 3;

        // Init visibility buffer
        VisibilityBufferDesc vbDesc = {};
        vbDesc.mFilterBatchCount = FILTER_BATCH_COUNT;
        vbDesc.mNumFrames = gDataBufferCount;
        vbDesc.mNumBuffers = gDataBufferCount;
        vbDesc.mNumGeometrySets = NUM_GEOMETRY_SETS;
        vbDesc.mNumViews = NUM_CULLING_VIEWPORTS;
        vbDesc.mIndexCount = gIndexCount;
        vbDesc.mFilterBatchSize = FILTER_BATCH_SIZE;
        initVisibilityBuffer(pRenderer, &vbDesc, &pVisibilityBuffer);

        /************************************************************************/
        // Cluster creation
        /************************************************************************/
        TFHiresTimer clusterTimer;
        initHiresTimer(&clusterTimer);

        // Calculate clusters
        for (uint32_t meshIndex = 0; meshIndex < gMeshCount; ++meshIndex)
        {
            TFMaterialFlags material = gPackage->pTextureMetadata->pMaterialProps[meshIndex].mFlags;

            FilterContainerDescriptor desc = {};
            // desc.mType = FILTER_CONTAINER_TYPE_CLUSTER;
            desc.mBaseIndex = (pGeom->pDrawArgs + meshIndex)->mStartIndex;
            desc.mInstanceIndex = INSTANCE_INDEX_NONE;

            desc.mGeometrySet = GEOMSET_OPAQUE;
            if (material & MATERIAL_FLAG_ALPHA_TESTED)
                desc.mGeometrySet = GEOMSET_ALPHA_CUTOUT;

            desc.mIndexCount = (pGeom->pDrawArgs + meshIndex)->mIndexCount;
            desc.mMeshIndex = meshIndex;
            addVBFilterContainer(&desc, &pFilterContainers[meshIndex]);
        }

        LOGF(LogLevel::eINFO, "Load clusters : %f ms", getHiresTimerUSec(&clusterTimer, true) / 1000.0f);

        // Generate sky box vertex buffer
        const float skyBoxPoints[] = {
            0.5f,  -0.5f, -0.5f, 1.0f, // -z
            -0.5f, -0.5f, -0.5f, 1.0f,  -0.5f, 0.5f,  -0.5f, 1.0f,  -0.5f, 0.5f,
            -0.5f, 1.0f,  0.5f,  0.5f,  -0.5f, 1.0f,  0.5f,  -0.5f, -0.5f, 1.0f,

            -0.5f, -0.5f, 0.5f,  1.0f, //-x
            -0.5f, -0.5f, -0.5f, 1.0f,  -0.5f, 0.5f,  -0.5f, 1.0f,  -0.5f, 0.5f,
            -0.5f, 1.0f,  -0.5f, 0.5f,  0.5f,  1.0f,  -0.5f, -0.5f, 0.5f,  1.0f,

            0.5f,  -0.5f, -0.5f, 1.0f, //+x
            0.5f,  -0.5f, 0.5f,  1.0f,  0.5f,  0.5f,  0.5f,  1.0f,  0.5f,  0.5f,
            0.5f,  1.0f,  0.5f,  0.5f,  -0.5f, 1.0f,  0.5f,  -0.5f, -0.5f, 1.0f,

            -0.5f, -0.5f, 0.5f,  1.0f, // +z
            -0.5f, 0.5f,  0.5f,  1.0f,  0.5f,  0.5f,  0.5f,  1.0f,  0.5f,  0.5f,
            0.5f,  1.0f,  0.5f,  -0.5f, 0.5f,  1.0f,  -0.5f, -0.5f, 0.5f,  1.0f,

            -0.5f, 0.5f,  -0.5f, 1.0f, //+y
            0.5f,  0.5f,  -0.5f, 1.0f,  0.5f,  0.5f,  0.5f,  1.0f,  0.5f,  0.5f,
            0.5f,  1.0f,  -0.5f, 0.5f,  0.5f,  1.0f,  -0.5f, 0.5f,  -0.5f, 1.0f,

            0.5f,  -0.5f, 0.5f,  1.0f, //-y
            0.5f,  -0.5f, -0.5f, 1.0f,  -0.5f, -0.5f, -0.5f, 1.0f,  -0.5f, -0.5f,
            -0.5f, 1.0f,  -0.5f, -0.5f, 0.5f,  1.0f,  0.5f,  -0.5f, 0.5f,  1.0f,
        };

        uint64_t         skyBoxDataSize = 4 * 6 * 6 * sizeof(float);
        TFBufferLoadDesc skyboxVbDesc = {};
        skyboxVbDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_VERTEX_BUFFER;
        skyboxVbDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_GPU_ONLY;
        skyboxVbDesc.mDesc.mSize = skyBoxDataSize;
        skyboxVbDesc.pData = skyBoxPoints;
        skyboxVbDesc.mDesc.mFlags = TF_BUFFER_CREATION_FLAG_OWN_MEMORY_BIT | TF_BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
        skyboxVbDesc.ppBuffer = &pSkyboxVertexBuffer;
        addResource(&skyboxVbDesc, NULL);

        /************************************************************************/
        // Most important options
        /************************************************************************/
        // Default NX settings for better performance.
#if NX64
        // Async compute is not optimal on the NX platform. Turning this off to make use of default graphics queue for triangle visibility.
        gAppSettings.mAsyncCompute = false;
        // High fill rate features are also disabled by default for performance.
        gAppSettings.mEnableGodray = false;
        gAppSettings.mEnableAO = false;
#endif

        /************************************************************************/
        // Finish the resource loading process since the next code depends on the loaded resources
        /************************************************************************/
        waitForAllResourceLoads();

        gAllTexturesCount = gMeshCount * TEXTURES_PER_MESH;

        TFHiresTimer setupBuffersTimer;
        initHiresTimer(&setupBuffersTimer);
        addTriangleFilteringBuffers();

        LOGF(LogLevel::eINFO, "Setup buffers : %f ms", getHiresTimerUSec(&setupBuffersTimer, true) / 1000.0f);

        LOGF(LogLevel::eINFO, "Total Load Time : %f ms", getHiresTimerUSec(&totalTimer, true) / 1000.0f);

        /************************************************************************/
        // Setup the fps camera for navigating through the scene
        /************************************************************************/
        vec3                     startPosition(620.0f, 490.0f, 70.0f);
        vec3                     startLookAt = startPosition + vec3(-1.0f - 0.0f, 0.1f, 0.0f);
        TFCameraMotionParameters camParams;
        camParams.acceleration = 1300 * 2.5f;
        camParams.braking = 1300 * 2.5f;
        camParams.maxSpeed = 200 * 2.5f;
        pCamera = initFpsCamera(startPosition, startLookAt);
        pCamera->setMotionParameters(camParams);

        // App Actions
        AddCustomInputBindings();
        initScreenshotCapturer(pRenderer, pGraphicsQueue, GetName());
        return true;
    }

    void Exit()
    {
        exitScreenshotCapturer();

        tf_free(gCameraPathData);

        threadSystemExit(&gThreadSystem, &gThreadSystemExitDescDefault);

        exitCamera(pCamera);
        for (uint32_t i = 0; i < gDataBufferCount; i++)
        {
            removeResource(pRenderTargetInfoConstantsBuffers[i][2]);
            removeResource(pRenderTargetInfoConstantsBuffers[i][1]);
            removeResource(pRenderTargetInfoConstantsBuffers[i][0]);
        }
        removeResource(pBufferGodRayConstant);
        for (uint32_t frameIdx = 0; frameIdx < gDataBufferCount; ++frameIdx)
        {
            removeResource(pGodRayBlurBuffer[BLUR_PASS_TYPE_HORIZONTAL][frameIdx]);
            removeResource(pGodRayBlurBuffer[BLUR_PASS_TYPE_VERTICAL][frameIdx]);
        }
        removeResource(pBufferBlurWeights);

        removeResource(gNullTextureResource);

        removeResource(pSkybox);
        removeTriangleFilteringBuffers();

        exitProfiler();

        exitUserInterface();

        removeFont(gFont);
        exitFontSystem();

        /************************************************************************/
        // Remove loaded scene
        /************************************************************************/
        removeResourcesFromPackage(gPackage);

        removeResource(pSkyboxVertexBuffer);

        tf_free(pFilterContainers);

#if defined(_WINDOWS)
        arrfree(gGuiResolution.mResNameContainer);
        arrfree(gGuiResolution.mResNamePointers);
#endif
        /************************************************************************/
        /************************************************************************/
        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            exitSemaphore(pRenderer, pImageAcquiredSemaphore[i]);
        }
        exitSemaphore(pRenderer, pPresentSemaphore);

        exitGpuCmdRing(pRenderer, &gGraphicsCmdRing);
        exitGpuCmdRing(pRenderer, &gComputeCmdRing);

        exitQueue(pRenderer, pGraphicsQueue);
        exitQueue(pRenderer, pComputeQueue);

        removeSampler(pRenderer, pSamplerTrilinearAniso);
        removeSampler(pRenderer, pSamplerBilinear);
        removeSampler(pRenderer, pSamplerPointClamp);
        removeSampler(pRenderer, pSamplerBilinearClamp);

        TFPipelineCacheSaveDesc saveDesc = {};
        saveDesc.pFileName = pPipelineCacheName;
        savePipelineCache(pRenderer, pPipelineCache, &saveDesc);
        removePipelineCache(pRenderer, pPipelineCache);

        exitVisibilityBuffer(pVisibilityBuffer);
        exitRootSignature(pRenderer);
        exitResourceLoaderInterface(pRenderer);

        exitRenderer(pRenderer);
        exitGPUConfig();
        pRenderer = NULL;
    }

    // Setup the render targets used in this demo.
    // The only render target that is being currently used stores the results of the Visibility Buffer pass.
    // As described earlier, this render target uses 32 bit per pixel to store draw / triangle IDs that will be
    // loaded later by the shade step to reconstruct interpolated triangle data per pixel.
    bool Load(TFReloadDesc* pReloadDesc)
    {
        UNREF_PARAM(pReloadDesc);

        gSceneRes = getGPUCfgSceneResolution(mSettings.mWidth, mSettings.mHeight);

        addShaders();
        addDescriptorSets();

        loadProfilerUI(mSettings.mWidth, mSettings.mHeight);

        gGuiWindowDesc = {};
        gGuiWindowDesc.mStartPos = vec2(mSettings.mWidth * 0.01f, mSettings.mHeight * 0.2f);
        gGuiWindowDesc.mStartSize = vec2(600.0f, 550.0f);
        gGuiWindowDesc.pWindowTitle = GetName();
        gGuiWindowDesc.mFlags =
            TF_UI_WINDOW_TITLE | TF_UI_WINDOW_SCALABLE | TF_UI_WINDOW_MOVABLE | TF_UI_WINDOW_MINIMIZABLE | TF_UI_WINDOW_BORDER;

        TFLuaWidgetVariableDesc luaVarDesc = {};

        /************************************************************************/
        // Most important options
        /************************************************************************/
        luaVarDesc.pLabel = "Enable Small Scale Rasterization";
        luaVarDesc.mWidgetType = TF_WIDGET_TYPE_CHECKBOX;
        luaVarDesc.pBool = &gAppSettings.mSmallScaleRaster;
        luaRegisterWidgetVariable(&luaVarDesc);

        luaVarDesc.pLabel = "Async Compute";
        luaVarDesc.mWidgetType = TF_WIDGET_TYPE_CHECKBOX;
        luaVarDesc.pBool = &gAppSettings.mAsyncCompute;
        luaRegisterWidgetVariable(&luaVarDesc);

#if defined(FORGE_DEBUG)
        luaVarDesc.pLabel = "Visualize Geometry";
        luaVarDesc.mWidgetType = TF_WIDGET_TYPE_CHECKBOX;
        luaVarDesc.pBool = &gAppSettings.mVisualizeGeometry;
        luaRegisterWidgetVariable(&luaVarDesc);

        luaVarDesc.pLabel = "Visualize Bin Triangle Count";
        luaVarDesc.mWidgetType = TF_WIDGET_TYPE_CHECKBOX;
        luaVarDesc.pBool = &gAppSettings.mVisualizeBinTriangleCount;
        luaRegisterWidgetVariable(&luaVarDesc);

        luaVarDesc.pLabel = "Visualize Bin Occupancy";
        luaVarDesc.mWidgetType = TF_WIDGET_TYPE_CHECKBOX;
        luaVarDesc.pBool = &gAppSettings.mVisualizeBinOccupancy;
        luaRegisterWidgetVariable(&luaVarDesc);
#endif

        luaVarDesc.pLabel = "Draw Debug Targets";
        luaVarDesc.mWidgetType = TF_WIDGET_TYPE_CHECKBOX;
        luaVarDesc.pBool = &gAppSettings.mDrawDebugTargets;
        luaRegisterWidgetVariable(&luaVarDesc);

        /************************************************************************/
        /************************************************************************/
        if (pRenderer->pGpu->mHDRSupported)
        {
            luaVarDesc.pLabel = "Output Mode";
            luaVarDesc.mWidgetType = TF_WIDGET_TYPE_DROPDOWN;
            luaVarDesc.pUint = (uint32_t*)&gAppSettings.mOutputMode;
            luaRegisterWidgetVariable(&luaVarDesc);
        }

        luaVarDesc.pLabel = "Cinematic Camera walking";
        luaVarDesc.mWidgetType = TF_WIDGET_TYPE_CHECKBOX;
        luaVarDesc.pBool = &gAppSettings.cameraWalking;
        luaRegisterWidgetVariable(&luaVarDesc);

        luaVarDesc.pLabel = "Cinematic Camera walking: Speed";
        luaVarDesc.mWidgetType = TF_WIDGET_TYPE_SLIDER_FLOAT;
        luaVarDesc.pFloat = &gAppSettings.cameraWalkingSpeed;
        luaRegisterWidgetVariable(&luaVarDesc);

        /************************************************************************/
        // Light Settings
        /************************************************************************/
        // offset max angle for sun control so the light won't bleed with
        // small glancing angles, i.e., when lightDir is almost parallel to the plane

        luaVarDesc.pLabel = "Light Color & Intensity";
        luaVarDesc.mWidgetType = TF_WIDGET_TYPE_SLIDER_FLOAT4;
        luaVarDesc.pFloat4 = &gAppSettings.mLightColor;
        luaRegisterWidgetVariable(&luaVarDesc);

        luaVarDesc.pLabel = "Time of Day";
        luaVarDesc.mWidgetType = TF_WIDGET_TYPE_SLIDER_FLOAT;
        luaVarDesc.pFloat = &gAppSettings.mTimeOfDay;
        luaRegisterWidgetVariable(&luaVarDesc);

        luaVarDesc.pLabel = "Enable Godray";
        luaVarDesc.mWidgetType = TF_WIDGET_TYPE_CHECKBOX;
        luaVarDesc.pBool = &gAppSettings.mEnableGodray;
        luaRegisterWidgetVariable(&luaVarDesc);

        luaVarDesc.pLabel = "God Ray : Sun Size";
        luaVarDesc.mWidgetType = TF_WIDGET_TYPE_SLIDER_FLOAT;
        luaVarDesc.pFloat = &gAppSettings.mSunSize;
        luaRegisterWidgetVariable(&luaVarDesc);

        luaVarDesc.pLabel = "God Ray: Scatter Factor";
        luaVarDesc.mWidgetType = TF_WIDGET_TYPE_SLIDER_FLOAT;
        luaVarDesc.pFloat = &gGodRayConstant.mScatterFactor;
        luaRegisterWidgetVariable(&luaVarDesc);

        luaVarDesc.pLabel = "God Ray : Gaussian Blur Kernel Size";
        luaVarDesc.mWidgetType = TF_WIDGET_TYPE_SLIDER_UINT;
        luaVarDesc.pUint = &gAppSettings.mFilterRadius;
        luaRegisterWidgetVariable(&luaVarDesc);

        luaVarDesc.pLabel = "God Ray : Gaussian Blur Sigma";
        luaVarDesc.mWidgetType = TF_WIDGET_TYPE_SLIDER_FLOAT;
        luaVarDesc.pFloat = &gGaussianBlurSigma[0];
        luaRegisterWidgetVariable(&luaVarDesc);

        luaVarDesc.pLabel = "Enable Random Point Lights";
        luaVarDesc.mWidgetType = TF_WIDGET_TYPE_CHECKBOX;
        luaVarDesc.pBool = &gAppSettings.mRenderLocalLights;
        luaRegisterWidgetVariable(&luaVarDesc);
        /************************************************************************/
        // Display Settings
        /************************************************************************/
        luaVarDesc.pLabel = "Display Color Range";
        luaVarDesc.mWidgetType = TF_WIDGET_TYPE_DROPDOWN;
        luaVarDesc.pUint = (uint32_t*)&gAppSettings.mDisplayColorRange;
        luaRegisterWidgetVariable(&luaVarDesc);

        luaVarDesc.pLabel = "Display Signal Range";
        luaVarDesc.mWidgetType = TF_WIDGET_TYPE_DROPDOWN;
        luaVarDesc.pUint = (uint32_t*)&gAppSettings.mDisplaySignalRange;
        luaRegisterWidgetVariable(&luaVarDesc);

        luaVarDesc.pLabel = "Display Color Space";
        luaVarDesc.mWidgetType = TF_WIDGET_TYPE_DROPDOWN;
        luaVarDesc.pUint = (uint32_t*)&gAppSettings.mCurrentSwapChainColorSpace;
        luaRegisterWidgetVariable(&luaVarDesc);

        /************************************************************************/
        // AO Settings
        /************************************************************************/
        luaVarDesc.pLabel = "Enable AO";
        luaVarDesc.mWidgetType = TF_WIDGET_TYPE_CHECKBOX;
        luaVarDesc.pBool = &gAppSettings.mEnableAO;
        luaRegisterWidgetVariable(&luaVarDesc);

        luaVarDesc.pLabel = "Visualize AO";
        luaVarDesc.mWidgetType = TF_WIDGET_TYPE_CHECKBOX;
        luaVarDesc.pBool = &gAppSettings.mVisualizeAO;
        luaRegisterWidgetVariable(&luaVarDesc);

        luaVarDesc.pLabel = "AO Intensity";
        luaVarDesc.mWidgetType = TF_WIDGET_TYPE_SLIDER_FLOAT;
        luaVarDesc.pFloat = &gAppSettings.mAOIntensity;
        luaRegisterWidgetVariable(&luaVarDesc);

        luaVarDesc.pLabel = "AO Quality";
        luaVarDesc.mWidgetType = TF_WIDGET_TYPE_SLIDER_INT;
        luaVarDesc.pInt = &gAppSettings.mAOQuality;
        luaRegisterWidgetVariable(&luaVarDesc);

        luaVarDesc.pLabel = "Linear Scale";
        luaVarDesc.mWidgetType = TF_WIDGET_TYPE_SLIDER_FLOAT;
        luaVarDesc.pFloat = &gAppSettings.LinearScale;
        luaRegisterWidgetVariable(&luaVarDesc);

        if (!addSwapChain())
            return false;

        addRenderTargets();
        // Debug Texture UI Window
        {
            gDebugTexturesWindowDesc = {};
            gDebugTexturesWindowDesc.mStartSize = { gSceneRes.mWidth / 2.0f, gSceneRes.mHeight / 4.0f };
            gDebugTexturesWindowDesc.mStartPos.x = 20.0f;
            gDebugTexturesWindowDesc.mStartPos.y = gSceneRes.mHeight - gDebugTexturesWindowDesc.mStartSize.y - 20.f;
            gDebugTexturesWindowDesc.pWindowTitle = "DEBUG RTs";
            gDebugTexturesWindowDesc.mFlags =
                TF_UI_WINDOW_TITLE | TF_UI_WINDOW_SCALABLE | TF_UI_WINDOW_MOVABLE | TF_UI_WINDOW_MINIMIZABLE | TF_UI_WINDOW_BORDER;
        }

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

        waitQueueIdle(pGraphicsQueue);
        waitQueueIdle(pComputeQueue);

        unloadFontSystem();
        unloadUserInterface();

        removePipelines();

        removeRenderTargets();

        removeSwapChain(pRenderer, pSwapChain);
        bdestroy(&gOutputSupportsHDRText);
        unloadProfilerUI();

#if defined(XBOX)
        esramResetAllocations(pRenderer->mDx.pESRAMManager);
#endif

        removeDescriptorSets();
        removeShaders();
    }

    void updateGui()
    {
        uiUpdateWindowVisibilityByTitle(gDebugTexturesWindowDesc.pWindowTitle, gAppSettings.mDrawDebugTargets);

        if (UI_WINDOW_IS_VISIBLE(uiBeginWidgetWindow(&gDebugTexturesWindowDesc)))
        {
            static const TFTexture* textures[3];
            uint32_t                texturesCount = 0;
            textures[texturesCount++] = pDepthBuffer->pTexture;
            textures[texturesCount++] = pRenderTargetShadow->pTexture;
            textures[texturesCount++] = pIntermediateRenderTarget->pTexture;

            uiLayoutSpaceBegin(TF_LAYOUT_STATIC, 0, texturesCount);
            vec2  windowSize = uiGetWindowSize();
            float aspect = (float)gSceneRes.mWidth / gSceneRes.mHeight;
            for (uint32_t i = 0; i < texturesCount; i++)
            {
                float imageWidth = (windowSize.x - uiLayoutGetPadding().x * 2 - 2) / texturesCount;
                float imageHeight = imageWidth / aspect;
                uiLayoutSpacePush(vec2(i * imageWidth, 0), vec2(imageWidth, imageHeight));
                uiDebugTexture(textures[i], float2(0.0f, 0.0f));
            }
            uiLayoutSpaceEnd();

            // Disable UI caching to update debug textures
            uiForceReferesh();
        }
        uiEndWidgetWindow();

        if (UI_WINDOW_IS_VISIBLE(uiBeginWidgetWindow(&gGuiWindowDesc)))
        {
            uiLayoutAutoRows(1);
            uiCheckbox("Enable Small Scale Rasterization", &gAppSettings.mSmallScaleRaster);
            uiCheckbox("Separate Alpha Testing Rasterization", &gAppSettings.mSeparateAlphaTestRasterization);
            uiCheckbox("Async Compute", &gAppSettings.mAsyncCompute);
#if defined(FORGE_DEBUG)
            uiCheckbox("Visualize Geometry", &gAppSettings.mVisualizeGeometry);
            uiCheckbox("Visualize Bin Triangle Count", &gAppSettings.mVisualizeBinTriangleCount);
            uiCheckbox("Visualize Bin Occupancy", &gAppSettings.mVisualizeBinOccupancy);
#endif
            uiCheckbox("Draw Debug Targets", &gAppSettings.mDrawDebugTargets);

            if (pRenderer->pGpu->mHDRSupported)
            {
                uiText(&gOutputSupportsHDRText, TF_ALIGN_LEFT);

                uiLayoutAutoRows(2);
                static const char* outputModeNames[] = { "SDR", "HDR10" };
                uiLabel("Output Mode", TF_ALIGN_LEFT);
                gAppSettings.mOutputMode = (OutputMode)UI_WIDGET_GET_SELECTED(
                    uiDropdown(outputModeNames, sizeof(outputModeNames) / sizeof(outputModeNames[0]), gAppSettings.mOutputMode));
                uiLayoutAutoRows(1);
            }

            uiCheckbox("Cinematic Camera walking", &gAppSettings.cameraWalking);
            uiLayoutAutoRows(2);
            uiLabel("Cinematic Camera walking: Speed", TF_ALIGN_LEFT);
            uiSliderFloat(&gAppSettings.cameraWalkingSpeed, 0.0f, 3.0f, 0.1f);

            uiLayoutAutoRows(2);
            uiLabel("Time of Day", TF_ALIGN_LEFT);
            uiSliderFloat(&gAppSettings.mTimeOfDay, gAppSettings.mTimeOfDaySunrise, gAppSettings.mTimeOfDaySunset, 0.1f);

            uiLayoutAutoRows(5);
            uiLabel("Light Color & Intensity", TF_ALIGN_LEFT);
            uiSliderFloat4(&gAppSettings.mLightColor, float4(0.0f), float4(30.0f), float4(0.01f));

            uiLayoutAutoRows(1);
            uiCheckbox("Enable Godray", &gAppSettings.mEnableGodray);

            if (gAppSettings.mEnableGodray)
            {
                uiLayoutAutoRows(2);
                uiLabel("God Ray : Sun Size", TF_ALIGN_LEFT);
                uiSliderFloat(&gAppSettings.mSunSize, 1.0f, 1000.0f, 0.1f);
                uiLabel("God Ray: Scatter Factor", TF_ALIGN_LEFT);
                uiSliderFloat(&gGodRayConstant.mScatterFactor, 0.0f, 1.0f, 0.1f);
                uiLabel("God Ray : Gaussian Blur Kernel Size", TF_ALIGN_LEFT);
                uiSliderUint(&gAppSettings.mFilterRadius, 1u, 8u, 1u);
                uiLabel("God Ray : Gaussian Blur Sigma", TF_ALIGN_LEFT);
                uiSliderFloat(&gGaussianBlurSigma[0], 0.1f, 5.0f, 0.1f);
                uiLayoutAutoRows(1);
            }

            uiCheckbox("Enable Random Point Lights", &gAppSettings.mRenderLocalLights);

            uiLayoutAutoRows(2);
            static const char* displayColorRangeNames[] = { "RGB" };
            uiLabel("Display Color Range", TF_ALIGN_LEFT);
            gAppSettings.mDisplayColorRange = (DisplayColorRange)UI_WIDGET_GET_SELECTED(
                uiDropdown(displayColorRangeNames, sizeof(displayColorRangeNames) / sizeof(displayColorRangeNames[0]),
                           gAppSettings.mDisplayColorRange));

            static const char* displaySignalRangeNames[] = { "Range Full", "Range Limited" };
            uiLabel("Display Signal Range", TF_ALIGN_LEFT);
            gAppSettings.mDisplaySignalRange = (DisplaySignalRange)UI_WIDGET_GET_SELECTED(
                uiDropdown(displaySignalRangeNames, sizeof(displaySignalRangeNames) / sizeof(displaySignalRangeNames[0]),
                           gAppSettings.mDisplaySignalRange));

            static const char* displayColorSpaceNames[] = { "ColorSpace Rec709", "ColorSpace Rec2020", "ColorSpace P3D65" };
            uiLabel("Display Color Space", TF_ALIGN_LEFT);
            gAppSettings.mCurrentSwapChainColorSpace = (DisplayColorSpace)UI_WIDGET_GET_SELECTED(
                uiDropdown(displayColorSpaceNames, sizeof(displayColorSpaceNames) / sizeof(displayColorSpaceNames[0]),
                           gAppSettings.mCurrentSwapChainColorSpace));

            uiLayoutAutoRows(1);
            uiCheckbox("Enable AO", &gAppSettings.mEnableAO);

            if (gAppSettings.mEnableAO)
            {
                uiCheckbox("Visualize AO", &gAppSettings.mVisualizeAO);
                uiLayoutAutoRows(2);
                uiLabel("AO Intensity", TF_ALIGN_LEFT);
                uiSliderFloat(&gAppSettings.mAOIntensity, 0.0f, 10.0f, 0.1f);
                uiLabel("AO Quality", TF_ALIGN_LEFT);
                uiSliderInt(&gAppSettings.mAOQuality, 1, 4, 1);
                uiLayoutAutoRows(1);
            }

            if (gAppSettings.mOutputMode != OutputMode::OUTPUT_MODE_SDR)
            {
                uiLayoutAutoRows(2);
                uiLabel("Linear Scale", TF_ALIGN_LEFT);
                uiSliderFloat(&gAppSettings.LinearScale, 80.0f, 400.0f, 0.1f);
            }

            uiLayoutAutoRows(2);
            uiLabel("Camera Preset", TF_ALIGN_LEFT);
            int previousSelected = gAppSettings.mSelectedCameraPreset;
            gAppSettings.mSelectedCameraPreset =
                UI_WIDGET_GET_SELECTED(uiDropdown(gAppSettings.mCameraPresetNames, sizeof(gAppSettings.mCameraPresetNames) / sizeof(char*),
                                                  gAppSettings.mSelectedCameraPreset));
            if (gAppSettings.mSelectedCameraPreset != previousSelected)
            {
                vec3 position;
                vec2 rotation;

                switch (gAppSettings.mSelectedCameraPreset)
                {
                case 1:
                {
                    position = vec3(-86.435646f, 686.432251f, 461.439667f);
                    rotation = vec2(0.642809f, -4.070758f);
                    break;
                }
                case 2:
                {
                    position = vec3(235.882187f, 252.093842f, -73.581871f);
                    rotation = vec2(-0.167931f, -5.134955f);
                    break;
                }
                case 3:
                {
                    position = vec3(37.862328f, 1179.324585f, 98.746460f);
                    rotation = vec2(1.239569f, -6.292443f);
                    break;
                }
                default:
                {
                    position = vec3(620.0f, 490.0f, 70.0f);
                    vec3 lookAt = position + vec3(-1.0f - 0.0f, 0.1f, 0.0f);
                    pCamera->moveTo(position);
                    pCamera->lookAt(lookAt);
                    rotation = pCamera->getRotationXY();
                    break;
                }
                }
                pCamera->moveTo(position);
                pCamera->setViewRotationXY(rotation);
            }
        }
        uiEndWidgetWindow();
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

        if (gWasColorSpace != gAppSettings.mCurrentSwapChainColorSpace || gWasDisplayColorRange != gAppSettings.mDisplayColorRange ||
            gWasDisplaySignalRange != gAppSettings.mDisplaySignalRange)
        {
            gWasColorSpace = gAppSettings.mCurrentSwapChainColorSpace;
            gWasDisplayColorRange = gAppSettings.mDisplayColorRange;
            gWasDisplaySignalRange = gAppSettings.mDisplaySignalRange;
        }

        pCamera->update(deltaTime);

        // Camera Walking Update

        if (gAppSettings.cameraWalking)
        {
            if (gTotalElpasedTime - (0.033333f * gAppSettings.cameraWalkingSpeed) <= gCameraWalkingTime)
            {
                gCameraWalkingTime = 0.0f;
            }

            gCameraWalkingTime += deltaTime * gAppSettings.cameraWalkingSpeed;

            uint  currentCameraFrame = (uint)(gCameraWalkingTime / 0.00833f);
            float remind = gCameraWalkingTime - (float)currentCameraFrame * 0.00833f;

            float3 newPos = (lerp((gCameraPathData[2 * currentCameraFrame]), (gCameraPathData[2 * (currentCameraFrame + 1)]), remind));
            pCamera->moveTo((newPos)*SCENE_SCALE);

            float3 newLookat =
                (lerp((gCameraPathData[2 * currentCameraFrame + 1]), (gCameraPathData[2 * (currentCameraFrame + 1) + 1]), remind));
            pCamera->lookAt((newLookat)*SCENE_SCALE);
        }

        updateDynamicUIElements();

        updateUniformData(mSettings.mFrameIdx);

        if (gGaussianBlurSigma[1] != gGaussianBlurSigma[0])
        {
            gGaussianBlurSigma[1] = gGaussianBlurSigma[0];
            for (int i = 0; i < MAX_BLUR_KERNEL_SIZE; i++)
            {
                gBlurWeightsUniform.mBlurWeights[i] = gaussian((float)i, gGaussianBlurSigma[0]);
            }
        }
    }

    void Draw()
    {
        if (pSwapChain->mEnableVsync != (uint32_t)mSettings.mVSyncEnabled)
        {
            waitQueueIdle(pGraphicsQueue);
            ::toggleVSync(pRenderer, &pSwapChain);
        }

        uint32_t presentIndex = 0;
        uint32_t frameIdx = mSettings.mFrameIdx;
        bool     useDedicatedComputeQueue = gAppSettings.mAsyncCompute;

        /************************************************************************/
        // Async compute pass
        /************************************************************************/
        if (useDedicatedComputeQueue)
        {
            GpuCmdRingElement computeElem = getNextGpuCmdRingElement(&gComputeCmdRing, true, 1);

            // Check to see if we can use the cmd buffer
            TFFenceStatus fenceStatus;
            getFenceStatus(pRenderer, computeElem.pFence, &fenceStatus);
            if (fenceStatus == TF_FENCE_STATUS_INCOMPLETE)
                waitForFences(pRenderer, 1, &computeElem.pFence);
            /************************************************************************/
            // Update uniform buffer to gpu
            /************************************************************************/
            TFBufferUpdateDesc update = { pPerFrameVBUniformBuffers[VB_UB_COMPUTE][frameIdx] };
            beginUpdateResource(&update);
            memcpy(update.pMappedData, &gPerFrame[frameIdx].gVBViewUniformData, sizeof(gPerFrame[frameIdx].gVBViewUniformData));
            endUpdateResource(&update);

            updateRenderTargetInfo(frameIdx);
            /************************************************************************/
            // Triangle filtering async compute pass
            /************************************************************************/
            TFCmd* computeCmd = computeElem.pCmds[0];

            resetCmdPool(pRenderer, computeElem.pCmdPool);
            beginCmd(computeCmd);
            cmdBindDescriptorSet(computeCmd, 0, pDescriptorSetPersistent);
            cmdBindDescriptorSet(computeCmd, frameIdx, pDescriptorSetPerFrame);
            cmdBeginGpuFrameProfile(computeCmd, gComputeProfileToken);

            // Clear VB pass
            {
                // Tiny triangles get emitted directly from filtering, so the visibility buffer must be cleared before filtering
                cmdBeginGpuTimestampQuery(computeCmd, gComputeProfileToken, "Clear Visibility Buffer");
                cmdBindPipeline(computeCmd, pPipelineClearRenderTarget);
                cmdBindDescriptorSet(computeCmd, frameIdx, pDescriptorSetTriangleFilteringPerDraw);
                cmdBindDescriptorSet(computeCmd, frameIdx * 3 + 2, pDescriptorSetRenderTargetPerBatch);
                cmdDispatch(computeCmd, gClearRenderTargetInfo.width / 256 + 1, 1, 1);
#ifdef METAL // is this necessary?
                cmdResourceBarrier(computeCmd, 1, nullptr, 0, nullptr, 0, nullptr);
#endif
                cmdEndGpuTimestampQuery(computeCmd, gComputeProfileToken);
            }

            // Triangle Filtering pass
            {
                TriangleFilteringPassDesc triangleFilteringDesc = {};
                triangleFilteringDesc.pFilterContainers = pFilterContainers;
                triangleFilteringDesc.mNumContainers = (uint32_t)gMeshCount;

                triangleFilteringDesc.mNumCullingViewports = NUM_CULLING_VIEWPORTS;
                triangleFilteringDesc.pViewportObjectSpace = gPerFrame[frameIdx].gEyeObjectSpace;

                triangleFilteringDesc.pPipelineClearBuffers = pPipelineClearBuffers;
                triangleFilteringDesc.pPipelineTriangleFiltering = pPipelineTriangleFiltering;

                triangleFilteringDesc.pDescriptorSetTriangleFilteringPerBatch = pDescriptorSetTriangleFilteringPerBatch;
                triangleFilteringDesc.pDescriptorSetTriangleFilteringPerDraw = pDescriptorSetTriangleFilteringPerDrawCompute;
                triangleFilteringDesc.mFrameIndex = frameIdx;
                triangleFilteringDesc.mBuffersIndex = frameIdx;
                triangleFilteringDesc.mGpuProfileToken = gComputeProfileToken;
                triangleFilteringDesc.mClearThreadCount = CLEAR_THREAD_COUNT;
                triangleFilteringDesc.mTriangleFilteringBatchIndex =
                    SRT_RES_IDX(TriangleFilteringCompSrtData, PerBatch, gTriangleFilteringBatchData);
                FilteringStats stats = cmdVisibilityBufferTriangleFilteringPass(pVisibilityBuffer, computeCmd, &triangleFilteringDesc);

                gPerFrame[frameIdx].gDrawCount[GEOMSET_OPAQUE] = stats.mGeomsetDrawCounts[GEOMSET_OPAQUE];
                gPerFrame[frameIdx].gDrawCount[GEOMSET_ALPHA_CUTOUT] = stats.mGeomsetDrawCounts[GEOMSET_ALPHA_CUTOUT];
                gPerFrame[frameIdx].gTotalDrawCount = stats.mTotalDrawCount;
            }

            // Clear Light Clusters pass
            {
                cmdBeginGpuTimestampQuery(computeCmd, gComputeProfileToken, "Clear Light Clusters");
                cmdBindPipeline(computeCmd, pPipelineClearLightClusters);
                cmdBindDescriptorSet(computeCmd, frameIdx, pDescriptorSetClusterLights);
                cmdDispatch(computeCmd, 1, 1, 1);
                cmdEndGpuTimestampQuery(computeCmd, gComputeProfileToken);
            }

            // Compute Light Clusters pass
            if (gAppSettings.mRenderLocalLights)
            {
                cmdBeginGpuTimestampQuery(computeCmd, gComputeProfileToken, "Compute Light Clusters");

                // Resource Transition
                {
                    TFBufferBarrier barriers[] = { { pLightClustersCount[frameIdx], TF_RESOURCE_STATE_UNORDERED_ACCESS,
                                                     TF_RESOURCE_STATE_UNORDERED_ACCESS } };
                    cmdResourceBarrier(computeCmd, 1, barriers, 0, NULL, 0, NULL);
                }
                // Dispatch
                {
                    cmdBindPipeline(computeCmd, pPipelineClusterLights);
                    cmdBindDescriptorSet(computeCmd, frameIdx, pDescriptorSetClusterLights);
                    cmdDispatch(computeCmd, LIGHT_COUNT, 1, 1);
                }

                cmdEndGpuTimestampQuery(computeCmd, gComputeProfileToken);
            }

            cmdEndGpuFrameProfile(computeCmd, gComputeProfileToken);
            endCmd(computeCmd);

            FlushResourceUpdateDesc flushUpdateDesc = {};
            flushUpdateDesc.mNodeIndex = 0;
            flushResourceUpdates(&flushUpdateDesc);
            TFSemaphore* waitSemaphores[] = { flushUpdateDesc.pOutSubmittedSemaphore,
                                              mSettings.mFrames > 1 ? gPrevGraphicsSemaphore : NULL };

            TFQueueSubmitDesc submitDesc = {};
            submitDesc.mCmdCount = 1;
            submitDesc.mSignalSemaphoreCount = 1;
            submitDesc.mWaitSemaphoreCount = waitSemaphores[1] ? TF_ARRAY_COUNT(waitSemaphores) : 1;
            submitDesc.ppCmds = &computeCmd;
            submitDesc.ppSignalSemaphores = &computeElem.pSemaphore;
            submitDesc.ppWaitSemaphores = waitSemaphores;
            submitDesc.pSignalFence = computeElem.pFence;
            submitDesc.mSubmitDone = (mSettings.mFrames < 1);
            queueSubmit(pComputeQueue, &submitDesc);

            gComputeSemaphores[frameIdx] = computeElem.pSemaphore;
        }

        /************************************************************************/
        // Graphics pass
        /************************************************************************/
        // Skip the first frame since draw will always be one frame behind compute
        if (!useDedicatedComputeQueue || mSettings.mFrames > 0)
        {
            frameIdx = useDedicatedComputeQueue ? ((mSettings.mFrames - 1) % mSettings.mFrameMaxCount) : frameIdx;

            GpuCmdRingElement graphicsElem = getNextGpuCmdRingElement(&gGraphicsCmdRing, true, 1);

            // Check to see if we can use the cmd buffer
            TFFenceStatus fenceStatus;
            getFenceStatus(pRenderer, graphicsElem.pFence, &fenceStatus);
            if (fenceStatus == TF_FENCE_STATUS_INCOMPLETE)
                waitForFences(pRenderer, 1, &graphicsElem.pFence);

            pScreenRenderTarget = pIntermediateRenderTarget;

            /************************************************************************/
            // Update uniform buffers
            /************************************************************************/
            if (!useDedicatedComputeQueue)
            {
                TFBufferUpdateDesc update = { pPerFrameVBUniformBuffers[VB_UB_COMPUTE][frameIdx] };
                beginUpdateResource(&update);
                memcpy(update.pMappedData, &gPerFrame[frameIdx].gVBViewUniformData, sizeof(gPerFrame[frameIdx].gVBViewUniformData));
                endUpdateResource(&update);

                updateRenderTargetInfo(frameIdx);
            }

            TFBufferUpdateDesc update = { pPerFrameVBUniformBuffers[VB_UB_GRAPHICS][frameIdx] };
            beginUpdateResource(&update);
            memcpy(update.pMappedData, &gPerFrame[frameIdx].gVBViewUniformData, sizeof(gPerFrame[frameIdx].gVBViewUniformData));
            endUpdateResource(&update);

            update = { pPerFrameUniformBuffers[frameIdx] };
            beginUpdateResource(&update);
            memcpy(update.pMappedData, &gPerFrame[frameIdx].gPerFrameUniformData, sizeof(gPerFrame[frameIdx].gPerFrameUniformData));
            endUpdateResource(&update);

            update = { pUniformBufferSky[frameIdx] };
            beginUpdateResource(&update);
            memcpy(update.pMappedData, &gPerFrame[frameIdx].gUniformDataSky, sizeof(gPerFrame[frameIdx].gUniformDataSky));
            endUpdateResource(&update);
            /************************************************************************/

            // Get command list to store rendering commands for this frame
            TFCmd* graphicsCmd = graphicsElem.pCmds[0];
            // Submit all render commands for this frame
            resetCmdPool(pRenderer, graphicsElem.pCmdPool);
            beginCmd(graphicsCmd);
            cmdBindDescriptorSet(graphicsCmd, 0, pDescriptorSetPersistent);
            cmdBindDescriptorSet(graphicsCmd, frameIdx, pDescriptorSetPerFrame);

            cmdBeginGpuFrameProfile(graphicsCmd, gGraphicsProfileToken);

            if (!useDedicatedComputeQueue)
            {
                // Clear VB pass
                {
                    cmdBeginGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken, "Clear Visibility Buffer");
                    cmdBindPipeline(graphicsCmd, pPipelineClearRenderTarget);
                    cmdBindDescriptorSet(graphicsCmd, frameIdx, pDescriptorSetTriangleFilteringPerDraw);
                    cmdBindDescriptorSet(graphicsCmd, frameIdx * 3 + 2, pDescriptorSetRenderTargetPerBatch);
                    cmdDispatch(graphicsCmd, gClearRenderTargetInfo.width / 256 + 1, 1, 1);
#ifdef METAL // is this necessary?
                    cmdResourceBarrier(graphicsCmd, 1, nullptr, 0, nullptr, 0, nullptr);
#endif
                    cmdEndGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken);
                }

                TriangleFilteringPassDesc triangleFilteringDesc = {};
                triangleFilteringDesc.pFilterContainers = pFilterContainers;
                triangleFilteringDesc.mNumContainers = (uint32_t)gMeshCount;

                triangleFilteringDesc.mNumCullingViewports = NUM_CULLING_VIEWPORTS;
                triangleFilteringDesc.pViewportObjectSpace = gPerFrame[frameIdx].gEyeObjectSpace;

                triangleFilteringDesc.pPipelineClearBuffers = pPipelineClearBuffers;
                triangleFilteringDesc.pPipelineTriangleFiltering = pPipelineTriangleFiltering;

                triangleFilteringDesc.pDescriptorSetTriangleFilteringPerBatch = pDescriptorSetTriangleFilteringPerBatch;
                triangleFilteringDesc.pDescriptorSetTriangleFilteringPerDraw = pDescriptorSetTriangleFilteringPerDraw;
                triangleFilteringDesc.mFrameIndex = frameIdx;
                triangleFilteringDesc.mBuffersIndex = frameIdx;
                triangleFilteringDesc.mGpuProfileToken = gGraphicsProfileToken;
                triangleFilteringDesc.mClearThreadCount = CLEAR_THREAD_COUNT;
                FilteringStats stats = cmdVisibilityBufferTriangleFilteringPass(pVisibilityBuffer, graphicsCmd, &triangleFilteringDesc);

                gPerFrame[frameIdx].gDrawCount[GEOMSET_OPAQUE] = stats.mGeomsetDrawCounts[GEOMSET_OPAQUE];
                gPerFrame[frameIdx].gDrawCount[GEOMSET_ALPHA_CUTOUT] = stats.mGeomsetDrawCounts[GEOMSET_ALPHA_CUTOUT];
                gPerFrame[frameIdx].gTotalDrawCount = stats.mTotalDrawCount;
            }

            // Clear Light Clusters pass
            if (!gAppSettings.mAsyncCompute)
            {
                cmdBeginGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken, "Clear Light Clusters");
                cmdBindPipeline(graphicsCmd, pPipelineClearLightClusters);
                cmdBindDescriptorSet(graphicsCmd, frameIdx, pDescriptorSetClusterLights);
                cmdDispatch(graphicsCmd, 1, 1, 1);
                cmdEndGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken);
            }

            // Compute Light Clusters pass
            if (!useDedicatedComputeQueue && gAppSettings.mRenderLocalLights)
            {
                cmdBeginGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken, "Compute Light Clusters");

                // Resource Transition
                {
                    TFBufferBarrier barriers[] = { { pLightClustersCount[frameIdx], TF_RESOURCE_STATE_UNORDERED_ACCESS,
                                                     TF_RESOURCE_STATE_UNORDERED_ACCESS } };
                    cmdResourceBarrier(graphicsCmd, 1, barriers, 0, NULL, 0, NULL);
                }
                // Dispatch
                {
                    cmdBindPipeline(graphicsCmd, pPipelineClusterLights);
                    cmdBindDescriptorSet(graphicsCmd, frameIdx, pDescriptorSetClusterLights);
                    cmdDispatch(graphicsCmd, LIGHT_COUNT, 1, 1);
                }
                // Resource Transition
                {
                    TFBufferBarrier barriers[] = { { pLightClusters[frameIdx], TF_RESOURCE_STATE_UNORDERED_ACCESS,
                                                     TF_RESOURCE_STATE_UNORDERED_ACCESS } };
                    cmdResourceBarrier(graphicsCmd, 1, barriers, 0, NULL, 0, NULL);
                }

                cmdEndGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken);
            }

            // Transition draw inputs and outputs into their graphics pass states
            {
                uint32_t              rtBarriersCount = 1;
                TFRenderTargetBarrier rtBarriers[] = {
                    { pScreenRenderTarget, TF_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, TF_RESOURCE_STATE_RENDER_TARGET },
                };

                const uint32_t  maxNumBarriers = NUM_CULLING_VIEWPORTS * 2 + 4;
                uint32_t        barrierCount = 0;
                TFBufferBarrier barriers[maxNumBarriers] = {};
                barriers[barrierCount++] = { pLightClusters[frameIdx], TF_RESOURCE_STATE_UNORDERED_ACCESS,
                                             TF_RESOURCE_STATE_SHADER_RESOURCE };
                barriers[barrierCount++] = { pLightClustersCount[frameIdx], TF_RESOURCE_STATE_UNORDERED_ACCESS,
                                             TF_RESOURCE_STATE_SHADER_RESOURCE };

                barriers[barrierCount++] = { pVisibilityBuffer->ppBinBuffer[frameIdx], TF_RESOURCE_STATE_UNORDERED_ACCESS,
                                             TF_RESOURCE_STATE_SHADER_RESOURCE };

                cmdResourceBarrier(graphicsCmd, barrierCount, barriers, 0, NULL, rtBarriersCount, rtBarriers);
            }

            /************************************************************************/
            // Frame render passes
            /************************************************************************/

            // VB Bin passes
            {
                cmdBeginGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken, "VB Bin Rasterizer");
                TFBufferBarrier bufferBarrier = { pVBDepthBuffer[frameIdx], TF_RESOURCE_STATE_UNORDERED_ACCESS,
                                                  TF_RESOURCE_STATE_UNORDERED_ACCESS };

                // VB Bin Shadow pass
                {
                    cmdBeginGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken, "VB Bin Rasterizer Shadow");

                    cmdResourceBarrier(graphicsCmd, 1, &bufferBarrier, 0, NULL, 0, NULL);

                    // Dispatch
                    {
                        const uint32_t threadSize = 128;
                        if (gAppSettings.mSeparateAlphaTestRasterization)
                        {
                            // Only Alpha pass
                            {
                                cmdBindPipeline(graphicsCmd, pPipelineVisibilityBufferDepthRasterSeparateAlpha[1]);
                                cmdBindDescriptorSet(graphicsCmd, frameIdx, pDescriptorSetTriangleFilteringPerDraw);
                                cmdBindDescriptorSet(graphicsCmd, frameIdx * 3, pDescriptorSetRenderTargetPerBatch);
                                cmdDispatch(graphicsCmd, (gShadowMapSize + threadSize - 1) / threadSize,
                                            (gShadowMapSize + threadSize - 1) / threadSize, 8);
                            }
                            // No Alpha pass
                            {
                                cmdBindPipeline(graphicsCmd, pPipelineVisibilityBufferDepthRasterSeparateAlpha[0]);
                                cmdBindDescriptorSet(graphicsCmd, frameIdx, pDescriptorSetTriangleFilteringPerDraw);
                                cmdBindDescriptorSet(graphicsCmd, frameIdx * 3, pDescriptorSetRenderTargetPerBatch);
                                cmdDispatch(graphicsCmd, (gShadowMapSize + threadSize - 1) / threadSize,
                                            (gShadowMapSize + threadSize - 1) / threadSize, 8);
                            }
#ifdef METAL
                            // Force a barrier to anchor the small triangle dispatch to the frame // TODO: check if still necessary
                            cmdResourceBarrier(graphicsCmd, 1, NULL, 0, NULL, 0, NULL);
#endif
                        }
                        else
                        {
                            cmdBindPipeline(graphicsCmd, pPipelineVisibilityBufferDepthRaster);
                            cmdBindDescriptorSet(graphicsCmd, frameIdx, pDescriptorSetTriangleFilteringPerDraw);
                            cmdBindDescriptorSet(graphicsCmd, frameIdx * 3, pDescriptorSetRenderTargetPerBatch);
                            cmdDispatch(graphicsCmd, (gShadowMapSize + threadSize - 1) / threadSize,
                                        (gShadowMapSize + threadSize - 1) / threadSize, 8);
#ifdef METAL
                            // Force a barrier to anchor the small triangle dispatch to the frame // TODO: check if still necessary
                            cmdResourceBarrier(graphicsCmd, 1, NULL, 0, NULL, 0, NULL);
#endif
                        }
                    }

                    cmdEndGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken);
                }
                // VB Bin Screen pass
                {
                    // Render the scene to perform the Visibility Buffer pass. In this pass the (filtered) scene geometry is rendered
                    // into a 32-bit per pixel render target. This contains triangle information (batch Id and triangle Id) that allows
                    // to reconstruct all triangle attributes per pixel. This is faster than a typical Deferred Shading pass, because
                    // less memory bandwidth is used.
                    cmdBeginGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken, "VB Bin Rasterizer Screen");

                    // Dispatch
                    {
                        const uint32_t binRasterThreadsZCount = 64;
                        if (gAppSettings.mSeparateAlphaTestRasterization)
                        {
                            // Only Alpha pass
                            {
                                cmdBindPipeline(graphicsCmd, pPipelineVisibilityBufferDepthRasterSeparateAlpha[1]);
                                cmdBindDescriptorSet(graphicsCmd, frameIdx * 3 + 1, pDescriptorSetRenderTargetPerBatch);
                                cmdDispatch(graphicsCmd, (gDepthRenderTargetInfo.width + BIN_SIZE - 1) / BIN_SIZE,
                                            (gDepthRenderTargetInfo.height + BIN_SIZE - 1) / BIN_SIZE, binRasterThreadsZCount);
                            }
                            // No Alpha pass
                            {
                                cmdBindPipeline(graphicsCmd, pPipelineVisibilityBufferDepthRasterSeparateAlpha[0]);
                                cmdBindDescriptorSet(graphicsCmd, frameIdx * 3 + 1, pDescriptorSetRenderTargetPerBatch);
                                cmdDispatch(graphicsCmd, (gDepthRenderTargetInfo.width + BIN_SIZE - 1) / BIN_SIZE,
                                            (gDepthRenderTargetInfo.height + BIN_SIZE - 1) / BIN_SIZE, binRasterThreadsZCount);
                            }
                        }
                        else
                        {
                            cmdBindPipeline(graphicsCmd, pPipelineVisibilityBufferDepthRaster);
                            cmdBindDescriptorSet(graphicsCmd, frameIdx * 3 + 1, pDescriptorSetRenderTargetPerBatch);
                            cmdDispatch(graphicsCmd, (gDepthRenderTargetInfo.width + BIN_SIZE - 1) / BIN_SIZE,
                                        (gDepthRenderTargetInfo.height + BIN_SIZE - 1) / BIN_SIZE, binRasterThreadsZCount);
                        }
                    }

                    cmdResourceBarrier(graphicsCmd, 1, &bufferBarrier, 0, NULL, 0, NULL);
                    cmdEndGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken);
                }
                cmdEndGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken);
            }
            // VB Blit passes
            {
                cmdBeginGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken, "VB Blits");
                // VB Blit Shadow pass
                {
                    cmdBeginGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken, "VB Blits Shadow");
                    blitVisibilityBufferDepthPass(graphicsCmd, 0, frameIdx, pRenderTargetShadow);
                    cmdEndGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken);
                }
                // VB Blit Screen pass
                {
                    cmdBeginGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken, "VB Blits Screen");
                    blitVisibilityBufferDepthPass(graphicsCmd, 1, frameIdx, pDepthBuffer);
                    cmdEndGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken);
                }
                cmdEndGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken);
            }
            // Visibility Buffer Shading pass
            {
                // Render a fullscreen triangle to evaluate shading for every pixel. This render step uses the render target generated by
                // DrawVisibilityBufferPass to get the draw / triangle IDs to reconstruct and interpolate vertex attributes per pixel. This
                // method doesn't set any vertex/index buffer because the triangle positions are calculated internally using vertex_id.
                cmdBeginGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken, "VB Shading Pass");

                // Bind Render Targets
                {
                    TFRenderTarget*         pDestinationRenderTarget = pScreenRenderTarget;
                    TFBindRenderTargetsDesc bindRenderTargets = {};
                    bindRenderTargets.mRenderTargetCount = 1;
                    bindRenderTargets.mRenderTargets[0] = { pDestinationRenderTarget, TF_LOAD_ACTION_CLEAR };
                    cmdBindRenderTargets(graphicsCmd, &bindRenderTargets);
                    cmdSetViewport(graphicsCmd, 0.0f, 0.0f, (float)pDestinationRenderTarget->mWidth,
                                   (float)pDestinationRenderTarget->mHeight, 0.0f, 1.0f);
                    cmdSetScissor(graphicsCmd, 0, 0, pDestinationRenderTarget->mWidth, pDestinationRenderTarget->mHeight);
                }
                // Draw
                {
                    cmdBindPipeline(graphicsCmd, pPipelineVisibilityBufferShadeSrgb[gAppSettings.mEnableAO]);
                    // A single triangle is rendered without specifying a vertex buffer (triangle positions are calculated internally using
                    // vertex_id)
                    cmdDraw(graphicsCmd, 3, 0);
                }

                cmdBindRenderTargets(graphicsCmd, NULL);
                cmdEndGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken);
            }

            // Skybox pass
            {
                cmdBeginGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken, "Draw Skybox");

                // Bind Render Targets
                {
                    TFBindRenderTargetsDesc bindRenderTargets = {};
                    bindRenderTargets.mRenderTargetCount = 1;
                    bindRenderTargets.mRenderTargets[0] = { pScreenRenderTarget, TF_LOAD_ACTION_LOAD };
                    cmdBindRenderTargets(graphicsCmd, &bindRenderTargets);
                    cmdSetViewport(graphicsCmd, 0.0f, 0.0f, (float)pScreenRenderTarget->mWidth, (float)pScreenRenderTarget->mHeight, 0.0f,
                                   1.0f);
                    cmdSetScissor(graphicsCmd, 0, 0, pScreenRenderTarget->mWidth, pScreenRenderTarget->mHeight);
                }
                // Draw
                {
                    const uint32_t stride = sizeof(float) * 4;
                    cmdBindPipeline(graphicsCmd, pSkyboxPipeline);
                    cmdBindVertexBuffer(graphicsCmd, 1, &pSkyboxVertexBuffer, &stride, NULL);
                    cmdDraw(graphicsCmd, 36, 0);
                }

                cmdBindRenderTargets(graphicsCmd, NULL);
                cmdEndGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken);
            }

            // Restore compute outputs for the next filtering pass
            {
                const uint32_t  maxNumBarriers = NUM_CULLING_VIEWPORTS * 2 + 4;
                uint32_t        barrierCount = 0;
                TFBufferBarrier computeOutputRestoreBarriers[maxNumBarriers] = {};
                computeOutputRestoreBarriers[barrierCount++] = { pLightClusters[frameIdx], TF_RESOURCE_STATE_SHADER_RESOURCE,
                                                                 TF_RESOURCE_STATE_UNORDERED_ACCESS };
                computeOutputRestoreBarriers[barrierCount++] = { pLightClustersCount[frameIdx], TF_RESOURCE_STATE_SHADER_RESOURCE,
                                                                 TF_RESOURCE_STATE_UNORDERED_ACCESS };
                computeOutputRestoreBarriers[barrierCount++] = { pVisibilityBuffer->ppBinBuffer[frameIdx],
                                                                 TF_RESOURCE_STATE_SHADER_RESOURCE, TF_RESOURCE_STATE_UNORDERED_ACCESS };

                cmdResourceBarrier(graphicsCmd, barrierCount, computeOutputRestoreBarriers, 0, NULL, 0, NULL);
            }

            if (gAppSettings.mEnableGodray)
            {
                cmdBeginGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken, "God Ray Passes");

                // God Ray pass
                {
                    cmdBeginGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken, "God Ray");

                    // Resource Transition
                    {
                        TFRenderTargetBarrier passBarriers[] = {
                            { pScreenRenderTarget, TF_RESOURCE_STATE_RENDER_TARGET, TF_RESOURCE_STATE_PIXEL_SHADER_RESOURCE },
                            { pRenderTargetGodRay[0], TF_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, TF_RESOURCE_STATE_RENDER_TARGET },
                        };
                        cmdResourceBarrier(graphicsCmd, 0, NULL, 0, NULL, TF_ARRAY_COUNT(passBarriers), passBarriers);
                    }
                    // Bind Render Targets
                    {
                        TFBindRenderTargetsDesc bindRenderTargets = {};
                        bindRenderTargets.mRenderTargetCount = 1;
                        bindRenderTargets.mRenderTargets[0] = { pRenderTargetGodRay[0], TF_LOAD_ACTION_CLEAR };
                        cmdBindRenderTargets(graphicsCmd, &bindRenderTargets);
                        cmdSetViewport(graphicsCmd, 0.0f, 0.0f, (float)pRenderTargetGodRay[0]->mWidth,
                                       (float)pRenderTargetGodRay[0]->mHeight, 0.0f, 1.0f);
                        cmdSetScissor(graphicsCmd, 0, 0, pRenderTargetGodRay[0]->mWidth, pRenderTargetGodRay[0]->mHeight);
                    }
                    // Draw
                    {
                        cmdBindPipeline(graphicsCmd, pPipelineGodRayPass);
                        cmdDraw(graphicsCmd, 3, 0);
                        cmdBindRenderTargets(graphicsCmd, NULL);
                    }
                    // Resource Transition
                    {
                        TFRenderTargetBarrier postBarrier[] = {
                            { pRenderTargetGodRay[0], TF_RESOURCE_STATE_RENDER_TARGET, TF_RESOURCE_STATE_PIXEL_SHADER_RESOURCE },
                        };
                        cmdResourceBarrier(graphicsCmd, 0, NULL, 0, NULL, TF_ARRAY_COUNT(postBarrier), postBarrier);
                    }

                    cmdEndGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken);
                }

                // Upload Gaussian blur weights shared by the horizontal and vertical blur passes
                {
                    TFBufferUpdateDesc bufferUpdate = { pBufferBlurWeights };
                    beginUpdateResource(&bufferUpdate);
                    memcpy(bufferUpdate.pMappedData, &gBlurWeightsUniform, sizeof(gBlurWeightsUniform));
                    endUpdateResource(&bufferUpdate);
                }

                // Horizontal Blur pass
                {
                    cmdBeginGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken, "God Ray Blur Horizontal");

                    // Resource Transition
                    {
                        TFRenderTargetBarrier passBarriers[] = {
                            { pRenderTargetGodRay[0], TF_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, TF_RESOURCE_STATE_UNORDERED_ACCESS },
                            { pRenderTargetGodRay[1], TF_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, TF_RESOURCE_STATE_UNORDERED_ACCESS },
                        };
                        cmdResourceBarrier(graphicsCmd, 0, NULL, 0, NULL, TF_ARRAY_COUNT(passBarriers), passBarriers);
                    }
                    // Dispatch
                    {
                        cmdBindPipeline(graphicsCmd, pPipelineGodRayBlurPass);

                        const uint32_t threadGroupSizeX = (pRenderTargetGodRay[0]->mWidth + 15) / 16;
                        const uint32_t threadGroupSizeY = (pRenderTargetGodRay[0]->mHeight + 15) / 16;

                        gGodRayBlurConstant.mBlurPassType = BLUR_PASS_TYPE_HORIZONTAL;
                        gGodRayBlurConstant.mFilterRadius = gAppSettings.mFilterRadius;
                        TFBufferUpdateDesc updateGR = { pGodRayBlurBuffer[BLUR_PASS_TYPE_HORIZONTAL][frameIdx] };
                        beginUpdateResource(&updateGR);
                        memcpy(updateGR.pMappedData, &gGodRayBlurConstant, sizeof(gGodRayBlurConstant));
                        endUpdateResource(&updateGR);

                        cmdBindDescriptorSet(graphicsCmd, frameIdx * BLUR_PASS_TYPE_COUNT + BLUR_PASS_TYPE_HORIZONTAL,
                                             pDescriptorSetGodRayBlurPassPerDraw);
                        cmdDispatch(graphicsCmd, threadGroupSizeX, threadGroupSizeY, 1);
                    }

                    cmdEndGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken);
                }

                // Vertical Blur pass
                {
                    cmdBeginGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken, "God Ray Blur Vertical");
                    // Resource Transition
                    {
                        TFRenderTargetBarrier blurPassBarriers[] = {
                            { pRenderTargetGodRay[0], TF_RESOURCE_STATE_UNORDERED_ACCESS, TF_RESOURCE_STATE_UNORDERED_ACCESS },
                            { pRenderTargetGodRay[1], TF_RESOURCE_STATE_UNORDERED_ACCESS, TF_RESOURCE_STATE_UNORDERED_ACCESS },
                        };
                        cmdResourceBarrier(graphicsCmd, 0, NULL, 0, NULL, TF_ARRAY_COUNT(blurPassBarriers), blurPassBarriers);
                    }
                    // Dispatch
                    {
                        cmdBindPipeline(graphicsCmd, pPipelineGodRayBlurPass);

                        const uint32_t threadGroupSizeX = (pRenderTargetGodRay[0]->mWidth + 15) / 16;
                        const uint32_t threadGroupSizeY = (pRenderTargetGodRay[0]->mHeight + 15) / 16;

                        gGodRayBlurConstant.mBlurPassType = BLUR_PASS_TYPE_VERTICAL;
                        gGodRayBlurConstant.mFilterRadius = gAppSettings.mFilterRadius;
                        TFBufferUpdateDesc updateGR = { pGodRayBlurBuffer[BLUR_PASS_TYPE_VERTICAL][frameIdx] };
                        beginUpdateResource(&updateGR);
                        memcpy(updateGR.pMappedData, &gGodRayBlurConstant, sizeof(gGodRayBlurConstant));
                        endUpdateResource(&updateGR);

                        cmdBindDescriptorSet(graphicsCmd, frameIdx * BLUR_PASS_TYPE_COUNT + BLUR_PASS_TYPE_VERTICAL,
                                             pDescriptorSetGodRayBlurPassPerDraw);
                        cmdDispatch(graphicsCmd, threadGroupSizeX, threadGroupSizeY, 1);
                    }
                    // Resource Transition
                    {
                        TFRenderTargetBarrier finalBlurBarriers[] = {
                            { pRenderTargetGodRay[0], TF_RESOURCE_STATE_UNORDERED_ACCESS, TF_RESOURCE_STATE_PIXEL_SHADER_RESOURCE },
                            { pRenderTargetGodRay[1], TF_RESOURCE_STATE_UNORDERED_ACCESS, TF_RESOURCE_STATE_PIXEL_SHADER_RESOURCE },
                        };
                        cmdResourceBarrier(graphicsCmd, 0, NULL, 0, NULL, TF_ARRAY_COUNT(finalBlurBarriers), finalBlurBarriers);
                    }

                    cmdEndGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken);
                }

                // Curve Conversion pass
                {
                    cmdBeginGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken, "Curve Conversion");

                    // Resource Transition
                    {
                        TFRenderTargetBarrier passBarrier[] = {
                            { pCurveConversionRenderTarget, TF_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, TF_RESOURCE_STATE_RENDER_TARGET },
                        };
                        cmdResourceBarrier(graphicsCmd, 0, NULL, 0, NULL, TF_ARRAY_COUNT(passBarrier), passBarrier);
                    }
                    // Bind Render Targets
                    {
                        TFBindRenderTargetsDesc bindRenderTargets = {};
                        bindRenderTargets.mRenderTargetCount = 1;
                        bindRenderTargets.mRenderTargets[0] = { pCurveConversionRenderTarget, TF_LOAD_ACTION_CLEAR };
                        cmdBindRenderTargets(graphicsCmd, &bindRenderTargets);

                        cmdSetViewport(graphicsCmd, 0.0f, 0.0f, (float)pCurveConversionRenderTarget->mWidth,
                                       (float)pCurveConversionRenderTarget->mHeight, 0.0f, 1.0f);
                        cmdSetScissor(graphicsCmd, 0, 0, pCurveConversionRenderTarget->mWidth, pCurveConversionRenderTarget->mHeight);
                    }
                    // Draw
                    {
                        cmdBindPipeline(graphicsCmd, pPipelineCurveConversionPass);
                        cmdDraw(graphicsCmd, 3, 0);
                    }

                    cmdBindRenderTargets(graphicsCmd, NULL);
                    pScreenRenderTarget = pCurveConversionRenderTarget;
                    cmdEndGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken);
                }
                cmdEndGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken);
            }

            acquireNextImage(pRenderer, pSwapChain, pImageAcquiredSemaphore[frameIdx], NULL, &presentIndex);

            // Draw Final Image pass
            {
                // Draws the final fullscreen image into the acquired swapchain target
                cmdBeginGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken, "Draw Final Image");

                // Resource Transition
                {
                    TFRenderTargetBarrier passBarriers[] = {
                        { pScreenRenderTarget, TF_RESOURCE_STATE_RENDER_TARGET, TF_RESOURCE_STATE_PIXEL_SHADER_RESOURCE },
                        { pSwapChain->ppRenderTargets[presentIndex], TF_RESOURCE_STATE_PRESENT, TF_RESOURCE_STATE_RENDER_TARGET },
                    };
                    cmdResourceBarrier(graphicsCmd, 0, NULL, 0, NULL, TF_ARRAY_COUNT(passBarriers), passBarriers);
                }
                // Bind Render Targets
                {
                    TFBindRenderTargetsDesc bindRenderTargets = {};
                    bindRenderTargets.mRenderTargetCount = 1;
                    bindRenderTargets.mRenderTargets[0] = { pSwapChain->ppRenderTargets[presentIndex], TF_LOAD_ACTION_DONTCARE };
                    cmdBindRenderTargets(graphicsCmd, &bindRenderTargets);
                    cmdSetViewport(graphicsCmd, 0.0f, 0.0f, (float)pSwapChain->ppRenderTargets[presentIndex]->mWidth,
                                   (float)pSwapChain->ppRenderTargets[presentIndex]->mHeight, 0.0f, 1.0f);
                    cmdSetScissor(graphicsCmd, 0, 0, pSwapChain->ppRenderTargets[presentIndex]->mWidth,
                                  pSwapChain->ppRenderTargets[presentIndex]->mHeight);
                }
                // Draw
                {
                    cmdBindPipeline(graphicsCmd, pPipelinePresentPass);
                    cmdBindDescriptorSet(graphicsCmd, gAppSettings.mEnableGodray ? 1 : 0, pDescriptorSetDisplayPerDraw);
                    cmdDraw(graphicsCmd, 3, 0);
                }

                cmdBindRenderTargets(graphicsCmd, NULL);
                cmdEndGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken);
            }

            // UI pass
            {
                cmdBeginGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken, "UI Pass");

                // Bind Render Targets
                {
                    TFBindRenderTargetsDesc bindRenderTargets = {};
                    bindRenderTargets.mRenderTargetCount = 1;
                    bindRenderTargets.mRenderTargets[0] = { pSwapChain->ppRenderTargets[presentIndex], TF_LOAD_ACTION_LOAD };
                    cmdBindRenderTargets(graphicsCmd, &bindRenderTargets);
                    cmdSetViewport(graphicsCmd, 0.0f, 0.0f, (float)pSwapChain->ppRenderTargets[presentIndex]->mWidth,
                                   (float)pSwapChain->ppRenderTargets[presentIndex]->mHeight, 0.0f, 1.0f);
                    cmdSetScissor(graphicsCmd, 0, 0, pSwapChain->ppRenderTargets[presentIndex]->mWidth,
                                  pSwapChain->ppRenderTargets[presentIndex]->mHeight);
                }
                // Draw
                {
                    gFrameTimeDraw.mFontColor = gAppSettings.mVisualizeAO ? 0xff000000 : 0xff00ffff;
                    gFrameTimeDraw.mFontSize = 18.0f;
                    gFrameTimeDraw.pFont = gFont;

                    cmdDrawCpuProfile(graphicsCmd, float2(8.0f, 15.0f), &gFrameTimeDraw);

                    if (gAppSettings.mAsyncCompute)
                    {
                        cmdDrawGpuProfile(graphicsCmd, float2(8.0f, 100.0f), gComputeProfileToken, &gFrameTimeDraw);
                        cmdDrawGpuProfile(graphicsCmd, float2(8.0f, 425.0f), gGraphicsProfileToken, &gFrameTimeDraw);
                    }
                    else
                    {
                        cmdDrawGpuProfile(graphicsCmd, float2(8.0f, 100.0f), gGraphicsProfileToken, &gFrameTimeDraw);
                    }

                    uiCmdDrawUserInterface(graphicsCmd, pSwapChain, pSwapChain->ppRenderTargets[presentIndex], gGraphicsProfileToken);
                }

                cmdBindRenderTargets(graphicsCmd, NULL);
                cmdEndGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken);
            }

            TFRenderTargetBarrier barrierPresent = { pSwapChain->ppRenderTargets[presentIndex], TF_RESOURCE_STATE_RENDER_TARGET,
                                                     TF_RESOURCE_STATE_PRESENT };
            cmdResourceBarrier(graphicsCmd, 0, NULL, 0, NULL, 1, &barrierPresent);

            cmdEndGpuFrameProfile(graphicsCmd, gGraphicsProfileToken);
            endCmd(graphicsCmd);

            // Submit all the work to the GPU and present
            FlushResourceUpdateDesc flushUpdateDesc = {};
            flushUpdateDesc.mNodeIndex = 0;
            flushResourceUpdates(&flushUpdateDesc);
            TFSemaphore* waitSemaphores[] = { flushUpdateDesc.pOutSubmittedSemaphore, pImageAcquiredSemaphore[frameIdx],
                                              gComputeSemaphores[frameIdx] };
            TFSemaphore* signalSemaphores[] = { graphicsElem.pSemaphore, pPresentSemaphore };

            TFQueueSubmitDesc submitDesc = {};
            submitDesc.mCmdCount = 1;
            submitDesc.ppCmds = &graphicsCmd;
            submitDesc.mSignalSemaphoreCount = TF_ARRAY_COUNT(signalSemaphores);
            submitDesc.ppSignalSemaphores = signalSemaphores;
            submitDesc.pSignalFence = graphicsElem.pFence;
            submitDesc.ppWaitSemaphores = waitSemaphores;
            submitDesc.mWaitSemaphoreCount =
                (useDedicatedComputeQueue && gComputeSemaphores[frameIdx]) ? TF_ARRAY_COUNT(waitSemaphores) : 2;
            queueSubmit(pGraphicsQueue, &submitDesc);

            gPrevGraphicsSemaphore = graphicsElem.pSemaphore;

            TFQueuePresentDesc presentDesc = {};
            presentDesc.mIndex = (uint8_t)presentIndex;
            presentDesc.mWaitSemaphoreCount = 1;
            presentDesc.ppWaitSemaphores = &pPresentSemaphore;
            presentDesc.pSwapChain = pSwapChain;
            presentDesc.mSubmitDone = true;
            queuePresent(pGraphicsQueue, &presentDesc);
            flipProfiler();
        }
    }

    const char* GetName() { return "Visibility_Buffer2"; }

private:
    bool addDescriptorSets()
    {
        // Persistent  set
        TFDescriptorSetDesc setDesc = SRT_SET_DESC(SrtData, Persistent, 1, 0);
        addDescriptorSet(pRenderer, &setDesc, &pDescriptorSetPersistent);

        // per frame set
        setDesc = SRT_SET_DESC(SrtData, PerFrame, gDataBufferCount, 0);
        addDescriptorSet(pRenderer, &setDesc, &pDescriptorSetPerFrame);

        // Triangle Filtering per batch
        setDesc = SRT_SET_DESC(TriangleFilteringCompSrtData, PerBatch, gDataBufferCount * FILTER_BATCH_COUNT, 0);
        addDescriptorSet(pRenderer, &setDesc, &pDescriptorSetTriangleFilteringPerBatch);

        // Triangle Filtering per draw
        setDesc = SRT_SET_DESC(TriangleFilteringCompSrtData, PerDraw, gDataBufferCount, 0);
        addDescriptorSet(pRenderer, &setDesc, &pDescriptorSetTriangleFilteringPerDraw);
        addDescriptorSet(pRenderer, &setDesc, &pDescriptorSetTriangleFilteringPerDrawCompute);

        // cluster lights set
        setDesc = SRT_SET_DESC(ClusterLightsSrtData, PerDraw, gDataBufferCount, 0);
        addDescriptorSet(pRenderer, &setDesc, &pDescriptorSetClusterLights);

        // display per draw : one with godray, another without
        setDesc = SRT_SET_DESC(DisplaySrtData, PerDraw, 2, 0);
        addDescriptorSet(pRenderer, &setDesc, &pDescriptorSetDisplayPerDraw);

        // display per draw : blit Depth
        setDesc = SRT_SET_DESC(TriangleFilteringCompSrtData, PerBatch, 3 * gDataBufferCount, 0);
        addDescriptorSet(pRenderer, &setDesc, &pDescriptorSetRenderTargetPerBatch);

        // God Ray Blur
        setDesc = SRT_SET_DESC(GodrayBlurCompSrtData, PerDraw, 2 * gDataBufferCount, 0);
        addDescriptorSet(pRenderer, &setDesc, &pDescriptorSetGodRayBlurPassPerDraw);

        return true;
    }

    void removeDescriptorSets()
    {
        removeDescriptorSet(pRenderer, pDescriptorSetTriangleFilteringPerDrawCompute);
        removeDescriptorSet(pRenderer, pDescriptorSetTriangleFilteringPerDraw);
        removeDescriptorSet(pRenderer, pDescriptorSetClusterLights);
        removeDescriptorSet(pRenderer, pDescriptorSetGodRayBlurPassPerDraw);
        removeDescriptorSet(pRenderer, pDescriptorSetRenderTargetPerBatch);
        removeDescriptorSet(pRenderer, pDescriptorSetDisplayPerDraw);
        removeDescriptorSet(pRenderer, pDescriptorSetTriangleFilteringPerBatch);
        removeDescriptorSet(pRenderer, pDescriptorSetPerFrame);
        removeDescriptorSet(pRenderer, pDescriptorSetPersistent);
    }

    void updateDescriptorSets()
    {
        // persistent set
        TFTexture*       godRayTextures[] = { pRenderTargetGodRay[0]->pTexture, pRenderTargetGodRay[1]->pTexture };
        TFDescriptorData persistentSetParams[18] = {};
        persistentSetParams[0].mIndex = SRT_RES_IDX(SrtData, Persistent, gVertexPositionBuffer);
        persistentSetParams[0].ppBuffers = &pGeom->pVertexBuffers[0];
        persistentSetParams[1].mIndex = SRT_RES_IDX(SrtData, Persistent, gVertexTexCoordBuffer);
        persistentSetParams[1].ppBuffers = &pGeom->pVertexBuffers[1];
        persistentSetParams[2].mIndex = SRT_RES_IDX(SrtData, Persistent, gVertexNormalBuffer);
        persistentSetParams[2].ppBuffers = &pGeom->pVertexBuffers[2];
        persistentSetParams[3].mIndex = SRT_RES_IDX(SrtData, Persistent, gIndexDataBuffer);
        persistentSetParams[3].ppBuffers = &pGeom->pIndexBuffer;
        persistentSetParams[4].mIndex = SRT_RES_IDX(SrtData, Persistent, gMeshConstantsBuffer);
        persistentSetParams[4].ppBuffers = &pMeshConstantsBuffer;
        persistentSetParams[5].mIndex = SRT_RES_IDX(SrtData, Persistent, gMaterialProps);
        persistentSetParams[5].ppBuffers = &pMaterialPropertyBuffer;
        persistentSetParams[6].mIndex = SRT_RES_IDX(SrtData, Persistent, gAllTextures);
        persistentSetParams[6].mCount = (uint32_t)gAllTexturesCount;
        persistentSetParams[6].ppTextures = gPackage->ppAllTextures;
        persistentSetParams[7].mIndex = SRT_RES_IDX(SrtData, Persistent, gLights);
        persistentSetParams[7].ppBuffers = &pLightsBuffer;
        persistentSetParams[8].mIndex = SRT_RES_IDX(SrtData, Persistent, gBlurWeights);
        persistentSetParams[8].ppBuffers = &pBufferBlurWeights;
        persistentSetParams[9].mIndex = SRT_RES_IDX(SrtData, Persistent, gDepthTexture);
        persistentSetParams[9].ppTextures = &pDepthBuffer->pTexture;
        persistentSetParams[10].mIndex = SRT_RES_IDX(SrtData, Persistent, gDepthTex);
        persistentSetParams[10].ppTextures = &pDepthBuffer->pTexture;
        persistentSetParams[11].mIndex = SRT_RES_IDX(SrtData, Persistent, gShadowMap);
        persistentSetParams[11].ppTextures = &pRenderTargetShadow->pTexture;
        persistentSetParams[12].mIndex = SRT_RES_IDX(SrtData, Persistent, gMeshConstantsBuffer);
        persistentSetParams[12].ppBuffers = &pMeshConstantsBuffer;
        persistentSetParams[13].mIndex = SRT_RES_IDX(SrtData, Persistent, gIndexDataBuffer);
        persistentSetParams[13].ppBuffers = &pGeom->pIndexBuffer;
        persistentSetParams[14].mIndex = SRT_RES_IDX(SrtData, Persistent, gSkyboxTex);
        persistentSetParams[14].ppTextures = &pSkybox;
        persistentSetParams[15].mIndex = SRT_RES_IDX(SrtData, Persistent, gSceneTex);
        persistentSetParams[15].ppTextures = &pIntermediateRenderTarget->pTexture;
        persistentSetParams[16].mIndex = SRT_RES_IDX(SrtData, Persistent, gGodRayTex);
        persistentSetParams[16].ppTextures = &pRenderTargetGodRay[0]->pTexture;
        updateDescriptorSet(pRenderer, 0, pDescriptorSetPersistent, 17, persistentSetParams);

        // per frame set
        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            const uint32_t   perFrameParameterCount = 7;
            TFDescriptorData perFrameSetParams[perFrameParameterCount] = {};
            perFrameSetParams[0].mIndex = SRT_RES_IDX(SrtData, PerFrame, gPerFrameConstants);
            perFrameSetParams[0].ppBuffers = &pPerFrameUniformBuffers[i];
            perFrameSetParams[1].mIndex = SRT_RES_IDX(SrtData, PerFrame, gVBViewConstants);
            perFrameSetParams[1].ppBuffers = &pPerFrameVBUniformBuffers[VB_UB_GRAPHICS][i];
            perFrameSetParams[2].mIndex = SRT_RES_IDX(SrtData, PerFrame, gLightClustersCount);
            perFrameSetParams[2].ppBuffers = &pLightClustersCount[i];
            perFrameSetParams[3].mIndex = SRT_RES_IDX(SrtData, PerFrame, gLightClusters);
            perFrameSetParams[3].ppBuffers = &pLightClusters[i];
            perFrameSetParams[4].mIndex = SRT_RES_IDX(SrtData, PerFrame, gBinBuffer);
            perFrameSetParams[4].ppBuffers = &pVisibilityBuffer->ppBinBuffer[i];
            perFrameSetParams[5].mIndex = SRT_RES_IDX(SrtData, PerFrame, gVisibilityBuffer);
            perFrameSetParams[5].ppBuffers = &pVBDepthBuffer[i];
            perFrameSetParams[6].mIndex = SRT_RES_IDX(SrtData, PerFrame, gUniformCameraSky);
            perFrameSetParams[6].ppBuffers = &pUniformBufferSky[i];
            updateDescriptorSet(pRenderer, i, pDescriptorSetPerFrame, perFrameParameterCount, perFrameSetParams);
        }

        // triangle filtering per draw
        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            TFDescriptorData triangleFilteringPerDrawParams[3] = {};
            triangleFilteringPerDrawParams[0].mIndex = SRT_RES_IDX(TriangleFilteringCompSrtData, PerDraw, gBinBufferRW);
            triangleFilteringPerDrawParams[0].ppBuffers = &pVisibilityBuffer->ppBinBuffer[i];
            triangleFilteringPerDrawParams[1].mIndex = SRT_RES_IDX(TriangleFilteringCompSrtData, PerDraw, gVisibilityBufferRW);
            triangleFilteringPerDrawParams[1].ppBuffers = &pVBDepthBuffer[i];
            triangleFilteringPerDrawParams[2].mIndex = SRT_RES_IDX(TriangleFilteringCompSrtData, PerDraw, gComputeVBViewConstants);
            triangleFilteringPerDrawParams[2].ppBuffers = &pPerFrameVBUniformBuffers[VB_UB_GRAPHICS][i];
            updateDescriptorSet(pRenderer, i, pDescriptorSetTriangleFilteringPerDraw, 3, triangleFilteringPerDrawParams);

            triangleFilteringPerDrawParams[2].ppBuffers = &pPerFrameVBUniformBuffers[VB_UB_COMPUTE][i];
            updateDescriptorSet(pRenderer, i, pDescriptorSetTriangleFilteringPerDrawCompute, 3, triangleFilteringPerDrawParams);
        }

        // cluster lights set
        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            TFDescriptorData clusterLightsParams[2] = {};
            clusterLightsParams[0].mIndex = SRT_RES_IDX(ClusterLightsSrtData, PerDraw, gLightClustersCountRW);
            clusterLightsParams[0].ppBuffers = &pLightClustersCount[i];
            clusterLightsParams[1].mIndex = SRT_RES_IDX(ClusterLightsSrtData, PerDraw, gLightClustersRW);
            clusterLightsParams[1].ppBuffers = &pLightClusters[i];
            updateDescriptorSet(pRenderer, i, pDescriptorSetClusterLights, 2, clusterLightsParams);
        }

        // blit depth
        {
            for (uint32_t i = 0; i < gDataBufferCount; ++i)
            {
                TFDescriptorData params[1] = {};
                params[0].mIndex = SRT_RES_IDX(TriangleFilteringCompSrtData, PerBatch, gRenderTargetInfo);
                params[0].ppBuffers = &pRenderTargetInfoConstantsBuffers[i][0];
                updateDescriptorSet(pRenderer, i * 3, pDescriptorSetRenderTargetPerBatch, 1, params);
                params[0].ppBuffers = &pRenderTargetInfoConstantsBuffers[i][1];
                updateDescriptorSet(pRenderer, i * 3 + 1, pDescriptorSetRenderTargetPerBatch, 1, params);
                params[0].ppBuffers = &pRenderTargetInfoConstantsBuffers[i][2];
                updateDescriptorSet(pRenderer, i * 3 + 2, pDescriptorSetRenderTargetPerBatch, 1, params);
            }
        }

        // God Ray Blur
        {
            TFDescriptorData params[2] = {};
            params[0].mIndex = SRT_RES_IDX(GodrayBlurCompSrtData, PerDraw, gGodrayTexturesRW);
            params[0].ppTextures = godRayTextures;
            params[0].mCount = 2;

            params[1].mIndex = SRT_RES_IDX(GodrayBlurCompSrtData, PerDraw, gBlurParams);
            for (uint32_t i = 0; i < gDataBufferCount; i++)
            {
                params[1].ppBuffers = &pGodRayBlurBuffer[BLUR_PASS_TYPE_HORIZONTAL][i];
                params[1].mCount = 1;
                updateDescriptorSet(pRenderer, i * BLUR_PASS_TYPE_COUNT, pDescriptorSetGodRayBlurPassPerDraw, 2, params);
                params[1].ppBuffers = &pGodRayBlurBuffer[BLUR_PASS_TYPE_VERTICAL][i];
                params[1].mCount = 1;
                updateDescriptorSet(pRenderer, i * BLUR_PASS_TYPE_COUNT + BLUR_PASS_TYPE_VERTICAL, pDescriptorSetGodRayBlurPassPerDraw, 2,
                                    params);
            }
        }
        // Present
        {
            TFDescriptorData params[1] = {};
            params[0].mIndex = SRT_RES_IDX(DisplaySrtData, PerDraw, gDisplaytexture);
            params[0].ppTextures = &pIntermediateRenderTarget->pTexture;
            updateDescriptorSet(pRenderer, 0, pDescriptorSetDisplayPerDraw, 1, params);
            params[0].ppTextures = &pCurveConversionRenderTarget->pTexture;
            updateDescriptorSet(pRenderer, 1, pDescriptorSetDisplayPerDraw, 1, params);
        }
    }
    /************************************************************************/
    // Add render targets
    /************************************************************************/
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
        swapChainDesc.mColorClearValue = { { 1, 1, 1, 1 } };
        swapChainDesc.mEnableVsync = mSettings.mVSyncEnabled;

        TinyImageFormat hdrFormat = getSupportedSwapchainFormat(pRenderer, &swapChainDesc, TF_COLOR_SPACE_P2020);
        const bool      wantsHDR = OUTPUT_MODE_P2020 == gAppSettings.mOutputMode;
        const bool      supportsHDR = TinyImageFormat_UNDEFINED != hdrFormat;
        if (pRenderer->pGpu->mHDRSupported)
        {
            if (supportsHDR)
            {
                bassignliteral(&gOutputSupportsHDRText, "Current Output Supports HDR");
            }
            else
            {
                bassignliteral(&gOutputSupportsHDRText, "Current Output Does Not Support HDR");
            }
        }

        if (wantsHDR)
        {
            if (supportsHDR)
            {
                swapChainDesc.mColorFormat = hdrFormat;
                swapChainDesc.mColorSpace = TF_COLOR_SPACE_P2020;
            }
            else
            {
                errorMessagePopup("Error",
                                  "Could not create hdr swapchain, please use a HDR-capable display and enable HDR in OS settings.",
                                  &pWindow->handle, NULL);
            }
        }

        ::addSwapChain(pRenderer, &swapChainDesc, &pSwapChain);
        return pSwapChain != NULL;
    }

    void addRenderTargets()
    {
        const uint32_t     width = gSceneRes.mWidth;
        const uint32_t     height = gSceneRes.mHeight;
        /************************************************************************/
        /************************************************************************/
        const TFClearValue depthStencilClear = { { 0.0f, 0 } };
        // Used for ESM render target shadow
        const TFClearValue lessEqualDepthStencilClear = { { 1.f, 0 } };

        /************************************************************************/
        // ESRAM layout
        /************************************************************************/
        // alloc0: Depth Buffer RT  |  Shared Depth Buffer  |  VB RT
        // alloc1:                  |  Intermediate  | ...

        /************************************************************************/
        // Main depth buffer
        /************************************************************************/
        // Add depth buffer
        BEGINALLOCATION("Depth / SharedDepth", 0u);
        TFRenderTargetDesc depthRT = {};
        depthRT.mArraySize = 1;
        depthRT.mClearValue = depthStencilClear;
        depthRT.mDepth = 1;
        depthRT.mDescriptors = TF_DESCRIPTOR_TYPE_TEXTURE;
        depthRT.mFormat = TinyImageFormat_D32_SFLOAT;
        depthRT.mStartState = TF_RESOURCE_STATE_SHADER_RESOURCE;
        depthRT.mHeight = height;
        depthRT.mSampleCount = TF_SAMPLE_COUNT_1;
        depthRT.mSampleQuality = 0;
        depthRT.mFlags = TF_TEXTURE_CREATION_FLAG_ESRAM;
        depthRT.mFlags |= TF_TEXTURE_CREATION_FLAG_TEXTURE_COMPATABILITY;
        depthRT.mWidth = width;
        depthRT.pName = "Depth Buffer RT";
        addRenderTarget(pRenderer, &depthRT, &pDepthBuffer);

#if defined(XBOX)
        const uint32_t depthAllocationOffset = ALLOCATIONOFFSET();
#endif

        // Here we create the depth buffer filled by the software rasterizer.
        // The depth buffer is filled both for shadow and visibility buffer.
        // We need a UINT depth buffer, as it's not possible to write to a depth texture (D32_SFLOAT or others) from a compute shader.
        // Metal 2 does not support AtomicMin2D or AtomicMax2D on image/texture resources. Thus, we fallback to a regular buffer for metal.
        // const uint32_t depthWidth = max(width, gShadowMapSize);
        // const uint32_t depthHeight = max(height, gShadowMapSize);
        uint32_t         vbSize = (gShadowMapSize * gShadowMapSize) + (width * height);
        TFBufferLoadDesc bufferDesc = {};
        bufferDesc.mDesc.mSize = vbSize * sizeof(uint64_t);
        bufferDesc.mDesc.mFirstElement = 0;
        bufferDesc.mDesc.mElementCount = vbSize;
        bufferDesc.mDesc.mStructStride = sizeof(uint64_t);
        bufferDesc.mDesc.pName = "Packed Depth Visibility Buffer - 0";
        bufferDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_GPU_ONLY;
        bufferDesc.mDesc.mFlags = TF_BUFFER_CREATION_FLAG_OWN_MEMORY_BIT;
        bufferDesc.mDesc.mStartState = TF_RESOURCE_STATE_SHADER_RESOURCE;
        bufferDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_RW_BUFFER | TF_DESCRIPTOR_TYPE_BUFFER;
        bufferDesc.pData = NULL;
        bufferDesc.ppBuffer = &pVBDepthBuffer[0];
        addResource(&bufferDesc, NULL);
        bufferDesc.mDesc.pName = "Packed Depth Visibility Buffer - 1";
        bufferDesc.ppBuffer = &pVBDepthBuffer[1];
        addResource(&bufferDesc, NULL);

        /************************************************************************/
        // Shadow pass render target
        /************************************************************************/
        TFRenderTargetDesc shadowRTDesc = {};
        shadowRTDesc.mArraySize = 1;
        shadowRTDesc.mClearValue = lessEqualDepthStencilClear;
        shadowRTDesc.mDepth = 1;
        shadowRTDesc.mDescriptors = TF_DESCRIPTOR_TYPE_TEXTURE;
        shadowRTDesc.mFormat = TinyImageFormat_D32_SFLOAT;
        shadowRTDesc.mStartState = TF_RESOURCE_STATE_SHADER_RESOURCE;
        shadowRTDesc.mWidth = gShadowMapSize;
        shadowRTDesc.mSampleCount = TF_SAMPLE_COUNT_1;
        shadowRTDesc.mSampleQuality = 0;
        // shadowRTDesc.mFlags = TF_TEXTURE_CREATION_FLAG_ESRAM;
        shadowRTDesc.mFlags |= TF_TEXTURE_CREATION_FLAG_TEXTURE_COMPATABILITY;
        shadowRTDesc.mHeight = gShadowMapSize;
        shadowRTDesc.pName = "Shadow Map RT";
        addRenderTarget(pRenderer, &shadowRTDesc, &pRenderTargetShadow);
        ENDALLOCATION("Depth / SharedDepth");

        /************************************************************************/
        // Intermediate render target
        /************************************************************************/
        BEGINALLOCATION("Intermediate", depthAllocationOffset);
        TFRenderTargetDesc postProcRTDesc = {};
        postProcRTDesc.mArraySize = 1;
        postProcRTDesc.mClearValue = { { 0.0f, 0.0f, 0.0f, 0.0f } };
        postProcRTDesc.mDepth = 1;
        postProcRTDesc.mDescriptors = TF_DESCRIPTOR_TYPE_TEXTURE;
        postProcRTDesc.mFormat = pSwapChain->mFormat;
        postProcRTDesc.mStartState = TF_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        postProcRTDesc.mHeight = gSceneRes.mHeight;
        postProcRTDesc.mWidth = gSceneRes.mWidth;
        postProcRTDesc.mSampleCount = pSwapChain->ppRenderTargets[0]->mSampleCount;
        postProcRTDesc.mSampleQuality = pSwapChain->ppRenderTargets[0]->mSampleQuality;
        postProcRTDesc.mFlags = TF_TEXTURE_CREATION_FLAG_ESRAM;
        postProcRTDesc.pName = "pIntermediateRenderTarget";
        addRenderTarget(pRenderer, &postProcRTDesc, &pIntermediateRenderTarget);

        /************************************************************************/
        // GodRay render target
        /************************************************************************/
        TinyImageFormat    GRRTFormat = pRenderer->pGpu->mFormatCaps[TinyImageFormat_B10G11R11_UFLOAT] & (TF_FORMAT_CAP_READ_WRITE)
                                            ? TinyImageFormat_B10G11R11_UFLOAT
                                            : TinyImageFormat_R8G8B8A8_UNORM;
        TFRenderTargetDesc GRRTDesc = {};
        GRRTDesc.mArraySize = 1;
        GRRTDesc.mClearValue = { { 0.0f, 0.0f, 0.0f, 1.0f } };
        GRRTDesc.mDepth = 1;
        GRRTDesc.mStartState = TF_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        GRRTDesc.mHeight = gSceneRes.mHeight / gGodrayScale;
        GRRTDesc.mWidth = gSceneRes.mWidth / gGodrayScale;
        GRRTDesc.mFormat = GRRTFormat;
        GRRTDesc.mDescriptors = TF_DESCRIPTOR_TYPE_RW_TEXTURE | TF_DESCRIPTOR_TYPE_TEXTURE;
        GRRTDesc.mSampleCount = pSwapChain->ppRenderTargets[0]->mSampleCount;
        GRRTDesc.mSampleQuality = pSwapChain->ppRenderTargets[0]->mSampleQuality;
        GRRTDesc.mFlags = TF_TEXTURE_CREATION_FLAG_ESRAM;

        GRRTDesc.pName = "GodRay RT A";
        addRenderTarget(pRenderer, &GRRTDesc, &pRenderTargetGodRay[0]);
        GRRTDesc.pName = "GodRay RT B";
        addRenderTarget(pRenderer, &GRRTDesc, &pRenderTargetGodRay[1]);
        ENDALLOCATION("Intermediate");
        /************************************************************************/
        // Color Conversion render target
        /************************************************************************/
        BEGINALLOCATION("CurveConversion", 0u);
        TFRenderTargetDesc postCurveConversionRTDesc = {};
        postCurveConversionRTDesc.mArraySize = 1;
        postCurveConversionRTDesc.mClearValue = { { 0.0f, 0.0f, 0.0f, 0.0f } };
        postCurveConversionRTDesc.mDepth = 1;
        postCurveConversionRTDesc.mDescriptors = TF_DESCRIPTOR_TYPE_TEXTURE;
        postCurveConversionRTDesc.mFormat = pSwapChain->ppRenderTargets[0]->mFormat;
        postCurveConversionRTDesc.mStartState = TF_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        postCurveConversionRTDesc.mHeight = height;
        postCurveConversionRTDesc.mWidth = width;
        postCurveConversionRTDesc.mSampleCount = pSwapChain->ppRenderTargets[0]->mSampleCount;
        postCurveConversionRTDesc.mSampleQuality = pSwapChain->ppRenderTargets[0]->mSampleQuality;
        postCurveConversionRTDesc.mFlags = TF_TEXTURE_CREATION_FLAG_ESRAM;
        postCurveConversionRTDesc.pName = "pCurveConversionRenderTarget";
        addRenderTarget(pRenderer, &postCurveConversionRTDesc, &pCurveConversionRenderTarget);
        ENDALLOCATION("CurveConversion");
    }

    void removeRenderTargets()
    {
        removeRenderTarget(pRenderer, pCurveConversionRenderTarget);

        removeRenderTarget(pRenderer, pRenderTargetGodRay[0]);
        removeRenderTarget(pRenderer, pRenderTargetGodRay[1]);

        removeRenderTarget(pRenderer, pIntermediateRenderTarget);
        removeRenderTarget(pRenderer, pDepthBuffer);
        removeResource(pVBDepthBuffer[0]);
        removeResource(pVBDepthBuffer[1]);
        removeRenderTarget(pRenderer, pRenderTargetShadow);
    }
    /************************************************************************/
    // Load all the shaders needed for the demo
    /************************************************************************/
    void addShaders()
    {
        TFShaderLoadDesc vbShade[2] = {};
        TFShaderLoadDesc clearBuffer = {};
        TFShaderLoadDesc triangleCulling = {};
        TFShaderLoadDesc clearLights = {};
        TFShaderLoadDesc clusterLights = {};
        TFShaderLoadDesc vbDepthRasterize = {};
        TFShaderLoadDesc vbDepthRasterizeSeparateAlpha[2] = { {}, {} };
        TFShaderLoadDesc clearRenderTarget = {};
        TFShaderLoadDesc blitDepth = {};

        const char* visibilityBufferShadeShaders[] = { "visibilityBuffer_shade_SAMPLE_1.frag", "visibilityBuffer_shade_SAMPLE_1_AO.frag" };

        for (uint32_t j = 0; j < 2; ++j)
        {
            uint32_t index = j;
            vbShade[index].mVert.pFileName = "visibilityBuffer_shade.vert";
            vbShade[index].mFrag.pFileName = visibilityBufferShadeShaders[index];
        }

        // Triangle culling compute shader
        triangleCulling.mComp.pFileName = "triangle_filtering.comp";
        if (gAppSettings.mLargeBinRasterGroups)
        {
            triangleCulling.mComp.pFileName = "triangle_filtering_LG.comp";
        }
        // Clear buffers compute shader
        clearBuffer.mComp.pFileName = "clear_buffers.comp";
        // Clear light clusters compute shader
        clearLights.mComp.pFileName = "clear_light_clusters.comp";
        // Cluster lights compute shader
        clusterLights.mComp.pFileName = "cluster_lights.comp";

        vbDepthRasterize.mComp.pFileName = "bin_rasterizer.comp";
        vbDepthRasterizeSeparateAlpha[0].mComp.pFileName = "bin_rasterizer_alpha_off.comp";
        vbDepthRasterizeSeparateAlpha[1].mComp.pFileName = "bin_rasterizer_alpha_only.comp";
        clearRenderTarget.mComp.pFileName = "clear_render_target.comp";

        blitDepth.mVert.pFileName = "visibilityBuffer_shade.vert";
        blitDepth.mFrag.pFileName = "visibilityBuffer_blitDepth.frag";

        TFShaderLoadDesc godrayShaderDesc = {};
        godrayShaderDesc.mVert.pFileName = "display.vert";
        godrayShaderDesc.mFrag.pFileName = "godray.frag";
        addShader(pRenderer, &godrayShaderDesc, &pGodRayPass);

        TFShaderLoadDesc godrayBlurShaderDesc = {};
        godrayBlurShaderDesc.mComp.pFileName = "godray_blur.comp";
        addShader(pRenderer, &godrayBlurShaderDesc, &pShaderGodRayBlurPass);

        TFShaderLoadDesc CurveConversionShaderDesc = {};
        CurveConversionShaderDesc.mVert.pFileName = "display.vert";
        CurveConversionShaderDesc.mFrag.pFileName = "CurveConversion.frag";
        addShader(pRenderer, &CurveConversionShaderDesc, &pShaderCurveConversion);

        TFShaderLoadDesc presentShaderDesc = {};
        presentShaderDesc.mVert.pFileName = "display.vert";
        presentShaderDesc.mFrag.pFileName = "display.frag";

        TFShaderLoadDesc skyboxShaderDesc = {};
        skyboxShaderDesc.mVert.pFileName = "skybox.vert";
        skyboxShaderDesc.mFrag.pFileName = "skybox.frag";

        addShader(pRenderer, &clearRenderTarget, &pShaderClearRenderTarget);
        addShader(pRenderer, &triangleCulling, &pShaderTriangleFiltering);
        addShader(pRenderer, &vbDepthRasterize, &pShaderVisibilityBufferDepthRaster);
        addShader(pRenderer, &vbDepthRasterizeSeparateAlpha[0], &pShaderVisibilityBufferDepthRasterSeparateAlpha[0]);
        addShader(pRenderer, &vbDepthRasterizeSeparateAlpha[1], &pShaderVisibilityBufferDepthRasterSeparateAlpha[1]);
        addShader(pRenderer, &blitDepth, &pShaderBlitDepth);

        for (uint32_t i = 0; i < 2; ++i)
            addShader(pRenderer, &vbShade[i], &pShaderVisibilityBufferShade[i]);
        addShader(pRenderer, &clearBuffer, &pShaderClearBuffers);
        addShader(pRenderer, &clearLights, &pShaderClearLightClusters);
        addShader(pRenderer, &clusterLights, &pShaderClusterLights);
        addShader(pRenderer, &skyboxShaderDesc, &pShaderSkybox);

        addShader(pRenderer, &presentShaderDesc, &pShaderPresentPass);
    }

    void removeShaders()
    {
        removeShader(pRenderer, pShaderClearRenderTarget);
        removeShader(pRenderer, pShaderTriangleFiltering);
        removeShader(pRenderer, pShaderVisibilityBufferDepthRaster);
        removeShader(pRenderer, pShaderVisibilityBufferDepthRasterSeparateAlpha[0]);
        removeShader(pRenderer, pShaderVisibilityBufferDepthRasterSeparateAlpha[1]);
        removeShader(pRenderer, pShaderBlitDepth);
        for (uint32_t i = 0; i < 2; ++i)
            removeShader(pRenderer, pShaderVisibilityBufferShade[i]);

        removeShader(pRenderer, pShaderClearBuffers);
        removeShader(pRenderer, pShaderClusterLights);
        removeShader(pRenderer, pShaderClearLightClusters);

        removeShader(pRenderer, pGodRayPass);
        removeShader(pRenderer, pShaderGodRayBlurPass);

        removeShader(pRenderer, pShaderSkybox);
        removeShader(pRenderer, pShaderCurveConversion);
        removeShader(pRenderer, pShaderPresentPass);
    }

    void addPipelines()
    {
        /************************************************************************/
        // Setup compute pipelines for triangle filtering
        /************************************************************************/
        TFPipelineDesc pipelineDesc = {};
        PIPELINE_LAYOUT_DESC(pipelineDesc, SRT_LAYOUT_DESC(TriangleFilteringCompSrtData, Persistent),
                             SRT_LAYOUT_DESC(TriangleFilteringCompSrtData, PerFrame),
                             SRT_LAYOUT_DESC(TriangleFilteringCompSrtData, PerBatch),
                             SRT_LAYOUT_DESC(TriangleFilteringCompSrtData, PerDraw));
        pipelineDesc.pCache = pPipelineCache;
        pipelineDesc.mType = TF_PIPELINE_TYPE_COMPUTE;
        TFComputePipelineDesc& compPipelineSettings = pipelineDesc.mComputeDesc;
        compPipelineSettings.pShaderProgram = pShaderClearBuffers;
        pipelineDesc.pName = "Clear Filtering Buffers";
        addPipeline(pRenderer, &pipelineDesc, &pPipelineClearBuffers);

        // Create the compute pipeline for GPU triangle filtering
        pipelineDesc.pName = "Triangle Filtering";
        compPipelineSettings.pShaderProgram = pShaderTriangleFiltering;
        addPipeline(pRenderer, &pipelineDesc, &pPipelineTriangleFiltering);

        // Setup the visibility buffer depth software rasterizer
        pipelineDesc.pName = "Visibility Buffer - Fill Depth";
        compPipelineSettings.pShaderProgram = pShaderVisibilityBufferDepthRaster;
        addPipeline(pRenderer, &pipelineDesc, &pPipelineVisibilityBufferDepthRaster);
        pipelineDesc.pName = "Visibility Buffer - Fill Depth (No alpha testing)";
        compPipelineSettings.pShaderProgram = pShaderVisibilityBufferDepthRasterSeparateAlpha[0];
        addPipeline(pRenderer, &pipelineDesc, &pPipelineVisibilityBufferDepthRasterSeparateAlpha[0]);
        pipelineDesc.pName = "Visibility Buffer - Fill Depth (Only alpha testing)";
        compPipelineSettings.pShaderProgram = pShaderVisibilityBufferDepthRasterSeparateAlpha[1];
        addPipeline(pRenderer, &pipelineDesc, &pPipelineVisibilityBufferDepthRasterSeparateAlpha[1]);

        // Setup the clear big triangles buffer
        pipelineDesc.pName = "Clear Render Target";
        compPipelineSettings.pShaderProgram = pShaderClearRenderTarget;
        addPipeline(pRenderer, &pipelineDesc, &pPipelineClearRenderTarget);

        PIPELINE_LAYOUT_DESC(pipelineDesc, SRT_LAYOUT_DESC(ClusterLightsSrtData, Persistent),
                             SRT_LAYOUT_DESC(ClusterLightsSrtData, PerFrame), NULL, SRT_LAYOUT_DESC(ClusterLightsSrtData, PerDraw));
        // Setup the clearing light clusters pipeline
        pipelineDesc.pName = "Clear Light Clusters";
        compPipelineSettings.pShaderProgram = pShaderClearLightClusters;
        addPipeline(pRenderer, &pipelineDesc, &pPipelineClearLightClusters);

        // Setup the compute the light clusters pipeline
        pipelineDesc.pName = "Cluster Lights";
        compPipelineSettings.pShaderProgram = pShaderClusterLights;
        addPipeline(pRenderer, &pipelineDesc, &pPipelineClusterLights);

        // God Ray Blur Pass
        PIPELINE_LAYOUT_DESC(pipelineDesc, SRT_LAYOUT_DESC(GodrayBlurCompSrtData, Persistent),
                             SRT_LAYOUT_DESC(GodrayBlurCompSrtData, PerFrame), NULL, SRT_LAYOUT_DESC(GodrayBlurCompSrtData, PerDraw));

        pipelineDesc.pName = "God Ray Blur";
        compPipelineSettings.pShaderProgram = pShaderGodRayBlurPass;
        addPipeline(pRenderer, &pipelineDesc, &pPipelineGodRayBlurPass);

        /************************************************************************/
        /************************************************************************/
        TFDepthStateDesc depthStateDisableDesc = {};

        TFRasterizerStateDesc rasterizerStateCullNoneDesc = { TF_CULL_MODE_NONE };

        TFBlendStateDesc blendStateSkyBoxDesc = {};
        blendStateSkyBoxDesc.mBlendModes[0] = TF_BM_ADD;
        blendStateSkyBoxDesc.mBlendAlphaModes[0] = TF_BM_ADD;

        blendStateSkyBoxDesc.mSrcFactors[0] = TF_BC_ONE_MINUS_DST_ALPHA;
        blendStateSkyBoxDesc.mDstFactors[0] = TF_BC_DST_ALPHA;

        blendStateSkyBoxDesc.mSrcAlphaFactors[0] = TF_BC_ZERO;
        blendStateSkyBoxDesc.mDstAlphaFactors[0] = TF_BC_ONE;

        blendStateSkyBoxDesc.mColorWriteMasks[0] = TF_COLOR_MASK_ALL;
        blendStateSkyBoxDesc.mRenderTargetMask = TF_BLEND_STATE_TARGET_0;
        blendStateSkyBoxDesc.mIndependentBlend = false;

        // Setup pipeline settings
        pipelineDesc = {};
        pipelineDesc.pCache = pPipelineCache;
        pipelineDesc.mType = TF_PIPELINE_TYPE_GRAPHICS;
        /************************************************************************/
        // Setup the resources needed for the Visibility Buffer Shade Pipeline
        /************************************************************************/
        // Create pipeline
        // Note: the vertex layout is set to null because the positions of the fullscreen triangle are being calculated automatically
        // in the vertex shader using each vertex_id.
        TFGraphicsPipelineDesc& vbShadePipelineSettings = pipelineDesc.mGraphicsDesc;
        vbShadePipelineSettings = { 0 };
        vbShadePipelineSettings.mPrimitiveTopo = TF_PRIMITIVE_TOPO_TRI_LIST;
        vbShadePipelineSettings.mRenderTargetCount = 1;
        vbShadePipelineSettings.pDepthState = &depthStateDisableDesc;
        vbShadePipelineSettings.pRasterizerState = &rasterizerStateCullNoneDesc;
        PIPELINE_LAYOUT_DESC(pipelineDesc, SRT_LAYOUT_DESC(SrtData, Persistent), SRT_LAYOUT_DESC(SrtData, PerFrame), NULL, NULL);
        for (uint32_t i = 0; i < 2; ++i)
        {
            vbShadePipelineSettings.pShaderProgram = pShaderVisibilityBufferShade[i];
            vbShadePipelineSettings.mSampleCount = TF_SAMPLE_COUNT_1;
            vbShadePipelineSettings.pColorFormats = &pIntermediateRenderTarget->mFormat;
            vbShadePipelineSettings.mSampleQuality = pSwapChain->ppRenderTargets[0]->mSampleQuality;

#if defined(XBOX)
            ExtendedGraphicsPipelineDesc edescs[2] = {};
            edescs[0].type = EXTENDED_GRAPHICS_PIPELINE_TYPE_SHADER_LIMITS;
            initExtendedGraphicsShaderLimits(&edescs[0].shaderLimitsDesc);
            // edescs[0].ShaderLimitsDesc.MaxWavesWithLateAllocParameterCache = 22;

            edescs[1].type = EXTENDED_GRAPHICS_PIPELINE_TYPE_PIXEL_SHADER_OPTIONS;
            edescs[1].pixelShaderOptions.outOfOrderRasterization = PIXEL_SHADER_OPTION_OUT_OF_ORDER_RASTERIZATION_ENABLE_WATER_MARK_7;
            edescs[1].pixelShaderOptions.depthBeforeShader =
                !i ? PIXEL_SHADER_OPTION_DEPTH_BEFORE_SHADER_ENABLE : PIXEL_SHADER_OPTION_DEPTH_BEFORE_SHADER_DEFAULT;

            pipelineDesc.pPipelineExtensions = edescs;
            pipelineDesc.mExtensionCount = sizeof(edescs) / sizeof(edescs[0]);
#endif
            pipelineDesc.pName = "VB Shade";
            addPipeline(pRenderer, &pipelineDesc, &pPipelineVisibilityBufferShadeSrgb[i]);

            pipelineDesc.mExtensionCount = 0;
        }
        /************************************************************************/
        // Setup blit depth pipeline
        /************************************************************************/
        TFDepthStateDesc depthStateWriteOnlyDesc = {};
        depthStateWriteOnlyDesc.mDepthTest = true;
        depthStateWriteOnlyDesc.mDepthWrite = true;
        depthStateWriteOnlyDesc.mDepthFunc = TF_CMP_ALWAYS;

        pipelineDesc = {};
        PIPELINE_LAYOUT_DESC(pipelineDesc, SRT_LAYOUT_DESC(TriangleFilteringCompSrtData, Persistent),
                             SRT_LAYOUT_DESC(TriangleFilteringCompSrtData, PerFrame),
                             SRT_LAYOUT_DESC(TriangleFilteringCompSrtData, PerBatch),
                             SRT_LAYOUT_DESC(TriangleFilteringCompSrtData, PerDraw));
        pipelineDesc.pCache = pPipelineCache;
        pipelineDesc.mType = TF_PIPELINE_TYPE_GRAPHICS;
        TFGraphicsPipelineDesc& blitDepthPipelineSettings = pipelineDesc.mGraphicsDesc;
        blitDepthPipelineSettings = { 0 };
        blitDepthPipelineSettings.mPrimitiveTopo = TF_PRIMITIVE_TOPO_TRI_LIST;
        blitDepthPipelineSettings.pDepthState = &depthStateWriteOnlyDesc;
        blitDepthPipelineSettings.mDepthStencilFormat = pDepthBuffer->mFormat;
        blitDepthPipelineSettings.mSampleCount = pDepthBuffer->mSampleCount;
        blitDepthPipelineSettings.mSampleQuality = pDepthBuffer->mSampleQuality;
        blitDepthPipelineSettings.pRasterizerState = &rasterizerStateCullNoneDesc;
        blitDepthPipelineSettings.pShaderProgram = pShaderBlitDepth;
        pipelineDesc.pName = "Blit Depth";
        addPipeline(pRenderer, &pipelineDesc, &pPipelineBlitDepth);

        /************************************************************************/
        // Setup Skybox pipeline
        /************************************************************************/

        // layout and pipeline for skybox draw
        TFVertexLayout vertexLayoutSkybox = {};
        vertexLayoutSkybox.mBindingCount = 1;
        vertexLayoutSkybox.mAttribCount = 1;
        vertexLayoutSkybox.mAttribs[0].mSemantic = TF_SEMANTIC_POSITION;
        vertexLayoutSkybox.mAttribs[0].mFormat = TinyImageFormat_R32G32B32A32_SFLOAT;
        vertexLayoutSkybox.mAttribs[0].mBinding = 0;
        vertexLayoutSkybox.mAttribs[0].mLocation = 0;
        vertexLayoutSkybox.mAttribs[0].mOffset = 0;
        PIPELINE_LAYOUT_DESC(pipelineDesc, SRT_LAYOUT_DESC(SrtData, Persistent), SRT_LAYOUT_DESC(SrtData, PerFrame), NULL, NULL);
        TFGraphicsPipelineDesc& pipelineSettings = pipelineDesc.mGraphicsDesc;
        pipelineSettings = { 0 };
        pipelineSettings.mPrimitiveTopo = TF_PRIMITIVE_TOPO_TRI_LIST;
        pipelineSettings.mRenderTargetCount = 1;
        pipelineSettings.pDepthState = NULL;

        pipelineSettings.pBlendState = &blendStateSkyBoxDesc;

        pipelineSettings.pColorFormats = &pIntermediateRenderTarget->mFormat;
        pipelineSettings.mSampleCount = pSwapChain->ppRenderTargets[0]->mSampleCount;
        pipelineSettings.mSampleQuality = pSwapChain->ppRenderTargets[0]->mSampleQuality;
        // pipelineSettings.mDepthStencilFormat = pDepthBuffer->mFormat;
        pipelineSettings.pShaderProgram = pShaderSkybox;
        pipelineSettings.pVertexLayout = &vertexLayoutSkybox;
        pipelineSettings.pRasterizerState = &rasterizerStateCullNoneDesc;
        pipelineDesc.pName = "Skybox";
        addPipeline(pRenderer, &pipelineDesc, &pSkyboxPipeline);

        /************************************************************************/
        // Setup Godray pipeline
        /************************************************************************/

        TFGraphicsPipelineDesc& pipelineSettingsGodRay = pipelineDesc.mGraphicsDesc;
        pipelineSettingsGodRay = { 0 };
        pipelineSettingsGodRay.mPrimitiveTopo = TF_PRIMITIVE_TOPO_TRI_LIST;
        pipelineSettingsGodRay.pRasterizerState = &rasterizerStateCullNoneDesc;
        pipelineSettingsGodRay.mRenderTargetCount = 1;
        pipelineSettingsGodRay.pColorFormats = &pRenderTargetGodRay[0]->mFormat;
        pipelineSettingsGodRay.mSampleCount = pSwapChain->ppRenderTargets[0]->mSampleCount;
        pipelineSettingsGodRay.mSampleQuality = pSwapChain->ppRenderTargets[0]->mSampleQuality;
        pipelineSettingsGodRay.pShaderProgram = pGodRayPass;
        pipelineDesc.pName = "God Ray";
        addPipeline(pRenderer, &pipelineDesc, &pPipelineGodRayPass);

        /************************************************************************/
        // Setup Curve Conversion pipeline
        /************************************************************************/

        TFGraphicsPipelineDesc& pipelineSettingsCurveConversion = pipelineDesc.mGraphicsDesc;
        pipelineSettingsCurveConversion = { 0 };
        pipelineSettingsCurveConversion.mPrimitiveTopo = TF_PRIMITIVE_TOPO_TRI_LIST;

        pipelineSettingsCurveConversion.pRasterizerState = &rasterizerStateCullNoneDesc;

        pipelineSettingsCurveConversion.mRenderTargetCount = 1;
        pipelineSettingsCurveConversion.pColorFormats = &pCurveConversionRenderTarget->mFormat;
        pipelineSettingsCurveConversion.mSampleCount = pSwapChain->ppRenderTargets[0]->mSampleCount;
        pipelineSettingsCurveConversion.mSampleQuality = pSwapChain->ppRenderTargets[0]->mSampleQuality;
        pipelineSettingsCurveConversion.pShaderProgram = pShaderCurveConversion;
        pipelineDesc.pName = "Curve Conversion";
        addPipeline(pRenderer, &pipelineDesc, &pPipelineCurveConversionPass);

        /************************************************************************/
        // Setup Present pipeline
        /************************************************************************/

        TFGraphicsPipelineDesc& pipelineSettingsFinalPass = pipelineDesc.mGraphicsDesc;
        pipelineSettingsFinalPass = { 0 };
        pipelineSettingsFinalPass.mPrimitiveTopo = TF_PRIMITIVE_TOPO_TRI_LIST;
        pipelineSettingsFinalPass.pRasterizerState = &rasterizerStateCullNoneDesc;
        pipelineSettingsFinalPass.mRenderTargetCount = 1;
        pipelineSettingsFinalPass.pColorFormats = &pSwapChain->ppRenderTargets[0]->mFormat;
        pipelineSettingsFinalPass.mSampleCount = pSwapChain->ppRenderTargets[0]->mSampleCount;
        pipelineSettingsFinalPass.mSampleQuality = pSwapChain->ppRenderTargets[0]->mSampleQuality;
        pipelineSettingsFinalPass.pShaderProgram = pShaderPresentPass;
        PIPELINE_LAYOUT_DESC(pipelineDesc, SRT_LAYOUT_DESC(DisplaySrtData, Persistent), SRT_LAYOUT_DESC(DisplaySrtData, PerFrame), NULL,
                             SRT_LAYOUT_DESC(DisplaySrtData, PerDraw));
        pipelineDesc.pName = "Composite";
        addPipeline(pRenderer, &pipelineDesc, &pPipelinePresentPass);
    }

    void removePipelines()
    {
        removePipeline(pRenderer, pPipelineGodRayPass);
        removePipeline(pRenderer, pPipelineGodRayBlurPass);

        removePipeline(pRenderer, pPipelineCurveConversionPass);
        removePipeline(pRenderer, pPipelinePresentPass);

        for (uint32_t i = 0; i < 2; ++i)
        {
            removePipeline(pRenderer, pPipelineVisibilityBufferShadeSrgb[i]);
        }

        removePipeline(pRenderer, pSkyboxPipeline);

        // Destroy triangle filtering pipelines
        removePipeline(pRenderer, pPipelineClusterLights);
        removePipeline(pRenderer, pPipelineClearLightClusters);
        removePipeline(pRenderer, pPipelineTriangleFiltering);
        removePipeline(pRenderer, pPipelineClearBuffers);

        removePipeline(pRenderer, pPipelineVisibilityBufferDepthRaster);
        removePipeline(pRenderer, pPipelineVisibilityBufferDepthRasterSeparateAlpha[0]);
        removePipeline(pRenderer, pPipelineVisibilityBufferDepthRasterSeparateAlpha[1]);
        removePipeline(pRenderer, pPipelineBlitDepth);
        removePipeline(pRenderer, pPipelineClearRenderTarget);
    }

    // This method sets the contents of the buffers to indicate the rendering pass that
    // the whole scene triangles must be rendered (no cluster / triangle filtering).
    // This is useful for testing purposes to compare visual / performance results.
    void addTriangleFilteringBuffers()
    {
        /************************************************************************/
        // Material props
        /************************************************************************/
        uint32_t* alphaTestMaterials = (uint32_t*)tf_malloc(gMeshCount * sizeof(uint32_t));
        for (uint32_t meshIndex = 0; meshIndex < gMeshCount; ++meshIndex)
        {
            alphaTestMaterials[meshIndex] =
                (gPackage->pTextureMetadata->pMaterialProps[meshIndex].mFlags & (MATERIAL_FLAG_ALPHA_TESTED | MATERIAL_FLAG_TRANSPARENT))
                    ? 1
                    : 0;
        }

        TFBufferLoadDesc materialPropDesc = {};
        materialPropDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_BUFFER;
        materialPropDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_GPU_ONLY;
        materialPropDesc.mDesc.mElementCount = (uint32_t)gMeshCount;
        materialPropDesc.mDesc.mStructStride = sizeof(uint32_t);
        materialPropDesc.mDesc.mSize = materialPropDesc.mDesc.mElementCount * materialPropDesc.mDesc.mStructStride;
        materialPropDesc.pData = alphaTestMaterials;
        materialPropDesc.ppBuffer = &pMaterialPropertyBuffer;
        materialPropDesc.mDesc.pName = "Material Prop Desc";
        addResource(&materialPropDesc, NULL);

        /************************************************************************/
        // Mesh constants
        /************************************************************************/
        // create mesh constants buffer
        MeshConstants* meshConstants = (MeshConstants*)tf_malloc(gMeshCount * sizeof(MeshConstants));

        for (uint32_t i = 0; i < gMeshCount; ++i)
        {
            meshConstants[i].indexOffset = pGeom->pDrawArgs[i].mStartIndex;
            meshConstants[i].vertexOffset = pGeom->pDrawArgs[i].mVertexOffset;
            meshConstants[i].flags = gPackage->pTextureMetadata->pMaterialProps[i].mFlags;
        }

        TFBufferLoadDesc meshConstantDesc = {};
        meshConstantDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_BUFFER;
        meshConstantDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_GPU_ONLY;
        meshConstantDesc.mDesc.mElementCount = (uint32_t)gMeshCount;
        meshConstantDesc.mDesc.mStructStride = sizeof(MeshConstants);
        meshConstantDesc.mDesc.mSize = meshConstantDesc.mDesc.mElementCount * meshConstantDesc.mDesc.mStructStride;
        meshConstantDesc.pData = meshConstants;
        meshConstantDesc.ppBuffer = &pMeshConstantsBuffer;
        meshConstantDesc.mDesc.pName = "Mesh Constant Desc";

        addResource(&meshConstantDesc, NULL);
        /************************************************************************/
        // Per Frame Constant Buffers
        /************************************************************************/
        uint64_t         size = sizeof(PerFrameConstantsData);
        TFBufferLoadDesc ubDesc = {};
        ubDesc.mDesc.mSize = size;
        ubDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        ubDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
        ubDesc.mDesc.mFlags = TF_BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
        ubDesc.pData = NULL;
        ubDesc.mDesc.pName = "Uniform Buffer Desc";

        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            ubDesc.ppBuffer = &pPerFrameUniformBuffers[i];
            addResource(&ubDesc, NULL);
        }

        ubDesc.mDesc.mSize = sizeof(VBViewConstantsData);
        ubDesc.mDesc.pName = "gVBViewConstants Uniform Buffer Desc";
        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            ubDesc.ppBuffer = &pPerFrameVBUniformBuffers[VB_UB_COMPUTE][i];
            addResource(&ubDesc, NULL);
            ubDesc.ppBuffer = &pPerFrameVBUniformBuffers[VB_UB_GRAPHICS][i];
            addResource(&ubDesc, NULL);
        }
        /************************************************************************/
        // Lighting buffers
        /************************************************************************/
        // Setup lights uniform buffer
        // It should cover the courtyard area with tree...
        for (uint32_t i = 0; i < LIGHT_COUNT; i++)
        {
            gLightData[i].position.x = (randomFloat(-90.0f, 55.0f));
            gLightData[i].position.y = (40.0f);
            gLightData[i].position.z = (randomFloat(-15.0f, 120.0f));
            gLightData[i].color.x = (randomFloat01());
            gLightData[i].color.y = (randomFloat01());
            gLightData[i].color.z = (randomFloat01());
        }
        TFBufferLoadDesc batchUb = {};
        batchUb.mDesc.mSize = sizeof(gLightData);
        batchUb.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_BUFFER | TF_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        batchUb.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_GPU_ONLY;
        batchUb.mDesc.mFirstElement = 0;
        batchUb.mDesc.mElementCount = LIGHT_COUNT;
        batchUb.mDesc.mStructStride = sizeof(LightData);
        batchUb.pData = gLightData;
        batchUb.ppBuffer = &pLightsBuffer;
        batchUb.mDesc.pName = "Lights Desc";
        addResource(&batchUb, NULL);

        // Setup lights cluster data
        uint32_t         lightClustersInitData[LIGHT_CLUSTER_WIDTH * LIGHT_CLUSTER_HEIGHT] = {};
        TFBufferLoadDesc lightClustersCountBufferDesc = {};
        lightClustersCountBufferDesc.mDesc.mSize = LIGHT_CLUSTER_WIDTH * LIGHT_CLUSTER_HEIGHT * sizeof(uint32_t);
        lightClustersCountBufferDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_BUFFER_RAW | TF_DESCRIPTOR_TYPE_RW_BUFFER;
        lightClustersCountBufferDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_GPU_ONLY;
        lightClustersCountBufferDesc.mDesc.mFirstElement = 0;
        lightClustersCountBufferDesc.mDesc.mElementCount = LIGHT_CLUSTER_WIDTH * LIGHT_CLUSTER_HEIGHT;
        lightClustersCountBufferDesc.mDesc.mStructStride = sizeof(uint32_t);
        lightClustersCountBufferDesc.mDesc.mStartState = TF_RESOURCE_STATE_UNORDERED_ACCESS;
        lightClustersCountBufferDesc.pData = lightClustersInitData;
        lightClustersCountBufferDesc.mDesc.pName = "Light Cluster Count Buffer Desc";
        for (uint32_t frameIdx = 0; frameIdx < gDataBufferCount; ++frameIdx)
        {
            lightClustersCountBufferDesc.ppBuffer = &pLightClustersCount[frameIdx];
            addResource(&lightClustersCountBufferDesc, NULL);
        }

        TFBufferLoadDesc lightClustersDataBufferDesc = {};
        lightClustersDataBufferDesc.mDesc.mSize = LIGHT_COUNT * LIGHT_CLUSTER_WIDTH * LIGHT_CLUSTER_HEIGHT * sizeof(uint32_t);
        lightClustersDataBufferDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_BUFFER_RAW | TF_DESCRIPTOR_TYPE_RW_BUFFER;
        lightClustersDataBufferDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_GPU_ONLY;
        lightClustersDataBufferDesc.mDesc.mFirstElement = 0;
        lightClustersDataBufferDesc.mDesc.mElementCount = LIGHT_COUNT * LIGHT_CLUSTER_WIDTH * LIGHT_CLUSTER_HEIGHT;
        lightClustersDataBufferDesc.mDesc.mStructStride = sizeof(uint32_t);
        lightClustersDataBufferDesc.mDesc.mStartState = TF_RESOURCE_STATE_UNORDERED_ACCESS;
        lightClustersDataBufferDesc.pData = NULL;
        lightClustersDataBufferDesc.mDesc.pName = "Light Cluster Data Buffer Desc";
        for (uint32_t frameIdx = 0; frameIdx < gDataBufferCount; ++frameIdx)
        {
            lightClustersDataBufferDesc.ppBuffer = &pLightClusters[frameIdx];
            addResource(&lightClustersDataBufferDesc, NULL);
        }

        TFBufferLoadDesc skyDataBufferDesc = {};
        skyDataBufferDesc.mDesc.mSize = sizeof(UniformDataSkybox);
        skyDataBufferDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        skyDataBufferDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
        skyDataBufferDesc.mDesc.mFlags = TF_BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
        skyDataBufferDesc.pData = NULL;
        skyDataBufferDesc.mDesc.pName = "Sky Uniforms";
        for (uint32_t frameIdx = 0; frameIdx < gDataBufferCount; ++frameIdx)
        {
            skyDataBufferDesc.ppBuffer = &pUniformBufferSky[frameIdx];
            addResource(&skyDataBufferDesc, NULL);
        }
        waitForAllResourceLoads();

        tf_free(alphaTestMaterials);
        tf_free(meshConstants);
    }

    void removeTriangleFilteringBuffers()
    {
        /************************************************************************/
        // Material props
        /************************************************************************/
        removeResource(pMaterialPropertyBuffer);

        /************************************************************************/
        // Mesh constants
        /************************************************************************/
        removeResource(pMeshConstantsBuffer);

        /************************************************************************/
        // Per Frame Constant Buffers
        /************************************************************************/
        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            removeResource(pPerFrameUniformBuffers[i]);
        }

        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            removeResource(pPerFrameVBUniformBuffers[VB_UB_COMPUTE][i]);
            removeResource(pPerFrameVBUniformBuffers[VB_UB_GRAPHICS][i]);
        }
        /************************************************************************/
        // Lighting buffers
        /************************************************************************/
        removeResource(pLightsBuffer);
        for (uint32_t frameIdx = 0; frameIdx < gDataBufferCount; ++frameIdx)
        {
            removeResource(pLightClustersCount[frameIdx]);
            removeResource(pLightClusters[frameIdx]);
            removeResource(pUniformBufferSky[frameIdx]);
        }
        /************************************************************************/
        /************************************************************************/
    }

    /************************************************************************/
    // Scene update
    /************************************************************************/
    // Updates uniform data for the given frame index.
    // This includes transform matrices, render target resolution and global information about the scene.
    void updateUniformData(uint currentFrameIdx)
    {
        const uint32_t width = gSceneRes.mWidth;
        const uint32_t height = gSceneRes.mHeight;
        const float    aspectRatioInv = (float)height / width;
        const uint32_t frameIdx = currentFrameIdx;
        PerFrameData*  currentFrame = &gPerFrame[frameIdx];

        mat4  cameraModel = mat4::scale(vec3(SCENE_SCALE));
        mat4  cameraView = pCamera->getViewMatrix().mMatrices[MONO_CAMERA_VIEW_INDEX];
        mat4  cameraProj = mat4::perspectiveLH_ReverseZ(PI / 2.0f, aspectRatioInv, gAppSettings.nearPlane, gAppSettings.farPlane);
        float timeNormalized =
            (gAppSettings.mTimeOfDay - gAppSettings.mTimeOfDaySunrise) / (gAppSettings.mTimeOfDaySunset - gAppSettings.mTimeOfDaySunrise);

        // light direction (for BRDFs)
        float sunRotation = lerp(0.0f, PI, timeNormalized);
        mat4  rotation = mat4::rotationYX(gAppSettings.mSunriseDirection, sunRotation);
        vec3  lightDir = (rotation * vec4::zAxis()).getXYZ();

        // shadow direction (for shadowmap camera)
        float shadowPadding = (1.0f - gAppSettings.mShadowRange) / 2.0f;
        float shadowRotationTime = lerp(shadowPadding, 1.0f - shadowPadding, timeNormalized);
        float sunRotationShadow = lerp(0.0f, PI, shadowRotationTime);
        mat4  rotationShadow = mat4::rotationYX(gAppSettings.mSunriseDirection, sunRotationShadow);
        vec3  shadowDir = (rotationShadow * vec4::zAxis()).getXYZ();

        float shadowCameraBounds = 12.0f * SCENE_SCALE;
        mat4  shadowModel = mat4::scale(vec3(SCENE_SCALE));

        float3 shadowCameraPivot = float3(-.927f, 13.93f, 6.815f) * SCENE_SCALE;
        float3 shadowCameraOffset = (-shadowDir * 25.0f) * SCENE_SCALE;
        mat4   translation = mat4::translation(-(shadowCameraPivot + shadowCameraOffset));

        mat4   shadowView = transpose(rotationShadow) * translation;
        mat4   shadowProj = mat4::orthographicLH_ReverseZ(-shadowCameraBounds, shadowCameraBounds, -shadowCameraBounds, shadowCameraBounds,
                                                        gAppSettings.lightNearPlane, gAppSettings.lightFarPlane);
        float2 twoOverRes = { 2.0f / float(width), 2.0f / float(height) };

        /************************************************************************/
        // Lighting data
        /************************************************************************/
        currentFrame->gPerFrameUniformData.camPos = (vec4(pCamera->getViewPosition()));
        currentFrame->gPerFrameUniformData.lightDir = (vec4(lightDir));
        currentFrame->gPerFrameUniformData.twoOverRes = twoOverRes;
        currentFrame->gPerFrameUniformData.esmControl = gAppSettings.mEsmControl;
        currentFrame->gPerFrameUniformData.mGodRayScatterFactor = gGodRayConstant.mScatterFactor;
        /************************************************************************/
        // Matrix data
        /************************************************************************/
        currentFrame->gVBViewUniformData.transform[VIEW_SHADOW].vp = shadowProj * shadowView;
        currentFrame->gVBViewUniformData.transform[VIEW_SHADOW].view = shadowView;
        currentFrame->gVBViewUniformData.transform[VIEW_SHADOW].invVP = inverse(currentFrame->gVBViewUniformData.transform[VIEW_SHADOW].vp);
        currentFrame->gVBViewUniformData.transform[VIEW_SHADOW].projection = shadowProj;
        currentFrame->gVBViewUniformData.transform[VIEW_SHADOW].mvp =
            currentFrame->gVBViewUniformData.transform[VIEW_SHADOW].vp * shadowModel;
        currentFrame->gVBViewUniformData.transform[VIEW_SHADOW].cameraPlane = { gAppSettings.lightNearPlane, gAppSettings.lightFarPlane };

        currentFrame->gVBViewUniformData.transform[VIEW_CAMERA].vp = cameraProj * cameraView;
        currentFrame->gVBViewUniformData.transform[VIEW_CAMERA].view = cameraView;
        currentFrame->gVBViewUniformData.transform[VIEW_CAMERA].invVP = inverse(currentFrame->gVBViewUniformData.transform[VIEW_CAMERA].vp);
        currentFrame->gVBViewUniformData.transform[VIEW_CAMERA].projection = cameraProj;
        currentFrame->gVBViewUniformData.transform[VIEW_CAMERA].mvp =
            currentFrame->gVBViewUniformData.transform[VIEW_CAMERA].vp * cameraModel;
        currentFrame->gVBViewUniformData.transform[VIEW_CAMERA].cameraPlane = { gAppSettings.nearPlane, gAppSettings.farPlane };

        /************************************************************************/
        // Culling data
        /************************************************************************/
        currentFrame->gVBViewUniformData.cullingViewports[VIEW_SHADOW].sampleCount = 1;
        currentFrame->gVBViewUniformData.cullingViewports[VIEW_SHADOW].windowSize = { (float)gShadowMapSize, (float)gShadowMapSize };

        currentFrame->gVBViewUniformData.cullingViewports[VIEW_CAMERA].sampleCount = TF_SAMPLE_COUNT_1;
        currentFrame->gVBViewUniformData.cullingViewports[VIEW_CAMERA].windowSize = { (float)width, (float)height };

        // Cache eye position in object space for cluster culling on the CPU
        currentFrame->gEyeObjectSpace[VIEW_SHADOW] = (inverse(shadowView * shadowModel) * vec4(0, 0, 0, 1)).getXYZ();
        currentFrame->gEyeObjectSpace[VIEW_CAMERA] =
            (inverse(cameraView * cameraModel) * vec4(0, 0, 0, 1)).getXYZ(); // vec4(0,0,0,1) is the camera position in eye space
        /************************************************************************/
        // Shading data
        /************************************************************************/
        currentFrame->gPerFrameUniformData.lightColor = gAppSettings.mLightColor;
        currentFrame->gPerFrameUniformData.outputMode = (uint)gAppSettings.mOutputMode;
        currentFrame->gPerFrameUniformData.CameraPlane = { gAppSettings.nearPlane, gAppSettings.farPlane };
        currentFrame->gPerFrameUniformData.aoQuality = gAppSettings.mEnableAO ? gAppSettings.mAOQuality : 0;
        currentFrame->gPerFrameUniformData.aoIntensity = gAppSettings.mAOIntensity;
        currentFrame->gPerFrameUniformData.frustumPlaneSizeNormalized = frustumPlaneSizeFovX(PI / 2.0f, (float)width / (float)height, 1.0f);
        currentFrame->gPerFrameUniformData.visualizeAo = gAppSettings.mVisualizeAO;
        currentFrame->gPerFrameUniformData.featureSelection =
            FeatureSelection(gAppSettings.mSmallScaleRaster, gAppSettings.mSeparateAlphaTestRasterization);
        currentFrame->gPerFrameUniformData.visualizeGeometry = gAppSettings.mVisualizeGeometry;
        currentFrame->gPerFrameUniformData.visualizeBinTriangleCount = gAppSettings.mVisualizeBinTriangleCount;
        currentFrame->gPerFrameUniformData.visualizeBinOccupancy = gAppSettings.mVisualizeBinOccupancy;
        currentFrame->gPerFrameUniformData.depthTexSize = { (float)pDepthBuffer->mWidth, (float)pDepthBuffer->mHeight };
        /************************************************************************/
        // Skybox
        /************************************************************************/
        cameraView.setTranslation(vec3(0));
        currentFrame->gUniformDataSky.mCamPos = pCamera->getViewPosition();
        currentFrame->gUniformDataSky.mProjectView = cameraProj * cameraView;
        /************************************************************************/
        // Tonemap
        /************************************************************************/
        currentFrame->gPerFrameUniformData.mLinearScale = gAppSettings.LinearScale;
        currentFrame->gPerFrameUniformData.mOutputMode = gAppSettings.mOutputMode;

        /************************************************************************/
        // Render Target Info - Used in rasterization
        /************************************************************************/
        gShadowRenderTargetInfo.view = VIEW_SHADOW;
        gShadowRenderTargetInfo.width = gShadowMapSize;
        gShadowRenderTargetInfo.height = gShadowMapSize;

        gDepthRenderTargetInfo.view = VIEW_CAMERA;
        gDepthRenderTargetInfo.width = (int)gSceneRes.mWidth;
        gDepthRenderTargetInfo.height = (int)gSceneRes.mHeight;

        int size = gSceneRes.mWidth * gSceneRes.mHeight + gShadowMapSize * gShadowMapSize;
        gClearRenderTargetInfo = { 0U, size, 0 }; // dims
    }
    /************************************************************************/
    // UI
    /************************************************************************/
    void updateDynamicUIElements()
    {
        // Async compute
        {
            static bool gPrevAsyncCompute = gAppSettings.mAsyncCompute;
            if (gPrevAsyncCompute != gAppSettings.mAsyncCompute)
            {
                gPrevAsyncCompute = gAppSettings.mAsyncCompute;
                waitQueueIdle(pGraphicsQueue);
                waitQueueIdle(pComputeQueue);
                gPrevGraphicsSemaphore = NULL;
                memset(gComputeSemaphores, 0, sizeof(gComputeSemaphores));
            }
        }

        // AO
        gAppSettings.mVisualizeAO &= gAppSettings.mEnableAO;
    }
    /************************************************************************/
    // Rendering
    /************************************************************************/

    void blitVisibilityBufferDepthPass(TFCmd* cmd, uint32_t view, uint32_t frameIdx, TFRenderTarget* depthTarget)
    {
        // Resource Transition
        {
            TFRenderTargetBarrier rtBarriers[] = {
                { depthTarget, TF_RESOURCE_STATE_SHADER_RESOURCE, TF_RESOURCE_STATE_DEPTH_WRITE },
            };
            cmdResourceBarrier(cmd, 0, NULL, 0, NULL, 1, rtBarriers);
        }
        // Bind Render Targets
        {
            // Render target is cleared to (1,1,1,1) because (0,0,0,0) represents the first triangle of the first draw batch
            TFBindRenderTargetsDesc bindRenderTargets = {};
            bindRenderTargets.mDepthStencil = { depthTarget, TF_LOAD_ACTION_CLEAR };

            RenderTargetInfo   data = { view, (int)depthTarget->mWidth, (int)depthTarget->mHeight };
            TFBufferUpdateDesc bufferUpdate = { pRenderTargetInfoConstantsBuffers[frameIdx][view] };
            beginUpdateResource(&bufferUpdate);
            memcpy(bufferUpdate.pMappedData, &data, sizeof(RenderTargetInfo));
            endUpdateResource(&bufferUpdate);

            // Start render pass and apply load actions
            cmdBindRenderTargets(cmd, &bindRenderTargets);
            cmdSetViewport(cmd, 0.0f, 0.0f, (float)depthTarget->mWidth, (float)depthTarget->mHeight, 0.0f, 1.0f);
            cmdSetScissor(cmd, 0, 0, depthTarget->mWidth, depthTarget->mHeight);
        }
        // Draw
        {
            cmdBindPipeline(cmd, pPipelineBlitDepth);
            cmdBindDescriptorSet(cmd, 0, pDescriptorSetPersistent);
            cmdBindDescriptorSet(cmd, frameIdx, pDescriptorSetPerFrame);
            cmdBindDescriptorSet(cmd, frameIdx * 3 + (int)view, pDescriptorSetRenderTargetPerBatch);
            // A single triangle is rendered without specifying a vertex buffer (triangle positions are calculated internally using
            // vertex_id)
            cmdDraw(cmd, 3, 0);
            cmdBindRenderTargets(cmd, NULL);
        }
        // Resource Transition
        {
            TFRenderTargetBarrier rtBarriers[] = {
                { depthTarget, TF_RESOURCE_STATE_DEPTH_WRITE, TF_RESOURCE_STATE_SHADER_RESOURCE },
            };
            cmdResourceBarrier(cmd, 0, NULL, 0, NULL, 1, rtBarriers);
        }
    }

    void updateRenderTargetInfo(uint32_t frameIdx)
    {
        // Update clear vb render target info (combined view pass)
        TFBufferUpdateDesc bufferUpdate = { pRenderTargetInfoConstantsBuffers[frameIdx][2] };
        beginUpdateResource(&bufferUpdate);
        memcpy(bufferUpdate.pMappedData, &gClearRenderTargetInfo, sizeof(RenderTargetInfo));
        endUpdateResource(&bufferUpdate);

        // Update main view render target data
        bufferUpdate = { pRenderTargetInfoConstantsBuffers[frameIdx][VIEW_CAMERA] };
        beginUpdateResource(&bufferUpdate);
        memcpy(bufferUpdate.pMappedData, &gDepthRenderTargetInfo, sizeof(RenderTargetInfo));
        endUpdateResource(&bufferUpdate);

        // Update shadow
        bufferUpdate = { pRenderTargetInfoConstantsBuffers[frameIdx][VIEW_SHADOW] };
        beginUpdateResource(&bufferUpdate);
        memcpy(bufferUpdate.pMappedData, &gShadowRenderTargetInfo, sizeof(RenderTargetInfo));
        endUpdateResource(&bufferUpdate);
    }
};

DEFINE_APPLICATION_MAIN(Visibility_Buffer)
