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

#include "../../Resources/ResourceLoader/Interfaces/IResourceLoader.h"
#include "../../Graphics/Interfaces/IGraphics.h"
#include "../../Graphics/FSL/defaults.h"

#include "../Interfaces/IGizmo.h"
#include "./Shaders/FSL/Gizmo.srt.h"

#include "../../Utilities/Math/Algorithms.h"
#include "../../Utilities/ThirdParty/OpenSource/Nothings/stb_ds.h"

#define UNIFORM_BUFFER_ALIGNMENT    256
#define MAX_ELEMENTS_CACHE_RENDERER UINT16_MAX

// common for handles and bound
typedef enum GizmoElementsParams
{
    GIZMO_HANDLES_INSTANCES_COUNT = 17,
    GIZMO_BOUND_INSTANCES_COUNT = 9,
    GIZMO_RENDERER_INSTANCES_COUNT = 1,
    GIZMO_GRID_INSTANCES_COUNT = 2,
    GIZMO_MAX_INSTANCES_COUNT = GIZMO_HANDLES_INSTANCES_COUNT,

    CONE_EDGES_COUNT = 32,
    CIRCLE_EDGES_COUNT = 32,
    GRID_LINES_COUNT = 100,

    CONE_VERTICES_COUNT = CONE_EDGES_COUNT + 2,
    CONE_INDEXES_COUNT = CONE_EDGES_COUNT * 3 * 2,
    CYLINDER_VERTICES_COUNT = CIRCLE_EDGES_COUNT * 2,
    CYLINDER_INDEXES_COUNT = CIRCLE_EDGES_COUNT * 2 * 3,
    CUBE_VERTICES_COUNT = 8,
    CUBE_INDEXES_COUNT = 36,
    LINE_CUBE_VERTICES_COUNT = 8,
    LINE_CUBE_INDEXES_COUNT = 24,
    LINE_CONE_VERTICES_COUNT = CONE_EDGES_COUNT + 1,
    LINE_CONE_INDEXES_COUNT = CONE_EDGES_COUNT * 2 + 8,
    LINE_CYLINDER_VERTICES_COUNT = CIRCLE_EDGES_COUNT * 2,
    LINE_CYLINDER_INDEXES_COUNT = LINE_CYLINDER_VERTICES_COUNT * 2 + 8,
    LINE_SPHERE_VERTICES_COUNT = CIRCLE_EDGES_COUNT * 3,
    LINE_SPHERE_INDEXES_COUNT = LINE_SPHERE_VERTICES_COUNT * 2,
    LINE_HALF_SPHERE_VERTICES_COUNT = CIRCLE_EDGES_COUNT + 2,
    LINE_HALF_SPHERE_INDEXES_COUNT = LINE_HALF_SPHERE_VERTICES_COUNT * 2,

    LINE_VERTICES_COUNT = 2,
    CENTERED_RECT_VERTICES_COUNT = 6,
    RECT_VERTICES_COUNT = 6,
    LINE_RECT_VERTICES_COUNT = 5,
    LINE_CENTERED_RECT_VERTICES_COUNT = 5,
    CIRCLE_VERTICES_COUNT = CIRCLE_EDGES_COUNT + 1,
    GRID_VERTICES_COUNT = (GRID_LINES_COUNT + 1) * 4,
} GizmoElementsParams;

typedef enum GizmoDrawQueue
{
    GIZMO_DRAW_QUEUE_OPAQUE = 0,
    GIZMO_DRAW_QUEUE_TRANSPARENT = 1,
} GizmoDrawQueue;

// common for handles and bound, H - handles, B - bound
typedef enum GizmoInstances
{
    H_LINE_COUNT = 3,
    H_LINE_START = 0,
    H_LINE_Z_IDX = H_LINE_START,
    H_LINE_Y_IDX = H_LINE_START + 1,
    H_LINE_X_IDX = H_LINE_START + 2,

    H_CONE_COUNT = 3,
    H_CONE_START = 3,
    H_CONE_Z_IDX = H_CONE_START,
    H_CONE_Y_IDX = H_CONE_START + 1,
    H_CONE_X_IDX = H_CONE_START + 2,

    H_CENTER_BOX_COUNT = 1,
    H_CENTER_BOX_START = 9,
    H_CENTER_BOX_IDX = H_CENTER_BOX_START,

    H_RECT_COUNT = 3,
    H_RECT_START = 6,
    H_RECT_XY_IDX = H_RECT_START,
    H_RECT_XZ_IDX = H_RECT_START + 1,
    H_RECT_YZ_IDX = H_RECT_START + 2,

    H_LINE_RECT_COUNT = H_RECT_COUNT,
    H_LINE_RECT_START = H_RECT_START,
    H_LINE_RECT_XY_IDX = H_RECT_XY_IDX,
    H_LINE_RECT_XZ_IDX = H_RECT_XZ_IDX,
    H_LINE_RECT_YZ_IDX = H_RECT_YZ_IDX,

    H_SCALE_BOX_COUNT = H_CONE_COUNT,
    H_SCALE_BOX_START = H_CONE_START,
    H_SCALE_BOX_Z_IDX = H_CONE_Z_IDX,
    H_SCALE_BOX_Y_IDX = H_CONE_Y_IDX,
    H_SCALE_BOX_X_IDX = H_CONE_X_IDX,

    H_CYLINDER_COUNT = H_CONE_COUNT,
    H_CYLINDER_START = H_CONE_START,
    H_CYLINDER_Z_IDX = H_CONE_Z_IDX,
    H_CYLINDER_Y_IDX = H_CONE_Y_IDX,
    H_CYLINDER_X_IDX = H_CONE_X_IDX,

    H_CIRCLE_COUNT = 2,
    H_CIRCLE_START = 6,
    H_CIRCLE_FRONT_IDX = H_CIRCLE_START,
    H_CIRCLE_OUTSIDE_IDX = H_CIRCLE_START + 1,

    // Bound
    B_POINT_MIN_X_IDX = 0,
    B_POINT_MIN_Y_IDX = 1,
    B_POINT_MIN_Z_IDX = 2,
    B_POINT_MAX_X_IDX = 3,
    B_POINT_MAX_Y_IDX = 4,
    B_POINT_MAX_Z_IDX = 5,

    B_POINT_COUNT = 6,
    B_POINT_START = 0,

    B_BOX_POINT_COUNT = B_POINT_COUNT,
    B_RECT_POINT_COUNT = 4,
    B_CIRCLE_POINT_COUNT = 4,
    B_CONE_POINT_COUNT = B_POINT_COUNT,
    B_SPHERE_POINT_COUNT = B_POINT_COUNT,
    B_CYLINDER_POINT_COUNT = B_POINT_COUNT,
    B_CAPSULE_POINT_COUNT = B_POINT_COUNT,

    B_VOLUME_COUNT = 1,
    B_VOLUME_IDX = B_POINT_COUNT,

    B_CAPSULE_TIP_COUNT = 2,
    B_CAPSULE_TIP_UP_IDX = B_VOLUME_IDX + B_VOLUME_COUNT,
    B_CAPSULE_TIP_DOWN_IDX = B_CAPSULE_TIP_UP_IDX + 1,
} GizmoInstances;

typedef enum GizmoAxis
{
    GIZMO_AXIS_NONE = 0,
    GIZMO_AXIS_X = 1,
    GIZMO_AXIS_Y = 2,
    GIZMO_AXIS_Z = 4,
    GIZMO_AXIS_XY = GIZMO_AXIS_X | GIZMO_AXIS_Y,
    GIZMO_AXIS_XZ = GIZMO_AXIS_X | GIZMO_AXIS_Z,
    GIZMO_AXIS_YZ = GIZMO_AXIS_Y | GIZMO_AXIS_Z,
    GIZMO_AXIS_XYZ = GIZMO_AXIS_X | GIZMO_AXIS_Y | GIZMO_AXIS_Z,
    GIZMO_AXIS_GLOBAL = 8,
    GIZMO_AXIS_CAMERA = 16,
} GizmoAxis;

typedef enum GizmoContact
{
    GIZMO_CONTACT_NONE = 0,
    GIZMO_CONTACT_SEGMENT = 1,
    GIZMO_CONTACT_PLANE = 2,
} GizmoContact;

typedef struct GizmoUpdateData
{
    float4x4 mCameraLocalToWorld;
    float4x4 mCameraWorldtoLocal;

    float3 mGizmoPosition;
    float3 mCameraPosition;
    float3 mCameraForward;
    float3 mDirectionToGizmo;      // direction from camera to gizmo
    float3 mLocalDirectionToGizmo; // direction from camera to gizmo in gizmo space

    float3 mGizmoForward;
    float3 mGizmoUp;
    float3 mGizmoRight;
    float3 mRayOrigin;
    float3 mRayDirection;
    float3 mRayEnd;
    float3 mLocalRayOrigin;
    float3 mLocalRayDirection;

    float2 mMousePosition;

    float mCameraFovTangent;
    float mGizmoScale;
} GizmoUpdateData;

typedef struct GizmoContactData
{
    // global space
    float3 mOrigin;
    float3 mDirection;
    float3 mContactPosition;

    float2 mMousePosition;
    float  distanceToContact;
    float  mScaleWithoutDistance;
    float  mScaleWithDistance;

    uint32_t mContactType;
    uint32_t mAxisType;
} GizmoContactData;

typedef struct GizmoVisibility
{
    float mScaleForCamera;
    float mBasicScale;
    float mFovTangent;
    bool  mIsVisible;
} GizmoVisibility;

typedef struct GizmoInstanceData
{
    float4x4 mTRS;
    float4   mColor;
} GizmoInstanceData;

typedef struct GizmoVertexData
{
    union
    {
        struct
        {
            float4 mData;
        };
        struct
        {
            float3   mPos;
            uint32_t mInstanceIndex;
        };
    };

} GizmoVertexData;

typedef struct GizmoRendererCache
{
    GizmoInstanceData* pInstances;
    float4*            pVertexLines;
    float4*            pVertexTriangles;
    uint16_t*          pIndexTriangles;

    int32_t mInstanceActive;

    bool isUsedInstance;
} GizmoRendererCache;

typedef struct GizmoRendererBuffers
{
    TFBuffer* pGizmoLocalInstanceBuffer;
    TFBuffer* pGizmoVertexLineBuffer;
    TFBuffer* pGizmoVertexTriangleBuffer;
    TFBuffer* pGizmoIndexTriangleBuffer;
    uint32_t  mInstanceCount;
    uint32_t  mVertexLineCount;
    uint32_t  mVertexTriangleCount;
    uint32_t  mIndexTriangleCount;

    bool mIsObsolete;
    bool mRequiresCleaning;
} GizmoRendererBuffers;

typedef struct GizmoInnerData GizmoInnerData;

typedef void (*UpdateDataFunction)(TFGizmo* pGizmo, const GizmoUpdateCameraDesc* pCameraDesc, const GizmoUpdateData* pData);
typedef void (*FillDataFunction)(TFGizmo* pGizmo, const GizmoUpdateCameraDesc* pCameraDesc);
typedef void (*DrawFunction)(GizmoInnerData* pInnerData, TFCmd* pCmd, GizmoDrawQueue queue);
typedef size_t (*SerializeFunction)(GizmoInnerData* pInnerData, uint8_t* pBuffer, size_t size);
typedef size_t (*DeserializeFunction)(GizmoInnerData* pInnerData, const uint8_t* pBuffer, size_t size);

struct GizmoInnerData
{
    union
    {
        // handles
        struct
        {
            quat                    mStartRotation;
            float3                  mScaleOffset;
            float3                  mStartScale;
            TFGizmoHandlesState     mState;
            TFGizmoHandlesScaleMode mScaleMode;
            GizmoAxis               mFrontAxis;
        };

        // bound
        struct
        {
            uint32_t         mMaskDisabledPoints;
            TFGizmoBoundType mBoundType;
        };

        // renderer
        struct
        {
            TFDescriptorSet*      pDescriptorSetPerDraw;
            GizmoRendererBuffers* pBuffers;
            GizmoRendererCache    mCache;
        } mRenderer;
    };

    GizmoContactData mContact;

    UpdateDataFunction  pUpdateData;
    FillDataFunction    pFillData;
    DrawFunction        pDraw;
    SerializeFunction   pSerialize;
    DeserializeFunction pDeserialize;

    GizmoVisibility mVisibility;

    TFBuffer**         ppGizmoInstanceBuffer;
    GizmoInstanceData* pWriteInstance;
    TFDescriptorSet*   pDescriptorSetPerBatch;
    uint32_t           mCountInstance;
    uint32_t           mSizeWriteInstance;

    bool mHasContact;
};

typedef struct GizmoHandlesMeshes
{
    TFIndirectDrawArguments      mLine; // 0 - X, 1 - Y, 2 - Z
    TFIndirectDrawIndexArguments mCone; // 0 - X, 1 - Y, 2 - Z
    TFIndirectDrawIndexArguments mCenterBox;
    TFIndirectDrawArguments      mRect;     // 0 - XY, 1 - XZ, 2 - ZY
    TFIndirectDrawArguments      mLineRect; // 0 - XY, 1 - XZ, 2 - ZY
    TFIndirectDrawIndexArguments mScaleBox; // 0 - X, 1 - Y, 2 - Z
    TFIndirectDrawIndexArguments mCylinder;
    TFIndirectDrawArguments      mCircle;
} GizmoHandlesMeshes;

typedef struct GizmoBoundMeshes
{
    TFIndirectDrawIndexArguments mCube;
    TFIndirectDrawIndexArguments mSphere;
    TFIndirectDrawIndexArguments mHalfSphere;
    TFIndirectDrawIndexArguments mCone;
    TFIndirectDrawIndexArguments mCylinder;
    TFIndirectDrawArguments      mCircle;
    TFIndirectDrawArguments      mLineCenteredRect;
    TFIndirectDrawArguments      mPoints;
} GizmoBoundMeshes;

typedef struct GizmoCameraMatrixes
{
    float4x4 mCameraProjection;
    float4x4 mCameraWorldToLocal;
} GizmoCameraMatrixes;

typedef struct GizmoSystem
{
    GizmoHandlesMeshes      mGizmoHandlesMeshes;
    GizmoBoundMeshes        mGizmoBoundMeshes;
    TFIndirectDrawArguments mGizmoGridMesh;

    TFRenderer*      pRenderer;
    TFPipelineCache* pCache;

    uint32_t        mFrameMaxCount;
    const uint32_t* pFrameIdx;

    TFShader*   pGizmoShader;
    TFShader*   pGizmoFaceShader;
    TFShader*   pGizmoLocalInstanceShader;
    TFShader*   pGizmoGrid;
    TFPipeline* pGizmoOpaquePipeline;
    TFPipeline* pGizmoFaceOpaquePipeline;
    TFPipeline* pGizmoTransparentPipeline;
    TFPipeline* pGizmoStripLinesPipeline;
    TFPipeline* pGizmoListLinesPipeline;
    TFPipeline* pGizmoLocalInstancePipeline;
    TFPipeline* pGizmoLocalInstanceLinesPipeline;
    TFPipeline* pGizmoGridPipeline;

    TFBuffer* pGizmoVertexBuffer;
    TFBuffer* pGizmoIndexBuffer;
    TFBuffer* pInstanceNumberingBuffer;

    uint64_t* pGizmoSortingArray;
    TFGizmo** ppGizmoSortedTemp;
    TFGizmo** ppGizmo;
    TFGizmo*  pLockedGizmo;

    GizmoCameraMatrixes mMatrixes;
    TFDescriptorSet*    pDescriptorSetPerFrame;
    TFBuffer*           pUniformBuffer;

    GizmoInnerData* pGizmoRendererBegun;
} GizmoSystem;

static GizmoSystem* pGizmoSystem = NULL;

static bool gGizmoTransformIsRequired[TF_GIZMO_TYPE_ALL] = { true, true, false, false };

static uint32_t gBoundPointsCount[] = { B_BOX_POINT_COUNT,    B_RECT_POINT_COUNT,     B_CIRCLE_POINT_COUNT, B_CONE_POINT_COUNT,
                                        B_SPHERE_POINT_COUNT, B_CYLINDER_POINT_COUNT, B_CAPSULE_POINT_COUNT };

/****************************************************************************/
// MARK: - Private Static Load Gizmo Resources
/****************************************************************************/

static TFIndirectDrawIndexArguments generateCube(uint32_t maxVertices, uint32_t maxIndexes, float4* vertices, uint16_t* indexes,
                                                 uint32_t* pVerticesCount, uint32_t* pIndexesCount)
{
    ASSERT((maxVertices - *pVerticesCount) >= CUBE_VERTICES_COUNT);
    ASSERT((maxIndexes - *pIndexesCount) >= CUBE_INDEXES_COUNT);

    TFIndirectDrawIndexArguments result = { CUBE_INDEXES_COUNT, 0, *pIndexesCount, *pVerticesCount, 0 };
    vertices += result.mVertexOffset;
    indexes += result.mStartIndex;

    vertices[0] = f4Make(1, -1, 1, 1);
    vertices[1] = f4Make(1, -1, -1, 1);
    vertices[2] = f4Make(-1, -1, -1, 1);
    vertices[3] = f4Make(-1, -1, 1, 1);
    vertices[4] = f4Make(1, 1, 1, 1);
    vertices[5] = f4Make(1, 1, -1, 1);
    vertices[6] = f4Make(-1, 1, -1, 1);
    vertices[7] = f4Make(-1, 1, 1, 1);

    const uint16_t cubeIndexes[] = { 0, 1, 2, 2, 3, 0, 4, 6, 5, 6, 4, 7, 0, 3, 7, 7, 4, 0,
                                     1, 5, 6, 6, 2, 1, 0, 4, 5, 5, 1, 0, 3, 2, 6, 6, 7, 3 };

    for (uint32_t i = 0; i < CUBE_INDEXES_COUNT; i++)
    {
        indexes[i] = cubeIndexes[i];
    }

    *pVerticesCount += CUBE_VERTICES_COUNT;
    *pIndexesCount += CUBE_INDEXES_COUNT;
    return result;
}

static TFIndirectDrawIndexArguments generateLineCube(uint32_t maxVertices, uint32_t maxIndexes, float4* vertices, uint16_t* indexes,
                                                     uint32_t* pVerticesCount, uint32_t* pIndexesCount)
{
    ASSERT((maxVertices - *pVerticesCount) >= LINE_CUBE_VERTICES_COUNT);
    ASSERT((maxIndexes - *pIndexesCount) >= LINE_CUBE_INDEXES_COUNT);

    TFIndirectDrawIndexArguments result = { LINE_CUBE_INDEXES_COUNT, 0, *pIndexesCount, *pVerticesCount, 0 };
    vertices += result.mVertexOffset;
    indexes += result.mStartIndex;

    vertices[0] = f4Make(1, -1, 1, 1);
    vertices[1] = f4Make(1, -1, -1, 1);
    vertices[2] = f4Make(-1, -1, -1, 1);
    vertices[3] = f4Make(-1, -1, 1, 1);
    vertices[4] = f4Make(1, 1, 1, 1);
    vertices[5] = f4Make(1, 1, -1, 1);
    vertices[6] = f4Make(-1, 1, -1, 1);
    vertices[7] = f4Make(-1, 1, 1, 1);

    const uint16_t cubeIndexes[] = {
        0, 1, 1, 2, 2, 3, 3, 0, 4, 5, 5, 6, 6, 7, 7, 4, 0, 4, 1, 5, 2, 6, 3, 7,
    };

    for (uint32_t i = 0; i < LINE_CUBE_INDEXES_COUNT; i++)
    {
        indexes[i] = cubeIndexes[i];
    }

    *pVerticesCount += LINE_CUBE_VERTICES_COUNT;
    *pIndexesCount += LINE_CUBE_INDEXES_COUNT;
    return result;
}

static TFIndirectDrawIndexArguments generateLineCone(uint32_t maxVertices, uint32_t maxIndexes, float4* vertices, uint16_t* indexes,
                                                     uint32_t* pVerticesCount, uint32_t* pIndexesCount)
{
    ASSERT((maxVertices - *pVerticesCount) >= LINE_CONE_VERTICES_COUNT);
    ASSERT((maxIndexes - *pIndexesCount) >= LINE_CONE_INDEXES_COUNT);

    TFIndirectDrawIndexArguments result = { LINE_CONE_INDEXES_COUNT, 0, *pIndexesCount, *pVerticesCount, 0 };
    vertices += result.mVertexOffset;
    indexes += result.mStartIndex;

    vertices[0] = f4Make(0, -1, 0, 1);

    const float angleStep = 2.0f * PI / CIRCLE_EDGES_COUNT;
    for (uint16_t i = 0; i < CIRCLE_EDGES_COUNT; i++)
    {
        float  angle = i * angleStep;
        float2 pos = f2Make(cosf(angle), sinf(angle));
        vertices[i + 1] = f4Make(pos.x, 1, pos.y, 1);
    }

    indexes[0] = 0;
    indexes[1] = 1;
    indexes[2] = 0;
    indexes[3] = 1 + CIRCLE_EDGES_COUNT / 4;
    indexes[4] = 0;
    indexes[5] = 1 + CIRCLE_EDGES_COUNT / 2;
    indexes[6] = 0;
    indexes[7] = 1 + (3 * CIRCLE_EDGES_COUNT) / 4;

    for (uint16_t i = 0; i < CIRCLE_EDGES_COUNT; i++)
    {
        uint16_t start = i * 2 + 8;
        uint16_t end = i * 2 + 9;

        indexes[start] = i;
        if (i == CIRCLE_EDGES_COUNT - 1)
        {
            indexes[end] = 1;
        }
        else
        {
            indexes[end] = i + 1;
        }
    }

    *pVerticesCount += LINE_CONE_VERTICES_COUNT;
    *pIndexesCount += LINE_CONE_INDEXES_COUNT;
    return result;
}

static TFIndirectDrawIndexArguments generateLineCylinder(uint32_t maxVertices, uint32_t maxIndexes, float4* vertices, uint16_t* indexes,
                                                         uint32_t* pVerticesCount, uint32_t* pIndexesCount)
{
    ASSERT((maxVertices - *pVerticesCount) >= LINE_CYLINDER_VERTICES_COUNT);
    ASSERT((maxIndexes - *pIndexesCount) >= LINE_CYLINDER_INDEXES_COUNT);

    TFIndirectDrawIndexArguments result = { LINE_CYLINDER_INDEXES_COUNT, 0, *pIndexesCount, *pVerticesCount, 0 };
    vertices += result.mVertexOffset;
    indexes += result.mStartIndex;

    const float angleStep = 2.0f * PI / CIRCLE_EDGES_COUNT;
    for (uint16_t i = 0; i < CIRCLE_EDGES_COUNT; i++)
    {
        float  angle = i * angleStep;
        float2 pos = f2Make(cosf(angle), sinf(angle));
        vertices[i] = f4Make(pos.x, 1, pos.y, 1);
        vertices[i + CIRCLE_EDGES_COUNT] = f4Make(pos.x, -1, pos.y, 1);
    }

    for (uint16_t i = 0; i < 2; i++)
    {
        uint16_t circleOffset = i * CIRCLE_EDGES_COUNT;
        for (uint16_t i2 = 0; i2 < CIRCLE_EDGES_COUNT; i2++)
        {
            uint16_t start = (circleOffset + i2) * 2;
            uint16_t end = start + 1;
            indexes[start] = circleOffset + i2;
            if (i2 == CIRCLE_EDGES_COUNT - 1)
            {
                indexes[end] = circleOffset;
            }
            else
            {
                indexes[end] = circleOffset + i2 + 1;
            }
        }
    }

    uint16_t offset = CIRCLE_EDGES_COUNT * 4;
    indexes[offset] = 0;
    indexes[offset + 1] = CIRCLE_EDGES_COUNT;

    indexes[offset + 2] = CIRCLE_EDGES_COUNT / 4;
    indexes[offset + 3] = CIRCLE_EDGES_COUNT + indexes[offset + 2];

    indexes[offset + 4] = CIRCLE_EDGES_COUNT / 2;
    indexes[offset + 5] = CIRCLE_EDGES_COUNT + indexes[offset + 4];

    indexes[offset + 6] = (3 * CIRCLE_EDGES_COUNT) / 4;
    indexes[offset + 7] = CIRCLE_EDGES_COUNT + indexes[offset + 6];

    *pVerticesCount += LINE_CYLINDER_VERTICES_COUNT;
    *pIndexesCount += LINE_CYLINDER_INDEXES_COUNT;
    return result;
}

static TFIndirectDrawIndexArguments generateLineSphere(uint32_t maxVertices, uint32_t maxIndexes, float4* vertices, uint16_t* indexes,
                                                       uint32_t* pVerticesCount, uint32_t* pIndexesCount)
{
    ASSERT((maxVertices - *pVerticesCount) >= LINE_SPHERE_VERTICES_COUNT);
    ASSERT((maxIndexes - *pIndexesCount) >= LINE_SPHERE_INDEXES_COUNT);

    TFIndirectDrawIndexArguments result = { LINE_SPHERE_INDEXES_COUNT, 0, *pIndexesCount, *pVerticesCount, 0 };
    vertices += result.mVertexOffset;
    indexes += result.mStartIndex;

    const float angleStep = 2.0f * PI / CIRCLE_EDGES_COUNT;

    for (uint16_t i = 0; i < CIRCLE_EDGES_COUNT; i++)
    {
        float  angle = i * angleStep;
        float2 pos = f2Make(cosf(angle), sinf(angle));
        vertices[i] = f4Make(pos.x, pos.y, 0, 1);
        vertices[i + CIRCLE_EDGES_COUNT] = f4Make(pos.x, 0, pos.y, 1);
        vertices[i + CIRCLE_EDGES_COUNT * 2] = f4Make(0, pos.x, pos.y, 1);

        bool     end = i == CIRCLE_EDGES_COUNT - 1;
        uint16_t idx = i * 2;
        indexes[idx] = i;
        indexes[idx + 1] = end ? 0 : i + 1;

        idx = (i + CIRCLE_EDGES_COUNT) * 2;
        indexes[idx] = i + CIRCLE_EDGES_COUNT;
        indexes[idx + 1] = end ? CIRCLE_EDGES_COUNT : i + CIRCLE_EDGES_COUNT + 1;

        idx = (i + CIRCLE_EDGES_COUNT * 2) * 2;
        indexes[idx] = i + CIRCLE_EDGES_COUNT * 2;
        indexes[idx + 1] = end ? CIRCLE_EDGES_COUNT * 2 : i + CIRCLE_EDGES_COUNT * 2 + 1;
    }

    *pVerticesCount += LINE_SPHERE_VERTICES_COUNT;
    *pIndexesCount += LINE_SPHERE_INDEXES_COUNT;
    return result;
}

