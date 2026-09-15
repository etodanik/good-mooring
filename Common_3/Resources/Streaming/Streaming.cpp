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

#include "../ResourceLoader/Interfaces/IResourceLoader.h"
#include "Interfaces/IStreaming.h"
#include "../../Utilities/ThirdParty/OpenSource/Nothings/stb_ds.h"

typedef struct StreamingZone
{ //-V802
    vec3               mMin;
    vec3               mMax;
    vec3               mCenter;
    TFTextureID        colorTexID;
    TFTextureLoadDesc* pColorLoadDesc;
    TFTextureID        normTexID;
    TFTextureLoadDesc* pNormLoadDesc;
    TFTextureID        specTexID;
    TFTextureLoadDesc* pSpecTexLoadDesc;
    uint32_t           mLastLoaded;
} StreamingZone;

typedef struct RemovalRequest
{
    uint32_t    mDelay = 0;
    TFTextureID mTexID = TEX_IDX_INVALID;
    TFSyncToken mToken = 0;
} RemovalRequest;

typedef struct LoadRequest
{
    TFTextureID texID;
    uint32_t    orderedIndex;
    TFSyncToken loadToken;
} LoadRequest;

typedef struct Streamer
{
    StreamingZone* pZones;
    uint32_t       mZonesCount;
    uint32_t       mTotalTextureCount;
    uint32_t       mLiveCount;
    uint32_t       mLiveCountMax;
    int            mLastLoadedIndex;
    TFMutex        mMutex;

    TFStreamingSystemState mState;

    uint8_t*     pTextureReferenceCount;
    TFTexture**  ppAllTextures;
    TFTexture**  ppOrderedTextures;
    TFHeapChunk* pAllocations;
    bool*        pTexLoadedForMesh;

    uint32_t        mRemovalDelay;
    RemovalRequest* pRemovalQueue;

    LoadRequest* pLoadRequestQueue;

    // default 1x1 textures that are used when textures are not loaded
    TFTexture* pDefaultTexture;

    TFResourceHeap*  pTexturesHeap;
    TFHeapAllocator* pTexturesHeapAllocator;
    TFBuffer*        pDebugDrawVertexBuffer;

    TFRenderer* pRenderer;
    uint32_t    mUpdateCount;
} Streamer;

FORGE_RENDERER_API void addStreamer(const TFPackage* desc, Streamer** ppStreamer);
FORGE_RENDERER_API void removeStreamer(Streamer* pStreamer);
void                    processRemovals();

#define STREAMING_MAX_FREE_BLOCKS 1024
Streamer* gStreamer = NULL;

