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

#define _USE_MATH_DEFINES
#include "../Utilities/Interfaces/ILog.h"
#include "Interfaces/ICamera.h"

#if defined(QUEST_VR) || defined(HOLOLENS2)
#include "../OS/OpenXR/OpenXRApi.h"
#endif

#include "../Utilities/Reflection/Serialization.h"

// Include this file as last include in all cpp files allocating memory
#include "../Utilities/Interfaces/IMemory.h"

// Required by reflection system to serialize TFCameraPreset from and to a file
REFLECT_STRUCT_BEGIN(TFCameraMotionParameters)
REFLECT_STRUCT_MEMBER(TFCameraMotionParameters, R_FLOAT(float), maxSpeed)
REFLECT_STRUCT_MEMBER(TFCameraMotionParameters, R_FLOAT(float), acceleration)
REFLECT_STRUCT_MEMBER(TFCameraMotionParameters, R_FLOAT(float), braking)
REFLECT_STRUCT_MEMBER(TFCameraMotionParameters, R_FLOAT(float), movementSpeed)
REFLECT_STRUCT_MEMBER(TFCameraMotionParameters, R_FLOAT(float), rotationSpeed)
REFLECT_STRUCT_END(TFCameraMotionParameters)

REFLECT_ENUM_BEGIN(TFInputBehavior)
REFLECT_ENUM_MEMBER(TFInputBehavior, TF_INPUT_BEHAVIOR_DIRECT)
REFLECT_ENUM_MEMBER(TFInputBehavior, TF_INPUT_BEHAVIOR_DISABLED)
REFLECT_ENUM_MEMBER(TFInputBehavior, TF_INPUT_BEHAVIOR_COUNT)
REFLECT_ENUM_END(TFInputBehavior)

REFLECT_STRUCT_BEGIN(TFCameraInputMapping)
REFLECT_STRUCT_MEMBER(TFCameraInputMapping, R_ENUM(TFInputBehavior), moveXBehavior)
REFLECT_STRUCT_MEMBER(TFCameraInputMapping, R_ENUM(TFInputBehavior), moveYBehavior)
REFLECT_STRUCT_MEMBER(TFCameraInputMapping, R_ENUM(TFInputBehavior), moveZBehavior)
REFLECT_STRUCT_MEMBER(TFCameraInputMapping, R_ENUM(TFInputBehavior), rotateXBehavior)
REFLECT_STRUCT_MEMBER(TFCameraInputMapping, R_ENUM(TFInputBehavior), rotateYBehavior)
REFLECT_STRUCT_END(TFCameraInputMapping)

REFLECT_ENUM_BEGIN(TFExposureType)
REFLECT_ENUM_MEMBER(TFExposureType, TF_EXPOSURE_TYPE_NONE)
REFLECT_ENUM_MEMBER(TFExposureType, TF_EXPOSURE_TYPE_FIXED)
REFLECT_ENUM_MEMBER(TFExposureType, TF_EXPOSURE_TYPE_MANUAL)
REFLECT_ENUM_MEMBER(TFExposureType, TF_EXPOSURE_TYPE_DYNAMIC)
REFLECT_ENUM_END(TFExposureType)

REFLECT_STRUCT_BEGIN(CameraSettings)
REFLECT_STRUCT_MEMBER(CameraSettings, R_FLOAT(float), ISO)
REFLECT_STRUCT_MEMBER(CameraSettings, R_FLOAT(float), fStop)
REFLECT_STRUCT_MEMBER(CameraSettings, R_FLOAT(float), focalLen)
REFLECT_STRUCT_MEMBER(CameraSettings, R_FLOAT(float), apertureDiameterMm)
REFLECT_STRUCT_MEMBER(CameraSettings, R_FLOAT(float), exposureTimeSeconds)
REFLECT_STRUCT_MEMBER(CameraSettings, R_FLOAT(float), focusDistance)
REFLECT_STRUCT_MEMBER(CameraSettings, R_FLOAT(float), sensorWidth)
REFLECT_STRUCT_MEMBER(CameraSettings, R_FLOAT(float), sensorHeight)
REFLECT_STRUCT_MEMBER(CameraSettings, R_ENUM(TFExposureType), exposureType)
REFLECT_STRUCT_MEMBER(CameraSettings, R_INT(uint32_t), autoFocusMode)
REFLECT_STRUCT_MEMBER(CameraSettings, R_BOOL(), fullAuto)
REFLECT_STRUCT_MEMBER(CameraSettings, R_FLOAT(float), focusSpeed)
REFLECT_STRUCT_MEMBER(CameraSettings, R_BOOL(), lockTarget)
REFLECT_STRUCT_END(CameraSettings)

REFLECT_ENUM_BEGIN(TFPostFXStages)
REFLECT_ENUM_MEMBER(TFPostFXStages, TF_POST_FX_STAGE_NONE)
REFLECT_ENUM_MEMBER(TFPostFXStages, TF_POST_FX_STAGE_DOF)
REFLECT_ENUM_MEMBER(TFPostFXStages, TF_POST_FX_STAGE_LOCAL_GAMMA)
REFLECT_ENUM_MEMBER(TFPostFXStages, TF_POST_FX_STAGE_COLOR_FILTERS)
REFLECT_ENUM_MEMBER(TFPostFXStages, TF_POST_FX_STAGE_GRAIN)
REFLECT_ENUM_MEMBER(TFPostFXStages, TF_POST_FX_STAGE_ALL)
REFLECT_ENUM_END(TFPostFXStages)

REFLECT_ENUM_BEGIN(TFPostFXColorSpaces)
REFLECT_ENUM_MEMBER(TFPostFXColorSpaces, TF_POST_FX_COLOR_SPACE_RGB)
REFLECT_ENUM_MEMBER(TFPostFXColorSpaces, TF_POST_FX_COLOR_SPACE_YXY)
REFLECT_ENUM_END(TFPostFXColorSpaces)

REFLECT_STRUCT_BEGIN(float3)
REFLECT_STRUCT_MEMBER(float3, R_FLOAT(float), x)
REFLECT_STRUCT_MEMBER(float3, R_FLOAT(float), y)
REFLECT_STRUCT_MEMBER(float3, R_FLOAT(float), z)
REFLECT_STRUCT_END(float3)

