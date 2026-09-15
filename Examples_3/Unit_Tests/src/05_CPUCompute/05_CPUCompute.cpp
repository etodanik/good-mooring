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

// Interfaces
#include "../../../../Common_3/Application/Interfaces/IApp.h"
#include "../../../../Common_3/Application/Interfaces/ICamera.h"
#include "../../../../Common_3/Application/Interfaces/IFont.h"
#include "../../../../Common_3/Application/Interfaces/IProfiler.h"
#include "../../../../Common_3/Application/Interfaces/IScreenshot.h"
#include "../../../../Common_3/Application/Interfaces/IUI.h"
#include "../../../../Common_3/Game/Interfaces/IScripting.h"
#include "../../../../Common_3/Utilities/Interfaces/IFileSystem.h"
#include "../../../../Common_3/Utilities/Interfaces/ILog.h"
#include "../../../../Common_3/Utilities/Interfaces/ITime.h"

#include "../../../../Common_3/Utilities/RingBuffer.h"

// Renderer
#include "../../../../Common_3/Graphics/Interfaces/IGraphics.h"
#include "../../../../Common_3/Resources/ResourceLoader/Interfaces/IResourceLoader.h"

#include "../../../../Common_3/Utilities/Interfaces/IMath.h"

#include "../../../../Common_3/Utilities/Interfaces/IMemory.h"

#include "../../../../Common_3/Utilities/Threading/ThreadSystem.h"

#include "../../../../Common_3/Utilities/ISPCRuntime.h"

#include "../../../../Common_3/Graphics/FSL/fsl_srt.h"
#include "../../../../Common_3/Graphics/FSL/defaults.h"
#include "./Shaders/FSL/Basic.srt.h"

// This header is generated from the ISPC shaders
// Filename is determined by the name we specify in CpuShaders.list + '.h'
#include "FillImage.h"

#define IMAGE_WIDTH  1280
#define IMAGE_HEIGHT 720

typedef struct ISPCTexture2D
{
    TinyImageFormat format;
    uint32_t        width;
    uint32_t        height;
    uint8_t*        data;
} ISPCTexture2D;

typedef struct ImageVertex
{
    float4 mPosition;
    float2 mTexCoords;
} ImageVertex;

typedef enum ImageFillMode
{
    FILL_MODE_GPU,
    FILL_MODE_CPU,
} ImageFillMode;
const char* pFillModeStrings[] = { "Gpu", "Cpu (ISPC)" };
typedef enum ImageFillPattern
{
    FILL_PATTERN_GRADIENT,
    FILL_PATTERN_MOVING_CHECKERBOARD,
    FILL_PATTERN_CIRCULAR_RIPPLE,
    FILL_PATTERN_STRIPES,
} ImageFillPattern;
const char* pFillPatternStrings[] = { "Gradient", "Moving Checkerboard", "Circular Ripple", "Stripes" };

typedef struct FillImageData
{
    float2           mImageSize;
    float            mTime;
    ImageFillPattern mPattern;
} FillImageData;

TFRenderer* pRenderer = NULL;
TFQueue*    pComputeQueue = NULL;
TFQueue*    pGraphicsQueue = NULL;
GpuCmdRing  gComputeCmdRing = {};
GpuCmdRing  gGraphicsCmdRing = {};

TFSwapChain* pSwapChain = NULL;
TFSemaphore* pImageAcquiredSemaphore = NULL;

const uint32_t   gDataBufferCount = 2;
ProfileToken     gGraphicsGpuToken = PROFILE_INVALID_TOKEN;
ProfileToken     gComputeGpuToken = PROFILE_INVALID_TOKEN;
ImageFillMode    gFillMode = FILL_MODE_GPU;
ImageFillPattern gFillPattern = FILL_PATTERN_GRADIENT;

TFUIWindowDesc gGuiWindowDesc;

static TFFont* gFont = NULL;

TFVR2DLayerDesc gVR2DLayer{ { 0.0f, 0.0f, -1.0f }, { 0.0f, 0.0f, 0.0f, 1.0f }, 1.0f };
TFFontDrawDesc  gFrameTimeDraw;

TFTexture* pImagesGPU[gDataBufferCount] = {};

uint8_t* pImageCPU = 0;

TFBuffer* pTransferDestBuffer = 0;