void prepareStreamingDebugVertexBuffer()
{
    TFSyncToken token = {};
    uint32_t    debugVertexDataSize = gStreamer->mZonesCount * 12 * 2 * sizeof(float3);

    float3* linesVertexData = (float3*)tf_calloc(1, debugVertexDataSize);

    float3* currVertex = linesVertexData;

    for (uint32_t i = 0; i < gStreamer->mZonesCount; i++)
    {
        StreamingZone* zone = gStreamer->pZones + i;

        // the 8 extreme bounds..
        vec3 LeftBottomBack(zone->mMin.x, zone->mMin.y, zone->mMin.z);
        vec3 RightBottomBack(zone->mMax.x, zone->mMin.y, zone->mMin.z);

        vec3 LeftTopBack(zone->mMin.x, zone->mMax.y, zone->mMin.z);
        vec3 RightTopBack(zone->mMax.x, zone->mMax.y, zone->mMin.z);

        vec3 LeftBottomFront(zone->mMin.x, zone->mMin.y, zone->mMax.z);
        vec3 RightBottomFront(zone->mMax.x, zone->mMin.y, zone->mMax.z);

        vec3 LeftTopFront(zone->mMin.x, zone->mMax.y, zone->mMax.z);
        vec3 RightTopFront(zone->mMax.x, zone->mMax.y, zone->mMax.z);

        // top 4 lines
        currVertex->x = LeftTopBack.x;
        currVertex->y = LeftTopBack.y;
        currVertex->z = LeftTopBack.z;
        currVertex++;
        currVertex->x = RightTopBack.x;
        currVertex->y = RightTopBack.y;
        currVertex->z = RightTopBack.z;
        currVertex++;

        currVertex->x = RightTopBack.x;
        currVertex->y = RightTopBack.y;
        currVertex->z = RightTopBack.z;
        currVertex++;
        currVertex->x = RightTopFront.x;
        currVertex->y = RightTopFront.y;
        currVertex->z = RightTopFront.z;
        currVertex++;

        currVertex->x = RightTopFront.x;
        currVertex->y = RightTopFront.y;
        currVertex->z = RightTopFront.z;
        currVertex++;
        currVertex->x = LeftTopFront.x;
        currVertex->y = LeftTopFront.y;
        currVertex->z = LeftTopFront.z;
        currVertex++;

        currVertex->x = LeftTopFront.x;
        currVertex->y = LeftTopFront.y;
        currVertex->z = LeftTopFront.z;
        currVertex++;
        currVertex->x = LeftTopBack.x;
        currVertex->y = LeftTopBack.y;
        currVertex->z = LeftTopBack.z;
        currVertex++;

        // bottom 4 lines
        currVertex->x = LeftBottomBack.x;
        currVertex->y = LeftBottomBack.y;
        currVertex->z = LeftBottomBack.z;
        currVertex++;
        currVertex->x = RightBottomBack.x;
        currVertex->y = RightBottomBack.y;
        currVertex->z = RightBottomBack.z;
        currVertex++;

        currVertex->x = RightBottomBack.x;
        currVertex->y = RightBottomBack.y;
        currVertex->z = RightBottomBack.z;
        currVertex++;
        currVertex->x = RightBottomFront.x;
        currVertex->y = RightBottomFront.y;
        currVertex->z = RightBottomFront.z;
        currVertex++;

        currVertex->x = RightBottomFront.x;
        currVertex->y = RightBottomFront.y;
        currVertex->z = RightBottomFront.z;
        currVertex++;
        currVertex->x = LeftBottomFront.x;
        currVertex->y = LeftBottomFront.y;
        currVertex->z = LeftBottomFront.z;
        currVertex++;

        currVertex->x = LeftBottomFront.x;
        currVertex->y = LeftBottomFront.y;
        currVertex->z = LeftBottomFront.z;
        currVertex++;
        currVertex->x = LeftBottomBack.x;
        currVertex->y = LeftBottomBack.y;
        currVertex->z = LeftBottomBack.z;
        currVertex++;

        // 4 vertical lines going from top to bottom
        currVertex->x = LeftBottomBack.x;
        currVertex->y = LeftBottomBack.y;
        currVertex->z = LeftBottomBack.z;
        currVertex++;
        currVertex->x = LeftTopBack.x;
        currVertex->y = LeftTopBack.y;
        currVertex->z = LeftTopBack.z;
        currVertex++;

        currVertex->x = RightBottomBack.x;
        currVertex->y = RightBottomBack.y;
        currVertex->z = RightBottomBack.z;
        currVertex++;
        currVertex->x = RightTopBack.x;
        currVertex->y = RightTopBack.y;
        currVertex->z = RightTopBack.z;
        currVertex++;

        currVertex->x = RightBottomFront.x;
        currVertex->y = RightBottomFront.y;
        currVertex->z = RightBottomFront.z;
        currVertex++;
        currVertex->x = RightTopFront.x;
        currVertex->y = RightTopFront.y;
        currVertex->z = RightTopFront.z;
        currVertex++;

        currVertex->x = LeftBottomFront.x;
        currVertex->y = LeftBottomFront.y;
        currVertex->z = LeftBottomFront.z;
        currVertex++;
        currVertex->x = LeftTopFront.x;
        currVertex->y = LeftTopFront.y;
        currVertex->z = LeftTopFront.z;
        currVertex++;
    }

    uint64_t         debugDrawDataSize = debugVertexDataSize;
    TFBufferLoadDesc lineVbDesc = {};
    lineVbDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_VERTEX_BUFFER;
    lineVbDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_GPU_ONLY;
    lineVbDesc.mDesc.mSize = debugDrawDataSize;
    lineVbDesc.pData = linesVertexData;
    lineVbDesc.ppBuffer = &gStreamer->pDebugDrawVertexBuffer;
    addResource(&lineVbDesc, &token);

    waitForToken(&token);
    tf_free(linesVertexData);
}

void prepareStreamingDefaultTextures()
{
    TFTextureDesc textureDesc = {};
    textureDesc.mArraySize = 1;
    textureDesc.mMipLevels = 1;
    textureDesc.mWidth = 1;
    textureDesc.mHeight = 1;
    textureDesc.mDepth = 1;
    textureDesc.mFormat = TinyImageFormat_R8G8B8A8_UNORM;
    textureDesc.mStartState = TF_RESOURCE_STATE_COMMON;
    textureDesc.mDescriptors = TF_DESCRIPTOR_TYPE_TEXTURE;
    textureDesc.mSampleCount = TF_SAMPLE_COUNT_1;
    textureDesc.mFlags = TF_TEXTURE_CREATION_FLAG_FORCE_2D;
    TFSyncToken       token = {};
    uint32_t          grayRGBA = 0xff808080;
    uint32_t*         pDestPixel = NULL;
    TFTextureLoadDesc desc = {};
    desc.pDesc = &textureDesc;
    desc.ppTexture = &gStreamer->pDefaultTexture;
    addResource(&desc, &token);
    TFTextureUpdateDesc updateDesc = { gStreamer->pDefaultTexture, 0, 1, 0, 1, TF_RESOURCE_STATE_PIXEL_SHADER_RESOURCE };
    beginUpdateResource(&updateDesc);
    pDestPixel = (uint32_t*)updateDesc.getSubresourceUpdateDesc(0, 0).pMappedData;
    *pDestPixel = grayRGBA;
    endUpdateResource(&updateDesc);
    waitForToken(&token);
}

