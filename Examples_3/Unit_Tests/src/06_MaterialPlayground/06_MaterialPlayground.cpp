/*
 *
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
 * The Forge - MATERIALS UNIT TEST
 *
 * The purpose of this demo is to show the material workflow of The-Forge,
 * featuring PBR materials and environment lighting.
 *
 *********************************************************************************************************/

// Interfaces
#include "../../../../Common_3/Application/Interfaces/IApp.h"
#include "../../../../Common_3/Application/Interfaces/ICamera.h"
#include "../../../../Common_3/Application/Interfaces/IFont.h"
#include "../../../../Common_3/Application/Interfaces/IProfiler.h"
#include "../../../../Common_3/Application/Interfaces/IScreenshot.h"
#include "../../../../Common_3/Application/Interfaces/IUI.h"
#include "../../../../Common_3/Game/Interfaces/IScripting.h"
#include "../../../../Common_3/Graphics/Interfaces/IGraphics.h"
#include "../../../../Common_3/OS/Interfaces/IInput.h"
#include "../../../../Common_3/Resources/ResourceLoader/Interfaces/IResourceLoader.h"
#include "../../../../Common_3/Utilities/Interfaces/IFileSystem.h"
#include "../../../../Common_3/Utilities/Interfaces/ILog.h"
#include "../../../../Common_3/Utilities/Interfaces/ITime.h"

#include "../../../../Common_3/Utilities/RingBuffer.h"

// Math
#include "../../../../Common_3/Utilities/Interfaces/IMath.h"

// input
//  Animations
#undef min
#undef max
#include "../../../../Common_3/Resources/AnimationSystem/Animation/AnimatedObject.h"
#include "../../../../Common_3/Resources/AnimationSystem/Animation/Animation.h"
#include "../../../../Common_3/Resources/AnimationSystem/Animation/Clip.h"
#include "../../../../Common_3/Resources/AnimationSystem/Animation/ClipController.h"
#include "../../../../Common_3/Resources/AnimationSystem/Animation/Rig.h"
#include "../../../../Common_3/Resources/AnimationSystem/Animation/SkeletonBatcher.h"
#include "../../../../Common_3/Utilities/Threading/ThreadSystem.h"
#include "Shaders/Shared.h"

#include "../../../../Common_3/Utilities/Interfaces/IMemory.h" // Must be the last include in a cpp file

// fsl
#define SHORT_CUT_RESOLVE_DEPTH
#include "../../../../Common_3/Graphics/FSL/defaults.h"
#include "./Shaders/FSL/Capsules.srt.h"
#include "./Shaders/FSL/Global.srt.h"
#include "./Shaders/FSL/Hair.srt.h"
#include "./Shaders/FSL/PBR.srt.h"

#define HAIR_DEV_UI                  false
#define MAX_FILENAME_LENGTH          128

// when set, all the textures are a 2x2 white image
// and the BRDF shader won't sample those textures
#define SKIP_LOADING_TEXTURES        0

#define SPECULAR_CONFIG_BUFFER_COUNT 8

//--------------------------------------------------------------------------------------------
// MATERIAL DEFINITIONS
//--------------------------------------------------------------------------------------------
typedef enum EMaterialTypes
{
    MATERIAL_METAL = 0,
    MATERIAL_WOOD,
    MATERIAL_HAIR,
    MATERIAL_BRDF_COUNT = MATERIAL_HAIR,
    MATERIAL_COUNT
} MaterialType;

typedef enum RenderMode
{
    RENDER_MODE_SHADED = 0,
    RENDER_MODE_ALBEDO,
    RENDER_MODE_NORMALS,
    RENDER_MODE_ROUGHNESS,
    RENDER_MODE_METALLIC,
    RENDER_MODE_AO,

    RENDER_MODE_COUNT
} RenderMode;

typedef enum HairType
{
    HAIR_TYPE_PONYTAIL,
    HAIR_TYPE_FEMALE_1,
    HAIR_TYPE_FEMALE_2,
    HAIR_TYPE_FEMALE_3,
    HAIR_TYPE_FEMALE_6,
    HAIR_TYPE_COUNT
} HairType;

typedef enum MeshResource
{
    MESH_MAT_BALL,
    MESH_CUBE,
    MESH_CAPSULE,
    MESH_COUNT,
} MeshResource;

typedef enum MaterialTexture
{
    MATERIAL_TEXTURE_ALBEDO,
    MATERIAL_TEXTURE_NORMAL,
    MATERIAL_TEXTURE_METALLIC,
    MATERIAL_TEXTURE_ROUGHNESS,
    MATERIAL_TEXTURE_OCCLUSION,
    MATERIAL_TEXUTRE_VMF,
    MATERIAL_TEXTURE_COUNT
} MaterialTexture;

typedef enum HairColor
{
    HAIR_COLOR_BROWN,
    HAIR_COLOR_BLONDE,
    HAIR_COLOR_BLACK,
    HAIR_COLOR_RED,
    HAIR_COLOR_COUNT
} HairColor;

// testing a material made of raisins...
#define RAISINS 0

static const char* metalEnumNames[] = { "Aluminum", "Scratched Gold",
                                        "Copper",   "Tiled Metal",
#if RAISINS
                                        "Raisins",
#else
                                        "Old Iron",
#endif
                                        "Bronze",   NULL };
static const char* woodEnumNames[] = { "Wooden Plank 05", "Wooden Plank 06", "Wood #03", "Wood #08", "Wood #16", "Wood #18", NULL };

static const uint32_t MATERIAL_INSTANCE_COUNT = sizeof(metalEnumNames) / sizeof(metalEnumNames[0]) - 1;

const char* gHeadAttachmentJointName = "Bip01 HeadNub";
const char* gLeftShoulderJointName = "LeftShoulder";
const char* gRightShoulderJointName = "RightShoulder";

static TFFont* gFont = NULL;

// #NOTE: Two sets of resources (one in flight and one being used on CPU)
const uint32_t gDataBufferCount = 2;

ProfileToken gHairGpuProfileToken;
ProfileToken gMetalWoodGpuProfileToken;
ProfileToken gCurrentGpuProfileToken;
//--------------------------------------------------------------------------------------------
// STRUCT DEFINITIONS
//--------------------------------------------------------------------------------------------
struct UniformCamData
{
    TFCameraMatrix mProjectView;
    TFCameraMatrix mInvProjectView;

    vec3 mCamPos;

    float fAmbientLightIntensity = 0.0f;

    int   bUseEnvMap = 0;
    float fEnvironmentLightIntensity = 0.5f;
    float fAOIntensity = 0.01f;
    int   iRenderMode = 0;
    float fNormalMapIntensity = 1.0f;
};

struct UniformCamDataShadow
{
    mat4 mProjectView;
    mat4 mInvProjectView;

    vec3 mCamPos;
};

struct UniformObjData
{
    mat4   mWorldMat;
    float3 mAlbedo = float3(1, 1, 1);
    float  mRoughness = 0.04f;

    float2 tiling = float2(1, 1);
    float  mMetallic = 0.0f;

    int textureConfig = 0;
};

struct UniformPrecomputeSkySpecularData
{
    uint  mipSize;
    float roughness;
};
enum ETextureConfigFlags
{
    // specifies which textures are used for the material
    DIFFUSE = (1 << 0),
    NORMAL = (1 << 1),
    METALLIC = (1 << 2),
    ROUGHNESS = (1 << 3),
    AO = (1 << 4),
    VMF = (1 << 5),

    TEXTURE_CONFIG_FLAGS_ALL = DIFFUSE | NORMAL | METALLIC | ROUGHNESS | AO | VMF,
    TEXTURE_CONFIG_FLAGS_NONE = 0,

    // specifies which diffuse reflection model to use
    OREN_NAYAR = (1 << 6), // Lambert otherwise, we just check if this flag is set for now

    NUM_TEXTURE_CONFIG_FLAGS = 7
};

enum EDiffuseReflectionModels
{
    LAMBERT_REFLECTION = 0,
    OREN_NAYAR_REFLECTION,

    DIFFUSE_REFLECTION_MODEL_COUNT
};

struct PointLight
{
    float3 mPosition;
    float  mRadius;
    float3 mColor;
    float  mIntensity;
};

struct DirectionalLight
{
    float3 mDirection;
    int    mShadowMap;
    float3 mColor;
    float  mIntensity;
    float  mShadowRange;
    float2 _pad;
    int    mShadowMapDimensions;
    mat4   mViewProj;
};

struct UniformDataPointLights
{
    PointLight mPointLights[MAX_NUM_POINT_LIGHTS] = {};
    uint       mNumPointLights = 0;
};

struct UniformDataDirectionalLights
{
    DirectionalLight mDirectionalLights[MAX_NUM_DIRECTIONAL_LIGHTS] = {};
    uint             mNumDirectionalLights = 0;
};

struct Capsule
{
    float3 mCenter0;
    float  mRadius0;
    float3 mCenter1;
    float  mRadius1;
};

struct NamedCapsule
{
    const char* mName = NULL;
    Capsule     mCapsule = {};
    int         mAttachedBone = -1;
};

struct Transform
{
    vec3  mPosition;
    vec3  mOrientation;
    float mScale;
};

struct NamedTransform
{
    Transform   mTransform = {};
    const char* mName = NULL;
    int         mAttachedBone = -1;
};

struct UniformDataHairGlobal
{
    float4 mViewport;
    float4 mGravity;
    float4 mWind;
    float  mTimeStep;
};

struct UniformDataHairShading
{
    mat4  mTransform;
    uint  mRootColor;
    uint  mStrandColor;
    float mColorBias;
    float mKDiffuse;
    float mKSpecular1;
    float mKExponent1;
    float mKSpecular2;
    float mKExponent2;
    float mStrandRadius;
    float mStrandSpacing;
    uint  mNumVerticesPerStrand;
};

struct UniformDataHairSimulation
{
    mat4 mTransform;
    Quat mQuatRotation;
#if HAIR_MAX_CAPSULE_COUNT > 0
    Capsule mCapsules[HAIR_MAX_CAPSULE_COUNT]; // Hair local space capsules
    uint    mCapsuleCount;
#endif
    float mScale;
    uint  mNumStrandsPerThreadGroup;
    uint  mNumFollowHairsPerGuideHair;
    uint  mNumVerticesPerStrand;
    float mDamping;
    float mGlobalConstraintStiffness;
    float mGlobalConstraintRange;
    float mShockPropagationStrength;
    float mShockPropagationAccelerationThreshold;
    float mLocalStiffness;
    uint  mLocalConstraintIterations;
    uint  mLengthConstraintIterations;
    float mTipSeperationFactor;
};

struct HairBuffer
{
    TFGeometry*               pGeom = NULL;
    TFGeometryData*           pGeomData = NULL;
    TFBuffer*                 pBufferHairVertexPositions = NULL;
    TFBuffer*                 pBufferHairVertexTangents = NULL;
    TFBuffer*                 pBufferTriangleIndices = NULL;
    TFBuffer*                 pBufferHairRestLenghts = NULL;
    TFBuffer*                 pBufferHairGlobalRotations = NULL;
    TFBuffer*                 pBufferHairRefsInLocalFrame = NULL;
    TFBuffer*                 pBufferFollowHairRootOffsets = NULL;
    TFBuffer*                 pBufferHairThicknessCoefficients = NULL;
    TFBuffer*                 pBufferHairSimulationVertexPositions[3] = { NULL };
    TFBuffer*                 pUniformBufferHairShading[gDataBufferCount] = { NULL };
    TFBuffer*                 pUniformBufferHairSimulation[gDataBufferCount] = { NULL };
    UniformDataHairShading    mUniformDataHairShading = {};
    UniformDataHairSimulation mUniformDataHairSimulation = {};
    uint                      mIndexCountHair = 0;
    uint                      mTotalVertexCount = 0;
    uint                      mNumGuideStrands = 0;
    float                     mStrandRadius = 0.0f;
    float                     mStrandSpacing = 0.0f;
    uint                      mTransform = 0; // Index into gTransforms
    bool                      mDisableRootColor = false;
#if HAIR_MAX_CAPSULE_COUNT > 0
    uint mCapsules[HAIR_MAX_CAPSULE_COUNT] = {}; // World space capsules
#endif
};

struct GlobalHairParameters
{
    float4 mGravity; // Gravity direction * magnitude
    float4 mWind;    // Wind direction * magnitude
};

struct HairShadingParameters
{
    float4 mRootColor;   // Hair color near the root
    float4 mStrandColor; // Hair color away from the root
    float  mKDiffuse;    // Diffuse light contribution
    float  mKSpecular1;  // Specular 1 light contribution
    float  mKExponent1;  // Specular 1 exponent
    float  mKSpecular2;  // Specular 2 light contribution
    float  mKExponent2;  // Specular 2 exponent
};

struct HairSectionShadingParameters
{
    float mColorBias;        // Bias between root and strand color
    float mStrandRadius;     // Strand width
    float mStrandSpacing;    // Strand density
    bool  mDisableRootColor; // Stops the root color from being used.
};

struct HairSimulationParameters
{
    float mDamping;                               // Dampens hair velocity over time
    float mGlobalConstraintStiffness;             // Force keeping the hair in its original position
    float mGlobalConstraintRange;                 // Range to apply global constraint to
    float mShockPropagationStrength;              // Force propagating sudden changes to the rest of the strand
    float mShockPropagationAccelerationThreshold; // Threshold at which to start shock propagation
    float mLocalConstraintStiffness;              // Force keeping strands in the rest shape
    uint  mLocalConstraintIterations;             // Number of local constraint iterations
    uint  mLengthConstraintIterations;            // Number of length constraint iterations
    float mTipSeperationFactor;                   // Separates follow hairs from their guide hair
#if HAIR_MAX_CAPSULE_COUNT > 0
    uint mCapsuleCount;                     // Number of collision capsules
    uint mCapsules[HAIR_MAX_CAPSULE_COUNT]; // Index into gCapsules for collision capsules the hair will collide with
#endif
};

struct HairTypeInfo
{
    bool mInView;
    bool mPreWarm;
};

//--------------------------------------------------------------------------------------------
// RENDERING PIPELINE DATA
//--------------------------------------------------------------------------------------------
TFRenderer*  pRenderer = NULL;
TFQueue*     pGraphicsQueue = NULL;
GpuCmdRing   gGraphicsCmdRing = {};
TFSwapChain* pSwapChain = NULL;
TFSemaphore* pImageAcquiredSemaphore[gDataBufferCount] = { NULL };

TFVertexLayout gVertexLayoutDefault = {};
//--------------------------------------------------------------------------------------------
// THE FORGE OBJECTS
//--------------------------------------------------------------------------------------------
TFICamera*     pCamera = NULL;
TFICamera*     pLightView = NULL;
TFFontDrawDesc gFrameTimeDraw;  // = TextDrawDesc(0, 0xff00ff00, 18);
TFFontDrawDesc gErrMsgDrawDesc; // = TextDrawDesc(0, 0xff0000ee, 18);
TFUIWindowDesc gGuiWindowMainDesc;
TFUIWindowDesc gGuiWindowHairSimDesc;
TFUIWindowDesc gGuiWindowMaterialDesc;
LuaManager     gLuaManager;

static ThreadSystem gThreadSystem = NULL;

//--------------------------------------------------------------------------------------------
// SAMPLERS
//--------------------------------------------------------------------------------------------
TFSampler* pSamplerBilinearRepeat = NULL;
TFSampler* pSamplerBilinearClampToEdge = NULL;
TFSampler* pSamplerPointRepeat = NULL;
TFSampler* pSamplerPointClampToEdge = NULL;

//--------------------------------------------------------------------------------------------
// MATERIALS
//--------------------------------------------------------------------------------------------

typedef enum SceneMaterials
{

    // Metal
    SCENE_MATERIAL_ALUMINUM,
    SCENE_MATERIAL_SCRATCHED_GOLD,
    SCENE_MATERIAL_COPPER,
    SCENE_MATERIAL_TILED_METAL,
    SCENE_MATERIAL_OLD_IRON,
    SCENE_MATERIAL_BRONZE,

    // Wood
    SCENE_MATERIAL_WOODEN_PLANKS_05,
    SCENE_MATERIAL_WOODEN_PLANKS_06,
    SCENE_MATERIAL_WOOD_03,
    SCENE_MATERIAL_WOOD_08,
    SCENE_MATERIAL_WOOD_16,
    SCENE_MATERIAL_WOOD_18,

    // Ground
    SCENE_MATERIAL_SNOW_WHITE_TILES,

    // Label Plane
    SCENE_MATERIAL_NAME_PLATE,

    // Helpers
    SCENE_MATERIAL_TOTAL_COUNT,

    SCENE_MATERIAL_METAL_COUNT = SCENE_MATERIAL_WOODEN_PLANKS_05,
    SCENE_MATERIAL_WOOD_COUNT = SCENE_MATERIAL_SNOW_WHITE_TILES - SCENE_MATERIAL_WOODEN_PLANKS_05,
    SCENE_MATERIAL_MATBALL_COUNT = SCENE_MATERIAL_SNOW_WHITE_TILES,
    SCENE_MATERIAL_FLOOR = SCENE_MATERIAL_SNOW_WHITE_TILES,

} SceneMaterials;

// All materials used for the balls (metal and wood)
// To select the specific material we use the indexes in the enum above, which are the same as the order in which materials are defined in
// the material file.
const char* gBallMaterialsFileName = "ball.fmat";
const char* gGroundAndNameplateMaterialsFileName = "ground_and_nameplate.fmat";

TFMaterial* pBallMaterials = NULL;
TFMaterial* pGroundAndNameplateMaterials =
    NULL; // These could be stored in independent materials, putting them together as another example of how Materials work

TFDescriptorSet* ppSceneMaterialDescriptorSets[SCENE_MATERIAL_TOTAL_COUNT] = { NULL };
TFPipeline*      ppSceneMaterialPipelines[SCENE_MATERIAL_TOTAL_COUNT] = { NULL };

//--------------------------------------------------------------------------------------------
// SHADERS
//--------------------------------------------------------------------------------------------
TFShader* pShaderSkybox = NULL;
TFShader* pShaderShadowPass = NULL;

TFShader* pShaderHairClear = NULL;
TFShader* pShaderHairDepthPeeling = NULL;
TFShader* pShaderHairDepthResolve = NULL;
TFShader* pShaderHairFillColors = NULL;
TFShader* pShaderHairResolveColor = NULL;
TFShader* pShaderHairIntegrate = NULL;
TFShader* pShaderHairShockPropagation = NULL;
TFShader* pShaderHairLocalConstraints = NULL;
TFShader* pShaderHairLengthConstraints = NULL;
TFShader* pShaderHairUpdateFollowHairs = NULL;
TFShader* pShaderHairPreWarm = NULL;
TFShader* pShaderShowCapsules = NULL;
TFShader* pShaderHairShadow = NULL;

//--------------------------------------------------------------------------------------------
// DESCRIPTOR SET
//--------------------------------------------------------------------------------------------
TFDescriptorSet* pDescriptorSetShadowPerDraw = { NULL };
TFDescriptorSet* pDescriptorSetPersistent = { NULL };
TFDescriptorSet* pDescriptorSetPerFrame = { NULL };

TFDescriptorSet* pDescriptorSetHairShadow = { NULL };
TFDescriptorSet* pDescriptorSetHairPerDraw = { NULL };
TFDescriptorSet* pDescriptorSetHairPerBatch = { NULL };
TFDescriptorSet* pDescriptorSetCapsuleData = { NULL };

uint32_t    gHairDynamicDescriptorSetCount = 0;
//--------------------------------------------------------------------------------------------
// PIPELINES
//--------------------------------------------------------------------------------------------
TFPipeline* pPipelineSkybox = NULL;
TFPipeline* pPipelineShadowPass = NULL;

TFPipeline* pPipelineHairClear = NULL;
TFPipeline* pPipelineHairDepthPeeling = NULL;
TFPipeline* pPipelineHairDepthResolve = NULL;
TFPipeline* pPipelineHairFillColors = NULL;
TFPipeline* pPipelineHairColorResolve = NULL;
TFPipeline* pPipelineHairIntegrate = NULL;
TFPipeline* pPipelineHairShockPropagation = NULL;
TFPipeline* pPipelineHairLocalConstraints = NULL;
TFPipeline* pPipelineHairLengthConstraints = NULL;
TFPipeline* pPipelineHairUpdateFollowHairs = NULL;
TFPipeline* pPipelineHairPreWarm = NULL;
TFPipeline* pPipelineShowCapsules = NULL;
TFPipeline* pPipelineHairShadow = NULL;

//--------------------------------------------------------------------------------------------
// RENDER TARGETS
//--------------------------------------------------------------------------------------------
TFRenderTarget* pRenderTargetShadowMap = NULL;
TFRenderTarget* pRenderTargetDepth = NULL;
TFRenderTarget* pRenderTargetDepthPeeling = NULL;
TFRenderTarget* pRenderTargetFillColors = NULL;
TFRenderTarget* pRenderTargetHairShadows[HAIR_TYPE_COUNT][MAX_NUM_DIRECTIONAL_LIGHTS] = { { NULL } };
TFTexture*      pTextureHairDepth = NULL;
TFBuffer*       pBufferHairDepth = NULL;

//--------------------------------------------------------------------------------------------
// VERTEX BUFFERS
//--------------------------------------------------------------------------------------------
TFBuffer*   pVertexBufferSkybox = NULL;
uint32_t    gHairCount = 0;
HairBuffer* gHair = NULL;
TFBuffer*   pVertexBufferSkeletonJoint = NULL;
int         gVertexCountSkeletonJoint = 0;
TFBuffer*   pVertexBufferSkeletonBone = NULL;
int         gVertexCountSkeletonBone = 0;

//--------------------------------------------------------------------------------------------
// INDEX BUFFERS
//--------------------------------------------------------------------------------------------

//--------------------------------------------------------------------------------------------
// MESHES
//--------------------------------------------------------------------------------------------
static uint32_t gMeshCount = 0;

static TFGeometry** gMeshes = NULL;

//--------------------------------------------------------------------------------------------
// UNIFORM BUFFERS
//--------------------------------------------------------------------------------------------
TFBuffer*  pUniformBufferCamera[gDataBufferCount] = { NULL };
TFBuffer*  pUniformBufferCameraShadowPass[gDataBufferCount] = { NULL };
TFBuffer*  pUniformBufferCameraSkybox[gDataBufferCount] = { NULL };
TFBuffer*  pUniformBufferCameraHairShadows[gDataBufferCount][HAIR_TYPE_COUNT][MAX_NUM_DIRECTIONAL_LIGHTS] = {};
TFBuffer*  pUniformBufferGroundPlane = NULL;
TFBuffer*  pUniformBufferMatBall[gDataBufferCount][MATERIAL_INSTANCE_COUNT];
TFBuffer*  pUniformBufferNamePlates[MATERIAL_INSTANCE_COUNT];
TFBuffer*  pUniformBufferPointLights = NULL;
TFBuffer*  pUniformBufferDirectionalLights[gDataBufferCount] = { NULL };
TFBuffer*  pUniformBufferHairGlobal = NULL;
TFBuffer*  pUniformBufferSpecularConfig[SPECULAR_CONFIG_BUFFER_COUNT] = { NULL };
TFBuffer*  pUniformBufferCapsuleData[gDataBufferCount][HAIR_TYPE_COUNT][HAIR_MAX_CAPSULE_COUNT] = {};
//--------------------------------------------------------------------------------------------
// TEXTURES
//--------------------------------------------------------------------------------------------
TFTexture* pTextureSkybox = NULL;
TFTexture* pTextureBRDFIntegrationMap = NULL;

TFTexture* pTextureIrradianceMap = NULL;
TFTexture* pTextureSpecularMap = NULL;

//--------------------------------------------------------------------------------------------
// UNIFORM DATA
//--------------------------------------------------------------------------------------------
UniformCamData               gUniformDataCamera;
UniformCamData               gUniformDataCameraSkybox;
UniformCamDataShadow         gUniformDataCameraHairShadows[HAIR_TYPE_COUNT][MAX_NUM_DIRECTIONAL_LIGHTS];
UniformDataPointLights       gUniformDataPointLights;
UniformObjData               gUniformDataMatBall[MATERIAL_INSTANCE_COUNT];
UniformDataDirectionalLights gUniformDataDirectionalLights;
UniformDataHairGlobal        gUniformDataHairGlobal;

//--------------------------------------------------------------------------------------------
// SKELETAL ANIMATION
//--------------------------------------------------------------------------------------------

Clip            gAnimationClipNeckCrack;
Clip            gAnimationClipStand;
ClipController  gAnimationClipControllerNeckCrack[HAIR_TYPE_COUNT];
ClipController  gAnimationClipControllerStand[HAIR_TYPE_COUNT];
Animation       gAnimation[HAIR_TYPE_COUNT];
Rig             gAnimationRig;
AnimatedObject  gAnimatedObject[HAIR_TYPE_COUNT];
SkeletonBatcher gSkeletonBatcher;

#define gCapsuleCount (HAIR_MAX_CAPSULE_COUNT < 3 ? 1 : 3)
NamedCapsule gCapsules[gCapsuleCount] = {};
#define gTransformCount 1
NamedTransform gTransforms[1];
// Stores the capsule transformed by the bone matrix
Capsule        gFinalCapsules[HAIR_TYPE_COUNT][gCapsuleCount] = {};

//--------------------------------------------------------------------------------------------
// UI & OTHER
//--------------------------------------------------------------------------------------------
bool         gShowCapsules = false;
uint         gHairType = 0;
uint32_t     gHairTypeIndicesCount[HAIR_TYPE_COUNT] = { 0 };
uint*        gHairTypeIndices[HAIR_TYPE_COUNT] = { 0 };
HairTypeInfo gHairTypeInfo[HAIR_TYPE_COUNT];
bool         gEnvironmentLighting = true;
bool         gDrawSkybox = true;
uint32_t     gMaterialType = MATERIAL_METAL;
uint32_t     gDiffuseReflectionModel = LAMBERT_REFLECTION;
bool         gbLuaScriptingSystemLoadedSuccessfully = false;
bool         gbAnimateCamera = false;

EDiffuseReflectionModels gMaterialLightingModelMap[MATERIAL_COUNT] = {};

TFFontDrawDesc gMaterialPropDraw; // = TextDrawDesc(0, 0xffaaaaaa, 32);

// light
const int gShadowMapDimensions = 2048;
float4    gDirectionalLightColor = float4(1.0f, 0.456f, 0.177f, 1.0f);
float     gDirectionalLightIntensity = 10.0f;
float3    gDirectionalLightDirection = float3(69.0f, 6.0f, -26.0f);
float     gAmbientLightIntensity = 0.01f;
float     gEnvironmentLightingIntensity = 0.35f;

// material
uint32_t gRenderMode = 0;
bool     gOverrideRoughnessTextures = false;
float    gRoughnessOverride = 0.04f;
bool     gDisableNormalMaps = false;
bool     gEnableVMFMaps = false;
float    gNormalMapIntensity = 0.56f;
bool     gDisableAOMaps = false;
float    gAOIntensity = 1.00f;

uint32_t         gHairColor = HAIR_COLOR_BROWN;
uint32_t         gLastHairColor = gHairColor;
bool             gFirstHairSimulationFrame = true;
TFGPUPresetLevel gGPUPresetLevel;

bool gSupportLinearSamplingBRDFTextures = true;
#ifdef METAL
bool gSupportTextureAtomics = false;
#else
bool gSupportTextureAtomics = true;
#endif

TFCameraMatrix gTextProjView;
mat4           gTextWorldMats[MATERIAL_INSTANCE_COUNT] = {};

// VR 2D layer transform (positioned at -1 along the Z axis, default rotation, default scale)
TFVR2DLayerDesc gVR2DLayer{ { 0.0f, 0.0f, -1.0f }, { 0.0f, 0.0f, 0.0f, 1.0f }, 1.0f };

void ReloadScriptButtonCallback(void* pUserData)
{
    UNREF_PARAM(pUserData);
    gLuaManager.ReloadUpdatableScript();
}

#define OUT_OF_POSITION float3(100000.0f, 100000.0f, 100000.0f)

// Finds the vertex in the direction of the normal
vec3 AABBGetVertex(const TFAABB& b, const vec3& normal)
{
    vec3 p = b.min;
    for (int i = 0; i < 3; ++i)
    {
        if (normal[i] >= 0.0f)
            p[i] = b.max[i];
    }
    return p;
}

bool AABBInFrustum(const TFAABB& b, vec4 frustumPlanes[6])
{
    for (int i = 0; i < 6; i++)
    {
        float distance = dot(AABBGetVertex(b, frustumPlanes[i].getXYZ()), frustumPlanes[i].getXYZ()) + frustumPlanes[i].w;
        if (distance < 0.0f)
            return false;
    }
    return true;
}

MaterialType gCurrentMaterialType;
uint         gCurrentHairType = 0;

// Dropdown structs
const char* gMaterialTypeNames[MATERIAL_COUNT] = {
    "Metals",
    "Wood",
    "Hair",
};
const char* gDiffuseReflectionNames[] = { "Lambert", "Oren-Nayar" };
const char* gRenderModeNames[] = { "Shaded", "Albedo", "Normals", "Roughness", "Metallic", "AO" };
const char* gHairNames[HAIR_TYPE_COUNT] = { "Ponytail", "Female hair 1", "Female hair 2", "Female hair 3", "Female hair 6" };
const char* gHairColorNames[HAIR_COLOR_COUNT] = { "Brown", "Blonde", "Black", "Red" };

const char* gTestScripts[] = { "Test_Metal.lua", "Test_Wood.lua", "Test_Hair.lua" };

uint32_t gCurrentScriptIndex = 0;

void RunScript(void* pUserData)
{
    UNREF_PARAM(pUserData);
    TFLuaScriptDesc runDesc = {};
    runDesc.pScriptFileName = gTestScripts[gCurrentScriptIndex];
    luaQueueScriptToRun(&runDesc);
}
class MaterialPlayground: public IApp
{
public:
    MaterialPlayground() //-V832
    {
#ifdef TARGET_IOS
        mSettings.mContentScaleFactor = 1.f;
#endif
    }

    struct StagingData
    {
        struct TextureData
        {
            const char* pFileName;
            bool        mIsSrgb;
        };

        uint32_t mModelCount;
        char**   mModelList;
        float*   pJointPoints;
        float*   pBonePoints;
        ~StagingData()
        {
            for (uint32_t i = 0; i < mModelCount; ++i)
                tf_free(mModelList[i]);
            mModelCount = 0;
            tf_free(mModelList);
            mModelList = NULL;

            tf_free(pJointPoints);
            tf_free(pBonePoints);
        }
    };
    StagingData* pStagingData = NULL;

    struct ThreadTaskInfo
    {
        MaterialPlayground* unitTest;
        uint32_t            index;
    };