TFShader* pBasicShader = 0;
TFShader* pImageFillShader = 0;

TFBuffer* pImageVerticesBuffer = 0;

TFDescriptorSet* pDescriptorSetTextures = 0;

TFPipeline* pDrawImagesPipeline = 0;
TFPipeline* pFillImagesPipeline = 0;

FillImageData gFillImageUniformData = {};
TFBuffer*     pFillImageDataBuffer[gDataBufferCount] = {};

int64_t gStartTimeUSec = 0;

class CPUCompute: public IApp
{
public:
    bool Init()
    {
        gStartTimeUSec = getUSec(true);

        // window and renderer setup
        TFRendererDesc settings;
        memset(&settings, 0, sizeof(settings));
        initGPUConfig(settings.pExtendedSettings);
        initRenderer(GetName(), &settings, &pRenderer);
        // check for init success
        if (!pRenderer)
        {
            ShowUnsupportedMessage("Failed To Initialize renderer!");
            return false;
        }
        setGPUConfig(pRenderer->pContext->mGpus, pRenderer->pContext->mGpuCount, (uint32_t)(pRenderer->pGpu - pRenderer->pContext->mGpus),
                     settings.pExtendedSettings);

        mSettings.mFrameMaxCount = gDataBufferCount;

        TFQueueDesc queueDesc = {};
        queueDesc.mType = TF_QUEUE_TYPE_GRAPHICS;
        queueDesc.mFlag = TF_QUEUE_FLAG_INIT_MICROPROFILE;
        initQueue(pRenderer, &queueDesc, &pGraphicsQueue);

        queueDesc = {};
        queueDesc.mType = TF_QUEUE_TYPE_COMPUTE;
        queueDesc.mFlag = TF_QUEUE_FLAG_INIT_MICROPROFILE;
        initQueue(pRenderer, &queueDesc, &pComputeQueue);

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

        initSemaphore(pRenderer, &pImageAcquiredSemaphore);

        initResourceLoaderInterface(pRenderer);

        addRootSignatures();

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

        gGraphicsGpuToken = initGpuProfiler(pRenderer, pGraphicsQueue, "Graphics");
        gComputeGpuToken = initGpuProfiler(pRenderer, pComputeQueue, "Compute");

        AddCustomInputBindings();

        for (uint32_t i = 0; i < gDataBufferCount; i += 1)
        {
            TFTextureLoadDesc loadDesc = {};
            TFTextureDesc     textureDesc = {};
            textureDesc.mArraySize = 1;
            textureDesc.mDepth = 1;
            textureDesc.mFormat = TinyImageFormat_R8G8B8A8_UNORM;
            textureDesc.mWidth = IMAGE_WIDTH;
            textureDesc.mHeight = IMAGE_HEIGHT;
            textureDesc.mMipLevels = 1;
            textureDesc.mSampleCount = TF_SAMPLE_COUNT_1;
            textureDesc.mStartState = TF_RESOURCE_STATE_UNORDERED_ACCESS;
            textureDesc.mDescriptors = TF_DESCRIPTOR_TYPE_RW_TEXTURE;
            textureDesc.pName = "image";

            loadDesc.pDesc = &textureDesc;
            loadDesc.ppTexture = &pImagesGPU[i];
            loadDesc.mCreationFlag = (TFTextureCreationFlags)0;
            addResource(&loadDesc, NULL);
            waitForAllResourceLoads();

            if (pImagesGPU[i] == NULL)
            {
                LOGF(eERROR, "Failed creating GPU diff texture");
                return false;
            }
        }
        pImageCPU = (uint8_t*)tf_malloc(IMAGE_WIDTH * IMAGE_HEIGHT * 4);

        TFBufferLoadDesc ubDesc = {};
        ubDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        ubDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
        ubDesc.mDesc.mFlags = TF_BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
        ubDesc.pData = NULL;
        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            ubDesc.mDesc.pName = "FillImageData";
            ubDesc.mDesc.mSize = sizeof(FillImageData);
            ubDesc.ppBuffer = &pFillImageDataBuffer[i];
            addResource(&ubDesc, NULL);
        }

        initScreenshotCapturer(pRenderer, pGraphicsQueue, GetName());

        initISPCRuntime();