void initStreamingInterface(TFStreamingDesc* pDesc)
{
    gStreamer->mLiveCountMax = pDesc->mLiveCountMax;
    gStreamer->ppAllTextures = (TFTexture**)tf_calloc(gStreamer->mLiveCountMax, sizeof(TFTexture*));
    gStreamer->mTotalTextureCount = pDesc->mTotalTextureCount;
    gStreamer->ppOrderedTextures = (TFTexture**)tf_calloc(gStreamer->mTotalTextureCount, sizeof(TFTexture*));
    gStreamer->pRenderer = pDesc->pRenderer;
    gStreamer->pRemovalQueue = NULL;
    gStreamer->pLoadRequestQueue = NULL;
    gStreamer->mRemovalDelay = pDesc->mRemovalDelay;
    gStreamer->mLiveCount = 0;

    prepareStreamingDefaultTextures();
    prepareStreamingDebugVertexBuffer();

    for (uint32_t index = 0; index < gStreamer->mTotalTextureCount; index++)
    {
        gStreamer->ppOrderedTextures[index] = gStreamer->pDefaultTexture;
    }

    // create the VRAM heap for textures
    TFResourceHeapDesc heapDesc = {};
    heapDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_GPU_ONLY;
    heapDesc.mDescriptors = TF_DESCRIPTOR_TYPE_TEXTURE;

    // TODO: investigate these flags
#ifdef DIRECT3D12
#if defined(XBOX)
    heapDesc.mFlags = TF_RESOURCE_HEAP_FLAG_ALLOW_ALL_BUFFERS_AND_TEXTURES;
#else
    heapDesc.mFlags = TF_RESOURCE_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES;
#endif
#else
    heapDesc.mFlags = TF_RESOURCE_HEAP_FLAG_ALLOW_ALL_BUFFERS_AND_TEXTURES;
#endif

#ifdef METAL
    heapDesc.mAlignment = 256 * 1024; // align to 256 k
#else
    heapDesc.mAlignment = 64 * 1024; // align to 64 k
#endif
    heapDesc.mSize = pDesc->mTexturesHeapSize;
    heapDesc.pName = "Streaming textures heap";
    addResourceHeap(gStreamer->pRenderer, &heapDesc, &gStreamer->pTexturesHeap);

    TFHeapAllocatorDesc heapAllocatorDesc = {};
    heapAllocatorDesc.mSize = pDesc->mTexturesHeapSize;
    heapAllocatorDesc.mMaxChunksCount = STREAMING_MAX_FREE_BLOCKS;
    heapAllocatorDesc.mAlignement = heapDesc.mAlignment;

    addHeapAllocator(&heapAllocatorDesc, &gStreamer->pTexturesHeapAllocator);
}

void exitStreamingInterface()
{
    while (arrlenu(gStreamer->pRemovalQueue) > 0)
    {
        processRemovals();
    }
    removeHeapAllocator(gStreamer->pTexturesHeapAllocator);
    removeResourceHeap(gStreamer->pRenderer, gStreamer->pTexturesHeap);
    for (uint32_t i = 0; i < gStreamer->mZonesCount; i++)
    {
        StreamingZone* currZone = gStreamer->pZones + i;
        if (currZone->colorTexID != TEX_IDX_INVALID)
        {
            if (gStreamer->pTextureReferenceCount[currZone->colorTexID] > 0)
            {
                if (gStreamer->ppAllTextures[currZone->colorTexID])
                {
                    removeResource(gStreamer->ppAllTextures[currZone->colorTexID]);
                    gStreamer->pTextureReferenceCount[currZone->colorTexID] = 0;
                }
            }
        }
        if (currZone->normTexID != TEX_IDX_INVALID)
        {
            if (gStreamer->pTextureReferenceCount[currZone->normTexID] > 0)
            {
                if (gStreamer->ppAllTextures[currZone->normTexID])
                {
                    removeResource(gStreamer->ppAllTextures[currZone->normTexID]);
                    gStreamer->pTextureReferenceCount[currZone->normTexID] = 0;
                }
            }
        }
        if (currZone->specTexID != TEX_IDX_INVALID)
        {
            if (gStreamer->pTextureReferenceCount[currZone->specTexID] > 0)
            {
                if (gStreamer->ppAllTextures[currZone->specTexID])
                {
                    removeResource(gStreamer->ppAllTextures[currZone->specTexID]);
                    gStreamer->pTextureReferenceCount[currZone->specTexID] = 0;
                }
            }
        }
    }
    removeResource(gStreamer->pDebugDrawVertexBuffer);
    removeResource(gStreamer->pDefaultTexture);
    removeStreamer(gStreamer);
}

