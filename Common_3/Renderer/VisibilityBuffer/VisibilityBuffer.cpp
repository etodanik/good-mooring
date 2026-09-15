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

#include "../../../Common_3/Application/Interfaces/ICamera.h"
#include "../../../Common_3/Application/Interfaces/IProfiler.h"
#include "../../../Common_3/Graphics/Interfaces/IGraphics.h"
#include "../../../Common_3/Utilities/Interfaces/ILog.h"
#include "../../../Common_3/Utilities/Interfaces/ITime.h"
#include "../../../Common_3/Utilities/ThirdParty/OpenSource/Nothings/stb_ds.h"
#include "../Interfaces/IVisibilityBuffer.h"

#include "../../../Common_3/Utilities/RingBuffer.h"

#include "../../../Common_3/Utilities/Interfaces/IMemory.h"

// fsl
#include "../../../Common_3/Graphics/FSL/defaults.h"
#include "../VisibilityBuffer/Shaders/FSL/VisibilityBufferStructs.h.fsl"

/************************************************************************/
// Settings
/************************************************************************/
typedef struct VisibilityBufferSettings
{
    // This defines the amount of viewports that are going to be culled in parallel.
    uint32_t mNumViews;
    // Define different geometry sets (opaque and alpha tested geometry)
    uint32_t mNumGeomSets;

    uint32_t mNumFrames;
    uint32_t mNumBuffers;
    // The max amount of triangles that will be processed in parallel by the triangle filter shader
    uint32_t mComputeThreads;
    uint32_t mMaxFilterBatches;

    uint32_t mUniformBufferAlignment;

    uint32_t mMaxBatches;

    bool     mEnablePreSkinPass;
    uint32_t mPreSkinBatchSize;
    uint32_t mPreSkinBatchCount;
    uint32_t mNumPreSkinBatchChunks;
} VisibilityBufferSettings;

VisibilityBufferSettings gVBSettings;

/************************************************************************/
// Pre skin vertexes data
/************************************************************************/
typedef struct PreSkinBatchChunk
{
    uint32_t mCurrentBatchCount;
} PreSkinBatchChunk;

GPURingBuffer gPreSkinBatchDataBuffer = {};

static void DispatchPreSkinVertexes(TFCmd* pCmd, PreSkinBatchChunk* pBatchChunk, GPURingBufferOffset* ringBufferOffset,
                                    uint32_t batchDataOffsetBytes, const PreSkinVertexesPassDesc* pDesc)
{
    ASSERT(pBatchChunk->mCurrentBatchCount > 0);
    ASSERT(ringBufferOffset);
    TFDescriptorDataRange range = { (uint32_t)ringBufferOffset->mOffset + batchDataOffsetBytes,
                                    gVBSettings.mPreSkinBatchCount * (uint32_t)sizeof(PreSkinBatchData) };
    TFDescriptorData      params[1] = {};
    params[0].mIndex = pDesc->mPreSkinBatchIndex;
    params[0].ppBuffers = &ringBufferOffset->pBuffer;
    params[0].pRanges = &range;
    updateDescriptorSet(pCmd->pRenderer, pDesc->mFrameIndex, pDesc->pDescriptorSetPreSkinVertexesPerDraw, 1, params);
    cmdBindDescriptorSet(pCmd, pDesc->mFrameIndex, pDesc->pDescriptorSetPreSkinVertexesPerDraw);
    cmdDispatch(pCmd, pBatchChunk->mCurrentBatchCount, 1, 1);

    // Reset batch chunk to start adding vertexes to it
    pBatchChunk->mCurrentBatchCount = 0;
}

