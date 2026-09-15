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

/********************************************************************************************************
 *
 * The Forge - ANIMATION - SKINNING UNIT TEST
 *
 * The purpose of this demo is to show how to use the asset pipeline and how to do GPU skinning.
 *
 *********************************************************************************************************/

#include "Shaders/Shared.h"

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
#include "../../../../Common_3/Utilities/Interfaces/ITime.h"

// Rendering
#include "../../../../Common_3/Graphics/Interfaces/IGraphics.h"
#include "../../../../Common_3/Resources/ResourceLoader/Interfaces/IResourceLoader.h"

#include "../../../../Common_3/Utilities/RingBuffer.h"

// Middleware packages
#include "../../../../Common_3/Resources/AnimationSystem/Animation/AnimatedObject.h"
#include "../../../../Common_3/Resources/AnimationSystem/Animation/Animation.h"
#include "../../../../Common_3/Resources/AnimationSystem/Animation/Clip.h"
#include "../../../../Common_3/Resources/AnimationSystem/Animation/ClipController.h"
#include "../../../../Common_3/Resources/AnimationSystem/Animation/Rig.h"
#include "../../../../Common_3/Resources/AnimationSystem/Animation/SkeletonBatcher.h"

// Math
#include "../../../../Common_3/Utilities/Interfaces/IMath.h"

// Memory
#include "../../../../Common_3/Utilities/Interfaces/IMemory.h"

// fsl
#include "../../../../Common_3/Graphics/FSL/defaults.h"
#include "./Shaders/FSL/Global.srt.h"

//--------------------------------------------------------------------------------------------
// RENDERING PIPELINE DATA
//--------------------------------------------------------------------------------------------
// #NOTE: Two sets of resources (one in flight and one being used on CPU)
const uint32_t gDataBufferCount = 2;

ProfileToken gGpuProfileToken;

TFRenderer* pRenderer = NULL;

TFQueue*   pGraphicsQueue = NULL;
GpuCmdRing gGraphicsCmdRing = {};

TFSwapChain*    pSwapChain = NULL;
TFRenderTarget* pDepthBuffer = NULL;
TFSemaphore*    pImageAcquiredSemaphore[gDataBufferCount] = { NULL };

TFSampler* pDefaultSampler = NULL;

TFBuffer* pJointVertexBuffer = NULL;
TFBuffer* pBoneVertexBuffer = NULL;
int       gNumberOfJointPoints;
int       gNumberOfBonePoints;

TFShader*   pPlaneDrawShader = NULL;
TFBuffer*   pPlaneVertexBuffer = NULL;
TFPipeline* pPlaneDrawPipeline = NULL;

TFShader*   pShaderSkinning = NULL;
TFPipeline* pPipelineSkinning = NULL;

TFDescriptorSet* pDescriptorSetPersistent = NULL;
TFDescriptorSet* pDescriptorSetPerDraw = NULL;

struct UniformDataBones
{
    mat4 mBoneMatrix[MAX_NUM_BONES];
};

TFVertexLayout   gVertexLayoutSkinned = {};
TFGeometry*      pGeom = NULL;
TFGeometryData*  pGeomData = NULL;
TFBuffer*        pUniformBufferBones[gDataBufferCount] = { NULL };
UniformDataBones gUniformDataBones;
TFTexture*       pTextureDiffuse = NULL;

struct shadow_cap
{
    Vector4 a;
    Vector4 b;
};

static shadow_cap generate_cap(const Vector3& a, const Vector3& b, float r)
{
    Vector3 ab = a - b;
    ab = normalize(ab);

    return shadow_cap{ Vector4(a - (ab * r * 0.5f)), Vector4(b + (ab * r * 0.5f), r) };
}

struct UniformBlockPlane
{
    TFCameraMatrix mProjectView;
    mat4           mToWorldMat;
    shadow_cap     capsules[MAX_NUM_BONES];
    uint           capsules_count;
};

UniformBlockPlane gUniformDataPlane;

TFBuffer* pPlaneUniformBuffer[gDataBufferCount] = { NULL };

//--------------------------------------------------------------------------------------------
// CAMERA CONTROLLER & SYSTEMS (File/Log/UI)
//--------------------------------------------------------------------------------------------

TFICamera*     pCamera = NULL;
TFUIWindowDesc gStandaloneControlsDesc;

TFFontDrawDesc gFrameTimeDraw;
TFFont*        gFont;

// VR 2D layer transform (positioned at -1 along the Z axis, default rotation, default scale)
TFVR2DLayerDesc gVR2DLayer{ { 0.0f, 0.0f, -1.0f }, { 0.0f, 0.0f, 0.0f, 1.0f }, 1.0f };

//--------------------------------------------------------------------------------------------
// ANIMATION DATA
//--------------------------------------------------------------------------------------------

// AnimatedObjects
AnimatedObject gStickFigureAnimObject;

// Animations
Animation gAnimation;

// ClipControllers
ClipController gClipController;

// Clips
Clip gClip;

// Rigs
Rig gStickFigureRig;

// SkeletonBatcher
SkeletonBatcher gSkeletonBatcher;

// Filenames
const char* gStickFigureName = "stormtrooper/skeleton.ozz";
const char* gClipName = "stormtrooper/animations/dance.ozz";
const char* gDiffuseTexture = "Stormtrooper_D.tex";

float* pJointPoints = 0;
float* pBonePoints = 0;

const float gBoneWidthRatio = 0.2f;                // Determines how far along the bone to put the max width [0,1]
const float gJointRadius = gBoneWidthRatio * 0.5f; // Set to replicate Ozz skeleton

// Timer to get animation system update time
static TFHiresTimer gAnimationUpdateTimer;
char                gAnimationUpdateText[64] = { 0 };

//--------------------------------------------------------------------------------------------
// UI DATA
//--------------------------------------------------------------------------------------------
struct UIData
{
    struct ClipData
    {
        bool*  mPlay;
        bool*  mLoop;
        float  mAnimationTime; // Will get set by clip controller
        float* mPlaybackSpeed;
    };
    ClipData mClip;