void getStreamingState(TFStreamingState* pStreamingState)
{
    pStreamingState->pDefaultTexture = gStreamer->pDefaultTexture;

    pStreamingState->mHeapSize = gStreamer->pTexturesHeapAllocator->mTotalSize;
    pStreamingState->mLiveTexturesCount = gStreamer->mLiveCount;
    pStreamingState->mUsedHeapMemory = gStreamer->pTexturesHeapAllocator->mUsedMemory;
    pStreamingState->mFreeChunksCount = gStreamer->pTexturesHeapAllocator->mFreeChunksCount;

    pStreamingState->ppOrderedTextures = gStreamer->ppOrderedTextures;
    pStreamingState->mState = gStreamer->mState;
}

void addStreamer(const TFPackage* pPackage, Streamer** ppStreamer)
{
    Streamer*      pStreamer = (Streamer*)tf_calloc(1, sizeof(Streamer));
    StreamingZone* streamingZones = NULL;
    size_t         streamingZonesSize = 0;

    uint32_t zonesCount = 0;
    uint32_t texCount = 0;
    for (uint32_t geoIndex = 0; geoIndex < pPackage->pPackageMetadata->mGeoCount; geoIndex++)
    {
        streamingZonesSize += pPackage->pTextureMetadata[geoIndex].mMeshCount * sizeof(StreamingZone);
        streamingZones = (StreamingZone*)tf_realloc(streamingZones, streamingZonesSize);
        for (uint32_t meshIndex = 0; meshIndex < pPackage->pTextureMetadata[geoIndex].mMeshCount; meshIndex++)
        {
            TFStreamZone* pCurrZone = pPackage->pTextureMetadata[geoIndex].pStreamZones + meshIndex;
            streamingZones[zonesCount + meshIndex].mMin = pCurrZone->mMin;
            streamingZones[zonesCount + meshIndex].mMax = pCurrZone->mMax;
            streamingZones[zonesCount + meshIndex].mCenter = pCurrZone->mCenter;
            TFTextureBundleID texBundleID = pPackage->pTextureMetadata[geoIndex].pTextureBundleIDs[meshIndex];
            streamingZones[zonesCount + meshIndex].colorTexID = GET_COLOR_TEXID(texBundleID);
            streamingZones[zonesCount + meshIndex].normTexID = GET_NORMAL_TEXID(texBundleID);
            streamingZones[zonesCount + meshIndex].specTexID = GET_SPEC_TEXID(texBundleID);
            streamingZones[zonesCount + meshIndex].pColorLoadDesc =
                &pPackage->pTextureData[streamingZones[zonesCount + meshIndex].colorTexID];
            streamingZones[zonesCount + meshIndex].pNormLoadDesc =
                &pPackage->pTextureData[streamingZones[zonesCount + meshIndex].normTexID];
            streamingZones[zonesCount + meshIndex].pSpecTexLoadDesc =
                &pPackage->pTextureData[streamingZones[zonesCount + meshIndex].specTexID];
        }
        zonesCount += (uint32_t)pPackage->pTextureMetadata[geoIndex].mMeshCount;
        texCount += (uint32_t)pPackage->pTextureMetadata[geoIndex].mTextureCount;
    }
    pStreamer->mZonesCount = zonesCount;
    pStreamer->pTexLoadedForMesh = (bool*)tf_calloc(zonesCount, sizeof(bool));
    pStreamer->pZones = streamingZones;
    pStreamer->pTextureReferenceCount = (uint8_t*)tf_calloc(texCount, sizeof(uint8_t));
    pStreamer->pAllocations = (TFHeapChunk*)tf_calloc(texCount, sizeof(TFHeapChunk));
    pStreamer->mUpdateCount = 0;
    initMutex(&pStreamer->mMutex);
    *ppStreamer = pStreamer;
}

void onStreamingGeometryLoaded(const TFPackage* pPackage) { addStreamer(pPackage, &gStreamer); }