REFLECT_STRUCT_BEGIN(TFPostFXSettings)
REFLECT_STRUCT_MEMBER(TFPostFXSettings, R_STRUCT(float3), colorFilters)
REFLECT_STRUCT_MEMBER(TFPostFXSettings, R_INT(uint32_t), stages)
REFLECT_STRUCT_MEMBER(TFPostFXSettings, R_ENUM(TFPostFXColorSpaces), colorSpace)
REFLECT_STRUCT_MEMBER(TFPostFXSettings, R_FLOAT(float), maxBlurCoCMm)
REFLECT_STRUCT_MEMBER(TFPostFXSettings, R_FLOAT(float), cocScale)
REFLECT_STRUCT_MEMBER(TFPostFXSettings, R_FLOAT(float), colorSaturation)
REFLECT_STRUCT_MEMBER(TFPostFXSettings, R_FLOAT(float), grainAmount)
REFLECT_STRUCT_MEMBER(TFPostFXSettings, R_FLOAT(float), grainPixelSize)
REFLECT_STRUCT_MEMBER(TFPostFXSettings, R_FLOAT(float), fixedEV100)
REFLECT_STRUCT_MEMBER(TFPostFXSettings, R_FLOAT(float), minEV100)
REFLECT_STRUCT_MEMBER(TFPostFXSettings, R_FLOAT(float), maxEV100)
REFLECT_STRUCT_MEMBER(TFPostFXSettings, R_FLOAT(float), minLogLuminance)
REFLECT_STRUCT_MEMBER(TFPostFXSettings, R_FLOAT(float), maxLogLuminance)
REFLECT_STRUCT_MEMBER(TFPostFXSettings, R_FLOAT(float), exposureCompensationEV)
REFLECT_STRUCT_MEMBER(TFPostFXSettings, R_FLOAT(float), eyeAdaptationSpeedUp)
REFLECT_STRUCT_MEMBER(TFPostFXSettings, R_FLOAT(float), eyeAdaptationSpeedDown)
REFLECT_STRUCT_MEMBER(TFPostFXSettings, R_BOOL(), acesEnabled)
REFLECT_STRUCT_MEMBER(TFPostFXSettings, R_FLOAT(float), acesInputScale)
REFLECT_STRUCT_MEMBER(TFPostFXSettings, R_FLOAT(float), acesOutputScale)
REFLECT_STRUCT_MEMBER(TFPostFXSettings, R_FLOAT(float), acesGamma)
REFLECT_STRUCT_END(TFPostFXSettings)

REFLECT_STRUCT_BEGIN(TFCameraPreset)
R_HINT_FLAGS(REFLECT_MEMBER_FLAG_STRING)
REFLECT_STRUCT_MEMBER(TFCameraPreset, R_ARRAY(R_INT(char), 64), name)
REFLECT_STRUCT_MEMBER(TFCameraPreset, R_STRUCT(TFCameraMotionParameters), camMotion)
REFLECT_STRUCT_MEMBER(TFCameraPreset, R_STRUCT(TFCameraInputMapping), camInputMapping)
REFLECT_STRUCT_MEMBER(TFCameraPreset, R_STRUCT(CameraSettings), camSettings)
REFLECT_STRUCT_MEMBER(TFCameraPreset, R_STRUCT(TFPostFXSettings), postFXSettings)
REFLECT_STRUCT_MEMBER(TFCameraPreset, R_STRUCT(float3), startPosition)
REFLECT_STRUCT_MEMBER(TFCameraPreset, R_STRUCT(float3), startLookAt)
REFLECT_STRUCT_END(TFCameraPreset)

static const float k_scrollSpeed = -5.0f;

class Camera: public TFICamera
{
public:
    Camera()
    {
        mProjectionParameters.mZFar = 1000;
        mProjectionParameters.mZNear = 0.1f;
        mProjectionParameters.mFovxRadians = PI / 2.0f;
        mProjectionParameters.mAspectInverse = 1;

        cacheProjection = camMatPerspectiveReverseZ(mProjectionParameters.mFovxRadians, mProjectionParameters.mAspectInverse,
                                                    mProjectionParameters.mZNear, mProjectionParameters.mZFar);
    }

    TFCameraProjectionParameters getProjectionParameters() const override;
    TFCameraMatrix               getProjectionMatrix() const override;

    void setProjectionParameters(TFCameraProjectionParameters parameters) override;

    TFCameraMatrix               cacheProjection;
    TFCameraProjectionParameters mProjectionParameters;
};

TFCameraProjectionParameters Camera::getProjectionParameters() const { return mProjectionParameters; }
TFCameraMatrix               Camera::getProjectionMatrix() const { return cacheProjection; }

void Camera::setProjectionParameters(TFCameraProjectionParameters parameters)
{
    mProjectionParameters = parameters;
    cacheProjection = camMatPerspectiveReverseZ(mProjectionParameters.mFovxRadians, mProjectionParameters.mAspectInverse,
                                                mProjectionParameters.mZNear, mProjectionParameters.mZFar);
}

class FpsCamera: public Camera
{
public:
    FpsCamera():
        viewRotation{ 0 }, viewPosition{ 0 }, currentVelocity{ 0 }, acceleration{ 100.0f }, deceleration{ 100.0f }, maxSpeed{ 100.0f },
        movementSpeed{ 1.0f }, rotationSpeed{ 1.0f }
    {
    }
    void setMotionParameters(const TFCameraMotionParameters&) override;

    TFCameraMatrix getViewMatrix() const override;
    TFCameraMatrix getInverseViewMatrix() const override;
    vec3           getViewPosition() const override;
    vec2           getRotationXY() const override { return viewRotation; }

    void moveTo(const vec3& location) override;
    void lookAt(const vec3& lookAt) override;
    void setViewRotationXY(const vec2& v) override { viewRotation = v; }

    void resetView() override
    {
        moveTo(startPosition);
        lookAt(startLookAt);
    }
    void onMove(const float2& vec) override
    {
        dx = vec[0];
        dz = vec[1];
    }
    void onMoveY(float y) override { dy = y; }
    void onRotate(const float2& vec) override
    {
        drx = -vec[1];
        dry = vec[0];
    }
    void onZoom(const float2& vec) override { zoom = vec[1]; }

    void update(float deltaTime) override;

    vec3 startPosition;
    vec3 startLookAt;

    vec2 viewRotation;
    vec3 viewPosition;
    vec3 currentVelocity;

    float acceleration;
    float deceleration;
    float maxSpeed;
    float movementSpeed;
    float rotationSpeed;

    float drx = 0.0f;
    float dry = 0.0f;
    float dx = 0.0f;
    float dy = 0.0f;
    float dz = 0.0f;
    float zoom = 0.0f;
};