PreSkinVertexesStats cmdVisibilityBufferPreSkinVertexesPass(VisibilityBuffer* pVisibilityBuffer, TFCmd* pCmd,
                                                            PreSkinVertexesPassDesc* pDesc)
{
    UNREF_PARAM(pVisibilityBuffer);
    ASSERT(gVBSettings.mEnablePreSkinPass);
    ASSERT(gVBSettings.mPreSkinBatchSize > 0);
    ASSERT(gVBSettings.mPreSkinBatchCount > 0);

    PreSkinVertexesStats stats = {};

    cmdBeginGpuTimestampQuery(pCmd, pDesc->mGpuProfileToken, "Pre Skin Vertexes");

    cmdBindPipeline(pCmd, pDesc->pPipelinePreSkinVertexes);
    cmdBindDescriptorSet(pCmd, 0, pDesc->pDescriptorSetPreSkinVertexes);
    cmdBindDescriptorSet(pCmd, pDesc->mFrameIndex, pDesc->pDescriptorSetPreSkinVertexesPerFrame);

    PreSkinBatchChunk batchChunk = {};
    batchChunk.mCurrentBatchCount = 0;

    const uint32_t      maxTotalPreSkinBatches = gVBSettings.mPreSkinBatchCount * gVBSettings.mNumPreSkinBatchChunks;
    const uint64_t      size = maxTotalPreSkinBatches * sizeof(PreSkinBatchData);
    GPURingBufferOffset offset = getGPURingBufferOffset(&gPreSkinBatchDataBuffer, (uint32_t)size, (uint32_t)size);
    TFBufferUpdateDesc  updateDesc = { offset.pBuffer, offset.mOffset };
    beginUpdateResource(&updateDesc);

    PreSkinBatchData* batches = (PreSkinBatchData*)updateDesc.pMappedData;
    PreSkinBatchData* origin = batches;

    for (uint32_t container = 0; container < pDesc->mPreSkinContainerCount; ++container)
    {
        const PreSkinContainer* pPreSkinContainer = &pDesc->pPreSkinContainers[container];

        const uint32_t batchCount = (pPreSkinContainer->mVertexCount + gVBSettings.mPreSkinBatchSize - 1) / gVBSettings.mPreSkinBatchSize;
        stats.mTotalVertexBatches += batchCount;

        for (uint32_t batch = 0; batch < batchCount; ++batch)
        {
            const uint32_t firstVertex = batch * gVBSettings.mComputeThreads;
            const uint32_t lastVertex = min(firstVertex + gVBSettings.mPreSkinBatchSize, pPreSkinContainer->mVertexCount);
            const uint32_t vertexesInBatch = lastVertex - firstVertex;
            stats.mTotalVertexes += vertexesInBatch;

            batches[batchChunk.mCurrentBatchCount].outputVertexOffset = pPreSkinContainer->mOutputVertexOffset + firstVertex;
            batches[batchChunk.mCurrentBatchCount].vertexCount = vertexesInBatch;
            batches[batchChunk.mCurrentBatchCount].vertexPositionOffset = pPreSkinContainer->mVertexPositionOffset + firstVertex;
            batches[batchChunk.mCurrentBatchCount].vertexJointsOffset = pPreSkinContainer->mJointOffset + firstVertex;
            batches[batchChunk.mCurrentBatchCount].jointMatrixOffset = pPreSkinContainer->mJointMatrixOffset;
            ++batchChunk.mCurrentBatchCount;

            // If batcher are full dispatch and start a new one
            if (batchChunk.mCurrentBatchCount >= gVBSettings.mPreSkinBatchCount)
            {
                stats.mTotalShaderDispatches++;

                const uint32_t batchDataOffset = (uint32_t)(batches - origin);
                ASSERT(batchDataOffset + batchChunk.mCurrentBatchCount <= maxTotalPreSkinBatches);
                const uint32_t dispatchedBatchCount = batchChunk.mCurrentBatchCount;
                DispatchPreSkinVertexes(pCmd, &batchChunk, &offset, batchDataOffset * sizeof(PreSkinBatchData), pDesc);

                ASSERT(round_up(dispatchedBatchCount, gVBSettings.mUniformBufferAlignment) == dispatchedBatchCount &&
                       "batches pointer will end up with wrong alignment expected by the GPU");
                batches += dispatchedBatchCount;
            }
        }
    }

    if (batchChunk.mCurrentBatchCount > 0)
    {
        stats.mTotalShaderDispatches++;

        const uint32_t batchDataOffset = (uint32_t)(batches - origin);
        ASSERT(batchDataOffset + batchChunk.mCurrentBatchCount <= maxTotalPreSkinBatches);
        DispatchPreSkinVertexes(pCmd, &batchChunk, &offset, batchDataOffset * sizeof(PreSkinBatchData), pDesc);
    }

    endUpdateResource(&updateDesc);
    cmdEndGpuTimestampQuery(pCmd, pDesc->mGpuProfileToken);

    return stats;
}

/************************************************************************/
// Visibility Buffer Filtering
/************************************************************************/
VBPreFilterStats updateVBMeshFilterGroups(VisibilityBuffer* pVisibilityBuffer, const UpdateVBMeshFilterGroupsDesc* pDesc)
{
    ASSERT(pVisibilityBuffer);
    ASSERT(pDesc);

    VBPreFilterStats vbPreFilterStats = {};
    uint32_t         dispatchGroupCount = 0;

    TFBufferUpdateDesc updateDesc = { pVisibilityBuffer->ppFilterDispatchGroupDataBuffer[pDesc->mFrameIndex], 0 };
    beginUpdateResource(&updateDesc);
    FilterDispatchGroupData* dispatchGroupData = (FilterDispatchGroupData*)updateDesc.pMappedData;

    for (uint32_t i = 0; i < pDesc->mNumMeshInstance; ++i)
    {
        VBMeshInstance* pVBMeshInstance = &pDesc->pVBMeshInstances[i];

        uint32_t numDispatchGroups = (pVBMeshInstance->mTriangleCount + gVBSettings.mComputeThreads - 1) / gVBSettings.mComputeThreads;

        for (uint32_t groupIdx = 0; groupIdx < numDispatchGroups; ++groupIdx)
        {
            FilterDispatchGroupData& groupData = dispatchGroupData[dispatchGroupCount++];

            const uint32_t firstTriangle = groupIdx * gVBSettings.mComputeThreads;
            const uint32_t lastTriangle = min(firstTriangle + gVBSettings.mComputeThreads, pVBMeshInstance->mTriangleCount);
            const uint32_t trianglesInGroup = lastTriangle - firstTriangle;

            // Fill GPU filter batch data
            ASSERT(trianglesInGroup <= gVBSettings.mComputeThreads && "Exceeds max face count!");
            groupData.meshIndex = pVBMeshInstance->mMeshIndex;
            groupData.instanceDataIndex = pVBMeshInstance->mInstanceIndex;
            groupData.geometrySet_faceCount = ((trianglesInGroup << BATCH_FACE_COUNT_LOW_BIT) & BATCH_FACE_COUNT_MASK) |
                                              ((pVBMeshInstance->mGeometrySet << BATCH_GEOMETRY_LOW_BIT) & BATCH_GEOMETRY_MASK);

            // Offset relative to the start of the mesh
            groupData.indexOffset = firstTriangle * 3;
        }

        ASSERT(pVBMeshInstance->mGeometrySet < TF_ARRAY_COUNT(vbPreFilterStats.mGeomsetMaxDrawCounts));
        ++vbPreFilterStats.mGeomsetMaxDrawCounts[pVBMeshInstance->mGeometrySet];
    }
    endUpdateResource(&updateDesc);

    vbPreFilterStats.mNumDispatchGroups = dispatchGroupCount;
    return vbPreFilterStats;
}

