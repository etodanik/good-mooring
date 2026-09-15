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

/********************************************************************************************************
 *
 * The Forge - ANIMATION UNIT TEST
 *
 * The purpose of this demo is to show how animations work using the
 * animation middleware
 *
 *********************************************************************************************************/

// Interfaces
#include "../../../../Common_3/Application/Interfaces/IApp.h"
#include "../../../../Common_3/Application/Interfaces/ICamera.h"
#include "../../../../Common_3/Application/Interfaces/IFont.h"
#include "../../../../Common_3/OS/Interfaces/IInput.h"
#include "../../../../Common_3/Application/Interfaces/IProfiler.h"
#include "../../../../Common_3/Application/Interfaces/IScreenshot.h"
#include "../../../../Common_3/Application/Interfaces/IUI.h"
#include "../../../../Common_3/Game/Interfaces/IScripting.h"
#include "../../../../Common_3/Utilities/Interfaces/IFileSystem.h"
#include "../../../../Common_3/Utilities/Interfaces/ILog.h"
#include "../../../../Common_3/Utilities/Interfaces/ITime.h"

// Rendering
#include "../../../../Common_3/Graphics/Interfaces/IGraphics.h"
#include "../../../../Common_3/Resources/ResourceLoader/Interfaces/IResourceLoader.h"

#include "../../../../Common_3/Utilities/RingBuffer.h"

// Middleware packages
#include "../../../../Common_3/Resources/AnimationSystem/Animation/AnimatedObject.h"
#include "../../../../Common_3/Resources/AnimationSystem/Animation/Animation.h"
#include "../../../../Common_3/Resources/AnimationSystem/Animation/Clip.h"
#include "../../../../Common_3/Resources/AnimationSystem/Animation/ClipController.h"
#include "../../../../Common_3/Resources/AnimationSystem/Animation/Rig.h"
#include "../../../../Common_3/Resources/AnimationSystem/Animation/SkeletonBatcher.h"

// Math
#include "../../../../Common_3/Utilities/Interfaces/IMath.h"
#include "../../../../Common_3/Utilities/Threading/ThreadSystem.h"

// Memory
#include "../../../../Common_3/Utilities/Interfaces/IMemory.h"

// fsl
#include "../../../../Common_3/Graphics/FSL/defaults.h"
#include "./Shaders/FSL/Global.srt.h"
#include "../../../../Common_3/Resources/AnimationSystem/Animation/Shaders/FSL/Animation.srt.h"

//--------------------------------------------------------------------------------------------
// RENDERING PIPELINE DATA
//--------------------------------------------------------------------------------------------
#define MAX_INSTANCES 804 // For allocating space in uniform block. Must match with shader.

// #NOTE: Two sets of resources (one in flight and one being used on CPU)
const uint32_t gDataBufferCount = 2;

ProfileToken gGpuProfileToken;

TFRenderer* pRenderer = NULL;

TFQueue*   pGraphicsQueue = NULL;
GpuCmdRing gGraphicsCmdRing = {};

TFSwapChain*    pSwapChain = NULL;
TFRenderTarget* pDepthBuffer = NULL;
TFSemaphore*    pImageAcquiredSemaphore[gDataBufferCount] = { NULL };

TFShader*   pCubeShader = NULL;
TFBuffer*   pJointVertexBuffer = NULL;
TFBuffer*   pBoneVertexBuffer = NULL;
TFBuffer*   pCuboidVertexBuffer = NULL;
TFBuffer*   pCubesVertexBuffer = NULL;
TFPipeline* pCubePipeline = NULL;
int         gNumberOfJointPoints;
int         gNumberOfBonePoints;
int         gNumberOfCuboidPoints;

// Baked Physics
TFPipeline* pOzzLogoSkeletonPipeline = NULL;
int         gNumberOfCubes;

TFShader*        pPlaneDrawShader = NULL;
TFBuffer*        pPlaneVertexBuffer = NULL;
TFPipeline*      pPlaneDrawPipeline = NULL;
TFDescriptorSet* pDescriptorSet = NULL;
TFDescriptorSet* pTargetDescriptorSet = NULL;

struct UniformBlockPlane
{
    TFCameraMatrix mProjectView;
    mat4           mToWorldMat;
};
UniformBlockPlane gUniformDataPlane;

TFBuffer* pPlaneUniformBuffer[gDataBufferCount] = { NULL };

struct UniformBlock
{
    TFCameraMatrix mProjectView;
    mat4           mViewMatrix;
    vec4           mColor[MAX_INSTANCES];
    vec4           mLightPosition;
    vec4           mLightColor;
    vec4           mJointColor;
    uint4          mSkeletonInfo;
    mat4           mToWorldMat[MAX_INSTANCES];
} gUniformDataCuboid;

TFBuffer*            pCuboidUniformBuffer[gDataBufferCount] = { NULL };
TFBuffer*            pTargetUniformBuffer[gDataBufferCount] = { NULL };
UniformSkeletonBlock gUniformDataTarget;

//--------------------------------------------------------------------------------------------
// Extended GPU Config settings
//--------------------------------------------------------------------------------------------
#define FOREACH_SETTING(X)    X(MaxRigs, 4096)

#define GENERATE_ENUM(x, y)   x,
#define GENERATE_STRING(x, y) #x,
#define GENERATE_STRUCT(x, y) uint32_t m##x = y;

typedef enum ESettings
{
    FOREACH_SETTING(GENERATE_ENUM) Count
} ESettings;

const char* gSettingNames[] = { FOREACH_SETTING(GENERATE_STRING) };

// Useful for using names directly instead of subscripting an array
struct ConfigSettings
{
    FOREACH_SETTING(GENERATE_STRUCT)
} gGpuSettings;

//--------------------------------------------------------------------------------------------
// CAMERA CONTROLLER & SYSTEMS (File/Log)
//--------------------------------------------------------------------------------------------
TFICamera* pCamera = NULL;

TFFontDrawDesc gFrameTimeDraw;
TFFont*        gFont;

const char* gTestScripts[] = { "Test0.lua" };
uint32_t    gScriptIndexes[] = { 0 };
uint32_t    gCurrentScriptIndex = 0;

void RunScript(void* pUserData)
{
    UNREF_PARAM(pUserData);
    TFLuaScriptDesc runDesc = {};
    runDesc.pScriptFileName = gTestScripts[gCurrentScriptIndex];
    luaQueueScriptToRun(&runDesc);
}

//--------------------------------------------------------------------------------------------
// ANIMATION DATA
//--------------------------------------------------------------------------------------------
#define ANIMATIONCOUNT       4

#define MAX_ANIMATED_OBJECTS 4096

unsigned int gNumRigs = 1; // Determines the number of rigs to update and draw

TFUIWindowDesc gStandaloneAnimationsDesc;
// Specific UIComponents for each animation example
TFUIWindowDesc gAnimationControlsDesc[ANIMATIONCOUNT];

// AnimatedObjects
AnimatedObject gStickFigureAnimObject[MAX_ANIMATED_OBJECTS];
AnimatedObject gOzzLogoAnimObject;

// Animations
Animation gAnimations[ANIMATIONCOUNT][MAX_ANIMATED_OBJECTS];
Animation gShatterAnimation;

// ClipControllers
ClipController gStandClipController[MAX_ANIMATED_OBJECTS];
ClipController gWalkClipController[MAX_ANIMATED_OBJECTS];
ClipController gJogClipController[MAX_ANIMATED_OBJECTS];
ClipController gRunClipController[MAX_ANIMATED_OBJECTS];
ClipController gNeckCrackClipController[MAX_ANIMATED_OBJECTS];
ClipController gShatterClipContoller;

// Clips
Clip gStandClip;
Clip gWalkClip;
Clip gJogClip;
Clip gRunClip;
Clip gNeckCrackClip;
Clip gShatterClip;

// ClipMasks
ClipMask gStandClipMask;
ClipMask gWalkClipMask;
ClipMask gNeckCrackClipMask;

// Rigs
Rig gStickFigureRig;
Rig gOzzLogoRig;

// SkeletonBatcher
SkeletonBatcher gSkeletonBatcher;
SkeletonBatcher gOzzLogoSkeletonBatcher;

// Parameters for aim IK
AimIKDesc      gAimIKDesc;
Point3         gAimTarget;
TwoBonesIKDesc gTwoBonesIKDesc;
int            gJointChain[4];
const Vector3  gJointUpVectors[4] = { Vector3::xAxis(), Vector3::xAxis(), Vector3::xAxis(), Vector3::xAxis() };

// Filenames
const char* gStickFigureName = "stickFigure/skeleton.ozz";
const char* gStandClipName = "stickFigure/animations/stand.ozz";
const char* gWalkClipName = "stickFigure/animations/walk.ozz";
const char* gJogClipName = "stickFigure/animations/jog.ozz";
const char* gRunClipName = "stickFigure/animations/run.ozz";
const char* gNeckCrackClipName = "stickFigure/animations/neckCrack.ozz";
const char* gOzzLogoName = "ozzLogo/skeleton.ozz";
const char* gShatterClipName = "ozzLogo/animations/shatter.ozz";
int         gCameraIndex;

float* pBonePoints = 0;
float* pJointPoints = 0;
float* pCuboidPoints = 0;
float* pCubesPoints;

const float gBoneWidthRatio = 0.2f;                // Determines how far along the bone to put the max width [0,1]
const float gJointRadius = gBoneWidthRatio * 0.5f; // Set to replicate Ozz skeleton

// Timer to get animation system update time
static TFHiresTimer gAnimationUpdateTimer;
char                gAnimationUpdateText[64] = { 0 };

// Attached Cuboid Object
mat4       gCuboidTransformMat = mat4::identity(); // Will get updated as the animated object updates
const mat4 gCuboidScaleMat = mat4::scale(vec3(0.05f, 0.05f, 0.4f));
const vec4 gCuboidColor = vec4(1.f, 0.f, 0.f, 1.f);

//--------------------------------------------------------------------------------------------
// MULTI THREADING DATA
//--------------------------------------------------------------------------------------------

// Toggle for enabling/disabling threading through UI
bool gEnableThreading = true;
bool gAutomateThreading = true;

// Number of rigs per task that will be adjusted by the UI
unsigned int gGrainSize = 1;

struct ThreadData
{
    AnimatedObject* mAnimatedObject;
    float           mDeltaTime;
    unsigned int    mNumberSystems;
};
static ThreadData gThreadData[MAX_ANIMATED_OBJECTS];

struct ThreadSkeletonData
{
    unsigned int mFrameNumber;
    unsigned int mNumberRigs;
    uint32_t     mOffset;
};
static ThreadSkeletonData gThreadSkeletonData[MAX_ANIMATED_OBJECTS];

static ThreadSystem gThreadSystem = NULL;

//--------------------------------------------------------------------------------------------
// UI DATA
//--------------------------------------------------------------------------------------------

// Used for JointAttachment example
const unsigned int kLeftHandMiddleJointIndex = 18; // Index of the left hand's middle joint in this specific skeleton

// Used for PartialBlending example
const float        kDefaultUpperBodyWeight = 1.0f;   // Sets mStandJointsWeight and mWalkJointsWeight to their default values
const float        kDefaultStandJointsWeight = 1.0f; // Stand clip will only effect children of UpperBodyJointIndex
const float        kDefaultWalkJointsWeight = 0.0f;  // Walk clip will only effect non-children of UpperBodyJointIndex
const unsigned int kSpineJointIndex = 3;             // Index of the spine joint in this specific skeleton

// Used for AdditiveBlending example
const float kDefaultNeckCrackJointsWeight = 1.0f;

// VR 2D layer transform (positioned at -1 along the Z axis, default rotation, default scale)
TFVR2DLayerDesc gVR2DLayer{ { 0.0f, 0.0f, -1.0f }, { 0.0f, 0.0f, 0.0f, 1.0f }, 1.0f };

struct UIData
{
    struct GeneralSettingsData
    {
        bool mDrawBakedPhysics = true;
        bool mAnimatedCamera = false;
        bool mShowBindPose = false;
        bool mDrawAttachedObject = true;
        bool mDrawPlane = true;
        bool mMultipleRigs = false;

        unsigned int* mNumberOfRigs = &gNumRigs;
    } mGeneralSettings;

    struct BlendingBlendParamsData
    {
        bool  mAutoSetBlendParams = true;
        float mThreshold = 0.1f;
        float mBlendRatio = 0.5f;
        float mWalkClipWeight = 0.2f;
        float mJogClipWeight = 0.2f;
        float mRunClipWeight = 0.2f;
    } mBlendingParams;

    struct PartialBlendingBlendParamsData
    {
        bool  mAutoSetBlendParams = true;
        float mThreshold = 0.1f;
        float mStandClipWeight = 0.5f;
        float mWalkClipWeight = 0.5f;
        float mUpperBodyWeight = kDefaultUpperBodyWeight;
        float mStandJointsWeight = kDefaultStandJointsWeight;
        float mWalkJointsWeight = kDefaultWalkJointsWeight;
    } mPartialBlendingParams;

    struct AdditiveBlendingParamsData
    {
        float mWalkClipWeight = 0.2f;
        float mNeckCrackClipWeight = 0.2f;
        float mThreshold = 0.1f;
    } mAdditiveBlendingParams;

    struct AttachedObjectData
    {
        unsigned int mJointIndex = kLeftHandMiddleJointIndex;
        float3       mOffset = { -0.001f, 0.041f, -0.141f }; // Values that will place it naturally in the hand
    } mAttachedObject;

    struct IKParamsData
    {
        bool  mAim = false;
        bool  mTwoBoneIK = false;
        float mFoot = 0.0f;
    } mIKParams;

    struct ThreadingControlData
    {
        bool*         mEnableThreading = &gEnableThreading;
        bool*         mAutomateThreading = &gAutomateThreading;
        unsigned int* mGrainSize = &gGrainSize;
    } mThreadingControl;

    struct ClipData
    {
        bool  mPlay = true;
        bool  mLoop = true;
        float mAnimationTime = 0.0f;
        float mPlaybackSpeed = 1.0f;
    };

    ClipData mStandClip = {};
    ClipData mWalkClip = {};
    ClipData mJogClip = {};
    ClipData mRunClip = {};
    ClipData mNeckCrackClip = {};
    ClipData mShatterClip = {};

    struct UpperBodyMaskData
    {
        bool         mEnableMask = true;
        float        mNeckCrackJointsWeight = kDefaultNeckCrackJointsWeight;
        unsigned int mUpperBodyJointIndex = kSpineJointIndex;
    } mUpperBodyMask;

    unsigned int mUpperBodyJointIndex = kSpineJointIndex;
} gUIData = {};

enum AnimationIndices
{
    ANIMATION_INDEX_PLAYBACK,
    ANIMATION_INDEX_BLEND,
    ANIMATION_INDEX_PARTIALBLEND,
    ANIMATION_INDEX_ADDITIVEBLEND
};

const char* gAnimationNames[] = { "PlayBack", "Blending", "PartialBlending", "AdditiveBlending" };

uint32_t gCurrentAnimationIndex = ANIMATION_INDEX_PLAYBACK;

// Hard set the controller's time ratio via callback when it is set in the UI
void ShatterClipTimeChangeCallback(void* pUserData)
{
    UNREF_PARAM(pUserData);
    if (gUIData.mGeneralSettings.mDrawBakedPhysics)
    {
        gShatterClipContoller.SetTimeRatioHard(0.0f);
        gShatterClipContoller.mPlay = true;
    }
    else
    {
        gShatterClipContoller.mPlay = false;
        gUIData.mGeneralSettings.mAnimatedCamera = false;
    }
}

void AnimatedCameraChangeCallback(void* pUserData)
{
    UNREF_PARAM(pUserData);
    if (!gUIData.mGeneralSettings.mDrawBakedPhysics)
    {
        gUIData.mGeneralSettings.mAnimatedCamera = false;
    }
}

// StandClip Callbacks
void StandClipPlayCallback(void* pUserData)
{
    UNREF_PARAM(pUserData);
    if ((gCurrentAnimationIndex != ANIMATION_INDEX_BLEND && gCurrentAnimationIndex != ANIMATION_INDEX_PARTIALBLEND) ||
        (!gUIData.mBlendingParams.mAutoSetBlendParams && gCurrentAnimationIndex == ANIMATION_INDEX_BLEND) ||
        (!gUIData.mPartialBlendingParams.mAutoSetBlendParams && gCurrentAnimationIndex == ANIMATION_INDEX_PARTIALBLEND))
    {
        for (size_t i = 0; i < gNumRigs; i++)
        {
            gStandClipController[i].mPlay = gUIData.mStandClip.mPlay;
        }
    }
    else
    {
        gUIData.mStandClip.mPlay = true;
    }
}
void StandClipLoopCallback(void* pUserData)
{
    UNREF_PARAM(pUserData);
    if ((gCurrentAnimationIndex != ANIMATION_INDEX_BLEND && gCurrentAnimationIndex != ANIMATION_INDEX_PARTIALBLEND) ||
        (!gUIData.mBlendingParams.mAutoSetBlendParams && gCurrentAnimationIndex == ANIMATION_INDEX_BLEND) ||
        (!gUIData.mPartialBlendingParams.mAutoSetBlendParams && gCurrentAnimationIndex == ANIMATION_INDEX_PARTIALBLEND))
    {
        for (size_t i = 0; i < gNumRigs; i++)
        {
            gStandClipController[i].mLoop = gUIData.mStandClip.mLoop;
        }
    }
    else
    {
        gUIData.mStandClip.mLoop = true;
    }
}
void StandClipTimeChangeCallback(void* pUserData)
{
    UNREF_PARAM(pUserData);
    if ((gCurrentAnimationIndex != ANIMATION_INDEX_BLEND && gCurrentAnimationIndex != ANIMATION_INDEX_PARTIALBLEND) ||
        (!gUIData.mBlendingParams.mAutoSetBlendParams && gCurrentAnimationIndex == ANIMATION_INDEX_BLEND) ||
        (!gUIData.mPartialBlendingParams.mAutoSetBlendParams && gCurrentAnimationIndex == ANIMATION_INDEX_PARTIALBLEND))
    {
        gUIData.mStandClip.mPlay = false;
        for (size_t i = 0; i < gNumRigs; i++)
        {
            gStandClipController[i].SetTimeRatioHard(gUIData.mStandClip.mAnimationTime);
        }
    }
}
void StandClipPlaybackSpeedChangeCallback(void* pUserData)
{
    UNREF_PARAM(pUserData);
    if ((gCurrentAnimationIndex != ANIMATION_INDEX_BLEND && gCurrentAnimationIndex != ANIMATION_INDEX_PARTIALBLEND) ||
        (!gUIData.mBlendingParams.mAutoSetBlendParams && gCurrentAnimationIndex == ANIMATION_INDEX_BLEND) ||
        (!gUIData.mPartialBlendingParams.mAutoSetBlendParams && gCurrentAnimationIndex == ANIMATION_INDEX_PARTIALBLEND))
    {
        for (size_t i = 0; i < gNumRigs; i++)
        {
            gStandClipController[i].mPlaybackSpeed = gUIData.mStandClip.mPlaybackSpeed;
        }
    }
    else
    {
        gUIData.mStandClip.mPlaybackSpeed = gStandClipController[0].mPlaybackSpeed;
    }
}
void SetStandClipJointsWeightWithUIValues(void* pUserData)
{
    UNREF_PARAM(pUserData);
    gStandClipMask.DisableAllJoints();
    gStandClipMask.SetAllChildrenOf(gUIData.mUpperBodyJointIndex, gUIData.mPartialBlendingParams.mStandJointsWeight);
}
void StandClipJointsWeightCallback(void* pUserData)
{
    if ((gCurrentAnimationIndex != ANIMATION_INDEX_BLEND && gCurrentAnimationIndex != ANIMATION_INDEX_PARTIALBLEND) ||
        (!gUIData.mBlendingParams.mAutoSetBlendParams && gCurrentAnimationIndex == ANIMATION_INDEX_BLEND) ||
        (!gUIData.mPartialBlendingParams.mAutoSetBlendParams && gCurrentAnimationIndex == ANIMATION_INDEX_PARTIALBLEND))
    {
        SetStandClipJointsWeightWithUIValues(pUserData);
    }
    else
    {
        gUIData.mPartialBlendingParams.mStandJointsWeight = gUIData.mPartialBlendingParams.mUpperBodyWeight;
    }
}
void StandClipWeightCallback(void* pUserData)
{
    UNREF_PARAM(pUserData);
    if ((!gUIData.mBlendingParams.mAutoSetBlendParams && gCurrentAnimationIndex == ANIMATION_INDEX_BLEND) ||
        (!gUIData.mPartialBlendingParams.mAutoSetBlendParams && gCurrentAnimationIndex == ANIMATION_INDEX_PARTIALBLEND))
    {
        if (gCurrentAnimationIndex == ANIMATION_INDEX_PARTIALBLEND)
        {
            for (size_t i = 0; i < gNumRigs; i++)
            {
                gStandClipController[i].mWeight = gUIData.mPartialBlendingParams.mStandClipWeight;
            }
        }
    }
    else
    {
        gUIData.mPartialBlendingParams.mStandClipWeight = gStandClipController[0].mWeight;
    }
}