TFICamera* initFpsCamera(const vec3& startPosition, const vec3& startLookAt)
{
    FpsCamera* cc = tf_placement_new<FpsCamera>(tf_calloc(1, sizeof(FpsCamera)));
    cc->moveTo(startPosition);
    cc->lookAt(startLookAt);

    cc->startPosition = startPosition;
    cc->startLookAt = startLookAt;

    return cc;
}

// TODO: Move to common file
void exitCamera(TFICamera* pCamera)
{
    pCamera->~TFICamera();
    tf_free(pCamera);
}

void FpsCamera::setMotionParameters(const TFCameraMotionParameters& cmp)
{
    acceleration = cmp.acceleration;
    deceleration = cmp.braking;
    maxSpeed = cmp.maxSpeed;
    movementSpeed = cmp.movementSpeed;
    rotationSpeed = cmp.rotationSpeed;
}

void FpsCamera::update(float deltaTime)
{
    // when frame time is too small (01 in releaseVK) the float comparison with zero is imprecise.
    // It returns when it shouldn't causing stutters
    // We should use doubles for frame time instead of just do this for now.
    deltaTime = max(deltaTime, 0.000001f);

    vec3 moveVec = { dx, dy, dz };

    viewRotation += vec2(drx, dry) * rotationSpeed * deltaTime;

    // divide by length to normalize if necessary
    float lenS = lengthSqr(moveVec);
    // one reason the check with > 1.0 instead of 0.0 is to avoid
    // normalizing when joystick is not fully down.
    if (lenS > 1.0f)
        moveVec /= sqrtf(lenS);

    vec3 accelVec = vec4(moveVec).getXYZ();
    // divide by length to normalize if necessary
    // this determines the directional of acceleration, should be normalized.
    lenS = lengthSqr(accelVec);
    if (lenS > 1.0f)
        accelVec /= sqrtf(lenS);

    // the acceleration vector should still be unit length.
    // assert(fabs(1.0f - lengthSqr(accelVec)) < 0.001f);
    float currentInAccelDir = dot(accelVec, currentVelocity);
    if (currentInAccelDir < 0)
        currentInAccelDir = 0;

    vec3  braking = (accelVec * currentInAccelDir) - currentVelocity;
    float brakingLen = length(braking);
    if (brakingLen > (deceleration * deltaTime))
    {
        braking *= deceleration / brakingLen;
    }
    else
    {
        braking /= deltaTime;
    }

    accelVec = (accelVec * acceleration) + braking;
    vec3  newVelocity = currentVelocity + (accelVec * deltaTime);
    float nvLen = lengthSqr(newVelocity);
    if (nvLen > (maxSpeed * maxSpeed))
    {
        nvLen = sqrtf(nvLen);
        newVelocity *= (maxSpeed / nvLen);
    }

    // create rotation matrix
    mat4 vrRotation = mat4::identity();

#if defined(QUEST_VR) || defined(HOLOLENS2)
    f4x4SetUpperf3x3(vrRotation, inverse(f3x3Identity()));
    viewRotation.x = (0.0f); // No rotation around the x axis when using vr
#endif
    mat4 rot = mat4::rotationYX(viewRotation.y, viewRotation.x) * vrRotation;

    moveVec = (rot * vec4(currentVelocity + newVelocity * .5f, 0.0f) * deltaTime).getXYZ();
    viewPosition += moveVec * movementSpeed;
    currentVelocity = newVelocity;

    if (zoom)
    {
        mat4        m{ mat4::rotationYX(viewRotation.y, viewRotation.x) };
        const vec3& v{ m.v[2].getXYZ() };
        viewPosition -= v * (zoom * k_scrollSpeed);
    }

    drx = 0.0f;
    dry = 0.0f;
    dx = 0.0f;
    dy = 0.0f;
    dz = 0.0f;
}

TFCameraMatrix FpsCamera::getViewMatrix() const
{
    TFCameraMatrix result;
    mat4           r = mat4::rotationXY(-viewRotation.x, -viewRotation.y);

#if !defined(QUEST_VR) && !defined(HOLOLENS2)
    result.mMatrices[MONO_CAMERA_VIEW_INDEX] = r;
    vec4 t = r * vec4(-viewPosition, 1.0f);
    result.mMatrices[MONO_CAMERA_VIEW_INDEX].setTranslation(t.getXYZ());
#else
    GetOpenXRViewMatrix(LEFT_EYE_VIEW_INDEX, &result.mMatrices[LEFT_EYE_VIEW_INDEX]);
    GetOpenXRViewMatrix(RIGHT_EYE_VIEW_INDEX, &result.mMatrices[RIGHT_EYE_VIEW_INDEX]);
    result.mMatrices[LEFT_EYE_VIEW_INDEX] = result.mMatrices[LEFT_EYE_VIEW_INDEX] * r;
    result.mMatrices[RIGHT_EYE_VIEW_INDEX] = result.mMatrices[RIGHT_EYE_VIEW_INDEX] * r;
    vec4 tLeft = result.mMatrices[LEFT_EYE_VIEW_INDEX] * vec4(-viewPosition, 1.0f);
    vec4 tRight = result.mMatrices[RIGHT_EYE_VIEW_INDEX] * vec4(-viewPosition, 1.0f);
    result.mMatrices[LEFT_EYE_VIEW_INDEX].setTranslation(tLeft.getXYZ());
    result.mMatrices[RIGHT_EYE_VIEW_INDEX].setTranslation(tRight.getXYZ());
#endif // QUEST_VR

    return result;
}

TFCameraMatrix FpsCamera::getInverseViewMatrix() const
{
    TFCameraMatrix result;

#if !defined(QUEST_VR) && !defined(HOLOLENS2)
    result.mMatrices[MONO_CAMERA_VIEW_INDEX] = transpose(mat4::rotationXY(-viewRotation.x, -viewRotation.y));
    result.mMatrices[MONO_CAMERA_VIEW_INDEX].setTranslation(viewPosition);
#else
    GetOpenXRViewMatrix(LEFT_EYE_VIEW_INDEX, &result.mMatrices[LEFT_EYE_VIEW_INDEX]);
    GetOpenXRViewMatrix(RIGHT_EYE_VIEW_INDEX, &result.mMatrices[RIGHT_EYE_VIEW_INDEX]);
    mat4 m = transpose(mat4::rotationXY(-viewRotation.x, -viewRotation.y));
    m.setTranslation(viewPosition);
    result.mMatrices[LEFT_EYE_VIEW_INDEX] = result.mMatrices[LEFT_EYE_VIEW_INDEX] * m;
    result.mMatrices[RIGHT_EYE_VIEW_INDEX] = result.mMatrices[RIGHT_EYE_VIEW_INDEX] * m;
#endif // QUEST_VR

    return result;
}

