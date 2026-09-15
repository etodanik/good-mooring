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

// Unit Test to create Bottom and Top Level Acceleration Structures using Raytracing API.

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

// Raytracing
#include "../../../../Common_3/Graphics/Interfaces/IRay.h"

// Math
#include "../../../../Common_3/Utilities/Interfaces/IMath.h"

// fsl

#define NO_FSL_DEFINITIONS
#include "../../../../Common_3/Graphics/FSL/defaults.h"
#include "Shaders/FSL/Shared.fsl.h"
#include "Shaders/FSL/Global.srt.h"

#include "../../../../Common_3/Utilities/Interfaces/IMemory.h"

#define USE_DENOISER  0
#define USE_RAY_QUERY 1

#define SCENE_SCALE   10.0f

bool gUseUavRwFallback = false;

TFICamera* pCamera = NULL;

ProfileToken gGpuProfileToken;

TFTexture* gNullTextureResource = NULL;

enum RaytracingTechnique
{
    RAY_QUERY = 0,
    // #NOTE: Extend enum when you add new raytracing technique
    RAYTRACING_TECHNIQUE_COUNT
};
uint32_t gRaytracingTechnique = RAY_QUERY;
bool     gRaytracingTechniqueSupported[RAYTRACING_TECHNIQUE_COUNT] = {};

struct PropData
{
    TFGeometry*     pGeom = NULL;
    TFGeometryData* pGeomData = NULL;
    size_t          mTextureCount = 0;
    TFBuffer*       pIndexBufferOffsetStream = NULL; // one per geometry
    TFTexture**     pTextureStorage = NULL;
    mat4            mWorldMatrix;
};

PropData   SanMiguelProp;
TFPackage* pPackage;

TFFont* gFont;

struct PathTracingData
{
    mat4   mHistoryProjView;
    float3 mHistoryLightDirection;
    uint   mFrameIndex;
    uint   mHaltonIndex;
    uint   mLastCameraMoveFrame;
    mat4   mWorldToCamera;
    mat4   mProjMat;
    mat4   mProjectView;
    mat4   mCameraToWorld;
    float  mProjNear;
    float  mProjFarMinusNear;
    float2 mZ1PlaneSize;
    float  mRandomSeed;
};

/************************************************************************/
// Data
/************************************************************************/
// Two sets of resources (one in flight and one being used on CPU)
static const uint32_t gDataBufferCount = 2;

TFRenderer*              pRenderer = NULL;
TFRaytracing*            pRaytracing = NULL;
TFQueue*                 pQueue = NULL;
GpuCmdRing               mCmdRing = {};
TFBuffer*                pRayGenConfigBuffer[gDataBufferCount] = {};
TFAccelerationStructure* pSanMiguelBottomAS = NULL;
TFAccelerationStructure* pSanMiguelAS = NULL;
TFShader*                pShaderRayQuery = NULL;
TFShader*                pDisplayTextureShader = NULL;
TFDescriptorSet*         pDescriptorSetRayTracingPersistent[RAYTRACING_TECHNIQUE_COUNT] = {};
TFDescriptorSet*         pDescriptorSetRaytracingPerBatch[RAYTRACING_TECHNIQUE_COUNT] = {};
TFDescriptorSet*         pDescriptorSetRayTracingPerFrame[RAYTRACING_TECHNIQUE_COUNT] = {};
TFDescriptorSet*         pDescriptorSetPerFrame = NULL;
TFPipeline*              pPipeline[RAYTRACING_TECHNIQUE_COUNT] = {};
TFPipeline*              pDisplayTexturePipeline = NULL;
TFSwapChain*             pSwapChain = NULL;
TFTexture*               pComputeOutput = NULL;
TFSemaphore*             pImageAcquiredSemaphore[gDataBufferCount] = { NULL };
PathTracingData          mPathTracingData = {};
TFUIWindowDesc           gGuiWindowDesc = {};
float3                   mLightDirection = float3(0.2f, 1.8f, 0.1f);

#if USE_DENOISER
TFTexture*       pAlbedoTexture = NULL;
TFDescriptorSet* pDenoiserInputsDescriptorSet = NULL;
TFRenderTarget*  pDepthNormalRenderTarget[2] = {};
TFRenderTarget*  pMotionVectorRenderTarget = NULL;
TFRenderTarget*  pDepthRenderTarget = NULL;
TFRootSignature* pDenoiserInputsRootSignature = NULL;
TFShader*        pDenoiserInputsShader = NULL;
TFPipeline*      pDenoiserInputsPipeline = NULL;
TFSSVGFDenoiser* pDenoiser = NULL;
#endif

static float haltonSequence(uint index, uint base)
{
    float f = 1.f;
    float r = 0.f;

    while (index > 0)
    {
        f /= (float)base;
        r += f * (float)(index % base);
        index /= base;
    }

    return r;
}

class UnitTest_NativeRaytracing: public IApp
{
public:
    UnitTest_NativeRaytracing()
    {
#ifdef TARGET_IOS
        mSettings.mContentScaleFactor = 1.f;
#endif
        readCmdArgs();
    }

