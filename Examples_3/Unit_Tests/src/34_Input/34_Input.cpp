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

// Unit Test for Input.

// Interfaces
#include "../../../../Common_3/Application/Interfaces/IApp.h"
#include "../../../../Common_3/Application/Interfaces/ICamera.h"
#include "../../../../Common_3/Application/Interfaces/IFont.h"
#include "../../../../Common_3/OS/Interfaces/IInput.h"
#include "../../../../Common_3/Application/Interfaces/IProfiler.h"
#include "../../../../Common_3/Application/Interfaces/IScreenshot.h"
#include "../../../../Common_3/Application/Interfaces/IUI.h"
#include "../../../../Common_3/Game/Interfaces/IScripting.h"
#include "../../../../Common_3/Utilities/Interfaces/IFileSystem.h"
#include "../../../../Common_3/Utilities/Interfaces/ILog.h"
#include "../../../../Common_3/Utilities/Interfaces/ITime.h"

#if defined(QUEST_VR)
#include "../../../../Common_3/OS/OpenXR/OpenXRApi.h"

#define XR_UUID_LENGTH 37
bool           gEnableVRPassThrough = false;
TFAnchorHandle gHandles[MAX_OPENXR_ANCHORS];
bstring        gAnchorDescription[MAX_OPENXR_ANCHORS];
extern "C"
{
    void XrUuidEXTToString(const XrUuidEXT* uuid, char result[XR_UUID_LENGTH]);
}
void addVRAnchor();
void saveVRAnchor();
void removeVRAnchor(uint32_t index);
void vrAnchorAdded(OpenXRAnchor* anchor);
void vrAnchorReady(OpenXRAnchor* anchor);
void vrAnchorRemoved(XrUuidEXT uuid);
#endif

// Renderer
#include "../../../../Common_3/Graphics/Interfaces/IGraphics.h"
#include "../../../../Common_3/Resources/ResourceLoader/Interfaces/IResourceLoader.h"

#include "../../../../Common_3/Utilities/RingBuffer.h"

// Math
#include "../../../../Common_3/Utilities/Interfaces/IMath.h"

#if defined(HOLOLENS2)
#include "../../../../Common_3/Application/Interfaces/ICamera.h"
#include "../../../../Common_3/OS/OpenXR/OpenXRApi.h"
extern "C"
{
    bool getHandTracked(int hand);
    void getHandJointPosRot(int hand, int joint, float3* posOut, quat* quatOut, float* radius);
}
#endif

#include "../../../../Common_3/Utilities/Interfaces/IMemory.h"

// fsl
#include "../../../../Common_3/Graphics/FSL/defaults.h"
#include "./Shaders/FSL/Global.srt.h"

// Button bit definitions
#include "ButtonDefs.h"

#if defined(ENABLE_FORGE_TOUCH_INPUT)
enum TouchPhase
{
    TOUCH_BEGAN,
    TOUCH_MOVED,
    TOUCH_ENDED,
    TOUCH_CANCELED,
};

struct TouchEvent
{
    int32_t    mId;
    int32_t    mPos[2];
    TouchPhase mPhase;
};
static const int32_t TOUCH_ID_INVALID = -1;
typedef void (*InputTouchEventCallback)(const TouchEvent* event, void* pData);
extern InputTouchEventCallback pCustomTouchEventFn;
#endif
/************************************************************************/
/************************************************************************/
struct Vertex
{
    float4 mPosition;
    float2 mTexcoord;
};

struct ConstantData
{
    float2 wndSize;

    float2 leftAxis = float2(0.0f, 0.0f);
    float2 rightAxis = float2(0.0f, 0.0f);

    // Gamepad specific
    float2 motionButtons = float2(0.0f, 0.0f);

    uint32_t deviceType = 0;
    uint32_t buttonSet0 = 0;
    uint32_t buttonSet1 = 0;
    uint32_t buttonSet2 = 0;
    uint32_t buttonSet3 = 0;
};

ConstantData     gConstantData;
// For UI purposes, a custom variable to remove 'invalid' device type - 0.Gamepad 1.KBM 2.Touch
// Default is KBM unless gamepad is detected or this is compiled with touch supportI
uint32_t         gChosenDeviceType = 1;
TFInputPortIndex gGamepadIndex = 0;
char             gGamepadNames[TF_MAX_GAMEPADS][TF_FS_MAX_PATH] = {};
bool             gEnableRumbleAndLights = false;

const uint32_t gCallbackStackSize = 4;
char           gCallbackStack[gCallbackStackSize][128];
uint32_t       gCallbackStackDisplayIndex = 0;
uint32_t       gCallbackStackIndex = 0;

#if defined(ENABLE_FORGE_TOUCH_INPUT)
TouchEvent gTouches[16] = {};
#endif

#define ARRAY_LENGTH(a) (sizeof(a) / sizeof(a[0]))

// #NOTE: Two sets of resources (one in flight and one being used on CPU)
const uint32_t gDataBufferCount = 2;

TFRenderer* pRenderer = NULL;

TFQueue*   pGraphicsQueue = NULL;
GpuCmdRing gGraphicsCmdRing = {};

TFSwapChain* pSwapChain = NULL;
TFSemaphore* pImageAcquiredSemaphore[gDataBufferCount] = { NULL };

TFShader*   pBasicShader = NULL;
TFPipeline* pBasicPipeline = NULL;

TFBuffer* pQuadVertexBuffer = NULL;
TFBuffer* pQuadIndexBuffer = NULL;

TFTexture*       pGamepad = NULL;
TFTexture*       pKeyboardMouse = NULL;
TFTexture*       pInputDeviceTexture = NULL;
TFTexture*       pPrevInputDeviceTexture = NULL;
TFDescriptorSet* pDescriptorSetPersistent = NULL;
TFDescriptorSet* pDescriptorSetPerFrame = NULL;

TFBuffer* pInputDataUniformBuffer[gDataBufferCount] = { NULL };

ProfileToken gGpuProfileToken = PROFILE_INVALID_TOKEN;

#if defined(HOLOLENS2)
#define HAND_JOINTS_PER_HAND  XR_HAND_JOINT_COUNT_EXT
#define HAND_BONES_PER_HAND   (HAND_JOINTS_PER_HAND - 2)
#define HAND_JOINT_INSTANCES  (2 * HAND_JOINTS_PER_HAND)
#define HAND_BONE_INSTANCES   (2 * HAND_BONES_PER_HAND)
#define HAND_RENDER_INSTANCES (HAND_JOINT_INSTANCES + HAND_BONE_INSTANCES)
COMPILE_ASSERT(HAND_RENDER_INSTANCES == 100);

static const int gHandJointParent[HAND_JOINTS_PER_HAND] = { -1, -1, 1,  2, 3,  4,  1,  6,  7, 8,  9,  1,  11,
                                                            12, 13, 14, 1, 16, 17, 18, 19, 1, 21, 22, 23, 24 };

struct HandJointVertex
{
    float4 mPosition;
    float4 mNormal;
};

struct HandJointUniform
{
    TFCameraMatrix mProjectView;
    mat4           mToWorld[HAND_RENDER_INSTANCES];
    float4         mColor;
};

TFShader*       pHandJointShader = NULL;
TFPipeline*     pHandJointPipeline = NULL;
TFRenderTarget* pDepthBuffer = NULL;
TFBuffer*       pHandJointVertexBuffer = NULL;
TFBuffer*       pHandJointIndexBuffer = NULL;
TFBuffer*       pHandJointUniformBuffer[gDataBufferCount] = { NULL };
#endif

/// UI
TFUIWindowDesc     gWindowDesc;
static const char* gEnumNames[] = {
    "Gamepad",
    "Keyboard+Mouse",
#if defined(ENABLE_FORGE_TOUCH_INPUT)
    "Touch",
#endif
};

TFFont* gFont;

TFFontDrawDesc gFrameTimeDraw = TFFontDrawDesc{ NULL, 0, 0xff00ffff, 18 };

Vertex gQuadVerts[4];

