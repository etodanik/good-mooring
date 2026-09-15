/*
 *
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

// Unit Test for testing materials and pbr.

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
#include "../../../../Common_3/Renderer/Interfaces/IVisibilityBuffer.h"
#include "../../../../Common_3/Resources/ResourceLoader/Interfaces/IResourceLoader.h"
#include "../../../../Common_3/Utilities/Interfaces/IFileSystem.h"
#include "../../../../Common_3/Utilities/Interfaces/ILog.h"
#include "../../../../Common_3/Utilities/Interfaces/ITime.h"

#include "../../../../Common_3/Utilities/RingBuffer.h"
#include "../../../../Common_3/Utilities/Threading/Atomics.h"

// Math
#include "../../../../Common_3/Utilities/Interfaces/IMath.h"

// Input
#include "../../../../Common_3/Utilities/Threading/ThreadSystem.h"

#include "SamplerBlueNoiseErrorDistribution_128x128_OptimizedFor_2d2d2d2d_2spp.cpp"

#include "../../../../Common_3/Utilities/Interfaces/IMemory.h"

#define NO_FSL_DEFINITIONS
#include "../../../../Common_3/Graphics/FSL/fsl_srt.h"
#include "Shaders/FSL/ShaderDefs.h.fsl"

// fsl
#include "../../../../Common_3/Graphics/FSL/defaults.h"
#include "./Shaders/FSL/Global.srt.h"
#include "./Shaders/FSL/SSSR.srt.h"
#include "./Shaders/FSL/PPR.srt.h"
#include "./Shaders/FSL/PBR.srt.h"
#include "./Shaders/FSL/GenerateMips.srt.h"
#include "./Shaders/FSL/DepthDownsample.srt.h"
#include "./Shaders/FSL/CopyDepth.srt.h"
#include "./Shaders/FSL/TriangleFiltering.srt.h"

#define MAX_PLANES                  4
#define SCENE_SCALE                 10.0f
#define MIN_BINDLESS_TEXTURES_COUNT 1024
#define FOREACH_SETTING(X)       \
    X(AddGeometryPassThrough, 0) \
    X(BindlessSupported, 1)

#define GENERATE_ENUM(x, y)    x,
#define GENERATE_STRING(x, y)  #x,
#define GENERATE_STRUCT(x, y)  uint32_t m##x;
#define GENERATE_VALUE(x, y)   y,
#define INIT_STRUCT(s)         s = { FOREACH_SETTING(GENERATE_VALUE) }

#define GENERATE_MIPS_MAX_MIPS 16

typedef enum ESettings
{
    FOREACH_SETTING(GENERATE_ENUM) Count
} ESettings;

const char* gSettingNames[] = { FOREACH_SETTING(GENERATE_STRING) };

// Useful for using names directly instead of subscripting an array
struct AppSettings
{
    FOREACH_SETTING(GENERATE_STRUCT)
} gSettings;

// Define different geometry sets (opaque and alpha tested geometry)
static const uint32_t gNumGeomSets = NUM_GEOMETRY_SETS;

// Have a uniform for camera data
struct UniformDataSkybox
{
    mat4 mProjectView;
};

// Have a uniform for extended camera data
struct UniformExtendedCamData
{
    mat4 mViewMat;
    mat4 mInvViewMat;
    mat4 mProjMat;
    mat4 mViewProjMat;
    mat4 mInvViewProjMat;

    vec4 mCameraWorldPos;

    float2 mViewPortSize;
    float  mNear;
    float  mFar;

    float3 mEnvColor;
    float  _pad1;

    float mOverrideRoughness;
    float mOverrideMetallic;
    uint  mOverrideRoughnessMetallic;
    uint  mUseEnvMap;
};

// Have a uniform for PPR properties
struct UniformPPRProData
{
    uint  renderMode;
    float useHolePatching;
    float useExpensiveHolePatching;
    float useNormalMap;

    float intensity;
    float useFadeEffect;
    float debugNonProjected;
    float padding01;
};

struct MeshInfoUniformBlock
{
    mat4 mWorldViewProjMat;
    mat4 mPrevWorldViewProjMat;
};

struct PerFrameData
{
    // Stores the camera/eye position in object space for cluster culling
    vec3     mEyeObjectSpace[NUM_CULLING_VIEWPORTS] = {};
    uint32_t mDrawCount[gNumGeomSets] = { 0 };
};

struct Light
{
    vec4  mPos;
    vec4  mCol;
    float mRadius;
    float mIntensity;
    char  _pad[8];
};

struct UniformLightData
{
    // Used to tell our shaders how many lights are currently present
    Light mLights[16] = {}; // array of lights seem to be broken so just a single light for now
    int   mCurrAmountOfLights = 0;
};

struct DirectionalLight
{
    vec4 mCol; // alpha is the intesity
    vec4 mDir;
};

struct UniformDirectionalLightData
{
    // Used to tell our shaders how many lights are currently present
    DirectionalLight mLights[16]; // array of lights seem to be broken so just a single light for now
    int              mCurrAmountOfDLights = 0;
};

struct PlaneInfo
{
    mat4 rotMat;
    vec4 centerPoint;
    vec4 size;
};

struct UniformPlaneInfoData
{
    PlaneInfo planeInfo[MAX_PLANES];
    uint32_t  numPlanes;
    uint32_t  pad01;
    uint32_t  pad02;
    uint32_t  pad03;
};

struct UniformSSSRConstantsData
{
    mat4 g_inv_view_proj;
    mat4 g_proj;
    mat4 g_inv_proj;
    mat4 g_view;
    mat4 g_inv_view;
    mat4 g_prev_view_proj;

    uint32_t g_frame_index;
    bool     g_clear_targets;
    uint32_t g_max_traversal_intersections;
    uint32_t g_min_traversal_occupancy;
    uint32_t g_most_detailed_mip;
    float    g_temporal_stability_factor;
    float    g_depth_buffer_thickness;
    uint32_t g_samples_per_quad;
    uint32_t g_temporal_variance_guided_tracing_enabled;
    float    g_roughness_threshold;
    uint32_t g_skip_denoiser;
};

enum
{
    SCENE_ONLY = 0,
    REFLECTIONS_ONLY = 1,
    SCENE_WITH_REFLECTIONS = 2,
    SCENE_EXCLU_REFLECTIONS = 3,
};

enum
{
    PP_REFLECTION = 0,
    SSS_REFLECTION = 1,
};

static bool gUseHolePatching;
static bool gUseExpensiveHolePatching;

static bool gUseNormalMap;
static bool gUseFadeEffect;
static bool gDebugNonProjectedPixels;

static uint32_t gRenderMode;
static uint32_t gReflectionType;
static uint32_t gLastReflectionType;

static uint32_t gPlaneNumber;
static float    gPlaneSize;
static float    gRRP_Intensity;
static float    gPlaneRotationOffset;

static bool gUseSPD;

static uint32_t gSSSR_MaxTravelsalIntersections;
#if defined(AUTOMATED_TESTING)
static uint32_t gSSSR_MinTravelsalOccupancy;
#else
static uint32_t gSSSR_MinTravelsalOccupancy;
#endif
static uint32_t gSSSR_MostDetailedMip;
static float    pSSSR_TemporalStability;
static float    gSSSR_DepthThickness;
static int32_t  gSSSR_SamplesPerQuad;
static int32_t  gSSSR_EAWPassCount;
static bool     gSSSR_TemporalVarianceEnabled;
static float    gSSSR_RougnessThreshold;
static bool     gSSSR_SkipDenoiser;

static bool gSSSRSupported;

UniformExtendedCamData gUniformDataExtenedCamera;
bool                   gUseEnvMap;
bool                   gOverrideRoughnessMetallic;

bool gUsingTextureAtlasFallback = false;
bool gUsingPrimitiveIDFallback = false;

TFTexture* gNullTextureResource = NULL;

// We are rendering the scene (geometry, skybox, ...) at this resolution, UI at window resolution (mSettings.mWidth, mSettings.mHeight)
// Render scene at gSceneRes
// Render UI into backbuffer
static TFResolution gSceneRes;

static void initAppSettings()
{
    gUseHolePatching = true;
    gUseExpensiveHolePatching = true;

    gUseNormalMap = false;
    gUseFadeEffect = true;
    gDebugNonProjectedPixels = false;

    gRenderMode = SCENE_WITH_REFLECTIONS;
    gReflectionType = SSS_REFLECTION;
    gLastReflectionType = gReflectionType;

    gPlaneNumber = 1;
    gPlaneSize = 260.0f;
    gRRP_Intensity = 0.2f;
    gPlaneRotationOffset = 0.0f;

    gUseSPD = true;

    gSSSR_MaxTravelsalIntersections = 128;
#if defined(AUTOMATED_TESTING)
    gSSSR_MinTravelsalOccupancy = 0;
#else
    gSSSR_MinTravelsalOccupancy = 4;
#endif
    gSSSR_MostDetailedMip = 1;
    pSSSR_TemporalStability = 0.99f;
    gSSSR_DepthThickness = 0.15f;
    gSSSR_SamplesPerQuad = 1;
    gSSSR_EAWPassCount = 1;
    gSSSR_TemporalVarianceEnabled = true;
    gSSSR_RougnessThreshold = 0.1f;
    gSSSR_SkipDenoiser = false;

    gSSSRSupported = false;

    gUseEnvMap = false;
    gOverrideRoughnessMetallic = true;
    gUniformDataExtenedCamera.mOverrideRoughness = 0.05f;
    gUniformDataExtenedCamera.mOverrideMetallic = 1.0f;
    gUniformDataExtenedCamera.mOverrideRoughnessMetallic = 0;
    gUniformDataExtenedCamera.mEnvColor = float3(0.095f, 0.095f, 0.11f);
}

// #NOTE: Two sets of resources (one in flight and one being used on CPU)
static const uint32_t gDataBufferCount = 2;

ProfileToken gPPRGpuProfileToken;
ProfileToken gSSSRGpuProfileToken;
ProfileToken gCurrentGpuProfileToken;

TFRenderer*  pRenderer = NULL;
TFQueue*     pGraphicsQueue = NULL;
GpuCmdRing   gGraphicsCmdRing = {};
TFSwapChain* pSwapChain = NULL;
TFSemaphore* pImageAcquiredSemaphore[gDataBufferCount] = { NULL };

VisibilityBuffer* pVisibilityBuffer = NULL;

TFRenderTarget* pRenderTargetVBPass = NULL;
TFRenderTarget* pSceneBuffer = NULL;
TFRenderTarget* pNormalRoughnessBuffers[gDataBufferCount] = { NULL };
TFRenderTarget* pReflectionBuffer = NULL;
TFRenderTarget* pDepthBuffer = NULL;

TFDescriptorSet* pDescriptorSetPersistent = NULL;
TFDescriptorSet* pDescriptorSetPerFrame = NULL;
TFDescriptorSet* pDescriptorSetDepthDownSamplePerBatch = NULL;
TFDescriptorSet* pDescriptorSetSSSR = NULL;
TFDescriptorSet* pDescriptorSetPPR = NULL;
TFDescriptorSet* pDescriptorSetCopyDepth = NULL;
TFDescriptorSet* pDescriptorSetTriangleFilteringPerBatch = NULL;

// Clear buffers pipeline
TFShader*   pShaderClearBuffers = NULL;
TFPipeline* pPipelineClearBuffers = NULL;

// Triangle filtering pipeline
TFShader*   pShaderTriangleFiltering = NULL;
TFPipeline* pPipelineTriangleFiltering = NULL;

// VB pass pipeline
TFShader*   pShaderVBBufferPass[gNumGeomSets] = {};
TFPipeline* pPipelineVBBufferPass[gNumGeomSets] = {};

// VB shade pipeline
TFShader*   pShaderVBShade = NULL;
TFPipeline* pPipelineVBShadeSrgb = NULL;

TFBuffer*   pSkyboxVertexBuffer = NULL;
TFShader*   pSkyboxShader = NULL;
TFPipeline* pSkyboxPipeline = NULL;

TFShader*   pPPR_ProjectionShader = NULL;
TFPipeline* pPPR_ProjectionPipeline = NULL;

TFShader*   pPPR_ReflectionShader = NULL;
TFPipeline* pPPR_ReflectionPipeline = NULL;

TFShader*   pPPR_HolePatchingShader = NULL;
TFPipeline* pPPR_HolePatchingPipeline = NULL;

TFShader*   pCopyDepthShader = NULL;
TFPipeline* pCopyDepthPipeline = NULL;

TFShader*        pGenerateMipShader = NULL;
TFPipeline*      pGenerateMipPipeline = NULL;
TFDescriptorSet* pDescriptorGenerateMip = NULL;
uint32_t         gMipSizeRootConstantIndex = 0;

TFShader*   pSPDShader = NULL;
TFPipeline* pSPDPipeline = NULL;

TFShader*   pSSSR_ClassifyTilesShader = NULL;
TFPipeline* pSSSR_ClassifyTilesPipeline = NULL;

TFShader*   pSSSR_PrepareIndirectArgsShader = NULL;
TFPipeline* pSSSR_PrepareIndirectArgsPipeline = NULL;

TFShader*   pSSSR_IntersectShader = NULL;
TFPipeline* pSSSR_IntersectPipeline = NULL;

TFShader*   pSSSR_ResolveSpatialShader = NULL;
TFPipeline* pSSSR_ResolveSpatialPipeline = NULL;

TFShader*   pSSSR_ResolveTemporalShader = NULL;
TFPipeline* pSSSR_ResolveTemporalPipeline = NULL;

TFShader*   pSSSR_ResolveEAWShader = NULL;
TFPipeline* pSSSR_ResolveEAWPipeline = NULL;

TFShader*   pSSSR_ResolveEAWStride2Shader = NULL;
TFPipeline* pSSSR_ResolveEAWStride2Pipeline = NULL;

TFShader*   pSSSR_ResolveEAWStride4Shader = NULL;
TFPipeline* pSSSR_ResolveEAWStride4Pipeline = NULL;

TFBuffer*                pSSSR_ConstantsBuffer[gDataBufferCount] = { NULL };
UniformSSSRConstantsData gUniformSSSRConstantsData;

TFBuffer* pSPD_AtomicCounterBuffer = NULL;

TFBuffer*       pSSSR_RayListBuffer = NULL;
TFBuffer*       pSSSR_TileListBuffer = NULL;
TFBuffer*       pSSSR_RayCounterBuffer = NULL;
TFBuffer*       pSSSR_TileCounterBuffer = NULL;
TFBuffer*       pSSSR_IntersectArgsBuffer = NULL;
TFBuffer*       pSSSR_DenoiserArgsBuffer = NULL;
TFBuffer*       pSSSR_SobolBuffer = NULL;
TFBuffer*       pSSSR_RankingTileBuffer = NULL;
TFBuffer*       pSSSR_ScramblingTileBuffer = NULL;
TFRenderTarget* pSSSR_TemporalResults[gDataBufferCount] = { NULL };
TFTexture*      pSSSR_TemporalVariance = NULL;
TFRenderTarget* pSSSR_RayLength = NULL;
TFTexture*      pSSSR_DepthHierarchy = NULL;

TFBuffer* pScreenQuadVertexBuffer = NULL;

TFBuffer*            pSSSR_GenMipsBuffers[GENERATE_MIPS_MAX_MIPS] = { NULL };
TFTextureDescriptor* pSSSR_DepthHierarchyMipDescs[GENERATE_MIPS_MAX_MIPS] = {};
TFTexture*           pSkybox = NULL;
TFTexture*           pBRDFIntegrationMap = NULL;
TFTexture*           pIrradianceMap = NULL;
TFTexture*           pSpecularMap = NULL;

TFBuffer* pIntermediateBuffer = NULL;
// For clearing Intermediate Buffer
uint32_t* gInitializeVal = NULL;

TFBuffer*            pBufferMeshTransforms[gDataBufferCount] = { NULL };
MeshInfoUniformBlock gMeshInfoUniformData[gDataBufferCount];

TFBuffer* pBufferMeshConstants = NULL;

UniformDataSkybox gUniformDataSky;

TFBuffer* pBufferUniformExtendedCamera[gDataBufferCount] = { NULL };

TFBuffer* pBufferUniformCameraSky[gDataBufferCount] = { NULL };

TFBuffer*         pBufferUniformPPRPro[gDataBufferCount] = { NULL };
UniformPPRProData gUniformPPRProData;

TFBuffer*        pBufferUniformLights = NULL;
UniformLightData gUniformDataLights;

TFBuffer*                   pBufferUniformDirectionalLights = NULL;
UniformDirectionalLightData gUniformDataDirectionalLights;

TFBuffer*            pBufferUniformPlaneInfo[gDataBufferCount] = { NULL };
UniformPlaneInfoData gUniformDataPlaneInfo;

TFBuffer*               pBufferVBConstants[gDataBufferCount] = { NULL };
PerFrameVBConstantsData gVBConstants[gDataBufferCount] = {};

PerFrameData gPerFrameData[gDataBufferCount] = {};

TFShader*   pShaderPostProc = NULL;
TFPipeline* pPipelinePostProc = NULL;

TFSampler* pDefaultSampler = NULL;
TFSampler* pSamplerBilinear = NULL;
TFSampler* pSamplerNearestClampToEdge = NULL;

uint32_t gFrameCount = 0;

TFICamera* pCamera = NULL;

TFFontDrawDesc gFrameTimeDraw;
TFFont*        gFont;

TFUIWindowDesc gGuiDesc;

TFSyncToken gResourceSyncStartToken = {};
TFSyncToken gResourceSyncToken = {};

VBMeshInstance*  pVBMeshInstances = NULL;
VBPreFilterStats gVBPreFilterStats[gDataBufferCount] = {};

TFPackage*      pPackage = NULL;
TFGeometry*     pSanMiguelModel;
TFGeometryData* pGeoData;
size_t          gMeshCount = 0;
size_t          gTextureCount = 0;
mat4            gSanMiguelModelMat;

size_t gAllTexturesCount = 0;

const char* gTestScripts[] = { "Test_RenderScene.lua", "Test_RenderReflections.lua", "Test_RenderSceneReflections.lua",
                               "Test_RenderSceneExReflections.lua" };
uint32_t    gCurrentScriptIndex = 0;

void RunScript(void* pUserData)
{
    UNREF_PARAM(pUserData);
    TFLuaScriptDesc runDesc = {};
    runDesc.pScriptFileName = gTestScripts[gCurrentScriptIndex];
    luaQueueScriptToRun(&runDesc);
}

class ScreenSpaceReflections: public IApp
{
public:
    ScreenSpaceReflections() //-V832
    {
#ifdef TARGET_IOS
        mSettings.mContentScaleFactor = 1.f;
#endif
    }

    bool Init()
    {
        initAppSettings();

        INIT_STRUCT(gSettings);

        TFExtendedSettings extendedSettings = {};
        extendedSettings.mNumSettings = ESettings::Count;
        extendedSettings.pSettings = (uint32_t*)&gSettings;
        extendedSettings.ppSettingNames = gSettingNames;

        TFRendererDesc settings;
        memset(&settings, 0, sizeof(settings));
        settings.pExtendedSettings = &extendedSettings;
        settings.mShaderTarget = TF_SHADER_TARGET_6_0;
        initGPUConfig(settings.pExtendedSettings);
        initRenderer(GetName(), &settings, &pRenderer);
        // Check for init success
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

// Android: Some devices might have support for all wave ops we use in this UT but the results the WaveReadLaneAt gives us might be
// incorrect.
#if defined(ANDROID)
        gSSSRSupported = false;
#else
        gSSSRSupported = (pRenderer->pGpu->mWaveOpsSupportFlags & TF_WAVE_OPS_SUPPORT_FLAG_BASIC_BIT) &&
                         (pRenderer->pGpu->mWaveOpsSupportFlags & TF_WAVE_OPS_SUPPORT_FLAG_SHUFFLE_BIT) &&
                         (pRenderer->pGpu->mWaveOpsSupportFlags & TF_WAVE_OPS_SUPPORT_FLAG_BALLOT_BIT) &&
                         (pRenderer->pGpu->mWaveOpsSupportFlags & TF_WAVE_OPS_SUPPORT_FLAG_VOTE_BIT);
#endif
        gLastReflectionType = gReflectionType = gSSSRSupported ? SSS_REFLECTION : PP_REFLECTION;

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
        initProfiler(&profiler);

        gPPRGpuProfileToken = initGpuProfiler(pRenderer, pGraphicsQueue, "Graphics");
        gSSSRGpuProfileToken = initGpuProfiler(pRenderer, pGraphicsQueue, "Graphics");
        gCurrentGpuProfileToken = gSSSRGpuProfileToken;

        computePBRMaps();

        // Some texture format are not well covered on android devices (R32G32_SFLOAT, R32G32B32A32_SFLOAT)
        // Albedo texture uses TinyImageFormat_DXBC1_RGBA_UNORM, might need an other sampler
        bool supportLinearFiltering = (pRenderer->pGpu->mFormatCaps[TinyImageFormat_R32G32B32A32_SFLOAT] & TF_FORMAT_CAP_LINEAR_FILTER) &&
                                      (pRenderer->pGpu->mFormatCaps[TinyImageFormat_R32G32_SFLOAT] & TF_FORMAT_CAP_LINEAR_FILTER);

        TFSamplerDesc samplerDesc = {};
        samplerDesc.mMinFilter = supportLinearFiltering ? TF_FILTER_LINEAR : TF_FILTER_NEAREST;
        samplerDesc.mMagFilter = supportLinearFiltering ? TF_FILTER_LINEAR : TF_FILTER_NEAREST;
        samplerDesc.mMipMapMode = supportLinearFiltering ? TF_MIPMAP_MODE_LINEAR : TF_MIPMAP_MODE_NEAREST;
        samplerDesc.mAddressU = TF_ADDRESS_MODE_REPEAT;
        samplerDesc.mAddressV = TF_ADDRESS_MODE_REPEAT;
        samplerDesc.mAddressW = TF_ADDRESS_MODE_REPEAT;
        addSampler(pRenderer, &samplerDesc, &pSamplerBilinear);

        samplerDesc = {};
        samplerDesc.mMinFilter = TF_FILTER_LINEAR;
        samplerDesc.mMagFilter = TF_FILTER_LINEAR;
        samplerDesc.mMipMapMode = TF_MIPMAP_MODE_LINEAR;
        samplerDesc.mAddressU = TF_ADDRESS_MODE_REPEAT;
        samplerDesc.mAddressV = TF_ADDRESS_MODE_REPEAT;
        samplerDesc.mAddressW = TF_ADDRESS_MODE_REPEAT;
        samplerDesc.mMipLodBias = 0.0f;
        samplerDesc.mSetLodRange = false;
        samplerDesc.mMinLod = 0.0f;
        samplerDesc.mMaxLod = 0.0f;
        samplerDesc.mMaxAnisotropy = 8.0f;
        addSampler(pRenderer, &samplerDesc, &pDefaultSampler);

        samplerDesc = {};
        samplerDesc.mMinFilter = TF_FILTER_NEAREST;
        samplerDesc.mMagFilter = TF_FILTER_NEAREST;
        samplerDesc.mMipMapMode = TF_MIPMAP_MODE_NEAREST;
        samplerDesc.mAddressU = TF_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerDesc.mAddressV = TF_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerDesc.mAddressW = TF_ADDRESS_MODE_CLAMP_TO_EDGE;
        addSampler(pRenderer, &samplerDesc, &pSamplerNearestClampToEdge);

        // Generate sky box vertex buffer
        float skyBoxPoints[] = {
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

        TFSyncToken token = {};

        uint64_t         skyBoxDataSize = 4 * 6 * 6 * sizeof(float);
        TFBufferLoadDesc skyboxVbDesc = {};
        skyboxVbDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_VERTEX_BUFFER;
        skyboxVbDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_GPU_ONLY;
        skyboxVbDesc.mDesc.mStartState = TF_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
        skyboxVbDesc.mDesc.mSize = skyBoxDataSize;
        skyboxVbDesc.pData = skyBoxPoints;
        skyboxVbDesc.ppBuffer = &pSkyboxVertexBuffer;
        addResource(&skyboxVbDesc, &token);

        float screenQuadPoints[] = {
            -1.0f, 3.0f, 0.5f, 0.0f, -1.0f, -1.0f, -1.0f, 0.5f, 0.0f, 1.0f, 3.0f, -1.0f, 0.5f, 2.0f, 1.0f,
        };

        uint64_t         screenQuadDataSize = 5 * 3 * sizeof(float);
        TFBufferLoadDesc screenQuadVbDesc = {};
        screenQuadVbDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_VERTEX_BUFFER;
        screenQuadVbDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_GPU_ONLY;
        screenQuadVbDesc.mDesc.mStartState = TF_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
        screenQuadVbDesc.mDesc.mSize = screenQuadDataSize;
        screenQuadVbDesc.pData = screenQuadPoints;
        screenQuadVbDesc.ppBuffer = &pScreenQuadVertexBuffer;
        addResource(&screenQuadVbDesc, &token);

        // Mesh transform constant buffer
        TFBufferLoadDesc ubDesc = {};
        ubDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        ubDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
        ubDesc.mDesc.mSize = sizeof(MeshInfoUniformBlock);
        ubDesc.mDesc.mFlags = TF_BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;

        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            ubDesc.ppBuffer = &pBufferMeshTransforms[i];
            addResource(&ubDesc, NULL);
        }

        // Vis buffer per-frame constant buffer
        {
            TFBufferLoadDesc vbConstantUBDesc = {};
            vbConstantUBDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            vbConstantUBDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
            vbConstantUBDesc.mDesc.mSize = sizeof(PerFrameVBConstantsData);
            vbConstantUBDesc.mDesc.mFlags = TF_BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
            vbConstantUBDesc.mDesc.pName = "gVBConstantsPerFrame Buffer Desc";
            vbConstantUBDesc.pData = NULL;

            for (uint32_t i = 0; i < gDataBufferCount; ++i)
            {
                vbConstantUBDesc.ppBuffer = &pBufferVBConstants[i];
                addResource(&vbConstantUBDesc, NULL);
            }
        }

        // Uniform buffer for camera data
        TFBufferLoadDesc ubCamDesc = {};
        ubCamDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        ubCamDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
        ubCamDesc.mDesc.mSize = sizeof(UniformDataSkybox);
        ubCamDesc.mDesc.mFlags = TF_BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
        ubCamDesc.pData = NULL;

        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            ubCamDesc.ppBuffer = &pBufferUniformCameraSky[i];
            addResource(&ubCamDesc, NULL);
        }

        // Uniform buffer for extended camera data
        TFBufferLoadDesc ubECamDesc = {};
        ubECamDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        ubECamDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
        ubECamDesc.mDesc.mSize = sizeof(UniformExtendedCamData);
        ubECamDesc.mDesc.mFlags = TF_BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
        ubECamDesc.pData = NULL;

        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            ubECamDesc.ppBuffer = &pBufferUniformExtendedCamera[i];
            addResource(&ubECamDesc, NULL);
        }

        // Uniform buffer for PPR's properties
        TFBufferLoadDesc ubPPR_ProDesc = {};
        ubPPR_ProDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        ubPPR_ProDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
        ubPPR_ProDesc.mDesc.mSize = sizeof(UniformPPRProData);
        ubPPR_ProDesc.mDesc.mFlags = TF_BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
        ubPPR_ProDesc.pData = NULL;

        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            ubPPR_ProDesc.ppBuffer = &pBufferUniformPPRPro[i];
            addResource(&ubPPR_ProDesc, NULL);
        }

        uint32_t zero = 0;

        TFBufferLoadDesc SPD_AtomicCounterDesc = {};
        SPD_AtomicCounterDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_RW_BUFFER | TF_DESCRIPTOR_TYPE_BUFFER;
        SPD_AtomicCounterDesc.mDesc.mElementCount = 1;
        SPD_AtomicCounterDesc.mDesc.mStructStride = sizeof(uint32_t);
        SPD_AtomicCounterDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_GPU_ONLY;
        SPD_AtomicCounterDesc.mDesc.mFlags = TF_BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
        SPD_AtomicCounterDesc.mDesc.mSize = SPD_AtomicCounterDesc.mDesc.mStructStride * SPD_AtomicCounterDesc.mDesc.mElementCount;
        SPD_AtomicCounterDesc.mDesc.pName = "SPD_AtomicCounterBuffer";
        SPD_AtomicCounterDesc.mDesc.mStartState = TF_RESOURCE_STATE_UNORDERED_ACCESS;
        SPD_AtomicCounterDesc.pData = &zero;
        SPD_AtomicCounterDesc.ppBuffer = &pSPD_AtomicCounterBuffer;
        addResource(&SPD_AtomicCounterDesc, &token);

        // SSSR
        TFBufferLoadDesc ubSSSR_ConstDesc = {};
        ubSSSR_ConstDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        ubSSSR_ConstDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
        ubSSSR_ConstDesc.mDesc.mSize = sizeof(UniformSSSRConstantsData);
        ubSSSR_ConstDesc.mDesc.mFlags = TF_BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
        ubSSSR_ConstDesc.mDesc.pName = "pSSSR_ConstantsBuffer";
        ubSSSR_ConstDesc.pData = NULL;

        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            ubSSSR_ConstDesc.ppBuffer = &pSSSR_ConstantsBuffer[i];
            addResource(&ubSSSR_ConstDesc, NULL);
        }

        TFBufferLoadDesc SSSR_RayCounterDesc = {};
        SSSR_RayCounterDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_RW_BUFFER | TF_DESCRIPTOR_TYPE_BUFFER;
        SSSR_RayCounterDesc.mDesc.mElementCount = 1;
        SSSR_RayCounterDesc.mDesc.mStructStride = sizeof(uint32_t);
        SSSR_RayCounterDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_GPU_ONLY;
        SSSR_RayCounterDesc.mDesc.mFlags = TF_BUFFER_CREATION_FLAG_NONE;
        SSSR_RayCounterDesc.mDesc.mSize = SSSR_RayCounterDesc.mDesc.mStructStride * SSSR_RayCounterDesc.mDesc.mElementCount;
        SSSR_RayCounterDesc.mDesc.pName = "SSSR_RayCounterBuffer";
        SSSR_RayCounterDesc.mDesc.mStartState = TF_RESOURCE_STATE_UNORDERED_ACCESS;
        SSSR_RayCounterDesc.pData = &zero;
        SSSR_RayCounterDesc.ppBuffer = &pSSSR_RayCounterBuffer;
        addResource(&SSSR_RayCounterDesc, &token);

        TFBufferLoadDesc SSSR_TileCounterDesc = {};
        SSSR_TileCounterDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_RW_BUFFER | TF_DESCRIPTOR_TYPE_BUFFER;
        SSSR_TileCounterDesc.mDesc.mElementCount = 1;
        SSSR_TileCounterDesc.mDesc.mStructStride = sizeof(uint32_t);
        SSSR_TileCounterDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_GPU_ONLY;
        SSSR_TileCounterDesc.mDesc.mFlags = TF_BUFFER_CREATION_FLAG_NONE;
        SSSR_TileCounterDesc.mDesc.mSize = SSSR_TileCounterDesc.mDesc.mStructStride * SSSR_TileCounterDesc.mDesc.mElementCount;
        SSSR_TileCounterDesc.mDesc.pName = "SSSR_TileCounterBuffer";
        SSSR_TileCounterDesc.mDesc.mStartState = TF_RESOURCE_STATE_UNORDERED_ACCESS;
        SSSR_TileCounterDesc.pData = &zero;
        SSSR_TileCounterDesc.ppBuffer = &pSSSR_TileCounterBuffer;
        addResource(&SSSR_TileCounterDesc, &token);

        const uint32_t   zeroArgs[3] = { 0, 0, 0 };
        TFBufferLoadDesc SSSR_IntersectArgsDesc = {};
        SSSR_IntersectArgsDesc.mDesc.mDescriptors =
            TF_DESCRIPTOR_TYPE_RW_BUFFER | TF_DESCRIPTOR_TYPE_BUFFER | TF_DESCRIPTOR_TYPE_INDIRECT_BUFFER;
        SSSR_IntersectArgsDesc.mDesc.mElementCount = 3;
        SSSR_IntersectArgsDesc.mDesc.mStructStride = sizeof(uint32_t);
        SSSR_IntersectArgsDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_GPU_ONLY;
        SSSR_IntersectArgsDesc.mDesc.mFlags = TF_BUFFER_CREATION_FLAG_NONE;
        SSSR_IntersectArgsDesc.mDesc.mStartState = TF_RESOURCE_STATE_INDIRECT_ARGUMENT;
        SSSR_IntersectArgsDesc.mDesc.mSize = SSSR_IntersectArgsDesc.mDesc.mStructStride * SSSR_IntersectArgsDesc.mDesc.mElementCount;
        SSSR_IntersectArgsDesc.mDesc.pName = "SSSR_IntersectArgsBuffer";
        SSSR_IntersectArgsDesc.pData = zeroArgs;
        SSSR_IntersectArgsDesc.ppBuffer = &pSSSR_IntersectArgsBuffer;
        addResource(&SSSR_IntersectArgsDesc, NULL);

        TFBufferLoadDesc SSSR_DenoiserArgsDesc = {};
        SSSR_DenoiserArgsDesc.mDesc.mDescriptors =
            TF_DESCRIPTOR_TYPE_RW_BUFFER | TF_DESCRIPTOR_TYPE_BUFFER | TF_DESCRIPTOR_TYPE_INDIRECT_BUFFER;
        SSSR_DenoiserArgsDesc.mDesc.mElementCount = 3;
        SSSR_DenoiserArgsDesc.mDesc.mStructStride = sizeof(uint32_t);
        SSSR_DenoiserArgsDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_GPU_ONLY;
        SSSR_DenoiserArgsDesc.mDesc.mFlags = TF_BUFFER_CREATION_FLAG_NONE;
        SSSR_DenoiserArgsDesc.mDesc.mStartState = TF_RESOURCE_STATE_INDIRECT_ARGUMENT;
        SSSR_DenoiserArgsDesc.mDesc.mSize = SSSR_DenoiserArgsDesc.mDesc.mStructStride * SSSR_DenoiserArgsDesc.mDesc.mElementCount;
        SSSR_DenoiserArgsDesc.mDesc.pName = "SSSR_DenoiserArgsBuffer";
        SSSR_DenoiserArgsDesc.pData = zeroArgs;
        SSSR_DenoiserArgsDesc.ppBuffer = &pSSSR_DenoiserArgsBuffer;
        addResource(&SSSR_DenoiserArgsDesc, NULL);

        TFBufferLoadDesc sobolDesc = {};
        sobolDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_BUFFER;
        sobolDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_GPU_ONLY;
        sobolDesc.mDesc.mStructStride = sizeof(sobol_256spp_256d[0]);
        sobolDesc.mDesc.mSize = sizeof(sobol_256spp_256d);
        sobolDesc.mDesc.mElementCount = (uint32_t)(sobolDesc.mDesc.mSize / sobolDesc.mDesc.mStructStride);
        sobolDesc.mDesc.pName = "SSSR_SobolBuffer";
        sobolDesc.mDesc.mStartState = TF_RESOURCE_STATE_SHADER_RESOURCE;
        sobolDesc.pData = sobol_256spp_256d;
        sobolDesc.ppBuffer = &pSSSR_SobolBuffer;
        addResource(&sobolDesc, &token);

        TFBufferLoadDesc rankingTileDesc = {};
        rankingTileDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_BUFFER;
        rankingTileDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_GPU_ONLY;
        rankingTileDesc.mDesc.mStructStride = sizeof(rankingTile[0]);
        rankingTileDesc.mDesc.mSize = sizeof(rankingTile);
        rankingTileDesc.mDesc.mElementCount = (uint32_t)(rankingTileDesc.mDesc.mSize / rankingTileDesc.mDesc.mStructStride);
        rankingTileDesc.mDesc.pName = "SSSR_RankingTileBuffer";
        rankingTileDesc.mDesc.mStartState = TF_RESOURCE_STATE_SHADER_RESOURCE;
        rankingTileDesc.pData = rankingTile;
        rankingTileDesc.ppBuffer = &pSSSR_RankingTileBuffer;
        addResource(&rankingTileDesc, &token);

        TFBufferLoadDesc scramblingTileDesc = {};
        scramblingTileDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_BUFFER;
        scramblingTileDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_GPU_ONLY;
        scramblingTileDesc.mDesc.mStructStride = sizeof(scramblingTile[0]);
        scramblingTileDesc.mDesc.mSize = sizeof(scramblingTile);
        scramblingTileDesc.mDesc.mElementCount = (uint32_t)(scramblingTileDesc.mDesc.mSize / scramblingTileDesc.mDesc.mStructStride);
        scramblingTileDesc.mDesc.pName = "SSSR_ScramblingTileBuffer";
        scramblingTileDesc.mDesc.mStartState = TF_RESOURCE_STATE_SHADER_RESOURCE;
        scramblingTileDesc.pData = scramblingTile;
        scramblingTileDesc.ppBuffer = &pSSSR_ScramblingTileBuffer;
        addResource(&scramblingTileDesc, &token);

        // Uniform buffer for light data
        TFBufferLoadDesc ubLightsDesc = {};
        ubLightsDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        ubLightsDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
        ubLightsDesc.mDesc.mSize = sizeof(UniformLightData);
        ubLightsDesc.mDesc.mFlags = TF_BUFFER_CREATION_FLAG_NONE;
        ubLightsDesc.pData = NULL;
        ubLightsDesc.ppBuffer = &pBufferUniformLights;
        addResource(&ubLightsDesc, NULL);

        // Uniform buffer for DirectionalLight data
        TFBufferLoadDesc ubDLightsDesc = {};
        ubDLightsDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        ubDLightsDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
        ubDLightsDesc.mDesc.mSize = sizeof(UniformDirectionalLightData);
        ubDLightsDesc.mDesc.mFlags = TF_BUFFER_CREATION_FLAG_NONE;
        ubDLightsDesc.pData = NULL;
        ubDLightsDesc.ppBuffer = &pBufferUniformDirectionalLights;
        addResource(&ubDLightsDesc, NULL);

        // Uniform buffer for extended camera data
        TFBufferLoadDesc ubPlaneInfoDesc = {};
        ubPlaneInfoDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        ubPlaneInfoDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
        ubPlaneInfoDesc.mDesc.mSize = sizeof(UniformPlaneInfoData);
        ubPlaneInfoDesc.mDesc.mFlags = TF_BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
        ubPlaneInfoDesc.pData = NULL;

        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            ubPlaneInfoDesc.ppBuffer = &pBufferUniformPlaneInfo[i];
            addResource(&ubPlaneInfoDesc, NULL);
        }

        // Add light to scene
        // Point light
        Light light = {};
        light.mCol = vec4(1.0f, 0.5f, 0.1f, 0.0f);
        light.mPos = vec4(15.0f, 40.0f, 4.7f, 0.0f);
        light.mRadius = 30.0f;
        light.mIntensity = 1.0f;
        gUniformDataLights.mLights[0] = light;

        light.mCol = vec4(1.0f, 0.1f, 0.5f, 0.0f);
        light.mPos = vec4(-25.0f, 40.0f, -3.7f, 0.0f);
        light.mRadius = 30.0f;
        light.mIntensity = 1.0f;
        gUniformDataLights.mLights[1] = light;

        light.mCol = vec4(0.5f, 1.0f, 0.1f, 0.0f);
        light.mPos = vec4(40.0f, 40.0f, 4.7f, 0.0f);
        light.mRadius = 30.0f;
        light.mIntensity = 1.0f;
        gUniformDataLights.mLights[2] = light;

        light.mCol = vec4(0.5f, 0.1f, 1.0f, 0.0f);
        light.mPos = vec4(-60.0f, 40.0f, -3.7f, 0.0f);
        light.mRadius = 30.0f;
        light.mIntensity = 1.0f;
        gUniformDataLights.mLights[3] = light;

        gUniformDataLights.mCurrAmountOfLights = 4;
        TFBufferUpdateDesc lightBuffUpdateDesc = { pBufferUniformLights };
        beginUpdateResource(&lightBuffUpdateDesc);
        memcpy(lightBuffUpdateDesc.pMappedData, &gUniformDataLights, sizeof(gUniformDataLights));
        endUpdateResource(&lightBuffUpdateDesc);

        // Directional light
        DirectionalLight dLight;
        dLight.mCol = vec4(1.0f, 1.0f, 1.0f, 1.0f);
        dLight.mDir = vec4(-1.0f, -1.5f, 1.0f, 0.0f);

        gUniformDataDirectionalLights.mLights[0] = dLight;
        gUniformDataDirectionalLights.mCurrAmountOfDLights = 1;
        TFBufferUpdateDesc directionalLightBuffUpdateDesc = { pBufferUniformDirectionalLights };
        beginUpdateResource(&directionalLightBuffUpdateDesc);
        memcpy(directionalLightBuffUpdateDesc.pMappedData, &gUniformDataDirectionalLights, sizeof(gUniformDataDirectionalLights));
        endUpdateResource(&directionalLightBuffUpdateDesc);

        // We need to allocate enough indices for the entire scene
        uint32_t visibilityBufferFilteredIndexCount[NUM_GEOMETRY_SETS] = {};

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
        if (gUsingTextureAtlasFallback)
        {
            packageLoadDesc.packageName = "SanMiguelPak.fallback.buny";
        }
        else
        {
            packageLoadDesc.packageName = "SanMiguelPak.buny";
        }
        packageLoadDesc.loadGeoData = false;
        packageLoadDesc.loadTexData = true;
        packageLoadDesc.pGeometryBuffer = NULL;
        packageLoadDesc.pGeometryBufferLayout = NULL;
        packageLoadDesc.pOnGeometryLoaded = NULL;
        packageLoadDesc.pOnGeometryLoadedUserData = NULL;
        packageLoadDesc.createAtlas = gUsingTextureAtlasFallback;
        packageLoadDesc.ppOutPackage = &pPackage;
        if (!addResourcesFromPackage(&packageLoadDesc, gNullTextureResource))
        {
            LOGF(LogLevel::eERROR, "Failed to load package.");
            return false;
        }

        gSanMiguelModelMat = mat4::scale(vec3(SCENE_SCALE));
        gMeshCount = pPackage->pTextureMetadata->mMeshCount;
        gTextureCount = pPackage->pTextureMetadata->mTextureCount;

        pVBMeshInstances = (VBMeshInstance*)tf_calloc(gMeshCount, sizeof(VBMeshInstance));

        const uint32_t      sceneGeoIndex = 0;
        TFGeometryLoadDesc* pLoadDesc = &pPackage->pGeoData[sceneGeoIndex];
        pLoadDesc->ppGeometry = &pSanMiguelModel;
        pLoadDesc->ppGeometryData = &pGeoData;
        TFSyncToken waitToken = {};
        addResource(pLoadDesc, &waitToken);
        waitForToken(&waitToken);

        MeshConstants* meshConstants = (MeshConstants*)tf_malloc(gMeshCount * sizeof(MeshConstants));
        // Calculate mesh constants and filter containers
        for (uint32_t i = 0; i < gMeshCount; ++i)
        {
            uint16_t matFlag = pPackage->pTextureMetadata->pMaterialProps[i].mFlags;
            uint32_t geomSet = matFlag & (MATERIAL_FLAG_ALPHA_TESTED | MATERIAL_FLAG_TRANSPARENT) ? GEOMSET_ALPHA_CUTOUT : GEOMSET_OPAQUE;
            visibilityBufferFilteredIndexCount[geomSet] += (pSanMiguelModel->pDrawArgs + i)->mIndexCount;
            pVBMeshInstances[i].mGeometrySet = geomSet;
            pVBMeshInstances[i].mMeshIndex = i;
            pVBMeshInstances[i].mTriangleCount = (pSanMiguelModel->pDrawArgs + i)->mIndexCount / 3;
            pVBMeshInstances[i].mInstanceIndex = INSTANCE_INDEX_NONE;

            meshConstants[i].indexOffset = pSanMiguelModel->pDrawArgs[i].mStartIndex;
            meshConstants[i].vertexOffset = pSanMiguelModel->pDrawArgs[i].mVertexOffset;
            meshConstants[i].materialID_flags = ((matFlag & FLAG_MASK) << FLAG_LOW_BIT) | ((i & MATERIAL_ID_MASK) << MATERIAL_ID_LOW_BIT);
            meshConstants[i].specEmissiveStrength = pPackage->pTextureMetadata->pMaterialProps[i].mSpecEmissiveStrength;
            meshConstants[i].baseColor = pPackage->pTextureMetadata->pMaterialProps[i].mBaseColor;
        }

        TFBufferLoadDesc meshConstantDesc = {};
        meshConstantDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_BUFFER;
        meshConstantDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_GPU_ONLY;
        meshConstantDesc.mDesc.mElementCount = (uint32_t)gMeshCount;
        meshConstantDesc.mDesc.mStructStride = sizeof(MeshConstants);
        meshConstantDesc.mDesc.mSize = meshConstantDesc.mDesc.mElementCount * meshConstantDesc.mDesc.mStructStride;
        meshConstantDesc.mDesc.mStartState = TF_RESOURCE_STATE_SHADER_RESOURCE;
        meshConstantDesc.ppBuffer = &pBufferMeshConstants;
        meshConstantDesc.pData = meshConstants;
        meshConstantDesc.mDesc.pName = "Mesh Constant desc";
        addResource(&meshConstantDesc, &token);

        VisibilityBufferDesc vbDesc = {};
        vbDesc.mNumFrames = gDataBufferCount;
        vbDesc.mNumBuffers = 1; // We don't use Async Compute for triangle filtering, 1 buffer is enough
        vbDesc.mNumGeometrySets = NUM_GEOMETRY_SETS;
        vbDesc.pMaxIndexCountPerGeomSet = visibilityBufferFilteredIndexCount;
        vbDesc.mNumViews = NUM_CULLING_VIEWPORTS;
        vbDesc.mComputeThreads = VB_COMPUTE_THREADS;
        initVisibilityBuffer(pRenderer, &vbDesc, &pVisibilityBuffer);

        UpdateVBMeshFilterGroupsDesc updateVBMeshFilterGroupsDesc = {};
        updateVBMeshFilterGroupsDesc.mNumMeshInstance = (uint32_t)gMeshCount;
        updateVBMeshFilterGroupsDesc.pVBMeshInstances = pVBMeshInstances;
        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            updateVBMeshFilterGroupsDesc.mFrameIndex = i;
            gVBPreFilterStats[i] = updateVBMeshFilterGroups(pVisibilityBuffer, &updateVBMeshFilterGroupsDesc);
        }

        for (uint32_t frameIdx = 0; frameIdx < gDataBufferCount; ++frameIdx)
        {
            gPerFrameData[frameIdx].mDrawCount[GEOMSET_OPAQUE] = gVBPreFilterStats[frameIdx].mGeomsetMaxDrawCounts[GEOMSET_OPAQUE];
            gPerFrameData[frameIdx].mDrawCount[GEOMSET_ALPHA_CUTOUT] =
                gVBPreFilterStats[frameIdx].mGeomsetMaxDrawCounts[GEOMSET_ALPHA_CUTOUT];
        }

        /************************************************************************/
        ////////////////////////////////////////////////

        TFCameraMotionParameters camParameters{ 200.0f, 150.0f, 300.0f };
        vec3                     camPos{ 95.5f, 47.2f, 70.75f };
        vec3                     lookAt{ -1.67f, 9.58f, 23.75f };

        pCamera = initFpsCamera(camPos, lookAt);
        pCamera->setMotionParameters(camParameters);

        AddCustomInputBindings();

        waitForAllResourceLoads();

        gAllTexturesCount = gMeshCount * TEXTURES_PER_MESH;

        tf_free(meshConstants);
        initScreenshotCapturer(pRenderer, pGraphicsQueue, GetName());
        return true;
    }

    void Exit()
    {
        exitScreenshotCapturer();
        exitCamera(pCamera);
        tf_free(gInitializeVal);
        gInitializeVal = NULL;

        gFrameCount = 0;

        exitGpuProfiler(gSSSRGpuProfileToken);
        exitGpuProfiler(gPPRGpuProfileToken);

        exitProfiler();
        removeResource(gNullTextureResource);
        removeResource(pSanMiguelModel);
        removeResource(pGeoData);
        removeResourcesFromPackage(pPackage);

        removeResource(pSpecularMap);
        removeResource(pIrradianceMap);
        removeResource(pSkybox);
        removeResource(pBRDFIntegrationMap);

        removeResource(pSPD_AtomicCounterBuffer);
        removeResource(pSSSR_RayCounterBuffer);
        removeResource(pSSSR_TileCounterBuffer);
        removeResource(pSSSR_IntersectArgsBuffer);
        removeResource(pSSSR_DenoiserArgsBuffer);
        removeResource(pSSSR_SobolBuffer);
        removeResource(pSSSR_RankingTileBuffer);
        removeResource(pSSSR_ScramblingTileBuffer);

        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            removeResource(pBufferMeshTransforms[i]);
            removeResource(pBufferVBConstants[i]);
            removeResource(pBufferUniformPlaneInfo[i]);
            removeResource(pBufferUniformPPRPro[i]);
            removeResource(pBufferUniformExtendedCamera[i]);
            removeResource(pBufferUniformCameraSky[i]);
            removeResource(pSSSR_ConstantsBuffer[i]);
        }

        tf_free(pVBMeshInstances);

        removeResource(pBufferMeshConstants);

        removeResource(pBufferUniformLights);
        removeResource(pBufferUniformDirectionalLights);
        removeResource(pSkyboxVertexBuffer);
        removeResource(pScreenQuadVertexBuffer);

        exitUserInterface();

        removeFont(gFont);
        exitFontSystem();

        removeSampler(pRenderer, pSamplerBilinear);
        removeSampler(pRenderer, pDefaultSampler);
        removeSampler(pRenderer, pSamplerNearestClampToEdge);

        exitVisibilityBuffer(pVisibilityBuffer);

        // Remove commands and command pool
        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            exitSemaphore(pRenderer, pImageAcquiredSemaphore[i]);
        }
        exitGpuCmdRing(pRenderer, &gGraphicsCmdRing);
        exitQueue(pRenderer, pGraphicsQueue);
        exitRootSignature(pRenderer);
        // Remove resource loader and renderer
        exitResourceLoaderInterface(pRenderer);
        exitRenderer(pRenderer);
        exitGPUConfig();

        pRenderer = NULL;
    }

    bool Load(TFReloadDesc* pReloadDesc)
    {
        UNREF_PARAM(pReloadDesc);

        gSceneRes = getGPUCfgSceneResolution(mSettings.mWidth, mSettings.mHeight);

        gResourceSyncStartToken = getLastTokenCompleted();

        addShaders();

        loadProfilerUI(mSettings.mWidth, mSettings.mHeight);

        luaRegisterUI();

        gGuiDesc = {};
        gGuiDesc.mStartPos = vec2(mSettings.mWidth * 0.01f, mSettings.mHeight * 0.2f);
        gGuiDesc.mStartSize = vec2(450, 600);
        gGuiDesc.pWindowTitle = "Screen Space Reflections";
        gGuiDesc.mFlags = TF_UI_WINDOW_TITLE | TF_UI_WINDOW_SCALABLE | TF_UI_WINDOW_MOVABLE | TF_UI_WINDOW_MINIMIZABLE |
                          TF_UI_WINDOW_BORDER | TF_UI_WINDOW_INIT_HEIGHT_FIT;

        if (!addSwapChain())
            return false;

        addRenderTargets();

        addDescriptorSets();

        addPipelines();

        waitForAllResourceLoads();

        // Create per-mip UAV descriptors for pSSSR_DepthHierarchy (used by the depth downsample / generate-mips
        // pass, which runs regardless of gSSSRSupported since PPR reflections also depend on it)
        for (uint32_t i = 0; i < pSSSR_DepthHierarchy->mMipLevels; ++i)
        {
            TFTextureDescriptorDesc mipDesc = {};
            mipDesc.pTexture = pSSSR_DepthHierarchy;
            mipDesc.mFormat = TinyImageFormat_R32_SFLOAT;
            mipDesc.mDescriptors = TF_DESCRIPTOR_TYPE_RW_TEXTURE;
            mipDesc.mBaseMipLevel = i;
            mipDesc.mMipLevelCount = 1;
            addTextureDescriptor(pRenderer, &mipDesc, &pSSSR_DepthHierarchyMipDescs[i]);
        }

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

        gFrameCount = 0;

        return true;
    }

    void Unload(TFReloadDesc* pReloadDesc)
    {
        UNREF_PARAM(pReloadDesc);

        waitQueueIdle(pGraphicsQueue);

        unloadFontSystem();
        unloadUserInterface();

        removePipelines();

        waitForToken(&gResourceSyncToken);
        waitForAllResourceLoads();

        gResourceSyncToken = 0;

        removeDescriptorSets();

        for (uint32_t i = 0; i < pSSSR_DepthHierarchy->mMipLevels; ++i)
        {
            removeTextureDescriptor(pRenderer, pSSSR_DepthHierarchyMipDescs[i]);
            pSSSR_DepthHierarchyMipDescs[i] = NULL;
        }

        removeRenderTargets();
        removeSwapChain(pRenderer, pSwapChain);

        // Release the ESRAM layouts opened in addRenderTargets(), otherwise every Load() leaks two layouts
        // (UT10 ships Test_DeviceReset.lua, so Load/Unload cycles do happen).
        TF_ESRAM_RESET_ALLOCS(pRenderer);

        unloadProfilerUI();

        removeShaders();
    }

    void Update(float deltaTime)
    {
        updateUI();

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

        // Update camera
        TFCameraMatrix viewMat = pCamera->getViewMatrix();
        const float    aspectInverse = (float)gSceneRes.mHeight / (float)gSceneRes.mWidth;
        const float    horizontalFov = PI / 2.0f;
        const float    nearPlane = 0.1f;
        const float    farPlane = 1000.f;
        TFCameraMatrix projMat = camMatPerspectiveReverseZ(horizontalFov, aspectInverse, nearPlane, farPlane);
        TFCameraMatrix ViewProjMat = camMatMul(&projMat, &viewMat);
        TFCameraMatrix mvp = camMatMulMat4(&ViewProjMat, &gSanMiguelModelMat);

        gMeshInfoUniformData[mSettings.mFrameIdx].mPrevWorldViewProjMat = gMeshInfoUniformData[mSettings.mFrameIdx].mWorldViewProjMat;
        gMeshInfoUniformData[mSettings.mFrameIdx].mWorldViewProjMat = mvp.mMatrices[MONO_CAMERA_VIEW_INDEX];

        gVBConstants[mSettings.mFrameIdx].transform[VIEW_CAMERA].mvp = mvp;
        gVBConstants[mSettings.mFrameIdx].cullingViewports[VIEW_CAMERA].windowSize = { (float)gSceneRes.mWidth, (float)gSceneRes.mHeight };
        gVBConstants[mSettings.mFrameIdx].cullingViewports[VIEW_CAMERA].sampleCount = 1;

        // Data uniforms
        gUniformDataExtenedCamera.mCameraWorldPos = vec4(pCamera->getViewPosition(), 1.0);
        gUniformDataExtenedCamera.mViewMat = viewMat.mMatrices[MONO_CAMERA_VIEW_INDEX];
        gUniformDataExtenedCamera.mInvViewMat = inverse(gUniformDataExtenedCamera.mViewMat);
        gUniformDataExtenedCamera.mProjMat = projMat.mMatrices[MONO_CAMERA_VIEW_INDEX];
        gUniformDataExtenedCamera.mViewProjMat = ViewProjMat.mMatrices[MONO_CAMERA_VIEW_INDEX];
        gUniformDataExtenedCamera.mInvViewProjMat = inverse(ViewProjMat.mMatrices[MONO_CAMERA_VIEW_INDEX]);
        gUniformDataExtenedCamera.mViewPortSize = { (float)gSceneRes.mWidth, (float)gSceneRes.mHeight };
        gUniformDataExtenedCamera.mNear = nearPlane;
        gUniformDataExtenedCamera.mFar = farPlane;
        gUniformDataExtenedCamera.mUseEnvMap = gUseEnvMap ? 1 : 0;
        gUniformDataExtenedCamera.mOverrideRoughnessMetallic = gOverrideRoughnessMetallic ? 1 : 0;

        // Projection uniforms
        gUniformPPRProData.renderMode = gRenderMode;
        gUniformPPRProData.useHolePatching =
            ((gReflectionType == PP_REFLECTION || !gSSSRSupported) && gUseHolePatching == true) ? 1.0f : 0.0f;
        gUniformPPRProData.useExpensiveHolePatching = gUseExpensiveHolePatching == true ? 1.0f : 0.0f;
        gUniformPPRProData.useNormalMap = gUseNormalMap == true ? 1.0f : 0.0f;
        gUniformPPRProData.useFadeEffect = gUseFadeEffect == true ? 1.0f : 0.0f;
        gUniformPPRProData.debugNonProjected = gDebugNonProjectedPixels == true ? 1.0f : 0.0f;
        gUniformPPRProData.intensity = (gReflectionType == PP_REFLECTION || !gSSSRSupported) ? gRRP_Intensity : 1.0f;

        // Planes
        gUniformDataPlaneInfo.numPlanes = gPlaneNumber;
        gUniformDataPlaneInfo.planeInfo[0].centerPoint = vec4(0.0, 24.0f, 0.0f, 0.0);
        gUniformDataPlaneInfo.planeInfo[0].size = vec4(gPlaneSize);

        gUniformDataPlaneInfo.planeInfo[1].centerPoint = vec4(10.0, 40.0f, 20.0f, 0.0);
        gUniformDataPlaneInfo.planeInfo[1].size = vec4(90.0f, 20.0f, 0.0f, 0.0f);

        gUniformDataPlaneInfo.planeInfo[2].centerPoint = vec4(10.0, 40.0f, 70.f, 0.0);
        gUniformDataPlaneInfo.planeInfo[2].size = vec4(90.0f, 20.0f, 0.0f, 0.0f);

        gUniformDataPlaneInfo.planeInfo[3].centerPoint = vec4(10.0, 50.0f, 0.9f, 0.0);
        gUniformDataPlaneInfo.planeInfo[3].size = vec4(100.f);

        static const float oneDegreeInRad = 0.01745329251994329576923690768489f;

        mat4 basicMat;
        basicMat[0] = vec4(1.0, 0.0, 0.0, 0.0);  // tan
        basicMat[1] = vec4(0.0, 0.0, -1.0, 0.0); // bitan
        basicMat[2] = vec4(0.0, 1.0, 0.0, 0.0);  // normal
        basicMat[3] = vec4(0.0, 0.0, 0.0, 1.0);

        gUniformDataPlaneInfo.planeInfo[0].rotMat = basicMat;
        gUniformDataPlaneInfo.planeInfo[1].rotMat = basicMat.rotationX(oneDegreeInRad * (-80.0f + gPlaneRotationOffset));
        gUniformDataPlaneInfo.planeInfo[2].rotMat = basicMat.rotationX(oneDegreeInRad * (-100.0f + gPlaneRotationOffset));
        gUniformDataPlaneInfo.planeInfo[3].rotMat = basicMat.rotationX(oneDegreeInRad * (90.0f + gPlaneRotationOffset));

        gUniformSSSRConstantsData.g_prev_view_proj =
            transpose(transpose(gUniformSSSRConstantsData.g_proj) * transpose(gUniformSSSRConstantsData.g_view));
        gUniformSSSRConstantsData.g_inv_view_proj = transpose(gUniformDataExtenedCamera.mInvViewProjMat);
        gUniformSSSRConstantsData.g_proj = transpose(projMat.mMatrices[MONO_CAMERA_VIEW_INDEX]);
        gUniformSSSRConstantsData.g_inv_proj = transpose(inverse(projMat.mMatrices[MONO_CAMERA_VIEW_INDEX]));
        gUniformSSSRConstantsData.g_view = transpose(gUniformDataExtenedCamera.mViewMat);
        gUniformSSSRConstantsData.g_inv_view = transpose(inverse(gUniformDataExtenedCamera.mViewMat));

        gUniformSSSRConstantsData.g_frame_index = gFrameCount;
        gUniformSSSRConstantsData.g_clear_targets = gFrameCount < gDataBufferCount; // clear both temporal denoise buffers
        gUniformSSSRConstantsData.g_max_traversal_intersections = gSSSR_MaxTravelsalIntersections;
        gUniformSSSRConstantsData.g_min_traversal_occupancy = gSSSR_MinTravelsalOccupancy;
        gUniformSSSRConstantsData.g_most_detailed_mip = gSSSR_MostDetailedMip;
        gUniformSSSRConstantsData.g_temporal_stability_factor = pSSSR_TemporalStability;
        gUniformSSSRConstantsData.g_depth_buffer_thickness = gSSSR_DepthThickness;
        gUniformSSSRConstantsData.g_samples_per_quad = gSSSR_SamplesPerQuad;
        gUniformSSSRConstantsData.g_temporal_variance_guided_tracing_enabled = gSSSR_TemporalVarianceEnabled;
        gUniformSSSRConstantsData.g_roughness_threshold = gSSSR_RougnessThreshold;
        gUniformSSSRConstantsData.g_skip_denoiser = gSSSR_SkipDenoiser;

        viewMat = camMatSetTranslation(&viewMat, vec3(0));
        gUniformDataSky.mProjectView = projMat.mMatrices[MONO_CAMERA_VIEW_INDEX] * viewMat.mMatrices[MONO_CAMERA_VIEW_INDEX];

        if (gReflectionType != gLastReflectionType)
        {
            gLastReflectionType = gReflectionType;
        }
    }

    void Draw()
    {
        if ((bool)pSwapChain->mEnableVsync != mSettings.mVSyncEnabled)
        {
            waitQueueIdle(pGraphicsQueue);
            ::toggleVSync(pRenderer, &pSwapChain);
        }

        // This will acquire the next swapchain image
        uint32_t swapchainImageIndex;
        acquireNextImage(pRenderer, pSwapChain, pImageAcquiredSemaphore[mSettings.mFrameIdx], NULL, &swapchainImageIndex);

        GpuCmdRingElement elem = getNextGpuCmdRingElement(&gGraphicsCmdRing, true, 1);

        // Stall if CPU is running "gDataBufferCount" frames ahead of GPU
        TFFenceStatus fenceStatus;
        getFenceStatus(pRenderer, elem.pFence, &fenceStatus);
        if (fenceStatus == TF_FENCE_STATUS_INCOMPLETE)
            waitForFences(pRenderer, 1, &elem.pFence);

        gPerFrameData[mSettings.mFrameIdx].mEyeObjectSpace[VIEW_CAMERA] =
            (gUniformDataExtenedCamera.mInvViewMat * vec4(0.f, 0.f, 0.f, 1.f)).getXYZ();
        gPerFrameData[mSettings.mFrameIdx].mEyeObjectSpace[VIEW_SHADOW] = gPerFrameData[mSettings.mFrameIdx].mEyeObjectSpace[VIEW_CAMERA];

        gVBConstants[mSettings.mFrameIdx].transform[VIEW_SHADOW].mvp = gVBConstants[mSettings.mFrameIdx].transform[VIEW_CAMERA].mvp;
        gVBConstants[mSettings.mFrameIdx].cullingViewports[VIEW_SHADOW] = gVBConstants[mSettings.mFrameIdx].cullingViewports[VIEW_CAMERA];

        TFBufferUpdateDesc meshUniformBufferUpdateDesc = { pBufferMeshTransforms[mSettings.mFrameIdx] };
        beginUpdateResource(&meshUniformBufferUpdateDesc);
        memcpy(meshUniformBufferUpdateDesc.pMappedData, &gMeshInfoUniformData[mSettings.mFrameIdx], sizeof(MeshInfoUniformBlock));
        endUpdateResource(&meshUniformBufferUpdateDesc);

        TFBufferUpdateDesc skyboxViewProjCbv = { pBufferUniformCameraSky[mSettings.mFrameIdx] };
        beginUpdateResource(&skyboxViewProjCbv);
        memcpy(skyboxViewProjCbv.pMappedData, &gUniformDataSky, sizeof(gUniformDataSky));
        endUpdateResource(&skyboxViewProjCbv);

        TFBufferUpdateDesc updateVisibilityBufferConstantDesc = { pBufferVBConstants[mSettings.mFrameIdx] };
        beginUpdateResource(&updateVisibilityBufferConstantDesc);
        memcpy(updateVisibilityBufferConstantDesc.pMappedData, &gVBConstants[mSettings.mFrameIdx], sizeof(PerFrameVBConstantsData));
        endUpdateResource(&updateVisibilityBufferConstantDesc);

        TFBufferUpdateDesc CbvExtendedCamera = { pBufferUniformExtendedCamera[mSettings.mFrameIdx] };
        beginUpdateResource(&CbvExtendedCamera);
        memcpy(CbvExtendedCamera.pMappedData, &gUniformDataExtenedCamera, sizeof(gUniformDataExtenedCamera));
        endUpdateResource(&CbvExtendedCamera);

        TFBufferUpdateDesc CbPPR_Prop = { pBufferUniformPPRPro[mSettings.mFrameIdx] };
        beginUpdateResource(&CbPPR_Prop);
        memcpy(CbPPR_Prop.pMappedData, &gUniformPPRProData, sizeof(gUniformPPRProData));
        endUpdateResource(&CbPPR_Prop);

        TFBufferUpdateDesc planeInfoBuffUpdateDesc = { pBufferUniformPlaneInfo[mSettings.mFrameIdx] };
        beginUpdateResource(&planeInfoBuffUpdateDesc);
        memcpy(planeInfoBuffUpdateDesc.pMappedData, &gUniformDataPlaneInfo, sizeof(gUniformDataPlaneInfo));
        endUpdateResource(&planeInfoBuffUpdateDesc);

        TFBufferUpdateDesc SSSR_ConstantsBuffUpdateDesc = { pSSSR_ConstantsBuffer[mSettings.mFrameIdx] };
        beginUpdateResource(&SSSR_ConstantsBuffUpdateDesc);
        memcpy(SSSR_ConstantsBuffUpdateDesc.pMappedData, &gUniformSSSRConstantsData, sizeof(gUniformSSSRConstantsData));
        endUpdateResource(&SSSR_ConstantsBuffUpdateDesc);

        resetCmdPool(pRenderer, elem.pCmdPool);

        TFCmd* cmd = elem.pCmds[0];
        beginCmd(cmd);
        cmdBindDescriptorSet(cmd, 0, pDescriptorSetPersistent);
        cmdBindDescriptorSet(cmd, mSettings.mFrameIdx, pDescriptorSetPerFrame);
        gCurrentGpuProfileToken = gReflectionType == PP_REFLECTION ? gPPRGpuProfileToken : gSSSRGpuProfileToken;

        cmdBeginGpuFrameProfile(cmd, gCurrentGpuProfileToken);

        // Triangle Filtering pass
        {
            TriangleFilteringPassDesc triangleFilteringDesc = {};
            triangleFilteringDesc.pPipelineClearBuffers = pPipelineClearBuffers;
            triangleFilteringDesc.pPipelineTriangleFiltering = pPipelineTriangleFiltering;
            triangleFilteringDesc.pDescriptorSetTriangleFilteringPerBatch = pDescriptorSetTriangleFilteringPerBatch;

            triangleFilteringDesc.mFrameIndex = mSettings.mFrameIdx;
            triangleFilteringDesc.mBuffersIndex = 0; // We don't use Async Compute for triangle filtering, we just have 1 buffer
            triangleFilteringDesc.mGpuProfileToken = gCurrentGpuProfileToken;
            triangleFilteringDesc.mVBPreFilterStats = gVBPreFilterStats[mSettings.mFrameIdx];
            cmdVBTriangleFilteringPass(pVisibilityBuffer, cmd, &triangleFilteringDesc);
        }
        // Transition triangle filtering outputs for rendering
        {
            const uint32_t  numBarriers = NUM_CULLING_VIEWPORTS + 2;
            TFBufferBarrier triangleFilteringOutputBarriers[numBarriers] = {};
            uint32_t        barrierCount = 0;
            triangleFilteringOutputBarriers[barrierCount++] = {
                pVisibilityBuffer->ppIndirectDrawArgBuffer[0],
                TF_RESOURCE_STATE_UNORDERED_ACCESS,
                TF_RESOURCE_STATE_INDIRECT_ARGUMENT | TF_RESOURCE_STATE_SHADER_RESOURCE,
            };
            triangleFilteringOutputBarriers[barrierCount++] = {
                pVisibilityBuffer->ppIndirectDataBuffer[mSettings.mFrameIdx],
                TF_RESOURCE_STATE_UNORDERED_ACCESS,
                TF_RESOURCE_STATE_SHADER_RESOURCE,
            };
            for (uint32_t i = 0; i < NUM_CULLING_VIEWPORTS; ++i)
            {
                triangleFilteringOutputBarriers[barrierCount++] = {
                    pVisibilityBuffer->ppFilteredIndexBuffer[i],
                    TF_RESOURCE_STATE_UNORDERED_ACCESS,
                    TF_RESOURCE_STATE_INDEX_BUFFER | TF_RESOURCE_STATE_SHADER_RESOURCE,
                };
            }
            cmdResourceBarrier(cmd, barrierCount, triangleFilteringOutputBarriers, 0, NULL, 0, NULL);
        }

        TFRenderTarget* pNormalRoughnessBuffer = pNormalRoughnessBuffers[mSettings.mFrameIdx];

        // Visibility Buffer pass
        {
            cmdBeginGpuTimestampQuery(cmd, gCurrentGpuProfileToken, "VB Pass");

            // Resource Transition
            {
                TFRenderTargetBarrier barriers[] = {
                    { pRenderTargetVBPass, TF_RESOURCE_STATE_SHADER_RESOURCE, TF_RESOURCE_STATE_RENDER_TARGET },
                    { pDepthBuffer, TF_RESOURCE_STATE_SHADER_RESOURCE, TF_RESOURCE_STATE_DEPTH_WRITE },
                };
                cmdResourceBarrier(cmd, 0, NULL, 0, NULL, TF_ARRAY_COUNT(barriers), barriers);
            }
            // Bind Render Targets
            {
                TFBindRenderTargetsDesc bindRenderTargets = {};
                bindRenderTargets.mRenderTargetCount = 1;
                bindRenderTargets.mRenderTargets[0] = { pRenderTargetVBPass, TF_LOAD_ACTION_CLEAR };
                bindRenderTargets.mDepthStencil = { pDepthBuffer, TF_LOAD_ACTION_CLEAR };
                cmdBindRenderTargets(cmd, &bindRenderTargets);
                cmdSetViewport(cmd, 0.0f, 0.0f, (float)pRenderTargetVBPass->mWidth, (float)pRenderTargetVBPass->mHeight, 0.0f, 1.0f);
                cmdSetScissor(cmd, 0, 0, pRenderTargetVBPass->mWidth, pRenderTargetVBPass->mHeight);
            }
            // Draw
            {
                TFBuffer* pIndexBuffer = pVisibilityBuffer->ppFilteredIndexBuffer[VIEW_CAMERA];
                cmdBindIndexBuffer(cmd, pIndexBuffer, TF_INDEX_TYPE_UINT32, 0);

                const char* profileNames[gNumGeomSets] = { "VB Pass Opaque", "VB Pass Alpha" };
                for (uint32_t i = 0; i < gNumGeomSets; ++i)
                {
                    cmdBeginGpuTimestampQuery(cmd, gCurrentGpuProfileToken, profileNames[i]);
                    cmdBindPipeline(cmd, pPipelineVBBufferPass[i]);

                    uint64_t  indirectBufferByteOffset = GET_INDIRECT_DRAW_ELEM_INDEX(VIEW_CAMERA, i, 0) * sizeof(uint32_t);
                    TFBuffer* pIndirectDrawBuffer = pVisibilityBuffer->ppIndirectDrawArgBuffer[0];

                    if (gUsingPrimitiveIDFallback)
                    {
                        cmdExecuteIndirect(cmd, TF_INDIRECT_DRAW, 1, pIndirectDrawBuffer, indirectBufferByteOffset, NULL, 0);
                    }
                    else
                    {
                        cmdExecuteIndirect(cmd, TF_INDIRECT_DRAW_INDEX, 1, pIndirectDrawBuffer, indirectBufferByteOffset, NULL, 0);
                    }

                    cmdEndGpuTimestampQuery(cmd, gCurrentGpuProfileToken);
                }
            }

            cmdBindRenderTargets(cmd, NULL);
            cmdEndGpuTimestampQuery(cmd, gCurrentGpuProfileToken);
        }

        // Visibility Buffer Shade pass
        {
            // Render a fullscreen triangle to evaluate shading for every pixel. This render step uses the render target generated by
            // drawVisibilityBufferPass
            //  to get the draw / triangle IDs to reconstruct and interpolate vertex attributes per pixel. This method doesn't set any
            //  vertex/index buffer because the triangle positions are calculated internally using vertex_id.
            cmdBeginGpuTimestampQuery(cmd, gCurrentGpuProfileToken, "VB Shade Pass");

            // Resource Transition
            {
                TFRenderTargetBarrier rtBarriers[] = {
                    { pRenderTargetVBPass, TF_RESOURCE_STATE_RENDER_TARGET, TF_RESOURCE_STATE_SHADER_RESOURCE },
                    { pSceneBuffer, TF_RESOURCE_STATE_SHADER_RESOURCE, TF_RESOURCE_STATE_RENDER_TARGET },
                    { pNormalRoughnessBuffer, TF_RESOURCE_STATE_SHADER_RESOURCE, TF_RESOURCE_STATE_RENDER_TARGET }
                };
                cmdResourceBarrier(cmd, 0, NULL, 0, NULL, TF_ARRAY_COUNT(rtBarriers), rtBarriers);
            }
            // Bind Render Targets
            {
                TFBindRenderTargetsDesc bindRenderTargets = {};
                bindRenderTargets.mRenderTargetCount = 2;
                bindRenderTargets.mRenderTargets[0] = { pSceneBuffer, TF_LOAD_ACTION_CLEAR };
                bindRenderTargets.mRenderTargets[1] = { pNormalRoughnessBuffer, TF_LOAD_ACTION_CLEAR };
                cmdBindRenderTargets(cmd, &bindRenderTargets);
                cmdSetViewport(cmd, 0.0f, 0.0f, (float)pSceneBuffer->mWidth, (float)pSceneBuffer->mHeight, 0.0f, 1.0f);
                cmdSetScissor(cmd, 0, 0, pSceneBuffer->mWidth, pSceneBuffer->mHeight);
            }
            // Draw
            {
                cmdBindPipeline(cmd, pPipelineVBShadeSrgb);

                // A single triangle is rendered without specifying a vertex buffer (triangle positions are calculated internally using
                // vertex_id)
                cmdDraw(cmd, 3, 0);
            }

            cmdEndGpuTimestampQuery(cmd, gCurrentGpuProfileToken);
        }

        // Skybox pass
        {
            cmdBeginGpuTimestampQuery(cmd, gCurrentGpuProfileToken, "Draw Skybox");

            // Bind Render Targets
            {
                TFBindRenderTargetsDesc bindRenderTargets = {};
                bindRenderTargets.mRenderTargetCount = 1;
                bindRenderTargets.mRenderTargets[0] = { pSceneBuffer, TF_LOAD_ACTION_LOAD };
                bindRenderTargets.mDepthStencil = { pDepthBuffer, TF_LOAD_ACTION_LOAD };
                cmdBindRenderTargets(cmd, &bindRenderTargets);
                // Android Mali G-77 NaN precision workaround
                cmdSetViewport(cmd, 0.0f, 0.0f, (float)pSceneBuffer->mWidth, (float)pSceneBuffer->mHeight, 1.0f, 1.0f);
                cmdSetScissor(cmd, 0, 0, pSceneBuffer->mWidth, pSceneBuffer->mHeight);
            }
            // Draw
            {
                const uint32_t skyboxStride = sizeof(float) * 4;
                cmdBindPipeline(cmd, pSkyboxPipeline);
                cmdBindVertexBuffer(cmd, 1, &pSkyboxVertexBuffer, &skyboxStride, NULL);
                cmdDraw(cmd, 36, 0);
            }

            cmdBindRenderTargets(cmd, NULL);
            cmdEndGpuTimestampQuery(cmd, gCurrentGpuProfileToken);
        }

        TFRenderTargetBarrier rtBarriers[4] = {};
        // Transition depth buffer for reflection passes
        {
            rtBarriers[0] = { pDepthBuffer, TF_RESOURCE_STATE_DEPTH_WRITE, TF_RESOURCE_STATE_SHADER_RESOURCE };
            cmdResourceBarrier(cmd, 0, NULL, 0, NULL, 1, rtBarriers);
        }

        const uint32_t quadStride = sizeof(float) * 5;

        // Pixel-Projected Reflections pass
        if (gReflectionType == PP_REFLECTION || !gSSSRSupported)
        {
            cmdBeginGpuTimestampQuery(cmd, gCurrentGpuProfileToken, "Pixel-Projected Reflections");

            // Projection pass
            {
                cmdBeginGpuTimestampQuery(cmd, gCurrentGpuProfileToken, "Projection Pass");

                // Order the previous frame's reflection-pass resets before projection writes this frame.
                TFBufferBarrier intermediateBufferBarrier = { pIntermediateBuffer, TF_RESOURCE_STATE_UNORDERED_ACCESS,
                                                              TF_RESOURCE_STATE_UNORDERED_ACCESS };
                cmdResourceBarrier(cmd, 1, &intermediateBufferBarrier, 0, NULL, 0, NULL);

                cmdBindPipeline(cmd, pPPR_ProjectionPipeline);
                cmdBindDescriptorSet(cmd, 0, pDescriptorSetPPR);
                const uint32_t* pThreadGroupSize = pPPR_ProjectionShader->mNumThreadsPerGroup;
                cmdDispatch(cmd, (gSceneRes.mWidth * gSceneRes.mHeight / pThreadGroupSize[0]) + 1, 1, 1);

                cmdEndGpuTimestampQuery(cmd, gCurrentGpuProfileToken);
            }

            // Reflection pass
            {
                cmdBeginGpuTimestampQuery(cmd, gCurrentGpuProfileToken, "Reflection Pass");

                // Resource Transition
                {
                    rtBarriers[0] = { pSceneBuffer, TF_RESOURCE_STATE_RENDER_TARGET, TF_RESOURCE_STATE_SHADER_RESOURCE };
                    rtBarriers[1] = { pNormalRoughnessBuffer, TF_RESOURCE_STATE_RENDER_TARGET, TF_RESOURCE_STATE_SHADER_RESOURCE };
                    rtBarriers[2] = { pReflectionBuffer, TF_RESOURCE_STATE_SHADER_RESOURCE, TF_RESOURCE_STATE_RENDER_TARGET };
                    TFBufferBarrier intermediateBufferBarrier = { pIntermediateBuffer, TF_RESOURCE_STATE_UNORDERED_ACCESS,
                                                                  TF_RESOURCE_STATE_UNORDERED_ACCESS };
                    cmdResourceBarrier(cmd, 1, &intermediateBufferBarrier, 0, NULL, 3, rtBarriers);
                }
                // Bind Render Targets
                {
                    TFBindRenderTargetsDesc bindRenderTargets = {};
                    bindRenderTargets.mRenderTargetCount = 1;
                    bindRenderTargets.mRenderTargets[0] = { pReflectionBuffer, TF_LOAD_ACTION_CLEAR };
                    cmdBindRenderTargets(cmd, &bindRenderTargets);
                    cmdSetViewport(cmd, 0.0f, 0.0f, (float)pReflectionBuffer->mWidth, (float)pReflectionBuffer->mHeight, 0.0f, 1.0f);
                    cmdSetScissor(cmd, 0, 0, pReflectionBuffer->mWidth, pReflectionBuffer->mHeight);
                }
                // Draw
                {
                    cmdBindPipeline(cmd, pPPR_ReflectionPipeline);
                    cmdBindDescriptorSet(cmd, 0, pDescriptorSetPPR);
                    cmdBindVertexBuffer(cmd, 1, &pScreenQuadVertexBuffer, &quadStride, NULL);
                    cmdDraw(cmd, 3, 0);
                }

                cmdBindRenderTargets(cmd, NULL);
                cmdEndGpuTimestampQuery(cmd, gCurrentGpuProfileToken);
            }

            cmdBeginGpuTimestampQuery(cmd, gCurrentGpuProfileToken, "Hole Patching");
            rtBarriers[0] = { pReflectionBuffer, TF_RESOURCE_STATE_RENDER_TARGET, TF_RESOURCE_STATE_SHADER_RESOURCE };
        }

        // Stochastic screen-space reflections pass
        else if (gReflectionType == SSS_REFLECTION)
        {
            cmdBeginGpuTimestampQuery(cmd, gCurrentGpuProfileToken, "Stochastic Screen Space Reflections");

            TFTextureBarrier textureBarriers[4] = {};
            TFBufferBarrier  bufferBarriers[4] = {};
            uint32_t         dim_x = (pDepthBuffer->mWidth + 7) / 8;
            uint32_t         dim_y = (pDepthBuffer->mHeight + 7) / 8;
            // Depth mips generation pass
            {
                cmdBeginGpuTimestampQuery(cmd, gCurrentGpuProfileToken, "Depth Mips Generation");
                // Copy depth pass
                {
                    cmdBeginGpuTimestampQuery(cmd, gCurrentGpuProfileToken, "Copy Depth");

                    // Resource Transition
                    {
                        textureBarriers[0] = { pSSSR_DepthHierarchy, TF_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                               TF_RESOURCE_STATE_UNORDERED_ACCESS };
                        cmdResourceBarrier(cmd, 0, NULL, 1, textureBarriers, 0, NULL);
                    }
                    // Dispatch
                    {
                        cmdBindPipeline(cmd, pCopyDepthPipeline);
                        cmdBindDescriptorSet(cmd, 0, pDescriptorSetCopyDepth);
                        cmdDispatch(cmd, dim_x, dim_y, 1);
                    }

                    cmdEndGpuTimestampQuery(cmd, gCurrentGpuProfileToken);
                }

                // SPD mip pass
                if (gUseSPD)
                {
                    cmdBeginGpuTimestampQuery(cmd, gCurrentGpuProfileToken, "SPD Generate Mips");

                    // Resource Transition
                    {
                        textureBarriers[0] = { pSSSR_DepthHierarchy, TF_RESOURCE_STATE_UNORDERED_ACCESS,
                                               TF_RESOURCE_STATE_UNORDERED_ACCESS };
                        bufferBarriers[0] = { pSPD_AtomicCounterBuffer, TF_RESOURCE_STATE_UNORDERED_ACCESS,
                                              TF_RESOURCE_STATE_UNORDERED_ACCESS };
                        cmdResourceBarrier(cmd, 1, bufferBarriers, 1, textureBarriers, 0, NULL);
                    }
                    // Dispatch
                    {
                        cmdBindPipeline(cmd, pSPDPipeline);
                        cmdBindDescriptorSet(cmd, 0, pDescriptorSetDepthDownSamplePerBatch);
                        cmdDispatch(cmd, (pDepthBuffer->mWidth + 63) / 64, (pDepthBuffer->mHeight + 63) / 64, pSceneBuffer->mArraySize);
                    }

                    cmdEndGpuTimestampQuery(cmd, gCurrentGpuProfileToken);
                }
                // Basic mip pass
                else
                {
                    cmdBeginGpuTimestampQuery(cmd, gCurrentGpuProfileToken, "Generate Mips");
                    uint32_t mipSizeX = 1 << (uint32_t)ceil(log2((float)pDepthBuffer->mWidth));
                    uint32_t mipSizeY = 1 << (uint32_t)ceil(log2((float)pDepthBuffer->mHeight));
                    cmdBindPipeline(cmd, pGenerateMipPipeline);
                    for (uint32_t i = 1; i < pSSSR_DepthHierarchy->mMipLevels; ++i)
                    {
                        mipSizeX >>= 1;
                        mipSizeY >>= 1;
                        cmdBindDescriptorSet(cmd, i - 1, pDescriptorGenerateMip);
                        textureBarriers[0] = { pSSSR_DepthHierarchy, TF_RESOURCE_STATE_UNORDERED_ACCESS,
                                               TF_RESOURCE_STATE_UNORDERED_ACCESS };
                        cmdResourceBarrier(cmd, 0, NULL, 1, textureBarriers, 0, NULL);

                        uint32_t groupCountX = mipSizeX / 16;
                        uint32_t groupCountY = mipSizeY / 16;
                        if (groupCountX == 0)
                            groupCountX = 1;
                        if (groupCountY == 0)
                            groupCountY = 1;
                        cmdDispatch(cmd, groupCountX, groupCountY, pSceneBuffer->mArraySize);
                    }
                    cmdEndGpuTimestampQuery(cmd, gCurrentGpuProfileToken);
                }

                cmdEndGpuTimestampQuery(cmd, gCurrentGpuProfileToken);
            }

            // SSSR classify pass
            {
                cmdBeginGpuTimestampQuery(cmd, gCurrentGpuProfileToken, "SSSR Classify");

                // Resource Transition
                {
                    bufferBarriers[0] = { pSSSR_RayCounterBuffer, TF_RESOURCE_STATE_UNORDERED_ACCESS, TF_RESOURCE_STATE_UNORDERED_ACCESS };
                    bufferBarriers[1] = { pSSSR_TileCounterBuffer, TF_RESOURCE_STATE_UNORDERED_ACCESS, TF_RESOURCE_STATE_UNORDERED_ACCESS };
                    bufferBarriers[2] = { pSSSR_RayListBuffer, TF_RESOURCE_STATE_UNORDERED_ACCESS, TF_RESOURCE_STATE_UNORDERED_ACCESS };
                    bufferBarriers[3] = { pSSSR_TileListBuffer, TF_RESOURCE_STATE_UNORDERED_ACCESS, TF_RESOURCE_STATE_UNORDERED_ACCESS };
                    rtBarriers[0] = { pReflectionBuffer, TF_RESOURCE_STATE_SHADER_RESOURCE, TF_RESOURCE_STATE_UNORDERED_ACCESS };
                    rtBarriers[1] = { pNormalRoughnessBuffer, TF_RESOURCE_STATE_RENDER_TARGET, TF_RESOURCE_STATE_SHADER_RESOURCE };
                    textureBarriers[0] = { pSSSR_TemporalVariance, TF_RESOURCE_STATE_UNORDERED_ACCESS, TF_RESOURCE_STATE_UNORDERED_ACCESS };
                    textureBarriers[1] = { pSSSR_TemporalResults[mSettings.mFrameIdx]->pTexture, TF_RESOURCE_STATE_UNORDERED_ACCESS,
                                           TF_RESOURCE_STATE_UNORDERED_ACCESS };
                    textureBarriers[2] = { pSSSR_RayLength->pTexture, TF_RESOURCE_STATE_UNORDERED_ACCESS,
                                           TF_RESOURCE_STATE_UNORDERED_ACCESS };
                    cmdResourceBarrier(cmd, 4, bufferBarriers, 3, textureBarriers, 2, rtBarriers);
                }
                // Dispatch
                {
                    cmdBindPipeline(cmd, pSSSR_ClassifyTilesPipeline);
                    cmdBindDescriptorSet(cmd, mSettings.mFrameIdx, pDescriptorSetSSSR);
                    cmdDispatch(cmd, dim_x, dim_y, pSceneBuffer->mArraySize);
                }

                cmdEndGpuTimestampQuery(cmd, gCurrentGpuProfileToken);
            }

            // SSSR prepare indirect pass
            {
                cmdBeginGpuTimestampQuery(cmd, gCurrentGpuProfileToken, "SSSR Prepare Indirect");

                // Resource Transition
                {
                    bufferBarriers[0] = { pSSSR_RayCounterBuffer, TF_RESOURCE_STATE_UNORDERED_ACCESS, TF_RESOURCE_STATE_UNORDERED_ACCESS };
                    bufferBarriers[1] = { pSSSR_TileCounterBuffer, TF_RESOURCE_STATE_UNORDERED_ACCESS, TF_RESOURCE_STATE_UNORDERED_ACCESS };
                    bufferBarriers[2] = { pSSSR_IntersectArgsBuffer, TF_RESOURCE_STATE_INDIRECT_ARGUMENT,
                                          TF_RESOURCE_STATE_UNORDERED_ACCESS };
                    bufferBarriers[3] = { pSSSR_DenoiserArgsBuffer, TF_RESOURCE_STATE_INDIRECT_ARGUMENT,
                                          TF_RESOURCE_STATE_UNORDERED_ACCESS };

                    cmdResourceBarrier(cmd, 4, bufferBarriers, 0, NULL, 0, NULL);
                }
                // Dispatch
                {
                    cmdBindPipeline(cmd, pSSSR_PrepareIndirectArgsPipeline);
                    cmdBindDescriptorSet(cmd, mSettings.mFrameIdx, pDescriptorSetSSSR);
                    cmdDispatch(cmd, 1, 1, 1);
                }

                cmdEndGpuTimestampQuery(cmd, gCurrentGpuProfileToken);
            }

            // SSSR intersect pass
            {
                cmdBeginGpuTimestampQuery(cmd, gCurrentGpuProfileToken, "SSSR Intersect");

                // Resource Transition
                {
                    bufferBarriers[0] = { pSSSR_IntersectArgsBuffer, TF_RESOURCE_STATE_UNORDERED_ACCESS,
                                          TF_RESOURCE_STATE_INDIRECT_ARGUMENT };
                    bufferBarriers[1] = { pSSSR_DenoiserArgsBuffer, TF_RESOURCE_STATE_UNORDERED_ACCESS,
                                          TF_RESOURCE_STATE_INDIRECT_ARGUMENT };
                    bufferBarriers[2] = { pSSSR_RayListBuffer, TF_RESOURCE_STATE_UNORDERED_ACCESS, TF_RESOURCE_STATE_UNORDERED_ACCESS };
                    textureBarriers[0] = { pSSSR_DepthHierarchy, TF_RESOURCE_STATE_UNORDERED_ACCESS,
                                           TF_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE };
                    textureBarriers[1] = { pSSSR_RayLength->pTexture, TF_RESOURCE_STATE_UNORDERED_ACCESS,
                                           TF_RESOURCE_STATE_UNORDERED_ACCESS };
                    textureBarriers[2] = { pSSSR_TemporalResults[mSettings.mFrameIdx]->pTexture, TF_RESOURCE_STATE_UNORDERED_ACCESS,
                                           TF_RESOURCE_STATE_UNORDERED_ACCESS };
                    rtBarriers[0] = { pReflectionBuffer, TF_RESOURCE_STATE_UNORDERED_ACCESS, TF_RESOURCE_STATE_UNORDERED_ACCESS };
                    rtBarriers[1] = { pSceneBuffer, TF_RESOURCE_STATE_RENDER_TARGET, TF_RESOURCE_STATE_SHADER_RESOURCE };
                    cmdResourceBarrier(cmd, 3, bufferBarriers, 3, textureBarriers, 2, rtBarriers);
                }
                // Dispatch
                {
                    cmdBindPipeline(cmd, pSSSR_IntersectPipeline);
                    cmdBindDescriptorSet(cmd, mSettings.mFrameIdx, pDescriptorSetSSSR);

                    cmdExecuteIndirect(cmd, TF_INDIRECT_DISPATCH, 1, pSSSR_IntersectArgsBuffer, 0, NULL, 0);
                }

                cmdEndGpuTimestampQuery(cmd, gCurrentGpuProfileToken);
            }

            if (!gSSSR_SkipDenoiser)
            {
                // SSSR spatial denoise pass
                {
                    cmdBeginGpuTimestampQuery(cmd, gCurrentGpuProfileToken, "SSSR Spatial Denoise");

                    // Resource Transition
                    {
                        bufferBarriers[0] = { pSSSR_TileListBuffer, TF_RESOURCE_STATE_UNORDERED_ACCESS,
                                              TF_RESOURCE_STATE_UNORDERED_ACCESS };
                        textureBarriers[0] = { pSSSR_RayLength->pTexture, TF_RESOURCE_STATE_UNORDERED_ACCESS,
                                               TF_RESOURCE_STATE_UNORDERED_ACCESS };
                        textureBarriers[1] = { pSSSR_TemporalResults[mSettings.mFrameIdx]->pTexture, TF_RESOURCE_STATE_UNORDERED_ACCESS,
                                               TF_RESOURCE_STATE_UNORDERED_ACCESS };
                        textureBarriers[2] = { pSSSR_TemporalVariance, TF_RESOURCE_STATE_UNORDERED_ACCESS,
                                               TF_RESOURCE_STATE_UNORDERED_ACCESS };
                        rtBarriers[0] = { pReflectionBuffer, TF_RESOURCE_STATE_UNORDERED_ACCESS, TF_RESOURCE_STATE_UNORDERED_ACCESS };
                        cmdResourceBarrier(cmd, 1, bufferBarriers, 3, textureBarriers, 1, rtBarriers);
                    }
                    // Dispatch
                    {
                        cmdBindPipeline(cmd, pSSSR_ResolveSpatialPipeline);
                        cmdBindDescriptorSet(cmd, mSettings.mFrameIdx, pDescriptorSetSSSR);

                        cmdExecuteIndirect(cmd, TF_INDIRECT_DISPATCH, 1, pSSSR_DenoiserArgsBuffer, 0, NULL, 0);
                    }

                    cmdEndGpuTimestampQuery(cmd, gCurrentGpuProfileToken);
                }

                // SSSR temporal denoise pass
                if (gFrameCount > 0) // Do not apply temporal denoise as long as we do not have the historical data from previous frame
                {
                    cmdBeginGpuTimestampQuery(cmd, gCurrentGpuProfileToken, "SSSR Temporal Denoise");

                    // Resource Transition
                    {
                        textureBarriers[0] = { pSSSR_TemporalResults[0]->pTexture, TF_RESOURCE_STATE_UNORDERED_ACCESS,
                                               TF_RESOURCE_STATE_UNORDERED_ACCESS };
                        textureBarriers[1] = { pSSSR_TemporalResults[1]->pTexture, TF_RESOURCE_STATE_UNORDERED_ACCESS,
                                               TF_RESOURCE_STATE_UNORDERED_ACCESS };
                        textureBarriers[2] = { pSSSR_TemporalVariance, TF_RESOURCE_STATE_UNORDERED_ACCESS,
                                               TF_RESOURCE_STATE_UNORDERED_ACCESS };
                        textureBarriers[3] = { pSSSR_RayLength->pTexture, TF_RESOURCE_STATE_UNORDERED_ACCESS,
                                               TF_RESOURCE_STATE_UNORDERED_ACCESS };
                        rtBarriers[0] = { pReflectionBuffer, TF_RESOURCE_STATE_UNORDERED_ACCESS, TF_RESOURCE_STATE_UNORDERED_ACCESS };
                        cmdResourceBarrier(cmd, 0, NULL, 4, textureBarriers, 1, rtBarriers);
                    }
                    // Dispatch
                    {
                        cmdBindPipeline(cmd, pSSSR_ResolveTemporalPipeline);
                        cmdBindDescriptorSet(cmd, mSettings.mFrameIdx, pDescriptorSetSSSR);

                        cmdExecuteIndirect(cmd, TF_INDIRECT_DISPATCH, 1, pSSSR_DenoiserArgsBuffer, 0, NULL, 0);
                    }

                    cmdEndGpuTimestampQuery(cmd, gCurrentGpuProfileToken);
                }

                // SSSR EAW denoise pass 1
                {
                    cmdBeginGpuTimestampQuery(cmd, gCurrentGpuProfileToken, "SSSR EAW Denoise Pass 1");

                    // Resource Transition
                    {
                        textureBarriers[0] = { pSSSR_TemporalResults[mSettings.mFrameIdx]->pTexture, TF_RESOURCE_STATE_UNORDERED_ACCESS,
                                               TF_RESOURCE_STATE_UNORDERED_ACCESS };
                        rtBarriers[0] = { pReflectionBuffer, TF_RESOURCE_STATE_UNORDERED_ACCESS, TF_RESOURCE_STATE_UNORDERED_ACCESS };
                        cmdResourceBarrier(cmd, 0, NULL, 1, textureBarriers, 1, rtBarriers);
                    }
                    // Dispatch
                    {
                        cmdBindPipeline(cmd, pSSSR_ResolveEAWPipeline);
                        cmdBindDescriptorSet(cmd, mSettings.mFrameIdx, pDescriptorSetSSSR);

                        cmdExecuteIndirect(cmd, TF_INDIRECT_DISPATCH, 1, pSSSR_DenoiserArgsBuffer, 0, NULL, 0);
                    }

                    cmdEndGpuTimestampQuery(cmd, gCurrentGpuProfileToken);
                }

                // SSSR EAW denoise passes 2, 3
                if (gSSSR_EAWPassCount == 3)
                {
                    // SSSR EAW denoise pass 2
                    {
                        cmdBeginGpuTimestampQuery(cmd, gCurrentGpuProfileToken, "SSSR EAW Denoise Pass 2");

                        // Resource Transition
                        {
                            textureBarriers[0] = { pSSSR_TemporalResults[mSettings.mFrameIdx]->pTexture, TF_RESOURCE_STATE_UNORDERED_ACCESS,
                                                   TF_RESOURCE_STATE_UNORDERED_ACCESS };
                            rtBarriers[0] = { pReflectionBuffer, TF_RESOURCE_STATE_UNORDERED_ACCESS, TF_RESOURCE_STATE_UNORDERED_ACCESS };
                            cmdResourceBarrier(cmd, 0, NULL, 1, textureBarriers, 1, rtBarriers);
                        }
                        // Dispatch
                        {
                            cmdBindPipeline(cmd, pSSSR_ResolveEAWStride2Pipeline);
                            cmdBindDescriptorSet(cmd, 0, pDescriptorSetSSSR);

                            cmdExecuteIndirect(cmd, TF_INDIRECT_DISPATCH, 1, pSSSR_DenoiserArgsBuffer, 0, NULL, 0);
                        }

                        cmdEndGpuTimestampQuery(cmd, gCurrentGpuProfileToken);
                    }

                    // SSSR EAW denoise pass 3
                    {
                        cmdBeginGpuTimestampQuery(cmd, gCurrentGpuProfileToken, "SSSR EAW Denoise Pass 3");

                        // Resource Transition
                        {
                            textureBarriers[0] = { pSSSR_TemporalResults[mSettings.mFrameIdx]->pTexture, TF_RESOURCE_STATE_UNORDERED_ACCESS,
                                                   TF_RESOURCE_STATE_UNORDERED_ACCESS };
                            rtBarriers[0] = { pReflectionBuffer, TF_RESOURCE_STATE_UNORDERED_ACCESS, TF_RESOURCE_STATE_UNORDERED_ACCESS };
                            cmdResourceBarrier(cmd, 0, NULL, 1, textureBarriers, 1, rtBarriers);
                        }
                        // Dispatch
                        {
                            cmdBindPipeline(cmd, pSSSR_ResolveEAWStride4Pipeline);
                            cmdBindDescriptorSet(cmd, 0, pDescriptorSetSSSR);

                            cmdExecuteIndirect(cmd, TF_INDIRECT_DISPATCH, 1, pSSSR_DenoiserArgsBuffer, 0, NULL, 0);
                        }

                        cmdEndGpuTimestampQuery(cmd, gCurrentGpuProfileToken);
                    }
                }
            }
            cmdBeginGpuTimestampQuery(cmd, gCurrentGpuProfileToken, "Apply Reflections");
            rtBarriers[0] = { pReflectionBuffer, TF_RESOURCE_STATE_UNORDERED_ACCESS, TF_RESOURCE_STATE_SHADER_RESOURCE };
        }

        TFRenderTarget* pRenderTarget = pSwapChain->ppRenderTargets[swapchainImageIndex];

        // Apply reflections to the swapchain image
        {
            // Resource Transition
            {
                rtBarriers[1] = { pRenderTarget, TF_RESOURCE_STATE_PRESENT, TF_RESOURCE_STATE_RENDER_TARGET };
                cmdResourceBarrier(cmd, 0, NULL, 0, NULL, 2, rtBarriers);
            }
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
                cmdBindPipeline(cmd, pPPR_HolePatchingPipeline);
                cmdBindDescriptorSet(cmd, 0, pDescriptorSetPPR);
                cmdBindVertexBuffer(cmd, 1, &pScreenQuadVertexBuffer, &quadStride, NULL);
                cmdDraw(cmd, 3, 0);
            }

            cmdEndGpuTimestampQuery(cmd, gCurrentGpuProfileToken); // Hole Patching / Apply Reflections
        }

        cmdEndGpuTimestampQuery(cmd, gCurrentGpuProfileToken); // End Reflections

        // UI pass
        {
            cmdBeginGpuTimestampQuery(cmd, gCurrentGpuProfileToken, "UI Pass");
            cmdBeginDebugMarker(cmd, 0, 1, 0, "Draw UI");

            // Bind Render Targets
            {
                TFBindRenderTargetsDesc bindRenderTargets = {};
                bindRenderTargets.mRenderTargetCount = 1;
                bindRenderTargets.mRenderTargets[0] = { pRenderTarget, TF_LOAD_ACTION_LOAD };
                cmdBindRenderTargets(cmd, &bindRenderTargets);
            }

            gFrameTimeDraw.mFontColor = 0xff00ffff;
            gFrameTimeDraw.mFontSize = 18.0f;
            gFrameTimeDraw.pFont = gFont;
            float2 txtSize = cmdDrawCpuProfile(cmd, float2(8.0f, 15.0f), &gFrameTimeDraw);
            cmdDrawGpuProfile(cmd, float2(8.f, txtSize.y + 75.f), gCurrentGpuProfileToken, &gFrameTimeDraw);

            uiCmdDrawUserInterface(cmd, pSwapChain, pRenderTarget, gCurrentGpuProfileToken);
            cmdEndDebugMarker(cmd);
            cmdEndGpuTimestampQuery(cmd, gCurrentGpuProfileToken);
            cmdBindRenderTargets(cmd, NULL);
        }

        // Restore frame resources for the next frame
        {
            const uint32_t        numBarriers = NUM_CULLING_VIEWPORTS + 2;
            TFRenderTargetBarrier presentBarrier = { pRenderTarget, TF_RESOURCE_STATE_RENDER_TARGET, TF_RESOURCE_STATE_PRESENT };
            TFBufferBarrier       restoreVisibilityBufferBarriers[numBarriers] = {};
            uint32_t              barrierCount = 0;

            restoreVisibilityBufferBarriers[barrierCount++] = {
                pVisibilityBuffer->ppIndirectDrawArgBuffer[0],
                TF_RESOURCE_STATE_INDIRECT_ARGUMENT | TF_RESOURCE_STATE_SHADER_RESOURCE,
                TF_RESOURCE_STATE_UNORDERED_ACCESS,
            };
            restoreVisibilityBufferBarriers[barrierCount++] = {
                pVisibilityBuffer->ppIndirectDataBuffer[mSettings.mFrameIdx],
                TF_RESOURCE_STATE_SHADER_RESOURCE,
                TF_RESOURCE_STATE_UNORDERED_ACCESS,
            };
            for (uint32_t i = 0; i < NUM_CULLING_VIEWPORTS; ++i)
            {
                restoreVisibilityBufferBarriers[barrierCount++] = {
                    pVisibilityBuffer->ppFilteredIndexBuffer[i],
                    TF_RESOURCE_STATE_INDEX_BUFFER | TF_RESOURCE_STATE_SHADER_RESOURCE,
                    TF_RESOURCE_STATE_UNORDERED_ACCESS,
                };
            }

            cmdResourceBarrier(cmd, numBarriers, restoreVisibilityBufferBarriers, 0, NULL, 1, &presentBarrier);
        }

        cmdEndGpuFrameProfile(cmd, gCurrentGpuProfileToken);
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

        ++gFrameCount;
    }

    const char* GetName() { return "10_ScreenSpaceReflections"; }

