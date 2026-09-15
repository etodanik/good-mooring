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

#include "../../../../Common_3/Utilities/ThirdParty/OpenSource/Nothings/stb_ds.h"
#include "../../../../Common_3/Utilities/ThirdParty/OpenSource/bstrlib/bstrlib.h"

#include "../../../../Common_3/Application/Interfaces/IApp.h"
#include "../../../../Common_3/Application/Interfaces/ICamera.h"
#include "../../../../Common_3/Application/Interfaces/IFont.h"
#include "../../../../Common_3/Application/Interfaces/IProfiler.h"
#include "../../../../Common_3/Application/Interfaces/IScreenshot.h"
#include "../../../../Common_3/Application/Interfaces/IUI.h"
#include "../../../../Common_3/Game/Interfaces/IScripting.h"
#include "../../../../Common_3/Graphics/Interfaces/IGraphics.h"
#include "../../../../Common_3/OS/Interfaces/IInput.h"
#include "../../../../Common_3/Renderer/Interfaces/IVisibilityBuffer.h"
#include "../../../../Common_3/Utilities/Interfaces/IFileSystem.h"
#include "../../../../Common_3/Utilities/Interfaces/ILog.h"
#include "../../../../Common_3/Utilities/Interfaces/IThread.h"
#include "../../../../Common_3/Utilities/Interfaces/ITime.h"

#include "../../../../Common_3/Resources/AnimationSystem/Animation/AnimatedObject.h"
#include "../../../../Common_3/Resources/AnimationSystem/Animation/Animation.h"
#include "../../../../Common_3/Resources/AnimationSystem/Animation/Clip.h"
#include "../../../../Common_3/Resources/AnimationSystem/Animation/ClipController.h"
#include "../../../../Common_3/Resources/AnimationSystem/Animation/Rig.h"
#include "../../../../Common_3/Resources/AnimationSystem/Animation/SkeletonBatcher.h"
#include "../../../../Common_3/Utilities/RingBuffer.h"

// fsl

#define NO_FSL_DEFINITIONS
#include "../../../../Common_3/Resources/Streaming/Interfaces/IStreaming.h"

#include "../../../../Common_3/Graphics/FSL/defaults.h"
#include "../../../../Common_3/Graphics/FSL/fsl_srt.h"
#include "Shaders/FSL/ShaderDefs.h.fsl"
#include "./Shaders/FSL/Display.srt.h"
#include "./Shaders/FSL/Global.srt.h"
#include "./Shaders/FSL/GodrayBlur.srt.h"
#include "./Shaders/FSL/LightClusters.srt.h"
#include "./Shaders/FSL/PreSkinVertexes.srt.h"
#include "./Shaders/FSL/ProgMSAAResolve.srt.h"
#include "./Shaders/FSL/ResolveVRS.srt.h"
#include "./Shaders/FSL/TriangleFiltering.srt.h"
#include "./Shaders/FSL/VisibilityBufferPassTransparent.srt.h"

#include "../../../../Common_3/Utilities/Interfaces/IMemory.h"

#define FOREACH_SETTING(X)       \
    X(BindlessSupported, 1)      \
    X(DisableGodRays, 0)         \
    X(MSAASampleCount, 4)        \
    X(AddGeometryPassThrough, 0) \
    X(MaxMSAALevel, 4)

#define GENERATE_ENUM(x, y)   x,
#define GENERATE_STRING(x, y) #x,
#define GENERATE_STRUCT(x, y) uint32_t m##x;
#define GENERATE_VALUE(x, y)  y,
#define INIT_STRUCT(s)        s = { FOREACH_SETTING(GENERATE_VALUE) }

typedef enum ESettings
{
    FOREACH_SETTING(GENERATE_ENUM) Count
} ESettings;

const char* gSettingNames[] = { FOREACH_SETTING(GENERATE_STRING) };
char        gDebugText[256];

// Useful for using names directly instead of subscripting an array
struct ConfigSettings
{
    FOREACH_SETTING(GENERATE_STRUCT)
} gGpuSettings;

#define SCENE_SCALE 10.0f

#ifndef AUTOMATED_TESTING
#define ENABLE_STREAMING 1
#endif

typedef enum OutputMode
{
    OUTPUT_MODE_SDR = 0,
    OUTPUT_MODE_HDR10,

    OUTPUT_MODE_COUNT
} OutputMode;

struct TonemapInfo
{
    float linearScale;
    uint  outputMode;
};

TonemapInfo gTonemapInformation;

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

FORGE_CONSTEXPR uint32_t MSAA_STENCIL_MASK = 1;
FORGE_CONSTEXPR uint32_t VRS_STENCIL_MASK = 1;

//--------------------------------------------------------------------------------------------
// STATIC MESH DATA
//--------------------------------------------------------------------------------------------

struct StaticMeshInstance
{
    // Initialization data, cannot be changed after Init/Load
    const uint32_t mGeomSet;

    float mRotationSpeedYDeg;

    // Instance dynamic data
    vec3 mTranslation;
    vec3 mScale;
    Quat mRotation;
};

// #NOTE: Two sets of resources (one in flight and one being used on CPU)
const uint32_t gDataBufferCount = 2;

// VRS related constants
uint32_t          gDivider = 2;
TFSampleLocations gLocations[4] = { { -4, -4 }, { 4, -4 }, { -4, 4 }, { 4, 4 } };
#define GEOMSET_ALPHA_CUTOUT_VRS NUM_GEOMETRY_SETS

//--------------------------------------------------------------------------------------------
// ANIMATION DATA
//--------------------------------------------------------------------------------------------

// Filenames
const char* gSkeletonFile = "stormtrooper/skeleton.ozz";
const char* gClipFile = "stormtrooper/animations/dance.ozz";

struct AnimatedMeshInstance
{
    const uint32_t mGeomSet;
    const float    mPlaybackSpeed;

    // Instance dynamic data
    vec3 mTranslation;
    vec3 mScale;
    Quat mRotation;

    AnimatedObject mAnimObject;
    Animation      mAnimation;
    ClipController mClipController;

    const TFGeometryData* pGeomData;
    mat4*                 pBoneMatrixes; // Size of this array is the size of Rig::mNumJoints

    uint32_t mJointMatrixOffset;
    uint32_t mPreSkinnedVertexOffset; // Relative to the first pre-skinned vertex in the buffer
};

Clip gClip;
Rig  gRig;

TFGeometryData* pAnimatedMeshGeomData;

TFTexture* gNullTextureResource = NULL;

// For now instances are hardcoded
StaticMeshInstance gStaticMeshInstances[] = {
    // 218 is the index for stormtrooper, the main scene has 218 meshes (index 217 contains the last mesh), stormtrooper mesh is placed
    // afterwards
    { GEOMSET_OPAQUE, 30.f, vec3(6.5f, 2.3f, 1.f), vec3(0.5f), Quat::identity() },
    { GEOMSET_OPAQUE, -50.f, vec3(8.f, 2.4f, 0.f), vec3(0.5f), Quat::identity() },
    { GEOMSET_OPAQUE, 60.f, vec3(4.f, 2.4f, 0.f), vec3(0.5f), Quat::identity() },
};

AnimatedMeshInstance gAnimatedMeshInstances[]{ { GEOMSET_OPAQUE, 2.f, vec3(5.f, 2.3f, 1.f), vec3(0.5f), quatRotationY(degToRad(80.f)) },
                                               { GEOMSET_OPAQUE, 1.f, vec3(9.f, 2.3f, 2.f), vec3(0.5f), quatRotationY(degToRad(270.f)) },
                                               { GEOMSET_OPAQUE, 0.5f, vec3(9.f, 2.3f, 4.f), vec3(0.5f), quatRotationY(degToRad(240.f)) } };

const uint32_t gStaticMeshInstanceCount = TF_ARRAY_COUNT(gStaticMeshInstances);
const uint32_t gAnimatedMeshInstanceCount = TF_ARRAY_COUNT(gAnimatedMeshInstances);
const uint32_t gMaxAnimatedInstances = gAnimatedMeshInstanceCount;
const uint32_t gMaxMeshInstances = TF_ARRAY_COUNT(gStaticMeshInstances) + gMaxAnimatedInstances;
const uint32_t gMaxJointMatrixes =
    gAnimatedMeshInstanceCount * 128; // Assume a max of 128 joints per animated instance (stormtrooper has just 70)
uint32_t gSceneMeshCount = 0;
uint32_t gPreSkinnedVertexCountPerFrame = 0;
uint32_t gPreSkinnedVertexStartOffset = 0;

// Camera Walking
static float gCameraWalkingTime = 0.0f;
float3*      gCameraPathData;

uint      gCameraPoints;
float     gTotalElpasedTime;
uint32_t* gTransparentMaterialIDs = NULL;
/************************************************************************/
// GUI CONTROLS
/************************************************************************/
#define MSAA_LEVELS_COUNT 3U

#if defined(NX64)
#define DEFAULT_ASYNC_COMPUTE false
#else
#define DEFAULT_ASYNC_COMPUTE true
#endif

// PVS-Studio V802 On 64-bit platform, structure size can be reduced from 136 to 120 bytes by rearranging the fields according to their
// sizes in decreasing order."
typedef struct AppSettings
{ //-V802
    OutputMode mOutputMode = OUTPUT_MODE_SDR;

    bool mUpdateSimulation = true;

    // Set this variable to true to bypass triangle filtering calculations, holding and representing the last filtered data.
    // This is useful for inspecting filtered geometry for debugging purposes.
    bool mHoldFilteredResults = false;

    bool mAsyncCompute = DEFAULT_ASYNC_COMPUTE;
    // toggle rendering of local point lights
    bool mRenderLocalLights = true;
#if defined(TF_ENABLE_WORKGRAPH)
    bool mGpuPipelineWorkgraph = true;
#endif

    bool mDrawDebugTargets = false;

    bool mEnableVRS = true;

    float nearPlane = 0.1f;
    float farPlane = 1000.0f;

    float4 mLightColor = { 1.0f, 0.8627f, 0.78f, 2.5f };

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

    TFUIWidget* pOutputSupportsHDRWidget = NULL;

    bool     mEnableGodray = true;
    uint32_t mFilterRadius = 3;

    float mEsmControl = 225.0f;

    float LinearScale = 260.0f;

    // HDR10
    DisplayColorSpace  mCurrentSwapChainColorSpace = ColorSpace_Rec2020;
    DisplayColorRange  mDisplayColorRange = ColorRange_RGB;
    DisplaySignalRange mDisplaySignalRange = Display_SIGNAL_RANGE_FULL;

    TFSampleCount mMSAALevel = TF_SAMPLE_COUNT_2;
    TFSampleCount mMaxMSAALevel = TF_SAMPLE_COUNT_4;
    uint32_t      mMSAAIndex = (uint32_t)log2((uint32_t)mMSAALevel);
    uint32_t      mMSAAIndexRequested = mMSAAIndex;

    // Camera Walking
    bool  cameraWalking = false;
    float cameraWalkingSpeed = 1.0f;

    // Streaming
    bool mDrawDebugBoxes = false;
    bool mUsePixelCoverage = true;

    uint32_t mLiveTexturesMaxCount = 0;

} AppSettings;

/************************************************************************/
// Pixel Coverage pipeline
/************************************************************************/
TFShader*   pShaderPixelCoverage[MSAA_LEVELS_COUNT] = {};
TFPipeline* pPipelinePixelCoverage = NULL;

TFBuffer*   gPixelCoverageBuffers[gDataBufferCount] = { NULL };
/************************************************************************/
// Debug drawing
/************************************************************************/
TFShader*   pShaderLine = nullptr;
TFPipeline* pPipelineLine = nullptr;

/************************************************************************/
// Constants
/************************************************************************/
const uint32_t gShadowMapSize = 1024;
const uint32_t gNumViews = NUM_CULLING_VIEWPORTS;

// The number of render targets to be resolved
FORGE_CONSTEXPR uint32_t gResolveTargetCount = 2;
FORGE_CONSTEXPR uint32_t gResolveGodRayPassIndex = 0;
FORGE_CONSTEXPR uint32_t gResolveFinalPassIndex = 1;

FORGE_CONSTEXPR TinyImageFormat gDebugRTFormat = TinyImageFormat_R8G8B8A8_UNORM;

struct UniformDataSkybox
{
    mat4 mProjectView;
    vec3 mCamPos;
};

struct UniformDataSkyboxTri
{
    mat4 mInverseViewProjection;
};

/************************************************************************/
// Per frame staging data
/************************************************************************/
struct PerFrameData
{
    // Stores the camera/eye position in object space for cluster culling
    vec3                  gEyeObjectSpace[NUM_CULLING_VIEWPORTS] = {};
    PerFrameConstantsData gPerFrameUniformData = {};
    UniformDataSkybox     gUniformDataSky;
    UniformDataSkyboxTri  gUniformDataSkyTri;

    MeshData* pMeshData = NULL;
    mat4      gJointMatrixes[gMaxJointMatrixes] = {};
};

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
TFSemaphore*      pGraphicsAsyncSemaphores[gDataBufferCount] = { NULL };

TFQueue*     pComputeQueue = NULL;
GpuCmdRing   gComputeCmdRing = {};
/************************************************************************/
// Swapchain
/************************************************************************/
TFSwapChain* pSwapChain = NULL;
TFSemaphore* pImageAcquiredSemaphore[gDataBufferCount] = { NULL };

TFDescriptorSet* pDescriptorSetPersistent = { NULL };
TFDescriptorSet* pDescriptorSetPerFrame = { NULL };
TFDescriptorSet* pDescriptorSetDisplayPerDraw = { NULL };
TFDescriptorSet* pDescriptorSetVisibilityPassTransparentPerBatch = { NULL };
TFDescriptorSet* pDescriptorSetResolveVRSPerBatch = { NULL };
TFDescriptorSet* pDescriptorSetClusterLightsPerBatch = { NULL };
TFDescriptorSet* pDescriptorTriangleFilteringPerBatch = { NULL };
/************************************************************************/
// Clear buffers pipeline
/************************************************************************/
TFShader*        pShaderClearBuffers = nullptr;
TFPipeline*      pPipelineClearBuffers = nullptr;
/************************************************************************/
// Pre Skin Vertexes
/************************************************************************/
// Two of each resource for Sync and Async triangle filtering
const uint32_t   PRE_SKIN_SYNC = 0;
const uint32_t   PRE_SKIN_ASYNC = 1;
TFShader*        pShaderPreSkinVertexes[2] = { NULL };
TFPipeline*      pPipelinePreSkinVertexes[2] = { NULL };
TFDescriptorSet* pDescriptorSetPreSkinVertexes[2] = { NULL };
/************************************************************************/
// Triangle filtering pipeline
/************************************************************************/
TFShader*        pShaderTriangleFiltering = nullptr;
TFPipeline*      pPipelineTriangleFiltering = nullptr;

#if defined(TF_ENABLE_WORKGRAPH)
TFShader*    pShaderGpuPipeline = {};
TFPipeline*  pPipelineGpuPipeline = {};
TFWorkgraph* pWorkgraphGpuPipeline = {};
#endif
/************************************************************************/
// Clear OIT Head Index pipeline
/************************************************************************/
TFShader*   pShaderClearHeadIndexOIT = NULL;
TFPipeline* pPipelineClearHeadIndexOIT = NULL;
/************************************************************************/
// Clear light clusters pipeline
/************************************************************************/
TFShader*   pShaderClearLightClusters = nullptr;
TFPipeline* pPipelineClearLightClusters = nullptr;

/************************************************************************/
// Compute light clusters pipeline
/************************************************************************/
TFShader*   pShaderClusterLights = nullptr;
TFPipeline* pPipelineClusterLights = nullptr;
/************************************************************************/
// Shadow pass pipeline
/************************************************************************/
TFShader*   pShaderShadowPass[NUM_GEOMETRY_SETS] = { NULL };
TFPipeline* pPipelineShadowPass[NUM_GEOMETRY_SETS] = { NULL };

/************************************************************************/
// VB pass pipeline
/************************************************************************/
// Extra one for VRS ALPHA_CUTOUT shader
TFShader*        pShaderVisibilityBufferPass[NUM_GEOMETRY_SETS + 1] = {};
TFPipeline*      pPipelineVisibilityBufferPass[NUM_GEOMETRY_SETS] = {};
/************************************************************************/
// VB shade pipeline
/************************************************************************/
// MSAA + VRS + Godray Variants
const uint32_t   kNumVisBufShaderVariants = 4 * (MSAA_LEVELS_COUNT + 1);
TFShader*        pShaderVisibilityBufferShade[kNumVisBufShaderVariants] = { nullptr };
TFPipeline*      pPipelineVisibilityBufferShadeSrgb[2] = { nullptr };
/************************************************************************/
// Resolve pipeline
/************************************************************************/
TFShader*        pShaderResolve[MSAA_LEVELS_COUNT] = { nullptr };
TFPipeline*      pPipelineResolve = nullptr;
TFPipeline*      pPipelineResolveGodRay = nullptr;
TFDescriptorSet* pDescriptorSetResolve = nullptr;

TFTexture*  pSkyboxTri = NULL;
/************************************************************************/
// Godray pipeline
/************************************************************************/
// MSAA + VRS
TFShader*   pGodRayPass[MSAA_LEVELS_COUNT + 1] = { nullptr };
TFPipeline* pPipelineGodRayPass = nullptr;
TFBuffer*   pBufferGodRayConstant = nullptr;

TFShader*        pShaderGodRayBlurPass = nullptr;
TFPipeline*      pPipelineGodRayBlurPass = nullptr;
TFDescriptorSet* pDescriptorSetGodRayBlurPassPerBatch = nullptr;
TFBuffer*        pBufferBlurWeights = nullptr;
TFBuffer*        pGodRayBlurBuffer[BLUR_PASS_TYPE_COUNT][gDataBufferCount] = { { NULL } };

OutputMode         gWasOutputMode = gAppSettings.mOutputMode;
DisplayColorSpace  gWasColorSpace = gAppSettings.mCurrentSwapChainColorSpace;
DisplayColorRange  gWasDisplayColorRange = gAppSettings.mDisplayColorRange;
DisplaySignalRange gWasDisplaySignalRange = gAppSettings.mDisplaySignalRange;

/************************************************************************/
// Present pipeline
/************************************************************************/
TFShader*                         pShaderPresentPass = nullptr;
TFPipeline*                       pPipelinePresentPass = nullptr;
/************************************************************************/
// Filling VRS map pipeline
/************************************************************************/
TFShader*                         pShaderFillStencil = NULL;
TFPipeline*                       pPipelineFillStencil = NULL;
/************************************************************************/
// VRS Resolve pipeline
/************************************************************************/
TFShader*                         pShaderResolveCompute = NULL;
TFPipeline*                       pPipelineResolveCompute = nullptr;
uint32_t                          gShadingRateRootConstantIndex = 0;
/************************************************************************/
// Programmable MSAA resources
/************************************************************************/
TFShader*                         pShaderDrawMSAAEdges[MSAA_LEVELS_COUNT - 1] = { nullptr };
TFShader*                         pShaderDownscaleMSAAEdges[MSAA_LEVELS_COUNT - 1] = { nullptr };
TFPipeline*                       pPipelineDrawMSAAEdges = nullptr;
TFPipeline*                       pPipelineDownscaleMSAAEdges = nullptr;
TFShader*                         pShaderDebugMSAA[MSAA_LEVELS_COUNT - 1] = { nullptr };
TFPipeline*                       pPipelineDebugMSAA = nullptr;
/************************************************************************/
// Render targets
/************************************************************************/
TFRenderTarget*                   pDepthBuffer = NULL;
TFRenderTarget*                   pDepthBufferOIT = NULL;
TFRenderTarget*                   pRenderTargetVBPass = NULL;
TFRenderTarget*                   pRenderTargetMSAA = NULL;
TFRenderTarget*                   pRenderTargetShadow = NULL;
TFRenderTarget*                   pIntermediateRenderTarget = NULL;
TFRenderTarget*                   pRenderTargetGodRay[2] = { NULL };
TFRenderTarget*                   pRenderTargetGodRayMS = NULL;
TFRenderTarget*                   pHistoryRenderTarget[2] = { NULL };
TFRenderTarget*                   pResolveVRSRenderTarget[gDataBufferCount] = {};
TFRenderTarget*                   pDebugVRSRenderTarget = NULL;
TFRenderTarget*                   pRenderTargetMSAAEdges = NULL;
TFRenderTarget*                   pRenderTargetMSAAEdgesDownscaled = NULL;
TFTextureDescriptor*              pDescriptorMSAAStencil = NULL; // stencil-plane SRV for gMSAAStencil binding
TFRenderTarget*                   pRenderTargetDebugMSAA = NULL;
TFRenderTarget*                   pRenderTargetDebugGodRayMSAA = NULL;
/************************************************************************/
// Samplers
/************************************************************************/
TFSampler*                        pSamplerTrilinearAniso = NULL;
TFSampler*                        pSamplerPointClamp = NULL;
// Bindless texture array
/************************************************************************/
size_t                            gAllTexturesCount = 0;
/************************************************************************/
// Vertex buffers for the scene
/************************************************************************/
static TFGeometryBufferLoadDesc   gGeometryBufferLoadDesc;
static uint32_t                   gGeometryBufferVertexBufferCount = 0;
static TFGeometry*                pTroopGeometry = NULL;
static TFGeometryBuffer*          pGeometryBuffer = NULL;
static TFGeometryBufferLayoutDesc gGeometryLayout = {};
#define GET_GEOMETRY_VERTEX_BINDING(semantic) (gGeometryLayout.mSemanticBindings[semantic])
#define GET_GEOMETRY_VERTEX_BUFFER(semantic)  (pGeometryBuffer->mVertex[GET_GEOMETRY_VERTEX_BINDING(semantic)].pBuffer)
/************************************************************************/
// Indirect buffers
/************************************************************************/
TFBuffer* pMaterialPropertyBuffer = NULL;

enum
{
    VB_UB_COMPUTE = 0,
    VB_UB_GRAPHICS,
    VB_UB_COUNT
};
TFBuffer* pPerFrameVBUniformBuffers[VB_UB_COUNT][gDataBufferCount] = {};
TFBuffer* pMeshDataBuffer[gDataBufferCount] = { NULL };
TFBuffer* pJointMatrixBuffer[gDataBufferCount] = { NULL };
TFBuffer* pPreSkinBufferOffsets = NULL; // Offsets when not using AsyncCompute

PreSkinACVertexBuffers* pAsyncComputeVertexBuffers = NULL;

/************************************************************************/
// Other buffers for lighting, point lights,...
/************************************************************************/
TFBuffer*        pLightsBuffer = NULL;
TFBuffer*        pLightClustersCount[gDataBufferCount] = { NULL };
TFBuffer*        pLightClusters[gDataBufferCount] = { NULL };
TFBuffer*        pUniformBufferSky[gDataBufferCount] = { NULL };
TFBuffer*        pUniformBufferSkyTri[gDataBufferCount] = { NULL };
uint64_t         gFrameCount = 0;
VBMeshInstance*  pVBMeshInstances = NULL;
PreSkinContainer gPreSkinContainers[gMaxAnimatedInstances] = {};
TFBufferChunk    gPreSkinGeometryChunks[2] = {}; // Position, Normal
uint32_t         gMaxMeshCount = 0;              // gMainSceneMeshCount + gMaxMeshInstances
uint32_t         gTextureCount = 0;
size_t           gTexMeshCount = 0; // Number of meshes using unique textures

TFUIWindowDesc gGuiWindowDesc;
TFUIWindowDesc gDebugTexturesWindowDesc;
const char*    pGuiOutputSupportsHDRLabel = NULL;

TFFontDrawDesc gFrameTimeDraw;
TFFont*        gFont;

TFUIWindowDesc gHistogramWindowDesc;
bool           gScaleGeometryPlots = false;
TFUIWidget*    pPlotSelector = NULL;
TFUIWidget*    pIndicesPlot = NULL;
TFUIWidget*    pVerticesPlot[TF_MAX_VERTEX_BINDINGS] = {};
typedef struct BufferAllocatorPlotData
{
    float2      mSize = float2(0.f, 0.f);
    const char* pName = NULL;
    int64_t*    pValues = NULL;
} BufferAllocatorPlotData;
BufferAllocatorPlotData mBufferChunkAllocatorPlots[1 + TF_ARRAY_COUNT(pVerticesPlot)];

TFSemaphore* gPrevGraphicsSemaphore = NULL;
TFSemaphore* gComputeSemaphores[gDataBufferCount] = {};

/************************************************************************/
TFICamera* pCamera = NULL;
/************************************************************************/
// CPU staging data
/************************************************************************/
// CPU buffers for light data
LightData  gLightData[LIGHT_COUNT] = {};

PerFrameData    gPerFrame[gDataBufferCount] = {};
TFRenderTarget* pScreenRenderTarget = NULL;

/************************************************************************/
// Order Independent Transparency Info
/************************************************************************/

struct TransparentNodeOIT
{
    uint32_t triangleData;
    uint32_t next;
};

struct GeometryCountOIT
{
    uint32_t count;
};

float gFlagAlphaPurple = 0.6f;
float gFlagAlphaBlue = 0.6f;
float gFlagAlphaGreen = 0.6f;
float gFlagAlphaRed = 0.6f;

TFBuffer* pGeometryCountBufferOIT = NULL;

TFBuffer* pHeadIndexBufferOIT = NULL;
TFBuffer* pVisBufLinkedListBufferOIT = NULL;

// We are rendering the scene (geometry, skybox, ...) at this resolution, UI at window resolution (mSettings.mWidth, mSettings.mHeight)
// Render scene at gSceneRes
// drawFinalImageToSwapchainPass -> The scene rendertarget composed into the swapchain/backbuffer
// Render UI into backbuffer
static TFResolution gSceneRes;

/************************************************************************/
// Screen resolution UI data
/************************************************************************/

const char*      pPipelineCacheName = "PipelineCache.cache";
TFPipelineCache* pPipelineCache = NULL;

static FORGE_CONSTEXPR inline uint32_t IndexTypeToSize(uint32_t type)
{
    ASSERT(type == TF_INDEX_TYPE_UINT16 || type == TF_INDEX_TYPE_UINT32);
    return type == TF_INDEX_TYPE_UINT32 ? sizeof(uint32_t) : sizeof(uint16_t);
}
/************************************************************************/
// world sectors definition and streaming functions
/************************************************************************/
typedef struct WorldSector
{
    TFPackage*      pPackage;
    TFGeometry*     pGeom;
    TFGeometryData* pGeomData;
    int2            mCoords;
    bool            mGenesisSector;
} WorldSector;

TFPackage* gGenesisPackage = NULL;

typedef struct WorldLocation
{
    // Sector coord in an infinite grid, could be +ve or -ve
    int2 mSectorCoord;
    // -1 -1 is bottom left, 1 1 is top right.. the sign indicate the closest neighbour sector index coord by adding to mSectorCoord
    int2 mQuadrants;
} WorldLocation;

const uint32_t gWorldSectorsSizeX = 2;
const uint32_t gWorldSectorsSizeY = 2;
const uint32_t gWorldSectorsCount = gWorldSectorsSizeX * gWorldSectorsSizeY;

WorldLocation gWorldLocation = { int2(0, 0), int2(0, 0) };
WorldSector   gWorldSectors[gWorldSectorsCount];
Vector3       gWorldSectorSize = Vector3(60.0f, 20.0f, 25.0f);

ThreadHandle gWorldStreamingUpdateThread;
TFMutex      gWorldStreamingUpdateMutex;
bool         gWorldStreamingUpdateFinished = false;
bool         gStreamingTexturesChanged[gDataBufferCount];

int firstWorldSectorAvailableIndex()
{
    int result = -1;
    for (uint32_t i = 0; i < gWorldSectorsCount; i++)
    {
        if (gWorldSectors[i].pPackage == NULL)
        {
            result = i;
            break;
        }
    }
    return result;
}

int firstWorldSector(int si, int sj)
{
    int result = -1;
    for (uint32_t i = 0; i < gWorldSectorsCount; i++)
    {
        if ((gWorldSectors[i].mCoords.x == si) && (gWorldSectors[i].mCoords.y == sj))
        {
            result = i;
            break;
        }
    }
    return result;
}

void calculateWorldLocation(const vec3* cameraLocation, WorldLocation* worldLocation)
{
    float fCameraX = cameraLocation->x / SCENE_SCALE;
    float fCameraZ = cameraLocation->z / SCENE_SCALE;
    float fCameraSectorX = fCameraX / (gWorldSectorSize.x) + (0.5f);
    float fCameraSectorZ = fCameraZ / (gWorldSectorSize.z) + (0.5f);
    int   cellIndexX = int(floor(fCameraSectorX));
    int   cellIndexZ = int(floor(fCameraSectorZ));
    worldLocation->mSectorCoord.x = (cellIndexX);
    worldLocation->mSectorCoord.y = (cellIndexZ);
    worldLocation->mQuadrants.x = (((fCameraSectorX - float(cellIndexX)) > 0.5f) ? 1 : -1);
    worldLocation->mQuadrants.y = (((fCameraSectorZ - float(cellIndexZ)) > 0.5f) ? 1 : -1);
}
void updateWorldSectorMeshConstants(WorldSector* pSector)
{
    TFGeometry* part = NULL;
    uint32_t    materialID = 0;
    int         si = pSector->mCoords.x;
    int         sj = pSector->mCoords.y;

    TFPackage* pSharedPackage = NULL;

    for (uint32_t i = 0; i < gWorldSectorsCount; i++)
    {
        if (gWorldSectors[i].pPackage)
        {
            pSharedPackage = gWorldSectors[i].pPackage;
            part = gWorldSectors[i].pGeom;
            break;
        }
        else
        {
            ASSERTMSG(false, "Sector doesn't have a valid Package.");
        }
    }

    mat4 sectorMtx = mat4::identity();
    sectorMtx.setTranslation(Vector3(gWorldSectorSize.x * si, 0.0f, gWorldSectorSize.z * sj));
    mat4 sectorInvMtx = inverse(sectorMtx);

    // Should fall between 0 and max sectors - 1
    int64_t sectorIndex = pSector - &gWorldSectors[0];
    ASSERT(sectorIndex >= 0);
    ASSERT(sectorIndex < gWorldSectorsCount);
    uint32_t meshCount = part->mDrawArgCount * (uint32_t)sectorIndex;

    // Extract the offset from the global vertex buffer only if geometry is loaded, otherwise
    // this sector is not loaded yet and should not render
    uint32_t globalVertexOffset = (pSector->pGeom->mVertexBufferChunks[0].mOffset) / sizeof(float3);

    for (uint32_t di = 0; di < part->mDrawArgCount; ++di)
    {
        uint32_t flag = 0;
        if (pSharedPackage)
        {
            flag = pSharedPackage->pTextureMetadata->pMaterialProps[di].mFlags;
        }
        uint32_t materialId = materialID++;
        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            gPerFrame[i].pMeshData[meshCount].indexOffset =
                part->mIndexBufferChunk.mOffset / IndexTypeToSize(part->mIndexType) + part->pDrawArgs[di].mStartIndex;
            gPerFrame[i].pMeshData[meshCount].vertexOffset =
                part->mVertexBufferChunks[0].mOffset / part->mVertexStrides[0] + part->pDrawArgs[di].mVertexOffset;
            gPerFrame[i].pMeshData[meshCount].vertexOffset += globalVertexOffset;
            gPerFrame[i].pMeshData[meshCount].modelMtx = sectorMtx;
            gPerFrame[i].pMeshData[meshCount].invModelMtx = sectorInvMtx;
            gPerFrame[i].pMeshData[meshCount].prevModelMtx = sectorMtx;
            gPerFrame[i].pMeshData[meshCount].preSkinnedVertexOffset = PRE_SKINNED_VERTEX_OFFSET_NONE;
            gPerFrame[i].pMeshData[meshCount].indirectVertexOffset = globalVertexOffset;

            gPerFrame[i].pMeshData[meshCount].materialID_flags =
                ((flag & FLAG_MASK) << FLAG_LOW_BIT) | ((materialId & MATERIAL_ID_MASK) << MATERIAL_ID_LOW_BIT);
            gPerFrame[i].pMeshData[meshCount].specEmissiveStrength =
                pSharedPackage->pTextureMetadata->pMaterialProps[di].mSpecEmissiveStrength;
            gPerFrame[i].pMeshData[meshCount].baseColor = pSharedPackage->pTextureMetadata->pMaterialProps[di].mBaseColor;
        }

        uint32_t geomSet = GEOMSET_OPAQUE;
        if (flag & MATERIAL_FLAG_ALPHA_TESTED)
            geomSet = GEOMSET_ALPHA_CUTOUT;
        if (flag & MATERIAL_FLAG_TRANSPARENT)
            geomSet = GEOMSET_ALPHA_BLEND;

        pVBMeshInstances[meshCount].mGeometrySet = geomSet;
        pVBMeshInstances[meshCount].mMeshIndex = meshCount;
        pVBMeshInstances[meshCount].mTriangleCount = part->pDrawArgs[di].mIndexCount / 3;
        pVBMeshInstances[meshCount].mInstanceIndex = 0;
        ++meshCount;
    }
}

void onWorldSectorLoaded(const TFOnGeometryDataLoadedData* geometryData)
{
    WorldSector* pSector = (WorldSector*)geometryData->pUserData;
    if (pSector)
    {
        /// Alter y of vertex position to visually show these are not the same
        float3* pvPositions = (float3*)geometryData->pVertexPositions;
        float   raisedUp = (pSector->mCoords.x * 5.0f) + (pSector->mCoords.y * 5.0f);
        for (uint32_t ii = 0; ii < geometryData->geom->mVertexCount; ii++)
        {
            pvPositions[ii].y += raisedUp;
        }
    }
}

void loadWorldSector(int si, int sj, bool genesisSector)
{
    TFPackage* pPackage = NULL;

    int iSectorIndex = firstWorldSectorAvailableIndex();
    if (iSectorIndex >= 0)
    {
        gWorldSectors[iSectorIndex].mCoords = int2(si, sj);
        gWorldSectors[iSectorIndex].mGenesisSector = genesisSector;
        bool pakLoaded = false;

        TFPackageLoadDesc packageLoadDesc = {};
        packageLoadDesc.loadGeoData = false;
        packageLoadDesc.loadTexData = false;
        packageLoadDesc.pGeometryBuffer = pGeometryBuffer;
        packageLoadDesc.pGeometryBufferLayout = &gGeometryLayout;
        packageLoadDesc.pOnGeometryLoaded = onWorldSectorLoaded;
        packageLoadDesc.pOnGeometryLoadedUserData = &(gWorldSectors[iSectorIndex]);
        packageLoadDesc.ppOutPackage = &pPackage;
        packageLoadDesc.packageName = "SanMiguelPak.buny";
        pakLoaded = addResourcesFromPackage(&packageLoadDesc, gNullTextureResource);

        if (!pakLoaded)
        {
            LOGF(LogLevel::eERROR, "Failed to Load package for sector- %d:%d", si, sj);
        }
        else
        {
            gWorldSectors[iSectorIndex].pPackage = pPackage;

            const uint32_t      sceneGeoIndex = 0;
            TFGeometryLoadDesc* pLoadDesc = &pPackage->pGeoData[sceneGeoIndex];
            pLoadDesc->ppGeometry = &gWorldSectors[iSectorIndex].pGeom;
            pLoadDesc->ppGeometryData = &gWorldSectors[iSectorIndex].pGeomData;
            TFSyncToken waitToken = {};
            addResource(pLoadDesc, &waitToken);
            waitForToken(&waitToken);
        }
    }
}