    struct GeneralSettingsData
    {
        bool mShowBindPose = false;
        bool mDrawBones = false;
        bool mDrawPlane = true;
        bool mDrawShadows = true;
    };
    GeneralSettingsData mGeneralSettings;
};
UIData gUIData;

// Hard set the controller's time ratio via callback when it is set in the UI
static void ClipTimeChangeCallback(void* pUserData)
{
    UNREF_PARAM(pUserData);
    gClipController.SetTimeRatioHard(gUIData.mClip.mAnimationTime);
}

//--------------------------------------------------------------------------------------------
// APP CODE
//--------------------------------------------------------------------------------------------
class Skinning: public IApp
{
public:
    bool Init()
    {
        initHiresTimer(&gAnimationUpdateTimer);

        /************************************************************************/
        // SETUP ANIMATION STRUCTURES
        /************************************************************************/

        // Rigs
        // Initialize the rig with the path to its ozz file
        gStickFigureRig.Initialize(TF_RD_ANIMATIONS, gStickFigureName);

        // Clips
        gClip.Initialize(TF_RD_ANIMATIONS, gClipName, &gStickFigureRig);

        // Clip controllers
        // Initialize with the length of the clip they are controlling and an
        // optional external time to set based on their updating
        gClipController.Initialize(gClip.GetDuration(), &gUIData.mClip.mAnimationTime);

        // Animations
        AnimationDesc animationDesc{};
        animationDesc.mRig = &gStickFigureRig;
        animationDesc.mNumLayers = 1;
        animationDesc.mLayerProperties[0].mClip = &gClip;
        animationDesc.mLayerProperties[0].mClipController = &gClipController;

        gAnimation.Initialize(animationDesc);

        // Animated objects
        gStickFigureAnimObject.Initialize(&gStickFigureRig, &gAnimation);
        gStickFigureAnimObject.ComputeBindPose(gStickFigureAnimObject.mRootTransform);
        gStickFigureAnimObject.ComputeJointScales(gStickFigureAnimObject.mRootTransform);

        // GENERATE VERTEX BUFFERS

        // Generate joint vertex buffer
        gNumberOfJointPoints = 0;
        generateQuad(NULL, &gNumberOfJointPoints, gJointRadius);
        pJointPoints = (float*)tf_malloc(sizeof(float) * gNumberOfJointPoints);
        generateQuad(pJointPoints, &gNumberOfJointPoints, gJointRadius);

        // Generate bone vertex buffer
        gNumberOfBonePoints = 0;
        generateIndexedBonePoints(NULL, &gNumberOfBonePoints, gBoneWidthRatio, gStickFigureRig.mNumJoints,
                                  &gStickFigureRig.mSkeleton.joint_parents()[0]);
        pBonePoints = (float*)tf_malloc(sizeof(float) * gNumberOfBonePoints);
        generateIndexedBonePoints(pBonePoints, &gNumberOfBonePoints, gBoneWidthRatio, gStickFigureRig.mNumJoints,
                                  &gStickFigureRig.mSkeleton.joint_parents()[0]);

        // Window and renderer setup
        TFRendererDesc settings;
        memset(&settings, 0, sizeof(settings));
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

        // Create command list and graphics queue
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

        // Initialize resource and debug systems
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

        // Initialize micro profiler and its UI.
        TFProfilerDesc profiler = {};
        profiler.pRenderer = pRenderer;
        initProfiler(&profiler);

        gGpuProfileToken = initGpuProfiler(pRenderer, pGraphicsQueue, "Graphics");

        // Initialize samplers
        TFSamplerDesc defaultSamplerDesc = {};
        defaultSamplerDesc.mAddressU = TF_ADDRESS_MODE_REPEAT;
        defaultSamplerDesc.mAddressV = TF_ADDRESS_MODE_REPEAT;
        defaultSamplerDesc.mAddressW = TF_ADDRESS_MODE_REPEAT;
        defaultSamplerDesc.mMinFilter = TF_FILTER_LINEAR;
        defaultSamplerDesc.mMagFilter = TF_FILTER_LINEAR;
        defaultSamplerDesc.mMipMapMode = TF_MIPMAP_MODE_LINEAR;
        addSampler(pRenderer, &defaultSamplerDesc, &pDefaultSampler);

        // Initialize pipeline states
        uint64_t         jointDataSize = gNumberOfJointPoints * sizeof(float);
        TFBufferLoadDesc jointVbDesc = {};
        jointVbDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_VERTEX_BUFFER;
        jointVbDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_GPU_ONLY;
        jointVbDesc.mDesc.mSize = jointDataSize;
        jointVbDesc.pData = pJointPoints;
        jointVbDesc.ppBuffer = &pJointVertexBuffer;
        addResource(&jointVbDesc, NULL);

        uint64_t         boneDataSize = gNumberOfBonePoints * sizeof(float);
        TFBufferLoadDesc boneVbDesc = {};
        boneVbDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_VERTEX_BUFFER;
        boneVbDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_GPU_ONLY;
        boneVbDesc.mDesc.mSize = boneDataSize;
        boneVbDesc.pData = pBonePoints;
        boneVbDesc.ppBuffer = &pBoneVertexBuffer;
        addResource(&boneVbDesc, NULL);

        // Generate plane vertex buffer
        float planePoints[] = { -10.0f, 0.0f, -10.0f, 1.0f, 0.0f, 0.0f, -10.0f, 0.0f, 10.0f,  1.0f, 1.0f, 0.0f,
                                10.0f,  0.0f, 10.0f,  1.0f, 1.0f, 1.0f, 10.0f,  0.0f, 10.0f,  1.0f, 1.0f, 1.0f,
                                10.0f,  0.0f, -10.0f, 1.0f, 0.0f, 1.0f, -10.0f, 0.0f, -10.0f, 1.0f, 0.0f, 0.0f };

        uint64_t         planeDataSize = 6 * 6 * sizeof(float);
        TFBufferLoadDesc planeVbDesc = {};
        planeVbDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_VERTEX_BUFFER;
        planeVbDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_GPU_ONLY;
        planeVbDesc.mDesc.mSize = planeDataSize;
        planeVbDesc.pData = planePoints;
        planeVbDesc.ppBuffer = &pPlaneVertexBuffer;
        addResource(&planeVbDesc, NULL);

        TFBufferLoadDesc ubDesc = {};
        ubDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        ubDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
        ubDesc.mDesc.mSize = sizeof(UniformBlockPlane);
        ubDesc.mDesc.mFlags = TF_BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
        ubDesc.pData = NULL;
        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            ubDesc.ppBuffer = &pPlaneUniformBuffer[i];
            addResource(&ubDesc, NULL);
        }