// WalkClip Callbacks
void WalkClipPlayCallback(void* pUserData)
{
    UNREF_PARAM(pUserData);
    if ((gCurrentAnimationIndex != ANIMATION_INDEX_BLEND && gCurrentAnimationIndex != ANIMATION_INDEX_PARTIALBLEND) ||
        (!gUIData.mBlendingParams.mAutoSetBlendParams && gCurrentAnimationIndex == ANIMATION_INDEX_BLEND) ||
        (!gUIData.mPartialBlendingParams.mAutoSetBlendParams && gCurrentAnimationIndex == ANIMATION_INDEX_PARTIALBLEND))
    {
        for (size_t i = 0; i < gNumRigs; i++)
        {
            gWalkClipController[i].mPlay = gUIData.mWalkClip.mPlay;
        }
    }
    else
    {
        gUIData.mWalkClip.mPlay = true;
    }
}
void WalkClipLoopCallback(void* pUserData)
{
    UNREF_PARAM(pUserData);
    if ((gCurrentAnimationIndex != ANIMATION_INDEX_BLEND && gCurrentAnimationIndex != ANIMATION_INDEX_PARTIALBLEND) ||
        (!gUIData.mBlendingParams.mAutoSetBlendParams && gCurrentAnimationIndex == ANIMATION_INDEX_BLEND) ||
        (!gUIData.mPartialBlendingParams.mAutoSetBlendParams && gCurrentAnimationIndex == ANIMATION_INDEX_PARTIALBLEND))
    {
        for (size_t i = 0; i < gNumRigs; i++)
        {
            gWalkClipController[i].mLoop = gUIData.mWalkClip.mLoop;
        }
    }
    else
    {
        gUIData.mWalkClip.mLoop = true;
    }
}
void WalkClipTimeChangeCallback(void* pUserData)
{
    UNREF_PARAM(pUserData);
    if ((gCurrentAnimationIndex != ANIMATION_INDEX_BLEND && gCurrentAnimationIndex != ANIMATION_INDEX_PARTIALBLEND) ||
        (!gUIData.mBlendingParams.mAutoSetBlendParams && gCurrentAnimationIndex == ANIMATION_INDEX_BLEND) ||
        (!gUIData.mPartialBlendingParams.mAutoSetBlendParams && gCurrentAnimationIndex == ANIMATION_INDEX_PARTIALBLEND))
    {
        gUIData.mWalkClip.mPlay = false;
        for (size_t i = 0; i < gNumRigs; i++)
        {
            gWalkClipController[i].SetTimeRatioHard(gUIData.mWalkClip.mAnimationTime);
        }
    }
}
void WalkClipPlaybackSpeedChangeCallback(void* pUserData)
{
    UNREF_PARAM(pUserData);
    if ((gCurrentAnimationIndex != ANIMATION_INDEX_BLEND && gCurrentAnimationIndex != ANIMATION_INDEX_PARTIALBLEND) ||
        (!gUIData.mBlendingParams.mAutoSetBlendParams && gCurrentAnimationIndex == ANIMATION_INDEX_BLEND) ||
        (!gUIData.mPartialBlendingParams.mAutoSetBlendParams && gCurrentAnimationIndex == ANIMATION_INDEX_PARTIALBLEND))
    {
        for (size_t i = 0; i < gNumRigs; i++)
        {
            gWalkClipController[i].mPlaybackSpeed = gUIData.mWalkClip.mPlaybackSpeed;
        }
    }
    else
    {
        gUIData.mWalkClip.mPlaybackSpeed = gWalkClipController[0].mPlaybackSpeed;
    }
}
void SetWalkClipJointsWeightWithUIValues()
{
    gWalkClipMask.EnableAllJoints();
    gWalkClipMask.SetAllChildrenOf(gUIData.mUpperBodyJointIndex, gUIData.mPartialBlendingParams.mWalkJointsWeight);
}
void WalkClipJointsWeightCallback(void* pUserData)
{
    UNREF_PARAM(pUserData);
    if ((gCurrentAnimationIndex != ANIMATION_INDEX_BLEND && gCurrentAnimationIndex != ANIMATION_INDEX_PARTIALBLEND) ||
        (!gUIData.mBlendingParams.mAutoSetBlendParams && gCurrentAnimationIndex == ANIMATION_INDEX_BLEND) ||
        (!gUIData.mPartialBlendingParams.mAutoSetBlendParams && gCurrentAnimationIndex == ANIMATION_INDEX_PARTIALBLEND))
    {
        SetWalkClipJointsWeightWithUIValues();
    }
    else
    {
        gUIData.mPartialBlendingParams.mWalkJointsWeight = 1.0f - gUIData.mPartialBlendingParams.mUpperBodyWeight;
    }
}

void WalkClipWeightCallback(void* pUserData)
{
    UNREF_PARAM(pUserData);
    if ((!gUIData.mBlendingParams.mAutoSetBlendParams && gCurrentAnimationIndex == ANIMATION_INDEX_BLEND) ||
        (!gUIData.mPartialBlendingParams.mAutoSetBlendParams && gCurrentAnimationIndex == ANIMATION_INDEX_PARTIALBLEND) ||
        gCurrentAnimationIndex == ANIMATION_INDEX_ADDITIVEBLEND)
    {
        if (gCurrentAnimationIndex == ANIMATION_INDEX_BLEND)
        {
            for (size_t i = 0; i < gNumRigs; i++)
            {
                gWalkClipController[i].mWeight = gUIData.mBlendingParams.mWalkClipWeight;
            }
        }

        if (gCurrentAnimationIndex == ANIMATION_INDEX_PARTIALBLEND)
        {
            for (size_t i = 0; i < gNumRigs; i++)
            {
                gWalkClipController[i].mWeight = gUIData.mPartialBlendingParams.mWalkClipWeight;
            }
        }

        if (gCurrentAnimationIndex == ANIMATION_INDEX_ADDITIVEBLEND)
        {
            for (size_t i = 0; i < gNumRigs; i++)
            {
                gWalkClipController[i].mWeight = gUIData.mAdditiveBlendingParams.mWalkClipWeight;
            }
        }
    }
    else
    {
        if (gCurrentAnimationIndex == ANIMATION_INDEX_BLEND)
        {
            gUIData.mBlendingParams.mWalkClipWeight = gWalkClipController[0].mWeight;
        }
        if (gCurrentAnimationIndex == ANIMATION_INDEX_PARTIALBLEND)
        {
            gUIData.mPartialBlendingParams.mWalkClipWeight = gWalkClipController[0].mWeight;
        }
    }
}

// JogClip Callbacks
void JogClipPlayCallback(void* pUserData)
{
    UNREF_PARAM(pUserData);
    if ((gCurrentAnimationIndex != ANIMATION_INDEX_BLEND && gCurrentAnimationIndex != ANIMATION_INDEX_PARTIALBLEND) ||
        (!gUIData.mBlendingParams.mAutoSetBlendParams && gCurrentAnimationIndex == ANIMATION_INDEX_BLEND) ||
        (!gUIData.mPartialBlendingParams.mAutoSetBlendParams && gCurrentAnimationIndex == ANIMATION_INDEX_PARTIALBLEND))
    {
        for (size_t i = 0; i < gNumRigs; i++)
        {
            gJogClipController[i].mPlay = gUIData.mJogClip.mPlay;
        }
    }
    else
    {
        gUIData.mJogClip.mPlay = true;
    }
}
void JogClipLoopCallback(void* pUserData)
{
    UNREF_PARAM(pUserData);
    if ((gCurrentAnimationIndex != ANIMATION_INDEX_BLEND && gCurrentAnimationIndex != ANIMATION_INDEX_PARTIALBLEND) ||
        (!gUIData.mBlendingParams.mAutoSetBlendParams && gCurrentAnimationIndex == ANIMATION_INDEX_BLEND) ||
        (!gUIData.mPartialBlendingParams.mAutoSetBlendParams && gCurrentAnimationIndex == ANIMATION_INDEX_PARTIALBLEND))
    {
        for (size_t i = 0; i < gNumRigs; i++)
        {
            gJogClipController[i].mLoop = gUIData.mJogClip.mLoop;
        }
    }
    else
    {
        gUIData.mJogClip.mLoop = true;
    }
}
void JogClipTimeChangeCallback(void* pUserData)
{
    UNREF_PARAM(pUserData);
    if ((gCurrentAnimationIndex != ANIMATION_INDEX_BLEND && gCurrentAnimationIndex != ANIMATION_INDEX_PARTIALBLEND) ||
        (!gUIData.mBlendingParams.mAutoSetBlendParams && gCurrentAnimationIndex == ANIMATION_INDEX_BLEND) ||
        (!gUIData.mPartialBlendingParams.mAutoSetBlendParams && gCurrentAnimationIndex == ANIMATION_INDEX_PARTIALBLEND))
    {
        gUIData.mJogClip.mPlay = false;
        for (size_t i = 0; i < gNumRigs; i++)
        {
            gJogClipController[i].SetTimeRatioHard(gUIData.mJogClip.mAnimationTime);
        }
    }
}
void JogClipPlaybackSpeedChangeCallback(void* pUserData)
{
    UNREF_PARAM(pUserData);
    if ((gCurrentAnimationIndex != ANIMATION_INDEX_BLEND && gCurrentAnimationIndex != ANIMATION_INDEX_PARTIALBLEND) ||
        (!gUIData.mBlendingParams.mAutoSetBlendParams && gCurrentAnimationIndex == ANIMATION_INDEX_BLEND) ||
        (!gUIData.mPartialBlendingParams.mAutoSetBlendParams && gCurrentAnimationIndex == ANIMATION_INDEX_PARTIALBLEND))
    {
        for (size_t i = 0; i < gNumRigs; i++)
        {
            gJogClipController[i].mPlaybackSpeed = gUIData.mJogClip.mPlaybackSpeed;
        }
    }
    else
    {
        gUIData.mJogClip.mPlaybackSpeed = gJogClipController[0].mPlaybackSpeed;
    }
}
void JogClipJointsWeightCallback(void* pUserData)
{
    UNREF_PARAM(pUserData);
    if ((!gUIData.mBlendingParams.mAutoSetBlendParams && gCurrentAnimationIndex == ANIMATION_INDEX_BLEND) ||
        (!gUIData.mPartialBlendingParams.mAutoSetBlendParams && gCurrentAnimationIndex == ANIMATION_INDEX_PARTIALBLEND))
    {
        SetWalkClipJointsWeightWithUIValues();
    }
    else
    {
        gUIData.mPartialBlendingParams.mWalkJointsWeight = 1.0f - gUIData.mPartialBlendingParams.mUpperBodyWeight;
    }
}
void JogClipWeightCallback(void* pUserData)
{
    UNREF_PARAM(pUserData);
    if ((!gUIData.mBlendingParams.mAutoSetBlendParams && gCurrentAnimationIndex == ANIMATION_INDEX_BLEND) ||
        (!gUIData.mPartialBlendingParams.mAutoSetBlendParams && gCurrentAnimationIndex == ANIMATION_INDEX_PARTIALBLEND))
    {
        if (gCurrentAnimationIndex == ANIMATION_INDEX_BLEND)
        {
            for (size_t i = 0; i < gNumRigs; i++)
            {
                gJogClipController[i].mWeight = gUIData.mBlendingParams.mJogClipWeight;
            }
        }
    }
    else
    {
        if (gCurrentAnimationIndex == ANIMATION_INDEX_BLEND)
        {
            gUIData.mBlendingParams.mJogClipWeight = gJogClipController[0].mWeight;
        }
    }
}

// RunClip Callbacks
void RunClipPlayCallback(void* pUserData)
{
    UNREF_PARAM(pUserData);
    if ((gCurrentAnimationIndex != ANIMATION_INDEX_BLEND && gCurrentAnimationIndex != ANIMATION_INDEX_PARTIALBLEND) ||
        (!gUIData.mBlendingParams.mAutoSetBlendParams && gCurrentAnimationIndex == ANIMATION_INDEX_BLEND) ||
        (!gUIData.mPartialBlendingParams.mAutoSetBlendParams && gCurrentAnimationIndex == ANIMATION_INDEX_PARTIALBLEND))
    {
        for (size_t i = 0; i < gNumRigs; i++)
        {
            gRunClipController[i].mPlay = gUIData.mRunClip.mPlay;
        }
    }
    else
    {
        gUIData.mRunClip.mPlay = true;
    }
}
void RunClipLoopCallback(void* pUserData)
{
    UNREF_PARAM(pUserData);
    if ((gCurrentAnimationIndex != ANIMATION_INDEX_BLEND && gCurrentAnimationIndex != ANIMATION_INDEX_PARTIALBLEND) ||
        (!gUIData.mBlendingParams.mAutoSetBlendParams && gCurrentAnimationIndex == ANIMATION_INDEX_BLEND) ||
        (!gUIData.mPartialBlendingParams.mAutoSetBlendParams && gCurrentAnimationIndex == ANIMATION_INDEX_PARTIALBLEND))
    {
        for (size_t i = 0; i < gNumRigs; i++)
        {
            gRunClipController[i].mLoop = gUIData.mRunClip.mLoop;
        }
    }
    else
    {
        gUIData.mRunClip.mLoop = true;
    }
}
void RunClipTimeChangeCallback(void* pUserData)
{
    UNREF_PARAM(pUserData);
    if ((gCurrentAnimationIndex != ANIMATION_INDEX_BLEND && gCurrentAnimationIndex != ANIMATION_INDEX_PARTIALBLEND) ||
        (!gUIData.mBlendingParams.mAutoSetBlendParams && gCurrentAnimationIndex == ANIMATION_INDEX_BLEND) ||
        (!gUIData.mPartialBlendingParams.mAutoSetBlendParams && gCurrentAnimationIndex == ANIMATION_INDEX_PARTIALBLEND))
    {
        gUIData.mRunClip.mPlay = false;
        for (size_t i = 0; i < gNumRigs; i++)
        {
            gRunClipController[i].SetTimeRatioHard(gUIData.mRunClip.mAnimationTime);
        }
    }
}
void RunClipPlaybackSpeedChangeCallback(void* pUserData)
{
    UNREF_PARAM(pUserData);
    if ((gCurrentAnimationIndex != ANIMATION_INDEX_BLEND && gCurrentAnimationIndex != ANIMATION_INDEX_PARTIALBLEND) ||
        (!gUIData.mBlendingParams.mAutoSetBlendParams && gCurrentAnimationIndex == ANIMATION_INDEX_BLEND) ||
        (!gUIData.mPartialBlendingParams.mAutoSetBlendParams && gCurrentAnimationIndex == ANIMATION_INDEX_PARTIALBLEND))
    {
        for (size_t i = 0; i < gNumRigs; i++)
        {
            gRunClipController[i].mPlaybackSpeed = gUIData.mRunClip.mPlaybackSpeed;
        }
    }
    else
    {
        gUIData.mRunClip.mPlaybackSpeed = gRunClipController[0].mPlaybackSpeed;
    }
}
void RunClipWeightCallback(void* pUserData)
{
    UNREF_PARAM(pUserData);
    if ((!gUIData.mBlendingParams.mAutoSetBlendParams && gCurrentAnimationIndex == ANIMATION_INDEX_BLEND) ||
        (!gUIData.mPartialBlendingParams.mAutoSetBlendParams && gCurrentAnimationIndex == ANIMATION_INDEX_PARTIALBLEND))
    {
        if (gCurrentAnimationIndex == ANIMATION_INDEX_BLEND)
        {
            for (size_t i = 0; i < gNumRigs; i++)
            {
                gRunClipController[i].mWeight = gUIData.mBlendingParams.mRunClipWeight;
            }
        }
    }
    else
    {
        if (gCurrentAnimationIndex == ANIMATION_INDEX_BLEND)
        {
            gUIData.mBlendingParams.mRunClipWeight = gRunClipController[0].mWeight;
        }
    }
}

// NeckClip Callbacks
void NeckClipPlayCallback(void* pUserData)
{
    UNREF_PARAM(pUserData);
    if ((gCurrentAnimationIndex != ANIMATION_INDEX_BLEND && gCurrentAnimationIndex != ANIMATION_INDEX_PARTIALBLEND) ||
        (!gUIData.mBlendingParams.mAutoSetBlendParams && gCurrentAnimationIndex == ANIMATION_INDEX_BLEND) ||
        (!gUIData.mPartialBlendingParams.mAutoSetBlendParams && gCurrentAnimationIndex == ANIMATION_INDEX_PARTIALBLEND))
    {
        for (size_t i = 0; i < gNumRigs; i++)
        {
            gNeckCrackClipController[i].mPlay = gUIData.mNeckCrackClip.mPlay;
        }
    }
    else
    {
        gUIData.mNeckCrackClip.mPlay = true;
    }
}
void NeckClipLoopCallback(void* pUserData)
{
    UNREF_PARAM(pUserData);
    if ((gCurrentAnimationIndex != ANIMATION_INDEX_BLEND && gCurrentAnimationIndex != ANIMATION_INDEX_PARTIALBLEND) ||
        (!gUIData.mBlendingParams.mAutoSetBlendParams && gCurrentAnimationIndex == ANIMATION_INDEX_BLEND) ||
        (!gUIData.mPartialBlendingParams.mAutoSetBlendParams && gCurrentAnimationIndex == ANIMATION_INDEX_PARTIALBLEND))
    {
        for (size_t i = 0; i < gNumRigs; i++)
        {
            gNeckCrackClipController[i].mLoop = gUIData.mNeckCrackClip.mLoop;
        }
    }
    else
    {
        gUIData.mNeckCrackClip.mLoop = true;
    }
}
void NeckCrackClipTimeChangeCallback(void* pUserData)
{
    UNREF_PARAM(pUserData);
    for (size_t i = 0; i < gNumRigs; i++)
    {
        gNeckCrackClipController[i].SetTimeRatioHard(gUIData.mNeckCrackClip.mAnimationTime);
    }
}
void NeckClipPlaybackSpeedChangeCallback(void* pUserData)
{
    UNREF_PARAM(pUserData);
    if ((gCurrentAnimationIndex != ANIMATION_INDEX_BLEND && gCurrentAnimationIndex != ANIMATION_INDEX_PARTIALBLEND) ||
        (!gUIData.mBlendingParams.mAutoSetBlendParams && gCurrentAnimationIndex == ANIMATION_INDEX_BLEND) ||
        (!gUIData.mPartialBlendingParams.mAutoSetBlendParams && gCurrentAnimationIndex == ANIMATION_INDEX_PARTIALBLEND))
    {
        for (size_t i = 0; i < gNumRigs; i++)
        {
            gNeckCrackClipController[i].mPlaybackSpeed = gUIData.mNeckCrackClip.mPlaybackSpeed;
        }
    }
    else
    {
        gUIData.mNeckCrackClip.mPlaybackSpeed = gNeckCrackClipController[0].mPlaybackSpeed;
    }
}
void SetNeckCrackClipJointsWeightWithUIValues()
{
    gNeckCrackClipMask.DisableAllJoints();
    gNeckCrackClipMask.SetAllChildrenOf(gUIData.mUpperBodyMask.mUpperBodyJointIndex, gUIData.mUpperBodyMask.mNeckCrackJointsWeight);
}
void NeckCrackClipJointsWeightCallback(void* pUserData)
{
    UNREF_PARAM(pUserData);
    if (gUIData.mUpperBodyMask.mEnableMask)
    {
        SetNeckCrackClipJointsWeightWithUIValues();
    }
}