void unloadWorldSector(int si, int sj)
{
    int sectorIndex = firstWorldSector(si, sj);
    if (sectorIndex > -1)
    {
        WorldSector* sector = &gWorldSectors[sectorIndex];
        TFPackage*   pPackage = sector->pPackage;
        if (pPackage)
        {
            if (sector->pGeom)
            {
                removeResource(sector->pGeom);
                sector->pGeom = NULL;
            }
            if (sector->pGeomData)
            {
                removeResource(sector->pGeomData);
                sector->pGeomData = NULL;
            }

            if (!sector->mGenesisSector)
            {
                removeResourcesFromPackage(pPackage);
            }
            sector->pPackage = NULL;
        }
    }
}

#ifdef ENABLE_STREAMING
static void worldStreamingUpdateThreadFunc(void*)
{
    acquireMutex(&gWorldStreamingUpdateMutex);
    int32_t baseIndexI = gWorldLocation.mSectorCoord.x;
    int32_t baseIndexJ = gWorldLocation.mSectorCoord.y;

    int32_t nearestIndexI = gWorldLocation.mSectorCoord.x + gWorldLocation.mQuadrants.x;
    int32_t nearestIndexJ = gWorldLocation.mSectorCoord.y + gWorldLocation.mQuadrants.y;

    // Loop on the live ones, any sector not using these indices should be unloaded
    for (uint32_t i = 0; i < gWorldSectorsCount; i++)
    {
        WorldSector* currSector = gWorldSectors + i;
        int2         currCoords = currSector->mCoords;
        if (((currCoords.x != baseIndexI) && (currCoords.x != nearestIndexI)) ||
            ((currCoords.y != baseIndexJ) && (currCoords.y != nearestIndexJ)))
        {
            unloadWorldSector(currCoords.x, currCoords.y);
        }
    }

    int iFirst = firstWorldSector(baseIndexI, baseIndexJ);
    if (iFirst == -1)
    {
        loadWorldSector(baseIndexI, baseIndexJ, false);
    }
    int iSecond = firstWorldSector(nearestIndexI, baseIndexJ);
    if (iSecond == -1)
    {
        loadWorldSector(nearestIndexI, baseIndexJ, false);
    }
    int iThird = firstWorldSector(baseIndexI, nearestIndexJ);
    if (iThird == -1)
    {
        loadWorldSector(baseIndexI, nearestIndexJ, false);
    }
    int iFourth = firstWorldSector(nearestIndexI, nearestIndexJ);
    if (iFourth == -1)
    {
        loadWorldSector(nearestIndexI, nearestIndexJ, false);
    }
    for (uint32_t i = 0; i < gWorldSectorsCount; i++)
    {
        updateWorldSectorMeshConstants(&gWorldSectors[i]);
    }
    gWorldStreamingUpdateFinished = true;
    releaseMutex(&gWorldStreamingUpdateMutex);
}
#endif // ENABLE_STREAMING

/************************************************************************/
// App implementation
/************************************************************************/
class VisibilityBufferOIT: public IApp
{
public:
    bool Init()
    {
        // When we change RenderAPI we need to reset these because otherwise we won't populate the PreSkin data in the vertex buffers
        // buffers
        gAppSettings.mUpdateSimulation = true;
        gAppSettings.mHoldFilteredResults = false;

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
        // Check for init success
        if (!pRenderer)
        {
            ShowUnsupportedMessage(getUnsupportedGPUMsg());
            return false;
        }
        setGPUConfig(pRenderer->pContext->mGpus, pRenderer->pContext->mGpuCount, (uint32_t)(pRenderer->pGpu - pRenderer->pContext->mGpus),
                     settings.pExtendedSettings);
        mSettings.mFrameMaxCount = gDataBufferCount;

        // Turn off by default depending on GPU config rules
        gAppSettings.mEnableVRS &= pRenderer->pGpu->mSoftwareVRSSupported && (gGpuSettings.mMaxMSAALevel >= TF_SAMPLE_COUNT_4);
        gAppSettings.mEnableGodray &= !gGpuSettings.mDisableGodRays;
        gAppSettings.mMaxMSAALevel = (TFSampleCount)gGpuSettings.mMaxMSAALevel;
        if (gAppSettings.mEnableVRS)
        {
            gAppSettings.mMSAALevel = TF_SAMPLE_COUNT_4;
        }
        else
        {
            gAppSettings.mMSAALevel =
                (TFSampleCount)clamp(gGpuSettings.mMSAASampleCount, (uint32_t)TF_SAMPLE_COUNT_1, (uint32_t)gAppSettings.mMaxMSAALevel);
        }
        gAppSettings.mMSAAIndex = (uint32_t)log2((uint32_t)gAppSettings.mMSAALevel);
        gAppSettings.mMSAAIndexRequested = gAppSettings.mMSAAIndex;
#if defined(TF_ENABLE_WORKGRAPH)
        gAppSettings.mGpuPipelineWorkgraph &= pRenderer->pGpu->mWorkgraphSupported;
#endif
        gDivider = gAppSettings.mEnableVRS ? 2 : 1;
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

        GpuCmdRingDesc cmdRingDesc = {};
        cmdRingDesc.pQueue = pGraphicsQueue;
        cmdRingDesc.mPoolCount = gDataBufferCount;
        cmdRingDesc.mCmdPerPoolCount = 1;
        cmdRingDesc.mAddSyncPrimitives = true;
        initGpuCmdRing(pRenderer, &cmdRingDesc, &gGraphicsCmdRing);

        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            initSemaphore(pRenderer, &pGraphicsAsyncSemaphores[i]);
        }

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
        TFSamplerDesc pointDesc = { TF_FILTER_NEAREST,
                                    TF_FILTER_NEAREST,
                                    TF_MIPMAP_MODE_NEAREST,
                                    TF_ADDRESS_MODE_CLAMP_TO_EDGE,
                                    TF_ADDRESS_MODE_CLAMP_TO_EDGE,
                                    TF_ADDRESS_MODE_CLAMP_TO_EDGE };

        addSampler(pRenderer, &trilinearDesc, &pSamplerTrilinearAniso);
        addSampler(pRenderer, &pointDesc, &pSamplerPointClamp);

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
        skyboxTriDesc.ppTexture = &pSkyboxTri;
        addResource(&skyboxTriDesc, NULL);
        /************************************************************************/
        // Load the scene using the SceneLoader class
        /************************************************************************/
        TFHiresTimer sceneLoadTimer;
        initHiresTimer(&sceneLoadTimer);

        /************************************************************************/
        // Allocate Visibility Buffer vertex buffers
        /************************************************************************/

        // TODO: Remove the magic values.
        //       These magic values are the total values of adding SanMiguel geometry to Stormtrooper geometry.
        //       This shouldn't be part of the App, the geometry should be packed by the AssetPipeline and generate a
        //       manifest with this information so that then we can load it and allocate the memory based on that.
        //       This would make the App data driven instead of hardcoding these values.
        const uint32_t mainSceneIndexCount = 8881594 * gWorldSectorsCount;
        const uint32_t mainSceneVertexCount = 3924717 * gWorldSectorsCount;

        const uint32_t stormtrooperIndexCount = 19554;
        const uint32_t stormtrooperVertexCount = 5860;

        const uint32_t visbuffMaxIndexCount = mainSceneIndexCount + stormtrooperIndexCount;

        const uint32_t visbuffMaxStaticVertexCount = mainSceneVertexCount + stormtrooperVertexCount;
        const uint32_t visBuffAnimatedVertexCount = stormtrooperVertexCount;
        gPreSkinnedVertexCountPerFrame = stormtrooperVertexCount * gMaxAnimatedInstances;

        // We need to create a geometry object that will contain all the data for all the geometry in the scene.
        // This includes Static geometry whose vertexes are already in world space and object instances whose
        // vertexes are in model space and we'll multiply by the model matrix in the shader.

        // Allocate PreSkin vertex buffers, this includes a ResourceHeap for skinned geometry and aliased PreSkin output buffers
        // to be able to run AsyncCompute. If an App doesn't use AsyncCompute triangle filtering this step can be skipped.
        {
            PreSkinACVertexBuffersDesc preSkinDesc = {};
            preSkinDesc.mNumBuffers = gDataBufferCount;
            preSkinDesc.mMaxStaticVertexCount = visbuffMaxStaticVertexCount;
            preSkinDesc.mMaxPreSkinnedVertexCountPerFrame = gPreSkinnedVertexCountPerFrame;

            initVBAsyncComputePreSkinVertexBuffers(pRenderer, &preSkinDesc, &pAsyncComputeVertexBuffers);
        }

        // Allocate the TFGeometryBuffer object that will contain all the geometry in the scene and additional space to store
        // pre-skinned vertex attributes
        {
            gGeometryLayout.mSemanticBindings[TF_SEMANTIC_POSITION] = 0;
            gGeometryLayout.mSemanticBindings[TF_SEMANTIC_TEXCOORD0] = 1;
            gGeometryLayout.mSemanticBindings[TF_SEMANTIC_NORMAL] = 2;
            gGeometryLayout.mSemanticBindings[TF_SEMANTIC_WEIGHTS] = 3;
            gGeometryLayout.mSemanticBindings[TF_SEMANTIC_JOINTS] = 4;

            gGeometryLayout.mVerticesStrides[0] = sizeof(float3);
            gGeometryLayout.mVerticesStrides[1] = sizeof(uint32_t);
            gGeometryLayout.mVerticesStrides[2] = sizeof(uint32_t);
            gGeometryLayout.mVerticesStrides[3] = sizeof(float4);
            gGeometryLayout.mVerticesStrides[4] = sizeof(uint16_t[4]);

            TFGeometryBufferLoadDesc geometryBufferLoadDesc = {};
            geometryBufferLoadDesc.mStartState = TF_RESOURCE_STATE_COPY_DEST;
            geometryBufferLoadDesc.pOutGeometryBuffer = &pGeometryBuffer;

            geometryBufferLoadDesc.mIndicesSize = sizeof(uint32_t) * visbuffMaxIndexCount;
            geometryBufferLoadDesc.mVerticesSizes[0] = (uint32_t)pAsyncComputeVertexBuffers->mPositions.mVBSize; // Position
            geometryBufferLoadDesc.mVerticesSizes[1] = sizeof(uint32_t) * visbuffMaxStaticVertexCount;           // UV
            geometryBufferLoadDesc.mVerticesSizes[2] = (uint32_t)pAsyncComputeVertexBuffers->mNormals.mVBSize;   // Normal
            geometryBufferLoadDesc.mVerticesSizes[3] = sizeof(float4) * visBuffAnimatedVertexCount;              // Weights
            geometryBufferLoadDesc.mVerticesSizes[4] = sizeof(uint16_t[4]) * visBuffAnimatedVertexCount;         // Joints

            geometryBufferLoadDesc.pNameIndexBuffer = "Geometry Indices";
            geometryBufferLoadDesc.pNamesVertexBuffers[0] = "Geometry Positions";
            geometryBufferLoadDesc.pNamesVertexBuffers[1] = "Geometry UVs";
            geometryBufferLoadDesc.pNamesVertexBuffers[2] = "Geometry Normals";
            geometryBufferLoadDesc.pNamesVertexBuffers[3] = "Geometry Weights";
            geometryBufferLoadDesc.pNamesVertexBuffers[4] = "Geometry Joints";

            // Skinned attributes need to be allocated in the PreSkinACVertexBuffers
            geometryBufferLoadDesc.pVerticesPlacements[0] = &pAsyncComputeVertexBuffers->mPositions.mVBPlacement;
            geometryBufferLoadDesc.pVerticesPlacements[2] = &pAsyncComputeVertexBuffers->mNormals.mVBPlacement;

            gGeometryBufferLoadDesc = geometryBufferLoadDesc;
            addGeometryBuffer(&geometryBufferLoadDesc);
        }

        // Allocate buffer parts for preskinned data, this lets the TFBufferChunkAllocator know we are using this memory
        // TODO: We need to make sure this matches with the Aliased Buffers we allocate in addVBAsyncComputePreSkinVertexBuffers
        // TODO: Move this to internal VisibilityBuffer interface?
        {
            const uint32_t preSkinRequiredVertexes = gPreSkinnedVertexCountPerFrame * gDataBufferCount;

            TFBufferChunk requestedChunk = { (uint32_t)pAsyncComputeVertexBuffers->mPositions.mOutputMemoryStartOffset,
                                             (uint32_t)pAsyncComputeVertexBuffers->mPositions.mOutputMemorySize };
            uint32_t      binding = gGeometryLayout.mSemanticBindings[TF_SEMANTIC_POSITION];
            uint32_t      vertexStride = gGeometryLayout.mVerticesStrides[binding];
            addGeometryBufferPart(&pGeometryBuffer->mVertex[binding], preSkinRequiredVertexes * vertexStride, vertexStride,
                                  &gPreSkinGeometryChunks[0], &requestedChunk);

            requestedChunk = { (uint32_t)pAsyncComputeVertexBuffers->mNormals.mOutputMemoryStartOffset,
                               (uint32_t)pAsyncComputeVertexBuffers->mNormals.mOutputMemorySize };
            binding = gGeometryLayout.mSemanticBindings[TF_SEMANTIC_NORMAL];
            vertexStride = gGeometryLayout.mVerticesStrides[binding];
            addGeometryBufferPart(&pGeometryBuffer->mVertex[binding], preSkinRequiredVertexes * vertexStride, vertexStride,
                                  &gPreSkinGeometryChunks[1], &requestedChunk);

            // Write memory for Pre-Skinned vertexes starts after main scene memory
            gPreSkinnedVertexStartOffset = gPreSkinGeometryChunks[0].mOffset / gGeometryLayout.mVerticesStrides[0];
            ASSERT(gPreSkinnedVertexStartOffset == (gPreSkinGeometryChunks[1].mOffset / gGeometryLayout.mVerticesStrides[1]) &&
                   "PreSkin output vertex offset needs to be the same for all attributes: Position, Normal");

            PreSkinBufferOffsets offsets = {};
            offsets.vertexOffset = (uint32_t)gPreSkinnedVertexStartOffset;

            TFBufferLoadDesc desc = {};
            desc.pData = &offsets;
            desc.ppBuffer = &pPreSkinBufferOffsets;
            desc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_BUFFER | TF_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            desc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_GPU_ONLY;
            desc.mDesc.mElementCount = 1;
            desc.mDesc.mStructStride = sizeof(PreSkinBufferOffsets);
            desc.mDesc.mSize = desc.mDesc.mElementCount * desc.mDesc.mStructStride;
            desc.mDesc.pName = "PreSkinShaderBufferOffsets";
            addResource(&desc, NULL);
        }

        // Main Scene
        gSceneMeshCount = 0;
        initMutex(&gWorldStreamingUpdateMutex);

        gWorldSectors[0].mCoords = int2(0, 0);
        gWorldSectors[0].mGenesisSector = true;
        gWorldSectors[0].pGeom = NULL;
        gWorldSectors[0].pGeomData = NULL;

        {
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
            addResource(&loadDesc, NULL);
        }

        TFPackageLoadDesc packageLoadDesc = {};
        packageLoadDesc.loadGeoData = false;
#ifdef ENABLE_STREAMING
        packageLoadDesc.loadTexData = false;
#else
        packageLoadDesc.loadTexData = true;
#endif // ENABLE_STREAMING
        packageLoadDesc.pGeometryBuffer = pGeometryBuffer;
        packageLoadDesc.pGeometryBufferLayout = &gGeometryLayout;
        packageLoadDesc.pOnGeometryLoaded = onWorldSectorLoaded;
        packageLoadDesc.pOnGeometryLoadedUserData = &(gWorldSectors[0]);
        packageLoadDesc.ppOutPackage = &gGenesisPackage;
        packageLoadDesc.packageName = "SanMiguelPak.buny";
        if (!addResourcesFromPackage(&packageLoadDesc, gNullTextureResource))
        {
            LOGF(LogLevel::eERROR, "Failed to Load package for genesis sector");
            return false;
        }
        gWorldSectors[0].pPackage = gGenesisPackage;
        gSceneMeshCount += (uint32_t)gWorldSectors[0].pPackage->pTextureMetadata[0].mMeshCount;

        TFSyncToken waitToken = {};

        const uint32_t      sceneGeoIndex = 0;
        TFGeometryLoadDesc* pLoadDesc = &gGenesisPackage->pGeoData[sceneGeoIndex];
        pLoadDesc->ppGeometry = &gWorldSectors[0].pGeom;
        pLoadDesc->ppGeometryData = &gWorldSectors[0].pGeomData;
        addResource(pLoadDesc, &waitToken);
        waitForToken(&waitToken);

        int sectorIndex = 1;
        for (uint32_t j = 0; j < gWorldSectorsSizeY; j++)
        {
            for (uint32_t i = 0; i < gWorldSectorsSizeX; i++)
            {
                if (i == 0 && j == 0)
                    continue;
                gWorldSectors[sectorIndex].mCoords = int2(i, j);
                // Only the first one needs to be used for the debug bound boxes
                gWorldSectors[sectorIndex].mGenesisSector = (i == 0 && j == 0);
                gWorldSectors[sectorIndex].pGeom = NULL;
                gWorldSectors[sectorIndex].pGeomData = NULL;
                loadWorldSector(i, j, (i == 0 && j == 0));
                gSceneMeshCount += (uint32_t)gWorldSectors[sectorIndex].pPackage->pTextureMetadata[0].mMeshCount;
                sectorIndex++;
            }
        }
        TFGeometry* pSanMiguelGeometry = gWorldSectors[0].pGeom;

#ifdef ENABLE_STREAMING
        onStreamingGeometryLoaded(gGenesisPackage);
#endif

        LOGF(LogLevel::eINFO, "Load scene : %f ms", getHiresTimerUSec(&sceneLoadTimer, true) / 1000.0f);
        gMaxMeshCount = gSceneMeshCount + gMaxMeshInstances;

        for (uint32_t geoIndex = 0; geoIndex < gGenesisPackage->pPackageMetadata->mGeoCount; geoIndex++)
        {
            gTextureCount += (uint32_t)gGenesisPackage->pTextureMetadata[geoIndex].mTextureCount;
        }

        ASSERT(MAX_TEXTURE_UNITS >= gTextureCount && "Not enough textures slots in the shader to bind all materials");

        const uint32_t troopGeoIndex = 1;
        pLoadDesc = &gGenesisPackage->pGeoData[troopGeoIndex];
        pLoadDesc->ppGeometry = &pTroopGeometry;
        pLoadDesc->ppGeometryData = &pAnimatedMeshGeomData;
        waitToken = {};
        addResource(pLoadDesc, &waitToken);
        waitForToken(&waitToken);

        /************************************************************************/
        // Texture loading
        /************************************************************************/
        for (uint32_t geoIndex = 0; geoIndex < gGenesisPackage->pPackageMetadata->mGeoCount; geoIndex++)
        {
            gTexMeshCount += gGenesisPackage->pTextureMetadata[geoIndex].mMeshCount;
        }

        gAllTexturesCount = gTexMeshCount * TEXTURES_PER_MESH;

#ifdef ENABLE_STREAMING
        gAppSettings.mLiveTexturesMaxCount = gTextureCount;
        TFStreamingDesc streamingDesc = {};
        streamingDesc.mTotalTextureCount = (uint32_t)gAllTexturesCount;
        streamingDesc.mLiveCountMax = gTextureCount;
        streamingDesc.mRemovalDelay = gDataBufferCount + 1;
        streamingDesc.pRenderer = pRenderer;
        // TODO : each platform should be separately investigated what is the optimal heap size
#ifdef METAL
        streamingDesc.mTexturesHeapSize = 512 * 1024 * 1024;
#else
        streamingDesc.mTexturesHeapSize = 256 * 1024 * 1024;
#endif

        initStreamingInterface(&streamingDesc);
        for (uint32_t i = 0; i < gDataBufferCount; i++)
        {
            gStreamingTexturesChanged[i] = false;
        }
#endif // ENABLE_STREAMING

        /************************************************************************/
        // Init visibility buffer
        /************************************************************************/

        // We need to allocate enough indices for the entire static scene + mesh instances + animated instances
        uint32_t visibilityBufferFilteredIndexCount[NUM_GEOMETRY_SETS] = {};
        pVBMeshInstances = (VBMeshInstance*)tf_calloc(gMaxMeshCount, sizeof(VBMeshInstance));

        TFHiresTimer vbSetupTimer;
        initHiresTimer(&vbSetupTimer);

        // Get indices count per geomset
        for (uint32_t meshIndex = 0; meshIndex < pSanMiguelGeometry->mDrawArgCount; ++meshIndex)
        {
            TFMaterialFlags materialFlags = gGenesisPackage->pTextureMetadata[0].pMaterialProps[meshIndex].mFlags;

            uint32_t geomSet = GEOMSET_OPAQUE;
            if (materialFlags & MATERIAL_FLAG_ALPHA_TESTED)
                geomSet = GEOMSET_ALPHA_CUTOUT;
            if (materialFlags & MATERIAL_FLAG_TRANSPARENT)
            {
                geomSet = GEOMSET_ALPHA_BLEND;
                arrpush(gTransparentMaterialIDs, meshIndex);
            }

            visibilityBufferFilteredIndexCount[geomSet] += (pSanMiguelGeometry->pDrawArgs + meshIndex)->mIndexCount * gWorldSectorsCount;
        }

        // Add  dynamic instances
        for (uint32_t i = 0; i < gStaticMeshInstanceCount; ++i)
        {
            visibilityBufferFilteredIndexCount[gStaticMeshInstances[i].mGeomSet] += pTroopGeometry->pDrawArgs->mIndexCount;
        }
        /************************************************************************/
        // Load animation data
        /************************************************************************/
        gRig.Initialize(TF_RD_ANIMATIONS, gSkeletonFile);

        gClip.Initialize(TF_RD_ANIMATIONS, gClipFile, &gRig);

        uint32_t currJointMatrixOffset = 0;
        uint32_t currPreSkinnedOutputVertex = 0;
        for (uint32_t i = 0; i < gAnimatedMeshInstanceCount; ++i)
        {
            AnimatedMeshInstance* am = &gAnimatedMeshInstances[i];

            const TFGeometryData* pMeshGeomData = pAnimatedMeshGeomData;

            // Instance initialization
            {
                am->mClipController.Initialize(gClip.GetDuration());
                am->mClipController.mPlaybackSpeed = am->mPlaybackSpeed;

                AnimationDesc animationDesc{};
                animationDesc.mRig = &gRig;
                animationDesc.mNumLayers = 1;
                animationDesc.mLayerProperties[0].mClip = &gClip;
                animationDesc.mLayerProperties[0].mClipController = &am->mClipController;
                am->mAnimation.Initialize(animationDesc);
                am->mAnimObject.Initialize(&gRig, &am->mAnimation);

                am->pGeomData = pMeshGeomData;
                am->pBoneMatrixes = (mat4*)tf_calloc(pMeshGeomData->mJointCount, sizeof(mat4));

                am->mJointMatrixOffset = currJointMatrixOffset;

                am->mPreSkinnedVertexOffset = currPreSkinnedOutputVertex;
            }

            // Pre skin container (input to vertex pre skinning stage)
            {
                // We fill PreSkinContainers on Init because our scene is constant (we don't add/remove objects from the scene)
                // In a dynamic scenario these containers could be filled/updated per frame
                gPreSkinContainers[i].mVertexCount = pTroopGeometry->mVertexCount;

                const uint32_t posBinding = GET_GEOMETRY_VERTEX_BINDING(TF_SEMANTIC_POSITION);
                const uint32_t jointBinding = GET_GEOMETRY_VERTEX_BINDING(TF_SEMANTIC_JOINTS);
                gPreSkinContainers[i].mVertexPositionOffset =
                    pTroopGeometry->mVertexBufferChunks[posBinding].mOffset / pTroopGeometry->mVertexStrides[posBinding];
                gPreSkinContainers[i].mJointOffset =
                    pTroopGeometry->mVertexBufferChunks[jointBinding].mOffset / pTroopGeometry->mVertexStrides[jointBinding];
                gPreSkinContainers[i].mJointMatrixOffset = currJointMatrixOffset;

                gPreSkinContainers[i].mOutputVertexOffset = currPreSkinnedOutputVertex;
            }
            // TODO: If we want to support AlphaCutout and AlphaBlend we need to modify the shaders to get the correct UVs.
            //            We have the problem of needing texture UVs that are not animated, currently Shadow shaders take Position and
            //            UVs as input, this is fine as long as the mesh is animated because index of position is the same as UVs, but
            //            with animated objects the animated positions are stored at a different index than the UVs.
            ASSERT(gAnimatedMeshInstances[i].mGeomSet == GEOMSET_OPAQUE);

            currPreSkinnedOutputVertex += pTroopGeometry->mVertexCount;
            currJointMatrixOffset += pMeshGeomData->mJointCount;
            // Count total indices per geom set
            {
                visibilityBufferFilteredIndexCount[gAnimatedMeshInstances[i].mGeomSet] += pTroopGeometry->pDrawArgs->mIndexCount;
            }
        }

        // Init visibility buffer
        VisibilityBufferDesc vbDesc = {};
        vbDesc.mNumFrames = gDataBufferCount;
        vbDesc.mNumBuffers = gDataBufferCount;
        vbDesc.mNumGeometrySets = NUM_GEOMETRY_SETS;
        vbDesc.pMaxIndexCountPerGeomSet = visibilityBufferFilteredIndexCount;
        vbDesc.mNumViews = NUM_CULLING_VIEWPORTS;
        vbDesc.mComputeThreads = VB_COMPUTE_THREADS;
        // PreSkin Pass
        vbDesc.mEnablePreSkinPass = true;
        vbDesc.mPreSkinBatchSize = SKIN_BATCH_SIZE;
        vbDesc.mPreSkinBatchCount = SKIN_BATCH_COUNT;
        initVisibilityBuffer(pRenderer, &vbDesc, &pVisibilityBuffer);

        LOGF(LogLevel::eINFO, "Setup vb : %f ms", getHiresTimerUSec(&vbSetupTimer, true) / 1000.0f);

        // Default NX settings for better performance.
#if NX64
        // Async compute is not optimal on the NX platform. Turning this off to make use of default graphics queue for triangle visibility.
        gAppSettings.mAsyncCompute = false;
        // High fill rate features are also disabled by default for performance.
        gAppSettings.mEnableGodray = false;
#endif

        /************************************************************************/
        // MSAA Settings
        /************************************************************************/
        /************************************************************************/
        /************************************************************************/
        // Finish the resource loading process since the next code depends on the loaded resources
        waitForAllResourceLoads();

        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            gPerFrame[i].pMeshData = (MeshData*)tf_malloc(gMaxMeshCount * sizeof(MeshData));
        }
        TFHiresTimer setupBuffersTimer;
        initHiresTimer(&setupBuffersTimer);
        addTriangleFilteringBuffers();

        addPixelCoverageBuffers((uint32_t)gTexMeshCount);

        UpdateVBMeshFilterGroupsDesc updateVBMeshFilterGroupsDesc = {};
        updateVBMeshFilterGroupsDesc.mNumMeshInstance = gMaxMeshCount;
        updateVBMeshFilterGroupsDesc.pVBMeshInstances = pVBMeshInstances;
        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            updateVBMeshFilterGroupsDesc.mFrameIndex = i;
            gVBPreFilterStats[i] = updateVBMeshFilterGroups(pVisibilityBuffer, &updateVBMeshFilterGroupsDesc);
        }

        LOGF(LogLevel::eINFO, "Setup buffers : %f ms", getHiresTimerUSec(&setupBuffersTimer, true) / 1000.0f);

        LOGF(LogLevel::eINFO, "Total Load Time : %f ms", getHiresTimerUSec(&totalTimer, true) / 1000.0f);

        /************************************************************************/
        // Setup the fps camera for navigating through the scene
        /************************************************************************/
        vec3                     startPosition(136.9f, 38.8f, 22.8f);
        vec3                     startLookAt = startPosition + vec3(-0.2f, 0.04f, 0.0f);
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
        exitCamera(pCamera);

        tf_free(gCameraPathData);

        removeResource(gNullTextureResource);
        removeResource(pBufferGodRayConstant);

        for (uint32_t passIdx = 0; passIdx < BLUR_PASS_TYPE_COUNT; ++passIdx)
        {
            for (uint32_t frameIdx = 0; frameIdx < gDataBufferCount; ++frameIdx)
            {
                removeResource(pGodRayBlurBuffer[passIdx][frameIdx]);
            }
        }
        removeResource(pBufferBlurWeights);

        removeResource(pSkyboxTri);
        removePixelCoverageBuffers();
        removeTriangleFilteringBuffers();

        exitProfiler();

        exitUserInterface();
        tf_free(mBufferChunkAllocatorPlots[0].pValues);
        for (uint32_t i = 0; i < TF_MAX_VERTEX_BINDINGS; ++i)
        {
            tf_free(mBufferChunkAllocatorPlots[1 + i].pValues);
        }

        removeFont(gFont);
        exitFontSystem();

        /************************************************************************/
        // Remove loaded scene
        /************************************************************************/
        for (uint32_t i = 0; i < gWorldSectorsCount; i++)
        {
            TFPackage* pPackage = gWorldSectors[i].pPackage;
            if (pPackage)
            {
                removeResource(gWorldSectors[i].pGeom);
                removeResource(gWorldSectors[i].pGeomData);
                removeResourcesFromPackage(pPackage);
            }
        }
        exitMutex(&gWorldStreamingUpdateMutex);

        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            tf_free(gPerFrame[i].pMeshData);
        }

        removeResource(pTroopGeometry);
        pTroopGeometry = nullptr;

        exitVBAsyncComputePreSkinVertexBuffers(pRenderer, pAsyncComputeVertexBuffers);

        removeResource(pAnimatedMeshGeomData);
        pAnimatedMeshGeomData = nullptr;

        removeResource(pPreSkinBufferOffsets);

        removeGeometryBufferPart(&pGeometryBuffer->mVertex[GET_GEOMETRY_VERTEX_BINDING(TF_SEMANTIC_POSITION)], &gPreSkinGeometryChunks[0]);
        removeGeometryBufferPart(&pGeometryBuffer->mVertex[GET_GEOMETRY_VERTEX_BINDING(TF_SEMANTIC_NORMAL)], &gPreSkinGeometryChunks[1]);

        removeGeometryBuffer(pGeometryBuffer);
        pGeometryBuffer = nullptr;

        for (uint32_t i = 0; i < gAnimatedMeshInstanceCount; ++i)
        {
            AnimatedMeshInstance* am = &gAnimatedMeshInstances[i];

            am->mAnimObject.Exit();
            am->mAnimation.Exit();
            tf_free(am->pBoneMatrixes);
            am->pBoneMatrixes = nullptr;
        }

        gClip.Exit();
        gRig.Exit();

        arrfree(gTransparentMaterialIDs);
#ifdef ENABLE_STREAMING

        exitStreamingInterface();

