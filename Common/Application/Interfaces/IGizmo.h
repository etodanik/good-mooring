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

#ifndef IGIZMO_H
#define IGIZMO_H

#include "../Config.h"
#include "../../Utilities/Interfaces/IMath.h"

// for IGraphics.h
typedef struct TFRenderer      TFRenderer;
typedef struct TFRenderTarget  TFRenderTarget;
typedef struct TFPipelineCache TFPipelineCache;
typedef enum TFSampleCount     TFSampleCount;
typedef enum TinyImageFormat   TinyImageFormat;

typedef enum TFGizmoType
{
    TF_GIZMO_TYPE_HANDLES = 0,
    TF_GIZMO_TYPE_BOUND = 1,
    TF_GIZMO_TYPE_RENDERER = 2,
    TF_GIZMO_TYPE_GRID = 3,
    TF_GIZMO_TYPE_ALL = 4,
} TFGizmoType;

typedef enum TFGizmoHandlesState
{
    TF_GIZMO_HANDLES_STATE_NONE = 0,
    TF_GIZMO_HANDLES_STATE_MOVE = 1,
    TF_GIZMO_HANDLES_STATE_ROTATE = 2,
    TF_GIZMO_HANDLES_STATE_SCALE = 3
} TFGizmoHandlesState;

typedef enum TFGizmoBoundType
{
    // pScale x: extend X, y: extend Y, z: extend Z
    TF_GIZMO_BOUND_TYPE_BOX = 0,
    // pScale x: extend X, y: 0, z: extend Z
    TF_GIZMO_BOUND_TYPE_RECT = 1,
    // pScale x: radius, y: 0, z: radius
    TF_GIZMO_BOUND_TYPE_CIRCLE = 2,
    // pScale x: radius, y: height / 2, z: radius
    TF_GIZMO_BOUND_TYPE_CONE = 3,
    // pScale x: radius, y: radius, z: radius
    TF_GIZMO_BOUND_TYPE_SPHERE = 4,
    // pScale x: radius, y: height / 2, z: radius
    TF_GIZMO_BOUND_TYPE_CYLINDER = 5,
    // pScale x: radius, y: height / 2, z: radius
    TF_GIZMO_BOUND_TYPE_CAPSULE = 6,
} TFGizmoBoundType;

typedef enum TFGizmoHandlesScaleMode
{
    TF_GIZMO_HANDLES_SCALE_MODE_NONE = 0,
    TF_GIZMO_HANDLES_SCALE_MODE_XY_CONNECTED = 1,
    TF_GIZMO_HANDLES_SCALE_MODE_XZ_CONNECTED = 2,
    TF_GIZMO_HANDLES_SCALE_MODE_YZ_CONNECTED = 3,
    TF_GIZMO_HANDLES_SCALE_MODE_XYZ_CONNECTED = 4,
} TFGizmoHandlesScaleMode;

typedef void (*GizmoCallback)(void* pUserData);
typedef void (*GizmoCallbackF3)(void* pUserData, float3 from, float3 to);
typedef void (*GizmoCallbackQuat)(void* pUserData, quat from, quat to);

typedef struct TFGizmo
{
    // Type of the gizmo
    TFGizmoType type;
    // Inner data
    void*       pGizmo;
    // Style data
    void*       pStyleGizmo;

    void*             pOnSelectUserData;
    GizmoCallback     pOnSelect;
    void*             pOnUnselectUserData;
    GizmoCallback     pOnUnselect;
    void*             pOnPositionChangeUserData;
    GizmoCallbackF3   pOnPositionChange;
    void*             pOnRotationChangeUserData;
    GizmoCallbackQuat pOnRotationChange;
    void*             pOnScaleChangeUserData;
    GizmoCallbackF3   pOnScaleChange;

    float3* pPosition;
    quat*   pRotation;
    float3* pScale;

    uint32_t mTouchPriority;
    uint16_t mRenderOrder;
    uint16_t mRenderOrderOffset;

    bool isActive;
    bool isSelected;
} TFGizmo;

typedef struct GizmoSystemDesc
{
    TFRenderer*      pRenderer = NULL;
    TFPipelineCache* pCache = NULL;

    uint32_t        mFrameMaxCount = 2u;
    const uint32_t* pFrameIdx = NULL;
} GizmoSystemDesc;

