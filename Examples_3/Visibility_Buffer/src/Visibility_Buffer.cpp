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
#include "../../../Common_3/Application/Interfaces/IFont.h"
#include "../../../Common_3/Application/Interfaces/IProfiler.h"
#include "../../../Common_3/Application/Interfaces/IScreenshot.h"
#include "../../../Common_3/Application/Interfaces/IUI.h"
#include "../../../Common_3/Game/Interfaces/IScripting.h"
#include "../../../Common_3/Graphics/Interfaces/IGraphics.h"
#include "../../../Common_3/OS/Interfaces/IInput.h"
#include "../../../Common_3/Renderer/Interfaces/IVisibilityBuffer.h"
#include "../../../Common_3/Utilities/Interfaces/IFileSystem.h"
#include "../../../Common_3/Utilities/Interfaces/ILog.h"
#include "../../../Common_3/Utilities/Interfaces/IThread.h"
#include "../../../Common_3/Utilities/Interfaces/ITime.h"

#include "../../../Common_3/Utilities/RingBuffer.h"
#include "../../../Common_3/Utilities/Threading/ThreadSystem.h"

// fsl
#include "../../../Common_3/Graphics/FSL/defaults.h"
#include "Shaders/FSL/ShaderDefs.h.fsl"
#include "Shaders/FSL/GLobal.srt.h"
#include "Shaders/FSL/GodrayBlur.srt.h"
#include "Shaders/FSL/LightClusters.srt.h"
#include "Shaders/FSL/ProgMSAAResolve.srt.h"
#include "Shaders/FSL/TriangleFiltering.srt.h"
#include "Shaders/FSL/ShadeCompute.srt.h"

#include "../../../Common_3/Utilities/Interfaces/IMemory.h"

#define FOREACH_SETTING(X)       \
    X(BindlessSupported, 1)      \
    X(DisableAO, 0)              \
    X(DisableGodRays, 0)         \
    X(MSAASampleCount, 4)        \
    X(AddGeometryPassThrough, 0) \
    X(MaxMSAALevel, 4)           \
    X(DisableAsyncCompute, 0)    \
    X(EnableHDR, 0)

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

#define SCENE_SCALE                 10.0f
#define MIN_BINDLESS_TEXTURES_COUNT 1024

typedef enum OutputMode
{
    OUTPUT_MODE_SDR = 0,
    OUTPUT_MODE_P2020,
    OUTPUT_MODE_COUNT
} OutputMode;

struct UniformShadingData
{
    float4 lightColor;
    uint   lightingMode;
    uint   outputMode;
    float4 CameraPlane; // x : near, y : far
};

struct GodRayConstant
{
    float mScatterFactor;
};

GodRayConstant gGodRayConstant{ 0.5f };

struct GodRayBlurConstant
{
    uint32_t mBlurPassType; // Horizontal or Vertical pass
    uint32_t mFilterRadius;
};

#define MAX_BLUR_KERNEL_SIZE 8

struct gBlurWeights
{
    float mBlurWeights[MAX_BLUR_KERNEL_SIZE];
};

GodRayBlurConstant gGodRayBlurConstant;
gBlurWeights       gBlurWeightsUniform;
float              gGaussianBlurSigma[2] = { 1.0f, 1.0f };

bool gUsingTextureAtlasFallback = false;
bool gUsingPrimitiveIDFallback = false;

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

// static const DisplayChromacities DisplayChromacityList[] =
//{
//	{ 0.64000f, 0.33000f, 0.30000f, 0.60000f, 0.15000f, 0.06000f, 0.31270f, 0.32900f }, // Display Gamut Rec709
//	{ 0.70800f, 0.29200f, 0.17000f, 0.79700f, 0.13100f, 0.04600f, 0.31270f, 0.32900f }, // Display Gamut Rec2020
//	{ 0.68000f, 0.32000f, 0.26500f, 0.69000f, 0.15000f, 0.06000f, 0.31270f, 0.32900f }, // Display Gamut P3D65
//	{ 0.68000f, 0.32000f, 0.26500f, 0.69000f, 0.15000f, 0.06000f, 0.31400f, 0.35100f }, // Display Gamut P3DCI(Theater)
//	{ 0.68000f, 0.32000f, 0.26500f, 0.69000f, 0.15000f, 0.06000f, 0.32168f, 0.33767f }, // Display Gamut P3D60(ACES Cinema)
// };

// Camera Walking
static float gCameraWalkingTime = 0.0f;
float3*      gCameraPathData;

uint  gCameraPoints;
float gTotalElpasedTime;
/************************************************************************/
// GUI CONTROLS
/************************************************************************/
#define MSAA_LEVELS_COUNT 3U
#if defined(ANDROID)
#define DEFAULT_ASYNC_COMPUTE false
#else
#define DEFAULT_ASYNC_COMPUTE true
#endif

static uint32_t getSupportedMSAAUIOptionCount(TFSampleCount maxMSAALevel, uint32_t supportedSampleCounts)
{
    uint32_t count = 1;
    for (uint32_t i = 1; i < MSAA_LEVELS_COUNT; ++i)
    {
        const TFSampleCount sampleCount = (TFSampleCount)(1u << i);
        if (sampleCount <= maxMSAALevel && (supportedSampleCounts & sampleCount))
            count = i + 1;
    }

    return count;
}

typedef struct AppSettings
{ //-V802
    OutputMode mOutputMode = OUTPUT_MODE_SDR;

    // Set this variable to true to bypass triangle filtering calculations, holding and representing the last filtered data.
    // This is useful for inspecting filtered geometry for debugging purposes.
    bool mHoldFilteredResults = false;

    // Multi-material visibility buffer path. Writes material IDs in the VB pass and shades by material shader
    bool mHandleMultipleMaterials = false;

    // Single-sample compute pixel-list shade path. MSAA uses the Grid-Tiles quad path
    bool mUseComputePixelList = true;

    bool mAsyncCompute = DEFAULT_ASYNC_COMPUTE;
    // Same-frame async build for the compute pixel-list path.
    bool mAsyncComputePixelListBuild = false;
    // toggle rendering of local point lights
    bool mRenderLocalLights = false;

    bool mDrawDebugTargets = false;

    float nearPlane = 0.1f;
    float farPlane = 1000.0f;

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
    float  mGodrayAttenuation = 0.3f;

    bool     mEnableGodray = true;
    uint32_t mFilterRadius = 3;

    float mEsmControl = 200.0f;

    // AO data
    bool  mEnableAO = true;
    bool  mVisualizeAO = false;
    float mAOIntensity = 3.0f;
    int   mAOQuality = 2;

    float LinearScale = 260.0f;

    // HDR10
    // DynamicUIWidgets mDisplaySetting;

    DisplayColorSpace  mCurrentSwapChainColorSpace = ColorSpace_Rec2020;
    DisplayColorRange  mDisplayColorRange = ColorRange_RGB;
    DisplaySignalRange mDisplaySignalRange = Display_SIGNAL_RANGE_FULL;

    TFSampleCount mMsaaLevel = TF_SAMPLE_COUNT_1;
    TFSampleCount mMaxMsaaLevel = TF_SAMPLE_COUNT_4;
    uint32_t      mMsaaIndex = (uint32_t)log2((uint32_t)mMsaaLevel);
    uint32_t      mMsaaIndexRequested = mMsaaIndex;

    // Camera Walking
    bool  cameraWalking = false;
    float cameraWalkingSpeed = 1.0f;

    // VR 2D layer transform (positioned at -1 along the Z axis, default rotation, default scale)
    TFVR2DLayerDesc mVR2DLayer{ { 0.0f, 0.0f, -1.0f }, { 0.0f, 0.0f, 0.0f, 1.0f }, 1.0f };
} AppSettings;

/************************************************************************/
// Constants
/************************************************************************/

// #NOTE: Two sets of resources (one in flight and one being used on CPU)
const uint32_t gDataBufferCount = 2;

// Constants
const uint32_t gShadowMapSize = 1024;
const uint32_t gNumViews = NUM_CULLING_VIEWPORTS;

// Define different geometry sets (opaque and alpha tested geometry)
const uint32_t gNumGeomSets = NUM_GEOMETRY_SETS;

// The number of render targets to be resolved
FORGE_CONSTEXPR uint32_t gResolveTargetCount = 2;
FORGE_CONSTEXPR uint32_t gResolveGodRayPassIndex = 0;
FORGE_CONSTEXPR uint32_t gResolveFinalPassIndex = 1;

FORGE_CONSTEXPR TinyImageFormat gDebugRTFormat = TinyImageFormat_R8G8B8A8_UNORM;

TFTexture* gNullTextureResource = NULL;

/************************************************************************/
// Per frame staging data
/************************************************************************/
struct PerFrameData
{
    // Stores the camera/eye position in object space for cluster culling
    vec3                    gEyeObjectSpace[NUM_CULLING_VIEWPORTS] = {};
    PerFrameConstantsData   gPerFrameUniformData = {};
    PerFrameVBConstantsData gPerFrameVBUniformData = {};
    UniformCameraSkyData    gUniformDataSky;
};

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
VBPreFilterStats  gVBPreFilterStats[gDataBufferCount];
/************************************************************************/
// Queues and Command buffers
/************************************************************************/
TFQueue*          pGraphicsQueue = NULL;
GpuCmdRing        gGraphicsCmdRing = {};

TFQueue*              pComputeQueue = NULL;
GpuCmdRing            gComputeCmdRing = {};
TFDescriptorSet*      pDescriptorSetPersistent = NULL;
TFDescriptorSet*      pDescriptorSetPerFrame = NULL;
TFDescriptorSet*      pDescriptorSetClusterLights = NULL;
TFDescriptorSet*      pDescriptorSetTriangleFiltering = NULL;
/************************************************************************/
// Swapchain
/************************************************************************/
TFSwapChain*          pSwapChain = NULL;
// Signaled by acquireNextImage; waited before rendering to the swapchain image.
TFSemaphore*          pImageAcquiredSemaphore[gDataBufferCount] = { NULL };
// Signaled by graphics submit; waited by present.
TFSemaphore*          pPresentSemaphore = NULL;
// Signaled by graphics submit; waited by next async compute submit before reusing frame resources.
TFSemaphore*          gPrevGraphicsSemaphore = NULL;
// Signaled by async compute submit; waited by graphics before consuming compute output.
TFSemaphore*          gComputeSemaphores[gDataBufferCount] = {};
/************************************************************************/
// Async compute pixel-list build (mAsyncComputePixelListBuild)
/************************************************************************/
// Per-frame VB fill -> async build handoff semaphores.
TFSemaphore*          pVBDoneSemaphores[gDataBufferCount] = {};
// Separate command ring for same-frame async pixel-list build.
GpuCmdRing            gAsyncBuildCmdRing = {};
ProfileToken          gAsyncPixelListBuildProfileToken = {};
// Async filtering and async pixel-list build share the compute queue; filtering has priority.
/************************************************************************/
// Clear buffers pipeline
/************************************************************************/
TFShader*             pShaderClearBuffers = nullptr;
TFPipeline*           pPipelineClearBuffers = nullptr;
TFShader*             pShaderBuildMaterialTileMasks[MSAA_LEVELS_COUNT] = {};
TFPipeline*           pPipelineBuildMaterialTileMasks = nullptr;
TFShader*             pShaderShaderInstancePrefixSum = nullptr;
TFPipeline*           pPipelineShaderInstancePrefixSum = nullptr;
TFShader*             pShaderScatterShaderInstances = nullptr;
TFPipeline*           pPipelineScatterShaderInstances = nullptr;
/************************************************************************/
// Compute pixel-list pipelines
/************************************************************************/
// One shade pipeline per material shader variant used by this sample
static const uint32_t gShadeComputePipelineCount = 32;
// PerDraw dispatch param slots = one per (shade pipeline, bucketed tile dispatch size)
static const uint32_t gShadeDispatchSlots = gShadeComputePipelineCount * (uint32_t)MAX_NUM_DISPATCH_BUCKETS;
TFShader*             pShaderClearPixelCounts = nullptr;
TFPipeline*           pPipelineClearPixelCounts = nullptr;
TFShader*             pShaderCountPixels = nullptr;
TFPipeline*           pPipelineCountPixels = nullptr;
TFShader*             pShaderPrefixSumTileMaterials = nullptr;
TFPipeline*           pPipelinePrefixSumTileMaterials = nullptr;
TFShader*             pShaderPrefixSumTiles = nullptr;
TFPipeline*           pPipelinePrefixSumTiles = nullptr;
TFShader*             pShaderPrefixSumPipelines = nullptr;
TFPipeline*           pPipelinePrefixSumPipelines = nullptr;
TFShader*             pShaderFinalizeTileMaterialOffsets = nullptr;
TFPipeline*           pPipelineFinalizeTileMaterialOffsets = nullptr;
TFShader*             pShaderWritePixelCommands = nullptr;
TFPipeline*           pPipelineWritePixelCommands = nullptr;
TFShader*             pShaderGenerateDispatchArgs = nullptr;
TFPipeline*           pPipelineGenerateDispatchArgs = nullptr;
TFShader*             pShaderShadeCompute[gShadeComputePipelineCount] = { nullptr };
TFPipeline*           pPipelineShadeCompute[gShadeComputePipelineCount] = { nullptr };
TFShader*             pShaderShadeComputeSky = nullptr;
TFPipeline*           pPipelineShadeComputeSky = nullptr;
TFDescriptorSet*      pDescriptorSetShadeDispatch = nullptr; // PerDraw set: one instance per (pipeline,bucket)
TFDescriptorSet*      pDescriptorSetShadeOutput = nullptr;
// Resolution-dependent compute pixel-list buffers
TFBuffer*             pPixelTileMaterialCountOffsetsBuffer = nullptr; // count, then absolute offset, then scatter cursor
TFBuffer*             pPixelCountPerPipelineTileBuffer = nullptr;
TFBuffer*             pPixelOffsetPerPipelineTileBuffer = nullptr;
TFBuffer*             pPixelPipelineCountOffsetsBuffer = nullptr; // count, then pipeline base offset
TFBuffer*             pPixelCommandsBuffer = nullptr; // sorted pixel list: one absolute-coordinate command per slot, never cleared
TFBuffer*             pPixelCommandPaddingMaskBuffer = nullptr;
TFBuffer*             pDispatchIndirectArgsBuffer = nullptr; // per-(pipeline,bucket) dispatch args
TFBuffer*             pDispatchOffsetsBuffer = nullptr;      // dispatch record -> absolute sorted-list slot base
TFBuffer*             pShadeDispatchParamsBuffer[gShadeDispatchSlots] = {};
// Cached compute tile/material key dimensions for the current render resolution
uint32_t              gComputeNumTilesX = 0;
uint32_t              gComputeNumTilesY = 0;
uint32_t              gComputeNumTiles = 0;
uint32_t              gComputePixelCommandSlotCount = 0;
uint32_t              gComputePixelCommandMaskUintCount = 0;
uint32_t              gComputeDispatchMaterialCount = 0;
// Falls back to the Grid-Tiles path when the scene exceeds the compute material bin limit
bool                  gComputePixelListSupported = true;
/************************************************************************/
// Triangle filtering pipeline
/************************************************************************/
TFShader*             pShaderTriangleFiltering = nullptr;
TFPipeline*           pPipelineTriangleFiltering = nullptr;
/************************************************************************/
// Clear light clusters pipeline
/************************************************************************/
TFShader*             pShaderClearLightClusters = nullptr;
TFPipeline*           pPipelineClearLightClusters = nullptr;
/************************************************************************/
// Compute light clusters pipeline
/************************************************************************/
TFShader*             pShaderClusterLights = nullptr;
TFPipeline*           pPipelineClusterLights = nullptr;
/************************************************************************/
// Shadow pass pipeline
/************************************************************************/
TFShader*             pShaderShadowPass[gNumGeomSets] = { NULL };
TFPipeline*           pPipelineShadowPass[gNumGeomSets] = { NULL };
/************************************************************************/
// VB pass pipeline
/************************************************************************/
// Default shaders and the ones which write material ID so multiplication by 2
TFShader*             pShaderVisibilityBufferPass[2 * gNumGeomSets] = {};
TFPipeline*           pPipelineVisibilityBufferPass[2 * gNumGeomSets] = {};
TFShader*             pShaderVisibilityBufferMaterialIDDepth[MSAA_LEVELS_COUNT] = {};
TFPipeline*           pPipelineVisibilityBufferMaterialIDDepth = nullptr;
/************************************************************************/
// VB shade pipeline
/************************************************************************/
// HandleMultiplMaterials * (MSAA + AO + Godray) variants
const uint32_t        gNumVisBufShaderVariants = 2 * 4 * MSAA_LEVELS_COUNT;
TFShader*             pShaderVisibilityBufferShade[gNumVisBufShaderVariants] = { nullptr };
TFShader*             pShaderVisibilityBufferShadeSky[2] = { nullptr };
TFPipeline*           pPipelineVisibilityBufferShadeSrgb[4] = { nullptr };
TFPipeline*           pPipelineVisibilityBufferShadeSkySrgb = nullptr;
TFTexture*            pSkybox = NULL;
/************************************************************************/
// MSAA Edge Detection pipeline
/************************************************************************/
TFShader*             pShaderDrawMSAAEdges[MSAA_LEVELS_COUNT - 1] = { nullptr };
TFShader*             pShaderDownscaleMSAAEdges[MSAA_LEVELS_COUNT - 1] = { nullptr };
TFPipeline*           pPipelineDrawMSAAEdges = nullptr;
TFPipeline*           pPipelineDownscaleMSAAEdges = nullptr;
/************************************************************************/
// Resolve pipeline
/************************************************************************/
TFShader*             pShaderResolve[MSAA_LEVELS_COUNT] = { nullptr };
TFPipeline*           pPipelineResolve = nullptr;
TFPipeline*           pPipelineResolveGodRay = nullptr;
TFDescriptorSet*      pDescriptorSetResolve = nullptr;
/************************************************************************/
// Godray pipeline
/************************************************************************/
TFShader*             pGodRayPass[MSAA_LEVELS_COUNT] = { nullptr };
TFPipeline*           pPipelineGodRayPass = nullptr;
TFBuffer*             pBufferGodRayConstant = nullptr;

TFShader*   pShaderGodRayBlurPass = nullptr;
TFPipeline* pPipelineGodRayBlurPass = nullptr;
TFBuffer*   pBufferBlurWeights = nullptr;

TFDescriptorSet* pDescriptorSetGodRayBlurPassPerDraw = nullptr;
TFBuffer*        pGodRayBlurBuffer[BLUR_PASS_TYPE_COUNT][gDataBufferCount] = { { NULL } };

/************************************************************************/
// Present pipeline
/************************************************************************/
TFShader*   pShaderPresentPass = nullptr;
TFPipeline* pPipelinePresentPass = nullptr;

/************************************************************************/
// Debug View pipeline
/************************************************************************/
TFShader*   pShaderDebugMSAA[MSAA_LEVELS_COUNT - 1] = { nullptr };
TFPipeline* pPipelineDebugMSAA = nullptr;

/************************************************************************/
// Render targets
/************************************************************************/
TFRenderTarget*      pDepthBuffer = NULL;
TFRenderTarget*      pRenderTargetVBPass = NULL;
TFRenderTarget*      pRenderTargetMaterialID = NULL;
TFRenderTarget*      pRenderTargetVBMaterialIDDepth = NULL;
TFTextureDescriptor* pDescriptorVBMSAAStencil = NULL;
TFRenderTarget*      pRenderTargetMSAA = NULL;
TFRenderTarget*      pRenderTargetDebugMSAA = NULL;
TFRenderTarget*      pRenderTargetShadow = NULL;
TFRenderTarget*      pIntermediateRenderTarget = NULL;
TFRenderTarget*      pRenderTargetGodRay[2] = { NULL };
TFRenderTarget*      pRenderTargetGodRayMS = NULL;
TFRenderTarget*      pRenderTargetDebugGodRayMSAA = NULL;
TFRenderTarget*      pDownscaledMSAAEdgesStencilBuffer = NULL;
/************************************************************************/
// Samplers
/************************************************************************/
TFSampler*           pSamplerTrilinearAniso = NULL;
TFSampler*           pSamplerBilinear = NULL;
TFSampler*           pSamplerPointClamp = NULL;
TFSampler*           pSamplerBilinearClamp = NULL;
/************************************************************************/
// Bindless texture array
/************************************************************************/
size_t               gAllTextureCount = 0;
/************************************************************************/
// Vertex buffers for the scene
/************************************************************************/
TFGeometry*          pGeom = NULL;
/************************************************************************/
// Indirect buffers
/************************************************************************/
TFBuffer*            pPerFrameUniformBuffers[gDataBufferCount] = { NULL };
enum
{
    VB_UB_COMPUTE = 0,
    VB_UB_GRAPHICS,
    VB_UB_COUNT
};
TFBuffer* pPerFrameVBUniformBuffers[VB_UB_COUNT][gDataBufferCount] = {};
TFBuffer* pMeshConstantsBuffer = NULL;
TFBuffer* pShadeQuadInstancesBuffer = NULL;
TFBuffer* pShadeDrawArgsBuffer = NULL;
TFBuffer* pMaterialTileMasksBuffer = NULL;
TFBuffer* pShaderInstanceCountsBuffer = NULL;
TFBuffer* pShaderInstanceCursorsBuffer = NULL;

/************************************************************************/
// Other buffers for lighting, point lights,...
/************************************************************************/
TFBuffer*       pLightsBuffer = NULL;
TFBuffer*       pLightClustersCount[gDataBufferCount] = { NULL };
TFBuffer*       pLightClusters[gDataBufferCount] = { NULL };
TFBuffer*       pUniformBufferSky[gDataBufferCount] = { NULL };
VBMeshInstance* pVBMeshInstances = NULL;
size_t          gMeshCount = 0;
TFUIWindowDesc  gGuiWindowDesc;
TFUIWindowDesc  gDebugTexturesWindowDesc;
bstring         gOutputSupportsHDRText = bempty();

TFFontDrawDesc gFrameTimeDraw;
TFFont*        gFont;

// True when the compute pixel-list shade writes pIntermediateRenderTarget as a UAV this reload cycle.
static bool isComputeShadeOutputUavEnabled()
{
    return gAppSettings.mHandleMultipleMaterials && gAppSettings.mUseComputePixelList && gAppSettings.mMsaaLevel == TF_SAMPLE_COUNT_1 &&
           (uint32_t)gMeshCount <= (uint32_t)MAX_DISPATCH_MATERIALS;
}

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

        TFRendererDesc settings;
        memset(&settings, 0, sizeof(settings));
        settings.pExtendedSettings = &extendedSettings;
        initGPUConfig(settings.pExtendedSettings);
        initRenderer(GetName(), &settings, &pRenderer);
        // check for init success
        if (!pRenderer)
        {
            ShowUnsupportedMessage(getUnsupportedGPUMsg());
            return false;
        }

        if (pRenderer->pGpu->mMaxBoundTextures < MIN_BINDLESS_TEXTURES_COUNT)
        {
            gUsingTextureAtlasFallback = true;
        }

        if (pRenderer->pGpu->mPrimitiveIdSupported == false)
        {
            gUsingPrimitiveIDFallback = true;
        }
        setGPUConfig(pRenderer->pContext->mGpus, pRenderer->pContext->mGpuCount, (uint32_t)(pRenderer->pGpu - pRenderer->pContext->mGpus),
                     settings.pExtendedSettings);
        mSettings.mFrameMaxCount = gDataBufferCount;

        // turn off by default depending on gpu config rules
        gAppSettings.mEnableGodray &= !gGpuSettings.mDisableGodRays;
        gAppSettings.mEnableAO &= !gGpuSettings.mDisableAO;
        gAppSettings.mMaxMsaaLevel = (TFSampleCount)gGpuSettings.mMaxMSAALevel;
        gAppSettings.mMsaaLevel =
            (TFSampleCount)clamp(gGpuSettings.mMSAASampleCount, (uint32_t)TF_SAMPLE_COUNT_1, (uint32_t)gAppSettings.mMaxMsaaLevel);
        gAppSettings.mMsaaIndex = (uint32_t)log2((uint32_t)gAppSettings.mMsaaLevel);
        gAppSettings.mMsaaIndexRequested = gAppSettings.mMsaaIndex;
        gAppSettings.mAsyncCompute &= !gGpuSettings.mDisableAsyncCompute;
        gAppSettings.mAsyncComputePixelListBuild &= !gGpuSettings.mDisableAsyncCompute;

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
            initSemaphore(pRenderer, &pVBDoneSemaphores[i]);
        }
        initSemaphore(pRenderer, &pPresentSemaphore);

        GpuCmdRingDesc cmdRingDesc = {};
        cmdRingDesc.pQueue = pGraphicsQueue;
        cmdRingDesc.mPoolCount = gDataBufferCount;
        // Async pixel-list build uses up to three graphics command buffers per frame.
        cmdRingDesc.mCmdPerPoolCount = 3;
        cmdRingDesc.mAddSyncPrimitives = true;
        initGpuCmdRing(pRenderer, &cmdRingDesc, &gGraphicsCmdRing);

        cmdRingDesc = {};
        cmdRingDesc.pQueue = pComputeQueue;
        cmdRingDesc.mPoolCount = gDataBufferCount;
        cmdRingDesc.mCmdPerPoolCount = 1;
        cmdRingDesc.mAddSyncPrimitives = true;
        initGpuCmdRing(pRenderer, &cmdRingDesc, &gComputeCmdRing);

        // Async pixel-list build submissions
        cmdRingDesc = {};
        cmdRingDesc.pQueue = pComputeQueue;
        cmdRingDesc.mPoolCount = gDataBufferCount;
        cmdRingDesc.mCmdPerPoolCount = 1;
        cmdRingDesc.mAddSyncPrimitives = true;
        initGpuCmdRing(pRenderer, &cmdRingDesc, &gAsyncBuildCmdRing);
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
        gAsyncPixelListBuildProfileToken = initGpuProfiler(pRenderer, pComputeQueue, "Async Pixel-List Build");
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
                                        true,
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
        blurWeightsBufferDesc.mDesc.mSize = sizeof(gBlurWeights);
        blurWeightsBufferDesc.ppBuffer = &pBufferBlurWeights;
        blurWeightsBufferDesc.pData = &gBlurWeightsUniform;
        blurWeightsBufferDesc.mDesc.mFlags = TF_BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
        addResource(&blurWeightsBufferDesc, NULL);

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
        TFPackageLoadDesc packageLoadDesc = {};
        if (gUsingTextureAtlasFallback)
        {
            packageLoadDesc.packageName = "SanMiguelPak.fallback.buny";
        }
        else
        {
            packageLoadDesc.packageName = "SanMiguelPak.buny";
        }

        packageLoadDesc.loadGeoData = true;
        packageLoadDesc.loadTexData = true;

        packageLoadDesc.createAtlas = gUsingTextureAtlasFallback;
        packageLoadDesc.ppOutPackage = &gPackage;

        if (!addResourcesFromPackage(&packageLoadDesc, gNullTextureResource))
        {
            LOGF(LogLevel::eERROR, "Failed to load package");
            return false;
        }

        LOGF(LogLevel::eINFO, "Load scene : %f ms", getHiresTimerUSec(&sceneLoadTimer, true) / 1000.0f);

        pGeom = *gPackage->pGeoData->ppGeometry;
        gMeshCount = gPackage->pTextureMetadata->mMeshCount;

        pVBMeshInstances = (VBMeshInstance*)tf_calloc(gMeshCount, sizeof(VBMeshInstance));

        /************************************************************************/
        // Init visibility buffer
        /************************************************************************/
        uint32_t visibilityBufferFilteredIndexCount[NUM_GEOMETRY_SETS] = { 0, 0 };

        TFHiresTimer vbSetupTimer;
        initHiresTimer(&vbSetupTimer);

        for (uint32_t i = 0; i < gMeshCount; ++i)
        {
            uint16_t matFlag = gPackage->pTextureMetadata->pMaterialProps[i].mFlags;
            uint32_t geomSet = matFlag & (MATERIAL_FLAG_ALPHA_TESTED | MATERIAL_FLAG_TRANSPARENT) ? GEOMSET_ALPHA_CUTOUT : GEOMSET_OPAQUE;
            visibilityBufferFilteredIndexCount[geomSet] += (pGeom->pDrawArgs + i)->mIndexCount;
            pVBMeshInstances[i].mGeometrySet = geomSet;
            pVBMeshInstances[i].mMeshIndex = i;
            pVBMeshInstances[i].mTriangleCount = (pGeom->pDrawArgs + i)->mIndexCount / 3;
            pVBMeshInstances[i].mInstanceIndex = INSTANCE_INDEX_NONE;
        }

        VisibilityBufferDesc vbDesc = {};
        vbDesc.mNumFrames = gDataBufferCount;
        vbDesc.mNumBuffers = gDataBufferCount;
        vbDesc.mNumGeometrySets = NUM_GEOMETRY_SETS;
        vbDesc.pMaxIndexCountPerGeomSet = visibilityBufferFilteredIndexCount;
        vbDesc.mNumViews = NUM_CULLING_VIEWPORTS;
        vbDesc.mComputeThreads = VB_COMPUTE_THREADS;
        initVisibilityBuffer(pRenderer, &vbDesc, &pVisibilityBuffer);

        LOGF(LogLevel::eINFO, "Setup vb : %f ms", getHiresTimerUSec(&vbSetupTimer, true) / 1000.0f);

        UpdateVBMeshFilterGroupsDesc updateVBMeshFilterGroupsDesc = {};
        updateVBMeshFilterGroupsDesc.mNumMeshInstance = (uint32_t)gMeshCount;
        updateVBMeshFilterGroupsDesc.pVBMeshInstances = pVBMeshInstances;
        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            updateVBMeshFilterGroupsDesc.mFrameIndex = i;
            gVBPreFilterStats[i] = updateVBMeshFilterGroups(pVisibilityBuffer, &updateVBMeshFilterGroupsDesc);
        }

        /************************************************************************/
        // Finish the resource loading process since the next code depends on the loaded resources
        /************************************************************************/
        waitForAllResourceLoads();

        gAllTextureCount = gMeshCount * TEXTURES_PER_MESH;

        TFHiresTimer setupBuffersTimer;
        initHiresTimer(&setupBuffersTimer);
        addTriangleFilteringBuffers();

        LOGF(LogLevel::eINFO, "Setup buffers : %f ms", getHiresTimerUSec(&setupBuffersTimer, true) / 1000.0f);

        LOGF(LogLevel::eINFO, "Total Load Time : %f ms", getHiresTimerUSec(&totalTimer, true) / 1000.0f);

        /************************************************************************/
        // Setup the fps camera for navigating through the scene
        /************************************************************************/
        vec3                     startPosition(124.0f, 98.0f, 14.0f);
        vec3                     startLookAt = startPosition + vec3(-0.2f, 0.02f, 0.0f);
        TFCameraMotionParameters camParams;
        camParams.acceleration = 1300 * 2.5f;
        camParams.braking = 1300 * 2.5f;
        camParams.maxSpeed = 200 * .25f;
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
        threadSystemExit(&gThreadSystem, &gThreadSystemExitDescDefault);

        tf_free(gCameraPathData);

        exitCamera(pCamera);
        for (uint32_t frameIdx = 0; frameIdx < gDataBufferCount; ++frameIdx)
        {
            removeResource(pGodRayBlurBuffer[BLUR_PASS_TYPE_HORIZONTAL][frameIdx]);
            removeResource(pGodRayBlurBuffer[BLUR_PASS_TYPE_VERTICAL][frameIdx]);
        }
        removeResource(pBufferGodRayConstant);
        removeResource(pBufferBlurWeights);
        removeResource(pSkybox);
        removeTriangleFilteringBuffers();
        exitProfiler();
        exitUserInterface();

        removeFont(gFont);
        exitFontSystem();

        removeResource(gNullTextureResource);

        /************************************************************************/
        // Remove loaded scene
        /************************************************************************/
        removeResourcesFromPackage(gPackage);
        tf_free(pVBMeshInstances);