#endif

        tf_free(pVBMeshInstances);

        /************************************************************************/
        /************************************************************************/
        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            exitSemaphore(pRenderer, pImageAcquiredSemaphore[i]);
        }

        exitGpuCmdRing(pRenderer, &gGraphicsCmdRing);
        exitGpuCmdRing(pRenderer, &gComputeCmdRing);

        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            exitSemaphore(pRenderer, pGraphicsAsyncSemaphores[i]);
        }

        exitQueue(pRenderer, pGraphicsQueue);
        exitQueue(pRenderer, pComputeQueue);

        removeSampler(pRenderer, pSamplerTrilinearAniso);
        removeSampler(pRenderer, pSamplerPointClamp);

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
        gFrameCount = 0;

        addShaders();
        addDescriptorSets();

        loadProfilerUI(mSettings.mWidth, mSettings.mHeight);

        gGuiWindowDesc = {};
        gGuiWindowDesc.mStartPos = vec2(mSettings.mWidth * 0.01f, mSettings.mHeight * 0.2f);
        gGuiWindowDesc.mStartSize = vec2(600.0f, 550.0f);
        gGuiWindowDesc.pWindowTitle = GetName();
        gGuiWindowDesc.mFlags = TF_UI_WINDOW_TITLE | TF_UI_WINDOW_MOVABLE | TF_UI_WINDOW_MINIMIZABLE | TF_UI_WINDOW_BORDER;

        gHistogramWindowDesc = {};
        gHistogramWindowDesc.mStartPos = vec2(mSettings.mWidth * 0.01f + 800.0f, mSettings.mHeight * 0.2f);
        gHistogramWindowDesc.mStartSize = vec2(516.0f, 400.0f);
        gHistogramWindowDesc.pWindowTitle = "Geometry Buffer";
        gHistogramWindowDesc.mFlags =
            TF_UI_WINDOW_TITLE | TF_UI_WINDOW_SCALABLE | TF_UI_WINDOW_MOVABLE | TF_UI_WINDOW_MINIMIZABLE | TF_UI_WINDOW_BORDER;

        {
            mBufferChunkAllocatorPlots[0].pName = gGeometryBufferLoadDesc.pNameIndexBuffer;
            uint32_t vertexBufferCount = 0;
            for (uint32_t i = 0; i < TF_ARRAY_COUNT(pVerticesPlot); ++i)
            {
                if (gGeometryBufferLoadDesc.pNamesVertexBuffers[i] == NULL)
                    break;
                vertexBufferCount++;
                mBufferChunkAllocatorPlots[i + 1].pName = gGeometryBufferLoadDesc.pNamesVertexBuffers[i];
            }
            recreateGeometryBufferPlots(vertexBufferCount);

            gGeometryBufferVertexBufferCount = vertexBufferCount;
        }

        luaRegisterUI();

        if (!addSwapChain())
            return false;

        if (gAppSettings.mMSAAIndex != gAppSettings.mMSAAIndexRequested)
        {
            gAppSettings.mMSAAIndex = gAppSettings.mMSAAIndexRequested;
            gAppSettings.mMSAALevel = (TFSampleCount)(1 << gAppSettings.mMSAAIndex);
            while (gAppSettings.mMSAAIndex > 0)
            {
                bool isValidLevel = (pRenderer->pGpu->mFrameBufferSamplesCount & gAppSettings.mMSAALevel) != 0;
                isValidLevel &= gAppSettings.mMSAALevel <= gAppSettings.mMaxMSAALevel;
                if (!isValidLevel)
                {
                    gAppSettings.mMSAAIndex--;
                    gAppSettings.mMSAALevel = (TFSampleCount)(gAppSettings.mMSAALevel / 2);
                }
                else
                {
                    break;
                }
            }
        }
        gDivider = gAppSettings.mEnableVRS ? 2 : 1;

        addRenderTargets();

        // Setup debug render target window
        {
            float  scale = 0.15f;
            float2 screenSize = { (float)pRenderTargetVBPass->mWidth * gDivider, (float)pRenderTargetVBPass->mHeight * gDivider };
            float2 texSize = screenSize * scale;

            gDebugTexturesWindowDesc = {};
            gDebugTexturesWindowDesc.mStartPos = vec2(0.0f, screenSize.y - texSize.y - 50.f);
            gDebugTexturesWindowDesc.mStartSize = vec2(600.0f, 550.0f);
            gDebugTexturesWindowDesc.pWindowTitle = "DEBUG RTs";
            gDebugTexturesWindowDesc.mFlags = gGuiWindowDesc.mFlags;
        }

        addOrderIndependentTransparencyResources();

        addPipelines();

        waitForAllResourceLoads();
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

        removeOrderIndependentTransparencyResources();

        TF_ESRAM_RESET_ALLOCS(pRenderer);

        removeSwapChain(pRenderer, pSwapChain);

        unloadProfilerUI();

        removeDescriptorSets();
        removeShaders();
    }

    void Update(float deltaTime)
    {
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
            if (gWasColorSpace != gAppSettings.mCurrentSwapChainColorSpace && gAppSettings.mOutputMode == OUTPUT_MODE_HDR10)
            {
                TFReloadDesc reloadDescriptor;
                reloadDescriptor.mType = TF_RELOAD_TYPE_RENDERTARGET;
                requestReload(&reloadDescriptor);
            }

            gWasColorSpace = gAppSettings.mCurrentSwapChainColorSpace;
            gWasDisplayColorRange = gAppSettings.mDisplayColorRange;
            gWasDisplaySignalRange = gAppSettings.mDisplaySignalRange;
            gWasOutputMode = gAppSettings.mOutputMode;
        }

        // Change format
        if (gWasOutputMode != gAppSettings.mOutputMode)
        {
            TFReloadDesc reloadDescriptor;
            reloadDescriptor.mType = TF_RELOAD_TYPE_RENDERTARGET;
            requestReload(&reloadDescriptor);

            gWasOutputMode = gAppSettings.mOutputMode;
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

        // We don't update the matrix if we are holding the filtered results because the meshes would keep moving leaving culled triangles
        // visible to the screen, because the triangles were filtered with an older modelMtx
        if (gAppSettings.mUpdateSimulation)
        {
            // Instances update
            for (uint32_t i = 0; i < gStaticMeshInstanceCount; ++i)
            {
                StaticMeshInstance* instance = &gStaticMeshInstances[i];
                instance->mRotation *= quatRotationY(degToRad(instance->mRotationSpeedYDeg) * deltaTime);
            }

            // Animation update
            for (uint32_t i = 0; i < gAnimatedMeshInstanceCount; ++i)
            {
                AnimatedMeshInstance* am = &gAnimatedMeshInstances[i];
                AnimatedObject*       pAnimObject = &am->mAnimObject;

                pAnimObject->Update(deltaTime);
                pAnimObject->ComputePose(pAnimObject->mRootTransform);
            }
        }

        // We have to update bone matrixes always because if we do it in the if(gAppSettings.mUpdateSimulation) above each frameIdx will
        // have different values for these matrixes when gAppSettings.mUpdateSimulation == false
        for (uint32_t i = 0; i < gAnimatedMeshInstanceCount; ++i)
        {
            AnimatedMeshInstance* am = &gAnimatedMeshInstances[i];
            const TFGeometryData* pGeomData = am->pGeomData;
            AnimatedObject*       pAnimObject = &am->mAnimObject;
            mat4*                 pBoneMatrixes = am->pBoneMatrixes;

            for (uint32_t j = 0; j < pGeomData->mJointCount; ++j)
            {
                pBoneMatrixes[j] = pAnimObject->mJointWorldMats[pGeomData->pJointRemaps[j]] * pGeomData->pInverseBindPoses[j];
            }
        }

#ifdef ENABLE_STREAMING
        Vector3 cameraPos = pCamera->getViewPosition();

        if (gFrameCount == 0)
        {
            calculateWorldLocation(&cameraPos, &gWorldLocation);
        }

        WorldLocation currWorldLocation;
        calculateWorldLocation(&cameraPos, &currWorldLocation);

        // Calculate camera position within the current sector
        Vector3 scaledSector = gWorldSectorSize * SCENE_SCALE;
        Vector3 camInSector = cameraPos;
        camInSector.x -= currWorldLocation.mSectorCoord.x * scaledSector.x;
        camInSector.z -= currWorldLocation.mSectorCoord.y * scaledSector.z;

        TFStreamingUpdateDesc streamingUpdate = {};
        streamingUpdate.mCameraPosition = camInSector;
        streamingUpdate.mMaxLiveMaterials = gAppSettings.mLiveTexturesMaxCount;
        streamingUpdate.mMode = (gAppSettings.mUsePixelCoverage) ? TF_StreamingPixelCoverage : TF_StreamingBoundingBoxes;
        streamingUpdate.mPinnedMaterialsCount = (uint32_t)arrlenu(gTransparentMaterialIDs);
        streamingUpdate.pPinnedMaterialIDs = gTransparentMaterialIDs;
        uint32_t frameIdx = 1 - (gFrameCount % gDataBufferCount);

        if (gPixelCoverageBuffers[frameIdx]->pCpuMappedAddress)
        {
            streamingUpdate.pPixelCoverageData = (uint32_t*)gPixelCoverageBuffers[frameIdx]->pCpuMappedAddress;
        }

        bool updateDescriptors = updateStreaming(&streamingUpdate);

        if (updateDescriptors)
        {
            for (uint32_t i = 0; i < gDataBufferCount; i++)
            {
                gStreamingTexturesChanged[i] = true;
            }
        }

        if (tryAcquireMutex(&gWorldStreamingUpdateMutex))
        {
            if (gWorldStreamingUpdateFinished == true)
            {
                UpdateVBMeshFilterGroupsDesc updateVBMeshFilterGroupsDesc = {};
                updateVBMeshFilterGroupsDesc.mNumMeshInstance = gMaxMeshCount;
                updateVBMeshFilterGroupsDesc.pVBMeshInstances = pVBMeshInstances;
                for (uint32_t i = 0; i < gDataBufferCount; ++i)
                {
                    updateVBMeshFilterGroupsDesc.mFrameIndex = i;
                    gVBPreFilterStats[i] = updateVBMeshFilterGroups(pVisibilityBuffer, &updateVBMeshFilterGroupsDesc);
                }
                gWorldStreamingUpdateFinished = false;
            }

            // Any change happening means we need to unload/load
            if ((currWorldLocation.mSectorCoord.x != gWorldLocation.mSectorCoord.x) ||
                (currWorldLocation.mSectorCoord.y != gWorldLocation.mSectorCoord.y) ||
                (currWorldLocation.mQuadrants.x != gWorldLocation.mQuadrants.x) ||
                (currWorldLocation.mQuadrants.y != gWorldLocation.mQuadrants.y))
            {
                // Unload and load
                gWorldLocation = currWorldLocation;
                TFThreadDesc threadDesc = {};
                threadDesc.pFunc = worldStreamingUpdateThreadFunc;
                threadDesc.pData = NULL;
                strncpy(threadDesc.mThreadName, "World Streaming", sizeof(threadDesc.mThreadName));
                initThread(&threadDesc, &gWorldStreamingUpdateThread);
                detachThread(gWorldStreamingUpdateThread);
            }

            releaseMutex(&gWorldStreamingUpdateMutex);
        }
#endif
        updateUniformData(gFrameCount % gDataBufferCount);

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

        uint32_t presentIndex = 0;
        uint32_t frameIdx = gFrameCount % gDataBufferCount;

        GpuCmdRingElement computeElem = getNextGpuCmdRingElement(&gComputeCmdRing, true, 1);
        GpuCmdRingElement graphicsElem = getNextGpuCmdRingElement(&gGraphicsCmdRing, true, 1);

        TFBufferUpdateDesc lightsUpdate = { pLightsBuffer };
        beginUpdateResource(&lightsUpdate);
        memcpy(lightsUpdate.pMappedData, &gLightData, sizeof(LightData) * 128);
        endUpdateResource(&lightsUpdate);

        /************************************************************************/
        // Async compute pass
        /************************************************************************/
        if (gAppSettings.mAsyncCompute && gAppSettings.mUpdateSimulation)
        {
            // Check to see if we can use the command buffer
            TFFenceStatus fenceStatus;
            getFenceStatus(pRenderer, computeElem.pFence, &fenceStatus);
            if (fenceStatus == TF_FENCE_STATUS_INCOMPLETE)
                waitForFences(pRenderer, 1, &computeElem.pFence);

            /************************************************************************/
            // Update uniform buffer to gpu
            /************************************************************************/
            if (!gAppSettings.mHoldFilteredResults)
            {
                TFBufferUpdateDesc update = { pPerFrameVBUniformBuffers[VB_UB_COMPUTE][frameIdx] };
                beginUpdateResource(&update);
                memcpy(update.pMappedData, &gPerFrame[frameIdx].gPerFrameUniformData, sizeof(gPerFrame[frameIdx].gPerFrameUniformData));
                endUpdateResource(&update);
            }

            /************************************************************************/
            // Triangle filtering async compute pass
            /************************************************************************/
            TFCmd* computeCmd = computeElem.pCmds[0];

            resetCmdPool(pRenderer, computeElem.pCmdPool);
            beginCmd(computeCmd);
            cmdBindDescriptorSet(computeCmd, 0, pDescriptorSetPersistent);
            cmdBindDescriptorSet(computeCmd, frameIdx, pDescriptorSetPerFrame);
            cmdBeginGpuFrameProfile(computeCmd, gComputeProfileToken);

            // Pre Skin pass
            {
                PreSkinVertexesPassDesc preSkinDesc = {};
                preSkinDesc.pPreSkinContainers = gPreSkinContainers;
                preSkinDesc.mPreSkinContainerCount = gAnimatedMeshInstanceCount;
                preSkinDesc.pPipelinePreSkinVertexes = pPipelinePreSkinVertexes[PRE_SKIN_ASYNC];
                preSkinDesc.pDescriptorSetPreSkinVertexesPerDraw = pDescriptorSetPreSkinVertexes[PRE_SKIN_ASYNC];

                TFBuffer* pPreSkinnedVertexBuffers[] = {
                    pAsyncComputeVertexBuffers->mPositions.pPreSkinBuffers[frameIdx],
                    pAsyncComputeVertexBuffers->mNormals.pPreSkinBuffers[frameIdx],
                };
                preSkinDesc.ppPreSkinOutputVertexBuffers = pPreSkinnedVertexBuffers;
                preSkinDesc.mPreSkinOutputVertexBufferCount = TF_ARRAY_COUNT(pPreSkinnedVertexBuffers);
                preSkinDesc.mFrameIndex = frameIdx;
                preSkinDesc.mGpuProfileToken = gComputeProfileToken;
                preSkinDesc.mPreSkinBatchIndex = SRT_RES_IDX(SrtPreSkinVertexesComp, PerBatch, gBatchData);
                preSkinDesc.pDescriptorSetPreSkinVertexes = pDescriptorSetPersistent;
                preSkinDesc.pDescriptorSetPreSkinVertexesPerFrame = pDescriptorSetPerFrame;
                cmdVisibilityBufferPreSkinVertexesPass(pVisibilityBuffer, computeCmd, &preSkinDesc);
            }

            // Triangle Filtering pass
            {
                TriangleFilteringPassDesc triangleFilteringDesc = {};
                triangleFilteringDesc.pPipelineClearBuffers = pPipelineClearBuffers;
                triangleFilteringDesc.pPipelineTriangleFiltering = pPipelineTriangleFiltering;
                triangleFilteringDesc.pDescriptorSetTriangleFilteringPerBatch = pDescriptorTriangleFilteringPerBatch;

#if defined(TF_ENABLE_WORKGRAPH)
                if (gAppSettings.mGpuPipelineWorkgraph)
                {
                    triangleFilteringDesc.pWorkgraph = pWorkgraphGpuPipeline;
                    triangleFilteringDesc.mInitWorkgraph = gFrameCount == 0;
                }
#endif

                triangleFilteringDesc.mFrameIndex = frameIdx;
                triangleFilteringDesc.mBuffersIndex = frameIdx;
                triangleFilteringDesc.mGpuProfileToken = gComputeProfileToken;
                triangleFilteringDesc.mVBPreFilterStats = gVBPreFilterStats[frameIdx];
                cmdVBTriangleFilteringPass(pVisibilityBuffer, computeCmd, &triangleFilteringDesc);
            }

            // Clear Light Clusters pass
            {
                cmdBeginGpuTimestampQuery(computeCmd, gComputeProfileToken, "Clear Light Clusters");
                cmdBindPipeline(computeCmd, pPipelineClearLightClusters);
                cmdBindDescriptorSet(computeCmd, frameIdx, pDescriptorSetClusterLightsPerBatch);
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
                    cmdBindDescriptorSet(computeCmd, frameIdx, pDescriptorSetClusterLightsPerBatch);
                    cmdDispatch(computeCmd, LIGHT_COUNT, 1, 1);
                }

                cmdEndGpuTimestampQuery(computeCmd, gComputeProfileToken);
            }

            cmdEndGpuFrameProfile(computeCmd, gComputeProfileToken);
            endCmd(computeCmd);

            FlushResourceUpdateDesc flushUpdateDesc = {};
            flushUpdateDesc.mNodeIndex = 0;
            flushResourceUpdates(&flushUpdateDesc);
            TFSemaphore* waitSemaphores[] = { flushUpdateDesc.pOutSubmittedSemaphore, gFrameCount > 1 ? gPrevGraphicsSemaphore : NULL };

            TFQueueSubmitDesc submitDesc = {};
            submitDesc.mCmdCount = 1;
            submitDesc.mSignalSemaphoreCount = 1;
            submitDesc.mWaitSemaphoreCount = waitSemaphores[1] ? TF_ARRAY_COUNT(waitSemaphores) : 1;
            submitDesc.ppCmds = &computeCmd;
            submitDesc.ppSignalSemaphores = &computeElem.pSemaphore;
            submitDesc.ppWaitSemaphores = waitSemaphores;
            submitDesc.pSignalFence = computeElem.pFence;
            submitDesc.mSubmitDone = (gFrameCount < 1);
            queueSubmit(pComputeQueue, &submitDesc);

            gComputeSemaphores[frameIdx] = computeElem.pSemaphore;
        }
        /************************************************************************/
        // Graphics Pass - Skip first frame since draw will always be one frame behind compute
        /************************************************************************/
        if (!gAppSettings.mAsyncCompute || gFrameCount > 0)
        {
            frameIdx = gAppSettings.mAsyncCompute ? ((gFrameCount - 1) % gDataBufferCount) : frameIdx;

            // Check to see if we can use the command buffer
            TFFenceStatus fenceStatus;
            getFenceStatus(pRenderer, graphicsElem.pFence, &fenceStatus);
            if (fenceStatus == TF_FENCE_STATUS_INCOMPLETE)
                waitForFences(pRenderer, 1, &graphicsElem.pFence);

            pScreenRenderTarget = pIntermediateRenderTarget;
            /************************************************************************/
            // Update uniform buffer to gpu
            /************************************************************************/
            if ((!gAppSettings.mAsyncCompute && !gAppSettings.mHoldFilteredResults) || !gAppSettings.mUpdateSimulation)
            {
                TFBufferUpdateDesc update = { pPerFrameVBUniformBuffers[VB_UB_COMPUTE][frameIdx] };
                beginUpdateResource(&update);
                memcpy(update.pMappedData, &gPerFrame[frameIdx].gPerFrameUniformData, sizeof(gPerFrame[frameIdx].gPerFrameUniformData));
                endUpdateResource(&update);
            }

#ifdef ENABLE_STREAMING
            // Update textures after streaming
            if (gStreamingTexturesChanged[frameIdx])
            {
#ifdef VULKAN
                if (gAppSettings.mAsyncCompute)
                {
                    // With async compute enabled, a compute submission may still be
                    // in-flight for frameIdx. To update the descriptor set without
                    // validation errors a fence is used for synchronization.
                    TFFence**     ppFence = &gComputeCmdRing.pFences[frameIdx][0];
                    TFFenceStatus status;
                    getFenceStatus(pRenderer, *ppFence, &status);
                    if (status == TF_FENCE_STATUS_INCOMPLETE)
                        waitForFences(pRenderer, 1, ppFence);
                }
#endif

                TFStreamingState currState;
                getStreamingState(&currState);
                TFDescriptorData perFrameParams[1] = {};
                perFrameParams[0].mIndex = SRT_RES_IDX(SrtData, PerFrame, gAllTextures);
                perFrameParams[0].mCount = (uint32_t)gAllTexturesCount;
                perFrameParams[0].ppTextures = currState.ppOrderedTextures;
                updateDescriptorSet(pRenderer, frameIdx, pDescriptorSetPerFrame, 1, perFrameParams);
                gStreamingTexturesChanged[frameIdx] = false;
            }
#endif

            TFBufferUpdateDesc update = { pPerFrameVBUniformBuffers[VB_UB_GRAPHICS][frameIdx] };
            beginUpdateResource(&update);
            memcpy(update.pMappedData, &gPerFrame[frameIdx].gPerFrameUniformData, sizeof(gPerFrame[frameIdx].gPerFrameUniformData));
            endUpdateResource(&update);

            // Update uniform buffers
            update = { pUniformBufferSky[frameIdx] };
            beginUpdateResource(&update);
            memcpy(update.pMappedData, &gPerFrame[frameIdx].gUniformDataSky, sizeof(gPerFrame[frameIdx].gUniformDataSky));
            endUpdateResource(&update);

            update = { pUniformBufferSkyTri[frameIdx] };
            beginUpdateResource(&update);
            memcpy(update.pMappedData, &gPerFrame[frameIdx].gUniformDataSkyTri, sizeof(gPerFrame[frameIdx].gUniformDataSkyTri));
            endUpdateResource(&update);

            // Update MeshData
            update = { pMeshDataBuffer[frameIdx], 0 };
            update.mSize = sizeof(MeshData) * gSceneMeshCount + sizeof(MeshData) * gMaxMeshInstances;
            beginUpdateResource(&update);
            memcpy(update.pMappedData, gPerFrame[frameIdx].pMeshData, update.mSize);
            endUpdateResource(&update);

            // Update Joint matrixes
            update = { pJointMatrixBuffer[frameIdx], 0 };
            update.mSize = sizeof(gPerFrame[frameIdx].gJointMatrixes);
            COMPILE_ASSERT(sizeof(mat4) * gMaxJointMatrixes == sizeof(gPerFrame[frameIdx].gJointMatrixes));
            beginUpdateResource(&update);
            memcpy(update.pMappedData, gPerFrame[frameIdx].gJointMatrixes, sizeof(gPerFrame[frameIdx].gJointMatrixes));
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

            if (!gAppSettings.mAsyncCompute && gAppSettings.mUpdateSimulation)
            {
                // Pre Skin pass
                {
                    PreSkinVertexesPassDesc preSkinDesc = {};
                    preSkinDesc.pPreSkinContainers = gPreSkinContainers;
                    preSkinDesc.mPreSkinContainerCount = gAnimatedMeshInstanceCount;
                    preSkinDesc.pPipelinePreSkinVertexes = pPipelinePreSkinVertexes[PRE_SKIN_SYNC];
                    preSkinDesc.pDescriptorSetPreSkinVertexes = pDescriptorSetPersistent;
                    preSkinDesc.pDescriptorSetPreSkinVertexesPerFrame = pDescriptorSetPerFrame;
                    preSkinDesc.pDescriptorSetPreSkinVertexesPerDraw = pDescriptorSetPreSkinVertexes[PRE_SKIN_SYNC];
                    // When we do Sync Triangle Filtering we can use the same buffers as we use for rendering on the PreSkin vertexes stage
                    // because there is no conflict on the TFResourceState
                    TFBuffer* pPreSkinOutputVtxBuffers[] = { GET_GEOMETRY_VERTEX_BUFFER(TF_SEMANTIC_POSITION),
                                                             GET_GEOMETRY_VERTEX_BUFFER(TF_SEMANTIC_NORMAL) };
                    preSkinDesc.ppPreSkinOutputVertexBuffers = pPreSkinOutputVtxBuffers;
                    preSkinDesc.mPreSkinOutputVertexBufferCount = TF_ARRAY_COUNT(pPreSkinOutputVtxBuffers);

                    preSkinDesc.mFrameIndex = frameIdx;
                    preSkinDesc.mGpuProfileToken = gGraphicsProfileToken;

                    TFBufferBarrier barriers[2] = { { pPreSkinOutputVtxBuffers[0], gVertexBufferState, TF_RESOURCE_STATE_UNORDERED_ACCESS },
                                                    { pPreSkinOutputVtxBuffers[1], gVertexBufferState,
                                                      TF_RESOURCE_STATE_UNORDERED_ACCESS } };
                    cmdResourceBarrier(graphicsCmd, TF_ARRAY_COUNT(barriers), barriers, 0, NULL, 0, NULL);

                    preSkinDesc.mPreSkinBatchIndex = SRT_RES_IDX(SrtPreSkinVertexesComp, PerBatch, gBatchData);
                    cmdVisibilityBufferPreSkinVertexesPass(pVisibilityBuffer, graphicsCmd, &preSkinDesc);

                    barriers[0] = { pPreSkinOutputVtxBuffers[0], TF_RESOURCE_STATE_UNORDERED_ACCESS, gVertexBufferState };
                    barriers[1] = { pPreSkinOutputVtxBuffers[1], TF_RESOURCE_STATE_UNORDERED_ACCESS, gVertexBufferState };
                    cmdResourceBarrier(graphicsCmd, TF_ARRAY_COUNT(barriers), barriers, 0, NULL, 0, NULL);
                }
                // Triangle Filtering pass
                {
                    TriangleFilteringPassDesc triangleFilteringDesc = {};
                    triangleFilteringDesc.pPipelineClearBuffers = pPipelineClearBuffers;
                    triangleFilteringDesc.pPipelineTriangleFiltering = pPipelineTriangleFiltering;
                    triangleFilteringDesc.pDescriptorSetTriangleFilteringPerBatch = pDescriptorTriangleFilteringPerBatch;

#if defined(TF_ENABLE_WORKGRAPH)
                    if (gAppSettings.mGpuPipelineWorkgraph)
                    {
                        triangleFilteringDesc.pWorkgraph = pWorkgraphGpuPipeline;
                        triangleFilteringDesc.mInitWorkgraph = gFrameCount == 0;
                    }
#endif

                    triangleFilteringDesc.mFrameIndex = frameIdx;
                    triangleFilteringDesc.mBuffersIndex = frameIdx;
                    triangleFilteringDesc.mGpuProfileToken = gGraphicsProfileToken;
                    triangleFilteringDesc.mVBPreFilterStats = gVBPreFilterStats[frameIdx];
                    cmdVBTriangleFilteringPass(pVisibilityBuffer, graphicsCmd, &triangleFilteringDesc);
                }
            }

            // Clear Light Clusters pass
            if (!gAppSettings.mAsyncCompute)
            {
                cmdBeginGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken, "Clear Light Clusters");
                cmdBindPipeline(graphicsCmd, pPipelineClearLightClusters);
                cmdBindDescriptorSet(graphicsCmd, frameIdx, pDescriptorSetClusterLightsPerBatch);
                cmdDispatch(graphicsCmd, 1, 1, 1);
                cmdEndGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken);
            }

            // Compute Light Clusters pass
            if (!gAppSettings.mAsyncCompute && gAppSettings.mRenderLocalLights)
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
                    cmdBindDescriptorSet(graphicsCmd, frameIdx, pDescriptorSetClusterLightsPerBatch);
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

            // Resource Transition
            {
                // Transition swapchain buffer to be used as a render target
                uint32_t              rtBarriersCount = gAppSettings.mMSAALevel > TF_SAMPLE_COUNT_1 ? 3 : 2;
                TFRenderTargetBarrier rtBarriers[] = {
                    { pScreenRenderTarget, TF_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, TF_RESOURCE_STATE_RENDER_TARGET },
                    //{ pDepthBuffer, TF_RESOURCE_STATE_SHADER_RESOURCE, TF_RESOURCE_STATE_DEPTH_WRITE },
                    { pDepthBufferOIT, TF_RESOURCE_STATE_SHADER_RESOURCE, TF_RESOURCE_STATE_DEPTH_WRITE },
                    { pRenderTargetMSAA, TF_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, TF_RESOURCE_STATE_RENDER_TARGET },
                };

                const uint32_t  maxNumBarriers = NUM_CULLING_VIEWPORTS * 2 + 6;
                uint32_t        barrierCount = 0;
                TFBufferBarrier drawInputBufferBarriers[maxNumBarriers] = {};
                drawInputBufferBarriers[barrierCount++] = { pLightClusters[frameIdx], TF_RESOURCE_STATE_UNORDERED_ACCESS,
                                                            TF_RESOURCE_STATE_SHADER_RESOURCE };
                drawInputBufferBarriers[barrierCount++] = { pLightClustersCount[frameIdx], TF_RESOURCE_STATE_UNORDERED_ACCESS,
                                                            TF_RESOURCE_STATE_SHADER_RESOURCE };

                drawInputBufferBarriers[barrierCount++] = { pVisibilityBuffer->ppIndirectDrawArgBuffer[frameIdx],
                                                            TF_RESOURCE_STATE_UNORDERED_ACCESS,
                                                            TF_RESOURCE_STATE_INDIRECT_ARGUMENT | TF_RESOURCE_STATE_SHADER_RESOURCE };

                drawInputBufferBarriers[barrierCount++] = { pVisibilityBuffer->ppIndirectDataBuffer[frameIdx],
                                                            TF_RESOURCE_STATE_UNORDERED_ACCESS, TF_RESOURCE_STATE_SHADER_RESOURCE };

                for (uint32_t i = 0; i < NUM_CULLING_VIEWPORTS; ++i)
                {
                    drawInputBufferBarriers[barrierCount++] = {
                        pVisibilityBuffer->ppFilteredIndexBuffer[frameIdx * NUM_CULLING_VIEWPORTS + i], TF_RESOURCE_STATE_UNORDERED_ACCESS,
                        TF_RESOURCE_STATE_INDEX_BUFFER | TF_RESOURCE_STATE_SHADER_RESOURCE
                    };
                }

                cmdResourceBarrier(graphicsCmd, barrierCount, drawInputBufferBarriers, 0, NULL, rtBarriersCount, rtBarriers);
            }

            // Shadow pass
            {
                cmdBeginGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken, "Shadow Pass");

                // Resource Transition
                {
                    TFRenderTargetBarrier passBarriers[] = {
                        { pRenderTargetShadow, TF_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, TF_RESOURCE_STATE_DEPTH_WRITE },
                    };
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

                    const char* profileNames[NUM_GEOMETRY_SETS] = { "SM Opaque", "SM Alpha", "SM Transparent" };
                    // Render all geometry sets onto shadow map
                    for (uint32_t i = 0; i < NUM_GEOMETRY_SETS; ++i)
                    {
                        cmdBeginGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken, profileNames[i]);
                        cmdBindPipeline(graphicsCmd, pPipelineShadowPass[i]);
                        uint64_t  indirectBufferByteOffset = GET_INDIRECT_DRAW_ELEM_INDEX(VIEW_SHADOW, i, 0) * sizeof(uint32_t);
                        TFBuffer* pIndirectBuffer = pVisibilityBuffer->ppIndirectDrawArgBuffer[frameIdx];
                        cmdExecuteIndirect(graphicsCmd, TF_INDIRECT_DRAW_INDEX, 1, pIndirectBuffer, indirectBufferByteOffset, NULL, 0);
                        cmdEndGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken);
                    }
                }

                cmdBindRenderTargets(graphicsCmd, NULL);
                cmdEndGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken);
            }

            cmdSetStencilReferenceValue(graphicsCmd, MSAA_STENCIL_MASK);

            // Clear OIT Head Index pass
            {
                cmdBeginGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken, "Clear OIT Head Index");

                // Resource Transition
                {
                    TFRenderTargetBarrier passBarriers[] = {
                        { pRenderTargetVBPass, TF_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, TF_RESOURCE_STATE_RENDER_TARGET },
                    };
                    TFBufferBarrier oitGeomCountBarrier = { pGeometryCountBufferOIT, TF_RESOURCE_STATE_UNORDERED_ACCESS,
                                                            TF_RESOURCE_STATE_UNORDERED_ACCESS };
                    cmdResourceBarrier(graphicsCmd, 1, &oitGeomCountBarrier, 0, NULL, TF_ARRAY_COUNT(passBarriers), passBarriers);
                }
                // Bind Render Targets
                {
                    TFBindRenderTargetsDesc bindRenderTargets = {};
                    bindRenderTargets.mDepthStencil = { pDepthBufferOIT, TF_LOAD_ACTION_DONTCARE, TF_STORE_ACTION_DONTCARE,
                                                        TF_LOAD_ACTION_DONTCARE, TF_STORE_ACTION_DONTCARE };
                    cmdBindRenderTargets(graphicsCmd, &bindRenderTargets);
                    cmdSetViewport(graphicsCmd, 0.0f, 0.0f, (float)pRenderTargetVBPass->mWidth * gDivider,
                                   (float)pRenderTargetVBPass->mHeight * gDivider, 0.0f, 1.0f);
                    cmdSetScissor(graphicsCmd, 0, 0, pRenderTargetVBPass->mWidth * gDivider, pRenderTargetVBPass->mHeight * gDivider);
                }
                // Draw
                {
                    // A single triangle is rendered without specifying a vertex buffer (triangle positions are calculated internally using
                    // vertex_id)
                    cmdBindPipeline(graphicsCmd, pPipelineClearHeadIndexOIT);
                    cmdBindDescriptorSet(graphicsCmd, 0, pDescriptorSetVisibilityPassTransparentPerBatch);
                    cmdDraw(graphicsCmd, 3, 0);
                }

                cmdBindRenderTargets(graphicsCmd, NULL);
                cmdEndGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken);
            }

            // VB Fill pass
            {
                // Render the scene to perform the Visibility Buffer pass. In this pass the (filtered) scene geometry is rendered
                // into a 32-bit per pixel render target. This contains triangle information (batch Id and triangle Id) that allows
                // to reconstruct all triangle attributes per pixel. This is faster than a typical Deferred Shading pass, because
                // less memory bandwidth is used.
                // Render target is cleared to (1,1,1,1) because (0,0,0,0) represents the first triangle of the first draw batch

                cmdBeginGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken, "VB Pass");

                // Resource Transition
                {
                    TFBufferBarrier bufferBarrier = { pHeadIndexBufferOIT, TF_RESOURCE_STATE_UNORDERED_ACCESS,
                                                      TF_RESOURCE_STATE_UNORDERED_ACCESS };
                    cmdResourceBarrier(graphicsCmd, 1, &bufferBarrier, 0, NULL, 0, NULL);
                }
                // Bind Render Targets
                {
                    TFBindRenderTargetsDesc bindRenderTargets = {};
                    bindRenderTargets.mRenderTargetCount = 1;
                    bindRenderTargets.mRenderTargets[0] = { pRenderTargetVBPass, TF_LOAD_ACTION_CLEAR };
                    bindRenderTargets.mDepthStencil = { pDepthBuffer, TF_LOAD_ACTION_CLEAR, TF_STORE_ACTION_STORE, TF_LOAD_ACTION_CLEAR };
                    if (gAppSettings.mEnableVRS)
                    {
                        bindRenderTargets.mSampleLocation = { gLocations, 1, 1 };
                    }

                    cmdBindRenderTargets(graphicsCmd, &bindRenderTargets);
                    cmdSetViewport(graphicsCmd, 0.0f, 0.0f, (float)pRenderTargetVBPass->mWidth, (float)pRenderTargetVBPass->mHeight, 0.0f,
                                   1.0f);
                    cmdSetScissor(graphicsCmd, 0, 0, pRenderTargetVBPass->mWidth, pRenderTargetVBPass->mHeight);
                }
                // Draw All Geometry Sets
                {
                    TFBuffer* pIndexBuffer = pVisibilityBuffer->ppFilteredIndexBuffer[frameIdx * NUM_CULLING_VIEWPORTS + VIEW_CAMERA];
                    cmdBindIndexBuffer(graphicsCmd, pIndexBuffer, TF_INDEX_TYPE_UINT32, 0);

                    const char* profileNames[NUM_GEOMETRY_SETS] = { "VB Opaque", "VB Alpha", "VB Transparent" };
                    for (uint32_t i = 0; i < NUM_GEOMETRY_SETS; ++i)
                    {
                        cmdBeginGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken, profileNames[i]);

                        // Bind Render Targets
                        if (i == GEOMSET_ALPHA_BLEND)
                        {
                            // Need set load action only to force barrier on some GPUs to make sure depth buffer writes are synchronized
                            // correctly
                            TFBindRenderTargetsDesc bindRenderTargetsAlpha = {};
                            bindRenderTargetsAlpha.mDepthStencil = { pDepthBufferOIT, TF_LOAD_ACTION_LOAD };
                            cmdBindRenderTargets(graphicsCmd, &bindRenderTargetsAlpha);
                            cmdSetViewport(graphicsCmd, 0.0f, 0.0f, (float)pRenderTargetVBPass->mWidth * gDivider,
                                           (float)pRenderTargetVBPass->mHeight * gDivider, 0.0f, 1.0f);
                            cmdSetScissor(graphicsCmd, 0, 0, pRenderTargetVBPass->mWidth * gDivider,
                                          pRenderTargetVBPass->mHeight * gDivider);
                        }
                        // Draw
                        {
                            cmdBindPipeline(graphicsCmd, pPipelineVisibilityBufferPass[i]);
                            cmdBindDescriptorSet(graphicsCmd, 0, pDescriptorSetVisibilityPassTransparentPerBatch);

                            uint64_t  indirectBufferByteOffset = GET_INDIRECT_DRAW_ELEM_INDEX(VIEW_CAMERA, i, 0) * sizeof(uint32_t);
                            TFBuffer* pIndirectBuffer = pVisibilityBuffer->ppIndirectDrawArgBuffer[frameIdx];
                            cmdExecuteIndirect(graphicsCmd, TF_INDIRECT_DRAW_INDEX, 1, pIndirectBuffer, indirectBufferByteOffset, NULL, 0);
                        }
                        cmdEndGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken);
                    }
                    cmdBindRenderTargets(graphicsCmd, NULL);
                }
                // Resource Transition
                {
                    TFBufferBarrier bufferBarriers[2] = {};
                    bufferBarriers[0] = { pVisBufLinkedListBufferOIT, TF_RESOURCE_STATE_UNORDERED_ACCESS,
                                          TF_RESOURCE_STATE_SHADER_RESOURCE };
                    bufferBarriers[1] = { pHeadIndexBufferOIT, TF_RESOURCE_STATE_UNORDERED_ACCESS, TF_RESOURCE_STATE_SHADER_RESOURCE };
                    cmdResourceBarrier(graphicsCmd, 2, bufferBarriers, 0, NULL, 0, NULL);
                }

                cmdEndGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken);
            }

            // VRS Fill pass
            if (gAppSettings.mEnableVRS)
            {
                // Generate the shading rate image
                cmdBeginGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken, "VRS Filling Pass");

                // Resource Transition
                {
                    TFRenderTargetBarrier barriers[] = {
                        { pHistoryRenderTarget[frameIdx], TF_RESOURCE_STATE_SHADER_RESOURCE, TF_RESOURCE_STATE_RENDER_TARGET },
                        { pRenderTargetVBPass, TF_RESOURCE_STATE_RENDER_TARGET, TF_RESOURCE_STATE_PIXEL_SHADER_RESOURCE },
                    };
                    cmdResourceBarrier(graphicsCmd, 0, NULL, 0, NULL, 2, barriers);
                }
                // Bind Render Targets
                {
                    TFBindRenderTargetsDesc bindRenderTargets = {};
                    bindRenderTargets.mRenderTargetCount = 1;
                    bindRenderTargets.mRenderTargets[0] = { pHistoryRenderTarget[frameIdx], TF_LOAD_ACTION_LOAD };
                    bindRenderTargets.mDepthStencil = { pDepthBuffer, TF_LOAD_ACTION_LOAD, TF_STORE_ACTION_STORE, TF_LOAD_ACTION_LOAD };
                    bindRenderTargets.mSampleLocation = { gLocations, 1, 1 };
                    cmdBindRenderTargets(graphicsCmd, &bindRenderTargets);
                    cmdSetViewport(graphicsCmd, 0.0f, 0.0f, (float)pDepthBuffer->mWidth, (float)pDepthBuffer->mHeight, 0.0f, 1.0f);
                    cmdSetScissor(graphicsCmd, 0, 0, pDepthBuffer->mWidth, pDepthBuffer->mHeight);
                }
                // Draw
                {
                    cmdBindPipeline(graphicsCmd, pPipelineFillStencil);
                    cmdSetStencilReferenceValue(graphicsCmd, 1);
                    cmdDraw(graphicsCmd, 3, 0);
                    cmdBindRenderTargets(graphicsCmd, NULL);
                }
                // Resource Transition
                {
                    TFRenderTargetBarrier barriers[] = {
                        { pHistoryRenderTarget[frameIdx], TF_RESOURCE_STATE_RENDER_TARGET, TF_RESOURCE_STATE_SHADER_RESOURCE },
                        { pRenderTargetVBPass, TF_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, TF_RESOURCE_STATE_RENDER_TARGET }
                    };
                    cmdResourceBarrier(graphicsCmd, 0, NULL, 0, NULL, 2, barriers);
                }

                cmdEndGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken);
            }

            // Resource Transition
            {
                TFRenderTargetBarrier sceneInputBarriers[] = {
                    { pRenderTargetVBPass, TF_RESOURCE_STATE_RENDER_TARGET, TF_RESOURCE_STATE_PIXEL_SHADER_RESOURCE },
                    { pRenderTargetShadow, TF_RESOURCE_STATE_DEPTH_WRITE, TF_RESOURCE_STATE_PIXEL_SHADER_RESOURCE },
                    { pDepthBufferOIT, TF_RESOURCE_STATE_DEPTH_WRITE, TF_RESOURCE_STATE_SHADER_RESOURCE },
                };
                cmdResourceBarrier(graphicsCmd, 0, NULL, 0, NULL, TF_ARRAY_COUNT(sceneInputBarriers), sceneInputBarriers);
            }

            const bool isRunningProgMSAA = gAppSettings.mMSAALevel > TF_SAMPLE_COUNT_1 && !gAppSettings.mEnableVRS;

            // MSAA edges stencil pass
            if (isRunningProgMSAA)
            {
                // This depth-only pass will render to the stencil buffer. Samples that must be shaded in
                // future shading or post-processing passes will be set to 0x01.
                cmdBeginGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken, "MSAA Edges Stencil Pass");

                // Resource Transition
                {
                    TFRenderTargetBarrier barriers[] = {
                        { pRenderTargetMSAAEdges, TF_RESOURCE_STATE_DEPTH_READ, TF_RESOURCE_STATE_DEPTH_WRITE },
                        { pDepthBuffer, TF_RESOURCE_STATE_DEPTH_WRITE, TF_RESOURCE_STATE_PIXEL_SHADER_RESOURCE },
                    };
                    cmdResourceBarrier(graphicsCmd, 0, NULL, 0, NULL, TF_ARRAY_COUNT(barriers), barriers);
                }
                // Bind Render Targets
                {
                    TFBindRenderTargetsDesc bindRenderTargets = {};
                    bindRenderTargets.mDepthStencil = { pRenderTargetMSAAEdges, TF_LOAD_ACTION_DONTCARE, TF_STORE_ACTION_NONE,
                                                        TF_LOAD_ACTION_CLEAR, TF_STORE_ACTION_STORE };
                    cmdBindRenderTargets(graphicsCmd, &bindRenderTargets);
                    cmdSetViewport(graphicsCmd, 0.0f, 0.0f, (float)pRenderTargetMSAAEdges->mWidth, (float)pRenderTargetMSAAEdges->mHeight,
                                   0.0f, 1.0f);
                    cmdSetScissor(graphicsCmd, 0, 0, pRenderTargetMSAAEdges->mWidth, pRenderTargetMSAAEdges->mHeight);
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

            // God Ray passes
            if (gAppSettings.mEnableGodray)
            {
                // MSAA edges stencil downscale pass
                if (isRunningProgMSAA)
                {
                    cmdBeginGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken, "MSAA Edges Stencil Downscale Pass");

                    // Resource Transition
                    {
                        TFRenderTargetBarrier barriers[] = { {
                                                                 pRenderTargetMSAAEdges,
                                                                 TF_RESOURCE_STATE_DEPTH_WRITE,
                                                                 TF_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                                                             },
                                                             { pRenderTargetMSAAEdgesDownscaled, TF_RESOURCE_STATE_DEPTH_READ,
                                                               TF_RESOURCE_STATE_DEPTH_WRITE } };
                        cmdResourceBarrier(graphicsCmd, 0, NULL, 0, NULL, 2, barriers);
                    }
                    // Bind Render Targets
                    {
                        TFBindRenderTargetsDesc bindRenderTargets = {};
                        bindRenderTargets.mDepthStencil = { pRenderTargetMSAAEdgesDownscaled, TF_LOAD_ACTION_DONTCARE,
                                                            TF_STORE_ACTION_DONTCARE, TF_LOAD_ACTION_CLEAR, TF_STORE_ACTION_STORE };
                        cmdBindRenderTargets(graphicsCmd, &bindRenderTargets);
                        cmdSetViewport(graphicsCmd, 0.0f, 0.0f, (float)pRenderTargetMSAAEdgesDownscaled->mWidth,
                                       (float)pRenderTargetMSAAEdgesDownscaled->mHeight, 0.0f, 1.0f);
                        cmdSetScissor(graphicsCmd, 0, 0, pRenderTargetMSAAEdgesDownscaled->mWidth,
                                      pRenderTargetMSAAEdgesDownscaled->mHeight);
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

                // God Ray pass
                {
                    cmdBeginGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken, "God Ray");

                    bool            isProgMSAA = gAppSettings.mMSAALevel > TF_SAMPLE_COUNT_1 && !gAppSettings.mEnableVRS;
                    TFRenderTarget* pGodRayActiveRenderTarget = pRenderTargetGodRay[0];

                    // Resource Transition
                    {
                        if (isProgMSAA)
                        {
                            pGodRayActiveRenderTarget = pRenderTargetGodRayMS;
                            TFRenderTargetBarrier barrier[] = {
                                { pRenderTargetGodRay[0], TF_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, TF_RESOURCE_STATE_RENDER_TARGET },
                                { pRenderTargetGodRayMS, TF_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, TF_RESOURCE_STATE_RENDER_TARGET },
                                { pRenderTargetMSAAEdgesDownscaled, TF_RESOURCE_STATE_DEPTH_WRITE, TF_RESOURCE_STATE_DEPTH_READ },
                            };
                            cmdResourceBarrier(graphicsCmd, 0, NULL, 0, NULL, TF_ARRAY_COUNT(barrier), barrier);
                        }
                        else
                        {
                            TFRenderTargetBarrier barrier[] = {
                                { pRenderTargetGodRay[0], TF_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, TF_RESOURCE_STATE_RENDER_TARGET },
                                { pDepthBuffer, TF_RESOURCE_STATE_DEPTH_WRITE, TF_RESOURCE_STATE_PIXEL_SHADER_RESOURCE },
                            };
                            cmdResourceBarrier(graphicsCmd, 0, NULL, 0, NULL, TF_ARRAY_COUNT(barrier), barrier);
                        }
                    }
                    // Bind Render Targets
                    {
                        TFBindRenderTargetsDesc bindRenderTargets = {};
                        bindRenderTargets.mRenderTargetCount = 1;
                        bindRenderTargets.mRenderTargets[0] = { pGodRayActiveRenderTarget, TF_LOAD_ACTION_CLEAR };
                        if (isProgMSAA)
                        {
                            bindRenderTargets.mDepthStencil = { pRenderTargetMSAAEdgesDownscaled, TF_LOAD_ACTION_DONTCARE,
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

                // MSAA Resolve pass
                if (isRunningProgMSAA)
                {
                    cmdBeginGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken, "MSAA Resolve GodRay Pass");

                    // Transition world render target to be used as input texture in post process pass
                    {
                        TFRenderTargetBarrier barrier = { pRenderTargetGodRayMS, TF_RESOURCE_STATE_RENDER_TARGET,
                                                          TF_RESOURCE_STATE_PIXEL_SHADER_RESOURCE };
                        cmdResourceBarrier(graphicsCmd, 0, NULL, 0, NULL, 1, &barrier);
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
                        cmdBindPipeline(graphicsCmd, pPipelineResolveGodRay);
                        cmdBindDescriptorSet(graphicsCmd, frameIdx * gResolveTargetCount + gResolveGodRayPassIndex, pDescriptorSetResolve);
                        cmdDraw(graphicsCmd, 3, 0);
                    }

                    cmdBindRenderTargets(graphicsCmd, NULL);
                    cmdEndGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken);
                }

                // God Ray blur pass
                {
                    cmdBeginGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken, "God Ray Blur");

                    // Update God Ray blur weights
                    {
                        TFBufferUpdateDesc bufferUpdate = { pBufferBlurWeights };
                        beginUpdateResource(&bufferUpdate);
                        memcpy(bufferUpdate.pMappedData, &gBlurWeightsUniform, sizeof(gBlurWeightsUniform));
                        endUpdateResource(&bufferUpdate);
                    }

                    const uint32_t threadGroupSizeX = pRenderTargetGodRay[0]->mWidth / 16 + 1;
                    const uint32_t threadGroupSizeY = pRenderTargetGodRay[0]->mHeight / 16 + 1;

                    cmdBindPipeline(graphicsCmd, pPipelineGodRayBlurPass);

                    // Horizontal pass
                    {
                        cmdBeginGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken, "God Ray Blur Horizontal");

                        // Resource Transition
                        {
                            TFRenderTargetBarrier blurBarriers[] = {
                                { pRenderTargetGodRay[0], TF_RESOURCE_STATE_RENDER_TARGET, TF_RESOURCE_STATE_UNORDERED_ACCESS },
                                { pRenderTargetGodRay[1], TF_RESOURCE_STATE_UNORDERED_ACCESS, TF_RESOURCE_STATE_UNORDERED_ACCESS },
                            };
                            cmdResourceBarrier(graphicsCmd, 0, NULL, 0, NULL, TF_ARRAY_COUNT(blurBarriers), blurBarriers);
                        }
                        // Update Constants
                        {
                            gGodRayBlurConstant.mBlurPassType = BLUR_PASS_TYPE_HORIZONTAL;
                            gGodRayBlurConstant.mFilterRadius = gAppSettings.mFilterRadius;
                            TFBufferUpdateDesc updateH = { pGodRayBlurBuffer[BLUR_PASS_TYPE_HORIZONTAL][frameIdx] };
                            beginUpdateResource(&updateH);
                            memcpy(updateH.pMappedData, &gGodRayBlurConstant, sizeof(gGodRayBlurConstant));
                            endUpdateResource(&updateH);
                        }
                        // Dispatch
                        {
                            cmdBindDescriptorSet(graphicsCmd, frameIdx * BLUR_PASS_TYPE_COUNT + BLUR_PASS_TYPE_HORIZONTAL,
                                                 pDescriptorSetGodRayBlurPassPerBatch);
                            cmdDispatch(graphicsCmd, threadGroupSizeX, threadGroupSizeY, 1);
                        }

                        cmdEndGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken);
                    }

                    // Vertical pass
                    {
                        cmdBeginGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken, "God Ray Blur Vertical");

                        // Resource Transition
                        {
                            TFRenderTargetBarrier blurBarriers[2] = {
                                { pRenderTargetGodRay[0], TF_RESOURCE_STATE_UNORDERED_ACCESS, TF_RESOURCE_STATE_UNORDERED_ACCESS },
                                { pRenderTargetGodRay[1], TF_RESOURCE_STATE_UNORDERED_ACCESS, TF_RESOURCE_STATE_UNORDERED_ACCESS }
                            };
                            cmdResourceBarrier(graphicsCmd, 0, NULL, 0, NULL, TF_ARRAY_COUNT(blurBarriers), blurBarriers);
                        }
                        // Update Constants
                        {
                            gGodRayBlurConstant.mBlurPassType = BLUR_PASS_TYPE_VERTICAL;
                            gGodRayBlurConstant.mFilterRadius = gAppSettings.mFilterRadius;
                            TFBufferUpdateDesc updateV = { pGodRayBlurBuffer[BLUR_PASS_TYPE_VERTICAL][frameIdx] };
                            beginUpdateResource(&updateV);
                            memcpy(updateV.pMappedData, &gGodRayBlurConstant, sizeof(gGodRayBlurConstant));
                            endUpdateResource(&updateV);
                        }
                        // Dispatch
                        {
                            cmdBindDescriptorSet(graphicsCmd, frameIdx * BLUR_PASS_TYPE_COUNT + BLUR_PASS_TYPE_VERTICAL,
                                                 pDescriptorSetGodRayBlurPassPerBatch);
                            cmdDispatch(graphicsCmd, threadGroupSizeX, threadGroupSizeY, 1);
                        }
                        // Resource Transition
                        {
                            TFRenderTargetBarrier blurBarriers[2] = {
                                { pRenderTargetGodRay[0], TF_RESOURCE_STATE_UNORDERED_ACCESS, TF_RESOURCE_STATE_PIXEL_SHADER_RESOURCE },
                                { pRenderTargetGodRay[1], TF_RESOURCE_STATE_UNORDERED_ACCESS, TF_RESOURCE_STATE_UNORDERED_ACCESS }
                            };
                            cmdResourceBarrier(graphicsCmd, 0, NULL, 0, NULL, TF_ARRAY_COUNT(blurBarriers), blurBarriers);
                        }

                        cmdEndGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken);
                    }
                    cmdEndGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken);
                }
            }

            // Visibility buffer shading pass
            {
                // Render a fullscreen triangle to evaluate shading for every pixel. This render step uses the render target generated by
                // DrawVisibilityBufferPass to get the draw / triangle IDs to reconstruct and interpolate vertex attributes per pixel. This
                // method doesn't set any vertex/index buffer because the triangle positions are calculated internally using vertex_id.
                cmdBeginGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken, "VB Shading Pass");

                bool            isProgMSAAEnabled = gAppSettings.mMSAALevel > TF_SAMPLE_COUNT_1 && !gAppSettings.mEnableVRS;
                TFRenderTarget* pDestinationRenderTarget =
                    gAppSettings.mMSAALevel > TF_SAMPLE_COUNT_1 ? pRenderTargetMSAA : pScreenRenderTarget;

                // Resource Transition
                {
                    if (isProgMSAAEnabled)
                    {
                        TFResourceState prevState =
                            gAppSettings.mEnableGodray ? TF_RESOURCE_STATE_PIXEL_SHADER_RESOURCE : TF_RESOURCE_STATE_DEPTH_WRITE;
                        TFRenderTargetBarrier barriers[] = {
                            { pRenderTargetMSAAEdges, prevState, TF_RESOURCE_STATE_DEPTH_READ },
                        };
                        cmdResourceBarrier(graphicsCmd, 0, NULL, 0, NULL, TF_ARRAY_COUNT(barriers), barriers);
                    }

                    if (gAppSettings.mEnableVRS)
                    {
                        if (gAppSettings.mEnableGodray)
                        {
                            TFRenderTargetBarrier barriers[] = {
                                { pDepthBuffer, TF_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, TF_RESOURCE_STATE_DEPTH_READ },
                            };
                            cmdResourceBarrier(graphicsCmd, 0, NULL, 0, NULL, TF_ARRAY_COUNT(barriers), barriers);
                        }
                        else if (!isProgMSAAEnabled)
                        {
                            TFRenderTargetBarrier barriers[] = {
                                { pDepthBuffer, TF_RESOURCE_STATE_DEPTH_WRITE, TF_RESOURCE_STATE_DEPTH_READ },
                            };
                            cmdResourceBarrier(graphicsCmd, 0, NULL, 0, NULL, TF_ARRAY_COUNT(barriers), barriers);
                        }
                    }
                }
                // Bind Render Targets
                {
                    TFBindRenderTargetsDesc bindRenderTargets = {};
                    bindRenderTargets.mRenderTargetCount = 1;
                    bindRenderTargets.mRenderTargets[0] = { pDestinationRenderTarget, TF_LOAD_ACTION_CLEAR };

                    if (isProgMSAAEnabled)
                    {
                        bindRenderTargets.mDepthStencil = { pRenderTargetMSAAEdges, TF_LOAD_ACTION_DONTCARE, TF_STORE_ACTION_DONTCARE,
                                                            TF_LOAD_ACTION_LOAD, TF_STORE_ACTION_NONE };
                    }
                    else
                    {
                        bindRenderTargets.mDepthStencil = { gAppSettings.mEnableVRS ? pDepthBuffer : NULL, TF_LOAD_ACTION_LOAD,
                                                            TF_STORE_ACTION_DONTCARE, TF_LOAD_ACTION_LOAD, TF_STORE_ACTION_NONE };
                    }
                    cmdBindRenderTargets(graphicsCmd, &bindRenderTargets);
                    cmdSetViewport(graphicsCmd, 0.0f, 0.0f, (float)pDestinationRenderTarget->mWidth,
                                   (float)pDestinationRenderTarget->mHeight, 0.0f, 1.0f);
                    cmdSetScissor(graphicsCmd, 0, 0, pDestinationRenderTarget->mWidth, pDestinationRenderTarget->mHeight);
                }
                // Draw
                {
                    cmdBindPipeline(graphicsCmd, pPipelineVisibilityBufferShadeSrgb[0]);

                    if (gAppSettings.mEnableVRS)
                    {
                        cmdSetStencilReferenceValue(graphicsCmd, VRS_STENCIL_MASK);
                    }
                    else
                    {
                        cmdSetStencilReferenceValue(graphicsCmd, MSAA_STENCIL_MASK);
                    }
                    // A single triangle is rendered without specifying a vertex buffer (triangle positions are calculated internally using
                    // vertex_id)
                    cmdDraw(graphicsCmd, 3, 0);
                    cmdBindRenderTargets(graphicsCmd, NULL);
                }
                // Resource Transition
                {
                    TFBufferBarrier bufferBarriers[2] = {
                        { pVisBufLinkedListBufferOIT, TF_RESOURCE_STATE_SHADER_RESOURCE, TF_RESOURCE_STATE_UNORDERED_ACCESS },
                        { pHeadIndexBufferOIT, TF_RESOURCE_STATE_SHADER_RESOURCE, TF_RESOURCE_STATE_UNORDERED_ACCESS }
                    };

                    cmdResourceBarrier(graphicsCmd, 2, bufferBarriers, 0, NULL, 0, NULL);
                }

                cmdEndGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken);
            }