void NeckCrackClipWeightCallback(void* pUserData)
{
    UNREF_PARAM(pUserData);
    for (size_t i = 0; i < gNumRigs; i++)
    {
        gNeckCrackClipController[i].mWeight = gUIData.mAdditiveBlendingParams.mNeckCrackClipWeight;
    }
}

// When the mask is enabled and disabled
void EnableMaskCallback(void* pUserData)
{
    UNREF_PARAM(pUserData);
    if (gUIData.mUpperBodyMask.mEnableMask)
    {
        SetNeckCrackClipJointsWeightWithUIValues();
    }
    else
    {
        gNeckCrackClipMask.EnableAllJoints();
    }
}

// When the upper body weight parameter is changed update the clip mask's joint weights
void UpperBodyWeightCallback(void* pUserData)
{
    if (gUIData.mPartialBlendingParams.mAutoSetBlendParams)
    {
        gUIData.mPartialBlendingParams.mStandJointsWeight = gUIData.mPartialBlendingParams.mUpperBodyWeight;
        gUIData.mPartialBlendingParams.mWalkJointsWeight = 1.0f - gUIData.mPartialBlendingParams.mUpperBodyWeight;

        SetStandClipJointsWeightWithUIValues(pUserData);
        SetWalkClipJointsWeightWithUIValues();
    }
}

// When the upper body root index is changed, update the clip mask's joint weights and update the clip mask's joint weights
void UpperBodyJointIndexCallback(void* pUserData)
{
    if (gUIData.mUpperBodyMask.mEnableMask)
    {
        SetNeckCrackClipJointsWeightWithUIValues();
    }

    SetStandClipJointsWeightWithUIValues(pUserData);
    SetWalkClipJointsWeightWithUIValues();
}

// When mAutoSetBlendParams is turned on we need to reset the clip controllers
void AutoSetBlendParamsCallback(void* pUserData)
{
    if (gUIData.mBlendingParams.mAutoSetBlendParams)
    {
        for (size_t i = 0; i < gNumRigs; i++)
        {
            // Reset the internal values
            gWalkClipController[i].Reset();
            gWalkClipController[i].mLoop = true;
            gJogClipController[i].Reset();
            gJogClipController[i].mLoop = true;
            gRunClipController[i].Reset();
            gRunClipController[i].mLoop = true;

            gAnimations[1][i].mAutoSetBlendParams = true;
        }

        // Reset the UI values
        gUIData.mWalkClip.mPlay = true;
        gUIData.mJogClip.mPlay = true;
        gUIData.mRunClip.mPlay = true;
        gUIData.mWalkClip.mLoop = true;
        gUIData.mJogClip.mLoop = true;
        gUIData.mRunClip.mLoop = true;
    }
    else
    {
        for (size_t i = 0; i < gNumRigs; i++)
        {
            gAnimations[1][i].mAutoSetBlendParams = false;
        }
    }

    if (gUIData.mPartialBlendingParams.mAutoSetBlendParams)
    {
        gUIData.mPartialBlendingParams.mUpperBodyWeight = kDefaultUpperBodyWeight;
        gUIData.mPartialBlendingParams.mStandJointsWeight = kDefaultStandJointsWeight;
        gUIData.mPartialBlendingParams.mWalkJointsWeight = kDefaultWalkJointsWeight;

        SetStandClipJointsWeightWithUIValues(pUserData);
        SetWalkClipJointsWeightWithUIValues();
    }
    else
    {
        for (size_t i = 0; i < gNumRigs; i++)
        {
            gAnimations[2][i].mAutoSetBlendParams = false;
        }
    }
}

void resetAnimations()
{
    for (size_t i = 0; i < gNumRigs; i++)
    {
        // Reset the internal values
        gStandClipController[i].Reset();
        gStandClipController[i].mLoop = true;

        gWalkClipController[i].Reset();
        gWalkClipController[i].mLoop = true;

        gJogClipController[i].Reset();
        gJogClipController[i].mLoop = true;

        gRunClipController[i].Reset();
        gRunClipController[i].mLoop = true;

        gNeckCrackClipController[i].Reset();
        gNeckCrackClipController[i].mLoop = true;
    }

    // Reset the UI values
    gUIData.mStandClip.mPlay = true;
    gUIData.mWalkClip.mPlay = true;
    gUIData.mJogClip.mPlay = true;
    gUIData.mRunClip.mPlay = true;
    gUIData.mNeckCrackClip.mPlay = true;

    gUIData.mStandClip.mLoop = true;
    gUIData.mWalkClip.mLoop = true;
    gUIData.mJogClip.mLoop = true;
    gUIData.mRunClip.mLoop = true;
    gUIData.mNeckCrackClip.mLoop = true;
}

void resetInverseKinematics()
{
    gUIData.mIKParams.mTwoBoneIK = false;
    gUIData.mIKParams.mAim = false;
    gUIData.mIKParams.mFoot = 0.0f;
}

void RunAnimation(void* pUserData)
{
    UNREF_PARAM(pUserData);
    gNumRigs = gNumRigs > gGpuSettings.mMaxRigs ? gGpuSettings.mMaxRigs : gNumRigs;
    // This resets all values to the defaults
    resetAnimations();
    resetInverseKinematics();

    for (size_t i = 0; i < gNumRigs; i++)
    {
        gStickFigureAnimObject[i].mAnimation = &gAnimations[gCurrentAnimationIndex][i];
    }
}

void RandomTimeCallback(void* pUserData)
{
    UNREF_PARAM(pUserData);
    for (size_t i = 0; i < gNumRigs; i++)
    {
        float randomTime = randomFloat(0.0f, 1.0f);
        gAnimations[gCurrentAnimationIndex][i].SetTimeRatio(randomTime);
    }
}

void ThresholdChangeCallback(void* pUserData)
{
    UNREF_PARAM(pUserData);
    if (gCurrentAnimationIndex == 1)
    {
        for (size_t i = 0; i < gNumRigs; i++)
        {
            gAnimations[1][i].mThreshold = gUIData.mBlendingParams.mThreshold;
        }
    }
    else if (gCurrentAnimationIndex == 2)
    {
        for (size_t i = 0; i < gNumRigs; i++)
        {
            gAnimations[2][i].mThreshold = gUIData.mPartialBlendingParams.mThreshold;
        }
    }
    else if (gCurrentAnimationIndex == 3)
    {
        for (size_t i = 0; i < gNumRigs; i++)
        {
            gAnimations[3][i].mThreshold = gUIData.mAdditiveBlendingParams.mThreshold;
        }
    }
}

void BlendRatioChangeCallback(void* pUserData)
{
    UNREF_PARAM(pUserData);
    for (size_t i = 0; i < gNumRigs; i++)
    {
        gAnimations[1][i].mBlendRatio = gUIData.mBlendingParams.mBlendRatio;
    }
}