static TFIndirectDrawIndexArguments generateLineHalfSphere(uint32_t maxVertices, uint32_t maxIndexes, float4* vertices, uint16_t* indexes,
                                                           uint32_t* pVerticesCount, uint32_t* pIndexesCount)
{
    ASSERT((maxVertices - *pVerticesCount) >= LINE_HALF_SPHERE_VERTICES_COUNT);
    ASSERT((maxIndexes - *pIndexesCount) >= LINE_HALF_SPHERE_INDEXES_COUNT);

    TFIndirectDrawIndexArguments result = { LINE_HALF_SPHERE_INDEXES_COUNT, 0, *pIndexesCount, *pVerticesCount, 0 };
    vertices += result.mVertexOffset;
    indexes += result.mStartIndex;

    const uint16_t halfCircl = CIRCLE_EDGES_COUNT / 2 + 1;
    const float    angleStep = 2.0f * PI / CIRCLE_EDGES_COUNT;

    for (uint16_t i = 0; i < halfCircl; i++)
    {
        float  angle = i * angleStep;
        float2 pos = f2Make(cosf(angle), sinf(angle));
        vertices[i] = f4Make(pos.x, pos.y, 0, 1);

        angle += PI / 2;
        pos = f2Make(cosf(angle), sinf(angle));
        vertices[i + halfCircl] = f4Make(0, -pos.x, pos.y, 1);

        bool end = i == halfCircl - 1;
        if (!end)
        {
            uint16_t idx = i * 2;
            indexes[idx] = i;
            indexes[idx + 1] = i + 1;

            idx = (i + halfCircl) * 2;
            indexes[idx] = i + halfCircl;
            indexes[idx + 1] = i + halfCircl + 1;
        }
    }

    *pVerticesCount += LINE_HALF_SPHERE_VERTICES_COUNT;
    *pIndexesCount += LINE_HALF_SPHERE_INDEXES_COUNT;
    return result;
}

static TFIndirectDrawIndexArguments generateCone(uint32_t maxVertices, uint32_t maxIndexes, float4* vertices, uint16_t* indexes,
                                                 uint32_t* pVerticesCount, uint32_t* pIndexesCount)
{
    ASSERT((maxVertices - *pVerticesCount) >= CONE_VERTICES_COUNT);
    ASSERT((maxIndexes - *pIndexesCount) >= CONE_INDEXES_COUNT);

    TFIndirectDrawIndexArguments result = { CONE_INDEXES_COUNT, 0, *pIndexesCount, *pVerticesCount, 0 };
    vertices += result.mVertexOffset;
    indexes += result.mStartIndex;

    vertices[0] = f4Make(0, 0, 0, 1); // bottom
    vertices[1] = f4Make(0, 1, 0, 1); // top

    const uint16_t offsetVertex = 2;
    const float    angleStep = 2.0f * PI / CONE_EDGES_COUNT;
    for (uint16_t i = 0; i < CONE_EDGES_COUNT; i++)
    {
        float angle = i * angleStep;
        vertices[i + offsetVertex] = f4Make(cosf(angle), 0, sinf(angle), 1);
    }

    uint16_t offsetIndex = 0;
    for (uint16_t i = 0; i < CONE_EDGES_COUNT; i++)
    {
        uint16_t next = offsetVertex + i + 1;
        if (i == CONE_EDGES_COUNT - 1)
            next = offsetVertex;

        indexes[offsetIndex] = 0;
        indexes[offsetIndex + 1] = offsetVertex + i;
        indexes[offsetIndex + 2] = next;

        indexes[offsetIndex + 3] = next;
        indexes[offsetIndex + 4] = offsetVertex + i;
        indexes[offsetIndex + 5] = 1;
        offsetIndex += 6;
    }

    *pVerticesCount += CONE_VERTICES_COUNT;
    *pIndexesCount += CONE_INDEXES_COUNT;
    return result;
}

static TFIndirectDrawIndexArguments generateCylinder(uint32_t maxVertices, uint32_t maxIndexes, float4* vertices, uint16_t* indexes,
                                                     uint32_t* pVerticesCount, uint32_t* pIndexesCount)
{
    ASSERT((maxVertices - *pVerticesCount) >= CYLINDER_VERTICES_COUNT);
    ASSERT((maxIndexes - *pIndexesCount) >= CYLINDER_INDEXES_COUNT);

    TFIndirectDrawIndexArguments result = { CYLINDER_INDEXES_COUNT, 0, *pIndexesCount, *pVerticesCount, 0 };
    vertices += result.mVertexOffset;
    indexes += result.mStartIndex;

    const float angleStep = 2.0f * PI / CIRCLE_EDGES_COUNT;
    for (int i = 0; i < CIRCLE_EDGES_COUNT; i++)
    {
        float  angle = i * angleStep;
        float2 pos = f2Make(cosf(angle), sinf(angle));
        vertices[i * 2] = f4Make(pos.x, -1, pos.y, 1);
        vertices[i * 2 + 1] = f4Make(pos.x, 1, pos.y, 1);
    }

    uint16_t offsetIndex = 0;
    for (uint16_t i = 0; i < CIRCLE_EDGES_COUNT; i++)
    {
        uint16_t nextBottom = (i + 1) * 2;
        uint16_t nextTop = nextBottom + 1;
        if (i == CIRCLE_EDGES_COUNT - 1)
        {
            nextBottom = 0;
            nextTop = 1;
        }

        indexes[offsetIndex] = nextTop;
        indexes[offsetIndex + 1] = i * 2 + 1;
        indexes[offsetIndex + 2] = i * 2;

        indexes[offsetIndex + 3] = nextBottom;
        indexes[offsetIndex + 4] = nextTop;
        indexes[offsetIndex + 5] = i * 2;
        offsetIndex += 6;
    }

    *pVerticesCount += CYLINDER_VERTICES_COUNT;
    *pIndexesCount += CYLINDER_INDEXES_COUNT;

    return result;
}

static TFIndirectDrawArguments generateCircle(uint32_t maxVertices, float4* vertices, uint32_t* pVerticesCount)
{
    ASSERT((maxVertices - *pVerticesCount) >= CIRCLE_VERTICES_COUNT);

    TFIndirectDrawArguments result = { CIRCLE_VERTICES_COUNT, 0, *pVerticesCount, 0 };
    vertices += result.mStartVertex;

    const float angleStep = 2.0f * PI / CIRCLE_EDGES_COUNT;

    for (int i = 0; i <= CIRCLE_EDGES_COUNT; i++)
    {
        float angle = i * angleStep;
        vertices[i] = f4Make(cosf(angle), 0, sinf(angle), 1);
    }
    *pVerticesCount += CIRCLE_VERTICES_COUNT;
    return result;
}

static TFIndirectDrawArguments generateLine(uint32_t maxVertices, float4* vertices, uint32_t* pVerticesCount)
{
    ASSERT((maxVertices - *pVerticesCount) >= LINE_VERTICES_COUNT);

    TFIndirectDrawArguments result = { LINE_VERTICES_COUNT, 0, *pVerticesCount, 0 };
    vertices += result.mStartVertex;

    vertices[0] = f4Make(0, 0, 0, 1);
    vertices[1] = f4Make(0, 0, 1, 1);
    *pVerticesCount += LINE_VERTICES_COUNT;
    return result;
}

static TFIndirectDrawArguments generateCenteredRect(uint32_t maxVertices, float4* vertices, uint32_t* pVerticesCount)
{
    ASSERT((maxVertices - *pVerticesCount) >= CENTERED_RECT_VERTICES_COUNT);

    TFIndirectDrawArguments result = { CENTERED_RECT_VERTICES_COUNT, 0, *pVerticesCount, 0 };
    vertices += result.mStartVertex;

    vertices[0] = f4Make(-1, -1, 0, 1);
    vertices[1] = f4Make(1, -1, 0, 1);
    vertices[2] = f4Make(1, 1, 0, 1);
    vertices[3] = f4Make(-1, -1, 0, 1);
    vertices[4] = f4Make(1, 1, 0, 1);
    vertices[5] = f4Make(-1, 1, 0, 1);
    *pVerticesCount += CENTERED_RECT_VERTICES_COUNT;
    return result;
}

static TFIndirectDrawArguments generateRect(uint32_t maxVertices, float4* vertices, uint32_t* pVerticesCount)
{
    ASSERT((maxVertices - *pVerticesCount) >= RECT_VERTICES_COUNT);

    TFIndirectDrawArguments result = { RECT_VERTICES_COUNT, 0, *pVerticesCount, 0 };
    vertices += result.mStartVertex;

    vertices[0] = f4Make(0, 0, 0, 1);
    vertices[1] = f4Make(0, 1, 0, 1);
    vertices[2] = f4Make(0, 1, 1, 1);
    vertices[3] = f4Make(0, 0, 0, 1);
    vertices[4] = f4Make(0, 1, 1, 1);
    vertices[5] = f4Make(0, 0, 1, 1);
    *pVerticesCount += RECT_VERTICES_COUNT;
    return result;
}

static TFIndirectDrawArguments generateLineCenteredRect(uint32_t maxVertices, float4* vertices, uint32_t* pVerticesCount)
{
    ASSERT((maxVertices - *pVerticesCount) >= LINE_CENTERED_RECT_VERTICES_COUNT);

    TFIndirectDrawArguments result = { LINE_CENTERED_RECT_VERTICES_COUNT, 0, *pVerticesCount, 0 };
    vertices += result.mStartVertex;

    vertices[0] = f4Make(-1, 0, -1, 1);
    vertices[1] = f4Make(1, 0, -1, 1);
    vertices[2] = f4Make(1, 0, 1, 1);
    vertices[3] = f4Make(-1, 0, 1, 1);
    vertices[4] = f4Make(-1, 0, -1, 1);

    *pVerticesCount += LINE_CENTERED_RECT_VERTICES_COUNT;
    return result;
}

static TFIndirectDrawArguments generateLineRect(uint32_t maxVertices, float4* vertices, uint32_t* pVerticesCount)
{
    ASSERT((maxVertices - *pVerticesCount) >= LINE_RECT_VERTICES_COUNT);

    TFIndirectDrawArguments result = { LINE_RECT_VERTICES_COUNT, 0, *pVerticesCount, 0 };
    vertices += result.mStartVertex;

    vertices[0] = f4Make(0, 0, 0, 1);
    vertices[1] = f4Make(0, 1, 0, 1);
    vertices[2] = f4Make(0, 1, 1, 1);
    vertices[3] = f4Make(0, 0, 1, 1);
    vertices[4] = f4Make(0, 0, 0, 1);
    *pVerticesCount += LINE_RECT_VERTICES_COUNT;
    return result;
}

static TFIndirectDrawArguments generateGrid(uint32_t maxVertices, float4* vertices, uint32_t* pVerticesCount)
{
    ASSERT((maxVertices - *pVerticesCount) >= GRID_VERTICES_COUNT);

    TFIndirectDrawArguments result = { GRID_VERTICES_COUNT, 0, *pVerticesCount, 0 };
    vertices += result.mStartVertex;

    uint32_t idx = 0;
    float    halfSize = 0.5f;
    for (uint32_t i = 0; i <= GRID_LINES_COUNT; i++)
    {
        float offset = i / (float)GRID_LINES_COUNT - halfSize;

        vertices[idx] = f4Make(-halfSize * 2, 0, offset * 2, 1);
        vertices[idx + 1] = f4Make(halfSize * 2, 0, offset * 2, 1);
        vertices[idx + 2] = f4Make(offset * 2, 0, -halfSize * 2, 1);
        vertices[idx + 3] = f4Make(offset * 2, 0, halfSize * 2, 1);
        idx += 4;
    }
    *pVerticesCount += GRID_VERTICES_COUNT;
    return result;
}

static void addResources()
{
    uint32_t maxVertices =
        (CIRCLE_VERTICES_COUNT + CONE_VERTICES_COUNT + CUBE_VERTICES_COUNT + CYLINDER_VERTICES_COUNT + LINE_RECT_VERTICES_COUNT +
         LINE_VERTICES_COUNT + RECT_VERTICES_COUNT + LINE_CUBE_VERTICES_COUNT + LINE_CENTERED_RECT_VERTICES_COUNT +
         CENTERED_RECT_VERTICES_COUNT + LINE_CONE_VERTICES_COUNT + LINE_CYLINDER_VERTICES_COUNT + LINE_SPHERE_VERTICES_COUNT +
         LINE_HALF_SPHERE_VERTICES_COUNT + GRID_VERTICES_COUNT);

    uint32_t maxIndexes =
        (CONE_INDEXES_COUNT + CYLINDER_INDEXES_COUNT + CUBE_INDEXES_COUNT + LINE_CUBE_INDEXES_COUNT + LINE_CONE_INDEXES_COUNT +
         LINE_CYLINDER_INDEXES_COUNT + LINE_SPHERE_INDEXES_COUNT + LINE_HALF_SPHERE_INDEXES_COUNT);

    uint32_t bufferVertexSize = maxVertices * sizeof(float4);
    uint32_t bufferIndexSize = maxIndexes * sizeof(uint16_t);

    float4*   bufferVertexData = (float4*)tf_calloc(1, bufferVertexSize);
    uint16_t* bufferIndexData = (uint16_t*)tf_calloc(1, bufferIndexSize);

    uint32_t offsetVertexBuffer = 0;
    uint32_t offsetIndexBuffer = 0;

    TFIndirectDrawIndexArguments cone =
        generateCone(maxVertices, maxIndexes, bufferVertexData, bufferIndexData, &offsetVertexBuffer, &offsetIndexBuffer);

    TFIndirectDrawIndexArguments cube =
        generateCube(maxVertices, maxIndexes, bufferVertexData, bufferIndexData, &offsetVertexBuffer, &offsetIndexBuffer);

    TFIndirectDrawIndexArguments cylinder =
        generateCylinder(maxVertices, maxIndexes, bufferVertexData, bufferIndexData, &offsetVertexBuffer, &offsetIndexBuffer);

    TFIndirectDrawArguments rect = generateRect(maxVertices, bufferVertexData, &offsetVertexBuffer);
    TFIndirectDrawArguments line = generateLine(maxVertices, bufferVertexData, &offsetVertexBuffer);
    TFIndirectDrawArguments lineRect = generateLineRect(maxVertices, bufferVertexData, &offsetVertexBuffer);
    TFIndirectDrawArguments lineCircle = generateCircle(maxVertices, bufferVertexData, &offsetVertexBuffer);

    TFIndirectDrawArguments lineCenteredRect = generateLineCenteredRect(maxVertices, bufferVertexData, &offsetVertexBuffer);
    TFIndirectDrawArguments centeredRect = generateCenteredRect(maxVertices, bufferVertexData, &offsetVertexBuffer);
    TFIndirectDrawArguments grid = generateGrid(maxVertices, bufferVertexData, &offsetVertexBuffer);

    TFIndirectDrawIndexArguments lineCube =
        generateLineCube(maxVertices, maxIndexes, bufferVertexData, bufferIndexData, &offsetVertexBuffer, &offsetIndexBuffer);

    TFIndirectDrawIndexArguments lineCone =
        generateLineCone(maxVertices, maxIndexes, bufferVertexData, bufferIndexData, &offsetVertexBuffer, &offsetIndexBuffer);

    TFIndirectDrawIndexArguments lineCylinder =
        generateLineCylinder(maxVertices, maxIndexes, bufferVertexData, bufferIndexData, &offsetVertexBuffer, &offsetIndexBuffer);

    TFIndirectDrawIndexArguments lineSphere =
        generateLineSphere(maxVertices, maxIndexes, bufferVertexData, bufferIndexData, &offsetVertexBuffer, &offsetIndexBuffer);

    TFIndirectDrawIndexArguments lineHalfSphere =
        generateLineHalfSphere(maxVertices, maxIndexes, bufferVertexData, bufferIndexData, &offsetVertexBuffer, &offsetIndexBuffer);

    line.mInstanceCount = H_LINE_COUNT;
    line.mStartInstance = H_LINE_START;
    pGizmoSystem->mGizmoHandlesMeshes.mLine = line;

    cone.mInstanceCount = H_CONE_COUNT;
    cone.mStartInstance = H_CONE_START;
    pGizmoSystem->mGizmoHandlesMeshes.mCone = cone;

    cube.mInstanceCount = H_CENTER_BOX_COUNT;
    cube.mStartInstance = H_CENTER_BOX_START;
    pGizmoSystem->mGizmoHandlesMeshes.mCenterBox = cube;

    rect.mInstanceCount = H_RECT_COUNT;
    rect.mStartInstance = H_RECT_START;
    pGizmoSystem->mGizmoHandlesMeshes.mRect = rect;

    lineRect.mInstanceCount = H_LINE_RECT_COUNT;
    lineRect.mStartInstance = H_LINE_RECT_START;
    pGizmoSystem->mGizmoHandlesMeshes.mLineRect = lineRect;

    cube.mInstanceCount = H_SCALE_BOX_COUNT;
    cube.mStartInstance = H_SCALE_BOX_START;
    pGizmoSystem->mGizmoHandlesMeshes.mScaleBox = cube;

    cylinder.mInstanceCount = H_CYLINDER_COUNT;
    cylinder.mStartInstance = H_CYLINDER_START;
    pGizmoSystem->mGizmoHandlesMeshes.mCylinder = cylinder;

    lineCircle.mInstanceCount = H_CIRCLE_COUNT;
    lineCircle.mStartInstance = H_CIRCLE_START;
    pGizmoSystem->mGizmoHandlesMeshes.mCircle = lineCircle;

    lineCube.mInstanceCount = B_VOLUME_COUNT;
    lineCube.mStartInstance = B_VOLUME_IDX;
    pGizmoSystem->mGizmoBoundMeshes.mCube = lineCube;

    lineCone.mInstanceCount = B_VOLUME_COUNT;
    lineCone.mStartInstance = B_VOLUME_IDX;
    pGizmoSystem->mGizmoBoundMeshes.mCone = lineCone;

    lineCylinder.mInstanceCount = B_VOLUME_COUNT;
    lineCylinder.mStartInstance = B_VOLUME_IDX;
    pGizmoSystem->mGizmoBoundMeshes.mCylinder = lineCylinder;

    lineSphere.mInstanceCount = B_VOLUME_COUNT;
    lineSphere.mStartInstance = B_VOLUME_IDX;
    pGizmoSystem->mGizmoBoundMeshes.mSphere = lineSphere;

    lineHalfSphere.mInstanceCount = B_CAPSULE_TIP_COUNT;
    lineHalfSphere.mStartInstance = B_CAPSULE_TIP_UP_IDX;
    pGizmoSystem->mGizmoBoundMeshes.mHalfSphere = lineHalfSphere;

    lineCenteredRect.mInstanceCount = B_VOLUME_COUNT;
    lineCenteredRect.mStartInstance = B_VOLUME_IDX;
    pGizmoSystem->mGizmoBoundMeshes.mLineCenteredRect = lineCenteredRect;

    lineCircle.mInstanceCount = B_VOLUME_COUNT;
    lineCircle.mStartInstance = B_VOLUME_IDX;
    pGizmoSystem->mGizmoBoundMeshes.mCircle = lineCircle;

    centeredRect.mInstanceCount = B_POINT_COUNT;
    centeredRect.mStartInstance = B_POINT_START;
    pGizmoSystem->mGizmoBoundMeshes.mPoints = centeredRect;

    grid.mInstanceCount = GIZMO_GRID_INSTANCES_COUNT;
    grid.mStartInstance = 0;
    pGizmoSystem->mGizmoGridMesh = grid;

    TFBufferLoadDesc gizmoVbDesc = { 0 };
    gizmoVbDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_VERTEX_BUFFER;
    gizmoVbDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_GPU_ONLY;
    gizmoVbDesc.mDesc.mSize = bufferVertexSize;
    gizmoVbDesc.mDesc.pName = "GizmoVertexBuffer";
    gizmoVbDesc.pData = bufferVertexData;
    gizmoVbDesc.ppBuffer = &pGizmoSystem->pGizmoVertexBuffer;
    addResource_Buffer(&gizmoVbDesc, NULL);

    TFBufferLoadDesc gizmoIbDesc = { 0 };
    gizmoIbDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_INDEX_BUFFER;
    gizmoIbDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_GPU_ONLY;
    gizmoIbDesc.mDesc.mSize = bufferIndexSize;
    gizmoIbDesc.mDesc.pName = "GizmoIndexBuffer";
    gizmoIbDesc.pData = bufferIndexData;
    gizmoIbDesc.ppBuffer = &pGizmoSystem->pGizmoIndexBuffer;
    addResource_Buffer(&gizmoIbDesc, NULL);

    TFBufferLoadDesc ubDesc = { 0 };
    ubDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    ubDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
    ubDesc.mDesc.mFlags = TF_BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
    ubDesc.pData = NULL;
    ubDesc.mDesc.pName = "GizmoUniformBuffer";
    ubDesc.mDesc.mSize = UNIFORM_BUFFER_ALIGNMENT * pGizmoSystem->mFrameMaxCount;
    ubDesc.ppBuffer = &pGizmoSystem->pUniformBuffer;
    addResource_Buffer(&ubDesc, NULL);

    TFBufferLoadDesc inbDesc = { 0 };
    inbDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    inbDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
    inbDesc.mDesc.mFlags = TF_BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
    inbDesc.mDesc.mSize = UNIFORM_BUFFER_ALIGNMENT * GIZMO_MAX_INSTANCES_COUNT * pGizmoSystem->mFrameMaxCount;
    inbDesc.pData = NULL;
    inbDesc.mDesc.pName = "GizmoInstanceNumberingBuffer";
    inbDesc.ppBuffer = &pGizmoSystem->pInstanceNumberingBuffer;
    addResource_Buffer(&inbDesc, NULL);

    TFDescriptorSetDesc descPerFrame = SRT_SET_DESC(GizmoSrt, PerFrame, pGizmoSystem->mFrameMaxCount, 0);
    addDescriptorSet(pGizmoSystem->pRenderer, &descPerFrame, &pGizmoSystem->pDescriptorSetPerFrame);

    TFDescriptorDataRange range = { 0 };
    range.mStructStride = sizeof(float4x4) * 2;
    range.mSize = range.mStructStride;

    TFDescriptorData param = { 0 };
    param.ppBuffers = &pGizmoSystem->pUniformBuffer;
    param.pRanges = &range;
    for (uint32_t i = 0; i < pGizmoSystem->mFrameMaxCount; i++)
    {
        range.mOffset = UNIFORM_BUFFER_ALIGNMENT * i;
        updateDescriptorSet(pGizmoSystem->pRenderer, i, pGizmoSystem->pDescriptorSetPerFrame, 1, &param);
    }

    waitForAllResourceLoads();

    tf_free(bufferVertexData);
    tf_free(bufferIndexData);

    // fill in GizmoInstanceNumberingBuffer
    TFBufferUpdateDesc instanceNumberingBuffer = { pGizmoSystem->pInstanceNumberingBuffer, 0,
                                                   pGizmoSystem->pInstanceNumberingBuffer->mSize };
    beginUpdateResource_Buffer(&instanceNumberingBuffer);
    uint8_t* mem = (uint8_t*)instanceNumberingBuffer.pMappedData;
    for (uint32_t i = 0; i < GIZMO_MAX_INSTANCES_COUNT; i++)
    {
        uint32_t offset = i * UNIFORM_BUFFER_ALIGNMENT;
        memcpy(mem + offset, &i, sizeof(uint32_t));
    }
    endUpdateResource_Buffer(&instanceNumberingBuffer);
}

static void removeResources()
{
    removeResource_Buffer(pGizmoSystem->pGizmoVertexBuffer);
    removeResource_Buffer(pGizmoSystem->pGizmoIndexBuffer);
    removeDescriptorSet(pGizmoSystem->pRenderer, pGizmoSystem->pDescriptorSetPerFrame);
    removeResource_Buffer(pGizmoSystem->pUniformBuffer);
    removeResource_Buffer(pGizmoSystem->pInstanceNumberingBuffer);
}

static void addShaders()
{
    TFShaderLoadDesc gizmoShader{};
    gizmoShader.mVert.pFileName = "gizmo.vert";
    gizmoShader.mFrag.pFileName = "gizmo.frag";

    TFShaderLoadDesc gizmoFaceShader{};
    gizmoFaceShader.mVert.pFileName = "gizmoFace.vert";
    gizmoFaceShader.mFrag.pFileName = "gizmo.frag";

    TFShaderLoadDesc gizmoLocalInstanceShader{};
    gizmoLocalInstanceShader.mVert.pFileName = "gizmoLocalInstance.vert";
    gizmoLocalInstanceShader.mFrag.pFileName = "gizmo.frag";

    TFShaderLoadDesc gizmoGridShader{};
    gizmoGridShader.mVert.pFileName = "gizmoGrid.vert";
    gizmoGridShader.mFrag.pFileName = "gizmoGrid.frag";

    addShader(pGizmoSystem->pRenderer, &gizmoShader, &pGizmoSystem->pGizmoShader);
    addShader(pGizmoSystem->pRenderer, &gizmoFaceShader, &pGizmoSystem->pGizmoFaceShader);
    addShader(pGizmoSystem->pRenderer, &gizmoLocalInstanceShader, &pGizmoSystem->pGizmoLocalInstanceShader);
    addShader(pGizmoSystem->pRenderer, &gizmoGridShader, &pGizmoSystem->pGizmoGrid);
}