vec3 FpsCamera::getViewPosition() const { return viewPosition; }

void FpsCamera::moveTo(const vec3& location)
{
    viewPosition = location;
    currentVelocity = vec3(0);
}

void FpsCamera::lookAt(const vec3& lookAt)
{
    vec3 lookDir = normalize(lookAt - viewPosition);

    float y = lookDir.y;
    viewRotation.x = (-asinf(y));

    float x = lookDir.x;
    float z = lookDir.z;
    float n = sqrtf((x * x) + (z * z));
    if (n > 0.01f)
    {
        // don't change the Y rotation if we're too close to vertical
        x /= n;
        z /= n;
        viewRotation.y = (atan2f(x, z));
    }
}

class GuiCamera: public Camera
{
public:
    GuiCamera(): viewRotation{ 0 }, viewPosition{ 0 }, velocity{ 0 }, maxSpeed{ 1.0f } {}
    void setMotionParameters(const TFCameraMotionParameters& cmp) override { maxSpeed = cmp.maxSpeed; }

    void update(float deltaTime) override
    {
        viewPosition += velocity * deltaTime;
        velocity = vec3{ 0 };
    }

    TFCameraMatrix getViewMatrix() const override
    {
        TFCameraMatrix result;
#if !defined(QUEST_VR) && !defined(HOLOLENS2)
        result.mMatrices[MONO_CAMERA_VIEW_INDEX] = mat4::rotationXY(-viewRotation.x, -viewRotation.y);
        vec4 t = result.mMatrices[MONO_CAMERA_VIEW_INDEX] * vec4(-viewPosition, 1.0f);
        result.mMatrices[MONO_CAMERA_VIEW_INDEX].setTranslation(t.getXYZ());
#else
        result.mMatrices[LEFT_EYE_VIEW_INDEX] = mat4::rotationXY(-viewRotation.x, -viewRotation.y);
        vec4 t = result.mMatrices[LEFT_EYE_VIEW_INDEX] * vec4(-viewPosition, 1.0f);
        result.mMatrices[LEFT_EYE_VIEW_INDEX].setTranslation(t.getXYZ());
        result.mMatrices[RIGHT_EYE_VIEW_INDEX] = result.mMatrices[LEFT_EYE_VIEW_INDEX];
#endif // QUEST_VR
        return result;
    }

    TFCameraMatrix getInverseViewMatrix() const override
    {
        TFCameraMatrix result;
#if !defined(QUEST_VR) && !defined(HOLOLENS2)
        result.mMatrices[MONO_CAMERA_VIEW_INDEX] = transpose(mat4::rotationXY(-viewRotation.x, -viewRotation.y));
        result.mMatrices[MONO_CAMERA_VIEW_INDEX].setTranslation(viewPosition);
#else
        result.mMatrices[LEFT_EYE_VIEW_INDEX] = transpose(mat4::rotationXY(-viewRotation.x, -viewRotation.y));
        result.mMatrices[LEFT_EYE_VIEW_INDEX].setTranslation(viewPosition);
        result.mMatrices[RIGHT_EYE_VIEW_INDEX] = result.mMatrices[LEFT_EYE_VIEW_INDEX];
#endif // QUEST_VR

        return result;
    }

    vec3 getViewPosition() const override { return viewPosition; }

    void moveTo(const vec3& location) override { viewPosition = location; }

    void lookAt(const vec3& lookAt) override
    {
        vec3 lookDir = normalize(lookAt - viewPosition);

        float y = lookDir.y;
        viewRotation.x = (-asinf(y));

        float x = lookDir.x;
        float z = lookDir.z;
        float n = sqrtf((x * x) + (z * z));
        if (n > 0.01f)
        {
            // don't change the Y rotation if we're too close to vertical
            x /= n;
            z /= n;
            viewRotation.y = (atan2f(x, z));
        }
    }

    void setViewRotationXY(const vec2& v) override { viewRotation = v; }

    vec2 getRotationXY() const override { return viewRotation; }

    void resetView() override
    {
        moveTo(startPosition);
        lookAt(startLookAt);
    }
    void onMove(const float2& vec) override { UNREF_PARAM(vec); }
    void onMoveY(float y) override { UNREF_PARAM(y); }
    void onRotate(const float2& vec) override { UNREF_PARAM(vec); }
    void onZoom(const float2& vec) override { UNREF_PARAM(vec); }

    // We put viewRotation at first becuase viewPosition is 16 bytes aligned. We have vtable pointer 8 bytes + vec2(8 bytes). This avoids
    // unnecessary padding.
    vec2  viewRotation;
    vec3  viewPosition;
    vec3  velocity;
    float maxSpeed;
    vec3  startPosition;
    vec3  startLookAt;
};

TFICamera* initGuiCamera(const vec3& startPosition, const vec3& startLookAt)
{
    GuiCamera* cc = tf_placement_new<GuiCamera>(tf_calloc(1, sizeof(GuiCamera)));
    cc->moveTo(startPosition);
    cc->lookAt(startLookAt);
    cc->startPosition = startPosition;
    cc->startLookAt = startLookAt;
    return cc;
}

void exitGuiCamera(TFICamera* pCamera)
{
    pCamera->~TFICamera();
    tf_free(pCamera);
}

class DynamicCamera: public Camera
{
public:
    void setMotionParameters(const TFCameraMotionParameters& cmp) override { camPreset.camMotion = cmp; }

    TFCameraMatrix getViewMatrix() const override;
    TFCameraMatrix getInverseViewMatrix() const override;
    vec3           getViewPosition() const override { return viewPosition; };
    vec2           getRotationXY() const override { return viewRotation; }

    void moveTo(const vec3& location) override;
    void lookAt(const vec3& lookAt) override;
    void setViewRotationXY(const vec2& v) override { viewRotation = v; }

    float processInput(float axis, TFInputBehavior behavior);

    void resetView() override
    {
        moveTo(camPreset.startPosition);
        lookAt(camPreset.startLookAt);
    }

    void onMove(const float2& vec) override
    {
        float processedX = processInput(vec[0], camPreset.camInputMapping.moveXBehavior);
        float processedZ = processInput(vec[1], camPreset.camInputMapping.moveZBehavior);

        dx = processedX;
        dz = processedZ;
    }

    void onMoveY(float y) override
    {
        float processedY = processInput(y, camPreset.camInputMapping.moveYBehavior);
        dy = processedY;
    }