// Executes the compute shader that performs triangle filtering on the GPU.
// This step performs different visibility tests per triangle to determine whether they
// potentially affect to the final image or not.
// The results of executing this shader are stored in:
// - pFilteredTriangles: list of triangle IDs that passed the culling tests
// - pIndirectDrawArguments: the vertexCount member of this structure is calculated in order to
// indicate the renderer the amount of vertices per batch to render.
void cmdVBTriangleFilteringPass(VisibilityBuffer* pVisibilityBuffer, TFCmd* pCmd, TriangleFilteringPassDesc* pDesc)
{
    ASSERT(pVisibilityBuffer);
    ASSERT(pDesc->mFrameIndex < gVBSettings.mNumFrames);
    ASSERT(pDesc->mBuffersIndex < gVBSettings.mNumBuffers);
    ASSERT(pDesc->mVBPreFilterStats.mNumDispatchGroups < gVBSettings.mMaxFilterBatches);

#if defined(TF_ENABLE_WORKGRAPH)
    if (pDesc->pWorkgraph)
    {
        /************************************************************************/
        // Clear previous indirect arguments
        /************************************************************************/
        cmdBeginGpuTimestampQuery(pCmd, pDesc->mGpuProfileToken, "Clear Buffers");
        cmdBindPipeline(pCmd, pDesc->pPipelineClearBuffers);
        cmdBindDescriptorSet(pCmd, pDesc->mFrameIndex, pDesc->pDescriptorSetTriangleFilteringPerBatch);
        cmdDispatch(pCmd, 1, 1, 1);
        cmdEndGpuTimestampQuery(pCmd, pDesc->mGpuProfileToken);

        /************************************************************************/
        // Synchronization
        /************************************************************************/
        cmdBeginGpuTimestampQuery(pCmd, pDesc->mGpuProfileToken, "Clear Buffers Synchronization");
        TFBufferBarrier filterBufferBarrier[2] = {
            { pVisibilityBuffer->ppFilterDispatchGroupDataBuffer[pDesc->mFrameIndex], TF_RESOURCE_STATE_UNORDERED_ACCESS,
              TF_RESOURCE_STATE_UNORDERED_ACCESS },
            { pVisibilityBuffer->ppIndirectDrawArgBuffer[pDesc->mBuffersIndex], TF_RESOURCE_STATE_UNORDERED_ACCESS,
              TF_RESOURCE_STATE_UNORDERED_ACCESS },
        };
        cmdResourceBarrier(pCmd, TF_ARRAY_COUNT(filterBufferBarrier), filterBufferBarrier, 0, nullptr, 0, nullptr);
        cmdEndGpuTimestampQuery(pCmd, pDesc->mGpuProfileToken);

        cmdBeginGpuTimestampQuery(pCmd, pDesc->mGpuProfileToken, "GPU Pipeline");
        cmdBindDescriptorSet(pCmd, pDesc->mFrameIndex, pDesc->pDescriptorSetTriangleFilteringPerBatch);
        struct EntryRecord
        {
            uint32_t mNumDispatchGroups;
        };
        EntryRecord         record = { pDesc->mVBPreFilterStats.mNumDispatchGroups };
        TFDispatchGraphDesc dispatchDesc = {};
        dispatchDesc.mInitialize = pDesc->mInitWorkgraph;
        dispatchDesc.mInputStride = sizeof(EntryRecord);
        dispatchDesc.mInputType = TF_DISPATCH_GRAPH_INPUT_CPU;
        dispatchDesc.pInput = &record;
        dispatchDesc.pWorkgraph = pDesc->pWorkgraph;
        cmdDispatchWorkgraph(pCmd, &dispatchDesc);
        cmdEndGpuTimestampQuery(pCmd, pDesc->mGpuProfileToken);
        return;
    }
#endif
    /************************************************************************/
    // Clear previous indirect arguments
    /************************************************************************/
    cmdBeginGpuTimestampQuery(pCmd, pDesc->mGpuProfileToken, "Clear Buffers");
    cmdBindPipeline(pCmd, pDesc->pPipelineClearBuffers);
    cmdBindDescriptorSet(pCmd, pDesc->mFrameIndex, pDesc->pDescriptorSetTriangleFilteringPerBatch);
    cmdDispatch(pCmd, 1, 1, 1);
    cmdEndGpuTimestampQuery(pCmd, pDesc->mGpuProfileToken);

    /************************************************************************/
    // Synchronization
    /************************************************************************/
    uint32_t bufferIndex = 0;
    cmdBeginGpuTimestampQuery(pCmd, pDesc->mGpuProfileToken, "Clear Buffers Synchronization");
    bufferIndex = 0;
    TFBufferBarrier filterBufferBarrier[2] = {};
    filterBufferBarrier[bufferIndex++] = { pVisibilityBuffer->ppFilterDispatchGroupDataBuffer[pDesc->mFrameIndex],
                                           TF_RESOURCE_STATE_UNORDERED_ACCESS, TF_RESOURCE_STATE_UNORDERED_ACCESS };

    filterBufferBarrier[bufferIndex++] = { pVisibilityBuffer->ppIndirectDrawArgBuffer[pDesc->mBuffersIndex],
                                           TF_RESOURCE_STATE_UNORDERED_ACCESS, TF_RESOURCE_STATE_UNORDERED_ACCESS };
    cmdResourceBarrier(pCmd, bufferIndex, filterBufferBarrier, 0, nullptr, 0, nullptr);

    cmdEndGpuTimestampQuery(pCmd, pDesc->mGpuProfileToken);
    /************************************************************************/
    // Run triangle filtering shader
    /************************************************************************/
    cmdBeginGpuTimestampQuery(pCmd, pDesc->mGpuProfileToken, "Filter Triangles");
    if (pDesc->mVBPreFilterStats.mNumDispatchGroups > 0)
    {
        cmdBindPipeline(pCmd, pDesc->pPipelineTriangleFiltering);
        cmdBindDescriptorSet(pCmd, pDesc->mFrameIndex, pDesc->pDescriptorSetTriangleFilteringPerBatch);
        cmdDispatch(pCmd, pDesc->mVBPreFilterStats.mNumDispatchGroups, 1, 1);
    }
    cmdEndGpuTimestampQuery(pCmd, pDesc->mGpuProfileToken);
}