// VR 2D layer transform (positioned at -1 along the Z axis, default rotation, default scale)
TFVR2DLayerDesc gVR2DLayer{ { 0.0f, 0.0f, -1.0f }, { 0.0f, 0.0f, 0.0f, 1.0f }, 1.0f };
///////////////////////////////////////////////////////////////////////////

#define SET_BUTTON_BIT(set, state, button_bit) ((set) = (state) ? ((set) | (button_bit)) : ((set) & ~(button_bit)));

IApp* gUnitTestApp = NULL;

class Input: public IApp
{
public:
    Input()
    {
        gUnitTestApp = this;

        mSettings.mCentered = true;
        mSettings.mBorderlessWindow = false;
        mSettings.mForceLowDPI = true;

        gConstantData.deviceType = gChosenDeviceType;
    }

    bool Init()
    {
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

        TFTextureLoadDesc textureDesc = {};
        textureDesc.pFileName = "input/controller.tex";
        textureDesc.ppTexture = &pGamepad;
        addResource(&textureDesc, NULL);

        textureDesc.pFileName = "input/keyboard+mouse.tex";
        textureDesc.ppTexture = &pKeyboardMouse;
        addResource(&textureDesc, NULL);

        TFBufferLoadDesc bufferDesc = {};
        bufferDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        bufferDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
        bufferDesc.mDesc.mSize = sizeof(ConstantData);
        bufferDesc.mDesc.mFlags = TF_BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
        bufferDesc.pData = NULL;
        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            bufferDesc.ppBuffer = &pInputDataUniformBuffer[i];
            addResource(&bufferDesc, NULL);
        }

        gQuadVerts[0].mPosition = { 1.0f, 1.0f, 0.0f, 1.0f };
        gQuadVerts[0].mTexcoord = { 1.0f, 0.0f };
        gQuadVerts[1].mPosition = { -1.0f, 1.0f, 0.0f, 1.0f };
        gQuadVerts[1].mTexcoord = { 0.0f, 0.0f };
        gQuadVerts[2].mPosition = { -1.0f, -1.0f, 0.0f, 1.0f };
        gQuadVerts[2].mTexcoord = { 0.0f, 1.0f };
        gQuadVerts[3].mPosition = { 1.0f, -1.0f, 0.0f, 1.0f };
        gQuadVerts[3].mTexcoord = { 1.0f, 1.0f };

        uint16_t indices[] = { 0, 1, 2, 0, 2, 3 };

        TFBufferLoadDesc joystickVBDesc = {};
        joystickVBDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_VERTEX_BUFFER;
        joystickVBDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_GPU_ONLY;
        joystickVBDesc.mDesc.mSize = sizeof(gQuadVerts);
        joystickVBDesc.pData = gQuadVerts;
        joystickVBDesc.ppBuffer = &pQuadVertexBuffer;
        addResource(&joystickVBDesc, NULL);

        TFBufferLoadDesc ibDesc = {};
        ibDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_INDEX_BUFFER;
        ibDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_GPU_ONLY;
        ibDesc.mDesc.mSize = sizeof(indices);
        ibDesc.pData = indices;
        ibDesc.ppBuffer = &pQuadIndexBuffer;
        addResource(&ibDesc, NULL);

#if defined(HOLOLENS2)
        {
            const float cubeCorners[8][3] = {
                { -1, -1, -1 }, { 1, -1, -1 }, { 1, 1, -1 }, { -1, 1, -1 }, { -1, -1, 1 }, { 1, -1, 1 }, { 1, 1, 1 }, { -1, 1, 1 },
            };
            const float     invSqrt3 = 0.57735026919f;
            HandJointVertex cubeVerts[8];
            for (uint32_t v = 0; v < 8; ++v)
            {
                cubeVerts[v].mPosition = { cubeCorners[v][0], cubeCorners[v][1], cubeCorners[v][2], 1.0f };
                cubeVerts[v].mNormal = { cubeCorners[v][0] * invSqrt3, cubeCorners[v][1] * invSqrt3, cubeCorners[v][2] * invSqrt3, 0.0f };
            }
            const uint16_t cubeIndices[36] = { 0, 1, 2, 0, 2, 3, 4, 6, 5, 4, 7, 6, 4, 5, 1, 4, 1, 0,
                                               3, 2, 6, 3, 6, 7, 4, 0, 3, 4, 3, 7, 1, 5, 6, 1, 6, 2 };

            TFBufferLoadDesc cubeVbDesc = {};
            cubeVbDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_VERTEX_BUFFER;
            cubeVbDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_GPU_ONLY;
            cubeVbDesc.mDesc.mSize = sizeof(cubeVerts);
            cubeVbDesc.pData = cubeVerts;
            cubeVbDesc.ppBuffer = &pHandJointVertexBuffer;
            addResource(&cubeVbDesc, NULL);

            TFBufferLoadDesc cubeIbDesc = {};
            cubeIbDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_INDEX_BUFFER;
            cubeIbDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_GPU_ONLY;
            cubeIbDesc.mDesc.mSize = sizeof(cubeIndices);
            cubeIbDesc.pData = cubeIndices;
            cubeIbDesc.ppBuffer = &pHandJointIndexBuffer;
            addResource(&cubeIbDesc, NULL);

            TFBufferLoadDesc handUbDesc = {};
            handUbDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            handUbDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
            handUbDesc.mDesc.mSize = sizeof(HandJointUniform);
            handUbDesc.mDesc.mFlags = TF_BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
            handUbDesc.pData = NULL;
            for (uint32_t i = 0; i < gDataBufferCount; ++i)
            {
                handUbDesc.ppBuffer = &pHandJointUniformBuffer[i];
                addResource(&handUbDesc, NULL);
            }
        }
#endif

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

        // Gpu profiler can only be added after initProfiler.
        gGpuProfileToken = initGpuProfiler(pRenderer, pGraphicsQueue, "Graphics");

        // Initialize Controller Callbacks
        inputGamepadSetAddedCallback(onControllerAdded);
        inputGamepadSetRemovedCallback(onControllerRemoved);

        waitForAllResourceLoads();

#if defined(ENABLE_FORGE_TOUCH_INPUT)
        for (uint32_t t = 0; t < TF_ARRAY_COUNT(gTouches); ++t)
        {
            gTouches[t].mId = TOUCH_ID_INVALID;
        }
        pCustomTouchEventFn = onTouchEvent;
#endif

        mSettings.mShowPlatformUI = false;

        // Add custom binding for the input
#define BINDING_ACTION(x) inputAddCustomBindings(#x "; analog; " #x "; 1.0f");
        DECL_INPUTS_BODY(BINDING_ACTION)

        initScreenshotCapturer(pRenderer, pGraphicsQueue, GetName());

#if defined(QUEST_VR)
        for (uint32_t i = 0; i < MAX_OPENXR_ANCHORS; i++)
        {
            gAnchorDescription[i] = bempty();
        }

        InitXRAnchorSystem(NULL, vrAnchorAdded, vrAnchorReady, vrAnchorRemoved);
#endif

        return true;
    }

    void Exit()
    {
        exitScreenshotCapturer();
        waitQueueIdle(pGraphicsQueue);

        exitUserInterface();

        removeFont(gFont);
        exitFontSystem();

        exitProfiler();

        for (uint32_t i = 0; i < gDataBufferCount; i++)
        {
            removeResource(pInputDataUniformBuffer[i]);
        }

        removeResource(pGamepad);
        removeResource(pKeyboardMouse);

        removeResource(pQuadVertexBuffer);
        removeResource(pQuadIndexBuffer);

#if defined(HOLOLENS2)
        removeResource(pHandJointVertexBuffer);
        removeResource(pHandJointIndexBuffer);
        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            removeResource(pHandJointUniformBuffer[i]);
        }
#endif

        exitGpuCmdRing(pRenderer, &gGraphicsCmdRing);
        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            exitSemaphore(pRenderer, pImageAcquiredSemaphore[i]);
        }
        exitRootSignature(pRenderer);
        exitResourceLoaderInterface(pRenderer);
        exitQueue(pRenderer, pGraphicsQueue);
        exitRenderer(pRenderer);
        exitGPUConfig();
        pRenderer = NULL;