#ifdef ENABLE_STREAMING
            // Pixel coverage pass
            if (gAppSettings.mUsePixelCoverage)
            {
                cmdBeginGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken, "VB Pixel Coverage");

                // Resource Transition
                {
                    TFBufferBarrier bufferBarrier = { gPixelCoverageBuffers[frameIdx], TF_RESOURCE_STATE_COPY_SOURCE,
                                                      TF_RESOURCE_STATE_UNORDERED_ACCESS };
                    cmdResourceBarrier(graphicsCmd, 1, &bufferBarrier, 0, NULL, 0, NULL);
                }
                // Dispatch
                {
                    cmdBindPipeline(graphicsCmd, pPipelinePixelCoverage);
                    cmdBindDescriptorSet(graphicsCmd, frameIdx, pDescriptorTriangleFilteringPerBatch);
                    cmdDispatch(graphicsCmd, 1, 1, 1);
                }
                // Resource Transition
                {
                    TFBufferBarrier bufferBarrier = { gPixelCoverageBuffers[frameIdx], TF_RESOURCE_STATE_UNORDERED_ACCESS,
                                                      TF_RESOURCE_STATE_COPY_SOURCE };
                    cmdResourceBarrier(graphicsCmd, 1, &bufferBarrier, 0, NULL, 0, NULL);
                }

                cmdEndGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken);
            }
#endif

            if (gAppSettings.mMSAALevel > TF_SAMPLE_COUNT_1)
            {
                // VRS Resolve Pass
                if (gAppSettings.mEnableVRS)
                {
                    cmdBeginGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken, "VRS Resolve Pass");
                    pScreenRenderTarget = pResolveVRSRenderTarget[1 - frameIdx];

                    // Resource Transition
                    {
                        TFRenderTargetBarrier barriers[] = {
                            { pRenderTargetMSAA, TF_RESOURCE_STATE_RENDER_TARGET, TF_RESOURCE_STATE_SHADER_RESOURCE },
                            { pIntermediateRenderTarget, TF_RESOURCE_STATE_RENDER_TARGET, TF_RESOURCE_STATE_PIXEL_SHADER_RESOURCE },
                            { pScreenRenderTarget, TF_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, TF_RESOURCE_STATE_UNORDERED_ACCESS },
                            { pDebugVRSRenderTarget, TF_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, TF_RESOURCE_STATE_UNORDERED_ACCESS }
                        };
                        cmdResourceBarrier(graphicsCmd, 0, NULL, 0, NULL, 4, barriers);
                    }
                    // Dispatch
                    {
                        cmdBindPipeline(graphicsCmd, pPipelineResolveCompute);
                        cmdBindDescriptorSet(graphicsCmd, 1 - frameIdx, pDescriptorSetResolveVRSPerBatch);
                        const uint32_t* pThreadGroupSize = pShaderResolveCompute->mNumThreadsPerGroup;
                        cmdDispatch(graphicsCmd, gSceneRes.mWidth / (gDivider * pThreadGroupSize[0]) + 1,
                                    gSceneRes.mHeight / (gDivider * pThreadGroupSize[1]) + 1, pThreadGroupSize[2]);
                    }
                    // Resource Transition
                    {
                        TFRenderTargetBarrier barriers[] = {
                            { pScreenRenderTarget, TF_RESOURCE_STATE_UNORDERED_ACCESS, TF_RESOURCE_STATE_RENDER_TARGET },
                            { pRenderTargetMSAA, TF_RESOURCE_STATE_SHADER_RESOURCE, TF_RESOURCE_STATE_PIXEL_SHADER_RESOURCE },
                            { pDebugVRSRenderTarget, TF_RESOURCE_STATE_UNORDERED_ACCESS, TF_RESOURCE_STATE_PIXEL_SHADER_RESOURCE }
                        };
                        cmdResourceBarrier(graphicsCmd, 0, NULL, 0, NULL, 3, barriers);
                    }
                    cmdEndGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken);
                }
                // MSAA Resolve pass
                else
                {
                    cmdBeginGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken, "MSAA Resolve Pass");

                    // Transition world render target to be used as input texture in post process pass
                    {
                        TFRenderTargetBarrier barrier = { pRenderTargetMSAA, TF_RESOURCE_STATE_RENDER_TARGET,
                                                          TF_RESOURCE_STATE_PIXEL_SHADER_RESOURCE };
                        cmdResourceBarrier(graphicsCmd, 0, NULL, 0, NULL, 1, &barrier);
                    }
                    // Bind Render Targets
                    {
                        TFBindRenderTargetsDesc bindRenderTargets = {};
                        bindRenderTargets.mRenderTargetCount = 1;
                        bindRenderTargets.mRenderTargets[0] = { pScreenRenderTarget, TF_LOAD_ACTION_CLEAR };
                        cmdBindRenderTargets(graphicsCmd, &bindRenderTargets);
                        cmdSetViewport(graphicsCmd, 0.0f, 0.0f, (float)pScreenRenderTarget->mWidth, (float)pScreenRenderTarget->mHeight,
                                       0.0f, 1.0f);
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
            }

            if (gAppSettings.mDrawDebugTargets && !gAppSettings.mEnableVRS && gAppSettings.mMSAALevel > TF_SAMPLE_COUNT_1)
            {
                // Draw Debug GodRay pass
                if (gAppSettings.mEnableGodray)
                {
                    cmdBeginGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken, "Draw MSAA Debug RTs - GodRay");
                    drawMSAADebugRenderTargetsPass(graphicsCmd, frameIdx, gResolveGodRayPassIndex, pRenderTargetDebugGodRayMSAA);
                    cmdEndGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken);
                }
                // Draw Debug VB Shade pass
                {
                    cmdBeginGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken, "Draw MSAA Debug RTs - VB Shade");
                    drawMSAADebugRenderTargetsPass(graphicsCmd, frameIdx, gResolveFinalPassIndex, pRenderTargetDebugMSAA);
                    cmdEndGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken);
                }
            }

            // Resource Transition
            {
                const uint32_t maxNumBarriers = NUM_CULLING_VIEWPORTS * 2 + 6;
                uint32_t       barrierCount = 0;
                barrierCount = 0;
                TFBufferBarrier drawOutputRestoreBufferBarriers[maxNumBarriers] = {};
                drawOutputRestoreBufferBarriers[barrierCount++] = { pLightClusters[frameIdx], TF_RESOURCE_STATE_SHADER_RESOURCE,
                                                                    TF_RESOURCE_STATE_UNORDERED_ACCESS };
                drawOutputRestoreBufferBarriers[barrierCount++] = { pLightClustersCount[frameIdx], TF_RESOURCE_STATE_SHADER_RESOURCE,
                                                                    TF_RESOURCE_STATE_UNORDERED_ACCESS };
                drawOutputRestoreBufferBarriers[barrierCount++] = { pVisibilityBuffer->ppIndirectDrawArgBuffer[frameIdx],
                                                                    TF_RESOURCE_STATE_SHADER_RESOURCE | TF_RESOURCE_STATE_INDIRECT_ARGUMENT,
                                                                    TF_RESOURCE_STATE_UNORDERED_ACCESS };

                drawOutputRestoreBufferBarriers[barrierCount++] = { pVisibilityBuffer->ppIndirectDataBuffer[frameIdx],
                                                                    TF_RESOURCE_STATE_SHADER_RESOURCE, TF_RESOURCE_STATE_UNORDERED_ACCESS };

                for (uint32_t i = 0; i < gNumViews; ++i)
                {
                    drawOutputRestoreBufferBarriers[barrierCount++] = {
                        pVisibilityBuffer->ppFilteredIndexBuffer[frameIdx * NUM_CULLING_VIEWPORTS + i],
                        TF_RESOURCE_STATE_INDEX_BUFFER | TF_RESOURCE_STATE_SHADER_RESOURCE, TF_RESOURCE_STATE_UNORDERED_ACCESS
                    };
                }

                const bool            isProgMSAAEnabled = gAppSettings.mMSAALevel > TF_SAMPLE_COUNT_1 && !gAppSettings.mEnableVRS;
                const TFResourceState lastDepthBufferResourceState =
                    gAppSettings.mEnableVRS && (gAppSettings.mEnableGodray || !isProgMSAAEnabled) ? TF_RESOURCE_STATE_DEPTH_READ
                    : gAppSettings.mEnableGodray || isProgMSAAEnabled                             ? TF_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
                                                                                                  : TF_RESOURCE_STATE_DEPTH_WRITE;
                TFRenderTargetBarrier rtBarriers[] = { { pDepthBuffer, lastDepthBufferResourceState, TF_RESOURCE_STATE_DEPTH_WRITE } };
                cmdResourceBarrier(graphicsCmd, barrierCount, drawOutputRestoreBufferBarriers, 0, NULL,
                                   lastDepthBufferResourceState != TF_RESOURCE_STATE_DEPTH_WRITE ? 1 : 0, rtBarriers);
            }

            // Get the current render target for this frame
            acquireNextImage(pRenderer, pSwapChain, pImageAcquiredSemaphore[frameIdx], NULL, &presentIndex);

            // Draw final image pass
            {
                // Draws the final fullscreen image into the acquired swapchain target
                cmdBeginGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken, "Draw Final Image");

                // Resource Transition
                {
                    TFRenderTargetBarrier barriers[] = {
                        { pScreenRenderTarget, TF_RESOURCE_STATE_RENDER_TARGET, TF_RESOURCE_STATE_PIXEL_SHADER_RESOURCE },
                        { pSwapChain->ppRenderTargets[presentIndex], TF_RESOURCE_STATE_PRESENT, TF_RESOURCE_STATE_RENDER_TARGET }
                    };

                    cmdResourceBarrier(graphicsCmd, 0, NULL, 0, NULL, 2, barriers);
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
                    cmdBindDescriptorSet(graphicsCmd, (gAppSettings.mEnableVRS ? (2 - frameIdx) : 0), pDescriptorSetDisplayPerDraw);
                    cmdDraw(graphicsCmd, 3, 0);

#ifdef ENABLE_STREAMING
                    if (gAppSettings.mDrawDebugBoxes)
                    {
                        cmdBindPipeline(graphicsCmd, pPipelineLine);
                        cmdStreamingDebugDraw(graphicsCmd);
                    }
#endif
                }

                cmdBindRenderTargets(graphicsCmd, NULL);
                cmdEndGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken);
            }

            // UI pass
            {
                cmdBeginGpuTimestampQuery(graphicsCmd, gGraphicsProfileToken, "UI Pass");
                drawGUIPass(graphicsCmd, presentIndex);
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
            TFSemaphore* signalSemaphores[] = { graphicsElem.pSemaphore, pGraphicsAsyncSemaphores[frameIdx] };

            TFQueueSubmitDesc submitDesc = {};
            submitDesc.mCmdCount = 1;
            submitDesc.ppCmds = &graphicsCmd;
            submitDesc.mSignalSemaphoreCount = gAppSettings.mAsyncCompute ? TF_ARRAY_COUNT(signalSemaphores) : 1;
            submitDesc.ppSignalSemaphores = signalSemaphores;
            submitDesc.pSignalFence = graphicsElem.pFence;
            submitDesc.ppWaitSemaphores = waitSemaphores;
            submitDesc.mWaitSemaphoreCount =
                (gAppSettings.mAsyncCompute && gComputeSemaphores[frameIdx]) ? TF_ARRAY_COUNT(waitSemaphores) : 2;
            queueSubmit(pGraphicsQueue, &submitDesc);

            gPrevGraphicsSemaphore = gAppSettings.mAsyncCompute ? signalSemaphores[1] : NULL;

            TFQueuePresentDesc presentDesc = {};
            presentDesc.mIndex = (uint8_t)presentIndex;
            presentDesc.mWaitSemaphoreCount = 1;
            presentDesc.ppWaitSemaphores = &graphicsElem.pSemaphore;
            presentDesc.pSwapChain = pSwapChain;
            presentDesc.mSubmitDone = true;
            queuePresent(pGraphicsQueue, &presentDesc);
            flipProfiler();
        }

        ++gFrameCount;
    }

    const char* GetName() { return "15a_VisibilityBufferOIT"; }