    bool Init()
    {
        /************************************************************************/
        // Initialize Raytracing
        /************************************************************************/
        TFRendererDesc settings = {};
        settings.mShaderTarget = TF_SHADER_TARGET_6_3;
#if defined(SHADER_STATS_AVAILABLE)
        settings.mEnableShaderStats = true;
#endif
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

        /************************************************************************/
        // Raytracing setup
        /************************************************************************/
        initRaytracing(pRenderer, &pRaytracing);
        gRaytracingTechniqueSupported[RAY_QUERY] = pRenderer->pGpu->mRayQuerySupported;

        gUseUavRwFallback = !(pRenderer->pGpu->mFormatCaps[TinyImageFormat_R16G16B16A16_SFLOAT] & TF_FORMAT_CAP_READ_WRITE);

        initResourceLoaderInterface(pRenderer);

        TFRootSignatureDesc rootDesc = {};
        INIT_RS_DESC(rootDesc, "default.rootsig", "compute.rootsig");
        initRootSignature(pRenderer, &rootDesc);

        TFQueueDesc queueDesc = {};
        queueDesc.mType = TF_QUEUE_TYPE_GRAPHICS;
        queueDesc.mFlag = TF_QUEUE_FLAG_INIT_MICROPROFILE;
        initQueue(pRenderer, &queueDesc, &pQueue);

        GpuCmdRingDesc cmdRingDesc = {};
        cmdRingDesc.pQueue = pQueue;
        cmdRingDesc.mPoolCount = gDataBufferCount;
        cmdRingDesc.mCmdPerPoolCount = 1;
        cmdRingDesc.mAddSyncPrimitives = true;
        initGpuCmdRing(pRenderer, &cmdRingDesc, &mCmdRing);

        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            initSemaphore(pRenderer, &pImageAcquiredSemaphore[i]);
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
        profiler.ppQueues = &pQueue;
        profiler.ppProfilerNames = ppGpuProfilerName;
        profiler.pProfileTokens = &gGpuProfileToken;
        profiler.mGpuProfilerCount = 1;
        initProfiler(&profiler);

        if (gRaytracingTechniqueSupported[RAY_QUERY])
        {
            TFVertexLayout vertexLayout = {};
            vertexLayout.mBindingCount = 3;
            vertexLayout.mAttribCount = 3;

            // Positions
            vertexLayout.mAttribs[0].mSemantic = TF_SEMANTIC_POSITION;
            vertexLayout.mAttribs[0].mFormat = TinyImageFormat_R32G32B32_SFLOAT;
            vertexLayout.mAttribs[0].mBinding = 0;
            vertexLayout.mAttribs[0].mLocation = 0;
            vertexLayout.mAttribs[0].mOffset = 0;

            // Normals
            vertexLayout.mAttribs[1].mSemantic = TF_SEMANTIC_NORMAL;
            vertexLayout.mAttribs[1].mFormat = TinyImageFormat_R32_UINT;
            vertexLayout.mAttribs[1].mLocation = 1;
            vertexLayout.mAttribs[1].mBinding = 1;
            vertexLayout.mAttribs[1].mOffset = 0;

            // Texture Coords
            vertexLayout.mAttribs[2].mSemantic = TF_SEMANTIC_TEXCOORD0;
            vertexLayout.mAttribs[2].mFormat = TinyImageFormat_R32_UINT;
            vertexLayout.mAttribs[2].mLocation = 2;
            vertexLayout.mAttribs[2].mBinding = 2;
            vertexLayout.mAttribs[2].mOffset = 0;

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
            packageLoadDesc.loadTexData = false;
            packageLoadDesc.geometryLoadFlags = TF_GEOMETRY_LOAD_FLAG_RAYTRACING_INPUT;
            packageLoadDesc.pGeoVertexLayout = &vertexLayout;
            packageLoadDesc.ppOutPackage = &pPackage;
            packageLoadDesc.packageName = "SanMiguelPak.buny";
            bool pakLoaded = addResourcesFromPackage(&packageLoadDesc, gNullTextureResource);

            if (!pakLoaded)
            {
                LOGF(LogLevel::eERROR, "Failed to Load the package");
                return false;
            }
            else
            {
                const uint32_t      sceneGeoIndex = 0;
                TFGeometryLoadDesc* pLoadDesc = &pPackage->pGeoData[sceneGeoIndex];
                pLoadDesc->ppGeometry = &SanMiguelProp.pGeom;
                pLoadDesc->ppGeometryData = &SanMiguelProp.pGeomData;
                TFSyncToken waitToken = {};
                addResource(pLoadDesc, &waitToken);
                waitForToken(&waitToken);
            }

            SanMiguelProp.mTextureCount = pPackage->pTextureMetadata->mMeshCount;
            SanMiguelProp.pTextureStorage = (TFTexture**)tf_malloc(sizeof(TFTexture*) * SanMiguelProp.mTextureCount);

            for (uint32_t i = 0; i < SanMiguelProp.mTextureCount; ++i)
            {
                TFTextureBundleID texBundle = pPackage->pTextureMetadata->pTextureBundleIDs[i];
                TFTextureID       colorTexID = GET_COLOR_TEXID(texBundle);
                if (colorTexID == TEX_IDX_INVALID)
                {
                    SanMiguelProp.pTextureStorage[i] = gNullTextureResource;
                    continue;
                }
                TFTextureLoadDesc* texDesc = &pPackage->pTextureData[colorTexID];
                texDesc->ppTexture = &SanMiguelProp.pTextureStorage[i];
                addResource(texDesc, NULL);
            }

            TFBufferLoadDesc desc = {};
            desc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_BUFFER_RAW;
            desc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_GPU_ONLY;
            desc.mDesc.mStartState = TF_RESOURCE_STATE_SHADER_RESOURCE;
            desc.mDesc.mSize = SanMiguelProp.pGeom->mDrawArgCount * sizeof(uint32_t);
            desc.mDesc.mElementCount = SanMiguelProp.pGeom->mDrawArgCount;
            desc.ppBuffer = &SanMiguelProp.pIndexBufferOffsetStream;
            addResource(&desc, NULL);

            TFBufferUpdateDesc updateDesc = {};
            updateDesc.pBuffer = SanMiguelProp.pIndexBufferOffsetStream;
            updateDesc.mCurrentState = desc.mDesc.mStartState;
            beginUpdateResource(&updateDesc);
            for (uint32_t i = 0, count = 0; i < SanMiguelProp.pGeom->mDrawArgCount; ++i)
            {
                ((uint32_t*)updateDesc.pMappedData)[count++] = SanMiguelProp.pGeom->pDrawArgs[i].mStartIndex;
            }
            endUpdateResource(&updateDesc);

            waitForAllResourceLoads();

            /************************************************************************/
            // Create Acceleration Structures
            /************************************************************************/
            TFAccelerationStructureDesc         asDesc = {};
            TFAccelerationStructureGeometryDesc geomDescs[1024] = {};

            for (uint32_t i = 0; i < pPackage->pTextureMetadata->mMeshCount; i++)
            {
                TFIndirectDrawIndexArguments& drawArg = SanMiguelProp.pGeom->pDrawArgs[i];
                TFMaterialFlags               materialFlag = pPackage->pTextureMetadata->pMaterialProps[i].mFlags;

                geomDescs[i].mFlags = (materialFlag & MATERIAL_FLAG_ALPHA_TESTED)
                                          ? TF_ACCELERATION_STRUCTURE_GEOMETRY_FLAG_NO_DUPLICATE_ANYHIT_INVOCATION
                                          : TF_ACCELERATION_STRUCTURE_GEOMETRY_FLAG_OPAQUE;
                geomDescs[i].pVertexBuffer = SanMiguelProp.pGeom->pVertexBuffers[0];
                geomDescs[i].mVertexCount = (uint32_t)SanMiguelProp.pGeom->mVertexCount;
                geomDescs[i].mVertexStride = SanMiguelProp.pGeom->mVertexStrides[0];
                geomDescs[i].mVertexFormat = TinyImageFormat_R32G32B32_SFLOAT;
                geomDescs[i].pIndexBuffer = SanMiguelProp.pGeom->pIndexBuffer;
                geomDescs[i].mIndexCount = drawArg.mIndexCount;
                geomDescs[i].mIndexOffset = drawArg.mStartIndex * sizeof(uint32_t);
                geomDescs[i].mIndexType = TF_INDEX_TYPE_UINT32;
            }

            asDesc.mBottom.mDescCount = SanMiguelProp.pGeom->mDrawArgCount;
            asDesc.mBottom.pGeometryDescs = geomDescs;
            asDesc.mType = TF_ACCELERATION_STRUCTURE_TYPE_BOTTOM;
            asDesc.mFlags = TF_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
            addAccelerationStructure(pRaytracing, &asDesc, &pSanMiguelBottomAS);

            // Transformation matrix for the instance
            SanMiguelProp.mWorldMatrix = mat4::scale(vec3(SCENE_SCALE));

            // Construct descriptions for Acceleration Structures Instances
            TFAccelerationStructureInstanceDesc instanceDesc = {};
            instanceDesc.mFlags = TF_ACCELERATION_STRUCTURE_INSTANCE_FLAG_NONE;
            instanceDesc.mInstanceContributionToHitGroupIndex = 0;
            instanceDesc.mInstanceID = 0;
            instanceDesc.mInstanceMask = 1;
            memcpy(instanceDesc.mTransform, &SanMiguelProp.mWorldMatrix, sizeof(float[12]));
            instanceDesc.pBottomAS = pSanMiguelBottomAS;

            asDesc = {};
            asDesc.mType = TF_ACCELERATION_STRUCTURE_TYPE_TOP;
            asDesc.mFlags = TF_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
            asDesc.mTop.mDescCount = 1;
            asDesc.mTop.pInstanceDescs = &instanceDesc;
            addAccelerationStructure(pRaytracing, &asDesc, &pSanMiguelAS);

            GpuCmdRingElement elem = getNextGpuCmdRingElement(&mCmdRing, true, 1);
            resetCmdPool(pRenderer, elem.pCmdPool);

            // Build Acceleration Structures
            TFRaytracingBuildASDesc buildASDesc = {};
            buildASDesc.pAccelerationStructure = pSanMiguelBottomAS;
            buildASDesc.mIssueRWBarrier = true;
            beginCmd(elem.pCmds[0]);
            cmdBuildAccelerationStructure(elem.pCmds[0], pRaytracing, &buildASDesc);

            buildASDesc = {};
            buildASDesc.pAccelerationStructure = pSanMiguelAS;
            cmdBuildAccelerationStructure(elem.pCmds[0], pRaytracing, &buildASDesc);

            endCmd(elem.pCmds[0]);

            TFQueueSubmitDesc submitDesc = {};
            submitDesc.mCmdCount = 1;
            submitDesc.ppCmds = elem.pCmds;
            submitDesc.pSignalFence = elem.pFence;
            submitDesc.mSubmitDone = true;
            queueSubmit(pQueue, &submitDesc);
            waitForFences(pRenderer, 1, &elem.pFence);

            removeAccelerationStructureScratch(pRaytracing, pSanMiguelBottomAS);
            removeAccelerationStructureScratch(pRaytracing, pSanMiguelAS);

            /************************************************************************/
            // Create Shader Binding Table to connect Pipeline with Acceleration Structure
            /************************************************************************/
            TFBufferLoadDesc ubDesc = {};
            ubDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            ubDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
            ubDesc.mDesc.mFlags = TF_BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
            ubDesc.mDesc.mSize = sizeof(ShadersConfigBlock);
            for (uint32_t i = 0; i < gDataBufferCount; i++)
            {
                ubDesc.ppBuffer = &pRayGenConfigBuffer[i];
                addResource(&ubDesc, NULL);
            }

#if USE_DENOISER
            addSSVGFDenoiser(pRenderer, &pDenoiser);
#endif
        }

        TFCameraMotionParameters cmp{ 200.0f, 250.0f, 300.0f };
        vec3                     camPos{ 80.0f, 60.0f, 50.0f };
        vec3                     lookAt{ 1.0f, 0.5f, 0.0f };

        pCamera = initFpsCamera(camPos, lookAt);
        pCamera->setMotionParameters(cmp);

        // App Actions
        AddCustomInputBindings();
        initScreenshotCapturer(pRenderer, pQueue, GetName());

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

        removeResource(gNullTextureResource);

        if (SanMiguelProp.pGeom)
        {
            removeResource(SanMiguelProp.pGeom);
            SanMiguelProp.pGeom = NULL;
        }

        if (SanMiguelProp.pGeomData)
        {
            removeResource(SanMiguelProp.pGeomData);
            SanMiguelProp.pGeomData = NULL;
        }

        if (SanMiguelProp.pIndexBufferOffsetStream)
        {
            removeResource(SanMiguelProp.pIndexBufferOffsetStream);
            SanMiguelProp.pIndexBufferOffsetStream = NULL;
        }

        if (SanMiguelProp.pTextureStorage)
        {
            for (uint32_t i = 0; i < SanMiguelProp.mTextureCount; ++i)
            {
                if (pPackage)
                {
                    TFTextureBundleID texBundle = pPackage->pTextureMetadata->pTextureBundleIDs[i];
                    TFTextureID       colorTexID = GET_COLOR_TEXID(texBundle);
                    if (colorTexID == TEX_IDX_INVALID)
                    {
                        continue;
                    }
                }
                removeResource(SanMiguelProp.pTextureStorage[i]);
                SanMiguelProp.pTextureStorage[i] = NULL;
            }
            tf_free(SanMiguelProp.pTextureStorage);
            SanMiguelProp.pTextureStorage = NULL;
        }

        if (pPackage)
        {
            removeResourcesFromPackage(pPackage);
            pPackage = NULL;
        }

        if (gRaytracingTechniqueSupported[RAY_QUERY])
        {
#if USE_DENOISER
            removeSSVGFDenoiser(pDenoiser);
#endif
            for (uint32_t i = 0; i < gDataBufferCount; i++)
            {
                removeResource(pRayGenConfigBuffer[i]);
            }

            removeAccelerationStructure(pRaytracing, pSanMiguelAS);
            removeAccelerationStructure(pRaytracing, pSanMiguelBottomAS);
        }

        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            exitSemaphore(pRenderer, pImageAcquiredSemaphore[i]);
        }
        exitGpuCmdRing(pRenderer, &mCmdRing);
        exitQueue(pRenderer, pQueue);
        exitRootSignature(pRenderer);
        exitResourceLoaderInterface(pRenderer);
        exitRaytracing(pRenderer, pRaytracing);
        exitRenderer(pRenderer);
        exitGPUConfig();
        pRenderer = NULL;
    }

    bool Load(TFReloadDesc* pReloadDesc)
    {
        UNREF_PARAM(pReloadDesc);

        mPathTracingData = {};

        loadProfilerUI(mSettings.mWidth, mSettings.mHeight);

        gGuiWindowDesc = {};
        gGuiWindowDesc.mStartPos = vec2(mSettings.mWidth * 0.01f, mSettings.mHeight * 0.2f);
        gGuiWindowDesc.mStartSize = vec2(600.0f, 550.0f);
        gGuiWindowDesc.pWindowTitle = GetName();
        gGuiWindowDesc.mFlags = TF_UI_WINDOW_TITLE | TF_UI_WINDOW_SCALABLE | TF_UI_WINDOW_MOVABLE | TF_UI_WINDOW_MINIMIZABLE |
                                TF_UI_WINDOW_BORDER | TF_UI_WINDOW_INIT_HEIGHT_FIT;

        {
            TFLuaWidgetVariableDesc luaVarDesc = {};

            // dropdowns
            if (RAYTRACING_TECHNIQUE_COUNT > 1)
            {
                luaVarDesc.mWidgetType = TF_WIDGET_TYPE_DROPDOWN;
                luaVarDesc.pLabel = "Raytracing Technique";
                luaVarDesc.pUint = &gRaytracingTechnique;
                luaRegisterWidgetVariable(&luaVarDesc);
            }

            // float sliders
            if (gRaytracingTechniqueSupported[RAY_QUERY])
            {
                luaVarDesc.mWidgetType = TF_WIDGET_TYPE_SLIDER_FLOAT;
                luaVarDesc.pLabel = "Light Direction X";
                luaVarDesc.pFloat = &mLightDirection.x;
                luaRegisterWidgetVariable(&luaVarDesc);
                luaVarDesc.pLabel = "Light Direction Y";
                luaVarDesc.pFloat = &mLightDirection.y;
                luaRegisterWidgetVariable(&luaVarDesc);
                luaVarDesc.pLabel = "Light Direction Z";
                luaVarDesc.pFloat = &mLightDirection.z;
                luaRegisterWidgetVariable(&luaVarDesc);
            }
        }

        if (!addSwapChain())
            return false;

        if (gRaytracingTechniqueSupported[RAY_QUERY])
        {
            addShaders();
            addDescriptorSets();

#if USE_DENOISER
            TFRenderTargetDesc rtDesc = {};
            rtDesc.mClearValue = { { FLT_MAX, 0, 0, 0 } };
            rtDesc.mWidth = mSettings.mWidth;
            rtDesc.mHeight = mSettings.mHeight;
            rtDesc.mDepth = 1;
            rtDesc.mSampleCount = TF_SAMPLE_COUNT_1;
            rtDesc.mSampleQuality = 0;
            rtDesc.mArraySize = 1;
            rtDesc.mStartState = TF_RESOURCE_STATE_SHADER_RESOURCE;

            rtDesc.mFormat = TinyImageFormat_R16G16B16A16_SFLOAT;
            addRenderTarget(pRenderer, &rtDesc, &pDepthNormalRenderTarget[0]);
            addRenderTarget(pRenderer, &rtDesc, &pDepthNormalRenderTarget[1]);

            rtDesc.mFormat = TinyImageFormat_R16G16_SFLOAT;
            rtDesc.mClearValue = { { 0, 0 } };
            addRenderTarget(pRenderer, &rtDesc, &pMotionVectorRenderTarget);

            rtDesc.mStartState = TF_RESOURCE_STATE_DEPTH_WRITE;
            rtDesc.mFormat = TinyImageFormat_D32_SFLOAT;
            rtDesc.mClearValue = { { 0.0f, 0 } };
            rtDesc.mFlags = TF_TEXTURE_CREATION_FLAG_ON_TILE;
            addRenderTarget(pRenderer, &rtDesc, &pDepthRenderTarget);
#endif

            TFTextureDesc uavDesc = {};
            uavDesc.mArraySize = 1;
            uavDesc.mDepth = 1;
            uavDesc.mFormat = TinyImageFormat_R16G16B16A16_SFLOAT;
            uavDesc.mHeight = mSettings.mHeight;
            uavDesc.mMipLevels = 1;
            uavDesc.mSampleCount = TF_SAMPLE_COUNT_1;
            uavDesc.mStartState = TF_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            uavDesc.mDescriptors = TF_DESCRIPTOR_TYPE_TEXTURE | TF_DESCRIPTOR_TYPE_RW_TEXTURE;
            uavDesc.mWidth = mSettings.mWidth;
            uavDesc.pName = "gOutput";
            TFTextureLoadDesc loadDesc = {};
            loadDesc.pDesc = &uavDesc;
            loadDesc.ppTexture = &pComputeOutput;
            addResource(&loadDesc, NULL);
#if USE_DENOISER
            uavDesc.mFormat = TinyImageFormat_B10G10R10A2_UNORM;
            loadDesc.ppTexture = &pAlbedoTexture;
            addResource(&loadDesc, NULL);
#endif

            addPipelines();

            updateDescriptorSets();
        }

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

        waitQueueIdle(pQueue);

        unloadFontSystem();
        unloadUserInterface();

        removeSwapChain(pRenderer, pSwapChain);

        unloadProfilerUI();

        if (gRaytracingTechniqueSupported[RAY_QUERY])
        {
            removePipelines();

#if USE_DENOISER
            removeRenderTarget(pRenderer, pMotionVectorRenderTarget);
            removeRenderTarget(pRenderer, pDepthNormalRenderTarget[0]);
            removeRenderTarget(pRenderer, pDepthNormalRenderTarget[1]);
            removeRenderTarget(pRenderer, pDepthRenderTarget);
#endif

            removeResource(pComputeOutput);
#if USE_DENOISER
            removeResource(pAlbedoTexture);
#endif
            removeDescriptorSets();
            removeShaders();
        }
    }

    void Update(float deltaTime)
    {
        PROFILER_SET_CPU_SCOPE("Cpu Profile", "update", 0x222222);

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

        static uint32_t prevRaytracingTechnique = UINT32_MAX;
        if (gRaytracingTechnique != prevRaytracingTechnique)
        {
            prevRaytracingTechnique = gRaytracingTechnique;
            mPathTracingData = {};
        }

        mat4 viewMat = pCamera->getViewMatrix().mMatrices[MONO_CAMERA_VIEW_INDEX];

        const float aspectInverse = (float)mSettings.mHeight / (float)mSettings.mWidth;
        const float horizontalFOV = PI / 2.0f;
        const float nearPlane = 0.1f;
        const float farPlane = 1000.f;
        mat4        projMat = mat4::perspectiveLH_ReverseZ(horizontalFOV, aspectInverse, nearPlane, farPlane);
        mat4        projectView = projMat * viewMat;

        mPathTracingData.mWorldToCamera = viewMat;
        mPathTracingData.mProjMat = projMat;
        mPathTracingData.mProjectView = projectView;
        mPathTracingData.mCameraToWorld = inverse(viewMat);
        mPathTracingData.mProjNear = nearPlane;
        mPathTracingData.mProjFarMinusNear = farPlane - nearPlane;
        mPathTracingData.mZ1PlaneSize = float2(1.0f / projMat[0][0], 1.0f / projMat[1][1]);

        mPathTracingData.mRandomSeed = randomFloat01();
    }

    void Draw()
    {
        if ((bool)pSwapChain->mEnableVsync != mSettings.mVSyncEnabled)
        {
            waitQueueIdle(pQueue);
            ::toggleVSync(pRenderer, &pSwapChain);
        }

        PROFILER_SET_CPU_SCOPE("Cpu Profile", "draw", 0xffffff);

        uint32_t swapchainImageIndex;
        acquireNextImage(pRenderer, pSwapChain, pImageAcquiredSemaphore[mSettings.mFrameIdx], NULL, &swapchainImageIndex);

        GpuCmdRingElement elem = getNextGpuCmdRingElement(&mCmdRing, true, 1);
        TFFenceStatus     fenceStatus = {};
        getFenceStatus(pRenderer, elem.pFence, &fenceStatus);
        if (fenceStatus == TF_FENCE_STATUS_INCOMPLETE)
            waitForFences(pRenderer, 1, &elem.pFence);

        resetCmdPool(pRenderer, elem.pCmdPool);

        TFCmd* pCmd = elem.pCmds[0];
        beginCmd(pCmd);
        cmdBeginGpuFrameProfile(pCmd, gGpuProfileToken);

        TFRenderTarget* pRenderTarget = pSwapChain->ppRenderTargets[swapchainImageIndex];

        const bool raytracingTechniqueSupported = gRaytracingTechniqueSupported[gRaytracingTechnique];

        if (raytracingTechniqueSupported)
        {
            bool cameraMoved = memcmp(&mPathTracingData.mProjectView, &mPathTracingData.mHistoryProjView, sizeof(mat4)) != 0;
            bool lightMoved = memcmp(&mLightDirection, &mPathTracingData.mHistoryLightDirection, sizeof(float3)) != 0; //-V1014

#if USE_DENOISER
            if (lightMoved)
            {
                clearSSVGFDenoiserTemporalHistory(pDenoiser);
            }
#else
            if (cameraMoved || lightMoved)
            {
                mPathTracingData.mFrameIndex = 0;
                mPathTracingData.mHaltonIndex = 0;
            }
#endif
            if (cameraMoved)
            {
                mPathTracingData.mLastCameraMoveFrame = mPathTracingData.mFrameIndex;
            }

            ShadersConfigBlock cb;
            cb.mCameraToWorld = mPathTracingData.mCameraToWorld;
            cb.mProjNear = mPathTracingData.mProjNear;
            cb.mProjFarMinusNear = mPathTracingData.mProjFarMinusNear;
            cb.mZ1PlaneSize = mPathTracingData.mZ1PlaneSize;
            cb.mLightDirection = normalize(mLightDirection);

            cb.mRandomSeed = mPathTracingData.mRandomSeed;

            // Loop through the first 16 items in the Halton sequence.
            // The Halton sequence takes one-based indices.
            cb.mSubpixelJitter =
                float2(haltonSequence(mPathTracingData.mHaltonIndex + 1, 2), haltonSequence(mPathTracingData.mHaltonIndex + 1, 3));

            cb.mFrameIndex = mPathTracingData.mFrameIndex;

            cb.mFramesSinceCameraMove = mPathTracingData.mFrameIndex - mPathTracingData.mLastCameraMoveFrame;
            cb.mWidth = mSettings.mWidth;
            cb.mHeight = mSettings.mHeight;
            cb.mWorldToCamera = mPathTracingData.mWorldToCamera;
            cb.mCameraToProjection = mPathTracingData.mProjMat;
            cb.mWorldToProjectionPrevious = mPathTracingData.mHistoryProjView;
            cb.mRtInvSize = float2(1.0f / mSettings.mWidth, 1.0f / mSettings.mHeight);

            cb.mWorldMatrix = SanMiguelProp.mWorldMatrix;

            TFBufferUpdateDesc bufferUpdate = { pRayGenConfigBuffer[mSettings.mFrameIdx] };
            beginUpdateResource(&bufferUpdate);
            memcpy(bufferUpdate.pMappedData, &cb, sizeof(cb));
            endUpdateResource(&bufferUpdate);

            mPathTracingData.mHistoryProjView = mPathTracingData.mProjectView;
            mPathTracingData.mHistoryLightDirection = mLightDirection;
            mPathTracingData.mFrameIndex += 1;
            mPathTracingData.mHaltonIndex = (mPathTracingData.mHaltonIndex + 1) % 16;

#if USE_DENOISER
            // Generate denoiser inputs pass
            {
                cmdBeginGpuTimestampQuery(pCmd, gGpuProfileToken, "Generate Denoiser Inputs");

                TFRenderTarget* depthNormalTarget = pDepthNormalRenderTarget[mPathTracingData.mFrameIndex & 0x1];

                // Resource Transition
                {
                    TFRenderTargetBarrier barriers[] = {
                        { depthNormalTarget, TF_RESOURCE_STATE_SHADER_RESOURCE, TF_RESOURCE_STATE_RENDER_TARGET },
                        { pMotionVectorRenderTarget, TF_RESOURCE_STATE_SHADER_RESOURCE, TF_RESOURCE_STATE_RENDER_TARGET },
                    };
                    cmdResourceBarrier(pCmd, 0, NULL, 0, NULL, 2, barriers);
                }
                // Bind Render Targets
                {
                    TFBindRenderTargetsDesc bindRenderTargets = {};
                    bindRenderTargets.mRenderTargetCount = 2;
                    bindRenderTargets.mRenderTargets[0].pRenderTarget = depthNormalTarget;
                    bindRenderTargets.mRenderTargets[0].mLoadAction = TF_LOAD_ACTION_CLEAR;
                    bindRenderTargets.mRenderTargets[0].mClearValue = { { FLT_MAX, 0, 0, 0 } };
                    bindRenderTargets.mRenderTargets[0].mOverrideClearValue = 1;
                    bindRenderTargets.mRenderTargets[1].pRenderTarget = pMotionVectorRenderTarget;
                    bindRenderTargets.mRenderTargets[1].mLoadAction = TF_LOAD_ACTION_CLEAR;
                    bindRenderTargets.mDepthStencil.pRenderTarget = pDepthRenderTarget;
                    bindRenderTargets.mDepthStencil.mLoadAction = TF_LOAD_ACTION_CLEAR;
                    cmdBindRenderTargets(pCmd, &bindRenderTargets);
                    cmdSetViewport(pCmd, 0.0f, 0.0f, (float)pDepthRenderTarget->mWidth, (float)pDepthRenderTarget->mHeight, 0.0f, 1.0f);

                    cmdSetScissor(pCmd, 0, 0, pDepthRenderTarget->mWidth, pDepthRenderTarget->mHeight);
                }
                // Draw
                {
                    cmdBindPipeline(pCmd, pDenoiserInputsPipeline);
                    cmdBindDescriptorSet(pCmd, mSettings.mFrameIdx, pDenoiserInputsDescriptorSet);

                    cmdBindVertexBuffer(pCmd, 2, SanMiguelProp.pGeom->pVertexBuffers, SanMiguelProp.pGeom->mVertexStrides, NULL);
                    cmdBindIndexBuffer(pCmd, SanMiguelProp.pGeom->pIndexBuffer, 0, (TFIndexType)SanMiguelProp.pGeom->mIndexType);

                    cmdDrawIndexed(pCmd, SanMiguelProp.pGeom->mIndexCount, 0, 0);
                }
                // Resource Transition
                {
                    TFRenderTargetBarrier barriers[] = {
                        { depthNormalTarget, TF_RESOURCE_STATE_RENDER_TARGET, TF_RESOURCE_STATE_SHADER_RESOURCE },
                        { pMotionVectorRenderTarget, TF_RESOURCE_STATE_RENDER_TARGET, TF_RESOURCE_STATE_SHADER_RESOURCE }
                    };
                    cmdResourceBarrier(pCmd, 0, NULL, 0, NULL, 2, barriers);
                }

                cmdBindRenderTargets(pCmd, NULL);
                cmdEndGpuTimestampQuery(pCmd, gGpuProfileToken);
            }
#endif
            // Path trace scene pass
            {
                cmdBeginGpuTimestampQuery(pCmd, gGpuProfileToken, "Path Trace Scene");

                // Transition UAV texture so raytracing shader can write to it
                {
                    TFTextureBarrier uavBarriers[] = {
                        { pComputeOutput, TF_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, TF_RESOURCE_STATE_UNORDERED_ACCESS },
                    };
                    cmdResourceBarrier(pCmd, 0, NULL, TF_ARRAY_COUNT(uavBarriers), uavBarriers, 0, NULL);
                }
                // Perform raytracing
                {
                    cmdBindPipeline(pCmd, pPipeline[gRaytracingTechnique]);

                    cmdBindDescriptorSet(pCmd, 0, pDescriptorSetRayTracingPersistent[gRaytracingTechnique]);
                    cmdBindDescriptorSet(pCmd, mSettings.mFrameIdx, pDescriptorSetRayTracingPerFrame[gRaytracingTechnique]);
                    cmdBindDescriptorSet(pCmd, 0, pDescriptorSetRaytracingPerBatch[gRaytracingTechnique]);

                    if (RAY_QUERY == gRaytracingTechnique)
                    {
                        const uint32_t* numThreads = pShaderRayQuery->mNumThreadsPerGroup;
                        uint32_t        groupX = round_up(mSettings.mWidth, numThreads[0]) / numThreads[0];
                        uint32_t        groupY = round_up(mSettings.mHeight, numThreads[1]) / numThreads[1];
                        cmdDispatch(pCmd, groupX, groupY, 1);
                    }
                }
                // Transition UAV to be used as source and swapchain as destination in copy operation
                {
                    TFTextureBarrier copyBarriers[] = {
                        { pComputeOutput, TF_RESOURCE_STATE_UNORDERED_ACCESS, TF_RESOURCE_STATE_PIXEL_SHADER_RESOURCE },
                    };
                    TFRenderTargetBarrier rtCopyBarriers[] = {
                        { pRenderTarget, TF_RESOURCE_STATE_PRESENT, TF_RESOURCE_STATE_RENDER_TARGET },
                    };
                    cmdResourceBarrier(pCmd, 0, NULL, 1, copyBarriers, 1, rtCopyBarriers);
                }

#if USE_DENOISER
                TFTexture* denoisedTexture = NULL;
                cmdSSVGFDenoise(pCmd, pDenoiser, pComputeOutput, pMotionVectorRenderTarget->pTexture,
                                pDepthNormalRenderTarget[mPathTracingData.mFrameIndex & 0x1]->pTexture,
                                pDepthNormalRenderTarget[(mPathTracingData.mFrameIndex + 1) & 0x1]->pTexture, &denoisedTexture);

                TFDescriptorData params[1] = {};
                params[0].mIndex = SRT_RES_IDX(SrtData, PerFrame, gDisplayTexture);
                params[0].ppTextures = &denoisedTexture;
                updateDescriptorSet(pRenderer, mSettings.mFrameIdx, pDescriptorSetPerFrame, 1, params);

                removeResource(denoisedTexture);
#endif

                cmdEndGpuTimestampQuery(pCmd, gGpuProfileToken);
            }
        }
        else
        {
            TFRenderTargetBarrier rtCopyBarriers[] = {
                { pRenderTarget, TF_RESOURCE_STATE_PRESENT, TF_RESOURCE_STATE_RENDER_TARGET },
            };
            cmdResourceBarrier(pCmd, 0, NULL, 0, NULL, 1, rtCopyBarriers);
        }

        // Display result pass
        {
            if (raytracingTechniqueSupported)
            {
                cmdBeginGpuTimestampQuery(pCmd, gGpuProfileToken, "Render Result");
            }

            // Bind Render Targets
            {
                TFBindRenderTargetsDesc bindRenderTargets = {};
                bindRenderTargets.mRenderTargetCount = 1;
                bindRenderTargets.mRenderTargets[0] = { pRenderTarget, TF_LOAD_ACTION_CLEAR };
                cmdBindRenderTargets(pCmd, &bindRenderTargets);
                cmdSetViewport(pCmd, 0.0f, 0.0f, (float)mSettings.mWidth, (float)mSettings.mHeight, 0.0f, 1.0f);
                cmdSetScissor(pCmd, 0, 0, mSettings.mWidth, mSettings.mHeight);
            }
            // Draw
            if (raytracingTechniqueSupported)
            {
                // Draw computed results
                cmdBindPipeline(pCmd, pDisplayTexturePipeline);
                cmdBindDescriptorSet(pCmd, 0, pDescriptorSetRayTracingPersistent[gRaytracingTechnique]);
                cmdBindDescriptorSet(pCmd, 0, pDescriptorSetRaytracingPerBatch[gRaytracingTechnique]);
                cmdBindDescriptorSet(pCmd, mSettings.mFrameIdx, pDescriptorSetPerFrame);
                cmdDraw(pCmd, 3, 0);
                cmdEndGpuTimestampQuery(pCmd, gGpuProfileToken);
            }
        }

        // UI pass
        {
            cmdBeginDebugMarker(pCmd, 0, 1, 0, "Draw UI");

            TFFontDrawDesc frameTimeDraw;
            frameTimeDraw.mFontColor = 0xff0080ff;
            frameTimeDraw.mFontSize = 18.0f;
            frameTimeDraw.pFont = gFont;
            float2 txtSize = cmdDrawCpuProfile(pCmd, float2(8.0f, 15.0f), &frameTimeDraw);
            cmdDrawGpuProfile(pCmd, float2(8.f, txtSize.y + 75.f), gGpuProfileToken, &frameTimeDraw);

            uiCmdDrawUserInterface(pCmd, pSwapChain, pRenderTarget, gGpuProfileToken);
            cmdBindRenderTargets(pCmd, NULL);
            cmdEndDebugMarker(pCmd);
        }

        TFRenderTargetBarrier presentBarrier = { pRenderTarget, TF_RESOURCE_STATE_RENDER_TARGET, TF_RESOURCE_STATE_PRESENT };
        cmdResourceBarrier(pCmd, 0, NULL, 0, NULL, 1, &presentBarrier);

        cmdEndGpuFrameProfile(pCmd, gGpuProfileToken);

        endCmd(pCmd);

        FlushResourceUpdateDesc flushUpdateDesc = {};
        flushUpdateDesc.mNodeIndex = 0;
        flushResourceUpdates(&flushUpdateDesc);
        TFSemaphore* waitSemaphores[2] = { flushUpdateDesc.pOutSubmittedSemaphore, pImageAcquiredSemaphore[mSettings.mFrameIdx] };

        TFQueueSubmitDesc submitDesc = {};
        submitDesc.mCmdCount = 1;
        submitDesc.mSignalSemaphoreCount = 1;
        submitDesc.mWaitSemaphoreCount = TF_ARRAY_COUNT(waitSemaphores);
        submitDesc.ppCmds = &pCmd;
        submitDesc.ppSignalSemaphores = &elem.pSemaphore;
        submitDesc.ppWaitSemaphores = waitSemaphores;
        submitDesc.pSignalFence = elem.pFence;
        queueSubmit(pQueue, &submitDesc);

        TFQueuePresentDesc presentDesc = {};
        presentDesc.mIndex = (uint8_t)swapchainImageIndex;
        presentDesc.mWaitSemaphoreCount = 1;
        presentDesc.ppWaitSemaphores = &elem.pSemaphore;
        presentDesc.pSwapChain = pSwapChain;
        presentDesc.mSubmitDone = true;
        queuePresent(pQueue, &presentDesc);
        flipProfiler();
    }

    const char* GetName() { return "16_Raytracing"; }