static void removeShaders()
{
    removeShader(pGizmoSystem->pRenderer, pGizmoSystem->pGizmoShader);
    removeShader(pGizmoSystem->pRenderer, pGizmoSystem->pGizmoFaceShader);
    removeShader(pGizmoSystem->pRenderer, pGizmoSystem->pGizmoLocalInstanceShader);
    removeShader(pGizmoSystem->pRenderer, pGizmoSystem->pGizmoGrid);
}

static void addPipelines(const GizmoSystemLoadDesc* pGizmoSystemLoadDesc)
{
    TFRasterizerStateDesc rasterizerStateDesc;
    memset(&rasterizerStateDesc, 0, sizeof(TFRasterizerStateDesc));

    TFDepthStateDesc depthStateDesc{};
    depthStateDesc.mDepthTest = true;
    depthStateDesc.mDepthWrite = true;
    depthStateDesc.mDepthFunc = TF_CMP_GEQUAL;

    TinyImageFormat mainColorFormat = pGizmoSystemLoadDesc->mColorFormat;

    TFPipelineDesc desc{};
    PIPELINE_LAYOUT_DESC(desc, NULL, SRT_LAYOUT_DESC(GizmoSrt, PerFrame), SRT_LAYOUT_DESC(GizmoSrt, PerBatch),
                         SRT_LAYOUT_DESC(GizmoSrt, PerDraw));
    desc.mType = TF_PIPELINE_TYPE_GRAPHICS;
    desc.mGraphicsDesc.mPrimitiveTopo = TF_PRIMITIVE_TOPO_TRI_LIST;
    desc.mGraphicsDesc.mRenderTargetCount = 1;
    desc.mGraphicsDesc.pDepthState = &depthStateDesc;
    desc.mGraphicsDesc.pColorFormats = &mainColorFormat;
    desc.mGraphicsDesc.mSampleCount = pGizmoSystemLoadDesc->mSampleCount;
    desc.mGraphicsDesc.mSampleQuality = pGizmoSystemLoadDesc->mSampleQuality;
    desc.mGraphicsDesc.mDepthStencilFormat = pGizmoSystemLoadDesc->mDepthsFormat;
    desc.mGraphicsDesc.mVRFoveatedRendering = true;
    desc.mGraphicsDesc.pRasterizerState = &rasterizerStateDesc;

    TFVertexLayout vertexLayout{};
    vertexLayout.mBindingCount = 1;
    vertexLayout.mBindings[0].mStride = sizeof(float4);
    vertexLayout.mBindings[0].mRate = TF_VERTEX_BINDING_RATE_VERTEX;
    vertexLayout.mBindings[1].mStride = sizeof(float4x4) + sizeof(float4);
    vertexLayout.mBindings[1].mRate = TF_VERTEX_BINDING_RATE_INSTANCE;
    vertexLayout.mAttribCount = 1;
    vertexLayout.mAttribs[0].mSemantic = TF_SEMANTIC_POSITION;
    vertexLayout.mAttribs[0].mFormat = TinyImageFormat_R32G32B32A32_SFLOAT;
    vertexLayout.mAttribs[0].mBinding = 0;
    vertexLayout.mAttribs[0].mLocation = 0;
    vertexLayout.mAttribs[0].mOffset = 0;

    desc.mGraphicsDesc.pVertexLayout = &vertexLayout;

    //
    desc.mGraphicsDesc.pShaderProgram = pGizmoSystem->pGizmoFaceShader;
    addPipeline(pGizmoSystem->pRenderer, &desc, &pGizmoSystem->pGizmoFaceOpaquePipeline);

    //
    desc.mGraphicsDesc.pShaderProgram = pGizmoSystem->pGizmoLocalInstanceShader;
    rasterizerStateDesc.mCullMode = TF_CULL_MODE_BACK;
    addPipeline(pGizmoSystem->pRenderer, &desc, &pGizmoSystem->pGizmoLocalInstancePipeline);

    desc.mGraphicsDesc.mPrimitiveTopo = TF_PRIMITIVE_TOPO_LINE_LIST;
    addPipeline(pGizmoSystem->pRenderer, &desc, &pGizmoSystem->pGizmoLocalInstanceLinesPipeline);

    //
    desc.mGraphicsDesc.pShaderProgram = pGizmoSystem->pGizmoShader;
    desc.mGraphicsDesc.mPrimitiveTopo = TF_PRIMITIVE_TOPO_TRI_LIST;
    addPipeline(pGizmoSystem->pRenderer, &desc, &pGizmoSystem->pGizmoOpaquePipeline);

    desc.mGraphicsDesc.mPrimitiveTopo = TF_PRIMITIVE_TOPO_LINE_STRIP;
    addPipeline(pGizmoSystem->pRenderer, &desc, &pGizmoSystem->pGizmoStripLinesPipeline);

    desc.mGraphicsDesc.mPrimitiveTopo = TF_PRIMITIVE_TOPO_LINE_LIST;
    addPipeline(pGizmoSystem->pRenderer, &desc, &pGizmoSystem->pGizmoListLinesPipeline);

    TFBlendStateDesc blend;
    memset(&blend, 0, sizeof(TFBlendStateDesc));
    blend.mBlendModes[0] = TF_BM_ADD;
    blend.mBlendAlphaModes[0] = TF_BM_ADD;
    blend.mColorWriteMasks[0] = TF_COLOR_MASK_ALL;
    blend.mAlphaToCoverage = false;
    blend.mIndependentBlend = false;
    blend.mSrcFactors[0] = TF_BC_SRC_ALPHA;
    blend.mSrcAlphaFactors[0] = TF_BC_SRC_ALPHA;
    blend.mDstFactors[0] = TF_BC_ONE_MINUS_SRC_ALPHA;
    blend.mDstAlphaFactors[0] = TF_BC_ONE_MINUS_SRC_ALPHA;
    blend.mRenderTargetMask = TF_BLEND_STATE_TARGET_0;

    desc.mGraphicsDesc.pBlendState = &blend;
    desc.mGraphicsDesc.pDepthState = NULL;
    desc.mGraphicsDesc.mPrimitiveTopo = TF_PRIMITIVE_TOPO_TRI_LIST;
    rasterizerStateDesc.mCullMode = TF_CULL_MODE_NONE;
    addPipeline(pGizmoSystem->pRenderer, &desc, &pGizmoSystem->pGizmoTransparentPipeline);

    desc.mGraphicsDesc.mPrimitiveTopo = TF_PRIMITIVE_TOPO_LINE_LIST;
    depthStateDesc.mDepthWrite = false;
    depthStateDesc.mDepthFunc = TFCompareMode::TF_CMP_GEQUAL;
    desc.mGraphicsDesc.pDepthState = &depthStateDesc;
    desc.mGraphicsDesc.pRasterizerState->mDepthClampEnable = true;
    desc.mGraphicsDesc.pShaderProgram = pGizmoSystem->pGizmoGrid;

    addPipeline(pGizmoSystem->pRenderer, &desc, &pGizmoSystem->pGizmoGridPipeline);
}

static void removePipelines()
{
    removePipeline(pGizmoSystem->pRenderer, pGizmoSystem->pGizmoOpaquePipeline);
    removePipeline(pGizmoSystem->pRenderer, pGizmoSystem->pGizmoFaceOpaquePipeline);
    removePipeline(pGizmoSystem->pRenderer, pGizmoSystem->pGizmoListLinesPipeline);
    removePipeline(pGizmoSystem->pRenderer, pGizmoSystem->pGizmoStripLinesPipeline);
    removePipeline(pGizmoSystem->pRenderer, pGizmoSystem->pGizmoTransparentPipeline);
    removePipeline(pGizmoSystem->pRenderer, pGizmoSystem->pGizmoGridPipeline);

    removePipeline(pGizmoSystem->pRenderer, pGizmoSystem->pGizmoLocalInstancePipeline);
    removePipeline(pGizmoSystem->pRenderer, pGizmoSystem->pGizmoLocalInstanceLinesPipeline);
}

/****************************************************************************/
// MARK: - Private Static Gizmo Common
/****************************************************************************/

static GizmoInnerData* addGizmoInnerData(uint32_t countInstances)
{
    GizmoInnerData* pInnerData = (GizmoInnerData*)tf_calloc(1, sizeof(GizmoInnerData));
    memset(pInnerData, 0, sizeof(GizmoInnerData));

    pInnerData->mCountInstance = countInstances;
    pInnerData->mSizeWriteInstance = countInstances * sizeof(GizmoInstanceData);

    pInnerData->pWriteInstance = (GizmoInstanceData*)tf_calloc(1, sizeof(GizmoInstanceData) * countInstances);

    pInnerData->ppGizmoInstanceBuffer = (TFBuffer**)tf_calloc(pGizmoSystem->mFrameMaxCount, sizeof(TFBuffer*));
    for (uint32_t i = 0; i < pGizmoSystem->mFrameMaxCount; i++)
    {
        TFBufferLoadDesc gizmoInDesc = { 0 };
        gizmoInDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_BUFFER;
        gizmoInDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
        gizmoInDesc.mDesc.mFlags = TF_BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
        gizmoInDesc.mDesc.mSize = countInstances * sizeof(GizmoInstanceData);
        gizmoInDesc.mDesc.mStructStride = sizeof(GizmoInstanceData);
        gizmoInDesc.mDesc.mElementCount = countInstances;
        gizmoInDesc.mDesc.pName = "GizmoInstanceBuffer";
        gizmoInDesc.ppBuffer = &(pInnerData->ppGizmoInstanceBuffer[i]);
        gizmoInDesc.pData = NULL;
        addResource_Buffer(&gizmoInDesc, NULL);
    }

    waitForAllResourceLoads();

    TFDescriptorSetDesc perBatch = SRT_SET_DESC(GizmoSrt, PerBatch, pGizmoSystem->mFrameMaxCount * countInstances, 0);
    addDescriptorSet(pGizmoSystem->pRenderer, &perBatch, &pInnerData->pDescriptorSetPerBatch);

    TFDescriptorDataRange rangeStartInstance{};
    TFDescriptorData      params[2] = {};
    params[1].pRanges = &rangeStartInstance;
    for (uint32_t i = 0; i < pGizmoSystem->mFrameMaxCount; i++)
    {
        params[0].mIndex = SRT_RES_IDX(GizmoSrt, PerBatch, gGizmoInstance);
        params[0].ppBuffers = &(pInnerData->ppGizmoInstanceBuffer[i]);
        for (uint32_t i2 = 0; i2 < countInstances; i2++)
        {
            params[1].mIndex = SRT_RES_IDX(GizmoSrt, PerBatch, gGizmoInstanceStart);
            params[1].ppBuffers = &pGizmoSystem->pInstanceNumberingBuffer;

            rangeStartInstance.mOffset = i2 * UNIFORM_BUFFER_ALIGNMENT;
            rangeStartInstance.mSize = UNIFORM_BUFFER_ALIGNMENT;
            rangeStartInstance.mStructStride = rangeStartInstance.mSize;
            updateDescriptorSet(pGizmoSystem->pRenderer, i * countInstances + i2, pInnerData->pDescriptorSetPerBatch,
                                TF_ARRAY_COUNT(params), params);
        }
    }
    return pInnerData;
}

static void sortGizmoByRenderOrder()
{
    size_t len = arrlen(pGizmoSystem->ppGizmo);
    arrsetlen(pGizmoSystem->pGizmoSortingArray, len);
    arrsetlen(pGizmoSystem->ppGizmoSortedTemp, len);

    for (size_t i = 0; i < len; i++)
    {
        uint32_t index = (uint32_t)i;
        uint32_t priority = (pGizmoSystem->ppGizmo[index]->mRenderOrder) << (sizeof(uint16_t) * 8);
        priority = priority | pGizmoSystem->ppGizmo[index]->mRenderOrderOffset;

        uint64_t sortData = ((uint64_t)priority << (sizeof(uint32_t) * 8)) | index;
        pGizmoSystem->pGizmoSortingArray[i] = sortData;
    }

    sortUInt64(pGizmoSystem->pGizmoSortingArray, len);

    for (size_t i = 0; i < len; i++)
    {
        uint64_t sortData = pGizmoSystem->pGizmoSortingArray[i];
        uint32_t index = sortData & UINT32_MAX;
        pGizmoSystem->ppGizmoSortedTemp[i] = pGizmoSystem->ppGizmo[index];
    }

    TFGizmo** ppTemp = pGizmoSystem->ppGizmoSortedTemp;
    pGizmoSystem->ppGizmoSortedTemp = pGizmoSystem->ppGizmo;
    pGizmoSystem->ppGizmo = ppTemp;
}

static void setupNewCameraMatrixes(const GizmoUpdateCameraDesc* pCameraDesc)
{
    pGizmoSystem->mMatrixes.mCameraProjection = pCameraDesc->mProjection;
    pGizmoSystem->mMatrixes.mCameraWorldToLocal = pCameraDesc->mCameraWorldtoLocal;
}

static void updateUniformBuffer()
{
    TFBufferUpdateDesc uniformData = { pGizmoSystem->pUniformBuffer, (*pGizmoSystem->pFrameIdx) * UNIFORM_BUFFER_ALIGNMENT,
                                       sizeof(pGizmoSystem->mMatrixes) };
    beginUpdateResource_Buffer(&uniformData);
    memcpy(uniformData.pMappedData, &pGizmoSystem->mMatrixes, sizeof(pGizmoSystem->mMatrixes));
    endUpdateResource_Buffer(&uniformData);
}

static void updateGizmoBuffers(GizmoInnerData* pInnerData)
{
    TFBufferUpdateDesc instances = { pInnerData->ppGizmoInstanceBuffer[*pGizmoSystem->pFrameIdx], 0, pInnerData->mSizeWriteInstance };

    beginUpdateResource_Buffer(&instances);
    memcpy(instances.pMappedData, pInnerData->pWriteInstance, pInnerData->mSizeWriteInstance);
    endUpdateResource_Buffer(&instances);
}

static float4x4 makePointInstanceMatrix(float3 point, float scale)
{
    return f4x4InitRows(scale, 0, 0, point.x, 0, scale, 0, point.y, 0, 0, scale, point.z, 0, 0, 0, 1);
}

static void calculateVisibility(TFGizmo* pGizmo, const GizmoUpdateCameraDesc* pCameraDesc, float scale)
{
    GizmoInnerData* pInnerData = (GizmoInnerData*)pGizmo->pGizmo;
    float3          localPos = f4GetXYZ(f4x4Mulf4(pCameraDesc->mCameraWorldtoLocal, f4Fromf3(*pGizmo->pPosition, 1)));
    float           distance = fabsf(localPos.z);
    pInnerData->mVisibility.mScaleForCamera = distance * scale * pCameraDesc->mFovTangent;
    pInnerData->mVisibility.mBasicScale = scale;
    pInnerData->mVisibility.mFovTangent = pCameraDesc->mFovTangent;
    pInnerData->mVisibility.mIsVisible = localPos.z > pCameraDesc->mNearPlane;
}

static void calculateCameraUpdateData(const GizmoUpdateCameraDesc* pCameraDesc, float2 mousePosition, GizmoUpdateData* pUpdateData)
{
    pUpdateData->mCameraLocalToWorld = pCameraDesc->mCameraLocalToWorld;
    pUpdateData->mCameraWorldtoLocal = pCameraDesc->mCameraWorldtoLocal;
    pUpdateData->mCameraFovTangent = pCameraDesc->mFovTangent;

    // ray
    float3 rayOrigin = f4GetXYZ(f4x4Mulf4(pCameraDesc->mCameraLocalToWorld, f4Make(0, 0, 0, 1)));

    float3 rayDirection;

    {
        float3 mouseProj = f3Make(mousePosition.x * 2.0f - 1.0f, mousePosition.y * 2.0f - 1.0f, pCameraDesc->mNearPlane);

        float farPlane = pCameraDesc->mFarPlane;

        mouseProj = f3Make(mouseProj.x * farPlane * pCameraDesc->mFovTangent,
                           -mouseProj.y * farPlane * pCameraDesc->mFovTangent * pCameraDesc->mAspectInverse, farPlane);

        float4 viewPos = f4Normalize(f4Fromf3(mouseProj, 0.0f));
        viewPos = f4x4Mulf4(pCameraDesc->mCameraLocalToWorld, viewPos);

        rayDirection = f4GetXYZ(viewPos);
        rayDirection = f3Normalize(rayDirection);
    }

    pUpdateData->mCameraForward = f4GetXYZ(f4x4GetCol(pCameraDesc->mCameraLocalToWorld, 2));
    pUpdateData->mCameraPosition = rayOrigin;
    pUpdateData->mRayOrigin = rayOrigin;
    pUpdateData->mRayDirection = rayDirection;
    pUpdateData->mRayEnd = f3Add(pUpdateData->mRayOrigin, f3MulScalar(pUpdateData->mRayDirection, pCameraDesc->mFarPlane));
    pUpdateData->mMousePosition = mousePosition;
}

static void calculateGizmoUpdateData(TFGizmo* pGizmo, GizmoUpdateData* pUpdateData)
{
    float4x4 inverseTRS = f4x4Transpose(f4x4RotationQuat(*pGizmo->pRotation));
    inverseTRS = f4x4Mul(inverseTRS, f4x4Translation(f3MulScalar(*pGizmo->pPosition, -1)));
    float3x3 gizmoRotM = f3x3RotationQuat(*pGizmo->pRotation);

    pUpdateData->mDirectionToGizmo = f3Normalize(f3Sub(*pGizmo->pPosition, pUpdateData->mCameraPosition));
    pUpdateData->mLocalDirectionToGizmo = f3x3Mulf3(f3x3Transpose(gizmoRotM), pUpdateData->mDirectionToGizmo);
    pUpdateData->mLocalRayOrigin = f4GetXYZ(f4x4Mulf4(inverseTRS, f4Fromf3(pUpdateData->mRayOrigin, 1)));
    pUpdateData->mLocalRayDirection = f4GetXYZ(f4x4Mulf4(inverseTRS, f4Fromf3(pUpdateData->mRayDirection, 0)));
    pUpdateData->mGizmoPosition = *pGizmo->pPosition;
    pUpdateData->mGizmoScale = ((GizmoInnerData*)pGizmo->pGizmo)->mVisibility.mScaleForCamera;
    pUpdateData->mGizmoRight = f3x3GetCol(gizmoRotM, 0);
    pUpdateData->mGizmoUp = f3x3GetCol(gizmoRotM, 1);
    pUpdateData->mGizmoForward = f3x3GetCol(gizmoRotM, 2);
}

static void finishContactData(const GizmoUpdateData* pData, GizmoContactData* pContactData)
{
    pContactData->distanceToContact = f3Length(f3Sub(pData->mRayOrigin, pContactData->mContactPosition));
    pContactData->mMousePosition = pData->mMousePosition;
}

static void transformLocalToWorld(const TFGizmo* pGizmo, float3 p, float3* pP)
{
    p = quatRotateVector(*pGizmo->pRotation, p);
    *pP = f3Add(p, *pGizmo->pPosition);
}

static void transformWorldToLocal(const TFGizmo* pGizmo, float3 p, float3* pP)
{
    p = f3Sub(p, *pGizmo->pPosition);
    quat rot = quatInverse(*pGizmo->pRotation);
    *pP = quatRotateVector(rot, p);
}

static float3 safeClosestPointSegnemntRay(float3 sOrigin, float3 sDir, float3 sEnd, float3 rOrgin, float3 rDir)
{
    float3 dirToOrigin = f3Normalize(f3Sub(rOrgin, sOrigin));
    float3 otherNormal = f3Cross(rDir, dirToOrigin);
    otherNormal = f3Cross(otherNormal, rDir);
    otherNormal = f3Normalize(otherNormal);
    float4 plane = makePlaneAsFloat4(otherNormal, rOrgin);

    float3 planeContact;
    if (!intersectionRayDistPlane(sOrigin, sDir, plane, &planeContact))
    {
        planeContact = projectPointOnPlane(sEnd, plane);
    }

    float d = f3Dot(rDir, f3Sub(planeContact, rOrgin));
    return f3Add(rOrgin, f3MulScalar(rDir, d));
}

static bool moveContact(const GizmoContactData* pStartContact, const GizmoUpdateData* pData, float3* pPoint)
{
    float4x4 inverseTRS = pData->mCameraWorldtoLocal;
    float3   localOriginData = f4GetXYZ(f4x4Mulf4(inverseTRS, f4Fromf3(pStartContact->mOrigin, 1)));
    float3   localContactData = f4GetXYZ(f4x4Mulf4(inverseTRS, f4Fromf3(pStartContact->mContactPosition, 1)));
    float3   localDirectionData = f3Normalize(f4GetXYZ(f4x4Mulf4(inverseTRS, f4Fromf3(pStartContact->mDirection, 0))));

    float3 localRayOrigin = f4GetXYZ(f4x4Mulf4(inverseTRS, f4Fromf3(pData->mRayOrigin, 1)));
    float3 localRayEnd = f4GetXYZ(f4x4Mulf4(inverseTRS, f4Fromf3(pData->mRayEnd, 1)));
    float3 localRayDirection = f3Normalize(f4GetXYZ(f4x4Mulf4(inverseTRS, f4Fromf3(pData->mRayDirection, 0))));

    float3 localNewPoint = f3Make(0, 0, 0);
    float3 localMoveDirection = f3Make(0, 0, 0);
    if (pStartContact->mContactType == GIZMO_CONTACT_SEGMENT)
    {
        localNewPoint = safeClosestPointSegnemntRay(localRayOrigin, localRayDirection, localRayEnd, localOriginData, localDirectionData);
        localMoveDirection = localDirectionData;
    }
    else
    {
        if (fabsf(localDirectionData.z) <= FLT_EPSILON)
            return false;

        float4 testPlane = makePlaneAsFloat4(localDirectionData, localOriginData);
        if (!intersectionRayDistPlane(localRayOrigin, localRayDirection, testPlane, &localNewPoint))
        {
            localNewPoint = projectPointOnPlane(localRayEnd, testPlane);
        }

        localMoveDirection = f3Normalize(f3Sub(localContactData, localOriginData));
    }

    float3 localStartPosition = f3Make(0, 0, 0);

    float cosDir = localMoveDirection.z;
    if (fabsf(cosDir) <= FLT_EPSILON)
    {
        localStartPosition = localNewPoint;
    }
    else
    {
        float t = localNewPoint.z;
        localStartPosition = f3Add(localNewPoint, f3MulScalar(localMoveDirection, (-t / cosDir)));
    }

    float lastScale = pStartContact->mScaleWithoutDistance * pData->mCameraFovTangent *
                      (f3Length(f3Sub(pStartContact->mContactPosition, pStartContact->mOrigin)) / pStartContact->mScaleWithDistance);

    float3 diff = f3Sub(localNewPoint, localStartPosition);
    float  distToNewPoint = f3Length(diff);
    float3 locDirToNewPoint = f3Make(0, 0, 0);
    if (distToNewPoint > FLT_EPSILON)
        locDirToNewPoint = f3Normalize(diff);

    cosDir *= sign(f3Dot(f3Sub(localContactData, localOriginData), localMoveDirection));
    float t = distToNewPoint / (1.0f + cosDir * lastScale);

    float3 locFinalPosition = f3Add(localStartPosition, f3MulScalar(locDirToNewPoint, t));
    *pPoint = f4GetXYZ(f4x4Mulf4(pData->mCameraLocalToWorld, f4Fromf3(locFinalPosition, 1)));
    return true;
}

static void changeGizmoPosition(const TFGizmo* pGizmo, float3 pos)
{
    float3 old = *pGizmo->pPosition;
    if (!(old.x == pos.x && old.y == pos.y && old.z == pos.z))
    {
        *pGizmo->pPosition = pos;
        if (pGizmo->pOnPositionChange != NULL)
        {
            pGizmo->pOnPositionChange(pGizmo->pOnPositionChangeUserData, old, pos);
        }
    }
}

static void changeGizmoRotation(const TFGizmo* pGizmo, quat rot)
{
    quat old = *pGizmo->pRotation;
    if (!(old.x == rot.x && old.y == rot.y && old.z == rot.z && old.w == rot.w))
    {
        *pGizmo->pRotation = rot;
        if (pGizmo->pOnRotationChange != NULL)
        {
            pGizmo->pOnRotationChange(pGizmo->pOnRotationChangeUserData, old, rot);
        }
    }
}

static void changeGizmoScale(const TFGizmo* pGizmo, float3 scale)
{
    float3 old = *pGizmo->pScale;
    if (!(old.x == scale.x && old.y == scale.y && old.z == scale.z))
    {
        *pGizmo->pScale = scale;
        if (pGizmo->pOnScaleChange != NULL)
        {
            pGizmo->pOnScaleChange(pGizmo->pOnScaleChangeUserData, old, scale);
        }
    }
}

static void gizmoReset(TFGizmo* pGizmo)
{
    GizmoInnerData* pInnerData = (GizmoInnerData*)pGizmo->pGizmo;
    pInnerData->mHasContact = false;
    pInnerData->mFrontAxis = (GizmoAxis)0;
    pInnerData->mScaleOffset = f3Make(1, 1, 1);
}

static void updateGizmoDefault(TFGizmo* pGizmo, const GizmoUpdateCameraDesc* pCameraDesc, const GizmoUpdateData* pData)
{
    UNREF_PARAM(pData);

    GizmoInnerData* pInnerData = (GizmoInnerData*)pGizmo->pGizmo;
    pInnerData->mHasContact = false;
    pInnerData->mVisibility.mBasicScale = 1;
    pInnerData->mVisibility.mScaleForCamera = 1;
    pInnerData->mVisibility.mIsVisible = true;
    pInnerData->mVisibility.mFovTangent = pCameraDesc->mFovTangent;
}

static void drawQueue(TFCmd* pCmd, GizmoDrawQueue queue, uint32_t start, uint32_t end)
{
    for (size_t i = start; i < end; i++)
    {
        TFGizmo* pGizmo = pGizmoSystem->ppGizmo[i];
        if (!gizmoIsActive(pGizmo) || !gizmoIsValid(pGizmo))
        {
            continue;
        }
        GizmoInnerData* innerData = (GizmoInnerData*)pGizmo->pGizmo;
        innerData->pDraw(innerData, pCmd, queue);
    }
}