#if defined(_WINDOWS)
        arrfree(gGuiResolution.mResNameContainer);
        arrfree(gGuiResolution.mResNamePointers);
#endif
        /************************************************************************/
        /************************************************************************/
        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            exitSemaphore(pRenderer, pImageAcquiredSemaphore[i]);
            exitSemaphore(pRenderer, pVBDoneSemaphores[i]);
        }
        exitSemaphore(pRenderer, pPresentSemaphore);

        exitGpuCmdRing(pRenderer, &gGraphicsCmdRing);
        exitGpuCmdRing(pRenderer, &gComputeCmdRing);
        exitGpuCmdRing(pRenderer, &gAsyncBuildCmdRing);

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

        gAppSettings.mHoldFilteredResults = false;

        luaVarDesc.pLabel = "Hold filtered results";
        luaVarDesc.mWidgetType = TF_WIDGET_TYPE_CHECKBOX;
        luaVarDesc.pBool = &gAppSettings.mHoldFilteredResults;
        luaRegisterWidgetVariable(&luaVarDesc);

        luaVarDesc.pLabel = "Async Compute";
        luaVarDesc.mWidgetType = TF_WIDGET_TYPE_CHECKBOX;
        luaVarDesc.pBool = &gAppSettings.mAsyncCompute;
        luaRegisterWidgetVariable(&luaVarDesc);

        luaVarDesc.pLabel = "Async Compute Build";
        luaVarDesc.mWidgetType = TF_WIDGET_TYPE_CHECKBOX;
        luaVarDesc.pBool = &gAppSettings.mAsyncComputePixelListBuild;
        luaRegisterWidgetVariable(&luaVarDesc);

        luaVarDesc.pLabel = "Handle Multiple Materials Prototype";
        luaVarDesc.mWidgetType = TF_WIDGET_TYPE_CHECKBOX;
        luaVarDesc.pBool = &gAppSettings.mHandleMultipleMaterials;
        luaRegisterWidgetVariable(&luaVarDesc);

        luaVarDesc.pLabel = "Draw Debug Targets";
        luaVarDesc.mWidgetType = TF_WIDGET_TYPE_CHECKBOX;
        luaVarDesc.pBool = &gAppSettings.mDrawDebugTargets;
        luaRegisterWidgetVariable(&luaVarDesc);

        /************************************************************************/
        /************************************************************************/

        luaVarDesc.pLabel = "Cinematic Camera walking";
        luaVarDesc.mWidgetType = TF_WIDGET_TYPE_CHECKBOX;
        luaVarDesc.pBool = &gAppSettings.cameraWalking;
        luaRegisterWidgetVariable(&luaVarDesc);

        luaVarDesc.pLabel = "Cinematic Camera walking: Speed";
        luaVarDesc.mWidgetType = TF_WIDGET_TYPE_SLIDER_FLOAT;
        luaVarDesc.pFloat = &gAppSettings.cameraWalkingSpeed;
        luaRegisterWidgetVariable(&luaVarDesc);

        // Light Settings
        //---------------------------------------------------------------------------------
        luaVarDesc.pLabel = "Light Color & Intensity";
        luaVarDesc.mWidgetType = TF_WIDGET_TYPE_SLIDER_FLOAT4;
        luaVarDesc.pFloat4 = &gAppSettings.mLightColor;
        luaRegisterWidgetVariable(&luaVarDesc);

        luaVarDesc.pLabel = "Time of Day";
        luaVarDesc.mWidgetType = TF_WIDGET_TYPE_SLIDER_FLOAT;
        luaVarDesc.pFloat = &gAppSettings.mTimeOfDay;
        luaRegisterWidgetVariable(&luaVarDesc);

        luaVarDesc.pLabel = "ESM Control";
        luaVarDesc.mWidgetType = TF_WIDGET_TYPE_SLIDER_FLOAT;
        luaVarDesc.pFloat = &gAppSettings.mEsmControl;
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
        // Rendering Settings
        /************************************************************************/
        /************************************************************************/
        // MSAA Settings
        /************************************************************************/
        luaVarDesc.pLabel = "MSAA";
        luaVarDesc.mWidgetType = TF_WIDGET_TYPE_DROPDOWN;
        luaVarDesc.pUint = &gAppSettings.mMsaaIndexRequested;
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

        if (gAppSettings.mMsaaIndex != gAppSettings.mMsaaIndexRequested)
        {
            gAppSettings.mMsaaIndex = gAppSettings.mMsaaIndexRequested;
            gAppSettings.mMsaaLevel = (TFSampleCount)(1 << gAppSettings.mMsaaIndex);
            while (gAppSettings.mMsaaIndex > 0)
            {
                bool isValidLevel = (pRenderer->pGpu->mFrameBufferSamplesCount & gAppSettings.mMsaaLevel) != 0;
                isValidLevel &= gAppSettings.mMsaaLevel <= gAppSettings.mMaxMsaaLevel;
                if (!isValidLevel)
                {
                    gAppSettings.mMsaaIndex--;
                    gAppSettings.mMsaaLevel = (TFSampleCount)(gAppSettings.mMsaaLevel / 2);
                }
                else
                {
                    break;
                }
            }
        }
        if (gAppSettings.mMsaaLevel != TF_SAMPLE_COUNT_1)
            gAppSettings.mUseComputePixelList = false;

        gAppSettings.mOutputMode = gGpuSettings.mEnableHDR ? OUTPUT_MODE_P2020 : OUTPUT_MODE_SDR;

        if (!addSwapChain())
            return false;

        addRenderTargets();
        // Debug Texture UI Window
        {
            float  scale = 0.15f;
            float2 screenSize = { (float)pRenderTargetVBPass->mWidth, (float)pRenderTargetVBPass->mHeight };
            float2 texSize = screenSize * scale;

            gDebugTexturesWindowDesc = {};
            gDebugTexturesWindowDesc.mStartPos.y = (screenSize.y - texSize.y - 50.f);
            gDebugTexturesWindowDesc.mStartSize = { 650, 500 };
            gDebugTexturesWindowDesc.pWindowTitle = "DEBUG RTs";
            gDebugTexturesWindowDesc.mFlags = TF_UI_WINDOW_TITLE | TF_UI_WINDOW_SCALABLE | TF_UI_WINDOW_MOVABLE | TF_UI_WINDOW_MINIMIZABLE |
                                              TF_UI_WINDOW_BORDER | TF_UI_WINDOW_INIT_HEIGHT_FIT;
        }

        addPipelines();

        updateDescriptorSets();

        TFUserInterfaceLoadDesc uiLoad = {};
        uiLoad.mColorFormat = pSwapChain->ppRenderTargets[0]->mFormat;
        uiLoad.mHeight = mSettings.mHeight;
        uiLoad.mWidth = mSettings.mWidth;
        TFVR2DLayerDesc vr2DLayer = gAppSettings.mVR2DLayer;
        uiLoad.mVR2DLayer.mPosition = float3(vr2DLayer.m2DLayerPosition.x, vr2DLayer.m2DLayerPosition.y, vr2DLayer.m2DLayerPosition.z);
        uiLoad.mVR2DLayer.mScale = vr2DLayer.m2DLayerScale;
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
        removeSwapChain(pRenderer, pSwapChain);
        bdestroy(&gOutputSupportsHDRText);
        unloadProfilerUI();

        removeRenderTargets();
        TF_ESRAM_RESET_ALLOCS(pRenderer);

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
            textures[texturesCount++] = pRenderTargetShadow->pTexture;

            if (gAppSettings.mMsaaLevel > TF_SAMPLE_COUNT_1)
            {
                if (gAppSettings.mEnableGodray)
                {
                    textures[texturesCount++] = pRenderTargetDebugGodRayMSAA->pTexture;
                }
                textures[texturesCount++] = pRenderTargetDebugMSAA->pTexture;
            }

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
            uiLayoutAutoTextRows(1);
            uiText(&gOutputSupportsHDRText, TF_ALIGN_CENTER);

            static const char* msaaNames[] = { "1x", "2x", "4x" };
            const uint32_t     msaaOptionCount =
                getSupportedMSAAUIOptionCount(gAppSettings.mMaxMsaaLevel, pRenderer->pGpu->mFrameBufferSamplesCount);
            if (gAppSettings.mMsaaIndexRequested >= msaaOptionCount)
                gAppSettings.mMsaaIndexRequested = msaaOptionCount - 1u;

            uiLayoutAutoRows(3);
            uiCheckbox("Hold filtered results", &gAppSettings.mHoldFilteredResults);
            uiCheckbox("Async Compute", &gAppSettings.mAsyncCompute);
            uiCheckbox("Async Compute Build (pixel-list)", &gAppSettings.mAsyncComputePixelListBuild);
            // These toggles affect render-target formats and require a reload.
            const bool prevHandleMultiMats = gAppSettings.mHandleMultipleMaterials;
            const bool prevUseComputePixelList = gAppSettings.mUseComputePixelList;
            bool       requestRenderPathReload = false;
            uiCheckbox("Handle Multiple Materials Prototype", &gAppSettings.mHandleMultipleMaterials);
            if (gAppSettings.mMsaaIndexRequested == 0u)
            {
                uiCheckbox("Use Compute Pixel-List", &gAppSettings.mUseComputePixelList);
            }
            else
            {
                gAppSettings.mUseComputePixelList = false;
                uiLayoutAutoRows(1);
                uiLabel("Set MSAA to 1x to try compute pixel-list", TF_ALIGN_LEFT);
                uiLayoutAutoRows(3);
            }
            uiCheckbox("Draw Debug Targets", &gAppSettings.mDrawDebugTargets);

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
            uiLayoutAutoRows(2);
            uiLabel("ESM Control", TF_ALIGN_LEFT);
            uiSliderFloat(&gAppSettings.mEsmControl, 0.0f, 400.0f, 0.1f);

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

            uiLabel("MSAA", TF_ALIGN_LEFT);
            TFUIWidgetInteraction msaaDropdown = uiDropdown(msaaNames, (int)msaaOptionCount, (int)gAppSettings.mMsaaIndexRequested);
            const uint32_t        selectedMsaaIndex = (uint32_t)UI_WIDGET_GET_SELECTED(msaaDropdown);
            if (selectedMsaaIndex != gAppSettings.mMsaaIndexRequested)
            {
                gAppSettings.mMsaaIndexRequested = selectedMsaaIndex;
                if (selectedMsaaIndex != 0u)
                    gAppSettings.mUseComputePixelList = false;
                requestRenderPathReload = true;
            }

            if (prevHandleMultiMats != gAppSettings.mHandleMultipleMaterials ||
                prevUseComputePixelList != gAppSettings.mUseComputePixelList)
                requestRenderPathReload = true;
            if (requestRenderPathReload)
            {
                TFReloadDesc reloadDesc = {};
                reloadDesc.mType = (TFReloadType)(TF_RELOAD_TYPE_RESIZE | TF_RELOAD_TYPE_SHADER | TF_RELOAD_TYPE_RENDERTARGET);
                requestReload(&reloadDesc);
            }

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
            TFUIWidgetInteraction swapChainColorSpaceDropdown =
                uiDropdown(displayColorSpaceNames, sizeof(displayColorSpaceNames) / sizeof(displayColorSpaceNames[0]),
                           gAppSettings.mCurrentSwapChainColorSpace);
            gAppSettings.mCurrentSwapChainColorSpace = (DisplayColorSpace)UI_WIDGET_GET_SELECTED(swapChainColorSpaceDropdown);

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
        if ((bool)pSwapChain->mEnableVsync != mSettings.mVSyncEnabled)
        {
            waitQueueIdle(pGraphicsQueue);
            ::toggleVSync(pRenderer, &pSwapChain);
        }

        uint32_t          presentIndex = 0;
        uint32_t          frameIdx = mSettings.mFrameIdx;
        GpuCmdRingElement computeElem = getNextGpuCmdRingElement(&gComputeCmdRing, true, 1);
        GpuCmdRingElement graphicsElem = getNextGpuCmdRingElement(&gGraphicsCmdRing, true, 3);
        GpuCmdRingElement asyncBuildElem = getNextGpuCmdRingElement(&gAsyncBuildCmdRing, true, 1);
        /************************************************************************/
        // Async compute pass
        /************************************************************************/
        bool              useDedicatedComputeQueue = gAppSettings.mAsyncCompute && !gAppSettings.mHoldFilteredResults;
        const bool        useComputePixelListPath = gAppSettings.mHandleMultipleMaterials && gAppSettings.mUseComputePixelList &&
                                             gComputePixelListSupported && (gAppSettings.mMsaaLevel == TF_SAMPLE_COUNT_1);
        const bool useQuadMaterialPath = gAppSettings.mHandleMultipleMaterials && !useComputePixelListPath;
        // Async build is disabled while frame-ahead async filtering owns the compute queue.
        const bool useAsyncPixelListBuild =
            gAppSettings.mAsyncComputePixelListBuild && useComputePixelListPath && !useDedicatedComputeQueue;
        if (useDedicatedComputeQueue)
        {
            // check to see if we can use the cmd buffer
            TFFenceStatus fenceStatus;
            getFenceStatus(pRenderer, computeElem.pFence, &fenceStatus);
            if (fenceStatus == TF_FENCE_STATUS_INCOMPLETE)
                waitForFences(pRenderer, 1, &computeElem.pFence);
            /************************************************************************/
            // Update uniform buffer to gpu
            /************************************************************************/
            TFBufferUpdateDesc update = { pPerFrameVBUniformBuffers[VB_UB_COMPUTE][frameIdx] };
            beginUpdateResource(&update);
            memcpy(update.pMappedData, &gPerFrame[frameIdx].gPerFrameVBUniformData, sizeof(gPerFrame[frameIdx].gPerFrameVBUniformData));
            endUpdateResource(&update);
            /************************************************************************/
            // Triangle filtering async compute pass
            /************************************************************************/
            TFCmd* computeCmd = computeElem.pCmds[0];

            resetCmdPool(pRenderer, computeElem.pCmdPool);
            beginCmd(computeCmd);
            cmdBindDescriptorSet(computeCmd, 0, pDescriptorSetPersistent);
            cmdBindDescriptorSet(computeCmd, frameIdx, pDescriptorSetPerFrame);
            cmdBeginGpuFrameProfile(computeCmd, gComputeProfileToken);

            TriangleFilteringPassDesc triangleFilteringDesc = {};
            triangleFilteringDesc.pPipelineClearBuffers = pPipelineClearBuffers;
            triangleFilteringDesc.pPipelineTriangleFiltering = pPipelineTriangleFiltering;

            triangleFilteringDesc.pDescriptorSetTriangleFilteringPerBatch = pDescriptorSetTriangleFiltering;

            triangleFilteringDesc.mFrameIndex = frameIdx;
            triangleFilteringDesc.mBuffersIndex = frameIdx;
            triangleFilteringDesc.mGpuProfileToken = gComputeProfileToken;
            triangleFilteringDesc.mVBPreFilterStats = gVBPreFilterStats[frameIdx];
            cmdVBTriangleFilteringPass(pVisibilityBuffer, computeCmd, &triangleFilteringDesc);

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

                TFBufferBarrier barriers[] = { { pLightClustersCount[frameIdx], TF_RESOURCE_STATE_UNORDERED_ACCESS,
                                                 TF_RESOURCE_STATE_UNORDERED_ACCESS } };
                cmdResourceBarrier(computeCmd, 1, barriers, 0, NULL, 0, NULL);

                cmdBindPipeline(computeCmd, pPipelineClusterLights);
                cmdBindDescriptorSet(computeCmd, frameIdx, pDescriptorSetClusterLights);
                cmdDispatch(computeCmd, LIGHT_COUNT, 1, 1);

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
            /************************************************************************/
            /************************************************************************/
        }

        /************************************************************************/
        // Draw Pass - Skip first frame since draw will always be one frame behind compute
        /************************************************************************/
        if (!useDedicatedComputeQueue || mSettings.mFrames > 0)
        {
            frameIdx = useDedicatedComputeQueue ? ((mSettings.mFrames - 1) % mSettings.mFrameMaxCount) : frameIdx;

            // Check to see if we can use the cmd buffer
            TFFenceStatus fenceStatus;
            getFenceStatus(pRenderer, graphicsElem.pFence, &fenceStatus);
            if (fenceStatus == TF_FENCE_STATUS_INCOMPLETE)
                waitForFences(pRenderer, 1, &graphicsElem.pFence);
            if (useAsyncPixelListBuild)
            {
                // The async build command buffer for this ring slot must also have retired before reuse.
                getFenceStatus(pRenderer, asyncBuildElem.pFence, &fenceStatus);
                if (fenceStatus == TF_FENCE_STATUS_INCOMPLETE)
                    waitForFences(pRenderer, 1, &asyncBuildElem.pFence);
            }

            pScreenRenderTarget = pIntermediateRenderTarget;

            // Async build waits on the acquired image only in the final graphics submit.
            acquireNextImage(pRenderer, pSwapChain, pImageAcquiredSemaphore[frameIdx], NULL, &presentIndex);

            // Multi-material paths consume the VB material-id output.
            const bool writeMaterialIDTarget = gAppSettings.mHandleMultipleMaterials;

            /************************************************************************/
            // Update uniform buffer to gpu
            /************************************************************************/
            if (!useDedicatedComputeQueue)
            {
                TFBufferUpdateDesc update = { pPerFrameVBUniformBuffers[VB_UB_COMPUTE][frameIdx] };
                beginUpdateResource(&update);
                memcpy(update.pMappedData, &gPerFrame[frameIdx].gPerFrameVBUniformData, sizeof(gPerFrame[frameIdx].gPerFrameVBUniformData));
                endUpdateResource(&update);
            }

            TFBufferUpdateDesc update = { pPerFrameVBUniformBuffers[VB_UB_GRAPHICS][frameIdx] };
            beginUpdateResource(&update);
            memcpy(update.pMappedData, &gPerFrame[frameIdx].gPerFrameVBUniformData, sizeof(gPerFrame[frameIdx].gPerFrameVBUniformData));
            endUpdateResource(&update);

            update = { pPerFrameUniformBuffers[frameIdx] };
            beginUpdateResource(&update);
            memcpy(update.pMappedData, &gPerFrame[frameIdx].gPerFrameUniformData, sizeof(gPerFrame[frameIdx].gPerFrameUniformData));
            endUpdateResource(&update);

            // Update uniform buffers
            update = { pUniformBufferSky[frameIdx] };
            beginUpdateResource(&update);
            memcpy(update.pMappedData, &gPerFrame[frameIdx].gUniformDataSky, sizeof(gPerFrame[frameIdx].gUniformDataSky));
            endUpdateResource(&update);

            /************************************************************************/
            /************************************************************************/
            // Get command list to store rendering commands for this frame
            TFCmd* graphicsCmd = graphicsElem.pCmds[0];
            // Submit all render commands for this frame
            resetCmdPool(pRenderer, graphicsElem.pCmdPool);
            beginCmd(graphicsCmd);
            cmdBindDescriptorSet(graphicsCmd, 0, pDescriptorSetPersistent);
            cmdBindDescriptorSet(graphicsCmd, frameIdx, pDescriptorSetPerFrame);

            cmdBeginGpuFrameProfile(graphicsCmd, gGraphicsProfileToken);

            if (!useDedicatedComputeQueue && !gAppSettings.mHoldFilteredResults)
            {
                TriangleFilteringPassDesc triangleFilteringDesc = {};
                triangleFilteringDesc.pPipelineClearBuffers = pPipelineClearBuffers;
                triangleFilteringDesc.pPipelineTriangleFiltering = pPipelineTriangleFiltering;

                triangleFilteringDesc.pDescriptorSetTriangleFilteringPerBatch = pDescriptorSetTriangleFiltering;

                triangleFilteringDesc.mFrameIndex = frameIdx;
                triangleFilteringDesc.mBuffersIndex = frameIdx;
                triangleFilteringDesc.mGpuProfileToken = gGraphicsProfileToken;
                triangleFilteringDesc.mVBPreFilterStats = gVBPreFilterStats[frameIdx];
                cmdVBTriangleFilteringPass(pVisibilityBuffer, graphicsCmd, &triangleFilteringDesc);
            }

            // Clear Light Clusters pass
            if (!useDedicatedComputeQueue)
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

                TFBufferBarrier barriers[] = { { pLightClustersCount[frameIdx], TF_RESOURCE_STATE_UNORDERED_ACCESS,
                                                 TF_RESOURCE_STATE_UNORDERED_ACCESS } };
                cmdResourceBarrier(graphicsCmd, 1, barriers, 0, NULL, 0, NULL);

                cmdBindPipeline(graphicsCmd, pPipelineClusterLights);
                cmdBindDescriptorSet(graphicsCmd, frameIdx, pDescriptorSetClusterLights);
                cmdDispatch(graphicsCmd, LIGHT_COUNT, 1, 1);

                barriers[0] = { pLightClusters[frameIdx], TF_RESOURCE_STATE_UNORDERED_ACCESS, TF_RESOURCE_STATE_UNORDERED_ACCESS };
                cmdResourceBarrier(graphicsCmd, 1, barriers, 0, NULL, 0, NULL);
                cmdEndGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken);
            }

            // Transition draw inputs and outputs into their graphics pass states
            {
                uint32_t              rtBarriersCount = gAppSettings.mMsaaLevel > TF_SAMPLE_COUNT_1 ? 2 : 1;
                TFRenderTargetBarrier rtBarriers[] = {
                    { pScreenRenderTarget, TF_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, TF_RESOURCE_STATE_RENDER_TARGET },
                    { pRenderTargetMSAA, TF_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, TF_RESOURCE_STATE_RENDER_TARGET }
                };

                const uint32_t  maxNumBarriers = NUM_CULLING_VIEWPORTS + 4;
                uint32_t        barrierCount = 0;
                TFBufferBarrier barriers[maxNumBarriers] = {};
                barriers[barrierCount++] = { pLightClusters[frameIdx], TF_RESOURCE_STATE_UNORDERED_ACCESS,
                                             TF_RESOURCE_STATE_SHADER_RESOURCE };
                barriers[barrierCount++] = { pLightClustersCount[frameIdx], TF_RESOURCE_STATE_UNORDERED_ACCESS,
                                             TF_RESOURCE_STATE_SHADER_RESOURCE };

                {
                    barriers[barrierCount++] = { pVisibilityBuffer->ppIndirectDrawArgBuffer[frameIdx], TF_RESOURCE_STATE_UNORDERED_ACCESS,
                                                 TF_RESOURCE_STATE_INDIRECT_ARGUMENT | TF_RESOURCE_STATE_SHADER_RESOURCE };

                    barriers[barrierCount++] = { pVisibilityBuffer->ppIndirectDataBuffer[frameIdx], TF_RESOURCE_STATE_UNORDERED_ACCESS,
                                                 TF_RESOURCE_STATE_SHADER_RESOURCE };

                    for (uint32_t i = 0; i < NUM_CULLING_VIEWPORTS; ++i)
                    {
                        barriers[barrierCount++] = { pVisibilityBuffer->ppFilteredIndexBuffer[frameIdx * NUM_CULLING_VIEWPORTS + i],
                                                     TF_RESOURCE_STATE_UNORDERED_ACCESS,
                                                     TF_RESOURCE_STATE_INDEX_BUFFER | TF_RESOURCE_STATE_SHADER_RESOURCE };
                    }
                }

                cmdResourceBarrier(graphicsCmd, barrierCount, barriers, 0, NULL, rtBarriersCount, rtBarriers);
            }

            /************************************************************************/
            // Frame render passes
            /************************************************************************/

            // Visibility buffer fill pass. Async build consumes its outputs before the shadow pass.
            {
                // Render the scene to perform the Visibility Buffer pass. In this pass the (filtered) scene geometry is rendered
                // into a 32-bit per pixel render target. This contains triangle information (batch Id and triangle Id) that allows
                // to reconstruct all triangle attributes per pixel. This is faster than a typical Deferred Shading pass, because
                // less memory bandwidth is used.
                cmdBeginGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken, "VB Filling Pass");

                // Resource Transition
                {
                    TFRenderTargetBarrier passBarriers[2] = {};
                    uint32_t              passBarrierCount = 0;
                    passBarriers[passBarrierCount++] = { pRenderTargetVBPass, TF_RESOURCE_STATE_SHADER_RESOURCE,
                                                         TF_RESOURCE_STATE_RENDER_TARGET };
                    if (writeMaterialIDTarget)
                        passBarriers[passBarrierCount++] = { pRenderTargetMaterialID, TF_RESOURCE_STATE_SHADER_RESOURCE,
                                                             TF_RESOURCE_STATE_RENDER_TARGET };
                    cmdResourceBarrier(graphicsCmd, 0, NULL, 0, NULL, passBarrierCount, passBarriers);
                }
                // Bind Render Targets
                {
                    // Render target is cleared to (1,1,1,1) because (0,0,0,0) represents the first triangle of the first draw batch
                    TFBindRenderTargetsDesc bindRenderTargets = {};
                    bindRenderTargets.mRenderTargetCount = writeMaterialIDTarget ? 2 : 1;
                    bindRenderTargets.mRenderTargets[0] = { pRenderTargetVBPass, TF_LOAD_ACTION_CLEAR };
                    if (writeMaterialIDTarget)
                        bindRenderTargets.mRenderTargets[1] = { pRenderTargetMaterialID, TF_LOAD_ACTION_CLEAR };
                    bindRenderTargets.mDepthStencil = { pDepthBuffer, TF_LOAD_ACTION_CLEAR };
                    cmdBindRenderTargets(graphicsCmd, &bindRenderTargets);
                    cmdSetViewport(graphicsCmd, 0.0f, 0.0f, (float)pRenderTargetVBPass->mWidth, (float)pRenderTargetVBPass->mHeight, 0.0f,
                                   1.0f);
                    cmdSetScissor(graphicsCmd, 0, 0, pRenderTargetVBPass->mWidth, pRenderTargetVBPass->mHeight);
                }
                // Draw
                {
                    TFBuffer* pIndexBuffer = pVisibilityBuffer->ppFilteredIndexBuffer[frameIdx * NUM_CULLING_VIEWPORTS + VIEW_CAMERA];
                    cmdBindIndexBuffer(graphicsCmd, pIndexBuffer, TF_INDEX_TYPE_UINT32, 0);

                    for (uint32_t i = 0; i < gNumGeomSets; ++i)
                    {
                        if (!writeMaterialIDTarget)
                        {
                            cmdBindPipeline(graphicsCmd, pPipelineVisibilityBufferPass[i]);
                        }
                        else
                        {
                            cmdBindPipeline(graphicsCmd, pPipelineVisibilityBufferPass[2 + i]);
                        }

                        uint64_t  indirectBufferByteOffset = GET_INDIRECT_DRAW_ELEM_INDEX(VIEW_CAMERA, i, 0) * sizeof(uint32_t);
                        TFBuffer* pIndirectBuffer = pVisibilityBuffer->ppIndirectDrawArgBuffer[frameIdx];
                        if (gUsingPrimitiveIDFallback)
                        {
                            cmdExecuteIndirect(graphicsCmd, TF_INDIRECT_DRAW, 1, pIndirectBuffer, indirectBufferByteOffset, NULL, 0);
                        }
                        else
                        {
                            cmdExecuteIndirect(graphicsCmd, TF_INDIRECT_DRAW_INDEX, 1, pIndirectBuffer, indirectBufferByteOffset, NULL, 0);
                        }
                    }
                    cmdBindRenderTargets(graphicsCmd, NULL);
                }
                // Resource Transition
                {
                    TFRenderTargetBarrier passBarriers[3] = {};
                    uint32_t              passBarrierCount = 0;
                    passBarriers[passBarrierCount++] = { pRenderTargetVBPass, TF_RESOURCE_STATE_RENDER_TARGET,
                                                         TF_RESOURCE_STATE_SHADER_RESOURCE };
                    if (writeMaterialIDTarget)
                        passBarriers[passBarrierCount++] = { pRenderTargetMaterialID, TF_RESOURCE_STATE_RENDER_TARGET,
                                                             TF_RESOURCE_STATE_SHADER_RESOURCE };
                    passBarriers[passBarrierCount++] = { pDepthBuffer, TF_RESOURCE_STATE_DEPTH_WRITE, TF_RESOURCE_STATE_SHADER_RESOURCE };
                    cmdResourceBarrier(graphicsCmd, 0, NULL, 0, NULL, passBarrierCount, passBarriers);
                }

                cmdEndGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken);
            }

            /************************************************************************/
            // Compute pixel-list build. Async mode submits VB fill first and records the build on the compute queue.
            /************************************************************************/
            TFCmd* pixelListBuildCmd = graphicsCmd;
            if (useAsyncPixelListBuild)
            {
                endCmd(graphicsCmd);

                FlushResourceUpdateDesc flushUpdateDesc = {};
                flushUpdateDesc.mNodeIndex = 0;
                flushResourceUpdates(&flushUpdateDesc);
                TFSemaphore* vbSubmitWaitSemaphores[2] = {};
                uint32_t     vbSubmitWaitCount = 0;
                vbSubmitWaitSemaphores[vbSubmitWaitCount++] = flushUpdateDesc.pOutSubmittedSemaphore;
                TFQueueSubmitDesc vbSubmitDesc = {};
                vbSubmitDesc.mCmdCount = 1;
                vbSubmitDesc.ppCmds = &graphicsCmd;
                vbSubmitDesc.ppWaitSemaphores = vbSubmitWaitSemaphores;
                vbSubmitDesc.mWaitSemaphoreCount = vbSubmitWaitCount;
                vbSubmitDesc.ppSignalSemaphores = &pVBDoneSemaphores[frameIdx];
                vbSubmitDesc.mSignalSemaphoreCount = 1;
                queueSubmit(pGraphicsQueue, &vbSubmitDesc);

                pixelListBuildCmd = asyncBuildElem.pCmds[0];
                resetCmdPool(pRenderer, asyncBuildElem.pCmdPool);
                beginCmd(pixelListBuildCmd);
                cmdBeginGpuFrameProfile(pixelListBuildCmd, gAsyncPixelListBuildProfileToken);
            }

            // Records the compute pixel list build chain keyed by (tile, materialID)
            if (useComputePixelListPath)
            {
                ProfileToken profileToken = useAsyncPixelListBuild ? gAsyncPixelListBuildProfileToken : gGraphicsProfileToken;
                cmdBeginGpuTimestampQuery(pixelListBuildCmd, profileToken, "Compute Pixel-List Build");

                const uint32_t numTiles = gComputeNumTiles;
                const uint32_t numDispatchMaterials = gComputeDispatchMaterialCount;
                const uint32_t numTileMaterials = numTiles * numDispatchMaterials;
                const uint32_t numPipelineTiles = gShadeComputePipelineCount * numTiles;
                const uint32_t numBucketSlots = gShadeDispatchSlots;
                const uint32_t renderWidth = pIntermediateRenderTarget->mWidth;
                const uint32_t renderHeight = pIntermediateRenderTarget->mHeight;
                const uint32_t groupsW = (renderWidth + 7u) / 8u;
                const uint32_t groupsH = (renderHeight + 7u) / 8u;

                const uint32_t indirectArgValueCount = numBucketSlots * 3u;
                const uint32_t maxCountOrMask =
                    indirectArgValueCount > gComputePixelCommandMaskUintCount ? indirectArgValueCount : gComputePixelCommandMaskUintCount;
                const uint32_t maxClearElems = numTileMaterials > maxCountOrMask ? numTileMaterials : maxCountOrMask;
                const uint32_t clearGroups = (maxClearElems + 63u) / 64u;

                cmdBindDescriptorSet(pixelListBuildCmd, 0, pDescriptorSetPersistent);
                cmdBindDescriptorSet(pixelListBuildCmd, frameIdx, pDescriptorSetPerFrame);

                // 1. Clear Pixel Counts pass
                {
                    // Zero accumulated counters and reset the padding mask
                    cmdBeginGpuTimestampQuery(pixelListBuildCmd, profileToken, "Clear Pixel Counts");

                    // Dispatch
                    {
                        cmdBindPipeline(pixelListBuildCmd, pPipelineClearPixelCounts);
                        const uint32_t clearGroupsY = (clearGroups + LINEAR_DISPATCH_GROUPS_X - 1u) / LINEAR_DISPATCH_GROUPS_X;
                        cmdDispatch(pixelListBuildCmd, LINEAR_DISPATCH_GROUPS_X, clearGroupsY, 1);
                    }
                    // Resource Transition
                    {
                        TFBufferBarrier clearBarriers[] = {
                            { pPixelTileMaterialCountOffsetsBuffer, TF_RESOURCE_STATE_UNORDERED_ACCESS,
                              TF_RESOURCE_STATE_UNORDERED_ACCESS },
                            { pDispatchIndirectArgsBuffer, TF_RESOURCE_STATE_UNORDERED_ACCESS, TF_RESOURCE_STATE_UNORDERED_ACCESS },
                            { pPixelCommandPaddingMaskBuffer, TF_RESOURCE_STATE_UNORDERED_ACCESS, TF_RESOURCE_STATE_UNORDERED_ACCESS },
                        };
                        cmdResourceBarrier(pixelListBuildCmd, TF_ARRAY_COUNT(clearBarriers), clearBarriers, 0, NULL, 0, NULL);
                    }

                    cmdEndGpuTimestampQuery(pixelListBuildCmd, profileToken);
                }

                // 2. Count Pixels per (tile,material) pass
                {
                    cmdBeginGpuTimestampQuery(pixelListBuildCmd, profileToken, "Count Pixels");

                    // Dispatch
                    {
                        cmdBindPipeline(pixelListBuildCmd, pPipelineCountPixels);
                        cmdDispatch(pixelListBuildCmd, groupsW, groupsH, 1);
                    }
                    // Resource Transition
                    {
                        TFBufferBarrier countBarriers[] = {
                            { pPixelTileMaterialCountOffsetsBuffer, TF_RESOURCE_STATE_UNORDERED_ACCESS,
                              TF_RESOURCE_STATE_UNORDERED_ACCESS },
                        };
                        cmdResourceBarrier(pixelListBuildCmd, TF_ARRAY_COUNT(countBarriers), countBarriers, 0, NULL, 0, NULL);
                    }

                    cmdEndGpuTimestampQuery(pixelListBuildCmd, profileToken);
                }

                // 3. Prefix Sum passes

                // Prefix Sum Material pass
                {
                    cmdBeginGpuTimestampQuery(pixelListBuildCmd, profileToken, "Prefix Sum Material");

                    // Dispatch
                    {
                        cmdBindPipeline(pixelListBuildCmd, pPipelinePrefixSumTileMaterials);
                        cmdDispatch(pixelListBuildCmd, numTiles, 1, 1);
                    }
                    // Resource Transition
                    {
                        TFBufferBarrier prefixMaterialBarriers[] = {
                            { pPixelTileMaterialCountOffsetsBuffer, TF_RESOURCE_STATE_UNORDERED_ACCESS,
                              TF_RESOURCE_STATE_UNORDERED_ACCESS },
                            { pPixelCountPerPipelineTileBuffer, TF_RESOURCE_STATE_UNORDERED_ACCESS, TF_RESOURCE_STATE_UNORDERED_ACCESS },
                        };
                        cmdResourceBarrier(pixelListBuildCmd, TF_ARRAY_COUNT(prefixMaterialBarriers), prefixMaterialBarriers, 0, NULL, 0,
                                           NULL);
                    }

                    cmdEndGpuTimestampQuery(pixelListBuildCmd, profileToken);
                }
                // Prefix Sum Tile pass
                {
                    cmdBeginGpuTimestampQuery(pixelListBuildCmd, profileToken, "Prefix Sum Tile");

                    // Dispatch
                    {
                        cmdBindPipeline(pixelListBuildCmd, pPipelinePrefixSumTiles); // one group per pipeline
                        cmdDispatch(pixelListBuildCmd, gShadeComputePipelineCount, 1, 1);
                    }
                    // Resource Transition
                    {
                        TFBufferBarrier prefixTileBarriers[] = {
                            { pPixelOffsetPerPipelineTileBuffer, TF_RESOURCE_STATE_UNORDERED_ACCESS, TF_RESOURCE_STATE_UNORDERED_ACCESS },
                            { pPixelPipelineCountOffsetsBuffer, TF_RESOURCE_STATE_UNORDERED_ACCESS, TF_RESOURCE_STATE_UNORDERED_ACCESS },
                        };
                        cmdResourceBarrier(pixelListBuildCmd, TF_ARRAY_COUNT(prefixTileBarriers), prefixTileBarriers, 0, NULL, 0, NULL);
                    }

                    cmdEndGpuTimestampQuery(pixelListBuildCmd, profileToken);
                }
                // Prefix Sum Pipeline pass
                {
                    cmdBeginGpuTimestampQuery(pixelListBuildCmd, profileToken, "Prefix Sum Pipeline");

                    // Dispatch
                    {
                        cmdBindPipeline(pixelListBuildCmd, pPipelinePrefixSumPipelines);
                        cmdDispatch(pixelListBuildCmd, 1, 1, 1);
                    }
                    // Resource Transition
                    {
                        TFBufferBarrier prefixPipelineBarriers[] = {
                            { pPixelPipelineCountOffsetsBuffer, TF_RESOURCE_STATE_UNORDERED_ACCESS, TF_RESOURCE_STATE_UNORDERED_ACCESS },
                        };
                        cmdResourceBarrier(pixelListBuildCmd, TF_ARRAY_COUNT(prefixPipelineBarriers), prefixPipelineBarriers, 0, NULL, 0,
                                           NULL);
                    }

                    cmdEndGpuTimestampQuery(pixelListBuildCmd, profileToken);
                }

                // 4. Generate Dispatch Args pass
                {
                    // Emit bucket records and indirect dispatch args per non empty pipeline tile
                    cmdBeginGpuTimestampQuery(pixelListBuildCmd, profileToken, "Generate Dispatch Args");

                    // Dispatch
                    {
                        cmdBindPipeline(pixelListBuildCmd, pPipelineGenerateDispatchArgs);
                        cmdDispatch(pixelListBuildCmd, (numPipelineTiles + 63u) / 64u, 1, 1);
                    }
                    // Resource Transition
                    {
                        TFBufferBarrier generateArgsBarriers[] = {
                            { pDispatchOffsetsBuffer, TF_RESOURCE_STATE_UNORDERED_ACCESS, TF_RESOURCE_STATE_UNORDERED_ACCESS },
                            { pDispatchIndirectArgsBuffer, TF_RESOURCE_STATE_UNORDERED_ACCESS, TF_RESOURCE_STATE_UNORDERED_ACCESS },
                        };
                        cmdResourceBarrier(pixelListBuildCmd, TF_ARRAY_COUNT(generateArgsBarriers), generateArgsBarriers, 0, NULL, 0, NULL);
                    }

                    cmdEndGpuTimestampQuery(pixelListBuildCmd, profileToken);
                }

                // 5. Finalize Offsets pass
                {
                    // Upgrade each key's offset to its absolute sorted list position
                    cmdBeginGpuTimestampQuery(pixelListBuildCmd, profileToken, "Finalize Offsets");

                    // Dispatch
                    {
                        cmdBindPipeline(pixelListBuildCmd, pPipelineFinalizeTileMaterialOffsets);
                        const uint32_t finalizeGroups = (numTileMaterials + 63u) / 64u;
                        const uint32_t finalizeGroupsY = (finalizeGroups + LINEAR_DISPATCH_GROUPS_X - 1u) / LINEAR_DISPATCH_GROUPS_X;
                        cmdDispatch(pixelListBuildCmd, LINEAR_DISPATCH_GROUPS_X, finalizeGroupsY, 1);
                    }
                    // Resource Transition
                    {
                        TFBufferBarrier finalizeOffsetBarriers[] = {
                            { pPixelTileMaterialCountOffsetsBuffer, TF_RESOURCE_STATE_UNORDERED_ACCESS,
                              TF_RESOURCE_STATE_UNORDERED_ACCESS },
                        };
                        cmdResourceBarrier(pixelListBuildCmd, TF_ARRAY_COUNT(finalizeOffsetBarriers), finalizeOffsetBarriers, 0, NULL, 0,
                                           NULL);
                    }

                    cmdEndGpuTimestampQuery(pixelListBuildCmd, profileToken);
                }

                // 6. Write Pixel Commands pass
                {
                    // Scatter every visible pixel into its sorted slot
                    cmdBeginGpuTimestampQuery(pixelListBuildCmd, profileToken, "Write Pixel Commands");

                    // Dispatch
                    {
                        cmdBindPipeline(pixelListBuildCmd, pPipelineWritePixelCommands);
                        cmdDispatch(pixelListBuildCmd, groupsW, groupsH, 1);
                    }
                    // Resource Transition
                    {
                        TFBufferBarrier writeCommandBarriers[] = {
                            { pPixelCommandsBuffer, TF_RESOURCE_STATE_UNORDERED_ACCESS, TF_RESOURCE_STATE_UNORDERED_ACCESS },
                            { pPixelTileMaterialCountOffsetsBuffer, TF_RESOURCE_STATE_UNORDERED_ACCESS,
                              TF_RESOURCE_STATE_UNORDERED_ACCESS },
                            { pPixelCommandPaddingMaskBuffer, TF_RESOURCE_STATE_UNORDERED_ACCESS, TF_RESOURCE_STATE_UNORDERED_ACCESS },
                        };
                        cmdResourceBarrier(pixelListBuildCmd, TF_ARRAY_COUNT(writeCommandBarriers), writeCommandBarriers, 0, NULL, 0, NULL);
                    }

                    cmdEndGpuTimestampQuery(pixelListBuildCmd, profileToken);
                }

                // Resource Transition
                {
                    TFBufferBarrier toIndirect[] = {
                        { pDispatchIndirectArgsBuffer, TF_RESOURCE_STATE_UNORDERED_ACCESS, TF_RESOURCE_STATE_INDIRECT_ARGUMENT },
                        { pDispatchOffsetsBuffer, TF_RESOURCE_STATE_UNORDERED_ACCESS, TF_RESOURCE_STATE_UNORDERED_ACCESS },
                        { pPixelCommandPaddingMaskBuffer, TF_RESOURCE_STATE_UNORDERED_ACCESS, TF_RESOURCE_STATE_UNORDERED_ACCESS },
                        { pPixelCommandsBuffer, TF_RESOURCE_STATE_UNORDERED_ACCESS, TF_RESOURCE_STATE_UNORDERED_ACCESS },
                    };
                    cmdResourceBarrier(pixelListBuildCmd, TF_ARRAY_COUNT(toIndirect), toIndirect, 0, NULL, 0, NULL);
                }

                cmdEndGpuTimestampQuery(pixelListBuildCmd, profileToken);
            }

            if (useAsyncPixelListBuild)
            {
                cmdEndGpuFrameProfile(pixelListBuildCmd, gAsyncPixelListBuildProfileToken);
                endCmd(pixelListBuildCmd);

                TFQueueSubmitDesc buildSubmitDesc = {};
                buildSubmitDesc.mCmdCount = 1;
                buildSubmitDesc.ppCmds = &pixelListBuildCmd;
                buildSubmitDesc.ppWaitSemaphores = &pVBDoneSemaphores[frameIdx];
                buildSubmitDesc.mWaitSemaphoreCount = 1;
                buildSubmitDesc.ppSignalSemaphores = &asyncBuildElem.pSemaphore;
                buildSubmitDesc.mSignalSemaphoreCount = 1;
                buildSubmitDesc.pSignalFence = asyncBuildElem.pFence;
                queueSubmit(pComputeQueue, &buildSubmitDesc);

                graphicsCmd = graphicsElem.pCmds[1];
                beginCmd(graphicsCmd);
                cmdBindDescriptorSet(graphicsCmd, 0, pDescriptorSetPersistent);
                cmdBindDescriptorSet(graphicsCmd, frameIdx, pDescriptorSetPerFrame);
            }

            /************************************************************************/
            // Shadow map pass
            /************************************************************************/
            {
                cmdBeginGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken, "Shadow Pass");

                // Resource Transition
                {
                    TFRenderTargetBarrier passBarriers[] = { { pRenderTargetShadow, TF_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                                                               TF_RESOURCE_STATE_DEPTH_WRITE } };
                    cmdResourceBarrier(graphicsCmd, 0, NULL, 0, NULL, TF_ARRAY_COUNT(passBarriers), passBarriers);
                }
                // Bind Render Targets
                {
                    TFBindRenderTargetsDesc bindRenderTargets = {};
                    bindRenderTargets.mDepthStencil = { pRenderTargetShadow, TF_LOAD_ACTION_CLEAR };
                    cmdBindRenderTargets(graphicsCmd, &bindRenderTargets);
                    cmdSetViewport(graphicsCmd, 0.0f, 0.0f, (float)pRenderTargetShadow->mWidth, (float)pRenderTargetShadow->mHeight, 0.0f,
                                   1.0f);
                    cmdSetScissor(graphicsCmd, 0, 0, pRenderTargetShadow->mWidth, pRenderTargetShadow->mHeight);
                }
                // Draw
                {
                    TFBuffer* pIndexBuffer = pVisibilityBuffer->ppFilteredIndexBuffer[frameIdx * NUM_CULLING_VIEWPORTS + VIEW_SHADOW];
                    cmdBindIndexBuffer(graphicsCmd, pIndexBuffer, TF_INDEX_TYPE_UINT32, 0);

                    for (uint32_t geomSet = 0; geomSet < NUM_GEOMETRY_SETS; ++geomSet)
                    {
                        cmdBindPipeline(graphicsCmd, pPipelineShadowPass[geomSet]);

                        uint64_t  indirectBufferByteOffset = GET_INDIRECT_DRAW_ELEM_INDEX(VIEW_SHADOW, geomSet, 0) * sizeof(uint32_t);
                        TFBuffer* pIndirectBuffer = pVisibilityBuffer->ppIndirectDrawArgBuffer[frameIdx];
                        cmdExecuteIndirect(graphicsCmd, TF_INDIRECT_DRAW_INDEX, 1, pIndirectBuffer, indirectBufferByteOffset, NULL, 0);
                    }
                    cmdBindRenderTargets(graphicsCmd, NULL);
                }

                // Resource Transition
                {
                    TFRenderTargetBarrier passBarriers[] = { { pRenderTargetShadow, TF_RESOURCE_STATE_DEPTH_WRITE,
                                                               TF_RESOURCE_STATE_PIXEL_SHADER_RESOURCE } };
                    cmdResourceBarrier(graphicsCmd, 0, NULL, 0, NULL, TF_ARRAY_COUNT(passBarriers), passBarriers);
                }

                cmdEndGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken);
            }

            if (useQuadMaterialPath)
            {
                // Build material tile masks pass
                {
                    // Bin the materials present in each screen tile, persist the per tile mask and count how many
                    // shade instances each shader will produce
                    cmdBeginGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken, "Build Material Tile Masks");

                    // Dispatch
                    {
                        cmdBindPipeline(graphicsCmd, pPipelineBuildMaterialTileMasks);
                        cmdBindDescriptorSet(graphicsCmd, 0, pDescriptorSetPersistent);
                        cmdBindDescriptorSet(graphicsCmd, frameIdx, pDescriptorSetPerFrame);
                        cmdDispatch(graphicsCmd, VB_SHADE_TILE_COUNT, 1, 1);
                    }
                    // Resource Transition
                    {
                        TFBufferBarrier passBarriers[] = {
                            { pMaterialTileMasksBuffer, TF_RESOURCE_STATE_UNORDERED_ACCESS, TF_RESOURCE_STATE_UNORDERED_ACCESS },
                            { pShaderInstanceCountsBuffer, TF_RESOURCE_STATE_UNORDERED_ACCESS, TF_RESOURCE_STATE_UNORDERED_ACCESS },
                        };
                        cmdResourceBarrier(graphicsCmd, TF_ARRAY_COUNT(passBarriers), passBarriers, 0, NULL, 0, NULL);
                    }

                    cmdEndGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken);
                }

                // Shader instance prefix sum pass
                {
                    // Turn the per shader counts into contiguous instance buffer regions and per shader indirect draw args
                    cmdBeginGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken, "Shader Instance Prefix Sum");

                    // Dispatch
                    {
                        cmdBindPipeline(graphicsCmd, pPipelineShaderInstancePrefixSum);
                        cmdBindDescriptorSet(graphicsCmd, 0, pDescriptorSetPersistent);
                        cmdBindDescriptorSet(graphicsCmd, frameIdx, pDescriptorSetPerFrame);
                        cmdDispatch(graphicsCmd, 1, 1, 1);
                    }
                    // Resource Transition
                    {
                        TFBufferBarrier passBarriers[] = {
                            { pShadeDrawArgsBuffer, TF_RESOURCE_STATE_UNORDERED_ACCESS, TF_RESOURCE_STATE_UNORDERED_ACCESS },
                            { pShaderInstanceCursorsBuffer, TF_RESOURCE_STATE_UNORDERED_ACCESS, TF_RESOURCE_STATE_UNORDERED_ACCESS },
                            { pShaderInstanceCountsBuffer, TF_RESOURCE_STATE_UNORDERED_ACCESS, TF_RESOURCE_STATE_UNORDERED_ACCESS },
                        };
                        cmdResourceBarrier(graphicsCmd, TF_ARRAY_COUNT(passBarriers), passBarriers, 0, NULL, 0, NULL);
                    }

                    cmdEndGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken);
                }

                // Scatter shader instances pass
                {
                    // Write each present tile-material pair into its shader contiguous region of the instance buffer
                    cmdBeginGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken, "Scatter Shader Instances");

                    // Dispatch
                    {
                        cmdBindPipeline(graphicsCmd, pPipelineScatterShaderInstances);
                        cmdBindDescriptorSet(graphicsCmd, 0, pDescriptorSetPersistent);
                        cmdBindDescriptorSet(graphicsCmd, frameIdx, pDescriptorSetPerFrame);
                        cmdDispatch(graphicsCmd, VB_SHADE_TILE_COUNT, 1, 1);
                    }
                    // Resource Transition
                    {
                        TFBufferBarrier passBarriers[] = {
#if defined(ORBIS)
                            { pShadeQuadInstancesBuffer, TF_RESOURCE_STATE_UNORDERED_ACCESS, TF_RESOURCE_STATE_UNORDERED_ACCESS },
#else
                            { pShadeQuadInstancesBuffer, TF_RESOURCE_STATE_UNORDERED_ACCESS, TF_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER },
#endif
                            { pShadeDrawArgsBuffer, TF_RESOURCE_STATE_UNORDERED_ACCESS, TF_RESOURCE_STATE_INDIRECT_ARGUMENT },
                        };
                        cmdResourceBarrier(graphicsCmd, TF_ARRAY_COUNT(passBarriers), passBarriers, 0, NULL, 0, NULL);
                    }

                    cmdEndGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken);
                }

                // Material ID depth pass
                {
                    // Copy the per pixel material ID into a depth target so the shade pass can reject non-matching
                    // materials with the hardware early depth test.
                    cmdBeginGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken, "Copy MaterialID Texture");

                    // Resource Transition
                    {
                        TFRenderTargetBarrier passBarriers[] = {
                            { pRenderTargetVBMaterialIDDepth, TF_RESOURCE_STATE_DEPTH_READ, TF_RESOURCE_STATE_DEPTH_WRITE },
                        };
                        cmdResourceBarrier(graphicsCmd, 0, NULL, 0, NULL, TF_ARRAY_COUNT(passBarriers), passBarriers);
                    }
                    // Bind Render Targets
                    {
                        TFBindRenderTargetsDesc bindRenderTargets = {};
                        bindRenderTargets.mDepthStencil = { pRenderTargetVBMaterialIDDepth, TF_LOAD_ACTION_CLEAR };
                        cmdBindRenderTargets(graphicsCmd, &bindRenderTargets);
                        cmdSetViewport(graphicsCmd, 0.0f, 0.0f, (float)pRenderTargetVBMaterialIDDepth->mWidth,
                                       (float)pRenderTargetVBMaterialIDDepth->mHeight, 0.0f, 1.0f);
                        cmdSetScissor(graphicsCmd, 0, 0, pRenderTargetVBMaterialIDDepth->mWidth, pRenderTargetVBMaterialIDDepth->mHeight);
                    }
                    // Draw
                    {
                        cmdBindPipeline(graphicsCmd, pPipelineVisibilityBufferMaterialIDDepth);
                        cmdDraw(graphicsCmd, 3, 0);
                        cmdBindRenderTargets(graphicsCmd, NULL);
                    }
                    // Resource Transition
                    {
                        TFRenderTargetBarrier passBarriers[] = {
                            { pRenderTargetVBMaterialIDDepth, TF_RESOURCE_STATE_DEPTH_WRITE, TF_RESOURCE_STATE_DEPTH_READ },
                        };
                        cmdResourceBarrier(graphicsCmd, 0, NULL, 0, NULL, TF_ARRAY_COUNT(passBarriers), passBarriers);
                    }

                    cmdEndGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken);
                }
            }
            // MSAA edge stencil pass
            if (gAppSettings.mMsaaLevel > TF_SAMPLE_COUNT_1)
            {
                // This depth-only pass will render to the stencil buffer. Samples that must be shaded in
                // future shading or post-processing passes will be set to 0x01.
                cmdBeginGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken, "MSAA Edges Stencil Pass");

                // Resource Transition
                {
                    TFRenderTargetBarrier passBarriers[] = {
                        { pRenderTargetVBMaterialIDDepth, TF_RESOURCE_STATE_DEPTH_READ, TF_RESOURCE_STATE_DEPTH_WRITE },
                    };
                    cmdResourceBarrier(graphicsCmd, 0, NULL, 0, NULL, TF_ARRAY_COUNT(passBarriers), passBarriers);
                }
                // Bind Render Targets
                {
                    TFBindRenderTargetsDesc bindRenderTargets = {};
                    bindRenderTargets.mDepthStencil = { pRenderTargetVBMaterialIDDepth, TF_LOAD_ACTION_LOAD, TF_STORE_ACTION_STORE,
                                                        TF_LOAD_ACTION_CLEAR, TF_STORE_ACTION_STORE };
                    cmdBindRenderTargets(graphicsCmd, &bindRenderTargets);
                    cmdSetViewport(graphicsCmd, 0.0f, 0.0f, (float)pRenderTargetVBMaterialIDDepth->mWidth,
                                   (float)pRenderTargetVBMaterialIDDepth->mHeight, 0.0f, 1.0f);
                    cmdSetScissor(graphicsCmd, 0, 0, pRenderTargetVBMaterialIDDepth->mWidth, pRenderTargetVBMaterialIDDepth->mHeight);
                }
                // Draw
                {
                    cmdBindPipeline(graphicsCmd, pPipelineDrawMSAAEdges);
                    cmdSetStencilReferenceValue(graphicsCmd, MSAA_STENCIL_MASK);
                    cmdDraw(graphicsCmd, 3, 0);
                }

                cmdBindRenderTargets(graphicsCmd, NULL);
                cmdEndGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken);
            }

            // God ray pass chain
            {
                if (gAppSettings.mEnableGodray)
                {
                    const bool isMSAAEnabled = gAppSettings.mMsaaLevel > TF_SAMPLE_COUNT_1;

                    // MSAA downsample pass
                    if (isMSAAEnabled)
                    {
                        cmdBeginGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken, "MSAA Edges Stencil Downscale Pass");

                        // Resource Transition
                        {
                            TFRenderTargetBarrier passBarriers[] = {
                                { pRenderTargetVBMaterialIDDepth, TF_RESOURCE_STATE_DEPTH_WRITE, TF_RESOURCE_STATE_PIXEL_SHADER_RESOURCE },
                                { pDownscaledMSAAEdgesStencilBuffer, TF_RESOURCE_STATE_DEPTH_READ, TF_RESOURCE_STATE_DEPTH_WRITE },
                            };
                            cmdResourceBarrier(graphicsCmd, 0, NULL, 0, NULL, TF_ARRAY_COUNT(passBarriers), passBarriers);
                        }
                        // Bind Render Targets
                        {
                            TFBindRenderTargetsDesc bindRenderTargets = {};
                            bindRenderTargets.mDepthStencil = { pDownscaledMSAAEdgesStencilBuffer, TF_LOAD_ACTION_DONTCARE,
                                                                TF_STORE_ACTION_DONTCARE, TF_LOAD_ACTION_CLEAR, TF_STORE_ACTION_STORE };
                            cmdBindRenderTargets(graphicsCmd, &bindRenderTargets);
                            cmdSetViewport(graphicsCmd, 0.0f, 0.0f, (float)pDownscaledMSAAEdgesStencilBuffer->mWidth,
                                           (float)pDownscaledMSAAEdgesStencilBuffer->mHeight, 0.0f, 1.0f);
                            cmdSetScissor(graphicsCmd, 0, 0, pDownscaledMSAAEdgesStencilBuffer->mWidth,
                                          pDownscaledMSAAEdgesStencilBuffer->mHeight);
                        }
                        // Draw
                        {
                            cmdBindPipeline(graphicsCmd, pPipelineDownscaleMSAAEdges);
                            cmdSetStencilReferenceValue(graphicsCmd, MSAA_STENCIL_MASK);
                            cmdDraw(graphicsCmd, 3, 0);
                        }

                        cmdBindRenderTargets(graphicsCmd, NULL);
                        cmdEndGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken);
                    }
                    // God ray pass
                    {
                        cmdBeginGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken, "God Ray");

                        // Resource Transition
                        {
                            TFRenderTarget* pGodRayActiveRenderTarget = isMSAAEnabled ? pRenderTargetGodRayMS : pRenderTargetGodRay[0];
                            TFRenderTargetBarrier passBarriers[] = {
                                { pGodRayActiveRenderTarget, TF_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, TF_RESOURCE_STATE_RENDER_TARGET },
                                { pDownscaledMSAAEdgesStencilBuffer, TF_RESOURCE_STATE_DEPTH_WRITE, TF_RESOURCE_STATE_DEPTH_READ }
                            };
                            cmdResourceBarrier(graphicsCmd, 0, NULL, 0, NULL, isMSAAEnabled ? TF_ARRAY_COUNT(passBarriers) : 1,
                                               passBarriers);
                        }
                        // Bind Render Targets
                        {
                            TFRenderTarget* pGodRayActiveRenderTarget = isMSAAEnabled ? pRenderTargetGodRayMS : pRenderTargetGodRay[0];

                            TFBindRenderTargetsDesc bindRenderTargets = {};
                            bindRenderTargets.mRenderTargetCount = 1;
                            bindRenderTargets.mRenderTargets[0] = { pGodRayActiveRenderTarget, TF_LOAD_ACTION_CLEAR };
                            if (isMSAAEnabled)
                            {
                                bindRenderTargets.mDepthStencil = { pDownscaledMSAAEdgesStencilBuffer, TF_LOAD_ACTION_DONTCARE,
                                                                    TF_STORE_ACTION_DONTCARE, TF_LOAD_ACTION_LOAD, TF_STORE_ACTION_NONE };
                            }
                            cmdBindRenderTargets(graphicsCmd, &bindRenderTargets);
                            cmdSetViewport(graphicsCmd, 0.0f, 0.0f, (float)pGodRayActiveRenderTarget->mWidth,
                                           (float)pGodRayActiveRenderTarget->mHeight, 0.0f, 1.0f);
                            cmdSetScissor(graphicsCmd, 0, 0, pGodRayActiveRenderTarget->mWidth, pGodRayActiveRenderTarget->mHeight);
                        }
                        // Draw
                        {
                            cmdBindPipeline(graphicsCmd, pPipelineGodRayPass);
                            cmdSetStencilReferenceValue(graphicsCmd, MSAA_STENCIL_MASK);
                            cmdDraw(graphicsCmd, 3, 0);
                        }

                        cmdBindRenderTargets(graphicsCmd, NULL);
                        cmdEndGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken);
                    }
                    // God ray resolve pass
                    if (isMSAAEnabled)
                    {
                        cmdBeginGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken, "God Ray Resolve");

                        // Resource Transition
                        {
                            TFRenderTargetBarrier passBarriers[] = {
                                { pRenderTargetGodRayMS, TF_RESOURCE_STATE_RENDER_TARGET, TF_RESOURCE_STATE_PIXEL_SHADER_RESOURCE },
                                { pRenderTargetGodRay[0], TF_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, TF_RESOURCE_STATE_RENDER_TARGET },
                            };
                            cmdResourceBarrier(graphicsCmd, 0, NULL, 0, NULL, TF_ARRAY_COUNT(passBarriers), passBarriers);
                        }
                        // Bind Render Targets
                        {
                            TFRenderTarget* pResolveTarget = pRenderTargetGodRay[0];

                            TFBindRenderTargetsDesc bindRenderTargets = {};
                            bindRenderTargets.mRenderTargetCount = 1;
                            bindRenderTargets.mRenderTargets[0] = { pResolveTarget, TF_LOAD_ACTION_CLEAR };
                            cmdBindRenderTargets(graphicsCmd, &bindRenderTargets);
                            cmdSetViewport(graphicsCmd, 0.0f, 0.0f, (float)pResolveTarget->mWidth, (float)pResolveTarget->mHeight, 0.0f,
                                           1.0f);
                            cmdSetScissor(graphicsCmd, 0, 0, pResolveTarget->mWidth, pResolveTarget->mHeight);
                        }
                        // Draw
                        {
                            cmdBindPipeline(graphicsCmd, pPipelineResolveGodRay);
                            cmdBindDescriptorSet(graphicsCmd, frameIdx * gResolveTargetCount + gResolveGodRayPassIndex,
                                                 pDescriptorSetResolve);
                            cmdDraw(graphicsCmd, 3, 0);
                        }

                        cmdBindRenderTargets(graphicsCmd, NULL);
                        cmdEndGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken);
                    }
                    // God ray blur passes
                    {
                        // Update blur weights
                        {
                            TFBufferUpdateDesc bufferUpdate = { pBufferBlurWeights };
                            beginUpdateResource(&bufferUpdate);
                            memcpy(bufferUpdate.pMappedData, &gBlurWeightsUniform, sizeof(gBlurWeightsUniform));
                            endUpdateResource(&bufferUpdate);
                        }
                        // Horizontal pass
                        {
                            cmdBeginGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken, "God Ray Blur Horizontal");

                            // Resource Transition
                            {
                                TFRenderTargetBarrier passBarriers[] = {
                                    { pRenderTargetGodRay[1], TF_RESOURCE_STATE_UNORDERED_ACCESS, TF_RESOURCE_STATE_UNORDERED_ACCESS },
                                    { pRenderTargetGodRay[0], TF_RESOURCE_STATE_RENDER_TARGET, TF_RESOURCE_STATE_UNORDERED_ACCESS },
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

                                cmdBindDescriptorSet(graphicsCmd, frameIdx * BLUR_PASS_TYPE_COUNT, pDescriptorSetGodRayBlurPassPerDraw);
                                cmdDispatch(graphicsCmd, threadGroupSizeX, threadGroupSizeY, 1);
                            }

                            cmdEndGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken);
                        }
                        // Vertical pass
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
                                    { pRenderTargetGodRay[1], TF_RESOURCE_STATE_UNORDERED_ACCESS, TF_RESOURCE_STATE_UNORDERED_ACCESS },
                                };
                                cmdResourceBarrier(graphicsCmd, 0, NULL, 0, NULL, TF_ARRAY_COUNT(finalBlurBarriers), finalBlurBarriers);
                            }

                            cmdEndGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken);
                        }
                    }
                }
            }

            /************************************************************************/
            // Async build outputs are consumed by the final graphics command buffer.
            /************************************************************************/
            if (useAsyncPixelListBuild)
            {
                endCmd(graphicsCmd);
                // Compute pixel build overlaps shadow map and godrays
                TFQueueSubmitDesc overlapSubmitDesc = {};
                overlapSubmitDesc.mCmdCount = 1;
                overlapSubmitDesc.ppCmds = &graphicsCmd;
                queueSubmit(pGraphicsQueue, &overlapSubmitDesc);

                graphicsCmd = graphicsElem.pCmds[2];
                beginCmd(graphicsCmd);
                cmdBindDescriptorSet(graphicsCmd, 0, pDescriptorSetPersistent);
                cmdBindDescriptorSet(graphicsCmd, frameIdx, pDescriptorSetPerFrame);
            }

            /************************************************************************/
            // Visibility buffer shading pass
            /************************************************************************/
            {
                // Render a fullscreen triangle to evaluate shading for every pixel. This render step uses the render target generated by
                // DrawVisibilityBufferPass to get the draw / triangle IDs to reconstruct and interpolate vertex attributes per pixel. This
                // method doesn't set any vertex/index buffer because the triangle positions are calculated internally using vertex_id.
                cmdBeginGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken, "VB Shading Pass");

                if (useComputePixelListPath)
                {
                    // Compute shade writes lit color directly into pIntermediateRenderTarget.
                    const uint32_t        renderWidth = pIntermediateRenderTarget->mWidth;
                    const uint32_t        renderHeight = pIntermediateRenderTarget->mHeight;
                    const uint32_t        groupsW = (renderWidth + 7u) / 8u;
                    const uint32_t        groupsH = (renderHeight + 7u) / 8u;
                    // The compute path writes the shared intermediate target as a UAV.
                    TFRenderTargetBarrier toUav[] = {
                        { pIntermediateRenderTarget, TF_RESOURCE_STATE_RENDER_TARGET, TF_RESOURCE_STATE_UNORDERED_ACCESS },
                        { pRenderTargetShadow, TF_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, TF_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE },
                    };
                    cmdResourceBarrier(graphicsCmd, 0, NULL, 0, NULL, TF_ARRAY_COUNT(toUav), toUav);

                    cmdBindDescriptorSet(graphicsCmd, 0, pDescriptorSetPersistent);
                    cmdBindDescriptorSet(graphicsCmd, frameIdx, pDescriptorSetPerFrame);

                    // Bucketed indirect dispatches per shade pipeline.
                    const uint64_t argsStride = 3u * sizeof(uint32_t);
                    for (uint32_t p = 0; p < gShadeComputePipelineCount; ++p)
                    {
                        cmdBindPipeline(graphicsCmd, pPipelineShadeCompute[p]);
                        if (p == 0)
                            cmdBindDescriptorSet(graphicsCmd, 0, pDescriptorSetShadeOutput);

                        for (uint32_t b = 0; b < (uint32_t)MAX_NUM_DISPATCH_BUCKETS; ++b)
                        {
                            uint32_t slot = p * (uint32_t)MAX_NUM_DISPATCH_BUCKETS + b;
#if !defined(ANDROID)
                            // Android recovers the bucket from groupCountX in-shader.
                            cmdBindDescriptorSet(graphicsCmd, slot, pDescriptorSetShadeDispatch);
#endif
                            cmdExecuteIndirect(graphicsCmd, TF_INDIRECT_DISPATCH, 1, pDispatchIndirectArgsBuffer, slot * argsStride, NULL,
                                               0);
                        }
                    }

                    // Foreground and sky passes write the same UAV.
                    TFRenderTargetBarrier shadeToSky[] = {
                        { pIntermediateRenderTarget, TF_RESOURCE_STATE_UNORDERED_ACCESS, TF_RESOURCE_STATE_UNORDERED_ACCESS },
                    };
                    cmdResourceBarrier(graphicsCmd, 0, NULL, 0, NULL, TF_ARRAY_COUNT(shadeToSky), shadeToSky);

                    // Background fill; the sorted pixel lists cover foreground only.
                    cmdBindPipeline(graphicsCmd, pPipelineShadeComputeSky);
                    cmdDispatch(graphicsCmd, groupsW, groupsH, 1);

                    // Restore states expected by the shared post-shade path.
                    TFRenderTargetBarrier toSrv[] = {
                        { pIntermediateRenderTarget, TF_RESOURCE_STATE_UNORDERED_ACCESS, TF_RESOURCE_STATE_RENDER_TARGET },
                        { pRenderTargetShadow, TF_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, TF_RESOURCE_STATE_PIXEL_SHADER_RESOURCE },
                    };
                    cmdResourceBarrier(graphicsCmd, 0, NULL, 0, NULL, TF_ARRAY_COUNT(toSrv), toSrv);
                    TFBufferBarrier argsBack[] = { { pDispatchIndirectArgsBuffer, TF_RESOURCE_STATE_INDIRECT_ARGUMENT,
                                                     TF_RESOURCE_STATE_UNORDERED_ACCESS } };
                    cmdResourceBarrier(graphicsCmd, TF_ARRAY_COUNT(argsBack), argsBack, 0, NULL, 0, NULL);

                    cmdEndGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken);
                }
                else
                {
                    // Resource Transition
                    if (gAppSettings.mMsaaLevel > TF_SAMPLE_COUNT_1)
                    {
                        TFRenderTargetBarrier passBarriers[] = { { pRenderTargetVBMaterialIDDepth,
                                                                   gAppSettings.mEnableGodray ? TF_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
                                                                                              : TF_RESOURCE_STATE_DEPTH_WRITE,
                                                                   TF_RESOURCE_STATE_DEPTH_READ } };
                        cmdResourceBarrier(graphicsCmd, 0, NULL, 0, NULL, TF_ARRAY_COUNT(passBarriers), passBarriers);
                    }
                    // Bind Render Targets
                    {
                        TFRenderTarget* pDestinationRenderTarget =
                            gAppSettings.mMsaaLevel > TF_SAMPLE_COUNT_1 ? pRenderTargetMSAA : pScreenRenderTarget;

                        // Set load actions to clear the screen to black
                        TFBindRenderTargetsDesc bindRenderTargets = {};
                        bindRenderTargets.mRenderTargetCount = 1;
                        bindRenderTargets.mRenderTargets[0] = { pDestinationRenderTarget, TF_LOAD_ACTION_CLEAR };
                        const TFLoadActionType depthLoadAction =
                            gAppSettings.mHandleMultipleMaterials ? TF_LOAD_ACTION_LOAD : TF_LOAD_ACTION_DONTCARE;
                        bindRenderTargets.mDepthStencil = { pRenderTargetVBMaterialIDDepth, depthLoadAction, TF_STORE_ACTION_DONTCARE,
                                                            TF_LOAD_ACTION_LOAD, TF_STORE_ACTION_NONE };
                        cmdBindRenderTargets(graphicsCmd, &bindRenderTargets);
                        cmdSetViewport(graphicsCmd, 0.0f, 0.0f, (float)pDestinationRenderTarget->mWidth,
                                       (float)pDestinationRenderTarget->mHeight, 0.0f, 1.0f);
                        cmdSetScissor(graphicsCmd, 0, 0, pDestinationRenderTarget->mWidth, pDestinationRenderTarget->mHeight);
                    }
                    // Draw
                    {
                        if (!gAppSettings.mHandleMultipleMaterials)
                        {
                            cmdBindPipeline(graphicsCmd, pPipelineVisibilityBufferShadeSrgb[gAppSettings.mEnableAO]);
                            cmdSetStencilReferenceValue(graphicsCmd, MSAA_STENCIL_MASK);
                            // A single triangle is rendered without specifying a vertex buffer (triangle positions are calculated
                            // internally using vertex_id)
                            cmdDraw(graphicsCmd, 3, 0);
                        }
                        else
                        {
                            cmdSetStencilReferenceValue(graphicsCmd, MSAA_STENCIL_MASK);

                            const uint32_t indirectArgsStride = 4u * sizeof(uint32_t);

                            cmdBindPipeline(graphicsCmd, pPipelineVisibilityBufferShadeSrgb[2 + gAppSettings.mEnableAO]);
#if !defined(ORBIS)
                            const uint32_t shadeInstanceStride = sizeof(uint32_t);
                            cmdBindVertexBuffer(graphicsCmd, 1, &pShadeQuadInstancesBuffer, &shadeInstanceStride, NULL);
#endif
                            for (uint32_t shaderIndex = 0; shaderIndex < MAX_MATERIAL_SHADERS; ++shaderIndex)
                            {
                                cmdExecuteIndirect(graphicsCmd, TF_INDIRECT_DRAW, 1, pShadeDrawArgsBuffer, shaderIndex * indirectArgsStride,
                                                   NULL, 0);
                            }

                            // Sky fills the background
                            cmdBindPipeline(graphicsCmd, pPipelineVisibilityBufferShadeSkySrgb);
                            cmdDraw(graphicsCmd, 3, 0);
                        }
                    }

                    cmdBindRenderTargets(graphicsCmd, NULL);

                    // Resource Transition
                    if (gAppSettings.mHandleMultipleMaterials)
                    {
                        TFBufferBarrier shadeBuffersBarrier[] = {
#if defined(ORBIS)
                            { pShadeQuadInstancesBuffer, TF_RESOURCE_STATE_UNORDERED_ACCESS, TF_RESOURCE_STATE_UNORDERED_ACCESS },
#else
                            { pShadeQuadInstancesBuffer, TF_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER, TF_RESOURCE_STATE_UNORDERED_ACCESS },
#endif
                            { pShadeDrawArgsBuffer, TF_RESOURCE_STATE_INDIRECT_ARGUMENT, TF_RESOURCE_STATE_UNORDERED_ACCESS },
                        };
                        cmdResourceBarrier(graphicsCmd, TF_ARRAY_COUNT(shadeBuffersBarrier), shadeBuffersBarrier, 0, NULL, 0, NULL);
                    }

                    cmdEndGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken);
                } // else (raster shade path)
            }

            // Final MSAA resolve pass
            if (gAppSettings.mMsaaLevel > TF_SAMPLE_COUNT_1)
            {
                cmdBeginGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken, "MSAA Resolve Pass");

                // Resource Transition
                {
                    TFRenderTargetBarrier passBarriers[] = {
                        { pRenderTargetMSAA, TF_RESOURCE_STATE_RENDER_TARGET, TF_RESOURCE_STATE_PIXEL_SHADER_RESOURCE },
                    };
                    cmdResourceBarrier(graphicsCmd, 0, NULL, 0, NULL, TF_ARRAY_COUNT(passBarriers), passBarriers);
                }
                // Bind Render Targets
                {
                    TFBindRenderTargetsDesc bindRenderTargets = {};
                    bindRenderTargets.mRenderTargetCount = 1;
                    bindRenderTargets.mRenderTargets[0] = { pScreenRenderTarget, TF_LOAD_ACTION_CLEAR };
                    cmdBindRenderTargets(graphicsCmd, &bindRenderTargets);
                    cmdSetViewport(graphicsCmd, 0.0f, 0.0f, (float)pScreenRenderTarget->mWidth, (float)pScreenRenderTarget->mHeight, 0.0f,
                                   1.0f);
                    cmdSetScissor(graphicsCmd, 0, 0, pScreenRenderTarget->mWidth, pScreenRenderTarget->mHeight);
                }
                // Draw
                {
                    cmdBindPipeline(graphicsCmd, pPipelineResolve);
                    cmdBindDescriptorSet(graphicsCmd, frameIdx * gResolveTargetCount + gResolveFinalPassIndex, pDescriptorSetResolve);
                    cmdDraw(graphicsCmd, 3, 0);
                }

                cmdBindRenderTargets(graphicsCmd, NULL);
                cmdEndGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken);
            }

            // Draw debug targets
            {
                if (gAppSettings.mDrawDebugTargets && gAppSettings.mMsaaLevel > TF_SAMPLE_COUNT_1)
                {
                    // MSAA God ray debug texture pass
                    if (gAppSettings.mEnableGodray)
                    {
                        cmdBeginGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken, "Draw MSAA Debug RTs - GodRay");

                        // Resource Transition
                        {
                            TFRenderTargetBarrier barrier[] = { { pRenderTargetDebugGodRayMSAA, TF_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                                                                  TF_RESOURCE_STATE_RENDER_TARGET } };
                            cmdResourceBarrier(graphicsCmd, 0, NULL, 0, NULL, 1, barrier);
                        }
                        // Bind Render Targets
                        {
                            TFBindRenderTargetsDesc bindRenderTargets = {};
                            bindRenderTargets.mRenderTargetCount = 1;
                            bindRenderTargets.mRenderTargets[0] = { pRenderTargetDebugGodRayMSAA, TF_LOAD_ACTION_CLEAR };

                            cmdBindRenderTargets(graphicsCmd, &bindRenderTargets);
                            cmdSetViewport(graphicsCmd, 0.0f, 0.0f, (float)pRenderTargetDebugGodRayMSAA->mWidth,
                                           (float)pRenderTargetDebugGodRayMSAA->mHeight, 0.0f, 1.0f);
                            cmdSetScissor(graphicsCmd, 0, 0, pRenderTargetDebugGodRayMSAA->mWidth, pRenderTargetDebugGodRayMSAA->mHeight);
                        }
                        // Draw
                        {
                            cmdBindPipeline(graphicsCmd, pPipelineDebugMSAA);
                            // We can re-use the resolve descriptor set here for code simplicity
                            cmdBindDescriptorSet(graphicsCmd, frameIdx * gResolveTargetCount + gResolveGodRayPassIndex,
                                                 pDescriptorSetResolve);
                            cmdDraw(graphicsCmd, 3, 0);
                            cmdBindRenderTargets(graphicsCmd, NULL);
                        }
                        // Resource Transition
                        {
                            TFRenderTargetBarrier barrier[] = { { pRenderTargetDebugGodRayMSAA, TF_RESOURCE_STATE_RENDER_TARGET,
                                                                  TF_RESOURCE_STATE_PIXEL_SHADER_RESOURCE } };
                            cmdResourceBarrier(graphicsCmd, 0, NULL, 0, NULL, 1, barrier);
                        }

                        cmdEndGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken);
                    }

                    // MSAA VB Shade debug texture pass
                    {
                        cmdBeginGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken, "Draw MSAA Debug RTs - VB Shade");

                        // Resource Transition
                        {
                            TFRenderTargetBarrier barrier[] = { { pRenderTargetDebugMSAA, TF_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                                                                  TF_RESOURCE_STATE_RENDER_TARGET } };
                            cmdResourceBarrier(graphicsCmd, 0, NULL, 0, NULL, 1, barrier);
                        }
                        // Bind Render Targets
                        {
                            TFBindRenderTargetsDesc bindRenderTargets = {};
                            bindRenderTargets.mRenderTargetCount = 1;
                            bindRenderTargets.mRenderTargets[0] = { pRenderTargetDebugMSAA, TF_LOAD_ACTION_CLEAR };

                            cmdBindRenderTargets(graphicsCmd, &bindRenderTargets);
                            cmdSetViewport(graphicsCmd, 0.0f, 0.0f, (float)pRenderTargetDebugMSAA->mWidth,
                                           (float)pRenderTargetDebugMSAA->mHeight, 0.0f, 1.0f);
                            cmdSetScissor(graphicsCmd, 0, 0, pRenderTargetDebugMSAA->mWidth, pRenderTargetDebugMSAA->mHeight);
                        }
                        // Draw
                        {
                            cmdBindPipeline(graphicsCmd, pPipelineDebugMSAA);
                            // We can re-use the resolve descriptor set here for code simplicity
                            cmdBindDescriptorSet(graphicsCmd, frameIdx * gResolveTargetCount + gResolveFinalPassIndex,
                                                 pDescriptorSetResolve);
                            cmdDraw(graphicsCmd, 3, 0);
                            cmdBindRenderTargets(graphicsCmd, NULL);
                        }
                        // Resource Transition
                        {
                            TFRenderTargetBarrier barrier[] = { { pRenderTargetDebugMSAA, TF_RESOURCE_STATE_RENDER_TARGET,
                                                                  TF_RESOURCE_STATE_PIXEL_SHADER_RESOURCE } };
                            cmdResourceBarrier(graphicsCmd, 0, NULL, 0, NULL, 1, barrier);
                        }

                        cmdEndGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken);
                    }
                }
            }

            // Transition draw inputs and outputs into their pre-graphics pass states
            {
                const uint32_t  maxNumBarriers = NUM_CULLING_VIEWPORTS + 4;
                uint32_t        barrierCount = 0;
                TFBufferBarrier barriers[maxNumBarriers] = {};

                barriers[barrierCount++] = { pLightClusters[frameIdx], TF_RESOURCE_STATE_SHADER_RESOURCE,
                                             TF_RESOURCE_STATE_UNORDERED_ACCESS };
                barriers[barrierCount++] = { pLightClustersCount[frameIdx], TF_RESOURCE_STATE_SHADER_RESOURCE,
                                             TF_RESOURCE_STATE_UNORDERED_ACCESS };

                {
                    barriers[barrierCount++] = { pVisibilityBuffer->ppIndirectDrawArgBuffer[frameIdx],
                                                 TF_RESOURCE_STATE_INDIRECT_ARGUMENT | TF_RESOURCE_STATE_SHADER_RESOURCE,
                                                 TF_RESOURCE_STATE_UNORDERED_ACCESS };

                    barriers[barrierCount++] = { pVisibilityBuffer->ppIndirectDataBuffer[frameIdx], TF_RESOURCE_STATE_SHADER_RESOURCE,
                                                 TF_RESOURCE_STATE_UNORDERED_ACCESS };

                    for (uint32_t i = 0; i < NUM_CULLING_VIEWPORTS; ++i)
                    {
                        barriers[barrierCount++] = { pVisibilityBuffer->ppFilteredIndexBuffer[frameIdx * NUM_CULLING_VIEWPORTS + i],
                                                     TF_RESOURCE_STATE_INDEX_BUFFER | TF_RESOURCE_STATE_SHADER_RESOURCE,
                                                     TF_RESOURCE_STATE_UNORDERED_ACCESS };
                    }
                }

                TFRenderTargetBarrier finalDepthBarrier = { pDepthBuffer, TF_RESOURCE_STATE_SHADER_RESOURCE,
                                                            TF_RESOURCE_STATE_DEPTH_WRITE };
                cmdResourceBarrier(graphicsCmd, barrierCount, barriers, 0, NULL, 1, &finalDepthBarrier);
            }

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
                    cmdBindDescriptorSet(graphicsCmd, 1, pDescriptorSetPersistent);
                    cmdDraw(graphicsCmd, 3, 0);
                    cmdBindRenderTargets(graphicsCmd, NULL);
                }

                cmdEndGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken);
            }
            // UI
            {
                cmdBeginGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken, "UI Pass");

                TFRenderTarget* rt = pSwapChain->ppRenderTargets[presentIndex];
                // Bind Render Targets
                {
                    TFBindRenderTargetsDesc bindRenderTargets = {};
                    bindRenderTargets.mRenderTargetCount = 1;
                    bindRenderTargets.mRenderTargets[0] = { rt, TF_LOAD_ACTION_LOAD };
                    cmdBindRenderTargets(graphicsCmd, &bindRenderTargets);
                    cmdSetViewport(graphicsCmd, 0.0f, 0.0f, (float)rt->mWidth, (float)rt->mHeight, 0.0f, 1.0f);
                    cmdSetScissor(graphicsCmd, 0, 0, rt->mWidth, rt->mHeight);
                }
                // Draw
                {
                    gFrameTimeDraw.mFontColor = gAppSettings.mVisualizeAO ? 0xff000000 : 0xff00ffff;
                    gFrameTimeDraw.mFontSize = 18.0f;
                    gFrameTimeDraw.pFont = gFont;

                    cmdDrawCpuProfile(graphicsCmd, float2(8.0f, 15.0f), &gFrameTimeDraw);

                    if (gAppSettings.mAsyncCompute)
                    {
                        if (!gAppSettings.mHoldFilteredResults)
                        {
                            cmdDrawGpuProfile(graphicsCmd, float2(8.0f, 100.0f), gComputeProfileToken, &gFrameTimeDraw);
                            cmdDrawGpuProfile(graphicsCmd, float2(8.0f, 425.0f), gGraphicsProfileToken, &gFrameTimeDraw);
                        }
                        else
                        {
                            cmdDrawGpuProfile(graphicsCmd, float2(8.0f, 100.0f), gGraphicsProfileToken, &gFrameTimeDraw);
                        }
                    }
                    else
                    {
                        cmdDrawGpuProfile(graphicsCmd, float2(8.0f, 100.0f), gGraphicsProfileToken, &gFrameTimeDraw);
                    }

                    if (useAsyncPixelListBuild)
                    {
                        cmdDrawGpuProfile(graphicsCmd, float2(8.0f, 750.0f), gAsyncPixelListBuildProfileToken, &gFrameTimeDraw);
                    }

                    uiCmdDrawUserInterface(graphicsCmd, pSwapChain, rt, gGraphicsProfileToken);
                }

                cmdBindRenderTargets(graphicsCmd, NULL);
                cmdEndGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken);
            }

            TFRenderTargetBarrier barrierPresent = { pSwapChain->ppRenderTargets[presentIndex], TF_RESOURCE_STATE_RENDER_TARGET,
                                                     TF_RESOURCE_STATE_PRESENT };
            cmdResourceBarrier(graphicsCmd, 0, NULL, 0, NULL, 1, &barrierPresent);

            cmdEndGpuFrameProfile(graphicsCmd, gGraphicsProfileToken);
            endCmd(graphicsCmd);

            // Submit all the work to the GPU and present.
            // Async build only flushes when passes after the split issued updates.
            TFSemaphore* waitSemaphores[3] = {};
            uint32_t     waitSemaphoreCount = 0;
            const bool   flushFinalGraphicsUpdates = !useAsyncPixelListBuild || gAppSettings.mEnableGodray;
            if (flushFinalGraphicsUpdates)
            {
                FlushResourceUpdateDesc flushUpdateDesc = {};
                flushUpdateDesc.mNodeIndex = 0;
                flushResourceUpdates(&flushUpdateDesc);
                waitSemaphores[waitSemaphoreCount++] = flushUpdateDesc.pOutSubmittedSemaphore;
            }
            if (useAsyncPixelListBuild)
            {
                waitSemaphores[waitSemaphoreCount++] = pImageAcquiredSemaphore[frameIdx];
                waitSemaphores[waitSemaphoreCount++] = asyncBuildElem.pSemaphore;
            }
            else
            {
                waitSemaphores[waitSemaphoreCount++] = pImageAcquiredSemaphore[frameIdx];
                if (useDedicatedComputeQueue && gComputeSemaphores[frameIdx])
                    waitSemaphores[waitSemaphoreCount++] = gComputeSemaphores[frameIdx];
            }
            TFSemaphore* signalSemaphores[] = { graphicsElem.pSemaphore, pPresentSemaphore };

            TFQueueSubmitDesc submitDesc = {};
            submitDesc.mCmdCount = 1;
            submitDesc.ppCmds = &graphicsCmd;
            submitDesc.mSignalSemaphoreCount = TF_ARRAY_COUNT(signalSemaphores);
            submitDesc.ppSignalSemaphores = signalSemaphores;
            submitDesc.pSignalFence = graphicsElem.pFence;
            submitDesc.ppWaitSemaphores = waitSemaphores;
            submitDesc.mWaitSemaphoreCount = waitSemaphoreCount;
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

    const char* GetName() { return "Visibility_Buffer"; }