void removeStreamer(Streamer* pStreamer)
{
    ASSERT(pStreamer);
    exitMutex(&pStreamer->mMutex);
    arrfree(gStreamer->pRemovalQueue);
    arrfree(gStreamer->pLoadRequestQueue);
    tf_free(pStreamer->ppOrderedTextures);
    tf_free(pStreamer->ppAllTextures);
    tf_free(pStreamer->pZones);
    tf_free(pStreamer->pTextureReferenceCount);
    tf_free(pStreamer->pAllocations);
    tf_free(pStreamer->pTexLoadedForMesh);
    tf_free(pStreamer);
}

void cmdStreamingDebugDraw(TFCmd* cmd)
{
    const uint32_t strides = sizeof(float3);
    uint32_t       vertex_count = gStreamer->mZonesCount * 12 * 2;
    cmdBindVertexBuffer(cmd, 1, &gStreamer->pDebugDrawVertexBuffer, &strides, nullptr);
    cmdDraw(cmd, vertex_count, 0);
}

int closestStreamingBoxIndex(const TFStreamingUpdateDesc* pStreamingLocation)
{
    float fClosestDistance = FLT_MAX;
    int   closestBoxIndex = -1;

    for (uint32_t i = 0; i < gStreamer->mZonesCount; i++)
    {
        StreamingZone* currZone = gStreamer->pZones + i;

        float currDistance = length(currZone->mCenter - pStreamingLocation->mCameraPosition);
        if (currDistance < fClosestDistance)
        {
            if (gStreamer->pTexLoadedForMesh[i] == false)
            {
                fClosestDistance = currDistance;
                closestBoxIndex = i;
            }
        }
    }
    return closestBoxIndex;
}

int furthestStreamingBoxIndex(const TFStreamingUpdateDesc* pStreamingLocation)
{
    float fFurthestDistance = 0.0f;
    int   furthestBoxIndex = -1;

    for (uint32_t i = 0; i < gStreamer->mZonesCount; i++)
    {
        StreamingZone* currZone = gStreamer->pZones + i;
        float          currDistance = length(currZone->mCenter - pStreamingLocation->mCameraPosition);
        if (currDistance > fFurthestDistance)
        {
            if (gStreamer->pTexLoadedForMesh[i])
            {
                fFurthestDistance = currDistance;
                furthestBoxIndex = i;
            }
        }
    }
    return furthestBoxIndex;
}

// Pixel coverage data is populated using the pixel coverage compute shader
// where each texture id in order in the array contains roughly how many pixels are using it in the TVB
// the highest numbers is the set id covering most pixels
int highestPixelCoverageIndex(const TFStreamingUpdateDesc* pStreamingLocation)
{
    uint32_t pixelCoverage = 0;
    int      highPixelCoverageIndex = -1;

    // pinned ones get highest priority
    for (uint32_t i = 0; i < pStreamingLocation->mPinnedMaterialsCount; i++)
    {
        uint32_t pinnedMaterialID = pStreamingLocation->pPinnedMaterialIDs[i];
        if (gStreamer->pTexLoadedForMesh[pinnedMaterialID] == false)
        {
            return pinnedMaterialID;
        }
    }

    for (uint32_t i = 0; i < gStreamer->mZonesCount; i++)
    {
        uint32_t currCoverage = pStreamingLocation->pPixelCoverageData[i];

        if (currCoverage > 0)
        {
            if (pixelCoverage <= currCoverage)
            {
                if (gStreamer->pTexLoadedForMesh[i] == false)
                {
                    pixelCoverage = currCoverage;
                    highPixelCoverageIndex = i;
                }
            }
        }
    }
    return highPixelCoverageIndex;
}

int lowestPixelCoverageIndex(const TFStreamingUpdateDesc* pStreamingLocation)
{
    uint32_t pixelCoverage = 0xffffffff;
    uint32_t lastLoaded = (uint32_t)-1;
    int      lowestPixelCoverageIndex = -1;
    for (uint32_t i = 0; i < gStreamer->mZonesCount; i++)
    {
        StreamingZone* currZone = gStreamer->pZones + i;

        if (gStreamer->pTexLoadedForMesh[i])
        {
            uint32_t currCoverage = pStreamingLocation->pPixelCoverageData[i];
            if (pixelCoverage > currCoverage || (pixelCoverage == currCoverage && currZone->mLastLoaded < lastLoaded))
            {
                // Make sure material is not pinned
                bool isPinned = false;
                for (uint32_t j = 0; j < pStreamingLocation->mPinnedMaterialsCount; j++)
                {
                    uint32_t pinnedMaterialID = pStreamingLocation->pPinnedMaterialIDs[j];
                    if (pinnedMaterialID == i)
                    {
                        isPinned = true;
                        break;
                    }
                }

                if (!isPinned)
                {
                    pixelCoverage = currCoverage;
                    lowestPixelCoverageIndex = i;
                    lastLoaded = currZone->mLastLoaded;
                }
            }
        }
    }

    // Pinned materials unloaded last
    if (lowestPixelCoverageIndex == -1)
    {
        for (uint32_t i = 0; i < pStreamingLocation->mPinnedMaterialsCount; i++)
        {
            uint32_t pinnedMaterialID = pStreamingLocation->pPinnedMaterialIDs[i];
            if (gStreamer->pTexLoadedForMesh[pinnedMaterialID])
            {
                lowestPixelCoverageIndex = pinnedMaterialID;
                break;
            }
        }
    }

    return lowestPixelCoverageIndex;
}