    bool Init() override
    {
        // Vulkan can be debugged in RenderDoc
        //	extern TFRendererApi gSelectedRendererApi;
        //	gSelectedRendererApi = TF_RENDERER_API_VULKAN;

        TFRendererDesc settings;
        memset(&settings, 0, sizeof(settings));
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

        gGPUPresetLevel = pRenderer->pGpu->mGpuVendorPreset.mPresetLevel;
        mSettings.mFrameMaxCount = gDataBufferCount;

        // Some texture format are not well covered on android devices (R32G32_SFLOAT, R32G32B32A32_SFLOAT)
        gSupportLinearSamplingBRDFTextures =
            (pRenderer->pGpu->mFormatCaps[TinyImageFormat_R32G32_SFLOAT] & TF_FORMAT_CAP_LINEAR_FILTER) &&
            (pRenderer->pGpu->mFormatCaps[TinyImageFormat_R32G32B32A32_SFLOAT] & TF_FORMAT_CAP_LINEAR_FILTER);

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

        bool threadSystemInitialized = threadSystemInit(&gThreadSystem, &gThreadSystemInitDescDefault);
        ASSERT(threadSystemInitialized);

        // INITIALIZE CAMERA & INPUT
        //
        TFCameraMotionParameters camParameters{ 100.0f, 150.0f, 300.0f };
        vec3                     camPos{ -0.21f, 12.2564745f, 59.3652649f };
        vec3                     lookAt{ 0, 0, 0 };

        gDirectionalLightDirection = normalize(gDirectionalLightDirection);
        pCamera = initFpsCamera(camPos, lookAt);
        pLightView = initGuiCamera(gDirectionalLightDirection, vec3(0, 0, 0));
        pCamera->setMotionParameters(camParameters);

        // INITIALIZE SCRIPTING
        //
        luaDestroyCurrentManager();

        gLuaManager.Init();
        TFICamera* cameraLocalPtr = pCamera;
        gLuaManager.SetFunction("GetCameraPosition",
                                [cameraLocalPtr](ILuaStateWrap* state) -> int
                                {
                                    vec3 pos = cameraLocalPtr->getViewPosition();
                                    state->PushResultNumber(pos.x);
                                    state->PushResultNumber(pos.y);
                                    state->PushResultNumber(pos.z);
                                    return 3; // return amount of arguments
                                });
        gLuaManager.SetFunction("SetCameraPosition",
                                [cameraLocalPtr](ILuaStateWrap* state) -> int
                                {
                                    float x = (float)state->GetNumberArg(1); // in Lua indexing starts from 1!
                                    float y = (float)state->GetNumberArg(2);
                                    float z = (float)state->GetNumberArg(3);
                                    cameraLocalPtr->moveTo(vec3(x, y, z));
                                    return 0; // return amount of arguments
                                });
        gLuaManager.SetFunction("CameraLookAtFromEye",
                                [cameraLocalPtr](ILuaStateWrap* state) -> int
                                {
                                    float px = (float)state->GetNumberArg(1); // in Lua indexing starts from 1!
                                    float py = (float)state->GetNumberArg(2);
                                    float pz = (float)state->GetNumberArg(3);
                                    float lx = (float)state->GetNumberArg(4);
                                    float ly = (float)state->GetNumberArg(5);
                                    float lz = (float)state->GetNumberArg(6);
                                    cameraLocalPtr->moveTo(vec3(px, py, pz));
                                    cameraLocalPtr->lookAt(vec3(lx, ly, lz));
                                    return 0; // return amount of arguments
                                });
        gLuaManager.SetFunction("ResetCamera",
                                [cameraLocalPtr](ILuaStateWrap* state) -> int
                                {
                                    UNREF_PARAM(state);
                                    // Same as the default value.
                                    cameraLocalPtr->moveTo(vec3(-0.21f, 12.2564745f, 59.3652649f));
                                    cameraLocalPtr->lookAt(vec3(0, 0, 0));
                                    return 0;
                                });
        gLuaManager.SetFunction("LookAtWorldOrigin",
                                [cameraLocalPtr](ILuaStateWrap* state) -> int
                                {
                                    UNREF_PARAM(state);
                                    cameraLocalPtr->lookAt(vec3(0, 0, 0));
                                    return 0; // return amount of arguments
                                });
        gLuaManager.SetFunction("GetIsCameraAnimated",
                                [](ILuaStateWrap* state) -> int
                                {
                                    state->PushResultInteger(gbAnimateCamera ? 1 : 0);
                                    return 1; // return amount of arguments
                                });
        gbLuaScriptingSystemLoadedSuccessfully = gLuaManager.SetUpdatableScript("updateCamera.lua", "Update", "Exit");

        // SET MATERIAL LIGHTING MODELS
        //
        gMaterialLightingModelMap[MATERIAL_METAL] = LAMBERT_REFLECTION;
        gMaterialLightingModelMap[MATERIAL_WOOD] = OREN_NAYAR_REFLECTION;
        // hair := custom shader. we still assign LAMBERT_REFLECTION to avoid branching logic when querying this map.
        gMaterialLightingModelMap[MATERIAL_HAIR] = LAMBERT_REFLECTION;

        // ... add more as new materials are introduced

        initAnimations();

        // INITIALIZE RESOURCE SYSTEMS
        //

        TFResourceLoaderDesc resourceLoaderDesc = gDefaultResourceLoaderDesc;
        resourceLoaderDesc.mUseMaterials = true;
        initResourceLoaderInterface(pRenderer, &resourceLoaderDesc);

        TFRootSignatureDesc rootDesc = {};
        INIT_RS_DESC(rootDesc, "default.rootsig", "compute.rootsig");
        initRootSignature(pRenderer, &rootDesc);

        pStagingData = tf_new(StagingData);

        // CREATE RENDERING RESOURCES
        //
        ComputePBRMaps();

        LoadModels();

        addSamplers();

        addUniformBuffers();

        addResources();

        // Create skeleton batcher
        SkeletonRenderDesc skeletonRenderDesc = {};
        skeletonRenderDesc.mRenderer = pRenderer;
        skeletonRenderDesc.mFrameCount = gDataBufferCount;
        skeletonRenderDesc.mMaxSkeletonBatches = 512;
        skeletonRenderDesc.mJointVertexBuffer = pVertexBufferSkeletonJoint;
        skeletonRenderDesc.mNumJointPoints = gVertexCountSkeletonJoint;
        skeletonRenderDesc.mDrawBones = true;
        skeletonRenderDesc.mBoneVertexBuffer = pVertexBufferSkeletonBone;
        skeletonRenderDesc.mNumBonePoints = gVertexCountSkeletonBone;
        skeletonRenderDesc.mBoneVertexStride = sizeof(float) * 8;
        skeletonRenderDesc.mJointVertexStride = sizeof(float) * 6;
        skeletonRenderDesc.mMaxAnimatedObjects = HAIR_TYPE_COUNT;
        skeletonRenderDesc.mJointMeshType = QuadSphere;
        gSkeletonBatcher.Initialize(skeletonRenderDesc);

        // Load skeleton batcher rigs
        for (uint hairType = 0; hairType < HAIR_TYPE_COUNT; ++hairType)
        {
            gSkeletonBatcher.AddAnimatedObject(&gAnimatedObject[hairType]);
        }

        threadSystemWaitIdle(gThreadSystem);
        waitForAllResourceLoads();

        /* both buffers for file names are singular allocations */
        tf_delete(pStagingData);

        InitializeUniformBuffers();

        luaAssignCustomManager(&gLuaManager);

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

        gMetalWoodGpuProfileToken = initGpuProfiler(pRenderer, pGraphicsQueue, "Graphics");
        gHairGpuProfileToken = initGpuProfiler(pRenderer, pGraphicsQueue, "Graphics");
        gCurrentGpuProfileToken = gMetalWoodGpuProfileToken;

        AddCustomInputBindings();
        initScreenshotCapturer(pRenderer, pGraphicsQueue, GetName());

        return true;
    }

    void Exit() override
    {
        exitScreenshotCapturer();
        exitAnimations();

        gLuaManager.Exit();

        threadSystemExit(&gThreadSystem, &gThreadSystemExitDescDefault);
        exitCamera(pCamera);
        exitCamera(pLightView);

        for (uint32_t i = 0; i < HAIR_TYPE_COUNT; ++i)
        {
            tf_free(gHairTypeIndices[i]);
            gHairTypeIndices[i] = NULL;
        }

        gFirstHairSimulationFrame = true;
        for (uint32_t i = 0; i < HAIR_TYPE_COUNT; ++i)
            gHairTypeInfo[i] = {};
        gHairDynamicDescriptorSetCount = 0;

        exitGpuProfiler(gHairGpuProfileToken);
        exitGpuProfiler(gMetalWoodGpuProfileToken);

        exitProfiler();

        removeUniformBuffers();

        // Destroy skeleton batcher
        gSkeletonBatcher.Exit();

        removeResources();
        removeModels();
        removePBRMaps();

        removeSamplers();

        exitUserInterface();

        removeFont(gFont);
        exitFontSystem();

        // Remove commands and command pool&
        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            exitSemaphore(pRenderer, pImageAcquiredSemaphore[i]);
        }
        exitGpuCmdRing(pRenderer, &gGraphicsCmdRing);

        exitQueue(pRenderer, pGraphicsQueue);
        exitRootSignature(pRenderer);
        // Remove resource loader and renderer
        exitResourceLoaderInterface(pRenderer);

        exitRenderer(pRenderer);
        exitGPUConfig();