private:
    bool addDescriptorSets()
    {
        // Clear Buffers
        TFDescriptorSetDesc setDesc = SRT_SET_DESC(SrtData, Persistent, 2, 0);
        addDescriptorSet(pRenderer, &setDesc, &pDescriptorSetPersistent);
        setDesc = SRT_SET_DESC(SrtData, PerFrame, gDataBufferCount, 0);
        addDescriptorSet(pRenderer, &setDesc, &pDescriptorSetPerFrame);

        setDesc = SRT_SET_DESC(SrtClusterLightsData, PerBatch, gDataBufferCount, 0);
        addDescriptorSet(pRenderer, &setDesc, &pDescriptorSetClusterLights);

        setDesc = SRT_SET_DESC(TriangleFilteringSrtData, PerBatch, gDataBufferCount, 0);
        addDescriptorSet(pRenderer, &setDesc, &pDescriptorSetTriangleFiltering);

        // God Ray Blur
        setDesc = SRT_SET_DESC(SrtGodrayBlurComp, PerDraw, 4, 0);
        addDescriptorSet(pRenderer, &setDesc, &pDescriptorSetGodRayBlurPassPerDraw);

        // Resolve
        // Currently, we do two resolve passes (godrays for blur and final resolve).
        const uint32_t resolveMaxSets = gDataBufferCount * gResolveTargetCount;
        setDesc = SRT_SET_DESC(SrtResolve, PerDraw, resolveMaxSets, 0);
        addDescriptorSet(pRenderer, &setDesc, &pDescriptorSetResolve);

        // Compute material-sort shade dispatch: one PerDraw instance per (shade pipeline,bucket) slot.
        setDesc = SRT_SET_DESC(ShadeComputeSrtData, PerDraw, gShadeDispatchSlots, 0);
        addDescriptorSet(pRenderer, &setDesc, &pDescriptorSetShadeDispatch);
        setDesc = SRT_SET_DESC(ShadeComputeSrtData, PerBatch, 1, 0);
        addDescriptorSet(pRenderer, &setDesc, &pDescriptorSetShadeOutput);
        return true;
    }

    void removeDescriptorSets()
    {
        removeDescriptorSet(pRenderer, pDescriptorSetShadeOutput);
        removeDescriptorSet(pRenderer, pDescriptorSetShadeDispatch);
        removeDescriptorSet(pRenderer, pDescriptorSetResolve);
        removeDescriptorSet(pRenderer, pDescriptorSetGodRayBlurPassPerDraw);
        removeDescriptorSet(pRenderer, pDescriptorSetTriangleFiltering);
        removeDescriptorSet(pRenderer, pDescriptorSetClusterLights);
        removeDescriptorSet(pRenderer, pDescriptorSetPerFrame);
        removeDescriptorSet(pRenderer, pDescriptorSetPersistent);
    }

    void updateDescriptorSets()
    {
        constexpr uint32_t PERSISTENT_SET_COUNT = 37;
        TFDescriptorData   persistentSetParams[PERSISTENT_SET_COUNT] = {};
        TFTexture*         godrayTextures[] = { pRenderTargetGodRay[0]->pTexture, pRenderTargetGodRay[1]->pTexture };

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
        persistentSetParams[5].mIndex = SRT_RES_IDX(SrtData, Persistent, gShadeQuadInstances);
        persistentSetParams[5].ppBuffers = &pShadeQuadInstancesBuffer;
        persistentSetParams[6].mIndex = SRT_RES_IDX(SrtData, Persistent, gTextureFilter);
        persistentSetParams[6].ppSamplers = &pSamplerPointClamp;
        persistentSetParams[7].mIndex = SRT_RES_IDX(SrtData, Persistent, gDepthSampler);
        persistentSetParams[7].ppSamplers = &pSamplerBilinearClamp;
        persistentSetParams[8].mIndex = SRT_RES_IDX(SrtData, Persistent, gTextureSampler);
        persistentSetParams[8].ppSamplers = &pSamplerTrilinearAniso;
        persistentSetParams[9].mIndex = SRT_RES_IDX(SrtData, Persistent, gLights);
        persistentSetParams[9].ppBuffers = &pLightsBuffer;

        if (gUsingTextureAtlasFallback)
        {
            persistentSetParams[10].mIndex = SRT_RES_IDX(SrtData, Persistent, gAtlasTextures);
            persistentSetParams[10].mCount = (uint32_t)gPackage->mAtlasCount;
            persistentSetParams[10].ppTextures = gPackage->ppAtlasTextures;
        }
        else
        {
            persistentSetParams[10].mIndex = SRT_RES_IDX(SrtData, Persistent, gAllTextures);
            persistentSetParams[10].mCount = (uint32_t)gAllTextureCount;
            persistentSetParams[10].ppTextures = gPackage->ppAllTextures;
        }

        persistentSetParams[11].mIndex = SRT_RES_IDX(SrtData, Persistent, gVBTex);
        persistentSetParams[11].ppTextures = &pRenderTargetVBPass->pTexture;
        persistentSetParams[12].mIndex = SRT_RES_IDX(SrtData, Persistent, gMaterialIDTex);
        persistentSetParams[12].ppTextures = &pRenderTargetMaterialID->pTexture;
        persistentSetParams[13].mIndex = SRT_RES_IDX(SrtData, Persistent, gDepthTex);
        persistentSetParams[13].ppTextures = &pDepthBuffer->pTexture;
        persistentSetParams[14].mIndex = SRT_RES_IDX(SrtData, Persistent, gShadowMap);
        persistentSetParams[14].ppTextures = &pRenderTargetShadow->pTexture;
        persistentSetParams[15].mIndex = SRT_RES_IDX(SrtData, Persistent, gGodRayTexture);
        persistentSetParams[15].ppTextures = &pRenderTargetGodRay[0]->pTexture;
        persistentSetParams[16].mIndex = SRT_RES_IDX(SrtData, Persistent, gBlurWeights);
        persistentSetParams[16].ppBuffers = &pBufferBlurWeights;
        persistentSetParams[17].mIndex = SRT_RES_IDX(SrtData, Persistent, gDisplayTexture);
        persistentSetParams[17].ppTextures = &pRenderTargetVBPass->pTexture;
        persistentSetParams[18].mIndex = SRT_RES_IDX(SrtData, Persistent, gSkyboxTex);
        persistentSetParams[18].ppTextures = &pSkybox;
        persistentSetParams[19].mIndex = SRT_RES_IDX(SrtData, Persistent, gMSAAStencil);
        persistentSetParams[19].ppTextureDescriptors = &pDescriptorVBMSAAStencil;
        persistentSetParams[19].mUseTextureDescriptors = 1;
        persistentSetParams[20].mIndex = SRT_RES_IDX(SrtData, Persistent, gShadeDrawArgs);
        persistentSetParams[20].ppBuffers = &pShadeDrawArgsBuffer;
        persistentSetParams[21].mIndex = SRT_RES_IDX(SrtData, Persistent, gMaterialTileMasks);
        persistentSetParams[21].ppBuffers = &pMaterialTileMasksBuffer;
        persistentSetParams[22].mIndex = SRT_RES_IDX(SrtData, Persistent, gShaderInstanceCounts);
        persistentSetParams[22].ppBuffers = &pShaderInstanceCountsBuffer;
        persistentSetParams[23].mIndex = SRT_RES_IDX(SrtData, Persistent, gShaderInstanceCursors);
        persistentSetParams[23].ppBuffers = &pShaderInstanceCursorsBuffer;

        // Compute material-sort buffers are always bound.
        persistentSetParams[24].mIndex = SRT_RES_IDX(SrtData, Persistent, gPixelTileMaterialCountOffsets);
        persistentSetParams[24].ppBuffers = &pPixelTileMaterialCountOffsetsBuffer;
        persistentSetParams[25].mIndex = SRT_RES_IDX(SrtData, Persistent, gPixelCountPerPipelineTile);
        persistentSetParams[25].ppBuffers = &pPixelCountPerPipelineTileBuffer;
        persistentSetParams[26].mIndex = SRT_RES_IDX(SrtData, Persistent, gPixelOffsetPerPipelineTile);
        persistentSetParams[26].ppBuffers = &pPixelOffsetPerPipelineTileBuffer;
        persistentSetParams[27].mIndex = SRT_RES_IDX(SrtData, Persistent, gPixelPipelineCountOffsets);
        persistentSetParams[27].ppBuffers = &pPixelPipelineCountOffsetsBuffer;
        persistentSetParams[28].mIndex = SRT_RES_IDX(SrtData, Persistent, gPixelCommands);
        persistentSetParams[28].ppBuffers = &pPixelCommandsBuffer;
        persistentSetParams[29].mIndex = SRT_RES_IDX(SrtData, Persistent, gPixelCommandPaddingMask);
        persistentSetParams[29].ppBuffers = &pPixelCommandPaddingMaskBuffer;
        persistentSetParams[30].mIndex = SRT_RES_IDX(SrtData, Persistent, gDispatchIndirectArgs);
        persistentSetParams[30].ppBuffers = &pDispatchIndirectArgsBuffer;
        persistentSetParams[31].mIndex = SRT_RES_IDX(SrtData, Persistent, gDispatchOffsets);
        persistentSetParams[31].ppBuffers = &pDispatchOffsetsBuffer;

        uint32_t persistentSetCount = 32;
        if (gUsingTextureAtlasFallback)
        {
            persistentSetParams[persistentSetCount].mIndex = SRT_RES_IDX(SrtData, Persistent, gAtlasPlacementsBuffer);
            persistentSetParams[persistentSetCount].mCount = 1;
            persistentSetParams[persistentSetCount].ppBuffers = &gPackage->pAtlasPlacementsBuffer;
            ++persistentSetCount;
        }
        updateDescriptorSet(pRenderer, 0, pDescriptorSetPersistent, persistentSetCount, persistentSetParams);

        persistentSetParams[17].ppTextures = &pIntermediateRenderTarget->pTexture;
        updateDescriptorSet(pRenderer, 1, pDescriptorSetPersistent, persistentSetCount, persistentSetParams);

        if (isComputeShadeOutputUavEnabled())
        {
            TFDescriptorData shadeOutputParam = {};
            shadeOutputParam.mIndex = SRT_RES_IDX(ShadeComputeSrtData, PerBatch, gShadeOutput);
            shadeOutputParam.ppTextures = &pIntermediateRenderTarget->pTexture;
            updateDescriptorSet(pRenderer, 0, pDescriptorSetShadeOutput, 1, &shadeOutputParam);
        }

        // Bind each (pipeline,bucket) shade-dispatch cbuffer to its PerDraw set instance.
        for (uint32_t slot = 0; slot < gShadeDispatchSlots; ++slot)
        {
            TFDescriptorData shadeParam = {};
            shadeParam.mIndex = SRT_RES_IDX(ShadeComputeSrtData, PerDraw, gShadeDispatchParams);
            shadeParam.ppBuffers = &pShadeDispatchParamsBuffer[slot];
            updateDescriptorSet(pRenderer, slot, pDescriptorSetShadeDispatch, 1, &shadeParam);
        }

        // per frame set
        {
            for (uint32_t i = 0; i < gDataBufferCount; ++i)
            {
                TFDescriptorData perFrameParams[10] = {};
                perFrameParams[0].mIndex = SRT_RES_IDX(SrtData, PerFrame, gPerFrameVBConstants);
                perFrameParams[0].ppBuffers = &pPerFrameVBUniformBuffers[VB_UB_GRAPHICS][i];
                perFrameParams[1].mIndex = SRT_RES_IDX(SrtData, PerFrame, gPerFrameVBConstantsComp);
                perFrameParams[1].ppBuffers = &pPerFrameVBUniformBuffers[VB_UB_COMPUTE][i];
                perFrameParams[2].mIndex = SRT_RES_IDX(SrtData, PerFrame, gFilterDispatchGroupDataBuffer);
                perFrameParams[2].ppBuffers = &pVisibilityBuffer->ppFilterDispatchGroupDataBuffer[i];
                perFrameParams[3].mIndex = SRT_RES_IDX(SrtData, PerFrame, gVBConstantBuffer);
                perFrameParams[3].ppBuffers = &pVisibilityBuffer->pVBConstantBuffer;
                perFrameParams[4].mIndex = SRT_RES_IDX(SrtData, PerFrame, gIndirectDataBuffer);
                perFrameParams[4].ppBuffers = &pVisibilityBuffer->ppIndirectDataBuffer[i];
                perFrameParams[5].mIndex = SRT_RES_IDX(SrtData, PerFrame, gLightClustersCount);
                perFrameParams[5].ppBuffers = &pLightClustersCount[i];
                perFrameParams[6].mIndex = SRT_RES_IDX(SrtData, PerFrame, gLightClusters);
                perFrameParams[6].ppBuffers = &pLightClusters[i];
                perFrameParams[7].mIndex = SRT_RES_IDX(SrtData, PerFrame, gPerFrameConstants);
                perFrameParams[7].ppBuffers = &pPerFrameUniformBuffers[i];
                perFrameParams[8].mIndex = SRT_RES_IDX(SrtData, PerFrame, gFilteredIndexBuffer);
                perFrameParams[8].ppBuffers = &pVisibilityBuffer->ppFilteredIndexBuffer[i * NUM_CULLING_VIEWPORTS + VIEW_CAMERA];
                perFrameParams[9].mIndex = SRT_RES_IDX(SrtData, PerFrame, gUniformCameraSky);
                perFrameParams[9].ppBuffers = &pUniformBufferSky[i];
                updateDescriptorSet(pRenderer, i, pDescriptorSetPerFrame, 10, perFrameParams);
            }
        }

        // cluster lights
        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            TFDescriptorData clusterLightsParams[2] = {};
            clusterLightsParams[0].mIndex = SRT_RES_IDX(SrtClusterLightsData, PerBatch, gLightClustersCountRW);
            clusterLightsParams[0].ppBuffers = &pLightClustersCount[i];
            clusterLightsParams[1].mIndex = SRT_RES_IDX(SrtClusterLightsData, PerBatch, gLightClustersRW);
            clusterLightsParams[1].ppBuffers = &pLightClusters[i];
            updateDescriptorSet(pRenderer, i, pDescriptorSetClusterLights, 2, clusterLightsParams);
        }

        // triangle filtering
        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            TFDescriptorData triangleFilteringParams[3] = {};
            triangleFilteringParams[0].mIndex = SRT_RES_IDX(TriangleFilteringSrtData, PerBatch, gIndirectDrawClearArgsRW);
            triangleFilteringParams[0].ppBuffers = &pVisibilityBuffer->ppIndirectDrawArgBuffer[i];
            triangleFilteringParams[1].mIndex = SRT_RES_IDX(TriangleFilteringSrtData, PerBatch, gFilteredIndicesBufferRW);
            triangleFilteringParams[1].mCount = gNumViews;
            triangleFilteringParams[1].ppBuffers = &pVisibilityBuffer->ppFilteredIndexBuffer[i * NUM_CULLING_VIEWPORTS];
            triangleFilteringParams[2].mIndex = SRT_RES_IDX(TriangleFilteringSrtData, PerBatch, gIndirectDataBufferRW);
            triangleFilteringParams[2].ppBuffers = &pVisibilityBuffer->ppIndirectDataBuffer[i];
            updateDescriptorSet(pRenderer, i, pDescriptorSetTriangleFiltering, 3, triangleFilteringParams);
        }

        // God Ray Blur
        {
            TFDescriptorData params[2] = {};
            params[0].mIndex = SRT_RES_IDX(SrtGodrayBlurComp, PerDraw, gGodRayTexturesRW);
            params[0].ppTextures = godrayTextures;
            params[0].mCount = 2;
            params[1].mIndex = SRT_RES_IDX(SrtGodrayBlurComp, PerDraw, gBlurParams);
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

        // Resolve
        if (gAppSettings.mMsaaLevel > TF_SAMPLE_COUNT_1)
        {
            TFTexture* resolveSources[] = { pRenderTargetGodRayMS->pTexture, pRenderTargetMSAA->pTexture };
            for (uint32_t frameIdx = 0; frameIdx < gDataBufferCount; frameIdx++)
            {
                for (uint32_t resolvePassIdx = 0; resolvePassIdx < gResolveTargetCount; ++resolvePassIdx)
                {
                    TFDescriptorData params[1] = {};
                    params[0].mIndex = SRT_RES_IDX(SrtResolve, PerDraw, gResolveSource);
                    params[0].ppTextures = &resolveSources[resolvePassIdx];
                    updateDescriptorSet(pRenderer, frameIdx * gResolveTargetCount + resolvePassIdx, pDescriptorSetResolve, 1, params);
                }
            }
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
        swapChainDesc.mFlags = TF_SWAP_CHAIN_CREATION_FLAG_ENABLE_2D_VR_LAYER | TF_SWAP_CHAIN_CREATION_FLAG_ENABLE_FOVEATED_RENDERING_VR;
        swapChainDesc.mVR.m2DLayer = gAppSettings.mVR2DLayer;

        swapChainDesc.mVR.mFoveationLevel = TF_FOVEATION_LEVEL_HIGH;

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
                gAppSettings.mOutputMode = OUTPUT_MODE_SDR;
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

        TFClearValue optimizedColorClearBlack = { { 0.0f, 0.0f, 0.0f, 0.0f } };
        TFClearValue optimizedColorClearWhite = { { 1.0f, 1.0f, 1.0f, 1.0f } };

        // Compute UAV shading requires full-resolution, non-foveated targets.
        const bool             useComputeShadeOutputUav = isComputeShadeOutputUavEnabled();
        TFTextureCreationFlags foveationFlags = (useComputeShadeOutputUav || gAppSettings.mMsaaLevel > TF_SAMPLE_COUNT_1)
                                                    ? TF_TEXTURE_CREATION_FLAG_NONE
                                                    : TF_TEXTURE_CREATION_FLAG_VR_FOVEATED_RENDERING;

        /************************************************************************/
        // Main depth buffer
        /************************************************************************/
        // Add depth buffer
        TF_ESRAM_BEGIN_ALLOC(pRenderer, "Depth Buffer", 0u);
        TFRenderTargetDesc depthRT = {};
        depthRT.mArraySize = 1;
        depthRT.mClearValue = depthStencilClear;
        depthRT.mDepth = 1;
        depthRT.mDescriptors = TF_DESCRIPTOR_TYPE_TEXTURE;
        depthRT.mFormat = TinyImageFormat_D32_SFLOAT;
        depthRT.mStartState = TF_RESOURCE_STATE_DEPTH_WRITE;
        depthRT.mHeight = height;
        depthRT.mSampleCount = gAppSettings.mMsaaLevel;
        depthRT.mSampleQuality = 0;
        depthRT.mFlags = TF_TEXTURE_CREATION_FLAG_VR_MULTIVIEW | foveationFlags |
                         (gAppSettings.mMsaaLevel > TF_SAMPLE_COUNT_2 ? TF_TEXTURE_CREATION_FLAG_NONE : TF_TEXTURE_CREATION_FLAG_ESRAM);
        depthRT.mWidth = width;
        depthRT.pName = "Depth Buffer RT";
        addRenderTarget(pRenderer, &depthRT, &pDepthBuffer);

        TF_ESRAM_CURRENT_OFFSET(pRenderer, depthAllocationOffset);

        TF_ESRAM_END_ALLOC(pRenderer);
        /************************************************************************/
        // Shadow pass render target
        /************************************************************************/
        TFRenderTargetDesc shadowRTDesc = {};
        shadowRTDesc.mArraySize = 1;
        shadowRTDesc.mClearValue = depthStencilClear;
        shadowRTDesc.mDepth = 1;
        shadowRTDesc.mDescriptors = TF_DESCRIPTOR_TYPE_TEXTURE;
        shadowRTDesc.mFormat = TinyImageFormat_D32_SFLOAT;
        shadowRTDesc.mStartState = TF_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        shadowRTDesc.mWidth = gShadowMapSize;
        shadowRTDesc.mSampleCount = TF_SAMPLE_COUNT_1;
        shadowRTDesc.mSampleQuality = 0;
        // shadowRTDesc.mFlags = TF_TEXTURE_CREATION_FLAG_ESRAM;
        shadowRTDesc.mFlags = TF_TEXTURE_CREATION_FLAG_TEXTURE_COMPATABILITY;
        shadowRTDesc.mHeight = gShadowMapSize;
        shadowRTDesc.pName = "Shadow Map RT";
        addRenderTarget(pRenderer, &shadowRTDesc, &pRenderTargetShadow);
        /************************************************************************/
        // Visibility buffer pass render target
        /************************************************************************/
        TF_ESRAM_BEGIN_ALLOC(pRenderer, "VB RT", depthAllocationOffset);
        TFRenderTargetDesc vbRTDesc = {};
        vbRTDesc.mArraySize = 1;
        vbRTDesc.mClearValue = optimizedColorClearWhite;
        vbRTDesc.mDepth = 1;
        vbRTDesc.mDescriptors = TF_DESCRIPTOR_TYPE_TEXTURE;
        vbRTDesc.mFormat = TinyImageFormat_R8G8B8A8_UNORM;
        vbRTDesc.mStartState = TF_RESOURCE_STATE_SHADER_RESOURCE;
        vbRTDesc.mHeight = height;
        vbRTDesc.mSampleCount = gAppSettings.mMsaaLevel;
        vbRTDesc.mSampleQuality = 0;
        vbRTDesc.mFlags = TF_TEXTURE_CREATION_FLAG_ESRAM | TF_TEXTURE_CREATION_FLAG_VR_MULTIVIEW | foveationFlags;
        vbRTDesc.mFlags |= TF_TEXTURE_CREATION_FLAG_DCC;
        vbRTDesc.mWidth = width;
        vbRTDesc.pName = "VB RT";
        addRenderTarget(pRenderer, &vbRTDesc, &pRenderTargetVBPass);

        // Material ID target the build / copy passes read only this 2-byte texel instead of the whole VB texel
        vbRTDesc.mFormat = TinyImageFormat_R16_UNORM;
        vbRTDesc.pName = "Material ID RT";
        addRenderTarget(pRenderer, &vbRTDesc, &pRenderTargetMaterialID);

        TFRenderTargetDesc materialIDDepthRTDesc = depthRT;
        if (gAppSettings.mMsaaLevel > TF_SAMPLE_COUNT_1)
        {
#if defined(TARGET_IOS)
            materialIDDepthRTDesc.mFormat = TinyImageFormat_D32_SFLOAT_S8_UINT;
#else
            const TinyImageFormat candidateMaterialIDDepthStencilFormats[] = { TinyImageFormat_D24_UNORM_S8_UINT,
                                                                               TinyImageFormat_D32_SFLOAT_S8_UINT,
                                                                               TinyImageFormat_D16_UNORM_S8_UINT };
            for (uint32_t i = 0; i < TF_ARRAY_COUNT(candidateMaterialIDDepthStencilFormats); ++i)
            {
                TinyImageFormat candidateFormat = candidateMaterialIDDepthStencilFormats[i];
                TFFormatCapability capMask = TF_FORMAT_CAP_DEPTH_STENCIL | TF_FORMAT_CAP_READ;
                if ((pRenderer->pGpu->mFormatCaps[candidateFormat] & capMask) == capMask)
                {
                    materialIDDepthRTDesc.mFormat = candidateFormat;
                    break;
                }
            }
#endif
        }
        else
        {
            materialIDDepthRTDesc.mFormat = TinyImageFormat_D16_UNORM;
        }
        materialIDDepthRTDesc.mStartState = TF_RESOURCE_STATE_DEPTH_READ;
        materialIDDepthRTDesc.mSampleCount = gAppSettings.mMsaaLevel;
        materialIDDepthRTDesc.mSampleQuality = 0;
        materialIDDepthRTDesc.pName = "VB Material ID Depth RT";
        addRenderTarget(pRenderer, &materialIDDepthRTDesc, &pRenderTargetVBMaterialIDDepth);

        TF_ESRAM_CURRENT_OFFSET(pRenderer, vbPassRTAllocationOffset);

        TF_ESRAM_END_ALLOC(pRenderer);
        /************************************************************************/
        // MSAA render target
        /************************************************************************/
        TFRenderTargetDesc msaaRTDesc = {};
        msaaRTDesc.mArraySize = 1;
        msaaRTDesc.mClearValue = optimizedColorClearBlack;
        msaaRTDesc.mDepth = 1;
        msaaRTDesc.mDescriptors = TF_DESCRIPTOR_TYPE_TEXTURE;
        msaaRTDesc.mFormat = pSwapChain->ppRenderTargets[0]->mFormat;
        msaaRTDesc.mStartState = TF_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        msaaRTDesc.mHeight = height;
        msaaRTDesc.mSampleCount = gAppSettings.mMsaaLevel;
        msaaRTDesc.mSampleQuality = 0;
        msaaRTDesc.mWidth = width;
        msaaRTDesc.pName = "MSAA RT";
        msaaRTDesc.mFlags = TF_TEXTURE_CREATION_FLAG_VR_MULTIVIEW | foveationFlags;
        msaaRTDesc.mFlags |= TF_TEXTURE_CREATION_FLAG_DCC;
        // Disabling compression data will avoid decompression phase before resolve pass.
        // However, the shading pass will require more memory bandwidth.
        // We measured with and without compression and without compression is faster in our case.
#ifndef PROSPERO
        msaaRTDesc.mFlags |= TF_TEXTURE_CREATION_FLAG_NO_COMPRESSION;
#endif
        addRenderTarget(pRenderer, &msaaRTDesc, &pRenderTargetMSAA);

        // Debug MSAA Render Target. It will only be used if drawing debug targets is enabled.
        msaaRTDesc.mSampleCount = TF_SAMPLE_COUNT_1;
        msaaRTDesc.mFlags |= TF_TEXTURE_CREATION_FLAG_DCC | TF_TEXTURE_CREATION_FLAG_TEXTURE_COMPATABILITY;
        msaaRTDesc.mStartState = TF_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        msaaRTDesc.mFormat = gDebugRTFormat;
        msaaRTDesc.pName = "MSAA Debug RT";
        addRenderTarget(pRenderer, &msaaRTDesc, &pRenderTargetDebugMSAA);

        /************************************************************************/
        // Intermediate render target
        /************************************************************************/
        TF_ESRAM_BEGIN_ALLOC(pRenderer, "Intermediate", vbPassRTAllocationOffset);
        TFRenderTargetDesc postProcRTDesc = {};
        postProcRTDesc.mArraySize = 1;
        postProcRTDesc.mClearValue = { { 0.0f, 0.0f, 0.0f, 0.0f } };
        postProcRTDesc.mDepth = 1;
        // Compute UAV shading needs an RW, full-resolution intermediate target.
        if (useComputeShadeOutputUav)
        {
            postProcRTDesc.mDescriptors = TF_DESCRIPTOR_TYPE_TEXTURE | TF_DESCRIPTOR_TYPE_RW_TEXTURE;
            const TinyImageFormat outputFormatCandidates[] = {
                TinyImageFormat_R16G16B16A16_SFLOAT,
                TinyImageFormat_R10G10B10A2_UNORM,
                TinyImageFormat_R8G8B8A8_UNORM,
            };
            const TFFormatCapability requiredOutputCaps =
                TF_FORMAT_CAP_READ | TF_FORMAT_CAP_WRITE | TF_FORMAT_CAP_RENDER_TARGET | TF_FORMAT_CAP_LINEAR_FILTER;
            postProcRTDesc.mFormat = TinyImageFormat_UNDEFINED;
            for (uint32_t i = 0; i < TF_ARRAY_COUNT(outputFormatCandidates); ++i)
            {
                const TinyImageFormat candidateFormat = outputFormatCandidates[i];
                if ((pRenderer->pGpu->mFormatCaps[candidateFormat] & requiredOutputCaps) == requiredOutputCaps)
                {
                    postProcRTDesc.mFormat = candidateFormat;
                    break;
                }
            }
            ASSERT(postProcRTDesc.mFormat != TinyImageFormat_UNDEFINED);
            postProcRTDesc.mFlags =
                TF_TEXTURE_CREATION_FLAG_ESRAM | TF_TEXTURE_CREATION_FLAG_VR_MULTIVIEW | TF_TEXTURE_CREATION_FLAG_NO_COMPRESSION;
        }
        else
        {
            postProcRTDesc.mDescriptors = TF_DESCRIPTOR_TYPE_TEXTURE;
            postProcRTDesc.mFormat = pSwapChain->mFormat;
            postProcRTDesc.mFlags =
                TF_TEXTURE_CREATION_FLAG_ESRAM | TF_TEXTURE_CREATION_FLAG_VR_MULTIVIEW | TF_TEXTURE_CREATION_FLAG_DCC | foveationFlags;
        }
        postProcRTDesc.mStartState = TF_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        postProcRTDesc.mHeight = height;
        postProcRTDesc.mWidth = width;
        postProcRTDesc.mSampleCount = pSwapChain->ppRenderTargets[0]->mSampleCount;
        postProcRTDesc.mSampleQuality = pSwapChain->ppRenderTargets[0]->mSampleQuality;
        postProcRTDesc.pName = "pIntermediateRenderTarget";
        addRenderTarget(pRenderer, &postProcRTDesc, &pIntermediateRenderTarget);

        /************************************************************************/
        // GodRay render target
        /************************************************************************/
        TinyImageFormat GRRTFormat = (pRenderer->pGpu->mFormatCaps[TinyImageFormat_R10G10B10A2_UNORM] & (TF_FORMAT_CAP_READ_WRITE))
                                         ? TinyImageFormat_R10G10B10A2_UNORM
                                         : TinyImageFormat_R8G8B8A8_UNORM;

        TFRenderTargetDesc GRRTDesc = {};
        GRRTDesc.mArraySize = 1;
        GRRTDesc.mClearValue = { { 0.0f, 0.0f, 0.0f, 0.0f } };
        GRRTDesc.mDepth = 1;
        GRRTDesc.mStartState = TF_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        GRRTDesc.mHeight = height / GODRAY_SCALE;
        GRRTDesc.mWidth = width / GODRAY_SCALE;
        GRRTDesc.mFormat = GRRTFormat;
        GRRTDesc.mDescriptors = TF_DESCRIPTOR_TYPE_RW_TEXTURE | TF_DESCRIPTOR_TYPE_TEXTURE;
        GRRTDesc.mSampleCount = TF_SAMPLE_COUNT_1;
        GRRTDesc.mSampleQuality = pSwapChain->ppRenderTargets[0]->mSampleQuality;
        GRRTDesc.mFlags = TF_TEXTURE_CREATION_FLAG_ESRAM | TF_TEXTURE_CREATION_FLAG_VR_MULTIVIEW | foveationFlags;

        GRRTDesc.pName = "GodRay RT A";
        addRenderTarget(pRenderer, &GRRTDesc, &pRenderTargetGodRay[0]);
        GRRTDesc.mStartState = TF_RESOURCE_STATE_UNORDERED_ACCESS;
        GRRTDesc.mSampleCount = TF_SAMPLE_COUNT_1;
        GRRTDesc.pName = "GodRay RT B";
        addRenderTarget(pRenderer, &GRRTDesc, &pRenderTargetGodRay[1]);

        if (gAppSettings.mMsaaLevel > TF_SAMPLE_COUNT_1)
        {
            GRRTDesc.mDescriptors = TF_DESCRIPTOR_TYPE_TEXTURE;
            GRRTDesc.mStartState = TF_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            GRRTDesc.mSampleCount = gAppSettings.mMsaaLevel;
            GRRTDesc.pName = "GodRay RT - MSAA";
            addRenderTarget(pRenderer, &GRRTDesc, &pRenderTargetGodRayMS);

            GRRTDesc.mStartState = TF_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            GRRTDesc.mSampleCount = TF_SAMPLE_COUNT_1;
            GRRTDesc.mFormat = gDebugRTFormat;
            GRRTDesc.pName = "GodRay RT - Debug MSAA View";
            addRenderTarget(pRenderer, &GRRTDesc, &pRenderTargetDebugGodRayMSAA);
        }

        TF_ESRAM_END_ALLOC(pRenderer);
        /************************************************************************/
        /************************************************************************/

        {
            TFRenderTargetDesc msaaDownscaledStencilRT = {};
            msaaDownscaledStencilRT.mArraySize = 1;
            msaaDownscaledStencilRT.mClearValue = depthStencilClear;
            msaaDownscaledStencilRT.mDepth = 1;
            TinyImageFormat       stencilImageFormat = TinyImageFormat_S8_UINT;
            const TinyImageFormat candidateStencilFormats[] = { TinyImageFormat_S8_UINT, TinyImageFormat_D24_UNORM_S8_UINT,
                                                                TinyImageFormat_D16_UNORM_S8_UINT, TinyImageFormat_D32_SFLOAT_S8_UINT };
            for (uint32_t i = 0; i < TF_ARRAY_COUNT(candidateStencilFormats); ++i)
            {
                TinyImageFormat    candidateFormat = candidateStencilFormats[i];
                TFFormatCapability capMask = TF_FORMAT_CAP_DEPTH_STENCIL | TF_FORMAT_CAP_READ;
                if ((pRenderer->pGpu->mFormatCaps[candidateFormat] & capMask) == capMask)
                {
                    stencilImageFormat = candidateFormat;
                    break;
                }
            }
            msaaDownscaledStencilRT.mFormat = stencilImageFormat;
            msaaDownscaledStencilRT.mStartState = TF_RESOURCE_STATE_DEPTH_READ;
            msaaDownscaledStencilRT.mDescriptors = TF_DESCRIPTOR_TYPE_TEXTURE;
            msaaDownscaledStencilRT.mHeight = height / GODRAY_SCALE;
            msaaDownscaledStencilRT.mWidth = width / GODRAY_SCALE;
            msaaDownscaledStencilRT.mSampleCount = gAppSettings.mMsaaLevel;
            msaaDownscaledStencilRT.mSampleQuality = 0;
            msaaDownscaledStencilRT.mFlags = TF_TEXTURE_CREATION_FLAG_VR_MULTIVIEW;
            msaaDownscaledStencilRT.pName = "Downscaled Stencil Buffer MSAA";
            addRenderTarget(pRenderer, &msaaDownscaledStencilRT, &pDownscaledMSAAEdgesStencilBuffer);
        }

        // Stencil view of whichever target gMSAAStencil samples this reload cycle
        {
            TFRenderTarget* pStencilSourceRT =
                (gAppSettings.mMsaaLevel > TF_SAMPLE_COUNT_1) ? pRenderTargetVBMaterialIDDepth : pDownscaledMSAAEdgesStencilBuffer;
            TFTextureDescriptorDesc stencilViewDesc = {};
            stencilViewDesc.pTexture = pStencilSourceRT->pTexture;
            stencilViewDesc.mIsStencil = 1;
            addTextureDescriptor(pRenderer, &stencilViewDesc, &pDescriptorVBMSAAStencil);
        }

        /************************************************************************/
        // Compute pixel-list resources (resolution-dependent)
        /************************************************************************/
        {
            gComputeNumTilesX = (width + TILE_SIZE_PX - 1u) / TILE_SIZE_PX;
            gComputeNumTilesY = (height + TILE_SIZE_PX - 1u) / TILE_SIZE_PX;
            gComputeNumTiles = gComputeNumTilesX * gComputeNumTilesY;

            ASSERT(gShadeComputePipelineCount <= 256u);
            ASSERT(gShadeComputePipelineCount <= (uint32_t)MAX_PIPELINES);
            ASSERT(gMeshCount <= MAX_MATERIAL_COUNT);
            // The compute path bins directly by materialID, capped by MAX_DISPATCH_MATERIALS.
            gComputePixelListSupported = (uint32_t)gMeshCount <= (uint32_t)MAX_DISPATCH_MATERIALS;
            if (!gComputePixelListSupported)
            {
                LOGF(LogLevel::eWARNING,
                     "[VB] Compute pixel-list path disabled: mesh/material count %u exceeds MAX_DISPATCH_MATERIALS (%u); "
                     "falling back to the Grid-Tiles quad path.",
                     (uint32_t)gMeshCount, (uint32_t)MAX_DISPATCH_MATERIALS);
            }
            const uint32_t numPipelines = gShadeComputePipelineCount;
            gComputeDispatchMaterialCount = min((uint32_t)gMeshCount, (uint32_t)MAX_DISPATCH_MATERIALS);
            const uint32_t numTileMaterials = gComputeNumTiles * gComputeDispatchMaterialCount;
            const uint32_t numPipelineTiles = numPipelines * gComputeNumTiles;
            const uint32_t maxDispatchRecords = numPipelines * (uint32_t)MAX_NUM_DISPATCH_BUCKETS * gComputeNumTiles;
            const uint32_t maxRoundedTileGroups =
                (TILE_SIZE_PX * TILE_SIZE_PX + gComputeDispatchMaterialCount * (COMPUTE_SHADE_GROUP_SIZE - 1u) + COMPUTE_SHADE_GROUP_SIZE -
                 1u) /
                COMPUTE_SHADE_GROUP_SIZE;
            ASSERT(maxRoundedTileGroups < (1u << (uint32_t)MAX_NUM_DISPATCH_BUCKETS));
            // Sorted pixel list includes wave-rounded padding for non-empty keys.
            const uint64_t paddingSlots =
                (uint64_t)gComputeNumTiles * gComputeDispatchMaterialCount * ((uint32_t)COMPUTE_SHADE_GROUP_SIZE - 1u);
            const uint64_t sortedPixelCount64 = (uint64_t)width * height + paddingSlots;
            ASSERT(sortedPixelCount64 <= (uint64_t)UINT32_MAX);
            gComputePixelCommandSlotCount = (uint32_t)sortedPixelCount64;
            gComputePixelCommandMaskUintCount = (gComputePixelCommandSlotCount + 31u) / 32u;

            TFBufferLoadDesc pixelTileMaterialCountOffsetsDesc = {};
            pixelTileMaterialCountOffsetsDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_BUFFER | TF_DESCRIPTOR_TYPE_RW_BUFFER;
            pixelTileMaterialCountOffsetsDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_GPU_ONLY;
            pixelTileMaterialCountOffsetsDesc.mDesc.mElementCount = numTileMaterials;
            pixelTileMaterialCountOffsetsDesc.mDesc.mStructStride = sizeof(uint32_t);
            pixelTileMaterialCountOffsetsDesc.mDesc.mSize = (uint64_t)numTileMaterials * sizeof(uint32_t);
            pixelTileMaterialCountOffsetsDesc.mDesc.mStartState = TF_RESOURCE_STATE_UNORDERED_ACCESS;
            pixelTileMaterialCountOffsetsDesc.ppBuffer = &pPixelTileMaterialCountOffsetsBuffer;
            pixelTileMaterialCountOffsetsDesc.mDesc.pName = "Pixel Tile-Material Count/Offset";
            addResource(&pixelTileMaterialCountOffsetsDesc, NULL);

            TFBufferLoadDesc pixelCountPerPipelineTileDesc = {};
            pixelCountPerPipelineTileDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_BUFFER | TF_DESCRIPTOR_TYPE_RW_BUFFER;
            pixelCountPerPipelineTileDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_GPU_ONLY;
            pixelCountPerPipelineTileDesc.mDesc.mElementCount = numPipelineTiles;
            pixelCountPerPipelineTileDesc.mDesc.mStructStride = sizeof(uint32_t);
            pixelCountPerPipelineTileDesc.mDesc.mSize = (uint64_t)numPipelineTiles * sizeof(uint32_t);
            pixelCountPerPipelineTileDesc.mDesc.mStartState = TF_RESOURCE_STATE_UNORDERED_ACCESS;
            pixelCountPerPipelineTileDesc.ppBuffer = &pPixelCountPerPipelineTileBuffer;
            pixelCountPerPipelineTileDesc.mDesc.pName = "Tile Total";
            addResource(&pixelCountPerPipelineTileDesc, NULL);

            TFBufferLoadDesc pixelOffsetPerPipelineTileDesc = {};
            pixelOffsetPerPipelineTileDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_BUFFER | TF_DESCRIPTOR_TYPE_RW_BUFFER;
            pixelOffsetPerPipelineTileDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_GPU_ONLY;
            pixelOffsetPerPipelineTileDesc.mDesc.mElementCount = numPipelineTiles;
            pixelOffsetPerPipelineTileDesc.mDesc.mStructStride = sizeof(uint32_t);
            pixelOffsetPerPipelineTileDesc.mDesc.mSize = (uint64_t)numPipelineTiles * sizeof(uint32_t);
            pixelOffsetPerPipelineTileDesc.mDesc.mStartState = TF_RESOURCE_STATE_UNORDERED_ACCESS;
            pixelOffsetPerPipelineTileDesc.ppBuffer = &pPixelOffsetPerPipelineTileBuffer;
            pixelOffsetPerPipelineTileDesc.mDesc.pName = "Tile Offset Within Pipeline";
            addResource(&pixelOffsetPerPipelineTileDesc, NULL);

            TFBufferLoadDesc pixelPipelineCountOffsetsDesc = {};
            pixelPipelineCountOffsetsDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_BUFFER | TF_DESCRIPTOR_TYPE_RW_BUFFER;
            pixelPipelineCountOffsetsDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_GPU_ONLY;
            pixelPipelineCountOffsetsDesc.mDesc.mElementCount = numPipelines;
            pixelPipelineCountOffsetsDesc.mDesc.mStructStride = sizeof(uint32_t);
            pixelPipelineCountOffsetsDesc.mDesc.mSize = (uint64_t)numPipelines * sizeof(uint32_t);
            pixelPipelineCountOffsetsDesc.mDesc.mStartState = TF_RESOURCE_STATE_UNORDERED_ACCESS;
            pixelPipelineCountOffsetsDesc.ppBuffer = &pPixelPipelineCountOffsetsBuffer;
            pixelPipelineCountOffsetsDesc.mDesc.pName = "Pixel Pipeline Count/Offset";
            addResource(&pixelPipelineCountOffsetsDesc, NULL);

            TFBufferLoadDesc pixelCommandsDesc = {};
            pixelCommandsDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_BUFFER | TF_DESCRIPTOR_TYPE_RW_BUFFER;
            pixelCommandsDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_GPU_ONLY;
            pixelCommandsDesc.mDesc.mElementCount = gComputePixelCommandSlotCount;
            pixelCommandsDesc.mDesc.mStructStride = sizeof(uint32_t);
            pixelCommandsDesc.mDesc.mSize = (uint64_t)gComputePixelCommandSlotCount * sizeof(uint32_t);
            pixelCommandsDesc.mDesc.mStartState = TF_RESOURCE_STATE_UNORDERED_ACCESS;
            pixelCommandsDesc.ppBuffer = &pPixelCommandsBuffer;
            pixelCommandsDesc.mDesc.pName = "Sorted Pixel List";
            addResource(&pixelCommandsDesc, NULL);

            TFBufferLoadDesc pixelCommandPaddingMaskDesc = {};
            pixelCommandPaddingMaskDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_BUFFER | TF_DESCRIPTOR_TYPE_RW_BUFFER;
            pixelCommandPaddingMaskDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_GPU_ONLY;
            pixelCommandPaddingMaskDesc.mDesc.mElementCount = gComputePixelCommandMaskUintCount;
            pixelCommandPaddingMaskDesc.mDesc.mStructStride = sizeof(uint32_t);
            pixelCommandPaddingMaskDesc.mDesc.mSize = (uint64_t)gComputePixelCommandMaskUintCount * sizeof(uint32_t);
            pixelCommandPaddingMaskDesc.mDesc.mStartState = TF_RESOURCE_STATE_UNORDERED_ACCESS;
            pixelCommandPaddingMaskDesc.ppBuffer = &pPixelCommandPaddingMaskBuffer;
            pixelCommandPaddingMaskDesc.mDesc.pName = "Pixel Command Padding Mask";
            addResource(&pixelCommandPaddingMaskDesc, NULL);

            TFBufferLoadDesc dispatchIndirectArgsDesc = {};
            dispatchIndirectArgsDesc.mDesc.mDescriptors =
                TF_DESCRIPTOR_TYPE_BUFFER | TF_DESCRIPTOR_TYPE_RW_BUFFER | TF_DESCRIPTOR_TYPE_INDIRECT_BUFFER;
            dispatchIndirectArgsDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_GPU_ONLY;
            dispatchIndirectArgsDesc.mDesc.mElementCount = gShadeDispatchSlots * 3u;
            dispatchIndirectArgsDesc.mDesc.mStructStride = sizeof(uint32_t);
            dispatchIndirectArgsDesc.mDesc.mSize = (uint64_t)gShadeDispatchSlots * 3u * sizeof(uint32_t);
            dispatchIndirectArgsDesc.mDesc.mStartState = TF_RESOURCE_STATE_UNORDERED_ACCESS;
            dispatchIndirectArgsDesc.ppBuffer = &pDispatchIndirectArgsBuffer;
            dispatchIndirectArgsDesc.mDesc.pName = "Dispatch Indirect Args";
            addResource(&dispatchIndirectArgsDesc, NULL);

            TFBufferLoadDesc dispatchOffsetsDesc = {};
            dispatchOffsetsDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_BUFFER | TF_DESCRIPTOR_TYPE_RW_BUFFER;
            dispatchOffsetsDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_GPU_ONLY;
            dispatchOffsetsDesc.mDesc.mElementCount = maxDispatchRecords;
            dispatchOffsetsDesc.mDesc.mStructStride = sizeof(uint32_t);
            dispatchOffsetsDesc.mDesc.mSize = (uint64_t)maxDispatchRecords * sizeof(uint32_t);
            dispatchOffsetsDesc.mDesc.mStartState = TF_RESOURCE_STATE_UNORDERED_ACCESS;
            dispatchOffsetsDesc.ppBuffer = &pDispatchOffsetsBuffer;
            dispatchOffsetsDesc.mDesc.pName = "Dispatch Record Pixel Offset";
            addResource(&dispatchOffsetsDesc, NULL);

            ShadeDispatchConstantData* shadeDispatchInit =
                (ShadeDispatchConstantData*)tf_calloc(gShadeDispatchSlots, sizeof(ShadeDispatchConstantData));
            for (uint32_t slot = 0; slot < gShadeDispatchSlots; ++slot)
            {
                shadeDispatchInit[slot].pipelineBucketIndex = slot;

                TFBufferLoadDesc cb = {};
                cb.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                cb.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_GPU_ONLY;
                cb.mDesc.mSize = sizeof(ShadeDispatchConstantData);
                cb.pData = &shadeDispatchInit[slot];
                cb.ppBuffer = &pShadeDispatchParamsBuffer[slot];
                cb.mDesc.pName = "Shade Dispatch Params";
                addResource(&cb, NULL);
            }
            waitForAllResourceLoads();
            tf_free(shadeDispatchInit);
        }
    }

    void removeRenderTargets()
    {
        for (uint32_t slot = 0; slot < gShadeDispatchSlots; ++slot)
            removeResource(pShadeDispatchParamsBuffer[slot]);
        removeResource(pPixelTileMaterialCountOffsetsBuffer);
        removeResource(pPixelCountPerPipelineTileBuffer);
        removeResource(pPixelOffsetPerPipelineTileBuffer);
        removeResource(pPixelPipelineCountOffsetsBuffer);
        removeResource(pPixelCommandsBuffer);
        removeResource(pPixelCommandPaddingMaskBuffer);
        removeResource(pDispatchIndirectArgsBuffer);
        removeResource(pDispatchOffsetsBuffer);
        removeTextureDescriptor(pRenderer, pDescriptorVBMSAAStencil);
        removeRenderTarget(pRenderer, pDownscaledMSAAEdgesStencilBuffer);
        if (gAppSettings.mMsaaLevel > TF_SAMPLE_COUNT_1)
        {
            removeRenderTarget(pRenderer, pRenderTargetDebugGodRayMSAA);
            removeRenderTarget(pRenderer, pRenderTargetGodRayMS);
        }
        removeRenderTarget(pRenderer, pRenderTargetGodRay[0]);
        removeRenderTarget(pRenderer, pRenderTargetGodRay[1]);
        removeRenderTarget(pRenderer, pIntermediateRenderTarget);
        removeRenderTarget(pRenderer, pRenderTargetDebugMSAA);
        removeRenderTarget(pRenderer, pRenderTargetMSAA);
        removeRenderTarget(pRenderer, pDepthBuffer);
        removeRenderTarget(pRenderer, pRenderTargetVBMaterialIDDepth);
        removeRenderTarget(pRenderer, pRenderTargetVBPass);
        removeRenderTarget(pRenderer, pRenderTargetMaterialID);
        removeRenderTarget(pRenderer, pRenderTargetShadow);
    }
    /************************************************************************/
    // Load all the shaders needed for the demo
    /************************************************************************/
    void addShaders()
    {
        TFShaderLoadDesc shadowPass = {};
        TFShaderLoadDesc shadowPassAlpha = {};
        TFShaderLoadDesc vbPass = {};
        TFShaderLoadDesc vbPassMultipleMats = {};
        TFShaderLoadDesc vbPassAlpha = {};
        TFShaderLoadDesc vbPassAlphaMultipleMats = {};
        TFShaderLoadDesc vbMaterialIDDepth[MSAA_LEVELS_COUNT] = {};
        TFShaderLoadDesc vbShade[gNumVisBufShaderVariants] = {};
        TFShaderLoadDesc vbShadeSky[2] = {};
        TFShaderLoadDesc resolvePass[MSAA_LEVELS_COUNT] = {};
        TFShaderLoadDesc msaaEdgesShader[MSAA_LEVELS_COUNT - 1] = {};
        TFShaderLoadDesc downscaleMSAAEdgesShader[MSAA_LEVELS_COUNT - 1] = {};
        TFShaderLoadDesc msaaDebugShader[MSAA_LEVELS_COUNT - 1] = {};
        TFShaderLoadDesc clearBuffer = {};
        TFShaderLoadDesc buildMaterialTileMasks[MSAA_LEVELS_COUNT] = {};
        TFShaderLoadDesc shaderInstancePrefixSum = {};
        TFShaderLoadDesc scatterShaderInstances = {};
        TFShaderLoadDesc triangleCulling = {};
        TFShaderLoadDesc clearLights = {};
        TFShaderLoadDesc clusterLights = {};

        shadowPass.mVert.pFileName = "shadow_pass.vert";
        shadowPassAlpha.mVert.pFileName = "shadow_pass_alpha.vert";
        shadowPassAlpha.mFrag.pFileName = "shadow_pass_alpha.frag";

        if (gUsingTextureAtlasFallback)
        {
            shadowPassAlpha.mFrag.pFileName = "shadow_pass_alpha_atlas.frag";
        }

        vbPass.mVert.pFileName = gUsingPrimitiveIDFallback ? "visibilityBuffer_pass_primid.vert" : "visibilityBuffer_pass.vert";
        vbPass.mFrag.pFileName = gUsingPrimitiveIDFallback ? "visibilityBuffer_pass_primid.frag" : "visibilityBuffer_pass.frag";
        vbPassMultipleMats.mVert.pFileName = vbPass.mVert.pFileName;
        vbPassMultipleMats.mFrag.pFileName =
            gUsingPrimitiveIDFallback ? "visibilityBuffer_pass_primid_multiple_mats.frag" : "visibilityBuffer_pass_multiple_mats.frag";
        vbPassAlpha.mVert.pFileName =
            gUsingPrimitiveIDFallback ? "visibilityBuffer_pass_alpha_primid.vert" : "visibilityBuffer_pass_alpha.vert";
        vbPassAlpha.mFrag.pFileName =
            gUsingPrimitiveIDFallback ? "visibilityBuffer_pass_alpha_primid.frag" : "visibilityBuffer_pass_alpha.frag";
        vbPassAlphaMultipleMats.mVert.pFileName = vbPassAlpha.mVert.pFileName;
        vbPassAlphaMultipleMats.mFrag.pFileName = gUsingPrimitiveIDFallback ? "visibilityBuffer_pass_alpha_primid_multiple_mats.frag"
                                                                            : "visibilityBuffer_pass_alpha_multiple_mats.frag";
        const char* vbMaterialIDDepthShaders[] = {
            "visibilityBuffer_materialIDDepth_SAMPLE_1.frag",
            "visibilityBuffer_materialIDDepth_SAMPLE_2.frag",
            "visibilityBuffer_materialIDDepth_SAMPLE_4.frag",
        };
        const char* buildMaterialTileMaskShaders[] = {
            "build_material_tiles_SAMPLE_1.comp",
            "build_material_tiles_SAMPLE_2.comp",
            "build_material_tiles_SAMPLE_4.comp",
        };
        shaderInstancePrefixSum.mComp.pFileName = "shader_instance_prefix_sum.comp";
        scatterShaderInstances.mComp.pFileName = "scatter_shader_instances.comp";
        for (uint32_t i = 0; i < MSAA_LEVELS_COUNT; ++i)
        {
            vbMaterialIDDepth[i].mVert.pFileName = "display.vert";
            vbMaterialIDDepth[i].mFrag.pFileName = vbMaterialIDDepthShaders[i];
            buildMaterialTileMasks[i].mComp.pFileName = buildMaterialTileMaskShaders[i];
        }

        if (gUsingTextureAtlasFallback)
        {
            vbPassAlpha.mFrag.pFileName =
                gUsingPrimitiveIDFallback ? "visibilityBuffer_pass_alpha_atlas_primid.frag" : "visibilityBuffer_pass_alpha_atlas.frag";
            vbPassAlphaMultipleMats.mFrag.pFileName = gUsingPrimitiveIDFallback
                                                          ? "visibilityBuffer_pass_alpha_atlas_primid_multiple_mats.frag"
                                                          : "visibilityBuffer_pass_alpha_atlas_multiple_mats.frag";
        }

        // Some vulkan driver doesn't generate glPrimitiveID without a geometry pass (steam deck as 03/30/2023)
        bool addGeometryPassThrough = gGpuSettings.mAddGeometryPassThrough && !gUsingPrimitiveIDFallback;
        if (addGeometryPassThrough)
        {
            vbPass.mGeom.pFileName = "visibilityBuffer_pass.geom";
            vbPassMultipleMats.mGeom.pFileName = "visibilityBuffer_pass.geom";
            vbPassAlpha.mGeom.pFileName = "visibilityBuffer_pass_alpha.geom";
            vbPassAlphaMultipleMats.mGeom.pFileName = "visibilityBuffer_pass_alpha.geom";
        }

        const char* visibilityBufferShadeShaders[gNumVisBufShaderVariants] = {
            "visibilityBuffer_shade_SAMPLE_1.frag",
            "visibilityBuffer_shade_SAMPLE_1_AO.frag",
            "visibilityBuffer_shade_SAMPLE_2.frag",
            "visibilityBuffer_shade_SAMPLE_2_AO.frag",
            "visibilityBuffer_shade_SAMPLE_4.frag",
            "visibilityBuffer_shade_SAMPLE_4_AO.frag",
            // Godray variants
            "visibilityBuffer_shade_SAMPLE_1_GRAY.frag",
            "visibilityBuffer_shade_SAMPLE_1_AO_GRAY.frag",
            "visibilityBuffer_shade_SAMPLE_2_GRAY.frag",
            "visibilityBuffer_shade_SAMPLE_2_AO_GRAY.frag",
            "visibilityBuffer_shade_SAMPLE_4_GRAY.frag",
            "visibilityBuffer_shade_SAMPLE_4_AO_GRAY.frag",
            // Multiple mats
            "visibilityBuffer_shade_multiple_mats_SAMPLE_1.frag",
            "visibilityBuffer_shade_multiple_mats_SAMPLE_1_AO.frag",
            "visibilityBuffer_shade_multiple_mats_SAMPLE_2.frag",
            "visibilityBuffer_shade_multiple_mats_SAMPLE_2_AO.frag",
            "visibilityBuffer_shade_multiple_mats_SAMPLE_4.frag",
            "visibilityBuffer_shade_multiple_mats_SAMPLE_4_AO.frag",
            // Godray variants
            "visibilityBuffer_shade_multiple_mats_SAMPLE_1_GRAY.frag",
            "visibilityBuffer_shade_multiple_mats_SAMPLE_1_AO_GRAY.frag",
            "visibilityBuffer_shade_multiple_mats_SAMPLE_2_GRAY.frag",
            "visibilityBuffer_shade_multiple_mats_SAMPLE_2_AO_GRAY.frag",
            "visibilityBuffer_shade_multiple_mats_SAMPLE_4_GRAY.frag",
            "visibilityBuffer_shade_multiple_mats_SAMPLE_4_AO_GRAY.frag",
        };

        const char* visibilityBufferShadeAtlasShaders[gNumVisBufShaderVariants] = {
            "visibilityBuffer_shade_atlas_SAMPLE_1.frag",
            "visibilityBuffer_shade_atlas_SAMPLE_1_AO.frag",
            "visibilityBuffer_shade_atlas_SAMPLE_2.frag",
            "visibilityBuffer_shade_atlas_SAMPLE_2_AO.frag",
            "visibilityBuffer_shade_atlas_SAMPLE_4.frag",
            "visibilityBuffer_shade_atlas_SAMPLE_4_AO.frag",
            // Godray variants
            "visibilityBuffer_shade_atlas_SAMPLE_1_GRAY.frag",
            "visibilityBuffer_shade_atlas_SAMPLE_1_AO_GRAY.frag",
            "visibilityBuffer_shade_atlas_SAMPLE_2_GRAY.frag",
            "visibilityBuffer_shade_atlas_SAMPLE_2_AO_GRAY.frag",
            "visibilityBuffer_shade_atlas_SAMPLE_4_GRAY.frag",
            "visibilityBuffer_shade_atlas_SAMPLE_4_AO_GRAY.frag",
            // Multiple mats
            "visibilityBuffer_shade_atlas_multiple_mats_SAMPLE_1.frag",
            "visibilityBuffer_shade_atlas_multiple_mats_SAMPLE_1_AO.frag",
            "visibilityBuffer_shade_atlas_multiple_mats_SAMPLE_2.frag",
            "visibilityBuffer_shade_atlas_multiple_mats_SAMPLE_2_AO.frag",
            "visibilityBuffer_shade_atlas_multiple_mats_SAMPLE_4.frag",
            "visibilityBuffer_shade_atlas_multiple_mats_SAMPLE_4_AO.frag",
            // Godray variants
            "visibilityBuffer_shade_atlas_multiple_mats_SAMPLE_1_GRAY.frag",
            "visibilityBuffer_shade_atlas_multiple_mats_SAMPLE_1_AO_GRAY.frag",
            "visibilityBuffer_shade_atlas_multiple_mats_SAMPLE_2_GRAY.frag",
            "visibilityBuffer_shade_atlas_multiple_mats_SAMPLE_2_AO_GRAY.frag",
            "visibilityBuffer_shade_atlas_multiple_mats_SAMPLE_4_GRAY.frag",
            "visibilityBuffer_shade_atlas_multiple_mats_SAMPLE_4_AO_GRAY.frag",
        };

        const char* resolveShaders[] = {
            "progMSAAResolve_SAMPLE_1.frag",
            "progMSAAResolve_SAMPLE_2.frag",
            "progMSAAResolve_SAMPLE_4.frag",
        };

        for (uint32_t i = 0; i < gNumVisBufShaderVariants; ++i)
        {
            vbShade[i].mVert.pFileName =
                (i >= gNumVisBufShaderVariants / 2) ? "visibilityBuffer_shade_multiple_mats.vert" : "visibilityBuffer_shade.vert";
            vbShade[i].mFrag.pFileName =
                gUsingTextureAtlasFallback ? visibilityBufferShadeAtlasShaders[i] : visibilityBufferShadeShaders[i];
        }
        vbShadeSky[0].mVert.pFileName = "visibilityBuffer_shade_sky.vert";
        vbShadeSky[0].mFrag.pFileName = "visibilityBuffer_shade_sky.frag";
        vbShadeSky[1].mVert.pFileName = "visibilityBuffer_shade_sky.vert";
        vbShadeSky[1].mFrag.pFileName = "visibilityBuffer_shade_sky_godray.frag";

        const char* edgeDetectShaders[] = { "msaa_edge_detect_SAMPLE_2.frag", "msaa_edge_detect_SAMPLE_4.frag" };
        for (uint32_t i = 0; i < MSAA_LEVELS_COUNT - 1; ++i)
        {
            msaaEdgesShader[i].mVert.pFileName = "display.vert";
            msaaEdgesShader[i].mFrag.pFileName = edgeDetectShaders[i];
        }

        const char* edgeDetectDownscaleShaders[] = { "msaa_stencil_downscale_SAMPLE_2.frag", "msaa_stencil_downscale_SAMPLE_4.frag" };
        for (uint32_t i = 0; i < MSAA_LEVELS_COUNT - 1; ++i)
        {
            downscaleMSAAEdgesShader[i].mVert.pFileName = "display.vert";
            downscaleMSAAEdgesShader[i].mFrag.pFileName = edgeDetectDownscaleShaders[i];
        }

        const char* debugMSAAFrag[] = { "msaa_debug_SAMPLE_2.frag", "msaa_debug_SAMPLE_4.frag" };
        for (uint32_t i = 0; i < MSAA_LEVELS_COUNT - 1; ++i)
        {
            msaaDebugShader[i].mVert.pFileName = "display.vert";
            msaaDebugShader[i].mFrag.pFileName = debugMSAAFrag[i];
        }

        for (uint32_t i = 0; i < MSAA_LEVELS_COUNT; ++i)
        {
            // Resolve shader
            resolvePass[i].mVert.pFileName = "progMSAAResolve.vert";
            resolvePass[i].mFrag.pFileName = resolveShaders[i];
        }

        // Triangle culling compute shader
        triangleCulling.mComp.pFileName = "triangle_filtering.comp";
        // Clear buffers compute shader
        clearBuffer.mComp.pFileName = "clear_buffers.comp";
        // Clear light clusters compute shader
        clearLights.mComp.pFileName = "clear_light_clusters.comp";
        // Cluster lights compute shader
        clusterLights.mComp.pFileName = "cluster_lights.comp";

        const char* godrayShaderFileName[] = { "godray_SAMPLE_COUNT_1.frag", "godray_SAMPLE_COUNT_2.frag", "godray_SAMPLE_COUNT_4.frag" };
        for (uint32_t i = 0; i < MSAA_LEVELS_COUNT; ++i)
        {
            TFShaderLoadDesc godrayShaderDesc = {};
            godrayShaderDesc.mVert.pFileName = "display.vert";
            godrayShaderDesc.mFrag.pFileName = godrayShaderFileName[i];
            addShader(pRenderer, &godrayShaderDesc, &pGodRayPass[i]);
        }

        TFShaderLoadDesc godrayBlurShaderDesc = {};
        godrayBlurShaderDesc.mComp.pFileName = "godray_blur.comp";
        addShader(pRenderer, &godrayBlurShaderDesc, &pShaderGodRayBlurPass);

        TFShaderLoadDesc presentShaderDesc = {};
        presentShaderDesc.mVert.pFileName = "display.vert";
        presentShaderDesc.mFrag.pFileName = "display.frag";

        addShader(pRenderer, &presentShaderDesc, &pShaderPresentPass);
        addShader(pRenderer, &shadowPass, &pShaderShadowPass[GEOMSET_OPAQUE]);
        addShader(pRenderer, &shadowPassAlpha, &pShaderShadowPass[GEOMSET_ALPHA_CUTOUT]);
        addShader(pRenderer, &vbPass, &pShaderVisibilityBufferPass[GEOMSET_OPAQUE]);
        addShader(pRenderer, &vbPassMultipleMats, &pShaderVisibilityBufferPass[2 + GEOMSET_OPAQUE]);
        addShader(pRenderer, &vbPassAlpha, &pShaderVisibilityBufferPass[GEOMSET_ALPHA_CUTOUT]);
        addShader(pRenderer, &vbPassAlphaMultipleMats, &pShaderVisibilityBufferPass[2 + GEOMSET_ALPHA_CUTOUT]);
        for (uint32_t i = 0; i < MSAA_LEVELS_COUNT; ++i)
        {
            addShader(pRenderer, &vbMaterialIDDepth[i], &pShaderVisibilityBufferMaterialIDDepth[i]);
            addShader(pRenderer, &buildMaterialTileMasks[i], &pShaderBuildMaterialTileMasks[i]);
        }
        addShader(pRenderer, &shaderInstancePrefixSum, &pShaderShaderInstancePrefixSum);
        addShader(pRenderer, &scatterShaderInstances, &pShaderScatterShaderInstances);

        // Compute pixel-list shaders.
        {
            struct
            {
                const char* file;
                TFShader**  out;
            } computeShaders[] = {
                { "clear_pixel_counts.comp", &pShaderClearPixelCounts },
                { "count_pixels.comp", &pShaderCountPixels },
                { "pixel_command_prefix_sum_tile_materials.comp", &pShaderPrefixSumTileMaterials },
                { "pixel_command_prefix_sum_tiles.comp", &pShaderPrefixSumTiles },
                { "pixel_command_prefix_sum_pipelines.comp", &pShaderPrefixSumPipelines },
                { "finalize_tile_material_offsets.comp", &pShaderFinalizeTileMaterialOffsets },
                { "write_pixel_commands.comp", &pShaderWritePixelCommands },
                { "generate_dispatch_args.comp", &pShaderGenerateDispatchArgs },
                { "visibilityBuffer_shade_sky_compute.comp", &pShaderShadeComputeSky },
            };
            for (uint32_t i = 0; i < TF_ARRAY_COUNT(computeShaders); ++i)
            {
                TFShaderLoadDesc desc = {};
                desc.mComp.pFileName = computeShaders[i].file;
                addShader(pRenderer, &desc, computeShaders[i].out);
            }
            const char* shadeComputeShaderFileNames[gShadeComputePipelineCount] = {
                "visibilityBuffer_shade_compute_mat_00.comp", "visibilityBuffer_shade_compute_mat_01.comp",
                "visibilityBuffer_shade_compute_mat_02.comp", "visibilityBuffer_shade_compute_mat_03.comp",
                "visibilityBuffer_shade_compute_mat_04.comp", "visibilityBuffer_shade_compute_mat_05.comp",
                "visibilityBuffer_shade_compute_mat_06.comp", "visibilityBuffer_shade_compute_mat_07.comp",
                "visibilityBuffer_shade_compute_mat_08.comp", "visibilityBuffer_shade_compute_mat_09.comp",
                "visibilityBuffer_shade_compute_mat_10.comp", "visibilityBuffer_shade_compute_mat_11.comp",
                "visibilityBuffer_shade_compute_mat_12.comp", "visibilityBuffer_shade_compute_mat_13.comp",
                "visibilityBuffer_shade_compute_mat_14.comp", "visibilityBuffer_shade_compute_mat_15.comp",
                "visibilityBuffer_shade_compute_mat_16.comp", "visibilityBuffer_shade_compute_mat_17.comp",
                "visibilityBuffer_shade_compute_mat_18.comp", "visibilityBuffer_shade_compute_mat_19.comp",
                "visibilityBuffer_shade_compute_mat_20.comp", "visibilityBuffer_shade_compute_mat_21.comp",
                "visibilityBuffer_shade_compute_mat_22.comp", "visibilityBuffer_shade_compute_mat_23.comp",
                "visibilityBuffer_shade_compute_mat_24.comp", "visibilityBuffer_shade_compute_mat_25.comp",
                "visibilityBuffer_shade_compute_mat_26.comp", "visibilityBuffer_shade_compute_mat_27.comp",
                "visibilityBuffer_shade_compute_mat_28.comp", "visibilityBuffer_shade_compute_mat_29.comp",
                "visibilityBuffer_shade_compute_mat_30.comp", "visibilityBuffer_shade_compute_mat_31.comp",
            };
            // Atlas fallback variants for devices below the bindless texture limit
            const char* shadeComputeAtlasShaderFileNames[gShadeComputePipelineCount] = {
                "visibilityBuffer_shade_compute_atlas_mat_00.comp", "visibilityBuffer_shade_compute_atlas_mat_01.comp",
                "visibilityBuffer_shade_compute_atlas_mat_02.comp", "visibilityBuffer_shade_compute_atlas_mat_03.comp",
                "visibilityBuffer_shade_compute_atlas_mat_04.comp", "visibilityBuffer_shade_compute_atlas_mat_05.comp",
                "visibilityBuffer_shade_compute_atlas_mat_06.comp", "visibilityBuffer_shade_compute_atlas_mat_07.comp",
                "visibilityBuffer_shade_compute_atlas_mat_08.comp", "visibilityBuffer_shade_compute_atlas_mat_09.comp",
                "visibilityBuffer_shade_compute_atlas_mat_10.comp", "visibilityBuffer_shade_compute_atlas_mat_11.comp",
                "visibilityBuffer_shade_compute_atlas_mat_12.comp", "visibilityBuffer_shade_compute_atlas_mat_13.comp",
                "visibilityBuffer_shade_compute_atlas_mat_14.comp", "visibilityBuffer_shade_compute_atlas_mat_15.comp",
                "visibilityBuffer_shade_compute_atlas_mat_16.comp", "visibilityBuffer_shade_compute_atlas_mat_17.comp",
                "visibilityBuffer_shade_compute_atlas_mat_18.comp", "visibilityBuffer_shade_compute_atlas_mat_19.comp",
                "visibilityBuffer_shade_compute_atlas_mat_20.comp", "visibilityBuffer_shade_compute_atlas_mat_21.comp",
                "visibilityBuffer_shade_compute_atlas_mat_22.comp", "visibilityBuffer_shade_compute_atlas_mat_23.comp",
                "visibilityBuffer_shade_compute_atlas_mat_24.comp", "visibilityBuffer_shade_compute_atlas_mat_25.comp",
                "visibilityBuffer_shade_compute_atlas_mat_26.comp", "visibilityBuffer_shade_compute_atlas_mat_27.comp",
                "visibilityBuffer_shade_compute_atlas_mat_28.comp", "visibilityBuffer_shade_compute_atlas_mat_29.comp",
                "visibilityBuffer_shade_compute_atlas_mat_30.comp", "visibilityBuffer_shade_compute_atlas_mat_31.comp",
            };
            // One shade shader per material.
            for (uint32_t i = 0; i < gShadeComputePipelineCount; ++i)
            {
                TFShaderLoadDesc desc = {};
                // Same runtime selection as the raster shade shaders: atlas devices bind gAtlasTextures only.
                desc.mComp.pFileName = gUsingTextureAtlasFallback ? shadeComputeAtlasShaderFileNames[i] : shadeComputeShaderFileNames[i];
                addShader(pRenderer, &desc, &pShaderShadeCompute[i]);
            }
        }
        for (uint32_t i = 0; i < gNumVisBufShaderVariants; ++i)
            addShader(pRenderer, &vbShade[i], &pShaderVisibilityBufferShade[i]);
        for (uint32_t i = 0; i < TF_ARRAY_COUNT(vbShadeSky); ++i)
            addShader(pRenderer, &vbShadeSky[i], &pShaderVisibilityBufferShadeSky[i]);
        addShader(pRenderer, &clearBuffer, &pShaderClearBuffers);
        addShader(pRenderer, &triangleCulling, &pShaderTriangleFiltering);
        addShader(pRenderer, &clearLights, &pShaderClearLightClusters);
        addShader(pRenderer, &clusterLights, &pShaderClusterLights);
        for (uint32_t i = 0; i < MSAA_LEVELS_COUNT; ++i)
        {
            addShader(pRenderer, &resolvePass[i], &pShaderResolve[i]);
        }
        for (uint32_t i = 0; i < MSAA_LEVELS_COUNT - 1; ++i)
        {
            addShader(pRenderer, &msaaEdgesShader[i], &pShaderDrawMSAAEdges[i]);
            addShader(pRenderer, &downscaleMSAAEdgesShader[i], &pShaderDownscaleMSAAEdges[i]);
            addShader(pRenderer, &msaaDebugShader[i], &pShaderDebugMSAA[i]);
        }
    }

    void removeShaders()
    {
        removeShader(pRenderer, pShaderShadowPass[GEOMSET_OPAQUE]);
        removeShader(pRenderer, pShaderShadowPass[GEOMSET_ALPHA_CUTOUT]);
        removeShader(pRenderer, pShaderVisibilityBufferPass[GEOMSET_OPAQUE]);
        removeShader(pRenderer, pShaderVisibilityBufferPass[2 + GEOMSET_OPAQUE]);
        removeShader(pRenderer, pShaderVisibilityBufferPass[GEOMSET_ALPHA_CUTOUT]);
        removeShader(pRenderer, pShaderVisibilityBufferPass[2 + GEOMSET_ALPHA_CUTOUT]);
        for (uint32_t i = 0; i < MSAA_LEVELS_COUNT; ++i)
        {
            removeShader(pRenderer, pShaderVisibilityBufferMaterialIDDepth[i]);
            removeShader(pRenderer, pShaderBuildMaterialTileMasks[i]);
        }
        removeShader(pRenderer, pShaderShaderInstancePrefixSum);
        removeShader(pRenderer, pShaderScatterShaderInstances);
        // Compute pixel-list shaders
        removeShader(pRenderer, pShaderClearPixelCounts);
        removeShader(pRenderer, pShaderCountPixels);
        removeShader(pRenderer, pShaderPrefixSumTileMaterials);
        removeShader(pRenderer, pShaderPrefixSumTiles);
        removeShader(pRenderer, pShaderPrefixSumPipelines);
        removeShader(pRenderer, pShaderFinalizeTileMaterialOffsets);
        removeShader(pRenderer, pShaderWritePixelCommands);
        removeShader(pRenderer, pShaderGenerateDispatchArgs);
        removeShader(pRenderer, pShaderShadeComputeSky);
        for (uint32_t i = 0; i < gShadeComputePipelineCount; ++i)
            removeShader(pRenderer, pShaderShadeCompute[i]);
        for (uint32_t i = 0; i < gNumVisBufShaderVariants; ++i)
            removeShader(pRenderer, pShaderVisibilityBufferShade[i]);
        for (uint32_t i = 0; i < TF_ARRAY_COUNT(pShaderVisibilityBufferShadeSky); ++i)
            removeShader(pRenderer, pShaderVisibilityBufferShadeSky[i]);
        removeShader(pRenderer, pShaderTriangleFiltering);
        removeShader(pRenderer, pShaderClearBuffers);
        removeShader(pRenderer, pShaderClusterLights);
        removeShader(pRenderer, pShaderClearLightClusters);
        for (uint32_t i = 0; i < MSAA_LEVELS_COUNT - 1; ++i)
        {
            removeShader(pRenderer, pShaderDebugMSAA[i]);
            removeShader(pRenderer, pShaderDownscaleMSAAEdges[i]);
            removeShader(pRenderer, pShaderDrawMSAAEdges[i]);
        }
        for (uint32_t i = 0; i < MSAA_LEVELS_COUNT; ++i)
        {
            removeShader(pRenderer, pShaderResolve[i]);
        }
        for (uint32_t i = 0; i < MSAA_LEVELS_COUNT; ++i)
            removeShader(pRenderer, pGodRayPass[i]);
        removeShader(pRenderer, pShaderGodRayBlurPass);

        removeShader(pRenderer, pShaderPresentPass);
    }

    void addPipelines()
    {
        /************************************************************************/
        // Setup compute pipelines for triangle filtering
        /************************************************************************/
        TFPipelineDesc pipelineDesc = {};
        PIPELINE_LAYOUT_DESC(pipelineDesc, SRT_LAYOUT_DESC(TriangleFilteringSrtData, Persistent),
                             SRT_LAYOUT_DESC(TriangleFilteringSrtData, PerFrame), SRT_LAYOUT_DESC(TriangleFilteringSrtData, PerBatch),
                             NULL);
        pipelineDesc.pCache = pPipelineCache;
        pipelineDesc.mType = TF_PIPELINE_TYPE_COMPUTE;
        TFComputePipelineDesc& compPipelineSettings = pipelineDesc.mComputeDesc;
        compPipelineSettings.pShaderProgram = pShaderClearBuffers;
        pipelineDesc.pName = "Clear Filtering Buffers";
        addPipeline(pRenderer, &pipelineDesc, &pPipelineClearBuffers);

        PIPELINE_LAYOUT_DESC(pipelineDesc, SRT_LAYOUT_DESC(SrtData, Persistent), SRT_LAYOUT_DESC(SrtData, PerFrame), NULL, NULL);
        pipelineDesc.pCache = pPipelineCache;
        pipelineDesc.mType = TF_PIPELINE_TYPE_COMPUTE;
        compPipelineSettings.pShaderProgram = pShaderBuildMaterialTileMasks[gAppSettings.mMsaaIndex];
        pipelineDesc.pName = "Build Material Tile Masks";
        addPipeline(pRenderer, &pipelineDesc, &pPipelineBuildMaterialTileMasks);

        compPipelineSettings.pShaderProgram = pShaderShaderInstancePrefixSum;
        pipelineDesc.pName = "Shader Instance Prefix Sum";
        addPipeline(pRenderer, &pipelineDesc, &pPipelineShaderInstancePrefixSum);

        compPipelineSettings.pShaderProgram = pShaderScatterShaderInstances;
        pipelineDesc.pName = "Scatter Shader Instances";
        addPipeline(pRenderer, &pipelineDesc, &pPipelineScatterShaderInstances);

        // Compute pixel-list pipelines.
        {
            struct
            {
                TFShader*    shader;
                TFPipeline** out;
                const char*  name;
            } computePipelines[] = {
                { pShaderClearPixelCounts, &pPipelineClearPixelCounts, "Clear Pixel Counts" },
                { pShaderCountPixels, &pPipelineCountPixels, "Count Pixels" },
                { pShaderPrefixSumTileMaterials, &pPipelinePrefixSumTileMaterials, "Pixel Cmd Prefix Sum (Tile Materials)" },
                { pShaderPrefixSumTiles, &pPipelinePrefixSumTiles, "Pixel Cmd Prefix Sum (Tiles)" },
                { pShaderPrefixSumPipelines, &pPipelinePrefixSumPipelines, "Pixel Cmd Prefix Sum (Pipelines)" },
                { pShaderFinalizeTileMaterialOffsets, &pPipelineFinalizeTileMaterialOffsets, "Finalize Tile Material Offsets" },
                { pShaderWritePixelCommands, &pPipelineWritePixelCommands, "Write Pixel Commands" },
                { pShaderGenerateDispatchArgs, &pPipelineGenerateDispatchArgs, "Generate Dispatch Args" },
            };
            for (uint32_t i = 0; i < TF_ARRAY_COUNT(computePipelines); ++i)
            {
                compPipelineSettings.pShaderProgram = computePipelines[i].shader;
                pipelineDesc.pName = computePipelines[i].name;
                addPipeline(pRenderer, &pipelineDesc, computePipelines[i].out);
            }

            PIPELINE_LAYOUT_DESC(pipelineDesc, SRT_LAYOUT_DESC(ShadeComputeSrtData, Persistent),
                                 SRT_LAYOUT_DESC(ShadeComputeSrtData, PerFrame), SRT_LAYOUT_DESC(ShadeComputeSrtData, PerBatch), NULL);
            pipelineDesc.pCache = pPipelineCache;
            pipelineDesc.mType = TF_PIPELINE_TYPE_COMPUTE;
            compPipelineSettings.pShaderProgram = pShaderShadeComputeSky;
            pipelineDesc.pName = "VB Shade Sky (Compute)";
            addPipeline(pRenderer, &pipelineDesc, &pPipelineShadeComputeSky);

#if defined(ANDROID)
            // Android shade pipelines omit the PerDraw cbuffer.
            PIPELINE_LAYOUT_DESC(pipelineDesc, SRT_LAYOUT_DESC(ShadeComputeSrtData, Persistent),
                                 SRT_LAYOUT_DESC(ShadeComputeSrtData, PerFrame), SRT_LAYOUT_DESC(ShadeComputeSrtData, PerBatch), NULL);
#else
            // Desktop shade pipelines use a PerDraw dispatch-params cbuffer.
            PIPELINE_LAYOUT_DESC(pipelineDesc, SRT_LAYOUT_DESC(ShadeComputeSrtData, Persistent),
                                 SRT_LAYOUT_DESC(ShadeComputeSrtData, PerFrame), SRT_LAYOUT_DESC(ShadeComputeSrtData, PerBatch),
                                 SRT_LAYOUT_DESC(ShadeComputeSrtData, PerDraw));
#endif
            pipelineDesc.pCache = pPipelineCache;
            pipelineDesc.mType = TF_PIPELINE_TYPE_COMPUTE;
            for (uint32_t i = 0; i < gShadeComputePipelineCount; ++i)
            {
                compPipelineSettings.pShaderProgram = pShaderShadeCompute[i];
                pipelineDesc.pName = "VB Shade (Compute)";
                addPipeline(pRenderer, &pipelineDesc, &pPipelineShadeCompute[i]);
            }

            // Restore the SrtData compute layout for any subsequent compute pipelines below.
            PIPELINE_LAYOUT_DESC(pipelineDesc, SRT_LAYOUT_DESC(SrtData, Persistent), SRT_LAYOUT_DESC(SrtData, PerFrame), NULL, NULL);
            pipelineDesc.pCache = pPipelineCache;
            pipelineDesc.mType = TF_PIPELINE_TYPE_COMPUTE;
        }

        // Create the compute pipeline for GPU triangle filtering
        PIPELINE_LAYOUT_DESC(pipelineDesc, SRT_LAYOUT_DESC(TriangleFilteringSrtData, Persistent),
                             SRT_LAYOUT_DESC(TriangleFilteringSrtData, PerFrame), SRT_LAYOUT_DESC(TriangleFilteringSrtData, PerBatch),
                             NULL);
        pipelineDesc.pCache = pPipelineCache;
        pipelineDesc.mType = TF_PIPELINE_TYPE_COMPUTE;
        pipelineDesc.pName = "Triangle Filtering";
        compPipelineSettings.pShaderProgram = pShaderTriangleFiltering;
        addPipeline(pRenderer, &pipelineDesc, &pPipelineTriangleFiltering);
        PIPELINE_LAYOUT_DESC(pipelineDesc, SRT_LAYOUT_DESC(SrtClusterLightsData, Persistent),
                             SRT_LAYOUT_DESC(SrtClusterLightsData, PerFrame), SRT_LAYOUT_DESC(SrtClusterLightsData, PerBatch), NULL);
        // Setup the clearing light clusters pipeline
        pipelineDesc.pName = "Clear Light Clusters";
        compPipelineSettings.pShaderProgram = pShaderClearLightClusters;
        addPipeline(pRenderer, &pipelineDesc, &pPipelineClearLightClusters);

        // Setup the compute the light clusters pipeline
        pipelineDesc.pName = "Cluster Lights";
        compPipelineSettings.pShaderProgram = pShaderClusterLights;
        addPipeline(pRenderer, &pipelineDesc, &pPipelineClusterLights);

        // God Ray Blur Pass
        pipelineDesc.pName = "God Ray Blur";
        PIPELINE_LAYOUT_DESC(pipelineDesc, SRT_LAYOUT_DESC(SrtGodrayBlurComp, Persistent), SRT_LAYOUT_DESC(SrtGodrayBlurComp, PerFrame),
                             NULL, SRT_LAYOUT_DESC(SrtGodrayBlurComp, PerDraw));
        compPipelineSettings.pShaderProgram = pShaderGodRayBlurPass;
        addPipeline(pRenderer, &pipelineDesc, &pPipelineGodRayBlurPass);

        /************************************************************************/
        /************************************************************************/
        TFDepthStateDesc depthStateDesc = {};
        depthStateDesc.mDepthTest = true;
        depthStateDesc.mDepthWrite = true;
        depthStateDesc.mDepthFunc = TF_CMP_GEQUAL;
        TFDepthStateDesc depthStateDisableDesc = {};
        TFDepthStateDesc depthStateReadDesc = {};
        depthStateReadDesc.mDepthTest = true;
        depthStateReadDesc.mDepthWrite = false;
        depthStateReadDesc.mDepthFunc = TF_CMP_EQUAL;
        TFDepthStateDesc depthStateWriteOnlyDesc = {};
        depthStateWriteOnlyDesc.mDepthTest = true;
        depthStateWriteOnlyDesc.mDepthWrite = true;
        depthStateWriteOnlyDesc.mDepthFunc = TF_CMP_ALWAYS;

        TFDepthStateDesc depthStateOnlyReadStencilDesc = {};
        depthStateOnlyReadStencilDesc.mStencilWriteMask = 0x00;
        depthStateOnlyReadStencilDesc.mStencilReadMask = 0xFF;
        depthStateOnlyReadStencilDesc.mStencilTest = true;
        depthStateOnlyReadStencilDesc.mStencilFrontFunc = TF_CMP_EQUAL;
        depthStateOnlyReadStencilDesc.mStencilFrontFail = TF_STENCIL_OP_KEEP;
        depthStateOnlyReadStencilDesc.mStencilFrontPass = TF_STENCIL_OP_KEEP;
        depthStateOnlyReadStencilDesc.mDepthFrontFail = TF_STENCIL_OP_KEEP;
        depthStateOnlyReadStencilDesc.mStencilBackFunc = TF_CMP_EQUAL;
        depthStateOnlyReadStencilDesc.mStencilBackFail = TF_STENCIL_OP_KEEP;
        depthStateOnlyReadStencilDesc.mStencilBackPass = TF_STENCIL_OP_KEEP;
        depthStateOnlyReadStencilDesc.mDepthBackFail = TF_STENCIL_OP_KEEP;

        TFDepthStateDesc depthStateReadStencilDesc = depthStateOnlyReadStencilDesc;
        depthStateReadStencilDesc.mDepthTest = true;
        depthStateReadStencilDesc.mDepthFunc = TF_CMP_EQUAL;

        TFDepthStateDesc depthStateOnlyWriteStencilDesc = {};
        depthStateOnlyWriteStencilDesc.mStencilWriteMask = 0xFF;
        depthStateOnlyWriteStencilDesc.mStencilReadMask = 0xFF;
        depthStateOnlyWriteStencilDesc.mStencilTest = true;
        depthStateOnlyWriteStencilDesc.mStencilFrontFunc = TF_CMP_GEQUAL;
        depthStateOnlyWriteStencilDesc.mStencilFrontFail = TF_STENCIL_OP_KEEP;
        depthStateOnlyWriteStencilDesc.mStencilFrontPass = TF_STENCIL_OP_REPLACE;
        depthStateOnlyWriteStencilDesc.mDepthFrontFail = TF_STENCIL_OP_KEEP;
        depthStateOnlyWriteStencilDesc.mStencilBackFunc = TF_CMP_GEQUAL;
        depthStateOnlyWriteStencilDesc.mStencilBackFail = TF_STENCIL_OP_KEEP;
        depthStateOnlyWriteStencilDesc.mStencilBackPass = TF_STENCIL_OP_REPLACE;
        depthStateOnlyWriteStencilDesc.mDepthBackFail = TF_STENCIL_OP_KEEP;

        TFRasterizerStateDesc rasterizerStateCullNoneDesc = { TF_CULL_MODE_NONE };
        TFRasterizerStateDesc rasterizerStateCullNoneMsDesc = { TF_CULL_MODE_NONE, 0, 0, TF_FILL_MODE_SOLID };
        rasterizerStateCullNoneMsDesc.mMultiSample = true;

        const bool isMSAAEnabled = gAppSettings.mMsaaLevel > TF_SAMPLE_COUNT_1;
        // Match pipeline foveation with render-target foveation.
        const bool isFoveationEnabled = !isMSAAEnabled && !isComputeShadeOutputUavEnabled();

        /************************************************************************/
        // Setup the Shadow Pass Pipeline
        /************************************************************************/
        // Setup pipeline settings
        pipelineDesc = {};
        PIPELINE_LAYOUT_DESC(pipelineDesc, SRT_LAYOUT_DESC(SrtData, Persistent), SRT_LAYOUT_DESC(SrtData, PerFrame), NULL, NULL);
        pipelineDesc.pCache = pPipelineCache;
        pipelineDesc.mType = TF_PIPELINE_TYPE_GRAPHICS;
        TFGraphicsPipelineDesc& shadowPipelineSettings = pipelineDesc.mGraphicsDesc;
        shadowPipelineSettings = { 0 };
        shadowPipelineSettings.mPrimitiveTopo = TF_PRIMITIVE_TOPO_TRI_LIST;
        shadowPipelineSettings.pDepthState = &depthStateDesc;
        shadowPipelineSettings.mDepthStencilFormat = pRenderTargetShadow->mFormat;
        shadowPipelineSettings.mSampleCount = pRenderTargetShadow->mSampleCount;
        shadowPipelineSettings.mSampleQuality = pRenderTargetShadow->mSampleQuality;
        shadowPipelineSettings.pVertexLayout = NULL;
        shadowPipelineSettings.pRasterizerState =
            gAppSettings.mMsaaLevel > TF_SAMPLE_COUNT_1 ? &rasterizerStateCullNoneMsDesc : &rasterizerStateCullNoneDesc;
        shadowPipelineSettings.pShaderProgram = pShaderShadowPass[0];
        pipelineDesc.pName = "Shadow Opaque";
        addPipeline(pRenderer, &pipelineDesc, &pPipelineShadowPass[0]);

        shadowPipelineSettings.pShaderProgram = pShaderShadowPass[1];
        pipelineDesc.pName = "Shadow AlphaTested";
        addPipeline(pRenderer, &pipelineDesc, &pPipelineShadowPass[1]);

        /************************************************************************/
        // Setup the Visibility Buffer Pass Pipeline
        /************************************************************************/
        // Setup pipeline settings
        TFGraphicsPipelineDesc& vbPassPipelineSettings = pipelineDesc.mGraphicsDesc;
        vbPassPipelineSettings = { 0 };
        vbPassPipelineSettings.mPrimitiveTopo = TF_PRIMITIVE_TOPO_TRI_LIST;
        TinyImageFormat vbPassFormats[] = { pRenderTargetVBPass->mFormat };
        TinyImageFormat vbPassMultipleMatsFormats[] = { pRenderTargetVBPass->mFormat, pRenderTargetMaterialID->mFormat };
        vbPassPipelineSettings.mRenderTargetCount = TF_ARRAY_COUNT(vbPassFormats);
        vbPassPipelineSettings.pDepthState = &depthStateDesc;
        vbPassPipelineSettings.pColorFormats = vbPassFormats;
        vbPassPipelineSettings.mSampleCount = pRenderTargetVBPass->mSampleCount;
        vbPassPipelineSettings.mSampleQuality = pRenderTargetVBPass->mSampleQuality;
        vbPassPipelineSettings.mDepthStencilFormat = pDepthBuffer->mFormat;
        vbPassPipelineSettings.pVertexLayout = NULL;
        vbPassPipelineSettings.pRasterizerState =
            (gAppSettings.mMsaaLevel > TF_SAMPLE_COUNT_1) ? &rasterizerStateCullNoneMsDesc : &rasterizerStateCullNoneDesc;
        vbPassPipelineSettings.mVRFoveatedRendering = isFoveationEnabled;

        for (uint32_t i = 0; i < 2 * gNumGeomSets; ++i)
        {
            vbPassPipelineSettings.pColorFormats = (i >= gNumGeomSets) ? vbPassMultipleMatsFormats : vbPassFormats;
            vbPassPipelineSettings.mRenderTargetCount =
                (i >= gNumGeomSets) ? TF_ARRAY_COUNT(vbPassMultipleMatsFormats) : TF_ARRAY_COUNT(vbPassFormats);
            vbPassPipelineSettings.pShaderProgram = pShaderVisibilityBufferPass[i];
#if defined(GFX_EXTENDED_PSO_OPTIONS)
            ExtendedGraphicsPipelineDesc edescs[2] = {};
            edescs[0].type = EXTENDED_GRAPHICS_PIPELINE_TYPE_SHADER_LIMITS;
            initExtendedGraphicsShaderLimits(&edescs[0].shaderLimitsDesc);
            edescs[0].shaderLimitsDesc.maxWavesWithLateAllocParameterCache = 16;

            edescs[1].type = EXTENDED_GRAPHICS_PIPELINE_TYPE_PIXEL_SHADER_OPTIONS;
            edescs[1].pixelShaderOptions.outOfOrderRasterization = PIXEL_SHADER_OPTION_OUT_OF_ORDER_RASTERIZATION_ENABLE_WATER_MARK_7;
            edescs[1].pixelShaderOptions.depthBeforeShader =
                !(i % gNumGeomSets) ? PIXEL_SHADER_OPTION_DEPTH_BEFORE_SHADER_ENABLE : PIXEL_SHADER_OPTION_DEPTH_BEFORE_SHADER_DEFAULT;

            pipelineDesc.pPipelineExtensions = edescs;
            pipelineDesc.mExtensionCount = sizeof(edescs) / sizeof(edescs[0]);
#endif
            pipelineDesc.pName = (GEOMSET_OPAQUE == (i % gNumGeomSets)) ? "VB Opaque" : "VB AlphaTested";
            addPipeline(pRenderer, &pipelineDesc, &pPipelineVisibilityBufferPass[i]);

            pipelineDesc.mExtensionCount = 0;
        }

        TFGraphicsPipelineDesc& vbMaterialIDDepthPipelineSettings = pipelineDesc.mGraphicsDesc;
        vbMaterialIDDepthPipelineSettings = { 0 };
        vbMaterialIDDepthPipelineSettings.mPrimitiveTopo = TF_PRIMITIVE_TOPO_TRI_LIST;
        vbMaterialIDDepthPipelineSettings.pDepthState = &depthStateWriteOnlyDesc;
        vbMaterialIDDepthPipelineSettings.mDepthStencilFormat = pRenderTargetVBMaterialIDDepth->mFormat;
        vbMaterialIDDepthPipelineSettings.mSampleCount = pRenderTargetVBMaterialIDDepth->mSampleCount;
        vbMaterialIDDepthPipelineSettings.mSampleQuality = pRenderTargetVBMaterialIDDepth->mSampleQuality;
        vbMaterialIDDepthPipelineSettings.pRasterizerState = &rasterizerStateCullNoneDesc;
        vbMaterialIDDepthPipelineSettings.pShaderProgram = pShaderVisibilityBufferMaterialIDDepth[gAppSettings.mMsaaIndex];
        pipelineDesc.pName = "VB Material ID Depth";
        addPipeline(pRenderer, &pipelineDesc, &pPipelineVisibilityBufferMaterialIDDepth);

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
        vbShadePipelineSettings.mDepthStencilFormat = pRenderTargetVBMaterialIDDepth->mFormat;
        vbShadePipelineSettings.pRasterizerState = isMSAAEnabled ? &rasterizerStateCullNoneMsDesc : &rasterizerStateCullNoneDesc;
        vbShadePipelineSettings.mVRFoveatedRendering = isFoveationEnabled;
#if !defined(ORBIS)
        TFVertexLayout vertexLayoutShadeInstances = {};
        vertexLayoutShadeInstances.mBindingCount = 1;
        vertexLayoutShadeInstances.mAttribCount = 1;
        vertexLayoutShadeInstances.mBindings[0].mStride = sizeof(uint32_t);
        vertexLayoutShadeInstances.mBindings[0].mRate = TF_VERTEX_BINDING_RATE_INSTANCE;
        vertexLayoutShadeInstances.mAttribs[0].mSemantic = TF_SEMANTIC_POSITION;
        vertexLayoutShadeInstances.mAttribs[0].mFormat = TinyImageFormat_R32_UINT;
        vertexLayoutShadeInstances.mAttribs[0].mBinding = 0;
        vertexLayoutShadeInstances.mAttribs[0].mLocation = 0;
        vertexLayoutShadeInstances.mAttribs[0].mOffset = 0;
#endif
        // Shader variants excluding godray
        const uint32_t kNumShaderVariantsExGodray = 2 * MSAA_LEVELS_COUNT;
        for (uint32_t j = 0; j < 2; ++j)
        {
            for (uint32_t i = 0; i < 2; ++i)
            {
                uint32_t shaderIndex = j * (gNumVisBufShaderVariants / 2) +
                                       ((gAppSettings.mMsaaIndex * 2 + i) + (kNumShaderVariantsExGodray * gAppSettings.mEnableGodray));
                vbShadePipelineSettings.pShaderProgram = pShaderVisibilityBufferShade[shaderIndex];
                vbShadePipelineSettings.mSampleCount = gAppSettings.mMsaaLevel;
                if (isMSAAEnabled)
                {
                    vbShadePipelineSettings.pColorFormats = &pRenderTargetMSAA->mFormat;
                    vbShadePipelineSettings.mSampleQuality = pRenderTargetMSAA->mSampleQuality;
                }
                else
                {
                    // vbShadePipelineSettings.pColorFormats = &pSwapChain->ppRenderTargets[0]->mFormat;
                    vbShadePipelineSettings.pColorFormats = &pIntermediateRenderTarget->mFormat;
                    vbShadePipelineSettings.mSampleQuality = pSwapChain->ppRenderTargets[0]->mSampleQuality;
                }
                if (isMSAAEnabled)
                {
                    vbShadePipelineSettings.pDepthState = (j == 0) ? &depthStateOnlyReadStencilDesc : &depthStateReadStencilDesc;
                }
                else
                {
                    vbShadePipelineSettings.pDepthState = (j == 0) ? &depthStateDisableDesc : &depthStateReadDesc;
                }
#if defined(ORBIS)
                vbShadePipelineSettings.pVertexLayout = NULL;
#else
                vbShadePipelineSettings.pVertexLayout = (j > 0) ? &vertexLayoutShadeInstances : NULL;
#endif
#if defined(GFX_EXTENDED_PSO_OPTIONS)
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
                addPipeline(pRenderer, &pipelineDesc, &pPipelineVisibilityBufferShadeSrgb[2 * j + i]);

                if (j > 0 && i == 0)
                {
                    vbShadePipelineSettings.pShaderProgram = pShaderVisibilityBufferShadeSky[gAppSettings.mEnableGodray];
                    vbShadePipelineSettings.pVertexLayout = NULL;
                    pipelineDesc.pName = "VB Shade Sky";
                    addPipeline(pRenderer, &pipelineDesc, &pPipelineVisibilityBufferShadeSkySrgb);
                }

                pipelineDesc.mExtensionCount = 0;
            }
        }

        /************************************************************************/
        // Setup Godray pipeline
        /************************************************************************/
        TFGraphicsPipelineDesc& pipelineSettingsGodRay = pipelineDesc.mGraphicsDesc;
        pipelineSettingsGodRay = { 0 };
        pipelineSettingsGodRay.mPrimitiveTopo = TF_PRIMITIVE_TOPO_TRI_LIST;
        pipelineSettingsGodRay.pRasterizerState = &rasterizerStateCullNoneDesc;
        pipelineSettingsGodRay.mVRFoveatedRendering = isFoveationEnabled;
        pipelineSettingsGodRay.mRenderTargetCount = 1;
        pipelineSettingsGodRay.pColorFormats = isMSAAEnabled ? &pRenderTargetGodRayMS->mFormat : &pRenderTargetGodRay[0]->mFormat;
        pipelineSettingsGodRay.mSampleCount = gAppSettings.mMsaaLevel;
        pipelineSettingsGodRay.mSampleQuality = pSwapChain->ppRenderTargets[0]->mSampleQuality;
        pipelineSettingsGodRay.pShaderProgram = pGodRayPass[gAppSettings.mMsaaIndex];
        pipelineSettingsGodRay.pDepthState = isMSAAEnabled ? &depthStateOnlyReadStencilDesc : &depthStateDisableDesc;
        pipelineSettingsGodRay.mDepthStencilFormat = isMSAAEnabled ? pDownscaledMSAAEdgesStencilBuffer->mFormat : TinyImageFormat_UNDEFINED;
        pipelineDesc.pName = "God Ray";
        addPipeline(pRenderer, &pipelineDesc, &pPipelineGodRayPass);

        /************************************************************************/
        // Setup Present pipeline
        /************************************************************************/

        TFGraphicsPipelineDesc& pipelineSettingsFinalPass = pipelineDesc.mGraphicsDesc;
        pipelineSettingsFinalPass = { 0 };
        pipelineSettingsFinalPass.mPrimitiveTopo = TF_PRIMITIVE_TOPO_TRI_LIST;
        pipelineSettingsFinalPass.pRasterizerState = &rasterizerStateCullNoneDesc;
        pipelineSettingsFinalPass.mVRFoveatedRendering = true;
        pipelineSettingsFinalPass.mRenderTargetCount = 1;
        pipelineSettingsFinalPass.pColorFormats = &pSwapChain->ppRenderTargets[0]->mFormat;
        pipelineSettingsFinalPass.mSampleCount = pSwapChain->ppRenderTargets[0]->mSampleCount;
        pipelineSettingsFinalPass.mSampleQuality = pSwapChain->ppRenderTargets[0]->mSampleQuality;
        pipelineSettingsFinalPass.pShaderProgram = pShaderPresentPass;
        pipelineSettingsGodRay.pDepthState = NULL;
        pipelineSettingsGodRay.mDepthStencilFormat = TinyImageFormat_UNDEFINED;
        pipelineDesc.pName = "Composite";
        addPipeline(pRenderer, &pipelineDesc, &pPipelinePresentPass);

        /************************************************************************/
        // Setup MSAA resolve pipeline
        /************************************************************************/
        depthStateDisableDesc = {};
        rasterizerStateCullNoneDesc = { TF_CULL_MODE_NONE };

        pipelineDesc = {};
        PIPELINE_LAYOUT_DESC(pipelineDesc, NULL, NULL, NULL, SRT_LAYOUT_DESC(SrtResolve, PerDraw));
        pipelineDesc.pCache = pPipelineCache;
        pipelineDesc.mType = TF_PIPELINE_TYPE_GRAPHICS;
        TFGraphicsPipelineDesc& resolvePipelineSettings = pipelineDesc.mGraphicsDesc;
        resolvePipelineSettings = { 0 };
        resolvePipelineSettings.mPrimitiveTopo = TF_PRIMITIVE_TOPO_TRI_LIST;
        resolvePipelineSettings.mRenderTargetCount = 1;
        resolvePipelineSettings.pDepthState = &depthStateDisableDesc;
        resolvePipelineSettings.pColorFormats = &pIntermediateRenderTarget->mFormat;
        resolvePipelineSettings.mSampleCount = TF_SAMPLE_COUNT_1;
        resolvePipelineSettings.mSampleQuality = 0;
        resolvePipelineSettings.pRasterizerState = &rasterizerStateCullNoneDesc;
        resolvePipelineSettings.pShaderProgram = pShaderResolve[gAppSettings.mMsaaIndex];
        pipelineDesc.pName = "MSAA Resolve - Final";
        addPipeline(pRenderer, &pipelineDesc, &pPipelineResolve);

        pipelineDesc.pName = "MSAA Resolve - GodRay";
        resolvePipelineSettings.pColorFormats = &pRenderTargetGodRay[0]->mFormat;
        addPipeline(pRenderer, &pipelineDesc, &pPipelineResolveGodRay);

        /************************************************************************/
        // Setup MSAA edge detect pipeline
        /************************************************************************/
        if (isMSAAEnabled)
        {
            pipelineDesc = {};
            PIPELINE_LAYOUT_DESC(pipelineDesc, SRT_LAYOUT_DESC(SrtData, Persistent), SRT_LAYOUT_DESC(SrtData, PerFrame), NULL, NULL);
            pipelineDesc.pCache = pPipelineCache;
            pipelineDesc.mType = TF_PIPELINE_TYPE_GRAPHICS;
            TFGraphicsPipelineDesc& msaaEdgesPipeline = pipelineDesc.mGraphicsDesc;
            msaaEdgesPipeline = { 0 };
            msaaEdgesPipeline.mPrimitiveTopo = TF_PRIMITIVE_TOPO_TRI_LIST;
            msaaEdgesPipeline.mRenderTargetCount = 0;
            msaaEdgesPipeline.pDepthState = &depthStateOnlyWriteStencilDesc;
            msaaEdgesPipeline.mDepthStencilFormat = pRenderTargetVBMaterialIDDepth->mFormat;
            msaaEdgesPipeline.mSampleCount = gAppSettings.mMsaaLevel;
            msaaEdgesPipeline.mSampleQuality = 0;
            msaaEdgesPipeline.pRasterizerState = &rasterizerStateCullNoneMsDesc;
            msaaEdgesPipeline.pShaderProgram = pShaderDrawMSAAEdges[gAppSettings.mMsaaIndex - 1];
            pipelineDesc.pName = "Render Stencil MSAA edges";
            addPipeline(pRenderer, &pipelineDesc, &pPipelineDrawMSAAEdges);

            // Downscale pipeline
            msaaEdgesPipeline.pShaderProgram = pShaderDownscaleMSAAEdges[gAppSettings.mMsaaIndex - 1];
            msaaEdgesPipeline.mDepthStencilFormat = pDownscaledMSAAEdgesStencilBuffer->mFormat;
            pipelineDesc.pName = "Downscale Stencil MSAA edges";
            addPipeline(pRenderer, &pipelineDesc, &pPipelineDownscaleMSAAEdges);

            /************************************************************************/
            // Setup MSAA debug view pipeline
            /************************************************************************/
            pipelineDesc = {};
            PIPELINE_LAYOUT_DESC(pipelineDesc, NULL, NULL, NULL, SRT_LAYOUT_DESC(SrtResolve, PerDraw));
            pipelineDesc.pCache = pPipelineCache;
            pipelineDesc.mType = TF_PIPELINE_TYPE_GRAPHICS;
            TFGraphicsPipelineDesc& msaaDebugViewPipeline = pipelineDesc.mGraphicsDesc;
            msaaDebugViewPipeline = { 0 };
            msaaDebugViewPipeline.mPrimitiveTopo = TF_PRIMITIVE_TOPO_TRI_LIST;
            msaaDebugViewPipeline.mRenderTargetCount = 1;
            TinyImageFormat targetFormat = gDebugRTFormat;
            msaaDebugViewPipeline.pColorFormats = &targetFormat;
            msaaDebugViewPipeline.pDepthState = &depthStateDisableDesc;
            msaaDebugViewPipeline.mSampleCount = TF_SAMPLE_COUNT_1;
            msaaDebugViewPipeline.mSampleQuality = 0;
            msaaDebugViewPipeline.pRasterizerState = &rasterizerStateCullNoneDesc;
            msaaDebugViewPipeline.pShaderProgram = pShaderDebugMSAA[gAppSettings.mMsaaIndex - 1];
            pipelineDesc.pName = "Render MSAA Debug Shader";
            addPipeline(pRenderer, &pipelineDesc, &pPipelineDebugMSAA);
        }
    }

    void removePipelines()
    {
        if (gAppSettings.mMsaaLevel > TF_SAMPLE_COUNT_1)
        {
            removePipeline(pRenderer, pPipelineDebugMSAA);
            removePipeline(pRenderer, pPipelineDownscaleMSAAEdges);
            removePipeline(pRenderer, pPipelineDrawMSAAEdges);
        }
        removePipeline(pRenderer, pPipelineResolve);
        removePipeline(pRenderer, pPipelineResolveGodRay);

        removePipeline(pRenderer, pPipelineGodRayPass);
        removePipeline(pRenderer, pPipelineGodRayBlurPass);

        removePipeline(pRenderer, pPipelinePresentPass);

        for (uint32_t i = 0; i < 4; ++i)
            removePipeline(pRenderer, pPipelineVisibilityBufferShadeSrgb[i]);

        removePipeline(pRenderer, pPipelineVisibilityBufferShadeSkySrgb);

        for (uint32_t i = 0; i < 2 * gNumGeomSets; ++i)
            removePipeline(pRenderer, pPipelineVisibilityBufferPass[i]);

        removePipeline(pRenderer, pPipelineVisibilityBufferMaterialIDDepth);

        for (uint32_t i = 0; i < gNumGeomSets; ++i)
            removePipeline(pRenderer, pPipelineShadowPass[i]);

        // Destroy triangle filtering pipelines
        removePipeline(pRenderer, pPipelineClusterLights);
        removePipeline(pRenderer, pPipelineClearLightClusters);
        removePipeline(pRenderer, pPipelineTriangleFiltering);
        removePipeline(pRenderer, pPipelineBuildMaterialTileMasks);
        removePipeline(pRenderer, pPipelineShaderInstancePrefixSum);
        removePipeline(pRenderer, pPipelineScatterShaderInstances);
        // Compute pixel-list pipelines
        removePipeline(pRenderer, pPipelineClearPixelCounts);
        removePipeline(pRenderer, pPipelineCountPixels);
        removePipeline(pRenderer, pPipelinePrefixSumTileMaterials);
        removePipeline(pRenderer, pPipelinePrefixSumTiles);
        removePipeline(pRenderer, pPipelinePrefixSumPipelines);
        removePipeline(pRenderer, pPipelineFinalizeTileMaterialOffsets);
        removePipeline(pRenderer, pPipelineWritePixelCommands);
        removePipeline(pRenderer, pPipelineGenerateDispatchArgs);
        removePipeline(pRenderer, pPipelineShadeComputeSky);
        for (uint32_t i = 0; i < gShadeComputePipelineCount; ++i)
            removePipeline(pRenderer, pPipelineShadeCompute[i]);
        removePipeline(pRenderer, pPipelineClearBuffers);
    }

    // This method sets the contents of the buffers to indicate the rendering pass that
    // the whole scene triangles must be rendered (no cluster / triangle filtering).
    // This is useful for testing purposes to compare visual / performance results.
    static void addTriangleFilteringBuffers()
    {
        /************************************************************************/
        // Mesh constants
        /************************************************************************/
        // create mesh constants buffer
        MeshConstants* meshConstants = (MeshConstants*)tf_malloc(gMeshCount * sizeof(MeshConstants));

        for (uint32_t meshIndex = 0; meshIndex < gMeshCount; ++meshIndex)
        {
            meshConstants[meshIndex].indexOffset = (*gPackage->pGeoData->ppGeometry)->pDrawArgs[meshIndex].mStartIndex;
            meshConstants[meshIndex].vertexOffset = (*gPackage->pGeoData->ppGeometry)->pDrawArgs[meshIndex].mVertexOffset;
            const TFMaterialProps materialProps = gPackage->pTextureMetadata->pMaterialProps[meshIndex];
            meshConstants[meshIndex].materialID_flags =
                ((materialProps.mFlags & FLAG_MASK) << FLAG_LOW_BIT) | ((meshIndex & MATERIAL_ID_MASK) << MATERIAL_ID_LOW_BIT);
            meshConstants[meshIndex].specEmissiveStrength = materialProps.mSpecEmissiveStrength;
            meshConstants[meshIndex].baseColor = materialProps.mBaseColor;
            // Test routing: spread scene materials across shader variants so the multi-shader paths show all PSOs
            // TODO: replace with real material data mapping  to shaders directly
            uint32_t materialShaderID = (meshIndex * gShadeComputePipelineCount) / (uint32_t)gMeshCount;
            if (materialShaderID >= gShadeComputePipelineCount)
                materialShaderID = gShadeComputePipelineCount - 1u;
            meshConstants[meshIndex].materialShaderID = materialShaderID;
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

        uint32_t         shadeQuadInstanceCount = VB_SHADE_TILE_COUNT * (uint32_t)gMeshCount;
        TFBufferLoadDesc shadeQuadInstancesDesc = {};
        shadeQuadInstancesDesc.mDesc.mDescriptors =
            TF_DESCRIPTOR_TYPE_BUFFER | TF_DESCRIPTOR_TYPE_RW_BUFFER | TF_DESCRIPTOR_TYPE_VERTEX_BUFFER;
        shadeQuadInstancesDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_GPU_ONLY;
        shadeQuadInstancesDesc.mDesc.mElementCount = shadeQuadInstanceCount;
        shadeQuadInstancesDesc.mDesc.mStructStride = sizeof(uint32_t);
        shadeQuadInstancesDesc.mDesc.mSize = (uint64_t)shadeQuadInstanceCount * sizeof(uint32_t);
        shadeQuadInstancesDesc.mDesc.mStartState = TF_RESOURCE_STATE_UNORDERED_ACCESS;
        shadeQuadInstancesDesc.ppBuffer = &pShadeQuadInstancesBuffer;
        shadeQuadInstancesDesc.mDesc.pName = "Shade Quad Instances";
        addResource(&shadeQuadInstancesDesc, NULL);

        // One IndirectDrawArguments {vertexCount, instanceCount, startVertex, startInstance} per shade shader
        uint32_t* shadeDrawArgsInit = (uint32_t*)tf_calloc(MAX_MATERIAL_SHADERS * 4u, sizeof(uint32_t));
        for (uint32_t shaderIndex = 0; shaderIndex < MAX_MATERIAL_SHADERS; ++shaderIndex)
            shadeDrawArgsInit[shaderIndex * 4u + 0u] = 6u;
        TFBufferLoadDesc shadeDrawArgsDesc = {};
        shadeDrawArgsDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_RW_BUFFER | TF_DESCRIPTOR_TYPE_INDIRECT_BUFFER;
        shadeDrawArgsDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_GPU_ONLY;
        shadeDrawArgsDesc.mDesc.mElementCount = MAX_MATERIAL_SHADERS * 4u;
        shadeDrawArgsDesc.mDesc.mStructStride = sizeof(uint32_t);
        shadeDrawArgsDesc.mDesc.mSize = (uint64_t)MAX_MATERIAL_SHADERS * 4u * sizeof(uint32_t);
        shadeDrawArgsDesc.mDesc.mStartState = TF_RESOURCE_STATE_UNORDERED_ACCESS;
        shadeDrawArgsDesc.pData = shadeDrawArgsInit;
        shadeDrawArgsDesc.ppBuffer = &pShadeDrawArgsBuffer;
        shadeDrawArgsDesc.mDesc.pName = "Shade Draw Args";
        addResource(&shadeDrawArgsDesc, NULL);

        uint32_t* shaderInstanceCountsInit = (uint32_t*)tf_calloc(MAX_MATERIAL_SHADERS, sizeof(uint32_t));
        ASSERT(gMeshCount <= MAX_MATERIAL_COUNT);
        uint32_t         materialTileBinCount = ((uint32_t)gMeshCount + MATERIAL_TILE_BIN_SIZE - 1u) / MATERIAL_TILE_BIN_SIZE;
        uint32_t         materialTileMaskCount = VB_SHADE_TILE_COUNT * materialTileBinCount;
        TFBufferLoadDesc materialTileMasksDesc = {};
        materialTileMasksDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_BUFFER | TF_DESCRIPTOR_TYPE_RW_BUFFER;
        materialTileMasksDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_GPU_ONLY;
        materialTileMasksDesc.mDesc.mElementCount = materialTileMaskCount;
        materialTileMasksDesc.mDesc.mStructStride = sizeof(uint32_t);
        materialTileMasksDesc.mDesc.mSize = (uint64_t)materialTileMaskCount * sizeof(uint32_t);
        materialTileMasksDesc.mDesc.mStartState = TF_RESOURCE_STATE_UNORDERED_ACCESS;
        materialTileMasksDesc.ppBuffer = &pMaterialTileMasksBuffer;
        materialTileMasksDesc.mDesc.pName = "Material Tile Masks";
        addResource(&materialTileMasksDesc, NULL);

        TFBufferLoadDesc shaderInstanceCountsDesc = {};
        shaderInstanceCountsDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_BUFFER | TF_DESCRIPTOR_TYPE_RW_BUFFER;
        shaderInstanceCountsDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_GPU_ONLY;
        shaderInstanceCountsDesc.mDesc.mElementCount = MAX_MATERIAL_SHADERS;
        shaderInstanceCountsDesc.mDesc.mStructStride = sizeof(uint32_t);
        shaderInstanceCountsDesc.mDesc.mSize = (uint64_t)MAX_MATERIAL_SHADERS * sizeof(uint32_t);
        shaderInstanceCountsDesc.mDesc.mStartState = TF_RESOURCE_STATE_UNORDERED_ACCESS;
        shaderInstanceCountsDesc.pData = shaderInstanceCountsInit;
        shaderInstanceCountsDesc.ppBuffer = &pShaderInstanceCountsBuffer;
        shaderInstanceCountsDesc.mDesc.pName = "Shader Instance Counts";
        addResource(&shaderInstanceCountsDesc, NULL);

        TFBufferLoadDesc shaderInstanceCursorsDesc = shaderInstanceCountsDesc;
        shaderInstanceCursorsDesc.ppBuffer = &pShaderInstanceCursorsBuffer;
        shaderInstanceCursorsDesc.mDesc.pName = "Shader Instance Cursors";
        addResource(&shaderInstanceCursorsDesc, NULL);
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

        ubDesc.mDesc.mSize = sizeof(PerFrameVBConstantsData);
        ubDesc.mDesc.pName = "gPerFrameVBConstants Uniform Buffer Desc";
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
        lightClustersCountBufferDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_BUFFER | TF_DESCRIPTOR_TYPE_RW_BUFFER;
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
        lightClustersDataBufferDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_BUFFER | TF_DESCRIPTOR_TYPE_RW_BUFFER;
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
        skyDataBufferDesc.mDesc.mSize = sizeof(UniformCameraSkyData);
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
        /************************************************************************/
        /************************************************************************/
        waitForAllResourceLoads();

        tf_free(meshConstants);
        tf_free(shadeDrawArgsInit);
        tf_free(shaderInstanceCountsInit);
    }

    void removeTriangleFilteringBuffers()
    {
        /************************************************************************/
        // Mesh constants
        /************************************************************************/
        removeResource(pMeshConstantsBuffer);
        removeResource(pShadeQuadInstancesBuffer);
        removeResource(pShadeDrawArgsBuffer);
        removeResource(pMaterialTileMasksBuffer);
        removeResource(pShaderInstanceCountsBuffer);
        removeResource(pShaderInstanceCursorsBuffer);

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

        mat4           cameraModel = mat4::scale(float3(SCENE_SCALE));
        TFCameraMatrix cameraView = pCamera->getViewMatrix();

        TFCameraMatrix cameraProj = camMatPerspectiveReverseZ(PI / 2.0f, aspectRatioInv, gAppSettings.nearPlane, gAppSettings.farPlane);

        float timeNormalized =
            (gAppSettings.mTimeOfDay - gAppSettings.mTimeOfDaySunrise) / (gAppSettings.mTimeOfDaySunset - gAppSettings.mTimeOfDaySunrise);

        // direction from light to scene (for BRDFs)
        float sunRotation = PI * timeNormalized;
        mat4  rotationLight = mat4::rotationYX(gAppSettings.mSunriseDirection, sunRotation);
        vec3  lightDir = (rotationLight * vec4::zAxis()).getXYZ();

        // direction from shadowmap camera to its pivot position
        float shadowPadding = (1.0f - gAppSettings.mShadowRange) / 2.0f;
        float shadowRotationTime = lerp(shadowPadding, 1.0f - shadowPadding, timeNormalized);
        float sunRotationShadow = PI * shadowRotationTime;
        mat4  rotationShadow = mat4::rotationYX(gAppSettings.mSunriseDirection, sunRotationShadow);
        vec3  shadowDir = (rotationShadow * vec4::zAxis()).getXYZ();

        float3 shadowCameraPivot = float3(-.927f, 13.93f, 6.815f) * SCENE_SCALE;
        float3 shadowCameraOffset = (-shadowDir * 25.0f) * SCENE_SCALE;
        mat4   invTranslationShadow = mat4::translation(-(shadowCameraPivot + shadowCameraOffset));

        float          shadowBounds = 12.0f * SCENE_SCALE;
        mat4           shadowModel = mat4::scale(float3(SCENE_SCALE));
        mat4           shadowView = transpose(rotationShadow) * invTranslationShadow;
        TFCameraMatrix shadowProj =
            camMatOrthographicReverseZ(-shadowBounds, shadowBounds, -shadowBounds, shadowBounds, 0.1f, 55.0f * SCENE_SCALE);

        float2 twoOverRes = { 2.0f / float(width), 2.0f / float(height) };

        /************************************************************************/
        // Lighting data
        /************************************************************************/
        currentFrame->gPerFrameUniformData.camPos = (vec4(pCamera->getViewPosition()));
        currentFrame->gPerFrameUniformData.lightDir = (vec4(lightDir));
        currentFrame->gPerFrameUniformData.twoOverRes = twoOverRes;
        currentFrame->gPerFrameUniformData.esmControl = gAppSettings.mEsmControl;
        currentFrame->gPerFrameUniformData.mGodRayScatterFactor = gGodRayConstant.mScatterFactor;
        currentFrame->gPerFrameUniformData.mMaxMaterialCount = (uint32_t)gMeshCount;
        // Compute material-sort path per-frame dimensions
        {
            uint32_t renderWidth = pIntermediateRenderTarget ? pIntermediateRenderTarget->mWidth : 0u;
            uint32_t renderHeight = pIntermediateRenderTarget ? pIntermediateRenderTarget->mHeight : 0u;
            currentFrame->gPerFrameUniformData.mNumPipelines = gShadeComputePipelineCount;
            currentFrame->gPerFrameUniformData.mNumDispatchMaterials = gComputeDispatchMaterialCount;
            currentFrame->gPerFrameUniformData.mNumTilesX = gComputeNumTilesX;
            currentFrame->gPerFrameUniformData.mNumTiles = gComputeNumTiles;
            currentFrame->gPerFrameUniformData.mRenderWidth = renderWidth;
            currentFrame->gPerFrameUniformData.mRenderHeight = renderHeight;
            currentFrame->gPerFrameUniformData.mPixelCommandMaskUintCount = gComputePixelCommandMaskUintCount;
        }
        /************************************************************************/
        // Matrix data
        /************************************************************************/
        currentFrame->gPerFrameVBUniformData.transform[VIEW_SHADOW].vp = camMatMulMat4(&shadowProj, &shadowView);
        currentFrame->gPerFrameVBUniformData.transform[VIEW_SHADOW].invVP =
            camMatInverse(&currentFrame->gPerFrameVBUniformData.transform[VIEW_SHADOW].vp);
        currentFrame->gPerFrameVBUniformData.transform[VIEW_SHADOW].projection = shadowProj;
        currentFrame->gPerFrameVBUniformData.transform[VIEW_SHADOW].mvp =
            camMatMulMat4(&currentFrame->gPerFrameVBUniformData.transform[VIEW_SHADOW].vp, &shadowModel);

        currentFrame->gPerFrameVBUniformData.transform[VIEW_CAMERA].vp = camMatMul(&cameraProj, &cameraView);
        currentFrame->gPerFrameVBUniformData.transform[VIEW_CAMERA].invVP =
            camMatInverse(&currentFrame->gPerFrameVBUniformData.transform[VIEW_CAMERA].vp);
        currentFrame->gPerFrameVBUniformData.transform[VIEW_CAMERA].projection = cameraProj;
        currentFrame->gPerFrameVBUniformData.transform[VIEW_CAMERA].mvp =
            camMatMulMat4(&currentFrame->gPerFrameVBUniformData.transform[VIEW_CAMERA].vp, &cameraModel);
#ifdef QUEST_VR
        currentFrame->gPerFrameVBUniformData.cullingMVP[VIEW_SHADOW] =
            currentFrame->gPerFrameVBUniformData.transform[VIEW_SHADOW].mvp.mMatrices[MONO_CAMERA_VIEW_INDEX];
        mat4 superFrustumView;
        mat4 superFrustumProject;
        camMatSuperFrustum(&cameraView, gAppSettings.nearPlane, gAppSettings.farPlane, &superFrustumView, &superFrustumProject, true);
        currentFrame->gPerFrameVBUniformData.cullingMVP[VIEW_CAMERA] = superFrustumProject * superFrustumView * cameraModel;
#endif
        /************************************************************************/
        // Culling data
        /************************************************************************/
        currentFrame->gPerFrameVBUniformData.cullingViewports[VIEW_SHADOW].sampleCount = 1;
        currentFrame->gPerFrameVBUniformData.cullingViewports[VIEW_SHADOW].windowSize = { (float)gShadowMapSize, (float)gShadowMapSize };

        currentFrame->gPerFrameVBUniformData.cullingViewports[VIEW_CAMERA].sampleCount = gAppSettings.mMsaaLevel;
        currentFrame->gPerFrameVBUniformData.cullingViewports[VIEW_CAMERA].windowSize = { (float)width, (float)height };

        // Cache eye position in object space for cluster culling on the CPU
        currentFrame->gEyeObjectSpace[VIEW_SHADOW] = (inverse(shadowView * shadowModel) * vec4(0, 0, 0, 1)).getXYZ();
        currentFrame->gEyeObjectSpace[VIEW_CAMERA] =
            (inverse(cameraView.mMatrices[MONO_CAMERA_VIEW_INDEX] * cameraModel) * vec4(0, 0, 0, 1))
                .getXYZ(); // vec4(0,0,0,1) is the camera position in eye space
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
        currentFrame->gPerFrameUniformData.depthTexSize = { (float)pDepthBuffer->mWidth, (float)pDepthBuffer->mHeight };
        currentFrame->gPerFrameUniformData.godrayAttenuation = gAppSettings.mGodrayAttenuation;
        currentFrame->gPerFrameUniformData.nearPlane = gAppSettings.nearPlane;
        currentFrame->gPerFrameUniformData.farPlane = gAppSettings.farPlane;

        /************************************************************************/
        // Skybox
        /************************************************************************/
        cameraView = camMatSetTranslation(&cameraView, vec3(0));
        TFCameraMatrix viewProj = camMatMul(&cameraProj, &cameraView);
        currentFrame->gUniformDataSky.invProjViewOrigin = camMatInverse(&viewProj);

        /************************************************************************/
        // Tonemap
        /************************************************************************/

        currentFrame->gPerFrameUniformData.mLinearScale = gAppSettings.LinearScale / 10000.0f;
        currentFrame->gPerFrameUniformData.mOutputMode = (uint)gAppSettings.mOutputMode;
    }
    /************************************************************************/
    // UI
    /************************************************************************/
    void updateDynamicUIElements()
    {
        // God Ray
        {
            static bool gPrevEnableGodRay = gAppSettings.mEnableGodray;
            if (gPrevEnableGodRay != gAppSettings.mEnableGodray)
            {
                gPrevEnableGodRay = gAppSettings.mEnableGodray;
                waitQueueIdle(pGraphicsQueue);
                waitQueueIdle(pComputeQueue);
                unloadFontSystem();
                unloadUserInterface();

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
            }
        }

        // Restart frame/semaphore pairing when async topology changes.
        {
            static bool gPrevAsyncCompute = gAppSettings.mAsyncCompute;
            static bool gPrevAsyncComputeBuild = gAppSettings.mAsyncComputePixelListBuild;
            if (gPrevAsyncCompute != gAppSettings.mAsyncCompute || gPrevAsyncComputeBuild != gAppSettings.mAsyncComputePixelListBuild)
            {
                gPrevAsyncCompute = gAppSettings.mAsyncCompute;
                gPrevAsyncComputeBuild = gAppSettings.mAsyncComputePixelListBuild;
                waitQueueIdle(pGraphicsQueue);
                waitQueueIdle(pComputeQueue);
                gPrevGraphicsSemaphore = NULL;
                memset(gComputeSemaphores, 0, sizeof(gComputeSemaphores));
            }
        }

        // AO
        gAppSettings.mVisualizeAO &= gAppSettings.mEnableAO;
    }
};

DEFINE_APPLICATION_MAIN(Visibility_Buffer)