typedef struct GizmoSystemLoadDesc
{
    TFPipelineCache* pCache;
    uint32_t         mLoadType;     // enum TFReloadType
    TinyImageFormat  mColorFormat;  // enum TinyImageFormat
    TinyImageFormat  mDepthsFormat; // enum TinyImageFormat
    TFSampleCount    mSampleCount;
    uint32_t         mSampleQuality;
} GizmoSystemLoadDesc;

typedef struct GizmoHandlesStyleDesc
{
    // X axis selected color
    float4 mXAxisSelectedColor;
    // Y axis selected color
    float4 mYAxisSelectedColor;
    // Z axis selected color
    float4 mZAxisSelectedColor;

    // X axis noselected color
    float4 mXAxisNoselectedColor;
    // Y axis noselected color
    float4 mYAxisNoselectedColor;
    // Z axis noselected color
    float4 mZAxisNoselectedColor;

    // Central cube selected color
    float4 mCentralCubeSelectedColor;

    // Central cube noselected color
    float4 mCentralCubeNoselectedColor;
    // Front axis color
    float4 mFrontAxisColor;
    // Outside axis color
    float4 mOutsideAxisColor;

    // Size of central cube for moving and scaling
    float mSizeCenterCube;
    // Radius of cone for moving
    float mRadiusCone;
    // Height of cone for moving
    float mHeightCone;
    // Size of cube for scale (on edges)
    float mSizeEdgeCube;
    // Size of plane for moving and scaling
    float mSizePlaneRect;
    // Offset of plane for moving and scaling (start from 0.0 0.0)
    float mOffsetPlaneRect;
    // Radius of main axis for rotating
    float mRadiusMainAxis;
    // Radius of front axis for rotating (must be a bit more than main axis)
    float mRadiusFrontAxis;
    // Radius of outside axis for rotating (rotate along of camera view)
    float mRadiusOutsideAxis;
    // Thinkness of main axis for rotating
    float mMainAxisThinkness;

    // Gizmo scale
    float mScale;
} GizmoHandlesStyleDesc;

typedef struct GizmoBoundStyleDesc
{
    // Bound noselected color
    float4 mBoundNoselectedColor;
    // Bound selected color
    float4 mBoundColorSelected;

    // Bound point scale
    float mBoundPointScale;
} GizmoBoundStyleDesc;

typedef struct GizmoRendererStyleDesc
{
    // Default TRS matrix
    float4x4 mDefaultMatrix;
    // Default color
    float4   mDefaultColor;
} GizmoRendererStyleDesc;

typedef struct GizmoGridStyleDesc
{
    // Color grid
    float4 mColorGrid;
    // Fade height multiply
    float  mFadeHeightMultiply;

} GizmoGridStyleDesc;

typedef struct GizmoUpdateCameraDesc
{
    // Matrix from local to world
    float4x4 mCameraLocalToWorld;
    // Matrix from world to local
    float4x4 mCameraWorldtoLocal;
    // Matrix projection
    float4x4 mProjection;
    // Near plane
    float    mNearPlane;
    // Far plane
    float    mFarPlane;

    // Height / Width
    float mAspectInverse;
    // tan(fov / 2)
    float mFovTangent;
} GizmoUpdateCameraDesc;

typedef struct GizmoRenderTarget
{
    TFRenderTarget* pColorTarget;
    TFRenderTarget* pDepthTarget;

    float mX;
    float mY;
    float mWidth;
    float mHeight;
    float mMinDepth;
    float mMaxDepth;
    bool  mClearDepth;
} GizmoRenderTarget;