        return true;
    }

    void Exit()
    {
        exitISPCRuntime();

        exitScreenshotCapturer();

        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            removeResource(pFillImageDataBuffer[i]);
        }

        tf_free(pImageCPU);

        for (uint32_t i = 0; i < gDataBufferCount; i += 1)
            removeResource(pImagesGPU[i]);

        exitUserInterface();

        removeFont(gFont);
        exitFontSystem();

        exitProfiler();

        exitGpuCmdRing(pRenderer, &gComputeCmdRing);
        exitGpuCmdRing(pRenderer, &gGraphicsCmdRing);
        exitSemaphore(pRenderer, pImageAcquiredSemaphore);

        removeRootSignatures();

        exitResourceLoaderInterface(pRenderer);

        exitQueue(pRenderer, pComputeQueue);
        exitQueue(pRenderer, pGraphicsQueue);

        exitRenderer(pRenderer);
        exitGPUConfig();
        pRenderer = NULL;
    }

    bool Load(TFReloadDesc* pReloadDesc)
    {
        if (pReloadDesc->mType & TF_RELOAD_TYPE_SHADER)
        {
            addShaders();
            addDescriptorSets();
        }

        if (pReloadDesc->mType & (TF_RELOAD_TYPE_RESIZE | TF_RELOAD_TYPE_RENDERTARGET))
        {
            // We only need to reload gui when the size of window changed
            loadProfilerUI(mSettings.mWidth, mSettings.mHeight);

            gGuiWindowDesc = {};
            gGuiWindowDesc.mStartPos = vec2(mSettings.mWidth * 0.01f, mSettings.mHeight * 0.2f);
            gGuiWindowDesc.mStartSize = vec2(600.0f, 550.0f);
            gGuiWindowDesc.pWindowTitle = GetName();
            gGuiWindowDesc.mFlags = TF_UI_WINDOW_TITLE | TF_UI_WINDOW_SCALABLE | TF_UI_WINDOW_MOVABLE | TF_UI_WINDOW_MINIMIZABLE |
                                    TF_UI_WINDOW_BORDER | TF_UI_WINDOW_INIT_HEIGHT_FIT;

            if (!addSwapChain())
                return false;
        }

        if (pReloadDesc->mType & (TF_RELOAD_TYPE_SHADER | TF_RELOAD_TYPE_RENDERTARGET))
        {
            if (!addPipelines())
                return false;
        }

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

        ImageVertex verts[] = {
            { { -1.0f, -1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f } }, { { -1.0f, 1.0f, 1.0f, 1.0f }, { 0.0f, 1.0f } },
            { { 1.0f, 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f } },   { { -1.0f, -1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f } },
            { { 1.0f, 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f } },   { { 1.0f, -1.0f, 0.0f, 1.0f }, { 1.0f, 0.0f } },
        };

        TFBufferLoadDesc vboDesc = {};
        vboDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_VERTEX_BUFFER;
        vboDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_GPU_ONLY;
        vboDesc.mDesc.mFlags = TF_BUFFER_CREATION_FLAG_NONE;
        vboDesc.mDesc.mSize = sizeof(verts);
        vboDesc.mDesc.mElementCount = sizeof(verts) / sizeof(verts[0]);
        vboDesc.pData = verts;
        vboDesc.ppBuffer = &pImageVerticesBuffer;
        addResource(&vboDesc, nullptr);

        waitForAllResourceLoads();

        return true;
    }

    void Unload(TFReloadDesc* pReloadDesc)
    {
        waitQueueIdle(pGraphicsQueue);
        waitQueueIdle(pComputeQueue);

        removeResource(pImageVerticesBuffer);

        unloadFontSystem();
        unloadUserInterface();

        if (pReloadDesc->mType & (TF_RELOAD_TYPE_SHADER | TF_RELOAD_TYPE_RENDERTARGET))
        {
            removePipelines();
        }

        if (pReloadDesc->mType & (TF_RELOAD_TYPE_RESIZE | TF_RELOAD_TYPE_RENDERTARGET))
        {
            removeSwapChain(pRenderer, pSwapChain);
            unloadProfilerUI();
        }

        if (pReloadDesc->mType & TF_RELOAD_TYPE_SHADER)
        {
            removeDescriptorSets();
            removeShaders();
        }
    }

    void Update(float)
    {
        updateGui();

        gFillImageUniformData.mImageSize.x = (float)IMAGE_WIDTH;
        gFillImageUniformData.mImageSize.y = (float)IMAGE_HEIGHT;

        int64_t nowUsec = getUSec(true) - gStartTimeUSec;

        gFillImageUniformData.mTime = ((float)nowUsec / 1000000.0f);
        gFillImageUniformData.mPattern = gFillPattern;
    }

    void Draw()
    {
        if ((bool)pSwapChain->mEnableVsync != mSettings.mVSyncEnabled)
        {
            waitQueueIdle(pGraphicsQueue);
            ::toggleVSync(pRenderer, &pSwapChain);
        }

        uint32_t swapchainImageIndex;
        acquireNextImage(pRenderer, pSwapChain, pImageAcquiredSemaphore, NULL, &swapchainImageIndex);

        // We need to wait for graphics fences before compute stuff, because graphics might still be
        // using the image.
        GpuCmdRingElement elemGraphics = getNextGpuCmdRingElement(&gGraphicsCmdRing, true, 1);
        waitForFences(pRenderer, 1, &elemGraphics.pFence);
        resetCmdPool(pRenderer, elemGraphics.pCmdPool);

        TFSemaphore* pComputeSemaphore = NULL;

        /************************************************************************/
        // GPU Fill Image pass
        /************************************************************************/
        if (gFillMode == FILL_MODE_GPU)
        {
            GpuCmdRingElement elemCompute = getNextGpuCmdRingElement(&gComputeCmdRing, true, 1);
            waitForFences(pRenderer, 1, &elemCompute.pFence);
            resetCmdPool(pRenderer, elemCompute.pCmdPool);

            TFBufferUpdateDesc fillImageDataUpdate = { pFillImageDataBuffer[mSettings.mFrameIdx] };
            beginUpdateResource(&fillImageDataUpdate);
            memcpy(fillImageDataUpdate.pMappedData, &gFillImageUniformData, sizeof(gFillImageUniformData));
            endUpdateResource(&fillImageDataUpdate);

            TFCmd* cmd = elemCompute.pCmds[0];
            beginCmd(cmd);
            cmdBeginGpuFrameProfile(cmd, gComputeGpuToken);
            // Fill Image pass
            {
                cmdBeginGpuTimestampQuery(cmd, gComputeGpuToken, "Fill Image");

                cmdBindPipeline(cmd, pFillImagesPipeline);

                cmdBindDescriptorSet(cmd, mSettings.mFrameIdx, pDescriptorSetTextures);

                cmdDispatch(cmd, (uint32_t)ceil((float)pImagesGPU[mSettings.mFrameIdx]->mWidth / 16.0f),
                            (uint32_t)ceil((float)pImagesGPU[mSettings.mFrameIdx]->mHeight / 16.0f), 1);

                cmdEndGpuTimestampQuery(cmd, gComputeGpuToken);
            }
            cmdEndGpuFrameProfile(cmd, gComputeGpuToken);

            endCmd(cmd);

            TFSemaphore* signalSemaphores[] = { elemCompute.pSemaphore };
            pComputeSemaphore = elemCompute.pSemaphore;

            TFQueueSubmitDesc submitDesc = {};
            submitDesc.mCmdCount = 1;
            submitDesc.mSignalSemaphoreCount = TF_ARRAY_COUNT(signalSemaphores);
            submitDesc.mWaitSemaphoreCount = 0;
            submitDesc.ppCmds = &cmd;
            submitDesc.ppSignalSemaphores = signalSemaphores;
            submitDesc.ppWaitSemaphores = NULL;
            submitDesc.pSignalFence = elemCompute.pFence;
            queueSubmit(pComputeQueue, &submitDesc);
        }

        /************************************************************************/
        // CPU Fill Image pass
        /************************************************************************/
        else if (gFillMode == FILL_MODE_CPU)
        {
            // We access resources in the CPU shader in a similar way to classic OpenGL uniforms.
            int32_t textureIndex = FillImage_get_uniform_index("gImage");
            ASSERTMSG(textureIndex >= 0, "FillImage shader missing resource 'gImage'");
            int32_t dataIndex = FillImage_get_uniform_index("gFillImageData");
            ASSERTMSG(dataIndex >= 0, "FillImage shader missing resource 'gFillImageData'");

            ISPCTexture2D*  tex = (ISPCTexture2D*)FillImage_get_uniform_pointer(textureIndex);
            FillImageData** ppFillImageData = (FillImageData**)FillImage_get_uniform_pointer(dataIndex);

            tex->format = TinyImageFormat_R8G8B8A8_UNORM;
            tex->width = pImagesGPU[mSettings.mFrameIdx]->mWidth;
            tex->height = pImagesGPU[mSettings.mFrameIdx]->mHeight;
            tex->data = pImageCPU;

            *ppFillImageData = &gFillImageUniformData;

            FillImage_dispatch((uint32_t)ceil((float)pImagesGPU[mSettings.mFrameIdx]->mWidth / 16.0f),
                               (uint32_t)ceil((float)pImagesGPU[mSettings.mFrameIdx]->mHeight / 16.0f), 1);

            TFTextureUpdateDesc updateDesc = {};
            updateDesc.pTexture = pImagesGPU[mSettings.mFrameIdx];
            updateDesc.mBaseMipLevel = 0;
            updateDesc.mMipLevels = 1;
            updateDesc.mBaseArrayLayer = 0;
            updateDesc.mLayerCount = 1;
            updateDesc.mCurrentState = TF_RESOURCE_STATE_UNORDERED_ACCESS;

            beginUpdateResource(&updateDesc);
            TFTextureSubresourceUpdate subresource = updateDesc.getSubresourceUpdateDesc(0, 0);

            for (uint32_t row = 0; row < subresource.mRowCount; ++row)
                memcpy(subresource.pMappedData + row * subresource.mDstRowStride, pImageCPU + row * IMAGE_WIDTH * 4, IMAGE_WIDTH * 4);

            endUpdateResource(&updateDesc);
        }

        /************************************************************************/
        // Graphics pass
        /************************************************************************/
        {
            TFRenderTarget* pRenderTarget = pSwapChain->ppRenderTargets[swapchainImageIndex];

            TFCmd* cmd = elemGraphics.pCmds[0];
            beginCmd(cmd);

            cmdBeginGpuFrameProfile(cmd, gGraphicsGpuToken);

            // Draw Image pass
            {
                cmdBeginGpuTimestampQuery(cmd, gGraphicsGpuToken, "Draw Reference Images");

                // Resource Transition
                {
                    TFRenderTargetBarrier barriers[] = {
                        { pRenderTarget, TF_RESOURCE_STATE_PRESENT, TF_RESOURCE_STATE_RENDER_TARGET },
                    };
                    cmdResourceBarrier(cmd, 0, NULL, 0, NULL, 1, barriers);
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
                    cmdBindPipeline(cmd, pDrawImagesPipeline);
                    cmdBindDescriptorSet(cmd, mSettings.mFrameIdx, pDescriptorSetTextures);
                    const uint32_t stride = sizeof(ImageVertex);
                    cmdBindVertexBuffer(cmd, 1, &pImageVerticesBuffer, &stride, NULL);
                    cmdDraw(cmd, 6, 0);
                }

                cmdEndGpuTimestampQuery(cmd, gGraphicsGpuToken);
            }

            // UI pass
            {
                cmdBeginGpuTimestampQuery(cmd, gGraphicsGpuToken, "Draw UI");

                // Bind Render Targets
                {
                    TFBindRenderTargetsDesc bindRenderTargets = {};
                    bindRenderTargets.mRenderTargetCount = 1;
                    bindRenderTargets.mRenderTargets[0] = { pRenderTarget, TF_LOAD_ACTION_LOAD };
                    bindRenderTargets.mDepthStencil = { NULL, TF_LOAD_ACTION_DONTCARE };
                    cmdBindRenderTargets(cmd, &bindRenderTargets);
                }

                gFrameTimeDraw.mFontColor = 0xff00ffff;
                gFrameTimeDraw.mFontSize = 18.0f;
                gFrameTimeDraw.pFont = gFont;
                float2 textPos = float2(8.f, 15.f);
                float2 txtSizePx = cmdDrawCpuProfile(cmd, textPos, &gFrameTimeDraw);
                textPos.y += txtSizePx.y + 75.f;
                txtSizePx = cmdDrawGpuProfile(cmd, textPos, gGraphicsGpuToken, &gFrameTimeDraw);
                textPos.y += txtSizePx.y + 75.f;
                cmdDrawGpuProfile(cmd, textPos, gComputeGpuToken, &gFrameTimeDraw);

                uiCmdDrawUserInterface(cmd, pSwapChain, pRenderTarget, gGraphicsGpuToken);
                cmdEndGpuTimestampQuery(cmd, gGraphicsGpuToken);
            }

            cmdBindRenderTargets(cmd, NULL);

            TFRenderTargetBarrier barriers[] = { { pRenderTarget, TF_RESOURCE_STATE_RENDER_TARGET, TF_RESOURCE_STATE_PRESENT } };
            cmdResourceBarrier(cmd, 0, NULL, 0, NULL, 1, barriers);

            cmdEndGpuFrameProfile(cmd, gGraphicsGpuToken);

            endCmd(cmd);

            FlushResourceUpdateDesc flushUpdateDesc = {};
            flushUpdateDesc.mNodeIndex = 0;
            flushResourceUpdates(&flushUpdateDesc);

            TFSemaphore* waitSemaphores[] = { flushUpdateDesc.pOutSubmittedSemaphore, pImageAcquiredSemaphore, pComputeSemaphore };
            uint32_t     waitSemaphoreCount = TF_ARRAY_COUNT(waitSemaphores);
            if (gFillMode != FILL_MODE_GPU)
            {
                // We only need to wait for pDiffImageReadySemaphore if we are doing the image fill on GPU
                waitSemaphoreCount -= 1;
            }

            TFQueueSubmitDesc submitDesc = {};
            submitDesc.mCmdCount = 1;
            submitDesc.mSignalSemaphoreCount = 1;
            submitDesc.mWaitSemaphoreCount = waitSemaphoreCount;
            submitDesc.ppCmds = &cmd;
            submitDesc.ppSignalSemaphores = &elemGraphics.pSemaphore;
            submitDesc.ppWaitSemaphores = waitSemaphores;
            submitDesc.pSignalFence = elemGraphics.pFence;
            queueSubmit(pGraphicsQueue, &submitDesc);

            TFQueuePresentDesc presentDesc = {};
            presentDesc.mIndex = (uint8_t)swapchainImageIndex;
            presentDesc.mWaitSemaphoreCount = 1;
            presentDesc.pSwapChain = pSwapChain;
            presentDesc.ppWaitSemaphores = &elemGraphics.pSemaphore;
            presentDesc.mSubmitDone = true;

            queuePresent(pGraphicsQueue, &presentDesc);
            flipProfiler();
        }
    }

    const char* GetName() { return "05_CPUCompute"; }