private:
    void readCmdArgs()
    {
        for (int i = 0; i < argc; i += 1)
        {
            if (strcmp(argv[i], "-w") == 0 && i + 1 < argc)
                mSettings.mWidth = min(max(atoi(argv[i + 1]), 64), 10000);
            else if (strcmp(argv[i], "-h") == 0 && i + 1 < argc)
                mSettings.mHeight = min(max(atoi(argv[i + 1]), 64), 10000);
            else if (strcmp(argv[i], "-f") == 0)
            {
                mSettings.mFullScreen = true;
            }
        }
    }

    bool addSwapChain()
    {
        TFSwapChainDesc swapChainDesc = {};
        swapChainDesc.mColorClearValue = {};
        swapChainDesc.mEnableVsync = mSettings.mVSyncEnabled;
        swapChainDesc.mWidth = mSettings.mWidth;
        swapChainDesc.mHeight = mSettings.mHeight;
        swapChainDesc.mImageCount = getRecommendedSwapchainImageCount(pRenderer, &pWindow->handle);
        swapChainDesc.ppPresentQueues = &pQueue;
        swapChainDesc.mPresentQueueCount = 1;
        swapChainDesc.mWindowHandle = pWindow->handle;
        swapChainDesc.mColorFormat = getSupportedSwapchainFormat(pRenderer, &swapChainDesc, TF_COLOR_SPACE_SDR_SRGB);
        swapChainDesc.mColorSpace = TF_COLOR_SPACE_SDR_SRGB;
        ::addSwapChain(pRenderer, &swapChainDesc, &pSwapChain);

        return pSwapChain != NULL;
    }

    void addDescriptorSets()
    {
        for (uint32_t t = 0; t < RAYTRACING_TECHNIQUE_COUNT; ++t)
        {
            TFDescriptorSetDesc setDesc = SRT_SET_DESC_LARGE_RW(SrtData, Persistent, 1, 0);
            addDescriptorSet(pRenderer, &setDesc, &pDescriptorSetRayTracingPersistent[t]);
            setDesc = SRT_SET_DESC_LARGE_RW(SrtData, PerFrame, gDataBufferCount, 0);
            addDescriptorSet(pRenderer, &setDesc, &pDescriptorSetRayTracingPerFrame[t]);
            setDesc = SRT_SET_DESC_LARGE_RW(SrtData, PerBatch, 1, 0);
            addDescriptorSet(pRenderer, &setDesc, &pDescriptorSetRaytracingPerBatch[t]);
        }

        TFDescriptorSetDesc setDesc = SRT_SET_DESC_LARGE_RW(SrtData, PerFrame, gDataBufferCount, 0);
        addDescriptorSet(pRenderer, &setDesc, &pDescriptorSetPerFrame);

#if USE_DENOISER
        setDesc = SRT_SET_DESC(SrtData, PerFrame, gDataBufferCount, 0, pDescriptorSetRayTracingPersistent[0]);
        addDescriptorSet(pRenderer, &setDesc, &pDenoiserInputsDescriptorSet);
#endif
    }

    void removeDescriptorSets()
    {
#if USE_DENOISER
        removeDescriptorSet(pRenderer, pDenoiserInputsDescriptorSet);
#endif
        removeDescriptorSet(pRenderer, pDescriptorSetPerFrame);

        for (uint32_t t = 0; t < RAYTRACING_TECHNIQUE_COUNT; ++t)
        {
            removeDescriptorSet(pRenderer, pDescriptorSetRaytracingPerBatch[t]);
            removeDescriptorSet(pRenderer, pDescriptorSetRayTracingPersistent[t]);
            removeDescriptorSet(pRenderer, pDescriptorSetRayTracingPerFrame[t]);
        }
    }

    void addShaders()
    {
        /************************************************************************/
        // Create Raytracing Shaders
        /************************************************************************/
        if (gRaytracingTechniqueSupported[RAY_QUERY])
        {
            TFShaderLoadDesc desc = {};
            desc.mComp.pFileName = USE_DENOISER ? (gUseUavRwFallback ? "RayQuery_denoise_rw_fallback.comp" : "RayQuery_denoise.comp")
                                                : (gUseUavRwFallback ? "RayQuery_rw_fallback.comp" : "RayQuery.comp");
            addShader(pRenderer, &desc, &pShaderRayQuery);
        }

#if USE_DENOISER
        TFShaderLoadDesc denoiserShader = {};
        denoiserShader.mVert.pFileName = "DenoiserInputsPass.vert";
        denoiserShader.mFrag.pFileName = "DenoiserInputsPass.frag";
        addShader(pRenderer, &denoiserShader, &pDenoiserInputsShader);
#endif

        /************************************************************************/
        // Blit texture Shaders
        /************************************************************************/
        const char* displayTextureVertShader[2] = { "DisplayTexture.vert", "DisplayTexture_USE_DENOISER.vert" };

        const char* displayTextureFragShader[2] = { "DisplayTexture.frag", "DisplayTexture_USE_DENOISER.frag" };

        TFShaderLoadDesc displayShader = {};
        displayShader.mVert.pFileName = displayTextureVertShader[USE_DENOISER];
        displayShader.mFrag.pFileName = displayTextureFragShader[USE_DENOISER];
        addShader(pRenderer, &displayShader, &pDisplayTextureShader);
    }

    void removeShaders()
    {
#if USE_DENOISER
        removeShader(pRenderer, pDenoiserInputsShader);
#endif
        removeShader(pRenderer, pDisplayTextureShader);

        if (gRaytracingTechniqueSupported[RAY_QUERY])
        {
            removeShader(pRenderer, pShaderRayQuery);
        }
    }

    void addPipelines()
    {
        /************************************************************************/
        //  Create Raytracing Pipelines
        /************************************************************************/
        if (gRaytracingTechniqueSupported[RAY_QUERY])
        {
            TFPipelineDesc rtPipelineDesc = {};
            rtPipelineDesc.mType = TF_PIPELINE_TYPE_COMPUTE;
            PIPELINE_LAYOUT_DESC(rtPipelineDesc, SRT_LAYOUT_DESC(SrtData, Persistent), SRT_LAYOUT_DESC(SrtData, PerFrame),
                                 SRT_LAYOUT_DESC(SrtData, PerBatch), NULL);
            TFComputePipelineDesc& pipelineDesc = rtPipelineDesc.mComputeDesc;
            pipelineDesc.pShaderProgram = pShaderRayQuery;
            addPipeline(pRenderer, &rtPipelineDesc, &pPipeline[RAY_QUERY]);

#if defined(SHADER_STATS_AVAILABLE)
            {
                TFPipelineStats pipelineStats = {};
                addPipelineStats(pRenderer, pPipeline[RAY_QUERY], false, &pipelineStats);
                TFShaderStats& stats = pipelineStats.mComp;
                if (stats.mValid)
                {
                    LOGF(eINFO,
                         "Ray query shader stats\n"
                         "    VGPRS         : Used %u / Physical %u / Available %u\n"
                         "    SGPRS         : Used %u / Physical %u / Available %u\n"
                         "    LDS size      : %u\n"
                         "    LDS usage     : %u\n"
                         "    Scratch usage : %u\n",
                         stats.mUsedVgprs, stats.mPhysicalVgprs, stats.mAvailableVgprs, stats.mUsedSgprs, stats.mPhysicalSgprs,
                         stats.mAvailableSgprs, stats.mLdsSizePerLocalWorkGroup, stats.mLdsUsageSizeInBytes, stats.mScratchMemUsageInBytes);
                }
                removePipelineStats(pRenderer, &pipelineStats);
            }
#endif
        }

#if USE_DENOISER
        {
            TFRasterizerStateDesc rasterState = {};
            rasterState.mCullMode = TF_CULL_MODE_BACK;
            rasterState.mFrontFace = TF_FRONT_FACE_CW;

            TFDepthStateDesc depthStateDesc = {};
            depthStateDesc.mDepthTest = true;
            depthStateDesc.mDepthWrite = true;
            depthStateDesc.mDepthFunc = TF_CMP_GEQUAL;

            TFPipelineDesc pipelineDesc = {};
            pipelineDesc.mType = TF_PIPELINE_TYPE_GRAPHICS;
            PIPELINE_LAYOUT_DESC(pipelineDesc, SRT_LAYOUT_DESC(SrtData, Persistent), SRT_LAYOUT_DESC(SrtData, PerFrame),
                                 SRT_LAYOUT_DESC(SrtData, PerBatch), NULL);

            TinyImageFormat rtFormats[] = { pDepthNormalRenderTarget[0]->mFormat, pMotionVectorRenderTarget->mFormat };

            TFVertexLayout vertexLayout = {};
            vertexLayout.mBindingCount = 2;
            vertexLayout.mAttribCount = 2;
            vertexLayout.mAttribs[0].mSemantic = TF_SEMANTIC_POSITION;
            vertexLayout.mAttribs[0].mFormat = TinyImageFormat_R32G32B32_SFLOAT;
            vertexLayout.mAttribs[0].mBinding = 0;
            vertexLayout.mAttribs[0].mLocation = 0;
            vertexLayout.mAttribs[1].mSemantic = TF_SEMANTIC_NORMAL;
            vertexLayout.mAttribs[1].mFormat = TinyImageFormat_R32G32B32_SFLOAT;
            vertexLayout.mAttribs[1].mBinding = 1;
            vertexLayout.mAttribs[1].mLocation = 1;

            TFGraphicsPipelineDesc& pipelineSettings = pipelineDesc.mGraphicsDesc;
            pipelineSettings.mPrimitiveTopo = TF_PRIMITIVE_TOPO_TRI_LIST;
            pipelineSettings.pRasterizerState = &rasterState;
            pipelineSettings.mRenderTargetCount = 2;
            pipelineSettings.pColorFormats = rtFormats;
            pipelineSettings.mDepthStencilFormat = pDepthRenderTarget->mFormat;
            pipelineSettings.pDepthState = &depthStateDesc;
            pipelineSettings.mSampleCount = TF_SAMPLE_COUNT_1;
            pipelineSettings.mSampleQuality = 0;
            pipelineSettings.pVertexLayout = &vertexLayout;
            pipelineSettings.pRootSignature = pDenoiserInputsRootSignature;
            pipelineSettings.pShaderProgram = pDenoiserInputsShader;

            addPipeline(pRenderer, &pipelineDesc, &pDenoiserInputsPipeline);
        }
#endif

        TFRasterizerStateDesc rasterizerStateDesc = {};
        rasterizerStateDesc.mCullMode = TF_CULL_MODE_NONE;

        TFPipelineDesc graphicsPipelineDesc = {};
        graphicsPipelineDesc.mType = TF_PIPELINE_TYPE_GRAPHICS;
        PIPELINE_LAYOUT_DESC(graphicsPipelineDesc, SRT_LAYOUT_DESC(SrtData, Persistent), SRT_LAYOUT_DESC(SrtData, PerFrame),
                             SRT_LAYOUT_DESC(SrtData, PerBatch), NULL);
        TFGraphicsPipelineDesc& pipelineSettings = graphicsPipelineDesc.mGraphicsDesc;
        pipelineSettings.mPrimitiveTopo = TF_PRIMITIVE_TOPO_TRI_LIST;
        pipelineSettings.pRasterizerState = &rasterizerStateDesc;
        pipelineSettings.mRenderTargetCount = 1;
        pipelineSettings.pColorFormats = &pSwapChain->ppRenderTargets[0]->mFormat;
        pipelineSettings.mSampleCount = pSwapChain->ppRenderTargets[0]->mSampleCount;
        pipelineSettings.mSampleQuality = pSwapChain->ppRenderTargets[0]->mSampleQuality;
        pipelineSettings.pVertexLayout = NULL;
        pipelineSettings.pShaderProgram = pDisplayTextureShader;
        addPipeline(pRenderer, &graphicsPipelineDesc, &pDisplayTexturePipeline);
    }

    void removePipelines()
    {
        removePipeline(pRenderer, pDisplayTexturePipeline);
#if USE_DENOISER
        removePipeline(pRenderer, pDenoiserInputsPipeline);
#endif
        for (uint32_t t = 0; t < RAYTRACING_TECHNIQUE_COUNT; ++t)
        {
            removePipeline(pRenderer, pPipeline[t]);
        }
    }

    void updateDescriptorSets()
    {
        TFDescriptorData perFrameParams[7] = {};
        TFDescriptorData perBatchParams[5] = {};

        perFrameParams[0].mIndex = SRT_RES_IDX(SrtData, Persistent, gRtScene);
        perFrameParams[0].ppAccelerationStructures = &pSanMiguelAS;
        perFrameParams[1].mIndex = SRT_RES_IDX(SrtData, Persistent, gIndexDataBuffer);
        perFrameParams[1].ppBuffers = &SanMiguelProp.pGeom->pIndexBuffer;
        perFrameParams[2].mIndex = SRT_RES_IDX(SrtData, Persistent, gVertexPositionBuffer);
        perFrameParams[2].ppBuffers = &SanMiguelProp.pGeom->pVertexBuffers[0];
        perFrameParams[3].mIndex = SRT_RES_IDX(SrtData, Persistent, gVertexNormalBuffer);
        perFrameParams[3].ppBuffers = &SanMiguelProp.pGeom->pVertexBuffers[1];
        perFrameParams[4].mIndex = SRT_RES_IDX(SrtData, Persistent, gVertexTexCoordBuffer);
        perFrameParams[4].ppBuffers = &SanMiguelProp.pGeom->pVertexBuffers[2];
        perFrameParams[5].mIndex = SRT_RES_IDX(SrtData, Persistent, gIndexOffsets);
        perFrameParams[5].ppBuffers = &SanMiguelProp.pIndexBufferOffsetStream;
        perFrameParams[6].mIndex = SRT_RES_IDX(SrtData, Persistent, gAllTextures);
        perFrameParams[6].ppTextures = SanMiguelProp.pTextureStorage;
        perFrameParams[6].mCount = (uint32_t)SanMiguelProp.mTextureCount;

        perBatchParams[0].mIndex = SRT_RES_IDX(SrtData, PerBatch, gOutput);
        perBatchParams[0].ppTextures = &pComputeOutput;

        uint32_t paramIndex = 1;
        if (gUseUavRwFallback)
        {
            perBatchParams[paramIndex].mIndex = SRT_RES_IDX(SrtData, PerBatch, gInput);
            perBatchParams[paramIndex].ppTextures = &pComputeOutput;
            ++paramIndex;
        }
#if USE_DENOISER
        perBatchParams[paramIndex].mIndex = SRT_RES_IDX(SrtData, PerBatch, gAlbedoOutput);
        perBatchParams[paramIndex].ppTextures = &pAlbedoTexture;
        ++paramIndex;
        if (gUseUavRwFallback)
        {
            perBatchParams[paramIndex].mIndex = SRT_RES_IDX(SrtData, PerBatch, gAlbedoInput);
            perBatchParams[paramIndex].ppTextures = &pAlbedoTexture;
            ++paramIndex;
        }
#endif
        for (uint32_t t = 0; t < RAYTRACING_TECHNIQUE_COUNT; ++t)
        {
            updateDescriptorSet(pRenderer, 0, pDescriptorSetRayTracingPersistent[t], 7, perFrameParams);
            updateDescriptorSet(pRenderer, 0, pDescriptorSetRaytracingPerBatch[t], paramIndex, perBatchParams);

            for (uint32_t i = 0; i < gDataBufferCount; ++i)
            {
                TFDescriptorData uParams[1] = {};
                uParams[0].mIndex = SRT_RES_IDX(SrtData, PerFrame, gSettings);
                uParams[0].ppBuffers = &pRayGenConfigBuffer[i];
                updateDescriptorSet(pRenderer, i, pDescriptorSetRayTracingPerFrame[t], 1, uParams);
            }
        }
        TFDescriptorData params[7] = {};
        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            params[0].mIndex = SRT_RES_IDX(SrtData, PerFrame, gDisplayTexture);
            params[0].ppTextures = &pComputeOutput;
#if USE_DENOISER
            params[1].mIndex = SRT_RES_IDX(SrtData, PerFrame, gAlbedoTex);
            params[1].ppTextures = &pAlbedoTexture;
#endif
            updateDescriptorSet(pRenderer, i, pDescriptorSetPerFrame, 1 + USE_DENOISER, params);
        }