void OnStreamingTextureLoaded(const TFOnTextureLoadedData* loadedTexture, TFResourcePlacement* pPlacement)
{
    acquireMutex(&gStreamer->mMutex);
    TFTextureID* pTexID = (TFTextureID*)loadedTexture->pUserData;

    if (pTexID)
    {
        TFHeapChunk heapChunk = {};

        heapAllocatorAlloc(gStreamer->pTexturesHeapAllocator, loadedTexture->mSize, &heapChunk);
        gStreamer->pAllocations[*pTexID].mOffset = heapChunk.mOffset;
        gStreamer->pAllocations[*pTexID].mSize = loadedTexture->mSize;

        pPlacement->pHeap = gStreamer->pTexturesHeap;
        pPlacement->mOffset = heapChunk.mOffset;
    }
    else
    {
        pPlacement->pHeap = NULL;
    }
    releaseMutex(&gStreamer->mMutex);
}

uint8_t loadTextureWithID(TFTextureID* pLoadTexID, TFTextureLoadDesc* pLoadDesc, TFSyncToken* pOutToken)
{
    uint8_t loaded = 0;

    // If this texture hasn't been loaded before, then load it
    if (gStreamer->pTextureReferenceCount[*pLoadTexID] == 0)
    {
        pLoadDesc->mOnTextureLoaded = OnStreamingTextureLoaded;
        pLoadDesc->pUserData = pLoadTexID;
        pLoadDesc->ppTexture = &gStreamer->ppAllTextures[*pLoadTexID];
        addResource(pLoadDesc, pOutToken);
        loaded = 1;
    }
    else
    {
        // If this texture is still being loaded reuse the token
        for (uint32_t j = 0; j < arrlenu(gStreamer->pLoadRequestQueue); j++)
        {
            if (gStreamer->pLoadRequestQueue[j].texID == *pLoadTexID)
            {
                *pOutToken = gStreamer->pLoadRequestQueue[j].loadToken;
                break;
            }
        }
    }

    // Increase the reference count for this texture
    gStreamer->pTextureReferenceCount[*pLoadTexID]++;

    return loaded;
}