private:
    bool addDescriptorSets()
    {
        TFDescriptorSetDesc setDesc = SRT_SET_DESC(SrtData, Persistent, 1, 0);
        addDescriptorSet(pRenderer, &setDesc, &pDescriptorSetPersistent);

        setDesc = SRT_SET_DESC(SrtData, PerFrame, gDataBufferCount, 0);
        addDescriptorSet(pRenderer, &setDesc, &pDescriptorSetPerFrame);

        setDesc = SRT_SET_DESC(SrtDisplay, PerDraw, 3, 0);
        addDescriptorSet(pRenderer, &setDesc, &pDescriptorSetDisplayPerDraw);

        // Pre Skin Vertexes
        setDesc = SRT_SET_DESC(SrtPreSkinVertexesComp, PerBatch, gDataBufferCount, 0);
        addDescriptorSet(pRenderer, &setDesc, &pDescriptorSetPreSkinVertexes[PRE_SKIN_SYNC]);

        setDesc = SRT_SET_DESC(SrtPreSkinVertexesComp, PerBatch, gDataBufferCount, 0);
        addDescriptorSet(pRenderer, &setDesc, &pDescriptorSetPreSkinVertexes[PRE_SKIN_ASYNC]);

        // God Ray Blur
        setDesc = SRT_SET_DESC(SrtGodrayBlurComp, PerDraw, gDataBufferCount * BLUR_PASS_TYPE_COUNT, 0);
        addDescriptorSet(pRenderer, &setDesc, &pDescriptorSetGodRayBlurPassPerBatch);

        // VB Pass transparent
        setDesc = SRT_SET_DESC(SrtVisibilityPassData, PerBatch, 1, 0);
        addDescriptorSet(pRenderer, &setDesc, &pDescriptorSetVisibilityPassTransparentPerBatch);

        // Resolve VRS
        setDesc = SRT_SET_DESC(SrtResolveVRSData, PerBatch, gDataBufferCount, 0);
        addDescriptorSet(pRenderer, &setDesc, &pDescriptorSetResolveVRSPerBatch);

        // Cluster lights
        setDesc = SRT_SET_DESC(SrtClusterLightsData, PerBatch, gDataBufferCount, 0);
        addDescriptorSet(pRenderer, &setDesc, &pDescriptorSetClusterLightsPerBatch);

        // Triangle filtering
        setDesc = SRT_SET_DESC(TriangleFilteringSrtData, PerBatch, gDataBufferCount, 0);
        addDescriptorSet(pRenderer, &setDesc, &pDescriptorTriangleFilteringPerBatch);

        const uint32_t resolveMaxSets = gDataBufferCount * gResolveTargetCount;
        setDesc = SRT_SET_DESC(SrtResolve, PerDraw, resolveMaxSets, 0);
        addDescriptorSet(pRenderer, &setDesc, &pDescriptorSetResolve);

        return true;
    }

    void removeDescriptorSets()
    {
        removeDescriptorSet(pRenderer, pDescriptorSetResolve);
        removeDescriptorSet(pRenderer, pDescriptorTriangleFilteringPerBatch);
        removeDescriptorSet(pRenderer, pDescriptorSetClusterLightsPerBatch);
        removeDescriptorSet(pRenderer, pDescriptorSetResolveVRSPerBatch);
        removeDescriptorSet(pRenderer, pDescriptorSetVisibilityPassTransparentPerBatch);
        removeDescriptorSet(pRenderer, pDescriptorSetDisplayPerDraw);
        removeDescriptorSet(pRenderer, pDescriptorSetPerFrame);
        removeDescriptorSet(pRenderer, pDescriptorSetPersistent);
        removeDescriptorSet(pRenderer, pDescriptorSetGodRayBlurPassPerBatch);
        removeDescriptorSet(pRenderer, pDescriptorSetPreSkinVertexes[PRE_SKIN_SYNC]);
        removeDescriptorSet(pRenderer, pDescriptorSetPreSkinVertexes[PRE_SKIN_ASYNC]);
    }

    void updateDescriptorSets()
    {
        // Persistent set
        TFTexture*         godrayTextures[] = { pRenderTargetGodRay[0]->pTexture, pRenderTargetGodRay[1]->pTexture };
        constexpr uint32_t PERSIST_PARAM_COUNT = 19;
        TFDescriptorData   persistentSetParams[PERSIST_PARAM_COUNT] = {};
        persistentSetParams[0].mIndex = SRT_RES_IDX(SrtData, Persistent, gVBTex);
        persistentSetParams[0].ppTextures = &pRenderTargetVBPass->pTexture;
        persistentSetParams[1].mIndex = SRT_RES_IDX(SrtData, Persistent, gVertexPositionBuffer);
        persistentSetParams[1].ppBuffers = &GET_GEOMETRY_VERTEX_BUFFER(TF_SEMANTIC_POSITION);
        persistentSetParams[2].mIndex = SRT_RES_IDX(SrtData, Persistent, gVertexTexCoordBuffer);
        persistentSetParams[2].ppBuffers = &GET_GEOMETRY_VERTEX_BUFFER(TF_SEMANTIC_TEXCOORD0);
        persistentSetParams[3].mIndex = SRT_RES_IDX(SrtData, Persistent, gVertexNormalBuffer);
        persistentSetParams[3].ppBuffers = &GET_GEOMETRY_VERTEX_BUFFER(TF_SEMANTIC_NORMAL);
        persistentSetParams[4].mIndex = SRT_RES_IDX(SrtData, Persistent, gLights);
        persistentSetParams[4].ppBuffers = &pLightsBuffer;
        persistentSetParams[5].mIndex = SRT_RES_IDX(SrtData, Persistent, gShadowMap);
        persistentSetParams[5].ppTextures = &pRenderTargetShadow->pTexture;
        persistentSetParams[6].mIndex = SRT_RES_IDX(SrtData, Persistent, gHeadIndexBufferSRV);
        persistentSetParams[6].ppBuffers = &pHeadIndexBufferOIT;
        persistentSetParams[7].mIndex = SRT_RES_IDX(SrtData, Persistent, gVBDepthLinkedListSRV);
        persistentSetParams[7].ppBuffers = &pVisBufLinkedListBufferOIT;
        persistentSetParams[8].mIndex = SRT_RES_IDX(SrtData, Persistent, gGodrayTexture);
        persistentSetParams[8].ppTextures = &pRenderTargetGodRay[0]->pTexture;
        persistentSetParams[8].mCount = 1;
        persistentSetParams[9].mIndex = SRT_RES_IDX(SrtData, Persistent, gVBTextureSampler);
        persistentSetParams[9].ppSamplers = &pSamplerTrilinearAniso;
        persistentSetParams[10].mIndex = SRT_RES_IDX(SrtData, Persistent, gVBTextureFilter);
        persistentSetParams[10].ppSamplers = &pSamplerPointClamp;
        persistentSetParams[11].mIndex = SRT_RES_IDX(SrtData, Persistent, gVertexWeightsBuffer);
        persistentSetParams[11].ppBuffers = &GET_GEOMETRY_VERTEX_BUFFER(TF_SEMANTIC_WEIGHTS);
        persistentSetParams[12].mIndex = SRT_RES_IDX(SrtData, Persistent, gVertexJointsBuffer);
        persistentSetParams[12].ppBuffers = &GET_GEOMETRY_VERTEX_BUFFER(TF_SEMANTIC_JOINTS);
        persistentSetParams[13].mIndex = SRT_RES_IDX(SrtData, Persistent, gIndexDataBuffer);
        persistentSetParams[13].ppBuffers = &pGeometryBuffer->mIndex.pBuffer;
        persistentSetParams[14].mIndex = SRT_RES_IDX(SrtData, Persistent, gDepthTexture);
        persistentSetParams[14].ppTextures = &pDepthBuffer->pTexture;
        persistentSetParams[15].mIndex = SRT_RES_IDX(SrtData, Persistent, gGodrayBlurWeights);
        persistentSetParams[15].ppBuffers = &pBufferBlurWeights;
        persistentSetParams[16].mCount = 1;
        persistentSetParams[16].mIndex = SRT_RES_IDX(SrtData, Persistent, gSkyboxTexture);
        persistentSetParams[16].ppTextures = &pSkyboxTri;
        persistentSetParams[17].mIndex = SRT_RES_IDX(SrtData, Persistent, gVBConstantBuffer);
        persistentSetParams[17].ppBuffers = &pVisibilityBuffer->pVBConstantBuffer;
        persistentSetParams[18].mIndex = SRT_RES_IDX(SrtData, Persistent, gMSAAStencil);
        persistentSetParams[18].ppTextureDescriptors = &pDescriptorMSAAStencil;
        persistentSetParams[18].mUseTextureDescriptors = 1;
        updateDescriptorSet(pRenderer, 0, pDescriptorSetPersistent, PERSIST_PARAM_COUNT, persistentSetParams);

        TFDescriptorData vbPassTransparentParams[3] = {};
        vbPassTransparentParams[0].mIndex = SRT_RES_IDX(SrtVisibilityPassData, PerBatch, gHeadIndexBufferUAV);
        vbPassTransparentParams[0].ppBuffers = &pHeadIndexBufferOIT;
        vbPassTransparentParams[1].mIndex = SRT_RES_IDX(SrtVisibilityPassData, PerBatch, gVBDepthLinkedListUAV);
        vbPassTransparentParams[1].ppBuffers = &pVisBufLinkedListBufferOIT;
        vbPassTransparentParams[2].mIndex = SRT_RES_IDX(SrtVisibilityPassData, PerBatch, gGeometryCountBuffer);
        vbPassTransparentParams[2].ppBuffers = &pGeometryCountBufferOIT;
        updateDescriptorSet(pRenderer, 0, pDescriptorSetVisibilityPassTransparentPerBatch, 3, vbPassTransparentParams);

        // PerFrame set
        TFDescriptorData perFrameParams[19] = {};
        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            perFrameParams[0].mIndex = SRT_RES_IDX(SrtData, PerFrame, gJointMatrixes);
            perFrameParams[0].ppBuffers = &pJointMatrixBuffer[i];
            perFrameParams[1].mIndex = SRT_RES_IDX(SrtData, PerFrame, gOutputBufferOffsets);
            perFrameParams[1].ppBuffers = &pAsyncComputeVertexBuffers->pShaderOffsets[i];
            perFrameParams[2].mIndex = SRT_RES_IDX(SrtData, PerFrame, gPerFrameConstants);
            perFrameParams[2].ppBuffers = &pPerFrameVBUniformBuffers[VB_UB_GRAPHICS][i];
            perFrameParams[3].mIndex = SRT_RES_IDX(SrtData, PerFrame, gPerFrameConstantsComp);
            perFrameParams[3].ppBuffers = &pPerFrameVBUniformBuffers[VB_UB_COMPUTE][i];
            perFrameParams[4].mIndex = SRT_RES_IDX(SrtData, PerFrame, gMeshDataBuffer);
            perFrameParams[4].ppBuffers = &pMeshDataBuffer[i];
            perFrameParams[5].mIndex = SRT_RES_IDX(SrtData, PerFrame, gFilterDispatchGroupDataBuffer);
            perFrameParams[5].ppBuffers = &pVisibilityBuffer->ppFilterDispatchGroupDataBuffer[i];
            perFrameParams[6].mIndex = SRT_RES_IDX(SrtData, PerFrame, gIndirectDataBuffer);
            perFrameParams[6].ppBuffers = &pVisibilityBuffer->ppIndirectDataBuffer[i];
            perFrameParams[7].mIndex = SRT_RES_IDX(SrtData, PerFrame, gLightClustersCount);
            perFrameParams[7].ppBuffers = &pLightClustersCount[i];
            perFrameParams[8].mIndex = SRT_RES_IDX(SrtData, PerFrame, gLightClusters);
            perFrameParams[8].ppBuffers = &pLightClusters[i];
            perFrameParams[9].mIndex = SRT_RES_IDX(SrtData, PerFrame, gFilteredIndexBuffer);
            perFrameParams[9].ppBuffers = &pVisibilityBuffer->ppFilteredIndexBuffer[i * NUM_CULLING_VIEWPORTS + VIEW_CAMERA];
            perFrameParams[10].mIndex = SRT_RES_IDX(SrtData, PerFrame, gSkyboxUniformBuffer);
            perFrameParams[10].ppBuffers = &pUniformBufferSkyTri[i];
            perFrameParams[11].mIndex = SRT_RES_IDX(SrtData, PerFrame, gPrevFrameTex);
            perFrameParams[11].ppTextures = &pResolveVRSRenderTarget[1 - i]->pTexture;
            perFrameParams[12].mIndex = SRT_RES_IDX(SrtData, PerFrame, gPrevHistoryTex);
            perFrameParams[12].ppTextures = &pHistoryRenderTarget[1 - i]->pTexture;
            perFrameParams[13].mIndex = SRT_RES_IDX(SrtData, PerFrame, gHistoryTex);
            perFrameParams[13].ppTextures = &pHistoryRenderTarget[i]->pTexture;
            perFrameParams[14].mIndex = SRT_RES_IDX(SrtData, PerFrame, gMsaaSource);
            perFrameParams[14].ppTextures = &pRenderTargetMSAA->pTexture;
            perFrameParams[15].mIndex = SRT_RES_IDX(SrtData, PerFrame, gHistoryTexVBShade);
            perFrameParams[15].ppTextures = &pHistoryRenderTarget[i]->pTexture;
            perFrameParams[16].mIndex = SRT_RES_IDX(SrtData, PerFrame, gAllTextures);
            perFrameParams[16].mCount = (uint32_t)gAllTexturesCount;
#ifdef ENABLE_STREAMING
            TFStreamingState currState;
            getStreamingState(&currState);
            perFrameParams[16].ppTextures = currState.ppOrderedTextures;
#else
            perFrameParams[16].ppTextures = gGenesisPackage->ppAllTextures;
#endif // ENABLE_STREAMING

            updateDescriptorSet(pRenderer, i, pDescriptorSetPerFrame, 17, perFrameParams);
        }

        // Resolve VRS set
        TFDescriptorData resolveVRSParams[2] = {};

        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            resolveVRSParams[0].mIndex = SRT_RES_IDX(SrtResolveVRSData, PerBatch, gResolvedTex);
            resolveVRSParams[0].ppTextures = &pResolveVRSRenderTarget[i]->pTexture;
            resolveVRSParams[1].mIndex = SRT_RES_IDX(SrtResolveVRSData, PerBatch, gDebugVRSTex);
            resolveVRSParams[1].ppTextures = &pDebugVRSRenderTarget->pTexture;
            updateDescriptorSet(pRenderer, i, pDescriptorSetResolveVRSPerBatch, 2, resolveVRSParams);
        }

        // Cluster lights set
        TFDescriptorData clusterLightsParams[2] = {};

        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            clusterLightsParams[0].mIndex = SRT_RES_IDX(SrtClusterLightsData, PerBatch, gLightClustersCountRW);
            clusterLightsParams[0].ppBuffers = &pLightClustersCount[i];
            clusterLightsParams[1].mIndex = SRT_RES_IDX(SrtClusterLightsData, PerBatch, gLightClustersRW);
            clusterLightsParams[1].ppBuffers = &pLightClusters[i];
            updateDescriptorSet(pRenderer, i, pDescriptorSetClusterLightsPerBatch, 2, clusterLightsParams);
        }

        // Triangle filtering set
        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            TFDescriptorData triangleFilteringParams[4] = {};
            triangleFilteringParams[0].mIndex = SRT_RES_IDX(TriangleFilteringSrtData, PerBatch, gIndirectDrawClearArgsRW);
            triangleFilteringParams[0].ppBuffers = &pVisibilityBuffer->ppIndirectDrawArgBuffer[i];
            triangleFilteringParams[1].mIndex = SRT_RES_IDX(TriangleFilteringSrtData, PerBatch, gFilteredIndexBufferRW);
            triangleFilteringParams[1].mCount = gNumViews;
            triangleFilteringParams[1].ppBuffers = &pVisibilityBuffer->ppFilteredIndexBuffer[i * gNumViews];
            triangleFilteringParams[2].mIndex = SRT_RES_IDX(TriangleFilteringSrtData, PerBatch, gIndirectDataBufferRW);
            triangleFilteringParams[2].ppBuffers = &pVisibilityBuffer->ppIndirectDataBuffer[i];
            triangleFilteringParams[3].mIndex = SRT_RES_IDX(TriangleFilteringSrtData, PerBatch, gPixelCoverageBufferRW);
            triangleFilteringParams[3].ppBuffers = &gPixelCoverageBuffers[i];
            updateDescriptorSet(pRenderer, i, pDescriptorTriangleFilteringPerBatch, 4, triangleFilteringParams);
        }

        // Pre Skin Triangles
        {
            TFDescriptorData filterParams[5] = {};

            filterParams[0].mIndex = SRT_RES_IDX(SrtPreSkinVertexesComp, PerBatch, gVertexPositionBufferSkinned);
            filterParams[0].ppBuffers = &GET_GEOMETRY_VERTEX_BUFFER(TF_SEMANTIC_POSITION);
            filterParams[1].mIndex = SRT_RES_IDX(SrtPreSkinVertexesComp, PerBatch, gVertexNormalBufferSkinned);
            filterParams[1].ppBuffers = &GET_GEOMETRY_VERTEX_BUFFER(TF_SEMANTIC_NORMAL);
            for (uint32_t i = 0; i < gDataBufferCount; ++i)
            {
                filterParams[2].mIndex = SRT_RES_IDX(SrtPreSkinVertexesComp, PerBatch, gVertexPositionBufferSkinnedAsync);
                filterParams[2].ppBuffers = &pAsyncComputeVertexBuffers->mPositions.pPreSkinBuffers[i];
                filterParams[3].mIndex = SRT_RES_IDX(SrtPreSkinVertexesComp, PerBatch, gVertexNormalBufferSkinnedAsync);
                filterParams[3].ppBuffers = &pAsyncComputeVertexBuffers->mNormals.pPreSkinBuffers[i];
                filterParams[4].mIndex = SRT_RES_IDX(SrtPreSkinVertexesComp, PerBatch, gBatchData);
                filterParams[4].ppBuffers = &pPreSkinBufferOffsets;
                updateDescriptorSet(pRenderer, i, pDescriptorSetPreSkinVertexes[PRE_SKIN_SYNC], 5, filterParams);
                filterParams[2].mIndex = SRT_RES_IDX(SrtPreSkinVertexesComp, PerBatch, gVertexPositionBufferSkinnedAsync);
                filterParams[2].ppBuffers = &pAsyncComputeVertexBuffers->mPositions.pPreSkinBuffers[i];
                filterParams[3].mIndex = SRT_RES_IDX(SrtPreSkinVertexesComp, PerBatch, gVertexNormalBufferSkinnedAsync);
                filterParams[3].ppBuffers = &pAsyncComputeVertexBuffers->mNormals.pPreSkinBuffers[i];
                filterParams[4].mIndex = SRT_RES_IDX(SrtPreSkinVertexesComp, PerBatch, gBatchData);
                filterParams[4].ppBuffers = &pAsyncComputeVertexBuffers->pShaderOffsets[i];
                updateDescriptorSet(pRenderer, i, pDescriptorSetPreSkinVertexes[PRE_SKIN_ASYNC], 5, filterParams);
            }
        }

        // God Ray Blur
        {
            TFDescriptorData params[2] = {};
            params[0].mIndex = SRT_RES_IDX(SrtGodrayBlurComp, PerDraw, gGodrayTextures);
            params[0].ppTextures = godrayTextures;
            params[0].mCount = 2;
            params[1].mIndex = SRT_RES_IDX(SrtGodrayBlurComp, PerDraw, gBlurParams);
            for (uint32_t i = 0; i < gDataBufferCount; i++)
            {
                params[1].ppBuffers = &pGodRayBlurBuffer[BLUR_PASS_TYPE_HORIZONTAL][i];
                params[1].mCount = 1;
                updateDescriptorSet(pRenderer, i * BLUR_PASS_TYPE_COUNT, pDescriptorSetGodRayBlurPassPerBatch, 2, params);
                params[1].ppBuffers = &pGodRayBlurBuffer[BLUR_PASS_TYPE_VERTICAL][i];
                params[1].mCount = 1;
                updateDescriptorSet(pRenderer, i * BLUR_PASS_TYPE_COUNT + BLUR_PASS_TYPE_VERTICAL, pDescriptorSetGodRayBlurPassPerBatch, 2,
                                    params);
            }
        }

        // Present
        {
            TFDescriptorData params[3] = {};
            params[0].mIndex = SRT_RES_IDX(SrtDisplay, PerDraw, gDisplayTexture);
            params[0].ppTextures = &pIntermediateRenderTarget->pTexture;
            updateDescriptorSet(pRenderer, 0, pDescriptorSetDisplayPerDraw, 1, params);
            params[0].ppTextures = &pResolveVRSRenderTarget[0]->pTexture;
            updateDescriptorSet(pRenderer, 1, pDescriptorSetDisplayPerDraw, 1, params);
            params[0].ppTextures = &pResolveVRSRenderTarget[1]->pTexture;
            updateDescriptorSet(pRenderer, 2, pDescriptorSetDisplayPerDraw, 1, params);
        }

        // Resolve
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
    // Order Independent Transparency
    /************************************************************************/

    void addOrderIndependentTransparencyResources()
    {
        const uint32_t width = gSceneRes.mWidth;
        const uint32_t height = gSceneRes.mHeight;

        const uint32_t maxNodeCountOIT = OIT_MAX_FRAG_COUNT * width * height;

        TFBufferLoadDesc oitHeadIndexBufferDesc = {};
        oitHeadIndexBufferDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_RW_BUFFER | TF_DESCRIPTOR_TYPE_BUFFER;
        oitHeadIndexBufferDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_GPU_ONLY;
        oitHeadIndexBufferDesc.mDesc.mElementCount = width * height;
        oitHeadIndexBufferDesc.mDesc.mStructStride = sizeof(uint32_t);
        oitHeadIndexBufferDesc.mDesc.mStartState = TF_RESOURCE_STATE_UNORDERED_ACCESS;
        oitHeadIndexBufferDesc.mDesc.mSize = oitHeadIndexBufferDesc.mDesc.mElementCount * oitHeadIndexBufferDesc.mDesc.mStructStride;
        oitHeadIndexBufferDesc.ppBuffer = &pHeadIndexBufferOIT;
        oitHeadIndexBufferDesc.mDesc.pName = "OIT Head Index";
        addResource(&oitHeadIndexBufferDesc, NULL);

        TFBufferLoadDesc geometryCountBufferDesc = {};
        geometryCountBufferDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_RW_BUFFER | TF_DESCRIPTOR_TYPE_BUFFER;
        geometryCountBufferDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_GPU_ONLY;
        geometryCountBufferDesc.mDesc.mElementCount = 1;
        geometryCountBufferDesc.mDesc.mStructStride = sizeof(GeometryCountOIT);
        geometryCountBufferDesc.mDesc.mStartState = TF_RESOURCE_STATE_UNORDERED_ACCESS;
        geometryCountBufferDesc.mDesc.mSize = geometryCountBufferDesc.mDesc.mElementCount * geometryCountBufferDesc.mDesc.mStructStride;
        geometryCountBufferDesc.mDesc.pName = "OIT Geometry Count";
        geometryCountBufferDesc.ppBuffer = &pGeometryCountBufferOIT;
        addResource(&geometryCountBufferDesc, NULL);

        TFBufferLoadDesc linkedListBufferDesc = {};
        linkedListBufferDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_RW_BUFFER | TF_DESCRIPTOR_TYPE_BUFFER;
        linkedListBufferDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_GPU_ONLY;
        linkedListBufferDesc.mDesc.mElementCount = maxNodeCountOIT;
        linkedListBufferDesc.mDesc.mStructStride = sizeof(TransparentNodeOIT);
        linkedListBufferDesc.mDesc.mStartState = TF_RESOURCE_STATE_UNORDERED_ACCESS;
        linkedListBufferDesc.mDesc.mSize = linkedListBufferDesc.mDesc.mElementCount * linkedListBufferDesc.mDesc.mStructStride;
        linkedListBufferDesc.ppBuffer = &pVisBufLinkedListBufferOIT;
        linkedListBufferDesc.mDesc.pName = "OIT VisBuf Linked List";
        addResource(&linkedListBufferDesc, NULL);
    }

    void removeOrderIndependentTransparencyResources()
    {
        removeResource(pGeometryCountBufferOIT);

        removeResource(pHeadIndexBufferOIT);
        removeResource(pVisBufLinkedListBufferOIT);
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
        const bool      wantsHDR = OUTPUT_MODE_HDR10 == gAppSettings.mOutputMode;
        const bool      supportsHDR = TinyImageFormat_UNDEFINED != hdrFormat;
        if (pRenderer->pGpu->mHDRSupported)
        {
            pGuiOutputSupportsHDRLabel = supportsHDR ? "Current Output Supports HDR" : "Current Output Does Not Support HDR";
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
        const uint32_t width = gSceneRes.mWidth;
        const uint32_t height = gSceneRes.mHeight;

        TFClearValue optimizedDepthClear = { { 0.0f, 0 } };
        TFClearValue optimizedColorClearBlack = { { 0.0f, 0.0f, 0.0f, 0.0f } };
        TFClearValue optimizedColorClearWhite = { { 1.0f, 1.0f, 1.0f, 1.0f } };

        uint32_t currentOffsetESRAM = 0;

        /************************************************************************/
        // Shadow pass render target
        /************************************************************************/
        TF_ESRAM_BEGIN_ALLOC(pRenderer, "Shadow Maps", currentOffsetESRAM);
        TFRenderTargetDesc shadowRTDesc = {};
        shadowRTDesc.mArraySize = 1;
        shadowRTDesc.mClearValue = optimizedDepthClear;
        shadowRTDesc.mDepth = 1;
        shadowRTDesc.mDescriptors = TF_DESCRIPTOR_TYPE_TEXTURE;
        shadowRTDesc.mFormat = TinyImageFormat_D32_SFLOAT;
        shadowRTDesc.mStartState = TF_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        shadowRTDesc.mWidth = gShadowMapSize;
        shadowRTDesc.mSampleCount = TF_SAMPLE_COUNT_1;
        shadowRTDesc.mSampleQuality = 0;
        shadowRTDesc.mFlags = TF_TEXTURE_CREATION_FLAG_ESRAM;
        shadowRTDesc.mFlags |= TF_TEXTURE_CREATION_FLAG_TEXTURE_COMPATABILITY;
        shadowRTDesc.mHeight = gShadowMapSize;
        shadowRTDesc.pName = "Shadow Map RT";
        addRenderTarget(pRenderer, &shadowRTDesc, &pRenderTargetShadow);
        TF_ESRAM_CURRENT_OFFSET(pRenderer, shadowMapOffsetESRAM);
        TF_ESRAM_END_ALLOC(pRenderer);

        currentOffsetESRAM = max(shadowMapOffsetESRAM, currentOffsetESRAM);

        /************************************************************************/
        // Main depth buffer
        /************************************************************************/
        // Add depth buffer
        TF_ESRAM_BEGIN_ALLOC(pRenderer, "Depth Buffer", currentOffsetESRAM);
        TFRenderTargetDesc depthRT = {};
        depthRT.mArraySize = 1;
        depthRT.mClearValue.depth = 0.0f; // optimizedDepthClear;
        depthRT.mClearValue.stencil = 0;
        depthRT.mDepth = 1;
        depthRT.mDescriptors = TF_DESCRIPTOR_TYPE_TEXTURE;
        depthRT.mFormat = TinyImageFormat_D32_SFLOAT_S8_UINT;
        depthRT.mStartState = TF_RESOURCE_STATE_DEPTH_WRITE;
        depthRT.mHeight = height / gDivider;
        depthRT.mSampleCount = gAppSettings.mMSAALevel;
        depthRT.mSampleQuality = 0;
        depthRT.mWidth = width / gDivider;
        depthRT.pName = "Depth Buffer RT";
        depthRT.mFlags = TF_TEXTURE_CREATION_FLAG_SAMPLE_LOCATIONS_COMPATIBLE;
        depthRT.mFlags |= TF_TEXTURE_CREATION_FLAG_ESRAM;
        addRenderTarget(pRenderer, &depthRT, &pDepthBuffer);
        TF_ESRAM_CURRENT_OFFSET(pRenderer, depthOffsetESRAM);
        TF_ESRAM_END_ALLOC(pRenderer);

        currentOffsetESRAM = max(depthOffsetESRAM, currentOffsetESRAM);

        /************************************************************************/
        // Visibility buffer pass render target
        /************************************************************************/
        TF_ESRAM_BEGIN_ALLOC(pRenderer, "VB RT", currentOffsetESRAM);
        TFRenderTargetDesc vbRTDesc = {};
        vbRTDesc.mArraySize = 1;
        vbRTDesc.mClearValue = optimizedColorClearWhite;
        vbRTDesc.mDepth = 1;
        vbRTDesc.mDescriptors = TF_DESCRIPTOR_TYPE_TEXTURE;
        vbRTDesc.mFormat = TinyImageFormat_R8G8B8A8_UNORM;
        vbRTDesc.mStartState = TF_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        vbRTDesc.mHeight = height / gDivider;
        vbRTDesc.mSampleCount = gAppSettings.mMSAALevel;
        vbRTDesc.mSampleQuality = 0;
        vbRTDesc.mFlags = TF_TEXTURE_CREATION_FLAG_ESRAM;
        vbRTDesc.mWidth = width / gDivider;
        vbRTDesc.pName = "VB RT";
        if (vbRTDesc.mSampleCount == TF_SAMPLE_COUNT_1)
        {
            vbRTDesc.mFlags |= TF_TEXTURE_CREATION_FLAG_TEXTURE_COMPATABILITY;
        }
        addRenderTarget(pRenderer, &vbRTDesc, &pRenderTargetVBPass);
        TF_ESRAM_CURRENT_OFFSET(pRenderer, vbOffsetESRAM);
        TF_ESRAM_END_ALLOC(pRenderer);

        currentOffsetESRAM = max(vbOffsetESRAM, currentOffsetESRAM);

        TF_ESRAM_BEGIN_ALLOC(pRenderer, "OIT Depth", currentOffsetESRAM);
        // Add OIT dummy depth buffer
        TFRenderTargetDesc oitDepthRT = {};
        oitDepthRT.mArraySize = 1;
        oitDepthRT.mClearValue = optimizedDepthClear;
        oitDepthRT.mDepth = 1;
        oitDepthRT.mDescriptors = TF_DESCRIPTOR_TYPE_TEXTURE;
        oitDepthRT.mFormat = TinyImageFormat_D32_SFLOAT;
        oitDepthRT.mStartState = TF_RESOURCE_STATE_SHADER_RESOURCE;
        oitDepthRT.mHeight = height;
        oitDepthRT.mSampleCount = TF_SAMPLE_COUNT_1;
        oitDepthRT.mSampleQuality = 0;
        oitDepthRT.mFlags = TF_TEXTURE_CREATION_FLAG_NONE;
        oitDepthRT.mFlags |= TF_TEXTURE_CREATION_FLAG_ESRAM;
        oitDepthRT.mFlags |= TF_TEXTURE_CREATION_FLAG_TEXTURE_COMPATABILITY;
        oitDepthRT.mWidth = width;
        oitDepthRT.pName = "Depth Buffer OIT RT";
        addRenderTarget(pRenderer, &oitDepthRT, &pDepthBufferOIT);
        TF_ESRAM_CURRENT_OFFSET(pRenderer, oitOffsetESRAM);
        TF_ESRAM_END_ALLOC(pRenderer);

        /************************************************************************/
        // Intermediate render target
        /************************************************************************/
        TF_ESRAM_BEGIN_ALLOC(pRenderer, "Intermediate", currentOffsetESRAM);
        TFRenderTargetDesc postProcRTDesc = {};
        postProcRTDesc.mArraySize = 1;
        postProcRTDesc.mClearValue = { { 0.0f, 0.0f, 0.0f, 0.0f } };
        postProcRTDesc.mDepth = 1;
        postProcRTDesc.mDescriptors = TF_DESCRIPTOR_TYPE_TEXTURE;
        postProcRTDesc.mFormat = pSwapChain->mFormat;
        postProcRTDesc.mStartState = TF_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        postProcRTDesc.mHeight = height;
        postProcRTDesc.mWidth = width;
        postProcRTDesc.mSampleCount = pSwapChain->ppRenderTargets[0]->mSampleCount;
        postProcRTDesc.mSampleQuality = pSwapChain->ppRenderTargets[0]->mSampleQuality;
        postProcRTDesc.mFlags = TF_TEXTURE_CREATION_FLAG_ESRAM;
        if (postProcRTDesc.mSampleCount == TF_SAMPLE_COUNT_1)
        {
            postProcRTDesc.mFlags |= TF_TEXTURE_CREATION_FLAG_DCC | TF_TEXTURE_CREATION_FLAG_TEXTURE_COMPATABILITY;
        }
        postProcRTDesc.pName = "pIntermediateRenderTarget";
        addRenderTarget(pRenderer, &postProcRTDesc, &pIntermediateRenderTarget);
        TF_ESRAM_CURRENT_OFFSET(pRenderer, intermediateOffsetESRAM);
        TF_ESRAM_END_ALLOC(pRenderer);

        /************************************************************************/
        // MSAA render target
        /************************************************************************/
        TF_ESRAM_BEGIN_ALLOC(pRenderer, "MSAA RT", currentOffsetESRAM);
        TFRenderTargetDesc msaaRTDesc = {};
        msaaRTDesc.mArraySize = 1;
        msaaRTDesc.mClearValue = optimizedColorClearBlack;
        msaaRTDesc.mDepth = 1;
        msaaRTDesc.mDescriptors = TF_DESCRIPTOR_TYPE_TEXTURE;
        msaaRTDesc.mFormat = pSwapChain->mFormat;
        msaaRTDesc.mStartState = TF_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        msaaRTDesc.mHeight = height / gDivider;
        msaaRTDesc.mSampleCount = gAppSettings.mMSAALevel;
        msaaRTDesc.mSampleQuality = 0;
        msaaRTDesc.mWidth = width / gDivider;
        msaaRTDesc.pName = "MSAA RT";
        msaaRTDesc.mFlags = TF_TEXTURE_CREATION_FLAG_ESRAM;
        if (msaaRTDesc.mSampleCount == TF_SAMPLE_COUNT_1)
        {
            msaaRTDesc.mFlags |= TF_TEXTURE_CREATION_FLAG_DCC | TF_TEXTURE_CREATION_FLAG_TEXTURE_COMPATABILITY;
        }
        addRenderTarget(pRenderer, &msaaRTDesc, &pRenderTargetMSAA);
        TF_ESRAM_CURRENT_OFFSET(pRenderer, msaaOffsetESRAM);
        TF_ESRAM_END_ALLOC(pRenderer);

        // Debug MSAA Render Target. It will only be used if drawing debug targets is enabled.
        msaaRTDesc.mSampleCount = TF_SAMPLE_COUNT_1;
        msaaRTDesc.mFlags |= TF_TEXTURE_CREATION_FLAG_DCC | TF_TEXTURE_CREATION_FLAG_TEXTURE_COMPATABILITY;
        msaaRTDesc.mStartState = TF_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        msaaRTDesc.mFormat = gDebugRTFormat;
        msaaRTDesc.pName = "MSAA Debug RT";
        addRenderTarget(pRenderer, &msaaRTDesc, &pRenderTargetDebugMSAA);

        currentOffsetESRAM = max(msaaOffsetESRAM, max(intermediateOffsetESRAM, max(oitOffsetESRAM, currentOffsetESRAM)));

        /************************************************************************/
        // VRS History render targets
        /************************************************************************/
        TFRenderTargetDesc historyRTDesc = {};
        historyRTDesc.mWidth = width / gDivider;
        historyRTDesc.mHeight = height / gDivider;
        historyRTDesc.mDepth = 1;
        historyRTDesc.mArraySize = 1;
        historyRTDesc.mMipLevels = 1;
        historyRTDesc.mFlags = TF_TEXTURE_CREATION_FLAG_OWN_MEMORY_BIT;
        historyRTDesc.mSampleCount = TF_SAMPLE_COUNT_4;
        historyRTDesc.mFormat = TinyImageFormat_R8_UINT;
        historyRTDesc.mStartState = TF_RESOURCE_STATE_SHADER_RESOURCE;
        historyRTDesc.mClearValue.r = 0.0f;
        historyRTDesc.mSampleQuality = 0;
        historyRTDesc.mDescriptors = TF_DESCRIPTOR_TYPE_TEXTURE;
        historyRTDesc.pName = "Color History RT A";
        addRenderTarget(pRenderer, &historyRTDesc, &pHistoryRenderTarget[0]);
        historyRTDesc.pName = "Color History RT B";
        addRenderTarget(pRenderer, &historyRTDesc, &pHistoryRenderTarget[1]);
        /************************************************************************/
        // VRS Resolve and Debug render targets
        /************************************************************************/
        TFRenderTargetDesc resolveVRSRTDesc = {};
        resolveVRSRTDesc.mWidth = width;
        resolveVRSRTDesc.mHeight = height;
        resolveVRSRTDesc.mClearValue = { { 0.0f, 0.0f, 0.0f, 0.0f } };
        resolveVRSRTDesc.mDepth = 1;
        resolveVRSRTDesc.mDescriptors = TF_DESCRIPTOR_TYPE_TEXTURE | TF_DESCRIPTOR_TYPE_RW_TEXTURE;
        resolveVRSRTDesc.mArraySize = 1;
        resolveVRSRTDesc.mMipLevels = 1;
        resolveVRSRTDesc.mSampleCount = TF_SAMPLE_COUNT_1;
        resolveVRSRTDesc.mFormat = TinyImageFormat_B10G11R11_UFLOAT;
        resolveVRSRTDesc.mStartState = TF_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        resolveVRSRTDesc.mSampleQuality = 0;
        resolveVRSRTDesc.mFlags |= TF_TEXTURE_CREATION_FLAG_TEXTURE_COMPATABILITY | TF_TEXTURE_CREATION_FLAG_DCC;
        resolveVRSRTDesc.pName = "VRS Resolve RT A";
        addRenderTarget(pRenderer, &resolveVRSRTDesc, &pResolveVRSRenderTarget[0]);
        resolveVRSRTDesc.pName = "VRS Resolve RT B";
        addRenderTarget(pRenderer, &resolveVRSRTDesc, &pResolveVRSRenderTarget[1]);
        resolveVRSRTDesc.pName = "VRS Debug RT";
        resolveVRSRTDesc.mFormat = TinyImageFormat_R8G8B8A8_UNORM;
        addRenderTarget(pRenderer, &resolveVRSRTDesc, &pDebugVRSRenderTarget);

        /************************************************************************/
        // GodRay render targets
        /************************************************************************/
        TinyImageFormat    GRRTFormat = (pRenderer->pGpu->mFormatCaps[TinyImageFormat_R10G10B10A2_UNORM] & (TF_FORMAT_CAP_READ_WRITE))
                                            ? TinyImageFormat_R10G10B10A2_UNORM
                                            : TinyImageFormat_R8G8B8A8_UNORM;
        TFRenderTargetDesc GRRTDesc = {};
        GRRTDesc.mArraySize = 1;
        GRRTDesc.mClearValue = { { 0.0f, 0.0f, 0.0f, 1.0f } };
        GRRTDesc.mDepth = 1;
        GRRTDesc.mStartState = TF_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        GRRTDesc.mHeight = height / GODRAY_SCALE;
        GRRTDesc.mWidth = width / GODRAY_SCALE;
        GRRTDesc.mFormat = GRRTFormat;
        GRRTDesc.mDescriptors = TF_DESCRIPTOR_TYPE_RW_TEXTURE | TF_DESCRIPTOR_TYPE_TEXTURE;
        GRRTDesc.mSampleCount = pSwapChain->ppRenderTargets[0]->mSampleCount;
        GRRTDesc.mSampleQuality = pSwapChain->ppRenderTargets[0]->mSampleQuality;
        if (GRRTDesc.mSampleCount == TF_SAMPLE_COUNT_1)
        {
            GRRTDesc.mFlags |= TF_TEXTURE_CREATION_FLAG_TEXTURE_COMPATABILITY | TF_TEXTURE_CREATION_FLAG_DCC;
        }
        GRRTDesc.pName = "GodRay RT A";
        addRenderTarget(pRenderer, &GRRTDesc, &pRenderTargetGodRay[0]);
        GRRTDesc.pName = "GodRay RT B";
        GRRTDesc.mStartState = TF_RESOURCE_STATE_UNORDERED_ACCESS;
        addRenderTarget(pRenderer, &GRRTDesc, &pRenderTargetGodRay[1]);

        GRRTDesc.pName = "GodRay RT - MSAA";
        GRRTDesc.mDescriptors = TF_DESCRIPTOR_TYPE_TEXTURE;
        GRRTDesc.mStartState = TF_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        GRRTDesc.mSampleCount = gAppSettings.mMSAALevel;
        addRenderTarget(pRenderer, &GRRTDesc, &pRenderTargetGodRayMS);

        GRRTDesc.mStartState = TF_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        GRRTDesc.mSampleCount = TF_SAMPLE_COUNT_1;
        GRRTDesc.mFlags |= TF_TEXTURE_CREATION_FLAG_DCC | TF_TEXTURE_CREATION_FLAG_TEXTURE_COMPATABILITY;
        GRRTDesc.mFormat = gDebugRTFormat;
        GRRTDesc.pName = "GodRay RT - Debug MSAA View";
        addRenderTarget(pRenderer, &GRRTDesc, &pRenderTargetDebugGodRayMSAA);

        /************************************************************************/
        // MSAA edges render target
        /************************************************************************/
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
        TFRenderTargetDesc msaaStencilRT = {};
        msaaStencilRT.mArraySize = 1;
        msaaStencilRT.mClearValue = optimizedDepthClear;
        msaaStencilRT.mDepth = 1;
        msaaStencilRT.mFormat = stencilImageFormat;
        msaaStencilRT.mStartState = TF_RESOURCE_STATE_DEPTH_READ;
        msaaStencilRT.mDescriptors = TF_DESCRIPTOR_TYPE_TEXTURE;
        msaaStencilRT.mHeight = height;
        msaaStencilRT.mWidth = width;
        msaaStencilRT.mSampleCount = gAppSettings.mMSAALevel;
        msaaStencilRT.mSampleQuality = 0;
        msaaStencilRT.mFlags = TF_TEXTURE_CREATION_FLAG_VR_MULTIVIEW;
        msaaStencilRT.pName = "Stencil Buffer MSAA";
        addRenderTarget(pRenderer, &msaaStencilRT, &pRenderTargetMSAAEdges);

        TFTextureDescriptorDesc stencilViewDesc = {};
        stencilViewDesc.pTexture = pRenderTargetMSAAEdges->pTexture;
        stencilViewDesc.mFormat = stencilImageFormat;
        stencilViewDesc.mIsStencil = 1;
        addTextureDescriptor(pRenderer, &stencilViewDesc, &pDescriptorMSAAStencil);

        msaaStencilRT.pName = "Downscaled Stencil Buffer MSAA";
        msaaStencilRT.mHeight = height / GODRAY_SCALE;
        msaaStencilRT.mWidth = width / GODRAY_SCALE;
        addRenderTarget(pRenderer, &msaaStencilRT, &pRenderTargetMSAAEdgesDownscaled);
    }

    void removeRenderTargets()
    {
        removeRenderTarget(pRenderer, pRenderTargetGodRayMS);
        removeRenderTarget(pRenderer, pRenderTargetGodRay[0]);
        removeRenderTarget(pRenderer, pRenderTargetGodRay[1]);

        removeRenderTarget(pRenderer, pHistoryRenderTarget[0]);
        removeRenderTarget(pRenderer, pHistoryRenderTarget[1]);

        removeRenderTarget(pRenderer, pResolveVRSRenderTarget[0]);
        removeRenderTarget(pRenderer, pResolveVRSRenderTarget[1]);
        removeRenderTarget(pRenderer, pDebugVRSRenderTarget);

        removeRenderTarget(pRenderer, pRenderTargetDebugMSAA);
        removeRenderTarget(pRenderer, pRenderTargetDebugGodRayMSAA);
        removeRenderTarget(pRenderer, pRenderTargetMSAAEdgesDownscaled);
        removeTextureDescriptor(pRenderer, pDescriptorMSAAStencil);
        removeRenderTarget(pRenderer, pRenderTargetMSAAEdges);
        removeRenderTarget(pRenderer, pIntermediateRenderTarget);
        removeRenderTarget(pRenderer, pRenderTargetMSAA);
        removeRenderTarget(pRenderer, pDepthBuffer);
        removeRenderTarget(pRenderer, pDepthBufferOIT);
        removeRenderTarget(pRenderer, pRenderTargetVBPass);
        removeRenderTarget(pRenderer, pRenderTargetShadow);
    }
    /************************************************************************/
    // Load all the shaders needed for the demo
    /************************************************************************/
    void addShaders()
    {
        TFShaderLoadDesc shadowPass = {};
        TFShaderLoadDesc shadowPassAlpha = {};
        TFShaderLoadDesc shadowPassTrans = {};
        TFShaderLoadDesc vbPass = {};
        TFShaderLoadDesc vbPassAlpha[2] = {};
        TFShaderLoadDesc vbPassTrans = {};
        // MSAA + VRS + GodRay variants
        TFShaderLoadDesc vbShade[kNumVisBufShaderVariants] = {};
        TFShaderLoadDesc resolvePass[MSAA_LEVELS_COUNT] = {};
        TFShaderLoadDesc clearBuffer = {};
        TFShaderLoadDesc pixelCoverage[MSAA_LEVELS_COUNT] = {};
        TFShaderLoadDesc preSkinVertexes = {};
        TFShaderLoadDesc preSkinVertexesAsync = {};
        TFShaderLoadDesc triangleCulling = {};
        TFShaderLoadDesc clearLights = {};
        TFShaderLoadDesc clusterLights = {};
        TFShaderLoadDesc lineShader = {};

        shadowPass.mVert.pFileName = "shadow_pass.vert";

        shadowPassAlpha.mVert = {
            "shadow_pass_alpha.vert",
        };
        shadowPassAlpha.mFrag = {
            "shadow_pass_alpha.frag",
        };

        shadowPassTrans.mVert = { "shadow_pass_transparent.vert" };
        shadowPassTrans.mFrag = { "shadow_pass_transparent.frag" };

        vbPass.mVert = { "visibilityBuffer_pass.vert" };
        vbPass.mFrag = { "visibilityBuffer_pass.frag" };

        vbPassAlpha[0].mVert = { "visibilityBuffer_pass_alpha.vert" };
        vbPassAlpha[0].mFrag = { "visibilityBuffer_pass_alpha.frag" };

        vbPassAlpha[1].mVert = { "visibilityBuffer_pass_alpha.vert" };
        vbPassAlpha[1].mFrag = { "visibilityBuffer_pass_alpha_vrs.frag" };

        vbPassTrans.mVert = { "visibilityBuffer_pass_transparent_ret.vert" };
        vbPassTrans.mFrag = { "visibilityBuffer_pass_transparent_void.frag" };

        // Some vulkan driver doesn't generate glPrimitiveID without a geometry pass (steam deck as 03/30/2023)
        bool addGeometryPassThrough = gGpuSettings.mAddGeometryPassThrough;
        if (addGeometryPassThrough)
        {
            // A passthrough gs
            vbPass.mGeom = { "visibilityBuffer_pass.geom" };
            vbPassAlpha[0].mGeom = { "visibilityBuffer_pass_alpha.geom" };
            vbPassAlpha[1].mGeom = { "visibilityBuffer_pass_alpha.geom" };
            vbPassTrans.mGeom = { "visibilityBuffer_pass_transparent_ret.geom" };
            vbPassTrans.mFrag = { "visibilityBuffer_pass_transparent_ret.frag" };
        }

        const char* visibilityBuffer_shade[kNumVisBufShaderVariants] = { "visibilityBuffer_shade_SAMPLE_COUNT_1.frag",
                                                                         "visibilityBuffer_shade_SAMPLE_COUNT_1_AO.frag",
                                                                         "visibilityBuffer_shade_SAMPLE_COUNT_2.frag",
                                                                         "visibilityBuffer_shade_SAMPLE_COUNT_2_AO.frag",
                                                                         "visibilityBuffer_shade_SAMPLE_COUNT_4.frag",
                                                                         "visibilityBuffer_shade_SAMPLE_COUNT_4_AO.frag",
                                                                         "visibilityBuffer_shade_VRS.frag",
                                                                         "visibilityBuffer_shade_VRS_AO.frag",
                                                                         "visibilityBuffer_shade_SAMPLE_COUNT_1_GRAY.frag",
                                                                         "visibilityBuffer_shade_SAMPLE_COUNT_1_AO_GRAY.frag",
                                                                         "visibilityBuffer_shade_SAMPLE_COUNT_2_GRAY.frag",
                                                                         "visibilityBuffer_shade_SAMPLE_COUNT_2_AO_GRAY.frag",
                                                                         "visibilityBuffer_shade_SAMPLE_COUNT_4_GRAY.frag",
                                                                         "visibilityBuffer_shade_SAMPLE_COUNT_4_AO_GRAY.frag",
                                                                         "visibilityBuffer_shade_VRS_GRAY.frag",
                                                                         "visibilityBuffer_shade_VRS_AO_GRAY.frag" };

        for (uint32_t i = 0; i < TF_ARRAY_COUNT(visibilityBuffer_shade); ++i)
        {
            vbShade[i].mVert.pFileName = "visibilityBuffer_shade.vert";
            vbShade[i].mFrag.pFileName = visibilityBuffer_shade[i];
        }

        const char* resolve[] = { "progMSAAResolve_SAMPLE_COUNT_1.frag", "progMSAAResolve_SAMPLE_COUNT_2.frag",
                                  "progMSAAResolve_SAMPLE_COUNT_4.frag" };
        const char* pixelCoverageNames[] = { "pixel_coverage_SAMPLE_COUNT_1.comp", "pixel_coverage_SAMPLE_COUNT_2.comp",
                                             "pixel_coverage_SAMPLE_COUNT_4.comp" };
        for (uint32_t i = 0; i < MSAA_LEVELS_COUNT; ++i)
        {
            // Resolve shader
            resolvePass[i].mVert.pFileName = "progMSAAResolve.vert";
            resolvePass[i].mFrag.pFileName = resolve[i];

            // Pixel coverage compute shader
            pixelCoverage[i].mComp.pFileName = pixelCoverageNames[i];
        }

        preSkinVertexes.mComp.pFileName = "pre_skin_vertexes.comp";
        preSkinVertexesAsync.mComp.pFileName = "pre_skin_vertexes_async.comp";

        // Triangle culling compute shader
        triangleCulling.mComp.pFileName = "triangle_filtering.comp";
        // Clear buffers compute shader
        clearBuffer.mComp.pFileName = "clear_buffers.comp";

        // Clear light clusters compute shader
        clearLights.mComp.pFileName = "clear_light_clusters.comp";
        // Cluster lights compute shader
        clusterLights.mComp.pFileName = "cluster_lights.comp";

        lineShader.mVert.pFileName = "line.vert";
        lineShader.mFrag.pFileName = "line.frag";

        TFShaderLoadDesc oitHeadIndexClearDesc = {};
        oitHeadIndexClearDesc.mVert.pFileName = "display.vert";
        oitHeadIndexClearDesc.mFrag.pFileName = "oitClear.frag";
        addShader(pRenderer, &oitHeadIndexClearDesc, &pShaderClearHeadIndexOIT);

        const char* godrayShaderFileName[] = { "godray_SAMPLE_COUNT_1.frag", "godray_SAMPLE_COUNT_2.frag", "godray_SAMPLE_COUNT_4.frag",
                                               "godray_VRS.frag" };
        for (uint32_t i = 0; i < MSAA_LEVELS_COUNT + 1; ++i)
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

        TFShaderLoadDesc fillStencilDesc = {};
        fillStencilDesc.mVert.pFileName = "fillStencil.vert";
        fillStencilDesc.mFrag.pFileName = "fillStencil.frag";

        TFShaderLoadDesc resolveComputeDesc = {};
        resolveComputeDesc.mComp.pFileName = "resolveVRS.comp";

        TFShaderLoadDesc msaaEdgesShader[MSAA_LEVELS_COUNT - 1] = {};
        const char*      edgeDetectShaders[] = { "msaa_edge_detect_SAMPLE_2.frag", "msaa_edge_detect_SAMPLE_4.frag" };
        for (uint32_t i = 0; i < MSAA_LEVELS_COUNT - 1; ++i)
        {
            msaaEdgesShader[i].mVert.pFileName = "display.vert";
            msaaEdgesShader[i].mFrag.pFileName = edgeDetectShaders[i];
        }

        TFShaderLoadDesc downscaleMSAAEdgesShader[MSAA_LEVELS_COUNT - 1] = {};
        const char*      edgeDetectDownscaleShaders[] = { "msaa_stencil_downscale_SAMPLE_2.frag", "msaa_stencil_downscale_SAMPLE_4.frag" };
        for (uint32_t i = 0; i < MSAA_LEVELS_COUNT - 1; ++i)
        {
            downscaleMSAAEdgesShader[i].mVert.pFileName = "display.vert";
            downscaleMSAAEdgesShader[i].mFrag.pFileName = edgeDetectDownscaleShaders[i];
        }

        TFShaderLoadDesc msaaDebugShader[MSAA_LEVELS_COUNT - 1] = {};
        const char*      debugMSAAFrag[] = { "msaa_debug_SAMPLE_2.frag", "msaa_debug_SAMPLE_4.frag" };
        for (uint32_t i = 0; i < MSAA_LEVELS_COUNT - 1; ++i)
        {
            msaaDebugShader[i].mVert.pFileName = "display.vert";
            msaaDebugShader[i].mFrag.pFileName = debugMSAAFrag[i];
        }

        addShader(pRenderer, &presentShaderDesc, &pShaderPresentPass);

        addShader(pRenderer, &shadowPass, &pShaderShadowPass[GEOMSET_OPAQUE]);
        addShader(pRenderer, &shadowPassAlpha, &pShaderShadowPass[GEOMSET_ALPHA_CUTOUT]);
        addShader(pRenderer, &shadowPassTrans, &pShaderShadowPass[GEOMSET_ALPHA_BLEND]);
        addShader(pRenderer, &vbPass, &pShaderVisibilityBufferPass[GEOMSET_OPAQUE]);
        addShader(pRenderer, &vbPassAlpha[0], &pShaderVisibilityBufferPass[GEOMSET_ALPHA_CUTOUT]);
        addShader(pRenderer, &vbPassTrans, &pShaderVisibilityBufferPass[GEOMSET_ALPHA_BLEND]);
        addShader(pRenderer, &vbPassAlpha[1], &pShaderVisibilityBufferPass[GEOMSET_ALPHA_CUTOUT_VRS]);
        for (uint32_t i = 0; i < kNumVisBufShaderVariants; ++i)
            addShader(pRenderer, &vbShade[i], &pShaderVisibilityBufferShade[i]);
        addShader(pRenderer, &clearBuffer, &pShaderClearBuffers);

        addShader(pRenderer, &preSkinVertexes, &pShaderPreSkinVertexes[PRE_SKIN_SYNC]);
        addShader(pRenderer, &preSkinVertexesAsync, &pShaderPreSkinVertexes[PRE_SKIN_ASYNC]);
        addShader(pRenderer, &triangleCulling, &pShaderTriangleFiltering);
        addShader(pRenderer, &clearLights, &pShaderClearLightClusters);
        addShader(pRenderer, &clusterLights, &pShaderClusterLights);
        for (uint32_t i = 0; i < MSAA_LEVELS_COUNT; ++i)
        {
            addShader(pRenderer, &resolvePass[i], &pShaderResolve[i]);
            addShader(pRenderer, &pixelCoverage[i], &pShaderPixelCoverage[i]);
        }
        addShader(pRenderer, &fillStencilDesc, &pShaderFillStencil);
        addShader(pRenderer, &resolveComputeDesc, &pShaderResolveCompute);

#if defined(TF_ENABLE_WORKGRAPH)
        if (pRenderer->pGpu->mWorkgraphSupported)
        {
            TFShaderLoadDesc gpuPipelineDesc = {};
            gpuPipelineDesc.mGraph = { "gpu_pipeline.graph" };
            addShader(pRenderer, &gpuPipelineDesc, &pShaderGpuPipeline);
        }
#endif

        addShader(pRenderer, &lineShader, &pShaderLine);
        for (uint32_t i = 0; i < MSAA_LEVELS_COUNT - 1; ++i)
        {
            addShader(pRenderer, &msaaEdgesShader[i], &pShaderDrawMSAAEdges[i]);
            addShader(pRenderer, &downscaleMSAAEdgesShader[i], &pShaderDownscaleMSAAEdges[i]);
            addShader(pRenderer, &msaaDebugShader[i], &pShaderDebugMSAA[i]);
        }
    }

    void removeShaders()
    {
        removeShader(pRenderer, pShaderLine);
        removeShader(pRenderer, pShaderShadowPass[GEOMSET_OPAQUE]);
        removeShader(pRenderer, pShaderShadowPass[GEOMSET_ALPHA_CUTOUT]);
        removeShader(pRenderer, pShaderShadowPass[GEOMSET_ALPHA_BLEND]);

        removeShader(pRenderer, pShaderVisibilityBufferPass[GEOMSET_OPAQUE]);
        removeShader(pRenderer, pShaderVisibilityBufferPass[GEOMSET_ALPHA_CUTOUT]);
        removeShader(pRenderer, pShaderVisibilityBufferPass[GEOMSET_ALPHA_BLEND]);
        removeShader(pRenderer, pShaderVisibilityBufferPass[GEOMSET_ALPHA_CUTOUT_VRS]);

        for (uint32_t i = 0; i < kNumVisBufShaderVariants; ++i)
            removeShader(pRenderer, pShaderVisibilityBufferShade[i]);

        removeShader(pRenderer, pShaderPreSkinVertexes[PRE_SKIN_SYNC]);
        removeShader(pRenderer, pShaderPreSkinVertexes[PRE_SKIN_ASYNC]);
        removeShader(pRenderer, pShaderTriangleFiltering);

        removeShader(pRenderer, pShaderClearBuffers);
        removeShader(pRenderer, pShaderClusterLights);
        removeShader(pRenderer, pShaderClearLightClusters);
        for (uint32_t i = 0; i < MSAA_LEVELS_COUNT; ++i)
        {
            removeShader(pRenderer, pShaderResolve[i]);
            removeShader(pRenderer, pShaderPixelCoverage[i]);
        }

        removeShader(pRenderer, pShaderClearHeadIndexOIT);

        for (uint32_t i = 0; i < MSAA_LEVELS_COUNT + 1; ++i)
            removeShader(pRenderer, pGodRayPass[i]);
        removeShader(pRenderer, pShaderGodRayBlurPass);

        removeShader(pRenderer, pShaderPresentPass);
        removeShader(pRenderer, pShaderFillStencil);
        removeShader(pRenderer, pShaderResolveCompute);

#if defined(TF_ENABLE_WORKGRAPH)
        if (pShaderGpuPipeline)
        {
            removeShader(pRenderer, pShaderGpuPipeline);
            pShaderGpuPipeline = {};
        }
#endif
        for (uint32_t i = 0; i < MSAA_LEVELS_COUNT - 1; ++i)
        {
            removeShader(pRenderer, pShaderDebugMSAA[i]);
            removeShader(pRenderer, pShaderDownscaleMSAAEdges[i]);
            removeShader(pRenderer, pShaderDrawMSAAEdges[i]);
        }
    }

    void addPipelines()
    {
        /************************************************************************/
        // Vertex layout used by all geometry passes (shadow, visibility)
        /************************************************************************/
        TFDepthStateDesc depthStateDesc = {};
        depthStateDesc.mDepthTest = true;
        depthStateDesc.mDepthWrite = true;
        depthStateDesc.mDepthFunc = TF_CMP_GREATER;
        TFDepthStateDesc depthStateDisableDesc = {};
        TFDepthStateDesc depthStateRWStencilDesc = {};
        depthStateRWStencilDesc.mDepthTest = false;
        depthStateRWStencilDesc.mDepthWrite = false;
        depthStateRWStencilDesc.mStencilWriteMask = 0xFF;
        depthStateRWStencilDesc.mStencilReadMask = 0xFF;
        depthStateRWStencilDesc.mStencilTest = true;
        depthStateRWStencilDesc.mStencilFrontFunc = TF_CMP_ALWAYS;
        depthStateRWStencilDesc.mStencilFrontFail = TF_STENCIL_OP_KEEP;
        depthStateRWStencilDesc.mStencilFrontPass = TF_STENCIL_OP_REPLACE;
        depthStateRWStencilDesc.mDepthFrontFail = TF_STENCIL_OP_KEEP;
        depthStateRWStencilDesc.mStencilBackFunc = TF_CMP_ALWAYS;
        depthStateRWStencilDesc.mStencilBackFail = TF_STENCIL_OP_KEEP;
        depthStateRWStencilDesc.mStencilBackPass = TF_STENCIL_OP_REPLACE;
        depthStateRWStencilDesc.mDepthBackFail = TF_STENCIL_OP_KEEP;
        TFDepthStateDesc depthStateOnlyStencilDesc = {};
        depthStateOnlyStencilDesc.mStencilWriteMask = 0x00;
        depthStateOnlyStencilDesc.mStencilReadMask = 0xFF;
#if defined(METAL)
        depthStateOnlyStencilDesc.mStencilTest = false;

#else
        depthStateOnlyStencilDesc.mStencilTest = true;
#endif
        depthStateOnlyStencilDesc.mStencilFrontFunc = TF_CMP_EQUAL;
        depthStateOnlyStencilDesc.mStencilFrontFail = TF_STENCIL_OP_KEEP;
        depthStateOnlyStencilDesc.mStencilFrontPass = TF_STENCIL_OP_KEEP;
        depthStateOnlyStencilDesc.mDepthFrontFail = TF_STENCIL_OP_KEEP;
        depthStateOnlyStencilDesc.mStencilBackFunc = TF_CMP_EQUAL;
        depthStateOnlyStencilDesc.mStencilBackFail = TF_STENCIL_OP_KEEP;
        depthStateOnlyStencilDesc.mStencilBackPass = TF_STENCIL_OP_KEEP;
        depthStateOnlyStencilDesc.mDepthBackFail = TF_STENCIL_OP_KEEP;

        TFRasterizerStateDesc rasterizerStateCullNoneDesc = { TF_CULL_MODE_NONE };
        TFRasterizerStateDesc rasterizerStateCullFrontDesc = { TF_CULL_MODE_FRONT };

        TFRasterizerStateDesc rasterizerStateCullNoneMsDesc = { TF_CULL_MODE_NONE, 0, 0, TF_FILL_MODE_SOLID };
        rasterizerStateCullNoneMsDesc.mMultiSample = true;
        TFRasterizerStateDesc rasterizerStateCullFrontMsDesc = { TF_CULL_MODE_FRONT, 0, 0, TF_FILL_MODE_SOLID };
        rasterizerStateCullFrontMsDesc.mMultiSample = true;

        // Setup pipeline settings
        TFPipelineDesc pipelineDesc = {};
        pipelineDesc.pCache = pPipelineCache;

        /************************************************************************/
        // Setup compute pipelines for triangle filtering
        /************************************************************************/
        PIPELINE_LAYOUT_DESC(pipelineDesc, SRT_LAYOUT_DESC(TriangleFilteringSrtData, Persistent),
                             SRT_LAYOUT_DESC(TriangleFilteringSrtData, PerFrame), SRT_LAYOUT_DESC(TriangleFilteringSrtData, PerBatch),
                             NULL);
        pipelineDesc.mType = TF_PIPELINE_TYPE_COMPUTE;
        TFComputePipelineDesc& computePipelineSettings = pipelineDesc.mComputeDesc;
        computePipelineSettings.pShaderProgram = pShaderClearBuffers;
        addPipeline(pRenderer, &pipelineDesc, &pPipelineClearBuffers);

        computePipelineSettings.pShaderProgram = pShaderPixelCoverage[gAppSettings.mMSAAIndex];
        addPipeline(pRenderer, &pipelineDesc, &pPipelinePixelCoverage);

        // God Ray Blur Pass
        PIPELINE_LAYOUT_DESC(pipelineDesc, SRT_LAYOUT_DESC(SrtGodrayBlurComp, Persistent), NULL, NULL,
                             SRT_LAYOUT_DESC(SrtGodrayBlurComp, PerDraw));
        computePipelineSettings.pShaderProgram = pShaderGodRayBlurPass;
        pipelineDesc.mType = TF_PIPELINE_TYPE_COMPUTE;
        addPipeline(pRenderer, &pipelineDesc, &pPipelineGodRayBlurPass);

        // Pre Skin Vertexes
        PIPELINE_LAYOUT_DESC(pipelineDesc, SRT_LAYOUT_DESC(SrtPreSkinVertexesComp, Persistent),
                             SRT_LAYOUT_DESC(SrtPreSkinVertexesComp, PerFrame), SRT_LAYOUT_DESC(SrtPreSkinVertexesComp, PerBatch), NULL);
        pipelineDesc.mType = TF_PIPELINE_TYPE_COMPUTE;
        computePipelineSettings.pShaderProgram = pShaderPreSkinVertexes[PRE_SKIN_SYNC];
        addPipeline(pRenderer, &pipelineDesc, &pPipelinePreSkinVertexes[PRE_SKIN_SYNC]);

        computePipelineSettings.pShaderProgram = pShaderPreSkinVertexes[PRE_SKIN_ASYNC];
        addPipeline(pRenderer, &pipelineDesc, &pPipelinePreSkinVertexes[PRE_SKIN_ASYNC]);

        // Create the compute pipeline for GPU triangle filtering
        PIPELINE_LAYOUT_DESC(pipelineDesc, SRT_LAYOUT_DESC(TriangleFilteringSrtData, Persistent),
                             SRT_LAYOUT_DESC(TriangleFilteringSrtData, PerFrame), SRT_LAYOUT_DESC(TriangleFilteringSrtData, PerBatch),
                             NULL);
        pipelineDesc.mType = TF_PIPELINE_TYPE_COMPUTE;
        computePipelineSettings.pShaderProgram = pShaderTriangleFiltering;
        addPipeline(pRenderer, &pipelineDesc, &pPipelineTriangleFiltering);

        // Setup the clearing light clusters pipeline
        PIPELINE_LAYOUT_DESC(pipelineDesc, SRT_LAYOUT_DESC(SrtClusterLightsData, Persistent),
                             SRT_LAYOUT_DESC(SrtClusterLightsData, PerFrame), SRT_LAYOUT_DESC(SrtClusterLightsData, PerBatch), NULL);
        pipelineDesc.mType = TF_PIPELINE_TYPE_COMPUTE;
        computePipelineSettings.pShaderProgram = pShaderClearLightClusters;
        addPipeline(pRenderer, &pipelineDesc, &pPipelineClearLightClusters);

        // Setup the compute the light clusters pipeline
        PIPELINE_LAYOUT_DESC(pipelineDesc, SRT_LAYOUT_DESC(SrtClusterLightsData, Persistent),
                             SRT_LAYOUT_DESC(SrtClusterLightsData, PerFrame), SRT_LAYOUT_DESC(SrtClusterLightsData, PerBatch), NULL);
        pipelineDesc.mType = TF_PIPELINE_TYPE_COMPUTE;
        computePipelineSettings.pShaderProgram = pShaderClusterLights;
        addPipeline(pRenderer, &pipelineDesc, &pPipelineClusterLights);

#if defined(TF_ENABLE_WORKGRAPH)
        if (pRenderer->pGpu->mWorkgraphSupported)
        {
            pipelineDesc.mType = TF_PIPELINE_TYPE_WORKGRAPH;
            pipelineDesc.mWorkgraphDesc.pShaderProgram = pShaderGpuPipeline;
            pipelineDesc.mWorkgraphDesc.pWorkgraphName = "GPUPipeline";
            addPipeline(pRenderer, &pipelineDesc, &pPipelineGpuPipeline);

            TFWorkgraphDesc workgraphDesc = {};
            workgraphDesc.pPipeline = pPipelineGpuPipeline;
            addWorkgraph(pRenderer, &workgraphDesc, &pWorkgraphGpuPipeline);
        }
#endif
        /************************************************************************/
        // Setup MSAA resolve pipeline
        /************************************************************************/
        PIPELINE_LAYOUT_DESC(pipelineDesc, NULL, NULL, NULL, SRT_LAYOUT_DESC(SrtResolve, PerDraw));
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
        resolvePipelineSettings.pShaderProgram = pShaderResolve[gAppSettings.mMSAAIndex];
        pipelineDesc.pName = "MSAA Resolve - Final";
        addPipeline(pRenderer, &pipelineDesc, &pPipelineResolve);

        pipelineDesc.pName = "MSAA Resolve - GodRay";
        resolvePipelineSettings.pColorFormats = &pRenderTargetGodRay[0]->mFormat;
        addPipeline(pRenderer, &pipelineDesc, &pPipelineResolveGodRay);

        /************************************************************************/
        // Setup Head Index Clear Pipeline
        /************************************************************************/
        PIPELINE_LAYOUT_DESC(pipelineDesc, SRT_LAYOUT_DESC(SrtVisibilityPassData, Persistent),
                             SRT_LAYOUT_DESC(SrtVisibilityPassData, PerFrame), SRT_LAYOUT_DESC(SrtVisibilityPassData, PerBatch), NULL);
        TFGraphicsPipelineDesc& headIndexPipelineSettings = pipelineDesc.mGraphicsDesc;
        headIndexPipelineSettings = { 0 };
        headIndexPipelineSettings.mPrimitiveTopo = TF_PRIMITIVE_TOPO_TRI_LIST;
        headIndexPipelineSettings.mRenderTargetCount = 0;
        headIndexPipelineSettings.pColorFormats = NULL;
        headIndexPipelineSettings.mSampleCount = pDepthBufferOIT->mSampleCount;
        headIndexPipelineSettings.mSampleQuality = 0;
        headIndexPipelineSettings.mDepthStencilFormat = pDepthBufferOIT->mFormat;
        headIndexPipelineSettings.pVertexLayout = NULL;
        headIndexPipelineSettings.pRasterizerState = &rasterizerStateCullNoneDesc;
        headIndexPipelineSettings.pDepthState = &depthStateDisableDesc;
        headIndexPipelineSettings.pBlendState = NULL;
        headIndexPipelineSettings.pShaderProgram = pShaderClearHeadIndexOIT;
        addPipeline(pRenderer, &pipelineDesc, &pPipelineClearHeadIndexOIT);

        /************************************************************************/
        // Setup the Shadow Pass Pipeline
        /************************************************************************/
        PIPELINE_LAYOUT_DESC(pipelineDesc, SRT_LAYOUT_DESC(SrtData, Persistent), SRT_LAYOUT_DESC(SrtData, PerFrame), NULL, NULL);
        TFGraphicsPipelineDesc& shadowPipelineSettings = pipelineDesc.mGraphicsDesc;
        shadowPipelineSettings = { 0 };
        shadowPipelineSettings.mPrimitiveTopo = TF_PRIMITIVE_TOPO_TRI_LIST;
        shadowPipelineSettings.pDepthState = &depthStateDesc;
        shadowPipelineSettings.mDepthStencilFormat = pRenderTargetShadow->mFormat;
        shadowPipelineSettings.mSampleCount = pRenderTargetShadow->mSampleCount;
        shadowPipelineSettings.mSampleQuality = pRenderTargetShadow->mSampleQuality;
        shadowPipelineSettings.pRasterizerState =
            gAppSettings.mMSAALevel > TF_SAMPLE_COUNT_1 ? &rasterizerStateCullNoneMsDesc : &rasterizerStateCullNoneDesc;
        shadowPipelineSettings.pShaderProgram = pShaderShadowPass[GEOMSET_OPAQUE];
        shadowPipelineSettings.pVertexLayout = NULL;
        addPipeline(pRenderer, &pipelineDesc, &pPipelineShadowPass[GEOMSET_OPAQUE]);

        shadowPipelineSettings.pShaderProgram = pShaderShadowPass[GEOMSET_ALPHA_CUTOUT];
        addPipeline(pRenderer, &pipelineDesc, &pPipelineShadowPass[GEOMSET_ALPHA_CUTOUT]);

        shadowPipelineSettings.pShaderProgram = pShaderShadowPass[GEOMSET_ALPHA_BLEND];
        addPipeline(pRenderer, &pipelineDesc, &pPipelineShadowPass[GEOMSET_ALPHA_BLEND]);

        /************************************************************************/
        // Setup the Visibility Buffer Pass Pipeline
        /************************************************************************/
        PIPELINE_LAYOUT_DESC(pipelineDesc, SRT_LAYOUT_DESC(SrtVisibilityPassData, Persistent),
                             SRT_LAYOUT_DESC(SrtVisibilityPassData, PerFrame), SRT_LAYOUT_DESC(SrtVisibilityPassData, PerBatch), NULL);
        TinyImageFormat         formats[] = { pRenderTargetVBPass->mFormat };
        // Setup pipeline settings
        TFGraphicsPipelineDesc& vbPassPipelineSettings = pipelineDesc.mGraphicsDesc;
        vbPassPipelineSettings = { 0 };
        vbPassPipelineSettings.mPrimitiveTopo = TF_PRIMITIVE_TOPO_TRI_LIST;
        vbPassPipelineSettings.pColorFormats = formats;
        vbPassPipelineSettings.mSampleQuality = pRenderTargetVBPass->mSampleQuality;
        vbPassPipelineSettings.pVertexLayout = NULL;

        for (uint32_t i = 0; i < NUM_GEOMETRY_SETS; ++i)
        {
            vbPassPipelineSettings.pDepthState = (i == GEOMSET_ALPHA_BLEND) ? &depthStateDisableDesc : &depthStateDesc;
            vbPassPipelineSettings.mSampleCount = (i == GEOMSET_ALPHA_BLEND) ? TF_SAMPLE_COUNT_1 : pRenderTargetVBPass->mSampleCount;
            vbPassPipelineSettings.mRenderTargetCount = (i == GEOMSET_ALPHA_BLEND) ? 0 : 1;
            vbPassPipelineSettings.mDepthStencilFormat = (i == GEOMSET_ALPHA_BLEND) ? pDepthBufferOIT->mFormat : pDepthBuffer->mFormat;
            vbPassPipelineSettings.mUseCustomSampleLocations = (i != GEOMSET_ALPHA_BLEND) && gAppSettings.mEnableVRS;
            vbPassPipelineSettings.pRasterizerState = (i == GEOMSET_ALPHA_BLEND) || (gAppSettings.mMSAALevel == 1)
                                                          ? &rasterizerStateCullNoneDesc
                                                          : &rasterizerStateCullNoneMsDesc;
            vbPassPipelineSettings.pShaderProgram = pShaderVisibilityBufferPass[i];

#if defined(GFX_EXTENDED_PSO_OPTIONS)
            ExtendedGraphicsPipelineDesc edescs[2] = {};
            edescs[0].type = EXTENDED_GRAPHICS_PIPELINE_TYPE_SHADER_LIMITS;
            initExtendedGraphicsShaderLimits(&edescs[0].shaderLimitsDesc);
            edescs[0].shaderLimitsDesc.maxWavesWithLateAllocParameterCache = 16;

            edescs[1].type = EXTENDED_GRAPHICS_PIPELINE_TYPE_PIXEL_SHADER_OPTIONS;
            edescs[1].pixelShaderOptions.outOfOrderRasterization = PIXEL_SHADER_OPTION_OUT_OF_ORDER_RASTERIZATION_ENABLE_WATER_MARK_7;
            edescs[1].pixelShaderOptions.depthBeforeShader =
                !i ? PIXEL_SHADER_OPTION_DEPTH_BEFORE_SHADER_ENABLE : PIXEL_SHADER_OPTION_DEPTH_BEFORE_SHADER_DEFAULT;

            pipelineDesc.pPipelineExtensions = edescs;
            pipelineDesc.mExtensionCount = TF_ARRAY_COUNT(edescs);
#endif
            addPipeline(pRenderer, &pipelineDesc, &pPipelineVisibilityBufferPass[i]);

            pipelineDesc.mExtensionCount = 0;
        }
        /************************************************************************/
        // Setup the resources needed for the Visibility Buffer Shade Pipeline
        /************************************************************************/
        // Create pipeline
        // Note: the vertex layout is set to null because the positions of the fullscreen triangle are being calculated automatically
        // in the vertex shader using each vertex_id.
        PIPELINE_LAYOUT_DESC(pipelineDesc, SRT_LAYOUT_DESC(SrtData, Persistent), SRT_LAYOUT_DESC(SrtData, PerFrame), NULL, NULL);
        TFGraphicsPipelineDesc& vbShadePipelineSettings = pipelineDesc.mGraphicsDesc;
        vbShadePipelineSettings = { 0 };
        vbShadePipelineSettings.mPrimitiveTopo = TF_PRIMITIVE_TOPO_TRI_LIST;
        vbShadePipelineSettings.mRenderTargetCount = 1;
        vbShadePipelineSettings.pDepthState = &depthStateDisableDesc;
        vbShadePipelineSettings.pRasterizerState =
            gAppSettings.mMSAALevel > TF_SAMPLE_COUNT_1 ? &rasterizerStateCullFrontMsDesc : &rasterizerStateCullFrontDesc;

        // Can't re-use VRS stencil because it has different settings on Metal
        TFDepthStateDesc depthStateOnlyReadStencilProgMSAADesc = {};
        depthStateOnlyReadStencilProgMSAADesc.mStencilWriteMask = 0x00;
        depthStateOnlyReadStencilProgMSAADesc.mStencilReadMask = 0xFF;
        depthStateOnlyReadStencilProgMSAADesc.mStencilTest = true;
        depthStateOnlyReadStencilProgMSAADesc.mStencilFrontFunc = TF_CMP_EQUAL;
        depthStateOnlyReadStencilProgMSAADesc.mStencilFrontFail = TF_STENCIL_OP_KEEP;
        depthStateOnlyReadStencilProgMSAADesc.mStencilFrontPass = TF_STENCIL_OP_KEEP;
        depthStateOnlyReadStencilProgMSAADesc.mDepthFrontFail = TF_STENCIL_OP_KEEP;
        depthStateOnlyReadStencilProgMSAADesc.mStencilBackFunc = TF_CMP_EQUAL;
        depthStateOnlyReadStencilProgMSAADesc.mStencilBackFail = TF_STENCIL_OP_KEEP;
        depthStateOnlyReadStencilProgMSAADesc.mStencilBackPass = TF_STENCIL_OP_KEEP;
        depthStateOnlyReadStencilProgMSAADesc.mDepthBackFail = TF_STENCIL_OP_KEEP;

        for (uint32_t i = 0; i < 2; ++i)
        {
            uint32_t shaderIndex = (gAppSettings.mMSAAIndex * 2 + i) + (2 * (MSAA_LEVELS_COUNT + 1) * gAppSettings.mEnableGodray);
            vbShadePipelineSettings.pShaderProgram = pShaderVisibilityBufferShade[shaderIndex];
            vbShadePipelineSettings.mSampleCount = gAppSettings.mMSAALevel;
            if (gAppSettings.mMSAALevel > TF_SAMPLE_COUNT_1)
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

#if defined(GFX_EXTENDED_PSO_OPTIONS)
            ExtendedGraphicsPipelineDesc edescs[2] = {};
            edescs[0].type = EXTENDED_GRAPHICS_PIPELINE_TYPE_SHADER_LIMITS;
            initExtendedGraphicsShaderLimits(&edescs[0].shaderLimitsDesc);
            // edescs[0].ShaderLimitsDesc.MaxWavesWithLateAllocParameterCache = 22;

            edescs[1].type = EXTENDED_GRAPHICS_PIPELINE_TYPE_PIXEL_SHADER_OPTIONS;
            edescs[1].pixelShaderOptions.outOfOrderRasterization = PIXEL_SHADER_OPTION_OUT_OF_ORDER_RASTERIZATION_ENABLE_WATER_MARK_7;
            edescs[1].pixelShaderOptions.depthBeforeShader =
                !i ? PIXEL_SHADER_OPTION_DEPTH_BEFORE_SHADER_ENABLE : PIXEL_SHADER_OPTION_DEPTH_BEFORE_SHADER_DEFAULT;

            if (!gAppSettings.mEnableVRS)
            {
                pipelineDesc.pPipelineExtensions = edescs;
                pipelineDesc.mExtensionCount = TF_ARRAY_COUNT(edescs);
            }
#endif
            if (gAppSettings.mEnableVRS)
            {
                vbShadePipelineSettings.mDepthStencilFormat = pDepthBuffer->mFormat;
                vbShadePipelineSettings.pDepthState = &depthStateOnlyStencilDesc;
                shaderIndex = (gAppSettings.mMSAAIndex + 1) * 2 + i + (2 * (MSAA_LEVELS_COUNT + 1) * gAppSettings.mEnableGodray);
                vbShadePipelineSettings.pShaderProgram = pShaderVisibilityBufferShade[shaderIndex];
            }
            else if (gAppSettings.mMSAALevel > TF_SAMPLE_COUNT_1)
            {
                vbShadePipelineSettings.mDepthStencilFormat = pRenderTargetMSAAEdges->mFormat;
                vbShadePipelineSettings.pDepthState = &depthStateOnlyReadStencilProgMSAADesc;
            }
            addPipeline(pRenderer, &pipelineDesc, &pPipelineVisibilityBufferShadeSrgb[i]);

            pipelineDesc.mExtensionCount = 0;
        }

        if (gAppSettings.mMSAALevel > TF_SAMPLE_COUNT_1)
        {
            vbShadePipelineSettings.pColorFormats = &pRenderTargetMSAA->mFormat;
            vbShadePipelineSettings.mSampleQuality = pRenderTargetMSAA->mSampleQuality;
        }
        else
        {
            vbShadePipelineSettings.pColorFormats = &pIntermediateRenderTarget->mFormat;
            vbShadePipelineSettings.mSampleQuality = pSwapChain->ppRenderTargets[0]->mSampleQuality;
        }

        /************************************************************************/
        // Setup Godray pipeline
        /************************************************************************/
        PIPELINE_LAYOUT_DESC(pipelineDesc, SRT_LAYOUT_DESC(SrtData, Persistent), SRT_LAYOUT_DESC(SrtData, PerFrame), NULL, NULL);
        TFGraphicsPipelineDesc& pipelineSettingsGodRay = pipelineDesc.mGraphicsDesc;
        pipelineSettingsGodRay = { 0 };
        pipelineSettingsGodRay.mPrimitiveTopo = TF_PRIMITIVE_TOPO_TRI_LIST;
        pipelineSettingsGodRay.pRasterizerState = &rasterizerStateCullNoneDesc;
        pipelineSettingsGodRay.mRenderTargetCount = 1;
        pipelineSettingsGodRay.pColorFormats = &pRenderTargetGodRay[0]->mFormat;
        bool isProgMSAAEnabled = gAppSettings.mMSAALevel > TF_SAMPLE_COUNT_1 && !gAppSettings.mEnableVRS;
        pipelineSettingsGodRay.mSampleCount = isProgMSAAEnabled ? gAppSettings.mMSAALevel : TF_SAMPLE_COUNT_1;
        pipelineSettingsGodRay.mSampleQuality = pSwapChain->ppRenderTargets[0]->mSampleQuality;
        pipelineSettingsGodRay.pShaderProgram = pGodRayPass[gAppSettings.mMSAAIndex + gAppSettings.mEnableVRS];
        pipelineSettingsGodRay.pDepthState = isProgMSAAEnabled ? &depthStateOnlyReadStencilProgMSAADesc : &depthStateDisableDesc;
        pipelineSettingsGodRay.mDepthStencilFormat =
            isProgMSAAEnabled ? pRenderTargetMSAAEdgesDownscaled->mFormat : TinyImageFormat_UNDEFINED;
        addPipeline(pRenderer, &pipelineDesc, &pPipelineGodRayPass);

        /************************************************************************/
        // Setup Present pipeline
        /************************************************************************/
        PIPELINE_LAYOUT_DESC(pipelineDesc, SRT_LAYOUT_DESC(SrtDisplay, Persistent), SRT_LAYOUT_DESC(SrtDisplay, PerFrame), NULL,
                             SRT_LAYOUT_DESC(SrtDisplay, PerDraw));
        TFGraphicsPipelineDesc& pipelineSettingsFinalPass = pipelineDesc.mGraphicsDesc;
        pipelineSettingsFinalPass = { 0 };
        pipelineSettingsFinalPass.mPrimitiveTopo = TF_PRIMITIVE_TOPO_TRI_LIST;
        pipelineSettingsFinalPass.pRasterizerState = &rasterizerStateCullNoneDesc;
        pipelineSettingsFinalPass.mRenderTargetCount = 1;
        pipelineSettingsFinalPass.pColorFormats = &pSwapChain->ppRenderTargets[0]->mFormat;
        pipelineSettingsFinalPass.mSampleCount = pSwapChain->ppRenderTargets[0]->mSampleCount;
        pipelineSettingsFinalPass.mSampleQuality = pSwapChain->ppRenderTargets[0]->mSampleQuality;
        pipelineSettingsFinalPass.pShaderProgram = pShaderPresentPass;

        addPipeline(pRenderer, &pipelineDesc, &pPipelinePresentPass);

        /************************************************************************/
        // Setup Fill VRS map pipeline
        /************************************************************************/
        PIPELINE_LAYOUT_DESC(pipelineDesc, SRT_LAYOUT_DESC(SrtData, Persistent), SRT_LAYOUT_DESC(SrtData, PerFrame), NULL, NULL);
        TFGraphicsPipelineDesc& pipelinefillStencilPass = pipelineDesc.mGraphicsDesc;
        pipelinefillStencilPass = { 0 };
        pipelinefillStencilPass.mPrimitiveTopo = TF_PRIMITIVE_TOPO_TRI_LIST;
        pipelinefillStencilPass.mRenderTargetCount = 1;
        pipelinefillStencilPass.pColorFormats = &pHistoryRenderTarget[0]->mFormat;
        pipelinefillStencilPass.mSampleCount = pDepthBuffer->mSampleCount;
        pipelinefillStencilPass.mSampleQuality = pDepthBuffer->mSampleQuality;
        pipelinefillStencilPass.mDepthStencilFormat = pDepthBuffer->mFormat;
        pipelinefillStencilPass.pVertexLayout = NULL;
        pipelinefillStencilPass.pRasterizerState = &rasterizerStateCullNoneDesc;
        pipelinefillStencilPass.pBlendState = NULL;
        pipelinefillStencilPass.pDepthState = &depthStateRWStencilDesc;
        pipelinefillStencilPass.pShaderProgram = pShaderFillStencil;
        pipelinefillStencilPass.mUseCustomSampleLocations = gAppSettings.mEnableVRS;
        addPipeline(pRenderer, &pipelineDesc, &pPipelineFillStencil);

        /************************************************************************/
        // Setup Resolve VRS pipeline
        /************************************************************************/
        TFPipelineDesc pipelineComputeDesc = {};
        PIPELINE_LAYOUT_DESC(pipelineComputeDesc, SRT_LAYOUT_DESC(SrtResolveVRSData, Persistent),
                             SRT_LAYOUT_DESC(SrtResolveVRSData, PerFrame), SRT_LAYOUT_DESC(SrtResolveVRSData, PerBatch), NULL);

        pipelineComputeDesc.pName = "Resolve Pipeline";
        pipelineComputeDesc.mType = TF_PIPELINE_TYPE_COMPUTE;

        TFComputePipelineDesc& pipelineResolveComputeDesc = pipelineComputeDesc.mComputeDesc;
        pipelineResolveComputeDesc.pShaderProgram = pShaderResolveCompute;
        addPipeline(pRenderer, &pipelineComputeDesc, &pPipelineResolveCompute);

        /************************************************************************/
        // Setup Debug drawing pipeline
        /************************************************************************/
        pipelineDesc = {};
        PIPELINE_LAYOUT_DESC(pipelineDesc, SRT_LAYOUT_DESC(SrtData, Persistent), SRT_LAYOUT_DESC(SrtData, PerFrame), NULL, NULL);
        pipelineDesc.pCache = pPipelineCache;
        pipelineDesc.mType = TF_PIPELINE_TYPE_GRAPHICS;

        TFVertexLayout vertexLayout = {};
        vertexLayout.mBindingCount = 1;
        vertexLayout.mBindings[0].mStride = sizeof(float3);
        vertexLayout.mAttribCount = 1;
        vertexLayout.mAttribs[0].mSemantic = TF_SEMANTIC_POSITION;
        vertexLayout.mAttribs[0].mFormat = TinyImageFormat_R32G32B32_SFLOAT;
        vertexLayout.mAttribs[0].mBinding = 0;
        vertexLayout.mAttribs[0].mLocation = 0;
        vertexLayout.mAttribs[0].mOffset = 0;

        TFGraphicsPipelineDesc& linesPipeline = pipelineDesc.mGraphicsDesc;
        linesPipeline = { 0 };
        linesPipeline.mPrimitiveTopo = TF_PRIMITIVE_TOPO_LINE_LIST;
        linesPipeline.pRasterizerState = &rasterizerStateCullNoneDesc;
        linesPipeline.mRenderTargetCount = 1;
        linesPipeline.pColorFormats = &pSwapChain->ppRenderTargets[0]->mFormat;
        linesPipeline.mSampleCount = pSwapChain->ppRenderTargets[0]->mSampleCount;
        linesPipeline.mSampleQuality = pSwapChain->ppRenderTargets[0]->mSampleQuality;
        linesPipeline.pDepthState = NULL;
        linesPipeline.mDepthStencilFormat = TinyImageFormat_UNDEFINED;
        linesPipeline.pShaderProgram = pShaderLine;
        linesPipeline.pVertexLayout = &vertexLayout;
        pipelineDesc.pName = "Debug Lines";

        addPipeline(pRenderer, &pipelineDesc, &pPipelineLine);

        // Setup MSAA edge detect pipeline
        /************************************************************************/
        if (gAppSettings.mMSAALevel > TF_SAMPLE_COUNT_1)
        {
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

            pipelineDesc = {};
            PIPELINE_LAYOUT_DESC(pipelineDesc, SRT_LAYOUT_DESC(SrtData, Persistent), SRT_LAYOUT_DESC(SrtData, PerFrame), NULL, NULL);
            pipelineDesc.mType = TF_PIPELINE_TYPE_GRAPHICS;
            TFGraphicsPipelineDesc& msaaEdgesPipeline = pipelineDesc.mGraphicsDesc;
            msaaEdgesPipeline = { 0 };
            msaaEdgesPipeline.mPrimitiveTopo = TF_PRIMITIVE_TOPO_TRI_LIST;
            msaaEdgesPipeline.mRenderTargetCount = 0;
            msaaEdgesPipeline.pDepthState = &depthStateOnlyWriteStencilDesc;
            msaaEdgesPipeline.mDepthStencilFormat = pRenderTargetMSAAEdges->mFormat;
            msaaEdgesPipeline.mSampleCount = gAppSettings.mMSAALevel;
            msaaEdgesPipeline.mSampleQuality = 0;
            msaaEdgesPipeline.pRasterizerState = &rasterizerStateCullNoneMsDesc;
            msaaEdgesPipeline.pShaderProgram = pShaderDrawMSAAEdges[gAppSettings.mMSAAIndex - 1];
            pipelineDesc.pName = "Render Stencil MSAA edges";
            addPipeline(pRenderer, &pipelineDesc, &pPipelineDrawMSAAEdges);

            // Downscale pipeline
            msaaEdgesPipeline.pShaderProgram = pShaderDownscaleMSAAEdges[gAppSettings.mMSAAIndex - 1];
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
            msaaDebugViewPipeline.pShaderProgram = pShaderDebugMSAA[gAppSettings.mMSAAIndex - 1];
            pipelineDesc.pName = "Render MSAA Debug Shader";
            addPipeline(pRenderer, &pipelineDesc, &pPipelineDebugMSAA);
        }
    }

    void removePipelines()
    {
        removePipeline(pRenderer, pPipelineLine);
        removePipeline(pRenderer, pPipelineResolveCompute);
        removePipeline(pRenderer, pPipelineFillStencil);
        removePipeline(pRenderer, pPipelineResolveGodRay);
        removePipeline(pRenderer, pPipelineResolve);

        removePipeline(pRenderer, pPipelineGodRayPass);
        removePipeline(pRenderer, pPipelineGodRayBlurPass);

        removePipeline(pRenderer, pPipelineClearHeadIndexOIT);

        removePipeline(pRenderer, pPipelinePresentPass);

        // Destroy graphics pipelines
        for (uint32_t i = 0; i < 2; ++i)
        {
            removePipeline(pRenderer, pPipelineVisibilityBufferShadeSrgb[i]);
        }

        for (uint32_t i = 0; i < NUM_GEOMETRY_SETS; ++i)
            removePipeline(pRenderer, pPipelineVisibilityBufferPass[i]);

        for (uint32_t i = 0; i < NUM_GEOMETRY_SETS; ++i)
            removePipeline(pRenderer, pPipelineShadowPass[i]);

        removePipeline(pRenderer, pPipelineClusterLights);
        removePipeline(pRenderer, pPipelineClearLightClusters);
        removePipeline(pRenderer, pPipelinePreSkinVertexes[PRE_SKIN_SYNC]);
        removePipeline(pRenderer, pPipelinePreSkinVertexes[PRE_SKIN_ASYNC]);
        removePipeline(pRenderer, pPipelineTriangleFiltering);
        removePipeline(pRenderer, pPipelineClearBuffers);
        removePipeline(pRenderer, pPipelinePixelCoverage);

#if defined(TF_ENABLE_WORKGRAPH)
        if (pPipelineGpuPipeline)
        {
            removePipeline(pRenderer, pPipelineGpuPipeline);
            removeWorkgraph(pRenderer, pWorkgraphGpuPipeline);
            pPipelineGpuPipeline = {};
            pWorkgraphGpuPipeline = {};
        }
#endif
        if (gAppSettings.mMSAALevel > TF_SAMPLE_COUNT_1)
        {
            removePipeline(pRenderer, pPipelineDebugMSAA);
            removePipeline(pRenderer, pPipelineDownscaleMSAAEdges);
            removePipeline(pRenderer, pPipelineDrawMSAAEdges);
        }
    }

    // This method sets the contents of the buffers to indicate the rendering pass that
    // the whole scene triangles must be rendered (no cluster / triangle filtering).
    // This is useful for testing purposes to compare visual / performance results.
    void addTriangleFilteringBuffers()
    {
        /************************************************************************/
        // Mesh constants
        /************************************************************************/
        for (uint32_t si = 0; si < gWorldSectorsCount; si++)
        {
            updateWorldSectorMeshConstants(&gWorldSectors[si]);
        }

        // Create mesh constants buffer
        TFGeometry* staticPart = gWorldSectors[0].pGeom;
        uint32_t    materialID = staticPart->mDrawArgCount;
        uint32_t    vertexCount = staticPart->mVertexCount * (gWorldSectorsSizeX * gWorldSectorsSizeY);
        uint32_t    meshCount = staticPart->mDrawArgCount * (gWorldSectorsSizeX * gWorldSectorsSizeY);

        TFGeometry* skinnedPart = pTroopGeometry;
        for (uint32_t instanceIdx = 0; instanceIdx < gMaxMeshInstances; ++instanceIdx)
        {
            for (uint32_t di = 0; di < skinnedPart->mDrawArgCount; ++di)
            {
                uint32_t flags = gWorldSectors[0].pPackage->pTextureMetadata[1].pMaterialProps[di].mFlags;

                for (uint32_t i = 0; i < gDataBufferCount; ++i)
                {
                    gPerFrame[i].pMeshData[meshCount].indexOffset =
                        skinnedPart->mIndexBufferChunk.mOffset / IndexTypeToSize(skinnedPart->mIndexType) +
                        skinnedPart->pDrawArgs[di].mStartIndex;
                    gPerFrame[i].pMeshData[meshCount].vertexOffset =
                        skinnedPart->mVertexBufferChunks[0].mOffset / skinnedPart->mVertexStrides[0] +
                        skinnedPart->pDrawArgs[di].mVertexOffset;
                    gPerFrame[i].pMeshData[meshCount].modelMtx = mat4::identity();
                    gPerFrame[i].pMeshData[meshCount].invModelMtx = mat4::identity();
                    gPerFrame[i].pMeshData[meshCount].prevModelMtx = mat4::identity();
                    gPerFrame[i].pMeshData[meshCount].preSkinnedVertexOffset = PRE_SKINNED_VERTEX_OFFSET_NONE;
                    gPerFrame[i].pMeshData[meshCount].indirectVertexOffset = vertexCount;
                    gPerFrame[i].pMeshData[meshCount].materialID_flags =
                        ((flags & FLAG_MASK) << FLAG_LOW_BIT) | ((materialID & MATERIAL_ID_MASK) << MATERIAL_ID_LOW_BIT);
                }
                pVBMeshInstances[meshCount].mGeometrySet = GEOMSET_OPAQUE;
                pVBMeshInstances[meshCount].mMeshIndex = meshCount;
                pVBMeshInstances[meshCount].mTriangleCount = skinnedPart->pDrawArgs[di].mIndexCount / 3;
                pVBMeshInstances[meshCount].mInstanceIndex = instanceIdx;
                ++meshCount;
            }
            // TODO: Reduce to an offset per mesh to align with VB_COMPUTE_THREADS
            // Requires to have vertex count information within submeshes "part->pDrawArgs[di]"
            vertexCount += skinnedPart->mVertexCount;
        }

        TFBufferLoadDesc meshConstantDesc = {};
        meshConstantDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_BUFFER;
        meshConstantDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
        meshConstantDesc.mDesc.mElementCount = gMaxMeshCount;
        meshConstantDesc.mDesc.mStructStride = sizeof(MeshData);
        meshConstantDesc.mDesc.mSize = meshConstantDesc.mDesc.mElementCount * meshConstantDesc.mDesc.mStructStride;
        meshConstantDesc.mDesc.pName = "Mesh Constant Desc";
        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            meshConstantDesc.pData = gPerFrame[i].pMeshData;
            meshConstantDesc.ppBuffer = &pMeshDataBuffer[i];
            addResource(&meshConstantDesc, NULL);
        }

        /************************************************************************/
        // InstanceData
        /************************************************************************/
        TFBufferLoadDesc jointMatrixDesc = {};
        jointMatrixDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_BUFFER;
        jointMatrixDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
        jointMatrixDesc.mDesc.mElementCount = gMaxJointMatrixes;
        jointMatrixDesc.mDesc.mStructStride = sizeof(mat4);
        jointMatrixDesc.mDesc.mSize = jointMatrixDesc.mDesc.mElementCount * jointMatrixDesc.mDesc.mStructStride;
        jointMatrixDesc.mDesc.pName = "Joint Matrix Buffer";
        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            jointMatrixDesc.ppBuffer = &pJointMatrixBuffer[i];
            addResource(&jointMatrixDesc, NULL);
        }

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
        ubDesc.mDesc.pName = "Uniform Buffer PerFrame";

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

        TFBufferLoadDesc skyTriDataBufferDesc = {};
        skyTriDataBufferDesc.mDesc.mSize = sizeof(UniformDataSkyboxTri);
        skyTriDataBufferDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        skyTriDataBufferDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
        skyTriDataBufferDesc.mDesc.mFlags = TF_BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
        skyTriDataBufferDesc.pData = NULL;
        skyTriDataBufferDesc.mDesc.pName = "Sky Tri Uniforms";
        for (uint32_t frameIdx = 0; frameIdx < gDataBufferCount; ++frameIdx)
        {
            skyTriDataBufferDesc.ppBuffer = &pUniformBufferSkyTri[frameIdx];
            addResource(&skyTriDataBufferDesc, NULL);
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
        /************************************************************************/
        // Cleanup
        /************************************************************************/
        waitForAllResourceLoads();
    }

    void removeTriangleFilteringBuffers()
    {
        /************************************************************************/
        // Mesh constants and JointMatrix
        /************************************************************************/
        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            removeResource(pMeshDataBuffer[i]);
            removeResource(pJointMatrixBuffer[i]);
        }

        /************************************************************************/
        // Per Frame Constant Buffers
        /************************************************************************/

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
            removeResource(pUniformBufferSkyTri[frameIdx]);
        }
        /************************************************************************/
        /************************************************************************/
    }

    void addPixelCoverageBuffers(uint32_t indexCount)
    {
        TFBufferLoadDesc pixelCoverageDesc = {};
        pixelCoverageDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_RW_BUFFER_RAW;
        pixelCoverageDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_GPU_TO_CPU;
        pixelCoverageDesc.mDesc.mElementCount = indexCount;
        pixelCoverageDesc.mDesc.mStructStride = sizeof(uint32_t);
        pixelCoverageDesc.mDesc.mSize = pixelCoverageDesc.mDesc.mElementCount * pixelCoverageDesc.mDesc.mStructStride;
        pixelCoverageDesc.mDesc.mStartState = TF_RESOURCE_STATE_COMMON;
        pixelCoverageDesc.mDesc.mFlags = TF_BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
        pixelCoverageDesc.mDesc.pName = "PixelCoverage buffer";
        pixelCoverageDesc.pData = nullptr;

        for (uint32_t i = 0; i < gDataBufferCount; i++)
        {
            pixelCoverageDesc.ppBuffer = &gPixelCoverageBuffers[i];
            addResource(&pixelCoverageDesc, NULL);
        }
    }

    void removePixelCoverageBuffers()
    {
        for (uint32_t i = 0; i < gDataBufferCount; i++)
        {
            removeResource(gPixelCoverageBuffers[i]);
        }
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
        TFPackage*     pPackage = gWorldSectors[0].pPackage;

        mat4           cameraModel = mat4::scale(vec3(SCENE_SCALE));
        TFCameraMatrix cameraView = pCamera->getViewMatrix();
        TFCameraMatrix cameraProj = camMatPerspectiveReverseZ(PI / 2.0f, aspectRatioInv, gAppSettings.nearPlane, gAppSettings.farPlane);

        float timeNormalized =
            (gAppSettings.mTimeOfDay - gAppSettings.mTimeOfDaySunrise) / (gAppSettings.mTimeOfDaySunset - gAppSettings.mTimeOfDaySunrise);

        // Direction from light to scene (for BRDFs)
        float sunRotation = PI * timeNormalized;
        mat4  rotationLight = mat4::rotationYX(gAppSettings.mSunriseDirection, sunRotation);
        vec3  lightDir = (rotationLight * vec4::zAxis()).getXYZ();

        // Direction from shadowmap camera to its pivot position
        float shadowPadding = (1.0f - gAppSettings.mShadowRange) / 2.0f;
        float shadowRotationTime = lerp(shadowPadding, 1.0f - shadowPadding, timeNormalized);
        float sunRotationShadow = PI * shadowRotationTime;
        mat4  rotationShadow = mat4::rotationYX(gAppSettings.mSunriseDirection, sunRotationShadow);
        vec3  shadowDir = (rotationShadow * vec4::zAxis()).getXYZ();

        // Have shadowmap follow camera
        float  shadowBounds = 16.0f * SCENE_SCALE;
        vec3   camLookDir = (pCamera->getInverseViewMatrix().mMatrices[MONO_CAMERA_VIEW_INDEX].v[2]).getXYZ();
        float3 shadowCameraPivot = pCamera->getViewPosition() + camLookDir * shadowBounds * 0.5;
        shadowCameraPivot.y = 15.0f * SCENE_SCALE;

        float3 shadowCameraOffset = (-shadowDir * 25.0f) * SCENE_SCALE;
        mat4   invTranslationShadow = mat4::translation(-(shadowCameraPivot + shadowCameraOffset));

        mat4           shadowModel = mat4::scale(float3(SCENE_SCALE));
        mat4           shadowView = transpose(rotationShadow) * invTranslationShadow;
        TFCameraMatrix shadowProj =
            camMatOrthographicReverseZ(-shadowBounds, shadowBounds, -shadowBounds, shadowBounds, 0.1f, 55.0f * SCENE_SCALE);

        float2 twoOverRes = { 2.0f / float(width), 2.0f / float(height) };

        /************************************************************************/
        // Order independent transparency data
        /************************************************************************/
        currentFrame->gPerFrameUniformData.transAlphaPerFlag[0] = gFlagAlphaRed;
        currentFrame->gPerFrameUniformData.transAlphaPerFlag[1] = gFlagAlphaGreen;
        currentFrame->gPerFrameUniformData.transAlphaPerFlag[2] = gFlagAlphaBlue;
        currentFrame->gPerFrameUniformData.transAlphaPerFlag[3] = gFlagAlphaPurple;
        currentFrame->gPerFrameUniformData.screenWidth = width;
        currentFrame->gPerFrameUniformData.screenHeight = height;
        currentFrame->gPerFrameUniformData.mFrameCounter = (uint32_t)gFrameCount;

        /************************************************************************/
        // Lighting data
        /************************************************************************/
        currentFrame->gPerFrameUniformData.camPos = (vec4(pCamera->getViewPosition()));
        currentFrame->gPerFrameUniformData.nearPlane = gAppSettings.nearPlane;
        currentFrame->gPerFrameUniformData.farPlane = gAppSettings.farPlane;
        currentFrame->gPerFrameUniformData.lightDir = (vec4(lightDir));
        currentFrame->gPerFrameUniformData.twoOverRes = twoOverRes;
        currentFrame->gPerFrameUniformData.esmControl = gAppSettings.mEsmControl;
        if (pPackage)
        {
            currentFrame->gPerFrameUniformData.meshCount = (uint32_t)gTexMeshCount;
        }
        currentFrame->gPerFrameUniformData.mGodRayScatterFactor = gGodRayConstant.mScatterFactor;
        currentFrame->gPerFrameUniformData.drawVRSDebug = gAppSettings.mDrawDebugTargets ? 1.0f : 0.0f;
        /************************************************************************/
        // Matrix data
        /************************************************************************/

        currentFrame->gPerFrameUniformData.transform[VIEW_SHADOW].vp = camMatMulMat4(&shadowProj, &shadowView);
        currentFrame->gPerFrameUniformData.transform[VIEW_SHADOW].invVP =
            camMatInverse(&currentFrame->gPerFrameUniformData.transform[VIEW_SHADOW].vp);
        currentFrame->gPerFrameUniformData.transform[VIEW_SHADOW].projection = shadowProj;
        currentFrame->gPerFrameUniformData.transform[VIEW_SHADOW].mvp =
            camMatMulMat4(&currentFrame->gPerFrameUniformData.transform[VIEW_SHADOW].vp, &shadowModel);

        currentFrame->gPerFrameUniformData.transform[VIEW_CAMERA].vp = camMatMul(&cameraProj, &cameraView);
        currentFrame->gPerFrameUniformData.transform[VIEW_CAMERA].invVP =
            camMatInverse(&currentFrame->gPerFrameUniformData.transform[VIEW_CAMERA].vp);
        currentFrame->gPerFrameUniformData.transform[VIEW_CAMERA].projection = cameraProj;
        currentFrame->gPerFrameUniformData.transform[VIEW_CAMERA].prevMVP = currentFrame->gPerFrameUniformData.transform[VIEW_CAMERA].mvp;
        currentFrame->gPerFrameUniformData.transform[VIEW_CAMERA].mvp =
            camMatMulMat4(&currentFrame->gPerFrameUniformData.transform[VIEW_CAMERA].vp, &cameraModel);
        currentFrame->gPerFrameUniformData.transform[VIEW_CAMERA].invMVP =
            camMatInverse(&currentFrame->gPerFrameUniformData.transform[VIEW_CAMERA].mvp);
        /************************************************************************/
        // Culling data
        /************************************************************************/
        currentFrame->gPerFrameUniformData.cullingViewports[VIEW_SHADOW].sampleCount = 1;
        currentFrame->gPerFrameUniformData.cullingViewports[VIEW_SHADOW].windowSize = { (float)gShadowMapSize, (float)gShadowMapSize };

        currentFrame->gPerFrameUniformData.cullingViewports[VIEW_CAMERA].sampleCount = gAppSettings.mMSAALevel;
        currentFrame->gPerFrameUniformData.cullingViewports[VIEW_CAMERA].windowSize = { (float)width, (float)height };

        // Cache eye position in object space for cluster culling on the CPU
        currentFrame->gEyeObjectSpace[VIEW_SHADOW] = (inverse(shadowView * shadowModel) * vec4(0, 0, 0, 1)).getXYZ();
        currentFrame->gEyeObjectSpace[VIEW_CAMERA] =
            (inverse(cameraView.mMatrices[MONO_CAMERA_VIEW_INDEX] * cameraModel) * vec4(0, 0, 0, 1))
                .getXYZ(); // vec4(0,0,0,1) is the camera position in eye space
        /************************************************************************/
        // Shading data
        /************************************************************************/
        currentFrame->gPerFrameUniformData.lightColor = gAppSettings.mLightColor;
        currentFrame->gPerFrameUniformData.outputMode = (uint32_t)gAppSettings.mOutputMode;
        /************************************************************************/
        // Skybox
        /************************************************************************/
        cameraView = camMatSetTranslation(&cameraView, vec3(0));
        currentFrame->gUniformDataSky.mCamPos = pCamera->getViewPosition();
        currentFrame->gUniformDataSky.mProjectView =
            cameraProj.mMatrices[MONO_CAMERA_VIEW_INDEX] * cameraView.mMatrices[MONO_CAMERA_VIEW_INDEX];
        currentFrame->gUniformDataSkyTri.mInverseViewProjection =
            inverse(cameraProj.mMatrices[MONO_CAMERA_VIEW_INDEX] * cameraView.mMatrices[MONO_CAMERA_VIEW_INDEX]);
        /************************************************************************/
        // Tonemap
        /************************************************************************/
        gTonemapInformation.outputMode = (uint)gAppSettings.mOutputMode;
        gTonemapInformation.linearScale = gAppSettings.LinearScale / 10000.0f;

        currentFrame->gPerFrameUniformData.mLinearScale = gTonemapInformation.linearScale;
        currentFrame->gPerFrameUniformData.mOutputMode = gTonemapInformation.outputMode;
        /************************************************************************/
        // MeshData
        /************************************************************************/
        for (uint32_t i = 0; i < gStaticMeshInstanceCount; ++i)
        {
            StaticMeshInstance* sm = &gStaticMeshInstances[i];

            MeshData* meshData = &currentFrame->pMeshData[i + gSceneMeshCount];
            meshData->prevModelMtx = meshData->modelMtx;
            meshData->modelMtx = mat4::translation(sm->mTranslation) * f4x4RotationQuat(sm->mRotation) * mat4::scale(sm->mScale);
            meshData->invModelMtx = inverse(meshData->modelMtx);
        }

        /************************************************************************/
        // Animation Data
        /************************************************************************/

        // When we use Async Compute we need one PreSkinned buffer per frame, this is the offset to the first pre-skinned
        // vertex for the current frame we are rendering.
        // If we don't use Async Compute we don't need different offsets, just use one buffer and write to it every frame
        const uint32_t preSkinnedVertexStartOffsetInUnifiedBuffer =
            gAppSettings.mAsyncCompute ? gPreSkinnedVertexStartOffset + gPreSkinnedVertexCountPerFrame * frameIdx
                                       : gPreSkinnedVertexStartOffset;

        for (uint32_t i = 0; i < gMaxAnimatedInstances; ++i)
        {
            const AnimatedMeshInstance* am = &gAnimatedMeshInstances[i];

            {
                const mat4* src = am->pBoneMatrixes;
                mat4*       dst = currentFrame->gJointMatrixes + am->mJointMatrixOffset;
                // Cast to void* to suppress warning due to doing memcpy on a class that has an assignment operator
                memcpy((void*)dst, (void*)src, sizeof(mat4) * am->pGeomData->mJointCount);
            }

            // Mesh Data
            {
                const uint32_t meshDataIdx = gSceneMeshCount + gStaticMeshInstanceCount + i;

                MeshData* meshData = &currentFrame->pMeshData[meshDataIdx];
                meshData->prevModelMtx = meshData->modelMtx;
                meshData->modelMtx = mat4::translation(am->mTranslation) * mat4::rotationQuat(am->mRotation) * mat4::scale(am->mScale);
                meshData->invModelMtx = inverse(meshData->modelMtx);
                meshData->preSkinnedVertexOffset = preSkinnedVertexStartOffsetInUnifiedBuffer + am->mPreSkinnedVertexOffset;
            }
        }
    }
    /************************************************************************/
    // UI
    /************************************************************************/
    void updateUI()
    {
        if (UI_WINDOW_IS_VISIBLE(uiBeginWidgetWindow(&gGuiWindowDesc)))
        {
            if (UI_WIDGET_IS_VISIBLE(uiCollapsingHeaderBegin("Transparent Alphas", true)))
            {
                uiLayoutAutoTextRows(2);
                uiLabel("Purple Flag", TF_ALIGN_LEFT);
                uiSliderFloat(&gFlagAlphaPurple, 0.0f, 1.0f, 0.01f);
                uiLabel("Blue Flag", TF_ALIGN_LEFT);
                uiSliderFloat(&gFlagAlphaBlue, 0.0f, 1.0f, 0.01f);
                uiLabel("Green Flag", TF_ALIGN_LEFT);
                uiSliderFloat(&gFlagAlphaGreen, 0.0f, 1.0f, 0.01f);
                uiLabel("Red Flag", TF_ALIGN_LEFT);
                uiSliderFloat(&gFlagAlphaRed, 0.0f, 1.0f, 0.01f);
                uiCollapsingHeaderEnd();
            }

#ifdef ENABLE_STREAMING
            {
                uiLayoutAutoTextRows(1);
                uiCheckbox("Draw Debug Boxes", &gAppSettings.mDrawDebugBoxes);
                uiCheckbox("Use Pixel Coverage", &gAppSettings.mUsePixelCoverage);
                uiLayoutAutoTextRows(2);
                uiLabel("Max live Textures", TF_ALIGN_LEFT);
                uiSliderUint(&gAppSettings.mLiveTexturesMaxCount, 0, (uint32_t)gTextureCount, 1);
            }
#endif
            uiLayoutAutoTextRows(1);
            uiCheckbox("Update Simulation", &gAppSettings.mUpdateSimulation);
            uiCheckbox("Hold filtered results", &gAppSettings.mHoldFilteredResults);
            uiCheckbox("Async Compute", &gAppSettings.mAsyncCompute);
            uiCheckbox("Draw Debug Targets", &gAppSettings.mDrawDebugTargets);
#if defined(TF_ENABLE_WORKGRAPH)
            if (pRenderer->pGpu->mWorkgraphSupported)
            {
                uiCheckbox("Enable GPU pipeline workgraph", &gAppSettings.mGpuPipelineWorkgraph);
            }
#endif
            /************************************************************************/
            /************************************************************************/
            if (pRenderer->pGpu->mHDRSupported)
            {
                static const char* outputModeNames[] = { "SDR", "HDR10" };
                static const int   outputModeNamesCount = sizeof(outputModeNames) / sizeof(outputModeNames[0]);

                uiLabel(pGuiOutputSupportsHDRLabel, TF_ALIGN_LEFT);
                uiLayoutAutoTextRows(2);
                uiLabel("Output Mode", TF_ALIGN_LEFT);
                int selected = gAppSettings.mOutputMode;
                gAppSettings.mOutputMode = (OutputMode)UI_WIDGET_GET_SELECTED(uiDropdown(outputModeNames, outputModeNamesCount, selected));
            }
            uiLayoutAutoTextRows(1);
            uiCheckbox("Cinematic Camera walking", &gAppSettings.cameraWalking);
            uiLayoutAutoTextRows(2);
            uiLabel("Cinematic Camera walking: Speed", TF_ALIGN_LEFT);
            uiSliderFloat(&gAppSettings.cameraWalkingSpeed, 0.0f, 3.0f, 0.01f);

            // Light Settings
            //---------------------------------------------------------------------------------
            // Offset max angle for sun control so the light won't bleed with
            // small glancing angles, i.e., when lightDir is almost parallel to the plane

            uiLayoutAutoRows(2);
            uiLabel("Time of Day", TF_ALIGN_LEFT);
            uiSliderFloat(&gAppSettings.mTimeOfDay, gAppSettings.mTimeOfDaySunrise, gAppSettings.mTimeOfDaySunset, 0.1f);

            uiLayoutAutoRows(5);
            uiLabel("Light Color & Intensity", TF_ALIGN_LEFT);
            uiSliderFloat4(&gAppSettings.mLightColor, float4(0.0f), float4(30.0f), float4(0.01f));
            uiLayoutAutoTextRows(2);
            uiLabel("ESM Control", TF_ALIGN_LEFT);
            uiSliderFloat(&gAppSettings.mEsmControl, 0.0f, 400.0f, 0.01f);
            if (gAppSettings.mEnableGodray)
            {
                uiLayoutAutoTextRows(2);
                uiLabel("God Ray : Scatter Factor", TF_ALIGN_LEFT);
                uiSliderFloat(&gGodRayConstant.mScatterFactor, 0.0f, 1.0f, 0.01f);
                uiLabel("God Ray : Gaussian Blur Kernel Size", TF_ALIGN_LEFT);
                uiSliderUint(&gAppSettings.mFilterRadius, 1u, 8u, 1u);
                uiLabel("God Ray : Gaussian Blur Sigma", TF_ALIGN_LEFT);
                uiSliderFloat(&gGaussianBlurSigma[0], 0.1f, 5.0f, 0.01f);
            }
            uiLayoutAutoTextRows(1);
            uiCheckbox("Enable Random Point Lights", &gAppSettings.mRenderLocalLights);

            /************************************************************************/
            // Display Settings
            /************************************************************************/
            {
                static const char* displayColorRangeNames[] = { "RGB" };
                static const int   displayColorRangeNamesCount = sizeof(displayColorRangeNames) / sizeof(displayColorRangeNames[0]);
                static const char* displaySignalRangeNames[] = { "Range Full", "Range Limited" };
                static const int   displaySignalRangeNamesCount = sizeof(displaySignalRangeNames) / sizeof(displaySignalRangeNames[0]);
                static const char* displayColorSpaceNames[] = { "ColorSpace Rec709", "ColorSpace Rec2020", "ColorSpace P3D65" };
                static const int   displayColorSpaceNamesCount = sizeof(displayColorSpaceNames) / sizeof(displayColorSpaceNames[0]);

                uiLayoutAutoTextRows(2);
                uiLabel("Display Color Range", TF_ALIGN_LEFT);
                int selected = gAppSettings.mDisplayColorRange;
                gAppSettings.mDisplayColorRange =
                    (DisplayColorRange)UI_WIDGET_GET_SELECTED(uiDropdown(displayColorRangeNames, displayColorRangeNamesCount, selected));

                uiLabel("Display Signal Range", TF_ALIGN_LEFT);
                selected = gAppSettings.mDisplaySignalRange;
                gAppSettings.mDisplaySignalRange =
                    (DisplaySignalRange)UI_WIDGET_GET_SELECTED(uiDropdown(displaySignalRangeNames, displaySignalRangeNamesCount, selected));

                uiLabel("Display Color Space", TF_ALIGN_LEFT);
                selected = gAppSettings.mCurrentSwapChainColorSpace;
                gAppSettings.mCurrentSwapChainColorSpace =
                    (DisplayColorSpace)UI_WIDGET_GET_SELECTED(uiDropdown(displayColorSpaceNames, displayColorSpaceNamesCount, selected));

                if (gAppSettings.mOutputMode != OutputMode::OUTPUT_MODE_SDR)
                {
                    uiLabel("Linear Scale", TF_ALIGN_LEFT);
                    uiSliderFloat(&gAppSettings.LinearScale, 80.0f, 400.0f, 0.1f);
                }
            }
        }
        uiEndWidgetWindow();

        // Buffer chunk allocator stats
        if (UI_WINDOW_IS_VISIBLE(uiBeginWidgetWindow(&gHistogramWindowDesc)))
        {
            const uint32_t vtxPlotCount = gGeometryBufferVertexBufferCount;

            uiLayoutAutoTextRows(1);
            if (UI_WIDGET_IS_CHANGED(uiCheckbox("Width relative to size", &gScaleGeometryPlots)))
            {
                recreateGeometryBufferPlots(vtxPlotCount);
            }

            int64_t*    intData = NULL;
            uint32_t    hoveredIdx = 0;
            const float rowHeight = 30.0f;
            float2      size = mBufferChunkAllocatorPlots[0].mSize;
            vec2        sizeRatio = { size.x / gHistogramWindowDesc.mStartSize[0], size.y / rowHeight };
            uiLayoutSpaceBegin(TF_LAYOUT_DYNAMIC, rowHeight, 3);
            uiLayoutSpacePush({ 0.0f, 0.0f }, { sizeRatio });
            uiChartBegin(TF_UI_CHART_COLUMN, (int)mBufferChunkAllocatorPlots[0].mSize[0], 0.0f, 1.0f);
            for (uint32_t i = 0; i < (uint32_t)mBufferChunkAllocatorPlots[0].mSize[0]; i++)
            {
                uiChartColumnCalcColor(mBufferChunkAllocatorPlots[0].pValues + 2, i);
                if (UI_WIDGET_IS_CHART_POINT_HOVERED(uiChartPush(1.0f)))
                {
                    intData = mBufferChunkAllocatorPlots[0].pValues + 2;
                    hoveredIdx = i;
                }
            }
            uiLayoutSpacePush({ 0.0f, 0.2f }, { sizeRatio[0], 0.3f });
            uiLabelBufferAllocatorPlot(&mBufferChunkAllocatorPlots[0]);
            uiLayoutSpacePush({ sizeRatio[0], 0.3f }, { 1.0f - sizeRatio[0], 0.3f });
            uiLabel(gGeometryBufferLoadDesc.pNameIndexBuffer, TF_ALIGN_LEFT);
            uiLayoutSpaceEnd();

            for (uint32_t i = 0; i < vtxPlotCount; ++i)
            {
                if (gGeometryBufferLoadDesc.pNamesVertexBuffers[i] == NULL)
                    break;

                uint32_t vIdx = i + 1;
                size = mBufferChunkAllocatorPlots[vIdx].mSize;
                sizeRatio = { size.x / gHistogramWindowDesc.mStartSize[0], size.y / rowHeight };
                uiLayoutSpaceBegin(TF_LAYOUT_DYNAMIC, rowHeight, 3);
                uiLayoutSpacePush({ 0.0f, 0.0f }, { sizeRatio });
                uiChartBegin(TF_UI_CHART_COLUMN, (int)mBufferChunkAllocatorPlots[vIdx].mSize[0], 0.0f, 1.0f);
                for (uint32_t j = 0; j < (uint32_t)mBufferChunkAllocatorPlots[vIdx].mSize[0]; j++)
                {
                    uiChartColumnCalcColor(mBufferChunkAllocatorPlots[vIdx].pValues + 2, j);
                    if (UI_WIDGET_IS_CHART_POINT_HOVERED(uiChartPush(1.0f)))
                    {
                        intData = mBufferChunkAllocatorPlots[vIdx].pValues + 2;
                        hoveredIdx = j;
                    }
                }
                uiLayoutSpacePush({ 0.0f, 0.2f }, { sizeRatio[0], 0.3f });
                uiLabelBufferAllocatorPlot(&mBufferChunkAllocatorPlots[vIdx]);
                uiLayoutSpacePush({ sizeRatio[0], 0.3f }, { 1.0f - sizeRatio[0], 0.3f });
                uiLabel(gGeometryBufferLoadDesc.pNamesVertexBuffers[i], TF_ALIGN_LEFT);
                uiLayoutSpaceEnd();
            }
            uiChartResetColor();

            uiTooltipBufferAllocatorPlot(intData, hoveredIdx);
        }
        uiEndWidgetWindow();

        if (gAppSettings.mDrawDebugTargets)
        {
            if (UI_WINDOW_IS_VISIBLE(uiBeginWidgetWindow(&gDebugTexturesWindowDesc)))
            {
                const float  scale = 0.15f;
                const float2 screenSize = { (float)pRenderTargetVBPass->mWidth * gDivider, (float)pRenderTargetVBPass->mHeight * gDivider };
                const float2 texSize = screenSize * scale;

                static const TFTexture* VBRTs[6];
                uint32_t                textureCount = 0;

                VBRTs[textureCount++] = pRenderTargetShadow->pTexture;

                // VRS debug textures
                if (gAppSettings.mEnableVRS)
                {
                    VBRTs[textureCount++] = pDebugVRSRenderTarget->pTexture;
                }

                // Prog MSAA debug textures
                if (gAppSettings.mMSAALevel > TF_SAMPLE_COUNT_1 && !gAppSettings.mEnableVRS)
                {
                    if (gAppSettings.mEnableGodray)
                    {
                        VBRTs[textureCount++] = pRenderTargetDebugGodRayMSAA->pTexture;
                    }
                    VBRTs[textureCount++] = pRenderTargetDebugMSAA->pTexture;
                }
                ASSERT(textureCount <= TF_ARRAY_COUNT(VBRTs));

                uiLayoutAutoTextRows(1);
                uiLabel("Debug RTs", TF_ALIGN_LEFT);
                uiLayoutSpaceBegin(TF_LAYOUT_STATIC, 0, textureCount);
                float startPosX = 0.0f;
                for (uint32_t i = 0; i < textureCount; i++)
                {
                    uiLayoutSpacePush(vec2(startPosX, 0), texSize);
                    uiDebugTexture(VBRTs[i], texSize);
                    startPosX += texSize.x + 10.0f;
                }
                uiLayoutSpaceEnd();
            }
            uiEndWidgetWindow();
        }
    }

    void updateDynamicUIElements()
    {
        updateUI();

        // Async compute
        {
            static bool gPrevAsyncCompute = gAppSettings.mAsyncCompute;
            if (gPrevAsyncCompute != gAppSettings.mAsyncCompute)
            {
                gPrevAsyncCompute = gAppSettings.mAsyncCompute;
                waitQueueIdle(pGraphicsQueue);
                waitQueueIdle(pComputeQueue);

                // Make sure async semaphores are not still in signaled state
                for (uint32_t i = 0; i < gDataBufferCount; ++i)
                {
                    exitSemaphore(pRenderer, pGraphicsAsyncSemaphores[i]);
                    initSemaphore(pRenderer, &pGraphicsAsyncSemaphores[i]);
                }

                gFrameCount = 0;
                gPrevGraphicsSemaphore = NULL;
                memset(gComputeSemaphores, 0, sizeof(gComputeSemaphores));
            }
        }
    }

    void luaRegisterUI()
    {
        TFLuaWidgetVariableDesc luaVarDesc = {};

        // float sliders
        luaVarDesc.mWidgetType = TF_WIDGET_TYPE_SLIDER_FLOAT;
        luaVarDesc.pLabel = "Purple Flag";
        luaVarDesc.pFloat = &gFlagAlphaPurple;
        luaRegisterWidgetVariable(&luaVarDesc);
        luaVarDesc.pLabel = "Blue Flag";
        luaVarDesc.pFloat = &gFlagAlphaBlue;
        luaRegisterWidgetVariable(&luaVarDesc);
        luaVarDesc.pLabel = "Green Flag";
        luaVarDesc.pFloat = &gFlagAlphaGreen;
        luaRegisterWidgetVariable(&luaVarDesc);
        luaVarDesc.pLabel = "Red Flag";
        luaVarDesc.pFloat = &gFlagAlphaRed;
        luaRegisterWidgetVariable(&luaVarDesc);
        luaVarDesc.pLabel = "Cinematic Camera walking: Speed";
        luaVarDesc.pFloat = &gAppSettings.cameraWalkingSpeed;
        luaRegisterWidgetVariable(&luaVarDesc);
        luaVarDesc.pLabel = "ESM Control";
        luaVarDesc.pFloat = &gAppSettings.mEsmControl;
        luaRegisterWidgetVariable(&luaVarDesc);
        luaVarDesc.pLabel = "God Ray : Scatter Factor";
        luaVarDesc.pFloat = &gGodRayConstant.mScatterFactor;
        luaRegisterWidgetVariable(&luaVarDesc);
        luaVarDesc.pLabel = "God Ray : Gaussian Blur Sigma";
        luaVarDesc.pFloat = &gGaussianBlurSigma[0];
        luaRegisterWidgetVariable(&luaVarDesc);
        luaVarDesc.mWidgetType = TF_WIDGET_TYPE_SLIDER_FLOAT;
        luaVarDesc.pLabel = "Time of Day";
        luaVarDesc.pFloat = &gAppSettings.mTimeOfDay;
        luaRegisterWidgetVariable(&luaVarDesc);

        // float4 sliders
        luaVarDesc.mWidgetType = TF_WIDGET_TYPE_SLIDER_FLOAT4;
        luaVarDesc.pLabel = "Light Color & Intensity";
        luaVarDesc.pFloat4 = &gAppSettings.mLightColor;
        luaRegisterWidgetVariable(&luaVarDesc);

        // uint sliders
        luaVarDesc.mWidgetType = TF_WIDGET_TYPE_SLIDER_UINT;
#ifdef ENABLE_STREAMING
        luaVarDesc.pLabel = "Max live Textures";
        luaVarDesc.pUint = &gAppSettings.mLiveTexturesMaxCount;
        luaRegisterWidgetVariable(&luaVarDesc);
#endif
        luaVarDesc.pLabel = "God Ray : Gaussian Blur Kernel Size";
        luaVarDesc.pUint = &gAppSettings.mFilterRadius;
        luaRegisterWidgetVariable(&luaVarDesc);

        // Checkboxes
        luaVarDesc.mWidgetType = TF_WIDGET_TYPE_CHECKBOX;
#ifdef ENABLE_STREAMING
        luaVarDesc.pLabel = "Draw Debug Boxes";
        luaVarDesc.pBool = &gAppSettings.mDrawDebugBoxes;
        luaRegisterWidgetVariable(&luaVarDesc);
        luaVarDesc.pLabel = "Use Pixel Coverage";
        luaVarDesc.pBool = &gAppSettings.mUsePixelCoverage;
        luaRegisterWidgetVariable(&luaVarDesc);
#endif
        luaVarDesc.pLabel = "Enable Variable Rate Shading";
        luaVarDesc.pBool = &gAppSettings.mEnableVRS;
        luaRegisterWidgetVariable(&luaVarDesc);
        luaVarDesc.pLabel = "Update Simulation";
        luaVarDesc.pBool = &gAppSettings.mUpdateSimulation;
        luaRegisterWidgetVariable(&luaVarDesc);
        luaVarDesc.pLabel = "Hold filtered results";
        luaVarDesc.pBool = &gAppSettings.mHoldFilteredResults;
        luaRegisterWidgetVariable(&luaVarDesc);
        luaVarDesc.pLabel = "Async Compute";
        luaVarDesc.pBool = &gAppSettings.mAsyncCompute;
        luaRegisterWidgetVariable(&luaVarDesc);
        luaVarDesc.pLabel = "Draw Debug Targets";
        luaVarDesc.pBool = &gAppSettings.mDrawDebugTargets;
        luaRegisterWidgetVariable(&luaVarDesc);
#if defined(TF_ENABLE_WORKGRAPH)
        if (pRenderer->pGpu->mWorkgraphSupported)
        {
            luaVarDesc.pLabel = "Enable GPU pipeline workgraph";
            luaVarDesc.pBool = &gAppSettings.mGpuPipelineWorkgraph;
            luaRegisterWidgetVariable(&luaVarDesc);
        }
#endif
        luaVarDesc.pLabel = "Cinematic Camera walking";
        luaVarDesc.pBool = &gAppSettings.cameraWalking;
        luaRegisterWidgetVariable(&luaVarDesc);
        luaVarDesc.pLabel = "Enable Godray";
        luaVarDesc.pBool = &gAppSettings.mEnableGodray;
        luaRegisterWidgetVariable(&luaVarDesc);
        luaVarDesc.pLabel = "Enable Random Point Lights";
        luaVarDesc.pBool = &gAppSettings.mRenderLocalLights;
        luaRegisterWidgetVariable(&luaVarDesc);
    }

    /************************************************************************/
    // Rendering
    /************************************************************************/
    void drawMSAADebugRenderTargetsPass(TFCmd* const cmd, uint32_t frameIdx, uint32_t sourceRTIndex, TFRenderTarget* targetRT)
    {
        TFRenderTargetBarrier barrier[] = { { targetRT, TF_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, TF_RESOURCE_STATE_RENDER_TARGET } };
        cmdResourceBarrier(cmd, 0, NULL, 0, NULL, 1, barrier);

        TFBindRenderTargetsDesc bindRenderTargets = {};
        bindRenderTargets.mRenderTargetCount = 1;
        bindRenderTargets.mRenderTargets[0] = { targetRT, TF_LOAD_ACTION_CLEAR };

        cmdBindRenderTargets(cmd, &bindRenderTargets);
        cmdSetViewport(cmd, 0.0f, 0.0f, (float)targetRT->mWidth, (float)targetRT->mHeight, 0.0f, 1.0f);
        cmdSetScissor(cmd, 0, 0, targetRT->mWidth, targetRT->mHeight);

        cmdBindPipeline(cmd, pPipelineDebugMSAA);
        // We can re-use the resolve descriptor set here for code simplicity
        cmdBindDescriptorSet(cmd, frameIdx * gResolveTargetCount + sourceRTIndex, pDescriptorSetResolve);
        cmdDraw(cmd, 3, 0);

        cmdBindRenderTargets(cmd, NULL);

        // Encapsulate all debugging behavior here, waiting for rendering to finish here isn't optimal
        // but results in cleaner code.
        barrier[0] = { targetRT, TF_RESOURCE_STATE_RENDER_TARGET, TF_RESOURCE_STATE_PIXEL_SHADER_RESOURCE };
        cmdResourceBarrier(cmd, 0, NULL, 0, NULL, 1, barrier);
    }

    // Draw GUI / 2D elements
    void drawGUIPass(TFCmd* cmd, uint32_t presentIndex)
    {
        TFRenderTarget*         rt = pSwapChain->ppRenderTargets[presentIndex];
        TFBindRenderTargetsDesc bindRenderTargets = {};
        bindRenderTargets.mRenderTargetCount = 1;
        bindRenderTargets.mRenderTargets[0] = { rt, TF_LOAD_ACTION_LOAD };

        bindRenderTargets.mSampleLocation.mGridSizeX = 1;
        bindRenderTargets.mSampleLocation.mGridSizeY = 1;
        TFSampleLocations location{};
        bindRenderTargets.mSampleLocation.pLocations = &location;

        cmdBindRenderTargets(cmd, &bindRenderTargets);
        cmdSetViewport(cmd, 0.0f, 0.0f, (float)rt->mWidth, (float)rt->mHeight, 0.0f, 1.0f);
        cmdSetScissor(cmd, 0, 0, rt->mWidth, rt->mHeight);

        gFrameTimeDraw.mFontColor = 0xff00ffff;
        gFrameTimeDraw.mFontSize = 18.0f;
        gFrameTimeDraw.pFont = gFont;
        cmdDrawCpuProfile(cmd, float2(8.0f, 15.0f), &gFrameTimeDraw);
        if (gAppSettings.mAsyncCompute)
        {
            if (!gAppSettings.mHoldFilteredResults || gAppSettings.mUpdateSimulation)
            {
                cmdDrawGpuProfile(cmd, float2(8.0f, 100.0f), gComputeProfileToken, &gFrameTimeDraw);
                cmdDrawGpuProfile(cmd, float2(8.0f, 425.0f), gGraphicsProfileToken, &gFrameTimeDraw);
            }
            else
            {
                cmdDrawGpuProfile(cmd, float2(8.0f, 100.0f), gGraphicsProfileToken, &gFrameTimeDraw);
            }
        }
        else
        {
            cmdDrawGpuProfile(cmd, float2(8.0f, 100.0f), gGraphicsProfileToken, &gFrameTimeDraw);
        }

#if defined(ENABLE_STREAMING) && !defined(AUTOMATED_TESTING)
        // Debug text for streaming
        TFStreamingState streamingState = {};
        getStreamingState(&streamingState);

        float2 textPosition = { 768, 50 };
        gFrameTimeDraw.pText = "Streaming Demo";
        cmdDrawText(cmd, textPosition, &gFrameTimeDraw);

        snprintf(gDebugText, 256, "VRAM heap size: %d MB", (uint32_t)streamingState.mHeapSize / (1024 * 1024));
        gFrameTimeDraw.pText = gDebugText;
        textPosition += float2(0.0f, 40.0f);
        cmdDrawText(cmd, textPosition, &gFrameTimeDraw);

        snprintf(gDebugText, 256, "Used VRAM heap : %d MB", (uint32_t)streamingState.mUsedHeapMemory / (1024 * 1024));
        gFrameTimeDraw.pText = gDebugText;
        textPosition += float2(0.0f, 40.0f);
        cmdDrawText(cmd, textPosition, &gFrameTimeDraw);

        snprintf(gDebugText, 256, "free chunks count : %u", (uint32_t)streamingState.mFreeChunksCount);
        gFrameTimeDraw.pText = gDebugText;
        textPosition += float2(0.0f, 40.0f);
        cmdDrawText(cmd, textPosition, &gFrameTimeDraw);

        snprintf(gDebugText, 256, "textures in memory : %u", (uint32_t)streamingState.mLiveTexturesCount);
        gFrameTimeDraw.pText = gDebugText;
        textPosition += float2(0.0f, 40.0f);
        cmdDrawText(cmd, textPosition, &gFrameTimeDraw);
        snprintf(gDebugText, 256, streamingState.mState == TF_StreamingIdle ? "Streaming is converged" : "Streaming is active");

        gFrameTimeDraw.pText = gDebugText;
        textPosition += float2(0.0f, 40.0f);
        cmdDrawText(cmd, textPosition, &gFrameTimeDraw);

        snprintf(gDebugText, 256, "current sector index is %d %d %d %d", gWorldLocation.mSectorCoord.x, gWorldLocation.mSectorCoord.y,
                 gWorldLocation.mQuadrants.x, gWorldLocation.mQuadrants.y);
        gFrameTimeDraw.pText = gDebugText;
        textPosition += float2(0.0f, 40.0f);
        cmdDrawText(cmd, textPosition, &gFrameTimeDraw);

        vec3 cameraPosition = pCamera->getViewPosition();

        snprintf(gDebugText, 256, "camera position is %f %f %f", (double)cameraPosition.x, (double)cameraPosition.y,
                 (double)cameraPosition.z);
        gFrameTimeDraw.pText = gDebugText;
        textPosition += float2(0.0f, 40.0f);
        cmdDrawText(cmd, textPosition, &gFrameTimeDraw);

#endif

        uiCmdDrawUserInterface(cmd, pSwapChain, rt, gGraphicsProfileToken);

        cmdBindRenderTargets(cmd, NULL);
    }

    static void recreateGeometryBufferPlots(uint32_t plotCount)
    {
        const float2 defaultSize(400.f, 30.f);
        size_t       maxSize = pGeometryBuffer->mIndex.mSize;
        float2       size = defaultSize;

        for (uint32_t i = 0; i < plotCount; ++i)
            if (pGeometryBuffer->mVertex[i].mSize > maxSize)
                maxSize = pGeometryBuffer->mVertex[i].mSize;

        if (gScaleGeometryPlots)
            size[0] = ((float)pGeometryBuffer->mIndex.mSize * defaultSize.x) / (float)maxSize;

        fillGeometryBufferPlot(0, size, &pGeometryBuffer->mIndex);

        for (uint32_t i = 0; i < plotCount; ++i)
        {
            size = defaultSize;
            if (gScaleGeometryPlots)
                size[0] = ((float)pGeometryBuffer->mVertex[i].mSize * defaultSize.x) / (float)maxSize;

            fillGeometryBufferPlot(i + 1, size, &pGeometryBuffer->mVertex[i]);
        }
    }

    static inline uint64_t histogramPointOffsetAproximation(uint32_t point, uint32_t pointCount, uint64_t bufferSize)
    {
        return (point * bufferSize) / pointCount;
    }

    static void fillGeometryBufferPlot(uint32_t index, float2 size, TFBufferChunkAllocator* data)
    {
        BufferAllocatorPlotData* pPlotData = &mBufferChunkAllocatorPlots[index];

        if (pPlotData->mSize.x != size.x || pPlotData->pValues == NULL)
        {
            tf_free(pPlotData->pValues);

            const uint32_t nValues = (uint32_t)size[0];
            const size_t   allocSize = sizeof(int64_t) * 2 * (nValues + 2);
            pPlotData->pValues = (int64_t*)tf_calloc(1, allocSize);
            memset(pPlotData->pValues, 0, allocSize);
        }

        pPlotData->mSize = size;

        uint32_t nValues = (uint32_t)pPlotData->mSize[0];
        int64_t* values = pPlotData->pValues;

        uint32_t unusedChunkCount = (uint32_t)arrlenu(data->mUnusedChunks);

        values[0] = (int64_t)data->mSize;
        ++values;

        int64_t* fragmentCount = values;
        ++values;

        uint32_t point = 0;
        int64_t  intensity = 0;

        int64_t floatingOccupiedChunks = unusedChunkCount + 1;

        for (uint32_t ci = 0; ci < unusedChunkCount; ++ci)
        {
            TFBufferChunk* freeChunk = data->mUnusedChunks + ci;

            if (ci == 0 && freeChunk->mOffset == 0)
                floatingOccupiedChunks -= 1;
            if (ci == unusedChunkCount - 1 && freeChunk->mOffset + freeChunk->mSize == data->mSize)
                floatingOccupiedChunks -= 1;

            uint64_t point_beg = 0;

            // While we are on occupied zone
            for (; point < nValues; ++point)
            {
                point_beg = histogramPointOffsetAproximation(point, nValues, data->mSize);

                if (point_beg >= freeChunk->mOffset)
                    break;

                values[2 * point] = (int64_t)point_beg;
                values[2 * point + 1] = intensity;

                intensity = 0;
            }

            // We hit free zone, add free chunk in a point
            ++intensity;

            // While we are in the free chunk
            for (uint64_t point_end = point_beg; point < nValues; ++point)
            {
                point_beg = point_end;
                point_end = histogramPointOffsetAproximation(point + 1, nValues, data->mSize);

                if (point_end > freeChunk->mOffset + freeChunk->mSize)
                    break;

                values[2 * point] = -(int64_t)point_beg;
                values[2 * point + 1] = intensity;

                intensity = 1;
            }
        }

        *fragmentCount = floatingOccupiedChunks;

        // Fill remaining space as occupied
        while (point < nValues)
        {
            values[2 * point] = (int64_t)histogramPointOffsetAproximation(point, nValues, data->mSize);
            values[2 * point + 1] = intensity;
            ++point;
            intensity = 0;
        }
    }

    static void uiLabelBufferAllocatorPlot(BufferAllocatorPlotData* data)
    {
        const int64_t* values = data->pValues;

        if (!values)
            return;

        unsigned char labelBuf[MAX_LABEL_STR_LENGTH];
        bstring       label = bemptyfromarr(labelBuf);
        bformat(&label, "%s##%p", data->pName, values);
        ASSERT(!bownsdata(&label));

        char title[1024];

        size_t tlen = strlen(data->pName);

        if (tlen > 1023)
            tlen = 1023;
        memcpy(title, data->pName, tlen);
        title[tlen] = 0;

        if (tlen < 900)
        {
            snprintf(title + tlen, TF_ARRAY_COUNT(title) - tlen, " %s; fragments: %lli", humanReadableSize((size_t)values[0]).str,
                     (long long)values[1]);
            title[tlen++] = ' ';
        }
        uiLabel(title, TF_ALIGN_CENTER);
    }

    static void uiTooltipBufferAllocatorPlot(int64_t* intData, uint32_t pointIdx)
    {
        if (!intData)
        {
            return;
        }

        char      tooltipBuf[1024];
        long long lb = intData[pointIdx * 2];
        long long intensity = intData[pointIdx * 2 + 1];
        long long rb = intData[pointIdx * 2 + 2];

        if (lb < 0)
            lb = -lb;
        if (rb < 0)
            rb = -rb;

        long long size = rb - lb;
        snprintf(tooltipBuf, TF_ARRAY_COUNT(tooltipBuf), "[%lli;%lli) %lli; [%s;%s) %s; free chunk count: %lli", lb, rb, size,
                 humanReadableSize(lb).str, humanReadableSize(rb).str, humanReadableSize(size).str, intensity);
        uiTooltipText(tooltipBuf);
    }

    static void uiChartColumnCalcColor(int64_t* intData, uint32_t pointIdx)
    {
        int64_t intensity = intData[pointIdx * 2 + 1];

        // Set redness based on intensity
        uint8_t blue = 0x33;
        uint8_t green = 0x99;
        uint8_t red = 0xff;
        while (intensity > 0 && red > 0)
        {
            red /= 2;
            blue /= 2;
            green /= 2;
            --intensity;
        }
        red = 255 - red;

        uiChartSetColor({ red / 255.f, green / 255.f, blue / 255.f, 1.0f }, float4(1.0f));
    }
};

DEFINE_APPLICATION_MAIN(VisibilityBufferOIT)