#if defined(__cplusplus)
extern "C"
{
#endif
// Init gizmo system resources
FORGE_API void initGizmoSystem(const GizmoSystemDesc* pGizmoSystemDesc);

// Release gizmo system resources
FORGE_API void exitGizmoSystem();

// Load shaders and pipelines
FORGE_API void loadGizmoSystem(const GizmoSystemLoadDesc* pGizmoSystemLoadDesc);

// Unload shaders and pipelines
FORGE_API void unloadGizmoSystem();

///  Put default parameters for gizmo handles to GizmoHandlesStyleDesc
FORGE_API void gizmoGetHandlesDefaultStyleDesc(GizmoHandlesStyleDesc* pGizmoHandlesStyleDesc);

///  Put default parameters for gizmo bound to GizmoBoundStyleDesc
FORGE_API void gizmoGetBoundDefaultStyleDesc(GizmoBoundStyleDesc* pGizmoBoundStyleDesc);

///  Put default parameters for gizmo renderer to GizmoBoundStyleDesc
FORGE_API void gizmoGetRendererDefaultStyleDesc(GizmoRendererStyleDesc* pGizmoRendererStyleDesc);

///  Put default parameters for gizmo renderer to GizmoGridStyleDesc
FORGE_API void gizmoGetGridDefaultStyleDesc(GizmoGridStyleDesc* pGizmoGridStyleDesc);

// Add new gizmo
FORGE_API TFGizmo* addGizmo(TFGizmoType type, const void* pGizmoStyleDesc);

// Remove gizmo
FORGE_API void removeGizmo(TFGizmo* pGizmo);

// Update gizmo by mouse position at screen, mousePosition - [0, 1]. Return true if it's possible to lock gizmo
FORGE_API bool gizmoUpdate(const GizmoUpdateCameraDesc* pCameraDesc, float2* pMousePosition);

// Update gizmo just by camera position without selecting
FORGE_API void gizmoUpdateBackground(const GizmoUpdateCameraDesc* pCameraDesc, bool reset);

// Try to lock gizmo control
FORGE_API bool gizmoSelect();

// Unlock gizmo control
FORGE_API void gizmoUnselect();

// Draw gizmo at the screen
FORGE_API void gizmoDrawOrderRange(TFCmd* pCmd, GizmoRenderTarget* renderTarget, uint16_t startRenderOrder, uint16_t endRenderOrder);
// Draw gizmo at the screen
FORGE_API void gizmoDraw(TFCmd* pCmd, GizmoRenderTarget* renderTarget);

// Get locked gizmo
FORGE_API TFGizmo* gizmoGetLocked();

// Get gizmo touch prioirty
FORGE_API uint32_t gizmoGetTouchPriority(TFGizmo* pGizmo);

// Set gizmo touch prioirty
FORGE_API void gizmoSetTouchPriority(TFGizmo* pGizmo, uint32_t priority);

// Get gizmo render order
FORGE_API uint32_t gizmoGetRenderOrder(TFGizmo* pGizmo);

// Get gizmo render order offset
FORGE_API uint16_t gizmoGetRenderOrderOffset(TFGizmo* pGizmo);

// Set gizmo render prioirty
FORGE_API void gizmoSetRenderPriority(TFGizmo* pGizmo, uint16_t order, uint16_t offset);

// Change gizmo handles state
FORGE_API void gizmoSetHandlesState(TFGizmo* pGizmo, TFGizmoHandlesState state);

// Get gizmo handles state
FORGE_API TFGizmoHandlesState gizmoGetHandlesState(const TFGizmo* pGizmo);

// Change gizmo handles scale mode
FORGE_API void gizmoSetHandlesScaleMode(TFGizmo* pGizmo, TFGizmoHandlesScaleMode mode);

// Get gizmo handles scale mode
FORGE_API TFGizmoHandlesScaleMode gizmoGetHandlesScaleMode(const TFGizmo* pGizmo);

// Change gizmo bound type
FORGE_API void gizmoSetBoundType(TFGizmo* pGizmo, TFGizmoBoundType type);

// Get gizmo bound type
FORGE_API TFGizmoBoundType gizmoGetBoundType(const TFGizmo* pGizmo);

// Get gizmo bound point count
FORGE_API uint32_t gizmoGetBoundPointsCount(TFGizmo* pGizmo);

// Get is gizmo bound point enable
FORGE_API bool gizmoGetBoundPointEnable(TFGizmo* pGizmo, uint32_t pointIdx);

// Set is gizmo bound point enable
FORGE_API void gizmoSetBoundPointEnable(TFGizmo* pGizmo, uint32_t pointIdx, bool enable);

// Check gizmo locked
FORGE_API void gizmoSetActive(TFGizmo* pGizmo, bool active);

// Check gizmo active
FORGE_API bool gizmoIsActive(const TFGizmo* pGizmo);

// Check gizmo valid
FORGE_API bool gizmoIsValid(const TFGizmo* pGizmo);

// Set onSelect callback function
FORGE_API void gizmoSetOnSelectCallback(TFGizmo* pGizmo, void* pUserData, GizmoCallback callback);

// Set onUnselect callback function
FORGE_API void gizmoSetOnUnselectCallback(TFGizmo* pGizmo, void* pUserData, GizmoCallback callback);

// Set onPositionChange callback function
FORGE_API void gizmoSetOnPositionChangeCallback(TFGizmo* pGizmo, void* pUserData, GizmoCallbackF3 callback);

// Set onRotationChange callback function
FORGE_API void gizmoSetOnRotationChangeCallback(TFGizmo* pGizmo, void* pUserData, GizmoCallbackQuat callback);

// Set onScaleChange callback function
FORGE_API void gizmoSetOnScaleChangeCallback(TFGizmo* pGizmo, void* pUserData, GizmoCallbackF3 callback);

// Set position, rotation and scale pointers for gizmo
FORGE_API void gizmoSetTransform(TFGizmo* pGizmo, float3* pPosition, quat* pRotation, float3* pScale);

// Start editing gizmo renderer
FORGE_API void gizmoRendererBegin(TFGizmo* pGizmo);

// Set matrix
FORGE_API void gizmoRendererSetMatrix(float4x4* matrix);

// Set color
FORGE_API void gizmoRendererSetColor(float4* color);

// Set default matrix and color
FORGE_API void gizmoRendererSetDefaultMatrixAndColor();

// Add line
FORGE_API void gizmoRendererAddLine(float3* start, float3* end);

// Add wire rect
FORGE_API void gizmoRendererAddWireRect(float3* center, float2* extend);

// Add wire cube
FORGE_API void gizmoRendererAddWireCube(float3* center, float3* extend);

// Add rect
FORGE_API void gizmoRendererAddRect(float3* center, float2* extend);

// Add cube
FORGE_API void gizmoRendererAddCube(float3* center, float3* extend);

// End editing gizmo renderer
FORGE_API void gizmoRendererEnd();

// Force update buffers for frames
FORGE_API void gizmoRendererForceUpdate(TFGizmo* pGizmo);

// Clear only gizmo redenrer cache
FORGE_API void gizmoRendererClearCache(TFGizmo* pGizmo);

// Clear gizmo redenrer data and memory
FORGE_API void gizmoRendererClear(TFGizmo* pGizmo);

// Get serialization size of pGizmo
FORGE_API size_t gizmoGetSerializationSize(TFGizmo* pGizmo);

// Serialize pGizmo into buffer
FORGE_API size_t gizmoSerialize(TFGizmo* pGizmo, uint8_t* pBuffer, size_t bufferSize);

// Begin deserialization
FORGE_API void gizmoBeginDeserialization();

// Deserialize gizmo data from pBuffer into pGizmo
FORGE_API size_t gizmoDeserialize(TFGizmo* pGizmo, const uint8_t* pBuffer, size_t bufferSize);

// End deserialization
FORGE_API void gizmoEndDeserialization();

#if defined(__cplusplus)
}
#endif