private:
    void updateGui()
    {
        if (UI_WINDOW_IS_VISIBLE(uiBeginWidgetWindow(&gGuiWindowDesc)))
        {
            uiLayoutAutoTextRows(2);
            uiLabel("Image Fill mode", TF_ALIGN_LEFT);
            gFillMode = (ImageFillMode)UI_WIDGET_GET_SELECTED(uiDropdown(pFillModeStrings, TF_ARRAY_COUNT(pFillModeStrings), gFillMode));

            uiLayoutAutoTextRows(2);
            uiLabel("Image Fill Pattern", TF_ALIGN_LEFT);
            gFillPattern = (ImageFillPattern)UI_WIDGET_GET_SELECTED(
                uiDropdown(pFillPatternStrings, TF_ARRAY_COUNT(pFillPatternStrings), gFillPattern));
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
        TFDescriptorSetDesc desc = SRT_SET_DESC(SrtData, PerFrame, gDataBufferCount, 0);
        addDescriptorSet(pRenderer, &desc, &pDescriptorSetTextures);
    }

    void removeDescriptorSets() { removeDescriptorSet(pRenderer, pDescriptorSetTextures); }

    void addRootSignatures()
    {
        TFRootSignatureDesc rootDesc = {};
        INIT_RS_DESC(rootDesc, "default.rootsig", "compute.rootsig");
        initRootSignature(pRenderer, &rootDesc);
    }

    void removeRootSignatures() { exitRootSignature(pRenderer); }

    void addShaders()
    {
        TFShaderLoadDesc shader = {};
        shader.mVert.pFileName = "basic.vert";
        shader.mFrag.pFileName = "basic.frag";

        addShader(pRenderer, &shader, &pBasicShader);

        shader = {};
        shader.mComp.pFileName = "FillImage";

        addShader(pRenderer, &shader, &pImageFillShader);
    }

    void removeShaders()
    {
        removeShader(pRenderer, pImageFillShader);
        removeShader(pRenderer, pBasicShader);
    }

    bool addPipelines()
    {
        {
            // Images pipeline
            TFVertexLayout layout = {};
            layout.mAttribCount = 2;
            layout.mBindingCount = 1;

            layout.mBindings[0].mStride = sizeof(ImageVertex);
            layout.mBindings[0].mRate = TF_VERTEX_BINDING_RATE_VERTEX;

            layout.mAttribs[0].mSemantic = TF_SEMANTIC_POSITION;
            layout.mAttribs[0].mFormat = TinyImageFormat_R32G32B32A32_SFLOAT;
            layout.mAttribs[0].mBinding = 0;
            layout.mAttribs[0].mLocation = 0;
            layout.mAttribs[0].mOffset = offsetof(ImageVertex, mPosition);

            layout.mAttribs[1].mSemantic = TF_SEMANTIC_TEXCOORD0;
            layout.mAttribs[1].mFormat = TinyImageFormat_R32G32_SFLOAT;
            layout.mAttribs[1].mBinding = 0;
            layout.mAttribs[1].mLocation = 1;
            layout.mAttribs[1].mOffset = offsetof(ImageVertex, mTexCoords);

            TFRasterizerStateDesc basicRasterizerStateDesc = {};
            basicRasterizerStateDesc.mCullMode = TF_CULL_MODE_NONE;

            // PVS false positive
            //-V::886

            TFPipelineDesc pipelineDesc = {};
            pipelineDesc.mType = TF_PIPELINE_TYPE_GRAPHICS;
            TFGraphicsPipelineDesc& pipelineSettings = pipelineDesc.mGraphicsDesc;
            PIPELINE_LAYOUT_DESC(pipelineDesc, NULL, SRT_LAYOUT_DESC(SrtData, PerFrame), NULL, NULL);
            pipelineSettings.mPrimitiveTopo = TF_PRIMITIVE_TOPO_TRI_LIST;
            pipelineSettings.mRenderTargetCount = 1;
            pipelineSettings.pColorFormats = &pSwapChain->ppRenderTargets[0]->mFormat;
            pipelineSettings.mSampleCount = pSwapChain->ppRenderTargets[0]->mSampleCount;
            pipelineSettings.mSampleQuality = pSwapChain->ppRenderTargets[0]->mSampleQuality;
            pipelineSettings.pShaderProgram = pBasicShader;
            pipelineSettings.pRasterizerState = &basicRasterizerStateDesc;
            pipelineSettings.pDepthState = 0;
            pipelineSettings.pVertexLayout = &layout;
            addPipeline(pRenderer, &pipelineDesc, &pDrawImagesPipeline);

            //-V::886

            if (!pDrawImagesPipeline)
            {
                LOGF(LogLevel::eERROR, "Failed to add draw image pipeline.");
                return false;
            }
        }

        {
            TFPipelineDesc pipelineDesc = {};
            pipelineDesc.mType = TF_PIPELINE_TYPE_COMPUTE;
            PIPELINE_LAYOUT_DESC(pipelineDesc, NULL, SRT_LAYOUT_DESC(SrtData, PerFrame), NULL, NULL);
            pipelineDesc.mComputeDesc.pShaderProgram = pImageFillShader;
            addPipeline(pRenderer, &pipelineDesc, &pFillImagesPipeline);
        }

        return true;
    }

    void removePipelines()
    {
        removePipeline(pRenderer, pFillImagesPipeline);
        removePipeline(pRenderer, pDrawImagesPipeline);
    }

    void updateDescriptorSets()
    {
        for (uint32_t i = 0; i < gDataBufferCount; i += 1)
        {
            TFDescriptorData params[2] = {};
            params[0].ppTextures = &pImagesGPU[i];
            params[0].mCount = 1;
            params[0].mIndex = SRT_RES_IDX(SrtData, PerFrame, gImage);
            params[1].ppBuffers = &pFillImageDataBuffer[i];
            params[1].mCount = 1;
            params[1].mIndex = SRT_RES_IDX(SrtData, PerFrame, gFillImageData);

            updateDescriptorSet(pRenderer, i, pDescriptorSetTextures, sizeof(params) / sizeof(params[0]), params);
        }
    }
};
DEFINE_APPLICATION_MAIN(CPUCompute)
