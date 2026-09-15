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

#include "../../../Utilities/Interfaces/IMath.h"

enum TFStreamingResourceType
{
    TF_DiffuseTexture,
    TF_NormalTexture,
    TF_SpecularTexture,
};

enum TFStreamingSystemState
{
    TF_StreamingIdle,
    TF_StreamingActive
};

enum TFStreamingMode
{
    TF_StreamingBoundingBoxes,
    TF_StreamingPixelCoverage
};

typedef struct TFStreamingState
{
    uint64_t               mHeapSize;
    uint64_t               mUsedHeapMemory;
    uint64_t               mLiveTexturesCount;
    uint64_t               mFreeChunksCount;
    TFTexture*             pDefaultTexture;
    TFTexture**            ppOrderedTextures;
    TFStreamingSystemState mState;
} TFStreamingState;

typedef const char* (*GetTextureFilenameFn)(uint32_t id, TFStreamingResourceType type);

typedef struct TFStreamingDesc
{
    // Number of stream zones i.e number of meshes.
    uint32_t mZonesCount;
    // Total number of texture pointers used for the descriptors
    uint32_t mTotalTextureCount;
    // Number of textures currently loaded
    uint32_t mLiveCount;
    // Maximum number of textures that can be loaded
    uint32_t mLiveCountMax;
    // Number of frames to wait before unloading a texture.
    uint32_t mRemovalDelay;
    // Size of the texture heap
    uint64_t mTexturesHeapSize;

    TFRenderer* pRenderer;
} TFStreamingDesc;

typedef struct TFStreamingUpdateDesc
{
    vec3            mCameraPosition;
    uint32_t        mMaxLiveMaterials;
    TFStreamingMode mMode;
    uint32_t*       pPixelCoverageData;
    uint32_t        mPinnedMaterialsCount;
    uint32_t*       pPinnedMaterialIDs;
    // more here : camera frustum, multiple streaming spots.. etc..
} TFStreamingUpdateDesc;

FORGE_RENDERER_API void initStreamingInterface(TFStreamingDesc* pDesc);
FORGE_RENDERER_API bool updateStreaming(const TFStreamingUpdateDesc* pStreamingLocation);
FORGE_RENDERER_API void getStreamingState(TFStreamingState* pStreamingState);
FORGE_RENDERER_API void cmdStreamingDebugDraw(TFCmd* cmd);
FORGE_RENDERER_API void exitStreamingInterface();
FORGE_RENDERER_API void onStreamingGeometryLoaded(const TFPackage* pPackage);