    void onRotate(const float2& vec) override
    {
        float processedRX = processInput(vec[0], camPreset.camInputMapping.rotateXBehavior);
        float processedRY = processInput(vec[1], camPreset.camInputMapping.rotateYBehavior);

        drx = -processedRY;
        dry = processedRX;
    }

    void onZoom(const float2& vec) override { UNREF_PARAM(vec); }

    void update(float deltaTime) override;

    void setCameraPreset(const TFCameraPreset& preset) override { camPreset = preset; };

    TFCameraPreset getCameraPreset() const override { return camPreset; }

    bool supportsCameraPresets() const override { return true; }

    TFCameraPreset camPreset = {};

    vec2 viewRotation = { 0, 0 };
    vec3 viewPosition = { 0, 0, 0 };
    vec3 currentVelocity = { 0, 0, 0 };

    float dx = 0.0f;
    float dy = 0.0f;
    float dz = 0.0f;

    float drx = 0.0f;
    float dry = 0.0f;
};

TFICamera* initDynamicCamera(const TFCameraPreset& camPreset)
{
    DynamicCamera* cc = tf_placement_new<DynamicCamera>(tf_calloc(1, sizeof(DynamicCamera)));
    cc->moveTo(camPreset.startPosition);
    cc->lookAt(camPreset.startLookAt);

    cc->camPreset = camPreset;

    return cc;
}

TFCameraMatrix DynamicCamera::getViewMatrix() const
{
    TFCameraMatrix result;
    mat4           r = mat4::rotationXY(-viewRotation.x, -viewRotation.y);

#if !defined(QUEST_VR) && !defined(HOLOLENS2)
    result.mMatrices[MONO_CAMERA_VIEW_INDEX] = r;
    vec4 t = r * vec4(-viewPosition, 1.0f);
    result.mMatrices[MONO_CAMERA_VIEW_INDEX].setTranslation(t.getXYZ());
#else
    GetOpenXRViewMatrix(LEFT_EYE_VIEW_INDEX, &result.mMatrices[LEFT_EYE_VIEW_INDEX]);
    GetOpenXRViewMatrix(RIGHT_EYE_VIEW_INDEX, &result.mMatrices[RIGHT_EYE_VIEW_INDEX]);
    result.mMatrices[LEFT_EYE_VIEW_INDEX] = result.mMatrices[LEFT_EYE_VIEW_INDEX] * r;
    result.mMatrices[RIGHT_EYE_VIEW_INDEX] = result.mMatrices[RIGHT_EYE_VIEW_INDEX] * r;
    vec4 tLeft = result.mMatrices[LEFT_EYE_VIEW_INDEX] * vec4(-viewPosition, 1.0f);
    vec4 tRight = result.mMatrices[RIGHT_EYE_VIEW_INDEX] * vec4(-viewPosition, 1.0f);
    result.mMatrices[LEFT_EYE_VIEW_INDEX].setTranslation(tLeft.getXYZ());
    result.mMatrices[RIGHT_EYE_VIEW_INDEX].setTranslation(tRight.getXYZ());
#endif

    return result;
}

TFCameraMatrix DynamicCamera::getInverseViewMatrix() const
{
    TFCameraMatrix result;

#if !defined(QUEST_VR) && !defined(HOLOLENS2)
    result.mMatrices[MONO_CAMERA_VIEW_INDEX] = transpose(mat4::rotationXY(-viewRotation.x, -viewRotation.y));
    result.mMatrices[MONO_CAMERA_VIEW_INDEX].setTranslation(viewPosition);
#else
    GetOpenXRViewMatrix(LEFT_EYE_VIEW_INDEX, &result.mMatrices[LEFT_EYE_VIEW_INDEX]);
    GetOpenXRViewMatrix(RIGHT_EYE_VIEW_INDEX, &result.mMatrices[RIGHT_EYE_VIEW_INDEX]);
    mat4 m = transpose(mat4::rotationXY(-viewRotation.x, -viewRotation.y));
    m.setTranslation(viewPosition);
    result.mMatrices[LEFT_EYE_VIEW_INDEX] = result.mMatrices[LEFT_EYE_VIEW_INDEX] * m;
    result.mMatrices[RIGHT_EYE_VIEW_INDEX] = result.mMatrices[RIGHT_EYE_VIEW_INDEX] * m;
#endif // QUEST_VR

    return result;
}

void DynamicCamera::lookAt(const vec3& lookAt)
{
    vec3 lookDir = normalize(lookAt - viewPosition);

    float y = lookDir.y;
    viewRotation.x = (-asinf(y));

    float x = lookDir.x;
    float z = lookDir.z;
    float n = sqrtf((x * x) + (z * z));
    if (n > 0.01f)
    {
        x /= n;
        z /= n;
        viewRotation.y = (atan2f(x, z));
    }
}

float DynamicCamera::processInput(float input, TFInputBehavior behavior)
{
    switch (behavior)
    {
    case TF_INPUT_BEHAVIOR_DISABLED:
        return 0.0f;
    default:
        return input;
    }
}

void DynamicCamera::moveTo(const vec3& location)
{
    viewPosition = location;
    currentVelocity = vec3(0);
}

void DynamicCamera::update(float deltaTime)
{
    const TFCameraMotionParameters& camMotion = camPreset.camMotion;

    // when frame time is too small (01 in releaseVK) the float comparison with zero is imprecise.
    // It returns when it shouldn't causing stutters
    // We should use doubles for frame time instead of just do this for now.
    deltaTime = max(deltaTime, 0.000001f);

    vec3 moveVec = { dx, dy, dz };

    viewRotation += vec2(drx, dry) * camMotion.rotationSpeed * deltaTime;

    // divide by length to normalize if necessary
    float lenS = lengthSqr(moveVec);
    // one reason the check with > 1.0 instead of 0.0 is to avoid
    // normalizing when joystick is not fully down.
    if (lenS > 1.0f)
        moveVec /= sqrtf(lenS);

    vec3 accelVec = vec4(moveVec).getXYZ();
    // divide by length to normalize if necessary
    // this determines the directional of acceleration, should be normalized.
    lenS = lengthSqr(accelVec);
    if (lenS > 1.0f)
        accelVec /= sqrtf(lenS);

    // the acceleration vector should still be unit length.
    // assert(fabs(1.0f - lengthSqr(accelVec)) < 0.001f);
    float currentInAccelDir = dot(accelVec, currentVelocity);
    if (currentInAccelDir < 0)
        currentInAccelDir = 0;

    vec3  braking = (accelVec * currentInAccelDir) - currentVelocity;
    float brakingLen = length(braking);
    if (brakingLen > (camMotion.braking * deltaTime))
    {
        braking *= camMotion.braking / brakingLen;
    }
    else
    {
        braking /= deltaTime;
    }

    accelVec = (accelVec * camMotion.acceleration) + braking;
    vec3  newVelocity = currentVelocity + (accelVec * deltaTime);
    float nvLen = lengthSqr(newVelocity);
    if (nvLen > (camMotion.maxSpeed * camMotion.maxSpeed))
    {
        nvLen = sqrtf(nvLen);
        newVelocity *= (camMotion.maxSpeed / nvLen);
    }

    // create rotation matrix
    mat4 vrRotation = mat4::identity();

#if defined(QUEST_VR) || defined(HOLOLENS2)
    f4x4SetUpperf3x3(vrRotation, inverse(f3x3Identity()));
    viewRotation.x = (0.0f); // No rotation around the x axis when using vr
#endif
    mat4 rot = mat4::rotationYX(viewRotation.y, viewRotation.x) * vrRotation;

    moveVec = (rot * vec4(currentVelocity + newVelocity * .5f, 0.0f) * deltaTime).getXYZ();
    viewPosition += moveVec * camMotion.movementSpeed;
    currentVelocity = newVelocity;

    drx = 0.0f;
    dry = 0.0f;
    dx = 0.0f;
    dy = 0.0f;
    dz = 0.0f;
}