static void addPreSkinACAliasedBuffer(TFRenderer* pRenderer, PreSkinACVertexBuffersDesc* pDesc, TFResourceHeap* pHeap, uint64_t heapOffset,
                                      uint64_t vbSize, PreSkinACAliasedBuffer* pOut, uint32_t attrStride, const char* pPreSkinBufferName)
{
    UNREF_PARAM(pRenderer);
    // Placement for the VertexBuffers
    pOut->mVBSize = vbSize;
    pOut->mVBPlacement.pHeap = pHeap;
    pOut->mVBPlacement.mOffset = heapOffset;

    // Aliased Pre-Skin Output Buffers span the entire Vertex Buffer, this makes code simpler and we just have to bind one buffer in the
    // shader
    TFResourcePlacement preSkinnedVertexPlacement = {};
    preSkinnedVertexPlacement.pHeap = pHeap;
    preSkinnedVertexPlacement.mOffset = heapOffset;

    TFBufferLoadDesc bufferDesc = {};
    bufferDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_BUFFER_RAW | TF_DESCRIPTOR_TYPE_RW_BUFFER_RAW;
    bufferDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_GPU_ONLY;
    bufferDesc.mDesc.mStartState = TF_RESOURCE_STATE_UNORDERED_ACCESS;
    bufferDesc.mDesc.mElementCount = (uint32_t)(vbSize / sizeof(uint32_t));
    bufferDesc.mDesc.mSize = vbSize;
    bufferDesc.mDesc.mStructStride = attrStride;
    bufferDesc.mDesc.pName = pPreSkinBufferName;
    bufferDesc.mDesc.pPlacement = &preSkinnedVertexPlacement;
    for (uint32_t i = 0; i < pDesc->mNumBuffers; ++i)
    {
        bufferDesc.ppBuffer = &pOut->pPreSkinBuffers[i];
        addResource(&bufferDesc, NULL);

        const uint64_t shaderOffsetBytes = attrStride * (pDesc->mMaxStaticVertexCount + pDesc->mMaxPreSkinnedVertexCountPerFrame * i);

        // We'll need to reserver this memory in the TFBufferChunkAllocator so that it's not used for meshes
        {
            if (i == 0)
                pOut->mOutputMemoryStartOffset = shaderOffsetBytes;

            const uint64_t thisBufferOutputMemorySize = shaderOffsetBytes + pDesc->mMaxPreSkinnedVertexCountPerFrame * attrStride;
            pOut->mOutputMemorySize = thisBufferOutputMemorySize - pOut->mOutputMemoryStartOffset;
            ASSERT(pOut->mOutputMemoryStartOffset + pOut->mOutputMemorySize <= vbSize);
        }
    }
}

static void removePreSkinACAliasedBuffer(TFRenderer* pRenderer, PreSkinACAliasedBuffer* pBuffers)
{
    UNREF_PARAM(pRenderer);
    for (uint32_t i = 0; i < gVBSettings.mNumBuffers; ++i)
        removeResource(pBuffers->pPreSkinBuffers[i]);
}