// to do: after make the TFICamera C compatible!
#if defined(__cplusplus)
#include "ICamera.h"

static void getCameraUpdateDesc(const TFICamera* pCamera, GizmoUpdateCameraDesc* pCameraDesc)
{
    TFCameraProjectionParameters projection = pCamera->getProjectionParameters();

    pCameraDesc->mAspectInverse = projection.mAspectInverse;
    pCameraDesc->mFovTangent = tanf(projection.mFovxRadians / 2.0f);
    pCameraDesc->mNearPlane = projection.mZNear;
    pCameraDesc->mFarPlane = projection.mZFar;
    pCameraDesc->mCameraWorldtoLocal = pCamera->getViewMatrix().mMatrices[MONO_CAMERA_VIEW_INDEX];
    pCameraDesc->mCameraLocalToWorld = pCamera->getInverseViewMatrix().mMatrices[MONO_CAMERA_VIEW_INDEX];
    pCameraDesc->mProjection = pCamera->getProjectionMatrix().mMatrices[MONO_CAMERA_VIEW_INDEX];
}

//  The same like main updateGizmo, but take CameraUpdateDesc from TFICamera
FORGE_API inline bool gizmoUpdateFromCamera(const TFICamera* pCameraDesc, float2* mousePosition)
{
    GizmoUpdateCameraDesc cameraDesc;
    getCameraUpdateDesc(pCameraDesc, &cameraDesc);
    return gizmoUpdate(&cameraDesc, mousePosition);
}

//  The same like main updateGizmoBackground, but take CameraUpdateDesc from TFICamera
FORGE_API inline void gizmoUpdateBackgroundFromCamera(const TFICamera* pCameraDesc, bool reset)
{
    GizmoUpdateCameraDesc cameraDesc;
    getCameraUpdateDesc(pCameraDesc, &cameraDesc);
    gizmoUpdateBackground(&cameraDesc, reset);
}
#endif

#endif