bool deserializeCameraPreset(const char* pFileName, TFCameraPreset* outCamPreset)
{
    return deserializeStructFromFile(TF_RD_OTHER_FILES, pFileName, GET_TYPE_INFO(TFCameraPreset), outCamPreset);
}

bool serializeCameraPreset(const char* pFileName, TFCameraPreset& camPreset)
{
    return serializeStructToFile(TF_RD_OTHER_FILES, pFileName, GET_TYPE_INFO(TFCameraPreset), &camPreset);
}

bool loadCameraPath(const char* pFileName, uint32_t& outNumCameraPoints, float3** pOutCameraPoints)
{
    TFFileStream fh = {};
    if (!fsOpenStreamFromPath(TF_RD_OTHER_FILES, pFileName, TF_FM_READ, &fh))
    {
        LOGF(LogLevel::eERROR, "Failed to open the camera path file. Function %s failed with error: %s", FS_ERR_CTX.func,
             getFSErrCodeString(FS_ERR_CTX.code));
        return false;
    }

    // Read whole file..
    ssize_t fhSize = fsGetStreamFileSize(&fh);
    char*   pBuffer = (char*)tf_malloc(sizeof(char) * fhSize);
    fsReadFromStream(&fh, pBuffer, sizeof(char) * fhSize);
    fsCloseStream(&fh);

    // Skip first line that contains a comment..
    char* cBuffer = strchr(pBuffer, '\n');
    if (cBuffer)
        cBuffer += 1;
    // Find number of points and skip the line..
    outNumCameraPoints = cBuffer ? atoi(cBuffer) : 0;
    if (outNumCameraPoints == 0)
        return false;

    float3* pCameraPoints = (float3*)tf_malloc(sizeof(float3) * outNumCameraPoints);

    cBuffer = strchr(cBuffer, '\n');
    if (cBuffer)
        cBuffer += 1;

    // Parse num 'pCameraPathPoints' lines in file..
    for (uint32_t i = 0; i < outNumCameraPoints; ++i)
    {
        // Parse the position at index i: Each line has 3 floats...
        for (uint32_t j = 0; j < 3; ++j)
        {
            if (cBuffer)
            {
                pCameraPoints[i][j] = (float)atof(cBuffer);
                cBuffer = strchr(cBuffer, ',');
                if (cBuffer)
                    cBuffer += 1;
            }
        }
        // skip line (newline character)..
        if (!cBuffer || (pBuffer - cBuffer) + 1 == fhSize)
        {
            LOGF(eERROR, "Failed to parse cameraPath.txt.");
            break;
        }
        cBuffer += 1;
    }

    tf_free(pBuffer);

    *pOutCameraPoints = pCameraPoints;
    return true;
}

#if defined(QUEST_VR) || defined(HOLOLENS2)
// Create a new projection matrix based on the left and right eye asymmetric FOV matrices. This combines the maximum
// field of view on each side (left, right, top, bottom). This will result in a new asymmetric matrix that will
// encompass both eyes. Move camera back the necessary amount to get the new frustum to encompass both eyes.
// This is useful to run a single pass triangle culling algorithm.
// Inspired by Oculus' work here: https://i.sstatic.net/TpHYa.jpg
void camMatSuperFrustum(const TFCameraMatrix* views, float zNear, float zFar, mat4* outView, mat4* outProject, bool reverseZ)
{
    float4 leftEyeFovs;
    float4 rightEyeFovs;
    GetOpenXRViewFovs(&leftEyeFovs, &rightEyeFovs);

    // Vector pointing from left eye to right eye
    const float3 leftEyeToRightEyeDir =
        f4GetXYZ(f4Sub(f4x4GetCol(views->mMatrices[RIGHT_EYE_VIEW_INDEX], 3), f4x4GetCol(views->mMatrices[LEFT_EYE_VIEW_INDEX], 3)));

    const float3 normalizedLeftToRight = f3Normalize(leftEyeToRightEyeDir);

    // IPD is the distance between each view's origin, which is the real world distance between each eye
    float ipd = f3Length(leftEyeToRightEyeDir);

    // expand fov a bit to fix triangles on edges
    float leftFov = leftEyeFovs.x * 1.025f;
    float rightFov = rightEyeFovs.y * 1.025f;
    float topFov = leftEyeFovs.z;
    float bottomFov = leftEyeFovs.w;

    // How we need the new view origin to go back
    float  recession = ipd / (tanf(-leftFov) + tanf(rightFov));
    // The new view origin offset along the leftEyeToRightEyeDir axis
    float  viewCenterOffset = ipd * tanf(-leftFov);
    float3 viewDirection = f4GetXYZ(f4Normalize(f4MulScalar(f4x4GetRow(views->mMatrices[LEFT_EYE_VIEW_INDEX], 2), -1.0f)));

    float3 superFrustumOrigin = f4GetXYZ(f4x4GetCol(views->mMatrices[LEFT_EYE_VIEW_INDEX], 3));
    superFrustumOrigin = f3Add(superFrustumOrigin, f3MulScalar(normalizedLeftToRight, viewCenterOffset));
    superFrustumOrigin = f3Sub(superFrustumOrigin, f3MulScalar(viewDirection, recession));

    *outView = views->mMatrices[LEFT_EYE_VIEW_INDEX];
    *outView = f4x4SetTranslation(*outView, superFrustumOrigin);

    if (reverseZ)
    {
        *outProject = f4x4PerspectiveLH_ReverseZ_AsymmetricFov(-leftFov, rightFov, topFov, -bottomFov, zNear, zFar, false);
    }
    else
    {
        *outProject = f4x4PerspectiveLH_AsymmetricFov(-leftFov, rightFov, topFov, -bottomFov, zNear, zFar, false);
    }
}
#endif // QUEST_VR