#if USE_DENOISER
        params[0].pName = "gSettings";
        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            params[0].ppBuffers = &pRayGenConfigBuffer[i];
            updateDescriptorSet(pRenderer, i, pDenoiserInputsDescriptorSet, 1, params);
        }
#endif
    }

    void updateGui()
    {
        if (UI_WINDOW_IS_VISIBLE(uiBeginWidgetWindow(&gGuiWindowDesc)))
        {
            static const char* raytracingOptions[] = { "Ray Query" };
            COMPILE_ASSERT(TF_ARRAY_COUNT(raytracingOptions) == RAYTRACING_TECHNIQUE_COUNT);
            if (RAYTRACING_TECHNIQUE_COUNT > 1)
            {
                uiLayoutAutoTextRows(2);
                uiLabel("Raytracing Technique", TF_ALIGN_LEFT);
                gRaytracingTechnique =
                    UI_WIDGET_GET_SELECTED(uiDropdown(raytracingOptions, TF_ARRAY_COUNT(raytracingOptions), gRaytracingTechnique));
            }
            else
            {
                uiLayoutAutoTextRows(1);
                uiLabel(raytracingOptions[0], TF_ALIGN_LEFT);
            }

            const bool raytracingTechniqueSupported = gRaytracingTechniqueSupported[gRaytracingTechnique];

            if (raytracingTechniqueSupported)
            {
                if (gRaytracingTechniqueSupported[RAY_QUERY])
                {
                    uiLayoutAutoTextRows(2);
                    uiLabel("Light Direction X", TF_ALIGN_LEFT);
                    uiSliderFloat(&mLightDirection.x, -2.0f, 2.0f, 0.001f);
                    uiLabel("Light Direction Y", TF_ALIGN_LEFT);
                    uiSliderFloat(&mLightDirection.y, -2.0f, 2.0f, 0.001f);
                    uiLabel("Light Direction Z", TF_ALIGN_LEFT);
                    uiSliderFloat(&mLightDirection.z, -2.0f, 2.0f, 0.001f);
                }
            }
            else
            {
                uiLayoutAutoTextRows(1);
                uiLabel("Raytracing technique is not supported on this GPU", TF_ALIGN_LEFT);
            }
        }
        uiEndWidgetWindow();
    }
};

DEFINE_APPLICATION_MAIN(UnitTest_NativeRaytracing)