        pRenderer = NULL;
    }

    bool Load(TFReloadDesc* pReloadDesc) override
    {
        UNREF_PARAM(pReloadDesc);

        addShaders();
        addDescriptorSets();

        loadProfilerUI(mSettings.mWidth, mSettings.mHeight);

        gGuiWindowMainDesc = {};
        gGuiWindowMainDesc.mStartPos = vec2(mSettings.mWidth * 0.01f, mSettings.mHeight * 0.2f);
        gGuiWindowMainDesc.mStartSize = vec2(450, 600);
        gGuiWindowMainDesc.pWindowTitle = GetName();
        gGuiWindowMainDesc.mFlags =
            TF_UI_WINDOW_TITLE | TF_UI_WINDOW_SCALABLE | TF_UI_WINDOW_MOVABLE | TF_UI_WINDOW_MINIMIZABLE | TF_UI_WINDOW_BORDER;

        gGuiWindowMaterialDesc = {};
        gGuiWindowMaterialDesc.mStartPos = vec2((float)mSettings.mWidth - 500.0f, 150.0f);
        gGuiWindowMaterialDesc.mStartSize = vec2(450, 600);
        gGuiWindowMaterialDesc.pWindowTitle = "Material Properties";
        gGuiWindowMaterialDesc.mFlags =
            TF_UI_WINDOW_TITLE | TF_UI_WINDOW_SCALABLE | TF_UI_WINDOW_MOVABLE | TF_UI_WINDOW_MINIMIZABLE | TF_UI_WINDOW_BORDER;

        gGuiWindowHairSimDesc = {};
        gGuiWindowHairSimDesc.mStartPos = vec2((float)mSettings.mWidth - 1000.0f, 150.0f);
        gGuiWindowHairSimDesc.mStartSize = vec2(450, 600);
        gGuiWindowHairSimDesc.pWindowTitle = "Hair simulation";
        gGuiWindowHairSimDesc.mFlags =
            TF_UI_WINDOW_TITLE | TF_UI_WINDOW_SCALABLE | TF_UI_WINDOW_MOVABLE | TF_UI_WINDOW_MINIMIZABLE | TF_UI_WINDOW_BORDER;

        luaRegisterGui();

        if (!addSwapChain())
            return false;

        addRenderTargets();

        addSceneMaterials();

        updateSceneMaterialDescriptorSets();
        addPipelines();

        waitForAllResourceLoads();
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
        fontLoad.mDepthFormat = pRenderTargetDepth->mFormat;
        fontLoad.mDepthCompareMode = TFCompareMode::TF_CMP_GEQUAL;
        fontLoad.mHeight = mSettings.mHeight;
        fontLoad.mWidth = mSettings.mWidth;
        loadFontSystem(&fontLoad);

        SkeletonBatcherLoadDesc skeletonLoad = {};
        skeletonLoad.mLoadType = pReloadDesc->mType;
        skeletonLoad.mColorFormat = pSwapChain->ppRenderTargets[0]->mFormat;
        skeletonLoad.mDepthFormat = pRenderTargetDepth->mFormat;
        skeletonLoad.mSampleCount = pSwapChain->ppRenderTargets[0]->mSampleCount;
        skeletonLoad.mSampleQuality = pSwapChain->ppRenderTargets[0]->mSampleQuality;

        gSkeletonBatcher.Load(&skeletonLoad);

        waitForAllResourceLoads();
        return true;
    }

    void Unload(TFReloadDesc* pReloadDesc) override
    {
        UNREF_PARAM(pReloadDesc);

        waitQueueIdle(pGraphicsQueue);

        gSkeletonBatcher.Unload();
        unloadUserInterface();
        unloadFontSystem();

        removeSceneMaterials();

        removePipelines();

        removeSwapChain(pRenderer, pSwapChain);
        removeRenderTargets();

        unloadProfilerUI();

        removeDescriptorSets();
        removeShaders();
    }

    void Update(float deltaTime) override
    {
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

        // UPDATE UI & CAMERA
        //
        updateDynamicUI();

        if (gbLuaScriptingSystemLoadedSuccessfully)
        {
            gLuaManager.Update(deltaTime);
        }

        pCamera->update(deltaTime);

        // Calculate matrices
        TFCameraMatrix viewMat = pCamera->getViewMatrix();
        const float    aspectInverse = (float)mSettings.mHeight / (float)mSettings.mWidth;
        const float    horizontal_fov = PI / 3.0f;
        TFCameraMatrix projMat = camMatPerspectiveReverseZ(horizontal_fov, aspectInverse, 0.1f, 1000.0f);

        // UPDATE UNIFORM BUFFERS
        //
        // Cameras
        gTextProjView = camMatMul(&projMat, &viewMat);
        gUniformDataCamera.mProjectView = camMatMul(&projMat, &viewMat);
        gUniformDataCamera.mInvProjectView = camMatInverse(&gUniformDataCamera.mProjectView);
        gUniformDataCamera.mCamPos = pCamera->getViewPosition();
        gUniformDataCamera.fAmbientLightIntensity = gAmbientLightIntensity;
        gUniformDataCamera.bUseEnvMap = gEnvironmentLighting;
        gUniformDataCamera.fAOIntensity = gAOIntensity;
        gUniformDataCamera.iRenderMode = gRenderMode;
        gUniformDataCamera.fNormalMapIntensity = gNormalMapIntensity;
        gUniformDataCamera.fEnvironmentLightIntensity = gEnvironmentLightingIntensity;

        vec4 frustumPlanes[6];
        camMatExtractFrustumClipPlanes(&gUniformDataCamera.mProjectView, &frustumPlanes[0], &frustumPlanes[1], &frustumPlanes[2],
                                       &frustumPlanes[3], &frustumPlanes[4], &frustumPlanes[5], true);

        viewMat = camMatSetTranslation(&viewMat, vec3(0));
        gUniformDataCameraSkybox = gUniformDataCamera;
        gUniformDataCameraSkybox.mProjectView = camMatMul(&projMat, &viewMat);

        // Ensure we have the correct light view in case the position changes (right now it can change through the UI)
        pLightView->moveTo((normalize(gDirectionalLightDirection) * 100.0f));
        pLightView->lookAt(vec3(0.f));
        viewMat = pLightView->getViewMatrix();

        // Lights
        gUniformDataDirectionalLights.mDirectionalLights[0].mDirection = (normalize((gDirectionalLightDirection)));
        gUniformDataDirectionalLights.mDirectionalLights[0].mShadowMap = 0;
        gUniformDataDirectionalLights.mDirectionalLights[0].mIntensity = gDirectionalLightIntensity;
        gUniformDataDirectionalLights.mDirectionalLights[0].mColor = gDirectionalLightColor.getXYZ();
        gUniformDataDirectionalLights.mDirectionalLights[0].mViewProj =
            projMat.mMatrices[MONO_CAMERA_VIEW_INDEX] * viewMat.mMatrices[MONO_CAMERA_VIEW_INDEX];
        gUniformDataDirectionalLights.mDirectionalLights[0].mShadowMapDimensions = gShadowMapDimensions;
        gUniformDataDirectionalLights.mNumDirectionalLights = 1;

        gUniformDataPointLights.mNumPointLights = 0; // short out point lights for now

        // Update the texture config (position and all other variables are
        // set during initialization and they dont change during Update()).
        //
        for (uint32_t i = 0; i < MATERIAL_INSTANCE_COUNT; ++i)
        {
            UniformObjData& objUniform = gUniformDataMatBall[i];

            // Add the Oren-Nayar diffuse model to the texture config.
            objUniform.textureConfig = ETextureConfigFlags::TEXTURE_CONFIG_FLAGS_ALL;
            if (gDiffuseReflectionModel == OREN_NAYAR_REFLECTION)
                objUniform.textureConfig |= ETextureConfigFlags::OREN_NAYAR;

            // Update material properties
            if (gOverrideRoughnessTextures)
            {
                objUniform.textureConfig = objUniform.textureConfig & ~ETextureConfigFlags::ROUGHNESS;
                objUniform.mRoughness = gRoughnessOverride;
            }
            if (gDisableNormalMaps)
            {
                objUniform.textureConfig = objUniform.textureConfig & ~ETextureConfigFlags::NORMAL;
            }
            if (gDisableAOMaps)
            {
                objUniform.textureConfig = objUniform.textureConfig & ~ETextureConfigFlags::AO;
            }
            if (!gEnableVMFMaps)
            {
                objUniform.textureConfig = objUniform.textureConfig & ~ETextureConfigFlags::VMF;
            }
            if (SKIP_LOADING_TEXTURES != 0)
            {
                objUniform.textureConfig = ETextureConfigFlags::TEXTURE_CONFIG_FLAGS_NONE;
            }
        }

        if (gMaterialType == MATERIAL_HAIR)
        {
            if (gHairColor != gLastHairColor)
            {
                for (size_t i = 0; i < gHairCount; ++i)
                    SetHairColor(&gHair[i], (HairColor)gHairColor);
                gLastHairColor = gHairColor;
            }

            if (gUniformDataDirectionalLights.mNumDirectionalLights > 0)
                gSkeletonBatcher.SetSharedUniforms(gUniformDataCamera.mProjectView, viewMat.mMatrices[MONO_CAMERA_VIEW_INDEX],
                                                   (gUniformDataDirectionalLights.mDirectionalLights[0].mDirection),
                                                   (gUniformDataDirectionalLights.mDirectionalLights[0].mColor));
            else
                gSkeletonBatcher.SetSharedUniforms(gUniformDataCamera.mProjectView, viewMat.mMatrices[MONO_CAMERA_VIEW_INDEX],
                                                   vec3(0.0f, 10.0f, 2.0f), vec3(1.0f, 1.0f, 1.0f));

            // Update animated objects
            for (uint hairType = 0; hairType < HAIR_TYPE_COUNT; ++hairType)
            {
                vec3   skeletonPosition = vec3(20.0f - hairType * 10.0f, -5.5f, 10.0f);
                TFAABB boundingBox;
                boundingBox.min = skeletonPosition + vec3(-2.0f, 0.0f, -2.0f);
                boundingBox.max = skeletonPosition + vec3(2.0f, 9.0f, 2.0f);

                if (AABBInFrustum(boundingBox, frustumPlanes))
                {
                    if (!gHairTypeInfo[hairType].mInView)
                    {
                        gHairTypeInfo[hairType].mInView = true;
                        gHairTypeInfo[hairType].mPreWarm = true;
                    }

                    gAnimatedObject[hairType].mRootTransform =
                        mat4::translation(vec3(20.0f - hairType * 10.0f, -5.5f, 10.0f)) * mat4::scale(vec3(5.0f));
                    if (!gAnimatedObject[hairType].Update(min(deltaTime, 1.0f / 60.0f)))
                        LOGF(eINFO, "Animation NOT Updating!");
                    gAnimatedObject[hairType].ComputePose(gAnimatedObject[hairType].mRootTransform);
                }
                else
                    gHairTypeInfo[hairType].mInView = false;
            }

            // Update final capsules
            mat4 boneMatrix = mat4::identity();
            mat3 boneRotation = mat3::identity();
            gUniformDataHairGlobal.mTimeStep = 0.01f;

            for (uint hairType = 0; hairType < HAIR_TYPE_COUNT; ++hairType)
            {
                if (!gHairTypeInfo[hairType].mInView)
                    continue;

                for (size_t i = 0; i < gCapsuleCount; ++i)
                {
                    Capsule capsule = gCapsules[i].mCapsule;
                    if (gCapsules[i].mAttachedBone != -1)
                    {
                        GetCorrectedBoneTranformation(hairType, gCapsules[i].mAttachedBone, &boneMatrix, &boneRotation);
                        vec4 p0 = vec4((capsule.mCenter0), 1.0f);
                        vec4 p1 = vec4((capsule.mCenter1), 1.0f);
                        capsule.mCenter0 = ((boneMatrix * p0).getXYZ());
                        capsule.mCenter1 = ((boneMatrix * p1).getXYZ());
                    }
                    gFinalCapsules[hairType][i] = capsule;
                }

                for (size_t i = 0; i < gHairTypeIndicesCount[hairType]; ++i)
                {
                    uint           k = gHairTypeIndices[hairType][i];
                    HairBuffer&    hair = gHair[k];
                    NamedTransform namedTransform = gTransforms[gHair[k].mTransform];
                    Transform      transform = namedTransform.mTransform;

                    boneMatrix = mat4::identity();
                    boneRotation = mat3::identity();

                    if (namedTransform.mAttachedBone != -1)
                        GetCorrectedBoneTranformation(hairType, namedTransform.mAttachedBone, &boneMatrix, &boneRotation);

                    hair.mUniformDataHairShading.mTransform = mat4::identity();
                    hair.mUniformDataHairShading.mStrandRadius = hair.mStrandRadius * transform.mScale;
                    hair.mUniformDataHairShading.mStrandSpacing = hair.mStrandSpacing * transform.mScale;

                    // Transform the hair to be centered around the origin in hair local space. Then transform it to follow the head.
                    hair.mUniformDataHairSimulation.mTransform = boneMatrix * mat4::rotationZYX(transform.mOrientation) *
                                                                 mat4::translation(transform.mPosition) *
                                                                 mat4::scale(vec3(transform.mScale));
                    hair.mUniformDataHairSimulation.mQuatRotation =
                        quatFromf3x3(boneRotation) * quatFromf3x3(mat3::rotationZYX(transform.mOrientation));
                    hair.mUniformDataHairSimulation.mScale = transform.mScale;
                    for (uint j = 0; j < hair.mUniformDataHairSimulation.mCapsuleCount; ++j)
                        hair.mUniformDataHairSimulation.mCapsules[j] = gFinalCapsules[hairType][hair.mCapsules[j]];
                }

                // Find head transform
                boneMatrix = mat4::identity();
                for (uint i = 0; i < gTransformCount; ++i)
                {
                    if (strcmp(gTransforms[i].mName, "Head") == 0)
                    {
                        GetCorrectedBoneTranformation(hairType, gTransforms[i].mAttachedBone, &boneMatrix, &boneRotation);
                        break;
                    }
                }
                vec4        headPos = boneMatrix * vec4(-1.0f, 0.0f, 0.0f, 1.0f);
                const float shadowRange = 3.0f;
                mat4 orto = mat4::orthographicLH_ReverseZ(-shadowRange, shadowRange, -shadowRange, shadowRange, -shadowRange, shadowRange);

                // Update hair shadow cameras
                for (int i = 0; i < MAX_NUM_DIRECTIONAL_LIGHTS; ++i)
                {
                    gUniformDataDirectionalLights.mDirectionalLights[i].mShadowMap = i;
                    mat4 lookAt = mat4::lookAtRH(
                        Point3(headPos.getXYZ()),
                        Point3(headPos.getXYZ() + normalize((gUniformDataDirectionalLights.mDirectionalLights[i].mDirection))),
                        vec3(0.0f, 1.0f, 0.0f));
                    gUniformDataCameraHairShadows[hairType][i].mProjectView = orto * lookAt;
                    gUniformDataCameraHairShadows[hairType][i].mCamPos =
                        headPos.getXYZ() - normalize((gUniformDataDirectionalLights.mDirectionalLights[i].mDirection)) * 1000.0f;
                    gUniformDataDirectionalLights.mDirectionalLights[i].mShadowRange = shadowRange * 2.0f;
                }
            }
        }
    }

    void Draw() override
    {
        if ((bool)pSwapChain->mEnableVsync != mSettings.mVSyncEnabled)
        {
            waitQueueIdle(pGraphicsQueue);
            ::toggleVSync(pRenderer, &pSwapChain);
        }

        // FRAME SYNC
        //
        // This will acquire the next swapchain image
        uint32_t swapchainImageIndex;
        acquireNextImage(pRenderer, pSwapChain, pImageAcquiredSemaphore[mSettings.mFrameIdx], NULL, &swapchainImageIndex);

        TFRenderTarget*   pRenderTarget = pSwapChain->ppRenderTargets[swapchainImageIndex];
        GpuCmdRingElement elem = getNextGpuCmdRingElement(&gGraphicsCmdRing, true, 1);

        TFFenceStatus fenceStatus;
        getFenceStatus(pRenderer, elem.pFence, &fenceStatus);
        if (fenceStatus == TF_FENCE_STATUS_INCOMPLETE)
            waitForFences(pRenderer, 1, &elem.pFence);

        resetCmdPool(pRenderer, elem.pCmdPool);

        // SET CONSTANT BUFFERS
        //
        for (size_t totalBuf = 0; totalBuf < MATERIAL_INSTANCE_COUNT; ++totalBuf)
        {
            TFBufferUpdateDesc objBuffUpdateDesc = { pUniformBufferMatBall[mSettings.mFrameIdx][totalBuf] };
            beginUpdateResource(&objBuffUpdateDesc);
            memcpy(objBuffUpdateDesc.pMappedData, &gUniformDataMatBall[totalBuf], sizeof(gUniformDataMatBall[totalBuf]));
            endUpdateResource(&objBuffUpdateDesc);
        }

        // Using the existing buffer for the shadow pass: &gUniformDataCamera -------------------------------------+
        // this will work as long as projView matrix is the first piece of data in &gUniformDataCamera             v
        // TFBufferUpdateDesc shadowMapCamBuffUpdatedesc = { pUniformBufferCameraShadowPass[mSettings.mFrameIdx], &gUniformDataCamera };
        TFBufferUpdateDesc shadowMapCamBuffUpdatedesc = { pUniformBufferCameraShadowPass[mSettings.mFrameIdx] };
        beginUpdateResource(&shadowMapCamBuffUpdatedesc);
        memcpy(shadowMapCamBuffUpdatedesc.pMappedData, &gUniformDataDirectionalLights.mDirectionalLights[0].mViewProj, sizeof(mat4));
        endUpdateResource(&shadowMapCamBuffUpdatedesc);

        TFBufferUpdateDesc camBuffUpdateDesc = { pUniformBufferCamera[mSettings.mFrameIdx] };
        beginUpdateResource(&camBuffUpdateDesc);
        memcpy(camBuffUpdateDesc.pMappedData, &gUniformDataCamera, sizeof(gUniformDataCamera));
        endUpdateResource(&camBuffUpdateDesc);

        TFBufferUpdateDesc skyboxViewProjCbv = { pUniformBufferCameraSkybox[mSettings.mFrameIdx] };
        beginUpdateResource(&skyboxViewProjCbv);
        memcpy(skyboxViewProjCbv.pMappedData, &gUniformDataCameraSkybox, sizeof(gUniformDataCameraSkybox));
        endUpdateResource(&skyboxViewProjCbv);

        for (uint hairType = 0; hairType < HAIR_TYPE_COUNT; ++hairType)
        {
            if (!gHairTypeInfo[hairType].mInView)
                continue;

            for (int i = 0; i < MAX_NUM_DIRECTIONAL_LIGHTS; ++i)
            {
                TFBufferUpdateDesc hairShadowBuffUpdateDesc = { pUniformBufferCameraHairShadows[mSettings.mFrameIdx][hairType][i] };
                beginUpdateResource(&hairShadowBuffUpdateDesc);
                memcpy(hairShadowBuffUpdateDesc.pMappedData, &gUniformDataCameraHairShadows[hairType][i],
                       sizeof(gUniformDataCameraHairShadows[hairType][i]));
                endUpdateResource(&hairShadowBuffUpdateDesc);
            }
        }

        TFBufferUpdateDesc directionalLightsBufferUpdateDesc = { pUniformBufferDirectionalLights[mSettings.mFrameIdx] };
        beginUpdateResource(&directionalLightsBufferUpdateDesc);
        memcpy(directionalLightsBufferUpdateDesc.pMappedData, &gUniformDataDirectionalLights, sizeof(gUniformDataDirectionalLights));
        endUpdateResource(&directionalLightsBufferUpdateDesc);

        TFBufferUpdateDesc hairGlobalBufferUpdateDesc = { pUniformBufferHairGlobal };
        beginUpdateResource(&hairGlobalBufferUpdateDesc);
        memcpy(hairGlobalBufferUpdateDesc.pMappedData, &gUniformDataHairGlobal, sizeof(gUniformDataHairGlobal));
        endUpdateResource(&hairGlobalBufferUpdateDesc);

        for (size_t i = 0; i < gHairCount; ++i)
        {
            TFBufferUpdateDesc hairShadingBufferUpdateDesc = { gHair[i].pUniformBufferHairShading[mSettings.mFrameIdx] };
            beginUpdateResource(&hairShadingBufferUpdateDesc);
            memcpy(hairShadingBufferUpdateDesc.pMappedData, &gHair[i].mUniformDataHairShading, sizeof(gHair[i].mUniformDataHairShading));
            endUpdateResource(&hairShadingBufferUpdateDesc);

            TFBufferUpdateDesc hairSimulationBufferUpdateDesc = { gHair[i].pUniformBufferHairSimulation[mSettings.mFrameIdx] };
            beginUpdateResource(&hairSimulationBufferUpdateDesc);
            memcpy(hairSimulationBufferUpdateDesc.pMappedData, &gHair[i].mUniformDataHairSimulation,
                   sizeof(gHair[i].mUniformDataHairSimulation));
            endUpdateResource(&hairSimulationBufferUpdateDesc);
        }

        if (gMaterialType == MATERIAL_HAIR)
        {
            gSkeletonBatcher.PreSetInstanceUniforms(mSettings.mFrameIdx);
            gSkeletonBatcher.SetPerInstanceUniforms(mSettings.mFrameIdx);
        }

        /************************************************************************/
        // Draw
        /************************************************************************/
        TFCmd* cmd = elem.pCmds[0];
        beginCmd(cmd);
        cmdBindDescriptorSet(cmd, 0, pDescriptorSetPersistent);
        cmdBindDescriptorSet(cmd, mSettings.mFrameIdx, pDescriptorSetPerFrame);
        gCurrentGpuProfileToken = gMaterialType == MATERIAL_HAIR ? gHairGpuProfileToken : gMetalWoodGpuProfileToken;

        cmdBeginGpuFrameProfile(cmd, gCurrentGpuProfileToken);

        TFRenderTargetBarrier barriers[] = { { pRenderTarget, TF_RESOURCE_STATE_PRESENT, TF_RESOURCE_STATE_RENDER_TARGET },
                                             { pRenderTargetShadowMap, TF_RESOURCE_STATE_SHADER_RESOURCE, TF_RESOURCE_STATE_DEPTH_WRITE } };
        cmdResourceBarrier(cmd, 0, NULL, 0, NULL, 2, barriers);

        TFBindRenderTargetsDesc bindRenderTargets = {};

        // Shadow Pass
        {
            cmdBeginGpuTimestampQuery(cmd, gCurrentGpuProfileToken, "Shadow Pass");

            // Bind Render Targets
            {
                bindRenderTargets = {};
                bindRenderTargets.mDepthStencil = { pRenderTargetShadowMap, TF_LOAD_ACTION_CLEAR };
                cmdBindRenderTargets(cmd, &bindRenderTargets);
                cmdSetViewport(cmd, 0.0f, 0.0f, (float)pRenderTargetShadowMap->mWidth, (float)pRenderTargetShadowMap->mHeight, 0.0f, 1.0f);
                cmdSetScissor(cmd, 0, 0, pRenderTargetShadowMap->mWidth, pRenderTargetShadowMap->mHeight);
            }

            cmdBindPipeline(cmd, pPipelineShadowPass);

            if (gMaterialType != MATERIAL_HAIR)
            {
                // Draw ground plane
                {
                    cmdBeginGpuTimestampQuery(cmd, gCurrentGpuProfileToken, "Draw Ground Plane Shadow");
                    cmdBindVertexBuffer(cmd, 1, &gMeshes[MESH_CUBE]->pVertexBuffers[0], &gMeshes[MESH_CUBE]->mVertexStrides[0], NULL);
                    cmdBindIndexBuffer(cmd, gMeshes[MESH_CUBE]->pIndexBuffer, gMeshes[MESH_CUBE]->mIndexType, 0);

                    cmdBindDescriptorSet(cmd, 0, pDescriptorSetShadowPerDraw);
                    cmdDrawIndexed(cmd, gMeshes[MESH_CUBE]->mIndexCount, 0, 0);
                    cmdEndGpuTimestampQuery(cmd, gCurrentGpuProfileToken);
                }

                // Draw name plates
                {
                    cmdBeginGpuTimestampQuery(cmd, gCurrentGpuProfileToken, "Draw Name Plates Shadow");
                    for (uint32_t j = 0; j < MATERIAL_INSTANCE_COUNT; ++j)
                    {
                        cmdBindDescriptorSet(cmd, 1 + j, pDescriptorSetShadowPerDraw);
                        cmdDrawIndexed(cmd, gMeshes[MESH_CUBE]->mIndexCount, 0, 0);
                    }
                    cmdEndGpuTimestampQuery(cmd, gCurrentGpuProfileToken);
                }

                // Draw material balls
                {
                    cmdBeginGpuTimestampQuery(cmd, gCurrentGpuProfileToken, "Draw Material Balls Shadow");
                    cmdBindVertexBuffer(cmd, 1, &gMeshes[MESH_MAT_BALL]->pVertexBuffers[0], &gMeshes[MESH_MAT_BALL]->mVertexStrides[0],
                                        NULL);
                    cmdBindIndexBuffer(cmd, gMeshes[MESH_MAT_BALL]->pIndexBuffer, gMeshes[MESH_MAT_BALL]->mIndexType, 0);
                    for (uint32_t i = 0; i < MATERIAL_INSTANCE_COUNT; ++i)
                    {
                        cmdBindDescriptorSet(cmd, 1 + MATERIAL_INSTANCE_COUNT + (mSettings.mFrameIdx * MATERIAL_INSTANCE_COUNT + i),
                                             pDescriptorSetShadowPerDraw);
                        cmdDrawIndexed(cmd, gMeshes[MESH_MAT_BALL]->mIndexCount, 0, 0);
                    }
                    cmdEndGpuTimestampQuery(cmd, gCurrentGpuProfileToken);
                }
            }

            cmdEndGpuTimestampQuery(cmd, gCurrentGpuProfileToken); // Shadow Pass
        }

        // Skybox Pass
        {
            cmdBeginGpuTimestampQuery(cmd, gCurrentGpuProfileToken, "Skybox Pass");

            // Bind Render Targets
            {
                bindRenderTargets = {};
                bindRenderTargets.mRenderTargetCount = 1;
                bindRenderTargets.mRenderTargets[0] = { pRenderTarget, TF_LOAD_ACTION_CLEAR };
                cmdBindRenderTargets(cmd, &bindRenderTargets);
                cmdSetViewport(cmd, 0.0f, 0.0f, (float)pRenderTarget->mWidth, (float)pRenderTarget->mHeight, 1.0f, 1.0f);
                cmdSetScissor(cmd, 0, 0, pRenderTarget->mWidth, pRenderTarget->mHeight);
            }
            // Draw
            if (gDrawSkybox) // TODO: do we need this condition?
            {
                const uint32_t skyboxStride = sizeof(float) * 4;
                cmdBindPipeline(cmd, pPipelineSkybox);
                cmdBindVertexBuffer(cmd, 1, &pVertexBufferSkybox, &skyboxStride, NULL);
                cmdDraw(cmd, 36, 0);
            }
            cmdSetViewport(cmd, 0.0f, 0.0f, (float)pRenderTarget->mWidth, (float)pRenderTarget->mHeight, 0.0f, 1.0f);
            cmdEndGpuTimestampQuery(cmd, gCurrentGpuProfileToken); // Skybox Pass
        }
        cmdBindRenderTargets(cmd, NULL);

        TFRenderTargetBarrier shadowTexBarrier = { pRenderTargetShadowMap, TF_RESOURCE_STATE_DEPTH_WRITE,
                                                   TF_RESOURCE_STATE_SHADER_RESOURCE };
        cmdResourceBarrier(cmd, 0, NULL, 0, NULL, 1, &shadowTexBarrier);

        if (gMaterialType == MATERIAL_METAL || gMaterialType == MATERIAL_WOOD)
        {
            cmdBeginGpuTimestampQuery(cmd, gCurrentGpuProfileToken, "Lighting Pass");
        }

        // Bind Render Targets
        {
            bindRenderTargets = {};
            bindRenderTargets.mRenderTargetCount = 1;
            bindRenderTargets.mRenderTargets[0] = { pRenderTarget, TF_LOAD_ACTION_LOAD };
            bindRenderTargets.mDepthStencil = { pRenderTargetDepth, TF_LOAD_ACTION_CLEAR };
            cmdBindRenderTargets(cmd, &bindRenderTargets);
            cmdSetViewport(cmd, 0.0f, 0.0f, (float)pRenderTarget->mWidth, (float)pRenderTarget->mHeight, 0.0f, 1.0f);
            cmdSetScissor(cmd, 0, 0, pRenderTarget->mWidth, pRenderTarget->mHeight);
        }

        // Draw ground plane
        {
            cmdBeginGpuTimestampQuery(cmd, gCurrentGpuProfileToken, "Draw Ground Plane");
            cmdBindPipeline(cmd, ppSceneMaterialPipelines[SCENE_MATERIAL_FLOOR]);
            cmdBindVertexBuffer(cmd, 1, &gMeshes[MESH_CUBE]->pVertexBuffers[0], &gMeshes[MESH_CUBE]->mVertexStrides[0], NULL);
            cmdBindIndexBuffer(cmd, gMeshes[MESH_CUBE]->pIndexBuffer, gMeshes[MESH_CUBE]->mIndexType, 0);
            cmdBindDescriptorSet(cmd, 0, ppSceneMaterialDescriptorSets[SCENE_MATERIAL_FLOOR]);
            cmdDrawIndexed(cmd, gMeshes[MESH_CUBE]->mIndexCount, 0, 0);
            cmdEndGpuTimestampQuery(cmd, gCurrentGpuProfileToken);
        }

        // DRAW THE OBJECTS W/ MATERIALS
        if (gMaterialType == MATERIAL_METAL || gMaterialType == MATERIAL_WOOD)
        {
            if (gMaterialType == MATERIAL_METAL)
            {
                // Draw name plates
                {
                    cmdBeginGpuTimestampQuery(cmd, gCurrentGpuProfileToken, "Draw Name Plates");
                    cmdBindPipeline(cmd, ppSceneMaterialPipelines[SCENE_MATERIAL_NAME_PLATE]);
                    for (uint32_t j = 0; j < MATERIAL_INSTANCE_COUNT; ++j)
                    {
                        cmdBindDescriptorSet(cmd, 1 + j, ppSceneMaterialDescriptorSets[SCENE_MATERIAL_NAME_PLATE]);
                        cmdDrawIndexed(cmd, gMeshes[MESH_CUBE]->mIndexCount, 0, 0);
                    }
                    cmdEndGpuTimestampQuery(cmd, gCurrentGpuProfileToken);
                }

                // Draw material balls
                {
                    cmdBeginGpuTimestampQuery(cmd, gCurrentGpuProfileToken, "Draw Material Balls");
                    cmdBindVertexBuffer(cmd, 1, &gMeshes[MESH_MAT_BALL]->pVertexBuffers[0], &gMeshes[MESH_MAT_BALL]->mVertexStrides[0],
                                        NULL);
                    cmdBindIndexBuffer(cmd, gMeshes[MESH_MAT_BALL]->pIndexBuffer, gMeshes[MESH_MAT_BALL]->mIndexType, 0);

                    for (uint32_t i = 0; i < MATERIAL_INSTANCE_COUNT; ++i)
                    {
                        uint32_t materialindex = i % SCENE_MATERIAL_METAL_COUNT;
                        uint32_t descriptorIndex = 1 + MATERIAL_INSTANCE_COUNT + (mSettings.mFrameIdx * MATERIAL_INSTANCE_COUNT) + i;

                        cmdBindPipeline(cmd, ppSceneMaterialPipelines[materialindex]);
                        cmdBindDescriptorSet(cmd, descriptorIndex, ppSceneMaterialDescriptorSets[materialindex]);
                        cmdDrawIndexed(cmd, gMeshes[MESH_MAT_BALL]->mIndexCount, 0, 0);
                    }
                    cmdEndGpuTimestampQuery(cmd, gCurrentGpuProfileToken);
                }
            }

            if (gMaterialType == MATERIAL_WOOD)
            {
                // Draw name plates
                {
                    cmdBeginGpuTimestampQuery(cmd, gCurrentGpuProfileToken, "Draw Name Plates");
                    cmdBindPipeline(cmd, ppSceneMaterialPipelines[SCENE_MATERIAL_NAME_PLATE]);
                    for (uint32_t j = 0; j < MATERIAL_INSTANCE_COUNT; ++j)
                    {
                        cmdBindDescriptorSet(cmd, 1 + j, ppSceneMaterialDescriptorSets[SCENE_MATERIAL_NAME_PLATE]);
                        cmdDrawIndexed(cmd, gMeshes[MESH_CUBE]->mIndexCount, 0, 0);
                    }
                    cmdEndGpuTimestampQuery(cmd, gCurrentGpuProfileToken);
                }

                // Draw material balls
                {
                    cmdBeginGpuTimestampQuery(cmd, gCurrentGpuProfileToken, "Draw Material Balls");
                    cmdBindVertexBuffer(cmd, 1, &gMeshes[MESH_MAT_BALL]->pVertexBuffers[0], &gMeshes[MESH_MAT_BALL]->mVertexStrides[0],
                                        NULL);
                    cmdBindIndexBuffer(cmd, gMeshes[MESH_MAT_BALL]->pIndexBuffer, gMeshes[MESH_MAT_BALL]->mIndexType, 0);

                    for (uint32_t i = 0; i < MATERIAL_INSTANCE_COUNT; ++i)
                    {
                        uint32_t materialindex = (i % SCENE_MATERIAL_WOOD_COUNT) + SCENE_MATERIAL_METAL_COUNT;
                        uint32_t descriptorIndex = 1 + MATERIAL_INSTANCE_COUNT + (mSettings.mFrameIdx * MATERIAL_INSTANCE_COUNT) + i;

                        cmdBindPipeline(cmd, ppSceneMaterialPipelines[materialindex]);
                        cmdBindDescriptorSet(cmd, descriptorIndex, ppSceneMaterialDescriptorSets[materialindex]);
                        cmdDrawIndexed(cmd, gMeshes[MESH_MAT_BALL]->mIndexCount, 0, 0);
                    }
                    cmdEndGpuTimestampQuery(cmd, gCurrentGpuProfileToken);
                }
            }

            cmdEndGpuTimestampQuery(cmd, gCurrentGpuProfileToken); // Lighting Pass
            cmdBindRenderTargets(cmd, NULL);
        }
        // Draw hair
        else // gMaterialType == MATERIAL_HAIR
        {
            // Draw the skeleton of the rig
            {
                gSkeletonBatcher.Draw(cmd, mSettings.mFrameIdx);
                cmdBindRenderTargets(cmd, NULL);
            }

            uint32_t descriptorSetIndex = mSettings.mFrameIdx * gHairDynamicDescriptorSetCount;

            // Hair Simulation Pass
            {
                cmdBeginGpuTimestampQuery(cmd, gCurrentGpuProfileToken, "Hair Simulation");

                for (uint hairType = 0; hairType < HAIR_TYPE_COUNT; ++hairType)
                {
                    if (!gHairTypeInfo[hairType].mInView)
                    {
                        descriptorSetIndex += gHairTypeIndicesCount[hairType];
                        continue;
                    }

                    for (size_t i = 0; i < gHairTypeIndicesCount[hairType]; ++i)
                    {
                        uint        k = gHairTypeIndices[hairType][i];
                        HairBuffer& hair = gHair[k];

                        uint dispatchGroupCountPerVertex =
                            hair.mTotalVertexCount / 64 / (hair.mUniformDataHairSimulation.mNumFollowHairsPerGuideHair + 1);
                        uint dispatchGroupCountPerStrand = hair.mNumGuideStrands / 64;

                        TFBufferBarrier bufferBarriers[3] = {};
                        bufferBarriers[0].pBuffer = hair.pBufferHairSimulationVertexPositions[0];
                        bufferBarriers[0].mCurrentState = TF_RESOURCE_STATE_SHADER_RESOURCE;
                        bufferBarriers[0].mNewState = TF_RESOURCE_STATE_UNORDERED_ACCESS;
                        bufferBarriers[1].pBuffer = hair.pBufferHairSimulationVertexPositions[1];
                        bufferBarriers[1].mCurrentState = TF_RESOURCE_STATE_UNORDERED_ACCESS;
                        bufferBarriers[1].mNewState = TF_RESOURCE_STATE_UNORDERED_ACCESS;
                        bufferBarriers[2].pBuffer = hair.pBufferHairSimulationVertexPositions[2];
                        bufferBarriers[2].mCurrentState = TF_RESOURCE_STATE_UNORDERED_ACCESS;
                        bufferBarriers[2].mNewState = TF_RESOURCE_STATE_UNORDERED_ACCESS;
                        cmdResourceBarrier(cmd, 3, bufferBarriers, 0, NULL, 0, NULL);

                        if (gFirstHairSimulationFrame || gHairTypeInfo[hairType].mPreWarm)
                        {
                            cmdBindPipeline(cmd, pPipelineHairPreWarm);
                            cmdBindDescriptorSet(cmd, descriptorSetIndex, pDescriptorSetHairPerDraw);

                            cmdDispatch(cmd, dispatchGroupCountPerVertex, 1, 1);

                            for (int j = 0; j < 3; ++j)
                            {
                                bufferBarriers[j].pBuffer = hair.pBufferHairSimulationVertexPositions[j];
                                bufferBarriers[j].mCurrentState = bufferBarriers[j].mNewState = TF_RESOURCE_STATE_UNORDERED_ACCESS;
                            }
                            cmdResourceBarrier(cmd, 3, bufferBarriers, 0, NULL, 0, NULL);
                        }

                        cmdBindPipeline(cmd, pPipelineHairIntegrate);
                        cmdBindDescriptorSet(cmd, descriptorSetIndex, pDescriptorSetHairPerDraw);
                        cmdDispatch(cmd, dispatchGroupCountPerVertex, 1, 1);

                        for (int j = 0; j < 3; ++j)
                        {
                            bufferBarriers[j].pBuffer = hair.pBufferHairSimulationVertexPositions[j];
                            bufferBarriers[j].mCurrentState = bufferBarriers[j].mNewState = TF_RESOURCE_STATE_UNORDERED_ACCESS;
                        }
                        cmdResourceBarrier(cmd, 3, bufferBarriers, 0, NULL, 0, NULL);

                        if (hair.mUniformDataHairSimulation.mShockPropagationStrength > 0.0f)
                        {
                            cmdBindPipeline(cmd, pPipelineHairShockPropagation);
                            cmdBindDescriptorSet(cmd, descriptorSetIndex, pDescriptorSetHairPerDraw);
                            cmdDispatch(cmd, dispatchGroupCountPerStrand, 1, 1);

                            for (int j = 0; j < 3; ++j)
                            {
                                bufferBarriers[j].pBuffer = hair.pBufferHairSimulationVertexPositions[j];
                                bufferBarriers[j].mCurrentState = bufferBarriers[j].mNewState = TF_RESOURCE_STATE_UNORDERED_ACCESS;
                            }
                            cmdResourceBarrier(cmd, 3, bufferBarriers, 0, NULL, 0, NULL);
                        }

                        if (hair.mUniformDataHairSimulation.mLocalConstraintIterations > 0 &&
                            hair.mUniformDataHairSimulation.mLocalStiffness > 0.0f)
                        {
                            cmdBindPipeline(cmd, pPipelineHairLocalConstraints);
                            cmdBindDescriptorSet(cmd, descriptorSetIndex, pDescriptorSetHairPerDraw);

                            for (int j = 0; j < 3; ++j)
                            {
                                bufferBarriers[j].pBuffer = hair.pBufferHairSimulationVertexPositions[j];
                                bufferBarriers[j].mCurrentState = bufferBarriers[j].mNewState = TF_RESOURCE_STATE_UNORDERED_ACCESS;
                            }

                            for (int j = 0; j < (int)hair.mUniformDataHairSimulation.mLocalConstraintIterations; ++j)
                            {
                                cmdDispatch(cmd, dispatchGroupCountPerStrand, 1, 1);
                                cmdResourceBarrier(cmd, 3, bufferBarriers, 0, NULL, 0, NULL);
                            }
                        }

                        cmdBindPipeline(cmd, pPipelineHairLengthConstraints);

                        bufferBarriers[0].pBuffer = hair.pBufferHairVertexTangents;
                        bufferBarriers[0].mCurrentState = TF_RESOURCE_STATE_SHADER_RESOURCE;
                        bufferBarriers[0].mNewState = TF_RESOURCE_STATE_UNORDERED_ACCESS;
                        cmdResourceBarrier(cmd, 1, bufferBarriers, 0, NULL, 0, NULL);
                        cmdBindDescriptorSet(cmd, descriptorSetIndex, pDescriptorSetHairPerDraw);
                        cmdDispatch(cmd, dispatchGroupCountPerVertex, 1, 1);

                        // Update follow hairs
                        if (hair.mUniformDataHairSimulation.mNumFollowHairsPerGuideHair > 0)
                        {
                            cmdBindPipeline(cmd, pPipelineHairUpdateFollowHairs);

                            bufferBarriers[0].pBuffer = hair.pBufferHairVertexTangents;
                            bufferBarriers[0].mCurrentState = TF_RESOURCE_STATE_UNORDERED_ACCESS;
                            bufferBarriers[0].mNewState = TF_RESOURCE_STATE_UNORDERED_ACCESS;
                            bufferBarriers[1].pBuffer = hair.pBufferHairSimulationVertexPositions[0];
                            bufferBarriers[1].mCurrentState = TF_RESOURCE_STATE_UNORDERED_ACCESS;
                            bufferBarriers[1].mNewState = TF_RESOURCE_STATE_UNORDERED_ACCESS;
                            cmdResourceBarrier(cmd, 2, bufferBarriers, 0, NULL, 0, NULL);
                            cmdBindDescriptorSet(cmd, descriptorSetIndex, pDescriptorSetHairPerDraw);
                            cmdDispatch(cmd, dispatchGroupCountPerVertex, 1, 1);
                        }

                        bufferBarriers[0].pBuffer = hair.pBufferHairSimulationVertexPositions[0];
                        bufferBarriers[0].mCurrentState = TF_RESOURCE_STATE_UNORDERED_ACCESS;
                        bufferBarriers[0].mNewState = TF_RESOURCE_STATE_SHADER_RESOURCE;
                        bufferBarriers[1].pBuffer = hair.pBufferHairVertexTangents;
                        bufferBarriers[1].mCurrentState = TF_RESOURCE_STATE_UNORDERED_ACCESS;
                        bufferBarriers[1].mNewState = TF_RESOURCE_STATE_SHADER_RESOURCE;
                        cmdResourceBarrier(cmd, 2, bufferBarriers, 0, NULL, 0, NULL);

                        ++descriptorSetIndex;
                    }

                    gHairTypeInfo[hairType].mPreWarm = false;
                }
                cmdEndGpuTimestampQuery(cmd, gCurrentGpuProfileToken); // Hair Simulation
            }

            // Hair Rendering Pass
            {
                cmdBeginGpuTimestampQuery(cmd, gCurrentGpuProfileToken, "Hair Rendering");

                TFRenderTargetBarrier rtBarriers[2] = {};
                TFBufferBarrier       bufferBarrier[1] = {};
                TFTextureBarrier      textureBarriers[2] = {};

                // Resource Transition
                {
                    if (gSupportTextureAtomics)
                    {
                        textureBarriers[0].pTexture = pTextureHairDepth;
                        textureBarriers[0].mCurrentState = TF_RESOURCE_STATE_SHADER_RESOURCE;
                        textureBarriers[0].mNewState = TF_RESOURCE_STATE_UNORDERED_ACCESS;
                        cmdResourceBarrier(cmd, 0, NULL, 1, textureBarriers, 0, NULL);
                    }
                    else
                    {
                        bufferBarrier[0].pBuffer = pBufferHairDepth;
                        bufferBarrier[0].mCurrentState = TF_RESOURCE_STATE_SHADER_RESOURCE;
                        bufferBarrier[0].mNewState = TF_RESOURCE_STATE_UNORDERED_ACCESS;
                        cmdResourceBarrier(cmd, 1, bufferBarrier, 0, NULL, 0, NULL);
                    }
                }

                // Hair Clear Pass
                {
                    cmdBeginGpuTimestampQuery(cmd, gCurrentGpuProfileToken, "Hair Clear");

                    // Bind Render Targets
                    {
                        bindRenderTargets = {};
                        bindRenderTargets.mDepthStencil = { pRenderTargetDepth, TF_LOAD_ACTION_LOAD };
                        cmdBindRenderTargets(cmd, &bindRenderTargets);
                        cmdSetViewport(cmd, 0.0f, 0.0f, (float)pRenderTarget->mWidth, (float)pRenderTarget->mHeight, 0.0f, 1.0f);
                        cmdSetScissor(cmd, 0, 0, pRenderTarget->mWidth, pRenderTarget->mHeight);
                    }
                    // Draw
                    {
                        cmdBindPipeline(cmd, pPipelineHairClear);
                        cmdBindDescriptorSet(cmd, 0, pDescriptorSetHairPerDraw);
                        cmdDraw(cmd, 3, 0);
                    }

                    cmdBindRenderTargets(cmd, NULL);
                    cmdEndGpuTimestampQuery(cmd, gCurrentGpuProfileToken);
                }

                // Hair Depth Peeling Pass
                {
                    // Draw hair - depth peeling and alpha accumulation
                    cmdBeginGpuTimestampQuery(cmd, gCurrentGpuProfileToken, "Hair Depth Peeling");

                    // Resource Transition
                    {
                        rtBarriers[0].pRenderTarget = pRenderTargetDepthPeeling;
                        rtBarriers[0].mCurrentState = TF_RESOURCE_STATE_SHADER_RESOURCE;
                        rtBarriers[0].mNewState = TF_RESOURCE_STATE_RENDER_TARGET;
                        if (gSupportTextureAtomics)
                        {
                            textureBarriers[0].pTexture = pTextureHairDepth;
                            textureBarriers[0].mCurrentState = TF_RESOURCE_STATE_UNORDERED_ACCESS;
                            textureBarriers[0].mNewState = TF_RESOURCE_STATE_UNORDERED_ACCESS;
                            cmdResourceBarrier(cmd, 0, NULL, 1, textureBarriers, 1, rtBarriers);
                        }
                        else
                        {
                            bufferBarrier[0].pBuffer = pBufferHairDepth;
                            bufferBarrier[0].mCurrentState = TF_RESOURCE_STATE_UNORDERED_ACCESS;
                            bufferBarrier[0].mNewState = TF_RESOURCE_STATE_UNORDERED_ACCESS;
                            cmdResourceBarrier(cmd, 1, bufferBarrier, 0, NULL, 1, rtBarriers);
                        }
                    }
                    // Bind Render Targets
                    {
                        bindRenderTargets = {};
                        bindRenderTargets.mRenderTargetCount = 1;
                        bindRenderTargets.mRenderTargets[0] = { pRenderTargetDepthPeeling, TF_LOAD_ACTION_CLEAR };
                        bindRenderTargets.mDepthStencil = { pRenderTargetDepth, TF_LOAD_ACTION_LOAD };
                        cmdBindRenderTargets(cmd, &bindRenderTargets);
                        cmdSetViewport(cmd, 0.0f, 0.0f, (float)pRenderTargetDepthPeeling->mWidth, (float)pRenderTargetDepthPeeling->mHeight,
                                       0.0f, 1.0f);
                        cmdSetScissor(cmd, 0, 0, pRenderTargetDepthPeeling->mWidth, pRenderTargetDepthPeeling->mHeight);
                    }
                    // Draw
                    {
                        cmdBindPipeline(cmd, pPipelineHairDepthPeeling);
                        descriptorSetIndex = mSettings.mFrameIdx * gHairDynamicDescriptorSetCount;

                        for (uint32_t hairType = 0; hairType < HAIR_TYPE_COUNT; ++hairType)
                        {
                            if (!gHairTypeInfo[hairType].mInView)
                            {
                                descriptorSetIndex += gHairTypeIndicesCount[hairType];
                                continue;
                            }

                            for (size_t i = 0; i < gHairTypeIndicesCount[hairType]; ++i)
                            {
                                uint32_t k = gHairTypeIndices[hairType][i];
                                cmdBindDescriptorSet(cmd, descriptorSetIndex, pDescriptorSetHairPerDraw);
                                cmdBindIndexBuffer(cmd, gHair[k].pBufferTriangleIndices, gHair[k].pGeom->mIndexType, 0);
                                cmdDrawIndexed(cmd, gHair[k].mIndexCountHair, 0, 0);

                                ++descriptorSetIndex;
                            }
                        }
                    }

                    cmdBindRenderTargets(cmd, NULL);
                    cmdEndGpuTimestampQuery(cmd, gCurrentGpuProfileToken);
                }

                // Hair Depth Resolve Pass
                {
                    cmdBeginGpuTimestampQuery(cmd, gCurrentGpuProfileToken, "Hair Depth Resolve");

                    // Resource Transition
                    {
                        if (gSupportTextureAtomics)
                        {
                            textureBarriers[0].pTexture = pTextureHairDepth;
                            textureBarriers[0].mCurrentState = TF_RESOURCE_STATE_UNORDERED_ACCESS;
                            textureBarriers[0].mNewState = TF_RESOURCE_STATE_SHADER_RESOURCE;
                            cmdResourceBarrier(cmd, 0, NULL, 1, textureBarriers, 0, NULL);
                        }
                        else
                        {
                            bufferBarrier[0].pBuffer = pBufferHairDepth;
                            bufferBarrier[0].mCurrentState = TF_RESOURCE_STATE_UNORDERED_ACCESS;
                            bufferBarrier[0].mNewState = TF_RESOURCE_STATE_SHADER_RESOURCE;
                            cmdResourceBarrier(cmd, 1, bufferBarrier, 0, NULL, 0, NULL);
                        }
                    }
                    // Bind Render Targets
                    {
                        bindRenderTargets = {};
                        bindRenderTargets.mDepthStencil = { pRenderTargetDepth, TF_LOAD_ACTION_LOAD };
                        cmdBindRenderTargets(cmd, &bindRenderTargets);
                    }
                    // Draw
                    {
                        cmdBindDescriptorSet(cmd, 0, pDescriptorSetHairPerDraw);
                        cmdBindPipeline(cmd, pPipelineHairDepthResolve);
                        cmdDraw(cmd, 3, 0);
                    }

                    cmdBindRenderTargets(cmd, NULL);
                    cmdEndGpuTimestampQuery(cmd, gCurrentGpuProfileToken);
                }

                uint32_t shadowDescriptorSetIndex[2] = { mSettings.mFrameIdx * MAX_NUM_DIRECTIONAL_LIGHTS * HAIR_TYPE_COUNT,
                                                         mSettings.mFrameIdx * gHairDynamicDescriptorSetCount *
                                                             MAX_NUM_DIRECTIONAL_LIGHTS };

                // Hair Shadow Pass
                {
                    cmdBeginGpuTimestampQuery(cmd, gCurrentGpuProfileToken, "Hair Shadow");

                    for (uint hairType = 0; hairType < HAIR_TYPE_COUNT; ++hairType)
                    {
                        if (!gHairTypeInfo[hairType].mInView)
                        {
                            shadowDescriptorSetIndex[0] += MAX_NUM_DIRECTIONAL_LIGHTS;
                            shadowDescriptorSetIndex[1] += MAX_NUM_DIRECTIONAL_LIGHTS * gHairTypeIndicesCount[hairType];
                            continue;
                        }

                        for (int i = 0; i < MAX_NUM_DIRECTIONAL_LIGHTS; ++i)
                        {
                            cmdBindRenderTargets(cmd, NULL);

                            // Resource Transition
                            {
                                rtBarriers[0].pRenderTarget = pRenderTargetHairShadows[hairType][i];
                                rtBarriers[0].mCurrentState = TF_RESOURCE_STATE_SHADER_RESOURCE;
                                rtBarriers[0].mNewState = TF_RESOURCE_STATE_DEPTH_WRITE;
                                cmdResourceBarrier(cmd, 0, NULL, 0, NULL, 1, rtBarriers);
                            }
                            // Bind Render Targets
                            {
                                bindRenderTargets = {};
                                bindRenderTargets.mDepthStencil = { pRenderTargetHairShadows[hairType][i], TF_LOAD_ACTION_CLEAR };
                                cmdBindRenderTargets(cmd, &bindRenderTargets);
                                cmdSetViewport(cmd, 0.0f, 0.0f, (float)pRenderTargetHairShadows[hairType][i]->mWidth,
                                               (float)pRenderTargetHairShadows[hairType][i]->mHeight, 0.0f, 1.0f);
                                cmdSetScissor(cmd, 0, 0, pRenderTargetHairShadows[hairType][i]->mWidth,
                                              pRenderTargetHairShadows[hairType][i]->mHeight);
                            }
                            // Draw
                            {
                                cmdBindPipeline(cmd, pPipelineHairShadow);

                                for (size_t j = 0; j < gHairTypeIndicesCount[hairType]; ++j)
                                {
                                    uint k = gHairTypeIndices[hairType][j];

                                    cmdBindDescriptorSet(cmd, shadowDescriptorSetIndex[1], pDescriptorSetHairShadow);
                                    cmdBindIndexBuffer(cmd, gHair[k].pBufferTriangleIndices, gHair[k].pGeom->mIndexType, 0);
                                    cmdDrawIndexed(cmd, gHair[k].mIndexCountHair, 0, 0);

                                    ++shadowDescriptorSetIndex[1];
                                }
                            }

                            ++shadowDescriptorSetIndex[0];
                        }
                    }

                    cmdBindRenderTargets(cmd, NULL);
                    cmdEndGpuTimestampQuery(cmd, gCurrentGpuProfileToken);
                }

                // Hair Fill Colors Pass
                {
                    cmdBeginGpuTimestampQuery(cmd, gCurrentGpuProfileToken, "Hair Fill Colors");

                    // Resource Transition
                    {
                        rtBarriers[0].pRenderTarget = pRenderTargetFillColors;
                        rtBarriers[0].mCurrentState = TF_RESOURCE_STATE_SHADER_RESOURCE;
                        rtBarriers[0].mNewState = TF_RESOURCE_STATE_RENDER_TARGET;
                        cmdResourceBarrier(cmd, 0, NULL, 0, NULL, 1, rtBarriers);

                        for (uint hairType = 0; hairType < HAIR_TYPE_COUNT; ++hairType)
                        {
                            if (!gHairTypeInfo[hairType].mInView)
                                continue;

                            for (int i = 0; i < MAX_NUM_DIRECTIONAL_LIGHTS; ++i)
                            {
                                rtBarriers[i].pRenderTarget = pRenderTargetHairShadows[hairType][i];
                                rtBarriers[i].mCurrentState = TF_RESOURCE_STATE_DEPTH_WRITE;
                                rtBarriers[i].mNewState = TF_RESOURCE_STATE_SHADER_RESOURCE;
                            }
                            cmdResourceBarrier(cmd, 0, NULL, 0, NULL, MAX_NUM_DIRECTIONAL_LIGHTS, rtBarriers);
                        }
                    }
                    // Bind Render Targets
                    {
                        bindRenderTargets = {};
                        bindRenderTargets.mRenderTargetCount = 1;
                        bindRenderTargets.mRenderTargets[0] = { pRenderTargetFillColors, TF_LOAD_ACTION_CLEAR };
                        bindRenderTargets.mDepthStencil = { pRenderTargetDepth, TF_LOAD_ACTION_LOAD };
                        cmdBindRenderTargets(cmd, &bindRenderTargets);
                        cmdSetViewport(cmd, 0.0f, 0.0f, (float)pRenderTargetFillColors->mWidth, (float)pRenderTargetFillColors->mHeight,
                                       0.0f, 1.0f);
                        cmdSetScissor(cmd, 0, 0, pRenderTargetFillColors->mWidth, pRenderTargetFillColors->mHeight);
                    }
                    // Draw
                    {
                        cmdBindPipeline(cmd, pPipelineHairFillColors);

                        descriptorSetIndex = mSettings.mFrameIdx * gHairDynamicDescriptorSetCount;

                        for (uint32_t hairType = 0; hairType < HAIR_TYPE_COUNT; ++hairType)
                        {
                            if (!gHairTypeInfo[hairType].mInView)
                            {
                                descriptorSetIndex += gHairTypeIndicesCount[hairType];
                                continue;
                            }

                            cmdBindDescriptorSet(cmd, mSettings.mFrameIdx * HAIR_TYPE_COUNT + hairType, pDescriptorSetHairPerBatch);

                            for (size_t i = 0; i < gHairTypeIndicesCount[hairType]; ++i)
                            {
                                uint32_t k = gHairTypeIndices[hairType][i];

                                cmdBindDescriptorSet(cmd, descriptorSetIndex, pDescriptorSetHairPerDraw);
                                cmdBindIndexBuffer(cmd, gHair[k].pBufferTriangleIndices, gHair[k].pGeom->mIndexType, 0);
                                cmdDrawIndexed(cmd, gHair[k].mIndexCountHair, 0, 0);

                                ++descriptorSetIndex;
                            }
                        }
                    }

                    cmdBindRenderTargets(cmd, NULL);
                    cmdEndGpuTimestampQuery(cmd, gCurrentGpuProfileToken);
                }

                // Hair Resolve Colors Pass
                {
                    cmdBeginGpuTimestampQuery(cmd, gCurrentGpuProfileToken, "Hair Resolve Colors");

                    // Resource Transition
                    {
                        rtBarriers[0].pRenderTarget = pRenderTargetFillColors;
                        rtBarriers[0].mCurrentState = TF_RESOURCE_STATE_RENDER_TARGET;
                        rtBarriers[0].mNewState = TF_RESOURCE_STATE_SHADER_RESOURCE;
                        rtBarriers[1].pRenderTarget = pRenderTargetDepthPeeling;
                        rtBarriers[1].mCurrentState = TF_RESOURCE_STATE_RENDER_TARGET;
                        rtBarriers[1].mNewState = TF_RESOURCE_STATE_SHADER_RESOURCE;
                        cmdResourceBarrier(cmd, 0, NULL, 0, NULL, 2, rtBarriers);
                    }
                    // Bind Render Targets
                    {
                        bindRenderTargets = {};
                        bindRenderTargets.mRenderTargetCount = 1;
                        bindRenderTargets.mRenderTargets[0] = { pRenderTarget, TF_LOAD_ACTION_LOAD };
                        bindRenderTargets.mDepthStencil = { pRenderTargetDepth, TF_LOAD_ACTION_LOAD };
                        cmdBindRenderTargets(cmd, &bindRenderTargets);
                        cmdSetViewport(cmd, 0.0f, 0.0f, (float)pRenderTarget->mWidth, (float)pRenderTarget->mHeight, 0.0f, 1.0f);
                        cmdSetScissor(cmd, 0, 0, pRenderTarget->mWidth, pRenderTarget->mHeight);
                    }
                    // Draw
                    {
                        cmdBindPipeline(cmd, pPipelineHairColorResolve);
                        cmdDraw(cmd, 3, 0);
                    }

                    cmdEndGpuTimestampQuery(cmd, gCurrentGpuProfileToken);
                }

                cmdEndGpuTimestampQuery(cmd, gCurrentGpuProfileToken); // Hair Rendering
            }

#if HAIR_MAX_CAPSULE_COUNT > 0
            // Capsule Visualization Pass
            if (gShowCapsules)
            {
                cmdBeginGpuTimestampQuery(cmd, gCurrentGpuProfileToken, "Capsule Visualization");

                // Bind Render Targets
                {
                    TFBindRenderTargetsDesc bindDesc = {};
                    bindDesc.mRenderTargetCount = 1;
                    bindDesc.mRenderTargets[0] = { pRenderTarget, TF_LOAD_ACTION_LOAD };
                    bindDesc.mDepthStencil = { pRenderTargetDepth, TF_LOAD_ACTION_LOAD };
                    cmdBindRenderTargets(cmd, &bindDesc);
                    cmdSetViewport(cmd, 0.0f, 0.0f, (float)pRenderTarget->mWidth, (float)pRenderTarget->mHeight, 0.0f, 1.0f);
                    cmdSetScissor(cmd, 0, 0, pRenderTarget->mWidth, pRenderTarget->mHeight);
                }
                // Draw
                {
                    cmdBindPipeline(cmd, pPipelineShowCapsules);
                    cmdBindVertexBuffer(cmd, 1, &gMeshes[MESH_CAPSULE]->pVertexBuffers[0], &gMeshes[MESH_CAPSULE]->mVertexStrides[0], NULL);
                    cmdBindIndexBuffer(cmd, gMeshes[MESH_CAPSULE]->pIndexBuffer, gMeshes[MESH_CAPSULE]->mIndexType, 0);

                    uint32_t capsuleIndex = 0;
                    for (uint hairType = 0; hairType < HAIR_TYPE_COUNT; ++hairType)
                    {
                        for (size_t i = 0; i < gCapsuleCount; ++i)
                        {
                            TFBufferUpdateDesc capsuleUpdateDesc = { pUniformBufferCapsuleData[mSettings.mFrameIdx][hairType][i] };
                            beginUpdateResource(&capsuleUpdateDesc);
                            memcpy(capsuleUpdateDesc.pMappedData, &gFinalCapsules[hairType][i], sizeof(Capsule));
                            endUpdateResource(&capsuleUpdateDesc);
                            cmdBindDescriptorSet(cmd, capsuleIndex + (mSettings.mFrameIdx * (HAIR_TYPE_COUNT * HAIR_MAX_CAPSULE_COUNT)),
                                                 pDescriptorSetCapsuleData);
                            cmdDrawIndexed(cmd, gMeshes[MESH_CAPSULE]->mIndexCount, 0, 0);
                            capsuleIndex++;
                        }
                    }
                }

                cmdEndGpuTimestampQuery(cmd, gCurrentGpuProfileToken);
            }
#endif
            cmdBindRenderTargets(cmd, NULL);

            gFirstHairSimulationFrame = false;
        }
        cmdBindRenderTargets(cmd, NULL);

        /************************************************************************/
        // Draw Commands UI
        /************************************************************************/
        // Text Pass
        {
            cmdBeginGpuTimestampQuery(cmd, gCurrentGpuProfileToken, "Text");

            // Bind Render Targets
            {
                bindRenderTargets = {};
                bindRenderTargets.mRenderTargetCount = 1;
                bindRenderTargets.mRenderTargets[0] = { pRenderTarget, TF_LOAD_ACTION_LOAD };
                bindRenderTargets.mDepthStencil = { pRenderTargetDepth, TF_LOAD_ACTION_LOAD };
                cmdBindRenderTargets(cmd, &bindRenderTargets);
                cmdSetViewport(cmd, 0.0f, 0.0f, (float)pRenderTarget->mWidth, (float)pRenderTarget->mHeight, 0.0f, 1.0f);
                cmdSetScissor(cmd, 0, 0, pRenderTarget->mWidth, pRenderTarget->mHeight);
            }

            const char** ppMaterialNames = NULL;
            switch (gCurrentMaterialType)
            {
            case MATERIAL_METAL:
                ppMaterialNames = metalEnumNames;
                break;
            case MATERIAL_WOOD:
                ppMaterialNames = woodEnumNames;
                break;

                // We don't use name plates for hair material, hairEnumNames are removed.
                //
                // case MATERIAL_HAIR:
                //	ppMaterialNames = hairEnumNames;
                //	break;

            default:
                ppMaterialNames = metalEnumNames;
                break;
            }

            if (gCurrentMaterialType != MATERIAL_HAIR)
            {
                for (uint32_t i = 0; i < MATERIAL_INSTANCE_COUNT; ++i)
                {
                    gMaterialPropDraw.pText = ppMaterialNames[i];
                    gMaterialPropDraw.mFontColor = 0xffaaaaaa;
                    gMaterialPropDraw.mFontSize = 32.0f;
                    gMaterialPropDraw.pFont = gFont;
                    cmdDrawWorldSpaceText(cmd, &gTextWorldMats[i], &gTextProjView, &gMaterialPropDraw);
                }
            }
            cmdEndGpuTimestampQuery(cmd, gCurrentGpuProfileToken); // HUD Text
        }

        // UI Pass
        {
            cmdBeginGpuTimestampQuery(cmd, gCurrentGpuProfileToken, "UI");

            // Bind Render Targets
            {
                bindRenderTargets = {};
                bindRenderTargets.mRenderTargetCount = 1;
                bindRenderTargets.mRenderTargets[0] = { pRenderTarget, TF_LOAD_ACTION_LOAD };
                cmdBindRenderTargets(cmd, &bindRenderTargets);
                cmdSetViewport(cmd, 0.0f, 0.0f, (float)pRenderTarget->mWidth, (float)pRenderTarget->mHeight, 0.0f, 1.0f);
                cmdSetScissor(cmd, 0, 0, pRenderTarget->mWidth, pRenderTarget->mHeight);
            }

            // Draw HUD text
            float2 screenCoords = float2(8, 15);

            gFrameTimeDraw.mFontColor = 0xff00ff00;
            gFrameTimeDraw.mFontSize = 18.0f;
            gFrameTimeDraw.pFont = gFont;

            float2 txtSize = cmdDrawCpuProfile(cmd, screenCoords, &gFrameTimeDraw);

            screenCoords = float2(8.0f, txtSize.y + 75.f);
            cmdDrawGpuProfile(cmd, screenCoords, gCurrentGpuProfileToken, &gFrameTimeDraw);

            if (!gbLuaScriptingSystemLoadedSuccessfully)
            {
                gErrMsgDrawDesc.pText = "Error loading LUA scripts!";
                gErrMsgDrawDesc.mFontColor = 0xff0000ee;
                gErrMsgDrawDesc.mFontSize = 18.0f;
                gErrMsgDrawDesc.pFont = gFont;
                cmdDrawText(cmd, screenCoords, &gErrMsgDrawDesc);
            }
            uiCmdDrawUserInterface(cmd, pSwapChain, pRenderTarget, gCurrentGpuProfileToken);
            cmdEndGpuTimestampQuery(cmd, gCurrentGpuProfileToken); // UI
        }
        cmdBindRenderTargets(cmd, NULL);

        // PRESENT THE GFX QUEUE
        //
        // Transition our texture to present state
        barriers[0] = { pRenderTarget, TF_RESOURCE_STATE_RENDER_TARGET, TF_RESOURCE_STATE_PRESENT };
        cmdResourceBarrier(cmd, 0, NULL, 0, NULL, 1, barriers);
        cmdEndGpuFrameProfile(cmd, gCurrentGpuProfileToken);

        endCmd(cmd);

        FlushResourceUpdateDesc flushUpdateDesc = {};
        flushUpdateDesc.mNodeIndex = 0;
        flushResourceUpdates(&flushUpdateDesc);
        TFSemaphore* waitSemaphores[2] = { flushUpdateDesc.pOutSubmittedSemaphore, pImageAcquiredSemaphore[mSettings.mFrameIdx] };

        TFQueueSubmitDesc submitDesc = {};
        submitDesc.mCmdCount = 1;
        submitDesc.ppCmds = &cmd;
        submitDesc.mSignalSemaphoreCount = 1;
        submitDesc.mWaitSemaphoreCount = TF_ARRAY_COUNT(waitSemaphores);
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

    const char* GetName() override { return "06_MaterialPlayground"; }

private:
    void GetCorrectedBoneTranformation(uint rigIndex, uint boneIndex, mat4* boneMatrix, mat3* boneRotation)
    {
        (*boneMatrix) = gAnimatedObject[rigIndex].mJointWorldMats[boneIndex];

        // Get skeleton scale. Assumes uniform scaling.
        float boneScale = length(((*boneMatrix) * vec4(1.0f, 0.0f, 0.0f, 0.0f)).getXYZ());

        // Get bone position
        vec3 bonePosition = ((*boneMatrix) * vec4(0.0f, 0.0f, 0.0f, 1.0f)).getXYZ();

        // Get bone rotation
        for (int i = 0; i < 3; ++i)
        {
            for (int j = 0; j < 3; ++j)
                (*boneRotation)[i][j] = (*boneMatrix)[i][j];
        }

        // Take scale out of rotation matrix
        *boneRotation = (1.0f / boneScale) * (*boneRotation);

        // Create new bone matrix without scale, with fixed rotations
        (*boneMatrix) = mat4((*boneRotation), bonePosition);
    }

    //--------------------------------------------------------------------------------------------
    // INIT FUNCTIONS
    //--------------------------------------------------------------------------------------------
    void addSamplers()
    {
        TFSamplerDesc repeatSamplerDesc = {};
        repeatSamplerDesc.mAddressU = TF_ADDRESS_MODE_REPEAT;
        repeatSamplerDesc.mAddressV = TF_ADDRESS_MODE_REPEAT;
        repeatSamplerDesc.mAddressW = TF_ADDRESS_MODE_REPEAT;

        repeatSamplerDesc.mMinFilter = TF_FILTER_LINEAR;
        repeatSamplerDesc.mMagFilter = TF_FILTER_LINEAR;
        repeatSamplerDesc.mMipMapMode = TF_MIPMAP_MODE_LINEAR;
        addSampler(pRenderer, &repeatSamplerDesc, &pSamplerBilinearRepeat);

        repeatSamplerDesc.mMinFilter = TF_FILTER_NEAREST;
        repeatSamplerDesc.mMagFilter = TF_FILTER_NEAREST;
        repeatSamplerDesc.mMipMapMode = TF_MIPMAP_MODE_NEAREST;
        addSampler(pRenderer, &repeatSamplerDesc, &pSamplerPointRepeat);

        TFSamplerDesc clampToEdgeSamplerDesc = {};
        clampToEdgeSamplerDesc.mAddressU = TF_ADDRESS_MODE_CLAMP_TO_EDGE;
        clampToEdgeSamplerDesc.mAddressV = TF_ADDRESS_MODE_CLAMP_TO_EDGE;
        clampToEdgeSamplerDesc.mAddressW = TF_ADDRESS_MODE_CLAMP_TO_EDGE;

        clampToEdgeSamplerDesc.mMinFilter = TF_FILTER_LINEAR;
        clampToEdgeSamplerDesc.mMagFilter = TF_FILTER_LINEAR;
        clampToEdgeSamplerDesc.mMipMapMode = TF_MIPMAP_MODE_LINEAR;
        addSampler(pRenderer, &clampToEdgeSamplerDesc, &pSamplerBilinearClampToEdge);

        clampToEdgeSamplerDesc.mMinFilter = TF_FILTER_NEAREST;
        clampToEdgeSamplerDesc.mMagFilter = TF_FILTER_NEAREST;
        clampToEdgeSamplerDesc.mMipMapMode = TF_MIPMAP_MODE_NEAREST;
        addSampler(pRenderer, &clampToEdgeSamplerDesc, &pSamplerPointClampToEdge);
    }

    void removeSamplers()
    {
        removeSampler(pRenderer, pSamplerBilinearRepeat);
        removeSampler(pRenderer, pSamplerPointRepeat);
        removeSampler(pRenderer, pSamplerBilinearClampToEdge);
        removeSampler(pRenderer, pSamplerPointClampToEdge);
    }

    void addShaders()
    {
        TFShaderLoadDesc skyboxShaderDesc = {};
        skyboxShaderDesc.mVert.pFileName = "skybox.vert";

        skyboxShaderDesc.mFrag.pFileName = "skybox.frag";
        addShader(pRenderer, &skyboxShaderDesc, &pShaderSkybox);

        TFShaderLoadDesc shadowPassShaderDesc = {};
        shadowPassShaderDesc.mVert.pFileName = "renderSceneShadows.vert";
        shadowPassShaderDesc.mFrag.pFileName = "renderSceneShadows.frag";
        addShader(pRenderer, &shadowPassShaderDesc, &pShaderShadowPass);

        TFShaderLoadDesc hairClearShaderDesc = {};
        hairClearShaderDesc.mVert.pFileName = "fullscreen.vert";
        hairClearShaderDesc.mFrag.pFileName = "hair_short_cut_clear.frag";
        addShader(pRenderer, &hairClearShaderDesc, &pShaderHairClear);

        TFShaderLoadDesc hairDepthPeelingShaderDesc = {};
        hairDepthPeelingShaderDesc.mVert.pFileName = "hair.vert";
        hairDepthPeelingShaderDesc.mFrag.pFileName = "hair_short_cut_depth_peeling.frag";
        addShader(pRenderer, &hairDepthPeelingShaderDesc, &pShaderHairDepthPeeling);

        TFShaderLoadDesc hairDepthResolveShaderDesc = {};
        hairDepthResolveShaderDesc.mVert.pFileName = "fullscreen.vert";
        hairDepthResolveShaderDesc.mFrag.pFileName = "hair_short_cut_resolve_depth.frag";
        addShader(pRenderer, &hairDepthResolveShaderDesc, &pShaderHairDepthResolve);

        TFShaderLoadDesc hairFillColorShaderDesc = {};
        hairFillColorShaderDesc.mVert.pFileName = "hair.vert";
        hairFillColorShaderDesc.mFrag.pFileName = "hair_short_cut_fill_color.frag";
        addShader(pRenderer, &hairFillColorShaderDesc, &pShaderHairFillColors);

        TFShaderLoadDesc hairColorResolveShaderDesc = {};
        hairColorResolveShaderDesc.mVert.pFileName = "fullscreen.vert";
        hairColorResolveShaderDesc.mFrag.pFileName = "hair_short_cut_resolve_color.frag";
        addShader(pRenderer, &hairColorResolveShaderDesc, &pShaderHairResolveColor);

        TFShaderLoadDesc hairShadowShaderDesc = {};
        hairShadowShaderDesc.mVert.pFileName = "hair_shadow.vert";
        hairShadowShaderDesc.mFrag.pFileName = "hair_shadow.frag";
        addShader(pRenderer, &hairShadowShaderDesc, &pShaderHairShadow);

        TFShaderLoadDesc hairIntegrateShaderDesc = {};
        hairIntegrateShaderDesc.mComp.pFileName = "hair_integrate.comp";
        addShader(pRenderer, &hairIntegrateShaderDesc, &pShaderHairIntegrate);

        TFShaderLoadDesc hairShockPropagationShaderDesc = {};
        hairShockPropagationShaderDesc.mComp.pFileName = "hair_shock_propagation.comp";
        addShader(pRenderer, &hairShockPropagationShaderDesc, &pShaderHairShockPropagation);

        TFShaderLoadDesc hairLocalConstraintsShaderDesc = {};
        hairLocalConstraintsShaderDesc.mComp.pFileName = "hair_local_constraints.comp";
        addShader(pRenderer, &hairLocalConstraintsShaderDesc, &pShaderHairLocalConstraints);

        TFShaderLoadDesc hairLengthConstraintsShaderDesc = {};
        hairLengthConstraintsShaderDesc.mComp.pFileName = "hair_length_constraints.comp";
        addShader(pRenderer, &hairLengthConstraintsShaderDesc, &pShaderHairLengthConstraints);

        TFShaderLoadDesc hairUpdateFollowHairsShaderDesc = {};
        hairUpdateFollowHairsShaderDesc.mComp.pFileName = "hair_update_follow_hairs.comp";
        addShader(pRenderer, &hairUpdateFollowHairsShaderDesc, &pShaderHairUpdateFollowHairs);

        TFShaderLoadDesc hairPreWarmShaderDesc = {};
        hairPreWarmShaderDesc.mComp.pFileName = "hair_pre_warm.comp";
        addShader(pRenderer, &hairPreWarmShaderDesc, &pShaderHairPreWarm);

        TFShaderLoadDesc showCapsulesShaderDesc = {};
        showCapsulesShaderDesc.mVert.pFileName = "showCapsules.vert";
        showCapsulesShaderDesc.mFrag.pFileName = "showCapsules.frag";
        addShader(pRenderer, &showCapsulesShaderDesc, &pShaderShowCapsules);
    }

    void removeShaders()
    {
        removeShader(pRenderer, pShaderSkybox);
        removeShader(pRenderer, pShaderShadowPass);

        removeShader(pRenderer, pShaderHairClear);
        removeShader(pRenderer, pShaderHairDepthPeeling);
        removeShader(pRenderer, pShaderHairDepthResolve);
        removeShader(pRenderer, pShaderHairFillColors);
        removeShader(pRenderer, pShaderHairResolveColor);
        removeShader(pRenderer, pShaderHairIntegrate);
        removeShader(pRenderer, pShaderHairShockPropagation);
        removeShader(pRenderer, pShaderHairLocalConstraints);
        removeShader(pRenderer, pShaderHairLengthConstraints);
        removeShader(pRenderer, pShaderHairUpdateFollowHairs);
        removeShader(pRenderer, pShaderHairPreWarm);
        removeShader(pRenderer, pShaderShowCapsules);
        removeShader(pRenderer, pShaderHairShadow);
    }

    void addSceneMaterials()
    {
        TFPipelineDesc graphicsPipelineDesc = {};
        PIPELINE_LAYOUT_DESC(graphicsPipelineDesc, SRT_LAYOUT_DESC(SrtData, Persistent), SRT_LAYOUT_DESC(SrtData, PerFrame), NULL,
                             SRT_LAYOUT_DESC(SrtData, PerDraw));
        graphicsPipelineDesc.mType = TF_PIPELINE_TYPE_GRAPHICS;
        TFGraphicsPipelineDesc& pipelineSettings = graphicsPipelineDesc.mGraphicsDesc;

        TFDepthStateDesc depthStateDesc = {};
        depthStateDesc.mDepthTest = true;
        depthStateDesc.mDepthWrite = true;
        depthStateDesc.mDepthFunc = TF_CMP_GEQUAL;

        TFRasterizerStateDesc rasterizerStateCullNoneDesc = {};
        rasterizerStateCullNoneDesc.mCullMode = TF_CULL_MODE_NONE;

        pipelineSettings = {};
        pipelineSettings.mPrimitiveTopo = TF_PRIMITIVE_TOPO_TRI_LIST;
        pipelineSettings.mRenderTargetCount = 1;
        pipelineSettings.pDepthState = &depthStateDesc;
        pipelineSettings.pColorFormats = &pSwapChain->ppRenderTargets[0]->mFormat;
        pipelineSettings.mSampleCount = pSwapChain->ppRenderTargets[0]->mSampleCount;
        pipelineSettings.mSampleQuality = pSwapChain->ppRenderTargets[0]->mSampleQuality;
        pipelineSettings.mDepthStencilFormat = pRenderTargetDepth->mFormat;
        pipelineSettings.pVertexLayout = &gVertexLayoutDefault;
        pipelineSettings.pRasterizerState = &rasterizerStateCullNoneDesc;

        TFSyncToken token = {};

        // DEMO: All of these materials have the same shader source in their fmat files
        //       The resource loader will therefore only store data for and load one shader, providing the same shader ID to both materials.
        addMaterial(gBallMaterialsFileName, &pBallMaterials, &token);
        waitForToken(&token);

        addMaterial(gGroundAndNameplateMaterialsFileName, &pGroundAndNameplateMaterials, &token);
        waitForToken(&token);

        // Enable this to make sure that shader/textures are not removed if there are materials that still use them.
#if 1
        removeMaterial(pBallMaterials);
        addMaterial(gBallMaterialsFileName, &pBallMaterials, &token);
        waitForToken(&token);

        TFMaterial* pTempMaterial = NULL;
        addMaterial(gBallMaterialsFileName, &pTempMaterial, &token);
        waitForToken(&token);

        removeMaterial(pTempMaterial);
#endif

        for (uint32_t i = 0; i < SCENE_MATERIAL_TOTAL_COUNT; ++i)
        {
            TFShader* pMaterialShader = NULL;

            if (i < SCENE_MATERIAL_MATBALL_COUNT)
                getMaterialShader(pBallMaterials, i, &pMaterialShader);
            else
                getMaterialShader(pGroundAndNameplateMaterials, i - SCENE_MATERIAL_MATBALL_COUNT, &pMaterialShader);
            TFDescriptorSetDesc setDesc =
                SRT_SET_DESC(SrtData, PerDraw, (1 + MATERIAL_INSTANCE_COUNT) + (gDataBufferCount * MATERIAL_INSTANCE_COUNT), 0);
            addDescriptorSet(pRenderer, &setDesc, &ppSceneMaterialDescriptorSets[i]);

            pipelineSettings.pShaderProgram = pMaterialShader;
            addPipeline(pRenderer, &graphicsPipelineDesc, &ppSceneMaterialPipelines[i]);
        }
    }

    void removeSceneMaterials()
    {
        for (uint32_t i = 0; i < SCENE_MATERIAL_TOTAL_COUNT; ++i)
        {
            removePipeline(pRenderer, ppSceneMaterialPipelines[i]);
            removeDescriptorSet(pRenderer, ppSceneMaterialDescriptorSets[i]);
        }

        removeMaterial(pBallMaterials);
        pBallMaterials = NULL;

        removeMaterial(pGroundAndNameplateMaterials);
        pGroundAndNameplateMaterials = NULL;
    }

    void updateSceneMaterialDescriptorSets()
    {
        TFTexture*       ppTextures[MATERIAL_TEXTURE_COUNT] = { NULL };
        const char*      pTextureNames[MATERIAL_TEXTURE_COUNT] = { NULL };
        TFDescriptorData params[MATERIAL_TEXTURE_COUNT + 6] = {};

        uint32_t pTextureResourceIds[MATERIAL_TEXTURE_COUNT] = {
            SRT_RES_IDX(SrtData, PerDraw, gAlbedoMap),   SRT_RES_IDX(SrtData, PerDraw, gNormalMap),
            SRT_RES_IDX(SrtData, PerDraw, gMetallicMap), SRT_RES_IDX(SrtData, PerDraw, gRoughnessMap),
            SRT_RES_IDX(SrtData, PerDraw, gAOMap),       SRT_RES_IDX(SrtData, PerDraw, gVMFMap),
        };

        // Material Balls
        for (uint32_t i = 0; i < SCENE_MATERIAL_TOTAL_COUNT; ++i)
        {
            if (i < SCENE_MATERIAL_MATBALL_COUNT)
                getMaterialTextures(pBallMaterials, i, pTextureNames, ppTextures, MATERIAL_TEXTURE_COUNT);
            else
                getMaterialTextures(pGroundAndNameplateMaterials, i - SCENE_MATERIAL_MATBALL_COUNT, pTextureNames, ppTextures,
                                    MATERIAL_TEXTURE_COUNT);

            // Per Frame
            for (uint32_t f = 0; f < gDataBufferCount; ++f)
            {
                for (uint32_t j = 0; j < MATERIAL_INSTANCE_COUNT; ++j)
                {
                    // Bind PBR textures
                    for (uint32_t k = 0; k < MATERIAL_TEXTURE_COUNT; ++k)
                    {
                        params[k].mIndex = pTextureResourceIds[k];
                        params[k].ppTextures = &ppTextures[k];
                    }

                    params[MATERIAL_TEXTURE_COUNT].mIndex = SRT_RES_IDX(SrtData, PerDraw, gObject);
                    params[MATERIAL_TEXTURE_COUNT].ppBuffers = &pUniformBufferMatBall[f][j];
                    const uint32_t index = 1 + MATERIAL_INSTANCE_COUNT + (f * MATERIAL_INSTANCE_COUNT) + j;
                    updateDescriptorSet(pRenderer, index, ppSceneMaterialDescriptorSets[i], MATERIAL_TEXTURE_COUNT + 1, params);
                }
            }
        }

        // Ground
        getMaterialTextures(pGroundAndNameplateMaterials, 0, pTextureNames, ppTextures, MATERIAL_TEXTURE_COUNT);

        for (uint32_t j = 0; j < MATERIAL_TEXTURE_COUNT; ++j)
        {
            params[j].mIndex = pTextureResourceIds[j];
            params[j].ppTextures = &ppTextures[j];
        }
        params[MATERIAL_TEXTURE_COUNT].mIndex = SRT_RES_IDX(SrtData, PerDraw, gObject);
        params[MATERIAL_TEXTURE_COUNT].ppBuffers = &pUniformBufferGroundPlane;
        updateDescriptorSet(pRenderer, 0, ppSceneMaterialDescriptorSets[SCENE_MATERIAL_FLOOR], MATERIAL_TEXTURE_COUNT + 1, params);

        // Name Plates
        getMaterialTextures(pGroundAndNameplateMaterials, 1, pTextureNames, ppTextures, MATERIAL_TEXTURE_COUNT);

        for (uint32_t i = 0; i < MATERIAL_INSTANCE_COUNT; ++i)
        {
            for (uint32_t j = 0; j < MATERIAL_TEXTURE_COUNT; ++j)
            {
                params[j].mIndex = pTextureResourceIds[j];
                params[j].ppTextures = &ppTextures[j];
            }
            params[MATERIAL_TEXTURE_COUNT].mIndex = SRT_RES_IDX(SrtData, PerDraw, gObject);
            params[MATERIAL_TEXTURE_COUNT].ppBuffers = &pUniformBufferNamePlates[i];
            updateDescriptorSet(pRenderer, 1 + i, ppSceneMaterialDescriptorSets[SCENE_MATERIAL_NAME_PLATE], MATERIAL_TEXTURE_COUNT + 1,
                                params);
        }
    }

    void addDescriptorSets()
    {
        // Persistent set
        TFDescriptorSetDesc setDesc = SRT_SET_DESC(SrtData, Persistent, 1, 0);
        addDescriptorSet(pRenderer, &setDesc, &pDescriptorSetPersistent);

        // per frame set
        setDesc = SRT_SET_DESC(SrtData, PerFrame, gDataBufferCount, 0);
        addDescriptorSet(pRenderer, &setDesc, &pDescriptorSetPerFrame);

        /// per draw set for shadows
        setDesc = SRT_SET_DESC(SrtData, PerDraw, (1 + MATERIAL_INSTANCE_COUNT) + (gDataBufferCount * MATERIAL_INSTANCE_COUNT), 0);
        addDescriptorSet(pRenderer, &setDesc, &pDescriptorSetShadowPerDraw);

        // Fill Colors
        setDesc = SRT_SET_DESC(SrtHairData, PerBatch, HAIR_TYPE_COUNT * gDataBufferCount, 0);
        addDescriptorSet(pRenderer, &setDesc, &pDescriptorSetHairPerBatch);

        // Hair Simulation
        for (uint32_t i = 0; i < HAIR_TYPE_COUNT; i++)
            gHairDynamicDescriptorSetCount += gHairTypeIndicesCount[i];

        setDesc = SRT_SET_DESC(SrtHairData, PerDraw, gHairDynamicDescriptorSetCount * gDataBufferCount, 0);
        addDescriptorSet(pRenderer, &setDesc, &pDescriptorSetHairPerDraw);

        // Hair Shadow
        setDesc = SRT_SET_DESC(SrtHairData, PerDraw, gHairDynamicDescriptorSetCount * MAX_NUM_DIRECTIONAL_LIGHTS * gDataBufferCount, 0);
        addDescriptorSet(pRenderer, &setDesc, &pDescriptorSetHairShadow);

        // Debug
        setDesc = SRT_SET_DESC(SrtCapsulesData, PerDraw, gDataBufferCount * HAIR_TYPE_COUNT * gCapsuleCount, 0);
        addDescriptorSet(pRenderer, &setDesc, &pDescriptorSetCapsuleData);
    }

    void removeDescriptorSets()
    {
        gHairDynamicDescriptorSetCount = 0;
        removeDescriptorSet(pRenderer, pDescriptorSetShadowPerDraw);
        removeDescriptorSet(pRenderer, pDescriptorSetPerFrame);
        removeDescriptorSet(pRenderer, pDescriptorSetPersistent);

        removeDescriptorSet(pRenderer, pDescriptorSetHairPerDraw);
        removeDescriptorSet(pRenderer, pDescriptorSetHairShadow);
        removeDescriptorSet(pRenderer, pDescriptorSetHairPerBatch);
        removeDescriptorSet(pRenderer, pDescriptorSetCapsuleData);
    }

    // Bake as many descriptor sets upfront as possible to avoid updates during runtime
    void updateDescriptorSets()
    {
        // persistent set
        {
            TFDescriptorData persistentSetParams[15] = {};
            persistentSetParams[0].mIndex = SRT_RES_IDX(SrtData, Persistent, gPointLights);
            persistentSetParams[0].ppBuffers = &pUniformBufferPointLights;
            persistentSetParams[1].mIndex = SRT_RES_IDX(SrtData, Persistent, gBRDFIntegrationMap);
            persistentSetParams[1].ppTextures = &pTextureBRDFIntegrationMap;
            persistentSetParams[2].mIndex = SRT_RES_IDX(SrtData, Persistent, gIrradianceMap);
            persistentSetParams[2].ppTextures = &pTextureIrradianceMap;
            persistentSetParams[3].mIndex = SRT_RES_IDX(SrtData, Persistent, gSpecularMap);
            persistentSetParams[3].ppTextures = &pTextureSpecularMap;
            persistentSetParams[4].mIndex = SRT_RES_IDX(SrtData, Persistent, gShadowMap);
            persistentSetParams[4].ppTextures = &pRenderTargetShadowMap->pTexture;
            persistentSetParams[5].mIndex = SRT_RES_IDX(SrtData, Persistent, gBRDFIntegrationSampler);
            persistentSetParams[5].ppSamplers =
                gSupportLinearSamplingBRDFTextures ? &pSamplerBilinearClampToEdge : &pSamplerPointClampToEdge;
            persistentSetParams[6].mIndex = SRT_RES_IDX(SrtData, Persistent, gEnvironmentSampler);
            persistentSetParams[6].ppSamplers = gSupportLinearSamplingBRDFTextures ? &pSamplerBilinearRepeat : &pSamplerPointRepeat;
            persistentSetParams[7].mIndex = SRT_RES_IDX(SrtData, Persistent, gMaterialSampler);
            persistentSetParams[7].ppSamplers = &pSamplerBilinearRepeat;
            persistentSetParams[8].mIndex = SRT_RES_IDX(SrtData, Persistent, gPointLights);
            persistentSetParams[8].ppBuffers = &pUniformBufferPointLights;
            persistentSetParams[9].mIndex = SRT_RES_IDX(SrtData, Persistent, gColorsTexture);
            persistentSetParams[9].ppTextures = &pRenderTargetFillColors->pTexture;
            persistentSetParams[10].mIndex = SRT_RES_IDX(SrtData, Persistent, gInvAlphaTexture);
            persistentSetParams[10].ppTextures = &pRenderTargetDepthPeeling->pTexture;
            persistentSetParams[11].mIndex = SRT_RES_IDX(SrtData, Persistent, gSkyboxTex);
            persistentSetParams[11].ppTextures = &pTextureSkybox;
            persistentSetParams[12].mIndex = SRT_RES_IDX(SrtHairData, Persistent, gHairGlobal);
            persistentSetParams[12].ppBuffers = &pUniformBufferHairGlobal;
#if TEXTURE_ATOMIC_SUPPORTED
            persistentSetParams[13].mIndex = SRT_RES_IDX(SrtData, Persistent, gDepthsTexture);
            persistentSetParams[13].ppTextures = &pTextureHairDepth;
            updateDescriptorSet(pRenderer, 0, pDescriptorSetPersistent, 14, persistentSetParams);
#else
            updateDescriptorSet(pRenderer, 0, pDescriptorSetPersistent, 13, persistentSetParams);
#endif
        }

        // per frame set
        {
            TFDescriptorData perFrameSetParams[4] = {};
            for (uint32_t f = 0; f < gDataBufferCount; ++f)
            {
                perFrameSetParams[0].mIndex = SRT_RES_IDX(SrtData, PerFrame, gCamera);
                perFrameSetParams[0].ppBuffers = &pUniformBufferCamera[f];
                perFrameSetParams[1].mIndex = SRT_RES_IDX(SrtData, PerFrame, gDirectionalLights);
                perFrameSetParams[1].ppBuffers = &pUniformBufferDirectionalLights[f];
                perFrameSetParams[2].mIndex = SRT_RES_IDX(SrtData, PerFrame, gShadowCamera);
                perFrameSetParams[2].ppBuffers = &pUniformBufferCameraShadowPass[f];
                perFrameSetParams[3].mIndex = SRT_RES_IDX(SrtData, PerFrame, gUniformBlock);
                perFrameSetParams[3].ppBuffers = &pUniformBufferCameraSkybox[f];
                updateDescriptorSet(pRenderer, f, pDescriptorSetPerFrame, 4, perFrameSetParams);
            }
        }

        // Shadow pass
        {
            TFDescriptorData shadowParams[4] = {};
            shadowParams[0].mIndex = SRT_RES_IDX(SrtData, PerDraw, gObject);
            shadowParams[0].ppBuffers = &pUniformBufferGroundPlane;
            updateDescriptorSet(pRenderer, 0, pDescriptorSetShadowPerDraw, 1, shadowParams);
            for (uint32_t j = 0; j < MATERIAL_INSTANCE_COUNT; ++j)
            {
                shadowParams[0].ppBuffers = &pUniformBufferNamePlates[j];
                updateDescriptorSet(pRenderer, 1 + j, pDescriptorSetShadowPerDraw, 1, shadowParams);
                for (uint32_t i = 0; i < gDataBufferCount; ++i)
                {
                    shadowParams[0].ppBuffers = &pUniformBufferMatBall[i][j];
                    updateDescriptorSet(pRenderer, 1 + MATERIAL_INSTANCE_COUNT + (i * MATERIAL_INSTANCE_COUNT + j),
                                        pDescriptorSetShadowPerDraw, 1, shadowParams);
                }
            }
        }

        // Hair
        {
            TFDescriptorData hairParams[16] = {};

            uint32_t descriptorSetIndex = 0;
            uint32_t shadowDescriptorSetIndex[2] = { 0 };
            shadowDescriptorSetIndex[0] = gDataBufferCount * 5;
            for (uint32_t f = 0; f < gDataBufferCount; ++f)
            {
                for (uint32_t hairType = 0; hairType < HAIR_TYPE_COUNT; ++hairType)
                {
                    for (size_t i = 0; i < gHairTypeIndicesCount[hairType]; ++i)
                    {
                        uint32_t k = gHairTypeIndices[hairType][i];

                        hairParams[0].mIndex = SRT_RES_IDX(SrtHairData, PerDraw, gHairSimulation);
                        hairParams[0].ppBuffers = &gHair[k].pUniformBufferHairSimulation[f];
                        hairParams[1].mIndex = SRT_RES_IDX(SrtHairData, PerDraw, gHairVertexPositions);
                        hairParams[1].ppBuffers = &gHair[k].pBufferHairSimulationVertexPositions[0];
                        hairParams[2].mIndex = SRT_RES_IDX(SrtHairData, PerDraw, gHairVertexPositionsPrev);
                        hairParams[2].ppBuffers = &gHair[k].pBufferHairSimulationVertexPositions[1];
                        hairParams[3].mIndex = SRT_RES_IDX(SrtHairData, PerDraw, gHairVertexPositionsPrevPrev);
                        hairParams[3].ppBuffers = &gHair[k].pBufferHairSimulationVertexPositions[2];
                        hairParams[4].mIndex = SRT_RES_IDX(SrtHairData, PerDraw, gHairRestLengths);
                        hairParams[4].ppBuffers = &gHair[k].pBufferHairRestLenghts;
                        hairParams[5].mIndex = SRT_RES_IDX(SrtHairData, PerDraw, gHairRestPositions);
                        hairParams[5].ppBuffers = &gHair[k].pBufferHairVertexPositions;
                        hairParams[6].mIndex = SRT_RES_IDX(SrtHairData, PerDraw, gFollowHairRootOffsets);
                        hairParams[6].ppBuffers = &gHair[k].pBufferFollowHairRootOffsets;
                        hairParams[7].mIndex = SRT_RES_IDX(SrtHairData, PerDraw, gHairGlobalRotations);
                        hairParams[7].ppBuffers = &gHair[k].pBufferHairGlobalRotations;
                        hairParams[8].mIndex = SRT_RES_IDX(SrtHairData, PerDraw, gHairRefsInLocalFrame);
                        hairParams[8].ppBuffers = &gHair[k].pBufferHairRefsInLocalFrame;
                        hairParams[9].mIndex = SRT_RES_IDX(SrtHairData, PerDraw, gHairVertexTangents);
                        hairParams[9].ppBuffers = &gHair[k].pBufferHairVertexTangents;
                        hairParams[10].mIndex = SRT_RES_IDX(SrtHairData, PerDraw, gHair);
                        hairParams[10].ppBuffers = &gHair[k].pUniformBufferHairShading[f];
                        hairParams[11].mIndex = SRT_RES_IDX(SrtHairData, PerDraw, gHairThicknessCoefficients);
                        hairParams[11].ppBuffers = &gHair[k].pBufferHairThicknessCoefficients;
                        hairParams[12].mIndex = SRT_RES_IDX(SrtHairData, PerDraw, gGuideHairVertexPositions);
                        hairParams[12].ppBuffers = &gHair[k].pBufferHairSimulationVertexPositions[0];
                        hairParams[13].mIndex = SRT_RES_IDX(SrtHairData, PerDraw, gGuideHairVertexTangents);
                        hairParams[13].ppBuffers = &gHair[k].pBufferHairVertexTangents;
#if TEXTURE_ATOMIC_SUPPORTED
                        hairParams[14].mIndex = SRT_RES_IDX(SrtHairData, PerDraw, gDestDepthsTexture);
                        hairParams[14].ppTextures = &pTextureHairDepth;
#else
                        hairParams[14].mIndex = SRT_RES_IDX(SrtHairData, PerDraw, gDepthsBuffer);
                        hairParams[14].ppBuffers = &pBufferHairDepth;
#endif
                        updateDescriptorSet(pRenderer, descriptorSetIndex, pDescriptorSetHairPerDraw, 15, hairParams);
                        ++descriptorSetIndex;
                    }

                    for (uint32_t i = 0; i < MAX_NUM_DIRECTIONAL_LIGHTS; ++i)
                    {
                        for (size_t j = 0; j < gHairTypeIndicesCount[hairType]; ++j)
                        {
                            uint32_t k = gHairTypeIndices[hairType][j];

                            hairParams[0].mIndex = SRT_RES_IDX(SrtHairData, PerDraw, gHair);
                            hairParams[0].ppBuffers = &gHair[k].pUniformBufferHairShading[f];
                            hairParams[1].mIndex = SRT_RES_IDX(SrtHairData, PerDraw, gGuideHairVertexPositions);
                            hairParams[1].ppBuffers = &gHair[k].pBufferHairSimulationVertexPositions[0];
                            hairParams[2].mIndex = SRT_RES_IDX(SrtHairData, PerDraw, gGuideHairVertexTangents);
                            hairParams[2].ppBuffers = &gHair[k].pBufferHairVertexTangents;
                            hairParams[3].mIndex = SRT_RES_IDX(SrtHairData, PerDraw, gHairThicknessCoefficients);
                            hairParams[3].ppBuffers = &gHair[k].pBufferHairThicknessCoefficients;
                            hairParams[4].mIndex = SRT_RES_IDX(SrtHairData, PerDraw, gHairShadowCamera);
                            hairParams[4].ppBuffers = &pUniformBufferCameraHairShadows[f][hairType][i];

                            updateDescriptorSet(pRenderer, shadowDescriptorSetIndex[1], pDescriptorSetHairShadow, 5, hairParams);

                            ++shadowDescriptorSetIndex[1];
                        }

                        ++shadowDescriptorSetIndex[0];
                    }

                    TFTexture* ppShadowMaps[MAX_NUM_DIRECTIONAL_LIGHTS] = {};
                    for (int i = 0; i < MAX_NUM_DIRECTIONAL_LIGHTS; ++i)
                        ppShadowMaps[i] = pRenderTargetHairShadows[hairType][i]->pTexture;

                    hairParams[0].mIndex = SRT_RES_IDX(SrtHairData, PerBatch, gHairDirectionalLightShadowMaps);
                    hairParams[0].ppTextures = ppShadowMaps; //-V507
                    hairParams[0].mCount = MAX_NUM_DIRECTIONAL_LIGHTS;
                    hairParams[1].mIndex = SRT_RES_IDX(SrtHairData, PerBatch, gHairDirectionalLightShadowCameras);
                    hairParams[1].ppBuffers = pUniformBufferCameraHairShadows[f][hairType];
                    hairParams[1].mCount = MAX_NUM_DIRECTIONAL_LIGHTS;
                    updateDescriptorSet(pRenderer, f * HAIR_TYPE_COUNT + hairType, pDescriptorSetHairPerBatch, 2, hairParams);
                    hairParams[0] = {};
                    hairParams[1] = {};
                }
            }
        }
        // Debug
        {
            TFDescriptorData params[1] = {};
            uint32_t         capsuleIndex = 0;
            for (uint32_t i = 0; i < gDataBufferCount; ++i)
            {
                for (uint hairType = 0; hairType < HAIR_TYPE_COUNT; ++hairType)
                {
                    for (int j = 0; j < HAIR_MAX_CAPSULE_COUNT; ++j)
                    {
                        params[0].mIndex = SRT_RES_IDX(SrtCapsulesData, PerDraw, gCapsuleData);
                        params[0].ppBuffers = &pUniformBufferCapsuleData[i][hairType][j];
                        updateDescriptorSet(pRenderer, capsuleIndex++, pDescriptorSetCapsuleData, 1, params);
                    }
                }
            }
        }
    }

    void removePBRMaps()
    {
        removeResource(pTextureSpecularMap);
        removeResource(pTextureIrradianceMap);
        removeResource(pTextureSkybox);
        removeResource(pTextureBRDFIntegrationMap);
    }

    void LoadModels()
    {
        gVertexLayoutDefault.mBindingCount = 1;
        gVertexLayoutDefault.mAttribCount = 3;
        gVertexLayoutDefault.mAttribs[0].mSemantic = TF_SEMANTIC_POSITION;
        gVertexLayoutDefault.mAttribs[0].mFormat = TinyImageFormat_R32G32B32_SFLOAT;
        gVertexLayoutDefault.mAttribs[0].mBinding = 0;
        gVertexLayoutDefault.mAttribs[0].mLocation = 0;
        gVertexLayoutDefault.mAttribs[0].mOffset = 0;
        gVertexLayoutDefault.mAttribs[1].mSemantic = TF_SEMANTIC_NORMAL;
        gVertexLayoutDefault.mAttribs[1].mFormat = TinyImageFormat_R32_UINT;
        gVertexLayoutDefault.mAttribs[1].mLocation = 1;
        gVertexLayoutDefault.mAttribs[1].mBinding = 0;
        gVertexLayoutDefault.mAttribs[1].mOffset = 3 * sizeof(float);
        gVertexLayoutDefault.mAttribs[2].mSemantic = TF_SEMANTIC_TEXCOORD0;
        gVertexLayoutDefault.mAttribs[2].mFormat = TinyImageFormat_R32_UINT;
        gVertexLayoutDefault.mAttribs[2].mLocation = 2;
        gVertexLayoutDefault.mAttribs[2].mBinding = 0;
        gVertexLayoutDefault.mAttribs[2].mOffset = 3 * sizeof(float) + sizeof(uint32_t);

        bool modelsAreLoaded = false;
        gLuaManager.SetFunction("LoadModel",
                                [this](ILuaStateWrap* state) -> int
                                {
                                    const char* filename;
                                    state->GetStringArg(1, &filename); // indexing in Lua starts from 1 (NOT 0) !!
                                    size_t len = strlen(filename) + 1;
                                    char*  copy = (char*)tf_malloc(len);
                                    memcpy(copy, filename, len);

                                    ++pStagingData->mModelCount;
                                    pStagingData->mModelList = (char**)tf_realloc(
                                        pStagingData->mModelList, pStagingData->mModelCount * sizeof(*pStagingData->mModelList));
                                    pStagingData->mModelList[pStagingData->mModelCount - 1] = copy;
                                    // this->LoadModel(filename);
                                    return 0; // return amount of arguments that we want to send back to script
                                });

        gLuaManager.AddAsyncScript("loadModels.lua",
                                   [&modelsAreLoaded](ScriptState state)
                                   {
                                       UNREF_PARAM(state);
                                       modelsAreLoaded = true;
                                   });

        while (!modelsAreLoaded) //-V776 //-V712
            threadSleep(0);

        gMeshCount = pStagingData->mModelCount;

        uint32_t meshesSize = gMeshCount * (sizeof(*gMeshes) + sizeof(struct ThreadTaskInfo));
        gMeshes = (TFGeometry**)tf_realloc(gMeshes, meshesSize);
        memset(gMeshes, 0, meshesSize);

        ThreadTaskInfo* info = (ThreadTaskInfo*)(gMeshes + gMeshCount);

        for (uint32_t i = 0; i < gMeshCount; ++i)
        {
            info[i].unitTest = this;
            info[i].index = i;
        }

        if (!isResourceLoaderSingleThreaded())
        {
            threadSystemAddTaskGroup(gThreadSystem, addModel, gMeshCount, info);
        }
        else
        {
            for (uint32_t i = 0; i < gMeshCount; ++i)
            {
                addModel(info + i, 0);
            }
        }
    }

    static void ComputePBRMaps()
    {
        TFShader*   pBRDFIntegrationShader = NULL;
        TFPipeline* pBRDFIntegrationPipeline = NULL;
        TFShader*   pIrradianceShader = NULL;
        TFPipeline* pIrradiancePipeline = NULL;
        TFShader*   pSpecularShader = NULL;
        TFPipeline* pSpecularPipeline = NULL;
        TFSampler*  pSkyboxSampler = NULL;

        TFDescriptorSet* pDescriptorSetPBRPPersistent = NULL;
        TFDescriptorSet* pDescriptorSetPBRPerFrame = NULL;
        TFDescriptorSet* pDescriptorSetPerBatch = NULL;

        static const int skyboxIndex = 0;
        const char*      skyboxNames[] = {
            "LA_Helipad3D.tex",
        };
        // PBR Texture values (these values are mirrored on the shaders).
        static const uint32_t gBRDFIntegrationSize = 512;
        static const uint32_t gSkyboxSize = 1024;
        static const uint32_t gSkyboxMips = (uint)log2(gSkyboxSize) + 1;
        static const uint32_t gIrradianceSize = 32;
        static const uint32_t gSpecularSize = 128;
        static const uint32_t gSpecularMips = 8u; // log2(gSpecularSize) + 1;
        TFTextureDescriptor*  pSpecularMipDescs[gSpecularMips] = {};
        TFTextureDescriptor*  pIrradianceUavDesc = NULL;

        TFSamplerDesc samplerDesc = { TF_FILTER_LINEAR,
                                      TF_FILTER_LINEAR,
                                      TF_MIPMAP_MODE_LINEAR,
                                      TF_ADDRESS_MODE_REPEAT,
                                      TF_ADDRESS_MODE_REPEAT,
                                      TF_ADDRESS_MODE_REPEAT,
                                      0,
                                      false,
                                      0.0f,
                                      0.0f,
                                      16 };
        addSampler(pRenderer, &samplerDesc, &pSkyboxSampler);

        // Uniform buffer for specular root data
        UniformPrecomputeSkySpecularData data[8] = {};
        for (uint32_t i = 0; i < gSpecularMips; i++)
        {
            data[i].roughness = (float)i / (float)(gSpecularMips - 1);
            data[i].mipSize = gSpecularSize >> i;
            TFBufferLoadDesc specularDataBufferDesc = {};
            specularDataBufferDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            specularDataBufferDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
            specularDataBufferDesc.mDesc.mSize = sizeof(UniformPrecomputeSkySpecularData);
            specularDataBufferDesc.mDesc.mFlags = TF_BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
            specularDataBufferDesc.pData = &data[i];
            specularDataBufferDesc.ppBuffer = &pUniformBufferSpecularConfig[i];
            addResource(&specularDataBufferDesc, NULL);
        }

        // Load the skybox panorama texture.
        TFSyncToken       token = {};
        TFTextureLoadDesc skyboxDesc = {};
        skyboxDesc.pFileName = skyboxNames[skyboxIndex];
        skyboxDesc.ppTexture = &pTextureSkybox;
        addResource(&skyboxDesc, &token);

        TFTextureDesc irrImgDesc = {};
        irrImgDesc.mArraySize = 6;
        irrImgDesc.mDepth = 1;
        irrImgDesc.mFormat = TinyImageFormat_R32G32B32A32_SFLOAT;
        irrImgDesc.mHeight = gIrradianceSize;
        irrImgDesc.mWidth = gIrradianceSize;
        irrImgDesc.mMipLevels = 1;
        irrImgDesc.mSampleCount = TF_SAMPLE_COUNT_1;
        irrImgDesc.mStartState = TF_RESOURCE_STATE_UNORDERED_ACCESS;
        irrImgDesc.mDescriptors = TF_DESCRIPTOR_TYPE_TEXTURE_CUBE | TF_DESCRIPTOR_TYPE_RW_TEXTURE;
        irrImgDesc.pName = "irrImgBuff";

        TFTextureLoadDesc irrLoadDesc = {};
        irrLoadDesc.pDesc = &irrImgDesc;
        irrLoadDesc.ppTexture = &pTextureIrradianceMap;
        addResource(&irrLoadDesc, &token);

        TFTextureDesc specImgDesc = {};
        specImgDesc.mArraySize = 6;
        specImgDesc.mDepth = 1;
        specImgDesc.mFormat = TinyImageFormat_R32G32B32A32_SFLOAT;
        specImgDesc.mHeight = gSpecularSize;
        specImgDesc.mWidth = gSpecularSize;
        specImgDesc.mMipLevels = gSpecularMips;
        specImgDesc.mSampleCount = TF_SAMPLE_COUNT_1;
        specImgDesc.mStartState = TF_RESOURCE_STATE_UNORDERED_ACCESS;
        specImgDesc.mDescriptors = TF_DESCRIPTOR_TYPE_TEXTURE_CUBE | TF_DESCRIPTOR_TYPE_RW_TEXTURE;
        specImgDesc.pName = "specImgBuff";

        TFTextureLoadDesc specImgLoadDesc = {};
        specImgLoadDesc.pDesc = &specImgDesc;
        specImgLoadDesc.ppTexture = &pTextureSpecularMap;
        addResource(&specImgLoadDesc, &token);

        // Create empty texture for BRDF integration map.
        TFTextureLoadDesc brdfIntegrationLoadDesc = {};
        TFTextureDesc     brdfIntegrationDesc = {};
        brdfIntegrationDesc.mWidth = gBRDFIntegrationSize;
        brdfIntegrationDesc.mHeight = gBRDFIntegrationSize;
        brdfIntegrationDesc.mDepth = 1;
        brdfIntegrationDesc.mArraySize = 1;
        brdfIntegrationDesc.mMipLevels = 1;
        brdfIntegrationDesc.mFormat = TinyImageFormat_R32G32_SFLOAT;
        brdfIntegrationDesc.mStartState = TF_RESOURCE_STATE_UNORDERED_ACCESS;
        brdfIntegrationDesc.mDescriptors = TF_DESCRIPTOR_TYPE_TEXTURE | TF_DESCRIPTOR_TYPE_RW_TEXTURE;
        brdfIntegrationDesc.mSampleCount = TF_SAMPLE_COUNT_1;
        brdfIntegrationLoadDesc.pDesc = &brdfIntegrationDesc;
        brdfIntegrationLoadDesc.ppTexture = &pTextureBRDFIntegrationMap;
        addResource(&brdfIntegrationLoadDesc, &token);

        TFGPUPresetLevel presetLevel = pRenderer->pGpu->mGpuVendorPreset.mPresetLevel;

        const char* brdfIntegrationShaders[TFGPUPresetLevel::TF_GPU_PRESET_COUNT] = {
            "BRDFIntegration_SAMPLES_0.comp",   // TF_GPU_PRESET_NONE
            "BRDFIntegration_SAMPLES_0.comp",   // TF_GPU_PRESET_OFFICE
            "BRDFIntegration_SAMPLES_32.comp",  // TF_GPU_PRESET_VERYLOW
            "BRDFIntegration_SAMPLES_64.comp",  // TF_GPU_PRESET_LOW
            "BRDFIntegration_SAMPLES_128.comp", // TF_GPU_PRESET_MEDIUM
            "BRDFIntegration_SAMPLES_256.comp", // TF_GPU_PRESET_HIGH
            "BRDFIntegration_SAMPLES_1024.comp" // TF_GPU_PRESET_ULTRA
        };

        const char* irradianceShaders[TFGPUPresetLevel::TF_GPU_PRESET_COUNT] = {
            "computeIrradianceMap_SAMPLE_DELTA_05.comp",   // TF_GPU_PRESET_NONE
            "computeIrradianceMap_SAMPLE_DELTA_05.comp",   // TF_GPU_PRESET_OFFICE
            "computeIrradianceMap_SAMPLE_DELTA_05.comp",   // TF_GPU_PRESET_VERYLOW
            "computeIrradianceMap_SAMPLE_DELTA_025.comp",  // TF_GPU_PRESET_LOW
            "computeIrradianceMap_SAMPLE_DELTA_0125.comp", // TF_GPU_PRESET_MEDIUM
            "computeIrradianceMap_SAMPLE_DELTA_005.comp",  // TF_GPU_PRESET_HIGH
            "computeIrradianceMap_SAMPLE_DELTA_0025.comp"  // TF_GPU_PRESET_ULTRA
        };

        const char* specularShaders[TFGPUPresetLevel::TF_GPU_PRESET_COUNT] = {
            "computeSpecularMap_SAMPLES_0.comp",   // TF_GPU_PRESET_NONE
            "computeSpecularMap_SAMPLES_0.comp",   // TF_GPU_PRESET_OFFICE
            "computeSpecularMap_SAMPLES_32.comp",  // TF_GPU_PRESET_VERYLOW
            "computeSpecularMap_SAMPLES_64.comp",  // TF_GPU_PRESET_LOW
            "computeSpecularMap_SAMPLES_128.comp", // TF_GPU_PRESET_MEDIUM
            "computeSpecularMap_SAMPLES_256.comp", // TF_GPU_PRESET_HIGH
            "computeSpecularMap_SAMPLES_1024.comp" // TF_GPU_PRESET_ULTRA
        };

        TFShaderLoadDesc brdfIntegrationShaderDesc = {};
        brdfIntegrationShaderDesc.mComp.pFileName = brdfIntegrationShaders[presetLevel];

        TFShaderLoadDesc irradianceShaderDesc = {};
        irradianceShaderDesc.mComp.pFileName = irradianceShaders[presetLevel];

        TFShaderLoadDesc specularShaderDesc = {};
        specularShaderDesc.mComp.pFileName = specularShaders[presetLevel];

        addShader(pRenderer, &irradianceShaderDesc, &pIrradianceShader);
        addShader(pRenderer, &specularShaderDesc, &pSpecularShader);
        addShader(pRenderer, &brdfIntegrationShaderDesc, &pBRDFIntegrationShader);
        waitForToken(&token);
        TFDescriptorSetDesc setDesc = SRT_SET_DESC(SrtComputeData, Persistent, 1, 0);
        addDescriptorSet(pRenderer, &setDesc, &pDescriptorSetPBRPPersistent);
        setDesc = SRT_SET_DESC(SrtComputeData, PerFrame, gSkyboxMips, 0);
        addDescriptorSet(pRenderer, &setDesc, &pDescriptorSetPBRPerFrame);
        setDesc = SRT_SET_DESC(SrtComputeData, PerBatch, gSkyboxMips, 0);
        addDescriptorSet(pRenderer, &setDesc, &pDescriptorSetPerBatch);
        TFDescriptorData params[2] = {};
        params[0].mIndex = SRT_RES_IDX(SrtComputeData, Persistent, gSrcTexture);
        params[0].ppTextures = &pTextureSkybox;
        updateDescriptorSet(pRenderer, 0, pDescriptorSetPBRPPersistent, 1, params);
        TFTextureDescriptorDesc irrUavDesc = {};
        irrUavDesc.pTexture = pTextureIrradianceMap;
        irrUavDesc.mDescriptors = TF_DESCRIPTOR_TYPE_RW_TEXTURE;
        addTextureDescriptor(pRenderer, &irrUavDesc, &pIrradianceUavDesc);
        params[0].mIndex = SRT_RES_IDX(SrtComputeData, PerBatch, gDestTextureIrradiance);
        params[0].ppTextureDescriptors = &pIrradianceUavDesc;
        params[0].mUseTextureDescriptors = 1;
        params[1].mIndex = SRT_RES_IDX(SrtComputeData, PerBatch, gDestTextureBRDF);
        params[1].ppTextures = &pTextureBRDFIntegrationMap;
        updateDescriptorSet(pRenderer, 0, pDescriptorSetPerBatch, 2, params);

        for (uint32_t i = 0; i < gSpecularMips; i++)
        {
            params[0] = {};
            params[0].mIndex = SRT_RES_IDX(SrtComputeData, PerFrame, gSpecularConfig);
            params[0].ppBuffers = &pUniformBufferSpecularConfig[i];
            updateDescriptorSet(pRenderer, i, pDescriptorSetPBRPerFrame, 1, params);

            TFTextureDescriptorDesc mipDesc = {};
            mipDesc.pTexture = pTextureSpecularMap;
            mipDesc.mDescriptors = TF_DESCRIPTOR_TYPE_RW_TEXTURE;
            mipDesc.mBaseMipLevel = i;
            mipDesc.mMipLevelCount = 1;
            addTextureDescriptor(pRenderer, &mipDesc, &pSpecularMipDescs[i]);

            params[0].mIndex = SRT_RES_IDX(SrtComputeData, PerBatch, gDestTextureSpecular);
            params[0].ppTextureDescriptors = &pSpecularMipDescs[i];
            params[0].mUseTextureDescriptors = 1;
            updateDescriptorSet(pRenderer, i, pDescriptorSetPerBatch, 1, params);
        }

        TFPipelineDesc desc = {};
        PIPELINE_LAYOUT_DESC(desc, SRT_LAYOUT_DESC(SrtComputeData, Persistent), SRT_LAYOUT_DESC(SrtComputeData, PerFrame),
                             SRT_LAYOUT_DESC(SrtComputeData, PerBatch), NULL);
        desc.mType = TF_PIPELINE_TYPE_COMPUTE;
        TFComputePipelineDesc& pipelineSettings = desc.mComputeDesc;
        pipelineSettings.pShaderProgram = pIrradianceShader;
        addPipeline(pRenderer, &desc, &pIrradiancePipeline);
        pipelineSettings.pShaderProgram = pSpecularShader;
        addPipeline(pRenderer, &desc, &pSpecularPipeline);
        pipelineSettings.pShaderProgram = pBRDFIntegrationShader;
        addPipeline(pRenderer, &desc, &pBRDFIntegrationPipeline);

        GpuCmdRingElement elem = getNextGpuCmdRingElement(&gGraphicsCmdRing, true, 1);
        TFCmd*            pCmd = elem.pCmds[0];

        // Compute the BRDF Integration map.
        resetCmdPool(pRenderer, elem.pCmdPool);
        beginCmd(pCmd);
        cmdBindPipeline(pCmd, pBRDFIntegrationPipeline);
        cmdBindDescriptorSet(pCmd, 0, pDescriptorSetPBRPPersistent);
        cmdBindDescriptorSet(pCmd, 0, pDescriptorSetPerBatch);
        const uint32_t* pThreadGroupSize = pBRDFIntegrationShader->mNumThreadsPerGroup;
        cmdDispatch(pCmd, gBRDFIntegrationSize / pThreadGroupSize[0], gBRDFIntegrationSize / pThreadGroupSize[1], pThreadGroupSize[2]);

        TFTextureBarrier srvBarrier[1] = { { pTextureBRDFIntegrationMap, TF_RESOURCE_STATE_UNORDERED_ACCESS,
                                             TF_RESOURCE_STATE_SHADER_RESOURCE } };

        cmdResourceBarrier(pCmd, 0, NULL, 1, srvBarrier, 0, NULL);

        /************************************************************************/
        // Compute sky irradiance
        /************************************************************************/

        cmdBindPipeline(pCmd, pIrradiancePipeline);
        cmdBindDescriptorSet(pCmd, 0, pDescriptorSetPBRPPersistent);
        pThreadGroupSize = pIrradianceShader->mNumThreadsPerGroup;
        cmdDispatch(pCmd, gIrradianceSize / pThreadGroupSize[0], gIrradianceSize / pThreadGroupSize[1], 6);
        /************************************************************************/
        // Compute specular sky
        /************************************************************************/

        cmdBindPipeline(pCmd, pSpecularPipeline);
        cmdBindDescriptorSet(pCmd, 0, pDescriptorSetPBRPPersistent);
        for (uint32_t i = 0; i < gSpecularMips; i++)
        {
            cmdBindDescriptorSet(pCmd, i, pDescriptorSetPBRPerFrame);
            cmdBindDescriptorSet(pCmd, i, pDescriptorSetPerBatch);
            pThreadGroupSize = pIrradianceShader->mNumThreadsPerGroup;
            cmdDispatch(pCmd, max(1u, (gSpecularSize >> i) / pThreadGroupSize[0]), max(1u, (gSpecularSize >> i) / pThreadGroupSize[1]), 6);
        }
        /************************************************************************/
        /************************************************************************/
        TFTextureBarrier srvBarriers2[2] = {
            { pTextureIrradianceMap, TF_RESOURCE_STATE_UNORDERED_ACCESS, TF_RESOURCE_STATE_SHADER_RESOURCE },
            { pTextureSpecularMap, TF_RESOURCE_STATE_UNORDERED_ACCESS, TF_RESOURCE_STATE_SHADER_RESOURCE }
        };
        cmdResourceBarrier(pCmd, 0, NULL, 2, srvBarriers2, 0, NULL);

        endCmd(pCmd);

        FlushResourceUpdateDesc flushDesc = {};
        flushResourceUpdates(&flushDesc);
        waitForFences(pRenderer, 1, &flushDesc.pOutFence);

        TFQueueSubmitDesc submitDesc = {};
        submitDesc.mCmdCount = 1;
        submitDesc.ppCmds = &pCmd;
        submitDesc.pSignalFence = elem.pFence;
        submitDesc.mSubmitDone = true;
        queueSubmit(pGraphicsQueue, &submitDesc);
        waitQueueIdle(pGraphicsQueue);

        removeDescriptorSet(pRenderer, pDescriptorSetPerBatch);
        removeTextureDescriptor(pRenderer, pIrradianceUavDesc);
        for (uint32_t i = 0; i < gSpecularMips; i++)
            removeTextureDescriptor(pRenderer, pSpecularMipDescs[i]);
        removeDescriptorSet(pRenderer, pDescriptorSetPBRPerFrame);
        removeDescriptorSet(pRenderer, pDescriptorSetPBRPPersistent);

        removePipeline(pRenderer, pSpecularPipeline);
        removeShader(pRenderer, pSpecularShader);
        removePipeline(pRenderer, pIrradiancePipeline);
        removeShader(pRenderer, pIrradianceShader);

        removePipeline(pRenderer, pBRDFIntegrationPipeline);
        removeShader(pRenderer, pBRDFIntegrationShader);
        removeSampler(pRenderer, pSkyboxSampler);

        for (uint32_t i = 0; i < gSpecularMips; i++)
        {
            removeResource(pUniformBufferSpecularConfig[i]);
        }
    }

    static void addModel(void* user, uint64_t)
    {
        ThreadTaskInfo* info = (ThreadTaskInfo*)user;

        TFGeometryLoadDesc loadDesc = {};
        loadDesc.pFileName = info->unitTest->pStagingData->mModelList[info->index];
        loadDesc.ppGeometry = &gMeshes[info->index];
        loadDesc.pVertexLayout = &gVertexLayoutDefault;
        addResource(&loadDesc, NULL);
    }

    void removeModels()
    {
        for (size_t i = 0; i < gMeshCount; ++i)
            removeResource(gMeshes[i]);
        gMeshCount = 0;
        tf_free(gMeshes);
        gMeshes = NULL;
    }

    void addResources()
    {
        // Generate skybox vertex buffer
        float skyBoxPoints[] = {
            0.5f,  -0.5f, -0.5f, 1.0f, // -z
            -0.5f, -0.5f, -0.5f, 1.0f,  -0.5f, 0.5f,  -0.5f, 1.0f,  -0.5f, 0.5f,
            -0.5f, 1.0f,  0.5f,  0.5f,  -0.5f, 1.0f,  0.5f,  -0.5f, -0.5f, 1.0f,

            -0.5f, -0.5f, 0.5f,  1.0f, //-x
            -0.5f, -0.5f, -0.5f, 1.0f,  -0.5f, 0.5f,  -0.5f, 1.0f,  -0.5f, 0.5f,
            -0.5f, 1.0f,  -0.5f, 0.5f,  0.5f,  1.0f,  -0.5f, -0.5f, 0.5f,  1.0f,

            0.5f,  -0.5f, -0.5f, 1.0f, //+x
            0.5f,  -0.5f, 0.5f,  1.0f,  0.5f,  0.5f,  0.5f,  1.0f,  0.5f,  0.5f,
            0.5f,  1.0f,  0.5f,  0.5f,  -0.5f, 1.0f,  0.5f,  -0.5f, -0.5f, 1.0f,

            -0.5f, -0.5f, 0.5f,  1.0f, // +z
            -0.5f, 0.5f,  0.5f,  1.0f,  0.5f,  0.5f,  0.5f,  1.0f,  0.5f,  0.5f,
            0.5f,  1.0f,  0.5f,  -0.5f, 0.5f,  1.0f,  -0.5f, -0.5f, 0.5f,  1.0f,

            -0.5f, 0.5f,  -0.5f, 1.0f, //+y
            0.5f,  0.5f,  -0.5f, 1.0f,  0.5f,  0.5f,  0.5f,  1.0f,  0.5f,  0.5f,
            0.5f,  1.0f,  -0.5f, 0.5f,  0.5f,  1.0f,  -0.5f, 0.5f,  -0.5f, 1.0f,

            0.5f,  -0.5f, 0.5f,  1.0f, //-y
            0.5f,  -0.5f, -0.5f, 1.0f,  -0.5f, -0.5f, -0.5f, 1.0f,  -0.5f, -0.5f,
            -0.5f, 1.0f,  -0.5f, -0.5f, 0.5f,  1.0f,  0.5f,  -0.5f, 0.5f,  1.0f,
        };

        uint64_t         skyBoxDataSize = 4 * 6 * 6 * sizeof(float);
        TFBufferLoadDesc skyboxVbDesc = {};
        skyboxVbDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_VERTEX_BUFFER;
        skyboxVbDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_GPU_ONLY;
        skyboxVbDesc.mDesc.mSize = skyBoxDataSize;
        skyboxVbDesc.mDesc.mStartState = TF_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
        skyboxVbDesc.pData = skyBoxPoints;
        skyboxVbDesc.ppBuffer = &pVertexBufferSkybox;
        addResource(&skyboxVbDesc, NULL);

        // Load hair models
        gUniformDataHairGlobal.mGravity = float4(0.0f, -9.81f, 0.0f, 0.0f);
        gUniformDataHairGlobal.mWind = float4(0.0f);

        NamedCapsule headCapsule = {};
        headCapsule.mName = "Head";
        headCapsule.mCapsule.mCenter0 = float3(-0.41f, 0.0f, 0.0f);
        headCapsule.mCapsule.mRadius0 = 0.5f;
        headCapsule.mCapsule.mCenter1 = float3(-0.83f, 0.0f, 0.0f);
        headCapsule.mCapsule.mRadius1 = 0.5f;
        headCapsule.mAttachedBone = gAnimationRig.FindJoint(gHeadAttachmentJointName);
        gCapsules[0] = headCapsule;

#if HAIR_MAX_CAPSULE_COUNT >= 3
        NamedCapsule leftShoulderCapsule = {};
        leftShoulderCapsule.mName = "LeftShoulder";
        leftShoulderCapsule.mCapsule.mCenter0 = float3(-0.17f, 0.0f, 0.0f);
        leftShoulderCapsule.mCapsule.mRadius0 = 0.25f;
        leftShoulderCapsule.mCapsule.mCenter1 = float3(0.74f, 0.0f, 0.0f);
        leftShoulderCapsule.mCapsule.mRadius1 = 0.21f;
        leftShoulderCapsule.mAttachedBone = gAnimationRig.FindJoint(gLeftShoulderJointName);
        gCapsules[1] = leftShoulderCapsule;

        NamedCapsule rightShoulderCapsule = {};
        rightShoulderCapsule.mName = "RightShoulder";
        rightShoulderCapsule.mCapsule.mCenter0 = float3(-0.17f, 0.0f, 0.0f);
        rightShoulderCapsule.mCapsule.mRadius0 = 0.25f;
        rightShoulderCapsule.mCapsule.mCenter1 = float3(0.74f, 0.0f, 0.0f);
        rightShoulderCapsule.mCapsule.mRadius1 = 0.21f;
        rightShoulderCapsule.mAttachedBone = gAnimationRig.FindJoint(gRightShoulderJointName);
        gCapsules[2] = rightShoulderCapsule;
#endif

        NamedTransform headTransform = {};
        headTransform.mName = "Head";
        headTransform.mTransform.mPosition = vec3(0.0f, -2.2f, 0.0f);
        headTransform.mTransform.mOrientation = vec3(0.0f, PI * 0.5f, -PI * 0.5f);
        headTransform.mTransform.mScale = 0.02f;
        headTransform.mAttachedBone = gAnimationRig.FindJoint(gHeadAttachmentJointName);
        gTransforms[0] = headTransform;

        float gpuPresetScore =
            (float)((uint)gGPUPresetLevel - TF_GPU_PRESET_VERYLOW) / (float)(TF_GPU_PRESET_ULTRA - TF_GPU_PRESET_VERYLOW);

        // Load all hair meshes
        HairSectionShadingParameters ponytailHairShadingParameters = {};
        ponytailHairShadingParameters.mColorBias = 10.0f;
        ponytailHairShadingParameters.mStrandRadius = 0.04f * gpuPresetScore + 0.21f * (1.0f - gpuPresetScore);
        ponytailHairShadingParameters.mStrandSpacing = 0.5f * gpuPresetScore + 0.2f * (1.0f - gpuPresetScore);
        ponytailHairShadingParameters.mDisableRootColor = true;

        HairSectionShadingParameters hairShadingParameters = {};
        hairShadingParameters.mColorBias = 5.0f;
        hairShadingParameters.mStrandRadius = 0.04f * gpuPresetScore + 0.21f * (1.0f - gpuPresetScore);
        hairShadingParameters.mStrandSpacing = 0.5f * gpuPresetScore + 0.2f * (1.0f - gpuPresetScore);
        hairShadingParameters.mDisableRootColor = false;

        HairSimulationParameters ponytailHairSimulationParameters = {};
        ponytailHairSimulationParameters.mDamping = 0.04f;
        ponytailHairSimulationParameters.mGlobalConstraintStiffness = 0.06f;
        ponytailHairSimulationParameters.mGlobalConstraintRange = 0.55f;
        ponytailHairSimulationParameters.mShockPropagationStrength = 0.0f;
        ponytailHairSimulationParameters.mShockPropagationAccelerationThreshold = 10.0f;
        ponytailHairSimulationParameters.mLocalConstraintStiffness = 0.04f;
        ponytailHairSimulationParameters.mLocalConstraintIterations = 2;
        ponytailHairSimulationParameters.mLengthConstraintIterations = 2;
        ponytailHairSimulationParameters.mTipSeperationFactor = 2.0f;
        ponytailHairSimulationParameters.mCapsuleCount = 1;
        ponytailHairSimulationParameters.mCapsules[0] = 0;
#if HAIR_MAX_CAPSULE_COUNT >= 3
        ponytailHairSimulationParameters.mCapsuleCount = 3;
        ponytailHairSimulationParameters.mCapsules[1] = 1;
        ponytailHairSimulationParameters.mCapsules[2] = 2;
#endif

        HairSimulationParameters hairSimulationParameters = {};
        hairSimulationParameters.mDamping = 0.1f;
        hairSimulationParameters.mGlobalConstraintStiffness = 0.06f;
        hairSimulationParameters.mGlobalConstraintRange = 0.55f;
        hairSimulationParameters.mShockPropagationStrength = 0.0f;
        hairSimulationParameters.mShockPropagationAccelerationThreshold = 10.0f;
        hairSimulationParameters.mLocalConstraintStiffness = 0.26f;
        hairSimulationParameters.mLocalConstraintIterations = 2;
        hairSimulationParameters.mLengthConstraintIterations = 2;
        hairSimulationParameters.mTipSeperationFactor = 2.0f;
        hairSimulationParameters.mCapsuleCount = 1;
        hairSimulationParameters.mCapsules[0] = 0;
#if HAIR_MAX_CAPSULE_COUNT >= 3
        hairSimulationParameters.mCapsuleCount = 3;
        hairSimulationParameters.mCapsules[1] = 1;
        hairSimulationParameters.mCapsules[2] = 2;
#endif

        HairSimulationParameters stiffHairSimulationParameters = hairSimulationParameters;
        stiffHairSimulationParameters.mGlobalConstraintRange = 0.8f;
        stiffHairSimulationParameters.mLocalConstraintStiffness = 0.7f;

        HairSimulationParameters staticHairSimulationParameters = hairSimulationParameters;
        staticHairSimulationParameters.mGlobalConstraintRange = 1.0f;
        staticHairSimulationParameters.mGlobalConstraintStiffness = 1.0f;

        for (uint32_t i = 0; i < HAIR_TYPE_COUNT; ++i)
        {
            gHairTypeIndicesCount[i] = 0;
            tf_free(gHairTypeIndices[i]);
            gHairTypeIndices[i] = NULL;
        }

        addHairMesh(HAIR_TYPE_PONYTAIL, "ponytail", "Hair/tail.bin", (uint)(5 * gpuPresetScore), 0.5f, 0, &ponytailHairShadingParameters,
                    &ponytailHairSimulationParameters);
        addHairMesh(HAIR_TYPE_PONYTAIL, "top", "Hair/front_top.bin", (uint)(5 * gpuPresetScore), 0.5f, 0, &hairShadingParameters,
                    &stiffHairSimulationParameters);
        addHairMesh(HAIR_TYPE_PONYTAIL, "side", "Hair/side.bin", (uint)(5 * gpuPresetScore), 0.5f, 0, &hairShadingParameters,
                    &stiffHairSimulationParameters);
        addHairMesh(HAIR_TYPE_PONYTAIL, "back", "Hair/back.bin", (uint)(5 * gpuPresetScore), 0.5f, 0, &hairShadingParameters,
                    &staticHairSimulationParameters);
        addHairMesh(HAIR_TYPE_FEMALE_1, "Female hair 1", "Hair/female_hair_1.bin", (uint)(5 * gpuPresetScore), 0.5f, 0,
                    &hairShadingParameters, &hairSimulationParameters);
        addHairMesh(HAIR_TYPE_FEMALE_2, "Female hair 2", "Hair/female_hair_2.bin", (uint)(5 * gpuPresetScore), 0.5f, 0,
                    &hairShadingParameters, &hairSimulationParameters);
        addHairMesh(HAIR_TYPE_FEMALE_3, "Female hair 3", "Hair/female_hair_3.bin", (uint)(5 * gpuPresetScore), 0.5f, 0,
                    &hairShadingParameters, &stiffHairSimulationParameters);
        addHairMesh(HAIR_TYPE_FEMALE_6, "female hair 6 top", "Hair/female_hair_6_top.bin", (uint)(5 * gpuPresetScore), 0.5f, 0,
                    &hairShadingParameters, &staticHairSimulationParameters);
        addHairMesh(HAIR_TYPE_FEMALE_6, "female hair 6 tail", "Hair/female_hair_6_tail.bin", (uint)(5 * gpuPresetScore), 0.5f, 0,
                    &ponytailHairShadingParameters, &ponytailHairSimulationParameters);

        // Create skeleton buffers
        const float boneWidthRatio = 0.2f;               // Determines how far along the bone to put the max width [0,1]
        const float jointRadius = boneWidthRatio * 0.5f; // set to replicate Ozz skeleton

        // Generate joint vertex buffer
        gVertexCountSkeletonJoint = 0;
        generateQuad(NULL, &gVertexCountSkeletonJoint, jointRadius);
        pStagingData->pJointPoints = (float*)tf_malloc(sizeof(float) * gVertexCountSkeletonJoint);
        generateQuad(pStagingData->pJointPoints, &gVertexCountSkeletonJoint, jointRadius);

        uint64_t         jointDataSize = gVertexCountSkeletonJoint * sizeof(float);
        TFBufferLoadDesc jointVbDesc = {};
        jointVbDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_VERTEX_BUFFER;
        jointVbDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_GPU_ONLY;
        jointVbDesc.mDesc.mSize = jointDataSize;
        jointVbDesc.mDesc.mStartState = TF_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
        jointVbDesc.pData = pStagingData->pJointPoints;
        jointVbDesc.ppBuffer = &pVertexBufferSkeletonJoint;
        addResource(&jointVbDesc, NULL);

        // Generate bone vertex buffer
        gVertexCountSkeletonBone = 0;
        generateIndexedBonePoints(NULL, &gVertexCountSkeletonBone, boneWidthRatio, gAnimationRig.mNumJoints,
                                  &gAnimationRig.mSkeleton.joint_parents()[0]);
        pStagingData->pBonePoints = (float*)tf_malloc(sizeof(float) * gVertexCountSkeletonBone);
        generateIndexedBonePoints(pStagingData->pBonePoints, &gVertexCountSkeletonBone, boneWidthRatio, gAnimationRig.mNumJoints,
                                  &gAnimationRig.mSkeleton.joint_parents()[0]);

        uint64_t         boneDataSize = gVertexCountSkeletonBone * sizeof(float);
        TFBufferLoadDesc boneVbDesc = {};
        boneVbDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_VERTEX_BUFFER;
        boneVbDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_GPU_ONLY;
        boneVbDesc.mDesc.mSize = boneDataSize;
        boneVbDesc.mDesc.mStartState = TF_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
        boneVbDesc.pData = pStagingData->pBonePoints;
        boneVbDesc.ppBuffer = &pVertexBufferSkeletonBone;
        addResource(&boneVbDesc, NULL);

        waitForAllResourceLoads();
    }

    void removeResources()
    {
        removeResource(pVertexBufferSkybox);
        removeHairMeshes();
        removeResource(pVertexBufferSkeletonJoint);
        removeResource(pVertexBufferSkeletonBone);
    }

    void addUniformBuffers()
    {
        // Ground plane uniform buffer
        TFBufferLoadDesc surfaceUBDesc = {};
        surfaceUBDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        surfaceUBDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
        surfaceUBDesc.mDesc.mSize = sizeof(UniformObjData);
        surfaceUBDesc.mDesc.mFlags = TF_BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
        surfaceUBDesc.pData = NULL;
        surfaceUBDesc.ppBuffer = &pUniformBufferGroundPlane;
        addResource(&surfaceUBDesc, NULL);

        // Nameplate uniform buffers
        TFBufferLoadDesc nameplateUBDesc = {};
        nameplateUBDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        nameplateUBDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
        nameplateUBDesc.mDesc.mSize = sizeof(UniformObjData);
        nameplateUBDesc.mDesc.mFlags = TF_BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
        nameplateUBDesc.pData = NULL;
        for (uint32_t i = 0; i < MATERIAL_INSTANCE_COUNT; ++i)
        {
            nameplateUBDesc.ppBuffer = &pUniformBufferNamePlates[i];
            addResource(&nameplateUBDesc, NULL);
        }

        // Create a uniform buffer per mat ball
        for (uint32_t frameIdx = 0; frameIdx < gDataBufferCount; ++frameIdx)
        {
            for (uint32_t i = 0; i < MATERIAL_INSTANCE_COUNT; ++i)
            {
                TFBufferLoadDesc matBallUBDesc = {};
                matBallUBDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                matBallUBDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
                matBallUBDesc.mDesc.mSize = sizeof(UniformObjData);
                matBallUBDesc.mDesc.mFlags = TF_BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
                matBallUBDesc.pData = NULL;
                matBallUBDesc.ppBuffer = &pUniformBufferMatBall[frameIdx][i];
                addResource(&matBallUBDesc, NULL);
            }
        }

        // Uniform buffer for camera data
        TFBufferLoadDesc cameraUBDesc = {};
        cameraUBDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        cameraUBDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
        cameraUBDesc.mDesc.mSize = sizeof(UniformCamData);
        cameraUBDesc.mDesc.mFlags = TF_BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
        cameraUBDesc.pData = NULL;
        for (uint i = 0; i < gDataBufferCount; ++i)
        {
            cameraUBDesc.ppBuffer = &pUniformBufferCamera[i];
            addResource(&cameraUBDesc, NULL);
            cameraUBDesc.ppBuffer = &pUniformBufferCameraSkybox[i];
            addResource(&cameraUBDesc, NULL);
            cameraUBDesc.ppBuffer = &pUniformBufferCameraShadowPass[i];
            addResource(&cameraUBDesc, NULL);

            for (uint hairType = 0; hairType < HAIR_TYPE_COUNT; ++hairType)
            {
                for (int j = 0; j < MAX_NUM_DIRECTIONAL_LIGHTS; ++j)
                {
                    cameraUBDesc.ppBuffer = &pUniformBufferCameraHairShadows[i][hairType][j];
                    addResource(&cameraUBDesc, NULL);
                }
            }
        }

        // Uniform buffer for capsules data
        TFBufferLoadDesc capsulesDesc = {};
        capsulesDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        capsulesDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
        capsulesDesc.mDesc.mSize = sizeof(Capsule);
        capsulesDesc.mDesc.mFlags = TF_BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
        capsulesDesc.pData = NULL;
        for (uint i = 0; i < gDataBufferCount; ++i)
        {
            for (uint hairType = 0; hairType < HAIR_TYPE_COUNT; ++hairType)
            {
                for (int j = 0; j < HAIR_MAX_CAPSULE_COUNT; ++j)
                {
                    capsulesDesc.ppBuffer = &pUniformBufferCapsuleData[i][hairType][j];
                    addResource(&capsulesDesc, NULL);
                }
            }
        }

        // Uniform buffer for directional light data
        TFBufferLoadDesc directionalLightBufferDesc = {};
        directionalLightBufferDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        directionalLightBufferDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
        directionalLightBufferDesc.mDesc.mSize = sizeof(UniformDataDirectionalLights);
        directionalLightBufferDesc.mDesc.mFlags = TF_BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
        directionalLightBufferDesc.pData = NULL;
        for (uint i = 0; i < gDataBufferCount; ++i)
        {
            directionalLightBufferDesc.ppBuffer = &pUniformBufferDirectionalLights[i];
            addResource(&directionalLightBufferDesc, NULL);
        }

        // Uniform buffer for light data
        TFBufferLoadDesc lightsUBDesc = {};
        lightsUBDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        lightsUBDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
        lightsUBDesc.mDesc.mSize = sizeof(UniformDataPointLights);
        lightsUBDesc.mDesc.mFlags = TF_BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
        lightsUBDesc.pData = NULL;
        lightsUBDesc.ppBuffer = &pUniformBufferPointLights;
        addResource(&lightsUBDesc, NULL);

        // Uniform buffer for hair data
        TFBufferLoadDesc hairGlobalBufferDesc = {};
        hairGlobalBufferDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        hairGlobalBufferDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
        hairGlobalBufferDesc.mDesc.mSize = sizeof(UniformDataHairGlobal);
        hairGlobalBufferDesc.mDesc.mFlags = TF_BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
        hairGlobalBufferDesc.pData = NULL;
        hairGlobalBufferDesc.ppBuffer = &pUniformBufferHairGlobal;
        addResource(&hairGlobalBufferDesc, NULL);
    }

    void removeUniformBuffers()
    {
        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            removeResource(pUniformBufferCameraSkybox[i]);
            removeResource(pUniformBufferCamera[i]);
            removeResource(pUniformBufferCameraShadowPass[i]);
            removeResource(pUniformBufferDirectionalLights[i]);
            for (uint32_t hairType = 0; hairType < HAIR_TYPE_COUNT; ++hairType)
            {
                for (uint32_t j = 0; j < MAX_NUM_DIRECTIONAL_LIGHTS; ++j)
                    removeResource(pUniformBufferCameraHairShadows[i][hairType][j]);

                for (uint32_t j = 0; j < HAIR_MAX_CAPSULE_COUNT; ++j)
                    removeResource(pUniformBufferCapsuleData[i][hairType][j]);
            }
            for (uint32_t j = 0; j < MATERIAL_INSTANCE_COUNT; ++j)
                removeResource(pUniformBufferMatBall[i][j]);
        }

        removeResource(pUniformBufferGroundPlane);
        for (uint32_t j = 0; j < MATERIAL_INSTANCE_COUNT; ++j)
            removeResource(pUniformBufferNamePlates[j]);

        removeResource(pUniformBufferPointLights);

        removeResource(pUniformBufferHairGlobal);
    }

    void InitializeUniformBuffers()
    {
        // Update the uniform buffer for the objects
        float baseX = 22.0f;
        float baseY = -1.8f;
        float baseZ = 12.0f;
        float offsetX = 8.0f;
        float scaleVal = 4.0f;
        float roughDelta = 1.0f;
        float materialPlateOffset = 4.0f;

        for (uint32_t i = 0; i < MATERIAL_INSTANCE_COUNT; ++i)
        {
            mat4 modelmat =
                mat4::translation(vec3(baseX - i - offsetX * i, baseY, baseZ)) * mat4::scale(vec3(scaleVal)) * mat4::rotationY(PI);

            gUniformDataMatBall[i] = {};
            gUniformDataMatBall[i].mWorldMat = modelmat;
            gUniformDataMatBall[i].mMetallic = i / (float)MATERIAL_INSTANCE_COUNT;
            gUniformDataMatBall[i].mRoughness = 0.04f + roughDelta;
            gUniformDataMatBall[i].textureConfig = ETextureConfigFlags::TEXTURE_CONFIG_FLAGS_ALL;
            // if not enough materials specified then set pbrMaterials to -1

            TFBufferUpdateDesc objBuffUpdateDesc = { pUniformBufferMatBall[mSettings.mFrameIdx][i] };
            beginUpdateResource(&objBuffUpdateDesc);
            memcpy(objBuffUpdateDesc.pMappedData, &gUniformDataMatBall[i], sizeof(gUniformDataMatBall[i]));
            endUpdateResource(&objBuffUpdateDesc);
            roughDelta -= .25f;

            {
                // plates
                modelmat = mat4::translation(vec3(baseX - i - offsetX * i, -5.8f, baseZ + materialPlateOffset)) *
                           mat4::rotationX(3.1415f * 0.2f) * mat4::scale(vec3(3.0f, 0.1f, 1.0f));
                UniformObjData plateUniform = {};
                plateUniform.mWorldMat = modelmat;
                plateUniform.mMetallic = 1.0f;
                plateUniform.mRoughness = 0.4f;
                plateUniform.mAlbedo = float3(0.04f);
                plateUniform.textureConfig = 0;
                TFBufferUpdateDesc objBuffUpdateDesc1 = { pUniformBufferNamePlates[i] };
                beginUpdateResource(&objBuffUpdateDesc1);
                memcpy(objBuffUpdateDesc1.pMappedData, &plateUniform, sizeof(plateUniform));
                endUpdateResource(&objBuffUpdateDesc1);

                // text
                const float ANGLE_OFFSET = 0.6f; // angle offset to tilt the text shown on the plates for materials
                gTextWorldMats[i] = mat4::translation(vec3(baseX - i - offsetX * i, -5.65f, baseZ + materialPlateOffset + 0)) *
                                    mat4::rotationX(-PI * 0.5f + ANGLE_OFFSET);
            }
        }

        // ground plane
        UniformObjData groundUniform = {};
        vec3           groundScale = vec3(30.0f, 0.2f, 20.0f);
        mat4           modelmat = mat4::translation(vec3(0.0f, -6.0f, 5.0f)) * mat4::scale(groundScale);
        groundUniform.mWorldMat = modelmat;
        groundUniform.mMetallic = 0;
        groundUniform.mRoughness = 0.74f;
        groundUniform.mAlbedo = float3(0.3f, 0.3f, 0.3f);
        groundUniform.textureConfig = ETextureConfigFlags::TEXTURE_CONFIG_FLAGS_ALL & ~ETextureConfigFlags::VMF;
        groundUniform.tiling = float2(groundScale.x / groundScale.z, 1.0f);
        // gUniformDataObject.textureConfig = ETextureConfigFlags::NORMAL | ETextureConfigFlags::METALLIC | ETextureConfigFlags::AO |
        // ETextureConfigFlags::ROUGHNESS;
        TFBufferUpdateDesc objBuffUpdateDesc = { pUniformBufferGroundPlane };
        beginUpdateResource(&objBuffUpdateDesc);
        memcpy(objBuffUpdateDesc.pMappedData, &groundUniform, sizeof(groundUniform));
        endUpdateResource(&objBuffUpdateDesc);

        // Directional light
        gUniformDataDirectionalLights.mDirectionalLights[0].mDirection = (normalize((gDirectionalLightDirection)));
        gUniformDataDirectionalLights.mDirectionalLights[0].mShadowMap = 0;

        gUniformDataDirectionalLights.mDirectionalLights[0].mColor = float3(255.0f, 180.0f, 117.0f) / 255.0f;
        // gUniformDataDirectionalLights.mDirectionalLights[0].mColor = float3(236.222f, 178.504f, 119.650f) / 255.0f;
        // gUniformDataDirectionalLights.mDirectionalLights[0].mColor = float3(255.0f, 0.5f, 0.5f) / 255.0f;
        gUniformDataDirectionalLights.mDirectionalLights[0].mIntensity = 10.0f;
        gUniformDataDirectionalLights.mNumDirectionalLights = 1;
        TFBufferUpdateDesc directionalLightsBufferUpdateDesc = { pUniformBufferDirectionalLights[0] };
        beginUpdateResource(&directionalLightsBufferUpdateDesc);
        memcpy(directionalLightsBufferUpdateDesc.pMappedData, &gUniformDataDirectionalLights, sizeof(gUniformDataDirectionalLights));
        endUpdateResource(&directionalLightsBufferUpdateDesc);

        // Point lights (currently none)
        gUniformDataPointLights.mNumPointLights = 0;
        TFBufferUpdateDesc pointLightBufferUpdateDesc = { pUniformBufferPointLights };
        beginUpdateResource(&pointLightBufferUpdateDesc);
        memcpy(pointLightBufferUpdateDesc.pMappedData, &gUniformDataPointLights, sizeof(gUniformDataPointLights));
        endUpdateResource(&pointLightBufferUpdateDesc);
    }

    static void addHairMesh(HairType type, const char* name, const char* tfxFile, uint numFollowHairs, float maxRadiusAroundGuideHair,
                            uint transform, HairSectionShadingParameters* shadingParameters, HairSimulationParameters* simulationParameters)
    {
        UNREF_PARAM(name);
        UNREF_PARAM(maxRadiusAroundGuideHair);
        HairBuffer hairBuffer = {};

        TFVertexLayout layout = {};
        layout.mAttribCount = 7;
        layout.mAttribs[0].mSemantic = TF_SEMANTIC_POSITION;
        layout.mAttribs[0].mBinding = 0;
        layout.mAttribs[1].mSemantic = TF_SEMANTIC_TANGENT;
        layout.mAttribs[1].mBinding = 1;
        layout.mAttribs[2].mSemantic = TF_SEMANTIC_TEXCOORD0;
        layout.mAttribs[2].mBinding = 2;
        layout.mAttribs[3].mSemantic = TF_SEMANTIC_TEXCOORD2;
        layout.mAttribs[3].mBinding = 4;
        layout.mAttribs[4].mSemantic = TF_SEMANTIC_TEXCOORD3;
        layout.mAttribs[4].mBinding = 5;
        layout.mAttribs[5].mSemantic = TF_SEMANTIC_TEXCOORD6;
        layout.mAttribs[5].mBinding = 8;
        layout.mAttribs[6].mSemantic = TF_SEMANTIC_TEXCOORD7;
        layout.mAttribs[6].mBinding = 9;

        TFSyncToken        token = {};
        TFGeometryLoadDesc loadDesc = {};
        loadDesc.pFileName = tfxFile;
        loadDesc.pVertexLayout = &layout;
        loadDesc.mFlags = TF_GEOMETRY_LOAD_FLAG_STRUCTURED_BUFFERS;
        loadDesc.ppGeometry = &hairBuffer.pGeom;
        loadDesc.ppGeometryData = &hairBuffer.pGeomData;
        addResource(&loadDesc, &token);
        waitForToken(&token);

        hairBuffer.pBufferTriangleIndices = hairBuffer.pGeom->pIndexBuffer;
        hairBuffer.pBufferHairVertexPositions = hairBuffer.pGeom->pVertexBuffers[0];
        hairBuffer.pBufferHairVertexTangents = hairBuffer.pGeom->pVertexBuffers[1];
        hairBuffer.pBufferHairGlobalRotations = hairBuffer.pGeom->pVertexBuffers[2];
        hairBuffer.pBufferHairRefsInLocalFrame = hairBuffer.pGeom->pVertexBuffers[3];
        hairBuffer.pBufferFollowHairRootOffsets = hairBuffer.pGeom->pVertexBuffers[4];
        hairBuffer.pBufferHairThicknessCoefficients = hairBuffer.pGeom->pVertexBuffers[5];
        hairBuffer.pBufferHairRestLenghts = hairBuffer.pGeom->pVertexBuffers[6];

        TFBufferLoadDesc vertexPositionsBufferDesc = {};
        vertexPositionsBufferDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_BUFFER;
        vertexPositionsBufferDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_GPU_ONLY;
        vertexPositionsBufferDesc.mDesc.mElementCount = hairBuffer.pGeom->mVertexCount;
        vertexPositionsBufferDesc.mDesc.mStructStride = sizeof(float4);
        vertexPositionsBufferDesc.mDesc.mFormat = TinyImageFormat_UNDEFINED;
        vertexPositionsBufferDesc.mDesc.mSize =
            vertexPositionsBufferDesc.mDesc.mElementCount * vertexPositionsBufferDesc.mDesc.mStructStride;
        vertexPositionsBufferDesc.mDesc.pName = "Hair vertex positions";

        for (int i = 0; i < 3; ++i)
        {
            vertexPositionsBufferDesc.mDesc.mStartState = i == 0 ? TF_RESOURCE_STATE_SHADER_RESOURCE : TF_RESOURCE_STATE_UNORDERED_ACCESS;
            vertexPositionsBufferDesc.mDesc.mDescriptors =
                (TFDescriptorType)(TF_DESCRIPTOR_TYPE_RW_BUFFER | (i == 0 ? TF_DESCRIPTOR_TYPE_BUFFER : 0));
            vertexPositionsBufferDesc.mDesc.pName = "Hair simulation vertex positions";
            vertexPositionsBufferDesc.ppBuffer = &hairBuffer.pBufferHairSimulationVertexPositions[i];
            addResource(&vertexPositionsBufferDesc, NULL);
        }

        TFBufferLoadDesc hairShadingBufferDesc = {};
        hairShadingBufferDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        hairShadingBufferDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
        hairShadingBufferDesc.mDesc.mSize = sizeof(UniformDataHairShading);
        hairShadingBufferDesc.mDesc.mFlags = TF_BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
        hairShadingBufferDesc.pData = NULL;
        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            hairShadingBufferDesc.ppBuffer = &hairBuffer.pUniformBufferHairShading[i];
            addResource(&hairShadingBufferDesc, NULL);
        }

        TFBufferLoadDesc hairSimulationBufferDesc = {};
        hairSimulationBufferDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        hairSimulationBufferDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
        hairSimulationBufferDesc.mDesc.mSize = sizeof(UniformDataHairSimulation);
        hairSimulationBufferDesc.mDesc.mFlags = TF_BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
        hairSimulationBufferDesc.pData = NULL;
        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            hairSimulationBufferDesc.ppBuffer = &hairBuffer.pUniformBufferHairSimulation[i];
            addResource(&hairSimulationBufferDesc, NULL);
        }

        hairBuffer.mUniformDataHairShading.mColorBias = shadingParameters->mColorBias;
        hairBuffer.mUniformDataHairShading.mStrandRadius = shadingParameters->mStrandRadius;
        hairBuffer.mUniformDataHairShading.mStrandSpacing = shadingParameters->mStrandSpacing;
        hairBuffer.mUniformDataHairShading.mNumVerticesPerStrand = hairBuffer.pGeomData->mHair.mVertexCountPerStrand;

        hairBuffer.mUniformDataHairSimulation.mNumStrandsPerThreadGroup = 64 / hairBuffer.pGeomData->mHair.mVertexCountPerStrand;
        hairBuffer.mUniformDataHairSimulation.mNumFollowHairsPerGuideHair = numFollowHairs;
        hairBuffer.mUniformDataHairSimulation.mDamping = clamp(simulationParameters->mDamping, 0.0f, 0.1f);
        hairBuffer.mUniformDataHairSimulation.mGlobalConstraintStiffness =
            clamp(simulationParameters->mGlobalConstraintStiffness, 0.0f, 1.0f);
        hairBuffer.mUniformDataHairSimulation.mGlobalConstraintRange = clamp(simulationParameters->mGlobalConstraintRange, 0.0f, 1.0f);
        hairBuffer.mUniformDataHairSimulation.mShockPropagationStrength =
            clamp(simulationParameters->mShockPropagationStrength, 0.0f, 1.0f);
        hairBuffer.mUniformDataHairSimulation.mShockPropagationAccelerationThreshold =
            max(0.0f, simulationParameters->mShockPropagationAccelerationThreshold);
        hairBuffer.mUniformDataHairSimulation.mLocalStiffness = clamp(simulationParameters->mLocalConstraintStiffness, 0.0f, 1.0f);
        hairBuffer.mUniformDataHairSimulation.mLocalConstraintIterations = simulationParameters->mLocalConstraintIterations;
        hairBuffer.mUniformDataHairSimulation.mLengthConstraintIterations = simulationParameters->mLengthConstraintIterations;
        hairBuffer.mUniformDataHairSimulation.mTipSeperationFactor = simulationParameters->mTipSeperationFactor;
        hairBuffer.mUniformDataHairSimulation.mNumVerticesPerStrand = hairBuffer.pGeomData->mHair.mVertexCountPerStrand;