static void drawRange(TFCmd* pCmd, GizmoRenderTarget* pRenderTarget, uint32_t start, uint32_t end)
{
    if (end == start)
        return;

    TFBindRenderTargetsDesc bindRenderTargets = { 0 };
    bindRenderTargets.mRenderTargetCount = 1;
    bindRenderTargets.mRenderTargets[0].pRenderTarget = pRenderTarget->pColorTarget;
    bindRenderTargets.mRenderTargets[0].mLoadAction = TF_LOAD_ACTION_LOAD;
    bindRenderTargets.mDepthStencil.pRenderTarget = pRenderTarget->pDepthTarget;
    bindRenderTargets.mDepthStencil.mLoadAction = pRenderTarget->mClearDepth ? TF_LOAD_ACTION_CLEAR : TF_LOAD_ACTION_LOAD;

    cmdBindRenderTargets(pCmd, &bindRenderTargets);
    cmdSetViewport(pCmd, pRenderTarget->mX, pRenderTarget->mY, pRenderTarget->mWidth, pRenderTarget->mHeight, pRenderTarget->mMinDepth,
                   pRenderTarget->mMaxDepth);
    cmdSetScissor(pCmd, (uint32_t)pRenderTarget->mX, (uint32_t)pRenderTarget->mY, (uint32_t)pRenderTarget->mWidth,
                  (uint32_t)pRenderTarget->mHeight);

    updateUniformBuffer();

    cmdBindPipeline(pCmd, pGizmoSystem->pGizmoStripLinesPipeline); // as pipeline layout for descriptor set
    cmdBindDescriptorSet(pCmd, *pGizmoSystem->pFrameIdx, pGizmoSystem->pDescriptorSetPerFrame);

    // start order
    uint32_t prevRenderOrder = pGizmoSystem->ppGizmo[start]->mRenderOrder;
    uint32_t indexStartPrevPriority = start;
    for (uint32_t i = start; i < end; i++)
    {
        TFGizmo*        pGizmo = pGizmoSystem->ppGizmo[i];
        GizmoInnerData* innerData = (GizmoInnerData*)pGizmo->pGizmo;
        updateGizmoBuffers(innerData);

        bool isEnd = i == end - 1;
        if (i != start && pGizmo->mRenderOrder != prevRenderOrder)
        {
            uint32_t localStart = indexStartPrevPriority;
            uint32_t localEnd = i;
            indexStartPrevPriority = i;

            drawQueue(pCmd, GIZMO_DRAW_QUEUE_OPAQUE, localStart, localEnd);
            drawQueue(pCmd, GIZMO_DRAW_QUEUE_TRANSPARENT, localStart, localEnd);

            // clear depth buffer
            cmdBindRenderTargets(pCmd, &bindRenderTargets);
        }

        if (isEnd)
        {
            uint32_t localStart = indexStartPrevPriority;
            uint32_t localEnd = end;
            drawQueue(pCmd, GIZMO_DRAW_QUEUE_OPAQUE, localStart, localEnd);
            drawQueue(pCmd, GIZMO_DRAW_QUEUE_TRANSPARENT, localStart, localEnd);
        }

        prevRenderOrder = pGizmo->mRenderOrder;
    }
}

/****************************************************************************/
// MARK: - Private Static Gizmo Handles
/****************************************************************************/

static void calculateHandlesVisibility(TFGizmo* pGizmo, const GizmoUpdateCameraDesc* pCameraDesc)
{
    calculateVisibility(pGizmo, pCameraDesc, ((GizmoHandlesStyleDesc*)pGizmo->pStyleGizmo)->mScale);
}

static GizmoAxis getFrontRotateAxis(const TFGizmo* pGizmo, const GizmoUpdateCameraDesc* pCameraDesc)
{
    float3 cameraPos = f4GetXYZ(f4x4Mulf4(pCameraDesc->mCameraLocalToWorld, f4Make(0, 0, 0, 1)));
    float3 gizmoPos = *pGizmo->pPosition;
    float3 dir = f3Normalize(f3Sub(gizmoPos, cameraPos));

    float3x3 rot = f3x3RotationQuat(*pGizmo->pRotation);
    float3   gizmoRight = f3x3Mulf3(rot, f3Make(1, 0, 0));
    float3   gizmoUp = f3x3Mulf3(rot, f3Make(0, 1, 0));
    float3   gizmoForward = f3x3Mulf3(rot, f3Make(0, 0, 1));

    const float activeFrontAngle = 0.90f;
    if (fabsf(f3Dot(dir, gizmoForward)) >= activeFrontAngle)
        return GIZMO_AXIS_Z;
    else if (fabsf(f3Dot(dir, gizmoUp)) >= activeFrontAngle)
        return GIZMO_AXIS_Y;
    else if (fabsf(f3Dot(dir, gizmoRight)) >= activeFrontAngle)
        return GIZMO_AXIS_X;

    return GIZMO_AXIS_NONE;
}

static bool getActiveMoveScaleAxis(const TFGizmo* pGizmo, const GizmoUpdateData* pData, GizmoContactData* pContactData)
{
    GizmoHandlesStyleDesc* pStyle = (GizmoHandlesStyleDesc*)pGizmo->pStyleGizmo;
    pContactData->mScaleWithoutDistance = pStyle->mScale;
    pContactData->mScaleWithDistance = pData->mGizmoScale;

    // center cube
    {
        float  sizeCube = pStyle->mSizeCenterCube * pData->mGizmoScale;
        TFAABB aabb;
        aabb.min = f3AlignedMake(-sizeCube, -sizeCube, -sizeCube);
        aabb.max = f3AlignedMake(sizeCube, sizeCube, sizeCube);

        float3 localContact = f3Make(0, 0, 0);
        if (intersectionRayAABB(pData->mLocalRayOrigin, pData->mLocalRayDirection, aabb, &localContact))
        {
            pContactData->mContactType = GIZMO_CONTACT_PLANE;
            pContactData->mOrigin = pData->mGizmoPosition;
            pContactData->mDirection = pData->mCameraForward;
            transformLocalToWorld(pGizmo, localContact, &pContactData->mContactPosition);
            pContactData->mAxisType = GIZMO_AXIS_XYZ;
            return true;
        }
    }

    // axises
    {
        float3 startAxisSegment = pData->mGizmoPosition;
        float3 endAxisSegmentX = f3Add(startAxisSegment, f3MulScalar(pData->mGizmoRight, pData->mGizmoScale));
        float3 endAxisSegmentY = f3Add(startAxisSegment, f3MulScalar(pData->mGizmoUp, pData->mGizmoScale));
        float3 endAxisSegmentZ = f3Add(startAxisSegment, f3MulScalar(pData->mGizmoForward, pData->mGizmoScale));

        float detectAxisDist = pStyle->mSizeEdgeCube * pData->mGizmoScale;
        detectAxisDist *= detectAxisDist; // s quare

        pContactData->mContactType = GIZMO_CONTACT_SEGMENT;

        float  s = 0.0f;
        float  t = 0.0f;
        float3 c1 = f3Make(0, 0, 0);
        float3 c2 = f3Make(0, 0, 0);
        float  distToX =
            closestPointRaySegment(pData->mRayOrigin, pData->mRayDirection, startAxisSegment, endAxisSegmentX, &s, &t, &c1, &c2);
        if (distToX <= detectAxisDist)
        {
            pContactData->mOrigin = pData->mGizmoPosition;
            pContactData->mDirection = pData->mGizmoRight;
            pContactData->mContactPosition = c2;
            pContactData->mAxisType = GIZMO_AXIS_X;
            return true;
        }

        float distToY =
            closestPointRaySegment(pData->mRayOrigin, pData->mRayDirection, startAxisSegment, endAxisSegmentY, &s, &t, &c1, &c2);
        if (distToY <= detectAxisDist)
        {
            pContactData->mOrigin = pData->mGizmoPosition;
            pContactData->mDirection = pData->mGizmoUp;
            pContactData->mContactPosition = c2;
            pContactData->mAxisType = GIZMO_AXIS_Y;
            return true;
        }

        float distToZ =
            closestPointRaySegment(pData->mRayOrigin, pData->mRayDirection, startAxisSegment, endAxisSegmentZ, &s, &t, &c1, &c2);
        if (distToZ <= detectAxisDist)
        {
            pContactData->mOrigin = pData->mGizmoPosition;
            pContactData->mDirection = pData->mGizmoForward;
            pContactData->mContactPosition = c2;
            pContactData->mAxisType = GIZMO_AXIS_Z;
            return true;
        }
    }

    // planes
    {
        float offsetRect = pStyle->mOffsetPlaneRect * pData->mGizmoScale;
        float sizeRect = pStyle->mSizePlaneRect * pData->mGizmoScale;

        int    resIndex = -1;
        float3 contact = f3Make(0, 0, 0);
        for (int i = 0; i < 3; i++)
        {
            float n[3] = { 0, 0, 0 };
            n[i] = 1.0f;
            float offset[3] = { offsetRect, offsetRect, offsetRect };
            offset[i] = 0.0f;

            if (intersectionRayDistPlane(pData->mLocalRayOrigin, pData->mLocalRayDirection, f4Make(n[0], n[1], n[2], 0), &contact))
            {
                float3 rL = f3DivScalar(f3Sub(contact, f3Make(offset[0], offset[1], offset[2])), sizeRect);
                offset[0] = rL.x;
                offset[1] = rL.y;
                offset[2] = rL.z;
                offset[i] = 0.5f;
                rL = f3Make(offset[0], offset[1], offset[2]);
                if (rL.x >= 0.0f && rL.x <= 1.0f && rL.y >= 0.0f && rL.y <= 1.0f && rL.z >= 0.0f && rL.z <= 1.0f)
                {
                    resIndex = i;
                    break;
                }
            }
        }

        if (resIndex != -1)
        {
            float3 directions[3] = { pData->mGizmoRight, pData->mGizmoUp, pData->mGizmoForward };

            pContactData->mContactType = GIZMO_CONTACT_PLANE;
            pContactData->mOrigin = pData->mGizmoPosition;
            pContactData->mDirection = directions[resIndex];
            transformLocalToWorld(pGizmo, contact, &pContactData->mContactPosition);

            if (resIndex == 0)
                pContactData->mAxisType = GIZMO_AXIS_YZ;
            else if (resIndex == 1)
                pContactData->mAxisType = GIZMO_AXIS_XZ;
            else
                pContactData->mAxisType = GIZMO_AXIS_XY;
            return true;
        }
    }
    return false;
}

static bool getActiveRotateAxis(const TFGizmo* pGizmo, const GizmoUpdateData* pData, GizmoContactData* pContactData)
{
    GizmoHandlesStyleDesc* pStyle = (GizmoHandlesStyleDesc*)pGizmo->pStyleGizmo;
    GizmoInnerData*        pInnerData = (GizmoInnerData*)pGizmo->pGizmo;
    pContactData->mScaleWithoutDistance = pStyle->mScale;
    pContactData->mScaleWithDistance = pData->mGizmoScale;

    uint32_t axises[3] = { GIZMO_AXIS_X, GIZMO_AXIS_Y, GIZMO_AXIS_Z };

    float3 axisesDirections[3] = { pData->mGizmoRight, pData->mGizmoUp, pData->mGizmoForward };

    float3 startAxisSegment = pData->mGizmoPosition;
    float3 axisesEnd[3] = { f3Add(startAxisSegment, f3MulScalar(axisesDirections[0], pData->mGizmoScale)),
                            f3Add(startAxisSegment, f3MulScalar(axisesDirections[1], pData->mGizmoScale)),
                            f3Add(startAxisSegment, f3MulScalar(axisesDirections[2], pData->mGizmoScale)) };
    float  detectAxisDist = pStyle->mSizeEdgeCube * pData->mGizmoScale;
    detectAxisDist *= detectAxisDist;

    uint32_t frontAxis = pInnerData->mFrontAxis;

    float radiusMainAxis = pStyle->mRadiusMainAxis * pData->mGizmoScale;
    float heightCylinder = pStyle->mMainAxisThinkness * 2 * pData->mGizmoScale;

    // test cylinder axises
    for (int i = 0; i < 3; i++)
    {
        if (frontAxis == axises[i])
            continue;

        float vecAr[3] = { 0, 0, 0 };
        vecAr[i] = 1.0f;
        float3 cylinderDirection = f3Make(vecAr[0], vecAr[1], vecAr[2]);

        float  d = f3Dot(cylinderDirection, pData->mLocalRayOrigin);
        float3 projOrigin = f3Sub(pData->mLocalRayOrigin, f3MulScalar(cylinderDirection, d));

        vecAr[0] = pData->mLocalRayDirection.x;
        vecAr[1] = pData->mLocalRayDirection.y;
        vecAr[2] = pData->mLocalRayDirection.z;
        vecAr[i] = 0.0f;
        float3 projDirection = f3Make(vecAr[0], vecAr[1], vecAr[2]);

        projDirection = f3Normalize(projDirection);

        float t = f3Dot(projDirection, f3MulScalar(projOrigin, -1));

        if (t < 0.0f)
            continue;

        float h = f3Length(f3Add(f3MulScalar(projDirection, t), projOrigin));

        if (h > radiusMainAxis)
            continue;

        float f = sqrtf(radiusMainAxis * radiusMainAxis - h * h);
        float c = f3Dot(projDirection, pData->mLocalRayDirection);

        float3 nearPoint = f3Add(pData->mLocalRayOrigin, f3MulScalar(pData->mLocalRayDirection, ((t - f) / c)));
        vecAr[0] = nearPoint.x;
        vecAr[1] = nearPoint.y;
        vecAr[2] = nearPoint.z;

        float3 globalContact;
        int    planeContact = intersectionRayDistPlane(pData->mRayOrigin, pData->mRayDirection,
                                                       makePlaneAsFloat4(axisesDirections[i], pData->mGizmoPosition), &globalContact);

        bool approvedContact = false;
        if (fabsf(vecAr[i]) <= heightCylinder)
        {
            approvedContact = true;
        }

        if (planeContact && f3Dot(f3Sub(globalContact, pData->mGizmoPosition), f3Sub(pData->mRayOrigin, pData->mGizmoPosition)) > 0.0f)
        {
            float distToContant = f3Length(f3Sub(pData->mGizmoPosition, globalContact));
            float diff = radiusMainAxis - distToContant;
            if (diff <= heightCylinder * 2 && diff >= 0)
            {
                approvedContact = true;
            }
        }

        if (approvedContact)
        {
            pContactData->mContactType = GIZMO_CONTACT_PLANE;
            pContactData->mOrigin = pData->mGizmoPosition;
            pContactData->mDirection = axisesDirections[i];
            pContactData->mAxisType = axises[i];
            pContactData->mContactPosition = globalContact;
            return true;
        }
    }

    // test line axises
    for (int i = 0; i < 3; i++)
    {
        float  s = 0.0f;
        float  t = 0.0f;
        float3 c1 = f3Make(0, 0, 0);
        float3 c2 = f3Make(0, 0, 0);
        float  distToAxis =
            closestPointRaySegment(pData->mRayOrigin, pData->mRayDirection, startAxisSegment, axisesEnd[i], &s, &t, &c1, &c2);
        if (distToAxis <= detectAxisDist)
        {
            pContactData->mContactType = GIZMO_CONTACT_SEGMENT;
            pContactData->mOrigin = pData->mGizmoPosition;
            pContactData->mDirection = axisesDirections[i];
            pContactData->mContactPosition = c2;
            pContactData->mAxisType = axises[i];
            return true;
        }
    }

    // test front axises;
    float3 dirToCenter = f3Sub(pData->mGizmoPosition, pData->mRayOrigin);
    float  distToCenter = f3Length(dirToCenter);
    dirToCenter = f3DivScalar(dirToCenter, distToCenter);

    float  distToCenterProj = 0.0f;
    float3 pointContactWithFrontPlane;
    {
        float c = f3Dot(dirToCenter, pData->mRayDirection);
        float f = distToCenter / c;
        distToCenterProj = sqrtf(f * f - distToCenter * distToCenter);
        intersectionRayDistPlane(pData->mRayOrigin, pData->mRayDirection, makePlaneAsFloat4(dirToCenter, pData->mGizmoPosition),
                                 &pointContactWithFrontPlane);
    }

    pContactData->mContactType = GIZMO_CONTACT_PLANE;
    pContactData->mOrigin = pData->mGizmoPosition;

    if (distToCenterProj <= pStyle->mRadiusOutsideAxis * pData->mGizmoScale)
    {
        if (frontAxis != GIZMO_AXIS_NONE)
        {
            const float contactDist = 0.04f;
            if (fabsf(distToCenterProj - pStyle->mRadiusFrontAxis * pData->mGizmoScale) <= contactDist * pData->mGizmoScale)
            {
                for (int i = 0; i < 3; i++)
                {
                    if (frontAxis != axises[i])
                        continue;

                    pContactData->mAxisType = axises[i];
                    pContactData->mDirection = axisesDirections[i];
                    intersectionRayDistPlane(pData->mRayOrigin, pData->mRayDirection,
                                             makePlaneAsFloat4(axisesDirections[i], pData->mGizmoPosition),
                                             &pContactData->mContactPosition);
                    break;
                }

                return true;
            }
        }

        pContactData->mDirection = dirToCenter;
        pContactData->mContactPosition = pointContactWithFrontPlane;

        if (distToCenterProj <= pStyle->mRadiusFrontAxis * pData->mGizmoScale)
        {
            pContactData->mAxisType = GIZMO_AXIS_GLOBAL;
            return true;
        }

        pContactData->mAxisType = GIZMO_AXIS_CAMERA;
        return true;
    }

    return false;
}

static void moveGizmoHandles(TFGizmo* pGizmo, const GizmoUpdateData* pData)
{
    GizmoInnerData* pInnerData = (GizmoInnerData*)pGizmo->pGizmo;
    float3          newPoint;
    if (moveContact(&pInnerData->mContact, pData, &newPoint))
    {
        changeGizmoPosition(pGizmo, newPoint);
    }
}

static void scaleGizmoHandles(TFGizmo* pGizmo, const GizmoUpdateData* pData)
{
    GizmoInnerData* pInnerData = (GizmoInnerData*)pGizmo->pGizmo;

    float3 scaleOffset = pInnerData->mScaleOffset;
    if (pInnerData->mContact.mContactType == GIZMO_CONTACT_SEGMENT)
    {
        float3 contactPoint = safeClosestPointSegnemntRay(pData->mRayOrigin, pData->mRayDirection, pData->mRayEnd,
                                                          pInnerData->mContact.mOrigin, pInnerData->mContact.mDirection);

        float segmentScale;
        if (f3Dot(f3Sub(contactPoint, pInnerData->mContact.mOrigin), pInnerData->mContact.mDirection) < 0.0f)
            segmentScale = 0;
        else
            segmentScale = f3Length(f3Sub(contactPoint, pInnerData->mContact.mOrigin)) / pInnerData->mContact.mScaleWithDistance;

        float fisrtScale =
            f3Length(f3Sub(pInnerData->mContact.mContactPosition, pInnerData->mContact.mOrigin)) / pInnerData->mContact.mScaleWithDistance;

        float copyScale = 0;
        if (pInnerData->mContact.mAxisType == GIZMO_AXIS_X)
        {
            scaleOffset.x = segmentScale / fisrtScale;
            copyScale = scaleOffset.x;
        }
        else if (pInnerData->mContact.mAxisType == GIZMO_AXIS_Y)
        {
            scaleOffset.y = segmentScale / fisrtScale;
            copyScale = scaleOffset.y;
        }
        else if (pInnerData->mContact.mAxisType == GIZMO_AXIS_Z)
        {
            scaleOffset.z = segmentScale / fisrtScale;
            copyScale = scaleOffset.z;
        }

        if (pInnerData->mScaleMode == TF_GIZMO_HANDLES_SCALE_MODE_XY_CONNECTED && (pInnerData->mContact.mAxisType & GIZMO_AXIS_XY) != 0)
        {
            scaleOffset.x = copyScale;
            scaleOffset.y = copyScale;
        }
        else if (pInnerData->mScaleMode == TF_GIZMO_HANDLES_SCALE_MODE_XZ_CONNECTED &&
                 (pInnerData->mContact.mAxisType & GIZMO_AXIS_XZ) != 0)
        {
            scaleOffset.x = copyScale;
            scaleOffset.z = copyScale;
        }
        else if (pInnerData->mScaleMode == TF_GIZMO_HANDLES_SCALE_MODE_YZ_CONNECTED &&
                 (pInnerData->mContact.mAxisType & GIZMO_AXIS_YZ) != 0)
        {
            scaleOffset.y = copyScale;
            scaleOffset.z = copyScale;
        }
        else if (pInnerData->mScaleMode == TF_GIZMO_HANDLES_SCALE_MODE_XYZ_CONNECTED)
        {
            scaleOffset = f3Make(copyScale, copyScale, copyScale);
        }
    }
    else
    {
        if (fabsf(f3Dot(pInnerData->mContact.mDirection, pData->mRayDirection)) <= FLT_EPSILON)
            return;

        float4 testPlane = makePlaneAsFloat4(pInnerData->mContact.mDirection, pInnerData->mContact.mOrigin);
        float3 contactPoint;
        if (!intersectionRayDistPlane(pData->mRayOrigin, pData->mRayDirection, testPlane, &contactPoint))
        {
            contactPoint = projectPointOnPlane(pData->mRayEnd, testPlane);
        }

        bool planes = true;
        if (pInnerData->mContact.mAxisType == GIZMO_AXIS_XYZ)
            planes = false;

        if (planes)
        {
            float4x4 inverseTRS = f4x4Transpose(f4x4RotationQuat(*pGizmo->pRotation));
            inverseTRS = f4x4Mul(inverseTRS, f4x4Translation(f3MulScalar(*pGizmo->pPosition, -1)));
            float3 localContactPoint = f4GetXYZ(f4x4Mulf4(inverseTRS, f4Fromf3(contactPoint, 1)));
            float3 localFirstContact = f4GetXYZ(f4x4Mulf4(inverseTRS, f4Fromf3(pInnerData->mContact.mContactPosition, 1)));

            float3 mask;
            if (pInnerData->mContact.mAxisType == GIZMO_AXIS_XY)
                mask = f3Make(1.0f, 1.0f, 0.0f);
            else if (pInnerData->mContact.mAxisType == GIZMO_AXIS_XZ)
                mask = f3Make(1.0f, 0.0f, 1.0f);
            else
                mask = f3Make(0.0f, 1.0f, 1.0f);

            localContactPoint.x = fmaxf(0.0f, localContactPoint.x);
            localContactPoint.y = fmaxf(0.0f, localContactPoint.y);
            localContactPoint.z = fmaxf(0.0f, localContactPoint.z);

            if (mask.x != 0.0f)
                localContactPoint.x = localContactPoint.x / fabsf(localFirstContact.x);
            if (mask.y != 0.0f)
                localContactPoint.y = localContactPoint.y / fabsf(localFirstContact.y);
            if (mask.z != 0.0f)
                localContactPoint.z = localContactPoint.z / fabsf(localFirstContact.z);

            localContactPoint = f3DivScalar(localContactPoint, f3Length(f3Make(1.0f, 1.0f, 0.0f)));

            if (pInnerData->mScaleMode == TF_GIZMO_HANDLES_SCALE_MODE_XY_CONNECTED)
            {
                mask.x = 1.0f;
                mask.y = 1.0f;
            }
            else if (pInnerData->mScaleMode == TF_GIZMO_HANDLES_SCALE_MODE_XZ_CONNECTED)
            {
                mask.x = 1.0f;
                mask.z = 1.0f;
            }
            else if (pInnerData->mScaleMode == TF_GIZMO_HANDLES_SCALE_MODE_YZ_CONNECTED)
            {
                mask.y = 1.0f;
                mask.z = 1.0f;
            }
            if (pInnerData->mScaleMode == TF_GIZMO_HANDLES_SCALE_MODE_XYZ_CONNECTED)
            {
                mask = f3Make(1, 1, 1);
            }
            float3 inverseMask = f3Sub(f3Make(1.0f, 1.0f, 1.0f), mask);
            float  scaleCoef = f3Length(localContactPoint);
            scaleOffset = f3Add(inverseMask, f3MulScalar(mask, scaleCoef));
        }
        else
        {
            float4x4 inverseTRS = pData->mCameraWorldtoLocal;
            float3   localContactPoint = f4GetXYZ(f4x4Mulf4(inverseTRS, f4Fromf3(contactPoint, 1)));
            float3   localFirstContact = f4GetXYZ(f4x4Mulf4(inverseTRS, f4Fromf3(pInnerData->mContact.mContactPosition, 1)));

            float2 dir = f2Make(localContactPoint.x - localFirstContact.x, localContactPoint.y - localFirstContact.y);
            float2 axisScale = f2Normalize(f2Make(1.0f, 1.0f));

            float scale = 1.0f + f2Dot(dir, axisScale) / pInnerData->mContact.mScaleWithDistance * pData->mCameraFovTangent;
            scale = fmaxf(0.0f, scale);

            scaleOffset = f3Make(scale, scale, scale);
        }
    }
    pInnerData->mScaleOffset = scaleOffset;
    changeGizmoScale(pGizmo, f3MulPerElem(pInnerData->mStartScale, scaleOffset));
}