void initVBAsyncComputePreSkinVertexBuffers(TFRenderer* pRenderer, PreSkinACVertexBuffersDesc* pDesc, PreSkinACVertexBuffers** pOut)
{
    ASSERT(pDesc);
    ASSERT(pOut);
    ASSERT(pDesc->mNumBuffers > 0);
    ASSERT(pDesc->mMaxStaticVertexCount > 0);
    ASSERT(pDesc->mMaxPreSkinnedVertexCountPerFrame > 0);

    PreSkinACVertexBuffers* pBuffers = (PreSkinACVertexBuffers*)tf_calloc(1, sizeof(PreSkinACVertexBuffers));

    TFResourceSizeAlign sizeAlignedVertexPositionBuffer = {};
    TFResourceSizeAlign sizeAlignedVertexNormalBuffer = {};

    // Create Heap to store all the main Vertex Buffers for skinned attributes.
    // This memory includes extra memory for pre-skinned vertex output.
    {
        // Make sure we allocate enough memory to hold the VB for all atributes with correct alignments
        TFBufferLoadDesc vertexPositionBufferDesc = {};
        vertexPositionBufferDesc.mDesc.mDescriptors =
            TF_DESCRIPTOR_TYPE_VERTEX_BUFFER | (TF_DESCRIPTOR_TYPE_BUFFER_RAW | TF_DESCRIPTOR_TYPE_RW_BUFFER_RAW);
        vertexPositionBufferDesc.mDesc.mSize =
            (pDesc->mMaxStaticVertexCount + pDesc->mMaxPreSkinnedVertexCountPerFrame * pDesc->mNumBuffers) * sizeof(float3);
        vertexPositionBufferDesc.mDesc.mElementCount = (uint32_t)(vertexPositionBufferDesc.mDesc.mSize / sizeof(uint32_t));
        vertexPositionBufferDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_GPU_ONLY;
        vertexPositionBufferDesc.mDesc.mStructStride = sizeof(float3);
        getResourceSizeAlign(&vertexPositionBufferDesc, &sizeAlignedVertexPositionBuffer);

        TFBufferLoadDesc vertexNormalBufferDesc = {};
        vertexNormalBufferDesc.mDesc.mDescriptors =
            TF_DESCRIPTOR_TYPE_VERTEX_BUFFER | (TF_DESCRIPTOR_TYPE_BUFFER_RAW | TF_DESCRIPTOR_TYPE_RW_BUFFER_RAW);
        vertexNormalBufferDesc.mDesc.mSize =
            (pDesc->mMaxStaticVertexCount + pDesc->mMaxPreSkinnedVertexCountPerFrame * pDesc->mNumBuffers) * sizeof(uint32_t);
        vertexNormalBufferDesc.mDesc.mElementCount = (uint32_t)(vertexNormalBufferDesc.mDesc.mSize / sizeof(uint32_t));
        vertexNormalBufferDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_GPU_ONLY;
        vertexNormalBufferDesc.mDesc.mStructStride = sizeof(uint32_t);
        getResourceSizeAlign(&vertexNormalBufferDesc, &sizeAlignedVertexNormalBuffer);

        // TODO: There are issues when buffer size is greater than uint32_max, the value is truncated in D3D12 by one call to
        // ID3D12Device::GetCopyableFootprints.
        //       For now 4Gb of vertex buffer memory per attribute seems more than enough
        ASSERT(sizeAlignedVertexPositionBuffer.mSize <= UINT32_MAX);
        ASSERT(sizeAlignedVertexNormalBuffer.mSize <= UINT32_MAX);

        const uint64_t totalRequiredSize = sizeAlignedVertexPositionBuffer.mSize + sizeAlignedVertexPositionBuffer.mAlignment +
                                           sizeAlignedVertexNormalBuffer.mSize + sizeAlignedVertexNormalBuffer.mAlignment;

        TFResourceHeapDesc desc = {};
        desc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_GPU_ONLY;
        desc.mDescriptors = TF_DESCRIPTOR_TYPE_BUFFER | (TF_DESCRIPTOR_TYPE_BUFFER_RAW | TF_DESCRIPTOR_TYPE_RW_BUFFER_RAW);
        desc.mFlags = TF_RESOURCE_HEAP_FLAG_ALLOW_ONLY_BUFFERS;

        desc.mAlignment = sizeAlignedVertexPositionBuffer.mAlignment; // Align it to the alignment of the first vertex buffer
        desc.mSize = totalRequiredSize;
        desc.pName = "Skinned VertexBuffer Heap";
        addResourceHeap(pRenderer, &desc, &pBuffers->pHeap);
    }

    const uint64_t posOffset = 0; // Heap is already aligned to the alignment of this vertex buffer
    const uint64_t normalOffset = round_up_64(posOffset + sizeAlignedVertexPositionBuffer.mSize, sizeAlignedVertexNormalBuffer.mAlignment);

    addPreSkinACAliasedBuffer(pRenderer, pDesc, pBuffers->pHeap, posOffset, sizeAlignedVertexPositionBuffer.mSize, &pBuffers->mPositions,
                              sizeof(float3), "PreSkinBuffer Positions");
    addPreSkinACAliasedBuffer(pRenderer, pDesc, pBuffers->pHeap, normalOffset, sizeAlignedVertexNormalBuffer.mSize, &pBuffers->mNormals,
                              sizeof(uint32_t), "PreSkinBuffer Normals");

    static PreSkinBufferOffsets offsets[VISIBILITY_BUFFER_MAX_NUM_BUFFERS] = {};

    for (uint32_t i = 0; i < pDesc->mNumBuffers; ++i)
    {
        const uint64_t vertexOffset = (uint64_t)pDesc->mMaxStaticVertexCount + (uint64_t)pDesc->mMaxPreSkinnedVertexCountPerFrame * i;
        ASSERT(vertexOffset <= UINT32_MAX &&
               "If we support vertex buffers of more than UINT32_MAX bytes we need to either pass this variable to the shader as uint64 or "
               "place the Pre-Skin buffers at the beggining so that the offset is within limits.");

        offsets[i].vertexOffset = (uint32_t)vertexOffset;

        TFBufferLoadDesc desc = {};
        desc.pData = &offsets[i];
        desc.ppBuffer = &pBuffers->pShaderOffsets[i];
        desc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_BUFFER | TF_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        desc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_GPU_ONLY;
        desc.mDesc.mElementCount = 1;
        desc.mDesc.mStructStride = sizeof(PreSkinBufferOffsets);
        desc.mDesc.mSize = desc.mDesc.mElementCount * desc.mDesc.mStructStride;
        desc.mDesc.pName = "PreSkinACShaderBufferOffsets";
        addResource(&desc, NULL);
    }

    *pOut = pBuffers;
}