void setupAnimationSpecificLua()
{
    TFLuaWidgetVariableDesc luaVarDesc = {};
    TFLuaWidgetFunctionDesc luaFuncDesc = {};

    // General Settings
    luaFuncDesc.pLabel = "Randomize Clips Time";
    luaFuncDesc.mType = TF_LUA_WIDGET_FUNCTION_ON_EDITED;
    luaFuncDesc.pFunc = RandomTimeCallback;
    luaFuncDesc.pFuncData = NULL;
    luaRegisterWidgetFunction(&luaFuncDesc);

    luaVarDesc.pLabel = "Number of Rigs";
    luaVarDesc.mWidgetType = TF_WIDGET_TYPE_SLIDER_UINT;
    luaVarDesc.pUint = gUIData.mGeneralSettings.mNumberOfRigs;
    luaRegisterWidgetVariable(&luaVarDesc);
    luaFuncDesc.pLabel = luaVarDesc.pLabel;
    luaFuncDesc.mType = TF_LUA_WIDGET_FUNCTION_ON_EDITED;
    luaFuncDesc.pFunc = RunAnimation;
    luaFuncDesc.pFuncData = NULL;
    luaRegisterWidgetFunction(&luaFuncDesc);

    luaVarDesc.pLabel = "Show Bind Pose";
    luaVarDesc.mWidgetType = TF_WIDGET_TYPE_CHECKBOX;
    luaVarDesc.pBool = &gUIData.mGeneralSettings.mShowBindPose;
    luaRegisterWidgetVariable(&luaVarDesc);

    luaVarDesc.pLabel = "Draw Attached Object";
    luaVarDesc.mWidgetType = TF_WIDGET_TYPE_CHECKBOX;
    luaVarDesc.pBool = &gUIData.mGeneralSettings.mDrawAttachedObject;
    luaRegisterWidgetVariable(&luaVarDesc);

    luaVarDesc.pLabel = "Draw Plane";
    luaVarDesc.mWidgetType = TF_WIDGET_TYPE_CHECKBOX;
    luaVarDesc.pBool = &gUIData.mGeneralSettings.mDrawPlane;
    luaRegisterWidgetVariable(&luaVarDesc);

    luaVarDesc.pLabel = "Draw Baked Physics";
    luaVarDesc.mWidgetType = TF_WIDGET_TYPE_CHECKBOX;
    luaVarDesc.pBool = &gUIData.mGeneralSettings.mDrawBakedPhysics;
    luaRegisterWidgetVariable(&luaVarDesc);
    luaFuncDesc.pLabel = luaVarDesc.pLabel;
    luaFuncDesc.mType = TF_LUA_WIDGET_FUNCTION_ON_EDITED;
    luaFuncDesc.pFunc = ShatterClipTimeChangeCallback;
    luaFuncDesc.pFuncData = NULL;
    luaRegisterWidgetFunction(&luaFuncDesc);

    luaVarDesc.pLabel = "Animate Camera";
    luaVarDesc.mWidgetType = TF_WIDGET_TYPE_CHECKBOX;
    luaVarDesc.pBool = &gUIData.mGeneralSettings.mAnimatedCamera;
    luaRegisterWidgetVariable(&luaVarDesc);
    luaFuncDesc.pLabel = luaVarDesc.pLabel;
    luaFuncDesc.mType = TF_LUA_WIDGET_FUNCTION_ON_EDITED;
    luaFuncDesc.pFunc = AnimatedCameraChangeCallback;
    luaFuncDesc.pFuncData = NULL;
    luaRegisterWidgetFunction(&luaFuncDesc);

    // Threading Control
    luaVarDesc.pLabel = "Enable Threading";
    luaVarDesc.mWidgetType = TF_WIDGET_TYPE_CHECKBOX;
    luaVarDesc.pBool = gUIData.mThreadingControl.mEnableThreading;
    luaRegisterWidgetVariable(&luaVarDesc);

    luaVarDesc.pLabel = "Automate Threading";
    luaVarDesc.mWidgetType = TF_WIDGET_TYPE_CHECKBOX;
    luaVarDesc.pBool = gUIData.mThreadingControl.mAutomateThreading;
    luaRegisterWidgetVariable(&luaVarDesc);

    luaVarDesc.pLabel = "Grain Size";
    luaVarDesc.mWidgetType = TF_WIDGET_TYPE_SLIDER_UINT;
    luaVarDesc.pUint = gUIData.mThreadingControl.mGrainSize;
    luaRegisterWidgetVariable(&luaVarDesc);

    // Inverse Kinematics
    luaVarDesc.pLabel = "Aim IK";
    luaVarDesc.mWidgetType = TF_WIDGET_TYPE_CHECKBOX;
    luaVarDesc.pBool = &gUIData.mIKParams.mAim;
    luaRegisterWidgetVariable(&luaVarDesc);

    luaVarDesc.pLabel = "Two Bone IK";
    luaVarDesc.mWidgetType = TF_WIDGET_TYPE_CHECKBOX;
    luaVarDesc.pBool = &gUIData.mIKParams.mTwoBoneIK;
    luaRegisterWidgetVariable(&luaVarDesc);

    luaVarDesc.pLabel = "Foot Two Bone IK";
    luaVarDesc.mWidgetType = TF_WIDGET_TYPE_SLIDER_FLOAT;
    luaVarDesc.pFloat = &gUIData.mIKParams.mFoot;
    luaRegisterWidgetVariable(&luaVarDesc);

    // Attachment
    luaVarDesc.pLabel = "Joint Index";
    luaVarDesc.mWidgetType = TF_WIDGET_TYPE_SLIDER_UINT;
    luaVarDesc.pUint = &gUIData.mAttachedObject.mJointIndex;
    luaRegisterWidgetVariable(&luaVarDesc);

    luaVarDesc.pLabel = "Offset";
    luaVarDesc.mWidgetType = TF_WIDGET_TYPE_SLIDER_FLOAT3;
    luaVarDesc.pFloat3 = &gUIData.mAttachedObject.mOffset;
    luaRegisterWidgetVariable(&luaVarDesc);

    // STAND CLIP
    {
        // Play
        luaVarDesc.pLabel = "Play Stand";
        luaVarDesc.mWidgetType = TF_WIDGET_TYPE_CHECKBOX;
        luaVarDesc.pBool = &gUIData.mStandClip.mPlay;
        luaRegisterWidgetVariable(&luaVarDesc);
        luaFuncDesc.pLabel = luaVarDesc.pLabel;
        luaFuncDesc.mType = TF_LUA_WIDGET_FUNCTION_ON_EDITED;
        luaFuncDesc.pFunc = StandClipPlayCallback;
        luaRegisterWidgetFunction(&luaFuncDesc);

        // Loop
        luaVarDesc.pLabel = "Loop Stand";
        luaVarDesc.mWidgetType = TF_WIDGET_TYPE_CHECKBOX;
        luaVarDesc.pBool = &gUIData.mStandClip.mLoop;
        luaRegisterWidgetVariable(&luaVarDesc);
        luaFuncDesc.pLabel = luaVarDesc.pLabel;
        luaFuncDesc.mType = TF_LUA_WIDGET_FUNCTION_ON_EDITED;
        luaFuncDesc.pFunc = StandClipLoopCallback;
        luaRegisterWidgetFunction(&luaFuncDesc);

        // Animation Time
        luaVarDesc.pLabel = "Animation Time Stand";
        luaVarDesc.mWidgetType = TF_WIDGET_TYPE_SLIDER_FLOAT;
        luaVarDesc.pFloat = &gUIData.mStandClip.mAnimationTime;
        luaRegisterWidgetVariable(&luaVarDesc);
        luaFuncDesc.pLabel = luaVarDesc.pLabel;
        luaFuncDesc.mType = TF_LUA_WIDGET_FUNCTION_ON_EDITED;
        luaFuncDesc.pFunc = StandClipTimeChangeCallback;
        luaRegisterWidgetFunction(&luaFuncDesc);

        // Playback Speed
        luaVarDesc.pLabel = "Playback Speed Stand";
        luaVarDesc.mWidgetType = TF_WIDGET_TYPE_SLIDER_FLOAT;
        luaVarDesc.pFloat = &gUIData.mStandClip.mPlaybackSpeed;
        luaRegisterWidgetVariable(&luaVarDesc);
        luaFuncDesc.pLabel = luaVarDesc.pLabel;
        luaFuncDesc.mType = TF_LUA_WIDGET_FUNCTION_ON_EDITED;
        luaFuncDesc.pFunc = StandClipPlaybackSpeedChangeCallback;
        luaRegisterWidgetFunction(&luaFuncDesc);
    }

    // WALK CLIP
    {
        // Play
        luaVarDesc.pLabel = "Play Walk";
        luaVarDesc.mWidgetType = TF_WIDGET_TYPE_CHECKBOX;
        luaVarDesc.pBool = &gUIData.mWalkClip.mPlay;
        luaRegisterWidgetVariable(&luaVarDesc);
        luaFuncDesc.pLabel = luaVarDesc.pLabel;
        luaFuncDesc.mType = TF_LUA_WIDGET_FUNCTION_ON_EDITED;
        luaFuncDesc.pFunc = WalkClipPlayCallback;
        luaRegisterWidgetFunction(&luaFuncDesc);

        // Loop
        luaVarDesc.pLabel = "Loop Walk";
        luaVarDesc.mWidgetType = TF_WIDGET_TYPE_CHECKBOX;
        luaVarDesc.pBool = &gUIData.mWalkClip.mLoop;
        luaRegisterWidgetVariable(&luaVarDesc);
        luaFuncDesc.pLabel = luaVarDesc.pLabel;
        luaFuncDesc.mType = TF_LUA_WIDGET_FUNCTION_ON_EDITED;
        luaFuncDesc.pFunc = WalkClipLoopCallback;
        luaRegisterWidgetFunction(&luaFuncDesc);

        // Animation Time
        luaVarDesc.pLabel = "Animation Time Walk";
        luaVarDesc.mWidgetType = TF_WIDGET_TYPE_SLIDER_FLOAT;
        luaVarDesc.pFloat = &gUIData.mWalkClip.mAnimationTime;
        luaRegisterWidgetVariable(&luaVarDesc);
        luaFuncDesc.pLabel = luaVarDesc.pLabel;
        luaFuncDesc.mType = TF_LUA_WIDGET_FUNCTION_ON_EDITED;
        luaFuncDesc.pFunc = WalkClipTimeChangeCallback;
        luaRegisterWidgetFunction(&luaFuncDesc);

        // Playback Speed
        luaVarDesc.pLabel = "Playback Speed Walk";
        luaVarDesc.mWidgetType = TF_WIDGET_TYPE_SLIDER_FLOAT;
        luaVarDesc.pFloat = &gUIData.mWalkClip.mPlaybackSpeed;
        luaRegisterWidgetVariable(&luaVarDesc);
        luaFuncDesc.pLabel = luaVarDesc.pLabel;
        luaFuncDesc.mType = TF_LUA_WIDGET_FUNCTION_ON_EDITED;
        luaFuncDesc.pFunc = WalkClipPlaybackSpeedChangeCallback;
        luaRegisterWidgetFunction(&luaFuncDesc);
    }

    // JOG CLIP
    {
        // Play
        luaVarDesc.pLabel = "Play Jog";
        luaVarDesc.mWidgetType = TF_WIDGET_TYPE_CHECKBOX;
        luaVarDesc.pBool = &gUIData.mJogClip.mPlay;
        luaRegisterWidgetVariable(&luaVarDesc);
        luaFuncDesc.pLabel = luaVarDesc.pLabel;
        luaFuncDesc.mType = TF_LUA_WIDGET_FUNCTION_ON_EDITED;
        luaFuncDesc.pFunc = JogClipPlayCallback;
        luaRegisterWidgetFunction(&luaFuncDesc);

        // Loop
        luaVarDesc.pLabel = "Loop Jog";
        luaVarDesc.mWidgetType = TF_WIDGET_TYPE_CHECKBOX;
        luaVarDesc.pBool = &gUIData.mJogClip.mLoop;
        luaRegisterWidgetVariable(&luaVarDesc);
        luaFuncDesc.pLabel = luaVarDesc.pLabel;
        luaFuncDesc.mType = TF_LUA_WIDGET_FUNCTION_ON_EDITED;
        luaFuncDesc.pFunc = JogClipLoopCallback;
        luaRegisterWidgetFunction(&luaFuncDesc);

        // Animation Time
        luaVarDesc.pLabel = "Animation Time Jog";
        luaVarDesc.mWidgetType = TF_WIDGET_TYPE_SLIDER_FLOAT;
        luaVarDesc.pFloat = &gUIData.mJogClip.mAnimationTime;
        luaRegisterWidgetVariable(&luaVarDesc);
        luaFuncDesc.pLabel = luaVarDesc.pLabel;
        luaFuncDesc.mType = TF_LUA_WIDGET_FUNCTION_ON_EDITED;
        luaFuncDesc.pFunc = JogClipTimeChangeCallback;
        luaRegisterWidgetFunction(&luaFuncDesc);

        // Playback Speed
        luaVarDesc.pLabel = "Playback Speed Jog";
        luaVarDesc.mWidgetType = TF_WIDGET_TYPE_SLIDER_FLOAT;
        luaVarDesc.pFloat = &gUIData.mJogClip.mPlaybackSpeed;
        luaRegisterWidgetVariable(&luaVarDesc);
        luaFuncDesc.pLabel = luaVarDesc.pLabel;
        luaFuncDesc.mType = TF_LUA_WIDGET_FUNCTION_ON_EDITED;
        luaFuncDesc.pFunc = JogClipPlaybackSpeedChangeCallback;
        luaRegisterWidgetFunction(&luaFuncDesc);
    }

    // RUN CLIP
    {
        // Play
        luaVarDesc.pLabel = "Play Run";
        luaVarDesc.mWidgetType = TF_WIDGET_TYPE_CHECKBOX;
        luaVarDesc.pBool = &gUIData.mRunClip.mPlay;
        luaRegisterWidgetVariable(&luaVarDesc);
        luaFuncDesc.pLabel = luaVarDesc.pLabel;
        luaFuncDesc.mType = TF_LUA_WIDGET_FUNCTION_ON_EDITED;
        luaFuncDesc.pFunc = RunClipPlayCallback;
        luaRegisterWidgetFunction(&luaFuncDesc);

        // Loop
        luaVarDesc.pLabel = "Loop Run";
        luaVarDesc.mWidgetType = TF_WIDGET_TYPE_CHECKBOX;
        luaVarDesc.pBool = &gUIData.mRunClip.mLoop;
        luaRegisterWidgetVariable(&luaVarDesc);
        luaFuncDesc.pLabel = luaVarDesc.pLabel;
        luaFuncDesc.mType = TF_LUA_WIDGET_FUNCTION_ON_EDITED;
        luaFuncDesc.pFunc = RunClipLoopCallback;
        luaRegisterWidgetFunction(&luaFuncDesc);

        // Animation Time
        luaVarDesc.pLabel = "Animation Time Run";
        luaVarDesc.mWidgetType = TF_WIDGET_TYPE_SLIDER_FLOAT;
        luaVarDesc.pFloat = &gUIData.mRunClip.mAnimationTime;
        luaRegisterWidgetVariable(&luaVarDesc);
        luaFuncDesc.pLabel = luaVarDesc.pLabel;
        luaFuncDesc.mType = TF_LUA_WIDGET_FUNCTION_ON_EDITED;
        luaFuncDesc.pFunc = RunClipTimeChangeCallback;
        luaRegisterWidgetFunction(&luaFuncDesc);

        // Playback Speed
        luaVarDesc.pLabel = "Playback Speed Run";
        luaVarDesc.mWidgetType = TF_WIDGET_TYPE_SLIDER_FLOAT;
        luaVarDesc.pFloat = &gUIData.mRunClip.mPlaybackSpeed;
        luaRegisterWidgetVariable(&luaVarDesc);
        luaFuncDesc.pLabel = luaVarDesc.pLabel;
        luaFuncDesc.mType = TF_LUA_WIDGET_FUNCTION_ON_EDITED;
        luaFuncDesc.pFunc = RunClipPlaybackSpeedChangeCallback;
        luaRegisterWidgetFunction(&luaFuncDesc);
    }

    // NECK CRACK CLIP
    {
        // Play
        luaVarDesc.pLabel = "Play Neck";
        luaVarDesc.mWidgetType = TF_WIDGET_TYPE_CHECKBOX;
        luaVarDesc.pBool = &gUIData.mNeckCrackClip.mPlay;
        luaRegisterWidgetVariable(&luaVarDesc);
        luaFuncDesc.pLabel = luaVarDesc.pLabel;
        luaFuncDesc.mType = TF_LUA_WIDGET_FUNCTION_ON_EDITED;
        luaFuncDesc.pFunc = NeckClipPlayCallback;
        luaRegisterWidgetFunction(&luaFuncDesc);

        // Loop
        luaVarDesc.pLabel = "Loop Neck";
        luaVarDesc.mWidgetType = TF_WIDGET_TYPE_CHECKBOX;
        luaVarDesc.pBool = &gUIData.mNeckCrackClip.mLoop;
        luaRegisterWidgetVariable(&luaVarDesc);
        luaFuncDesc.pLabel = luaVarDesc.pLabel;
        luaFuncDesc.mType = TF_LUA_WIDGET_FUNCTION_ON_EDITED;
        luaFuncDesc.pFunc = NeckClipLoopCallback;
        luaRegisterWidgetFunction(&luaFuncDesc);

        // Animation Time
        luaVarDesc.pLabel = "Animation Time Neck";
        luaVarDesc.mWidgetType = TF_WIDGET_TYPE_SLIDER_FLOAT;
        luaVarDesc.pFloat = &gUIData.mNeckCrackClip.mAnimationTime;
        luaRegisterWidgetVariable(&luaVarDesc);
        luaFuncDesc.pLabel = luaVarDesc.pLabel;
        luaFuncDesc.mType = TF_LUA_WIDGET_FUNCTION_ON_EDITED;
        luaFuncDesc.pFunc = NeckCrackClipTimeChangeCallback;
        luaRegisterWidgetFunction(&luaFuncDesc);

        // Playback Speed
        luaVarDesc.pLabel = "Playback Speed Neck";
        luaVarDesc.mWidgetType = TF_WIDGET_TYPE_SLIDER_FLOAT;
        luaVarDesc.pFloat = &gUIData.mNeckCrackClip.mPlaybackSpeed;
        luaRegisterWidgetVariable(&luaVarDesc);
        luaFuncDesc.pLabel = luaVarDesc.pLabel;
        luaFuncDesc.mType = TF_LUA_WIDGET_FUNCTION_ON_EDITED;
        luaFuncDesc.pFunc = NeckClipPlaybackSpeedChangeCallback;
        luaRegisterWidgetFunction(&luaFuncDesc);
    }

    // Blending
    {
        luaVarDesc.pLabel = "Auto Set Blend Params Blending";
        luaVarDesc.mWidgetType = TF_WIDGET_TYPE_CHECKBOX;
        luaVarDesc.pBool = &gUIData.mBlendingParams.mAutoSetBlendParams;
        luaRegisterWidgetVariable(&luaVarDesc);
        luaFuncDesc.pLabel = luaVarDesc.pLabel;
        luaFuncDesc.mType = TF_LUA_WIDGET_FUNCTION_ON_EDITED;
        luaFuncDesc.pFunc = AutoSetBlendParamsCallback;
        luaRegisterWidgetFunction(&luaFuncDesc);

        luaVarDesc.pLabel = "Blend Ratio";
        luaVarDesc.mWidgetType = TF_WIDGET_TYPE_SLIDER_FLOAT;
        luaVarDesc.pFloat = &gUIData.mBlendingParams.mBlendRatio;
        luaRegisterWidgetVariable(&luaVarDesc);
        luaFuncDesc.pLabel = luaVarDesc.pLabel;
        luaFuncDesc.mType = TF_LUA_WIDGET_FUNCTION_ON_EDITED;
        luaFuncDesc.pFunc = BlendRatioChangeCallback;
        luaRegisterWidgetFunction(&luaFuncDesc);

        luaVarDesc.pLabel = "Clip Weight Blending [Walk]";
        luaVarDesc.mWidgetType = TF_WIDGET_TYPE_SLIDER_FLOAT;
        luaVarDesc.pFloat = &gUIData.mBlendingParams.mWalkClipWeight;
        luaRegisterWidgetVariable(&luaVarDesc);
        luaFuncDesc.pLabel = luaVarDesc.pLabel;
        luaFuncDesc.mType = TF_LUA_WIDGET_FUNCTION_ON_EDITED;
        luaFuncDesc.pFunc = WalkClipWeightCallback;
        luaRegisterWidgetFunction(&luaFuncDesc);

        luaVarDesc.pLabel = "Clip Weight Blending [Jog]";
        luaVarDesc.mWidgetType = TF_WIDGET_TYPE_SLIDER_FLOAT;
        luaVarDesc.pFloat = &gUIData.mBlendingParams.mJogClipWeight;
        luaRegisterWidgetVariable(&luaVarDesc);
        luaFuncDesc.pLabel = luaVarDesc.pLabel;
        luaFuncDesc.mType = TF_LUA_WIDGET_FUNCTION_ON_EDITED;
        luaFuncDesc.pFunc = JogClipWeightCallback;
        luaRegisterWidgetFunction(&luaFuncDesc);

        luaVarDesc.pLabel = "Clip Weight Blending [Run]";
        luaVarDesc.mWidgetType = TF_WIDGET_TYPE_SLIDER_FLOAT;
        luaVarDesc.pFloat = &gUIData.mBlendingParams.mRunClipWeight;
        luaRegisterWidgetVariable(&luaVarDesc);
        luaFuncDesc.pLabel = luaVarDesc.pLabel;
        luaFuncDesc.mType = TF_LUA_WIDGET_FUNCTION_ON_EDITED;
        luaFuncDesc.pFunc = RunClipWeightCallback;
        luaRegisterWidgetFunction(&luaFuncDesc);

        luaVarDesc.pLabel = "Threshold Blending";
        luaVarDesc.mWidgetType = TF_WIDGET_TYPE_SLIDER_FLOAT;
        luaVarDesc.pFloat = &gUIData.mBlendingParams.mThreshold;
        luaRegisterWidgetVariable(&luaVarDesc);
        luaFuncDesc.pLabel = luaVarDesc.pLabel;
        luaFuncDesc.mType = TF_LUA_WIDGET_FUNCTION_ON_EDITED;
        luaFuncDesc.pFunc = ThresholdChangeCallback;
        luaRegisterWidgetFunction(&luaFuncDesc);
    }

    // PartialBlending
    {
        luaVarDesc.pLabel = "Auto Set Blend Params PartialBlending";
        luaVarDesc.mWidgetType = TF_WIDGET_TYPE_CHECKBOX;
        luaVarDesc.pBool = &gUIData.mPartialBlendingParams.mAutoSetBlendParams;
        luaRegisterWidgetVariable(&luaVarDesc);
        luaFuncDesc.pLabel = luaVarDesc.pLabel;
        luaFuncDesc.mType = TF_LUA_WIDGET_FUNCTION_ON_EDITED;
        luaFuncDesc.pFunc = AutoSetBlendParamsCallback;
        luaRegisterWidgetFunction(&luaFuncDesc);

        luaVarDesc.pLabel = "Upper Body Weight";
        luaVarDesc.mWidgetType = TF_WIDGET_TYPE_SLIDER_FLOAT;
        luaVarDesc.pFloat = &gUIData.mPartialBlendingParams.mUpperBodyWeight;
        luaRegisterWidgetVariable(&luaVarDesc);
        luaFuncDesc.pLabel = luaVarDesc.pLabel;
        luaFuncDesc.mType = TF_LUA_WIDGET_FUNCTION_ON_EDITED;
        luaFuncDesc.pFunc = UpperBodyWeightCallback;
        luaRegisterWidgetFunction(&luaFuncDesc);

        luaVarDesc.pLabel = "Clip Weight PartialBlending [Stand]";
        luaVarDesc.mWidgetType = TF_WIDGET_TYPE_SLIDER_FLOAT;
        luaVarDesc.pFloat = &gUIData.mPartialBlendingParams.mStandClipWeight;
        luaRegisterWidgetVariable(&luaVarDesc);
        luaFuncDesc.pLabel = luaVarDesc.pLabel;
        luaFuncDesc.mType = TF_LUA_WIDGET_FUNCTION_ON_EDITED;
        luaFuncDesc.pFunc = StandClipWeightCallback;
        luaRegisterWidgetFunction(&luaFuncDesc);

        luaVarDesc.pLabel = "Joints Weight PartialBlending [Stand]";
        luaVarDesc.mWidgetType = TF_WIDGET_TYPE_SLIDER_FLOAT;
        luaVarDesc.pFloat = &gUIData.mPartialBlendingParams.mStandJointsWeight;
        luaRegisterWidgetVariable(&luaVarDesc);
        luaFuncDesc.pLabel = luaVarDesc.pLabel;
        luaFuncDesc.mType = TF_LUA_WIDGET_FUNCTION_ON_EDITED;
        luaFuncDesc.pFunc = StandClipJointsWeightCallback;
        luaRegisterWidgetFunction(&luaFuncDesc);

        luaVarDesc.pLabel = "Clip Weight PartialBlending [Walk]";
        luaVarDesc.mWidgetType = TF_WIDGET_TYPE_SLIDER_FLOAT;
        luaVarDesc.pFloat = &gUIData.mPartialBlendingParams.mWalkClipWeight;
        luaRegisterWidgetVariable(&luaVarDesc);
        luaFuncDesc.pLabel = luaVarDesc.pLabel;
        luaFuncDesc.mType = TF_LUA_WIDGET_FUNCTION_ON_EDITED;
        luaFuncDesc.pFunc = WalkClipWeightCallback;
        luaRegisterWidgetFunction(&luaFuncDesc);

        luaVarDesc.pLabel = "Joints Weight PartialBlending [Walk]";
        luaVarDesc.mWidgetType = TF_WIDGET_TYPE_SLIDER_FLOAT;
        luaVarDesc.pFloat = &gUIData.mPartialBlendingParams.mWalkJointsWeight;
        luaRegisterWidgetVariable(&luaVarDesc);
        luaFuncDesc.pLabel = luaVarDesc.pLabel;
        luaFuncDesc.mType = TF_LUA_WIDGET_FUNCTION_ON_EDITED;
        luaFuncDesc.pFunc = WalkClipJointsWeightCallback;
        luaRegisterWidgetFunction(&luaFuncDesc);

        luaVarDesc.pLabel = "Threshold PartialBlending";
        luaVarDesc.mWidgetType = TF_WIDGET_TYPE_SLIDER_FLOAT;
        luaVarDesc.pFloat = &gUIData.mPartialBlendingParams.mThreshold;
        luaRegisterWidgetVariable(&luaVarDesc);
        luaFuncDesc.pLabel = luaVarDesc.pLabel;
        luaFuncDesc.mType = TF_LUA_WIDGET_FUNCTION_ON_EDITED;
        luaFuncDesc.pFunc = ThresholdChangeCallback;
        luaRegisterWidgetFunction(&luaFuncDesc);

        luaVarDesc.pLabel = "UpperBody Joint Index";
        luaVarDesc.mWidgetType = TF_WIDGET_TYPE_SLIDER_UINT;
        luaVarDesc.pUint = &gUIData.mUpperBodyJointIndex;
        luaRegisterWidgetVariable(&luaVarDesc);
        luaFuncDesc.pLabel = luaVarDesc.pLabel;
        luaFuncDesc.mType = TF_LUA_WIDGET_FUNCTION_ON_EDITED;
        luaFuncDesc.pFunc = UpperBodyJointIndexCallback;
        luaRegisterWidgetFunction(&luaFuncDesc);
    }

    // AdditiveBlending
    {
        luaVarDesc.pLabel = "Clip Weight AdditiveBlending [Walk]";
        luaVarDesc.mWidgetType = TF_WIDGET_TYPE_SLIDER_FLOAT;
        luaVarDesc.pFloat = &gUIData.mAdditiveBlendingParams.mWalkClipWeight;
        luaRegisterWidgetVariable(&luaVarDesc);
        luaFuncDesc.pLabel = luaVarDesc.pLabel;
        luaFuncDesc.mType = TF_LUA_WIDGET_FUNCTION_ON_EDITED;
        luaFuncDesc.pFunc = WalkClipWeightCallback;
        luaRegisterWidgetFunction(&luaFuncDesc);

        luaVarDesc.pLabel = "Clip Weight AdditiveBlending [NeckCrack]";
        luaVarDesc.mWidgetType = TF_WIDGET_TYPE_SLIDER_FLOAT;
        luaVarDesc.pFloat = &gUIData.mAdditiveBlendingParams.mNeckCrackClipWeight;
        luaRegisterWidgetVariable(&luaVarDesc);
        luaFuncDesc.pLabel = luaVarDesc.pLabel;
        luaFuncDesc.mType = TF_LUA_WIDGET_FUNCTION_ON_EDITED;
        luaFuncDesc.pFunc = NeckCrackClipWeightCallback;
        luaRegisterWidgetFunction(&luaFuncDesc);

        luaVarDesc.pLabel = "Enable Mask";
        luaVarDesc.mWidgetType = TF_WIDGET_TYPE_CHECKBOX;
        luaVarDesc.pBool = &gUIData.mUpperBodyMask.mEnableMask;
        luaRegisterWidgetVariable(&luaVarDesc);
        luaFuncDesc.pLabel = luaVarDesc.pLabel;
        luaFuncDesc.mType = TF_LUA_WIDGET_FUNCTION_ON_EDITED;
        luaFuncDesc.pFunc = EnableMaskCallback;
        luaRegisterWidgetFunction(&luaFuncDesc);

        luaVarDesc.pLabel = "Joints Weight AdditiveBlending [NeckCrack]";
        luaVarDesc.mWidgetType = TF_WIDGET_TYPE_SLIDER_FLOAT;
        luaVarDesc.pFloat = &gUIData.mUpperBodyMask.mNeckCrackJointsWeight;
        luaRegisterWidgetVariable(&luaVarDesc);
        luaFuncDesc.pLabel = luaVarDesc.pLabel;
        luaFuncDesc.mType = TF_LUA_WIDGET_FUNCTION_ON_EDITED;
        luaFuncDesc.pFunc = NeckCrackClipJointsWeightCallback;
        luaRegisterWidgetFunction(&luaFuncDesc);

        luaVarDesc.pLabel = "Root Joint Index";
        luaVarDesc.mWidgetType = TF_WIDGET_TYPE_SLIDER_UINT;
        luaVarDesc.pUint = &gUIData.mUpperBodyMask.mUpperBodyJointIndex;
        luaRegisterWidgetVariable(&luaVarDesc);
        luaFuncDesc.pLabel = luaVarDesc.pLabel;
        luaFuncDesc.mType = TF_LUA_WIDGET_FUNCTION_ON_EDITED;
        luaFuncDesc.pFunc = UpperBodyJointIndexCallback;
        luaRegisterWidgetFunction(&luaFuncDesc);
    }

    // Animations
    luaVarDesc.pLabel = "Animation";
    luaVarDesc.mWidgetType = TF_WIDGET_TYPE_DROPDOWN;
    luaVarDesc.pInt = (int32_t*)&gCurrentAnimationIndex;
    luaRegisterWidgetVariable(&luaVarDesc);

    luaFuncDesc.pLabel = "Run Animation";
    luaFuncDesc.mType = TF_LUA_WIDGET_FUNCTION_ON_EDITED;
    luaFuncDesc.pFunc = RunAnimation;
    luaRegisterWidgetFunction(&luaFuncDesc);

    // Scripts
    luaVarDesc.pLabel = "Test Scripts";
    luaVarDesc.mWidgetType = TF_WIDGET_TYPE_DROPDOWN;
    luaVarDesc.pInt = (int32_t*)&gCurrentScriptIndex;
    luaRegisterWidgetVariable(&luaVarDesc);

    luaFuncDesc.pLabel = "Run Script";
    luaFuncDesc.mType = TF_LUA_WIDGET_FUNCTION_ON_EDITED;
    luaFuncDesc.pFunc = RunScript;
    luaRegisterWidgetFunction(&luaFuncDesc);
}