static void rotateGizmoHandles(TFGizmo* pGizmo, const GizmoUpdateData* pData)
{
    GizmoInnerData* pInnerData = (GizmoInnerData*)pGizmo->pGizmo;

    float2 mouseDelta = f2Sub(pData->mMousePosition, pInnerData->mContact.mMousePosition);

    if (pInnerData->mContact.mContactType == GIZMO_CONTACT_SEGMENT)
    {
        float rot = mouseDelta.x * PI * 2;

        Quat rotQ = quatMul(quatRotationRadians(-rot, pInnerData->mContact.mDirection), pInnerData->mStartRotation);
        changeGizmoRotation(pGizmo, quatNormalize(rotQ));
    }
    else if (pInnerData->mContact.mContactType == GIZMO_CONTACT_PLANE)
    {
        float3 dirToGizmo = f3Normalize(f3Sub(pInnerData->mContact.mOrigin, pData->mCameraPosition));
        if (pInnerData->mContact.mAxisType == GIZMO_AXIS_GLOBAL)
        {
            float2 delta = f2Sub(pData->mMousePosition, pInnerData->mContact.mMousePosition);
            if (fabsf(delta.x) <= FLT_EPSILON && fabsf(delta.y) <= FLT_EPSILON)
                return;

            delta.y = delta.y * -1;

            float3 dirRotate = f3Normalize(f3Make(delta.x, delta.y, 0.0f));
            float3 dir = f4GetXYZ(f4x4Mulf4(pData->mCameraLocalToWorld, f4Fromf3(dirRotate, 0)));

            float3 axisRotate = f3Normalize(f3Cross(dir, dirToGizmo));
            Quat   rotQ = quatMul(quatRotationRadians(f2Length(delta) * PI * 2, axisRotate), pInnerData->mStartRotation);
            changeGizmoRotation(pGizmo, quatNormalize(rotQ));
        }
        else
        {
            const float angleLimit = 0.15f; // cos limit rotate angle for planes

            if (fabsf(f3Dot(pInnerData->mContact.mDirection, dirToGizmo)) <= angleLimit)
            {
                float3 up = f3Cross(dirToGizmo, pInnerData->mContact.mDirection);
                float3 localUp = f4GetXYZ(f4x4Mulf4(pData->mCameraWorldtoLocal, f4Fromf3(up, 0)));

                localUp.z = 0;
                localUp = f3Normalize(localUp);

                float t = f2Dot(mouseDelta, f2Make(localUp.x, localUp.y)) * PI * 2;
                Quat  rotQ = quatMul(quatRotationRadians(-t, pInnerData->mContact.mDirection), pInnerData->mStartRotation);
                changeGizmoRotation(pGizmo, quatNormalize(rotQ));
            }
            else
            {
                float3 newContact;
                float4 planeToRotation = makePlaneAsFloat4(pInnerData->mContact.mDirection, pInnerData->mContact.mOrigin);
                if (!intersectionRayDistPlane(pData->mRayOrigin, pData->mRayDirection, planeToRotation, &newContact))
                {
                    newContact = projectPointOnPlane(pData->mRayEnd, planeToRotation);
                }

                float3 dirMainContact = f3Normalize(f3Sub(pInnerData->mContact.mContactPosition, pInnerData->mContact.mOrigin));
                float3 dirNewContact = f3Normalize(f3Sub(newContact, pInnerData->mContact.mOrigin));
                float3 otherSide = f3Cross(pInnerData->mContact.mDirection, dirMainContact);

                float angle = 0.0f;
                if (1.0f - fabsf(f3Dot(dirMainContact, dirNewContact)) > FLT_EPSILON)
                    angle = acosf(f3Dot(dirMainContact, dirNewContact));

                if (f3Dot(otherSide, dirNewContact) < 0.0f)
                    angle *= -1;

                Quat rotQ = quatMul(quatRotationRadians(angle, pInnerData->mContact.mDirection), pInnerData->mStartRotation);
                changeGizmoRotation(pGizmo, quatNormalize(rotQ));

                pInnerData->mContact.mContactPosition = newContact;
                pInnerData->mStartRotation = rotQ;
            }
        }
    }
}

static void fillMatrixesMoveScaleData(TFGizmo* pGizmo)
{
    GizmoHandlesStyleDesc* pStyle = (GizmoHandlesStyleDesc*)pGizmo->pStyleGizmo;
    GizmoInnerData*        pInnerData = (GizmoInnerData*)pGizmo->pGizmo;
    GizmoInstanceData*     pWrite = pInnerData->pWriteInstance;

    float    scale = pInnerData->mVisibility.mScaleForCamera;
    float4x4 gizmoTRS = f4x4Mul(f4x4Translation(*pGizmo->pPosition), f4x4RotationQuat(*pGizmo->pRotation));
    float4x4 scaleM = f4x4Scale(f3Make(scale, scale, scale));
    float4x4 directionY = f4x4RotationX(-PI / 2);
    float4x4 directionX = f4x4RotationY(PI / 2);

    float4x4 gizmoM = f4x4Mul(gizmoTRS, scaleM);
    float4x4 gizmoMY = f4x4Mul(gizmoM, directionY);
    float4x4 gizmoMX = f4x4Mul(gizmoM, directionX);

    float4x4 scaleLine;
    if (pInnerData->mState == TF_GIZMO_HANDLES_STATE_MOVE)
    {
        float distToCone = 1 - pStyle->mHeightCone;
        scaleLine = f4x4Scale(f3Make(0, 0, distToCone));
        pWrite[H_LINE_Z_IDX].mTRS = f4x4Mul(gizmoM, scaleLine);
        pWrite[H_LINE_Y_IDX].mTRS = f4x4Mul(gizmoMY, scaleLine);
        pWrite[H_LINE_X_IDX].mTRS = f4x4Mul(gizmoMX, scaleLine);
    }
    else
    {
        float3 offsetScale = pInnerData->mScaleOffset;
        float  sizeEdgeCube = pStyle->mSizeEdgeCube;
        scaleLine = f4x4Scale(f3Make(0, 0, offsetScale.z - sizeEdgeCube));
        pWrite[H_LINE_Z_IDX].mTRS = f4x4Mul(gizmoM, scaleLine);
        scaleLine = f4x4Scale(f3Make(0, 0, offsetScale.y - sizeEdgeCube));
        pWrite[H_LINE_Y_IDX].mTRS = f4x4Mul(gizmoMY, scaleLine);
        scaleLine = f4x4Scale(f3Make(0, 0, offsetScale.x - sizeEdgeCube));
        pWrite[H_LINE_X_IDX].mTRS = f4x4Mul(gizmoMX, scaleLine);
    }

    // conees
    if (pInnerData->mState == TF_GIZMO_HANDLES_STATE_MOVE)
    {
        float    radiusCone = pStyle->mRadiusCone;
        float    heightCone = pStyle->mHeightCone;
        float4x4 scaleAndOffsetCone = f4x4Mul(f4x4RotationX(PI / 2), f4x4Translation(f3Make(0, 1 - heightCone, 0)));
        scaleAndOffsetCone = f4x4Mul(scaleAndOffsetCone, f4x4Scale(f3Make(radiusCone, heightCone, radiusCone)));

        pWrite[H_CONE_Z_IDX].mTRS = f4x4Mul(gizmoM, scaleAndOffsetCone);
        pWrite[H_CONE_Y_IDX].mTRS = f4x4Mul(gizmoMY, scaleAndOffsetCone);
        pWrite[H_CONE_X_IDX].mTRS = f4x4Mul(gizmoMX, scaleAndOffsetCone);
    }
    else
    {
        float  sizeCube = pStyle->mSizeEdgeCube;
        float3 offsetScale = pInnerData->mScaleOffset;

        float4x4 scaleCube = f4x4Scale(f3Make(sizeCube, sizeCube, sizeCube));

        pWrite[H_SCALE_BOX_Z_IDX].mTRS = f4x4Mul(f4x4Mul(gizmoM, f4x4Translation(f3Make(0, 0, offsetScale.z))), scaleCube);
        pWrite[H_SCALE_BOX_Y_IDX].mTRS = f4x4Mul(f4x4Mul(gizmoMY, f4x4Translation(f3Make(0, 0, offsetScale.y))), scaleCube);
        pWrite[H_SCALE_BOX_X_IDX].mTRS = f4x4Mul(f4x4Mul(gizmoMX, f4x4Translation(f3Make(0, 0, offsetScale.x))), scaleCube);
    }

    // plane rects
    float    rectSize = pStyle->mSizePlaneRect;
    float    rectOffset = pStyle->mOffsetPlaneRect;
    float4x4 offsetAndScale = f4x4Mul(f4x4Translation(f3Make(0, rectOffset, rectOffset)), f4x4Scale(f3Make(0, rectSize, rectSize)));
    pWrite[H_RECT_XY_IDX].mTRS = f4x4Mul(gizmoMX, offsetAndScale);
    pWrite[H_RECT_XZ_IDX].mTRS = f4x4Mul(f4x4Mul(gizmoM, f4x4RotationZ(-PI / 2)), offsetAndScale);
    pWrite[H_RECT_YZ_IDX].mTRS = f4x4Mul(gizmoM, offsetAndScale);

    // white cube
    float sizeCube = pStyle->mSizeCenterCube;
    pWrite[H_CENTER_BOX_IDX].mTRS = f4x4Mul(gizmoM, f4x4Scale(f3Make(sizeCube, sizeCube, sizeCube)));
}