void exitVBAsyncComputePreSkinVertexBuffers(TFRenderer* pRenderer, PreSkinACVertexBuffers* pBuffers)
{
    ASSERT(pRenderer);
    ASSERT(pBuffers);

    for (uint32_t i = 0; i < gVBSettings.mNumBuffers; ++i)
        removeResource(pBuffers->pShaderOffsets[i]);

    removePreSkinACAliasedBuffer(pRenderer, &pBuffers->mPositions);
    removePreSkinACAliasedBuffer(pRenderer, &pBuffers->mNormals);

    removeResourceHeap(pRenderer, pBuffers->pHeap);

    tf_free(pBuffers);
}

//************************************************************************/
//
//************************************************************************/
bool initVisibilityBuffer(TFRenderer* pRenderer, const VisibilityBufferDesc* pDesc, VisibilityBuffer** ppVisibilityBuffer)
{
    ASSERT(ppVisibilityBuffer);
    ASSERT(pDesc);
    ASSERT(pDesc->mNumFrames > 0);
    ASSERT(pDesc->mNumBuffers > 0);
    ASSERT(pDesc->mNumGeometrySets > 0);
    ASSERT(pDesc->pMaxIndexCountPerGeomSet);
    ASSERT(pDesc->mNumGeometrySets <= VISIBILITY_BUFFER_MAX_GEOMETRY_SETS &&
           "Please update the configuration macro named VISIBILITY_BUFFER_MAX_GEOMETRY_SETS to be of the proper value");
    ASSERT(pDesc->mNumViews > 0);
    ASSERT(pDesc->mComputeThreads > 0);

    VisibilityBuffer* pVisibilityBuffer = (VisibilityBuffer*)tf_malloc(sizeof(VisibilityBuffer));
    pVisibilityBuffer->pRenderer = pRenderer;
    gVBSettings.mUniformBufferAlignment = pRenderer->pGpu->mUniformBufferAlignment;
    gVBSettings.mNumGeomSets = pDesc->mNumGeometrySets;
    gVBSettings.mNumViews = pDesc->mNumViews;
    gVBSettings.mNumFrames = pDesc->mNumFrames;
    gVBSettings.mNumBuffers = pDesc->mNumBuffers;
    gVBSettings.mComputeThreads = pDesc->mComputeThreads;

    if (pDesc->mEnablePreSkinPass)
    {
        ASSERT(pDesc->mPreSkinBatchSize > 0);
        ASSERT(pDesc->mPreSkinBatchCount > 0);

        gVBSettings.mEnablePreSkinPass = pDesc->mEnablePreSkinPass;
        gVBSettings.mPreSkinBatchSize = pDesc->mPreSkinBatchSize;
        gVBSettings.mPreSkinBatchCount = pDesc->mPreSkinBatchCount;
        gVBSettings.mNumPreSkinBatchChunks =
            max(1U, 512U / pDesc->mPreSkinBatchSize) * 16U; // number of batch chunks for vertex pre skinning
    }

    TFSyncToken token = {};

    // Create filter batch data buffers
    pVisibilityBuffer->ppFilterDispatchGroupDataBuffer = (TFBuffer**)tf_malloc(sizeof(TFBuffer*) * pDesc->mNumFrames);
    TFBufferLoadDesc filterBatchDesc = {};
    filterBatchDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_BUFFER;
    filterBatchDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
    // Worst case 1 triangle in each mesh triangle batch.
    // Optimal each batch is filled with "gVBSettings.mComputeThreads" triangles.
    // Current based half of pDesc->mComputeThreads, expect at least half of all batches is filled.
    uint32_t maxIndices = 0;
    for (uint32_t geomSet = 0; geomSet < pDesc->mNumGeometrySets; ++geomSet)
    {
        maxIndices += pDesc->pMaxIndexCountPerGeomSet[geomSet];
    }
    gVBSettings.mMaxFilterBatches = (maxIndices / 3) / (pDesc->mComputeThreads >> 1);

    filterBatchDesc.mDesc.mElementCount = gVBSettings.mMaxFilterBatches;
    filterBatchDesc.mDesc.mStructStride = sizeof(FilterDispatchGroupData);
    filterBatchDesc.mDesc.mSize = filterBatchDesc.mDesc.mElementCount * filterBatchDesc.mDesc.mStructStride;
    filterBatchDesc.mDesc.mStartState = TF_RESOURCE_STATE_UNORDERED_ACCESS;
    filterBatchDesc.mDesc.pName = "FilterDispatchGroupDataBuffer";
    filterBatchDesc.pData = NULL;

    for (uint32_t i = 0; i < pDesc->mNumFrames; ++i)
    {
        filterBatchDesc.ppBuffer = &pVisibilityBuffer->ppFilterDispatchGroupDataBuffer[i];
        addResource(&filterBatchDesc, &token);
    }

    // Create IndirectDataBuffers
    pVisibilityBuffer->ppIndirectDataBuffer = (TFBuffer**)tf_malloc(sizeof(TFBuffer*) * pDesc->mNumFrames);
    TFBufferLoadDesc indirectIbDesc = {};
    indirectIbDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_BUFFER | TF_DESCRIPTOR_TYPE_RW_BUFFER;
    indirectIbDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_GPU_ONLY;
    indirectIbDesc.mDesc.mElementCount = maxIndices;
    indirectIbDesc.mDesc.mStructStride = sizeof(uint32_t);
    indirectIbDesc.mDesc.mSize = indirectIbDesc.mDesc.mElementCount * indirectIbDesc.mDesc.mStructStride;
    indirectIbDesc.mDesc.mStartState = TF_RESOURCE_STATE_UNORDERED_ACCESS;
    indirectIbDesc.mDesc.pName = "IndirectDataBuffer";
    indirectIbDesc.pData = NULL;
    for (uint32_t i = 0; i < pDesc->mNumFrames; ++i)
    {
        indirectIbDesc.ppBuffer = &pVisibilityBuffer->ppIndirectDataBuffer[i];
        addResource(&indirectIbDesc, NULL);
    }

    // create dedicated VRAM heap for filtered index buffers
    uint32_t heapBlockSize = maxIndices * sizeof(uint32_t);
    heapBlockSize = (heapBlockSize + 0xffff) & ~0xffff;

    TFResourceHeapDesc heapDesc = {};
    heapDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_GPU_ONLY;
    heapDesc.mDescriptors = TF_DESCRIPTOR_TYPE_INDEX_BUFFER | TF_DESCRIPTOR_TYPE_BUFFER_RAW | TF_DESCRIPTOR_TYPE_RW_BUFFER_RAW;
    heapDesc.mFlags = TF_RESOURCE_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
    heapDesc.mAlignment = 64 * 1024; // align to 64 k
    heapDesc.mSize = heapBlockSize * pDesc->mNumBuffers * pDesc->mNumViews;
    heapDesc.pName = "FilteredIndexBuffer heap";
    addResourceHeap(pRenderer, &heapDesc, &pVisibilityBuffer->pFilteredIndexBufferHeap);

    // Create filteredIndexBuffers
    pVisibilityBuffer->ppFilteredIndexBuffer = (TFBuffer**)tf_malloc(sizeof(TFBuffer*) * pDesc->mNumBuffers * pDesc->mNumViews);
    TFBufferLoadDesc filterIbDesc = {};
    filterIbDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_INDEX_BUFFER | TF_DESCRIPTOR_TYPE_BUFFER_RAW | TF_DESCRIPTOR_TYPE_RW_BUFFER_RAW;
    filterIbDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_GPU_ONLY;
    filterIbDesc.mDesc.mElementCount = maxIndices;
    filterIbDesc.mDesc.mStructStride = sizeof(uint32_t);
    filterIbDesc.mDesc.mSize = filterIbDesc.mDesc.mElementCount * filterIbDesc.mDesc.mStructStride;
    filterIbDesc.mDesc.mStartState = TF_RESOURCE_STATE_UNORDERED_ACCESS;
    filterIbDesc.mDesc.pName = "FilteredIndexBuffer";
    filterIbDesc.pData = NULL;

    TFResourcePlacement* placements = NULL;
    for (uint32_t i = 0; i < pDesc->mNumBuffers * pDesc->mNumViews; ++i)
    {
        TFResourcePlacement placement = { pVisibilityBuffer->pFilteredIndexBufferHeap, i * heapBlockSize };
        arrpush(placements, placement);
        filterIbDesc.ppBuffer = &pVisibilityBuffer->ppFilteredIndexBuffer[i];
        filterIbDesc.mDesc.pPlacement = placements + i;
        addResource(&filterIbDesc, NULL);
    }
    arrfree(placements);
    // Create ppIndirectDrawArgBuffer
    pVisibilityBuffer->ppIndirectDrawArgBuffer = (TFBuffer**)tf_malloc(sizeof(TFBuffer*) * pDesc->mNumBuffers);

    TFBufferLoadDesc indirectDrawArgsDesc = {};
    indirectDrawArgsDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_BUFFER | TF_DESCRIPTOR_TYPE_RW_BUFFER | TF_DESCRIPTOR_TYPE_INDIRECT_BUFFER;
    indirectDrawArgsDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_GPU_ONLY;
    indirectDrawArgsDesc.mDesc.mElementCount = pDesc->mNumGeometrySets * pDesc->mNumViews * (8);
    indirectDrawArgsDesc.mDesc.mStructStride = sizeof(uint32_t);
    indirectDrawArgsDesc.mDesc.mSize = indirectDrawArgsDesc.mDesc.mElementCount * indirectDrawArgsDesc.mDesc.mStructStride;
    indirectDrawArgsDesc.mDesc.mStartState = TF_RESOURCE_STATE_UNORDERED_ACCESS;
    indirectDrawArgsDesc.mDesc.pName = "Indirect draw arg buffer";
    indirectDrawArgsDesc.pData = nullptr;

    for (uint32_t i = 0; i < pDesc->mNumBuffers; ++i)
    {
        indirectDrawArgsDesc.ppBuffer = &pVisibilityBuffer->ppIndirectDrawArgBuffer[i];
        addResource(&indirectDrawArgsDesc, NULL);
    }

    if (pDesc->mEnablePreSkinPass)
    {
        const uint32_t skinBatchRingBufferSizeTotal =
            pDesc->mNumFrames * (gVBSettings.mUniformBufferAlignment +
                                 gVBSettings.mNumPreSkinBatchChunks * pDesc->mPreSkinBatchCount * sizeof(PreSkinBatchData));
        addUniformGPURingBuffer(pRenderer, skinBatchRingBufferSizeTotal, &gPreSkinBatchDataBuffer);
    }

    // Create VB constant buffer
    pVisibilityBuffer->pVBConstants = (VBConstants*)tf_malloc(sizeof(VBConstants) * pDesc->mNumGeometrySets);
    uint32_t indexOffset = 0;
    for (uint32_t geomSet = 0; geomSet < pDesc->mNumGeometrySets; ++geomSet)
    {
        pVisibilityBuffer->pVBConstants[geomSet].indexOffset = indexOffset;
        indexOffset += pDesc->pMaxIndexCountPerGeomSet[geomSet];
    }

    TFBufferLoadDesc constantDesc = {};
    constantDesc.pData = pVisibilityBuffer->pVBConstants;
    constantDesc.ppBuffer = &pVisibilityBuffer->pVBConstantBuffer;
    constantDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_BUFFER | TF_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    constantDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_GPU_ONLY;
    constantDesc.mDesc.mElementCount = pDesc->mNumGeometrySets;
    constantDesc.mDesc.mStructStride = sizeof(VBConstants);
    constantDesc.mDesc.mSize = constantDesc.mDesc.mElementCount * constantDesc.mDesc.mStructStride;
    constantDesc.mDesc.pName = "VBConstantBuffer";
    addResource(&constantDesc, &token);

    waitForToken(&token);

    *ppVisibilityBuffer = pVisibilityBuffer;
    return true;
}