TFCameraMatrix camMatPerspective(float fovxRadians, float aspectInverse, float zNear, float zFar)
{
    TFCameraMatrix result;
#if defined(QUEST_VR) || defined(HOLOLENS2)
    UNREF_PARAM(fovxRadians);
    UNREF_PARAM(aspectInverse);
    GetOpenXRProjMatrixPerspective(LEFT_EYE_VIEW_INDEX, zNear, zFar, &result.mMatrices[LEFT_EYE_VIEW_INDEX]);
    GetOpenXRProjMatrixPerspective(RIGHT_EYE_VIEW_INDEX, zNear, zFar, &result.mMatrices[RIGHT_EYE_VIEW_INDEX]);
#else
    result.mMatrices[MONO_CAMERA_VIEW_INDEX] = f4x4PerspectiveLH(fovxRadians, aspectInverse, zNear, zFar);
#endif
    return result;
}

TFCameraMatrix camMatPerspectiveReverseZ(float fovxRadians, float aspectInverse, float zNear, float zFar)
{
    TFCameraMatrix result;
#if defined(QUEST_VR) || defined(HOLOLENS2)
    UNREF_PARAM(fovxRadians);
    UNREF_PARAM(aspectInverse);
    GetOpenXRProjMatrixPerspectiveReverseZ(LEFT_EYE_VIEW_INDEX, zNear, zFar, &result.mMatrices[LEFT_EYE_VIEW_INDEX]);
    GetOpenXRProjMatrixPerspectiveReverseZ(RIGHT_EYE_VIEW_INDEX, zNear, zFar, &result.mMatrices[RIGHT_EYE_VIEW_INDEX]);
#else
    result.mMatrices[MONO_CAMERA_VIEW_INDEX] = f4x4PerspectiveLH_ReverseZ(fovxRadians, aspectInverse, zNear, zFar);
#endif
    return result;
}

TFCameraMatrix camMatMul(const TFCameraMatrix* a, const TFCameraMatrix* b)
{
    TFCameraMatrix result;
#if defined(QUEST_VR) || defined(HOLOLENS2)
    result.mMatrices[LEFT_EYE_VIEW_INDEX] = f4x4Mul(a->mMatrices[LEFT_EYE_VIEW_INDEX], b->mMatrices[LEFT_EYE_VIEW_INDEX]);
    result.mMatrices[RIGHT_EYE_VIEW_INDEX] = f4x4Mul(a->mMatrices[RIGHT_EYE_VIEW_INDEX], b->mMatrices[RIGHT_EYE_VIEW_INDEX]);
#else
    result.mMatrices[MONO_CAMERA_VIEW_INDEX] = f4x4Mul(a->mMatrices[MONO_CAMERA_VIEW_INDEX], b->mMatrices[MONO_CAMERA_VIEW_INDEX]);
#endif
    return result;
}

TFCameraMatrix mat4MulCamMat(const Matrix4* a, const TFCameraMatrix* b)
{
    TFCameraMatrix result;
#if defined(QUEST_VR) || defined(HOLOLENS2)
    result.mMatrices[LEFT_EYE_VIEW_INDEX] = f4x4Mul(*a, b->mMatrices[LEFT_EYE_VIEW_INDEX]);
    result.mMatrices[RIGHT_EYE_VIEW_INDEX] = f4x4Mul(*a, b->mMatrices[RIGHT_EYE_VIEW_INDEX]);
#else
    result.mMatrices[MONO_CAMERA_VIEW_INDEX] = f4x4Mul(*a, b->mMatrices[MONO_CAMERA_VIEW_INDEX]);
#endif
    return result;
}

TFCameraMatrix camMatMulMat4(const TFCameraMatrix* a, const Matrix4* b)
{
    TFCameraMatrix result;
#if defined(QUEST_VR) || defined(HOLOLENS2)
    result.mMatrices[LEFT_EYE_VIEW_INDEX] = f4x4Mul(a->mMatrices[LEFT_EYE_VIEW_INDEX], *b);
    result.mMatrices[RIGHT_EYE_VIEW_INDEX] = f4x4Mul(a->mMatrices[RIGHT_EYE_VIEW_INDEX], *b);
#else
    result.mMatrices[MONO_CAMERA_VIEW_INDEX] = f4x4Mul(a->mMatrices[MONO_CAMERA_VIEW_INDEX], *b);
#endif
    return result;
}

TFCameraMatrix camMatInverse(const TFCameraMatrix* a)
{
    TFCameraMatrix result;
#if defined(QUEST_VR) || defined(HOLOLENS2)
    result.mMatrices[LEFT_EYE_VIEW_INDEX] = f4x4Inverse(a->mMatrices[LEFT_EYE_VIEW_INDEX]);
    result.mMatrices[RIGHT_EYE_VIEW_INDEX] = f4x4Inverse(a->mMatrices[RIGHT_EYE_VIEW_INDEX]);
#else
    result.mMatrices[MONO_CAMERA_VIEW_INDEX] = f4x4Inverse(a->mMatrices[MONO_CAMERA_VIEW_INDEX]);
#endif
    return result;
}

TFCameraMatrix camMatTranspose(const TFCameraMatrix* a)
{
    TFCameraMatrix result;
#if defined(QUEST_VR) || defined(HOLOLENS2)
    result.mMatrices[LEFT_EYE_VIEW_INDEX] = f4x4Transpose(a->mMatrices[LEFT_EYE_VIEW_INDEX]);
    result.mMatrices[RIGHT_EYE_VIEW_INDEX] = f4x4Transpose(a->mMatrices[RIGHT_EYE_VIEW_INDEX]);
#else
    result.mMatrices[MONO_CAMERA_VIEW_INDEX] = f4x4Transpose(a->mMatrices[MONO_CAMERA_VIEW_INDEX]);
#endif
    return result;
}

