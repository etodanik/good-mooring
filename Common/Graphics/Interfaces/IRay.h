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

#include "IGraphics.h"

#ifdef METAL
#import <MetalKit/MetalKit.h>
#import <MetalPerformanceShaders/MetalPerformanceShaders.h>
#endif

typedef struct TFRenderer                        TFRenderer;
typedef struct TFRaytracing                      TFRaytracing;
typedef struct TFBuffer                          TFBuffer;
typedef struct TFTexture                         TFTexture;
typedef struct TFCmd                             TFCmd;
typedef struct TFAccelerationStructure           TFAccelerationStructure;
typedef struct TFAccelerationStructureDescBottom TFAccelerationStructureDescBottom;
typedef struct TFRootSignature                   TFRootSignature;
typedef struct TFShaderResource                  TFShaderResource;
typedef struct TFDescriptorData                  TFDescriptorData;
typedef struct ID3D12Device5                     ID3D12Device5;
typedef struct TFSSVGFDenoiser                   TFSSVGFDenoiser;

typedef enum TFAccelerationStructureType
{
    TF_ACCELERATION_STRUCTURE_TYPE_BOTTOM = 0,
    TF_ACCELERATION_STRUCTURE_TYPE_TOP,
} TFAccelerationStructureType;

// Supported by DXR. Metal ignores this.
typedef enum TFAccelerationStructureBuildFlags
{
    TF_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE = 0,
    TF_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE = 0x1,
    TF_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_COMPACTION = 0x2,
    TF_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE = 0x4,
    TF_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD = 0x8,
    TF_ACCELERATION_STRUCTURE_BUILD_FLAG_MINIMIZE_MEMORY = 0x10,
    TF_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE = 0x20,
} TFAccelerationStructureBuildFlags;
MAKE_ENUM_FLAG(uint32_t, TFAccelerationStructureBuildFlags)

// Rustam: check if this can be mapped to Metal
typedef enum TFAccelerationStructureGeometryFlags
{
    TF_ACCELERATION_STRUCTURE_GEOMETRY_FLAG_NONE = 0,
    TF_ACCELERATION_STRUCTURE_GEOMETRY_FLAG_OPAQUE = 0x1,
    TF_ACCELERATION_STRUCTURE_GEOMETRY_FLAG_NO_DUPLICATE_ANYHIT_INVOCATION = 0x2
} TFAccelerationStructureGeometryFlags;
MAKE_ENUM_FLAG(uint32_t, TFAccelerationStructureGeometryFlags)

// Rustam: check if this can be mapped to Metal
typedef enum TFAccelerationStructureInstanceFlags
{
    TF_ACCELERATION_STRUCTURE_INSTANCE_FLAG_NONE = 0,
    TF_ACCELERATION_STRUCTURE_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE = 0x1,
    TF_ACCELERATION_STRUCTURE_INSTANCE_FLAG_TRIANGLE_FRONT_COUNTERCLOCKWISE = 0x2,
    TF_ACCELERATION_STRUCTURE_INSTANCE_FLAG_FORCE_OPAQUE = 0x4,
    TF_ACCELERATION_STRUCTURE_INSTANCE_FLAG_FORCE_NON_OPAQUE = 0x8
} TFAccelerationStructureInstanceFlags;
MAKE_ENUM_FLAG(uint32_t, TFAccelerationStructureInstanceFlags)

typedef struct TFAccelerationStructureInstanceDesc
{
    TFAccelerationStructure*             pBottomAS;
    /// Row major affine transform for transforming the vertices in the geometry stored in pAccelerationStructure
    float                                mTransform[12];
    /// User defined instanced ID which can be queried in the shader
    uint32_t                             mInstanceID;
    uint32_t                             mInstanceMask;
    uint32_t                             mInstanceContributionToHitGroupIndex;
    TFAccelerationStructureInstanceFlags mFlags;
} TFAccelerationStructureInstanceDesc;

typedef struct TFAccelerationStructureGeometryDesc
{
    TFBuffer*                            pVertexBuffer;
    TFBuffer*                            pIndexBuffer;
    uint32_t                             mVertexOffset;
    uint32_t                             mVertexCount;
    uint32_t                             mVertexStride;
    TinyImageFormat                      mVertexFormat;
    uint32_t                             mIndexOffset;
    uint32_t                             mIndexCount;
    TFIndexType                          mIndexType;
    TFAccelerationStructureGeometryFlags mFlags;
} TFAccelerationStructureGeometryDesc;
/************************************************************************/
//	  Bottom Level Structures define the geometry data such as vertex buffers, index buffers
//	  Top Level Structures define the instance data for the geometry such as instance matrix, instance ID, ...
// #mDescCount - Number of geometries or instances in this structure
/************************************************************************/
typedef struct TFAccelerationStructureDescBottom
{
    /// Number of geometries / instances in thie acceleration structure
    uint32_t                             mDescCount;
    /// Array of geometries in the bottom level acceleration structure
    TFAccelerationStructureGeometryDesc* pGeometryDescs;
} TFAccelerationStructureDescBottom;

typedef struct TFAccelerationStructureDescTop
{
    uint32_t                             mDescCount;
    TFAccelerationStructureInstanceDesc* pInstanceDescs;
} TFAccelerationStructureDescTop;

typedef struct TFAccelerationStructureDesc
{
    TFAccelerationStructureType       mType;
    TFAccelerationStructureBuildFlags mFlags;
    union
    {
        TFAccelerationStructureDescBottom mBottom;
        TFAccelerationStructureDescTop    mTop;
    };
} TFAccelerationStructureDesc;

typedef struct TFRaytracingBuildASDesc
{
    TFAccelerationStructure* pAccelerationStructure;
    bool                     mIssueRWBarrier;
} TFRaytracingBuildASDesc;

#ifdef __cplusplus
extern "C"
{
#endif

bool initRaytracing(TFRenderer* pRenderer, TFRaytracing** ppRaytracing);
void exitRaytracing(TFRenderer* pRenderer, TFRaytracing* pRaytracing);

/// pScratchBufferSize - Holds the size of scratch buffer to be passed to cmdBuildAccelerationStructure
void addAccelerationStructure(TFRaytracing* pRaytracing, const TFAccelerationStructureDesc* pDesc,
                              TFAccelerationStructure** ppAccelerationStructure);
void removeAccelerationStructure(TFRaytracing* pRaytracing, TFAccelerationStructure* pAccelerationStructure);
/// Free the scratch memory allocated by acceleration structure after it has been built completely
/// Does not free acceleration structure
void removeAccelerationStructureScratch(TFRaytracing* pRaytracing, TFAccelerationStructure* pAccelerationStructure);

void cmdBuildAccelerationStructure(TFCmd* pCmd, TFRaytracing* pRaytracing, TFRaytracingBuildASDesc* pDesc);

#ifdef METAL
void addSSVGFDenoiser(TFRenderer* pRenderer, TFSSVGFDenoiser** ppDenoiser);
void removeSSVGFDenoiser(TFSSVGFDenoiser* pDenoiser);
void clearSSVGFDenoiserTemporalHistory(TFSSVGFDenoiser* pDenoiser);
void cmdSSVGFDenoise(TFCmd* pCmd, TFSSVGFDenoiser* pDenoiser, TFTexture* pSourceTexture, TFTexture* pMotionVectorTexture,
                     TFTexture* pDepthNormalTexture, TFTexture* pPreviousDepthNormalTexture, TFTexture** ppOut);
#endif

#ifdef __cplusplus
}
#endif