void exitVisibilityBuffer(VisibilityBuffer* pVisibilityBuffer)
{
    ASSERT(pVisibilityBuffer);
    removeResource(pVisibilityBuffer->pVBConstantBuffer);
    tf_free(pVisibilityBuffer->pVBConstants);
    pVisibilityBuffer->pVBConstants = NULL;

    for (uint32_t i = 0; i < gVBSettings.mNumBuffers; ++i)
    {
        removeResource(pVisibilityBuffer->ppIndirectDrawArgBuffer[i]);
        for (uint32_t v = 0; v < gVBSettings.mNumViews; ++v)
        {
            removeResource(pVisibilityBuffer->ppFilteredIndexBuffer[i * gVBSettings.mNumViews + v]);
        }
    }

    for (uint32_t i = 0; i < gVBSettings.mNumFrames; ++i)
    {
        removeResource(pVisibilityBuffer->ppFilterDispatchGroupDataBuffer[i]);
        removeResource(pVisibilityBuffer->ppIndirectDataBuffer[i]);
    }

    if (gVBSettings.mEnablePreSkinPass)
    {
        removeGPURingBuffer(&gPreSkinBatchDataBuffer);
    }

    tf_free(pVisibilityBuffer->ppFilterDispatchGroupDataBuffer);
    pVisibilityBuffer->ppFilterDispatchGroupDataBuffer = NULL;
    tf_free(pVisibilityBuffer->ppFilteredIndexBuffer);
    pVisibilityBuffer->ppFilteredIndexBuffer = NULL;
    tf_free(pVisibilityBuffer->ppIndirectDataBuffer);
    pVisibilityBuffer->ppIndirectDataBuffer = NULL;
    tf_free(pVisibilityBuffer->ppIndirectDrawArgBuffer);
    pVisibilityBuffer->ppIndirectDrawArgBuffer = NULL;
    removeResourceHeap(pVisibilityBuffer->pRenderer, pVisibilityBuffer->pFilteredIndexBufferHeap);

    tf_free(pVisibilityBuffer);
    pVisibilityBuffer = NULL;
}