void updateGuiClip(const char* pLabel, UIData::ClipData* pClip, ClipController* pController, void (**ppCallbacks)(void*))
{
    if (UI_WIDGET_IS_VISIBLE(uiCollapsingHeaderBegin(pLabel, true)))
    {
        uiLayoutAutoTextRows(1);
        if (UI_WIDGET_IS_CHANGED(uiCheckbox("Play", &pClip->mPlay)))
        {
            ppCallbacks[0](NULL);
        }
        if (UI_WIDGET_IS_CHANGED(uiCheckbox("Loop", &pClip->mLoop)))
        {
            ppCallbacks[1](NULL);
        }
        uiLayoutAutoTextRows(2);
        uiLabel("Animation Time", TF_ALIGN_LEFT);
        if (UI_WIDGET_IS_CHANGED(uiSliderFloat(&pClip->mAnimationTime, 0.0f, pController->mDuration, 0.01f)))
        {
            ppCallbacks[2](NULL);
        }
        uiLabel("Playback Speed", TF_ALIGN_LEFT);
        if (UI_WIDGET_IS_CHANGED(uiSliderFloat(&pClip->mPlaybackSpeed, -5.0f, 5.0f, 0.1f)))
        {
            ppCallbacks[3](NULL);
        }

        uiCollapsingHeaderEnd();
    }
}

void updateGui()
{
    if (UI_WINDOW_IS_VISIBLE(uiBeginWidgetWindow(&gStandaloneAnimationsDesc)))
    {
        if (UI_WIDGET_IS_VISIBLE(uiCollapsingHeaderBegin("General Settings", true)))
        {
            uiLayoutAutoTextRows(1);
            if (UI_WIDGET_IS_PRESSED(uiButton("Randomize Clips Time")))
            {
                RandomTimeCallback(NULL);
            }
            uiLayoutAutoTextRows(2);
            uiLabel("Number of Rigs", TF_ALIGN_LEFT);
            if (UI_WIDGET_IS_CHANGED(uiSliderUint(gUIData.mGeneralSettings.mNumberOfRigs, 1, gGpuSettings.mMaxRigs, 1)))
            {
                RunAnimation(NULL);
            }
            uiLayoutAutoTextRows(1);
            uiCheckbox("Show Bind Pose", &gUIData.mGeneralSettings.mShowBindPose);
            uiCheckbox("Draw Attached Object", &gUIData.mGeneralSettings.mDrawAttachedObject);
            uiCheckbox("Draw Plane", &gUIData.mGeneralSettings.mDrawPlane);
            if (UI_WIDGET_IS_CHANGED(uiCheckbox("Draw Baked Physics", &gUIData.mGeneralSettings.mDrawBakedPhysics)))
            {
                ShatterClipTimeChangeCallback(NULL);
            }
            uiCheckbox("Animate Camera", &gUIData.mGeneralSettings.mAnimatedCamera);

            uiCollapsingHeaderEnd();
        }
        if (UI_WIDGET_IS_VISIBLE(uiCollapsingHeaderBegin("Threading Control", true)))
        {
            uiLayoutAutoTextRows(1);
            uiCheckbox("Enable Threading", gUIData.mThreadingControl.mEnableThreading);
            uiCheckbox("Automate Threading", gUIData.mThreadingControl.mAutomateThreading);
            uiLayoutAutoTextRows(2);
            uiLabel("Grain Size", TF_ALIGN_LEFT);
            uiSliderUint(gUIData.mThreadingControl.mGrainSize, 1, gGpuSettings.mMaxRigs, 1);

            uiCollapsingHeaderEnd();
        }
        if (UI_WIDGET_IS_VISIBLE(uiCollapsingHeaderBegin("Inverse Kinematics", true)))
        {
            uiLayoutAutoTextRows(1);
            uiCheckbox("Aim IK", &gUIData.mIKParams.mAim);
            uiCheckbox("Two Bone IK", &gUIData.mIKParams.mTwoBoneIK);
            uiLayoutAutoTextRows(2);
            uiLabel("Foot Two Bone IK", TF_ALIGN_LEFT);
            uiSliderFloat(&gUIData.mIKParams.mFoot, 0.0f, 0.5f, 0.01f);

            uiCollapsingHeaderEnd();
        }
        if (UI_WIDGET_IS_VISIBLE(uiCollapsingHeaderBegin("Attachment", true)))
        {
            uiLayoutAutoTextRows(2);
            uiLabel("Joint Index", TF_ALIGN_LEFT);
            uiSliderUint(&gUIData.mAttachedObject.mJointIndex, 0, gStickFigureRig.mNumJoints - 1, 1);
            uiLayoutAutoTextRows(4);
            uiLabel("Offset", TF_ALIGN_LEFT);
            uiSliderFloat3(&gUIData.mAttachedObject.mOffset, { -1.0f, -1.0f, -1.0f }, { 1.0f, 1.0f, 1.0f }, { 0.001f, 0.001f, 0.001f });

            uiCollapsingHeaderEnd();
        }

        uiLayoutAutoTextRows(2);
        uiLabel("Animation", TF_ALIGN_LEFT);
        gCurrentAnimationIndex = UI_WIDGET_GET_SELECTED(uiDropdown(gAnimationNames, ANIMATIONCOUNT, gCurrentAnimationIndex));
        uiLayoutAutoTextRows(1);
        if (UI_WIDGET_IS_PRESSED(uiButton("Run Animation")))
        {
            RunAnimation(NULL);
        }
        uiLayoutAutoTextRows(2);
        uiLabel("Test Scripts", TF_ALIGN_LEFT);
        gCurrentScriptIndex =
            UI_WIDGET_GET_SELECTED(uiDropdown(gTestScripts, sizeof(gTestScripts) / sizeof(gTestScripts[0]), gCurrentScriptIndex));
        uiLayoutAutoTextRows(1);
        if (UI_WIDGET_IS_PRESSED(uiButton("Run Script")))
        {
            RunScript(NULL);
        }
    }
    uiEndWidgetWindow();

    // Playback
    if (gCurrentAnimationIndex == 0)
    {
        if (UI_WINDOW_IS_VISIBLE(uiBeginWidgetWindow(&gAnimationControlsDesc[0])))
        {
            void (*callbacks[4])(void*) = { StandClipPlayCallback, StandClipLoopCallback, StandClipTimeChangeCallback,
                                            StandClipPlaybackSpeedChangeCallback };
            updateGuiClip("Stand Clip", &gUIData.mStandClip, &gStandClipController[0], callbacks);
        }
        uiEndWidgetWindow();
    }

    // Blending
    if (gCurrentAnimationIndex == 1)
    {
        if (UI_WINDOW_IS_VISIBLE(uiBeginWidgetWindow(&gAnimationControlsDesc[1])))
        {
            void (*callbacks[4])(void*) = { WalkClipPlayCallback, WalkClipLoopCallback, WalkClipTimeChangeCallback,
                                            WalkClipPlaybackSpeedChangeCallback };
            updateGuiClip("Walk Clip", &gUIData.mWalkClip, &gWalkClipController[0], callbacks);
            callbacks[0] = JogClipPlayCallback;
            callbacks[1] = JogClipLoopCallback;
            callbacks[2] = JogClipTimeChangeCallback;
            callbacks[3] = JogClipPlaybackSpeedChangeCallback;
            updateGuiClip("Jog Clip", &gUIData.mJogClip, &gJogClipController[0], callbacks);
            callbacks[0] = RunClipPlayCallback;
            callbacks[1] = RunClipLoopCallback;
            callbacks[2] = RunClipTimeChangeCallback;
            callbacks[3] = RunClipPlaybackSpeedChangeCallback;
            updateGuiClip("Run Clip", &gUIData.mRunClip, &gRunClipController[0], callbacks);

            if (UI_WIDGET_IS_VISIBLE(uiCollapsingHeaderBegin("Blend Parameters", true)))
            {
                if (UI_WIDGET_IS_CHANGED(uiCheckbox("Auto Set Blend Params", &gUIData.mBlendingParams.mAutoSetBlendParams)))
                {
                    AutoSetBlendParamsCallback(NULL);
                }
                uiLayoutAutoTextRows(2);
                uiLabel("Blend Ratio", TF_ALIGN_LEFT);
                if (UI_WIDGET_IS_CHANGED(uiSliderFloat(&gUIData.mBlendingParams.mBlendRatio, 0.0f, 1.0f, 0.01f)))
                {
                    BlendRatioChangeCallback(NULL);
                }
                uiLabel("Clip Weight [Walk]", TF_ALIGN_LEFT);
                if (UI_WIDGET_IS_CHANGED(uiSliderFloat(&gUIData.mBlendingParams.mWalkClipWeight, 0.0f, 1.0f, 0.01f)))
                {
                    WalkClipWeightCallback(NULL);
                }
                uiLabel("Clip Weight [Jog]", TF_ALIGN_LEFT);
                if (UI_WIDGET_IS_CHANGED(uiSliderFloat(&gUIData.mBlendingParams.mJogClipWeight, 0.0f, 1.0f, 0.01f)))
                {
                    JogClipWeightCallback(NULL);
                }
                uiLabel("Clip Weight [Run]", TF_ALIGN_LEFT);
                if (UI_WIDGET_IS_CHANGED(uiSliderFloat(&gUIData.mBlendingParams.mRunClipWeight, 0.0f, 1.0f, 0.01f)))
                {
                    RunClipWeightCallback(NULL);
                }
                uiLabel("Threshold", TF_ALIGN_LEFT);
                if (UI_WIDGET_IS_CHANGED(uiSliderFloat(&gUIData.mBlendingParams.mThreshold, 0.01f, 1.0f, 0.01f)))
                {
                    ThresholdChangeCallback(NULL);
                }

                uiCollapsingHeaderEnd();
            }
        }
        uiEndWidgetWindow();
    }

    // PartialBlending
    if (gCurrentAnimationIndex == 2)
    {
        if (UI_WINDOW_IS_VISIBLE(uiBeginWidgetWindow(&gAnimationControlsDesc[2])))
        {
            void (*callbacks[4])(void*) = { StandClipPlayCallback, StandClipLoopCallback, StandClipTimeChangeCallback,
                                            StandClipPlaybackSpeedChangeCallback };
            updateGuiClip("Stand Clip", &gUIData.mStandClip, &gStandClipController[0], callbacks);
            callbacks[0] = WalkClipPlayCallback;
            callbacks[1] = WalkClipLoopCallback;
            callbacks[2] = WalkClipTimeChangeCallback;
            callbacks[3] = WalkClipPlaybackSpeedChangeCallback;
            updateGuiClip("Walk Clip", &gUIData.mWalkClip, &gWalkClipController[0], callbacks);

            if (UI_WIDGET_IS_VISIBLE(uiCollapsingHeaderBegin("Blend Parameters", true)))
            {
                uiLayoutAutoTextRows(1);
                if (UI_WIDGET_IS_CHANGED(uiCheckbox("Auto Set Blend Params", &gUIData.mPartialBlendingParams.mAutoSetBlendParams)))
                {
                    AutoSetBlendParamsCallback(NULL);
                }
                uiLayoutAutoTextRows(2);
                uiLabel("Upper Body Weight", TF_ALIGN_LEFT);
                if (UI_WIDGET_IS_CHANGED(uiSliderFloat(&gUIData.mPartialBlendingParams.mUpperBodyWeight, 0.0f, 1.0f, 0.01f)))
                {
                    UpperBodyWeightCallback(NULL);
                }
                uiLabel("Clip Weight [Stand]", TF_ALIGN_LEFT);
                if (UI_WIDGET_IS_CHANGED(uiSliderFloat(&gUIData.mPartialBlendingParams.mStandClipWeight, 0.0f, 1.0f, 0.01f)))
                {
                    StandClipWeightCallback(NULL);
                }
                uiLabel("Joints Weight [Stand]", TF_ALIGN_LEFT);
                if (UI_WIDGET_IS_CHANGED(uiSliderFloat(&gUIData.mPartialBlendingParams.mStandJointsWeight, 0.0f, 1.0f, 0.01f)))
                {
                    StandClipJointsWeightCallback(NULL);
                }
                uiLabel("Clip Weight [Walk]", TF_ALIGN_LEFT);
                if (UI_WIDGET_IS_CHANGED(uiSliderFloat(&gUIData.mPartialBlendingParams.mWalkClipWeight, 0.0f, 1.0f, 0.01f)))
                {
                    WalkClipWeightCallback(NULL);
                }
                uiLabel("Joints Weight [Walk]", TF_ALIGN_LEFT);
                if (UI_WIDGET_IS_CHANGED(uiSliderFloat(&gUIData.mPartialBlendingParams.mWalkJointsWeight, 0.0f, 1.0f, 0.01f)))
                {
                    WalkClipJointsWeightCallback(NULL);
                }
                uiLabel("Threshold", TF_ALIGN_LEFT);
                if (UI_WIDGET_IS_CHANGED(uiSliderFloat(&gUIData.mPartialBlendingParams.mThreshold, 0.01f, 1.0f, 0.01f)))
                {
                    ThresholdChangeCallback(NULL);
                }

                uiCollapsingHeaderEnd();
            }
            if (UI_WIDGET_IS_VISIBLE(uiCollapsingHeaderBegin("Upper Body Root", true)))
            {
                uiLayoutAutoTextRows(2);
                uiLabel("UpperBody Joint Index", TF_ALIGN_LEFT);
                if (UI_WIDGET_IS_CHANGED(uiSliderUint(&gUIData.mUpperBodyJointIndex, 0, gStickFigureRig.mNumJoints - 1, 1)))
                {
                    UpperBodyJointIndexCallback(NULL);
                }

                uiCollapsingHeaderEnd();
            }
        }
        uiEndWidgetWindow();
    }

    // AdditiveBlending
    if (gCurrentAnimationIndex == 3)
    {
        if (UI_WINDOW_IS_VISIBLE(uiBeginWidgetWindow(&gAnimationControlsDesc[3])))
        {
            void (*callbacks[4])(void*) = { WalkClipPlayCallback, WalkClipLoopCallback, WalkClipTimeChangeCallback,
                                            WalkClipPlaybackSpeedChangeCallback };
            updateGuiClip("Walk Clip", &gUIData.mWalkClip, &gWalkClipController[0], callbacks);
            callbacks[0] = NeckClipPlayCallback;
            callbacks[1] = NeckClipLoopCallback;
            callbacks[2] = NeckCrackClipTimeChangeCallback;
            callbacks[3] = NeckClipPlaybackSpeedChangeCallback;
            updateGuiClip("Neck Crack Clip", &gUIData.mNeckCrackClip, &gNeckCrackClipController[0], callbacks);

            if (UI_WIDGET_IS_VISIBLE(uiCollapsingHeaderBegin("Blend Parameters", true)))
            {
                uiLayoutAutoTextRows(2);
                uiLabel("Clip Weight [Walk]", TF_ALIGN_LEFT);
                if (UI_WIDGET_IS_CHANGED(uiSliderFloat(&gUIData.mAdditiveBlendingParams.mWalkClipWeight, 0.0f, 1.0f, 0.01f)))
                {
                    WalkClipWeightCallback(NULL);
                }
                uiLabel("Clip Weight [NeckCrack]", TF_ALIGN_LEFT);
                if (UI_WIDGET_IS_CHANGED(uiSliderFloat(&gUIData.mAdditiveBlendingParams.mNeckCrackClipWeight, 0.0f, 1.0f, 0.01f)))
                {
                    NeckCrackClipWeightCallback(NULL);
                }

                uiCollapsingHeaderEnd();
            }
            if (UI_WIDGET_IS_VISIBLE(uiCollapsingHeaderBegin("Upper Body Masking", true)))
            {
                uiLayoutAutoTextRows(1);
                if (UI_WIDGET_IS_CHANGED(uiCheckbox("Enable Mask", &gUIData.mUpperBodyMask.mEnableMask)))
                {
                    EnableMaskCallback(NULL);
                }
                uiLayoutAutoTextRows(2);
                uiLabel("Joints Weight [NeckCrack]", TF_ALIGN_LEFT);
                if (UI_WIDGET_IS_CHANGED(uiSliderFloat(&gUIData.mUpperBodyMask.mNeckCrackJointsWeight, 0.01f, 1.0f, 0.01f)))
                {
                    NeckCrackClipJointsWeightCallback(NULL);
                }
                uiLabel("Root Joint Index", TF_ALIGN_LEFT);
                if (UI_WIDGET_IS_CHANGED(uiSliderUint(&gUIData.mUpperBodyMask.mUpperBodyJointIndex, 0, gStickFigureRig.mNumJoints - 1, 1)))
                {
                    UpperBodyJointIndexCallback(NULL);
                }

                uiCollapsingHeaderEnd();
            }
        }
        uiEndWidgetWindow();
    }
}

