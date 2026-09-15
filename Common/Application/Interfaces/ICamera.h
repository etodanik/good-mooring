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

#include "../../OS/Interfaces/IOperatingSystem.h"

#include "../../Utilities/Interfaces/IMath.h"

typedef struct TFCameraMatrix
{
    mat4 mMatrices[VR_MULTIVIEW_COUNT];
} TFCameraMatrix;

// Maintain shader size for alignment (see TFCameraMatrix definition in fsl)
COMPILE_ASSERT(sizeof(TFCameraMatrix) == sizeof(mat4) * VR_MULTIVIEW_COUNT);

#if defined(QUEST_VR) || defined(HOLOLENS2)
FORGE_API void camMatSuperFrustum(const TFCameraMatrix* views, float zNear, float zFar, mat4* outView, mat4* outProject, bool reverseZ);
#endif
FORGE_API TFCameraMatrix camMatPerspective(float fovxRadians, float aspectInverse, float zNear, float zFar);
FORGE_API TFCameraMatrix camMatPerspectiveReverseZ(float fovxRadians, float aspectInverse, float zNear, float zFar);
FORGE_API TFCameraMatrix camMatMul(const TFCameraMatrix* a, const TFCameraMatrix* b);
FORGE_API TFCameraMatrix mat4MulCamMat(const Matrix4* a, const TFCameraMatrix* b);
FORGE_API TFCameraMatrix camMatMulMat4(const TFCameraMatrix* a, const Matrix4* b);
FORGE_API TFCameraMatrix camMatInverse(const TFCameraMatrix* a);
FORGE_API TFCameraMatrix camMatTranspose(const TFCameraMatrix* a);
FORGE_API TFCameraMatrix camMatOrthographic(float left, float right, float bottom, float top, float zNear, float zFar);
FORGE_API TFCameraMatrix camMatOrthographicReverseZ(float left, float right, float bottom, float top, float zNear, float zFar);
FORGE_API TFCameraMatrix camMatIdentity();
FORGE_API TFCameraMatrix camMatApplyProjectionSampleOffset(const TFCameraMatrix* originalMat, float xOffset, float yOffset);
FORGE_API TFCameraMatrix camMatSetTranslation(const TFCameraMatrix* originalMat, float3 translation);
FORGE_API void           camMatExtractFrustumClipPlanes(const TFCameraMatrix* vp, Vector4* rcp, Vector4* lcp, Vector4* tcp, Vector4* bcp,
                                                        Vector4* fcp, Vector4* ncp, bool const normalizePlanes);

struct TFCameraMotionParameters
{
    float maxSpeed = 160.0f;
    float acceleration = 600.0f; // only used with binary inputs such as keypresses
    float braking = 200.0f;      // also acceleration but orthogonal to the acceleration vector
    float movementSpeed = 1.0f;  // customize move speed
    float rotationSpeed = 1.0f;  // customize rotation speed
};

typedef struct TFCameraProjectionParameters
{
    float mZNear;
    float mZFar;
    float mFovxRadians;
    float mAspectInverse;
} TFCameraProjectionParameters;

enum TFInputBehavior
{
    TF_INPUT_BEHAVIOR_DIRECT,   // direct 1:1 mapping
    TF_INPUT_BEHAVIOR_DISABLED, // ignore input
    TF_INPUT_BEHAVIOR_COUNT
};

struct TFCameraInputMapping
{
    TFInputBehavior moveXBehavior = TF_INPUT_BEHAVIOR_DIRECT;
    TFInputBehavior moveYBehavior = TF_INPUT_BEHAVIOR_DIRECT;
    TFInputBehavior moveZBehavior = TF_INPUT_BEHAVIOR_DIRECT;

    TFInputBehavior rotateXBehavior = TF_INPUT_BEHAVIOR_DIRECT;
    TFInputBehavior rotateYBehavior = TF_INPUT_BEHAVIOR_DIRECT;
};

enum TFExposureType
{
    TF_EXPOSURE_TYPE_NONE,
    TF_EXPOSURE_TYPE_FIXED,
    TF_EXPOSURE_TYPE_MANUAL,
    TF_EXPOSURE_TYPE_DYNAMIC,
};

struct CameraSettings
{
    float ISO = 160.0f;
    float fStop = 11.0f;
    // in millimeters
    float focalLen = 21.0f;
    float apertureDiameterMm = 21.0f / 11.0f;
    // in seconds
    float exposureTimeSeconds = 1.0f / 250.0f;
    // in meters
    float focusDistance = 4.0f;
    float sensorWidth = 36.0f;
    float sensorHeight = 24.0f;

    TFExposureType exposureType = TF_EXPOSURE_TYPE_NONE;

    // 0 = manual focus distance, 1 = automatic center focus
    uint32_t autoFocusMode = 0;
    float    focusSpeed = 5.0f;
    bool     fullAuto = true;
    bool     lockTarget = false;
};