#if defined(QUEST_VR)
        for (uint32_t i = 0; i < MAX_OPENXR_ANCHORS; i++)
        {
            bdestroy(&gAnchorDescription[i]);
        }
#endif
    }

    bool Load(TFReloadDesc* pReloadDesc)
    {
        UNREF_PARAM(pReloadDesc);

        // Choose a device. Gamepad has higher priority than touch.
#if defined(ENABLE_FORGE_TOUCH_INPUT)
        gChosenDeviceType = 2;
#endif
        for (uint32_t i = 0; i < TF_MAX_GAMEPADS; ++i)
        {
            if (inputGamepadIsActive(i))
            {
                gGamepadIndex = i;
                gChosenDeviceType = 0;
                break;
            }
        }

        // Setup action mappings and callbacks
        onDeviceSwitch(nullptr);
        addShaders();
        addDescriptorSets();

        loadProfilerUI(mSettings.mWidth, mSettings.mHeight);

        gWindowDesc = {};
        gWindowDesc.mStartPos += vec2(mSettings.mWidth * 0.01f, mSettings.mHeight * 0.2f);
        gWindowDesc.pWindowTitle = GetName();
        gWindowDesc.mFlags = TF_UI_WINDOW_TITLE | TF_UI_WINDOW_SCALABLE | TF_UI_WINDOW_MOVABLE | TF_UI_WINDOW_MINIMIZABLE |
                             TF_UI_WINDOW_BORDER | TF_UI_WINDOW_INIT_HEIGHT_FIT;
        gWindowDesc.mStartSize = vec2(600.0f, 300.0f);

        TFLuaWidgetVariableDesc luaVarDesc = {};
        TFLuaWidgetFunctionDesc luaFuncDesc = {};

        luaVarDesc.pLabel = "Device Type: ";
        luaVarDesc.mWidgetType = TF_WIDGET_TYPE_DROPDOWN;
        luaVarDesc.pInt = (int*)&gChosenDeviceType;
        luaRegisterWidgetVariable(&luaVarDesc);
        luaFuncDesc.pLabel = luaVarDesc.pLabel;
        luaFuncDesc.mType = TF_LUA_WIDGET_FUNCTION_ON_EDITED;
        luaFuncDesc.pFunc = onDeviceSwitch;
        luaRegisterWidgetFunction(&luaFuncDesc);

        luaVarDesc.pLabel = "Enable Rumble and Lights Test Sequence:";
        luaVarDesc.mWidgetType = TF_WIDGET_TYPE_CHECKBOX;
        luaVarDesc.pBool = &gEnableRumbleAndLights;
        luaRegisterWidgetVariable(&luaVarDesc);
        luaFuncDesc.pLabel = luaVarDesc.pLabel;
        luaFuncDesc.mType = TF_LUA_WIDGET_FUNCTION_ON_EDITED;
        luaFuncDesc.pFunc = onLightsAndRumbleSwitch;
        luaRegisterWidgetFunction(&luaFuncDesc);

        luaVarDesc.pLabel = "Gamepad";
        luaVarDesc.mWidgetType = TF_WIDGET_TYPE_DROPDOWN;
        luaVarDesc.pInt = (int*)&gGamepadIndex;
        luaRegisterWidgetVariable(&luaVarDesc);

        if (!addSwapChain())
            return false;

#if defined(HOLOLENS2)
        if (!addDepthBuffer())
            return false;
#endif

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

    void Unload(TFReloadDesc* pReloadDesc)
    {
        UNREF_PARAM(pReloadDesc);

        waitQueueIdle(pGraphicsQueue);

        unloadFontSystem();
        unloadUserInterface();

        removePipelines();

#if defined(HOLOLENS2)
        removeRenderTarget(pRenderer, pDepthBuffer);
#endif

        removeSwapChain(pRenderer, pSwapChain);
        unloadProfilerUI();

        removeDescriptorSets();
        removeShaders();
    }

    void Update(float deltaTime)
    {
        updateGui();
        updateInputState();

        if (pInputDeviceTexture != pPrevInputDeviceTexture)
        {
            TFDescriptorData param = {};
            param.mIndex = SRT_RES_IDX(SrtData, Persistent, gTexture);
            param.ppTextures = &pInputDeviceTexture;
            updateDescriptorSet(pRenderer, 0, pDescriptorSetPersistent, 1, &param);

            pPrevInputDeviceTexture = pInputDeviceTexture;
        }

        if (gEnableRumbleAndLights)
        {
            updateRumbleAndLightsTestSequence(deltaTime);
        }
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

        TFRenderTarget* pRenderTarget = pSwapChain->ppRenderTargets[swapchainImageIndex];

        // Stall if CPU is running "gDataBufferCount" frames ahead of GPU
        GpuCmdRingElement elem = getNextGpuCmdRingElement(&gGraphicsCmdRing, true, 1);
        TFFenceStatus     fenceStatus;
        getFenceStatus(pRenderer, elem.pFence, &fenceStatus);
        if (fenceStatus == TF_FENCE_STATUS_INCOMPLETE)
            waitForFences(pRenderer, 1, &elem.pFence);

        // Update uniform buffers
        gConstantData.wndSize = { (float)pRenderTarget->mWidth, (float)pRenderTarget->mHeight };

        TFBufferUpdateDesc inputData = { pInputDataUniformBuffer[mSettings.mFrameIdx] };
        beginUpdateResource(&inputData);
        memcpy(inputData.pMappedData, &gConstantData, sizeof(gConstantData));
        endUpdateResource(&inputData);

#if defined(HOLOLENS2)
        {
            HandJointUniform handUniform = {};

            TFCameraMatrix view = {};
            TFCameraMatrix proj = {};
            for (uint32_t e = 0; e < VR_MULTIVIEW_COUNT; ++e)
            {
                GetOpenXRViewMatrix(e, &view.mMatrices[e]);
                float4 eyePosition = getCol3(view.mMatrices[e]);
                mat4   eyeRotation = view.mMatrices[e];
                eyeRotation.setTranslation(float3(0.0f, 0.0f, 0.0f));
                view.mMatrices[e] = eyeRotation * mat4::translation(float3(-eyePosition.x, -eyePosition.y, eyePosition.z));
                GetOpenXRProjMatrixPerspectiveReverseZ(e, 0.01f, 100.0f, &proj.mMatrices[e]);
            }
            handUniform.mProjectView = camMatMul(&proj, &view);
            handUniform.mColor = { 0.95f, 0.85f, 0.15f, 1.0f };

            float3 jointPos[2][HAND_JOINTS_PER_HAND];
            bool   handTracked[2] = { false, false };

            for (int h = 0; h < 2; ++h)
            {
                const bool tracked = getHandTracked(h);
                handTracked[h] = tracked;
                for (int j = 0; j < HAND_JOINTS_PER_HAND; ++j)
                {
                    const uint32_t instance = (uint32_t)(h * HAND_JOINTS_PER_HAND + j);
                    if (!tracked)
                    {
                        handUniform.mToWorld[instance] = f4x4Scale(float3(0.0f, 0.0f, 0.0f));
                        continue;
                    }

                    float3 pos;
                    quat   rot;
                    float  radius = 0.0f;
                    getHandJointPosRot(h, j, &pos, &rot, &radius);
                    jointPos[h][j] = pos;
                    radius *= 0.7f;

                    handUniform.mToWorld[instance] =
                        f4x4Mul(f4x4Mul(f4x4Translation(pos), f4x4RotationQuat(rot)), f4x4Scale(float3(radius, radius, radius)));
                }
            }

            for (int h = 0; h < 2; ++h)
            {
                for (int j = 2; j < HAND_JOINTS_PER_HAND; ++j)
                {
                    const uint32_t boneInstance = (uint32_t)(HAND_JOINT_INSTANCES + h * HAND_BONES_PER_HAND + (j - 2));
                    const int      parent = gHandJointParent[j];

                    float  len = 0.0f;
                    float3 dir = float3(0.0f, 0.0f, 1.0f);
                    float3 mid = float3(0.0f, 0.0f, 0.0f);
                    if (handTracked[h])
                    {
                        const float3 a = jointPos[h][parent];
                        const float3 b = jointPos[h][j];
                        const float3 d = f3Sub(b, a);
                        len = f3Length(d);
                        if (len > 1.0e-5f)
                        {
                            dir = f3MulScalar(d, 1.0f / len);
                            mid = f3Lerp(a, b, 0.5f);
                        }
                    }

                    if (len <= 1.0e-5f)
                    {
                        handUniform.mToWorld[boneInstance] = f4x4Scale(float3(0.0f, 0.0f, 0.0f));
                        continue;
                    }

                    const float3 axis = f3Cross(float3(0.0f, 0.0f, 1.0f), dir);
                    const float  axisLen = f3Length(axis);
                    mat4         rot;
                    if (axisLen < 1.0e-5f)
                        rot = (dir.z >= 0.0f) ? f4x4Identity() : f4x4Rotation(PI, float3(1.0f, 0.0f, 0.0f));
                    else
                        rot = f4x4Rotation(atan2f(axisLen, dir.z), f3MulScalar(axis, 1.0f / axisLen));

                    const float boneRadius = 0.004f;
                    handUniform.mToWorld[boneInstance] =
                        f4x4Mul(f4x4Mul(f4x4Translation(mid), rot), f4x4Scale(float3(boneRadius, boneRadius, len * 0.5f)));
                }
            }

            TFBufferUpdateDesc handData = { pHandJointUniformBuffer[mSettings.mFrameIdx] };
            beginUpdateResource(&handData);
            memcpy(handData.pMappedData, &handUniform, sizeof(handUniform));
            endUpdateResource(&handData);
        }
#endif

        // Reset cmd pool for this frame
        resetCmdPool(pRenderer, elem.pCmdPool);

        TFCmd* cmd = elem.pCmds[0];
        beginCmd(cmd);

        cmdBeginGpuFrameProfile(cmd, gGpuProfileToken);

        TFRenderTargetBarrier barrier;

        barrier = { pRenderTarget, TF_RESOURCE_STATE_PRESENT, TF_RESOURCE_STATE_RENDER_TARGET };
        cmdResourceBarrier(cmd, 0, NULL, 0, NULL, 1, &barrier);

        // Clear target
        TFBindRenderTargetsDesc bindRenderTargets = {};
        bindRenderTargets.mRenderTargetCount = 1;
        bindRenderTargets.mRenderTargets[0] = { pRenderTarget, TF_LOAD_ACTION_CLEAR };
#if defined(HOLOLENS2)
        bindRenderTargets.mDepthStencil = { pDepthBuffer, TF_LOAD_ACTION_CLEAR };
#endif
        cmdBindRenderTargets(cmd, &bindRenderTargets);
        cmdSetViewport(cmd, 0.0f, 0.0f, (float)pRenderTarget->mWidth, (float)pRenderTarget->mHeight, 0.0f, 1.0f);
        cmdSetScissor(cmd, 0, 0, pRenderTarget->mWidth, pRenderTarget->mHeight);

#if defined(HOLOLENS2)
        {
            cmdBeginGpuTimestampQuery(cmd, gGpuProfileToken, "Draw Hand Joints");

            const uint32_t handStride = sizeof(HandJointVertex);

            cmdBindPipeline(cmd, pHandJointPipeline);
            cmdBindDescriptorSet(cmd, 0, pDescriptorSetPersistent);
            cmdBindDescriptorSet(cmd, mSettings.mFrameIdx, pDescriptorSetPerFrame);
            cmdBindVertexBuffer(cmd, 1, &pHandJointVertexBuffer, &handStride, NULL);
            cmdBindIndexBuffer(cmd, pHandJointIndexBuffer, TF_INDEX_TYPE_UINT16, 0);
            cmdDrawIndexedInstanced(cmd, 36, 0, HAND_RENDER_INSTANCES, 0, 0);

            cmdEndGpuTimestampQuery(cmd, gGpuProfileToken);
        }
#endif

#if !defined(HOLOLENS2)
#if defined(QUEST_VR)
        if (!gEnableVRPassThrough)
#endif
        {
            cmdBeginGpuTimestampQuery(cmd, gGpuProfileToken, "Draw Input Device");

            const uint32_t stride = 6 * sizeof(float);

            cmdBindPipeline(cmd, pBasicPipeline);
            cmdBindIndexBuffer(cmd, pQuadIndexBuffer, TF_INDEX_TYPE_UINT16, 0);

            cmdBindDescriptorSet(cmd, 0, pDescriptorSetPersistent);
            cmdBindDescriptorSet(cmd, mSettings.mFrameIdx, pDescriptorSetPerFrame);
            cmdBindVertexBuffer(cmd, 1, &pQuadVertexBuffer, &stride, NULL);
            cmdDrawIndexed(cmd, 6, 0, 0);

            cmdEndGpuTimestampQuery(cmd, gGpuProfileToken);
        }
#endif

#if defined(HOLOLENS2)
        {
            TFBindRenderTargetsDesc uiBindRenderTargets = {};
            uiBindRenderTargets.mRenderTargetCount = 1;
            uiBindRenderTargets.mRenderTargets[0] = { pRenderTarget, TF_LOAD_ACTION_LOAD };
            cmdBindRenderTargets(cmd, &uiBindRenderTargets);
        }
#endif
        // UI pass
        {
            cmdBeginGpuTimestampQuery(cmd, gGpuProfileToken, "Draw UI");

#if defined(ENABLE_FORGE_TOUCH_INPUT)
            gFrameTimeDraw.mFontColor = 0xff00ffff;
            gFrameTimeDraw.pFont = gFont;

            float  yTxtOffset = 12.f;
            float  xTxtOffset = 8.f;
            float  yTxtOrig = yTxtOffset;
            float2 txtSizePx = {};
            yTxtOrig += txtSizePx.y + 7 * yTxtOffset;
            yTxtOffset = 32.f;
            txtSizePx.y = 15.0f;
            if (gChosenDeviceType == INPUT_DEVICE_TOUCH)
            {
                for (uint32_t t = 0; t < TF_ARRAY_COUNT(gTouches); ++t)
                {
                    const char* phaseNames[] = {
                        "BEGAN",
                        "MOVED",
                        "ENDED",
                        "CANCELED",
                    };
                    const TouchEvent& event = gTouches[t];
                    if (TOUCH_ID_INVALID == event.mId)
                    {
                        continue;
                    }
                    const int textSize = 256;
                    char      gestureText[textSize] = { 0 };
                    snprintf(gestureText, textSize, "TouchEvent %10d : Pos (%d : %d) Phase (%s)", event.mId, event.mPos[0], event.mPos[1],
                             phaseNames[event.mPhase]);
                    gFrameTimeDraw.pText = gestureText;
                    cmdDrawText(cmd, float2(xTxtOffset, yTxtOrig), &gFrameTimeDraw);
                    yTxtOrig += txtSizePx.y + yTxtOffset;
                }
            }
#endif

            uiCmdDrawUserInterface(cmd, pSwapChain, pRenderTarget, gGpuProfileToken);
            cmdEndGpuTimestampQuery(cmd, gGpuProfileToken);
        }
        cmdBindRenderTargets(cmd, NULL);

        barrier = { pRenderTarget, TF_RESOURCE_STATE_RENDER_TARGET, TF_RESOURCE_STATE_PRESENT };
        cmdResourceBarrier(cmd, 0, NULL, 0, NULL, 1, &barrier);

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
        presentDesc.pSwapChain = pSwapChain;
        presentDesc.ppWaitSemaphores = &elem.pSemaphore;
        presentDesc.mSubmitDone = true;
        queuePresent(pGraphicsQueue, &presentDesc);

        flipProfiler();
    }

    const char* GetName() { return "34_Input"; }

private:
    bool updateInputState()
    {
        gConstantData.leftAxis = float2(0.0f, 0.0f);
        gConstantData.rightAxis = float2(0.0f, 0.0f);
        gConstantData.motionButtons = float2(0.0f, 0.0f);
        gConstantData.buttonSet0 = 0;
        gConstantData.buttonSet1 = 0;
        gConstantData.buttonSet2 = 0;
        gConstantData.buttonSet3 = 0;

        switch (gConstantData.deviceType)
        {
        case INPUT_DEVICE_GAMEPAD:
        {
            gConstantData.leftAxis.x = inputGetValue(gGamepadIndex, GPAD_LX);
            gConstantData.leftAxis.y = -inputGetValue(gGamepadIndex, GPAD_LY);
            gConstantData.rightAxis.x = inputGetValue(gGamepadIndex, GPAD_RX);
            gConstantData.rightAxis.y = -inputGetValue(gGamepadIndex, GPAD_RY);
            gConstantData.motionButtons.x = inputGetValue(gGamepadIndex, GPAD_L2);
            gConstantData.motionButtons.y = inputGetValue(gGamepadIndex, GPAD_R2);
            SET_BUTTON_BIT(gConstantData.buttonSet0, inputGetValue(gGamepadIndex, GPAD_LEFT), CONTROLLER_DPAD_LEFT_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet0, inputGetValue(gGamepadIndex, GPAD_RIGHT), CONTROLLER_DPAD_RIGHT_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet0, inputGetValue(gGamepadIndex, GPAD_UP), CONTROLLER_DPAD_UP_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet0, inputGetValue(gGamepadIndex, GPAD_DOWN), CONTROLLER_DPAD_DOWN_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet0, inputGetValue(gGamepadIndex, GPAD_A), CONTROLLER_A_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet0, inputGetValue(gGamepadIndex, GPAD_B), CONTROLLER_B_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet0, inputGetValue(gGamepadIndex, GPAD_X), CONTROLLER_X_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet0, inputGetValue(gGamepadIndex, GPAD_Y), CONTROLLER_Y_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet0, inputGetValue(gGamepadIndex, GPAD_L1), CONTROLLER_L1_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet0, inputGetValue(gGamepadIndex, GPAD_R1), CONTROLLER_R1_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet0, inputGetValue(gGamepadIndex, GPAD_L3), CONTROLLER_L3_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet0, inputGetValue(gGamepadIndex, GPAD_R3), CONTROLLER_R3_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet0, inputGetValue(gGamepadIndex, GPAD_START), CONTROLLER_START_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet0, inputGetValue(gGamepadIndex, GPAD_BACK), CONTROLLER_SELECT_BIT);
            break;
        }
        case INPUT_DEVICE_KBM:
        {
            SET_BUTTON_BIT(gConstantData.buttonSet0, inputGetValue(0, K_ESCAPE), ESCAPE_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet0, inputGetValue(0, K_F1), F1_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet0, inputGetValue(0, K_F2), F2_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet0, inputGetValue(0, K_F3), F3_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet0, inputGetValue(0, K_F4), F4_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet0, inputGetValue(0, K_F5), F5_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet0, inputGetValue(0, K_F6), F6_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet0, inputGetValue(0, K_F7), F7_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet0, inputGetValue(0, K_F8), F8_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet0, inputGetValue(0, K_F9), F9_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet0, inputGetValue(0, K_F10), F10_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet0, inputGetValue(0, K_F11), F11_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet0, inputGetValue(0, K_F12), F12_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet0, inputGetValue(0, K_INS), INSERT_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet0, inputGetValue(0, K_DEL), DEL_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet0, inputGetValue(0, K_KP_NUMLOCK), NUM_LOCK_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet0, inputGetValue(0, K_KP_STAR), KP_MULTIPLY_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet0, inputGetValue(0, K_1), NUM_1_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet0, inputGetValue(0, K_2), NUM_2_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet0, inputGetValue(0, K_3), NUM_3_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet0, inputGetValue(0, K_4), NUM_4_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet0, inputGetValue(0, K_5), NUM_5_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet0, inputGetValue(0, K_6), NUM_6_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet0, inputGetValue(0, K_7), NUM_7_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet0, inputGetValue(0, K_8), NUM_8_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet0, inputGetValue(0, K_9), NUM_9_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet0, inputGetValue(0, K_0), NUM_0_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet0, inputGetValue(0, K_MINUS), MINUS_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet0, inputGetValue(0, K_EQUAL), EQUAL_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet0, inputGetValue(0, K_BACKSPACE), BACK_SPACE_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet1, inputGetValue(0, K_KP_PLUS), KP_ADD_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet1, inputGetValue(0, K_KP_MINUS), KP_SUBTRACT_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet1, inputGetValue(0, K_TAB), TAB_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet1, inputGetValue(0, K_Q), Q_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet1, inputGetValue(0, K_W), W_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet1, inputGetValue(0, K_E), E_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet1, inputGetValue(0, K_R), R_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet1, inputGetValue(0, K_T), T_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet1, inputGetValue(0, K_Y), Y_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet1, inputGetValue(0, K_U), U_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet1, inputGetValue(0, K_I), I_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet1, inputGetValue(0, K_O), O_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet1, inputGetValue(0, K_P), P_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet1, inputGetValue(0, K_LEFTBRACKET), BRACKET_LEFT_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet1, inputGetValue(0, K_RIGHTBRACKET), BRACKET_RIGHT_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet1, inputGetValue(0, K_BACKSLASH), BACK_SLASH_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet1, inputGetValue(0, K_KP_HOME), KP_7_HOME_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet1, inputGetValue(0, K_KP_UPARROW), KP_8_UP_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet1, inputGetValue(0, K_KP_PGUP), KP_9_PAGE_UP_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet1, inputGetValue(0, K_CAPSLOCK), CAPS_LOCK_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet1, inputGetValue(0, K_A), A_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet1, inputGetValue(0, K_S), S_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet1, inputGetValue(0, K_D), D_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet1, inputGetValue(0, K_F), F_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet1, inputGetValue(0, K_G), G_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet1, inputGetValue(0, K_H), H_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet1, inputGetValue(0, K_J), J_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet1, inputGetValue(0, K_K), K_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet1, inputGetValue(0, K_L), L_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet1, inputGetValue(0, K_SEMICOLON), SEMICOLON_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet1, inputGetValue(0, K_APOSTROPHE), APOSTROPHE_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet1, inputGetValue(0, K_ENTER), ENTER_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet2, inputGetValue(0, K_KP_LEFTARROW), KP_4_LEFT_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet2, inputGetValue(0, K_KP_RIGHTARROW), KP_6_RIGHT_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet2, inputGetValue(0, K_LSHIFT), SHIFT_L_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet2, inputGetValue(0, K_Z), Z_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet2, inputGetValue(0, K_X), X_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet2, inputGetValue(0, K_C), C_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet2, inputGetValue(0, K_V), V_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet2, inputGetValue(0, K_B), B_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet2, inputGetValue(0, K_N), N_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet2, inputGetValue(0, K_M), M_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet2, inputGetValue(0, K_COMMA), COMMA_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet2, inputGetValue(0, K_PERIOD), PERIOD_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet2, inputGetValue(0, K_SLASH), FWRD_SLASH_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet2, inputGetValue(0, K_RSHIFT), SHIFT_R_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet2, inputGetValue(0, K_KP_END), KP_1_END_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet2, inputGetValue(0, K_KP_DOWNARROW), KP_2_DOWN_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet2, inputGetValue(0, K_KP_PGDN), KP_3_PAGE_DOWN_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet2, inputGetValue(0, K_LCTRL), CTRL_L_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet2, inputGetValue(0, K_LALT), ALT_L_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet2, inputGetValue(0, K_SPACE), SPACE_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet2, inputGetValue(0, K_RALT), ALT_R_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet2, inputGetValue(0, K_RCTRL), CTRL_R_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet2, inputGetValue(0, K_LEFTARROW), LEFT_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet2, inputGetValue(0, K_RIGHTARROW), RIGHT_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet2, inputGetValue(0, K_UPARROW), UP_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet2, inputGetValue(0, K_DOWNARROW), DOWN_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet2, inputGetValue(0, K_KP_INS), KP_0_INSERT_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet2, inputGetValue(0, K_KP_NUMPAD_5), KP_5_BEGIN_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet2, inputGetValue(0, MOUSE_1), LEFT_CLICK_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet2, inputGetValue(0, MOUSE_2), RIGHT_CLICK_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet2, inputGetValue(0, MOUSE_3), MID_CLICK_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet2, inputGetValue(0, MOUSE_WHEEL_UP), SCROLL_UP_BIT);
            SET_BUTTON_BIT(gConstantData.buttonSet3, inputGetValue(0, MOUSE_WHEEL_DOWN), SCROLL_DOWN_BIT);
            // SET_BUTTON_BIT(gConstantData.buttonSet0, ctx->mBool, BREAK_BIT);
            // SET_BUTTON_BIT(gConstantData.buttonSet0, ctx->mBool, ACUTE_BIT);
            break;
        }
#if defined(ENABLE_FORGE_TOUCH_INPUT)
        case INPUT_DEVICE_TOUCH:
            break;
#endif
        }

        return true;
    }
    static void callbackStackPush(const char* text, ...)
    {
        if (gCallbackStackDisplayIndex == gCallbackStackSize)
        {
            for (uint32_t i = 1; i < gCallbackStackSize; ++i)
            {
                strcpy(gCallbackStack[i - 1], gCallbackStack[i]);
            }
            gCallbackStackDisplayIndex--;
        }
        va_list list;
        va_start(list, text);
        vsnprintf(gCallbackStack[gCallbackStackDisplayIndex], sizeof(gCallbackStack[gCallbackStackDisplayIndex]), text, list);
        va_end(list);
        gCallbackStackDisplayIndex++;
        gCallbackStackIndex++;
    }
    static void onControllerAdded(TFInputPortIndex port)
    {
        callbackStackPush("%i: Controller added at port: %i", gCallbackStackIndex, port);
    }
    static void onControllerRemoved(TFInputPortIndex port)
    {
        callbackStackPush("%i: Controller removed at port: %i", gCallbackStackIndex, port);
    }
    static void onDeviceSwitch(void* pUserData)
    {
        UNREF_PARAM(pUserData);
        waitQueueIdle(pGraphicsQueue);
        gConstantData.deviceType = gChosenDeviceType;

        switch (gConstantData.deviceType)
        {
        case INPUT_DEVICE_GAMEPAD:
        {
            pInputDeviceTexture = pGamepad;
            break;
        }
        case INPUT_DEVICE_KBM:
        {
            pInputDeviceTexture = pKeyboardMouse;
            break;
        }
#if defined(ENABLE_FORGE_TOUCH_INPUT)
        case INPUT_DEVICE_TOUCH:
        {
            break;
        }
#endif
        }
    };

    static void onLightsAndRumbleSwitch(void* pUserData)
    {
        UNREF_PARAM(pUserData);
        // Reset controller events
        TFInputEffectValue effectValue = {};
        inputSetEffect(gGamepadIndex, TF_INPUT_EFFECT_GPAD_RUMBLE_LOW, &effectValue);
        inputSetEffect(gGamepadIndex, TF_INPUT_EFFECT_GPAD_RUMBLE_HIGH, &effectValue);
        inputSetEffect(gGamepadIndex, TF_INPUT_EFFECT_GPAD_LIGHT_RESET, &effectValue);
    }

    void updateRumbleAndLightsTestSequence(float deltaTime)
    {
        static float total = 0.0f;
        const float  threshold = 1.0f;
        total += deltaTime;
        bool set = total >= threshold;
        if (set)
        {
            total = 0.0f;
            TFInputEffectValue effectValue = {};
            effectValue.mRumble = randomFloat(0.0f, 1.0f);
            inputSetEffect(gGamepadIndex, TF_INPUT_EFFECT_GPAD_RUMBLE_LOW, &effectValue);
            effectValue.mRumble = randomFloat(0.0f, 0.5f);
            inputSetEffect(gGamepadIndex, TF_INPUT_EFFECT_GPAD_RUMBLE_HIGH, &effectValue);
            effectValue.mLight = { randomFloat(0.0f, 1.0f), randomFloat(0.0f, 1.0f), randomFloat(0.0f, 1.0f) };
            inputSetEffect(gGamepadIndex, TF_INPUT_EFFECT_GPAD_LIGHT, &effectValue);
        }
    }

#if defined(ENABLE_FORGE_TOUCH_INPUT)
    static void onTouchEvent(const TouchEvent* event, void* pData)
    {
        UNREF_PARAM(pData);
        if (TOUCH_BEGAN == event->mPhase)
        {
            for (uint32_t t = 0; t < TF_ARRAY_COUNT(gTouches); ++t)
            {
                if (TOUCH_ID_INVALID == gTouches[t].mId)
                {
                    gTouches[t] = *event;
                    break;
                }
            }
        }
        else if (TOUCH_MOVED == event->mPhase)
        {
            for (uint32_t t = 0; t < TF_ARRAY_COUNT(gTouches); ++t)
            {
                if (event->mId == gTouches[t].mId)
                {
                    gTouches[t] = *event;
                    break;
                }
            }
        }
        else
        {
            for (uint32_t t = 0; t < TF_ARRAY_COUNT(gTouches); ++t)
            {
                if (event->mId == gTouches[t].mId)
                {
                    gTouches[t].mId = TOUCH_ID_INVALID;
                    break;
                }
            }
        }
    }
#endif

    void updateGui()
    {
        for (uint32_t i = 0; i < TF_ARRAY_COUNT(gGamepadNames); ++i)
        {
            snprintf(gGamepadNames[i], TF_ARRAY_COUNT(gGamepadNames[i]), "Gamepad[%u] %s", i,
                     (inputGamepadIsActive(i) ? inputGamepadName(i) : "Not connected"));
        }

        if (UI_WINDOW_IS_VISIBLE(uiBeginWidgetWindow(&gWindowDesc)))
        {
            uiLayoutAutoTextRows(2);
            uiLabel("Device Type: ", TF_ALIGN_LEFT);
            uint32_t deviceTypeBefore = gChosenDeviceType;
            gChosenDeviceType = UI_WIDGET_GET_SELECTED(uiDropdown(gEnumNames, TF_ARRAY_COUNT(gEnumNames), gChosenDeviceType));
            if (gChosenDeviceType != deviceTypeBefore)
            {
                onDeviceSwitch(NULL);
            }

            uiLayoutAutoTextRows(1);
            if (UI_WIDGET_IS_CHANGED(uiCheckbox("Enable Rumble and Lights Test Sequence:", &gEnableRumbleAndLights)))
            {
                onLightsAndRumbleSwitch(NULL);
            }

            uiLayoutAutoTextRows(2);
            uiLabel("Gamepad", TF_ALIGN_LEFT);
            static const char* gamepadNames[TF_MAX_GAMEPADS] = {};
            for (uint32_t i = 0; i < TF_ARRAY_COUNT(gamepadNames); ++i)
            {
                gamepadNames[i] = gGamepadNames[i];
            }
            gGamepadIndex = UI_WIDGET_GET_SELECTED(uiDropdown(gamepadNames, TF_MAX_GAMEPADS, gGamepadIndex));

            if (UI_WIDGET_IS_VISIBLE(uiCollapsingHeaderBegin("Gamepad Callback Stack", true)))
            {
                if (gCallbackStackDisplayIndex < gCallbackStackIndex)
                {
                    uiLayoutAutoTextRows(1);
                    uiLabel("................................", TF_ALIGN_LEFT);
                }
                for (uint32_t i = 0; i < gCallbackStackSize; ++i)
                {
                    uiLayoutAutoTextRows(1);
                    uiLabel(gCallbackStack[i], TF_ALIGN_LEFT);
                }
                uiCollapsingHeaderEnd();
            }

#if defined(HOLOLENS2)
            if (UI_WIDGET_IS_VISIBLE(uiCollapsingHeaderBegin("HoloLens2 Input", true)))
            {
                char valueText[64] = {};

                size_t leftPinch = (size_t)(saturate(inputGetValue(0, HAND_L_PINCHING)) * 100.0f);
                uiLayoutAutoTextRows(1);
                uiLabel("Left Pinch", TF_ALIGN_LEFT);
                uiProgressBar(&leftPinch, 100, false);

                size_t rightPinch = (size_t)(saturate(inputGetValue(0, HAND_R_PINCHING)) * 100.0f);
                uiLayoutAutoTextRows(1);
                uiLabel("Right Pinch", TF_ALIGN_LEFT);
                uiProgressBar(&rightPinch, 100, false);

                uiLayoutAutoTextRows(2);
                uiLabel("Left Poke", TF_ALIGN_LEFT);
                snprintf(valueText, sizeof(valueText), "% .2f  % .2f  % .2f", inputGetValue(0, HAND_L_POKE_PX),
                         inputGetValue(0, HAND_L_POKE_PY), inputGetValue(0, HAND_L_POKE_PZ));
                uiLabel(valueText, TF_ALIGN_RIGHT);

                uiLayoutAutoTextRows(2);
                uiLabel("Right Poke", TF_ALIGN_LEFT);
                snprintf(valueText, sizeof(valueText), "% .2f  % .2f  % .2f", inputGetValue(0, HAND_R_POKE_PX),
                         inputGetValue(0, HAND_R_POKE_PY), inputGetValue(0, HAND_R_POKE_PZ));
                uiLabel(valueText, TF_ALIGN_RIGHT);

                uiLayoutAutoTextRows(2);
                uiLabel("Eye Gaze Dir", TF_ALIGN_LEFT);
                snprintf(valueText, sizeof(valueText), "% .2f  % .2f  % .2f", inputGetValue(0, EYE_GAZE_DX), inputGetValue(0, EYE_GAZE_DY),
                         inputGetValue(0, EYE_GAZE_DZ));
                uiLabel(valueText, TF_ALIGN_RIGHT);

                const bool rightAimTracked = inputGetValue(0, VRCTRL_RTR) != 0.0f;
                uiLayoutAutoTextRows(2);
                uiLabel("Right Aim Ray", TF_ALIGN_LEFT);
                uiColorLabel(rightAimTracked ? "tracked" : "not tracked", TF_ALIGN_RIGHT,
                             rightAimTracked ? float4(0.40f, 0.90f, 0.40f, 1.0f) : float4(0.90f, 0.45f, 0.45f, 1.0f));

                uiLayoutAutoTextRows(2);
                uiLabel("  Position", TF_ALIGN_LEFT);
                snprintf(valueText, sizeof(valueText), "% .2f  % .2f  % .2f", inputGetValue(0, VRCTRL_RPX), inputGetValue(0, VRCTRL_RPY),
                         inputGetValue(0, VRCTRL_RPZ));
                uiLabel(valueText, TF_ALIGN_RIGHT);

                uiLayoutAutoTextRows(2);
                uiLabel("  Direction", TF_ALIGN_LEFT);
                snprintf(valueText, sizeof(valueText), "% .2f  % .2f  % .2f", inputGetValue(0, VRCTRL_RDX), inputGetValue(0, VRCTRL_RDY),
                         inputGetValue(0, VRCTRL_RDZ));
                uiLabel(valueText, TF_ALIGN_RIGHT);

                uiCollapsingHeaderEnd();
            }
#endif

#if defined(QUEST_VR)
            if (UI_WIDGET_IS_VISIBLE(uiCollapsingHeaderBegin("VR anchors", false)))
            {
                uiLayoutAutoTextRows(1);
                if (UI_WIDGET_IS_CHANGED(uiCheckbox("Enable VR Passthrough:", &gEnableVRPassThrough)))
                {
                    TFReloadDesc reloadDescriptor;
                    reloadDescriptor.mType = TF_RELOAD_TYPE_RENDERTARGET;
                    requestReload(&reloadDescriptor);
                }
                uiLayoutAutoRows(2);
                uiLabel("Add Anchor:", TF_ALIGN_LEFT);
                if (UI_WIDGET_IS_PRESSED(uiButton("+")))
                {
                    addVRAnchor();
                }

                uiLabel("Query Anchor:", TF_ALIGN_LEFT);
                if (UI_WIDGET_IS_PRESSED(uiButton("+")))
                {
                    RequestQueryAllStoredAnchor();
                }

                uiLabel("Save Anchor:", TF_ALIGN_LEFT);
                if (UI_WIDGET_IS_PRESSED(uiButton("+")))
                {
                    saveVRAnchor();
                }

                uiLayoutAutoRows(1);
                uiLayoutAutoTextRows(1);
                uiLabel("Anchors List:", TF_ALIGN_LEFT);

                uint32_t anchorCount = GetXRAnchorCount();
                GetXRAnchors(gHandles, anchorCount);

                uiLayoutAutoRows(2);
                for (uint32_t i = 0; i < anchorCount; i++)
                {
                    TFAnchorHandle handle = gHandles[i];
                    const char*    validStr = isXRAnchorValid(handle) ? "valid" : "invalid";
                    char           uuidStr[XR_UUID_LENGTH] = { 0 };
                    XrUuidEXTToString(&handle->mUuid, uuidStr);
                    float stagePosX = -1.0f;
                    float stagePosY = -1.0f;
                    float stagePosZ = -1.0f;

                    if (isXRAnchorValid(handle))
                    {
                        XrSpaceLocation location = {};
                        location.type = XR_TYPE_SPACE_LOCATION;
                        location.next = nullptr;
                        XrFrameState* pXrFrameState = getXRCurrentFrameState();
                        XrResult result = xrLocateSpace(handle->mSpace, getXRStageSpace(), pXrFrameState->predictedDisplayTime, &location);
                        if (XR_SUCCEEDED(result))
                        {
                            stagePosX = location.pose.position.x;
                            stagePosY = location.pose.position.y;
                            stagePosZ = location.pose.position.z;
                        }
                    }

                    bformat(&gAnchorDescription[i], "Anchor %s: %s, %f, %f, %f", uuidStr, validStr, stagePosX, stagePosY, stagePosZ);
                    uiText(&gAnchorDescription[i], TF_ALIGN_LEFT);

                    if (UI_WIDGET_IS_PRESSED(uiButton("-")))
                    {
                        removeVRAnchor(i);
                    }
                }
                uiCollapsingHeaderEnd();
            }
#endif
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
#if defined(QUEST_VR)
        if (gEnableVRPassThrough)
        {
            swapChainDesc.mFlags |= TF_SWAP_CHAIN_CREATION_FLAG_ENABLE_VR_PASSTHROUGH;
        }
#endif
        swapChainDesc.mVR.m2DLayer = gVR2DLayer;

        ::addSwapChain(pRenderer, &swapChainDesc, &pSwapChain);

        return pSwapChain != NULL;
    }

    void addDescriptorSets()
    {
        TFDescriptorSetDesc desc = SRT_SET_DESC(SrtData, Persistent, 1, 0);
        addDescriptorSet(pRenderer, &desc, &pDescriptorSetPersistent);

        desc = SRT_SET_DESC(SrtData, PerFrame, gDataBufferCount, 0);
        addDescriptorSet(pRenderer, &desc, &pDescriptorSetPerFrame);
    }

    void removeDescriptorSets()
    {
        removeDescriptorSet(pRenderer, pDescriptorSetPersistent);
        removeDescriptorSet(pRenderer, pDescriptorSetPerFrame);
    }

    void addShaders()
    {
        TFShaderLoadDesc basicShader = {};
        basicShader.mVert.pFileName = "basic.vert";
        basicShader.mFrag.pFileName = "basic.frag";
        addShader(pRenderer, &basicShader, &pBasicShader);

#if defined(HOLOLENS2)
        TFShaderLoadDesc handJointShader = {};
        handJointShader.mVert.pFileName = "handjoints.vert";
        handJointShader.mFrag.pFileName = "handjoints.frag";
        addShader(pRenderer, &handJointShader, &pHandJointShader);
#endif
    }

    void removeShaders()
    {
        removeShader(pRenderer, pBasicShader);
#if defined(HOLOLENS2)
        removeShader(pRenderer, pHandJointShader);
#endif
    }

    void addPipelines()
    {
        // Layout and pipeline for device and input draw
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

        TFRasterizerStateDesc rasterizerStateDesc = {};
        rasterizerStateDesc.mCullMode = TF_CULL_MODE_BACK;

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
        pipelineSettings.pShaderProgram = pBasicShader;
        pipelineSettings.pVertexLayout = &vertexLayout;
        pipelineSettings.pRasterizerState = &rasterizerStateDesc;
        addPipeline(pRenderer, &desc, &pBasicPipeline);

#if defined(HOLOLENS2)
        TFVertexLayout handVertexLayout = {};
        handVertexLayout.mBindingCount = 1;
        handVertexLayout.mAttribCount = 2;
        handVertexLayout.mAttribs[0].mSemantic = TF_SEMANTIC_POSITION;
        handVertexLayout.mAttribs[0].mFormat = TinyImageFormat_R32G32B32A32_SFLOAT;
        handVertexLayout.mAttribs[0].mBinding = 0;
        handVertexLayout.mAttribs[0].mLocation = 0;
        handVertexLayout.mAttribs[0].mOffset = 0;
        handVertexLayout.mAttribs[1].mSemantic = TF_SEMANTIC_NORMAL;
        handVertexLayout.mAttribs[1].mFormat = TinyImageFormat_R32G32B32A32_SFLOAT;
        handVertexLayout.mAttribs[1].mBinding = 0;
        handVertexLayout.mAttribs[1].mLocation = 1;
        handVertexLayout.mAttribs[1].mOffset = 4 * sizeof(float);

        TFRasterizerStateDesc handRasterizerStateDesc = {};
        handRasterizerStateDesc.mCullMode = TF_CULL_MODE_NONE;

        TFDepthStateDesc handDepthStateDesc = {};
        handDepthStateDesc.mDepthTest = true;
        handDepthStateDesc.mDepthWrite = true;
        handDepthStateDesc.mDepthFunc = TF_CMP_GEQUAL;

        TFPipelineDesc handDesc = {};
        PIPELINE_LAYOUT_DESC(handDesc, SRT_LAYOUT_DESC(SrtData, Persistent), SRT_LAYOUT_DESC(SrtData, PerFrame), NULL, NULL);
        handDesc.mType = TF_PIPELINE_TYPE_GRAPHICS;
        TFGraphicsPipelineDesc& handPipelineSettings = handDesc.mGraphicsDesc;
        handPipelineSettings.mPrimitiveTopo = TF_PRIMITIVE_TOPO_TRI_LIST;
        handPipelineSettings.mRenderTargetCount = 1;
        handPipelineSettings.pDepthState = &handDepthStateDesc;
        handPipelineSettings.pColorFormats = &pSwapChain->ppRenderTargets[0]->mFormat;
        handPipelineSettings.mSampleCount = pSwapChain->ppRenderTargets[0]->mSampleCount;
        handPipelineSettings.mSampleQuality = pSwapChain->ppRenderTargets[0]->mSampleQuality;
        handPipelineSettings.mDepthStencilFormat = pDepthBuffer->mFormat;
        handPipelineSettings.pShaderProgram = pHandJointShader;
        handPipelineSettings.pVertexLayout = &handVertexLayout;
        handPipelineSettings.pRasterizerState = &handRasterizerStateDesc;
        addPipeline(pRenderer, &handDesc, &pHandJointPipeline);
#endif
    }

    void removePipelines()
    {
        removePipeline(pRenderer, pBasicPipeline);
#if defined(HOLOLENS2)
        removePipeline(pRenderer, pHandJointPipeline);
#endif
    }

#if defined(HOLOLENS2)
    bool addDepthBuffer()
    {
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
        depthRT.mFlags = TF_TEXTURE_CREATION_FLAG_ON_TILE | TF_TEXTURE_CREATION_FLAG_VR_MULTIVIEW;
        addRenderTarget(pRenderer, &depthRT, &pDepthBuffer);

        return pDepthBuffer != NULL;
    }
#endif

    void updateDescriptorSets()
    {
        TFDescriptorData param = {};
        param.mIndex = SRT_RES_IDX(SrtData, Persistent, gTexture);
        switch (gConstantData.deviceType)
        {
        case INPUT_DEVICE_GAMEPAD:
            param.ppTextures = &pGamepad;
            updateDescriptorSet(pRenderer, 0, pDescriptorSetPersistent, 1, &param);
            break;
        case INPUT_DEVICE_KBM:
            param.ppTextures = &pKeyboardMouse;
            updateDescriptorSet(pRenderer, 0, pDescriptorSetPersistent, 1, &param);
            break;
#if defined(ENABLE_FORGE_TOUCH_INPUT)
        case INPUT_DEVICE_TOUCH:
            break;
#endif
        }

        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            TFDescriptorData uParam = {};
            uParam.mIndex = SRT_RES_IDX(SrtData, PerFrame, gInputData);
            uParam.ppBuffers = &pInputDataUniformBuffer[i];
            updateDescriptorSet(pRenderer, i, pDescriptorSetPerFrame, 1, &uParam);
        }

#if defined(HOLOLENS2)
        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            TFDescriptorData hParam = {};
            hParam.mIndex = SRT_RES_IDX(SrtData, PerFrame, gHandJointData);
            hParam.ppBuffers = &pHandJointUniformBuffer[i];
            updateDescriptorSet(pRenderer, i, pDescriptorSetPerFrame, 1, &hParam);
        }
#endif
    }
};