        /************************************************************************/
        // LOAD SKINNED MESH
        /************************************************************************/
        gVertexLayoutSkinned.mBindingCount = 1;
        gVertexLayoutSkinned.mAttribCount = 5;
        gVertexLayoutSkinned.mAttribs[0].mSemantic = TF_SEMANTIC_POSITION;
        gVertexLayoutSkinned.mAttribs[0].mFormat = TinyImageFormat_R32G32B32_SFLOAT;
        gVertexLayoutSkinned.mAttribs[0].mBinding = 0;
        gVertexLayoutSkinned.mAttribs[0].mLocation = 0;
        gVertexLayoutSkinned.mAttribs[0].mOffset = 0;
        gVertexLayoutSkinned.mAttribs[1].mSemantic = TF_SEMANTIC_NORMAL;
        gVertexLayoutSkinned.mAttribs[1].mFormat = TinyImageFormat_R32_UINT;
        gVertexLayoutSkinned.mAttribs[1].mBinding = 0;
        gVertexLayoutSkinned.mAttribs[1].mLocation = 1;
        gVertexLayoutSkinned.mAttribs[1].mOffset = 3 * sizeof(float);
        gVertexLayoutSkinned.mAttribs[2].mSemantic = TF_SEMANTIC_TEXCOORD0;
        gVertexLayoutSkinned.mAttribs[2].mFormat = TinyImageFormat_R32_UINT;
        gVertexLayoutSkinned.mAttribs[2].mBinding = 0;
        gVertexLayoutSkinned.mAttribs[2].mLocation = 2;
        gVertexLayoutSkinned.mAttribs[2].mOffset = 3 * sizeof(float) + sizeof(uint32_t);
        gVertexLayoutSkinned.mAttribs[3].mSemantic = TF_SEMANTIC_WEIGHTS;
        gVertexLayoutSkinned.mAttribs[3].mFormat = TinyImageFormat_R32G32B32A32_SFLOAT;
        gVertexLayoutSkinned.mAttribs[3].mBinding = 0;
        gVertexLayoutSkinned.mAttribs[3].mLocation = 3;
        gVertexLayoutSkinned.mAttribs[3].mOffset = 3 * sizeof(float) + sizeof(uint32_t) * 2;
        gVertexLayoutSkinned.mAttribs[4].mSemantic = TF_SEMANTIC_JOINTS;
        gVertexLayoutSkinned.mAttribs[4].mFormat = TinyImageFormat_R16G16B16A16_UINT;
        gVertexLayoutSkinned.mAttribs[4].mBinding = 0;
        gVertexLayoutSkinned.mAttribs[4].mLocation = 4;
        gVertexLayoutSkinned.mAttribs[4].mOffset = 7 * sizeof(float) + sizeof(uint32_t) * 2;

        TFGeometryLoadDesc loadDesc = {};
        loadDesc.pFileName = "stormtrooper/riggedMesh.bin";
        loadDesc.pVertexLayout = &gVertexLayoutSkinned;
        loadDesc.ppGeometry = &pGeom;
        loadDesc.ppGeometryData = &pGeomData;
        addResource(&loadDesc, NULL);