TFCameraMatrix camMatOrthographic(float left, float right, float bottom, float top, float zNear, float zFar)
{
    TFCameraMatrix result;
#if defined(QUEST_VR) || defined(HOLOLENS2)
    result.mMatrices[LEFT_EYE_VIEW_INDEX] = f4x4OrthographicLH(left, right, bottom, top, zNear, zFar);
    result.mMatrices[RIGHT_EYE_VIEW_INDEX] = result.mMatrices[LEFT_EYE_VIEW_INDEX];
#else
    result.mMatrices[MONO_CAMERA_VIEW_INDEX] = f4x4OrthographicLH(left, right, bottom, top, zNear, zFar);
#endif
    return result;
}

TFCameraMatrix camMatOrthographicReverseZ(float left, float right, float bottom, float top, float zNear, float zFar)
{
    TFCameraMatrix result;
#if defined(QUEST_VR) || defined(HOLOLENS2)
    result.mMatrices[LEFT_EYE_VIEW_INDEX] = f4x4OrthographicLH(left, right, bottom, top, zFar, zNear);
    result.mMatrices[RIGHT_EYE_VIEW_INDEX] = result.mMatrices[LEFT_EYE_VIEW_INDEX];
#else
    result.mMatrices[MONO_CAMERA_VIEW_INDEX] = f4x4OrthographicLH_ReverseZ(left, right, bottom, top, zNear, zFar);
#endif
    return result;
}

TFCameraMatrix camMatIdentity()
{
    TFCameraMatrix result;
#if defined(QUEST_VR) || defined(HOLOLENS2)
    result.mMatrices[LEFT_EYE_VIEW_INDEX] = f4x4Identity();
    result.mMatrices[RIGHT_EYE_VIEW_INDEX] = result.mMatrices[LEFT_EYE_VIEW_INDEX];
#else
    result.mMatrices[MONO_CAMERA_VIEW_INDEX] = f4x4Identity();
#endif
    return result;
}

// Applies offsets to the projection matrices (useful when needing to jitter the camera for techniques like TAA)
TFCameraMatrix camMatApplyProjectionSampleOffset(const TFCameraMatrix* originalMat, float xOffset, float yOffset)
{
    TFCameraMatrix result = *originalMat;
#if defined(QUEST_VR) || defined(HOLOLENS2)
    result.mMatrices[LEFT_EYE_VIEW_INDEX].v[2].x += xOffset;
    result.mMatrices[LEFT_EYE_VIEW_INDEX].v[2].y += yOffset;
    result.mMatrices[RIGHT_EYE_VIEW_INDEX].v[2].x += xOffset;
    result.mMatrices[RIGHT_EYE_VIEW_INDEX].v[2].y += yOffset;
#else
    result.mMatrices[MONO_CAMERA_VIEW_INDEX].v[2].x += xOffset;
    result.mMatrices[MONO_CAMERA_VIEW_INDEX].v[2].y += yOffset;
#endif
    return result;
}

TFCameraMatrix camMatSetTranslation(const TFCameraMatrix* originalMat, float3 translation)
{
    TFCameraMatrix result = *originalMat;
#if defined(QUEST_VR) || defined(HOLOLENS2)
    result.mMatrices[LEFT_EYE_VIEW_INDEX] = f4x4SetTranslation(result.mMatrices[LEFT_EYE_VIEW_INDEX], translation);
    result.mMatrices[RIGHT_EYE_VIEW_INDEX] = f4x4SetTranslation(result.mMatrices[RIGHT_EYE_VIEW_INDEX], translation);
#else
    result.mMatrices[MONO_CAMERA_VIEW_INDEX] = f4x4SetTranslation(result.mMatrices[MONO_CAMERA_VIEW_INDEX], translation);
#endif
    return result;
}

void camMatExtractFrustumClipPlanes(const TFCameraMatrix* vp, Vector4* rcp, Vector4* lcp, Vector4* tcp, Vector4* bcp, Vector4* fcp,
                                    Vector4* ncp, bool const normalizePlanes)
{
#if defined(QUEST_VR) || defined(HOLOLENS2)
    // Left plane
    *lcp = f4Add(f4x4GetRow(vp->mMatrices[LEFT_EYE_VIEW_INDEX], 3), f4x4GetRow(vp->mMatrices[LEFT_EYE_VIEW_INDEX], 0));

    // Right plane
    *rcp = f4Sub(f4x4GetRow(vp->mMatrices[RIGHT_EYE_VIEW_INDEX], 3), f4x4GetRow(vp->mMatrices[RIGHT_EYE_VIEW_INDEX], 0));

    // Bottom plane
    *bcp = f4Add(f4x4GetRow(vp->mMatrices[LEFT_EYE_VIEW_INDEX], 3), f4x4GetRow(vp->mMatrices[LEFT_EYE_VIEW_INDEX], 1));

    // Top plane
    *tcp = f4Sub(f4x4GetRow(vp->mMatrices[LEFT_EYE_VIEW_INDEX], 3), f4x4GetRow(vp->mMatrices[LEFT_EYE_VIEW_INDEX], 1));

    // Near plane
    *ncp = f4Add(f4x4GetRow(vp->mMatrices[LEFT_EYE_VIEW_INDEX], 3), f4x4GetRow(vp->mMatrices[LEFT_EYE_VIEW_INDEX], 2));

    // Far plane
    *fcp = f4Sub(f4x4GetRow(vp->mMatrices[LEFT_EYE_VIEW_INDEX], 3), f4x4GetRow(vp->mMatrices[LEFT_EYE_VIEW_INDEX], 2));

    // Normalize if needed
    if (normalizePlanes)
    {
        float lcpNorm = f3Length(f4GetXYZ(*lcp));
        *lcp = f4DivScalar(*lcp, lcpNorm);

        float rcpNorm = f3Length(f4GetXYZ(*rcp));
        *rcp = f4DivScalar(*rcp, rcpNorm);

        float bcpNorm = f3Length(f4GetXYZ(*bcp));
        *bcp = f4DivScalar(*bcp, bcpNorm);

        float tcpNorm = f3Length(f4GetXYZ(*tcp));
        *tcp = f4DivScalar(*tcp, tcpNorm);

        float ncpNorm = f3Length(f4GetXYZ(*ncp));
        *ncp = f4DivScalar(*ncp, ncpNorm);

        float fcpNorm = f3Length(f4GetXYZ(*fcp));
        *fcp = f4DivScalar(*fcp, fcpNorm);
    }
#else
    f4x4ExtractFrustumClipPlanes(vp->mMatrices[MONO_CAMERA_VIEW_INDEX], rcp, lcp, tcp, bcp, fcp, ncp, normalizePlanes);
#endif // QUEST_VR
}