#if HAIR_MAX_CAPSULE_COUNT > 0
        hairBuffer.mUniformDataHairSimulation.mCapsuleCount = simulationParameters->mCapsuleCount;
#endif

        hairBuffer.mIndexCountHair = (uint)(hairBuffer.pGeom->mIndexCount);
        hairBuffer.mTotalVertexCount = (uint)hairBuffer.pGeom->mVertexCount;
        hairBuffer.mNumGuideStrands = (uint)hairBuffer.pGeomData->mHair.mGuideCountPerStrand;
        hairBuffer.mStrandRadius = shadingParameters->mStrandRadius;
        hairBuffer.mStrandSpacing = shadingParameters->mStrandSpacing;
        hairBuffer.mTransform = transform;
        hairBuffer.mDisableRootColor = shadingParameters->mDisableRootColor;

#if HAIR_MAX_CAPSULE_COUNT > 0
        for (uint i = 0; i < simulationParameters->mCapsuleCount; ++i)
            hairBuffer.mCapsules[i] = simulationParameters->mCapsules[i];
#endif

        SetHairColor(&hairBuffer, (HairColor)gHairColor);

        ++gHairCount;
        gHair = (HairBuffer*)tf_realloc(gHair, gHairCount * sizeof(*gHair));
        gHair[gHairCount - 1] = hairBuffer;

        ++gHairTypeIndicesCount[type];
        gHairTypeIndices[type] =
            (uint32_t*)tf_realloc(gHairTypeIndices[type], gHairTypeIndicesCount[type] * sizeof(*gHairTypeIndices[type]));
        gHairTypeIndices[type][gHairTypeIndicesCount[type] - 1] = gHairCount - 1;
    }

    static void removeHairMeshes()
    {
        for (size_t i = 0; i < gHairCount; ++i)
        {
            removeResource(gHair[i].pGeom);
            removeResource(gHair[i].pGeomData);
            for (int j = 0; j < 3; ++j)
                removeResource(gHair[i].pBufferHairSimulationVertexPositions[j]);
            for (uint32_t j = 0; j < gDataBufferCount; ++j)
            {
                removeResource(gHair[i].pUniformBufferHairShading[j]);
                removeResource(gHair[i].pUniformBufferHairSimulation[j]);
            }
        }
        gHairCount = 0;
        tf_free(gHair);
        gHair = nullptr;
    }

    static void SetHairColor(HairBuffer* hairBuffer, HairColor hairColor)
    {
        // Fill these variables for each hair color
        float4 rootColor = float4(0.06f, 0.02f, 0.0f, 1.0f);
        float4 strandColor = float4(0.41f, 0.3f, 0.26f, 1.0f);
        float  kDiffuse = 0.14f;
        float  kSpecular1 = 0.03f;
        float  kExponent1 = 12.0f;
        float  kSpecular2 = 0.02f;
        float  kExponent2 = 20.0f;

        // Fill variables
        if (hairColor == HAIR_COLOR_BROWN)
        {
            rootColor = float4(0.06f, 0.02f, 0.0f, 1.0f);
            strandColor = float4(0.41f, 0.3f, 0.26f, 1.0f);
            kDiffuse = 0.14f;
            kSpecular1 = 0.03f;
            kExponent1 = 12.0f;
            kSpecular2 = 0.02f;
            kExponent2 = 20.0f;
        }
        else if (hairColor == HAIR_COLOR_BLONDE)
        {
            rootColor = float4(0.2f, 0.08f, 0.03f, 1.0f);
            strandColor = float4(0.9f, 0.78f, 0.66f, 1.0f);
            kDiffuse = 0.14f;
            kSpecular1 = 0.03f;
            kExponent1 = 12.0f;
            kSpecular2 = 0.02f;
            kExponent2 = 20.0f;
        }
        else if (hairColor == HAIR_COLOR_BLACK)
        {
            rootColor = float4(0.0f, 0.0f, 0.0f, 1.0f);
            strandColor = float4(0.04f, 0.03f, 0.02f, 1.0f);
            kDiffuse = 0.14f;
            kSpecular1 = 0.03f;
            kExponent1 = 12.0f;
            kSpecular2 = 0.02f;
            kExponent2 = 20.0f;
        }
        else if (hairColor == HAIR_COLOR_RED)
        {
            rootColor = float4(0.15f, 0.0f, 0.0f, 1.0f);
            strandColor = float4(0.55f, 0.29f, 0.26f, 1.0f);
            kDiffuse = 0.14f;
            kSpecular1 = 0.03f;
            kExponent1 = 12.0f;
            kSpecular2 = 0.02f;
            kExponent2 = 20.0f;
        }

        if (hairBuffer->mDisableRootColor)
            rootColor = strandColor;

        // Set variables of uniform buffer
        rootColor[0] = clamp(rootColor[0], 0.0f, 1.0f);
        rootColor[1] = clamp(rootColor[1], 0.0f, 1.0f);
        rootColor[2] = clamp(rootColor[2], 0.0f, 1.0f);
        rootColor[3] = clamp(rootColor[3], 0.0f, 1.0f);
        hairBuffer->mUniformDataHairShading.mRootColor = packR8G8B8A8(rootColor);
        strandColor[0] = clamp(strandColor[0], 0.0f, 1.0f);
        strandColor[1] = clamp(strandColor[1], 0.0f, 1.0f);
        strandColor[2] = clamp(strandColor[2], 0.0f, 1.0f);
        strandColor[3] = clamp(strandColor[3], 0.0f, 1.0f);
        hairBuffer->mUniformDataHairShading.mStrandColor = packR8G8B8A8(strandColor);
        hairBuffer->mUniformDataHairShading.mKDiffuse = kDiffuse;
        hairBuffer->mUniformDataHairShading.mKSpecular1 = kSpecular1;
        hairBuffer->mUniformDataHairShading.mKExponent1 = kExponent1;
        hairBuffer->mUniformDataHairShading.mKSpecular2 = kSpecular2;
        hairBuffer->mUniformDataHairShading.mKExponent2 = kExponent2;
    }

    void initAnimations()
    {
        // Load rigs
        gAnimationRig.Initialize(TF_RD_ANIMATIONS, "stickFigure/skeleton.ozz");

        // Load clips
        gAnimationClipNeckCrack.Initialize(TF_RD_ANIMATIONS, "stickFigure/animations/neckCrack.ozz", &gAnimationRig);

        gAnimationClipStand.Initialize(TF_RD_ANIMATIONS, "stickFigure/animations/stand.ozz", &gAnimationRig);

        for (uint hairType = 0; hairType < HAIR_TYPE_COUNT; ++hairType)
        {
            // Create clip controllers
            gAnimationClipControllerNeckCrack[hairType].Initialize(gAnimationClipNeckCrack.GetDuration(), NULL);
            gAnimationClipControllerStand[hairType].Initialize(gAnimationClipStand.GetDuration(), NULL);

            /*gAnimationClipControllerNeckCrack[hairType].SetPlay(false);
            gAnimationClipControllerStand[hairType].SetPlay(false);*/

            // Create animations
            AnimationDesc animationDesc{};
            animationDesc.mRig = &gAnimationRig;
            animationDesc.mNumLayers = 2;
            animationDesc.mLayerProperties[0].mClip = &gAnimationClipStand;
            animationDesc.mLayerProperties[0].mClipController = &gAnimationClipControllerStand[hairType];
            animationDesc.mLayerProperties[0].mAdditive = false;
            animationDesc.mLayerProperties[1].mClip = &gAnimationClipNeckCrack;
            animationDesc.mLayerProperties[1].mClipController = &gAnimationClipControllerNeckCrack[hairType];
            animationDesc.mLayerProperties[1].mAdditive = true;
            animationDesc.mBlendType = BlendType::EQUAL;
            gAnimation[hairType].Initialize(animationDesc);

            // Create animated object
            gAnimatedObject[hairType].Initialize(&gAnimationRig, &gAnimation[hairType]);
            gAnimatedObject[hairType].ComputeBindPose(gAnimatedObject[hairType].mRootTransform);
            gAnimatedObject[hairType].ComputeJointScales(gAnimatedObject[hairType].mRootTransform);
        }
    }

    void exitAnimations()
    {
        // Destroy clips
        gAnimationClipNeckCrack.Exit();
        gAnimationClipStand.Exit();

        // Destroy rigs, animations and animated objects
        for (uint hairType = 0; hairType < HAIR_TYPE_COUNT; ++hairType)
        {
            gAnimation[hairType].Exit();
            gAnimatedObject[hairType].Exit();
            gAnimationClipControllerNeckCrack[hairType].Reset();
            gAnimationClipControllerStand[hairType].Reset();
        }

        gAnimationRig.Exit();
    }

    //--------------------------------------------------------------------------------------------
    // LOAD FUNCTIONS
    //--------------------------------------------------------------------------------------------
    void addPipelines()
    {
        // Create vertex layouts
        TFVertexLayout skyboxVertexLayout = {};
        skyboxVertexLayout.mBindingCount = 1;
        skyboxVertexLayout.mAttribCount = 1;
        skyboxVertexLayout.mAttribs[0].mSemantic = TF_SEMANTIC_POSITION;
        skyboxVertexLayout.mAttribs[0].mFormat = TinyImageFormat_R32G32B32A32_SFLOAT;
        skyboxVertexLayout.mAttribs[0].mBinding = 0;
        skyboxVertexLayout.mAttribs[0].mLocation = 0;
        skyboxVertexLayout.mAttribs[0].mOffset = 0;

        // Create pipelines
        TFPipelineDesc graphicsPipelineDesc = {};
        PIPELINE_LAYOUT_DESC(graphicsPipelineDesc, SRT_LAYOUT_DESC(SrtData, Persistent), SRT_LAYOUT_DESC(SrtData, PerFrame), NULL,
                             SRT_LAYOUT_DESC(SrtData, PerDraw));

        graphicsPipelineDesc.mType = TF_PIPELINE_TYPE_GRAPHICS;
        TFGraphicsPipelineDesc& pipelineSettings = graphicsPipelineDesc.mGraphicsDesc;

        TFRasterizerStateDesc rasterizerStateCullNoneDesc = {};
        rasterizerStateCullNoneDesc.mCullMode = TF_CULL_MODE_NONE;

        TFRasterizerStateDesc rasterizerStateDesc = {};
        rasterizerStateDesc.mCullMode = TF_CULL_MODE_BACK;

        TFDepthStateDesc depthStateDesc = {};
        depthStateDesc.mDepthTest = true;
        depthStateDesc.mDepthWrite = true;
        depthStateDesc.mDepthFunc = TF_CMP_GEQUAL;

        TFDepthStateDesc depthStateDisableDesc = {};
        depthStateDisableDesc.mDepthTest = false;
        depthStateDisableDesc.mDepthWrite = false;
        depthStateDisableDesc.mDepthFunc = TF_CMP_GEQUAL;

        TFDepthStateDesc depthStateNoWriteDesc = {};
        depthStateNoWriteDesc.mDepthTest = true;
        depthStateNoWriteDesc.mDepthWrite = false;
        depthStateNoWriteDesc.mDepthFunc = TF_CMP_GEQUAL;

        TFBlendStateDesc blendStateDesc = {};
        blendStateDesc.mSrcFactors[0] = TF_BC_SRC_ALPHA;
        blendStateDesc.mDstFactors[0] = TF_BC_ONE_MINUS_SRC_ALPHA;
        blendStateDesc.mBlendModes[0] = TF_BM_ADD;
        blendStateDesc.mSrcAlphaFactors[0] = TF_BC_ONE;
        blendStateDesc.mDstAlphaFactors[0] = TF_BC_ZERO;
        blendStateDesc.mBlendAlphaModes[0] = TF_BM_ADD;
        blendStateDesc.mColorWriteMasks[0] = TF_COLOR_MASK_ALL;
        blendStateDesc.mRenderTargetMask = TF_BLEND_STATE_TARGET_0;
        blendStateDesc.mIndependentBlend = false;

        TFBlendStateDesc blendStateDepthPeelingDesc = {};
        blendStateDepthPeelingDesc.mSrcFactors[0] = TF_BC_ZERO;
        blendStateDepthPeelingDesc.mDstFactors[0] = TF_BC_SRC_COLOR;
        blendStateDepthPeelingDesc.mBlendModes[0] = TF_BM_ADD;
        blendStateDepthPeelingDesc.mSrcAlphaFactors[0] = TF_BC_ZERO;
        blendStateDepthPeelingDesc.mDstAlphaFactors[0] = TF_BC_SRC_ALPHA;
        blendStateDepthPeelingDesc.mBlendAlphaModes[0] = TF_BM_ADD;
        blendStateDepthPeelingDesc.mColorWriteMasks[0] = TF_COLOR_MASK_RED;
        blendStateDepthPeelingDesc.mRenderTargetMask = TF_BLEND_STATE_TARGET_0;
        blendStateDepthPeelingDesc.mIndependentBlend = false;

        TFBlendStateDesc blendStateAddDesc = {};
        blendStateAddDesc.mSrcFactors[0] = TF_BC_ONE;
        blendStateAddDesc.mDstFactors[0] = TF_BC_ONE;
        blendStateAddDesc.mBlendModes[0] = TF_BM_ADD;
        blendStateAddDesc.mSrcAlphaFactors[0] = TF_BC_ONE;
        blendStateAddDesc.mDstAlphaFactors[0] = TF_BC_ONE;
        blendStateAddDesc.mBlendAlphaModes[0] = TF_BM_ADD;
        blendStateAddDesc.mColorWriteMasks[0] = TF_COLOR_MASK_ALL;
        blendStateAddDesc.mRenderTargetMask = TF_BLEND_STATE_TARGET_0;
        blendStateAddDesc.mIndependentBlend = false;

        TFBlendStateDesc blendStateColorResolveDesc = {};
        blendStateColorResolveDesc.mSrcFactors[0] = TF_BC_ONE;
        blendStateColorResolveDesc.mDstFactors[0] = TF_BC_SRC_ALPHA;
        blendStateColorResolveDesc.mBlendModes[0] = TF_BM_ADD;
        blendStateColorResolveDesc.mSrcAlphaFactors[0] = TF_BC_ZERO;
        blendStateColorResolveDesc.mDstAlphaFactors[0] = TF_BC_ZERO;
        blendStateColorResolveDesc.mBlendAlphaModes[0] = TF_BM_ADD;
        blendStateColorResolveDesc.mColorWriteMasks[0] = TF_COLOR_MASK_ALL;
        blendStateColorResolveDesc.mRenderTargetMask = TF_BLEND_STATE_TARGET_0;
        blendStateColorResolveDesc.mIndependentBlend = false;

        // skybox
        pipelineSettings.mPrimitiveTopo = TF_PRIMITIVE_TOPO_TRI_LIST;
        pipelineSettings.mRenderTargetCount = 1;
        pipelineSettings.pDepthState = NULL;
        pipelineSettings.pColorFormats = &pSwapChain->ppRenderTargets[0]->mFormat;
        pipelineSettings.mSampleCount = pSwapChain->ppRenderTargets[0]->mSampleCount;
        pipelineSettings.mSampleQuality = pSwapChain->ppRenderTargets[0]->mSampleQuality;
        pipelineSettings.mDepthStencilFormat = TinyImageFormat_UNDEFINED;
        pipelineSettings.pShaderProgram = pShaderSkybox;
        pipelineSettings.pVertexLayout = &skyboxVertexLayout;
        pipelineSettings.pRasterizerState = &rasterizerStateCullNoneDesc;
        addPipeline(pRenderer, &graphicsPipelineDesc, &pPipelineSkybox);

        // shadow pass
        pipelineSettings = {};
        pipelineSettings.mPrimitiveTopo = TF_PRIMITIVE_TOPO_TRI_LIST;
        pipelineSettings.mRenderTargetCount = 0;
        pipelineSettings.pDepthState = &depthStateDesc;
        pipelineSettings.pColorFormats = NULL;
        pipelineSettings.mSampleCount = TF_SAMPLE_COUNT_1;
        pipelineSettings.mSampleQuality = 0;
        pipelineSettings.mDepthStencilFormat = pRenderTargetShadowMap->mFormat;
        pipelineSettings.pShaderProgram = pShaderShadowPass;
        pipelineSettings.pVertexLayout = &gVertexLayoutDefault;
        pipelineSettings.pRasterizerState = &rasterizerStateCullNoneDesc;
        addPipeline(pRenderer, &graphicsPipelineDesc, &pPipelineShadowPass);

        TFPipelineDesc hairPipelineDesc = {};
        PIPELINE_LAYOUT_DESC(hairPipelineDesc, SRT_LAYOUT_DESC(SrtHairData, Persistent), NULL, NULL, SRT_LAYOUT_DESC(SrtHairData, PerDraw));
        hairPipelineDesc.mType = TF_PIPELINE_TYPE_GRAPHICS;
        TFGraphicsPipelineDesc& hairPipelineSettings = hairPipelineDesc.mGraphicsDesc;

        hairPipelineSettings = {};
        hairPipelineSettings.mPrimitiveTopo = TF_PRIMITIVE_TOPO_TRI_LIST;
        hairPipelineSettings.mRenderTargetCount = 0;
        hairPipelineSettings.pDepthState = &depthStateDisableDesc;
        hairPipelineSettings.pColorFormats = NULL;
        hairPipelineSettings.mSampleCount = TF_SAMPLE_COUNT_1;
        hairPipelineSettings.mSampleQuality = 0;
        hairPipelineSettings.mDepthStencilFormat = pRenderTargetDepth->mFormat;
        hairPipelineSettings.pShaderProgram = pShaderHairClear;
        hairPipelineSettings.pRasterizerState = &rasterizerStateCullNoneDesc;
        hairPipelineSettings.pBlendState = NULL;
        addPipeline(pRenderer, &hairPipelineDesc, &pPipelineHairClear);

        PIPELINE_LAYOUT_DESC(hairPipelineDesc, SRT_LAYOUT_DESC(SrtHairData, Persistent), SRT_LAYOUT_DESC(SrtHairData, PerFrame), NULL,
                             SRT_LAYOUT_DESC(SrtHairData, PerDraw));
        TinyImageFormat depthPeelingFormat = TinyImageFormat_R16_SFLOAT;

        hairPipelineSettings = {};
        hairPipelineSettings.mPrimitiveTopo = TF_PRIMITIVE_TOPO_TRI_LIST;
        hairPipelineSettings.mRenderTargetCount = 1;
        hairPipelineSettings.pDepthState = &depthStateNoWriteDesc;
        hairPipelineSettings.pColorFormats = &depthPeelingFormat;
        hairPipelineSettings.mSampleCount = TF_SAMPLE_COUNT_1;
        hairPipelineSettings.mSampleQuality = 0;
        hairPipelineSettings.mDepthStencilFormat = pRenderTargetDepth->mFormat;
        hairPipelineSettings.pShaderProgram = pShaderHairDepthPeeling;
        hairPipelineSettings.pRasterizerState = &rasterizerStateDesc;
        hairPipelineSettings.pBlendState = &blendStateDepthPeelingDesc;
        addPipeline(pRenderer, &hairPipelineDesc, &pPipelineHairDepthPeeling);

        PIPELINE_LAYOUT_DESC(hairPipelineDesc, SRT_LAYOUT_DESC(SrtHairData, Persistent), SRT_LAYOUT_DESC(SrtHairData, PerFrame), NULL,
                             NULL);

        hairPipelineSettings = {};
        hairPipelineSettings.mPrimitiveTopo = TF_PRIMITIVE_TOPO_TRI_LIST;
        hairPipelineSettings.mRenderTargetCount = 0;
        hairPipelineSettings.pDepthState = &depthStateDesc;
        hairPipelineSettings.pColorFormats = NULL;
        hairPipelineSettings.mSampleCount = TF_SAMPLE_COUNT_1;
        hairPipelineSettings.mSampleQuality = 0;
        hairPipelineSettings.mDepthStencilFormat = pRenderTargetDepth->mFormat;
        hairPipelineSettings.pShaderProgram = pShaderHairDepthResolve;
        hairPipelineSettings.pRasterizerState = &rasterizerStateCullNoneDesc;
        addPipeline(pRenderer, &hairPipelineDesc, &pPipelineHairDepthResolve);

        PIPELINE_LAYOUT_DESC(hairPipelineDesc, SRT_LAYOUT_DESC(SrtHairData, Persistent), SRT_LAYOUT_DESC(SrtHairData, PerFrame),
                             SRT_LAYOUT_DESC(SrtHairData, PerBatch), SRT_LAYOUT_DESC(SrtHairData, PerDraw));

        TinyImageFormat fillColorsFormat = TinyImageFormat_R16G16B16A16_SFLOAT;

        hairPipelineSettings = {};
        hairPipelineSettings.mPrimitiveTopo = TF_PRIMITIVE_TOPO_TRI_LIST;
        hairPipelineSettings.mRenderTargetCount = 1;
        if (gSupportTextureAtomics)
        {
            hairPipelineSettings.pDepthState = &depthStateNoWriteDesc;
        }
        else
        {
            hairPipelineSettings.pDepthState = &depthStateDisableDesc;
        }
        hairPipelineSettings.pColorFormats = &fillColorsFormat;
        hairPipelineSettings.mSampleCount = TF_SAMPLE_COUNT_1;
        hairPipelineSettings.mSampleQuality = 0;
        hairPipelineSettings.mDepthStencilFormat = pRenderTargetDepth->mFormat;
        hairPipelineSettings.pShaderProgram = pShaderHairFillColors;
        hairPipelineSettings.pRasterizerState = &rasterizerStateCullNoneDesc;
        hairPipelineSettings.pBlendState = &blendStateAddDesc;
        addPipeline(pRenderer, &hairPipelineDesc, &pPipelineHairFillColors);

        PIPELINE_LAYOUT_DESC(hairPipelineDesc, SRT_LAYOUT_DESC(SrtHairData, Persistent), NULL, NULL, NULL);

        hairPipelineSettings = {};
        hairPipelineSettings.mPrimitiveTopo = TF_PRIMITIVE_TOPO_TRI_LIST;
        hairPipelineSettings.mRenderTargetCount = 1;
        hairPipelineSettings.pDepthState = &depthStateDisableDesc;
        hairPipelineSettings.pColorFormats = &pSwapChain->ppRenderTargets[0]->mFormat;
        hairPipelineSettings.mSampleCount = pSwapChain->ppRenderTargets[0]->mSampleCount;
        hairPipelineSettings.mSampleQuality = pSwapChain->ppRenderTargets[0]->mSampleQuality;
        hairPipelineSettings.mDepthStencilFormat = pRenderTargetDepth->mFormat;
        hairPipelineSettings.pShaderProgram = pShaderHairResolveColor;
        hairPipelineSettings.pRasterizerState = &rasterizerStateCullNoneDesc;
        hairPipelineSettings.pBlendState = &blendStateColorResolveDesc;
        addPipeline(pRenderer, &hairPipelineDesc, &pPipelineHairColorResolve);

        PIPELINE_LAYOUT_DESC(hairPipelineDesc, NULL, SRT_LAYOUT_DESC(SrtHairData, PerFrame), NULL, SRT_LAYOUT_DESC(SrtHairData, PerDraw));

        hairPipelineSettings = {};
        hairPipelineSettings.mPrimitiveTopo = TF_PRIMITIVE_TOPO_TRI_LIST;
        hairPipelineSettings.mRenderTargetCount = 0;
        hairPipelineSettings.pDepthState = &depthStateDesc;
        hairPipelineSettings.mSampleCount = pSwapChain->ppRenderTargets[0]->mSampleCount;
        hairPipelineSettings.mSampleQuality = pSwapChain->ppRenderTargets[0]->mSampleQuality;
        hairPipelineSettings.mDepthStencilFormat = pRenderTargetHairShadows[0][0]->mFormat;
        hairPipelineSettings.pShaderProgram = pShaderHairShadow;
        hairPipelineSettings.pRasterizerState = &rasterizerStateCullNoneDesc;
        addPipeline(pRenderer, &hairPipelineDesc, &pPipelineHairShadow);

        TFPipelineDesc computeDesc = {};
        PIPELINE_LAYOUT_DESC(computeDesc, SRT_LAYOUT_DESC(SrtHairData, Persistent), SRT_LAYOUT_DESC(SrtHairData, PerFrame),
                             SRT_LAYOUT_DESC(SrtHairData, PerBatch), SRT_LAYOUT_DESC(SrtHairData, PerDraw));
        computeDesc.mType = TF_PIPELINE_TYPE_COMPUTE;
        TFComputePipelineDesc& computePipelineDesc = computeDesc.mComputeDesc;
        computePipelineDesc.pShaderProgram = pShaderHairIntegrate;
        addPipeline(pRenderer, &computeDesc, &pPipelineHairIntegrate);

        computePipelineDesc = {};
        computePipelineDesc.pShaderProgram = pShaderHairShockPropagation;
        addPipeline(pRenderer, &computeDesc, &pPipelineHairShockPropagation);

        computePipelineDesc = {};
        computePipelineDesc.pShaderProgram = pShaderHairLocalConstraints;
        addPipeline(pRenderer, &computeDesc, &pPipelineHairLocalConstraints);

        computePipelineDesc = {};
        computePipelineDesc.pShaderProgram = pShaderHairLengthConstraints;
        addPipeline(pRenderer, &computeDesc, &pPipelineHairLengthConstraints);

        computePipelineDesc = {};
        computePipelineDesc.pShaderProgram = pShaderHairUpdateFollowHairs;
        addPipeline(pRenderer, &computeDesc, &pPipelineHairUpdateFollowHairs);

        computePipelineDesc = {};
        computePipelineDesc.pShaderProgram = pShaderHairPreWarm;
        addPipeline(pRenderer, &computeDesc, &pPipelineHairPreWarm);

        TFPipelineDesc capsulesPipelineDesc = {};
        PIPELINE_LAYOUT_DESC(capsulesPipelineDesc, SRT_LAYOUT_DESC(SrtCapsulesData, Persistent), SRT_LAYOUT_DESC(SrtCapsulesData, PerFrame),
                             NULL, SRT_LAYOUT_DESC(SrtCapsulesData, PerDraw));
        capsulesPipelineDesc.mType = TF_PIPELINE_TYPE_GRAPHICS;
        TFGraphicsPipelineDesc& capsulesPipelineSettings = capsulesPipelineDesc.mGraphicsDesc;

        capsulesPipelineSettings = {};
        capsulesPipelineSettings.mPrimitiveTopo = TF_PRIMITIVE_TOPO_TRI_LIST;
        capsulesPipelineSettings.mRenderTargetCount = 1;
        capsulesPipelineSettings.pDepthState = &depthStateNoWriteDesc;
        capsulesPipelineSettings.pColorFormats = &pSwapChain->ppRenderTargets[0]->mFormat;
        capsulesPipelineSettings.mSampleCount = pSwapChain->ppRenderTargets[0]->mSampleCount;
        capsulesPipelineSettings.mSampleQuality = pSwapChain->ppRenderTargets[0]->mSampleQuality;
        capsulesPipelineSettings.mDepthStencilFormat = pRenderTargetDepth->mFormat;
        capsulesPipelineSettings.pShaderProgram = pShaderShowCapsules;
        capsulesPipelineSettings.pVertexLayout = &gVertexLayoutDefault;
        capsulesPipelineSettings.pRasterizerState = &rasterizerStateCullNoneDesc;
        capsulesPipelineSettings.pBlendState = &blendStateDesc;
        addPipeline(pRenderer, &capsulesPipelineDesc, &pPipelineShowCapsules);

        gUniformDataHairGlobal.mViewport = float4(0.0f, 0.0f, (float)mSettings.mWidth, (float)mSettings.mHeight);
    }

    void removePipelines()
    {
        removePipeline(pRenderer, pPipelineSkybox);
        removePipeline(pRenderer, pPipelineShadowPass);

        removePipeline(pRenderer, pPipelineHairClear);
        removePipeline(pRenderer, pPipelineHairDepthPeeling);
        removePipeline(pRenderer, pPipelineHairDepthResolve);
        removePipeline(pRenderer, pPipelineHairFillColors);
        removePipeline(pRenderer, pPipelineHairColorResolve);
        removePipeline(pRenderer, pPipelineHairIntegrate);
        removePipeline(pRenderer, pPipelineHairShockPropagation);
        removePipeline(pRenderer, pPipelineHairLocalConstraints);
        removePipeline(pRenderer, pPipelineHairLengthConstraints);
        removePipeline(pRenderer, pPipelineHairUpdateFollowHairs);
        removePipeline(pRenderer, pPipelineHairPreWarm);
        removePipeline(pRenderer, pPipelineShowCapsules);
        removePipeline(pRenderer, pPipelineHairShadow);
    }

    void addRenderTargets()
    {
        uint32_t currentOffsetESRAM = 0;

        TF_ESRAM_BEGIN_ALLOC(pRenderer, "Shadow Map", currentOffsetESRAM);
        TFRenderTargetDesc shadowPassRenderTargetDesc = {};
        shadowPassRenderTargetDesc.mArraySize = 1;
        shadowPassRenderTargetDesc.mClearValue.depth = 0.0f;
        shadowPassRenderTargetDesc.mClearValue.stencil = 0;
        shadowPassRenderTargetDesc.mDepth = 1;
        shadowPassRenderTargetDesc.mDescriptors = TF_DESCRIPTOR_TYPE_TEXTURE;
        shadowPassRenderTargetDesc.mFormat = TinyImageFormat_D32_SFLOAT;
        shadowPassRenderTargetDesc.mStartState = TF_RESOURCE_STATE_SHADER_RESOURCE;
        shadowPassRenderTargetDesc.mHeight = gShadowMapDimensions;
        shadowPassRenderTargetDesc.mWidth = gShadowMapDimensions;
        shadowPassRenderTargetDesc.mSampleCount = TF_SAMPLE_COUNT_1;
        shadowPassRenderTargetDesc.mSampleQuality = 0;
        shadowPassRenderTargetDesc.mFlags = TF_TEXTURE_CREATION_FLAG_OWN_MEMORY_BIT;
        shadowPassRenderTargetDesc.mFlags |= TF_TEXTURE_CREATION_FLAG_ESRAM;
        shadowPassRenderTargetDesc.mFlags |= TF_TEXTURE_CREATION_FLAG_TEXTURE_COMPATABILITY;
        shadowPassRenderTargetDesc.pName = "Shadow Map Render Target";
        addRenderTarget(pRenderer, &shadowPassRenderTargetDesc, &pRenderTargetShadowMap);

        TF_ESRAM_CURRENT_OFFSET(pRenderer, shadowOffsetESRAM);
        TF_ESRAM_END_ALLOC(pRenderer);

        TF_ESRAM_BEGIN_ALLOC(pRenderer, "Hair Shadow", 0);
        TFRenderTargetDesc hairShadowRenderTargetDesc = {};
        hairShadowRenderTargetDesc.mWidth = 1024;
        hairShadowRenderTargetDesc.mHeight = 1024;
        hairShadowRenderTargetDesc.mDepth = 1;
        hairShadowRenderTargetDesc.mArraySize = 1;
        hairShadowRenderTargetDesc.mMipLevels = 1;
        hairShadowRenderTargetDesc.mSampleCount = TF_SAMPLE_COUNT_1;
        hairShadowRenderTargetDesc.mFormat = TinyImageFormat_D32_SFLOAT;
        hairShadowRenderTargetDesc.mStartState = TF_RESOURCE_STATE_SHADER_RESOURCE;
        hairShadowRenderTargetDesc.mClearValue.depth = 0.0f;
        hairShadowRenderTargetDesc.mClearValue.stencil = 0;
        hairShadowRenderTargetDesc.mSampleQuality = 0;
        hairShadowRenderTargetDesc.mDescriptors = TF_DESCRIPTOR_TYPE_TEXTURE;
        hairShadowRenderTargetDesc.mFlags = TF_TEXTURE_CREATION_FLAG_OWN_MEMORY_BIT;
        hairShadowRenderTargetDesc.mFlags |= TF_TEXTURE_CREATION_FLAG_ESRAM;
        hairShadowRenderTargetDesc.mFlags |= TF_TEXTURE_CREATION_FLAG_TEXTURE_COMPATABILITY;
        hairShadowRenderTargetDesc.pName = "Hair shadow RT";
        for (uint hairType = 0; hairType < HAIR_TYPE_COUNT; ++hairType)
        {
            for (int i = 0; i < MAX_NUM_DIRECTIONAL_LIGHTS; ++i)
            {
                addRenderTarget(pRenderer, &hairShadowRenderTargetDesc, &pRenderTargetHairShadows[hairType][i]);
            }
        }
        TF_ESRAM_CURRENT_OFFSET(pRenderer, hairShadowOffsetESRAM);
        TF_ESRAM_END_ALLOC(pRenderer);

        uint32_t hairDepthsOffsetESRAM = currentOffsetESRAM;
        if (gSupportTextureAtomics)
        {
            TF_ESRAM_BEGIN_ALLOC(pRenderer, "Hair depths", currentOffsetESRAM);
            TFTextureDesc hairDepthsTextureDesc = {};
            hairDepthsTextureDesc.mWidth = mSettings.mWidth;
            hairDepthsTextureDesc.mHeight = mSettings.mHeight;
            hairDepthsTextureDesc.mDepth = 1;
            hairDepthsTextureDesc.mArraySize = 3;
            hairDepthsTextureDesc.mMipLevels = 1;
            hairDepthsTextureDesc.mSampleCount = TF_SAMPLE_COUNT_1;
            hairDepthsTextureDesc.mFormat = TinyImageFormat_R32_UINT;
            hairDepthsTextureDesc.mStartState = TF_RESOURCE_STATE_SHADER_RESOURCE;
            hairDepthsTextureDesc.mClearValue.r = 1.0f;
            hairDepthsTextureDesc.mClearValue.g = 1.0f;
            hairDepthsTextureDesc.mClearValue.b = 1.0f;
            hairDepthsTextureDesc.mClearValue.a = 1.0f;
            hairDepthsTextureDesc.mDescriptors = TF_DESCRIPTOR_TYPE_RW_TEXTURE | TF_DESCRIPTOR_TYPE_TEXTURE;
            hairDepthsTextureDesc.mFlags = TF_TEXTURE_CREATION_FLAG_VR_MULTIVIEW;
            hairDepthsTextureDesc.mFlags |= TF_TEXTURE_CREATION_FLAG_ESRAM;
            hairDepthsTextureDesc.pName = "Hair depths texture";

            TFTextureLoadDesc hairDepthsTextureLoadDesc = {};
            hairDepthsTextureLoadDesc.pDesc = &hairDepthsTextureDesc;
            hairDepthsTextureLoadDesc.ppTexture = &pTextureHairDepth;
            addResource(&hairDepthsTextureLoadDesc, NULL);

            TF_ESRAM_CURRENT_OFFSET(pRenderer, currentOffset);
            hairDepthsOffsetESRAM = currentOffset;
            TF_ESRAM_END_ALLOC(pRenderer);
        }
        else
        {
            TFBufferLoadDesc hairDepthsBufferLoadDesc = {};
            hairDepthsBufferLoadDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_BUFFER | TF_DESCRIPTOR_TYPE_RW_BUFFER;
            hairDepthsBufferLoadDesc.mDesc.mElementCount = mSettings.mWidth * mSettings.mHeight * 3;
            hairDepthsBufferLoadDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_GPU_ONLY;
            hairDepthsBufferLoadDesc.mDesc.mFlags = TF_BUFFER_CREATION_FLAG_OWN_MEMORY_BIT;
            hairDepthsBufferLoadDesc.mDesc.mStructStride = sizeof(uint);
            hairDepthsBufferLoadDesc.mDesc.mSize =
                hairDepthsBufferLoadDesc.mDesc.mElementCount * hairDepthsBufferLoadDesc.mDesc.mStructStride;
            hairDepthsBufferLoadDesc.mDesc.pName = "Hair depths buffer";
            hairDepthsBufferLoadDesc.ppBuffer = &pBufferHairDepth;
            addResource(&hairDepthsBufferLoadDesc, NULL);
        }

        currentOffsetESRAM = max(shadowOffsetESRAM, currentOffsetESRAM);
        currentOffsetESRAM = max(hairShadowOffsetESRAM, currentOffsetESRAM);
        currentOffsetESRAM = max(hairDepthsOffsetESRAM, currentOffsetESRAM);

        TF_ESRAM_BEGIN_ALLOC(pRenderer, "Depth", currentOffsetESRAM);
        TFRenderTargetDesc depthRenderTargetDesc = {};
        depthRenderTargetDesc.mArraySize = 1;
        depthRenderTargetDesc.mClearValue.depth = 0.0f;
        depthRenderTargetDesc.mClearValue.stencil = 0;
        depthRenderTargetDesc.mDepth = 1;
        depthRenderTargetDesc.mDescriptors = TF_DESCRIPTOR_TYPE_TEXTURE;
        depthRenderTargetDesc.mFormat = TinyImageFormat_D32_SFLOAT;
        depthRenderTargetDesc.mStartState = TF_RESOURCE_STATE_DEPTH_WRITE;
        depthRenderTargetDesc.mSampleCount = TF_SAMPLE_COUNT_1;
        depthRenderTargetDesc.mSampleQuality = 0;
        depthRenderTargetDesc.mWidth = mSettings.mWidth;
        depthRenderTargetDesc.mHeight = mSettings.mHeight;
        depthRenderTargetDesc.mFlags = TF_TEXTURE_CREATION_FLAG_OWN_MEMORY_BIT | TF_TEXTURE_CREATION_FLAG_VR_MULTIVIEW;
        depthRenderTargetDesc.mFlags |= TF_TEXTURE_CREATION_FLAG_ESRAM;
        depthRenderTargetDesc.pName = "Depth buffer";
        addRenderTarget(pRenderer, &depthRenderTargetDesc, &pRenderTargetDepth);

        TF_ESRAM_CURRENT_OFFSET(pRenderer, depthOffsetESRAM);
        TF_ESRAM_END_ALLOC(pRenderer);
        currentOffsetESRAM = max(depthOffsetESRAM, currentOffsetESRAM);

        TFRenderTargetDesc fillColorsRenderTargetDesc = {};
        fillColorsRenderTargetDesc.mWidth = mSettings.mWidth;
        fillColorsRenderTargetDesc.mHeight = mSettings.mHeight;
        fillColorsRenderTargetDesc.mDepth = 1;
        fillColorsRenderTargetDesc.mArraySize = 1;
        fillColorsRenderTargetDesc.mMipLevels = 1;
        fillColorsRenderTargetDesc.mSampleCount = TF_SAMPLE_COUNT_1;
        fillColorsRenderTargetDesc.mFormat = TinyImageFormat_R16G16B16A16_SFLOAT;
        fillColorsRenderTargetDesc.mStartState = TF_RESOURCE_STATE_SHADER_RESOURCE;
        fillColorsRenderTargetDesc.mClearValue.r = 0.0f;
        fillColorsRenderTargetDesc.mClearValue.g = 0.0f;
        fillColorsRenderTargetDesc.mClearValue.b = 0.0f;
        fillColorsRenderTargetDesc.mClearValue.a = 0.0f;
        fillColorsRenderTargetDesc.mSampleQuality = 0;
        fillColorsRenderTargetDesc.mDescriptors = TF_DESCRIPTOR_TYPE_TEXTURE;
        fillColorsRenderTargetDesc.mFlags = TF_TEXTURE_CREATION_FLAG_OWN_MEMORY_BIT | TF_TEXTURE_CREATION_FLAG_VR_MULTIVIEW;
        fillColorsRenderTargetDesc.mFlags |= TF_TEXTURE_CREATION_FLAG_DCC | TF_TEXTURE_CREATION_FLAG_TEXTURE_COMPATABILITY;
        fillColorsRenderTargetDesc.pName = "Fill colors RT";
        addRenderTarget(pRenderer, &fillColorsRenderTargetDesc, &pRenderTargetFillColors);

        TF_ESRAM_BEGIN_ALLOC(pRenderer, "Depth Peeling", currentOffsetESRAM);
        TFRenderTargetDesc depthPeelingRenderTargetDesc = {};
        depthPeelingRenderTargetDesc.mWidth = mSettings.mWidth;
        depthPeelingRenderTargetDesc.mHeight = mSettings.mHeight;
        depthPeelingRenderTargetDesc.mDepth = 1;
        depthPeelingRenderTargetDesc.mArraySize = 1;
        depthPeelingRenderTargetDesc.mMipLevels = 1;
        depthPeelingRenderTargetDesc.mSampleCount = TF_SAMPLE_COUNT_1;
        depthPeelingRenderTargetDesc.mFormat = TinyImageFormat_R16_SFLOAT;
        depthPeelingRenderTargetDesc.mStartState = TF_RESOURCE_STATE_SHADER_RESOURCE;
        depthPeelingRenderTargetDesc.mClearValue.r = 1.0f;
        depthPeelingRenderTargetDesc.mClearValue.g = 1.0f;
        depthPeelingRenderTargetDesc.mClearValue.b = 1.0f;
        depthPeelingRenderTargetDesc.mClearValue.a = 1.0f;
        depthPeelingRenderTargetDesc.mSampleQuality = 0;
        depthPeelingRenderTargetDesc.mDescriptors = TF_DESCRIPTOR_TYPE_TEXTURE;
        depthPeelingRenderTargetDesc.mFlags = TF_TEXTURE_CREATION_FLAG_OWN_MEMORY_BIT | TF_TEXTURE_CREATION_FLAG_VR_MULTIVIEW;
        depthPeelingRenderTargetDesc.mFlags |= TF_TEXTURE_CREATION_FLAG_DCC | TF_TEXTURE_CREATION_FLAG_TEXTURE_COMPATABILITY;
        depthPeelingRenderTargetDesc.pName = "Depth peeling RT";
        addRenderTarget(pRenderer, &depthPeelingRenderTargetDesc, &pRenderTargetDepthPeeling);
    }

    void removeRenderTargets()
    {
        removeRenderTarget(pRenderer, pRenderTargetShadowMap);
        removeRenderTarget(pRenderer, pRenderTargetDepth);

        for (uint hairType = 0; hairType < HAIR_TYPE_COUNT; ++hairType)
        {
            for (int i = 0; i < MAX_NUM_DIRECTIONAL_LIGHTS; ++i)
                removeRenderTarget(pRenderer, pRenderTargetHairShadows[hairType][i]);
        }

        removeRenderTarget(pRenderer, pRenderTargetFillColors);

        if (gSupportTextureAtomics)
        {
            removeResource(pTextureHairDepth);
        }
        else
        {
            removeResource(pBufferHairDepth);
        }

        removeRenderTarget(pRenderer, pRenderTargetDepthPeeling);
    }

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
        swapChainDesc.mColorClearValue.r = 0.0f;
        swapChainDesc.mColorClearValue.g = 0.0f;
        swapChainDesc.mColorClearValue.b = 0.0f;
        swapChainDesc.mColorClearValue.a = 0.0f;
        swapChainDesc.mEnableVsync = mSettings.mVSyncEnabled;
        swapChainDesc.mFlags = TF_SWAP_CHAIN_CREATION_FLAG_ENABLE_2D_VR_LAYER;
        swapChainDesc.mVR.m2DLayer = gVR2DLayer;

        ::addSwapChain(pRenderer, &swapChainDesc, &pSwapChain);

        return pSwapChain != NULL;
    }

    //--------------------------------------------------------------------------------------------
    // UI
    //--------------------------------------------------------------------------------------------
    void updateDynamicUI()
    {
        // SCENE GUI
        if (UI_WINDOW_IS_VISIBLE(uiBeginWidgetWindow(&gGuiWindowMainDesc)))
        {
            uiLayoutAutoTextRows(2);
            uiLabel("Material Type", TF_ALIGN_LEFT);
            int itemCount = sizeof(gMaterialTypeNames) / sizeof(gMaterialTypeNames[0]);
            gMaterialType = UI_WIDGET_GET_SELECTED(uiDropdown(gMaterialTypeNames, itemCount, gMaterialType));
            gDiffuseReflectionModel = gMaterialLightingModelMap[gMaterialType];

            uiLayoutAutoTextRows(1);
            uiCheckbox("Animate Camera", &gbAnimateCamera);
            uiCheckbox("Skybox", &gDrawSkybox);

            if (UI_WIDGET_IS_VISIBLE(uiCollapsingHeaderBegin("Lighting Options", true)))
            {
                uiLayoutAutoTextRows(1);
                uiCheckbox("Environment Lighting", &gEnvironmentLighting);
                uiLayoutAutoTextRows(2);
                uiLabel("Environment Light Intensity", TF_ALIGN_LEFT);
                uiSliderFloat(&gEnvironmentLightingIntensity, 0.0f, 1.0f, 0.005f);
                uiLabel("Ambient Light Intensity", TF_ALIGN_LEFT);
                uiSliderFloat(&gAmbientLightIntensity, 0.0f, 1.0f, 0.005f);
                uiLabel("Directional Light Intensity", TF_ALIGN_LEFT);
                uiSliderFloat(&gDirectionalLightIntensity, 0.0f, 150.0f, 0.1f);
                uiLabel("Light Direction", TF_ALIGN_LEFT);
                uiSliderFloat3(&gDirectionalLightDirection, float3(-PI), float3(PI), float3(0.1f));
                uiLabel("Light Color", TF_ALIGN_LEFT);
                uiColorButton(&gDirectionalLightColor);

                uiCollapsingHeaderEnd();
            }

            // scripts
            uiLayoutAutoTextRows(1);
            if (UI_WIDGET_IS_PRESSED(uiButton("Reload script")))
            {
                ReloadScriptButtonCallback(NULL);
            }
            uiLayoutAutoTextRows(2);
            uiLabel("Test Scripts", TF_ALIGN_LEFT);
            itemCount = sizeof(gTestScripts) / sizeof(gTestScripts[0]);
            gCurrentScriptIndex = UI_WIDGET_GET_SELECTED(uiDropdown(gTestScripts, itemCount, gCurrentScriptIndex));
            uiLayoutAutoTextRows(1);
            if (UI_WIDGET_IS_PRESSED(uiButton("Run")))
            {
                RunScript(NULL);
            }
        }
        uiEndWidgetWindow();

        // MATERIAL PROPERTIES GUI
        if (UI_WINDOW_IS_VISIBLE(uiBeginWidgetWindow(&gGuiWindowMaterialDesc)))
        {
            uiLayoutAutoTextRows(2);
            if (gMaterialType == MATERIAL_WOOD)
            {
                uiLabel("Diffuse Reflection Model", TF_ALIGN_LEFT);
                int itemCount = sizeof(gDiffuseReflectionNames) / sizeof(gDiffuseReflectionNames[0]);
                gDiffuseReflectionModel = UI_WIDGET_GET_SELECTED(uiDropdown(gDiffuseReflectionNames, itemCount, gDiffuseReflectionModel));
                gMaterialLightingModelMap[MATERIAL_WOOD] = (EDiffuseReflectionModels)gDiffuseReflectionModel;
            }
            uiLabel("Render Mode", TF_ALIGN_LEFT);
            int itemCount = sizeof(gRenderModeNames) / sizeof(gRenderModeNames[0]);
            gRenderMode = UI_WIDGET_GET_SELECTED(uiDropdown(gRenderModeNames, itemCount, gRenderMode));
            uiLayoutAutoTextRows(1);
            uiCheckbox("Override Roughness", &gOverrideRoughnessTextures);
            uiLayoutAutoTextRows(2);
            uiLabel("Roughness", TF_ALIGN_LEFT);
            uiSliderFloat(&gRoughnessOverride, 0.04f, 1.0f, 0.01f);
            uiLayoutAutoTextRows(1);
            uiCheckbox("Disable Normal Maps", &gDisableNormalMaps);
            uiCheckbox("Enable vMF filtered Normal Maps", &gEnableVMFMaps);
            uiLayoutAutoTextRows(2);
            uiLabel("Normal Map Intensity", TF_ALIGN_LEFT);
            uiSliderFloat(&gNormalMapIntensity, 0.0f, 1.0f, 0.01f);
            uiLayoutAutoTextRows(1);
            uiCheckbox("Disable AO Maps", &gDisableAOMaps);
            uiLayoutAutoTextRows(2);
            uiLabel("AO Intensity", TF_ALIGN_LEFT);
            uiSliderFloat(&gAOIntensity, 0.0f, 1.0f, 0.001f);
        }
        uiEndWidgetWindow();

        // HAIR GUI
        if (gMaterialType == MATERIAL_HAIR)
        {
            if ((int)gMaterialType != gCurrentMaterialType)
            {
                gFirstHairSimulationFrame = true;
            }

            if (UI_WINDOW_IS_VISIBLE(uiBeginWidgetWindow(&gGuiWindowHairSimDesc)))
            {
                uiLayoutAutoTextRows(1);
                uiLabel("Hair Shading", TF_ALIGN_LEFT);
                uiLayoutAutoTextRows(2);
                uiLabel("Hair Color", TF_ALIGN_LEFT);
                gHairColor = UI_WIDGET_GET_SELECTED(uiDropdown(gHairColorNames, HAIR_COLOR_COUNT, gHairColor));
#if HAIR_DEV_UI
                uiLabel("Hair type", TF_ALIGN_LEFT);
                gCurrentHairType = UI_WIDGET_GET_SELECTED(uiDropdown(gHairNames, HAIR_TYPE_COUNT, gCurrentHairType));
                gHairType = gCurrentHairType;

                for (size_t j = 0; j < (size_t)gHairTypeIndicesCount[gHairType]; ++j)
                {
                    char headerName[32];
                    snprintf(headerName, 32, "Head %zu color", j);

                    if (UI_WIDGET_IS_VISIBLE(uiCollapsingHeaderBegin(headerName, true)))
                    {
                        uint                    k = gHairTypeIndices[gHairType][j];
                        UniformDataHairShading* hair = &gHair[k].mUniformDataHairShading;

                        uiLayoutAutoTextRows(2);
                        uiLabel("Root Color", TF_ALIGN_LEFT);
                        uiColorSliderRGBA8(&hair->mRootColor);
                        uiLabel("Strand Color", TF_ALIGN_LEFT);
                        uiColorSliderRGBA8(&hair->mStrandColor);
                        uiLabel("Color Bias", TF_ALIGN_LEFT);
                        uiSliderFloat(&hair->mColorBias, 0.0f, 10.0f, 0.01f);
                        uiLabel("Kd", TF_ALIGN_LEFT);
                        uiSliderFloat(&hair->mKDiffuse, 0.0f, 1.0f, 0.01f);
                        uiLabel("Ks1", TF_ALIGN_LEFT);
                        uiSliderFloat(&hair->mKSpecular1, 0.0f, 0.1f, 0.001f);
                        uiLabel("Ex1", TF_ALIGN_LEFT);
                        uiSliderFloat(&hair->mKExponent1, 0.0f, 128.0f, 0.01f);
                        uiLabel("Ks2", TF_ALIGN_LEFT);
                        uiSliderFloat(&hair->mKSpecular2, 0.0f, 0.1f, 0.001f);
                        uiLabel("Ex2", TF_ALIGN_LEFT);
                        uiSliderFloat(&hair->mKExponent2, 0.0f, 128.0f, 0.01f);
                        uiLabel("Strand Radius", TF_ALIGN_LEFT);
                        uiSliderFloat(&gHair[k].mStrandRadius, 0.0f, 1.0f, 0.01f);
                        uiLabel("Strand Spacing", TF_ALIGN_LEFT);
                        uiSliderFloat(&gHair[k].mStrandSpacing, 0.0f, 1.0f, 0.01f);

                        uiCollapsingHeaderEnd();
                    }
                }
#endif
                uiLayoutAutoTextRows(1);
                uiLabel("Hair Simulation", TF_ALIGN_LEFT);
                uiLayoutAutoTextRows(2);
                uiLabel("Gravity", TF_ALIGN_LEFT);
                uiSliderFloat3((float3*)&gUniformDataHairGlobal.mGravity, float3(-10.0f), float3(10.0f), float3(1.0f));
                uiLabel("Wind", TF_ALIGN_LEFT);
                uiSliderFloat3((float3*)&gUniformDataHairGlobal.mWind, float3(-1024.0f), float3(1024.0f), float3(1.0f));
#if HAIR_MAX_CAPSULE_COUNT > 0
                uiLayoutAutoTextRows(1);
                uiCheckbox("Show Collision Capsules", &gShowCapsules);
#endif

#if HAIR_DEV_UI
                if (UI_WIDGET_IS_VISIBLE(uiCollapsingHeaderBegin("Transforms", true)))
                {
                    for (size_t i = 0; i < gTransformCount; ++i)
                    {
                        Transform* transform = &gTransforms[i].mTransform;
                        char       headerName[32];
                        snprintf(headerName, 32, "Transform %zu", i);

                        uiLayoutAutoTextRows(1);
                        if (UI_WIDGET_IS_VISIBLE(uiCollapsingHeaderBegin(headerName, true)))
                        {
                            uiLayoutAutoTextRows(2);
                            uiLabel("Position", TF_ALIGN_LEFT);
                            uiSliderFloat3((float3*)&transform->mPosition, float3(-10.0f), float3(10.0f), float3(0.1f));
                            uiLabel("Orientation", TF_ALIGN_LEFT);
                            uiSliderFloat3((float3*)&transform->mOrientation, float3(-PI * 2.0f), float3(PI * 2.0f), float3(0.1f));
                            uiLabel("Scale", TF_ALIGN_LEFT);
                            uiSliderFloat(&transform->mScale, 0.001f, 1.0f, 0.001f);
                            uiLabel("Attached To Bone", TF_ALIGN_LEFT);
                            uiSliderInt(&gTransforms[i].mAttachedBone, -1, gAnimationRig.mNumJoints - 1, 1);
                            uiCollapsingHeaderEnd();
                        }
                    }
                    uiCollapsingHeaderEnd();
                }
#if HAIR_MAX_CAPSULE_COUNT > 0
                if (UI_WIDGET_IS_VISIBLE(uiCollapsingHeaderBegin("Capsules", true)))
                {
                    for (size_t i = 0; i < gCapsuleCount; ++i)
                    {
                        Capsule* capsule = &gCapsules[i].mCapsule;
                        char     headerName[32];
                        snprintf(headerName, 32, "Capsule %zu", i);

                        uiLayoutAutoTextRows(1);
                        if (UI_WIDGET_IS_VISIBLE(uiCollapsingHeaderBegin(headerName, true)))
                        {
                            uiLayoutAutoTextRows(2);
                            uiLabel("Center0", TF_ALIGN_LEFT);
                            uiSliderFloat3(&capsule->mCenter0, float3(-10.0f), float3(10.0f), float3(0.1f));
                            uiLabel("Radius0", TF_ALIGN_LEFT);
                            uiSliderFloat(&capsule->mRadius0, 0.0f, 10.0f, 0.01f);
                            uiLabel("Center1", TF_ALIGN_LEFT);
                            uiSliderFloat3(&capsule->mCenter1, float3(-10.0f), float3(10.0f), float3(0.1f));
                            uiLabel("Radius1", TF_ALIGN_LEFT);
                            uiSliderFloat(&capsule->mRadius1, 0.0f, 10.0f, 0.01f);
                            uiLabel("Attached To Bone", TF_ALIGN_LEFT);
                            uiSliderInt(&gCapsules[i].mAttachedBone, -1, gAnimationRig.mNumJoints - 1, 1);
                            uiCollapsingHeaderEnd();
                        }
                    }
                    uiCollapsingHeaderEnd();
                }
#endif
                for (uint i = 0; i < HAIR_TYPE_COUNT; ++i)
                {
                    for (size_t j = 0; j < (size_t)gHairTypeIndicesCount[i]; ++j)
                    {
                        uint                       k = gHairTypeIndices[i][j];
                        UniformDataHairSimulation* hair = &gHair[k].mUniformDataHairSimulation;
                        char                       headerLabel[32];
                        snprintf(headerLabel, sizeof(headerLabel), "Hair %u", k);

                        uiLayoutAutoTextRows(1);
                        if (UI_WIDGET_IS_VISIBLE(uiCollapsingHeaderBegin(headerLabel, true)))
                        {
                            uiLayoutAutoTextRows(2);
                            uiLabel("Damping", TF_ALIGN_LEFT);
                            uiSliderFloat(&hair->mDamping, 0.0f, 0.1f, 0.001f);
                            uiLabel("Global Constraint Stiffness", TF_ALIGN_LEFT);
                            uiSliderFloat(&hair->mGlobalConstraintStiffness, 0.0f, 0.1f, 0.001f);
                            uiLabel("Global Constraint Range", TF_ALIGN_LEFT);
                            uiSliderFloat(&hair->mGlobalConstraintRange, 0.0f, 1.0f, 0.01f);
                            uiLabel("Shock Propagation Strength", TF_ALIGN_LEFT);
                            uiSliderFloat(&hair->mShockPropagationStrength, 0.0f, 1.0f, 0.01f);
                            uiLabel("Shock Propagation Acceleration Threshold", TF_ALIGN_LEFT);
                            uiSliderFloat(&hair->mShockPropagationAccelerationThreshold, 0.0f, 10.0f, 0.01f);
                            uiLabel("Local Stiffness", TF_ALIGN_LEFT);
                            uiSliderFloat(&hair->mLocalStiffness, 0.0f, 1.0f, 0.01f);
                            uiLabel("Local Constraint Iterations", TF_ALIGN_LEFT);
                            uiSliderUint(&hair->mLocalConstraintIterations, 0, 32, 1);
                            uiLabel("Length Constraint Iterations", TF_ALIGN_LEFT);
                            uiSliderUint(&hair->mLengthConstraintIterations, 1, 32, 1);

                            uiCollapsingHeaderEnd();
                        }
                    }
                }
#endif
            }
            uiEndWidgetWindow();
        }

        gCurrentMaterialType = (MaterialType)gMaterialType;
    }

    void luaRegisterGui()
    {
        TFLuaWidgetVariableDesc luaVarDesc = {};
        TFLuaWidgetFunctionDesc luaFuncDesc = {};

        // Dropdowns
        luaVarDesc.mWidgetType = TF_WIDGET_TYPE_DROPDOWN;
        luaVarDesc.pLabel = "Material Type";
        luaVarDesc.pUint = &gMaterialType;
        luaRegisterWidgetVariable(&luaVarDesc);
        luaVarDesc.pLabel = "Render Mode";
        luaVarDesc.pUint = &gRenderMode;
        luaRegisterWidgetVariable(&luaVarDesc);
        luaVarDesc.pLabel = "Hair Color";
        luaVarDesc.pUint = &gHairColor;
        luaRegisterWidgetVariable(&luaVarDesc);
#if HAIR_DEV_UI
        luaVarDesc.pLabel = "Hair type";
        luaVarDesc.pUint = &gCurrentHairType;
        luaRegisterWidgetVariable(&luaVarDesc);
#endif
        luaVarDesc.pLabel = "Test Scripts";
        luaVarDesc.pUint = &gCurrentScriptIndex;
        luaRegisterWidgetVariable(&luaVarDesc);

        // Checkboxes
        luaVarDesc.mWidgetType = TF_WIDGET_TYPE_CHECKBOX;
        luaVarDesc.pLabel = "Animate Camera";
        luaVarDesc.pBool = &gbAnimateCamera;
        luaRegisterWidgetVariable(&luaVarDesc);
        luaVarDesc.pLabel = "Skybox";
        luaVarDesc.pBool = &gDrawSkybox;
        luaRegisterWidgetVariable(&luaVarDesc);
        luaVarDesc.pLabel = "Environment Lighting";
        luaVarDesc.pBool = &gEnvironmentLighting;
        luaRegisterWidgetVariable(&luaVarDesc);
        luaVarDesc.pLabel = "Override Roughness";
        luaVarDesc.pBool = &gOverrideRoughnessTextures;
        luaRegisterWidgetVariable(&luaVarDesc);
        luaVarDesc.pLabel = "Disable Normal Maps";
        luaVarDesc.pBool = &gDisableNormalMaps;
        luaRegisterWidgetVariable(&luaVarDesc);
        luaVarDesc.pLabel = "Enable vMF filtered Normal Maps";
        luaVarDesc.pBool = &gEnableVMFMaps;
        luaRegisterWidgetVariable(&luaVarDesc);
        luaVarDesc.pLabel = "Disable AO Maps";
        luaVarDesc.pBool = &gDisableAOMaps;
        luaRegisterWidgetVariable(&luaVarDesc);

        // Buttons
        luaFuncDesc.mType = TF_LUA_WIDGET_FUNCTION_ON_EDITED;
        luaFuncDesc.pLabel = "Reload script";
        luaFuncDesc.pFunc = ReloadScriptButtonCallback;
        luaRegisterWidgetFunction(&luaFuncDesc);
        luaFuncDesc.pLabel = "Run"; // script
        luaFuncDesc.pFunc = RunScript;
        luaRegisterWidgetFunction(&luaFuncDesc);

        // float sliders
        luaVarDesc.mWidgetType = TF_WIDGET_TYPE_SLIDER_FLOAT;
        luaVarDesc.pLabel = "Environment Light Intensity";
        luaVarDesc.pFloat = &gEnvironmentLightingIntensity;
        luaRegisterWidgetVariable(&luaVarDesc);
        luaVarDesc.pLabel = "Ambient Light Intensity";
        luaVarDesc.pFloat = &gAmbientLightIntensity;
        luaRegisterWidgetVariable(&luaVarDesc);
        luaVarDesc.pLabel = "Directional Light Intensity";
        luaVarDesc.pFloat = &gDirectionalLightIntensity;
        luaRegisterWidgetVariable(&luaVarDesc);
        luaVarDesc.pLabel = "Roughness";
        luaVarDesc.pFloat = &gRoughnessOverride;
        luaRegisterWidgetVariable(&luaVarDesc);
        luaVarDesc.pLabel = "Normal Map Intensity";
        luaVarDesc.pFloat = &gNormalMapIntensity;
        luaRegisterWidgetVariable(&luaVarDesc);
        luaVarDesc.pLabel = "AO Intensity";
        luaVarDesc.pFloat = &gAOIntensity;
        luaRegisterWidgetVariable(&luaVarDesc);

        // float3 sliders
        luaVarDesc.mWidgetType = TF_WIDGET_TYPE_SLIDER_FLOAT3;
        luaVarDesc.pLabel = "Light Direction";
        luaVarDesc.pFloat3 = &gDirectionalLightDirection;
        luaRegisterWidgetVariable(&luaVarDesc);

        // Color pickers
        luaVarDesc.mWidgetType = TF_WIDGET_TYPE_COLOR_PICKER;
        luaVarDesc.pLabel = "Light Color";
        luaVarDesc.pFloat4 = &gDirectionalLightColor;
        luaRegisterWidgetVariable(&luaVarDesc);
    }
};

DEFINE_APPLICATION_MAIN(MaterialPlayground)