private:
    void computePBRMaps()
    {
        TFShader*        pBRDFIntegrationShader = NULL;
        TFPipeline*      pBRDFIntegrationPipeline = NULL;
        TFShader*        pIrradianceShader = NULL;
        TFPipeline*      pIrradiancePipeline = NULL;
        TFShader*        pSpecularShader = NULL;
        TFPipeline*      pSpecularPipeline = NULL;
        TFSampler*       pSkyboxSampler = NULL;
        TFDescriptorSet* pDescriptorSetPBRPersistent = { NULL };
        TFDescriptorSet* pDescriptorSetPBRPerFrame = { NULL };
        TFDescriptorSet* pDescriptorSetPBRPerBatch = { NULL };
        TFBuffer*        pUniformBufferSpecularConfig[8] = { NULL };

        // PBR Texture values (these values are mirrored on the shaders).
        static const uint32_t gBRDFIntegrationSize = 512;
        static const uint32_t gSkyboxMips = 11;
        static const uint32_t gIrradianceSize = 32;
        static const uint32_t gSpecularSize = 128;
        static const uint32_t gSpecularMips = (uint)log2(gSpecularSize) + 1;

        // Add Resources
        {
            static const int skyboxIndex = 0;
            const char*      skyboxNames[] = {
                "LA_Helipad3D.tex",
            };

            // Some texture format are not well covered on android devices (R32G32_SFLOAT, D32_SFLOAT, R32G32B32A32_SFLOAT)
            bool supportLinearFiltering = pRenderer->pGpu->mFormatCaps[TinyImageFormat_R32G32B32A32_SFLOAT] & TF_FORMAT_CAP_LINEAR_FILTER;

            TFSamplerDesc samplerDesc = {};
            samplerDesc.mMinFilter = supportLinearFiltering ? TF_FILTER_LINEAR : TF_FILTER_NEAREST;
            samplerDesc.mMagFilter = supportLinearFiltering ? TF_FILTER_LINEAR : TF_FILTER_NEAREST;
            samplerDesc.mMipMapMode = supportLinearFiltering ? TF_MIPMAP_MODE_LINEAR : TF_MIPMAP_MODE_NEAREST;
            samplerDesc.mAddressU = TF_ADDRESS_MODE_REPEAT;
            samplerDesc.mAddressV = TF_ADDRESS_MODE_REPEAT;
            samplerDesc.mAddressW = TF_ADDRESS_MODE_REPEAT;
            samplerDesc.mMipLodBias = 0.0f;
            samplerDesc.mSetLodRange = false;
            samplerDesc.mMinLod = 0.0f;
            samplerDesc.mMaxLod = 0.0f;
            samplerDesc.mMaxAnisotropy = 16.0f;

            addSampler(pRenderer, &samplerDesc, &pSkyboxSampler);

            // Load the skybox panorama texture.
            TFSyncToken       token = {};
            TFTextureLoadDesc skyboxDesc = {};
            skyboxDesc.pFileName = skyboxNames[skyboxIndex];
            skyboxDesc.ppTexture = &pSkybox;
            addResource(&skyboxDesc, &token);

            TFTextureDesc irrImgDesc = {};
            irrImgDesc.mArraySize = 6;
            irrImgDesc.mDepth = 1;
            irrImgDesc.mFormat = TinyImageFormat_R32G32B32A32_SFLOAT;
            irrImgDesc.mHeight = gIrradianceSize;
            irrImgDesc.mWidth = gIrradianceSize;
            irrImgDesc.mMipLevels = 1;
            irrImgDesc.mSampleCount = TF_SAMPLE_COUNT_1;
            irrImgDesc.mStartState = TF_RESOURCE_STATE_UNORDERED_ACCESS;
            irrImgDesc.mDescriptors = TF_DESCRIPTOR_TYPE_TEXTURE_CUBE | TF_DESCRIPTOR_TYPE_RW_TEXTURE;
            irrImgDesc.pName = "irrImgBuff";

            TFTextureLoadDesc irrLoadDesc = {};
            irrLoadDesc.pDesc = &irrImgDesc;
            irrLoadDesc.ppTexture = &pIrradianceMap;
            addResource(&irrLoadDesc, &token);

            TFTextureDesc specImgDesc = {};
            specImgDesc.mArraySize = 6;
            specImgDesc.mDepth = 1;
            specImgDesc.mFormat = TinyImageFormat_R32G32B32A32_SFLOAT;
            specImgDesc.mHeight = gSpecularSize;
            specImgDesc.mWidth = gSpecularSize;
            specImgDesc.mMipLevels = gSpecularMips;
            specImgDesc.mSampleCount = TF_SAMPLE_COUNT_1;
            specImgDesc.mStartState = TF_RESOURCE_STATE_UNORDERED_ACCESS;
            specImgDesc.mDescriptors = TF_DESCRIPTOR_TYPE_TEXTURE_CUBE | TF_DESCRIPTOR_TYPE_RW_TEXTURE;
            specImgDesc.pName = "specImgBuff";

            TFTextureLoadDesc specImgLoadDesc = {};
            specImgLoadDesc.pDesc = &specImgDesc;
            specImgLoadDesc.ppTexture = &pSpecularMap;
            addResource(&specImgLoadDesc, &token);

            // Create empty texture for BRDF integration map.
            TFTextureLoadDesc brdfIntegrationLoadDesc = {};
            TFTextureDesc     brdfIntegrationDesc = {};
            brdfIntegrationDesc.mWidth = gBRDFIntegrationSize;
            brdfIntegrationDesc.mHeight = gBRDFIntegrationSize;
            brdfIntegrationDesc.mDepth = 1;
            brdfIntegrationDesc.mArraySize = 1;
            brdfIntegrationDesc.mMipLevels = 1;
            brdfIntegrationDesc.mFormat = TinyImageFormat_R32G32_SFLOAT;
            brdfIntegrationDesc.mStartState = TF_RESOURCE_STATE_UNORDERED_ACCESS;
            brdfIntegrationDesc.mDescriptors = TF_DESCRIPTOR_TYPE_TEXTURE | TF_DESCRIPTOR_TYPE_RW_TEXTURE;
            brdfIntegrationDesc.mSampleCount = TF_SAMPLE_COUNT_1;
            brdfIntegrationLoadDesc.pDesc = &brdfIntegrationDesc;
            brdfIntegrationLoadDesc.ppTexture = &pBRDFIntegrationMap;
            addResource(&brdfIntegrationLoadDesc, &token);

            TFGPUPresetLevel presetLevel = pRenderer->pGpu->mGpuVendorPreset.mPresetLevel;

            const char* brdfIntegrationShaders[TFGPUPresetLevel::TF_GPU_PRESET_COUNT] = {
                "BRDFIntegration_SAMPLES_0.comp",   // TF_GPU_PRESET_NONE
                "BRDFIntegration_SAMPLES_0.comp",   // TF_GPU_PRESET_OFFICE
                "BRDFIntegration_SAMPLES_32.comp",  // TF_GPU_PRESET_VERYLOW
                "BRDFIntegration_SAMPLES_64.comp",  // TF_GPU_PRESET_LOW
                "BRDFIntegration_SAMPLES_128.comp", // TF_GPU_PRESET_MEDIUM
                "BRDFIntegration_SAMPLES_256.comp", // TF_GPU_PRESET_HIGH
                "BRDFIntegration_SAMPLES_1024.comp" // TF_GPU_PRESET_ULTRA
            };

            const char* irradianceShaders[TFGPUPresetLevel::TF_GPU_PRESET_COUNT] = {
                "computeIrradianceMap_SAMPLE_DELTA_05.comp",   // TF_GPU_PRESET_NONE
                "computeIrradianceMap_SAMPLE_DELTA_05.comp",   // TF_GPU_PRESET_OFFICE
                "computeIrradianceMap_SAMPLE_DELTA_05.comp",   // TF_GPU_PRESET_VERYLOW
                "computeIrradianceMap_SAMPLE_DELTA_025.comp",  // TF_GPU_PRESET_LOW
                "computeIrradianceMap_SAMPLE_DELTA_0125.comp", // TF_GPU_PRESET_MEDIUM
                "computeIrradianceMap_SAMPLE_DELTA_005.comp",  // TF_GPU_PRESET_HIGH
                "computeIrradianceMap_SAMPLE_DELTA_0025.comp"  // TF_GPU_PRESET_ULTRA
            };

            const char* specularShaders[TFGPUPresetLevel::TF_GPU_PRESET_COUNT] = {
                "computeSpecularMap_SAMPLES_0.comp",   // TF_GPU_PRESET_NONE
                "computeSpecularMap_SAMPLES_0.comp",   // TF_GPU_PRESET_OFFICE
                "computeSpecularMap_SAMPLES_32.comp",  // TF_GPU_PRESET_VERYLOW
                "computeSpecularMap_SAMPLES_64.comp",  // TF_GPU_PRESET_LOW
                "computeSpecularMap_SAMPLES_128.comp", // TF_GPU_PRESET_MEDIUM
                "computeSpecularMap_SAMPLES_256.comp", // TF_GPU_PRESET_HIGH
                "computeSpecularMap_SAMPLES_1024.comp" // TF_GPU_PRESET_ULTRA
            };

            TFShaderLoadDesc brdfIntegrationShaderDesc = {};
            brdfIntegrationShaderDesc.mComp.pFileName = brdfIntegrationShaders[presetLevel];

            TFShaderLoadDesc irradianceShaderDesc = {};
            irradianceShaderDesc.mComp.pFileName = irradianceShaders[presetLevel];

            TFShaderLoadDesc specularShaderDesc = {};
            specularShaderDesc.mComp.pFileName = specularShaders[presetLevel];

            addShader(pRenderer, &irradianceShaderDesc, &pIrradianceShader);
            addShader(pRenderer, &specularShaderDesc, &pSpecularShader);
            addShader(pRenderer, &brdfIntegrationShaderDesc, &pBRDFIntegrationShader);

            TFDescriptorSetDesc setDesc = SRT_SET_DESC(ComputeSpecularSrtData, Persistent, 1, 0);
            addDescriptorSet(pRenderer, &setDesc, &pDescriptorSetPBRPersistent);
            setDesc = SRT_SET_DESC(ComputeSpecularSrtData, PerBatch, gSkyboxMips, 0);
            addDescriptorSet(pRenderer, &setDesc, &pDescriptorSetPBRPerBatch);

            setDesc = SRT_SET_DESC(ComputeSpecularSrtData, PerFrame, gSkyboxMips, 0);
            addDescriptorSet(pRenderer, &setDesc, &pDescriptorSetPBRPerFrame);

            TFPipelineDesc desc = {};
            PIPELINE_LAYOUT_DESC(desc, SRT_LAYOUT_DESC(ComputeSpecularSrtData, Persistent),
                                 SRT_LAYOUT_DESC(ComputeSpecularSrtData, PerFrame), SRT_LAYOUT_DESC(ComputeSpecularSrtData, PerBatch),
                                 NULL);
            desc.mType = TF_PIPELINE_TYPE_COMPUTE;
            TFComputePipelineDesc& pipelineSettings = desc.mComputeDesc;
            pipelineSettings.pShaderProgram = pIrradianceShader;
            addPipeline(pRenderer, &desc, &pIrradiancePipeline);
            pipelineSettings.pShaderProgram = pSpecularShader;

            addPipeline(pRenderer, &desc, &pSpecularPipeline);
            pipelineSettings.pShaderProgram = pBRDFIntegrationShader;
            addPipeline(pRenderer, &desc, &pBRDFIntegrationPipeline);

            waitForToken(&token);

            struct PrecomputeSkySpecularData
            {
                uint  mipSize;
                float roughness;
            };
            PrecomputeSkySpecularData data[8] = {};
            for (uint32_t i = 0; i < gSpecularMips; i++)
            {
                data[i].roughness = (float)i / (float)(gSpecularMips - 1);
                data[i].mipSize = gSpecularSize >> i;
                TFBufferLoadDesc specularDataBufferDesc = {};
                specularDataBufferDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                specularDataBufferDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
                specularDataBufferDesc.mDesc.mSize = sizeof(PrecomputeSkySpecularData);
                specularDataBufferDesc.mDesc.mFlags = TF_BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
                specularDataBufferDesc.pData = &data[i];
                specularDataBufferDesc.ppBuffer = &pUniformBufferSpecularConfig[i];
                addResource(&specularDataBufferDesc, NULL);
            }
        }

        TFTextureDescriptor* pSpecularMipDescs[8] = {};
        TFTextureDescriptor* pIrradianceUavDesc = NULL;
        for (uint32_t i = 0; i < gSpecularMips; i++)
        {
            TFTextureDescriptorDesc specMipDesc = {};
            specMipDesc.pTexture = pSpecularMap;
            specMipDesc.mDescriptors = TF_DESCRIPTOR_TYPE_RW_TEXTURE;
            specMipDesc.mBaseMipLevel = i;
            specMipDesc.mMipLevelCount = 1;
            addTextureDescriptor(pRenderer, &specMipDesc, &pSpecularMipDescs[i]);
        }

        GpuCmdRingElement elem = getNextGpuCmdRingElement(&gGraphicsCmdRing, true, 1);
        TFCmd*            pCmd = elem.pCmds[0];

        // Update Descriptor Sets
        {
            TFDescriptorData params[2] = {};
            params[0].mIndex = SRT_RES_IDX(ComputeSpecularSrtData, Persistent, gSrcTexture);
            params[0].ppTextures = &pSkybox;
            params[1].mIndex = SRT_RES_IDX(ComputeSpecularSrtData, Persistent, gSkyboxSampler);
            params[1].ppSamplers = &pSkyboxSampler;
            updateDescriptorSet(pRenderer, 0, pDescriptorSetPBRPersistent, 2, params);

            TFTextureDescriptorDesc irrUavDesc = {};
            irrUavDesc.pTexture = pIrradianceMap;
            irrUavDesc.mDescriptors = TF_DESCRIPTOR_TYPE_RW_TEXTURE;
            addTextureDescriptor(pRenderer, &irrUavDesc, &pIrradianceUavDesc);
            params[0].mIndex = SRT_RES_IDX(ComputeSpecularSrtData, PerBatch, gDstTextureRW);
            params[0].ppTextures = &pBRDFIntegrationMap;
            params[1].mIndex = SRT_RES_IDX(ComputeSpecularSrtData, PerBatch, gDstTextureArrayRW);
            params[1].ppTextureDescriptors = &pIrradianceUavDesc;
            params[1].mUseTextureDescriptors = 1;
            updateDescriptorSet(pRenderer, 0, pDescriptorSetPBRPerBatch, 2, params);

            for (uint32_t i = 0; i < gSpecularMips; i++)
            {
                params[0].mIndex = SRT_RES_IDX(ComputeSpecularSrtData, PerFrame, gComputeSpecularParams);
                params[0].ppBuffers = &pUniformBufferSpecularConfig[i];
                updateDescriptorSet(pRenderer, i, pDescriptorSetPBRPerFrame, 1, params);
                params[0].mIndex = SRT_RES_IDX(ComputeSpecularSrtData, PerBatch, gDstTexturePerDraw);
                params[0].ppTextureDescriptors = &pSpecularMipDescs[i];
                params[0].mUseTextureDescriptors = 1;
                updateDescriptorSet(pRenderer, i, pDescriptorSetPBRPerBatch, 1, params);
            }
        }

        resetCmdPool(pRenderer, elem.pCmdPool);
        beginCmd(pCmd);
        // Compute the BRDF Integration Map pass
        {
            cmdBindPipeline(pCmd, pBRDFIntegrationPipeline);
            cmdBindDescriptorSet(pCmd, 0, pDescriptorSetPBRPersistent);
            cmdBindDescriptorSet(pCmd, 0, pDescriptorSetPBRPerBatch);
            const uint32_t* pThreadGroupSize = pBRDFIntegrationShader->mNumThreadsPerGroup;
            cmdDispatch(pCmd, gBRDFIntegrationSize / pThreadGroupSize[0], gBRDFIntegrationSize / pThreadGroupSize[1], pThreadGroupSize[2]);

            // Resource Transition
            {
                TFTextureBarrier srvBarrier[1] = { { pBRDFIntegrationMap, TF_RESOURCE_STATE_UNORDERED_ACCESS,
                                                     TF_RESOURCE_STATE_SHADER_RESOURCE } };

                cmdResourceBarrier(pCmd, 0, NULL, 1, srvBarrier, 0, NULL);
            }
        }

        // Compute Sky Irradiance pass
        {
            cmdBindPipeline(pCmd, pIrradiancePipeline);
            cmdBindDescriptorSet(pCmd, 0, pDescriptorSetPBRPersistent);
            const uint32_t* pThreadGroupSize = pIrradianceShader->mNumThreadsPerGroup;
            cmdDispatch(pCmd, gIrradianceSize / pThreadGroupSize[0], gIrradianceSize / pThreadGroupSize[1], 6);
        }

        // Compute Sky Specular pass
        {
            cmdBindPipeline(pCmd, pSpecularPipeline);
            cmdBindDescriptorSet(pCmd, 0, pDescriptorSetPBRPersistent);
            const uint32_t* pThreadGroupSize = pIrradianceShader->mNumThreadsPerGroup;
            for (uint32_t i = 0; i < gSpecularMips; i++)
            {
                cmdBindDescriptorSet(pCmd, i, pDescriptorSetPBRPerBatch);
                cmdBindDescriptorSet(pCmd, i, pDescriptorSetPBRPerFrame);
                cmdDispatch(pCmd, max(1u, (gSpecularSize >> i) / pThreadGroupSize[0]), max(1u, (gSpecularSize >> i) / pThreadGroupSize[1]),
                            6);
            }
        }

        /************************************************************************/
        /************************************************************************/
        TFTextureBarrier srvBarriers2[2] = { { pIrradianceMap, TF_RESOURCE_STATE_UNORDERED_ACCESS, TF_RESOURCE_STATE_SHADER_RESOURCE },
                                             { pSpecularMap, TF_RESOURCE_STATE_UNORDERED_ACCESS, TF_RESOURCE_STATE_SHADER_RESOURCE } };
        cmdResourceBarrier(pCmd, 0, NULL, 2, srvBarriers2, 0, NULL);

        endCmd(pCmd);

        FlushResourceUpdateDesc flushDesc = {};
        flushResourceUpdates(&flushDesc);
        waitForFences(pRenderer, 1, &flushDesc.pOutFence);

        TFQueueSubmitDesc submitDesc = {};
        submitDesc.mCmdCount = 1;
        submitDesc.ppCmds = &pCmd;
        submitDesc.pSignalFence = elem.pFence;
        submitDesc.mSubmitDone = true;
        queueSubmit(pGraphicsQueue, &submitDesc);
        waitForFences(pRenderer, 1, &elem.pFence);

        // Remove Resources
        {
            removeDescriptorSet(pRenderer, pDescriptorSetPBRPerFrame);
            removeDescriptorSet(pRenderer, pDescriptorSetPBRPersistent);
            removeDescriptorSet(pRenderer, pDescriptorSetPBRPerBatch);

            removeTextureDescriptor(pRenderer, pIrradianceUavDesc);
            for (uint32_t i = 0; i < gSpecularMips; i++)
            {
                removeTextureDescriptor(pRenderer, pSpecularMipDescs[i]);
            }

            removePipeline(pRenderer, pSpecularPipeline);
            removeShader(pRenderer, pSpecularShader);
            removePipeline(pRenderer, pIrradiancePipeline);
            removeShader(pRenderer, pIrradianceShader);

            for (uint32_t i = 0; i < gSpecularMips; i++)
            {
                removeResource(pUniformBufferSpecularConfig[i]);
            }

            removePipeline(pRenderer, pBRDFIntegrationPipeline);
            removeShader(pRenderer, pBRDFIntegrationShader);
            removeSampler(pRenderer, pSkyboxSampler);
        }
    }

    void updateDescriptorSets()
    {
        TFDescriptorData persistentSetParams[30] = {};

        persistentSetParams[0].mIndex = SRT_RES_IDX(SrtData, Persistent, gVBPassTexture);
        persistentSetParams[0].ppTextures = &pRenderTargetVBPass->pTexture;

        TFTexture** ppPaddedTextures = NULL;
        if (gUsingTextureAtlasFallback)
        {
            persistentSetParams[1].mIndex = SRT_RES_IDX(SrtData, Persistent, gAtlasTextures);
            persistentSetParams[1].mCount = (uint32_t)pPackage->mAtlasCount;
            persistentSetParams[1].ppTextures = pPackage->ppAtlasTextures;
        }
        else
        {
            ppPaddedTextures = (TFTexture**)tf_malloc(MAX_TEXTURE_UNITS * sizeof(TFTexture*));
            for (uint32_t i = 0; i < MAX_TEXTURE_UNITS; ++i)
            {
                ppPaddedTextures[i] = (i < gAllTexturesCount) ? pPackage->ppAllTextures[i] : gNullTextureResource;
            }
            persistentSetParams[1].mIndex = SRT_RES_IDX(SrtData, Persistent, gAllTextures);
            persistentSetParams[1].mCount = MAX_TEXTURE_UNITS;
            persistentSetParams[1].ppTextures = ppPaddedTextures;
        }

        persistentSetParams[2].mIndex = SRT_RES_IDX(SrtData, Persistent, gVertexPositionBuffer);
        persistentSetParams[2].ppBuffers = &pSanMiguelModel->pVertexBuffers[0];
        persistentSetParams[3].mIndex = SRT_RES_IDX(SrtData, Persistent, gVertexTexCoordBuffer);
        persistentSetParams[3].ppBuffers = &pSanMiguelModel->pVertexBuffers[1];
        persistentSetParams[4].mIndex = SRT_RES_IDX(SrtData, Persistent, gVertexNormalBuffer);
        persistentSetParams[4].ppBuffers = &pSanMiguelModel->pVertexBuffers[2];
        persistentSetParams[5].mIndex = SRT_RES_IDX(SrtData, Persistent, gMeshConstantsBuffer);
        persistentSetParams[5].ppBuffers = &pBufferMeshConstants;
        persistentSetParams[6].mIndex = SRT_RES_IDX(SrtData, Persistent, gCBLights);
        persistentSetParams[6].ppBuffers = &pBufferUniformLights;
        persistentSetParams[7].mIndex = SRT_RES_IDX(SrtData, Persistent, gCBDLights);
        persistentSetParams[7].ppBuffers = &pBufferUniformDirectionalLights;
        persistentSetParams[8].mIndex = SRT_RES_IDX(SrtData, Persistent, gBRDFIntegrationMap);
        persistentSetParams[8].ppTextures = &pBRDFIntegrationMap;
        persistentSetParams[9].mIndex = SRT_RES_IDX(SrtData, Persistent, gIrradianceMap);
        persistentSetParams[9].ppTextures = &pIrradianceMap;
        persistentSetParams[10].mIndex = SRT_RES_IDX(SrtData, Persistent, gSpecularMap);
        persistentSetParams[10].ppTextures = &pSpecularMap;
        persistentSetParams[11].mIndex = SRT_RES_IDX(SrtData, Persistent, gVBConstantBuffer);
        persistentSetParams[11].ppBuffers = &pVisibilityBuffer->pVBConstantBuffer;
        persistentSetParams[12].mIndex = SRT_RES_IDX(SrtData, Persistent, gDefaultSampler);
        persistentSetParams[12].ppSamplers = &pDefaultSampler;
        persistentSetParams[13].mIndex = SRT_RES_IDX(SrtData, Persistent, gEnvSampler);
        persistentSetParams[13].ppSamplers = &pSamplerBilinear;
        persistentSetParams[14].mIndex = SRT_RES_IDX(SrtData, Persistent, gDepthSampler);
        persistentSetParams[14].ppSamplers = &pSamplerBilinear;
        persistentSetParams[15].mIndex = SRT_RES_IDX(SrtData, Persistent, gSkyboxSampler);
        persistentSetParams[15].ppSamplers = &pSamplerBilinear;
        persistentSetParams[16].mIndex = SRT_RES_IDX(SrtData, Persistent, gBilinearSampler);
        persistentSetParams[16].ppSamplers = &pSamplerBilinear;
        persistentSetParams[17].mIndex = SRT_RES_IDX(SrtData, Persistent, gSkyboxTex);
        persistentSetParams[17].ppTextures = &pSkybox;
        persistentSetParams[18].mIndex = SRT_RES_IDX(SrtData, Persistent, gIndexDataBuffer);
        persistentSetParams[18].ppBuffers = &pSanMiguelModel->pIndexBuffer;
        persistentSetParams[19].mIndex = SRT_RES_IDX(SrtData, Persistent, gDepthTexture);
        persistentSetParams[19].ppTextures = &pDepthBuffer->pTexture;
        persistentSetParams[20].mIndex = SRT_RES_IDX(SrtData, Persistent, gSceneTexture);
        persistentSetParams[20].ppTextures = &pSceneBuffer->pTexture;
        persistentSetParams[21].mIndex = SRT_RES_IDX(SrtData, Persistent, gSSRTexture);
        persistentSetParams[21].ppTextures = &pReflectionBuffer->pTexture;
        persistentSetParams[22].mIndex = SRT_RES_IDX(SrtData, Persistent, gDepthBuffer);
        persistentSetParams[22].ppTextures = &pDepthBuffer->pTexture;
        persistentSetParams[23].mIndex = SRT_RES_IDX(SrtData, Persistent, gSourceDepth);
        persistentSetParams[23].ppTextures = &pDepthBuffer->pTexture;
        persistentSetParams[24].mIndex = SRT_RES_IDX(SrtData, Persistent, gLitScene);
        persistentSetParams[24].ppTextures = &pSceneBuffer->pTexture;
        persistentSetParams[25].mIndex = SRT_RES_IDX(SrtData, Persistent, gDepthBufferHierarchy);
        persistentSetParams[25].ppTextures = &pSSSR_DepthHierarchy;
        persistentSetParams[26].mIndex = SRT_RES_IDX(SrtData, Persistent, gSobolBuffer);
        persistentSetParams[26].ppBuffers = &pSSSR_SobolBuffer;
        persistentSetParams[27].mIndex = SRT_RES_IDX(SrtData, Persistent, gRankingTileBuffer);
        persistentSetParams[27].ppBuffers = &pSSSR_RankingTileBuffer;
        persistentSetParams[28].mIndex = SRT_RES_IDX(SrtData, Persistent, gScramblingTileBuffer);
        persistentSetParams[28].ppBuffers = &pSSSR_ScramblingTileBuffer;

        if (gUsingTextureAtlasFallback)
        {
            persistentSetParams[29].mIndex = SRT_RES_IDX(SrtData, Persistent, gAtlasPlacementsBuffer);
            persistentSetParams[29].mCount = 1;
            persistentSetParams[29].ppBuffers = &pPackage->pAtlasPlacementsBuffer;
            updateDescriptorSet(pRenderer, 0, pDescriptorSetPersistent, 30, persistentSetParams);
        }
        else
        {
            updateDescriptorSet(pRenderer, 0, pDescriptorSetPersistent, 29, persistentSetParams);
            tf_free(ppPaddedTextures);
        }

        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            uint8_t          prevIndex = (i + gDataBufferCount - 1) % gDataBufferCount;
            TFDescriptorData SSSRParams[14] = {};
            SSSRParams[0].mIndex = SRT_RES_IDX(SSSRSrtData, PerBatch, gTileList);
            SSSRParams[0].ppBuffers = &pSSSR_TileListBuffer;
            SSSRParams[1].mIndex = SRT_RES_IDX(SSSRSrtData, PerBatch, gRayList);
            SSSRParams[1].ppBuffers = &pSSSR_RayListBuffer;
            SSSRParams[2].mIndex = SRT_RES_IDX(SSSRSrtData, PerBatch, gRayCounter);
            SSSRParams[2].ppBuffers = &pSSSR_RayCounterBuffer;
            SSSRParams[3].mIndex = SRT_RES_IDX(SSSRSrtData, PerBatch, gTileCounter);
            SSSRParams[3].ppBuffers = &pSSSR_TileCounterBuffer;
            SSSRParams[4].mIndex = SRT_RES_IDX(SSSRSrtData, PerBatch, gIntersectArgs);
            SSSRParams[4].ppBuffers = &pSSSR_IntersectArgsBuffer;
            SSSRParams[5].mIndex = SRT_RES_IDX(SSSRSrtData, PerBatch, gDenoiserArgs);
            SSSRParams[5].ppBuffers = &pSSSR_DenoiserArgsBuffer;
            SSSRParams[6].mIndex = SRT_RES_IDX(SSSRSrtData, PerBatch, gRayLengths);
            SSSRParams[6].ppTextures = &pSSSR_RayLength->pTexture;
            SSSRParams[7].mIndex = SRT_RES_IDX(SSSRSrtData, PerBatch, gHasRay);
            SSSRParams[7].ppTextures = &pSSSR_TemporalVariance;
            SSSRParams[8].mIndex = SRT_RES_IDX(SSSRSrtData, PerBatch, gTemporalVariance);
            SSSRParams[8].ppTextures = &pSSSR_TemporalVariance;
            SSSRParams[9].mIndex = SRT_RES_IDX(SSSRSrtData, PerBatch, gDenoisedReflections);
            SSSRParams[9].ppTextures = &pReflectionBuffer->pTexture;
            SSSRParams[10].mIndex = SRT_RES_IDX(SSSRSrtData, PerBatch, gTemporallyDenoisedReflections);
            SSSRParams[10].ppTextures = &pSSSR_TemporalResults[i]->pTexture;
            SSSRParams[11].mIndex = SRT_RES_IDX(SSSRSrtData, PerBatch, gIntersectionResult);
            SSSRParams[11].ppTextures = &pSSSR_TemporalResults[i]->pTexture;
            SSSRParams[12].mIndex = SRT_RES_IDX(SSSRSrtData, PerBatch, gSpatiallyDenoisedReflections);
            SSSRParams[12].ppTextures = &pReflectionBuffer->pTexture;
            SSSRParams[13].mIndex = SRT_RES_IDX(SSSRSrtData, PerBatch, gTemporallyDenoisedReflectionsHistory);
            SSSRParams[13].ppTextures = &pSSSR_TemporalResults[prevIndex]->pTexture;
            updateDescriptorSet(pRenderer, i, pDescriptorSetSSSR, 14, SSSRParams);
        }

        TFDescriptorData PPRParams[2] = {};
        PPRParams[0].mIndex = SRT_RES_IDX(PPRSrtData, PerBatch, gIntermediateBuffer);
        PPRParams[0].ppBuffers = &pIntermediateBuffer;
        updateDescriptorSet(pRenderer, 0, pDescriptorSetPPR, 1, PPRParams);

        TFDescriptorData copyDepthParams[2] = {};
        copyDepthParams[0].mIndex = SRT_RES_IDX(CopyDepthSrtData, PerBatch, gDestinationDepth);
        copyDepthParams[0].ppTextures = &pSSSR_DepthHierarchy;
        updateDescriptorSet(pRenderer, 0, pDescriptorSetCopyDepth, 1, copyDepthParams);

        TFDescriptorData depthDownsampleParams[2] = {};
        depthDownsampleParams[0].mIndex = SRT_RES_IDX(DepthDownSampleSrtData, PerBatch, gGlobalAtomic);
        depthDownsampleParams[0].ppBuffers = &pSPD_AtomicCounterBuffer;
        depthDownsampleParams[1].mIndex = SRT_RES_IDX(DepthDownSampleSrtData, PerBatch, gDownsampledDepthBuffer);
        depthDownsampleParams[1].ppTextureDescriptors = pSSSR_DepthHierarchyMipDescs;
        depthDownsampleParams[1].mCount = pSSSR_DepthHierarchy->mMipLevels;
        depthDownsampleParams[1].mUseTextureDescriptors = 1;
        updateDescriptorSet(pRenderer, 0, pDescriptorSetDepthDownSamplePerBatch, 2, depthDownsampleParams);

        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            TFDescriptorData indirectDrawClearParams[4] = {};
            indirectDrawClearParams[0].mIndex = SRT_RES_IDX(TriangleFilteringSrtData, PerBatch, gIndirectDrawClearArgsRW);
            indirectDrawClearParams[0].ppBuffers = &pVisibilityBuffer->ppIndirectDrawArgBuffer[0];
            indirectDrawClearParams[1].mIndex = SRT_RES_IDX(TriangleFilteringSrtData, PerBatch, gIndirectDataBufferRW);
            indirectDrawClearParams[1].ppBuffers = &pVisibilityBuffer->ppIndirectDataBuffer[i];
            indirectDrawClearParams[2].mIndex = SRT_RES_IDX(TriangleFilteringSrtData, PerBatch, gFilteredIndicesBufferRW);
            indirectDrawClearParams[2].mCount = NUM_CULLING_VIEWPORTS;
            indirectDrawClearParams[2].ppBuffers = &pVisibilityBuffer->ppFilteredIndexBuffer[0];
            updateDescriptorSet(pRenderer, i, pDescriptorSetTriangleFilteringPerBatch, 3, indirectDrawClearParams);
        }

        TFDescriptorData perFrameSetParams[12] = {};
        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            uint8_t prevIndex = (i + gDataBufferCount - 1) % gDataBufferCount;
            perFrameSetParams[0].mIndex = SRT_RES_IDX(SrtData, PerFrame, gUniformBlockPerFrame);
            perFrameSetParams[0].ppBuffers = &pBufferUniformCameraSky[i];
            perFrameSetParams[1].mIndex = SRT_RES_IDX(SrtData, PerFrame, gVBConstantsPerFrame);
            perFrameSetParams[1].ppBuffers = &pBufferVBConstants[i];
            perFrameSetParams[2].mIndex = SRT_RES_IDX(SrtData, PerFrame, gFilterDispatchGroupDataBuffer);
            perFrameSetParams[2].ppBuffers = &pVisibilityBuffer->ppFilterDispatchGroupDataBuffer[i];
            perFrameSetParams[3].mIndex = SRT_RES_IDX(SrtData, PerFrame, gObjectUniformBlockPerFrame);
            perFrameSetParams[3].ppBuffers = &pBufferMeshTransforms[i];
            perFrameSetParams[4].mIndex = SRT_RES_IDX(SrtData, PerFrame, gIndirectDataBuffer);
            perFrameSetParams[4].ppBuffers = &pVisibilityBuffer->ppIndirectDataBuffer[i];
            perFrameSetParams[5].mIndex = SRT_RES_IDX(SrtData, PerFrame, gFilteredIndexBuffer);
            perFrameSetParams[5].ppBuffers = &pVisibilityBuffer->ppFilteredIndexBuffer[VIEW_CAMERA];
            perFrameSetParams[6].mIndex = SRT_RES_IDX(SrtData, PerFrame, gCBExtendCamera);
            perFrameSetParams[6].ppBuffers = &pBufferUniformExtendedCamera[i];
            perFrameSetParams[7].mIndex = SRT_RES_IDX(SrtData, PerFrame, gPlaneInfoBuffer);
            perFrameSetParams[7].ppBuffers = &pBufferUniformPlaneInfo[i];
            perFrameSetParams[8].mIndex = SRT_RES_IDX(SrtData, PerFrame, gCBProperties);
            perFrameSetParams[8].ppBuffers = &pBufferUniformPPRPro[i];
            perFrameSetParams[9].mIndex = SRT_RES_IDX(SrtData, PerFrame, gNormalRoughness);
            perFrameSetParams[9].ppTextures = &pNormalRoughnessBuffers[i]->pTexture;
            perFrameSetParams[10].mIndex = SRT_RES_IDX(SrtData, PerFrame, gConstants);
            perFrameSetParams[10].ppBuffers = &pSSSR_ConstantsBuffer[i];
            perFrameSetParams[11].mIndex = SRT_RES_IDX(SrtData, PerFrame, gNormalRoughnessHistory);
            perFrameSetParams[11].ppTextures = &pNormalRoughnessBuffers[prevIndex]->pTexture;
            updateDescriptorSet(pRenderer, i, pDescriptorSetPerFrame, 12, perFrameSetParams);
        }
        if (gSSSRSupported)
        {
            for (uint32_t i = 1; i < pSSSR_DepthHierarchy->mMipLevels; ++i)
            {
                perFrameSetParams[0].mIndex = SRT_RES_IDX(GenerateMipsSrtData, PerBatch, gSourceTexture);
                perFrameSetParams[0].ppTextureDescriptors = &pSSSR_DepthHierarchyMipDescs[i - 1];
                perFrameSetParams[0].mUseTextureDescriptors = 1;
                perFrameSetParams[1].mIndex = SRT_RES_IDX(GenerateMipsSrtData, PerBatch, gDestinationTexture);
                perFrameSetParams[1].ppTextureDescriptors = &pSSSR_DepthHierarchyMipDescs[i];
                perFrameSetParams[1].mUseTextureDescriptors = 1;
                perFrameSetParams[2].mIndex = SRT_RES_IDX(GenerateMipsSrtData, PerBatch, gGenMipsConstants);
                perFrameSetParams[2].ppBuffers = &pSSSR_GenMipsBuffers[i];
                updateDescriptorSet(pRenderer, i - 1, pDescriptorGenerateMip, 3, perFrameSetParams);
            }
        }
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
        // None set
        TFDescriptorSetDesc setDesc = SRT_SET_DESC(SrtData, Persistent, 1, 0);
        addDescriptorSet(pRenderer, &setDesc, &pDescriptorSetPersistent);

        // Per frame set
        setDesc = SRT_SET_DESC(SrtData, PerFrame, gDataBufferCount, 0);
        addDescriptorSet(pRenderer, &setDesc, &pDescriptorSetPerFrame);

        if (gSSSRSupported)
        {
            // DepthDownsample
            setDesc = SRT_SET_DESC(GenerateMipsSrtData, PerBatch, 13, 0);
            addDescriptorSet(pRenderer, &setDesc, &pDescriptorGenerateMip);
        }

        // Depth Downsample
        setDesc = SRT_SET_DESC_LARGE_RW(DepthDownSampleSrtData, PerBatch, 1, 0);
        addDescriptorSet(pRenderer, &setDesc, &pDescriptorSetDepthDownSamplePerBatch);

        // SSSR
        setDesc = SRT_SET_DESC(SSSRSrtData, PerBatch, gDataBufferCount, 0);
        addDescriptorSet(pRenderer, &setDesc, &pDescriptorSetSSSR);

        // PPR
        setDesc = SRT_SET_DESC(PPRSrtData, PerBatch, 1, 0);
        addDescriptorSet(pRenderer, &setDesc, &pDescriptorSetPPR);

        // Copy depth
        setDesc = SRT_SET_DESC(CopyDepthSrtData, PerBatch, 1, 0);
        addDescriptorSet(pRenderer, &setDesc, &pDescriptorSetCopyDepth);

        // Indirect draw clear
        setDesc = SRT_SET_DESC(TriangleFilteringSrtData, PerBatch, gDataBufferCount, 0);
        addDescriptorSet(pRenderer, &setDesc, &pDescriptorSetTriangleFilteringPerBatch);
    }

    void removeDescriptorSets()
    {
        removeDescriptorSet(pRenderer, pDescriptorSetTriangleFilteringPerBatch);
        removeDescriptorSet(pRenderer, pDescriptorSetCopyDepth);
        removeDescriptorSet(pRenderer, pDescriptorSetPPR);
        removeDescriptorSet(pRenderer, pDescriptorSetSSSR);
        removeDescriptorSet(pRenderer, pDescriptorSetDepthDownSamplePerBatch);
        if (gSSSRSupported)
        {
            removeDescriptorSet(pRenderer, pDescriptorGenerateMip);
        }
        removeDescriptorSet(pRenderer, pDescriptorSetPerFrame);
        removeDescriptorSet(pRenderer, pDescriptorSetPersistent);
    }

    void addShaders()
    {
        // Load shaders for Vis Buffer
        TFShaderLoadDesc clearBuffersShaderDesc = {};
        clearBuffersShaderDesc.mComp.pFileName = "clearVisibilityBuffers.comp";
        addShader(pRenderer, &clearBuffersShaderDesc, &pShaderClearBuffers);

        TFShaderLoadDesc triangleFilteringShaderDesc = {};
        triangleFilteringShaderDesc.mComp.pFileName = "triangleFiltering.comp";
        addShader(pRenderer, &triangleFilteringShaderDesc, &pShaderTriangleFiltering);

        TFShaderLoadDesc shaderVBrepass = {};
        shaderVBrepass.mVert.pFileName = gUsingPrimitiveIDFallback ? "visibilityBufferPass_primid.vert" : "visibilityBufferPass.vert";
        shaderVBrepass.mFrag.pFileName = gUsingPrimitiveIDFallback ? "visibilityBufferPass_primid.frag" : "visibilityBufferPass.frag";

        // Some vulkan driver doesn't generate glPrimitiveID without a geometry pass (steam deck as 03/30/2023)
        bool addGeometryPassThrough = gSettings.mAddGeometryPassThrough;
        if (addGeometryPassThrough)
        {
            // A passthrough gs
            shaderVBrepass.mGeom.pFileName = "visibilityBufferPass.geom";
        }

        addShader(pRenderer, &shaderVBrepass, &pShaderVBBufferPass[GEOMSET_OPAQUE]);

        TFShaderLoadDesc visibilityBufferPassAlphaShaderDesc = {};
        visibilityBufferPassAlphaShaderDesc.mVert.pFileName =
            gUsingPrimitiveIDFallback ? "visibilityBufferPassAlpha_primid.vert" : "visibilityBufferPassAlpha.vert";

        if (gUsingTextureAtlasFallback)
        {
            visibilityBufferPassAlphaShaderDesc.mFrag.pFileName =
                gUsingPrimitiveIDFallback ? "visibilityBufferPassAlpha_atlas_primid.frag" : "visibilityBufferPassAlpha_atlas.frag";
        }
        else
        {
            visibilityBufferPassAlphaShaderDesc.mFrag.pFileName =
                gUsingPrimitiveIDFallback ? "visibilityBufferPassAlpha_primid.frag" : "visibilityBufferPassAlpha.frag";
        }

        if (addGeometryPassThrough)
        {
            // A passthrough gs
            visibilityBufferPassAlphaShaderDesc.mGeom.pFileName = "visibilityBufferPassAlpha.geom";
        }

        addShader(pRenderer, &visibilityBufferPassAlphaShaderDesc, &pShaderVBBufferPass[GEOMSET_ALPHA_CUTOUT]);

        TFShaderLoadDesc visibilityBufferShadeShaderDesc = {};
        visibilityBufferShadeShaderDesc.mVert.pFileName = "visibilityBufferShade.vert";
        visibilityBufferShadeShaderDesc.mFrag.pFileName =
            gUsingTextureAtlasFallback ? "visibilityBufferShade_atlas.frag" : "visibilityBufferShade.frag";
        addShader(pRenderer, &visibilityBufferShadeShaderDesc, &pShaderVBShade);

        TFShaderLoadDesc skyboxShaderDesc = {};
        skyboxShaderDesc.mVert.pFileName = "skybox.vert";
        skyboxShaderDesc.mFrag.pFileName = "skybox.frag";
        addShader(pRenderer, &skyboxShaderDesc, &pSkyboxShader);

        // PPR_Projection
        TFShaderLoadDesc PPR_ProjectionShaderDesc = {};
        PPR_ProjectionShaderDesc.mComp.pFileName = "PPR_Projection.comp";
        addShader(pRenderer, &PPR_ProjectionShaderDesc, &pPPR_ProjectionShader);

        // PPR_Reflection
        TFShaderLoadDesc PPR_ReflectionShaderDesc = {};
        PPR_ReflectionShaderDesc.mVert.pFileName = "PPR_Reflection.vert";
        PPR_ReflectionShaderDesc.mFrag.pFileName = "PPR_Reflection.frag";
        addShader(pRenderer, &PPR_ReflectionShaderDesc, &pPPR_ReflectionShader);

        // PPR_HolePatching
        TFShaderLoadDesc PPR_HolePatchingShaderDesc = {};
        PPR_HolePatchingShaderDesc.mVert.pFileName = "PPR_Holepatching.vert";
        PPR_HolePatchingShaderDesc.mFrag.pFileName = "PPR_Holepatching.frag";
        addShader(pRenderer, &PPR_HolePatchingShaderDesc, &pPPR_HolePatchingShader);

        if (gSSSRSupported)
        {
            TFShaderLoadDesc SPDDesc = {};
            SPDDesc.mComp.pFileName = "DepthDownsample.comp";
            addShader(pRenderer, &SPDDesc, &pSPDShader);

            TFShaderLoadDesc CopyDepthShaderDesc = {};
            CopyDepthShaderDesc.mComp.pFileName = "copyDepth.comp";
            addShader(pRenderer, &CopyDepthShaderDesc, &pCopyDepthShader);

            TFShaderLoadDesc GenerateMipShaderDesc = {};
            GenerateMipShaderDesc.mComp.pFileName = "generateMips.comp";
            addShader(pRenderer, &GenerateMipShaderDesc, &pGenerateMipShader);

            // SSSR
            TFShaderLoadDesc SSSR_ClassifyTilesShaderDesc = {};
            SSSR_ClassifyTilesShaderDesc.mComp.pFileName = "SSSR_ClassifyTiles.comp";
            addShader(pRenderer, &SSSR_ClassifyTilesShaderDesc, &pSSSR_ClassifyTilesShader);

            TFShaderLoadDesc SSSR_PrepareIndirectArgsShaderDesc = {};
            SSSR_PrepareIndirectArgsShaderDesc.mComp.pFileName = "SSSR_PrepareIndirectArgs.comp";
            addShader(pRenderer, &SSSR_PrepareIndirectArgsShaderDesc, &pSSSR_PrepareIndirectArgsShader);

            TFShaderLoadDesc SSSR_IntersectShaderDesc = {};
            SSSR_IntersectShaderDesc.mComp.pFileName = "SSSR_Intersect.comp";
            addShader(pRenderer, &SSSR_IntersectShaderDesc, &pSSSR_IntersectShader);

            TFShaderLoadDesc SSSR_ResolveSpatialShaderDesc = {};
            SSSR_ResolveSpatialShaderDesc.mComp.pFileName = "SSSR_ResolveSpatial.comp";
            addShader(pRenderer, &SSSR_ResolveSpatialShaderDesc, &pSSSR_ResolveSpatialShader);

            TFShaderLoadDesc SSSR_ResolveTemporalShaderDesc = {};
            SSSR_ResolveTemporalShaderDesc.mComp.pFileName = "SSSR_ResolveTemporal.comp";
            addShader(pRenderer, &SSSR_ResolveTemporalShaderDesc, &pSSSR_ResolveTemporalShader);

            TFShaderLoadDesc SSSR_ResolveEAWShaderDesc = {};
            SSSR_ResolveEAWShaderDesc.mComp.pFileName = "SSSR_ResolveEaw.comp";
            addShader(pRenderer, &SSSR_ResolveEAWShaderDesc, &pSSSR_ResolveEAWShader);

            TFShaderLoadDesc SSSR_ResolveEAWStride2ShaderDesc = {};
            SSSR_ResolveEAWStride2ShaderDesc.mComp.pFileName = "SSSR_ResolveEawStride_2.comp";
            addShader(pRenderer, &SSSR_ResolveEAWStride2ShaderDesc, &pSSSR_ResolveEAWStride2Shader);

            TFShaderLoadDesc SSSR_ResolveEAWStride4ShaderDesc = {};
            SSSR_ResolveEAWStride4ShaderDesc.mComp.pFileName = "SSSR_ResolveEawStride_4.comp";
            addShader(pRenderer, &SSSR_ResolveEAWStride4ShaderDesc, &pSSSR_ResolveEAWStride4Shader);
        }
    }

    void removeShaders()
    {
        removeShader(pRenderer, pShaderClearBuffers);
        removeShader(pRenderer, pShaderTriangleFiltering);
        removeShader(pRenderer, pShaderVBBufferPass[GEOMSET_OPAQUE]);
        removeShader(pRenderer, pShaderVBBufferPass[GEOMSET_ALPHA_CUTOUT]);
        removeShader(pRenderer, pShaderVBShade);

        if (gSSSRSupported)
        {
            removeShader(pRenderer, pSSSR_ResolveEAWStride4Shader);
            removeShader(pRenderer, pSSSR_ResolveEAWStride2Shader);
            removeShader(pRenderer, pSSSR_ResolveEAWShader);
            removeShader(pRenderer, pSSSR_ResolveTemporalShader);
            removeShader(pRenderer, pSSSR_ResolveSpatialShader);
            removeShader(pRenderer, pSSSR_IntersectShader);
            removeShader(pRenderer, pSSSR_PrepareIndirectArgsShader);
            removeShader(pRenderer, pSSSR_ClassifyTilesShader);
            removeShader(pRenderer, pSPDShader);
            removeShader(pRenderer, pGenerateMipShader);
            removeShader(pRenderer, pCopyDepthShader);
        }

        removeShader(pRenderer, pPPR_HolePatchingShader);
        removeShader(pRenderer, pPPR_ReflectionShader);
        removeShader(pRenderer, pPPR_ProjectionShader);
        removeShader(pRenderer, pSkyboxShader);
    }

    void addPipelines()
    {
        TFDepthStateDesc depthStateDisabledDesc = {};
        TFDepthStateDesc depthStateTestAndWriteDesc = {};
        depthStateTestAndWriteDesc.mDepthTest = true;
        depthStateTestAndWriteDesc.mDepthWrite = true;
        depthStateTestAndWriteDesc.mDepthFunc = TF_CMP_GEQUAL;

        // Create rasteriser state objects
        TFRasterizerStateDesc rasterizerStateCullNoneDesc = { TF_CULL_MODE_NONE };
        {
            /************************************************************************/
            // Setup the resources needed for the Visibility Buffer Pipeline
            /******************************/

            TinyImageFormat formats[2] = {};
            formats[0] = pRenderTargetVBPass->mFormat;

            TFPipelineDesc desc = {};
            PIPELINE_LAYOUT_DESC(desc, SRT_LAYOUT_DESC(SrtData, Persistent), SRT_LAYOUT_DESC(SrtData, PerFrame), NULL, NULL);
            desc.mType = TF_PIPELINE_TYPE_GRAPHICS;
            TFGraphicsPipelineDesc& vbPassPipelineSettings = desc.mGraphicsDesc;
            vbPassPipelineSettings.mPrimitiveTopo = TF_PRIMITIVE_TOPO_TRI_LIST;
            vbPassPipelineSettings.mRenderTargetCount = 1;
            vbPassPipelineSettings.pDepthState = &depthStateTestAndWriteDesc;
            vbPassPipelineSettings.pColorFormats = formats;
            vbPassPipelineSettings.mSampleCount = pRenderTargetVBPass->mSampleCount;
            vbPassPipelineSettings.mSampleQuality = pRenderTargetVBPass->mSampleQuality;
            vbPassPipelineSettings.mDepthStencilFormat = pDepthBuffer->mFormat;
            vbPassPipelineSettings.pVertexLayout = NULL;
            vbPassPipelineSettings.pRasterizerState = &rasterizerStateCullNoneDesc;

            for (uint32_t i = 0; i < gNumGeomSets; ++i)
            {
                vbPassPipelineSettings.pShaderProgram = pShaderVBBufferPass[i];

#if defined(GFX_EXTENDED_PSO_OPTIONS)
                ExtendedGraphicsPipelineDesc edescs[2] = {};
                edescs[0].type = EXTENDED_GRAPHICS_PIPELINE_TYPE_SHADER_LIMITS;
                initExtendedGraphicsShaderLimits(&edescs[0].shaderLimitsDesc);
                edescs[0].shaderLimitsDesc.maxWavesWithLateAllocParameterCache = 16;

                edescs[1].type = EXTENDED_GRAPHICS_PIPELINE_TYPE_PIXEL_SHADER_OPTIONS;
                edescs[1].pixelShaderOptions.outOfOrderRasterization = PIXEL_SHADER_OPTION_OUT_OF_ORDER_RASTERIZATION_ENABLE_WATER_MARK_7;
                edescs[1].pixelShaderOptions.depthBeforeShader =
                    !i ? PIXEL_SHADER_OPTION_DEPTH_BEFORE_SHADER_ENABLE : PIXEL_SHADER_OPTION_DEPTH_BEFORE_SHADER_DEFAULT;

                desc.mExtensionCount = 2;
                desc.pPipelineExtensions = edescs;
#endif
                addPipeline(pRenderer, &desc, &pPipelineVBBufferPass[i]);

                desc.mExtensionCount = 0;
            }

            formats[0] = pSceneBuffer->mFormat;
            formats[1] = pNormalRoughnessBuffers[0]->mFormat;

            desc.mGraphicsDesc = {};
            TFGraphicsPipelineDesc& vbShadePipelineSettings = desc.mGraphicsDesc;
            vbShadePipelineSettings.mPrimitiveTopo = TF_PRIMITIVE_TOPO_TRI_LIST;
            vbShadePipelineSettings.mRenderTargetCount = 2;
            vbShadePipelineSettings.pDepthState = &depthStateDisabledDesc;
            vbShadePipelineSettings.pRasterizerState = &rasterizerStateCullNoneDesc;
            vbShadePipelineSettings.pShaderProgram = pShaderVBShade;
            vbShadePipelineSettings.mSampleCount = TF_SAMPLE_COUNT_1;
            vbShadePipelineSettings.pColorFormats = formats;
            vbShadePipelineSettings.mSampleQuality = pSceneBuffer->mSampleQuality;
#if defined(GFX_EXTENDED_PSO_OPTIONS)
            ExtendedGraphicsPipelineDesc edescs[2] = {};
            edescs[0].type = EXTENDED_GRAPHICS_PIPELINE_TYPE_SHADER_LIMITS;
            initExtendedGraphicsShaderLimits(&edescs[0].shaderLimitsDesc);
            // edescs[0].ShaderLimitsDesc.MaxWavesWithLateAllocParameterCache = 22;

            edescs[1].type = EXTENDED_GRAPHICS_PIPELINE_TYPE_PIXEL_SHADER_OPTIONS;
            edescs[1].pixelShaderOptions.outOfOrderRasterization = PIXEL_SHADER_OPTION_OUT_OF_ORDER_RASTERIZATION_ENABLE_WATER_MARK_7;
            edescs[1].pixelShaderOptions.depthBeforeShader = PIXEL_SHADER_OPTION_DEPTH_BEFORE_SHADER_ENABLE;

            desc.mExtensionCount = 2;
            desc.pPipelineExtensions = edescs;
#endif
            PIPELINE_LAYOUT_DESC(desc, SRT_LAYOUT_DESC(SrtData, Persistent), SRT_LAYOUT_DESC(SrtData, PerFrame), NULL, NULL);
            addPipeline(pRenderer, &desc, &pPipelineVBShadeSrgb);

            PIPELINE_LAYOUT_DESC(desc, SRT_LAYOUT_DESC(TriangleFilteringSrtData, Persistent),
                                 SRT_LAYOUT_DESC(TriangleFilteringSrtData, PerFrame), SRT_LAYOUT_DESC(TriangleFilteringSrtData, PerBatch),
                                 NULL);
            desc.mExtensionCount = 0;
            desc.mType = TF_PIPELINE_TYPE_COMPUTE;
            desc.mComputeDesc = {};
            TFComputePipelineDesc& clearBufferPipelineSettings = desc.mComputeDesc;
            clearBufferPipelineSettings.pShaderProgram = pShaderClearBuffers;
            addPipeline(pRenderer, &desc, &pPipelineClearBuffers);

            PIPELINE_LAYOUT_DESC(desc, SRT_LAYOUT_DESC(TriangleFilteringSrtData, Persistent),
                                 SRT_LAYOUT_DESC(TriangleFilteringSrtData, PerFrame), SRT_LAYOUT_DESC(TriangleFilteringSrtData, PerBatch),
                                 NULL);
            desc.mComputeDesc = {};
            TFComputePipelineDesc& triangleFilteringPipelineSettings = desc.mComputeDesc;
            triangleFilteringPipelineSettings.pShaderProgram = pShaderTriangleFiltering;
            addPipeline(pRenderer, &desc, &pPipelineTriangleFiltering);
        }

        // Layout and pipeline for skybox draw
        TFBlendStateDesc blendStateSkyBoxDesc = {};
        blendStateSkyBoxDesc.mBlendModes[0] = TF_BM_ADD;
        blendStateSkyBoxDesc.mBlendAlphaModes[0] = TF_BM_ADD;
        blendStateSkyBoxDesc.mSrcFactors[0] = TF_BC_ONE_MINUS_DST_ALPHA;
        blendStateSkyBoxDesc.mDstFactors[0] = TF_BC_DST_ALPHA;
        blendStateSkyBoxDesc.mSrcAlphaFactors[0] = TF_BC_ZERO;
        blendStateSkyBoxDesc.mDstAlphaFactors[0] = TF_BC_ONE;
        blendStateSkyBoxDesc.mColorWriteMasks[0] = TF_COLOR_MASK_ALL;
        blendStateSkyBoxDesc.mRenderTargetMask = TF_BLEND_STATE_TARGET_0;

        TFVertexLayout vertexLayoutSkybox = {};
        vertexLayoutSkybox.mBindingCount = 1;
        vertexLayoutSkybox.mAttribCount = 1;
        vertexLayoutSkybox.mAttribs[0].mSemantic = TF_SEMANTIC_POSITION;
        vertexLayoutSkybox.mAttribs[0].mFormat = TinyImageFormat_R32G32B32A32_SFLOAT;
        vertexLayoutSkybox.mAttribs[0].mBinding = 0;
        vertexLayoutSkybox.mAttribs[0].mLocation = 0;
        vertexLayoutSkybox.mAttribs[0].mOffset = 0;

        TFPipelineDesc desc = {};
        PIPELINE_LAYOUT_DESC(desc, SRT_LAYOUT_DESC(SrtData, Persistent), SRT_LAYOUT_DESC(SrtData, PerFrame), NULL, NULL);
        desc.mType = TF_PIPELINE_TYPE_GRAPHICS;
        TFGraphicsPipelineDesc& skyboxPipelineDesc = desc.mGraphicsDesc;
        skyboxPipelineDesc.mPrimitiveTopo = TF_PRIMITIVE_TOPO_TRI_LIST;
        skyboxPipelineDesc.pDepthState = NULL;
        skyboxPipelineDesc.pBlendState = &blendStateSkyBoxDesc;
        skyboxPipelineDesc.mRenderTargetCount = 1;
        skyboxPipelineDesc.pColorFormats = &pSceneBuffer->mFormat;
        skyboxPipelineDesc.mSampleCount = pSceneBuffer->mSampleCount;
        skyboxPipelineDesc.mSampleQuality = pSceneBuffer->mSampleQuality;
        skyboxPipelineDesc.mDepthStencilFormat = pDepthBuffer->mFormat;
        skyboxPipelineDesc.pShaderProgram = pSkyboxShader;
        skyboxPipelineDesc.pVertexLayout = &vertexLayoutSkybox;
        skyboxPipelineDesc.pRasterizerState = &rasterizerStateCullNoneDesc;
        addPipeline(pRenderer, &desc, &pSkyboxPipeline);

        // Position
        TFVertexLayout vertexLayoutScreenQuad = {};
        vertexLayoutScreenQuad.mBindingCount = 1;
        vertexLayoutScreenQuad.mAttribCount = 2;

        vertexLayoutScreenQuad.mAttribs[0].mSemantic = TF_SEMANTIC_POSITION;
        vertexLayoutScreenQuad.mAttribs[0].mFormat = TinyImageFormat_R32G32B32_SFLOAT;
        vertexLayoutScreenQuad.mAttribs[0].mBinding = 0;
        vertexLayoutScreenQuad.mAttribs[0].mLocation = 0;
        vertexLayoutScreenQuad.mAttribs[0].mOffset = 0;

        // Uv
        vertexLayoutScreenQuad.mAttribs[1].mSemantic = TF_SEMANTIC_TEXCOORD0;
        vertexLayoutScreenQuad.mAttribs[1].mFormat = TinyImageFormat_R32G32_SFLOAT;
        vertexLayoutScreenQuad.mAttribs[1].mLocation = 1;
        vertexLayoutScreenQuad.mAttribs[1].mBinding = 0;
        vertexLayoutScreenQuad.mAttribs[1].mOffset = 3 * sizeof(float); // first attribute contains 3 floats

        // PPR_Reflection
        PIPELINE_LAYOUT_DESC(desc, SRT_LAYOUT_DESC(PPRSrtData, Persistent), SRT_LAYOUT_DESC(PPRSrtData, PerFrame),
                             SRT_LAYOUT_DESC(PPRSrtData, PerBatch), NULL);
        desc.mGraphicsDesc = {};
        TFGraphicsPipelineDesc& pipelineSettings = desc.mGraphicsDesc;
        pipelineSettings.mPrimitiveTopo = TF_PRIMITIVE_TOPO_TRI_LIST;
        pipelineSettings.mRenderTargetCount = 1;
        pipelineSettings.pDepthState = NULL;

        pipelineSettings.pColorFormats = &pReflectionBuffer->mFormat;
        pipelineSettings.mSampleCount = pReflectionBuffer->mSampleCount;
        pipelineSettings.mSampleQuality = pReflectionBuffer->mSampleQuality;

        pipelineSettings.mDepthStencilFormat = TinyImageFormat_UNDEFINED;
        pipelineSettings.pShaderProgram = pPPR_ReflectionShader;
        pipelineSettings.pVertexLayout = &vertexLayoutScreenQuad;
        pipelineSettings.pRasterizerState = &rasterizerStateCullNoneDesc;
        addPipeline(pRenderer, &desc, &pPPR_ReflectionPipeline);

        // PPR_HolePatching -> Present
        pipelineSettings = { 0 };
        pipelineSettings.mPrimitiveTopo = TF_PRIMITIVE_TOPO_TRI_LIST;
        pipelineSettings.mRenderTargetCount = 1;
        pipelineSettings.pDepthState = NULL;

        pipelineSettings.pColorFormats = &pSwapChain->ppRenderTargets[0]->mFormat;
        pipelineSettings.mSampleCount = pSwapChain->ppRenderTargets[0]->mSampleCount;
        pipelineSettings.mSampleQuality = pSwapChain->ppRenderTargets[0]->mSampleQuality;

        pipelineSettings.mDepthStencilFormat = TinyImageFormat_UNDEFINED;
        pipelineSettings.pShaderProgram = pPPR_HolePatchingShader;
        pipelineSettings.pVertexLayout = &vertexLayoutScreenQuad;
        pipelineSettings.pRasterizerState = &rasterizerStateCullNoneDesc;
        addPipeline(pRenderer, &desc, &pPPR_HolePatchingPipeline);

        // PPR_Projection
        TFPipelineDesc computeDesc = {};
        PIPELINE_LAYOUT_DESC(computeDesc, SRT_LAYOUT_DESC(PPRSrtData, Persistent), SRT_LAYOUT_DESC(PPRSrtData, PerFrame),
                             SRT_LAYOUT_DESC(PPRSrtData, PerBatch), NULL);
        computeDesc.mType = TF_PIPELINE_TYPE_COMPUTE;
        TFComputePipelineDesc& cpipelineSettings = computeDesc.mComputeDesc;
        cpipelineSettings.pShaderProgram = pPPR_ProjectionShader;
        addPipeline(pRenderer, &computeDesc, &pPPR_ProjectionPipeline);

        if (gSSSRSupported)
        {
            PIPELINE_LAYOUT_DESC(computeDesc, SRT_LAYOUT_DESC(DepthDownSampleSrtData, Persistent),
                                 SRT_LAYOUT_DESC(DepthDownSampleSrtData, PerFrame), SRT_LAYOUT_DESC(DepthDownSampleSrtData, PerBatch),
                                 NULL);
            cpipelineSettings = { 0 };
            cpipelineSettings.pShaderProgram = pSPDShader;
            addPipeline(pRenderer, &computeDesc, &pSPDPipeline);

            PIPELINE_LAYOUT_DESC(computeDesc, SRT_LAYOUT_DESC(CopyDepthSrtData, Persistent), SRT_LAYOUT_DESC(CopyDepthSrtData, PerFrame),
                                 SRT_LAYOUT_DESC(CopyDepthSrtData, PerBatch), NULL);
            cpipelineSettings = { 0 };
            cpipelineSettings.pShaderProgram = pCopyDepthShader;
            addPipeline(pRenderer, &computeDesc, &pCopyDepthPipeline);

            PIPELINE_LAYOUT_DESC(computeDesc, SRT_LAYOUT_DESC(GenerateMipsSrtData, Persistent),
                                 SRT_LAYOUT_DESC(GenerateMipsSrtData, PerFrame), NULL, SRT_LAYOUT_DESC(GenerateMipsSrtData, PerBatch));
            cpipelineSettings = { 0 };
            cpipelineSettings.pShaderProgram = pGenerateMipShader;
            addPipeline(pRenderer, &computeDesc, &pGenerateMipPipeline);

            // SSSR
            PIPELINE_LAYOUT_DESC(computeDesc, SRT_LAYOUT_DESC(SSSRSrtData, Persistent), SRT_LAYOUT_DESC(SSSRSrtData, PerFrame),
                                 SRT_LAYOUT_DESC(SSSRSrtData, PerBatch), NULL);
            cpipelineSettings = { 0 };
            cpipelineSettings.pShaderProgram = pSSSR_ClassifyTilesShader;
            addPipeline(pRenderer, &computeDesc, &pSSSR_ClassifyTilesPipeline);

            cpipelineSettings = { 0 };
            cpipelineSettings.pShaderProgram = pSSSR_PrepareIndirectArgsShader;
            addPipeline(pRenderer, &computeDesc, &pSSSR_PrepareIndirectArgsPipeline);

            cpipelineSettings = { 0 };
            cpipelineSettings.pShaderProgram = pSSSR_IntersectShader;
            addPipeline(pRenderer, &computeDesc, &pSSSR_IntersectPipeline);

            cpipelineSettings = { 0 };
            cpipelineSettings.pShaderProgram = pSSSR_ResolveSpatialShader;
            addPipeline(pRenderer, &computeDesc, &pSSSR_ResolveSpatialPipeline);

            cpipelineSettings = { 0 };
            cpipelineSettings.pShaderProgram = pSSSR_ResolveTemporalShader;
            addPipeline(pRenderer, &computeDesc, &pSSSR_ResolveTemporalPipeline);

            cpipelineSettings = { 0 };
            cpipelineSettings.pShaderProgram = pSSSR_ResolveEAWShader;
            addPipeline(pRenderer, &computeDesc, &pSSSR_ResolveEAWPipeline);

            cpipelineSettings = { 0 };
            cpipelineSettings.pShaderProgram = pSSSR_ResolveEAWStride2Shader;
            addPipeline(pRenderer, &computeDesc, &pSSSR_ResolveEAWStride2Pipeline);

            cpipelineSettings = { 0 };
            cpipelineSettings.pShaderProgram = pSSSR_ResolveEAWStride4Shader;
            addPipeline(pRenderer, &computeDesc, &pSSSR_ResolveEAWStride4Pipeline);
        }
    }

    void removePipelines()
    {
        removePipeline(pRenderer, pPipelineClearBuffers);
        removePipeline(pRenderer, pPipelineTriangleFiltering);

        for (uint32_t i = 0; i < gNumGeomSets; ++i)
        {
            removePipeline(pRenderer, pPipelineVBBufferPass[i]);
        }
        removePipeline(pRenderer, pPipelineVBShadeSrgb);

        removePipeline(pRenderer, pSkyboxPipeline);
        removePipeline(pRenderer, pPPR_ProjectionPipeline);
        removePipeline(pRenderer, pPPR_ReflectionPipeline);
        removePipeline(pRenderer, pPPR_HolePatchingPipeline);
        if (gSSSRSupported)
        {
            removePipeline(pRenderer, pSPDPipeline);
            removePipeline(pRenderer, pCopyDepthPipeline);
            removePipeline(pRenderer, pGenerateMipPipeline);
            removePipeline(pRenderer, pSSSR_ClassifyTilesPipeline);
            removePipeline(pRenderer, pSSSR_PrepareIndirectArgsPipeline);
            removePipeline(pRenderer, pSSSR_IntersectPipeline);
            removePipeline(pRenderer, pSSSR_ResolveSpatialPipeline);
            removePipeline(pRenderer, pSSSR_ResolveTemporalPipeline);
            removePipeline(pRenderer, pSSSR_ResolveEAWPipeline);
            removePipeline(pRenderer, pSSSR_ResolveEAWStride2Pipeline);
            removePipeline(pRenderer, pSSSR_ResolveEAWStride4Pipeline);
        }
    }

    void addRenderTargets()
    {
        // layout 0 is now empty: the VB RT no longer requests ESRAM (see addVisibilityBuffer). Both layouts are
        // opened at offset 0, so anything placed here would share physical ESRAM pages with layout 1 below.
        TF_ESRAM_BEGIN_ALLOC(pRenderer, "layout 0", 0);
        VERIFY(addVisibilityBuffer());
        TF_ESRAM_END_ALLOC(pRenderer);

        // The Reflection Buffer at 1920x1080 is 16MB; with layout 0 empty it, the depth buffer and the SSSR ray
        // length target chain within this single layout and nothing overlaps.
        TF_ESRAM_BEGIN_ALLOC(pRenderer, "layout 1", 0);
        VERIFY(addReflectionBuffer());
        VERIFY(addDepthBuffer());
        VERIFY(addSceneBuffer());
        VERIFY(addNormalRoughnessBuffer());
        VERIFY(addIntermediateBuffer());
        TF_ESRAM_END_ALLOC(pRenderer);
    }

    void removeRenderTargets()
    {
        for (uint32_t i = 1; i < pSSSR_DepthHierarchy->mMipLevels; ++i)
        {
            removeResource(pSSSR_GenMipsBuffers[i]);
        }
        removeRenderTarget(pRenderer, pRenderTargetVBPass);
        removeRenderTarget(pRenderer, pDepthBuffer);
        removeRenderTarget(pRenderer, pSceneBuffer);

        for (uint8_t i = 0; i < gDataBufferCount; ++i)
        {
            removeRenderTarget(pRenderer, pNormalRoughnessBuffers[i]);
            removeRenderTarget(pRenderer, pSSSR_TemporalResults[i]);
        }

        removeRenderTarget(pRenderer, pReflectionBuffer);
        removeResource(pIntermediateBuffer);
        removeResource(pSSSR_DepthHierarchy);
        removeResource(pSSSR_TemporalVariance);
        removeRenderTarget(pRenderer, pSSSR_RayLength);
        removeResource(pSSSR_RayListBuffer);
        removeResource(pSSSR_TileListBuffer);
    }

    bool addVisibilityBuffer()
    {
        TFRenderTargetDesc vbRTDesc = {};
        vbRTDesc.mArraySize = 1;
        vbRTDesc.mClearValue = { { 1.0f, 1.0f, 1.0f, 1.0f } };
        vbRTDesc.mDepth = 1;
        vbRTDesc.mDescriptors = TF_DESCRIPTOR_TYPE_TEXTURE;
        vbRTDesc.mFormat = TinyImageFormat_R8G8B8A8_UNORM;
        vbRTDesc.mStartState = TF_RESOURCE_STATE_SHADER_RESOURCE;
        vbRTDesc.mWidth = gSceneRes.mWidth;
        vbRTDesc.mHeight = gSceneRes.mHeight;
        vbRTDesc.mSampleCount = TF_SAMPLE_COUNT_1;
        vbRTDesc.mSampleQuality = 0;
        vbRTDesc.mFlags = TF_TEXTURE_CREATION_FLAG_VR_MULTIVIEW;
        vbRTDesc.pName = "VB RT";
        addRenderTarget(pRenderer, &vbRTDesc, &pRenderTargetVBPass);

        return pRenderTargetVBPass != NULL;
    }

    bool addSceneBuffer()
    {
        TFRenderTargetDesc sceneRT = {};
        sceneRT.mArraySize = 1;
        sceneRT.mClearValue = { { 0.0f, 0.0f, 0.0f, 0.0f } };
        sceneRT.mDepth = 1;
        sceneRT.mDescriptors = TF_DESCRIPTOR_TYPE_TEXTURE;
        sceneRT.mFormat = TinyImageFormat_R16G16B16A16_SFLOAT;
        sceneRT.mStartState = TF_RESOURCE_STATE_SHADER_RESOURCE;
        sceneRT.mHeight = gSceneRes.mHeight;
        sceneRT.mWidth = gSceneRes.mWidth;
        sceneRT.mSampleCount = TF_SAMPLE_COUNT_1;
        sceneRT.mSampleQuality = 0;
        sceneRT.mFlags = TF_TEXTURE_CREATION_FLAG_VR_MULTIVIEW;
        sceneRT.mFlags |= TF_TEXTURE_CREATION_FLAG_DCC | TF_TEXTURE_CREATION_FLAG_TEXTURE_COMPATABILITY;
        sceneRT.pName = "Scene Buffer";

        addRenderTarget(pRenderer, &sceneRT, &pSceneBuffer);

        return pSceneBuffer != NULL;
    }

    bool addNormalRoughnessBuffer()
    {
        TFRenderTargetDesc desc = {};
        desc.mArraySize = 1;
        desc.mClearValue = { { 0.0f, 0.0f, 0.0f, 1.0f } };
        desc.mDepth = 1;
        desc.mDescriptors = TF_DESCRIPTOR_TYPE_TEXTURE;
        desc.mFormat = TinyImageFormat_R16G16B16A16_SFLOAT;
        desc.mStartState = TF_RESOURCE_STATE_SHADER_RESOURCE;
        desc.mHeight = gSceneRes.mHeight;
        desc.mWidth = gSceneRes.mWidth;
        desc.mSampleCount = TF_SAMPLE_COUNT_1;
        desc.mSampleQuality = 0;
        desc.mFlags = TF_TEXTURE_CREATION_FLAG_VR_MULTIVIEW;
        desc.mFlags |= TF_TEXTURE_CREATION_FLAG_DCC | TF_TEXTURE_CREATION_FLAG_TEXTURE_COMPATABILITY;
        desc.pName = "Normal Roughness Buffer";

        for (uint8_t i = 0; i < gDataBufferCount; ++i)
        {
            addRenderTarget(pRenderer, &desc, &pNormalRoughnessBuffers[i]);

            if (pNormalRoughnessBuffers[i] == NULL)
                return false;
        }

        return true;
    }

    bool addReflectionBuffer()
    {
        TFRenderTargetDesc RT = {};
        RT.mArraySize = 1;
        RT.mClearValue = { { 0.0f, 0.0f, 0.0f, 0.0f } };
        RT.mDepth = 1;
        RT.mDescriptors = TF_DESCRIPTOR_TYPE_TEXTURE | TF_DESCRIPTOR_TYPE_RW_TEXTURE;
        RT.mFormat = TinyImageFormat_R16G16B16A16_SFLOAT;
        RT.mStartState = TF_RESOURCE_STATE_SHADER_RESOURCE;
        RT.mHeight = gSceneRes.mHeight;
        RT.mWidth = gSceneRes.mWidth;
        RT.mSampleCount = TF_SAMPLE_COUNT_1;
        RT.mSampleQuality = 0;
        RT.mFlags = TF_TEXTURE_CREATION_FLAG_ESRAM | TF_TEXTURE_CREATION_FLAG_VR_MULTIVIEW;
        RT.mFlags |= TF_TEXTURE_CREATION_FLAG_DCC | TF_TEXTURE_CREATION_FLAG_TEXTURE_COMPATABILITY;
        RT.pName = "Reflection Buffer";

        addRenderTarget(pRenderer, &RT, &pReflectionBuffer);

        return pReflectionBuffer != NULL;
    }

    bool addDepthBuffer()
    {
        // Add depth buffer
        TFRenderTargetDesc depthRT = {};
        depthRT.mArraySize = 1;
        depthRT.mClearValue = { { 0.0f, 0 } };
        depthRT.mDepth = 1;
        depthRT.mDescriptors = TF_DESCRIPTOR_TYPE_TEXTURE;
        depthRT.mFormat = TinyImageFormat_D32_SFLOAT;
        depthRT.mStartState = TF_RESOURCE_STATE_SHADER_RESOURCE;
        depthRT.mHeight = gSceneRes.mHeight;
        depthRT.mSampleCount = TF_SAMPLE_COUNT_1;
        depthRT.mSampleQuality = 0;
        depthRT.mWidth = gSceneRes.mWidth;
        depthRT.mFlags = TF_TEXTURE_CREATION_FLAG_ESRAM | TF_TEXTURE_CREATION_FLAG_VR_MULTIVIEW;
        depthRT.mFlags |= TF_TEXTURE_CREATION_FLAG_TEXTURE_COMPATABILITY;
        depthRT.pName = "Depth Buffer";
        addRenderTarget(pRenderer, &depthRT, &pDepthBuffer);

        return pDepthBuffer != NULL;
    }

    bool addIntermediateBuffer()
    {
        int bufferSize = gSceneRes.mWidth * gSceneRes.mHeight * pSwapChain->ppRenderTargets[0]->mArraySize;

        // Add Intermediate buffer
        TFBufferLoadDesc IntermediateBufferDesc = {};
        IntermediateBufferDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_RW_BUFFER | TF_DESCRIPTOR_TYPE_BUFFER;
        IntermediateBufferDesc.mDesc.mElementCount = bufferSize;
        IntermediateBufferDesc.mDesc.mStructStride = sizeof(uint32_t);
        IntermediateBufferDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_GPU_ONLY;
        IntermediateBufferDesc.mDesc.mStartState = TF_RESOURCE_STATE_UNORDERED_ACCESS;
        IntermediateBufferDesc.mDesc.mSize = IntermediateBufferDesc.mDesc.mStructStride * bufferSize;
        IntermediateBufferDesc.mDesc.pName = "PPR Intermediate buffer";

        gInitializeVal = (uint32_t*)tf_realloc(gInitializeVal, IntermediateBufferDesc.mDesc.mSize);
        memset(gInitializeVal, 255, IntermediateBufferDesc.mDesc.mSize);

        TFSyncToken token = {};
        IntermediateBufferDesc.pData = gInitializeVal;
        IntermediateBufferDesc.ppBuffer = &pIntermediateBuffer;
        addResource(&IntermediateBufferDesc, &token);

        if (pIntermediateBuffer == NULL)
            return false;
        waitForToken(&token);

        TFTextureDesc depthHierarchyDesc = {};
        depthHierarchyDesc.mArraySize = 1;
        depthHierarchyDesc.mDepth = 1;
        depthHierarchyDesc.mFormat = TinyImageFormat_R32_SFLOAT;
        depthHierarchyDesc.mHeight = gSceneRes.mHeight;
        depthHierarchyDesc.mWidth = gSceneRes.mWidth;
        depthHierarchyDesc.mMipLevels =
            static_cast<uint32_t>(log2(gSceneRes.mWidth > gSceneRes.mHeight ? gSceneRes.mWidth : gSceneRes.mHeight)) + 1;
        depthHierarchyDesc.mSampleCount = TF_SAMPLE_COUNT_1;
        depthHierarchyDesc.mStartState = TF_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        depthHierarchyDesc.mDescriptors = TF_DESCRIPTOR_TYPE_RW_TEXTURE | TF_DESCRIPTOR_TYPE_TEXTURE;
        depthHierarchyDesc.mFlags = TF_TEXTURE_CREATION_FLAG_VR_MULTIVIEW;
        depthHierarchyDesc.pName = "SSSR_DepthHierarchy";

        TFTextureLoadDesc depthHierarchyLoadDesc = {};
        depthHierarchyLoadDesc.pDesc = &depthHierarchyDesc;
        depthHierarchyLoadDesc.ppTexture = &pSSSR_DepthHierarchy;
        addResource(&depthHierarchyLoadDesc, NULL);

        if (pSSSR_DepthHierarchy == NULL)
            return false;

        uint32_t mipSizeX = 1 << (uint32_t)ceil(log2((float)pDepthBuffer->mWidth));
        uint32_t mipSizeY = 1 << (uint32_t)ceil(log2((float)pDepthBuffer->mHeight));
        uint     mipSizes[GENERATE_MIPS_MAX_MIPS][2] = {};
        for (uint32_t i = 1; i < pSSSR_DepthHierarchy->mMipLevels; ++i)
        {
            mipSizeX >>= 1;
            mipSizeY >>= 1;
            mipSizes[i][0] = mipSizeX;
            mipSizes[i][1] = mipSizeY;
            TFBufferLoadDesc bufferDesc = {};
            bufferDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            bufferDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
            bufferDesc.mDesc.mSize = sizeof(uint32_t) * 2;
            bufferDesc.mDesc.mFlags = TF_BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
            bufferDesc.mDesc.pName = "Mip generations uniforms";
            bufferDesc.pData = mipSizes[i];
            bufferDesc.ppBuffer = &pSSSR_GenMipsBuffers[i];
            addResource(&bufferDesc, NULL);
        }

        TFRenderTargetDesc intersectResultsDesc = {};
        intersectResultsDesc.mArraySize = 1;
        intersectResultsDesc.mDepth = 1;
        intersectResultsDesc.mFormat = TinyImageFormat_R16G16B16A16_SFLOAT;
        intersectResultsDesc.mHeight = gSceneRes.mHeight;
        intersectResultsDesc.mWidth = gSceneRes.mWidth;
        intersectResultsDesc.mMipLevels = 1;
        intersectResultsDesc.mSampleCount = TF_SAMPLE_COUNT_1;
        intersectResultsDesc.mStartState = TF_RESOURCE_STATE_UNORDERED_ACCESS;
        intersectResultsDesc.mDescriptors = TF_DESCRIPTOR_TYPE_RW_TEXTURE | TF_DESCRIPTOR_TYPE_TEXTURE;
        intersectResultsDesc.pName = "pSSSR_TemporalResults";
        intersectResultsDesc.mClearValue = { { 0.0f, 0.0f, 0.0f, 0.0f } };
        intersectResultsDesc.mFlags = TF_TEXTURE_CREATION_FLAG_VR_MULTIVIEW;
        intersectResultsDesc.mFlags |= TF_TEXTURE_CREATION_FLAG_DCC | TF_TEXTURE_CREATION_FLAG_TEXTURE_COMPATABILITY;

        for (uint8_t i = 0; i < gDataBufferCount; ++i)
        {
            addRenderTarget(pRenderer, &intersectResultsDesc, &pSSSR_TemporalResults[i]);

            if (pSSSR_TemporalResults[i] == NULL)
                return false;
        }

        const uint32_t capMask = TF_FORMAT_CAP_READ | TF_FORMAT_CAP_WRITE;
        const bool     isR16SFSupported = (pRenderer->pGpu->mFormatCaps[TinyImageFormat_R16_SFLOAT] & capMask) == capMask;

        TFTextureDesc temporalVarianceDesc = {};
        temporalVarianceDesc.mArraySize = 1;
        temporalVarianceDesc.mDepth = 1;
        temporalVarianceDesc.mFormat = isR16SFSupported ? TinyImageFormat_R16_SFLOAT : TinyImageFormat_R32_SFLOAT;
        temporalVarianceDesc.mHeight = gSceneRes.mHeight;
        temporalVarianceDesc.mWidth = gSceneRes.mWidth;
        temporalVarianceDesc.mMipLevels = 1;
        temporalVarianceDesc.mSampleCount = TF_SAMPLE_COUNT_1;
        temporalVarianceDesc.mStartState = TF_RESOURCE_STATE_UNORDERED_ACCESS;
        temporalVarianceDesc.mDescriptors = TF_DESCRIPTOR_TYPE_RW_TEXTURE | TF_DESCRIPTOR_TYPE_TEXTURE;
        temporalVarianceDesc.mFlags = TF_TEXTURE_CREATION_FLAG_VR_MULTIVIEW;
        temporalVarianceDesc.pName = "SSSR_TemporalVariance";

        TFTextureLoadDesc temporalVarianceLoadDesc = {};
        temporalVarianceLoadDesc.pDesc = &temporalVarianceDesc;
        temporalVarianceLoadDesc.ppTexture = &pSSSR_TemporalVariance;
        addResource(&temporalVarianceLoadDesc, NULL);

        if (pSSSR_TemporalVariance == NULL)
            return false;

        TFRenderTargetDesc rayLengthDesc = {};
        rayLengthDesc.mArraySize = 1;
        rayLengthDesc.mDepth = 1;
        rayLengthDesc.mFormat = isR16SFSupported ? TinyImageFormat_R16_SFLOAT : TinyImageFormat_R32_SFLOAT;
        rayLengthDesc.mHeight = gSceneRes.mHeight;
        rayLengthDesc.mWidth = gSceneRes.mWidth;
        rayLengthDesc.mMipLevels = 1;
        rayLengthDesc.mSampleCount = TF_SAMPLE_COUNT_1;
        rayLengthDesc.mStartState = TF_RESOURCE_STATE_UNORDERED_ACCESS;
        rayLengthDesc.mDescriptors = TF_DESCRIPTOR_TYPE_RW_TEXTURE | TF_DESCRIPTOR_TYPE_TEXTURE;
        rayLengthDesc.pName = "SSSR_RayLength";
        rayLengthDesc.mClearValue = { { 0.0f, 0.0f, 0.0f, 0.0f } };
        rayLengthDesc.mFlags = TF_TEXTURE_CREATION_FLAG_VR_MULTIVIEW;
        addRenderTarget(pRenderer, &rayLengthDesc, &pSSSR_RayLength);

        if (pSSSR_RayLength == NULL)
            return false;

        const uint32_t rayListElementCount = (((uint32_t)bufferSize + 63u) / 64u) * 64u;
        uint32_t*      pZeroRayList = (uint32_t*)tf_calloc(rayListElementCount, sizeof(uint32_t));

        TFBufferLoadDesc SSSR_RayListDesc = {};
        SSSR_RayListDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_RW_BUFFER | TF_DESCRIPTOR_TYPE_BUFFER;
        SSSR_RayListDesc.mDesc.mElementCount = rayListElementCount;
        SSSR_RayListDesc.mDesc.mStructStride = sizeof(uint32_t);
        SSSR_RayListDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_GPU_ONLY;
        SSSR_RayListDesc.mDesc.mFlags = TF_BUFFER_CREATION_FLAG_NONE;
        SSSR_RayListDesc.mDesc.mSize = SSSR_RayListDesc.mDesc.mStructStride * rayListElementCount;
        SSSR_RayListDesc.mDesc.pName = "SSSR_RayListBuffer";
        SSSR_RayListDesc.mDesc.mStartState = TF_RESOURCE_STATE_UNORDERED_ACCESS;
        SSSR_RayListDesc.pData = pZeroRayList;
        SSSR_RayListDesc.ppBuffer = &pSSSR_RayListBuffer;
        addResource(&SSSR_RayListDesc, &token);
        waitForToken(&token);
        tf_free(pZeroRayList);

        if (pSSSR_RayListBuffer == NULL)
            return false;

        const uint32_t tileListElementCount =
            ((gSceneRes.mWidth + 7u) / 8u) * ((gSceneRes.mHeight + 7u) / 8u) * pSwapChain->ppRenderTargets[0]->mArraySize;
        uint32_t*        pZeroTileList = (uint32_t*)tf_calloc(tileListElementCount, sizeof(uint32_t));
        TFBufferLoadDesc SSSR_TileListDesc = {};
        SSSR_TileListDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_RW_BUFFER | TF_DESCRIPTOR_TYPE_BUFFER;
        SSSR_TileListDesc.mDesc.mElementCount = tileListElementCount;
        SSSR_TileListDesc.mDesc.mStructStride = sizeof(uint32_t);
        SSSR_TileListDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_GPU_ONLY;
        SSSR_TileListDesc.mDesc.mFlags = TF_BUFFER_CREATION_FLAG_NONE;
        SSSR_TileListDesc.mDesc.mSize = SSSR_TileListDesc.mDesc.mStructStride * SSSR_TileListDesc.mDesc.mElementCount;
        SSSR_TileListDesc.mDesc.pName = "SSSR_TileListBuffer";
        SSSR_TileListDesc.mDesc.mStartState = TF_RESOURCE_STATE_UNORDERED_ACCESS;
        SSSR_TileListDesc.pData = pZeroTileList;
        SSSR_TileListDesc.ppBuffer = &pSSSR_TileListBuffer;
        addResource(&SSSR_TileListDesc, &token);
        waitForToken(&token);
        tf_free(pZeroTileList);

        return pSSSR_TileListBuffer != NULL;
    }

    void updateUI()
    {
        if (UI_WINDOW_IS_VISIBLE(uiBeginWidgetWindow(&gGuiDesc)))
        {
            static const char* enumRenderModeNames[] = { "Render Scene Only", "Render Reflections Only", "Render Scene with Reflections",
                                                         "Render Scene with exclusive Reflections" };

            static const char* enumReflectionTypeNames[] = { "Pixel Projected Reflections", "Stochastic Screen Space Reflections" };

            const int enumRenderModeNamesCount = sizeof(enumRenderModeNames) / sizeof(enumRenderModeNames[0]);
            const int enumReflectionTypeNamesCount = sizeof(enumReflectionTypeNames) / sizeof(enumReflectionTypeNames[0]);
            const int testScriptsCount = sizeof(gTestScripts) / sizeof(gTestScripts[0]);

            uiLayoutAutoTextRows(2);
            uiLabel("Render Mode", TF_ALIGN_LEFT);
            gRenderMode = UI_WIDGET_GET_SELECTED(uiDropdown(enumRenderModeNames, enumRenderModeNamesCount, gRenderMode));

            uiLabel("Reflection Type", TF_ALIGN_LEFT);
            gReflectionType = UI_WIDGET_GET_SELECTED(uiDropdown(enumReflectionTypeNames, enumReflectionTypeNamesCount, gReflectionType));

            uiLayoutAutoRows(2);
            uiLabel("Scene roughness", TF_ALIGN_LEFT);
            uiSliderFloat(&gUniformDataExtenedCamera.mOverrideRoughness, 0.0f, 0.1f, 0.01f);
            uiLabel("Scene metallic", TF_ALIGN_LEFT);
            uiSliderFloat(&gUniformDataExtenedCamera.mOverrideMetallic, 0.0f, 1.0f, 0.01f);

            uiLayoutAutoTextRows(1);
            uiCheckbox("Use Env map", &gUseEnvMap);
            uiLayoutAutoTextRows(2);
            uiLabel("Env color", TF_ALIGN_LEFT);
            uiColor3Button(&gUniformDataExtenedCamera.mEnvColor);

            if (gReflectionType == PP_REFLECTION)
            {
                uiLayoutAutoTextRows(1);
                uiCheckbox("Use Holepatching", &gUseHolePatching);
                uiCheckbox("Use Expensive Holepatching", &gUseExpensiveHolePatching);
                uiCheckbox("Use Fade Effect", &gUseFadeEffect);

                uiLayoutAutoTextRows(2);
                uiLabel("Intensity of PPR", TF_ALIGN_LEFT);
                uiSliderFloat(&gRRP_Intensity, 0.0f, 1.0f, 0.01f);

                uiLabel("Number of Planes", TF_ALIGN_LEFT);
                uiSliderUint(&gPlaneNumber, 1, 4, 1);
                uiLabel("Size of Main Plane", TF_ALIGN_LEFT);
                uiSliderFloat(&gPlaneSize, 5.0f, 500.0f, 1.0f);
                uiLabel("Rotation of Non-Main Planes", TF_ALIGN_LEFT);
                uiSliderFloat(&gPlaneRotationOffset, -180.0f, 180.0f, 1.0f);

                uiLayoutAutoTextRows(1);
                uiCheckbox("Debug Non Projected Pixels", &gDebugNonProjectedPixels);
            }
            else if (gReflectionType == SSS_REFLECTION)
            {
                if (gSSSRSupported)
                {
                    uiLayoutAutoTextRows(1);
                    uiCheckbox("Use Singlepass Downsampler", &gUseSPD);
                    uiCheckbox("Skip Denoiser", &gSSSR_SkipDenoiser);

                    uiLayoutAutoTextRows(2);
                    uiLabel("Max Traversal Iterations", TF_ALIGN_LEFT);
                    uiSliderUint(&gSSSR_MaxTravelsalIntersections, 0, 256, 1);
                    uiLabel("Min Traversal Occupancy", TF_ALIGN_LEFT);
                    uiSliderUint(&gSSSR_MinTravelsalOccupancy, 0, 32, 1);
                    uiLabel("Most Detailed Level", TF_ALIGN_LEFT);
                    uiSliderUint(&gSSSR_MostDetailedMip, 0, 5, 1);

                    uiLabel("Depth Buffer Thickness", TF_ALIGN_LEFT);
                    uiSliderFloat(&gSSSR_DepthThickness, 0.0f, 0.3f, 0.01f);
                    uiLabel("Roughness Threshold", TF_ALIGN_LEFT);
                    uiSliderFloat(&gSSSR_RougnessThreshold, 0.0f, 1.0f, 0.01f);
                    uiLabel("Temporal Stability", TF_ALIGN_LEFT);
                    uiSliderFloat(&pSSSR_TemporalStability, 0.0f, 1.0f, 0.01f);

                    uiLayoutAutoTextRows(1);
                    uiCheckbox("Enable Variance Guided Tracing", &gSSSR_TemporalVarianceEnabled);

                    bool is1Sample = gSSSR_SamplesPerQuad == 1;
                    if (UI_WIDGET_IS_CHANGED(uiRadioButton("1 Sample Per Quad", &is1Sample)))
                    {
                        gSSSR_SamplesPerQuad = 1;
                    }
                    bool is2Sample = gSSSR_SamplesPerQuad == 2;
                    if (UI_WIDGET_IS_CHANGED(uiRadioButton("2 Sample Per Quad", &is2Sample)))
                    {
                        gSSSR_SamplesPerQuad = 2;
                    }
                    bool is4Sample = gSSSR_SamplesPerQuad == 4;
                    if (UI_WIDGET_IS_CHANGED(uiRadioButton("4 Sample Per Quad", &is4Sample)))
                    {
                        gSSSR_SamplesPerQuad = 4;
                    }

                    bool is1EAWPass = gSSSR_EAWPassCount == 1;
                    if (UI_WIDGET_IS_CHANGED(uiRadioButton("1 EAW Pass", &is1EAWPass)))
                    {
                        gSSSR_EAWPassCount = 1;
                    }
                    bool is3EAWPass = gSSSR_EAWPassCount == 3;
                    if (UI_WIDGET_IS_CHANGED(uiRadioButton("3 EAW Pass", &is3EAWPass)))
                    {
                        gSSSR_EAWPassCount = 3;
                    }
                }
                else
                {
                    uiLayoutAutoTextRows(1);
                    uiLabel("Not supported by your GPU", TF_ALIGN_LEFT);
                }
            }

            uiLayoutAutoTextRows(2);
            uiLabel("Test Scripts", TF_ALIGN_LEFT);
            gCurrentScriptIndex = UI_WIDGET_GET_SELECTED(uiDropdown(gTestScripts, testScriptsCount, gCurrentScriptIndex));

            if (UI_WIDGET_IS_PRESSED(uiButton("Run")))
            {
                RunScript(NULL);
            }
        }
        uiEndWidgetWindow();
    }

    void luaRegisterUI()
    {
        TFLuaWidgetVariableDesc luaVarDesc = {};
        TFLuaWidgetFunctionDesc luaFuncDesc = {};

        // Dropdowns
        luaVarDesc.mWidgetType = TF_WIDGET_TYPE_DROPDOWN;
        luaVarDesc.pLabel = "Render Mode";
        luaVarDesc.pUint = &gRenderMode;
        luaRegisterWidgetVariable(&luaVarDesc);
        luaVarDesc.pLabel = "Reflection Type";
        luaVarDesc.pUint = &gReflectionType;
        luaRegisterWidgetVariable(&luaVarDesc);
        luaVarDesc.pLabel = "Test Scripts";
        luaVarDesc.pUint = &gCurrentScriptIndex;
        luaRegisterWidgetVariable(&luaVarDesc);

        // float sliders
        luaVarDesc.mWidgetType = TF_WIDGET_TYPE_SLIDER_FLOAT;
        luaVarDesc.pLabel = "Override roughness";
        luaVarDesc.pFloat = &gUniformDataExtenedCamera.mOverrideRoughness;
        luaRegisterWidgetVariable(&luaVarDesc);
        luaVarDesc.pLabel = "Override metallic";
        luaVarDesc.pFloat = &gUniformDataExtenedCamera.mOverrideMetallic;
        luaRegisterWidgetVariable(&luaVarDesc);
        luaVarDesc.pLabel = "Intensity of PPR";
        luaVarDesc.pFloat = &gRRP_Intensity;
        luaRegisterWidgetVariable(&luaVarDesc);
        luaVarDesc.pLabel = "Size of Main Plane";
        luaVarDesc.pFloat = &gPlaneSize;
        luaRegisterWidgetVariable(&luaVarDesc);
        luaVarDesc.pLabel = "Rotation of Non-Main Planes";
        luaVarDesc.pFloat = &gPlaneRotationOffset;
        luaRegisterWidgetVariable(&luaVarDesc);

        // uint sliders
        luaVarDesc.mWidgetType = TF_WIDGET_TYPE_SLIDER_UINT;
        luaVarDesc.pLabel = "Number of Planes";
        luaVarDesc.pUint = &gPlaneNumber;
        luaRegisterWidgetVariable(&luaVarDesc);

        // Checkboxes
        luaVarDesc.mWidgetType = TF_WIDGET_TYPE_CHECKBOX;
        luaVarDesc.pLabel = "Use Env map";
        luaVarDesc.pBool = &gUseEnvMap;
        luaRegisterWidgetVariable(&luaVarDesc);
        luaVarDesc.pLabel = "Override roughness metallic";
        luaVarDesc.pBool = &gOverrideRoughnessMetallic;
        luaRegisterWidgetVariable(&luaVarDesc);
        luaVarDesc.pLabel = "Use Holepatching";
        luaVarDesc.pBool = &gUseHolePatching;
        luaRegisterWidgetVariable(&luaVarDesc);
        luaVarDesc.pLabel = "Use Expensive Holepatching";
        luaVarDesc.pBool = &gUseExpensiveHolePatching;
        luaRegisterWidgetVariable(&luaVarDesc);
        luaVarDesc.pLabel = "Use Fade Effect";
        luaVarDesc.pBool = &gUseFadeEffect;
        luaRegisterWidgetVariable(&luaVarDesc);
        luaVarDesc.pLabel = "Debug Non Projected Pixels";
        luaVarDesc.pBool = &gDebugNonProjectedPixels;
        luaRegisterWidgetVariable(&luaVarDesc);

        // color3 pickers
        luaVarDesc.mWidgetType = TF_WIDGET_TYPE_COLOR3_PICKER;
        luaVarDesc.pLabel = "Env color";
        luaVarDesc.pFloat3 = &gUniformDataExtenedCamera.mEnvColor;
        luaRegisterWidgetVariable(&luaVarDesc);

        if (gSSSRSupported)
        {
            // Checkboxes
            luaVarDesc.mWidgetType = TF_WIDGET_TYPE_CHECKBOX;
            luaVarDesc.pLabel = "Use Singlepass Downsampler";
            luaVarDesc.pBool = &gUseSPD;
            luaRegisterWidgetVariable(&luaVarDesc);
            luaVarDesc.pLabel = "Show Intersection Results";
            luaVarDesc.pBool = &gSSSR_SkipDenoiser;
            luaRegisterWidgetVariable(&luaVarDesc);
            luaVarDesc.pLabel = "Enable Variance Guided Tracing";
            luaVarDesc.pBool = &gSSSR_TemporalVarianceEnabled;
            luaRegisterWidgetVariable(&luaVarDesc);

            // uint sliders
            luaVarDesc.mWidgetType = TF_WIDGET_TYPE_SLIDER_UINT;
            luaVarDesc.pLabel = "Max Traversal Iterations";
            luaVarDesc.pUint = &gSSSR_MaxTravelsalIntersections;
            luaRegisterWidgetVariable(&luaVarDesc);
            luaVarDesc.pLabel = "Min Traversal Occupancy";
            luaVarDesc.pUint = &gSSSR_MinTravelsalOccupancy;
            luaRegisterWidgetVariable(&luaVarDesc);
            luaVarDesc.pLabel = "Most Detailed Level";
            luaVarDesc.pUint = &gSSSR_MostDetailedMip;
            luaRegisterWidgetVariable(&luaVarDesc);

            // float sliders
            luaVarDesc.mWidgetType = TF_WIDGET_TYPE_SLIDER_FLOAT;
            luaVarDesc.pLabel = "Depth Buffer Thickness";
            luaVarDesc.pFloat = &gSSSR_DepthThickness;
            luaRegisterWidgetVariable(&luaVarDesc);
            luaVarDesc.pLabel = "Roughness Threshold";
            luaVarDesc.pFloat = &gSSSR_RougnessThreshold;
            luaRegisterWidgetVariable(&luaVarDesc);
            luaVarDesc.pLabel = "Temporal Stability";
            luaVarDesc.pFloat = &pSSSR_TemporalStability;
            luaRegisterWidgetVariable(&luaVarDesc);
        }

        // Button functions
        luaFuncDesc.mType = TF_LUA_WIDGET_FUNCTION_ON_EDITED;
        luaFuncDesc.pLabel = "Run"; // script
        luaFuncDesc.pFuncData = NULL;
        luaFuncDesc.pFunc = RunScript;
        luaRegisterWidgetFunction(&luaFuncDesc);
    }
};

DEFINE_APPLICATION_MAIN(ScreenSpaceReflections)