        TFBufferLoadDesc boneBufferDesc = {};
        boneBufferDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        boneBufferDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
        boneBufferDesc.mDesc.mFlags = TF_BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
        ASSERT(MAX_NUM_BONES >= gStickFigureRig.mNumJoints);
        boneBufferDesc.mDesc.mSize = sizeof(mat4) * MAX_NUM_BONES;
        boneBufferDesc.pData = NULL;
        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            boneBufferDesc.ppBuffer = &pUniformBufferBones[i];
            addResource(&boneBufferDesc, NULL);
        }

        TFTextureLoadDesc diffuseTextureDesc = {};
        diffuseTextureDesc.pFileName = gDiffuseTexture;
        diffuseTextureDesc.ppTexture = &pTextureDiffuse;
        // Textures representing color should be stored in SRGB or HDR format
        diffuseTextureDesc.mCreationFlag = TF_TEXTURE_CREATION_FLAG_SRGB;
        addResource(&diffuseTextureDesc, NULL);
        /************************************************************************/

        // Skeleton renderer
        // Set up details for rendering the skeletons
        SkeletonRenderDesc skeletonRenderDesc = {};
        skeletonRenderDesc.mRenderer = pRenderer;
        skeletonRenderDesc.mFrameCount = gDataBufferCount;
        skeletonRenderDesc.mMaxSkeletonBatches = 512;
        skeletonRenderDesc.mJointVertexBuffer = pJointVertexBuffer;
        skeletonRenderDesc.mNumJointPoints = gNumberOfJointPoints;
        skeletonRenderDesc.mDrawBones = true;
        skeletonRenderDesc.mBoneVertexBuffer = pBoneVertexBuffer;
        skeletonRenderDesc.mNumBonePoints = gNumberOfBonePoints;
        skeletonRenderDesc.mBoneVertexStride = sizeof(float) * 8;
        skeletonRenderDesc.mJointVertexStride = sizeof(float) * 6;
        skeletonRenderDesc.mMaxAnimatedObjects = 1;
        skeletonRenderDesc.mJointMeshType = QuadSphere;
        gSkeletonBatcher.Initialize(skeletonRenderDesc);

        // Add the rig to the list of skeletons to render
        gSkeletonBatcher.AddAnimatedObject(&gStickFigureAnimObject);

        // Set gUIData members that need pointers to animation data
        // Clip
        gUIData.mClip.mPlay = &gClipController.mPlay;
        gUIData.mClip.mLoop = &gClipController.mLoop;
        gUIData.mClip.mPlaybackSpeed = &gClipController.mPlaybackSpeed;

        // Setup the main camera
        TFCameraMotionParameters cmp{ 50.0f, 75.0f, 150.0f };
        vec3                     camPos{ -3.0f, 3.0f, 5.0f };
        vec3                     lookAt{ 0.0f, 1.0f, 0.0f };
        pCamera = initFpsCamera(camPos, lookAt);
        pCamera->setMotionParameters(cmp);

        // App Actions
        AddCustomInputBindings();
        initScreenshotCapturer(pRenderer, pGraphicsQueue, GetName());
        waitForAllResourceLoads();

        // Need to free memory;
        tf_free(pBonePoints);
        tf_free(pJointPoints);

        return true;
    }

    void Exit()
    {
        gStickFigureRig.Exit();
        gClip.Exit();
        gAnimation.Exit();
        gStickFigureAnimObject.Exit();
        exitScreenshotCapturer();
        exitCamera(pCamera);

        exitProfiler();

        exitUserInterface();

        removeFont(gFont);
        exitFontSystem();

        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            removeResource(pPlaneUniformBuffer[i]);
        }

        removeResource(pGeomData);
        pGeomData = nullptr;
        removeResource(pGeom);
        pGeom = nullptr;

        for (uint32_t i = 0; i < gDataBufferCount; ++i)
            removeResource(pUniformBufferBones[i]);
        removeResource(pTextureDiffuse);

        removeResource(pJointVertexBuffer);
        removeResource(pBoneVertexBuffer);
        removeResource(pPlaneVertexBuffer);

        removeSampler(pRenderer, pDefaultSampler);

        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            exitSemaphore(pRenderer, pImageAcquiredSemaphore[i]);
        }
        exitGpuCmdRing(pRenderer, &gGraphicsCmdRing);

        // Animation data
        gSkeletonBatcher.Exit();
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

        addShaders();
        addDescriptorSets();

        loadProfilerUI(mSettings.mWidth, mSettings.mHeight);

        // Add the GUI Panels/Windows
        gStandaloneControlsDesc.mStartPos = vec2(mSettings.mWidth * 0.01f, mSettings.mHeight * 0.2f);
        gStandaloneControlsDesc.mStartSize = vec2(650.0f, 500.0f);
        gStandaloneControlsDesc.pWindowTitle = "Animation";
        gStandaloneControlsDesc.mFlags = TF_UI_WINDOW_TITLE | TF_UI_WINDOW_SCALABLE | TF_UI_WINDOW_MOVABLE | TF_UI_WINDOW_MINIMIZABLE |
                                         TF_UI_WINDOW_BORDER | TF_UI_WINDOW_INIT_HEIGHT_FIT;

        // Set up GUI based on gUIData
        {
            TFLuaWidgetVariableDesc luaVarDesc = {};
            TFLuaWidgetFunctionDesc luaFuncDesc = {};

            luaVarDesc.mWidgetType = TF_WIDGET_TYPE_CHECKBOX;
            luaVarDesc.pLabel = "Play";
            luaVarDesc.pBool = gUIData.mClip.mPlay;
            luaRegisterWidgetVariable(&luaVarDesc);

            luaVarDesc.mWidgetType = TF_WIDGET_TYPE_CHECKBOX;
            luaVarDesc.pLabel = "Loop";
            luaVarDesc.pBool = gUIData.mClip.mLoop;
            luaRegisterWidgetVariable(&luaVarDesc);

            luaVarDesc.mWidgetType = TF_WIDGET_TYPE_SLIDER_FLOAT;
            luaVarDesc.pLabel = "Animation Time";
            luaVarDesc.pFloat = &gUIData.mClip.mAnimationTime;
            luaRegisterWidgetVariable(&luaVarDesc);

            luaFuncDesc.mType = TF_LUA_WIDGET_FUNCTION_ON_ACTIVE;
            luaFuncDesc.pLabel = luaVarDesc.pLabel;
            luaFuncDesc.pFunc = ClipTimeChangeCallback;
            luaRegisterWidgetFunction(&luaFuncDesc);

            luaVarDesc.mWidgetType = TF_WIDGET_TYPE_SLIDER_FLOAT;
            luaVarDesc.pLabel = "Playback Speed";
            luaVarDesc.pFloat = gUIData.mClip.mPlaybackSpeed;
            luaRegisterWidgetVariable(&luaVarDesc);

            luaVarDesc.mWidgetType = TF_WIDGET_TYPE_CHECKBOX;
            luaVarDesc.pLabel = "Show Bind Pose";
            luaVarDesc.pBool = &gUIData.mGeneralSettings.mShowBindPose;
            luaRegisterWidgetVariable(&luaVarDesc);

            luaVarDesc.mWidgetType = TF_WIDGET_TYPE_CHECKBOX;
            luaVarDesc.pLabel = "Draw Bones";
            luaVarDesc.pBool = &gUIData.mGeneralSettings.mDrawBones;
            luaRegisterWidgetVariable(&luaVarDesc);

            luaVarDesc.mWidgetType = TF_WIDGET_TYPE_CHECKBOX;
            luaVarDesc.pLabel = "Draw Plane";
            luaVarDesc.pBool = &gUIData.mGeneralSettings.mDrawPlane;
            luaRegisterWidgetVariable(&luaVarDesc);

            luaVarDesc.mWidgetType = TF_WIDGET_TYPE_CHECKBOX;
            luaVarDesc.pLabel = "Draw Shadows";
            luaVarDesc.pBool = &gUIData.mGeneralSettings.mDrawShadows;
            luaRegisterWidgetVariable(&luaVarDesc);
        }

        if (!addSwapChain())
            return false;

        if (!addDepthBuffer())
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

        SkeletonBatcherLoadDesc skeletonLoad = {};
        skeletonLoad.mLoadType = pReloadDesc->mType;
        skeletonLoad.mColorFormat = pSwapChain->ppRenderTargets[0]->mFormat;
        skeletonLoad.mDepthFormat = pDepthBuffer->mFormat;
        skeletonLoad.mSampleCount = pSwapChain->ppRenderTargets[0]->mSampleCount;
        skeletonLoad.mSampleQuality = pSwapChain->ppRenderTargets[0]->mSampleQuality;

        gSkeletonBatcher.Load(&skeletonLoad);

        return true;
    }

    void Unload(TFReloadDesc* pReloadDesc)
    {
        UNREF_PARAM(pReloadDesc);

        waitQueueIdle(pGraphicsQueue);

        gSkeletonBatcher.Unload();
        unloadFontSystem();
        unloadUserInterface();

        removePipelines();

        removeSwapChain(pRenderer, pSwapChain);
        removeRenderTarget(pRenderer, pDepthBuffer);

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

        // Update camera with time
        TFCameraMatrix viewMat = pCamera->getViewMatrix();

        const float    aspectInverse = (float)mSettings.mHeight / (float)mSettings.mWidth;
        const float    horizontal_fov = PI / 2.0f;
        TFCameraMatrix projMat = camMatPerspectiveReverseZ(horizontal_fov, aspectInverse, 0.1f, 1000.0f);
        TFCameraMatrix projViewMat = camMatMul(&projMat, &viewMat);

        vec3 lightPos = vec3(0.0f, 1000.0f, 0.0f);
        vec3 lightColor = vec3(1.0f, 1.0f, 1.0f);

        /************************************************************************/
        // Animation
        /************************************************************************/
        resetHiresTimer(&gAnimationUpdateTimer);

        // Update the animated object for this frame
        if (!gStickFigureAnimObject.Update(deltaTime))
            LOGF(eINFO, "Animation NOT Updating!");

        if (!gUIData.mGeneralSettings.mShowBindPose)
        {
            // Pose the rig based on the animated object's updated values
            gStickFigureAnimObject.ComputePose(gStickFigureAnimObject.mRootTransform);
        }
        else
        {
            // Ignore the updated values and pose in bind
            gStickFigureAnimObject.ComputeBindPose(gStickFigureAnimObject.mRootTransform);
        }

        // Record animation update time
        getHiresTimerUSec(&gAnimationUpdateTimer, true);

        // Update uniforms that will be shared between all skeletons
        gSkeletonBatcher.SetSharedUniforms(projViewMat, viewMat.mMatrices[MONO_CAMERA_VIEW_INDEX], lightPos, lightColor);

        for (uint i = 0; i < pGeomData->mJointCount; ++i)
        {
            gUniformDataBones.mBoneMatrix[i] = // mat4::scale(vec3(1, 1, -1)) *
                gStickFigureAnimObject.mJointWorldMats[pGeomData->pJointRemaps[i]] * pGeomData->pInverseBindPoses[i];
        }

        /************************************************************************/
        // Shadow Capsules
        /************************************************************************/

        if (gUIData.mGeneralSettings.mDrawShadows)
        {
            gUniformDataPlane.capsules_count = 8;
            // Head
            gUniformDataPlane.capsules[0] = generate_cap(getBonePos("mixamorig:Neck"), getBonePos("mixamorig:HeadTop_End"), 0.2f);

            // Spine
            gUniformDataPlane.capsules[1] = generate_cap(getBonePos("mixamorig:Hips"), getBonePos("mixamorig:Spine2"), 0.2f);

            // Upper leg
            gUniformDataPlane.capsules[2] = generate_cap(getBonePos("mixamorig:LeftUpLeg"), getBonePos("mixamorig:LeftLeg"), 0.15f);
            gUniformDataPlane.capsules[3] = generate_cap(getBonePos("mixamorig:RightUpLeg"), getBonePos("mixamorig:RightLeg"), 0.15f);

            // Leg
            gUniformDataPlane.capsules[4] = generate_cap(getBonePos("mixamorig:LeftLeg"), getBonePos("mixamorig:LeftFoot"), 0.1f);
            gUniformDataPlane.capsules[5] = generate_cap(getBonePos("mixamorig:RightLeg"), getBonePos("mixamorig:RightFoot"), 0.1f);
            // Foot
            gUniformDataPlane.capsules[6] = generate_cap(getBonePos("mixamorig:LeftFoot"), getBonePos("mixamorig:LeftToeBase"), 0.1f);
            gUniformDataPlane.capsules[7] = generate_cap(getBonePos("mixamorig:RightFoot"), getBonePos("mixamorig:RightToeBase"), 0.1f);

            // Looks better without the arms as capsules look weird when they align with light vector
            // Arm
            // gUniformDataPlane.capsules[8] = generate_cap(getBonePos("mixamorig:LeftArm"), getBonePos("mixamorig:LeftForeArm"), 0.08f);
            // gUniformDataPlane.capsules[9] = generate_cap(getBonePos("mixamorig:RightArm"), getBonePos("mixamorig:RightForeArm"), 0.08f);
            // Forearm
            // gUniformDataPlane.capsules[10] = generate_cap(getBonePos("mixamorig:LeftForeArm"), getBonePos("mixamorig:LeftHand"), 0.08f);
            // gUniformDataPlane.capsules[11] = generate_cap(getBonePos("mixamorig:RightForeArm"), getBonePos("mixamorig:RightHand"),
            // 0.08f);
        }
        else
        {
            gUniformDataPlane.capsules_count = 0;
        }

        /************************************************************************/
        // Plane
        /************************************************************************/
        gUniformDataPlane.mProjectView = projViewMat;
        gUniformDataPlane.mToWorldMat = mat4::identity();
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

        /************************************************************************/
        // Frame sync and command buffer setup
        /************************************************************************/
        GpuCmdRingElement elem = getNextGpuCmdRingElement(&gGraphicsCmdRing, true, 1);
        TFFenceStatus     fenceStatus = {};
        getFenceStatus(pRenderer, elem.pFence, &fenceStatus);
        if (fenceStatus == TF_FENCE_STATUS_INCOMPLETE)
            waitForFences(pRenderer, 1, &elem.pFence);

        resetCmdPool(pRenderer, elem.pCmdPool);

        /************************************************************************/
        // Update uniform buffers
        /************************************************************************/
        // Update all the instanced uniform data for each batch of joints and bones
        gSkeletonBatcher.PreSetInstanceUniforms(mSettings.mFrameIdx);
        gSkeletonBatcher.SetPerInstanceUniforms(mSettings.mFrameIdx);

        TFBufferUpdateDesc planeViewProjCbv = { pPlaneUniformBuffer[mSettings.mFrameIdx] };
        beginUpdateResource(&planeViewProjCbv);
        memcpy(planeViewProjCbv.pMappedData, &gUniformDataPlane, sizeof(gUniformDataPlane));
        endUpdateResource(&planeViewProjCbv);

        TFBufferUpdateDesc boneBufferUpdateDesc = { pUniformBufferBones[mSettings.mFrameIdx] };
        beginUpdateResource(&boneBufferUpdateDesc);
        memcpy(boneBufferUpdateDesc.pMappedData, &gUniformDataBones, sizeof(mat4) * gStickFigureRig.mNumJoints);
        endUpdateResource(&boneBufferUpdateDesc);

        // Acquire the main render target from the swapchain
        TFRenderTarget* pRenderTarget = pSwapChain->ppRenderTargets[swapchainImageIndex];
        TFCmd*          cmd = elem.pCmds[0];
        beginCmd(cmd); // Start recording commands

        // Start gpu frame profiler
        cmdBeginGpuFrameProfile(cmd, gGpuProfileToken);

        TFRenderTargetBarrier barriers[] = // wait for resource transition
            {
                { pRenderTarget, TF_RESOURCE_STATE_PRESENT, TF_RESOURCE_STATE_RENDER_TARGET },
            };
        cmdResourceBarrier(cmd, 0, NULL, 0, NULL, 1, barriers);

        // Bind and clear the render target
        TFBindRenderTargetsDesc bindRenderTargets = {};
        bindRenderTargets.mRenderTargetCount = 1;
        bindRenderTargets.mRenderTargets[0] = { pRenderTarget, TF_LOAD_ACTION_CLEAR };
        bindRenderTargets.mDepthStencil = { pDepthBuffer, TF_LOAD_ACTION_CLEAR };
        cmdBindRenderTargets(cmd, &bindRenderTargets);
        cmdSetViewport(cmd, 0.0f, 0.0f, (float)pRenderTarget->mWidth, (float)pRenderTarget->mHeight, 0.0f, 1.0f);
        cmdSetScissor(cmd, 0, 0, pRenderTarget->mWidth, pRenderTarget->mHeight);

        // Plane pass
        if (gUIData.mGeneralSettings.mDrawPlane)
        {
            cmdBeginGpuTimestampQuery(cmd, gGpuProfileToken, "Plane Pass");
            const uint32_t stride = sizeof(float) * 6;
            cmdBeginDebugMarker(cmd, 1, 0, 1, "Draw Plane");
            cmdBindPipeline(cmd, pPlaneDrawPipeline);
            cmdBindDescriptorSet(cmd, mSettings.mFrameIdx, pDescriptorSetPerDraw);
            cmdBindVertexBuffer(cmd, 1, &pPlaneVertexBuffer, &stride, NULL);
            cmdDraw(cmd, 6, 0);
            cmdEndDebugMarker(cmd);
            cmdEndGpuTimestampQuery(cmd, gGpuProfileToken);
        }

        // Skeleton pass
        if (gUIData.mGeneralSettings.mDrawBones)
        {
            cmdBeginGpuTimestampQuery(cmd, gGpuProfileToken, "Skeleton Pass");
            cmdBeginDebugMarker(cmd, 1, 0, 1, "Draw Skeletons");
            gSkeletonBatcher.Draw(cmd, mSettings.mFrameIdx);
            cmdEndDebugMarker(cmd);
            cmdEndGpuTimestampQuery(cmd, gGpuProfileToken);
        }

        // Skinned mesh pass
        if (!gUIData.mGeneralSettings.mDrawBones)
        {
            cmdBeginGpuTimestampQuery(cmd, gGpuProfileToken, "Skinned Mesh Pass");
            cmdBeginDebugMarker(cmd, 1, 0, 1, "Draw Skinned Mesh");
            cmdBindPipeline(cmd, pPipelineSkinning);
            cmdBindDescriptorSet(cmd, 0, pDescriptorSetPersistent);
            cmdBindDescriptorSet(cmd, mSettings.mFrameIdx, pDescriptorSetPerDraw);
            cmdBindVertexBuffer(cmd, 1, &pGeom->pVertexBuffers[0], pGeom->mVertexStrides, NULL);
            cmdBindIndexBuffer(cmd, pGeom->pIndexBuffer, pGeom->mIndexType, 0);
            cmdDrawIndexed(cmd, pGeom->mIndexCount, 0, 0);
            cmdEndDebugMarker(cmd);
            cmdEndGpuTimestampQuery(cmd, gGpuProfileToken);
        }

        // UI pass
        {
            cmdBeginGpuTimestampQuery(cmd, gGpuProfileToken, "UI Pass");
            cmdBeginDebugMarker(cmd, 0, 1, 0, "Draw UI");
            bindRenderTargets = {};
            bindRenderTargets.mRenderTargetCount = 1;
            bindRenderTargets.mRenderTargets[0] = { pRenderTarget, TF_LOAD_ACTION_LOAD };
            cmdBindRenderTargets(cmd, &bindRenderTargets);
            {
                gFrameTimeDraw.mFontColor = 0xff00ffff;
                gFrameTimeDraw.mFontSize = 18.0f;
                gFrameTimeDraw.pFont = gFont;

                float2 txtSize = cmdDrawCpuProfile(cmd, float2(8.0f, 15.0f), &gFrameTimeDraw);

                snprintf(gAnimationUpdateText, 64, "Animation Update %f ms", getHiresTimerUSecAverage(&gAnimationUpdateTimer) / 1000.0f);

                // Disable UI rendering when taking screenshots
                if (getIsProfilerDrawing())
                {
                    gFrameTimeDraw.pText = gAnimationUpdateText;
                    cmdDrawText(cmd, float2(8.f, txtSize.y + 75.f), &gFrameTimeDraw);
                }

                cmdDrawGpuProfile(cmd, float2(8.f, txtSize.y * 2.f + 100.f), gGpuProfileToken, &gFrameTimeDraw);
                uiCmdDrawUserInterface(cmd, pSwapChain, pRenderTarget, gGpuProfileToken);
                cmdBindRenderTargets(cmd, NULL);

                cmdEndDebugMarker(cmd);
                cmdEndGpuTimestampQuery(cmd, gGpuProfileToken);
            }
        }

        // Present Graphics queue
        barriers[0] = { pRenderTarget, TF_RESOURCE_STATE_RENDER_TARGET, TF_RESOURCE_STATE_PRESENT };
        cmdResourceBarrier(cmd, 0, NULL, 0, NULL, 1, barriers);
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

    const char* GetName() { return "28_Skinning"; }

private:
    void updateGui()
    {
        if (UI_WINDOW_IS_VISIBLE(uiBeginWidgetWindow(&gStandaloneControlsDesc)))
        {
            if (UI_WIDGET_IS_VISIBLE(uiCollapsingHeaderBegin("Clip", true)))
            {
                uiLayoutAutoTextRows(1);
                uiCheckbox("Play", gUIData.mClip.mPlay);
                uiCheckbox("Loop", gUIData.mClip.mLoop);
                uiLayoutAutoTextRows(2);
                uiLabel("Animation Time", TF_ALIGN_LEFT);

                // cannot modify the animation timestamp while playing
                if (gUIData.mClip.mPlay && *gUIData.mClip.mPlay)
                {
                    uiBeginWidgetDisable();
                    uiSliderFloat(&gUIData.mClip.mAnimationTime, 0.0f, gClipController.mDuration, 0.01f);
                    uiEndWidgetDisable();
                }
                else
                {
                    uiSliderFloat(&gUIData.mClip.mAnimationTime, 0.0f, gClipController.mDuration, 0.01f);
                    ClipTimeChangeCallback(NULL);
                }

                uiLabel("Playback Speed", TF_ALIGN_LEFT);
                uiSliderFloat(gUIData.mClip.mPlaybackSpeed, -5.0f, 5.0f, 0.1f);

                uiCollapsingHeaderEnd();
            }

            uiVerticalSeparator({ 1.0f, 1.0f, 1.0f, 1.0f }, 1.0f);

            if (UI_WIDGET_IS_VISIBLE(uiCollapsingHeaderBegin("General Settings", true)))
            {
                uiLayoutAutoTextRows(1);
                uiCheckbox("Show Bind Pose", &gUIData.mGeneralSettings.mShowBindPose);
                uiCheckbox("Draw Bones", &gUIData.mGeneralSettings.mDrawBones);
                uiCheckbox("Draw Plane", &gUIData.mGeneralSettings.mDrawPlane);
                uiCheckbox("Draw Shadows", &gUIData.mGeneralSettings.mDrawShadows);

                uiCollapsingHeaderEnd();
            }
        }
        uiEndWidgetWindow();
    }

    Vector3 getBonePos(uint i) { return gStickFigureAnimObject.mJointWorldMats[i][3].getXYZ(); }

    Vector3 getBonePos(const char* jointName)
    {
        auto jointNames = gStickFigureAnimObject.mAnimation->mRig->mSkeleton.joint_names();

        uint32_t index = UINT32_MAX;
        for (uint32_t i = 0; i < jointNames.size(); ++i)
        {
            if (strcmp(jointName, jointNames[i]) == 0)
            {
                index = i;
                break;
            }
        }

        ASSERT(index != UINT32_MAX);
        return getBonePos(index);
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
        swapChainDesc.mColorClearValue = { { 0.15f, 0.15f, 0.15f, 1.0f } };
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
        setDesc = SRT_SET_DESC(SrtData, PerDraw, gDataBufferCount, 0);
        addDescriptorSet(pRenderer, &setDesc, &pDescriptorSetPerDraw);
    }

    void removeDescriptorSets()
    {
        removeDescriptorSet(pRenderer, pDescriptorSetPerDraw);
        removeDescriptorSet(pRenderer, pDescriptorSetPersistent);
    }

    void addShaders()
    {
        TFShaderLoadDesc planeShader = {};
        planeShader.mVert.pFileName = "plane.vert";
        planeShader.mFrag.pFileName = "plane.frag";
        TFShaderLoadDesc skinningShader = {};
        skinningShader.mVert.pFileName = "skinning.vert";
        skinningShader.mFrag.pFileName = "skinning.frag";

        addShader(pRenderer, &planeShader, &pPlaneDrawShader);
        addShader(pRenderer, &skinningShader, &pShaderSkinning);
    }

    void removeShaders()
    {
        removeShader(pRenderer, pShaderSkinning);
        removeShader(pRenderer, pPlaneDrawShader);
    }

    void addPipelines()
    {
        TFRasterizerStateDesc rasterizerStateDesc = {};
        rasterizerStateDesc.mCullMode = TF_CULL_MODE_NONE;

        TFRasterizerStateDesc skeletonRasterizerStateDesc = {};
        skeletonRasterizerStateDesc.mCullMode = TF_CULL_MODE_FRONT;

        TFDepthStateDesc depthStateDesc = {};
        depthStateDesc.mDepthTest = true;
        depthStateDesc.mDepthWrite = true;
        depthStateDesc.mDepthFunc = TF_CMP_GEQUAL;

        TFPipelineDesc desc = {};
        desc.mType = TF_PIPELINE_TYPE_GRAPHICS;
        PIPELINE_LAYOUT_DESC(desc, SRT_LAYOUT_DESC(SrtData, Persistent), NULL, NULL, SRT_LAYOUT_DESC(SrtData, PerDraw));
        TFGraphicsPipelineDesc& pipelineSettings = desc.mGraphicsDesc;
        pipelineSettings.mPrimitiveTopo = TF_PRIMITIVE_TOPO_TRI_STRIP;
        pipelineSettings.mRenderTargetCount = 1;
        pipelineSettings.pColorFormats = &pSwapChain->ppRenderTargets[0]->mFormat;
        pipelineSettings.mSampleCount = pSwapChain->ppRenderTargets[0]->mSampleCount;
        pipelineSettings.mSampleQuality = pSwapChain->ppRenderTargets[0]->mSampleQuality;
        pipelineSettings.mDepthStencilFormat = pDepthBuffer->mFormat;
        pipelineSettings.pDepthState = &depthStateDesc;

        // Layout and pipeline for plane draw
        TFVertexLayout vertexLayout = {};
        vertexLayout.mBindingCount = 1;
        vertexLayout.mAttribCount = 2;
        vertexLayout.mAttribs[0].mSemantic = TF_SEMANTIC_POSITION;
        vertexLayout.mAttribs[0].mFormat = TinyImageFormat_R32G32B32A32_SFLOAT;
        vertexLayout.mAttribs[0].mBinding = 0;
        vertexLayout.mAttribs[0].mLocation = 0;
        vertexLayout.mAttribs[0].mOffset = 0;
        vertexLayout.mAttribs[1].mSemantic = TF_SEMANTIC_TEXCOORD0;
        vertexLayout.mAttribs[1].mFormat = TinyImageFormat_R32G32_SFLOAT;
        vertexLayout.mAttribs[1].mBinding = 0;
        vertexLayout.mAttribs[1].mLocation = 1;
        vertexLayout.mAttribs[1].mOffset = 4 * sizeof(float);

        pipelineSettings.pRasterizerState = &rasterizerStateDesc;
        pipelineSettings.pShaderProgram = pPlaneDrawShader;
        pipelineSettings.pVertexLayout = &vertexLayout;
        addPipeline(pRenderer, &desc, &pPlaneDrawPipeline);

        // Layout and pipeline for skinning
        pipelineSettings = {};
        pipelineSettings.mPrimitiveTopo = TF_PRIMITIVE_TOPO_TRI_LIST;
        pipelineSettings.mRenderTargetCount = 1;
        pipelineSettings.pColorFormats = &pSwapChain->ppRenderTargets[0]->mFormat;
        pipelineSettings.mSampleCount = pSwapChain->ppRenderTargets[0]->mSampleCount;
        pipelineSettings.mSampleQuality = pSwapChain->ppRenderTargets[0]->mSampleQuality;
        pipelineSettings.pShaderProgram = pShaderSkinning;
        pipelineSettings.pVertexLayout = &gVertexLayoutSkinned;
        pipelineSettings.pRasterizerState = &skeletonRasterizerStateDesc;
        pipelineSettings.mDepthStencilFormat = pDepthBuffer->mFormat;
        pipelineSettings.pDepthState = &depthStateDesc;
        addPipeline(pRenderer, &desc, &pPipelineSkinning);
    }

    void removePipelines()
    {
        removePipeline(pRenderer, pPipelineSkinning);
        removePipeline(pRenderer, pPlaneDrawPipeline);
    }

    void updateDescriptorSets()
    {
        TFDescriptorData params[2] = {};
        params[0].mIndex = SRT_RES_IDX(SrtData, Persistent, gDiffuseTexture);
        params[0].ppTextures = &pTextureDiffuse;
        params[1].mIndex = SRT_RES_IDX(SrtData, Persistent, gDdefaultSampler);
        params[1].ppSamplers = &pDefaultSampler;
        updateDescriptorSet(pRenderer, 0, pDescriptorSetPersistent, 2, params);

        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            TFDescriptorData uParams[2] = {};
            uParams[0].mIndex = SRT_RES_IDX(SrtData, PerDraw, gUniformBlock);
            uParams[0].ppBuffers = &pPlaneUniformBuffer[i];
            uParams[1].mIndex = SRT_RES_IDX(SrtData, PerDraw, gBoneMatrices);
            uParams[1].ppBuffers = &pUniformBufferBones[i];
            updateDescriptorSet(pRenderer, i, pDescriptorSetPerDraw, 2, uParams);
        }
    }

    bool addDepthBuffer()
    {
        // Add depth buffer
        TF_ESRAM_BEGIN_ALLOC(pRenderer, "Depth", 0);

        TFRenderTargetDesc depthRT = {};
        depthRT.mArraySize = 1;
        depthRT.mClearValue.depth = 0.0f;
        depthRT.mClearValue.stencil = 0;
        depthRT.mDepth = 1;
        depthRT.mFormat = TinyImageFormat_D32_SFLOAT;
        depthRT.mStartState = TF_RESOURCE_STATE_DEPTH_WRITE;
        depthRT.mHeight = mSettings.mHeight;
        depthRT.mSampleCount = TF_SAMPLE_COUNT_1;
        depthRT.mSampleQuality = 0;
        depthRT.mWidth = mSettings.mWidth;
        depthRT.mFlags = TF_TEXTURE_CREATION_FLAG_ESRAM | TF_TEXTURE_CREATION_FLAG_ON_TILE | TF_TEXTURE_CREATION_FLAG_VR_MULTIVIEW;
        addRenderTarget(pRenderer, &depthRT, &pDepthBuffer);

        TF_ESRAM_END_ALLOC(pRenderer);

        return pDepthBuffer != NULL;
    }
};

DEFINE_APPLICATION_MAIN(Skinning)