void requestStreamLoad(uint32_t closestBoxIndex)
{
    // Check removal queue for texture
    bool haveTextures = false;
    bool haveNorms = false;
    bool haveSpecs = false;
    acquireMutex(&gStreamer->mMutex);
    StreamingZone* requestedZone = gStreamer->pZones + closestBoxIndex;
    uint8_t        numLoaded = 0;
    const uint32_t loadTexID = closestBoxIndex * TEXTURES_PER_MESH;

    gStreamer->pTexLoadedForMesh[closestBoxIndex] = true;
    for (uint32_t i = 0; i < arrlenu(gStreamer->pRemovalQueue); i++)
    {
        RemovalRequest* pRequest = gStreamer->pRemovalQueue + i;
        if (pRequest->mTexID == requestedZone->colorTexID)
        {
            haveTextures = true;
            numLoaded++;
            LoadRequest loadRequest = { requestedZone->colorTexID, loadTexID, pRequest->mToken };
            arrput(gStreamer->pLoadRequestQueue, loadRequest);
            gStreamer->pTextureReferenceCount[requestedZone->colorTexID]++;
            arrdelswap(gStreamer->pRemovalQueue, i);
            i -= 1;
        }
        else if (pRequest->mTexID == requestedZone->normTexID)
        {
            haveNorms = true;
            numLoaded++;
            LoadRequest loadRequest = { requestedZone->normTexID, loadTexID + 1, pRequest->mToken };
            arrput(gStreamer->pLoadRequestQueue, loadRequest);
            gStreamer->pTextureReferenceCount[requestedZone->normTexID]++;
            arrdelswap(gStreamer->pRemovalQueue, i);
            i -= 1;
        }
        else if (pRequest->mTexID == requestedZone->specTexID)
        {
            haveSpecs = true;
            numLoaded++;
            LoadRequest loadRequest = { requestedZone->specTexID, loadTexID + 2, pRequest->mToken };
            arrput(gStreamer->pLoadRequestQueue, loadRequest);
            gStreamer->pTextureReferenceCount[requestedZone->specTexID]++;
            arrdelswap(gStreamer->pRemovalQueue, i);
            i -= 1;
        }
    }

    if (!haveTextures && requestedZone->colorTexID != TEX_IDX_INVALID)
    {
        // request loading a material ( 3 textures )
        TFTextureLoadDesc* pDesc = requestedZone->pColorLoadDesc;
        // Textures representing color should be stored in SRGB or HDR format
        pDesc->mCreationFlag = TF_TEXTURE_CREATION_FLAG_SRGB;
        LoadRequest loadRequest = { requestedZone->colorTexID, loadTexID, 0 };
        numLoaded += loadTextureWithID(&requestedZone->colorTexID, pDesc, &loadRequest.loadToken);
        arrput(gStreamer->pLoadRequestQueue, loadRequest);
    }

    if (!haveNorms && requestedZone->normTexID != TEX_IDX_INVALID)
    {
        TFTextureLoadDesc* pDesc = requestedZone->pNormLoadDesc;
        LoadRequest        loadRequest = { requestedZone->normTexID, loadTexID + 1, 0 };
        numLoaded += loadTextureWithID(&requestedZone->normTexID, pDesc, &loadRequest.loadToken);
        arrput(gStreamer->pLoadRequestQueue, loadRequest);
    }

    if (!haveSpecs && requestedZone->specTexID != TEX_IDX_INVALID)
    {
        TFTextureLoadDesc* pDesc = requestedZone->pSpecTexLoadDesc;
        LoadRequest        loadRequest = { requestedZone->specTexID, loadTexID + 2, 0 };
        numLoaded += loadTextureWithID(&requestedZone->specTexID, pDesc, &loadRequest.loadToken);
        arrput(gStreamer->pLoadRequestQueue, loadRequest);
    }

    gStreamer->mLiveCount += numLoaded;

    releaseMutex(&gStreamer->mMutex);
}

uint8_t unloadTextureWithID(TFTextureID unloadTexID, uint32_t orderedIndex)
{
    uint8_t unloaded = 0;

    if (unloadTexID == TEX_IDX_INVALID)
    {
        // Attempting to unload invalid texture. Skip this.
        return unloaded;
    }

    ASSERT(gStreamer->pTextureReferenceCount[unloadTexID] > 0);

    // Decrease the reference count for this texture
    gStreamer->pTextureReferenceCount[unloadTexID]--;

    if (gStreamer->pTextureReferenceCount[unloadTexID] == 0)
    {
        // If this texture only has one reference remaining we can remove it
        RemovalRequest request;
        request.mDelay = gStreamer->mRemovalDelay;
        request.mTexID = unloadTexID;

        // Check for unhandled load requests
        for (uint32_t i = 0; i < arrlenu(gStreamer->pLoadRequestQueue); i++)
        {
            if (gStreamer->pLoadRequestQueue[i].texID == unloadTexID)
            {
                request.mToken = gStreamer->pLoadRequestQueue[i].loadToken;
                arrdelswap(gStreamer->pLoadRequestQueue, i);
                i--;
            }
        }

        arrput(gStreamer->pRemovalQueue, request);
        unloaded = 1;
    }
    else
    {
        // Check for unhandled load requests
        for (uint32_t i = 0; i < arrlenu(gStreamer->pLoadRequestQueue); i++)
        {
            if (gStreamer->pLoadRequestQueue[i].orderedIndex == orderedIndex)
            {
                arrdelswap(gStreamer->pLoadRequestQueue, i);
                i--;
            }
        }
    }

    LoadRequest loadRequest = { TEX_IDX_INVALID, orderedIndex, 0 };
    arrput(gStreamer->pLoadRequestQueue, loadRequest);

    return unloaded;
}

void requestStreamUnload(int furthestBoxIndex)
{
    acquireMutex(&gStreamer->mMutex);
    StreamingZone* requestedZone = gStreamer->pZones + furthestBoxIndex;
    uint32_t       unloadID = furthestBoxIndex * TEXTURES_PER_MESH;
    uint8_t        unloadedCount = 0;
    gStreamer->pTexLoadedForMesh[furthestBoxIndex] = false;

    if (requestedZone->colorTexID != TEX_IDX_INVALID)
    {
        unloadedCount += unloadTextureWithID(requestedZone->colorTexID, unloadID);
    }

    if (requestedZone->normTexID != TEX_IDX_INVALID)
    {
        unloadedCount += unloadTextureWithID(requestedZone->normTexID, unloadID + 1);
    }

    if (requestedZone->specTexID != TEX_IDX_INVALID)
    {
        unloadedCount += unloadTextureWithID(requestedZone->specTexID, unloadID + 2);
    }

    ASSERT(gStreamer->mLiveCount >= unloadedCount);
    gStreamer->mLiveCount -= unloadedCount;

    requestedZone->mLastLoaded = gStreamer->mUpdateCount;
    releaseMutex(&gStreamer->mMutex);
}