static void fillMatrixesRotateData(TFGizmo* pGizmo, const GizmoUpdateCameraDesc* pCameraDesc)
{
    GizmoHandlesStyleDesc* pStyle = (GizmoHandlesStyleDesc*)pGizmo->pStyleGizmo;
    GizmoInnerData*        pInnerData = (GizmoInnerData*)pGizmo->pGizmo;
    GizmoInstanceData*     pWrite = pInnerData->pWriteInstance;

    float3 pos = *pGizmo->pPosition;
    quat   rot = *pGizmo->pRotation;

    float    scale = pInnerData->mVisibility.mScaleForCamera;
    float4x4 gizmoTRS = f4x4Mul(f4x4Translation(pos), f4x4RotationQuat(rot));
    float3   gizmoPos = pos;
    float4x4 scaleM = f4x4Scale(f3Make(scale, scale, scale));

    float4x4 directionY = f4x4RotationX(-PI / 2);
    float4x4 directionX = f4x4RotationY(PI / 2);

    float4x4 gizmoM = f4x4Mul(gizmoTRS, scaleM);
    float4x4 gizmoMY = f4x4Mul(gizmoM, directionY);
    float4x4 gizmoMX = f4x4Mul(gizmoM, directionX);

    float    radiusMainAxis = pStyle->mRadiusMainAxis;
    float4x4 scaleLine = f4x4Scale(f3Make(0, 0, radiusMainAxis));
    float4x4 scaleCylinders = f4x4Scale(f3Make(radiusMainAxis, pStyle->mMainAxisThinkness, radiusMainAxis));
    pWrite[H_LINE_Z_IDX].mTRS = f4x4Mul(gizmoM, scaleLine);
    pWrite[H_LINE_Y_IDX].mTRS = f4x4Mul(gizmoMY, scaleLine);
    pWrite[H_LINE_X_IDX].mTRS = f4x4Mul(gizmoMX, scaleLine);

    pWrite[H_CONE_Z_IDX].mTRS = f4x4Mul(gizmoMY, scaleCylinders);
    pWrite[H_CONE_Y_IDX].mTRS = f4x4Mul(gizmoM, scaleCylinders);
    pWrite[H_CONE_X_IDX].mTRS = f4x4Mul(f4x4Mul(gizmoM, f4x4RotationZ(PI / 2)), scaleCylinders);

    float4x4 f4x4Zero = f4x4InitRows(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    if (pInnerData->mFrontAxis == GIZMO_AXIS_Z)
        pWrite[H_CONE_Z_IDX].mTRS = f4x4Zero;
    if (pInnerData->mFrontAxis == GIZMO_AXIS_Y)
        pWrite[H_CONE_Y_IDX].mTRS = f4x4Zero;
    if (pInnerData->mFrontAxis == GIZMO_AXIS_X)
        pWrite[H_CONE_X_IDX].mTRS = f4x4Zero;

    float3 dirToGizmo = f3Sub(gizmoPos, f4GetXYZ(f4x4Mulf4(pCameraDesc->mCameraLocalToWorld, f4Make(0, 0, 0, 1))));
    dirToGizmo = f3Normalize(dirToGizmo);
    float3 up;
    if (fabsf(f3Dot(dirToGizmo, f3Make(0, 1, 0))) < 0.1f)
        up = f3Make(1, 0, 0);
    else
        up = f3Make(0, 1, 0);

    float3 right = f3Cross(dirToGizmo, up);
    right = f3Normalize(right);
    up = f3Cross(right, dirToGizmo);
    up = f3Normalize(up);

    float3x3 rot3x3 = f3x3Identity();
    f3x3SetCol(&rot3x3, right, 0);
    f3x3SetCol(&rot3x3, dirToGizmo, 1);
    f3x3SetCol(&rot3x3, up, 2);
    float4x4 rotM = f4x4Identity();
    rotM = f4x4SetUpperf3x3(rotM, rot3x3);
    float4x4 circleM = f4x4Mul(f4x4Mul(f4x4Translation(gizmoPos), rotM), scaleM);

    float radiusFrontAxis = pStyle->mRadiusFrontAxis;
    float radiusOutsideCircle = pStyle->mRadiusOutsideAxis;

    pWrite[H_CIRCLE_FRONT_IDX].mTRS = f4x4Mul(circleM, f4x4Scale(f3Make(radiusFrontAxis, 0, radiusFrontAxis)));
    pWrite[H_CIRCLE_OUTSIDE_IDX].mTRS = f4x4Mul(circleM, f4x4Scale(f3Make(radiusOutsideCircle, 0, radiusOutsideCircle)));
}

static void fillColorData(TFGizmo* pGizmo)
{
    GizmoHandlesStyleDesc* pStyle = (GizmoHandlesStyleDesc*)pGizmo->pStyleGizmo;
    GizmoInnerData*        pInnerData = (GizmoInnerData*)pGizmo->pGizmo;
    GizmoInstanceData*     pWrite = pInnerData->pWriteInstance;

    uint32_t axisType = pInnerData->mHasContact ? pInnerData->mContact.mAxisType : 0;

    if ((axisType & GIZMO_AXIS_Z) != 0)
    {
        pWrite[H_LINE_Z_IDX].mColor = pStyle->mZAxisSelectedColor;
        pWrite[H_CONE_Z_IDX].mColor = pStyle->mZAxisSelectedColor;
    }
    else
    {
        pWrite[H_LINE_Z_IDX].mColor = pStyle->mZAxisNoselectedColor;
        pWrite[H_CONE_Z_IDX].mColor = pStyle->mZAxisNoselectedColor;
    }

    if ((axisType & GIZMO_AXIS_Y) != 0)
    {
        pWrite[H_LINE_Y_IDX].mColor = pStyle->mYAxisSelectedColor;
        pWrite[H_CONE_Y_IDX].mColor = pStyle->mYAxisSelectedColor;
    }
    else
    {
        pWrite[H_LINE_Y_IDX].mColor = pStyle->mYAxisNoselectedColor;
        pWrite[H_CONE_Y_IDX].mColor = pStyle->mYAxisNoselectedColor;
    }

    if ((axisType & GIZMO_AXIS_X) != 0)
    {
        pWrite[H_LINE_X_IDX].mColor = pStyle->mXAxisSelectedColor;
        pWrite[H_CONE_X_IDX].mColor = pStyle->mXAxisSelectedColor;
    }
    else
    {
        pWrite[H_LINE_X_IDX].mColor = pStyle->mXAxisNoselectedColor;
        pWrite[H_CONE_X_IDX].mColor = pStyle->mXAxisNoselectedColor;
    }

    if (axisType == GIZMO_AXIS_XY)
    {
        pWrite[H_RECT_XY_IDX].mColor = pStyle->mZAxisSelectedColor;
    }
    else
    {
        pWrite[H_RECT_XY_IDX].mColor = pStyle->mZAxisNoselectedColor;
    }

    if (axisType == GIZMO_AXIS_YZ)
    {
        pWrite[H_RECT_YZ_IDX].mColor = pStyle->mXAxisSelectedColor;
    }
    else
    {
        pWrite[H_RECT_YZ_IDX].mColor = pStyle->mXAxisNoselectedColor;
    }

    if (axisType == GIZMO_AXIS_XZ)
    {
        pWrite[H_RECT_XZ_IDX].mColor = pStyle->mYAxisSelectedColor;
    }
    else
    {
        pWrite[H_RECT_XZ_IDX].mColor = pStyle->mYAxisNoselectedColor;
    }

    const float alphaTransparent = 0.5f;

    for (uint32_t i = 0; i < H_RECT_COUNT; i++)
        pWrite[i + H_RECT_START].mColor.w = alphaTransparent;

    if (axisType == GIZMO_AXIS_XYZ)
    {
        pWrite[H_CENTER_BOX_START].mColor = pStyle->mCentralCubeSelectedColor;
    }
    else
    {
        pWrite[H_CENTER_BOX_START].mColor = pStyle->mCentralCubeNoselectedColor;
    }

    if (pInnerData->mState == TF_GIZMO_HANDLES_STATE_ROTATE)
    {
        pWrite[H_CIRCLE_FRONT_IDX].mColor = pStyle->mFrontAxisColor;

        if (pInnerData->mFrontAxis == GIZMO_AXIS_Z)
            pWrite[H_CIRCLE_FRONT_IDX].mColor = pWrite[H_LINE_Z_IDX].mColor;
        else if (pInnerData->mFrontAxis == GIZMO_AXIS_Y)
            pWrite[H_CIRCLE_FRONT_IDX].mColor = pWrite[H_LINE_Y_IDX].mColor;
        else if (pInnerData->mFrontAxis == GIZMO_AXIS_X)
            pWrite[H_CIRCLE_FRONT_IDX].mColor = pWrite[H_LINE_X_IDX].mColor;

        pWrite[H_CIRCLE_OUTSIDE_IDX].mColor = pStyle->mOutsideAxisColor;
    }
}

static void fillHandlesData(TFGizmo* pGizmo, const GizmoUpdateCameraDesc* pCameraDesc)
{
    GizmoInnerData* pInnerData = (GizmoInnerData*)pGizmo->pGizmo;
    fillColorData(pGizmo);
    if (pInnerData->mState == TF_GIZMO_HANDLES_STATE_ROTATE)
    {
        fillMatrixesRotateData(pGizmo, pCameraDesc);
    }
    else
    {
        fillMatrixesMoveScaleData(pGizmo);
    }
}

static void updateHandles(TFGizmo* pGizmo, const GizmoUpdateCameraDesc* pCameraDesc, const GizmoUpdateData* pData)
{
    GizmoInnerData* pInnerData = (GizmoInnerData*)pGizmo->pGizmo;

    calculateHandlesVisibility(pGizmo, pCameraDesc);
    if (pInnerData->mState == TF_GIZMO_HANDLES_STATE_ROTATE)
    {
        pInnerData->mFrontAxis = getFrontRotateAxis(pGizmo, pCameraDesc);
    }

    if (pData != NULL)
    {
        if (pGizmoSystem->pLockedGizmo == pGizmo)
        {
            if (pInnerData->mState == TF_GIZMO_HANDLES_STATE_MOVE)
            {
                moveGizmoHandles(pGizmo, pData);
                calculateHandlesVisibility(pGizmo, pCameraDesc); // -- position has been changed
            }
            else if (pInnerData->mState == TF_GIZMO_HANDLES_STATE_SCALE)
            {
                scaleGizmoHandles(pGizmo, pData);
            }
            else if (pInnerData->mState == TF_GIZMO_HANDLES_STATE_ROTATE)
            {
                rotateGizmoHandles(pGizmo, pData);
            }
        }
        else
        {
            pInnerData->mHasContact = false;

            if (pInnerData->mState != TF_GIZMO_HANDLES_STATE_NONE)
            {
                GizmoContactData newContact{};
                if (pInnerData->mState == TF_GIZMO_HANDLES_STATE_ROTATE)
                {
                    pInnerData->mHasContact = getActiveRotateAxis(pGizmo, pData, &newContact);
                }
                else
                {
                    pInnerData->mHasContact = getActiveMoveScaleAxis(pGizmo, pData, &newContact);
                }

                if (pInnerData->mHasContact)
                {
                    finishContactData(pData, &newContact);
                    pInnerData->mContact = newContact;
                    pInnerData->mScaleOffset = f3Make(1, 1, 1);
                    pInnerData->mStartScale = *pGizmo->pScale;
                    pInnerData->mStartRotation = *pGizmo->pRotation;
                }
            }
        }
    }
}

static void drawHandles(GizmoInnerData* pInnerData, TFCmd* pCmd, GizmoDrawQueue queue)
{
    TFBuffer* vertexBuffer = pGizmoSystem->pGizmoVertexBuffer;
    uint32_t  stride = sizeof(float4);
    uint64_t  offset = 0;
    uint32_t  startInstanceFrame = (*pGizmoSystem->pFrameIdx) * pInnerData->mCountInstance;

    if (queue == GIZMO_DRAW_QUEUE_OPAQUE)
    {
        if (pInnerData->mState == TF_GIZMO_HANDLES_STATE_MOVE || pInnerData->mState == TF_GIZMO_HANDLES_STATE_SCALE)
        {
            cmdBindPipeline(pCmd, pGizmoSystem->pGizmoStripLinesPipeline);
            cmdBindVertexBuffer(pCmd, 1, &vertexBuffer, &stride, &offset);

            TFIndirectDrawArguments line = pGizmoSystem->mGizmoHandlesMeshes.mLine;
            TFIndirectDrawArguments lineRect = pGizmoSystem->mGizmoHandlesMeshes.mLineRect;
            cmdBindDescriptorSet(pCmd, startInstanceFrame + line.mStartInstance, pInnerData->pDescriptorSetPerBatch);
            cmdDrawInstanced(pCmd, line.mVertexCount, line.mStartVertex, line.mInstanceCount, 0);

            cmdBindDescriptorSet(pCmd, startInstanceFrame + lineRect.mStartInstance, pInnerData->pDescriptorSetPerBatch);
            cmdDrawInstanced(pCmd, lineRect.mVertexCount, lineRect.mStartVertex, lineRect.mInstanceCount, 0);

            cmdBindPipeline(pCmd, pGizmoSystem->pGizmoOpaquePipeline);
            cmdBindVertexBuffer(pCmd, 1, &vertexBuffer, &stride, &offset);
            cmdBindIndexBuffer(pCmd, pGizmoSystem->pGizmoIndexBuffer, TF_INDEX_TYPE_UINT16, 0);
            if (pInnerData->mState == TF_GIZMO_HANDLES_STATE_MOVE)
            {
                TFIndirectDrawIndexArguments cone = pGizmoSystem->mGizmoHandlesMeshes.mCone;
                cmdBindDescriptorSet(pCmd, startInstanceFrame + cone.mStartInstance, pInnerData->pDescriptorSetPerBatch);
                cmdDrawIndexedInstanced(pCmd, cone.mIndexCount, cone.mStartIndex, cone.mInstanceCount, cone.mVertexOffset, 0);
            }
            else
            {
                TFIndirectDrawIndexArguments scaleBox = pGizmoSystem->mGizmoHandlesMeshes.mScaleBox;
                cmdBindDescriptorSet(pCmd, startInstanceFrame + scaleBox.mStartInstance, pInnerData->pDescriptorSetPerBatch);
                cmdDrawIndexedInstanced(pCmd, scaleBox.mIndexCount, scaleBox.mStartIndex, scaleBox.mInstanceCount, scaleBox.mVertexOffset,
                                        0);
            }

            TFIndirectDrawIndexArguments centerBox = pGizmoSystem->mGizmoHandlesMeshes.mCenterBox;
            cmdBindDescriptorSet(pCmd, startInstanceFrame + centerBox.mStartInstance, pInnerData->pDescriptorSetPerBatch);
            cmdDrawIndexedInstanced(pCmd, centerBox.mIndexCount, centerBox.mStartIndex, centerBox.mInstanceCount, centerBox.mVertexOffset,
                                    0);
        }
        else if (pInnerData->mState == TF_GIZMO_HANDLES_STATE_ROTATE)
        {
            cmdBindPipeline(pCmd, pGizmoSystem->pGizmoStripLinesPipeline);
            cmdBindVertexBuffer(pCmd, 1, &vertexBuffer, &stride, &offset);
            TFIndirectDrawArguments line = pGizmoSystem->mGizmoHandlesMeshes.mLine;
            cmdBindDescriptorSet(pCmd, startInstanceFrame + line.mStartInstance, pInnerData->pDescriptorSetPerBatch);
            cmdDrawInstanced(pCmd, line.mVertexCount, line.mStartVertex, line.mInstanceCount, 0);
            TFIndirectDrawArguments circle = pGizmoSystem->mGizmoHandlesMeshes.mCircle;
            cmdBindDescriptorSet(pCmd, startInstanceFrame + circle.mStartInstance, pInnerData->pDescriptorSetPerBatch);
            cmdDrawInstanced(pCmd, circle.mVertexCount, circle.mStartVertex, circle.mInstanceCount, 0);

            cmdBindPipeline(pCmd, pGizmoSystem->pGizmoOpaquePipeline);
            cmdBindVertexBuffer(pCmd, 1, &vertexBuffer, &stride, &offset);
            cmdBindIndexBuffer(pCmd, pGizmoSystem->pGizmoIndexBuffer, TF_INDEX_TYPE_UINT16, 0);
            TFIndirectDrawIndexArguments cylinder = pGizmoSystem->mGizmoHandlesMeshes.mCylinder;
            cmdBindDescriptorSet(pCmd, startInstanceFrame + cylinder.mStartInstance, pInnerData->pDescriptorSetPerBatch);
            cmdDrawIndexedInstanced(pCmd, cylinder.mIndexCount, cylinder.mStartIndex, cylinder.mInstanceCount, cylinder.mVertexOffset, 0);
        }
    }
    else
    {
        if (pInnerData->mState == TF_GIZMO_HANDLES_STATE_MOVE || pInnerData->mState == TF_GIZMO_HANDLES_STATE_SCALE)
        {
            cmdBindPipeline(pCmd, pGizmoSystem->pGizmoTransparentPipeline);
            cmdBindVertexBuffer(pCmd, 1, &vertexBuffer, &stride, &offset);
            TFIndirectDrawArguments rect = pGizmoSystem->mGizmoHandlesMeshes.mRect;
            cmdBindDescriptorSet(pCmd, startInstanceFrame + rect.mStartInstance, pInnerData->pDescriptorSetPerBatch);
            cmdDrawInstanced(pCmd, rect.mVertexCount, rect.mStartVertex, rect.mInstanceCount, 0);
        }
    }
}

typedef struct ALIGNAS(8) SerializedHandlesData
{
    quat                    mStartRotation;
    float3                  mScaleOffset;
    float3                  mStartScale;
    TFGizmoHandlesState     mState;
    TFGizmoHandlesScaleMode mScaleMode;
    GizmoAxis               mFrontAxis;
} SerializedHandlesData;

static size_t serializeHandles(GizmoInnerData* pInnerData, uint8_t* pBuffer, size_t size)
{
    if (!pBuffer)
    {
        return sizeof(SerializedHandlesData);
    }
    else
    {
        ASSERT(size >= sizeof(SerializedHandlesData));
    }

    SerializedHandlesData data{};
    data.mStartRotation = pInnerData->mStartRotation;
    data.mScaleOffset = pInnerData->mScaleOffset;
    data.mStartScale = pInnerData->mStartScale;
    data.mState = pInnerData->mState;
    data.mScaleMode = pInnerData->mScaleMode;
    data.mFrontAxis = pInnerData->mFrontAxis;
    memcpy(pBuffer, &data, sizeof(SerializedHandlesData));
    return sizeof(SerializedHandlesData);
};

static size_t deserializeHandles(GizmoInnerData* pInnerData, const uint8_t* pBuffer, size_t size)
{
    ASSERT(size >= sizeof(SerializedHandlesData));

    SerializedHandlesData data;
    memcpy(&data, pBuffer, sizeof(SerializedHandlesData));
    pInnerData->mStartRotation = data.mStartRotation;
    pInnerData->mScaleOffset = data.mScaleOffset;
    pInnerData->mStartScale = data.mStartScale;
    pInnerData->mState = data.mState;
    pInnerData->mScaleMode = data.mScaleMode;
    pInnerData->mFrontAxis = data.mFrontAxis;

    return sizeof(SerializedHandlesData);
}

/****************************************************************************/
// MARK: - Private Static Gizmo Bound
/****************************************************************************/

static void calculateBoundVisibility(TFGizmo* pGizmo, const GizmoUpdateCameraDesc* pCameraDesc)
{
    calculateVisibility(pGizmo, pCameraDesc, ((GizmoBoundStyleDesc*)pGizmo->pStyleGizmo)->mBoundPointScale);
}

static bool generateContactWithBoundPoint(const TFGizmo* pGizmo, const GizmoUpdateData* pData, uint32_t axisType, float3 pointPos,
                                          float3 pointDir, GizmoContactData* pContactData)
{
    GizmoInnerData* pInnerData = (GizmoInnerData*)pGizmo->pGizmo;
    float           pointScale = ((GizmoBoundStyleDesc*)pGizmo->pStyleGizmo)->mBoundPointScale * 2;

    float3 diff = f3Sub(pointPos, pData->mRayOrigin);
    float  projT = f3Dot(diff, pData->mRayDirection);
    float3 projection = f3Add(pData->mRayOrigin, f3MulScalar(pData->mRayDirection, projT));
    float  dist = f3LengthSqr(f3Sub(pointPos, projection));
    float  pointSize = pointScale * pInnerData->mVisibility.mFovTangent * f3Length(diff);

    if (pointSize * pointSize >= dist)
    {
        float3 segmentStart = f3Add(f3MulScalar(pointDir, -pointSize * 2), pointPos);
        float3 segmentEnd = f3Add(f3MulScalar(pointDir, pointSize * 2), pointPos);

        float  s = 0.0f;
        float  t = 0.0f;
        float3 c1 = f3Make(0, 0, 0);
        float3 c2 = f3Make(0, 0, 0);
        closestPointRaySegment(pData->mRayOrigin, pData->mRayDirection, segmentStart, segmentEnd, &s, &t, &c1, &c2);

        pContactData->mContactType = GIZMO_CONTACT_SEGMENT;
        pContactData->mAxisType = axisType;
        pContactData->mOrigin = pointPos;
        pContactData->mDirection = pointDir;
        pContactData->mMousePosition = pData->mMousePosition;
        pContactData->mScaleWithDistance = pointSize;
        pContactData->mScaleWithoutDistance = pointScale;
        pContactData->mContactPosition = c2;
        return true;
    }

    return false;
}

static bool getBoundPoint(const TFGizmo* pGizmo, uint32_t axisType, float3* pPoint, float3* pDirection)
{
    GizmoInnerData* pInnerData = (GizmoInnerData*)pGizmo->pGizmo;

    float3 extend = *pGizmo->pScale;
    float3 pos = *pGizmo->pPosition;
    float3 directions[3] = { f3Make(1, 0, 0), f3Make(0, 1, 0), f3Make(0, 0, 1) };

    if ((pInnerData->mBoundType == TF_GIZMO_BOUND_TYPE_CIRCLE || pInnerData->mBoundType == TF_GIZMO_BOUND_TYPE_RECT) &&
        (axisType == B_POINT_MIN_Y_IDX || axisType == B_POINT_MAX_Y_IDX))
    {
        return false;
    }

    float3 direction = quatRotateVector(*pGizmo->pRotation, directions[axisType % 3]);
    if (axisType < B_POINT_MAX_X_IDX)
    {
        direction = f3MulScalar(direction, -1);
    }

    float3 localPos = f3MulScalar(direction, (&extend.x)[axisType % 3]);
    if (pInnerData->mBoundType == TF_GIZMO_BOUND_TYPE_CONE && axisType != B_POINT_MIN_Y_IDX && axisType != B_POINT_MAX_Y_IDX)
    {
        float3 dirUp = quatRotateVector(*pGizmo->pRotation, directions[1]);
        localPos = f3Add(localPos, f3MulScalar(dirUp, extend.y));
    }
    *pDirection = direction;
    *pPoint = f3Add(pos, localPos);
    return true;
}

static bool generateContactBound(TFGizmo* pGizmo, const GizmoUpdateData* pData, GizmoContactData* pContactData)
{
    uint32_t offsetIndex = 0;
    for (int32_t i = 0; i < B_POINT_COUNT; i++)
    {
        float3 point;
        float3 direction;
        if (getBoundPoint(pGizmo, i, &point, &direction))
        {
            if (gizmoGetBoundPointEnable(pGizmo, offsetIndex) &&
                generateContactWithBoundPoint(pGizmo, pData, i, point, direction, pContactData))
            {
                return true;
            }
            else
            {
                offsetIndex++;
            }
        }
    }
    return false;
}

static void applyNewBoundExtend(const GizmoInnerData* pInnerData, uint32_t axisType, float* pExtend)
{
    TFGizmoBoundType type = pInnerData->mBoundType;

    uint32_t mainAxis = axisType % B_POINT_MAX_X_IDX;
    float    oppositValue;

    if (axisType >= B_POINT_MAX_X_IDX)
    {
        oppositValue = pExtend[axisType - B_POINT_MAX_X_IDX];
    }
    else
    {
        oppositValue = pExtend[axisType + B_POINT_MAX_X_IDX];
    }

    float changedValue = pExtend[axisType];

    if ((type == TF_GIZMO_BOUND_TYPE_CIRCLE || type == TF_GIZMO_BOUND_TYPE_CONE || type == TF_GIZMO_BOUND_TYPE_CYLINDER ||
         type == TF_GIZMO_BOUND_TYPE_CAPSULE || type == TF_GIZMO_BOUND_TYPE_SPHERE))
    {
        float radius = (fabsf(changedValue) + fabsf(oppositValue)) / 2.0f;
        if (type == TF_GIZMO_BOUND_TYPE_SPHERE)
        {
            if (mainAxis == B_POINT_MIN_X_IDX)
            {
                pExtend[B_POINT_MIN_Z_IDX] = -radius;
                pExtend[B_POINT_MAX_Z_IDX] = radius;
                pExtend[B_POINT_MIN_Y_IDX] = -radius;
                pExtend[B_POINT_MAX_Y_IDX] = radius;
            }
            else if (mainAxis == B_POINT_MIN_Y_IDX)
            {
                pExtend[B_POINT_MIN_X_IDX] = -radius;
                pExtend[B_POINT_MAX_X_IDX] = radius;
                pExtend[B_POINT_MIN_Z_IDX] = -radius;
                pExtend[B_POINT_MAX_Z_IDX] = radius;
            }
            else
            {
                pExtend[B_POINT_MIN_X_IDX] = -radius;
                pExtend[B_POINT_MAX_X_IDX] = radius;
                pExtend[B_POINT_MIN_Y_IDX] = -radius;
                pExtend[B_POINT_MAX_Y_IDX] = radius;
            }
        }
        else if (axisType != B_POINT_MIN_Y_IDX && axisType != B_POINT_MAX_Y_IDX)
        {
            if (mainAxis == B_POINT_MIN_X_IDX)
            {
                pExtend[B_POINT_MIN_Z_IDX] = -radius;
                pExtend[B_POINT_MAX_Z_IDX] = radius;
            }
            else
            {
                pExtend[B_POINT_MIN_X_IDX] = -radius;
                pExtend[B_POINT_MAX_X_IDX] = radius;
            }
        }
    }

    // correct height for capsule
    if (type == TF_GIZMO_BOUND_TYPE_CAPSULE)
    {
        float height = pExtend[B_POINT_MAX_Y_IDX] - pExtend[B_POINT_MIN_Y_IDX];
        if (mainAxis == B_POINT_MIN_Y_IDX)
        {
            float diameter = pExtend[B_POINT_MAX_X_IDX] - pExtend[B_POINT_MIN_X_IDX];
            if (height < diameter)
            {
                if (axisType >= B_POINT_MAX_X_IDX)
                {
                    pExtend[axisType] = pExtend[B_POINT_MAX_X_IDX];
                }
                else
                {
                    pExtend[axisType] = pExtend[B_POINT_MIN_X_IDX];
                }
            }
        }
        else
        {
            float diameter = (fabsf(changedValue) + fabsf(oppositValue));
            float radius = diameter / 2.0f;
            if (diameter > height)
            {
                pExtend[B_POINT_MAX_Y_IDX] = radius;
                pExtend[B_POINT_MIN_Y_IDX] = -radius;
            }
        }
    }

    // reset -y & +y
    if (type == TF_GIZMO_BOUND_TYPE_CIRCLE || type == TF_GIZMO_BOUND_TYPE_RECT)
    {
        pExtend[B_POINT_MIN_Y_IDX] = pExtend[B_POINT_MAX_Y_IDX] = 0.0f;
    }
}

static void correctBoundExtend(TFGizmo* pGizmo)
{
    TFGizmoBoundType type = gizmoGetBoundType(pGizmo);
    float3           extend = *pGizmo->pScale;

    if (type == TF_GIZMO_BOUND_TYPE_CIRCLE || type == TF_GIZMO_BOUND_TYPE_CONE || type == TF_GIZMO_BOUND_TYPE_CYLINDER ||
        type == TF_GIZMO_BOUND_TYPE_CAPSULE)
    {
        float radius = fmaxf(extend.x, extend.z);
        extend.x = radius;
        extend.z = radius;

        if (type == TF_GIZMO_BOUND_TYPE_CAPSULE)
        {
            float height = fmaxf(extend.y, radius);
            extend.y = height;
        }
    }

    if (type == TF_GIZMO_BOUND_TYPE_SPHERE)
    {
        float radius = fmaxf(extend.x, fmaxf(extend.y, extend.z));
        extend = f3Make(radius, radius, radius);
    }

    // reset y
    if (type == TF_GIZMO_BOUND_TYPE_CIRCLE || type == TF_GIZMO_BOUND_TYPE_RECT)
    {
        extend.y = 0.0f;
    }

    changeGizmoScale(pGizmo, extend);
}

static void extendGizmoBound(TFGizmo* pGizmo, const GizmoUpdateData* pData)
{
    GizmoInnerData* pInnerData = (GizmoInnerData*)pGizmo->pGizmo;

    uint32_t boundSide = pInnerData->mContact.mAxisType;
    float3   newPoint;
    if (moveContact(&pInnerData->mContact, pData, &newPoint))
    {
        float3 localPoint;
        transformWorldToLocal(pGizmo, newPoint, &localPoint);

        float3 extend = *pGizmo->pScale;
        float3 pos = *pGizmo->pPosition;

        float extends[6] = { -extend.x, -extend.y, -extend.z, extend.x, extend.y, extend.z };
        extends[boundSide] = (&localPoint.x)[boundSide % 3];

        if (boundSide >= 3)
        {
            extends[boundSide] = fmaxf(extends[boundSide - 3], extends[boundSide]);
        }
        else
        {
            extends[boundSide] = fminf(extends[boundSide + 3], extends[boundSide]);
        }

        applyNewBoundExtend(pInnerData, boundSide, extends);

        float3 min = f3Make(extends[0], extends[1], extends[2]);
        float3 max = f3Make(extends[3], extends[4], extends[5]);

        float3 newExtend = f3DivScalar(f3Sub(max, min), 2);
        float3 offset = f3DivScalar(f3Add(max, min), 2);

        float3 globalOffset = quatRotateVector(*pGizmo->pRotation, offset);
        changeGizmoPosition(pGizmo, f3Add(pos, globalOffset));
        changeGizmoScale(pGizmo, newExtend);
    }
}

static void fillBoundData(TFGizmo* pGizmo, const GizmoUpdateCameraDesc* pCameraDesc)
{
    GizmoBoundStyleDesc* pStyle = (GizmoBoundStyleDesc*)pGizmo->pStyleGizmo;
    GizmoInnerData*      pInnerData = (GizmoInnerData*)pGizmo->pGizmo;

    GizmoInstanceData* pWrite = pInnerData->pWriteInstance;
    float3             pos = *pGizmo->pPosition;
    quat               rot = *pGizmo->pRotation;
    float4x4           rotM = f4x4RotationQuat(rot);
    float3             extend = *pGizmo->pScale;

    pWrite[B_VOLUME_IDX].mColor = pStyle->mBoundNoselectedColor;

    if (pInnerData->mBoundType == TF_GIZMO_BOUND_TYPE_CAPSULE)
    {
        float radius = extend.x;
        float halfHeight = extend.y;

        float3 cylinderScale = f3Make(radius, halfHeight - radius, radius);
        pWrite[B_VOLUME_IDX].mTRS = f4x4Mul(f4x4Translation(pos), f4x4Mul(rotM, f4x4Scale(cylinderScale)));

        float3 halfSphereScale = f3Make(radius, radius, radius);
        float3 up = f3MulScalar(quatRotateVector(rot, f3Make(0, 1, 0)), cylinderScale.y);

        float3 tipUpPos = f3Add(pos, up);
        pWrite[B_CAPSULE_TIP_UP_IDX].mTRS = f4x4Mul(f4x4Translation(tipUpPos), f4x4Mul(rotM, f4x4Scale(halfSphereScale)));
        float3 tipDownPos = f3Sub(pos, up);

        float4x4 rotTipDown = f4x4RotationX(PI);

        pWrite[B_CAPSULE_TIP_DOWN_IDX].mTRS =
            f4x4Mul(f4x4Translation(tipDownPos), f4x4Mul(f4x4Mul(rotM, rotTipDown), f4x4Scale(halfSphereScale)));

        pWrite[B_CAPSULE_TIP_UP_IDX].mColor = pStyle->mBoundNoselectedColor;
        pWrite[B_CAPSULE_TIP_DOWN_IDX].mColor = pStyle->mBoundNoselectedColor;
    }
    else
    {
        pWrite[B_VOLUME_IDX].mTRS = f4x4Mul(f4x4Translation(pos), f4x4Mul(rotM, f4x4Scale(extend)));
    }

    float    pointScale = pStyle->mBoundPointScale * pCameraDesc->mFovTangent;
    uint32_t offsetIndex = 0;
    for (uint32_t i = 0; i < B_POINT_COUNT; i++)
    {
        float3 point;
        float3 direction;
        if (getBoundPoint(pGizmo, i, &point, &direction))
        {
            if (!gizmoGetBoundPointEnable(pGizmo, offsetIndex))
            {
                memset(&pWrite[offsetIndex], 0, sizeof(GizmoInstanceData));
            }
            else
            {
                pWrite[offsetIndex].mTRS = makePointInstanceMatrix(point, pointScale);
                if (pInnerData->mHasContact && pInnerData->mContact.mAxisType == i)
                {
                    pWrite[offsetIndex].mColor = pStyle->mBoundColorSelected;
                }
                else
                {
                    pWrite[offsetIndex].mColor = pStyle->mBoundNoselectedColor;
                }
            }
            offsetIndex++;
        }
    }
}

static void updateBound(TFGizmo* pGizmo, const GizmoUpdateCameraDesc* pCameraDesc, const GizmoUpdateData* pData)
{
    GizmoInnerData* pInnerData = (GizmoInnerData*)pGizmo->pGizmo;

    calculateBoundVisibility(pGizmo, pCameraDesc);
    correctBoundExtend(pGizmo);

    if (pData != NULL)
    {
        if (pGizmoSystem->pLockedGizmo == pGizmo)
        {
            extendGizmoBound(pGizmo, pData);
            calculateBoundVisibility(pGizmo, pCameraDesc); // -- position has been changed
        }
        else
        {
            GizmoContactData newContact{};
            pInnerData->mHasContact = generateContactBound(pGizmo, pData, &newContact);

            if (pInnerData->mHasContact)
            {
                finishContactData(pData, &newContact);
                pInnerData->mContact = newContact;
            }
        }
    }
}

static void drawBound(GizmoInnerData* pInnerData, TFCmd* pCmd, GizmoDrawQueue queue)
{
    if (queue != GIZMO_DRAW_QUEUE_OPAQUE)
        return;

    TFBuffer* vertexBuffer = pGizmoSystem->pGizmoVertexBuffer;
    uint32_t  stride = sizeof(float4);
    uint64_t  offset = 0;
    uint32_t  startInstanceFrame = (*pGizmoSystem->pFrameIdx) * pInnerData->mCountInstance;

    if (pInnerData->mBoundType == TF_GIZMO_BOUND_TYPE_CIRCLE || pInnerData->mBoundType == TF_GIZMO_BOUND_TYPE_RECT)
    {
        cmdBindPipeline(pCmd, pGizmoSystem->pGizmoStripLinesPipeline);
    }
    else
    {
        cmdBindPipeline(pCmd, pGizmoSystem->pGizmoListLinesPipeline);
    }

    cmdBindIndexBuffer(pCmd, pGizmoSystem->pGizmoIndexBuffer, TF_INDEX_TYPE_UINT16, 0);
    cmdBindVertexBuffer(pCmd, 1, &vertexBuffer, &stride, &offset);

    if (pInnerData->mBoundType == TF_GIZMO_BOUND_TYPE_BOX)
    {
        TFIndirectDrawIndexArguments lineCube = pGizmoSystem->mGizmoBoundMeshes.mCube;
        cmdBindDescriptorSet(pCmd, startInstanceFrame + lineCube.mStartInstance, pInnerData->pDescriptorSetPerBatch);
        cmdDrawIndexedInstanced(pCmd, lineCube.mIndexCount, lineCube.mStartIndex, lineCube.mInstanceCount, lineCube.mVertexOffset, 0);
    }
    else if (pInnerData->mBoundType == TF_GIZMO_BOUND_TYPE_RECT)
    {
        TFIndirectDrawArguments rect = pGizmoSystem->mGizmoBoundMeshes.mLineCenteredRect;
        cmdBindDescriptorSet(pCmd, startInstanceFrame + rect.mStartInstance, pInnerData->pDescriptorSetPerBatch);
        cmdDrawInstanced(pCmd, rect.mVertexCount, rect.mStartVertex, rect.mInstanceCount, 0);
    }
    else if (pInnerData->mBoundType == TF_GIZMO_BOUND_TYPE_CIRCLE)
    {
        TFIndirectDrawArguments circle = pGizmoSystem->mGizmoBoundMeshes.mCircle;
        cmdBindDescriptorSet(pCmd, startInstanceFrame + circle.mStartInstance, pInnerData->pDescriptorSetPerBatch);
        cmdDrawInstanced(pCmd, circle.mVertexCount, circle.mStartVertex, circle.mInstanceCount, 0);
    }
    else if (pInnerData->mBoundType == TF_GIZMO_BOUND_TYPE_CONE)
    {
        TFIndirectDrawIndexArguments lineCone = pGizmoSystem->mGizmoBoundMeshes.mCone;
        cmdBindDescriptorSet(pCmd, startInstanceFrame + lineCone.mStartInstance, pInnerData->pDescriptorSetPerBatch);
        cmdDrawIndexedInstanced(pCmd, lineCone.mIndexCount, lineCone.mStartIndex, lineCone.mInstanceCount, lineCone.mVertexOffset, 0);
    }
    else if (pInnerData->mBoundType == TF_GIZMO_BOUND_TYPE_SPHERE)
    {
        TFIndirectDrawIndexArguments lineSphere = pGizmoSystem->mGizmoBoundMeshes.mSphere;
        cmdBindDescriptorSet(pCmd, startInstanceFrame + lineSphere.mStartInstance, pInnerData->pDescriptorSetPerBatch);
        cmdDrawIndexedInstanced(pCmd, lineSphere.mIndexCount, lineSphere.mStartIndex, lineSphere.mInstanceCount, lineSphere.mVertexOffset,
                                0);
    }
    else if (pInnerData->mBoundType == TF_GIZMO_BOUND_TYPE_CYLINDER)
    {
        TFIndirectDrawIndexArguments lineCylinder = pGizmoSystem->mGizmoBoundMeshes.mCylinder;
        cmdBindDescriptorSet(pCmd, startInstanceFrame + lineCylinder.mStartInstance, pInnerData->pDescriptorSetPerBatch);
        cmdDrawIndexedInstanced(pCmd, lineCylinder.mIndexCount, lineCylinder.mStartIndex, lineCylinder.mInstanceCount,
                                lineCylinder.mVertexOffset, 0);
    }
    else
    {
        TFIndirectDrawIndexArguments lineCylinder = pGizmoSystem->mGizmoBoundMeshes.mCylinder;
        cmdBindDescriptorSet(pCmd, startInstanceFrame + lineCylinder.mStartInstance, pInnerData->pDescriptorSetPerBatch);

        cmdDrawIndexedInstanced(pCmd, lineCylinder.mIndexCount, lineCylinder.mStartIndex, lineCylinder.mInstanceCount,
                                lineCylinder.mVertexOffset, 0);
        TFIndirectDrawIndexArguments lineHalfSphere = pGizmoSystem->mGizmoBoundMeshes.mHalfSphere;
        cmdBindDescriptorSet(pCmd, startInstanceFrame + lineHalfSphere.mStartInstance, pInnerData->pDescriptorSetPerBatch);
        cmdDrawIndexedInstanced(pCmd, lineHalfSphere.mIndexCount, lineHalfSphere.mStartIndex, lineHalfSphere.mInstanceCount,
                                lineHalfSphere.mVertexOffset, 0);
    }

    TFIndirectDrawArguments point = pGizmoSystem->mGizmoBoundMeshes.mPoints;

    cmdBindPipeline(pCmd, pGizmoSystem->pGizmoFaceOpaquePipeline);
    cmdBindVertexBuffer(pCmd, 1, &vertexBuffer, &stride, &offset);
    cmdBindDescriptorSet(pCmd, startInstanceFrame + point.mStartInstance, pInnerData->pDescriptorSetPerBatch);
    cmdDrawInstanced(pCmd, point.mVertexCount, point.mStartVertex, gBoundPointsCount[pInnerData->mBoundType], 0);
}

typedef struct ALIGNAS(8) SerializedBoundData
{
    uint32_t         mMaskDisabledPoints;
    TFGizmoBoundType mBoundType;
} SerializedBoundData;

static size_t serializeBound(GizmoInnerData* pInnerData, uint8_t* pBuffer, size_t size)
{
    if (!pBuffer)
    {
        return sizeof(SerializedBoundData);
    }
    else
    {
        ASSERT(size >= sizeof(SerializedBoundData));
    }

    SerializedBoundData data{};
    data.mMaskDisabledPoints = pInnerData->mMaskDisabledPoints;
    data.mBoundType = pInnerData->mBoundType;
    memcpy(pBuffer, &data, sizeof(SerializedBoundData));
    return sizeof(SerializedBoundData);
};

static size_t deserializeBound(GizmoInnerData* pInnerData, const uint8_t* pBuffer, size_t size)
{
    ASSERT(size >= sizeof(SerializedBoundData));

    SerializedBoundData data;
    memcpy(&data, pBuffer, sizeof(SerializedBoundData));
    pInnerData->mBoundType = data.mBoundType;
    pInnerData->mMaskDisabledPoints = data.mMaskDisabledPoints;
    return sizeof(SerializedBoundData);
}

/****************************************************************************/
// MARK: - Private Static Gizmo Renderer
/****************************************************************************/

static void addInnerDataGizmoRenderer(GizmoInnerData* pInnerData)
{
    TFDescriptorSetDesc perDraw = SRT_SET_DESC(GizmoSrt, PerDraw, pGizmoSystem->mFrameMaxCount, 0);
    addDescriptorSet(pGizmoSystem->pRenderer, &perDraw, &pInnerData->mRenderer.pDescriptorSetPerDraw);

    pInnerData->mRenderer.pBuffers = (GizmoRendererBuffers*)tf_calloc(pGizmoSystem->mFrameMaxCount, sizeof(GizmoRendererBuffers));
    memset(pInnerData->mRenderer.pBuffers, 0, pGizmoSystem->mFrameMaxCount * sizeof(GizmoRendererBuffers));
}

static void removeGizmoRendererBuffers(GizmoRendererBuffers* pBuffers)
{
    if (pBuffers->pGizmoLocalInstanceBuffer)
    {
        removeResource_Buffer(pBuffers->pGizmoLocalInstanceBuffer);
        pBuffers->pGizmoLocalInstanceBuffer = NULL;
    }

    if (pBuffers->pGizmoVertexLineBuffer)
    {
        removeResource_Buffer(pBuffers->pGizmoVertexLineBuffer);
        pBuffers->pGizmoVertexLineBuffer = NULL;
    }

    if (pBuffers->pGizmoVertexTriangleBuffer)
    {
        removeResource_Buffer(pBuffers->pGizmoVertexTriangleBuffer);
        pBuffers->pGizmoVertexTriangleBuffer = NULL;
    }

    if (pBuffers->pGizmoIndexTriangleBuffer)
    {
        removeResource_Buffer(pBuffers->pGizmoIndexTriangleBuffer);
        pBuffers->pGizmoIndexTriangleBuffer = NULL;
    }

    pBuffers->mInstanceCount = 0;
    pBuffers->mVertexLineCount = 0;
    pBuffers->mVertexTriangleCount = 0;
    pBuffers->mIndexTriangleCount = 0;
}

static void removeGizmoRendererCache(GizmoInnerData* pInnerData)
{
    GizmoRendererCache* cache = &pInnerData->mRenderer.mCache;

    arrfree(cache->pInstances);
    arrfree(cache->pVertexLines);
    arrfree(cache->pVertexTriangles);
    arrfree(cache->pIndexTriangles);
}

static void removeInnerDataGizmoRenderer(GizmoInnerData* pInnerData)
{
    for (uint32_t i = 0; i < pGizmoSystem->mFrameMaxCount; i++)
    {
        removeGizmoRendererBuffers(pInnerData->mRenderer.pBuffers + i);
    }

    tf_free(pInnerData->mRenderer.pBuffers);
    removeGizmoRendererCache(pInnerData);

    removeDescriptorSet(pGizmoSystem->pRenderer, pInnerData->mRenderer.pDescriptorSetPerDraw);
}

static void fillRendererData(TFGizmo* pGizmo, const GizmoUpdateCameraDesc* pCameraDesc)
{
    UNREF_PARAM(pCameraDesc);

    GizmoInnerData*    pInnerData = (GizmoInnerData*)pGizmo->pGizmo;
    GizmoInstanceData* pWrite = pInnerData->pWriteInstance;

    if (pGizmo->pPosition != NULL && pGizmo->pRotation != NULL && pGizmo->pScale != NULL)
    {
        float3 pos = *pGizmo->pPosition;
        quat   rot = *pGizmo->pRotation;
        float3 scale = *pGizmo->pScale;

        pWrite[0].mTRS = f4x4Mul(f4x4Translation(pos), f4x4Mul(f4x4RotationQuat(rot), f4x4Scale(scale)));
    }
    else
    {
        pWrite[0].mTRS = f4x4Identity();
    }

    pWrite[0].mColor = f4Make(1, 1, 1, 1);
}

static void updateGizmoRendererBuffers(GizmoInnerData* pInnerData, uint32_t frameIdx)
{
    GizmoRendererBuffers* buffers = pInnerData->mRenderer.pBuffers + frameIdx;
    GizmoRendererCache*   cache = &pInnerData->mRenderer.mCache;

    if (buffers->mRequiresCleaning)
    {
        removeGizmoRendererBuffers(buffers);
        buffers->mRequiresCleaning = false;
    }

    if (buffers->mIsObsolete && arrlen(cache->pInstances))
    {
        TFBufferLoadDesc bufferDesc = { 0 };
        bufferDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
        bufferDesc.mDesc.mFlags = TF_BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
        bufferDesc.pData = NULL;

        bool updatedBuffer = false;

        uint32_t sizeForInstances = (uint32_t)maxu(1, arrlen(cache->pInstances)) * sizeof(GizmoInstanceData);
        if (!buffers->pGizmoLocalInstanceBuffer || sizeForInstances > buffers->pGizmoLocalInstanceBuffer->mSize)
        {
            if (buffers->pGizmoLocalInstanceBuffer)
            {
                removeResource_Buffer(buffers->pGizmoLocalInstanceBuffer);
            }

            bufferDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_BUFFER;
            bufferDesc.mDesc.mSize = sizeForInstances;
            bufferDesc.mDesc.mFormat = TinyImageFormat_R32G32B32A32_SFLOAT;
            bufferDesc.mDesc.mElementCount = sizeForInstances / sizeof(float4);
            bufferDesc.mDesc.pName = "GizmoLocalInstanceBuffer";
            bufferDesc.ppBuffer = &buffers->pGizmoLocalInstanceBuffer;
            bufferDesc.pData = cache->pInstances;
            addResource_Buffer(&bufferDesc, NULL);

            updatedBuffer = true;
        }
        else
        {
            TFBufferUpdateDesc instanceBuffer = { buffers->pGizmoLocalInstanceBuffer, 0, sizeForInstances };
            beginUpdateResource_Buffer(&instanceBuffer);
            memcpy(instanceBuffer.pMappedData, cache->pInstances, sizeForInstances);
            endUpdateResource_Buffer(&instanceBuffer);
        }

        uint32_t sizeForVertexLine = (uint32_t)maxu(1, arrlen(cache->pVertexLines)) * sizeof(float4);
        if (!buffers->pGizmoVertexLineBuffer || sizeForVertexLine > buffers->pGizmoVertexLineBuffer->mSize)
        {
            if (buffers->pGizmoVertexLineBuffer)
            {
                removeResource_Buffer(buffers->pGizmoVertexLineBuffer);
            }

            bufferDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_VERTEX_BUFFER;
            bufferDesc.mDesc.mSize = sizeForVertexLine;
            bufferDesc.mDesc.pName = "GizmoRendererVertexLineBuffer";
            bufferDesc.ppBuffer = &buffers->pGizmoVertexLineBuffer;
            bufferDesc.pData = cache->pVertexLines;
            addResource_Buffer(&bufferDesc, NULL);

            updatedBuffer = true;
        }
        else if (cache->pVertexLines)
        {
            TFBufferUpdateDesc vertexLineBuffer = { buffers->pGizmoVertexLineBuffer, 0, sizeForVertexLine };
            beginUpdateResource_Buffer(&vertexLineBuffer);
            memcpy(vertexLineBuffer.pMappedData, cache->pVertexLines, sizeForVertexLine);
            endUpdateResource_Buffer(&vertexLineBuffer);
        }

        uint32_t sizeForVertexTriangle = (uint32_t)maxu(1, arrlen(cache->pVertexTriangles)) * sizeof(float4);
        if (!buffers->pGizmoVertexTriangleBuffer || sizeForVertexTriangle > buffers->pGizmoVertexTriangleBuffer->mSize)
        {
            if (buffers->pGizmoVertexTriangleBuffer)
            {
                removeResource_Buffer(buffers->pGizmoVertexTriangleBuffer);
            }

            bufferDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_VERTEX_BUFFER;
            bufferDesc.mDesc.mSize = sizeForVertexTriangle;
            bufferDesc.mDesc.pName = "GizmoRendererVertexTriangleBuffer";
            bufferDesc.ppBuffer = &buffers->pGizmoVertexTriangleBuffer;
            bufferDesc.pData = cache->pVertexTriangles;
            addResource_Buffer(&bufferDesc, NULL);

            updatedBuffer = true;
        }
        else if (cache->pVertexTriangles)
        {
            TFBufferUpdateDesc vertexTriangleBuffer = { buffers->pGizmoVertexTriangleBuffer, 0, sizeForVertexTriangle };
            beginUpdateResource_Buffer(&vertexTriangleBuffer);
            memcpy(vertexTriangleBuffer.pMappedData, cache->pVertexTriangles, sizeForVertexTriangle);
            endUpdateResource_Buffer(&vertexTriangleBuffer);
        }

        uint32_t sizeForIndexTriangle = (uint32_t)maxu(1, arrlen(cache->pIndexTriangles)) * sizeof(uint16_t);
        if (!buffers->pGizmoIndexTriangleBuffer || sizeForIndexTriangle > buffers->pGizmoIndexTriangleBuffer->mSize)
        {
            if (buffers->pGizmoIndexTriangleBuffer)
            {
                removeResource_Buffer(buffers->pGizmoIndexTriangleBuffer);
            }

            bufferDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_INDEX_BUFFER;
            bufferDesc.mDesc.mSize = sizeForIndexTriangle;
            bufferDesc.mDesc.pName = "GizmoRendererIndexTriangleBuffer";
            bufferDesc.ppBuffer = &buffers->pGizmoIndexTriangleBuffer;
            bufferDesc.pData = cache->pIndexTriangles;
            addResource_Buffer(&bufferDesc, NULL);

            updatedBuffer = true;
        }
        else if (cache->pIndexTriangles)
        {
            TFBufferUpdateDesc indexTriangleBuffer = { buffers->pGizmoIndexTriangleBuffer, 0, sizeForIndexTriangle };
            beginUpdateResource_Buffer(&indexTriangleBuffer);
            memcpy(indexTriangleBuffer.pMappedData, cache->pIndexTriangles, sizeForIndexTriangle);
            endUpdateResource_Buffer(&indexTriangleBuffer);
        }

        if (updatedBuffer)
        {
            waitForAllResourceLoads();
        }

        TFDescriptorData uParams = { 0 };
        uParams.mIndex = 0;
        uParams.ppBuffers = &buffers->pGizmoLocalInstanceBuffer;
        updateDescriptorSet(pGizmoSystem->pRenderer, frameIdx, pInnerData->mRenderer.pDescriptorSetPerDraw, 1, &uParams);

        buffers->mInstanceCount = (uint32_t)arrlen(cache->pInstances);
        buffers->mVertexLineCount = (uint32_t)arrlen(cache->pVertexLines);
        buffers->mVertexTriangleCount = (uint32_t)arrlen(cache->pVertexTriangles);
        buffers->mIndexTriangleCount = (uint32_t)arrlen(cache->pIndexTriangles);

        buffers->mIsObsolete = false;
    }
}

static void gizmoRendererUpdateInstance(GizmoRendererCache* pCache)
{
    int32_t instanceActive = pCache->mInstanceActive;
    bool    makeNewInstance = pCache->isUsedInstance || instanceActive == -1;
    if (makeNewInstance)
    {
        uint32_t          instanceIdx = (uint32_t)arrlen(pCache->pInstances);
        GizmoInstanceData newInstance{};
        arrpush(pCache->pInstances, newInstance);

        if (instanceActive != -1)
        {
            pCache->pInstances[instanceIdx] = pCache->pInstances[instanceActive];
        }

        pCache->mInstanceActive = instanceIdx;
    }
}

static void drawRenderer(GizmoInnerData* pInnerData, TFCmd* pCmd, GizmoDrawQueue queue)
{
    if (queue != GIZMO_DRAW_QUEUE_OPAQUE)
        return;

    uint32_t frameIdx = *pGizmoSystem->pFrameIdx;
    updateGizmoRendererBuffers(pInnerData, frameIdx);

    GizmoRendererBuffers* buffers = pInnerData->mRenderer.pBuffers + frameIdx;

    if (!buffers->mInstanceCount)
        return;

    uint32_t stride = sizeof(float4);
    uint64_t offset = 0;
    uint32_t startInstanceFrame = frameIdx * pInnerData->mCountInstance;

    if (buffers->mVertexLineCount)
    {
        TFBuffer* vertexBuffer = buffers->pGizmoVertexLineBuffer;

        cmdBindPipeline(pCmd, pGizmoSystem->pGizmoLocalInstanceLinesPipeline);
        cmdBindDescriptorSet(pCmd, frameIdx, pInnerData->mRenderer.pDescriptorSetPerDraw);

        cmdBindVertexBuffer(pCmd, 1, &vertexBuffer, &stride, &offset);
        cmdBindDescriptorSet(pCmd, startInstanceFrame, pInnerData->pDescriptorSetPerBatch);
        cmdDraw(pCmd, buffers->mVertexLineCount, 0);
    }

    if (buffers->mIndexTriangleCount)
    {
        TFBuffer* vertexBuffer = buffers->pGizmoVertexTriangleBuffer;

        cmdBindPipeline(pCmd, pGizmoSystem->pGizmoLocalInstancePipeline);
        cmdBindDescriptorSet(pCmd, frameIdx, pInnerData->mRenderer.pDescriptorSetPerDraw);

        cmdBindVertexBuffer(pCmd, 1, &vertexBuffer, &stride, &offset);
        cmdBindIndexBuffer(pCmd, buffers->pGizmoIndexTriangleBuffer, TF_INDEX_TYPE_UINT16, 0);
        cmdBindDescriptorSet(pCmd, startInstanceFrame, pInnerData->pDescriptorSetPerBatch);
        cmdDrawIndexed(pCmd, buffers->mIndexTriangleCount, 0, 0);
    }
}

/****************************************************************************/
// MARK: - Private Static Gizmo Grid
/****************************************************************************/

static void fillGridData(TFGizmo* pGizmo, const GizmoUpdateCameraDesc* pCameraDesc)
{
    GizmoInnerData*     pInnerData = (GizmoInnerData*)pGizmo->pGizmo;
    GizmoInstanceData*  pWrite = pInnerData->pWriteInstance;
    GizmoGridStyleDesc* pGridStyle = (GizmoGridStyleDesc*)pGizmo->pStyleGizmo;

    float3 cameraPos = f4GetXYZ(pCameraDesc->mCameraLocalToWorld.v[3]);
    float  height = fabsf(cameraPos.y);
    float  main = fmaxf(1.0f, ceilf(log10f(height)));
    float  second = main - 1.0f;

    float mainHeight = powf(10, main);
    float secondHeight = powf(10, second);

    const uint32_t sizeGrid = 100;
    float          stepMain = mainHeight * 2.0f;
    float          stepSecond = mainHeight * 2.0f;

    float3 gridMainPos = f3Make(ceilf(cameraPos.x / stepMain) * stepMain, 0.0f, ceilf(cameraPos.z / stepMain) * stepMain);
    float3 gridSecondPos = f3Make(ceilf(cameraPos.x / stepSecond) * stepSecond, 0.0f, ceilf(cameraPos.z / stepSecond) * stepSecond);

    const float gridBias = 0.001f;
    gridMainPos = f3Add(gridMainPos, f3Make(0, mainHeight * gridBias * sign(cameraPos.y), 0));

    pWrite[0].mTRS = f4x4Mul(f4x4Translation(gridSecondPos),
                             f4x4Mul(f4x4RotationQuat(quatIdentity()), f4x4Scale(f3MakeScalar(secondHeight * sizeGrid))));
    pWrite[1].mTRS =
        f4x4Mul(f4x4Translation(gridMainPos), f4x4Mul(f4x4RotationQuat(quatIdentity()), f4x4Scale(f3MakeScalar(mainHeight * sizeGrid))));

    float fadeDist = sqrtf(height * height * 2) * pGridStyle->mFadeHeightMultiply;
    pWrite[0].mTRS.v[3].w = fadeDist;
    pWrite[1].mTRS.v[3].w = fadeDist;

    float intensityMain = (height - secondHeight) / (mainHeight - secondHeight);
    float intensitySecond = 1.0f - intensityMain;

    pWrite[0].mColor = f4Fromf3(f4GetXYZ(pGridStyle->mColorGrid), intensitySecond * pGridStyle->mColorGrid.w);
    pWrite[1].mColor = f4Fromf3(f4GetXYZ(pGridStyle->mColorGrid), intensityMain * pGridStyle->mColorGrid.w);
}

static void drawGrid(GizmoInnerData* pInnerData, TFCmd* pCmd, GizmoDrawQueue queue)
{
    if (queue == GIZMO_DRAW_QUEUE_OPAQUE)
        return;

    uint32_t stride = sizeof(float4);
    uint64_t offset = 0;
    uint32_t startInstanceFrame = (*pGizmoSystem->pFrameIdx) * pInnerData->mCountInstance;

    TFBuffer* vertexBuffer = pGizmoSystem->pGizmoVertexBuffer;

    cmdBindPipeline(pCmd, pGizmoSystem->pGizmoGridPipeline);
    cmdBindVertexBuffer(pCmd, 1, &vertexBuffer, &stride, &offset);
    cmdBindDescriptorSet(pCmd, startInstanceFrame, pInnerData->pDescriptorSetPerBatch);
    TFIndirectDrawArguments grid = pGizmoSystem->mGizmoGridMesh;
    cmdDrawInstanced(pCmd, grid.mVertexCount, grid.mStartVertex, grid.mInstanceCount, 0);
}

/****************************************************************************/
// MARK: - Public function realization
/****************************************************************************/

void initGizmoSystem(const GizmoSystemDesc* pGizmoSystemDesc)
{
    pGizmoSystem = (GizmoSystem*)tf_calloc(1, sizeof(GizmoSystem));
    memset(pGizmoSystem, 0, sizeof(GizmoSystem));
    pGizmoSystem->pRenderer = pGizmoSystemDesc->pRenderer;
    pGizmoSystem->mFrameMaxCount = pGizmoSystemDesc->mFrameMaxCount;
    pGizmoSystem->pFrameIdx = pGizmoSystemDesc->pFrameIdx;
    pGizmoSystem->pCache = pGizmoSystemDesc->pCache;
    pGizmoSystem->ppGizmo = NULL;

    addResources();
}

void exitGizmoSystem()
{
    removeResources();
    arrfree(pGizmoSystem->pGizmoSortingArray);
    arrfree(pGizmoSystem->ppGizmoSortedTemp);
    arrfree(pGizmoSystem->ppGizmo);
    tf_free(pGizmoSystem);
}

void loadGizmoSystem(const GizmoSystemLoadDesc* pGizmoSystemLoadDesc)
{
    addShaders();
    addPipelines(pGizmoSystemLoadDesc);
}

void unloadGizmoSystem()
{
    removePipelines();
    removeShaders();
}

void gizmoGetHandlesDefaultStyleDesc(GizmoHandlesStyleDesc* pGizmoHandlesStyleDesc)
{
    pGizmoHandlesStyleDesc->mSizeCenterCube = 0.05f;
    pGizmoHandlesStyleDesc->mRadiusCone = 0.035f;
    pGizmoHandlesStyleDesc->mHeightCone = 0.17f;
    pGizmoHandlesStyleDesc->mSizePlaneRect = 0.25f;
    pGizmoHandlesStyleDesc->mOffsetPlaneRect = 0.5f - pGizmoHandlesStyleDesc->mSizePlaneRect / 2.0f;
    pGizmoHandlesStyleDesc->mSizeEdgeCube = pGizmoHandlesStyleDesc->mRadiusCone;
    pGizmoHandlesStyleDesc->mRadiusMainAxis = 0.8f;
    pGizmoHandlesStyleDesc->mRadiusFrontAxis = 0.82f;
    pGizmoHandlesStyleDesc->mRadiusOutsideAxis = 1.0f;
    pGizmoHandlesStyleDesc->mMainAxisThinkness = 0.02f;

    const float selectedValue = 0.61f;
    const float noselectedValue = 0.45f;
    float4      whiteSelected = f4Make(selectedValue, selectedValue, selectedValue, 1);
    float4      whiteNoselected = f4Make(noselectedValue, noselectedValue, noselectedValue, 1);

    pGizmoHandlesStyleDesc->mXAxisSelectedColor = whiteSelected;
    pGizmoHandlesStyleDesc->mXAxisNoselectedColor = f4Make(0.79f, 0.054f, 0.054f, 1);
    pGizmoHandlesStyleDesc->mYAxisSelectedColor = whiteSelected;
    pGizmoHandlesStyleDesc->mYAxisNoselectedColor = f4Make(0.022f, 0.79f, 0.193f, 1);
    pGizmoHandlesStyleDesc->mZAxisSelectedColor = whiteSelected;
    pGizmoHandlesStyleDesc->mZAxisNoselectedColor = f4Make(0.022f, 0.25f, 0.79f, 1);

    pGizmoHandlesStyleDesc->mCentralCubeSelectedColor = whiteSelected;
    pGizmoHandlesStyleDesc->mCentralCubeNoselectedColor = whiteNoselected;

    pGizmoHandlesStyleDesc->mFrontAxisColor = f4Make(0.2f, 0.2f, 0.2f, 1);
    pGizmoHandlesStyleDesc->mOutsideAxisColor = f4Make(0.45f, 0.45f, 0.45f, 1);

    pGizmoHandlesStyleDesc->mScale = 0.2f;
}

void gizmoGetBoundDefaultStyleDesc(GizmoBoundStyleDesc* pGizmoBoundStyleDesc)
{
    pGizmoBoundStyleDesc->mBoundNoselectedColor = f4Make(0.022f, 0.79f, 0.193f, 1);
    pGizmoBoundStyleDesc->mBoundColorSelected = f4Make(0.61f, 0.61f, 0.61f, 1);

    pGizmoBoundStyleDesc->mBoundPointScale = 0.004f;
}

void gizmoGetRendererDefaultStyleDesc(GizmoRendererStyleDesc* pGizmoRendererStyleDesc)
{
    pGizmoRendererStyleDesc->mDefaultMatrix = f4x4Identity();
    pGizmoRendererStyleDesc->mDefaultColor = f4Make(1, 1, 1, 1);
}

void gizmoGetGridDefaultStyleDesc(GizmoGridStyleDesc* pGizmoGridStyleDesc)
{
    pGizmoGridStyleDesc->mColorGrid = f4Make(0.5, 0.5, 0.5, 0.4f);
    pGizmoGridStyleDesc->mFadeHeightMultiply = 5.0f;
}

TFGizmo* addGizmo(TFGizmoType type, const void* pGizmoStyleDesc)
{
    uint32_t styleSize[] = { sizeof(GizmoHandlesStyleDesc), sizeof(GizmoBoundStyleDesc), sizeof(GizmoRendererStyleDesc),
                             sizeof(GizmoGridStyleDesc), 0 };
    uint32_t instancesCount[] = { GIZMO_HANDLES_INSTANCES_COUNT, GIZMO_BOUND_INSTANCES_COUNT, GIZMO_RENDERER_INSTANCES_COUNT,
                                  GIZMO_GRID_INSTANCES_COUNT, 0 };

    ASSERT(pGizmoSystem);
    TFGizmo* pGizmo = (TFGizmo*)tf_calloc(1, sizeof(TFGizmo));
    memset(pGizmo, 0, sizeof(TFGizmo));
    pGizmo->type = type;
    pGizmo->isActive = true;
    pGizmo->pStyleGizmo = tf_calloc(1, styleSize[type]);
    memcpy(pGizmo->pStyleGizmo, pGizmoStyleDesc, styleSize[type]);
    GizmoInnerData* pInnerData = addGizmoInnerData(instancesCount[type]);
    pGizmo->pGizmo = pInnerData;

    if (pGizmo->type == TF_GIZMO_TYPE_HANDLES)
    {
        pInnerData->pUpdateData = updateHandles;
        pInnerData->pFillData = fillHandlesData;
        pInnerData->pDraw = drawHandles;
        pInnerData->pSerialize = serializeHandles;
        pInnerData->pDeserialize = deserializeHandles;
    }
    else if (pGizmo->type == TF_GIZMO_TYPE_BOUND)
    {
        pInnerData->pUpdateData = updateBound;
        pInnerData->pFillData = fillBoundData;
        pInnerData->pDraw = drawBound;
        pInnerData->pSerialize = serializeBound;
        pInnerData->pDeserialize = deserializeBound;
    }
    else if (pGizmo->type == TF_GIZMO_TYPE_RENDERER)
    {
        addInnerDataGizmoRenderer(pInnerData);

        pInnerData->pUpdateData = updateGizmoDefault;
        pInnerData->pFillData = fillRendererData;
        pInnerData->pDraw = drawRenderer;
        pInnerData->pSerialize = NULL;
        pInnerData->pDeserialize = NULL;
    }
    else
    {
        pInnerData->pUpdateData = updateGizmoDefault;
        pInnerData->pFillData = fillGridData;
        pInnerData->pDraw = drawGrid;
        pInnerData->pSerialize = NULL;
        pInnerData->pDeserialize = NULL;
    }

    arrins(pGizmoSystem->ppGizmo, 0, pGizmo);
    return pGizmo;
}

void removeGizmo(TFGizmo* pGizmo)
{
    if (pGizmoSystem->pLockedGizmo == pGizmo)
    {
        pGizmoSystem->pLockedGizmo = NULL;
    }

    tf_free(pGizmo->pStyleGizmo);

    GizmoInnerData* pInnerData = (GizmoInnerData*)pGizmo->pGizmo;

    removeDescriptorSet(pGizmoSystem->pRenderer, pInnerData->pDescriptorSetPerBatch);

    for (uint32_t i = 0; i < pGizmoSystem->mFrameMaxCount; i++)
    {
        removeResource_Buffer(pInnerData->ppGizmoInstanceBuffer[i]);
    }

    tf_free(pInnerData->ppGizmoInstanceBuffer);

    if (pGizmo->type == TF_GIZMO_TYPE_RENDERER)
    {
        removeInnerDataGizmoRenderer(pInnerData);
    }

    tf_free(pInnerData->pWriteInstance);
    tf_free(pInnerData);

    uint32_t i;
    for (i = 0; i < arrlen(pGizmoSystem->ppGizmo); i++)
    {
        if (pGizmoSystem->ppGizmo[i] == pGizmo)
        {
            break;
        }
    }

    if (i != arrlen(pGizmoSystem->ppGizmo))
    {
        arrdel(pGizmoSystem->ppGizmo, i);
    }

    tf_free(pGizmo);
}

bool gizmoUpdate(const GizmoUpdateCameraDesc* pCameraDesc, float2* pMousePosition)
{
    setupNewCameraMatrixes(pCameraDesc);

    GizmoUpdateData updateData{};
    calculateCameraUpdateData(pCameraDesc, *pMousePosition, &updateData);

    int32_t gizmoCount = (int32_t)arrlen(pGizmoSystem->ppGizmo);
    int32_t maxPriority = INT32_MIN;
    int32_t minDistContactIdx = -1;
    float   minDist = FLT_MAX;

    for (int32_t i = 0; i < gizmoCount; i++)
    {
        TFGizmo*        pGizmo = pGizmoSystem->ppGizmo[i];
        GizmoInnerData* pInnerData = (GizmoInnerData*)pGizmo->pGizmo;
        if (!gizmoIsActive(pGizmo) || !gizmoIsValid(pGizmo))
        {
            continue;
        }

        if (gGizmoTransformIsRequired[pGizmo->type])
            calculateGizmoUpdateData(pGizmo, &updateData);

        pInnerData->pUpdateData(pGizmo, pCameraDesc, &updateData);

        int32_t priority = (int32_t)pGizmo->mTouchPriority;
        if (pInnerData->mHasContact)
        {
            bool change = false;
            if (priority > maxPriority)
            {
                change = true;
            }
            else if (priority == maxPriority && minDist > pInnerData->mContact.distanceToContact)
            {
                change = true;
            }

            if (change)
            {
                maxPriority = priority;
                minDistContactIdx = i;
                minDist = pInnerData->mContact.distanceToContact;
            }
        }
    }

    for (int32_t i = 0; i < gizmoCount; i++)
    {
        TFGizmo* pGizmo = pGizmoSystem->ppGizmo[i];
        if (pGizmoSystem->pLockedGizmo == pGizmo)
        {
            continue;
        }
        else if (i == minDistContactIdx && pGizmoSystem->pLockedGizmo == NULL)
        {
            continue;
        }

        GizmoInnerData* pInnerData = (GizmoInnerData*)pGizmo->pGizmo;
        pInnerData->mHasContact = false;
    }

    for (int32_t i = 0; i < gizmoCount; i++)
    {
        TFGizmo* pGizmo = pGizmoSystem->ppGizmo[i];
        if (!gizmoIsActive(pGizmo) || !gizmoIsValid(pGizmo))
        {
            continue;
        }
        GizmoInnerData* pInnerData = (GizmoInnerData*)pGizmo->pGizmo;

        pInnerData->pFillData(pGizmo, pCameraDesc);
    }

    // make available only nearest contact
    if (pGizmoSystem->pLockedGizmo == NULL)
    {
        return minDistContactIdx != -1;
    }
    else
    {
        return false;
    }
}

void gizmoUpdateBackground(const GizmoUpdateCameraDesc* pCameraDesc, bool reset)
{
    setupNewCameraMatrixes(pCameraDesc);

    size_t gizmoCount = arrlen(pGizmoSystem->ppGizmo);
    for (size_t i = 0; i < gizmoCount; i++)
    {
        TFGizmo* pGizmo = pGizmoSystem->ppGizmo[i];
        if (!gizmoIsActive(pGizmo) || !gizmoIsValid(pGizmo))
        {
            continue;
        }

        if (reset)
        {
            gizmoReset(pGizmo);
        }

        GizmoInnerData* pInnerData = (GizmoInnerData*)pGizmo->pGizmo;
        pInnerData->pUpdateData(pGizmo, pCameraDesc, NULL);
        pInnerData->pFillData(pGizmo, pCameraDesc);
    }
}

bool gizmoSelect()
{
    if (pGizmoSystem->pLockedGizmo != NULL)
        return false;

    size_t countGizmos = arrlen(pGizmoSystem->ppGizmo);
    for (size_t i = 0; i < countGizmos; i++)
    {
        TFGizmo* pGizmo = pGizmoSystem->ppGizmo[i];
        if (gizmoIsValid(pGizmo) && gizmoIsActive(pGizmo))
        {
            GizmoInnerData* pInnerData = (GizmoInnerData*)pGizmo->pGizmo;
            if (pInnerData->mHasContact)
            {
                pGizmo->isSelected = true;
                pGizmoSystem->pLockedGizmo = pGizmo;
                if (pGizmo->pOnSelect)
                {
                    pGizmo->pOnSelect(pGizmo->pOnSelectUserData);
                }
                return true;
            }
        }
    }

    return false;
}

void gizmoUnselect()
{
    if (pGizmoSystem->pLockedGizmo)
    {
        TFGizmo* pGizmo = pGizmoSystem->pLockedGizmo;
        gizmoReset(pGizmo);
        pGizmo->isSelected = false;
        if (pGizmo->pOnUnselect)
        {
            pGizmo->pOnUnselect(pGizmo->pOnUnselectUserData);
        }
        pGizmoSystem->pLockedGizmo = NULL;
    }
}

void gizmoDrawOrderRange(TFCmd* pCmd, GizmoRenderTarget* pRenderTarget, uint16_t startRenderOrder, uint16_t endRenderOrder)
{
    ASSERT(endRenderOrder >= startRenderOrder);

    uint32_t len = (uint32_t)arrlen(pGizmoSystem->ppGizmo);
    uint32_t start = len;
    uint32_t end = len;

    for (uint32_t i = 0; i < len; i++)
    {
        TFGizmo* gizmo = pGizmoSystem->ppGizmo[i];
        uint16_t order = gizmo->mRenderOrder;
        if (start > i && order >= startRenderOrder)
        {
            start = i;
        }

        if (order > endRenderOrder)
        {
            end = i;
            break;
        }
    }

    if (end > 0 && end > start)
    {
        drawRange(pCmd, pRenderTarget, start, end);
    }
}

void gizmoDraw(TFCmd* pCmd, GizmoRenderTarget* pRenderTarget)
{
    drawRange(pCmd, pRenderTarget, 0, (uint32_t)arrlen(pGizmoSystem->ppGizmo));
}

TFGizmo* gizmoGetLocked() { return pGizmoSystem->pLockedGizmo; }

uint32_t gizmoGetTouchPriority(TFGizmo* pGizmo) { return pGizmo->mTouchPriority; }

void gizmoSetTouchPriority(TFGizmo* pGizmo, uint32_t priority) { pGizmo->mTouchPriority = priority; }

uint32_t gizmoGetRenderOrder(TFGizmo* pGizmo) { return pGizmo->mRenderOrder; }

uint16_t gizmoGetRenderOrderOffset(TFGizmo* pGizmo) { return pGizmo->mRenderOrderOffset; }

void gizmoSetRenderPriority(TFGizmo* pGizmo, uint16_t order, uint16_t offset)
{
    pGizmo->mRenderOrder = order;
    pGizmo->mRenderOrderOffset = offset;
    sortGizmoByRenderOrder();
}

bool gizmoIsLocked() { return pGizmoSystem->pLockedGizmo != NULL; }

void gizmoSetHandlesState(TFGizmo* pGizmo, TFGizmoHandlesState state)
{
    gizmoReset(pGizmo);
    ((GizmoInnerData*)pGizmo->pGizmo)->mState = state;
}

TFGizmoHandlesState gizmoGetHandlesState(const TFGizmo* pGizmo) { return ((GizmoInnerData*)pGizmo->pGizmo)->mState; }

void gizmoSetHandlesScaleMode(TFGizmo* pGizmo, TFGizmoHandlesScaleMode mode) { ((GizmoInnerData*)pGizmo->pGizmo)->mScaleMode = mode; }

TFGizmoHandlesScaleMode gizmoGetHandlesScaleMode(const TFGizmo* pGizmo) { return ((GizmoInnerData*)pGizmo->pGizmo)->mScaleMode; }

void gizmoSetBoundType(TFGizmo* pGizmo, TFGizmoBoundType type)
{
    gizmoReset(pGizmo);
    GizmoInnerData* innerData = (GizmoInnerData*)pGizmo->pGizmo;
    innerData->mMaskDisabledPoints = 0;
    innerData->mBoundType = type;
}

TFGizmoBoundType gizmoGetBoundType(const TFGizmo* pGizmo) { return ((GizmoInnerData*)pGizmo->pGizmo)->mBoundType; }

uint32_t gizmoGetBoundPointsCount(TFGizmo* pGizmo) { return gBoundPointsCount[gizmoGetBoundType(pGizmo)]; }

bool gizmoGetBoundPointEnable(TFGizmo* pGizmo, uint32_t pointIdx)
{
    ASSERT(gizmoGetBoundPointsCount(pGizmo) > pointIdx);
    return (((GizmoInnerData*)pGizmo->pGizmo)->mMaskDisabledPoints & (1 << pointIdx)) == 0;
}

void gizmoSetBoundPointEnable(TFGizmo* pGizmo, uint32_t pointIdx, bool enable)
{
    ASSERT(gizmoGetBoundPointsCount(pGizmo) > pointIdx);
    if (enable)
    {
        ((GizmoInnerData*)pGizmo->pGizmo)->mMaskDisabledPoints &= ~(1 << pointIdx);
    }
    else
    {
        ((GizmoInnerData*)pGizmo->pGizmo)->mMaskDisabledPoints |= (1 << pointIdx);
    }
}

void gizmoSetActive(TFGizmo* pGizmo, bool active)
{
    pGizmo->isActive = active;
    if (!active && pGizmoSystem->pLockedGizmo == pGizmo)
    {
        pGizmoSystem->pLockedGizmo = NULL;
    }
}

bool gizmoIsActive(const TFGizmo* pGizmo) { return pGizmo->isActive; }

bool gizmoIsValid(const TFGizmo* pGizmo)
{
    return !gGizmoTransformIsRequired[pGizmo->type] || (pGizmo->pPosition != NULL && pGizmo->pScale && pGizmo->pRotation);
}

void gizmoSetOnSelectCallback(TFGizmo* pGizmo, void* pUserData, GizmoCallback callback)
{
    pGizmo->pOnSelect = callback;
    pGizmo->pOnSelectUserData = pUserData;
}

void gizmoSetOnUnselectCallback(TFGizmo* pGizmo, void* pUserData, GizmoCallback callback)
{
    pGizmo->pOnUnselect = callback;
    pGizmo->pOnUnselectUserData = pUserData;
}

void gizmoSetOnPositionChangeCallback(TFGizmo* pGizmo, void* pUserData, GizmoCallbackF3 callback)
{
    pGizmo->pOnPositionChange = callback;
    pGizmo->pOnPositionChangeUserData = pUserData;
}

void gizmoSetOnRotationChangeCallback(TFGizmo* pGizmo, void* pUserData, GizmoCallbackQuat callback)
{
    pGizmo->pOnRotationChange = callback;
    pGizmo->pOnRotationChangeUserData = pUserData;
}

void gizmoSetOnScaleChangeCallback(TFGizmo* pGizmo, void* pUserData, GizmoCallbackF3 callback)
{
    pGizmo->pOnScaleChange = callback;
    pGizmo->pOnScaleChangeUserData = pUserData;
}

void gizmoSetTransform(TFGizmo* pGizmo, float3* pPosition, quat* pRotation, float3* pScale)
{
    pGizmo->pPosition = pPosition;
    pGizmo->pRotation = pRotation;
    pGizmo->pScale = pScale;
}

void gizmoRendererBegin(TFGizmo* pGizmo)
{
    ASSERT(!pGizmoSystem->pGizmoRendererBegun);

    pGizmoSystem->pGizmoRendererBegun = (GizmoInnerData*)pGizmo->pGizmo;
    GizmoRendererCache* cache = &pGizmoSystem->pGizmoRendererBegun->mRenderer.mCache;

    cache->mInstanceActive = -1;
    arrsetlen(cache->pInstances, 0);
    arrsetlen(cache->pVertexLines, 0);
    arrsetlen(cache->pVertexTriangles, 0);
    arrsetlen(cache->pIndexTriangles, 0);

    GizmoRendererStyleDesc* styleDesc = (GizmoRendererStyleDesc*)pGizmo->pStyleGizmo;
    gizmoRendererSetMatrix(&styleDesc->mDefaultMatrix);
    gizmoRendererSetColor(&styleDesc->mDefaultColor);

    // to keep instance 0 intact.
    cache->isUsedInstance = true;
}

void gizmoRendererSetMatrix(float4x4* matrix)
{
    ASSERT(pGizmoSystem->pGizmoRendererBegun);
    GizmoRendererCache* cache = &pGizmoSystem->pGizmoRendererBegun->mRenderer.mCache;
    gizmoRendererUpdateInstance(cache);
    int32_t instanceActive = cache->mInstanceActive;
    cache->pInstances[instanceActive].mTRS = *matrix;
}

void gizmoRendererSetColor(float4* color)
{
    ASSERT(pGizmoSystem->pGizmoRendererBegun);
    GizmoRendererCache* cache = &pGizmoSystem->pGizmoRendererBegun->mRenderer.mCache;
    gizmoRendererUpdateInstance(cache);
    int32_t instanceActive = cache->mInstanceActive;
    cache->pInstances[instanceActive].mColor = *color;
}

void gizmoRendererSetDefaultMatrixAndColor()
{
    ASSERT(pGizmoSystem->pGizmoRendererBegun);
    GizmoRendererCache* cache = &pGizmoSystem->pGizmoRendererBegun->mRenderer.mCache;
    cache->mInstanceActive = 0;
    cache->isUsedInstance = true;
}

void gizmoRendererAddLine(float3* start, float3* end)
{
    ASSERT(pGizmoSystem->pGizmoRendererBegun);
    GizmoRendererCache* cache = &pGizmoSystem->pGizmoRendererBegun->mRenderer.mCache;

    int32_t instanceActive = cache->mInstanceActive;
    ASSERT(instanceActive != -1);

    GizmoVertexData vertex[2]{};
    vertex[0].mPos = *start;
    vertex[1].mPos = *end;
    for (uint32_t i = 0; i < 2; i++)
    {
        vertex[i].mInstanceIndex = (uint32_t)instanceActive;
        arrpush(cache->pVertexLines, vertex[i].mData);
    }

    cache->isUsedInstance = true;
}

void gizmoRendererAddWireRect(float3* center, float2* extend)
{
    ASSERT(pGizmoSystem->pGizmoRendererBegun);
    GizmoRendererCache* cache = &pGizmoSystem->pGizmoRendererBegun->mRenderer.mCache;

    int32_t instanceActive = cache->mInstanceActive;
    ASSERT(instanceActive != -1);

    GizmoVertexData vertex[8]{};
    float3          min = f3Sub(*center, f3Make(extend->x, 0, extend->y));
    float3          max = f3Add(*center, f3Make(extend->x, 0, extend->y));
    vertex[0].mPos = f3Make(min.x, min.y, min.z);
    vertex[1].mPos = f3Make(min.x, min.y, max.z);
    vertex[2].mPos = f3Make(min.x, min.y, max.z);
    vertex[3].mPos = f3Make(max.x, min.y, max.z);
    vertex[4].mPos = f3Make(max.x, min.y, max.z);
    vertex[5].mPos = f3Make(max.x, min.y, min.z);
    vertex[6].mPos = f3Make(max.x, min.y, min.z);
    vertex[7].mPos = f3Make(min.x, min.y, min.z);

    for (uint32_t i = 0; i < 8; i++)
    {
        vertex[i].mInstanceIndex = (uint32_t)instanceActive;
        arrpush(cache->pVertexLines, vertex[i].mData);
    }

    cache->isUsedInstance = true;
}

void gizmoRendererAddWireCube(float3* center, float3* extend)
{
    ASSERT(pGizmoSystem->pGizmoRendererBegun);
    GizmoRendererCache* cache = &pGizmoSystem->pGizmoRendererBegun->mRenderer.mCache;

    int32_t instanceActive = cache->mInstanceActive;
    ASSERT(instanceActive != -1);

    float3 vertices[8];
    float3 min = f3Sub(*center, *extend);
    float3 max = f3Add(*center, *extend);
    vertices[0] = f3Make(max.x, min.y, max.z);
    vertices[1] = f3Make(max.x, min.y, min.z);
    vertices[2] = f3Make(min.x, min.y, min.z);
    vertices[3] = f3Make(min.x, min.y, max.z);
    vertices[4] = f3Make(max.x, max.y, max.z);
    vertices[5] = f3Make(max.x, max.y, min.z);
    vertices[6] = f3Make(min.x, max.y, min.z);
    vertices[7] = f3Make(min.x, max.y, max.z);

    const uint16_t cubeIndexes[] = {
        0, 1, 1, 2, 2, 3, 3, 0, 4, 5, 5, 6, 6, 7, 7, 4, 0, 4, 1, 5, 2, 6, 3, 7,
    };

    GizmoVertexData vertex{};
    vertex.mInstanceIndex = (uint32_t)instanceActive;

    for (uint32_t i = 0; i < 24; i++)
    {
        vertex.mPos = vertices[cubeIndexes[i]];
        arrpush(cache->pVertexLines, vertex.mData);
    }

    cache->isUsedInstance = true;
}

void gizmoRendererAddRect(float3* center, float2* extend)
{
    ASSERT(pGizmoSystem->pGizmoRendererBegun);
    GizmoRendererCache* cache = &pGizmoSystem->pGizmoRendererBegun->mRenderer.mCache;

    int32_t instanceActive = cache->mInstanceActive;
    ASSERT(instanceActive != -1);

    GizmoVertexData vertex[4]{};
    float3          min = f3Sub(*center, f3Make(extend->x, 0, extend->y));
    float3          max = f3Add(*center, f3Make(extend->x, 0, extend->y));
    vertex[0].mInstanceIndex = vertex[1].mInstanceIndex = vertex[2].mInstanceIndex = vertex[3].mInstanceIndex = (uint32_t)instanceActive;
    vertex[0].mPos = f3Make(min.x, min.y, min.z);
    vertex[1].mPos = f3Make(min.x, min.y, max.z);
    vertex[2].mPos = f3Make(max.x, min.y, max.z);
    vertex[3].mPos = f3Make(max.x, min.y, min.z);

    uint16_t vertexIdx = (uint16_t)arrlen(cache->pVertexTriangles);
    arrsetlen(cache->pVertexTriangles, vertexIdx + 4);
    memcpy(cache->pVertexTriangles + vertexIdx, vertex, sizeof(vertex));

    uint16_t index[] = { (uint16_t)(vertexIdx + 2), (uint16_t)(vertexIdx + 1), vertexIdx,
                         (uint16_t)(vertexIdx + 3), (uint16_t)(vertexIdx + 2), vertexIdx };

    uint32_t indexIdx = (uint32_t)arrlen(cache->pIndexTriangles);
    arrsetlen(cache->pIndexTriangles, indexIdx + 6);
    memcpy(cache->pIndexTriangles + indexIdx, index, sizeof(index));

    cache->isUsedInstance = true;
}

void gizmoRendererAddCube(float3* center, float3* extend)
{
    ASSERT(pGizmoSystem->pGizmoRendererBegun);
    GizmoRendererCache* cache = &pGizmoSystem->pGizmoRendererBegun->mRenderer.mCache;

    int32_t instanceActive = cache->mInstanceActive;
    ASSERT(instanceActive != -1);

    GizmoVertexData vertices[8]{};
    float3          min = f3Sub(*center, *extend);
    float3          max = f3Add(*center, *extend);
    vertices[0].mPos = f3Make(max.x, min.y, max.z);
    vertices[1].mPos = f3Make(max.x, min.y, min.z);
    vertices[2].mPos = f3Make(min.x, min.y, min.z);
    vertices[3].mPos = f3Make(min.x, min.y, max.z);
    vertices[4].mPos = f3Make(max.x, max.y, max.z);
    vertices[5].mPos = f3Make(max.x, max.y, min.z);
    vertices[6].mPos = f3Make(min.x, max.y, min.z);
    vertices[7].mPos = f3Make(min.x, max.y, max.z);

    for (uint32_t i = 0; i < 8; i++)
    {
        vertices[i].mInstanceIndex = (uint32_t)instanceActive;
    }
    uint32_t vertexIdx = (uint32_t)arrlen(cache->pVertexTriangles);
    arrsetlen(cache->pVertexTriangles, vertexIdx + 8);
    memcpy(cache->pVertexTriangles + vertexIdx, vertices, sizeof(vertices));

    uint16_t cubeIndexes[] = { 0, 1, 2, 2, 3, 0, 4, 6, 5, 6, 4, 7, 0, 3, 7, 7, 4, 0, 1, 5, 6, 6, 2, 1, 0, 4, 5, 5, 1, 0, 3, 2, 6, 6, 7, 3 };

    for (uint32_t i = 0; i < 36; i++)
    {
        cubeIndexes[i] += (uint16_t)vertexIdx;
    }

    uint32_t indexIdx = (uint32_t)arrlen(cache->pIndexTriangles);
    arrsetlen(cache->pIndexTriangles, indexIdx + 36);
    memcpy(cache->pIndexTriangles + indexIdx, cubeIndexes, sizeof(cubeIndexes));

    cache->isUsedInstance = true;
}

void gizmoRendererEnd()
{
    ASSERT(pGizmoSystem->pGizmoRendererBegun);
    for (uint32_t i = 0; i < pGizmoSystem->mFrameMaxCount; i++)
    {
        pGizmoSystem->pGizmoRendererBegun->mRenderer.pBuffers[i].mIsObsolete = true;
    }
    pGizmoSystem->pGizmoRendererBegun = NULL;
}

void gizmoRendererForceUpdate(TFGizmo* pGizmo)
{
    GizmoInnerData* pInnerData = (GizmoInnerData*)pGizmo->pGizmo;
    for (uint32_t i = 0; i < pGizmoSystem->mFrameMaxCount; i++)
    {
        updateGizmoRendererBuffers(pInnerData, i);
    }
}

void gizmoRendererClearCache(TFGizmo* pGizmo)
{
    GizmoInnerData* pInnerData = (GizmoInnerData*)pGizmo->pGizmo;
    removeGizmoRendererCache(pInnerData);
}

void gizmoRendererClear(TFGizmo* pGizmo)
{
    GizmoInnerData* pInnerData = (GizmoInnerData*)pGizmo->pGizmo;

    removeGizmoRendererCache(pInnerData);
    for (uint32_t i = 0; i < pGizmoSystem->mFrameMaxCount; i++)
    {
        pInnerData->mRenderer.pBuffers[i].mRequiresCleaning = true;
    }
}

/****************************************************************************/
// MARK: - Public function for serialize/deserialize
/****************************************************************************/

typedef struct ALIGNAS(8) SerializedGizmoData
{
    GizmoContactData mContact;
    quat             mRotation;
    float3           mPosition;
    float3           mScale;

    TFGizmoType mType;

    uint32_t mTouchPriority;
    uint16_t mRenderOrder;
    uint16_t mRenderOrderOffset;

    bool mHasContact;
    bool mIsActive;
    bool mIsSelected;
} SerializedGizmoData;

size_t gizmoGetSerializationSize(TFGizmo* pGizmo)
{
    GizmoInnerData* pInnerData = (GizmoInnerData*)pGizmo->pGizmo;
    ASSERT(pInnerData->pSerialize != NULL);

    size_t size = pInnerData->pSerialize(pInnerData, NULL, 0);
    size += sizeof(SerializedGizmoData);
    return size;
}

size_t gizmoSerialize(TFGizmo* pGizmo, uint8_t* pBuffer, size_t bufferSize)
{
    GizmoInnerData* pInnerData = (GizmoInnerData*)pGizmo->pGizmo;

    size_t requiredSize = gizmoGetSerializationSize(pGizmo);
    ASSERT(bufferSize >= requiredSize);

    size_t size = pInnerData->pSerialize(pInnerData, pBuffer, bufferSize);

    SerializedGizmoData data{};
    if (gizmoIsValid(pGizmo))
    {
        data.mRotation = *pGizmo->pRotation;
        data.mPosition = *pGizmo->pPosition;
        data.mScale = *pGizmo->pScale;
    }
    data.mContact = pInnerData->mContact;
    data.mType = pGizmo->type;
    data.mTouchPriority = pGizmo->mTouchPriority;
    data.mRenderOrder = pGizmo->mRenderOrder;
    data.mRenderOrderOffset = pGizmo->mRenderOrderOffset;
    data.mHasContact = pInnerData->mHasContact;
    data.mIsActive = pGizmo->isActive;
    data.mIsSelected = pGizmo->isSelected;

    memcpy(pBuffer + size, &data, sizeof(SerializedGizmoData));
    size += sizeof(SerializedGizmoData);

    return size;
}

void gizmoBeginDeserialization()
{
    pGizmoSystem->pLockedGizmo = NULL;
    int32_t gizmoCount = (int32_t)arrlen(pGizmoSystem->ppGizmo);
    for (int32_t i = 0; i < gizmoCount; i++)
    {
        TFGizmo* pGizmo = pGizmoSystem->ppGizmo[i];
        pGizmo->isSelected = false;
        GizmoInnerData* pInnerData = (GizmoInnerData*)pGizmo->pGizmo;
        pInnerData->mHasContact = false;
    }
}

size_t gizmoDeserialize(TFGizmo* pGizmo, const uint8_t* pBuffer, size_t bufferSize)
{
    GizmoInnerData* pInnerData = (GizmoInnerData*)pGizmo->pGizmo;

    ASSERT(pInnerData->pDeserialize != NULL);
    size_t size = pInnerData->pDeserialize(pInnerData, pBuffer, bufferSize);

    SerializedGizmoData data;
    memcpy(&data, pBuffer + size, sizeof(SerializedGizmoData));
    size += sizeof(SerializedGizmoData);

    ASSERT(pGizmo->type == data.mType);

    pInnerData->mContact = data.mContact;
    pInnerData->mHasContact = data.mHasContact;
    pGizmo->isActive = data.mIsActive;
    pGizmo->isSelected = data.mIsSelected;
    pGizmo->mTouchPriority = data.mTouchPriority;
    pGizmo->mRenderOrder = data.mRenderOrder;
    pGizmo->mRenderOrderOffset = data.mRenderOrderOffset;
    if (gizmoIsValid(pGizmo))
    {
        changeGizmoPosition(pGizmo, data.mPosition);
        changeGizmoRotation(pGizmo, data.mRotation);
        changeGizmoScale(pGizmo, data.mScale);
    }
    return size;
}

void gizmoEndDeserialization()
{
    pGizmoSystem->pLockedGizmo = NULL;

    int32_t gizmoCount = (int32_t)arrlen(pGizmoSystem->ppGizmo);
    for (int32_t i = 0; i < gizmoCount; i++)
    {
        TFGizmo* pGizmo = pGizmoSystem->ppGizmo[i];
        if (pGizmo->isSelected)
        {
            if (pGizmoSystem->pLockedGizmo == NULL)
            {
                pGizmoSystem->pLockedGizmo = pGizmo;
            }
            else
            {
                pGizmo->isSelected = false;
                GizmoInnerData* pInnerData = (GizmoInnerData*)pGizmo->pGizmo;
                pInnerData->mHasContact = false;
            }
        }
    }

    sortGizmoByRenderOrder();
}