//--------------------------------------------------------------------------------------------
// APP CODE
//--------------------------------------------------------------------------------------------
class Animations: public IApp
{
public:
    bool Init() override
    {
        initHiresTimer(&gAnimationUpdateTimer);

        // WINDOW AND RENDERER SETUP
        //
        TFExtendedSettings extendedSettings = {};
        extendedSettings.mNumSettings = ESettings::Count;
        extendedSettings.pSettings = (uint32_t*)&gGpuSettings;
        extendedSettings.ppSettingNames = gSettingNames;

        TFRendererDesc settings;
        memset(&settings, 0, sizeof(settings));
        settings.pExtendedSettings = &extendedSettings;
        initGPUConfig(settings.pExtendedSettings);
        initRenderer(GetName(), &settings, &pRenderer);
        // Check for init success
        if (!pRenderer)
        {
            ShowUnsupportedMessage(getUnsupportedGPUMsg());
            return false;
        }
        setGPUConfig(pRenderer->pContext->mGpus, pRenderer->pContext->mGpuCount, (uint32_t)(pRenderer->pGpu - pRenderer->pContext->mGpus),
                     settings.pExtendedSettings);
        mSettings.mFrameMaxCount = gDataBufferCount;

        // RIG
        //
        // Initialize the rig with the path to its ozz file and its rendering details
        gStickFigureRig.Initialize(TF_RD_ANIMATIONS, gStickFigureName);

        // Generate joint vertex buffer
        gNumberOfJointPoints = 0;
        generateQuad(NULL, &gNumberOfJointPoints, gJointRadius);
        pJointPoints = (float*)tf_malloc(sizeof(float) * gNumberOfJointPoints);
        generateQuad(pJointPoints, &gNumberOfJointPoints, gJointRadius);

        // Generate bone vertex buffer
        gNumberOfBonePoints = 0;
        generateIndexedBonePoints(NULL, &gNumberOfBonePoints, gBoneWidthRatio, gStickFigureRig.mNumJoints,
                                  &gStickFigureRig.mSkeleton.joint_parents()[0]);
        pBonePoints = (float*)tf_malloc(sizeof(float) * gNumberOfBonePoints);
        generateIndexedBonePoints(pBonePoints, &gNumberOfBonePoints, gBoneWidthRatio, gStickFigureRig.mNumJoints,
                                  &gStickFigureRig.mSkeleton.joint_parents()[0]);

        // Generate attached object vertex buffer
        gNumberOfCuboidPoints = 0;
        generateCuboidPoints(NULL, &gNumberOfCuboidPoints, 1.0f, 1.0f, 1.0f, Vector3(0.0f));
        pCuboidPoints = (float*)tf_malloc(sizeof(float) * gNumberOfCuboidPoints);
        generateCuboidPoints(pCuboidPoints, &gNumberOfCuboidPoints, 1.0f, 1.0f, 1.0f, Vector3(0.0f));

        // Generate cubes vertex buffer
        generateCuboidPoints(NULL, &gNumberOfCubes, 0.065f, 0.065f, 0.065f, Vector3(0.0f)); // Use cuboids of size 1x1x1
        pCubesPoints = (float*)tf_malloc(sizeof(float) * gNumberOfCubes);
        generateCuboidPoints(pCubesPoints, &gNumberOfCubes, 0.065f, 0.065f, 0.065f, Vector3(0.0f)); // Use cuboids of size 1x1x1

        gOzzLogoRig.Initialize(TF_RD_ANIMATIONS, gOzzLogoName);

        // Find the index of the joint to mount the camera to
        gCameraIndex = gOzzLogoRig.FindJoint("camera");

        // CLIPS
        //
        // Since all the skeletons are the same we can just initialize with the first one
        gStandClip.Initialize(TF_RD_ANIMATIONS, gStandClipName, &gStickFigureRig);
        gWalkClip.Initialize(TF_RD_ANIMATIONS, gWalkClipName, &gStickFigureRig);
        gJogClip.Initialize(TF_RD_ANIMATIONS, gJogClipName, &gStickFigureRig);
        gRunClip.Initialize(TF_RD_ANIMATIONS, gRunClipName, &gStickFigureRig);
        gNeckCrackClip.Initialize(TF_RD_ANIMATIONS, gNeckCrackClipName, &gStickFigureRig);
        gShatterClip.Initialize(TF_RD_ANIMATIONS, gShatterClipName, &gOzzLogoRig);

        // CLIP MASKS
        //
        gStandClipMask.Initialize(&gStickFigureRig);
        gWalkClipMask.Initialize(&gStickFigureRig);
        gNeckCrackClipMask.Initialize(&gStickFigureRig);

        // Initialize the masks with their default values
        gStandClipMask.DisableAllJoints();
        gStandClipMask.SetAllChildrenOf(kSpineJointIndex, kDefaultStandJointsWeight);

        gWalkClipMask.EnableAllJoints();
        gWalkClipMask.SetAllChildrenOf(kSpineJointIndex, kDefaultWalkJointsWeight);

        gNeckCrackClipMask.DisableAllJoints();
        gNeckCrackClipMask.SetAllChildrenOf(kSpineJointIndex, kDefaultNeckCrackJointsWeight);

        // CLIP CONTROLLERS
        //
        // Initialize with the length of the clip they are controlling and an
        // optional external time to set based on their updating
        gStandClipController[0].Initialize(gStandClip.GetDuration(), &gUIData.mStandClip.mAnimationTime);
        gWalkClipController[0].Initialize(gWalkClip.GetDuration(), &gUIData.mWalkClip.mAnimationTime);
        gJogClipController[0].Initialize(gJogClip.GetDuration(), &gUIData.mJogClip.mAnimationTime);
        gRunClipController[0].Initialize(gRunClip.GetDuration(), &gUIData.mRunClip.mAnimationTime);
        gNeckCrackClipController[0].Initialize(gNeckCrackClip.GetDuration(), &gUIData.mNeckCrackClip.mAnimationTime);
        gShatterClipContoller.Initialize(gShatterClip.GetDuration(), &gUIData.mShatterClip.mAnimationTime);

        for (size_t i = 1; i < gGpuSettings.mMaxRigs; i++)
        {
            gStandClipController[i].Initialize(gStandClip.GetDuration(), NULL);
            gWalkClipController[i].Initialize(gWalkClip.GetDuration(), NULL);
            gJogClipController[i].Initialize(gJogClip.GetDuration(), NULL);
            gRunClipController[i].Initialize(gRunClip.GetDuration(), NULL);
            gNeckCrackClipController[i].Initialize(gNeckCrackClip.GetDuration(), NULL);
        }

        // ANIMATIONS
        //
        AnimationDesc animationDesc{};

        for (size_t i = 0; i < gGpuSettings.mMaxRigs; i++)
        {
            // Stand Animation
            animationDesc = {};
            animationDesc.mRig = &gStickFigureRig;
            animationDesc.mNumLayers = 1;
            animationDesc.mLayerProperties[0].mClip = &gStandClip;
            animationDesc.mLayerProperties[0].mClipController = &gStandClipController[i];
            gAnimations[0][i].Initialize(animationDesc);

            // Blend Animation
            animationDesc = {};
            animationDesc.mRig = &gStickFigureRig;
            animationDesc.mNumLayers = 3;
            animationDesc.mLayerProperties[0].mClip = &gWalkClip;
            animationDesc.mLayerProperties[0].mClipController = &gWalkClipController[i];
            animationDesc.mLayerProperties[1].mClip = &gJogClip;
            animationDesc.mLayerProperties[1].mClipController = &gJogClipController[i];
            animationDesc.mLayerProperties[2].mClip = &gRunClip;
            animationDesc.mLayerProperties[2].mClipController = &gRunClipController[i];
            animationDesc.mBlendType = BlendType::CROSS_DISSOLVE_SYNC;
            gAnimations[1][i].Initialize(animationDesc);

            // PartialBlending Animation
            animationDesc = {};
            animationDesc.mRig = &gStickFigureRig;
            animationDesc.mNumLayers = 2;
            animationDesc.mLayerProperties[0].mClip = &gStandClip;
            animationDesc.mLayerProperties[0].mClipController = &gStandClipController[i];
            animationDesc.mLayerProperties[0].mClipMask = &gStandClipMask;
            animationDesc.mLayerProperties[1].mClip = &gWalkClip;
            animationDesc.mLayerProperties[1].mClipController = &gWalkClipController[i];
            animationDesc.mLayerProperties[1].mClipMask = &gWalkClipMask;
            animationDesc.mBlendType = BlendType::EQUAL;
            gAnimations[2][i].Initialize(animationDesc);

            // AdditiveBlending Animation
            animationDesc = {};
            animationDesc.mRig = &gStickFigureRig;
            animationDesc.mNumLayers = 2;
            animationDesc.mLayerProperties[0].mClip = &gWalkClip;
            animationDesc.mLayerProperties[0].mClipController = &gWalkClipController[i];
            animationDesc.mLayerProperties[1].mClip = &gNeckCrackClip;
            animationDesc.mLayerProperties[1].mClipController = &gNeckCrackClipController[i];
            animationDesc.mLayerProperties[1].mClipMask = &gNeckCrackClipMask;
            animationDesc.mLayerProperties[1].mAdditive = true;
            animationDesc.mBlendType = BlendType::EQUAL;
            gAnimations[3][i].Initialize(animationDesc);
            // For this example we always want the UI and not the animation to control blend parameters
            gAnimations[3][i].mAutoSetBlendParams = false;
        }

        animationDesc = {};
        animationDesc.mRig = &gOzzLogoRig;
        animationDesc.mNumLayers = 1;
        animationDesc.mLayerProperties[0].mClip = &gShatterClip;
        animationDesc.mLayerProperties[0].mClipController = &gShatterClipContoller;
        gShatterAnimation.Initialize(animationDesc);

        // ANIMATED OBJECTS
        //
        const unsigned int gridWidth = 25;
        const unsigned int gridDepth = 10;
        for (unsigned int i = 0; i < gGpuSettings.mMaxRigs; i++)
        {
            gStickFigureAnimObject[i].Initialize(&gStickFigureRig, &gAnimations[0][i]);

            // Calculate and set offset for each rig
            vec3 offset =
                vec3(-8.75f + 0.75f * (i % gridWidth), ((i / gridWidth) / gridDepth) * 2.0f, 8.0f - 2 * ((i / gridWidth) % gridDepth));
            gStickFigureAnimObject[i].mRootTransform = mat4::translation(offset);

            gStickFigureAnimObject[i].ComputeBindPose(gStickFigureAnimObject[i].mRootTransform);
            gStickFigureAnimObject[i].ComputeJointScales(gStickFigureAnimObject[i].mRootTransform);

            // Alternate the bone colors
            if (i % 2 == 1)
            {
#ifdef ENABLE_FORGE_ANIMATION_DEBUG
                gStickFigureAnimObject[i].mBoneColor = vec4(.1f, .2f, .9f, 1.f);
#endif
            }
        }
        gOzzLogoAnimObject.Initialize(&gOzzLogoRig, &gShatterAnimation);
        gOzzLogoAnimObject.mRootTransform = mat4::translation(vec3(-12.5f, 0.0f, 0.0f)) * mat4::rotationY(degToRad(180.0f));

        const char* aimJointNames[4] = { "Head", "Spine3", "Spine2", "Spine1" };
        gAimIKDesc.mForward = Vector3::yAxis();
        gAimIKDesc.mOffset = Vector3(.07f, .1f, 0.f);
        gAimIKDesc.mPoleVector = Vector3::yAxis();
        gAimIKDesc.mTwistAngle = 0.0f;
        gAimIKDesc.mJointWeight = 0.5f;
        gAimIKDesc.mJointChainLength = 4;
        gAimIKDesc.mJointChain = gJointChain;
        gAimIKDesc.mJointUpVectors = gJointUpVectors;
        gStickFigureRig.FindJointChain(aimJointNames, gAimIKDesc.mJointChainLength, gJointChain);

        const char* twoBonesJointNames[] = { "RightUpLeg", "RightLeg", "RightFoot" };
        gTwoBonesIKDesc.mSoften = 1.0f;
        gTwoBonesIKDesc.mWeight = 1.0f;
        gTwoBonesIKDesc.mTwistAngle = 0.0f;
        gTwoBonesIKDesc.mPoleVector = Vector3::zAxis();
        gTwoBonesIKDesc.mMidAxis = Vector3::zAxis();
        gStickFigureRig.FindJointChain(twoBonesJointNames, 3, gTwoBonesIKDesc.mJointChain);

        // CREATE COMMAND LIST AND GRAPHICS/COMPUTE QUEUES
        //
        TFQueueDesc queueDesc = {};
        queueDesc.mType = TF_QUEUE_TYPE_GRAPHICS;
        queueDesc.mFlag = TF_QUEUE_FLAG_INIT_MICROPROFILE;
        initQueue(pRenderer, &queueDesc, &pGraphicsQueue);

        GpuCmdRingDesc cmdRingDesc = {};
        cmdRingDesc.pQueue = pGraphicsQueue;
        cmdRingDesc.mPoolCount = gDataBufferCount;
        cmdRingDesc.mCmdPerPoolCount = 1;
        cmdRingDesc.mAddSyncPrimitives = true;
        initGpuCmdRing(pRenderer, &cmdRingDesc, &gGraphicsCmdRing);

        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            initSemaphore(pRenderer, &pImageAcquiredSemaphore[i]);
        }

        // INITIALIZE RESOURCE/DEBUG SYSTEMS
        //
        initResourceLoaderInterface(pRenderer);

        TFRootSignatureDesc rootDesc = {};
        INIT_RS_DESC(rootDesc, "default.rootsig", "compute.rootsig");
        initRootSignature(pRenderer, &rootDesc);

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

        const uint32_t  numScripts = sizeof(gTestScripts) / sizeof(gTestScripts[0]);
        TFLuaScriptDesc scriptDescs[numScripts] = {};
        for (uint32_t i = 0; i < numScripts; ++i)
            scriptDescs[i].pScriptFileName = gTestScripts[i];
        luaDefineScripts(scriptDescs, numScripts);

        // Initialize micro profiler and its UI.
        TFProfilerDesc profiler = {};
        profiler.pRenderer = pRenderer;
        initProfiler(&profiler);

        gGpuProfileToken = initGpuProfiler(pRenderer, pGraphicsQueue, "Graphics");

        // INITIALIZE PIPILINE STATES
        //

        uint64_t         jointDataSize = gNumberOfJointPoints * sizeof(float);
        TFBufferLoadDesc jointVbDesc = {};
        jointVbDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_VERTEX_BUFFER;
        jointVbDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_GPU_ONLY;
        jointVbDesc.mDesc.mSize = jointDataSize;
        jointVbDesc.pData = pJointPoints;
        jointVbDesc.ppBuffer = &pJointVertexBuffer;
        addResource(&jointVbDesc, NULL);

        uint64_t         boneDataSize = gNumberOfBonePoints * sizeof(float);
        TFBufferLoadDesc boneVbDesc = {};
        boneVbDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_VERTEX_BUFFER;
        boneVbDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_GPU_ONLY;
        boneVbDesc.mDesc.mSize = boneDataSize;
        boneVbDesc.pData = pBonePoints;
        boneVbDesc.ppBuffer = &pBoneVertexBuffer;
        addResource(&boneVbDesc, NULL);

        uint64_t         cuboidDataSize = gNumberOfCuboidPoints * sizeof(float);
        TFBufferLoadDesc cuboidVbDesc = {};
        cuboidVbDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_VERTEX_BUFFER;
        cuboidVbDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_GPU_ONLY;
        cuboidVbDesc.mDesc.mSize = cuboidDataSize;
        cuboidVbDesc.pData = pCuboidPoints;
        cuboidVbDesc.ppBuffer = &pCuboidVertexBuffer;
        addResource(&cuboidVbDesc, NULL);

        uint64_t         cubesDataSize = gNumberOfCubes * sizeof(float);
        TFBufferLoadDesc cubeVbDesc = {};
        cubeVbDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_VERTEX_BUFFER;
        cubeVbDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_GPU_ONLY;
        cubeVbDesc.mDesc.mSize = cubesDataSize;
        cubeVbDesc.pData = pCubesPoints;
        cubeVbDesc.ppBuffer = &pCubesVertexBuffer;
        addResource(&cubeVbDesc, NULL);

        // Generate plane vertex buffer
        float planePoints[] = { -15.0f, 0.0f, -15.0f, 1.0f, 0.0f, 0.0f, -15.0f, 0.0f, 15.0f,  1.0f, 1.0f, 0.0f,
                                15.0f,  0.0f, 15.0f,  1.0f, 1.0f, 1.0f, 15.0f,  0.0f, 15.0f,  1.0f, 1.0f, 1.0f,
                                15.0f,  0.0f, -15.0f, 1.0f, 0.0f, 1.0f, -15.0f, 0.0f, -15.0f, 1.0f, 0.0f, 0.0f };

        uint64_t         planeDataSize = 6 * 6 * sizeof(float);
        TFBufferLoadDesc planeVbDesc = {};
        planeVbDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_VERTEX_BUFFER;
        planeVbDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_GPU_ONLY;
        planeVbDesc.mDesc.mSize = planeDataSize;
        planeVbDesc.pData = planePoints;
        planeVbDesc.ppBuffer = &pPlaneVertexBuffer;
        addResource(&planeVbDesc, NULL);

