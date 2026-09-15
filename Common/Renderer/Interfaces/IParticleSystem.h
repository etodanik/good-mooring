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

#pragma once

#include "../../Application/Config.h"
#include "../../Utilities/Interfaces/IMath.h"

typedef struct TFTexture                    TFTexture;
typedef struct TFCmd                        TFCmd;
typedef struct TFBuffer                     TFBuffer;
typedef struct TFRenderer                   TFRenderer;
typedef struct TFParticleConstantBufferData TFParticleConstantBufferData;
typedef struct TFParticleSystemStats        TFParticleSystemStats;

typedef struct TFParticleSystemInitDesc
{
    TFRenderer* pRenderer = NULL;

    uint32_t mSwapWidth = 0;
    uint32_t mSwapHeight = 0;
    uint32_t mFramesInFlight = 0;

    uint32_t mSwapColorFormat = 0;
    uint32_t mDepthFormat = 0;
    uint32_t mColorSampleQuality = 0;
    uint32_t mParticleTextureCount = 0;
    uint32_t mDefaultParticleSetsCount = 0;

    TFTexture*  pColorBuffer = NULL;
    TFTexture*  pDepthBuffer = NULL;
    TFTexture*  pShadowMap = NULL;
    TFTexture*  pRainTopDownMap = NULL;
    TFTexture*  pCurlNoiseTexture = NULL;
    TFTexture** ppParticleTextures = NULL;

    TFBuffer* pParticlesBuffer = NULL;
    TFBuffer* pBitfieldBuffer = NULL;
    TFBuffer* pParticleSetsBuffer = NULL;
    TFBuffer* pTransparencyListBuffer = NULL;
    TFBuffer* pTransparencyListHeadsBuffer = NULL;
    TFBuffer* pBufferParticleRenderIndirectData = NULL;

    TFBuffer** ppParticleConstantBuffer = NULL;

    TFDescriptorSet* pDescriptorSetPersistent = NULL;
    TFDescriptorSet* pDescriptorSetPerFrame = NULL;
    TFDescriptorSet* pDescriptorSetPerBatch = NULL;

    TFPipeline* pParticleRenderPipeline = NULL;
    TFPipeline* pParticleSimulatePipeline = NULL;

    uint32_t mParticleTexturesIndex = 0;
    uint32_t mDepthBufferIndex = 0;
    uint32_t mTopDownMapIndex = 0;
    uint32_t mParticlesDataBufferIndex = 0;
    uint32_t mParticlesBufferStateIndex = 0;
    uint32_t mTransparencyListIndex = 0;
    uint32_t mBitfieldBufferIndex = 0;
    uint32_t mParticleSetBufferIndex = 0;
    uint32_t mParticlesToRasterizeIndex = 0;
    uint32_t mTransparencyListHeadsIndex = 0;
    uint32_t mParticleRenderIndirectDataIndex = 0;
    uint32_t mStatsBufferIndex = 0;
    uint32_t mShadowMapIndex = 0;
    uint32_t mShadedSceneIndex = 0;
    uint32_t mCurlNoiseIndex = 0;

} TFParticleSystemInitDesc;

FORGE_RENDERER_API bool                  initParticleSystem(const TFParticleSystemInitDesc* pDesc);
FORGE_RENDERER_API TFParticleSystemStats getParticleSystemStats(uint32_t frameIndex);
FORGE_RENDERER_API void                  exitParticleSystem();

FORGE_RENDERER_API void updateParticleSystemConstantBuffers(uint32_t frameIndex, TFParticleConstantBufferData* cameraConstantBufferData);

FORGE_RENDERER_API void cmdParticleSystemSimulate(TFCmd* pCmd, uint32_t frameIndex);
FORGE_RENDERER_API void cmdParticleSystemRender(TFCmd* pCmd, uint32_t frameIndex);