enum TFPostFXStages
{
    TF_POST_FX_STAGE_NONE = 0,
    TF_POST_FX_STAGE_DOF = 1 << 0,
    TF_POST_FX_STAGE_LOCAL_GAMMA = 1 << 1,
    TF_POST_FX_STAGE_COLOR_FILTERS = 1 << 2,
    TF_POST_FX_STAGE_GRAIN = 1 << 3,
    TF_POST_FX_STAGE_ALL = 0x7FFFFFFF
};
MAKE_ENUM_FLAG(uint32_t, TFPostFXStages);

enum TFPostFXColorSpaces
{
    TF_POST_FX_COLOR_SPACE_RGB,
    TF_POST_FX_COLOR_SPACE_YXY,
};

struct TFPostFXSettings
{
    float3              colorFilters = { 1.0f, 1.0f, 1.0f };
    TFPostFXStages      stages = TF_POST_FX_STAGE_NONE;
    TFPostFXColorSpaces colorSpace = TF_POST_FX_COLOR_SPACE_RGB;
    float               maxBlurCoCMm = 0.2f;
    float               cocScale = 1.0f;
    float               colorSaturation = 1.0f;
    float               grainAmount = 0.01f;
    float               grainPixelSize = 1.0f;
    float               fixedEV100 = -0.2630344f;
    float               minEV100 = -6.0f;
    float               maxEV100 = 16.0f;
    float               minLogLuminance = -9.0f;
    float               maxLogLuminance = 13.0f;
    float               exposureCompensationEV = 0.0f;
    float               eyeAdaptationSpeedUp = 2.0f;
    float               eyeAdaptationSpeedDown = 1.0f;
    bool                acesEnabled = true;
    float               acesInputScale = 1.0f;
    float               acesOutputScale = 1.0f;
    float               acesGamma = 1.0f;
};

struct TFCameraPreset
{
    TFCameraMotionParameters camMotion;
    TFCameraInputMapping     camInputMapping;
    CameraSettings           camSettings;
    TFPostFXSettings         postFXSettings;
    float3                   startPosition;
    float3                   startLookAt;

    char name[64];
};

class TFICamera
{
public:
    virtual ~TFICamera() {}
    virtual void setMotionParameters(const TFCameraMotionParameters&) = 0;
    virtual void update(float deltaTime) = 0;

    // there are also implicit dependencies on the keyboard state.

    virtual TFCameraProjectionParameters getProjectionParameters() const = 0;
    virtual TFCameraMatrix               getProjectionMatrix() const = 0;
    /// from world to local
    virtual TFCameraMatrix               getViewMatrix() const = 0;
    /// from local to world
    virtual TFCameraMatrix               getInverseViewMatrix() const = 0;
    virtual vec3                         getViewPosition() const = 0;
    virtual vec2                         getRotationXY() const = 0;
    virtual void                         setProjectionParameters(TFCameraProjectionParameters parameters) = 0;
    virtual void                         moveTo(const vec3& location) = 0;
    virtual void                         lookAt(const vec3& lookAt) = 0;
    virtual void                         setViewRotationXY(const vec2& v) = 0;
    virtual void                         resetView() = 0;

    virtual void onMove(const float2& vec) = 0;
    virtual void onMoveY(float y) = 0;
    virtual void onRotate(const float2& vec) = 0;
    virtual void onZoom(const float2& vec) = 0;

    // used for classes which wish to change state depending on the TFCameraPreset structure.
    // defaults to setting motionParameters if setCameraPreset is not implemented
    virtual void           setCameraPreset(const TFCameraPreset& preset) { setMotionParameters(preset.camMotion); }
    virtual TFCameraPreset getCameraPreset() const
    {
        TFCameraPreset preset = {};
        return preset;
    }
    virtual bool supportsCameraPresets() const { return false; }
};

/// \c initGuiCamera assumes that the camera is not rotated around the look direction;
/// in its matrix, \c Z points at \c startLookAt and \c X is horizontal.
FORGE_API TFICamera* initGuiCamera(const vec3& startPosition, const vec3& startLookAt);

/// \c initFpsCamera does basic FPS-style god mode navigation; tf_free-look is constrained
/// to about +/- 88 degrees and WASD translates in the camera's local XZ plane.
FORGE_API TFICamera* initFpsCamera(const vec3& startPosition, const vec3& startLookAt);

/// \c initDynamicCamera creates a configurable camera controller based on given a TFCameraPreset,
/// which can be set dynamically with setCameraPreset.
FORGE_API TFICamera* initDynamicCamera(const TFCameraPreset& camPreset);

/// \c deserializes a TFCameraPreset from a given file. See Common/Utilities/Reflection/Serialization.h
/// for more info on the required format of this file.
/// returns whether the operation was successful
FORGE_API bool deserializeCameraPreset(const char* pFileName, TFCameraPreset* outCamPreset);

/// \c serializes the given TFCameraPreset to the desired file path.
/// returns whether the operation was successful
FORGE_API bool serializeCameraPreset(const char* pFileName, TFCameraPreset& camPreset);

FORGE_API bool loadCameraPath(const char* pFileName, uint32_t& outNumCameraPoints, float3** pOutCameraPoints);

FORGE_API void exitCamera(TFICamera* pCamera);