        TFBufferLoadDesc ubDesc = {};
        ubDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        ubDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
        ubDesc.mDesc.mSize = sizeof(UniformBlockPlane);
        ubDesc.mDesc.mFlags = TF_BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
        ubDesc.pData = NULL;
        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            ubDesc.ppBuffer = &pPlaneUniformBuffer[i];
            addResource(&ubDesc, NULL);
        }

        TFBufferLoadDesc ubDescCuboid = {};
        ubDescCuboid.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        ubDescCuboid.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
        ubDescCuboid.mDesc.mSize = sizeof(UniformBlock);
        ubDescCuboid.mDesc.mFlags = TF_BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
        ubDescCuboid.pData = NULL;
        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            ubDescCuboid.ppBuffer = &pCuboidUniformBuffer[i];
            addResource(&ubDescCuboid, NULL);
        }
        ubDesc.mDesc.mSize = sizeof(UniformSkeletonBlock);
        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            ubDesc.ppBuffer = &pTargetUniformBuffer[i];
            addResource(&ubDesc, NULL);
        }
        /************************************************************************/
        // SETUP ANIMATION STRUCTURES
        /************************************************************************/

        // SKELETON RENDERER
        //

        // Set up details for rendering the skeletons
        SkeletonRenderDesc skeletonRenderDesc = {};
        skeletonRenderDesc.mRenderer = pRenderer;
        skeletonRenderDesc.mFrameCount = gDataBufferCount;
        skeletonRenderDesc.mMaxSkeletonBatches = 512;
        skeletonRenderDesc.mJointVertexBuffer = pJointVertexBuffer;
        skeletonRenderDesc.mNumJointPoints = gNumberOfJointPoints;
        skeletonRenderDesc.mDrawBones = true;
        skeletonRenderDesc.mBoneVertexBuffer = pBoneVertexBuffer;
        skeletonRenderDesc.mNumBonePoints = gNumberOfBonePoints;
        skeletonRenderDesc.mBoneVertexStride = sizeof(float) * 8;
        skeletonRenderDesc.mJointVertexStride = sizeof(float) * 6;
        skeletonRenderDesc.mMaxAnimatedObjects = gGpuSettings.mMaxRigs;
        skeletonRenderDesc.mJointMeshType = QuadSphere;
        gSkeletonBatcher.Initialize(skeletonRenderDesc);

        // Add the rig to the list of skeletons to render
        for (size_t i = 0; i < gGpuSettings.mMaxRigs; i++)
        {
            gSkeletonBatcher.AddAnimatedObject(&gStickFigureAnimObject[i]);
        }

        SkeletonRenderDesc ozzSkeletonRenderDesc = {};
        ozzSkeletonRenderDesc.mRenderer = pRenderer;
        ozzSkeletonRenderDesc.mFrameCount = gDataBufferCount;
        ozzSkeletonRenderDesc.mMaxSkeletonBatches = 512;
        ozzSkeletonRenderDesc.mJointVertexBuffer = pCubesVertexBuffer;
        ozzSkeletonRenderDesc.mNumJointPoints = gNumberOfCubes;
        ozzSkeletonRenderDesc.mDrawBones = false; // Indicate that we do not wish to have bones between each joint
        ozzSkeletonRenderDesc.mBoneVertexStride = sizeof(float) * 6;
        ozzSkeletonRenderDesc.mJointVertexStride = sizeof(float) * 6;
        ozzSkeletonRenderDesc.mMaxAnimatedObjects = 1;
        ozzSkeletonRenderDesc.mJointMeshType = Cube;
        ozzSkeletonRenderDesc.mJointVertShaderName = "cube.vert";
        ozzSkeletonRenderDesc.mJointFragShaderName = "cube.frag";
        gOzzLogoSkeletonBatcher.Initialize(ozzSkeletonRenderDesc);
        gOzzLogoSkeletonBatcher.AddAnimatedObject(&gOzzLogoAnimObject);

        /************************************************************************/

        // SETUP THE MAIN CAMERA
        //
        TFCameraMotionParameters cmp{ 50.0f, 75.0f, 150.0f };
        vec3                     camPos{ -15.0f, 5.0f, 13.0f };
        vec3                     lookAt{ 0.0f, 0.0f, -1.5f };

        pCamera = initFpsCamera(camPos, lookAt);
        pCamera->setMotionParameters(cmp);

        // INITIALIZE THREAD SYSTEM
        //
        threadSystemInit(&gThreadSystem, &gThreadSystemInitDescDefault);

        // App Actions
        AddCustomInputBindings();

        waitForAllResourceLoads();

        // Need to free memory
        tf_free(pCubesPoints);
        tf_free(pJointPoints);
        tf_free(pBonePoints);
        tf_free(pCuboidPoints);
        initScreenshotCapturer(pRenderer, pGraphicsQueue, GetName());

        return true;
    }

    void Exit() override
    {
        exitScreenshotCapturer();
        threadSystemWaitIdle(gThreadSystem);

        exitCamera(pCamera);

        for (size_t i = 0; i < gGpuSettings.mMaxRigs; i++)
        {
            gStickFigureAnimObject[i].Exit();

            for (size_t j = 0; j < ANIMATIONCOUNT; j++)
            {
                gAnimations[j][i].Exit();
            }
        }
        gStickFigureRig.Exit();

        gOzzLogoAnimObject.Exit();
        gShatterAnimation.Exit();
        gOzzLogoRig.Exit();

        gStandClipMask.Exit();
        gWalkClipMask.Exit();
        gNeckCrackClipMask.Exit();

        gStandClip.Exit();
        gJogClip.Exit();
        gWalkClip.Exit();
        gRunClip.Exit();
        gNeckCrackClip.Exit();
        gShatterClip.Exit();

        exitProfiler();

        threadSystemExit(&gThreadSystem, &gThreadSystemExitDescDefault);

        exitUserInterface();

        exitFontSystem();

        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            removeResource(pPlaneUniformBuffer[i]);
            removeResource(pCuboidUniformBuffer[i]);
            removeResource(pTargetUniformBuffer[i]);
        }

        removeResource(pCubesVertexBuffer);
        removeResource(pCuboidVertexBuffer);
        removeResource(pJointVertexBuffer);
        removeResource(pBoneVertexBuffer);
        removeResource(pPlaneVertexBuffer);

        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            exitSemaphore(pRenderer, pImageAcquiredSemaphore[i]);
        }
        exitGpuCmdRing(pRenderer, &gGraphicsCmdRing);

        // Animation data
        gOzzLogoSkeletonBatcher.Exit();
        gSkeletonBatcher.Exit();
        exitRootSignature(pRenderer);
        exitResourceLoaderInterface(pRenderer);
        exitQueue(pRenderer, pGraphicsQueue);
        exitRenderer(pRenderer);
        exitGPUConfig();
        pRenderer = NULL;
    }

    bool Load(TFReloadDesc* pReloadDesc) override
    {
        UNREF_PARAM(pReloadDesc);

        loadProfilerUI(mSettings.mWidth, mSettings.mHeight);

        // Add the GUI Panels/Windows
        vec2 UIPosition = { mSettings.mWidth * 0.006f, mSettings.mHeight * 0.17f };
        vec2 UIPanelSize = { 650, 500 };
        gStandaloneAnimationsDesc.mStartPos = UIPosition;
        gStandaloneAnimationsDesc.mStartSize = UIPanelSize;
        gStandaloneAnimationsDesc.pWindowTitle = "Animations";
        gStandaloneAnimationsDesc.mFlags =
            TF_UI_WINDOW_TITLE | TF_UI_WINDOW_SCALABLE | TF_UI_WINDOW_MOVABLE | TF_UI_WINDOW_MINIMIZABLE | TF_UI_WINDOW_BORDER;

        UIPosition = { mSettings.mWidth * 0.15f, mSettings.mHeight * 0.17f };
        UIPanelSize = { 300, 200 };
        gAnimationControlsDesc[0].mStartPos = UIPosition;
        gAnimationControlsDesc[0].mStartSize = UIPanelSize;
        gAnimationControlsDesc[0].pWindowTitle = "Stand Animation";
        gAnimationControlsDesc[0].mFlags = TF_UI_WINDOW_TITLE | TF_UI_WINDOW_SCALABLE | TF_UI_WINDOW_MOVABLE | TF_UI_WINDOW_MINIMIZABLE |
                                           TF_UI_WINDOW_BORDER | TF_UI_WINDOW_INIT_HEIGHT_FIT;

        gAnimationControlsDesc[1] = gAnimationControlsDesc[0];
        gAnimationControlsDesc[1].pWindowTitle = "Blend Animation";

        gAnimationControlsDesc[2] = gAnimationControlsDesc[0];
        gAnimationControlsDesc[2].pWindowTitle = "PartialBlending Animation";

        gAnimationControlsDesc[3] = gAnimationControlsDesc[0];
        gAnimationControlsDesc[3].pWindowTitle = "AdditiveBlending Animation";

        setupAnimationSpecificLua();

        if (!addSwapChain())
            return false;

        if (!addDepthBuffer())
            return false;

        SkeletonBatcherLoadDesc skeletonLoad = {};
        skeletonLoad.mLoadType = pReloadDesc->mType;
        skeletonLoad.mColorFormat = pSwapChain->ppRenderTargets[0]->mFormat;
        skeletonLoad.mDepthFormat = pDepthBuffer->mFormat;
        skeletonLoad.mSampleCount = pSwapChain->ppRenderTargets[0]->mSampleCount;
        skeletonLoad.mSampleQuality = pSwapChain->ppRenderTargets[0]->mSampleQuality;

        gSkeletonBatcher.Load(&skeletonLoad);
        gOzzLogoSkeletonBatcher.Load(&skeletonLoad);

        addShaders();
        addDescriptorSets();

        addPipelines();

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
        fontLoad.mDepthFormat = pDepthBuffer->mFormat;
        fontLoad.mDepthCompareMode = TFCompareMode::TF_CMP_GEQUAL;
        fontLoad.mHeight = mSettings.mHeight;
        fontLoad.mWidth = mSettings.mWidth;
        loadFontSystem(&fontLoad);

        return true;
    }

    void Unload(TFReloadDesc* pReloadDesc) override
    {
        UNREF_PARAM(pReloadDesc);

        waitQueueIdle(pGraphicsQueue);

        unloadFontSystem();
        unloadUserInterface();

        gSkeletonBatcher.Unload();
        gOzzLogoSkeletonBatcher.Unload();

        removePipelines();

        removeSwapChain(pRenderer, pSwapChain);
        removeRenderTarget(pRenderer, pDepthBuffer);

        unloadProfilerUI();

        removeDescriptorSets();
        removeShaders();
    }

    void Update(float deltaTime) override
    {
        updateGui();

        if (!uiIsFocused())
        {
            pCamera->onMove({ inputGetValue(0, CUSTOM_MOVE_X), inputGetValue(0, CUSTOM_MOVE_Y) });
            pCamera->onRotate({ inputGetValue(0, CUSTOM_LOOK_X), inputGetValue(0, CUSTOM_LOOK_Y) });
            pCamera->onMoveY(inputGetValue(0, CUSTOM_MOVE_UP));
            if (inputGetValue(0, CUSTOM_RESET_VIEW))
            {
                pCamera->resetView();
            }
            if (inputGetValue(0, CUSTOM_TOGGLE_FULLSCREEN))
            {
                toggleFullscreen(pWindow);
            }
            if (inputGetValue(0, CUSTOM_DUMP_PROFILE))
            {
                dumpProfileData(GetName());
            }
            if (inputGetValue(0, CUSTOM_EXIT))
            {
                requestShutdown();
            }
        }
        /************************************************************************/
        // Scene Update
        /************************************************************************/

        // Update camera with time
        TFCameraMatrix viewMat = pCamera->getViewMatrix();

        const float    aspectInverse = (float)mSettings.mHeight / (float)mSettings.mWidth;
        const float    horizontal_fov = PI / 2.0f;
        TFCameraMatrix projMat = camMatPerspectiveReverseZ(horizontal_fov, aspectInverse, 0.1f, 1000.0f);
        TFCameraMatrix projViewMat = camMatMul(&projMat, &viewMat);

        vec3 lightPos = vec3(0.0f, 1000.0f, 0.0f);
        vec3 lightColor = vec3(1.0f, 1.0f, 1.0f);

        /************************************************************************/
        // Animation
        /************************************************************************/
        resetHiresTimer(&gAnimationUpdateTimer);

        // Update the animated objects and pose the rigs based on the animated object's updated values for this frame
        gSkeletonBatcher.SetActiveRigs(gNumRigs);

        // Setup aim target
        static float time = 0.0f;
        time += 2.0f * deltaTime;

        Vector3 stickPos = (gStickFigureAnimObject[0].mJointWorldMats[0])[3].getXYZ();
        gAimTarget = Point3(sinf(time * .5f), cosf(time * .25f), cosf(time) * .5f + .5f);

        gUniformDataTarget.mViewMatrix = viewMat.mMatrices[MONO_CAMERA_VIEW_INDEX];
        gUniformDataTarget.mProjectView = projViewMat;
        gUniformDataTarget.mLightPosition = Vector4(lightPos);
        gUniformDataTarget.mLightColor = Vector4(lightColor);
        gUniformDataTarget.mToWorldMat[0] = Matrix4(Matrix3::identity() * 0.25f, Vector3(stickPos + (Vector3)gAimTarget));
        gUniformDataTarget.mColor[0] = Vector4(1.0f, 0.0f, 0.0f, 1.0f);
        gUniformDataTarget.mJointColor = Vector4(1.0f, 0.0f, 0.0f, 1.0f);

        // Threading
        if (gEnableThreading)
        {
            if (gAutomateThreading)
            {
                struct ThreadSystemInfo info;
                threadSystemGetInfo(gThreadSystem, &info);
                gGrainSize = max(1U, gNumRigs / (unsigned int)info.threadCount);
            }

            gGrainSize = min(gGrainSize, gNumRigs);
            unsigned int taskCount = max(1U, gNumRigs / gGrainSize);

            // Submit taskCount number of jobs
            for (unsigned int i = 0; i < taskCount; i++)
            {
                gThreadData[i].mAnimatedObject = &gStickFigureAnimObject[gGrainSize * i];
                gThreadData[i].mDeltaTime = deltaTime;
                gThreadData[i].mNumberSystems = gGrainSize;
            }
            threadSystemAddTaskGroup(gThreadSystem, AnimatedObjectThreadedUpdate, taskCount, gThreadData);

            // If there is a remainder, submit another job to finish it
            unsigned int remainder = (uint32_t)max(0, (int32_t)gNumRigs - (int32_t)(taskCount * gGrainSize));
            if (remainder != 0)
            {
                gThreadData[taskCount].mAnimatedObject = &gStickFigureAnimObject[gGrainSize * taskCount];
                gThreadData[taskCount].mDeltaTime = deltaTime;
                gThreadData[taskCount].mNumberSystems = remainder;

                threadSystemAddTasks(gThreadSystem, AnimatedObjectThreadedUpdate, 1, 0, &gThreadData[taskCount]);
            }
        }
        else
        {
            for (unsigned int i = 0; i < gNumRigs; ++i)
            {
                if (!gStickFigureAnimObject[i].Update(deltaTime))
                    LOGF(eERROR, "Animation NOT Updating!");

                if (gUIData.mIKParams.mAim)
                {
                    if (!gStickFigureAnimObject[i].AimIK(&gAimIKDesc, gAimTarget))
                        LOGF(eINFO, "Aim IK failed!");
                }

                if (gUIData.mIKParams.mTwoBoneIK)
                {
                    Matrix4 mat = gStickFigureAnimObject[i].mJointModelMats[gTwoBonesIKDesc.mJointChain[2]];
                    Point3  twoBoneTarget = (Vector3)mat[3].getXYZ() + Vector3(0.0f, gUIData.mIKParams.mFoot, 0.0f);

                    if (!gStickFigureAnimObject[i].TwoBonesIK(&gTwoBonesIKDesc, twoBoneTarget))
                        LOGF(eINFO, "Two bone IK failed!");
                }

                // Pose rig
                if (!gUIData.mGeneralSettings.mShowBindPose)
                {
                    // Pose the rig based on the animated object's updated values
                    gStickFigureAnimObject[i].ComputePose(gStickFigureAnimObject[i].mRootTransform);
                }
                else
                {
                    // Ignore the updated values and pose in bind
                    gStickFigureAnimObject[i].ComputeBindPose(gStickFigureAnimObject[i].mRootTransform);
                }
            }

            // Record animation update time
            getHiresTimerUSec(&gAnimationUpdateTimer, true);
        }

        if (gUIData.mGeneralSettings.mDrawBakedPhysics)
        {
            // Update the animated object for this frame
            if (!gOzzLogoAnimObject.Update(deltaTime))
                LOGF(eINFO, "Animation NOT Updating!");
            gOzzLogoAnimObject.ComputePose(gOzzLogoAnimObject.mRootTransform);

            // Set the transform of the camera based on the updated world matrix of
            // the joint in the rig at index gCameraIndex
            if (gUIData.mGeneralSettings.mAnimatedCamera)
            {
                // Alter to view front of ozz logo
                mat4 cameraMat = gOzzLogoAnimObject.mJointWorldMats[gCameraIndex];
                vec4 cameraMatCol3 = cameraMat[3];
                vec3 viewPos = vec3(cameraMatCol3.x, cameraMatCol3.y, -cameraMatCol3.z);
                vec4 cameraMatCol2 = cameraMat[2];
                vec3 lookAt = vec3(-cameraMatCol2.x, -cameraMatCol2.y, cameraMatCol2.z) + viewPos;
                pCamera->moveTo(viewPos);
                pCamera->lookAt(lookAt);
            }
            else
            {
                pCamera->update(deltaTime);
            }

            gOzzLogoAnimObject.mJointWorldMats[gCameraIndex] = mat4::scale(vec3(0));

            gOzzLogoSkeletonBatcher.SetSharedUniforms(projViewMat, viewMat.mMatrices[MONO_CAMERA_VIEW_INDEX], lightPos, lightColor);
        }
        else
        {
            pCamera->update(deltaTime);
        }

        // Update uniforms that will be shared between all skeletons
        gSkeletonBatcher.SetSharedUniforms(projViewMat, viewMat.mMatrices[MONO_CAMERA_VIEW_INDEX], lightPos, lightColor);
        /************************************************************************/
        // Attached object
        /************************************************************************/
        gUniformDataCuboid.mProjectView = projViewMat;
        gUniformDataCuboid.mLightPosition = Vector4(lightPos);
        gUniformDataCuboid.mLightColor = Vector4(lightColor);

        // Set the transform of the attached object based on the updated world matrix of
        // the joint in the rig specified by the UI
        // * TODO: add UI to modify mJointIndex
        gCuboidTransformMat = gStickFigureAnimObject[0].mJointWorldMats[gUIData.mAttachedObject.mJointIndex];

        // Compute the offset translation based on the UI values
        mat4 offset = mat4::translation(
            vec3(gUIData.mAttachedObject.mOffset.x, gUIData.mAttachedObject.mOffset.y, gUIData.mAttachedObject.mOffset.z));

        gUniformDataCuboid.mToWorldMat[0] = gCuboidTransformMat * offset * gCuboidScaleMat;
        gUniformDataCuboid.mColor[0] = gCuboidColor;
        gUniformDataCuboid.mJointColor = gCuboidColor;

        /************************************************************************/
        // Plane
        /************************************************************************/
        gUniformDataPlane.mProjectView = projViewMat;
        gUniformDataPlane.mToWorldMat = mat4::identity();

        if (gEnableThreading)
        {
            threadSystemWaitIdle(gThreadSystem);

            // Record animation update time
            getHiresTimerUSec(&gAnimationUpdateTimer, true);
        }

        // Automated blending controls the clip weights. Update gui after all threads are finished.
        if (gCurrentAnimationIndex == ANIMATION_INDEX_BLEND && gUIData.mBlendingParams.mAutoSetBlendParams)
        {
            gUIData.mBlendingParams.mWalkClipWeight = gWalkClipController[0].mWeight;
            gUIData.mBlendingParams.mJogClipWeight = gJogClipController[0].mWeight;
            gUIData.mBlendingParams.mRunClipWeight = gRunClipController[0].mWeight;
        }
    }

    void Draw() override
    {
        if ((bool)pSwapChain->mEnableVsync != mSettings.mVSyncEnabled)
        {
            waitQueueIdle(pGraphicsQueue);
            ::toggleVSync(pRenderer, &pSwapChain);
        }

        uint32_t swapchainImageIndex;
        acquireNextImage(pRenderer, pSwapChain, pImageAcquiredSemaphore[mSettings.mFrameIdx], NULL, &swapchainImageIndex);

        // Stall if CPU is running "gDataBufferCount" frames ahead of GPU
        GpuCmdRingElement elem = getNextGpuCmdRingElement(&gGraphicsCmdRing, true, 1);
        TFFenceStatus     fenceStatus;
        getFenceStatus(pRenderer, elem.pFence, &fenceStatus);
        if (fenceStatus == TF_FENCE_STATUS_INCOMPLETE)
            waitForFences(pRenderer, 1, &elem.pFence);

        /************************************************************************/
        // Update uniform buffers
        /************************************************************************/
        gSkeletonBatcher.PreSetInstanceUniforms(mSettings.mFrameIdx);

        // Update all the instanced uniform data for each batch of joints and bones
        // Threading
        if (gEnableThreading)
        {
            unsigned int taskCount = max(1U, gNumRigs / gGrainSize);

            // Submit taskCount number of jobs
            for (unsigned int i = 0; i < taskCount; ++i)
            {
                gThreadSkeletonData[i].mFrameNumber = mSettings.mFrameIdx;
                gThreadSkeletonData[i].mNumberRigs = gGrainSize;
                gThreadSkeletonData[i].mOffset = i * gGrainSize;
            }
            threadSystemAddTaskGroup(gThreadSystem, SkeletonBatchUniformsThreaded, taskCount, gThreadSkeletonData);

            // If there is a remainder, submit another job to finish it
            unsigned int remainder = (uint32_t)max(0, (int32_t)gNumRigs - (int32_t)(taskCount * gGrainSize));
            if (remainder != 0)
            {
                gThreadSkeletonData[taskCount].mFrameNumber = mSettings.mFrameIdx;
                gThreadSkeletonData[taskCount].mNumberRigs = remainder;
                gThreadSkeletonData[taskCount].mOffset = taskCount * gGrainSize;

                threadSystemAddTasks(gThreadSystem, SkeletonBatchUniformsThreaded, 1, 0, &gThreadSkeletonData[taskCount]);
            }

            // Ensure all jobs are finished before proceeding
            threadSystemWaitIdle(gThreadSystem);
        }
        else
        {
            gSkeletonBatcher.SetPerInstanceUniforms(mSettings.mFrameIdx, gNumRigs);
        }

        if (gUIData.mGeneralSettings.mDrawBakedPhysics)
        {
            gOzzLogoSkeletonBatcher.PreSetInstanceUniforms(mSettings.mFrameIdx);
            gOzzLogoSkeletonBatcher.SetPerInstanceUniforms(mSettings.mFrameIdx);
        }

        TFBufferUpdateDesc planeViewProjCbv = { pPlaneUniformBuffer[mSettings.mFrameIdx] };
        beginUpdateResource(&planeViewProjCbv);
        memcpy(planeViewProjCbv.pMappedData, &gUniformDataPlane, sizeof(gUniformDataPlane));
        endUpdateResource(&planeViewProjCbv);
        TFBufferUpdateDesc cuboidViewProjCbv = { pCuboidUniformBuffer[mSettings.mFrameIdx] };
        beginUpdateResource(&cuboidViewProjCbv);
        memcpy(cuboidViewProjCbv.pMappedData, &gUniformDataCuboid, sizeof(gUniformDataCuboid));
        endUpdateResource(&cuboidViewProjCbv);
        TFBufferUpdateDesc targetViewProjCbv = { pTargetUniformBuffer[mSettings.mFrameIdx] };
        beginUpdateResource(&targetViewProjCbv);
        memcpy(targetViewProjCbv.pMappedData, &gUniformDataTarget, sizeof(gUniformDataTarget));
        endUpdateResource(&targetViewProjCbv);

        resetCmdPool(pRenderer, elem.pCmdPool);

        // Acquire the main render target from the swapchain
        TFRenderTarget* pRenderTarget = pSwapChain->ppRenderTargets[swapchainImageIndex];
        TFCmd*          cmd = elem.pCmds[0];
        beginCmd(cmd); // start recording commands

        // Start gpu frame profiler
        cmdBeginGpuFrameProfile(cmd, gGpuProfileToken);

        TFRenderTargetBarrier barriers[] = { { pRenderTarget, TF_RESOURCE_STATE_PRESENT, TF_RESOURCE_STATE_RENDER_TARGET } };
        cmdResourceBarrier(cmd, 0, NULL, 0, NULL, 1, barriers);

        // Bind and clear the render target
        TFBindRenderTargetsDesc bindRenderTargets = {};
        bindRenderTargets.mRenderTargetCount = 1;
        bindRenderTargets.mRenderTargets[0] = { pRenderTarget, TF_LOAD_ACTION_CLEAR };
        bindRenderTargets.mDepthStencil = { pDepthBuffer, TF_LOAD_ACTION_CLEAR };
        cmdBindRenderTargets(cmd, &bindRenderTargets);
        cmdSetViewport(cmd, 0.0f, 0.0f, (float)pRenderTarget->mWidth, (float)pRenderTarget->mHeight, 0.0f, 1.0f);
        cmdSetScissor(cmd, 0, 0, pRenderTarget->mWidth, pRenderTarget->mHeight);

        const uint32_t stride = sizeof(float) * 6;
        // Plane pass
        if (gUIData.mGeneralSettings.mDrawPlane)
        {
            cmdBeginGpuTimestampQuery(cmd, gGpuProfileToken, "Draw Plane");
            cmdBeginDebugMarker(cmd, 1, 0, 1, "Draw Plane");
            cmdBindPipeline(cmd, pPlaneDrawPipeline);
            cmdBindDescriptorSet(cmd, mSettings.mFrameIdx * 2 + 0, pDescriptorSet);
            cmdBindVertexBuffer(cmd, 1, &pPlaneVertexBuffer, &stride, NULL);
            cmdDraw(cmd, 6, 0);
            cmdEndDebugMarker(cmd);
            cmdEndGpuTimestampQuery(cmd, gGpuProfileToken);
        }

        // Baked physics pass
        if (gUIData.mGeneralSettings.mDrawBakedPhysics)
        {
            // Draw the Ozz Logo of the rig
            cmdBeginGpuTimestampQuery(cmd, gGpuProfileToken, "Draw Baked Physics");
            cmdBeginDebugMarker(cmd, 1, 0, 1, "Draw Baked Physics");
            gOzzLogoSkeletonBatcher.Draw(cmd, mSettings.mFrameIdx);

            TFFontDrawDesc drawDesc = {};
            drawDesc.pText = "Baked Physics";
            drawDesc.pFont = gFont;
            drawDesc.mFontColor = 0xffff0000;
            mat4 worldMat = mat4::translation(vec3(-12.5f, 3.0f, 0.5f));
            cmdDrawWorldSpaceText(cmd, &worldMat, &gUniformDataPlane.mProjectView, &drawDesc);

            cmdEndDebugMarker(cmd);
            cmdEndGpuTimestampQuery(cmd, gGpuProfileToken);
        }

        // Skeleton pass
        {
            cmdBeginGpuTimestampQuery(cmd, gGpuProfileToken, "Draw Skeletons");
            cmdBeginDebugMarker(cmd, 1, 0, 1, "Draw Skeletons");
            gSkeletonBatcher.Draw(cmd, mSettings.mFrameIdx);
            cmdEndDebugMarker(cmd);
            cmdEndGpuTimestampQuery(cmd, gGpuProfileToken);
        }

        // IK target pass
        {
            cmdBeginGpuTimestampQuery(cmd, gGpuProfileToken, "Draw IK Target");
            cmdBeginDebugMarker(cmd, 1, 0, 1, "Draw IK Target");
            cmdBindPipeline(cmd, gSkeletonBatcher.mJointPipeline);
            cmdBindVertexBuffer(cmd, 1, &pJointVertexBuffer, &stride, NULL);
            cmdBindDescriptorSet(cmd, mSettings.mFrameIdx, pTargetDescriptorSet);
            cmdDrawInstanced(cmd, gNumberOfJointPoints / 6, 0, 1, 0);
            cmdEndDebugMarker(cmd);
            cmdEndGpuTimestampQuery(cmd, gGpuProfileToken);
        }
        // Attached object pass
        if (gUIData.mGeneralSettings.mDrawAttachedObject)
        {
            // Draw the object attached to the rig
            cmdBeginGpuTimestampQuery(cmd, gGpuProfileToken, "Draw Cuboid");
            cmdBeginDebugMarker(cmd, 1, 0, 1, "Draw Cuboid");
            cmdBindPipeline(cmd, pCubePipeline);
            cmdBindDescriptorSet(cmd, mSettings.mFrameIdx * 2 + 1, pDescriptorSet);
            cmdBindVertexBuffer(cmd, 1, &pCuboidVertexBuffer, &stride, NULL);
            cmdDrawInstanced(cmd, gNumberOfCuboidPoints / 6, 0, 1, 0);
            cmdEndDebugMarker(cmd);
            cmdEndGpuTimestampQuery(cmd, gGpuProfileToken);
        }

        // UI pass
        {
            bindRenderTargets = {};
            bindRenderTargets.mRenderTargetCount = 1;
            bindRenderTargets.mRenderTargets[0] = { pRenderTarget, TF_LOAD_ACTION_LOAD };
            cmdBindRenderTargets(cmd, &bindRenderTargets);

            cmdBeginDebugMarker(cmd, 0, 1, 0, "Draw UI");
            {
                gFrameTimeDraw.mFontColor = 0xff00ffff;
                gFrameTimeDraw.mFontSize = 18.0f;
                gFrameTimeDraw.pFont = gFont;

                float2 txtSize = cmdDrawCpuProfile(cmd, float2(8.0f, 15.0f), &gFrameTimeDraw);

                snprintf(gAnimationUpdateText, 64, "Animation Update %f ms", getHiresTimerUSecAverage(&gAnimationUpdateTimer) / 1000.0f);

                // Disable UI rendering when taking screenshots
                if (getIsProfilerDrawing())
                {
                    gFrameTimeDraw.pText = gAnimationUpdateText;
                    cmdDrawText(cmd, float2(8.f, txtSize.y + 50.0f), &gFrameTimeDraw);
                }

                cmdDrawGpuProfile(cmd, float2(8.f, txtSize.y + 75.0f), gGpuProfileToken, &gFrameTimeDraw);

                uiCmdDrawUserInterface(cmd, pSwapChain, pRenderTarget, gGpuProfileToken);
            }
            cmdEndDebugMarker(cmd);
            cmdBindRenderTargets(cmd, NULL);
        }

        // PRESENT THE GRPAHICS QUEUE
        //
        barriers[0] = { pRenderTarget, TF_RESOURCE_STATE_RENDER_TARGET, TF_RESOURCE_STATE_PRESENT };
        cmdResourceBarrier(cmd, 0, NULL, 0, NULL, 1, barriers);
        cmdEndGpuFrameProfile(cmd, gGpuProfileToken);
        endCmd(cmd);

        FlushResourceUpdateDesc flushUpdateDesc = {};
        flushUpdateDesc.mNodeIndex = 0;
        flushResourceUpdates(&flushUpdateDesc);
        TFSemaphore* waitSemaphores[2] = { flushUpdateDesc.pOutSubmittedSemaphore, pImageAcquiredSemaphore[mSettings.mFrameIdx] };

        TFQueueSubmitDesc submitDesc = {};
        submitDesc.mCmdCount = 1;
        submitDesc.mSignalSemaphoreCount = 1;
        submitDesc.mWaitSemaphoreCount = TF_ARRAY_COUNT(waitSemaphores);
        submitDesc.ppCmds = &cmd;
        submitDesc.ppSignalSemaphores = &elem.pSemaphore;
        submitDesc.ppWaitSemaphores = waitSemaphores;
        submitDesc.pSignalFence = elem.pFence;
        queueSubmit(pGraphicsQueue, &submitDesc);

        TFQueuePresentDesc presentDesc = {};
        presentDesc.mIndex = (uint8_t)swapchainImageIndex;
        presentDesc.mWaitSemaphoreCount = 1;
        presentDesc.ppWaitSemaphores = &elem.pSemaphore;
        presentDesc.pSwapChain = pSwapChain;
        presentDesc.mSubmitDone = true;
        queuePresent(pGraphicsQueue, &presentDesc);
        flipProfiler();
    }

    const char* GetName() override { return "21_Animations"; }