#if defined(QUEST_VR)

void addVRAnchor()
{
    XrSpace headSetSpace = getXRViewSpace();
    XrSpace stageSpace = getXRStageSpace();

    XrFrameState*   pXrFrameState = getXRCurrentFrameState();
    XrSpaceLocation location{ XR_TYPE_SPACE_LOCATION };
    XrResult        result = xrLocateSpace(headSetSpace, stageSpace, pXrFrameState->predictedDisplayTime, &location);

    if (XR_SUCCEEDED(result))
    {
        RequestXRAnchorCreation(stageSpace, location.pose);
    }
}

void saveVRAnchor()
{
    uint32_t anchorCount = GetXRAnchorCount();
    GetXRAnchors(gHandles, anchorCount);
    RequestSaveXRAnchors(gHandles, anchorCount);
}

void removeVRAnchor(uint32_t index) { RequestXRAnchorDeletion(gHandles[index]); }

void vrAnchorAdded(TFAnchorHandle handle)
{
    char uuidStr[XR_UUID_LENGTH] = { 0 };
    XrUuidEXTToString(&handle->mUuid, uuidStr);
    LOGF(eINFO, "anchor created: %s", uuidStr);
}

void vrAnchorReady(TFAnchorHandle handle)
{
    XrSpaceLocation location{};
    location.type = XR_TYPE_SPACE_LOCATION;
    location.next = nullptr;
    XrFrameState* pXrFrameState = getXRCurrentFrameState();
    XrResult      result = xrLocateSpace(handle->mSpace, getXRStageSpace(), pXrFrameState->predictedDisplayTime, &location);

    if (XR_SUCCEEDED(result))
    {
        float stagePosX = location.pose.position.x;
        float stagePosY = location.pose.position.y;
        float stagePosZ = location.pose.position.z;
        char  uuidStr[XR_UUID_LENGTH] = { 0 };
        XrUuidEXTToString(&handle->mUuid, uuidStr);
        LOGF(eINFO, "anchor ready stage %s: %f, %f, %f", uuidStr, stagePosX, stagePosY, stagePosZ);
    }
}

void vrAnchorRemoved(XrUuidEXT uuid)
{
    char uuidStr[XR_UUID_LENGTH] = { 0 };
    XrUuidEXTToString(&uuid, uuidStr);
    LOGF(eINFO, "anchor deleted: %s", uuidStr);
}
#endif

DEFINE_APPLICATION_MAIN(Input)