void processRemovals()
{
    acquireMutex(&gStreamer->mMutex);
    for (uint32_t i = 0; i < arrlenu(gStreamer->pRemovalQueue); i++)
    {
        RemovalRequest* pRequest = gStreamer->pRemovalQueue + i;
        pRequest->mDelay--;
        if (pRequest->mDelay == 0)
        {
            // Check for unhandled load request
            bool loaded = pRequest->mToken == 0 || isTokenCompleted(&pRequest->mToken);

            if (loaded)
            {
                removeResource(gStreamer->ppAllTextures[pRequest->mTexID]);
                heapAllocatorFree(gStreamer->pTexturesHeapAllocator, gStreamer->pAllocations[pRequest->mTexID].mOffset,
                                  gStreamer->pAllocations[pRequest->mTexID].mSize);
                arrdelswap(gStreamer->pRemovalQueue, i);
                i -= 1;
            }
            else
            {
                pRequest->mDelay++;
            }
        }
    }
    releaseMutex(&gStreamer->mMutex);
}

bool updateOrderedTextureList()
{
    bool updatedTextureList = false;
    acquireMutex(&gStreamer->mMutex);
    for (uint32_t i = 0; i < arrlenu(gStreamer->pLoadRequestQueue); i++)
    {
        LoadRequest* pLoadReq = gStreamer->pLoadRequestQueue + i;

        if (pLoadReq->loadToken == 0 || isTokenCompleted(&pLoadReq->loadToken))
        {
            if (pLoadReq->texID == TEX_IDX_INVALID)
            {
                updatedTextureList = true;
                gStreamer->ppOrderedTextures[pLoadReq->orderedIndex] = gStreamer->pDefaultTexture;
            }
            else
            {
                ASSERT(gStreamer->ppAllTextures[pLoadReq->texID]);
                updatedTextureList = true;
                gStreamer->ppOrderedTextures[pLoadReq->orderedIndex] = gStreamer->ppAllTextures[pLoadReq->texID];
            }
            arrdelswap(gStreamer->pLoadRequestQueue, i);
            i -= 1;
        }
    }
    releaseMutex(&gStreamer->mMutex);
    return updatedTextureList;
}

bool updateStreaming(const TFStreamingUpdateDesc* pDesc)
{
    processRemovals();

    const uint32_t maxRequests = 2;
    bool           isIdle = true;

    for (uint32_t i = 0; i < maxRequests; i++)
    {
        int bestBoxIndex = -1;
        bestBoxIndex = pDesc->mMode == TF_StreamingPixelCoverage ? highestPixelCoverageIndex(pDesc) : closestStreamingBoxIndex(pDesc);

        if (pDesc->mMaxLiveMaterials == gStreamer->mLiveCountMax)
        {
            // simple way to allow converging to happen
            // if we are requesting same streaming box and don't need to unload, we don't need to proceed
            if (bestBoxIndex == gStreamer->mLastLoadedIndex && gStreamer->mLiveCount <= gStreamer->mLiveCountMax)
            {
                break;
            }
        }

        gStreamer->mLiveCountMax = pDesc->mMaxLiveMaterials;

        if (bestBoxIndex > -1)
        {
            if (gStreamer->mLiveCount <= gStreamer->mLiveCountMax)
            {
                requestStreamLoad(bestBoxIndex);
                isIdle = false;
                gStreamer->mLastLoadedIndex = bestBoxIndex;
            }
        }

        // unload if we have more than max
        if (gStreamer->mLiveCount > gStreamer->mLiveCountMax)
        {
            int worstBoxIndex =
                pDesc->mMode == TF_StreamingPixelCoverage ? lowestPixelCoverageIndex(pDesc) : furthestStreamingBoxIndex(pDesc);

            if (worstBoxIndex > -1)
            {
                requestStreamUnload(worstBoxIndex);
                isIdle = false;
            }
        }
    }

    gStreamer->mState = isIdle ? TF_StreamingIdle : TF_StreamingActive;
    gStreamer->mUpdateCount++;

    bool updatedOrderedList = updateOrderedTextureList();
    return updatedOrderedList;
}