private:
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
        swapChainDesc.mColorClearValue = { { 0.39f, 0.41f, 0.37f, 1.0f } };
        swapChainDesc.mEnableVsync = mSettings.mVSyncEnabled;
        swapChainDesc.mFlags = TF_SWAP_CHAIN_CREATION_FLAG_ENABLE_2D_VR_LAYER;
        swapChainDesc.mVR.m2DLayer = gVR2DLayer;

        ::addSwapChain(pRenderer, &swapChainDesc, &pSwapChain);

        return pSwapChain != NULL;
    }

    void addDescriptorSets()
    {
        // Descriptor indices: 0 -> plane, 1 -> cuboid, target descriptor set -> IK target

        TFDescriptorSetDesc setDesc = SRT_SET_DESC(SrtData, PerDraw, gDataBufferCount * 2, 0);
        addDescriptorSet(pRenderer, &setDesc, &pDescriptorSet);

        TFDescriptorSetDesc targetDesc = SRT_SET_DESC(SrtAnimationData, PerDraw, gDataBufferCount, 0);
        addDescriptorSet(pRenderer, &targetDesc, &pTargetDescriptorSet);
    }

    void removeDescriptorSets()
    {
        removeDescriptorSet(pRenderer, pTargetDescriptorSet);
        removeDescriptorSet(pRenderer, pDescriptorSet);
    }

    void addShaders()
    {
        TFShaderLoadDesc planeShader = {};
        planeShader.mVert.pFileName = "plane.vert";
        planeShader.mFrag.pFileName = "plane.frag";

        TFShaderLoadDesc cubeShader = {};
        cubeShader.mVert.pFileName = "cube.vert";
        cubeShader.mFrag.pFileName = "cube.frag";

        addShader(pRenderer, &planeShader, &pPlaneDrawShader);
        addShader(pRenderer, &cubeShader, &pCubeShader);
    }

    void removeShaders()
    {
        removeShader(pRenderer, pCubeShader);
        removeShader(pRenderer, pPlaneDrawShader);
    }

    void addPipelines()
    {
        // Layout and pipeline for skeleton draw
        TFVertexLayout vertexLayout = {};
        vertexLayout.mBindingCount = 1;
        vertexLayout.mAttribCount = 2;
        vertexLayout.mAttribs[0].mSemantic = TF_SEMANTIC_POSITION;
        vertexLayout.mAttribs[0].mFormat = TinyImageFormat_R32G32B32_SFLOAT;
        vertexLayout.mAttribs[0].mBinding = 0;
        vertexLayout.mAttribs[0].mLocation = 0;
        vertexLayout.mAttribs[0].mOffset = 0;
        vertexLayout.mAttribs[1].mSemantic = TF_SEMANTIC_NORMAL;
        vertexLayout.mAttribs[1].mFormat = TinyImageFormat_R32G32B32_SFLOAT;
        vertexLayout.mAttribs[1].mBinding = 0;
        vertexLayout.mAttribs[1].mLocation = 1;
        vertexLayout.mAttribs[1].mOffset = 3 * sizeof(float);

        TFRasterizerStateDesc rasterizerStateDesc = {};
        rasterizerStateDesc.mCullMode = TF_CULL_MODE_NONE;

        TFRasterizerStateDesc skeletonRasterizerStateDesc = {};
        skeletonRasterizerStateDesc.mCullMode = TF_CULL_MODE_FRONT;

        TFDepthStateDesc depthStateDesc = {};
        depthStateDesc.mDepthTest = true;
        depthStateDesc.mDepthWrite = true;
        depthStateDesc.mDepthFunc = TF_CMP_GEQUAL;

        TFPipelineDesc desc = {};
        desc.mType = TF_PIPELINE_TYPE_GRAPHICS;
        PIPELINE_LAYOUT_DESC(desc, NULL, NULL, NULL, SRT_LAYOUT_DESC(SrtData, PerDraw));
        TFGraphicsPipelineDesc& pipelineSettings = desc.mGraphicsDesc;
        pipelineSettings.mPrimitiveTopo = TF_PRIMITIVE_TOPO_TRI_LIST;
        pipelineSettings.mRenderTargetCount = 1;
        pipelineSettings.pDepthState = &depthStateDesc;
        pipelineSettings.pColorFormats = &pSwapChain->ppRenderTargets[0]->mFormat;
        pipelineSettings.mSampleCount = pSwapChain->ppRenderTargets[0]->mSampleCount;
        pipelineSettings.mSampleQuality = pSwapChain->ppRenderTargets[0]->mSampleQuality;
        pipelineSettings.mDepthStencilFormat = pDepthBuffer->mFormat;
        pipelineSettings.pShaderProgram = pCubeShader;
        pipelineSettings.pVertexLayout = &vertexLayout;
        pipelineSettings.pRasterizerState = &skeletonRasterizerStateDesc;
        addPipeline(pRenderer, &desc, &pCubePipeline);

        // Layout and pipeline for plane draw
        vertexLayout = {};
        vertexLayout.mBindingCount = 1;
        vertexLayout.mAttribCount = 2;
        vertexLayout.mAttribs[0].mSemantic = TF_SEMANTIC_POSITION;
        vertexLayout.mAttribs[0].mFormat = TinyImageFormat_R32G32B32A32_SFLOAT;
        vertexLayout.mAttribs[0].mBinding = 0;
        vertexLayout.mAttribs[0].mLocation = 0;
        vertexLayout.mAttribs[0].mOffset = 0;
        vertexLayout.mAttribs[1].mSemantic = TF_SEMANTIC_TEXCOORD0;
        vertexLayout.mAttribs[1].mFormat = TinyImageFormat_R32G32_SFLOAT;
        vertexLayout.mAttribs[1].mBinding = 0;
        vertexLayout.mAttribs[1].mLocation = 1;
        vertexLayout.mAttribs[1].mOffset = 4 * sizeof(float);

        pipelineSettings.pDepthState = NULL;
        pipelineSettings.pRasterizerState = &rasterizerStateDesc;
        pipelineSettings.pShaderProgram = pPlaneDrawShader;
        addPipeline(pRenderer, &desc, &pPlaneDrawPipeline);
    }

    void removePipelines()
    {
        removePipeline(pRenderer, pPlaneDrawPipeline);
        removePipeline(pRenderer, pCubePipeline);
    }

    void updateDescriptorSets()
    {
        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            TFDescriptorData params[1] = {};
            params[0].mIndex = SRT_RES_IDX(SrtData, PerDraw, gUniformBlock);
            params[0].ppBuffers = &pPlaneUniformBuffer[i];
            updateDescriptorSet(pRenderer, i * 2 + 0, pDescriptorSet, 1, params);
            params[0].mIndex = SRT_RES_IDX(SrtData, PerDraw, gUniformBlock);
            params[0].ppBuffers = &pCuboidUniformBuffer[i];
            updateDescriptorSet(pRenderer, i * 2 + 1, pDescriptorSet, 1, params);
            params[0].mIndex = SRT_RES_IDX(SrtData, PerDraw, gUniformBlock);
            params[0].ppBuffers = &pTargetUniformBuffer[i];
            updateDescriptorSet(pRenderer, i, pTargetDescriptorSet, 1, params);
        }
    }

    bool addDepthBuffer()
    {
        // Add depth buffer
        TF_ESRAM_BEGIN_ALLOC(pRenderer, "Depth", 0);

        TFRenderTargetDesc depthRT = {};
        depthRT.mArraySize = 1;
        depthRT.mClearValue.depth = 0.0f;
        depthRT.mClearValue.stencil = 0;
        depthRT.mDepth = 1;
        depthRT.mFormat = TinyImageFormat_D32_SFLOAT;
        depthRT.mStartState = TF_RESOURCE_STATE_DEPTH_WRITE;
        depthRT.mHeight = mSettings.mHeight;
        depthRT.mSampleCount = TF_SAMPLE_COUNT_1;
        depthRT.mSampleQuality = 0;
        depthRT.mWidth = mSettings.mWidth;
        depthRT.mFlags = TF_TEXTURE_CREATION_FLAG_ESRAM | TF_TEXTURE_CREATION_FLAG_ON_TILE | TF_TEXTURE_CREATION_FLAG_VR_MULTIVIEW;
        addRenderTarget(pRenderer, &depthRT, &pDepthBuffer);

        TF_ESRAM_END_ALLOC(pRenderer);

        return pDepthBuffer != NULL;
    }

    static void SkeletonBatchUniformsThreaded(void* pData, uint64_t)
    {
        ThreadSkeletonData* data = (ThreadSkeletonData*)pData;
        gSkeletonBatcher.SetPerInstanceUniforms(data->mFrameNumber, data->mNumberRigs, data->mOffset);
    }

    // Threaded animated object update call
    static void AnimatedObjectThreadedUpdate(void* pData, uint64_t)
    {
        // Unpack data
        ThreadData* data = (ThreadData*)pData;

        AnimatedObject* animSystem = data->mAnimatedObject;
        float           deltaTime = data->mDeltaTime;
        unsigned int    numberSystems = data->mNumberSystems;

        // Update the systems
        for (unsigned int i = 0; i < numberSystems; ++i)
        {
            if (!(animSystem[i].Update(deltaTime)))
                LOGF(eERROR, "Animation NOT Updating!");

            if (gUIData.mIKParams.mAim)
            {
                if (!animSystem[i].AimIK(&gAimIKDesc, gAimTarget))
                    LOGF(eINFO, "Aim IK failed!");
            }

            if (gUIData.mIKParams.mTwoBoneIK)
            {
                Matrix4 mat = animSystem[i].mJointModelMats[gTwoBonesIKDesc.mJointChain[2]];
                Point3  twoBoneTarget = (Vector3)mat[3].getXYZ() + Vector3(0.0f, gUIData.mIKParams.mFoot, 0.0f);

                if (!animSystem[i].TwoBonesIK(&gTwoBonesIKDesc, twoBoneTarget))
                    LOGF(eINFO, "Two bone IK failed!");
            }

            // Pose rig
            if (!gUIData.mGeneralSettings.mShowBindPose)
            {
                // Pose the rig based on the animated object's updated values
                animSystem[i].ComputePose(animSystem[i].mRootTransform);
            }
            else
            {
                // Ignore the updated values and pose in bind
                animSystem[i].ComputeBindPose(animSystem[i].mRootTransform);
            }
        }
    }
};

DEFINE_APPLICATION_MAIN(Animations)
