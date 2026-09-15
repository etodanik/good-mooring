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

#include "Interfaces/IGraphicsConfig.h"

#include "../Utilities/ThirdParty/OpenSource/Nothings/stb_ds.h"
#include "../Utilities/ThirdParty/OpenSource/bstrlib/bstrlib.h"
#include "../Utilities/Interfaces/IMath.h"

#include "../Utilities/Interfaces/IMemory.h"
#include "../Resources/ResourceLoader/ThirdParty/OpenSource/tinyimageformat/tinyimageformat_query.h"
// tinyimageformat_query.h declares TinyImageFormat_FromName as C99 'inline' (not 'static inline').
// In C compilation units the compiler is not required to emit a callable out-of-line symbol.
// This extern redeclaration forces an external definition.
extern TinyImageFormat TinyImageFormat_FromName(char const* p);

/* ------------------------ gpu.data ------------------------ */
#define MAX_GPU_VENDOR_COUNT                64
#define MAX_GPU_VENDOR_IDENTIFIER_LENGTH    20
#define MAX_IDENTIFIER_PER_GPU_VENDOR_COUNT 8
#define FORMAT_CAPABILITY_COUNT             7
#define SCENE_RESOLUTION_COUNT              16

typedef enum GPUTarget
{
    GPU_TARGET_UNDEFINED,
    GPU_TARGET_CONSOLE_BEGIN,
    GPU_TARGET_X1 = GPU_TARGET_CONSOLE_BEGIN,
    GPU_TARGET_X1X,
    GPU_TARGET_XSS,
    GPU_TARGET_XSX,
    GPU_TARGET_ORBIS,
    GPU_TARGET_NEO,
    GPU_TARGET_PROSPERO,
    GPU_TARGET_CONSOLE_END = GPU_TARGET_PROSPERO,
    GPU_TARGET_MOBILE_BEGIN,
    GPU_TARGET_ANDROID = GPU_TARGET_MOBILE_BEGIN,
    GPU_TARGET_IOS,
    GPU_TARGET_SWITCH,
    GPU_TARGET_MOBILE_END = GPU_TARGET_SWITCH,
    GPU_TARGET_VR_BEGIN,
    GPU_TARGET_QUEST = GPU_TARGET_VR_BEGIN,
    GPU_TARGET_QUEST2,
    GPU_TARGET_QUEST3,
    GPU_TARGET_HOLOLENS2,
    GPU_TARGET_VR_END = GPU_TARGET_HOLOLENS2,
    GPU_TARGET_STEAM_DECK,
} GPUTarget;

static GPUTarget             stringToTarget(const char* targetStr);
static GPUTarget             getGPUTarget(uint32_t vendorId, uint32_t modelId);
static TFWaveOpsSupportFlags stringToWaveOpsSupportFlags(const char* str);
static TFShaderStage         stringToShaderStage(const char* str);
static const char*           waveOpsSupportFlagsToString(uint32_t flags);
static const char*           shaderStageToString(uint32_t stage);

typedef struct GPUVendorDefinition
{
    char     vendorName[TF_MAX_GPU_VENDOR_STRING_LENGTH];
    uint32_t identifierArray[MAX_IDENTIFIER_PER_GPU_VENDOR_COUNT];
    uint32_t identifierCount;
} GPUVendorDefinition;

typedef struct GPUModelDefinition
{
    uint32_t         mVendorId;
    uint32_t         mDeviceId;
    TFGPUPresetLevel mPreset;
    GPUTarget        mTarget;
    char             mModelName[TF_MAX_GPU_VENDOR_STRING_LENGTH];
} GPUModelDefinition;

TFGPUSelection   gGpuSelection;
TFGPUPresetLevel gDefaultPresetLevel;
TFResolution     gAvailableSceneResolutions[SCENE_RESOLUTION_COUNT] = { { 0 } };
uint32_t         gAvailableSceneResolutionCount = 0;
uint32_t         gSceneResolutionIndex = 0;
GPUTarget        gDefaultTarget = GPU_TARGET_UNDEFINED;

#if defined(SUPPORT_LOW_ENERGY_MODE)
extern bool gLowEnergyMode;
#endif

///////////////////////////////////////////////////////////
// HELPER DECLARATIONS
#define INVALID_OPTION                  UINT_MAX
#define MAXIMUM_GPU_COMPARISON_CHOICES  256
#define MAXIMUM_GPU_CONFIGURATION_RULES 256
#define MAXIMUM_TEXTURE_SUPPORT_RULES   128

#define GPUCFG_VERSION_MAJOR            0
#define GPUCFG_VERSION_MINOR            3

typedef uint64_t (*PropertyRead)(const TFGpuDesc* pSetting);
typedef void (*PropertyWrite)(TFGpuDesc* pSetting, uint64_t value);
typedef uint32_t (*PropertyStringToEnum)(const char* name);
typedef const char* (*EnumToPropertyString)(uint32_t);

// DEFINE_GPU_PROP_* generate a Read_<token>/Write_<token> function pair per property.
// 'token' must be a valid C identifier; use an alias for nested fields (e.g. mPresetLevel for mGpuVendorPreset.mPresetLevel).
//
// Assigning uint64_t to a narrower field (enum, bit-field, uint32_t) triggers C4244 on MSVC
// and -Wshorten-64-to-32 / -Wimplicit-int-conversion on Clang; suppressed via compiler flags.
#define WRITE_GPU_PROP(field, v) (field) = (v)

#define DEFINE_GPU_PROP_RW(token, field)                                              \
    static uint64_t Read_##token(const TFGpuDesc* s) { return (uint64_t)(s->field); } \
    static void     Write_##token(TFGpuDesc* s, uint64_t v) { WRITE_GPU_PROP(s->field, v); }

// V627: ASSERT(false) expands to sizeof(false) in release; suppressed here since it is intentional (compile-time reachability check).
//-V:DEFINE_GPU_PROP_RO:627
#define DEFINE_GPU_PROP_RO(token, field)                                                    \
    static uint64_t Read_##token(const TFGpuDesc* s) { return (uint64_t)(s->field); }       \
    static void     Write_##token(TFGpuDesc* s, uint64_t v)                                 \
    {                                                                                       \
        UNREF_PARAM(s);                                                                     \
        UNREF_PARAM(v);                                                                     \
        LOGF(eDEBUG, "GPUConfig: Read-only setting " #token " cannot be set from gpu.cfg"); \
        ASSERT(false);                                                                      \
    }

#define DEFINE_GPU_PROP_GLOBAL_RW(token, var)           \
    static uint64_t Read_##token(const TFGpuDesc* s)    \
    {                                                   \
        UNREF_PARAM(s);                                 \
        return (uint64_t)(var);                         \
    }                                                   \
    static void Write_##token(TFGpuDesc* s, uint64_t v) \
    {                                                   \
        UNREF_PARAM(s);                                 \
        WRITE_GPU_PROP(var, v);                         \
    }

//-V:DEFINE_GPU_PROP_GLOBAL_RO:627
#define DEFINE_GPU_PROP_GLOBAL_RO(token, var)                                               \
    static uint64_t Read_##token(const TFGpuDesc* s)                                        \
    {                                                                                       \
        UNREF_PARAM(s);                                                                     \
        return (uint64_t)(var);                                                             \
    }                                                                                       \
    static void Write_##token(TFGpuDesc* s, uint64_t v)                                     \
    {                                                                                       \
        UNREF_PARAM(s);                                                                     \
        UNREF_PARAM(v);                                                                     \
        LOGF(eDEBUG, "GPUConfig: Read-only setting " #token " cannot be set from gpu.cfg"); \
        ASSERT(false);                                                                      \
    }

#define GPU_CONFIG_PROPERTY(name, token, ...)                  { name, Read_##token, Write_##token, ##__VA_ARGS__ }
#define GPU_CONFIG_PROPERTY_READ_ONLY(name, token, ...)        { name, Read_##token, Write_##token, ##__VA_ARGS__ }
#define GPU_CONFIG_PROPERTY_GLOBAL(name, token, ...)           { name, Read_##token, Write_##token, ##__VA_ARGS__ }
#define GPU_CONFIG_PROPERTY_READ_ONLY_GLOBAL(name, token, ...) { name, Read_##token, Write_##token, ##__VA_ARGS__ }

typedef struct GPUProperty
{
    const char*          name;
    PropertyRead         readValue;
    PropertyWrite        writeValue;
    PropertyStringToEnum strToEnum;
    EnumToPropertyString enumToStr;
} GPUProperty;

static uint32_t StrToPresetLevel(const char* s) { return (uint32_t)stringToPresetLevel(s); }
static uint32_t StrToWaveOpsFlags(const char* s) { return (uint32_t)stringToWaveOpsSupportFlags(s); }
static uint32_t StrToShaderStage(const char* s) { return (uint32_t)stringToShaderStage(s); }
static uint32_t StrToGPUTarget(const char* s) { return (uint32_t)stringToTarget(s); }

// One function pair per property — generated by the DEFINE macros above.
// Nested field aliases: mModelId = mGpuVendorPreset.mModelId, mPresetLevel = mGpuVendorPreset.mPresetLevel,
//                       mVendorId = mGpuVendorPreset.mVendorId
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4244) // uint64_t -> narrower field type, intentional truncation
#endif
DEFINE_GPU_PROP_RW(mAllowBufferTextureInSameHeap, mAllowBufferTextureInSameHeap)
DEFINE_GPU_PROP_RW(mAmdAsicFamily, mAmdAsicFamily)
DEFINE_GPU_PROP_RW(mBuiltinDrawID, mBuiltinDrawID)
#if defined(METAL)
DEFINE_GPU_PROP_RW(mCubeMapTextureArraySupported, mCubeMapTextureArraySupported)
DEFINE_GPU_PROP_RW(mTessellationIndirectDrawSupported, mTessellationIndirectDrawSupported)
#if !defined(TARGET_IOS)
DEFINE_GPU_PROP_RW(mIsHeadLess, mIsHeadLess)
#endif
#endif
DEFINE_GPU_PROP_RO(mModelId, mGpuVendorPreset.mModelId)
#if defined(DIRECT3D12)
DEFINE_GPU_PROP_RW(mFeatureLevel, mFeatureLevel)
DEFINE_GPU_PROP_RW(mSuppressInvalidSubresourceStateAfterExit, mSuppressInvalidSubresourceStateAfterExit)
DEFINE_GPU_PROP_RW(mIsIntelIntegrated, mIsIntelIntegrated)
#endif
DEFINE_GPU_PROP_RW(mWorkgraphSupported, mWorkgraphSupported)
DEFINE_GPU_PROP_RW(mGeometryShaderSupported, mGeometryShaderSupported)
DEFINE_GPU_PROP_RW(mPresetLevel, mGpuVendorPreset.mPresetLevel)
DEFINE_GPU_PROP_RW(mGraphicsQueueSupported, mGraphicsQueueSupported)
DEFINE_GPU_PROP_RW(mHDRSupported, mHDRSupported)
#if defined(VULKAN)
DEFINE_GPU_PROP_RW(mVulkanVersionMinor, mVulkanVersionMinor)
DEFINE_GPU_PROP_RW(mVulkanVersionMajor, mVulkanVersionMajor)
DEFINE_GPU_PROP_RW(mDynamicRenderingSupported, mDynamicRenderingSupported)
DEFINE_GPU_PROP_RW(mXclipseTransferQueueWorkaround, mXclipseTransferQueueWorkaround)
DEFINE_GPU_PROP_RW(mDeviceMemoryReportCrashWorkaround, mDeviceMemoryReportCrashWorkaround)
DEFINE_GPU_PROP_RW(mYCbCrExtension, mYCbCrExtension)
DEFINE_GPU_PROP_RW(mFillModeNonSolid, mFillModeNonSolid)
DEFINE_GPU_PROP_RW(mKHRRayQueryExtension, mKHRRayQueryExtension)
DEFINE_GPU_PROP_RW(mAMDGCNShaderExtension, mAMDGCNShaderExtension)
DEFINE_GPU_PROP_RW(mAMDDrawIndirectCountExtension, mAMDDrawIndirectCountExtension)
DEFINE_GPU_PROP_RW(mAMDShaderInfoExtension, mAMDShaderInfoExtension)
DEFINE_GPU_PROP_RW(mDescriptorIndexingExtension, mDescriptorIndexingExtension)
DEFINE_GPU_PROP_RW(mDynamicRenderingExtension, mDynamicRenderingExtension)
DEFINE_GPU_PROP_RW(mNonUniformResourceIndexSupported, mNonUniformResourceIndexSupported)
DEFINE_GPU_PROP_RW(mBufferDeviceAddressSupported, mBufferDeviceAddressSupported)
DEFINE_GPU_PROP_RW(mDrawIndirectCountExtension, mDrawIndirectCountExtension)
DEFINE_GPU_PROP_RW(mDedicatedAllocationExtension, mDedicatedAllocationExtension)
DEFINE_GPU_PROP_RW(mDebugMarkerExtension, mDebugMarkerExtension)
DEFINE_GPU_PROP_RW(mMemoryReq2Extension, mMemoryReq2Extension)
DEFINE_GPU_PROP_RW(mFragmentShaderInterlockExtension, mFragmentShaderInterlockExtension)
DEFINE_GPU_PROP_RW(mBufferDeviceAddressExtension, mBufferDeviceAddressExtension)
DEFINE_GPU_PROP_RW(mAccelerationStructureExtension, mAccelerationStructureExtension)
DEFINE_GPU_PROP_RW(mRayTracingPipelineExtension, mRayTracingPipelineExtension)
DEFINE_GPU_PROP_RW(mRayQueryExtension, mRayQueryExtension)
DEFINE_GPU_PROP_RW(mShaderAtomicInt64Extension, mShaderAtomicInt64Extension)
DEFINE_GPU_PROP_RW(mBufferDeviceAddressFeature, mBufferDeviceAddressFeature)
DEFINE_GPU_PROP_RW(mShaderFloatControlsExtension, mShaderFloatControlsExtension)
DEFINE_GPU_PROP_RW(mSpirv14Extension, mSpirv14Extension)
DEFINE_GPU_PROP_RW(mDeferredHostOperationsExtension, mDeferredHostOperationsExtension)
DEFINE_GPU_PROP_RW(mDeviceFaultExtension, mDeviceFaultExtension)
DEFINE_GPU_PROP_RW(mDeviceFaultSupported, mDeviceFaultSupported)
DEFINE_GPU_PROP_RW(mASTCDecodeModeExtension, mASTCDecodeModeExtension)
DEFINE_GPU_PROP_RW(mDeviceMemoryReportExtension, mDeviceMemoryReportExtension)
DEFINE_GPU_PROP_RW(mAMDBufferMarkerExtension, mAMDBufferMarkerExtension)
DEFINE_GPU_PROP_RW(mAMDDeviceCoherentMemoryExtension, mAMDDeviceCoherentMemoryExtension)
DEFINE_GPU_PROP_RW(mAMDDeviceCoherentMemorySupported, mAMDDeviceCoherentMemorySupported)
DEFINE_GPU_PROP_RW(mShaderViewportIndexLayerExtension, mShaderViewportIndexLayerExtension)
#if defined(VK_USE_PLATFORM_WIN32_KHR)
DEFINE_GPU_PROP_RW(mExternalMemoryExtension, mExternalMemoryExtension)
DEFINE_GPU_PROP_RW(mExternalMemoryWin32Extension, mExternalMemoryWin32Extension)
#endif
#if defined(QUEST_VR)
DEFINE_GPU_PROP_RW(mMultiviewExtension, mMultiviewExtension)
#endif
#endif
DEFINE_GPU_PROP_RW(mIndirectCommandBuffer, mIndirectCommandBuffer)
DEFINE_GPU_PROP_RW(mIndirectRootConstant, mIndirectRootConstant)
DEFINE_GPU_PROP_RW(mMaxBoundTextures, mMaxBoundTextures)
DEFINE_GPU_PROP_RW(mMaxStorageBufferSize, mMaxStorageBufferSize)
DEFINE_GPU_PROP_RW(mMaxTotalComputeThreads, mMaxTotalComputeThreads)
#if defined(DIRECT3D12)
DEFINE_GPU_PROP_RW(mMaxRootSignatureDWORDS, mMaxRootSignatureDWORDS)
#endif
DEFINE_GPU_PROP_RW(mMaxVertexInputBindings, mMaxVertexInputBindings)
DEFINE_GPU_PROP_RW(mMultiDrawIndirect, mMultiDrawIndirect)
DEFINE_GPU_PROP_RW(mOcclusionQueries, mOcclusionQueries)
DEFINE_GPU_PROP_RW(mPipelineStatsQueries, mPipelineStatsQueries)
DEFINE_GPU_PROP_RW(mPrimitiveIdSupported, mPrimitiveIdSupported)
DEFINE_GPU_PROP_RW(mPrimitiveIdPsSupported, mPrimitiveIdPsSupported)
DEFINE_GPU_PROP_RW(mROVsSupported, mROVsSupported)
DEFINE_GPU_PROP_RW(mRaytracingSupported, mRaytracingSupported)
DEFINE_GPU_PROP_RW(mUnifiedMemorySupport, mUnifiedMemorySupport)
DEFINE_GPU_PROP_RW(mRayQuerySupported, mRayQuerySupported)
DEFINE_GPU_PROP_RW(mRayPipelineSupported, mRayPipelineSupported)
DEFINE_GPU_PROP_RW(mSoftwareVRSSupported, mSoftwareVRSSupported)
DEFINE_GPU_PROP_RW(mTessellationSupported, mTessellationSupported)
DEFINE_GPU_PROP_RW(mCoopVectorsSupported, mCoopVectorsSupported)
DEFINE_GPU_PROP_RW(mTimestampQueries, mTimestampQueries)
DEFINE_GPU_PROP_RW(mUniformBufferAlignment, mUniformBufferAlignment)
DEFINE_GPU_PROP_RW(mUploadBufferTextureAlignment, mUploadBufferTextureAlignment)
DEFINE_GPU_PROP_RW(mUploadBufferTextureRowAlignment, mUploadBufferTextureRowAlignment)
DEFINE_GPU_PROP_RO(mVendorId, mGpuVendorPreset.mVendorId)
DEFINE_GPU_PROP_RW(mVRAM, mVRAM)
DEFINE_GPU_PROP_RW(mWaveLaneCount, mWaveLaneCount)
DEFINE_GPU_PROP_RW(mWaveOpsSupportFlags, mWaveOpsSupportFlags)
DEFINE_GPU_PROP_RW(mWaveOpsSupportedStageFlags, mWaveOpsSupportedStageFlags)
DEFINE_GPU_PROP_RW(m64BitAtomicsSupported, m64BitAtomicsSupported)
DEFINE_GPU_PROP_GLOBAL_RW(gSceneResolutionIndex, gSceneResolutionIndex)
DEFINE_GPU_PROP_RW(mGPUTarget, mGPUTarget)
#if defined(SUPPORT_LOW_ENERGY_MODE)
DEFINE_GPU_PROP_GLOBAL_RO(gLowEnergyMode, gLowEnergyMode)
#endif
DEFINE_GPU_PROP_RW(mTargetFPS, mTargetFPS)

#ifdef _MSC_VER
#pragma warning(pop)
#endif

// should we enable all setter? modifying the model or vendor id for example...
const GPUProperty availableGpuProperties[] = {
    GPU_CONFIG_PROPERTY("allowbuffertextureinsameheap", mAllowBufferTextureInSameHeap),
    GPU_CONFIG_PROPERTY("amdasicfamily", mAmdAsicFamily),
    GPU_CONFIG_PROPERTY("builtindrawid", mBuiltinDrawID),
#if defined(METAL)
    GPU_CONFIG_PROPERTY("cubemaptexturearraysupported", mCubeMapTextureArraySupported),
    GPU_CONFIG_PROPERTY("tessellationindirectdrawsupported", mTessellationIndirectDrawSupported),
#if !defined(TARGET_IOS)
    GPU_CONFIG_PROPERTY("isheadless", mIsHeadLess),
#endif
#endif
    GPU_CONFIG_PROPERTY_READ_ONLY("deviceid", mModelId),
#if defined(DIRECT3D12)
    GPU_CONFIG_PROPERTY("directxfeaturelevel", mFeatureLevel),
    GPU_CONFIG_PROPERTY("suppressinvalidsubresourcestateafterexit", mSuppressInvalidSubresourceStateAfterExit),
    GPU_CONFIG_PROPERTY("isintelintegrated", mIsIntelIntegrated),
#endif
    GPU_CONFIG_PROPERTY("workgraphsupported", mWorkgraphSupported),
    GPU_CONFIG_PROPERTY("geometryshadersupported", mGeometryShaderSupported),
    GPU_CONFIG_PROPERTY("gpupresetlevel", mPresetLevel, StrToPresetLevel),
    GPU_CONFIG_PROPERTY("graphicqueuesupported", mGraphicsQueueSupported),
    GPU_CONFIG_PROPERTY("hdrsupported", mHDRSupported),
#if defined(VULKAN)
    GPU_CONFIG_PROPERTY("vulkanversionminor", mVulkanVersionMinor),
    GPU_CONFIG_PROPERTY("vulkanversionmajor", mVulkanVersionMajor),

    GPU_CONFIG_PROPERTY("dynamicrenderingenabled", mDynamicRenderingSupported),
    GPU_CONFIG_PROPERTY("xclipsetransferqueueworkaroundenabled", mXclipseTransferQueueWorkaround),
    GPU_CONFIG_PROPERTY("devicememoryreportcrashworkaround", mDeviceMemoryReportCrashWorkaround),

    GPU_CONFIG_PROPERTY("ycbcrextension", mYCbCrExtension),
    GPU_CONFIG_PROPERTY("fillmodenonSolid", mFillModeNonSolid),
    GPU_CONFIG_PROPERTY("khrrayqueryextension", mKHRRayQueryExtension),
    GPU_CONFIG_PROPERTY("amdgcnshaderextension", mAMDGCNShaderExtension),
    GPU_CONFIG_PROPERTY("mamddrawindirectcountextension", mAMDDrawIndirectCountExtension),
    GPU_CONFIG_PROPERTY("mamdshaderinfoextension", mAMDShaderInfoExtension),
    GPU_CONFIG_PROPERTY("descriptorindexingextension", mDescriptorIndexingExtension),
    GPU_CONFIG_PROPERTY("dynamicrenderingextension", mDynamicRenderingExtension),
    GPU_CONFIG_PROPERTY("nonuniformresourceindexsupported", mNonUniformResourceIndexSupported),
    GPU_CONFIG_PROPERTY("bufferdeviceaddresssupported", mBufferDeviceAddressSupported),
    GPU_CONFIG_PROPERTY("drawindirectcountextension", mDrawIndirectCountExtension),
    GPU_CONFIG_PROPERTY("dedicatedallocationextension", mDedicatedAllocationExtension),
    GPU_CONFIG_PROPERTY("debugmarkerextension", mDebugMarkerExtension),
    GPU_CONFIG_PROPERTY("memoryreq2extension", mMemoryReq2Extension),
    GPU_CONFIG_PROPERTY("fragmentshaderinterlockextension", mFragmentShaderInterlockExtension),
    GPU_CONFIG_PROPERTY("bufferdeviceaddressextension", mBufferDeviceAddressExtension),
    GPU_CONFIG_PROPERTY("accelerationstructureextension", mAccelerationStructureExtension),
    GPU_CONFIG_PROPERTY("raytracingpipelineextension", mRayTracingPipelineExtension),
    GPU_CONFIG_PROPERTY("rayqueryextension", mRayQueryExtension),
    GPU_CONFIG_PROPERTY("shaderatomicint64extension", mShaderAtomicInt64Extension),
    GPU_CONFIG_PROPERTY("bufferdeviceaddressfeature", mBufferDeviceAddressFeature),
    GPU_CONFIG_PROPERTY("shaderfloatcontrolsextension", mShaderFloatControlsExtension),
    GPU_CONFIG_PROPERTY("spirv14extension", mSpirv14Extension),
    GPU_CONFIG_PROPERTY("deferredhostoperationsextension", mDeferredHostOperationsExtension),
    GPU_CONFIG_PROPERTY("devicefaultextension", mDeviceFaultExtension),
    GPU_CONFIG_PROPERTY("deviceFaultSupported", mDeviceFaultSupported),
    GPU_CONFIG_PROPERTY("astcdecodemodeextension", mASTCDecodeModeExtension),
    GPU_CONFIG_PROPERTY("devicememoryreportextension", mDeviceMemoryReportExtension),
    GPU_CONFIG_PROPERTY("amdbuffermarkerextension", mAMDBufferMarkerExtension),
    GPU_CONFIG_PROPERTY("amddevicecoherentmemoryextension", mAMDDeviceCoherentMemoryExtension),
    GPU_CONFIG_PROPERTY("amddevicecoherentmemorysupported", mAMDDeviceCoherentMemorySupported),
    GPU_CONFIG_PROPERTY("shaderviewportindexlayerextension", mShaderViewportIndexLayerExtension),
#if defined(VK_USE_PLATFORM_WIN32_KHR)
    GPU_CONFIG_PROPERTY("externalmemoryextension", mExternalMemoryExtension),
    GPU_CONFIG_PROPERTY("externalmemorywin32extension", mExternalMemoryWin32Extension),
#endif
#if defined(QUEST_VR)
    GPU_CONFIG_PROPERTY("multiviewextension", mMultiviewExtension),
#endif

#endif
    GPU_CONFIG_PROPERTY("indirectcommandbuffer", mIndirectCommandBuffer),
    GPU_CONFIG_PROPERTY("indirectrootconstant", mIndirectRootConstant),
    GPU_CONFIG_PROPERTY("maxboundtextures", mMaxBoundTextures),
    GPU_CONFIG_PROPERTY("maxstoragebuffersize", mMaxStorageBufferSize),
    GPU_CONFIG_PROPERTY("maxtotalcomputethreads", mMaxTotalComputeThreads),
#if defined(DIRECT3D12)
    GPU_CONFIG_PROPERTY("maxrootsignaturedwords", mMaxRootSignatureDWORDS),
#endif
    GPU_CONFIG_PROPERTY("maxvertexinputbindings", mMaxVertexInputBindings),
    GPU_CONFIG_PROPERTY("multidrawindirect", mMultiDrawIndirect),
    GPU_CONFIG_PROPERTY("occlusionqueries", mOcclusionQueries),
    GPU_CONFIG_PROPERTY("pipelinestatsqueries", mPipelineStatsQueries),
    GPU_CONFIG_PROPERTY("primitiveidsupported", mPrimitiveIdSupported),
    GPU_CONFIG_PROPERTY("primitiveidpssupported", mPrimitiveIdPsSupported),
    GPU_CONFIG_PROPERTY("rasterorderviewsupport", mROVsSupported),
    GPU_CONFIG_PROPERTY("raytracingsupported", mRaytracingSupported),
    GPU_CONFIG_PROPERTY("unifiedmemorysupport", mUnifiedMemorySupport),
    GPU_CONFIG_PROPERTY("rayquerysupported", mRayQuerySupported),
    GPU_CONFIG_PROPERTY("raypipelinesupported", mRayPipelineSupported),
    GPU_CONFIG_PROPERTY("softwarevrssupported", mSoftwareVRSSupported),
    GPU_CONFIG_PROPERTY("tessellationsupported", mTessellationSupported),
    GPU_CONFIG_PROPERTY("coopvectorssupported", mCoopVectorsSupported),
    GPU_CONFIG_PROPERTY("timestampqueries", mTimestampQueries),
    GPU_CONFIG_PROPERTY("uniformbufferalignment", mUniformBufferAlignment),
    GPU_CONFIG_PROPERTY("uploadbuffertexturealignment", mUploadBufferTextureAlignment),
    GPU_CONFIG_PROPERTY("uploadbuffertexturerowalignment", mUploadBufferTextureRowAlignment),
    GPU_CONFIG_PROPERTY_READ_ONLY("vendorid", mVendorId),
    GPU_CONFIG_PROPERTY("vram", mVRAM),
    GPU_CONFIG_PROPERTY("wavelanecount", mWaveLaneCount),
    GPU_CONFIG_PROPERTY("waveopssupportflags", mWaveOpsSupportFlags, StrToWaveOpsFlags, waveOpsSupportFlagsToString),
    GPU_CONFIG_PROPERTY("waveopssupportstage", mWaveOpsSupportedStageFlags, StrToShaderStage, shaderStageToString),
    GPU_CONFIG_PROPERTY("64bitatomicssupported", m64BitAtomicsSupported),
    GPU_CONFIG_PROPERTY_GLOBAL("sceneres", gSceneResolutionIndex),
    GPU_CONFIG_PROPERTY("target", mGPUTarget, StrToGPUTarget),
#if defined(SUPPORT_LOW_ENERGY_MODE)
    GPU_CONFIG_PROPERTY_READ_ONLY_GLOBAL("lowenergymode", gLowEnergyMode),
#endif
    GPU_CONFIG_PROPERTY("targetfps", mTargetFPS),
};

void setDefaultGPUProperties(TFGpuDesc* pGpuDesc)
{
    pGpuDesc->mVRAM = 0;
    pGpuDesc->mUniformBufferAlignment = 0;
    pGpuDesc->mUploadBufferAlignment = 0;
    pGpuDesc->mUploadBufferTextureAlignment = 0;
    pGpuDesc->mUploadBufferTextureRowAlignment = 0;
    pGpuDesc->mMaxVertexInputBindings = 0;
#if defined(DIRECT3D12)
    pGpuDesc->mMaxRootSignatureDWORDS = 0;
#endif
    pGpuDesc->mWaveLaneCount = 0;
    pGpuDesc->mWaveOpsSupportFlags = TF_WAVE_OPS_SUPPORT_FLAG_NONE;
    memset(&pGpuDesc->mGpuVendorPreset, 0, sizeof(TFGPUVendorPreset));
    pGpuDesc->mWaveOpsSupportedStageFlags = TF_SHADER_STAGE_NONE;

    pGpuDesc->mMaxTotalComputeThreads = 0;
    memset(pGpuDesc->mMaxComputeThreads, 0, sizeof(pGpuDesc->mMaxComputeThreads));
    pGpuDesc->mMultiDrawIndirect = 0;
    pGpuDesc->mMultiDrawIndirectCount = 0;
    pGpuDesc->mRootConstant = 0;
    pGpuDesc->mIndirectRootConstant = 0;
    pGpuDesc->mBuiltinDrawID = 0;
    pGpuDesc->mIndirectCommandBuffer = 0;
    pGpuDesc->mROVsSupported = 0;
    pGpuDesc->mTessellationSupported = 0;
    pGpuDesc->mGeometryShaderSupported = 0;
    pGpuDesc->mGpuMarkers = 0;
    pGpuDesc->mHDRSupported = 0;
    pGpuDesc->mTimestampQueries = 0;
    pGpuDesc->mOcclusionQueries = 0;
    pGpuDesc->mPipelineStatsQueries = 0;
    pGpuDesc->mAllowBufferTextureInSameHeap = 0;
    pGpuDesc->mRaytracingSupported = 0;
    pGpuDesc->mUnifiedMemorySupport = TF_UMA_SUPPORT_NONE;
    pGpuDesc->mRayPipelineSupported = 0;
    pGpuDesc->mRayQuerySupported = 0;
    pGpuDesc->mSoftwareVRSSupported = 0;
    pGpuDesc->mPrimitiveIdSupported = 1;
    pGpuDesc->mPrimitiveIdPsSupported = 0;
    pGpuDesc->m64BitAtomicsSupported = 0;
    pGpuDesc->mCoopVectorsSupported = 0;
#if defined(DIRECT3D12)
#if defined(XBOX)
    pGpuDesc->mFeatureLevel = D3D_FEATURE_LEVEL_9_1; // minimum possible
#else
    pGpuDesc->mFeatureLevel = D3D_FEATURE_LEVEL_1_0_GENERIC; // minimum possible
#endif
    pGpuDesc->mSuppressInvalidSubresourceStateAfterExit = 0;
    pGpuDesc->mWorkgraphSupported = 0;
#endif
#if defined(VULKAN)
    pGpuDesc->mDynamicRenderingSupported = 0;
    pGpuDesc->mXclipseTransferQueueWorkaround = 0;
    pGpuDesc->mDeviceMemoryReportCrashWorkaround = 0;
    pGpuDesc->mYCbCrExtension = 0;
    pGpuDesc->mFillModeNonSolid = 0;
    pGpuDesc->mKHRRayQueryExtension = 0;
    pGpuDesc->mAMDGCNShaderExtension = 0;
    pGpuDesc->mAMDDrawIndirectCountExtension = 0;
    pGpuDesc->mAMDShaderInfoExtension = 0;
    pGpuDesc->mDescriptorIndexingExtension = 0;
    pGpuDesc->mDynamicRenderingExtension = 0;
    pGpuDesc->mNonUniformResourceIndexSupported = 0;
    pGpuDesc->mBufferDeviceAddressSupported = 0;
    pGpuDesc->mDrawIndirectCountExtension = 0;
    pGpuDesc->mDedicatedAllocationExtension = 0;
    pGpuDesc->mDebugMarkerExtension = 0;
    pGpuDesc->mMemoryReq2Extension = 0;
    pGpuDesc->mFragmentShaderInterlockExtension = 0;
    pGpuDesc->mBufferDeviceAddressExtension = 0;
    pGpuDesc->mAccelerationStructureExtension = 0;
    pGpuDesc->mRayTracingPipelineExtension = 0;
    pGpuDesc->mRayQueryExtension = 0;
    pGpuDesc->mShaderAtomicInt64Extension = 0;
    pGpuDesc->mBufferDeviceAddressFeature = 0;
    pGpuDesc->mShaderFloatControlsExtension = 0;
    pGpuDesc->mSpirv14Extension = 0;
    pGpuDesc->mDeferredHostOperationsExtension = 0;
    pGpuDesc->mDeviceFaultExtension = 0;
    pGpuDesc->mDeviceFaultSupported = 0;
    pGpuDesc->mASTCDecodeModeExtension = 0;
    pGpuDesc->mDeviceMemoryReportExtension = 0;
    pGpuDesc->mAMDBufferMarkerExtension = 0;
    pGpuDesc->mAMDDeviceCoherentMemoryExtension = 0;
    pGpuDesc->mAMDDeviceCoherentMemorySupported = 0;
#if defined(VK_USE_PLATFORM_WIN32_KHR)
    pGpuDesc->mExternalMemoryExtension = 0;
    pGpuDesc->mExternalMemoryWin32Extension = 0;
#endif
#if defined(QUEST_VR)
    pGpuDesc->mMultiviewExtension = 0;
#endif

#endif
    pGpuDesc->mMaxBoundTextures = 0;
    pGpuDesc->mMaxStorageBufferSize = UINT32_MAX;
    pGpuDesc->mSamplerAnisotropySupported = 1;
    pGpuDesc->mGraphicsQueueSupported = 1;
#if defined(METAL)
    pGpuDesc->mHeaps = 0;
    pGpuDesc->mPlacementHeaps = 0;
    pGpuDesc->mResidencySetsSupported = 0;
    pGpuDesc->mTessellationIndirectDrawSupported = 0;
    pGpuDesc->mDrawIndexVertexOffsetSupported = 0;
    pGpuDesc->mCubeMapTextureArraySupported = 0;
#if !defined(TARGET_IOS)
    pGpuDesc->mIsHeadLess = 0; // indicates whether a GPU device does not have a connection to a display.
#endif
#endif
    pGpuDesc->mAmdAsicFamily = 0;
    pGpuDesc->mFrameBufferSamplesCount = TF_SAMPLE_COUNT_ALL_BITS;
    pGpuDesc->mTargetFPS = 60;
}

/* ------------------------ gpu.cfg ------------------------ */
typedef struct ConfigurationRule
{
    const GPUProperty* pGpuProperty;
    char               comparator[3]; // optional
    uint64_t           comparatorValue;
} ConfigurationRule;

typedef struct GPUSelectionRule
{
    ConfigurationRule* pGpuComparisonRules;
    uint32_t           comparisonRulesCount;
} GPUSelectionRule;

typedef struct GPUConfigurationRule
{
    const GPUProperty* pUpdateProperty;
    ConfigurationRule* pConfigurationRules;
    uint32_t           comparisonRulesCount;
    uint64_t           assignmentValue;
} GPUConfigurationRule;

typedef struct TextureSupportRule
{
    ConfigurationRule* pConfigurationRules;
    TinyImageFormat    imageFormat;
    TFFormatCapability formatCapability;
    uint32_t           comparisonRulesCount;
    bool               enableCapibility;
} TextureSupportRule;

// Used to configure the application's TFExtendedSettings via the .cfg file
typedef struct ExtendedConfigurationRule
{
    const char*        name;
    uint32_t*          pSettingValue;
    ConfigurationRule* pConfigurationRules;
    uint32_t           comparisonRulesCount;
    uint32_t           assignmentValue;
} ExtendedConfigurationRule;

typedef struct DriverVersion
{
    uint32_t versionNumbers[4];
    uint32_t versionNumbersCount;
} DriverVersion;

typedef struct DriverRejectionRule
{
    uint32_t      vendorId;
    char          comparator[3];
    DriverVersion driverComparisonValue;
    char          reasonStr[TF_MAX_GPU_VENDOR_STRING_LENGTH];
} DriverRejectionRule;
// ------ Interpreting Helpers ------ //
bool               isValidGPUVendorId(uint32_t vendorId);
bool               isValidGPUVendorId(uint32_t vendorId);
const char*        getGPUVendorName(uint32_t modelId);
const GPUProperty* propertyNameToGpuProperty(const char* str);
TFFormatCapability stringToFormatCapability(const char* str);
const char*        formatCapabilityToString(TFFormatCapability caps);
uint32_t           getSettingIndex(const char* string, uint32_t numSettings, const char** gameSettingNames);
bool               parseDriverVersion(const char* driverStr, DriverVersion* pDriverVersionOut);

// -------- Parsing Helpers ------- //
void parseGPUDataFile();
void parseGPUConfigFile(TFExtendedSettings* pExtendedSettings, uint32_t preferedGpuId);
void parseDefaultDataConfigurationLine(char* currentLine);
void parseGPUVendorLine(char* currentLine);
void parseGPUModelLine(char* currentLine, const char* pLineEnd);
void parseGPUSelectionLine(char* currentLine, uint32_t preferedGpuId);
void parseDriverRejectionLine(char* currentLine);
void parseGPUConfigurationLine(char* currentLine, uint32_t preferedGpuId);
void parseTextureSupportLine(char* currentLine, uint32_t preferedGpuId);
void formatCapabilityToCapabilityFlags(TFFormatCapability caps, char* pStrOut);
void parseResolutionConfigurationLine(char* currentLine);
void parseExtendedConfigurationLine(char* currentLine, TFExtendedSettings* pExtendedSettings, uint32_t preferedGpuId);
void parseConfigurationRules(ConfigurationRule** ppConfigurationRules, uint32_t* pRulesCount, char* ruleStr, uint32_t preferedGpuId);
void printConfigureRules(ConfigurationRule* pRules, uint32_t rulesCount, char* result);

///////////////////////////////////////////////////////////
// CONFIG INTERFACE

typedef enum DataParsingStatus
{
    DATA_PARSE_NONE,
    DATA_PARSE_DEFAULT_CONFIGURATION,
    DATA_PARSE_GPU_VENDOR,
    DATA_PARSE_GPU_MODEL,
} DataParsingStatus;

typedef enum ConfigParsingStatus
{
    CONFIG_PARSE_NONE,
    CONFIG_PARSE_SELECTION_RULE,
    CONFIG_PARSE_DRIVER_REJECTION,
    CONFIG_PARSE_GPU_CONFIGURATION,
    CONFIG_PARSE_RESOLUTION_SETTINGS,
    CONFIG_PARSE_USER_EXTENDED_SETTINGS,
    CONFIG_PARSE_TEXTURE_FORMAT,
} ConfigParsingStatus;

/************************************************************************/
// Internal initialization settings
/************************************************************************/

// ------ gpu.data
static GPUVendorDefinition gGPUVendorDefinitions[MAX_GPU_VENDOR_COUNT];
static uint32_t            gGPUVendorCount = 0;

typedef struct GraphicsConfigRules
{
    GPUModelDefinition*       mGPUModels;
    // ------ gpu.cfg
    GPUSelectionRule          mGPUSelectionRules[MAXIMUM_GPU_COMPARISON_CHOICES];
    GPUConfigurationRule      mGPUConfigurationRules[MAXIMUM_GPU_CONFIGURATION_RULES];
    TextureSupportRule        mTextureSupportRules[MAXIMUM_TEXTURE_SUPPORT_RULES];
    ExtendedConfigurationRule mExtendedConfigurationRules[MAXIMUM_GPU_CONFIGURATION_RULES];
    // gDriverRejectionRules[MAXIMUM_GPU_COMPARISON_CHOICES]; 72776 bytes moves it on the heap instead
    DriverRejectionRule*      mDriverRejectionRules;
    uint32_t                  mGPUSelectionRulesCount;
    uint32_t                  mDriverRejectionRulesCount;
    uint32_t                  mGPUConfigurationRulesCount;
    uint32_t                  mTextureSupportRulesCount;
    uint32_t                  mExtendedConfigurationRulesCount;
} GraphicsConfigRules;

static GraphicsConfigRules gGraphicsConfigRules;
#define MAX_UNSUPPORTED_MSG_LEN 256
static char gUnsupportedReason[MAX_UNSUPPORTED_MSG_LEN] = "Failed to Init Renderer";

/************************************************************************/
// Convert functions
/************************************************************************/

const char* presetLevelToString(TFGPUPresetLevel preset)
{
    switch (preset)
    {
    case TF_GPU_PRESET_NONE:
        return "";
    case TF_GPU_PRESET_OFFICE:
        return "office";
    case TF_GPU_PRESET_VERYLOW:
        return "verylow";
    case TF_GPU_PRESET_LOW:
        return "low";
    case TF_GPU_PRESET_MEDIUM:
        return "medium";
    case TF_GPU_PRESET_HIGH:
        return "high";
    case TF_GPU_PRESET_ULTRA:
        return "ultra";
    default:
        return "null";
    }
}

TFGPUPresetLevel stringToPresetLevel(const char* presetLevel)
{
    if (!stricmp(presetLevel, "office"))
    {
        return TF_GPU_PRESET_OFFICE;
    }
    if (!stricmp(presetLevel, "verylow"))
    {
        return TF_GPU_PRESET_VERYLOW;
    }
    if (!stricmp(presetLevel, "low"))
    {
        return TF_GPU_PRESET_LOW;
    }
    if (!stricmp(presetLevel, "medium"))
    {
        return TF_GPU_PRESET_MEDIUM;
    }
    if (!stricmp(presetLevel, "high"))
    {
        return TF_GPU_PRESET_HIGH;
    }
    if (!stricmp(presetLevel, "ultra"))
    {
        return TF_GPU_PRESET_ULTRA;
    }

    return TF_GPU_PRESET_NONE;
}

static GPUTarget stringToTarget(const char* targetStr)
{
    if (!stricmp(targetStr, "console_begin"))
    {
        return GPU_TARGET_CONSOLE_BEGIN;
    }
    if (!stricmp(targetStr, "console_end"))
    {
        return GPU_TARGET_CONSOLE_END;
    }
    if (!stricmp(targetStr, "mobile_begin"))
    {
        return GPU_TARGET_MOBILE_BEGIN;
    }
    if (!stricmp(targetStr, "mobile_end"))
    {
        return GPU_TARGET_MOBILE_END;
    }
    if (!stricmp(targetStr, "vr_begin"))
    {
        return GPU_TARGET_VR_BEGIN;
    }
    if (!stricmp(targetStr, "vr_end"))
    {
        return GPU_TARGET_VR_END;
    }
    if (!stricmp(targetStr, "x1"))
    {
        return GPU_TARGET_X1;
    }
    if (!stricmp(targetStr, "x1x"))
    {
        return GPU_TARGET_X1X;
    }
    if (!stricmp(targetStr, "xss"))
    {
        return GPU_TARGET_XSS;
    }
    if (!stricmp(targetStr, "xsx"))
    {
        return GPU_TARGET_XSX;
    }
    if (!stricmp(targetStr, "orbis"))
    {
        return GPU_TARGET_ORBIS;
    }
    if (!stricmp(targetStr, "neo"))
    {
        return GPU_TARGET_NEO;
    }
    if (!stricmp(targetStr, "prospero"))
    {
        return GPU_TARGET_PROSPERO;
    }
    if (!stricmp(targetStr, "android"))
    {
        return GPU_TARGET_ANDROID;
    }
    if (!stricmp(targetStr, "ios"))
    {
        return GPU_TARGET_IOS;
    }
    if (!stricmp(targetStr, "switch"))
    {
        return GPU_TARGET_SWITCH;
    }
    if (!stricmp(targetStr, "quest"))
    {
        return GPU_TARGET_QUEST;
    }
    if (!stricmp(targetStr, "quest2"))
    {
        return GPU_TARGET_QUEST2;
    }
    if (!stricmp(targetStr, "quest3"))
    {
        return GPU_TARGET_QUEST3;
    }
    if (!stricmp(targetStr, "hololens2"))
    {
        return GPU_TARGET_HOLOLENS2;
    }
    if (!stricmp(targetStr, "steam_deck"))
    {
        return GPU_TARGET_STEAM_DECK;
    }

    return GPU_TARGET_UNDEFINED;
}

static TFResolution stringToResolution(const char* str)
{
    TFResolution ret = { 0 };
    if (!stricmp(str, "native"))
    {
        ret = (TFResolution){ 0, 0 };
    }
    // 720p, 1080p, ...
    else if (str[strlen(str) - 1] == 'p')
    {
        const uint32_t height = (uint32_t)atoi(str);
        const uint32_t width = (uint32_t)(height * (16.0f / 9.0f));
        ret = (TFResolution){ width, height };
    }
    // 1024x768, 800x600, ...
    else
    {
        char resolutionStr[TF_MAX_GPU_VENDOR_STRING_LENGTH] = { 0 };
        strncpy(resolutionStr, str, TF_MAX_GPU_VENDOR_STRING_LENGTH);
        const char* widthStr = strtok(resolutionStr, "x");
        const char* heightStr = strtok(NULL, "x");
        ASSERT(widthStr && widthStr[0]);
        ASSERT(heightStr && heightStr[0]);
        ret = (TFResolution){ (uint32_t)atoi(widthStr), (uint32_t)atoi(heightStr) };
    }

    return ret;
}

TFFormatCapability stringToFormatCapability(const char* str)
{
    if (!stricmp(str, "TF_FORMAT_CAP_LINEAR_FILTER"))
        return TF_FORMAT_CAP_LINEAR_FILTER;
    if (!stricmp(str, "TF_FORMAT_CAP_READ"))
        return TF_FORMAT_CAP_READ;
    if (!stricmp(str, "TF_FORMAT_CAP_WRITE"))
        return TF_FORMAT_CAP_WRITE;
    if (!stricmp(str, "TF_FORMAT_CAP_READ_WRITE"))
        return TF_FORMAT_CAP_READ_WRITE;
    if (!stricmp(str, "TF_FORMAT_CAP_RENDER_TARGET"))
        return TF_FORMAT_CAP_RENDER_TARGET;
    if (!stricmp(str, "TF_FORMAT_CAP_DEPTH_STENCIL"))
        return TF_FORMAT_CAP_DEPTH_STENCIL;

    return TF_FORMAT_CAP_NONE;
}

const char* formatCapabilityToString(TFFormatCapability cap)
{
    switch (cap)
    {
    case TF_FORMAT_CAP_NONE:
        return "TF_FORMAT_CAP_NONE";
    case TF_FORMAT_CAP_LINEAR_FILTER:
        return "TF_FORMAT_CAP_LINEAR_FILTER";
    case TF_FORMAT_CAP_READ:
        return "TF_FORMAT_CAP_READ";
    case TF_FORMAT_CAP_WRITE:
        return "TF_FORMAT_CAP_WRITE";
    case TF_FORMAT_CAP_READ_WRITE:
        return "TF_FORMAT_CAP_READ_WRITE";
    case TF_FORMAT_CAP_RENDER_TARGET:
        return "TF_FORMAT_CAP_RENDER_TARGET";
    case TF_FORMAT_CAP_DEPTH_STENCIL:
        return "TF_FORMAT_CAP_DEPTH_STENCIL";
    default:
        return "null";
    }
}

static const char* waveOpsSupportFlagsToString(uint32_t flags)
{
    enum
    {
        MAX_WAVE_OPS_STR = 512
    };
    static char waveOpsStr[MAX_WAVE_OPS_STR];
    memset(waveOpsStr, 0, MAX_WAVE_OPS_STR);

    if (flags == TF_WAVE_OPS_SUPPORT_FLAG_NONE)
    {
        snprintf(waveOpsStr, MAX_WAVE_OPS_STR, " TF_WAVE_OPS_SUPPORT_FLAG_NONE ");
        return waveOpsStr;
    }

    uint32_t waveOpsStrLen = 0;
    if (flags & TF_WAVE_OPS_SUPPORT_FLAG_BASIC_BIT)
        waveOpsStrLen += snprintf(waveOpsStr + waveOpsStrLen, MAX_WAVE_OPS_STR - waveOpsStrLen, " TF_WAVE_OPS_SUPPORT_FLAG_BASIC_BIT |");

    if (flags & TF_WAVE_OPS_SUPPORT_FLAG_VOTE_BIT)
        waveOpsStrLen += snprintf(waveOpsStr + waveOpsStrLen, MAX_WAVE_OPS_STR - waveOpsStrLen, " TF_WAVE_OPS_SUPPORT_FLAG_VOTE_BIT |");

    if (flags & TF_WAVE_OPS_SUPPORT_FLAG_ARITHMETIC_BIT)
        waveOpsStrLen +=
            snprintf(waveOpsStr + waveOpsStrLen, MAX_WAVE_OPS_STR - waveOpsStrLen, " TF_WAVE_OPS_SUPPORT_FLAG_ARITHMETIC_BIT |");

    if (flags & TF_WAVE_OPS_SUPPORT_FLAG_BALLOT_BIT)
        waveOpsStrLen += snprintf(waveOpsStr + waveOpsStrLen, MAX_WAVE_OPS_STR - waveOpsStrLen, " TF_WAVE_OPS_SUPPORT_FLAG_BALLOT_BIT |");

    if (flags & TF_WAVE_OPS_SUPPORT_FLAG_SHUFFLE_BIT)
        waveOpsStrLen += snprintf(waveOpsStr + waveOpsStrLen, MAX_WAVE_OPS_STR - waveOpsStrLen, " TF_WAVE_OPS_SUPPORT_FLAG_SHUFFLE_BIT |");

    if (flags & TF_WAVE_OPS_SUPPORT_FLAG_SHUFFLE_RELATIVE_BIT)
        waveOpsStrLen +=
            snprintf(waveOpsStr + waveOpsStrLen, MAX_WAVE_OPS_STR - waveOpsStrLen, " TF_WAVE_OPS_SUPPORT_FLAG_SHUFFLE_RELATIVE_BIT |");

    if (flags & TF_WAVE_OPS_SUPPORT_FLAG_CLUSTERED_BIT)
        waveOpsStrLen +=
            snprintf(waveOpsStr + waveOpsStrLen, MAX_WAVE_OPS_STR - waveOpsStrLen, " TF_WAVE_OPS_SUPPORT_FLAG_CLUSTERED_BIT |");

    if (flags & TF_WAVE_OPS_SUPPORT_FLAG_QUAD_BIT)
        waveOpsStrLen += snprintf(waveOpsStr + waveOpsStrLen, MAX_WAVE_OPS_STR - waveOpsStrLen, " TF_WAVE_OPS_SUPPORT_FLAG_QUAD_BIT |");

    if (flags & TF_WAVE_OPS_SUPPORT_FLAG_PARTITIONED_BIT_NV)
        waveOpsStrLen +=
            snprintf(waveOpsStr + waveOpsStrLen, MAX_WAVE_OPS_STR - waveOpsStrLen, " TF_WAVE_OPS_SUPPORT_FLAG_PARTITIONED_BIT_NV |");

    if (flags & TF_WAVE_OPS_SUPPORT_FLAG_ALL)
        waveOpsStrLen += snprintf(waveOpsStr + waveOpsStrLen, MAX_WAVE_OPS_STR - waveOpsStrLen, " TF_WAVE_OPS_SUPPORT_FLAG_ALL |");

    if (waveOpsStrLen > 0)
        waveOpsStr[waveOpsStrLen - 1] = '\0'; // Remove the last | in the string

    return waveOpsStr;
}

static TFWaveOpsSupportFlags stringToWaveOpsSupportFlags(const char* str)
{
    if (!stricmp(str, "TF_WAVE_OPS_SUPPORT_FLAG_NONE"))
        return TF_WAVE_OPS_SUPPORT_FLAG_NONE;
    if (!stricmp(str, "TF_WAVE_OPS_SUPPORT_FLAG_BASIC_BIT"))
        return TF_WAVE_OPS_SUPPORT_FLAG_BASIC_BIT;
    if (!stricmp(str, "TF_WAVE_OPS_SUPPORT_FLAG_VOTE_BIT"))
        return TF_WAVE_OPS_SUPPORT_FLAG_VOTE_BIT;
    if (!stricmp(str, "TF_WAVE_OPS_SUPPORT_FLAG_ARITHMETIC_BIT"))
        return TF_WAVE_OPS_SUPPORT_FLAG_ARITHMETIC_BIT;
    if (!stricmp(str, "TF_WAVE_OPS_SUPPORT_FLAG_BALLOT_BIT"))
        return TF_WAVE_OPS_SUPPORT_FLAG_BALLOT_BIT;
    if (!stricmp(str, "TF_WAVE_OPS_SUPPORT_FLAG_SHUFFLE_BIT"))
        return TF_WAVE_OPS_SUPPORT_FLAG_SHUFFLE_BIT;
    if (!stricmp(str, "TF_WAVE_OPS_SUPPORT_FLAG_SHUFFLE_RELATIVE_BIT"))
        return TF_WAVE_OPS_SUPPORT_FLAG_SHUFFLE_RELATIVE_BIT;
    if (!stricmp(str, "TF_WAVE_OPS_SUPPORT_FLAG_CLUSTERED_BIT"))
        return TF_WAVE_OPS_SUPPORT_FLAG_CLUSTERED_BIT;
    if (!stricmp(str, "TF_WAVE_OPS_SUPPORT_FLAG_QUAD_BIT"))
        return TF_WAVE_OPS_SUPPORT_FLAG_QUAD_BIT;
    if (!stricmp(str, "TF_WAVE_OPS_SUPPORT_FLAG_PARTITIONED_BIT_NV"))
        return TF_WAVE_OPS_SUPPORT_FLAG_PARTITIONED_BIT_NV;
    if (!stricmp(str, "TF_WAVE_OPS_SUPPORT_FLAG_ALL"))
        return TF_WAVE_OPS_SUPPORT_FLAG_ALL;

    return TF_WAVE_OPS_SUPPORT_FLAG_NONE;
}

static const char* shaderStageToString(uint32_t stage)
{
    enum
    {
        MAX_SHADER_STAGE_STR = 256
    };
    static char shaderStageStr[MAX_SHADER_STAGE_STR];
    memset(shaderStageStr, 0, MAX_SHADER_STAGE_STR);

    if (stage == TF_SHADER_STAGE_NONE)
    {
        snprintf(shaderStageStr, MAX_SHADER_STAGE_STR, " SHADER_STAGE_NONE ");
        return shaderStageStr;
    }

    uint32_t shaderStageStrLen = 0;
    if (stage & TF_SHADER_STAGE_VERT)
        shaderStageStrLen += snprintf(shaderStageStr + shaderStageStrLen, MAX_SHADER_STAGE_STR - shaderStageStrLen, " SHADER_STAGE_VERT |");

    if (stage & TF_SHADER_STAGE_FRAG)
        shaderStageStrLen += snprintf(shaderStageStr + shaderStageStrLen, MAX_SHADER_STAGE_STR - shaderStageStrLen, " SHADER_STAGE_FRAG |");

    if (stage & TF_SHADER_STAGE_COMP)
        shaderStageStrLen += snprintf(shaderStageStr + shaderStageStrLen, MAX_SHADER_STAGE_STR - shaderStageStrLen, " SHADER_STAGE_COMP |");

    if (stage & TF_SHADER_STAGE_GEOM)
        shaderStageStrLen += snprintf(shaderStageStr + shaderStageStrLen, MAX_SHADER_STAGE_STR - shaderStageStrLen, " SHADER_STAGE_GEOM |");

    if (stage & TF_SHADER_STAGE_TESC)
        shaderStageStrLen += snprintf(shaderStageStr + shaderStageStrLen, MAX_SHADER_STAGE_STR - shaderStageStrLen, " SHADER_STAGE_TESC |");

    if (stage & TF_SHADER_STAGE_TESE)
        shaderStageStrLen += snprintf(shaderStageStr + shaderStageStrLen, MAX_SHADER_STAGE_STR - shaderStageStrLen, " SHADER_STAGE_TESE |");

#if defined(TF_ENABLE_WORKGRAPH)
    if (stage & TF_SHADER_STAGE_WORKGRAPH)
        shaderStageStrLen +=
            snprintf(shaderStageStr + shaderStageStrLen, MAX_SHADER_STAGE_STR - shaderStageStrLen, " SHADER_STAGE_WORKGRAPH |");
#endif
    if (shaderStageStrLen > 0)
        shaderStageStr[shaderStageStrLen - 1] = '\0'; // Remove the last | in the string

    return shaderStageStr;
}

static TFShaderStage stringToShaderStage(const char* str)
{
    if (!stricmp(str, "SHADER_STAGE_NONE"))
        return TF_SHADER_STAGE_NONE;
    if (!stricmp(str, "SHADER_STAGE_VERT"))
        return TF_SHADER_STAGE_VERT;
    if (!stricmp(str, "SHADER_STAGE_FRAG"))
        return TF_SHADER_STAGE_FRAG;
    if (!stricmp(str, "SHADER_STAGE_COMP"))
        return TF_SHADER_STAGE_COMP;
    if (!stricmp(str, "SHADER_STAGE_GEOM"))
        return TF_SHADER_STAGE_GEOM;
    if (!stricmp(str, "SHADER_STAGE_TESC"))
        return TF_SHADER_STAGE_TESC;
    if (!stricmp(str, "SHADER_STAGE_TESE"))
        return TF_SHADER_STAGE_TESE;
    if (!stricmp(str, "SHADER_STAGE_ALL_GRAPHICS"))
        return TF_SHADER_STAGE_ALL_GRAPHICS;
#if defined(TF_ENABLE_WORKGRAPH)
    if (!stricmp(str, "SHADER_STAGE_WORKGRAPH"))
        return TF_SHADER_STAGE_WORKGRAPH;
#endif

    return TF_SHADER_STAGE_NONE;
}

// Function to generate the unsupported message based on the failed GPUConfig rule when the gpuVal is an int
static const char* GenerateUnsupportedMessage(const char* propertyName, uint64_t gpuVal, const char* compareToken, uint64_t requiredVal)
{
    if ((strcmp(propertyName, "vendorid") == 0) || (strcmp(propertyName, "deviceid") == 0))
    {
        snprintf(gUnsupportedReason, MAX_UNSUPPORTED_MSG_LEN, "GPU Unsupported. Application does not run on this device.");
    }
    else
    {
        snprintf(gUnsupportedReason, MAX_UNSUPPORTED_MSG_LEN, "GPU Unsupported. Gpu property (%s) - '%lu' is NOT %s required value '%lu'",
                 propertyName, (unsigned long)gpuVal, compareToken, (unsigned long)requiredVal);
    }
    return gUnsupportedReason;
}

// Function to generate the unsupported message based on the failed GPUConfig rule when the gpuVal is a string
static const char* GenerateUnsupportedMessageStr(const char* propertyName, const char* gpuVal, const char* requiredVal)
{
    if ((strcmp(propertyName, "vendorid") == 0) || (strcmp(propertyName, "deviceid") == 0))
    {
        snprintf(gUnsupportedReason, MAX_UNSUPPORTED_MSG_LEN, "GPU Unsupported. Application does not run on this device.");
    }
    else
    {
        snprintf(gUnsupportedReason, MAX_UNSUPPORTED_MSG_LEN, "GPU Unsupported. Gpu property (%s) - '%s' does not have required value '%s'",
                 propertyName, gpuVal, requiredVal);
    }
    return gUnsupportedReason;
}

const char* getUnsupportedGPUMsg() { return gUnsupportedReason; }

/************************************************************************/
/************************************************************************/
void addGPUConfigurationRules(TFExtendedSettings* pExtendedSettings)
{
    parseGPUDataFile();
    parseGPUConfigFile(pExtendedSettings, gGpuSelection.mPreferedGpuId);
}

void parseGPUDataFile()
{
    gDefaultPresetLevel = TF_GPU_PRESET_LOW;

    TFFileStream fh = { 0 };
    if (!fsOpenStreamFromPath(TF_RD_OTHER_FILES, "gpu.data", TF_FM_READ, &fh))
    {
        LOGF(eERROR, "Failed to open gpu.data file. Function %s failed with error: %s", FS_ERR_CTX.func,
             getFSErrCodeString(FS_ERR_CTX.code));
        LOGF(eWARNING, "Setting preset will be set to Low as a default.");
        return;
    }
    DataParsingStatus parsingStatus = DATA_PARSE_NONE;
    char              currentLineStr[1024] = { 0 };
    gGPUVendorCount = 0;
    char* gpuListBeginFileCursor = NULL;
    char* gpuListEndFileCursor = NULL;

    size_t fileSize = fsGetStreamFileSize(&fh);
    char*  gpuDataFileBuffer = (char*)tf_malloc(fileSize * sizeof(char));
    fsReadFromStream(&fh, (void*)gpuDataFileBuffer, fileSize);
    char* fileCursor = gpuDataFileBuffer;
    char* gGpuDataFileEnd = gpuDataFileBuffer + fileSize;
    char* previousLineCursor = fileCursor;

    if (bufferedGetLine(currentLineStr, &fileCursor, gGpuDataFileEnd))
    {
        uint32_t versionMajor = 0;
        uint32_t versionMinor = 0;
        int      read = sscanf(currentLineStr, "version:%u.%u", &versionMajor, &versionMinor);
        if (read != 2)
        {
            LOGF(eINFO, "Ill-formatted gpu.data file. Missing version at beginning of file");
            fsCloseStream(&fh);
            tf_free(gpuDataFileBuffer);
            return;
        }
        else if (versionMajor != GPUCFG_VERSION_MAJOR || versionMinor != GPUCFG_VERSION_MINOR)
        {
            LOGF(eINFO, "gpu.data version mismatch. Expected version %u.%u but got %u.%u", GPUCFG_VERSION_MAJOR, GPUCFG_VERSION_MINOR,
                 versionMajor, versionMinor);
            fsCloseStream(&fh);
            tf_free(gpuDataFileBuffer);
            return;
        }
    }

    while (bufferedGetLine(currentLineStr, &fileCursor, gGpuDataFileEnd))
    {
        char*       lineCursor = currentLineStr;
        // skip spaces, empty line and comments
        size_t      ruleLength = strcspn(lineCursor, "#");
        const char* pLineEnd = lineCursor + ruleLength;
        while (currentLineStr != pLineEnd && isspace(*lineCursor))
        {
            ++lineCursor;
        }
        if (lineCursor == pLineEnd)
        {
            continue;
        }

        switch (parsingStatus)
        {
        case DATA_PARSE_NONE:
            if (strcmp(currentLineStr, "BEGIN_DEFAULT_CONFIGURATION;") == 0)
            {
                parsingStatus = DATA_PARSE_DEFAULT_CONFIGURATION;
            }
            else if (strcmp(currentLineStr, "BEGIN_VENDOR_LIST;") == 0)
            {
                parsingStatus = DATA_PARSE_GPU_VENDOR;
            }
            else if (strcmp(currentLineStr, "BEGIN_GPU_LIST;") == 0)
            {
                // mark the first line after BEGIN_GPU_SELECTION as the beginingCursor
                parsingStatus = DATA_PARSE_GPU_MODEL;
                gpuListBeginFileCursor = fileCursor;
            }
            break;
        case DATA_PARSE_GPU_VENDOR:
            if (strcmp(currentLineStr, "END_VENDOR_LIST;") == 0)
            {
                parsingStatus = DATA_PARSE_NONE;
            }
            else
            {
                parseGPUVendorLine(lineCursor);
            }
            break;
        case DATA_PARSE_GPU_MODEL:
            if (strcmp(currentLineStr, "END_GPU_LIST;") == 0)
            {
                // in an ideal world it would need to be before END_VENDOR_LIST; but we can live with this
                gpuListEndFileCursor = previousLineCursor;
                parsingStatus = DATA_PARSE_NONE;
            }
            else
            {
                parseGPUModelLine(currentLineStr, pLineEnd);
            }
            break;
        case DATA_PARSE_DEFAULT_CONFIGURATION:
        {
            if (strcmp(currentLineStr, "END_DEFAULT_CONFIGURATION;") == 0)
            {
                parsingStatus = DATA_PARSE_NONE;
            }
            else
            {
                parseDefaultDataConfigurationLine(lineCursor);
            }
            break;
        }
        default:
            break;
        }
        previousLineCursor = fileCursor;
    }

    // log default configuration
    LOGF(eINFO, "Default GPU Data:");
    LOGF(eINFO, "    DefaultGPUPresetLevel set to %s", presetLevelToString(gDefaultPresetLevel));
    // log gpu vendors
    LOGF(eINFO, "GPU vendors:");
    for (uint32_t vendorIndex = 0; vendorIndex < gGPUVendorCount; vendorIndex++)
    {
        char                 vendorStr[1024] = { '\0' };
        GPUVendorDefinition* currentGPUVendor = &gGPUVendorDefinitions[vendorIndex];
        int                  success = snprintf(vendorStr, 1024, "%s: ", currentGPUVendor->vendorName);
        if (success > 0)
        {
            const char* separator = "";
            for (uint32_t identifierIndex = 0; identifierIndex < currentGPUVendor->identifierCount; identifierIndex++)
            {
                char versionNumber[MAX_GPU_VENDOR_IDENTIFIER_LENGTH] = { 0 };
                snprintf(versionNumber, MAX_GPU_VENDOR_IDENTIFIER_LENGTH, "%s%#x", separator,
                         currentGPUVendor->identifierArray[identifierIndex]);
                strcat(vendorStr, versionNumber);
                separator = ", ";
            }
        }
        LOGF(eINFO, "    %s", vendorStr);
    }

    if (!gpuListBeginFileCursor || !gpuListEndFileCursor)
    {
        LOGF(eINFO, "Could not find a valid list of gpu in gpu.data please check BEGIN_GPU_LIST; END_GPU_LIST; is properly defined");
    }

    tf_free(gpuDataFileBuffer);
    fsCloseStream(&fh);
}

void parseGPUConfigFile(TFExtendedSettings* pExtendedSettings, uint32_t preferedGpuId)
{
    TFFileStream fh = { 0 };
    if (!fsOpenStreamFromPath(TF_RD_GPU_CONFIG, "gpu.cfg", TF_FM_READ, &fh))
    {
        LOGF(eERROR, "Failed to open gpu.cfg file. Function %s failed with error: %s", FS_ERR_CTX.func,
             getFSErrCodeString(FS_ERR_CTX.code));
        LOGF(eWARNING, "Setting first gpu found as active gpu.");
        return;
    }
    ConfigParsingStatus parsingStatus = CONFIG_PARSE_NONE;
    char                currentLineStr[1024] = { 0 };
    gGraphicsConfigRules.mDriverRejectionRules =
        (DriverRejectionRule*)tf_malloc(MAXIMUM_GPU_CONFIGURATION_RULES * sizeof(DriverRejectionRule));
    gGraphicsConfigRules.mGPUConfigurationRulesCount = 0;
    gGraphicsConfigRules.mDriverRejectionRulesCount = 0;
    gGraphicsConfigRules.mGPUSelectionRulesCount = 0;
    gGraphicsConfigRules.mTextureSupportRulesCount = 0;
    gGraphicsConfigRules.mExtendedConfigurationRulesCount = 0;

    size_t fileSize = fsGetStreamFileSize(&fh);
    char*  fileBuffer = (char*)tf_malloc(fileSize * sizeof(char));
    fsReadFromStream(&fh, fileBuffer, fileSize);
    char* fileCursor = fileBuffer;
    char* fileEnd = fileBuffer + fileSize;
    while (bufferedGetLine(currentLineStr, &fileCursor, fileEnd))
    {
        char*       lineCursor = currentLineStr;
        // skip spaces, empty line and comments
        size_t      ruleLength = strcspn(lineCursor, "#");
        const char* pLineEnd = lineCursor + ruleLength;
        while (currentLineStr != pLineEnd && isspace(*lineCursor))
            ++lineCursor;
        if (lineCursor == pLineEnd)
            continue;

        // parse configuration rules and settings
        switch (parsingStatus)
        {
        case CONFIG_PARSE_NONE:
            if (strcmp(currentLineStr, "BEGIN_GPU_SELECTION;") == 0)
            {
                parsingStatus = CONFIG_PARSE_SELECTION_RULE;
            }
            else if (strcmp(currentLineStr, "BEGIN_DRIVER_REJECTION;") == 0)
            {
                parsingStatus = CONFIG_PARSE_DRIVER_REJECTION;
            }
            else if (strcmp(currentLineStr, "BEGIN_GPU_SETTINGS;") == 0)
            {
                parsingStatus = CONFIG_PARSE_GPU_CONFIGURATION;
            }
            else if (strcmp(currentLineStr, "BEGIN_TEXTURE_FORMAT;") == 0)
            {
                parsingStatus = CONFIG_PARSE_TEXTURE_FORMAT;
            }
            else if (strcmp(currentLineStr, "BEGIN_RESOLUTION_SETTINGS;") == 0)
            {
                parsingStatus = CONFIG_PARSE_RESOLUTION_SETTINGS;
            }
            else if (strcmp(currentLineStr, "BEGIN_USER_SETTINGS;") == 0)
            {
                if (pExtendedSettings != NULL)
                    parsingStatus = CONFIG_PARSE_USER_EXTENDED_SETTINGS;
            }
            break;
        case CONFIG_PARSE_SELECTION_RULE:
            if (strcmp(currentLineStr, "END_GPU_SELECTION;") == 0)
            {
                parsingStatus = CONFIG_PARSE_NONE;
            }
            else
            {
                parseGPUSelectionLine(lineCursor, preferedGpuId);
            }
            break;
        case CONFIG_PARSE_DRIVER_REJECTION:
            if (strcmp(currentLineStr, "END_DRIVER_REJECTION;") == 0)
            {
                parsingStatus = CONFIG_PARSE_NONE;
            }
            else
            {
                parseDriverRejectionLine(lineCursor);
            }
            break;
        case CONFIG_PARSE_GPU_CONFIGURATION:
            if (strcmp(currentLineStr, "END_GPU_SETTINGS;") == 0)
            {
                parsingStatus = CONFIG_PARSE_NONE;
            }
            else
            {
                parseGPUConfigurationLine(lineCursor, preferedGpuId);
            }
            break;
        case CONFIG_PARSE_TEXTURE_FORMAT:
            if (strcmp(currentLineStr, "END_TEXTURE_FORMAT;") == 0)
            {
                parsingStatus = CONFIG_PARSE_NONE;
            }
            else
            {
                parseTextureSupportLine(lineCursor, preferedGpuId);
            }
            break;
        case CONFIG_PARSE_RESOLUTION_SETTINGS:
            if (strcmp(currentLineStr, "END_RESOLUTION_SETTINGS;") == 0)
            {
                parsingStatus = CONFIG_PARSE_NONE;
            }
            else
            {
                parseResolutionConfigurationLine(lineCursor);
            }
            break;
        case CONFIG_PARSE_USER_EXTENDED_SETTINGS:
            if (strcmp(currentLineStr, "END_USER_SETTINGS;") == 0)
            {
                parsingStatus = CONFIG_PARSE_NONE;
            }
            else
            {
                ASSERT(pExtendedSettings);
                parseExtendedConfigurationLine(lineCursor, pExtendedSettings, preferedGpuId);
            }
            break;
        default:
            break;
        }
    }

    // log configuration rules and settings
    LOGF(eINFO, "GPU selection rules:");
    for (uint32_t choiceIndex = 0; choiceIndex < gGraphicsConfigRules.mGPUSelectionRulesCount; choiceIndex++)
    {
        char              choiceStr[1024] = { '\0' };
        GPUSelectionRule* currentSelectionRule = &gGraphicsConfigRules.mGPUSelectionRules[choiceIndex];
        printConfigureRules(currentSelectionRule->pGpuComparisonRules, currentSelectionRule->comparisonRulesCount, choiceStr);
        LOGF(eINFO, "    Rule: %s", choiceStr);
    }
    // log driver rejection rules
    LOGF(eINFO, "Driver rejections:");
    for (uint32_t driverIndex = 0; driverIndex < gGraphicsConfigRules.mDriverRejectionRulesCount; driverIndex++)
    {
        char                 ruleStr[1024] = { 0 };
        DriverRejectionRule* currentRule = &gGraphicsConfigRules.mDriverRejectionRules[driverIndex];
        snprintf(ruleStr, 1024, "%s: DriverVersion %s ", getGPUVendorName(currentRule->vendorId), currentRule->comparator);
        const char* separator = "";
        for (uint32_t numberIndex = 0; numberIndex < currentRule->driverComparisonValue.versionNumbersCount; numberIndex++)
        {
            char versionNumber[32] = { 0 };
            snprintf(versionNumber, 32, "%s%u", separator, currentRule->driverComparisonValue.versionNumbers[numberIndex]);
            strcat(ruleStr, versionNumber);
            separator = ".";
        }
        strcat(ruleStr, "; ");
        strcat(ruleStr, currentRule->reasonStr);
        LOGF(eINFO, "    %s", ruleStr);
    }
    LOGF(eINFO, "GPU configuration rules:");
    for (uint32_t settingIndex = 0; settingIndex < gGraphicsConfigRules.mGPUConfigurationRulesCount; settingIndex++)
    {
        char                  settingStr[1024] = { 0 };
        char                  assignmentValueStr[32] = { 0 };
        GPUConfigurationRule* currentConfigurationRule = &gGraphicsConfigRules.mGPUConfigurationRules[settingIndex];
        snprintf(settingStr, 1024, "%s: ", currentConfigurationRule->pUpdateProperty->name);
        printConfigureRules(currentConfigurationRule->pConfigurationRules, currentConfigurationRule->comparisonRulesCount, settingStr);
        snprintf(assignmentValueStr, 32, ": %lld", (long long)currentConfigurationRule->assignmentValue);
        strcat(settingStr, assignmentValueStr);
        LOGF(eINFO, "    %s", settingStr);
    }
    LOGF(eINFO, "Texture support rules:");
    for (uint32_t settingIndex = 0; settingIndex < gGraphicsConfigRules.mTextureSupportRulesCount; settingIndex++)
    {
        char                settingStr[1024] = { 0 };
        char                assignmentValueStr[64] = { 0 };
        TextureSupportRule* textureSupportRule = &gGraphicsConfigRules.mTextureSupportRules[settingIndex];
        char                insertModeStr = textureSupportRule->enableCapibility ? '+' : '-';
        const char*         capStr = formatCapabilityToString(textureSupportRule->formatCapability);
        snprintf(settingStr, 1024, "%s: ", TinyImageFormat_Name(textureSupportRule->imageFormat));
        printConfigureRules(textureSupportRule->pConfigurationRules, textureSupportRule->comparisonRulesCount, settingStr);
        snprintf(assignmentValueStr, 64, ": %c%s", insertModeStr, capStr);
        strcat(settingStr, assignmentValueStr);
        LOGF(eINFO, "    %s", settingStr);
    }
    LOGF(eINFO, "Extended configuration rules:");
    for (uint32_t settingIndex = 0; settingIndex < gGraphicsConfigRules.mExtendedConfigurationRulesCount; settingIndex++)
    {
        char                       settingStr[1024] = { 0 };
        char                       assignmentValueStr[32] = { 0 };
        ExtendedConfigurationRule* extendedRule = &gGraphicsConfigRules.mExtendedConfigurationRules[settingIndex];
        snprintf(settingStr, 1024, "%s: ", extendedRule->name);
        printConfigureRules(extendedRule->pConfigurationRules, extendedRule->comparisonRulesCount, settingStr);
        snprintf(assignmentValueStr, 32, ": %u", extendedRule->assignmentValue);
        strcat(settingStr, assignmentValueStr);
        LOGF(eINFO, "    %s", settingStr);
    }

    tf_free(fileBuffer);
    fsCloseStream(&fh);
}

void removeGPUConfigurationRules()
{
    tf_free(gGraphicsConfigRules.mDriverRejectionRules);
    arrfree(gGraphicsConfigRules.mGPUModels);
    gGraphicsConfigRules.mDriverRejectionRules = NULL;

    for (uint32_t i = 0; i < gGraphicsConfigRules.mGPUSelectionRulesCount; i++)
    {
        tf_free(gGraphicsConfigRules.mGPUSelectionRules[i].pGpuComparisonRules);
    }

    for (uint32_t i = 0; i < gGraphicsConfigRules.mGPUConfigurationRulesCount; i++)
    {
        tf_free(gGraphicsConfigRules.mGPUConfigurationRules[i].pConfigurationRules);
    }

    for (uint32_t i = 0; i < gGraphicsConfigRules.mTextureSupportRulesCount; i++)
    {
        tf_free(gGraphicsConfigRules.mTextureSupportRules[i].pConfigurationRules);
    }

    for (uint32_t i = 0; i < gGraphicsConfigRules.mExtendedConfigurationRulesCount; i++)
    {
        tf_free(gGraphicsConfigRules.mExtendedConfigurationRules[i].pConfigurationRules);
    }
    gGraphicsConfigRules.mGPUSelectionRulesCount = 0;
    gGraphicsConfigRules.mDriverRejectionRulesCount = 0;
    gGraphicsConfigRules.mGPUConfigurationRulesCount = 0;
    gGraphicsConfigRules.mTextureSupportRulesCount = 0;
    gGraphicsConfigRules.mExtendedConfigurationRulesCount = 0;
}

void parseDefaultDataConfigurationLine(char* currentLine)
{
    size_t      ruleLength = strcspn(currentLine, "#");
    const char* pLineEnd = currentLine + ruleLength;
    char        ruleName[TF_MAX_GPU_VENDOR_STRING_LENGTH] = { 0 };
    char        assignmentValue[TF_MAX_GPU_VENDOR_STRING_LENGTH] = { 0 };
    char*       tokens[] = { ruleName, assignmentValue };
    tokenizeLine(currentLine, pLineEnd, ";", TF_MAX_GPU_VENDOR_STRING_LENGTH, TF_ARRAY_COUNT(tokens), tokens);

    if (strcmp(ruleName, "DefaultPresetLevel") == 0)
    {
        stringToLower(assignmentValue);
        TFGPUPresetLevel defaultLevel = stringToPresetLevel(assignmentValue);
        if (defaultLevel != TF_GPU_PRESET_NONE)
        {
            gDefaultPresetLevel = defaultLevel;
        }
        else
        {
            LOGF(eDEBUG, "Error invalid preset level in GPU Default Data Configuration value '%s' in '%s'.", assignmentValue, currentLine);
        }
    }
    else if (strcmp(ruleName, "DefaultTarget") == 0)
    {
        stringToLower(assignmentValue);
        GPUTarget defaultTarget = stringToTarget(assignmentValue);
        if (defaultTarget != GPU_TARGET_UNDEFINED)
        {
            gDefaultTarget = defaultTarget;
        }
        else
        {
            LOGF(eDEBUG, "Error invalid target in GPU Default Data Configuration value '%s' in '%s'.", assignmentValue, currentLine);
        }
    }
    else
    {
        LOGF(eDEBUG, "Error could not parse GPU Default Data Configuration rule '%s' in '%s'.", ruleName, currentLine);
    }
}

void parseGPUVendorLine(char* currentLine)
{
    char                 gpuIdentifierList[TF_MAX_GPU_VENDOR_STRING_LENGTH] = { 0 };
    char                 gpuIdentifier[MAX_GPU_VENDOR_IDENTIFIER_LENGTH] = { 0 };
    size_t               ruleLength = strcspn(currentLine, "#");
    const char*          pLineEnd = currentLine + ruleLength;
    GPUVendorDefinition* currentVendor = &gGPUVendorDefinitions[gGPUVendorCount];
    memset(currentVendor, 0, sizeof(*currentVendor));
    char* tokens[] = { currentVendor->vendorName, gpuIdentifierList };
    tokenizeLine(currentLine, pLineEnd, ";", TF_MAX_GPU_VENDOR_STRING_LENGTH, TF_ARRAY_COUNT(tokens), tokens);

    char* currentIdentifier = gpuIdentifierList;
    char* identifierEnd = gpuIdentifierList + strlen(gpuIdentifierList);
    while (currentIdentifier < identifierEnd)
    {
        //  read in the field name
        size_t optionLength = strcspn(currentIdentifier, ",");
        optionLength = TF_MIN(optionLength, MAX_GPU_VENDOR_IDENTIFIER_LENGTH - 1);
        strncpy(gpuIdentifier, currentIdentifier, optionLength);
        gpuIdentifier[optionLength] = '\0';
        if (currentVendor->identifierCount >= MAX_IDENTIFIER_PER_GPU_VENDOR_COUNT)
        {
            LOGF(eDEBUG, "max number of GPU vendor identifiers exceeded %d in '%s'.", MAX_IDENTIFIER_PER_GPU_VENDOR_COUNT, currentLine);
            break;
        }

        bool validConversion = stringToInteger(gpuIdentifier, &currentVendor->identifierArray[currentVendor->identifierCount], 16);
        if (validConversion)
        {
            currentVendor->identifierCount++;
        }
        else
        {
            LOGF(eDEBUG, "Invalid GPU vendor identifier %s from line: '%s'.", gpuIdentifier, currentLine);
            break;
        }
        currentIdentifier += optionLength;
        currentIdentifier += strspn(currentIdentifier, " ,");
    }

    if (currentVendor->identifierCount > 0)
    {
        gGPUVendorCount++;
    }
    else
    {
        LOGF(eDEBUG, "Error could not parse GPU vendor in '%s'.", currentLine);
    }
}

void parseGPUModelLine(char* pCurrentLine, const char* pLineEnd)
{
    char  vendorIdStr[TF_MAX_GPU_VENDOR_STRING_LENGTH] = { 0 };
    char  modelIdStr[TF_MAX_GPU_VENDOR_STRING_LENGTH] = { 0 };
    char  presetStr[TF_MAX_GPU_VENDOR_STRING_LENGTH] = { 0 };
    char  vendorNameStr[TF_MAX_GPU_VENDOR_STRING_LENGTH] = { 0 };
    char  modelNameStr[TF_MAX_GPU_VENDOR_STRING_LENGTH] = { 0 };
    char  targetNameStr[TF_MAX_GPU_VENDOR_STRING_LENGTH] = { 0 };
    char* tokens[] = { vendorIdStr, modelIdStr, presetStr, vendorNameStr, modelNameStr, targetNameStr };
    tokenizeLine(pCurrentLine, pLineEnd, ";", TF_MAX_GPU_VENDOR_STRING_LENGTH, TF_ARRAY_COUNT(tokens), tokens);
    GPUModelDefinition model = { 0 };
    model.mVendorId = (uint32_t)strtoul(vendorIdStr + 2, NULL, 16);
    model.mDeviceId = (uint32_t)strtoul(modelIdStr + 2, NULL, 16);
    model.mPreset = stringToPresetLevel(presetStr);
    model.mTarget = gDefaultTarget;
    if (modelNameStr[0] != '\0')
    {
        strncpy(model.mModelName, modelNameStr, TF_ARRAY_COUNT(modelNameStr));
    }
    if (targetNameStr[0] != '\0')
    {
        model.mTarget = stringToTarget(targetNameStr);
    }
    if (model.mVendorId && model.mDeviceId)
    {
        arrpush(gGraphicsConfigRules.mGPUModels, model);
    }
}

void parseGPUSelectionLine(char* currentLine, uint32_t preferedGpuId)
{
    // parse selection rule
    char              ruleParameters[TF_MAX_GPU_VENDOR_STRING_LENGTH] = { 0 };
    size_t            ruleLength = strcspn(currentLine, "#");
    const char*       pLineEnd = currentLine + ruleLength;
    GPUSelectionRule* currentChoice = &gGraphicsConfigRules.mGPUSelectionRules[gGraphicsConfigRules.mGPUSelectionRulesCount];
    memset(currentChoice, 0, sizeof(*currentChoice));
    char* tokens[] = { ruleParameters };
    tokenizeLine(currentLine, pLineEnd, ";", TF_MAX_GPU_VENDOR_STRING_LENGTH, TF_ARRAY_COUNT(tokens), tokens);

    // parse comparison rules separated by ","
    parseConfigurationRules(&currentChoice->pGpuComparisonRules, &currentChoice->comparisonRulesCount, ruleParameters, preferedGpuId);
    if (currentChoice->pGpuComparisonRules == NULL)
    {
        LOGF(eDEBUG, "Invalid GPU Selection rule: '%s'.", currentLine);
    }
    else
    {
        gGraphicsConfigRules.mGPUSelectionRulesCount++;
    }
}

void parseDriverRejectionLine(char* currentLine)
{
    char        vendorStr[TF_MAX_GPU_VENDOR_STRING_LENGTH] = { 0 };
    char        ruleParameters[TF_MAX_GPU_VENDOR_STRING_LENGTH] = { 0 };
    char        reasonStr[TF_MAX_GPU_VENDOR_STRING_LENGTH] = { 0 };
    // parse driver rule
    size_t      ruleLength = strcspn(currentLine, "#");
    const char* pLineEnd = currentLine + ruleLength;
    char*       tokens[] = { vendorStr, ruleParameters, reasonStr };
    tokenizeLine(currentLine, pLineEnd, ";", TF_MAX_GPU_VENDOR_STRING_LENGTH, TF_ARRAY_COUNT(tokens), tokens);
    uint32_t vendorId;
    stringToInteger(vendorStr, &vendorId, 16);
    if (isValidGPUVendorId(vendorId))
    {
        DriverRejectionRule* currentRule = &gGraphicsConfigRules.mDriverRejectionRules[gGraphicsConfigRules.mDriverRejectionRulesCount];
        memset(currentRule, 0, sizeof(*currentRule));
        DriverVersion* currentVersion = &currentRule->driverComparisonValue;
        currentRule->vendorId = vendorId;
        char* comparisonRule = ruleParameters;
        comparisonRule += strspn(ruleParameters, " ");
        size_t optionLength = strcspn(comparisonRule, " <=>!,");
        char   fieldName[TF_MAX_GPU_VENDOR_STRING_LENGTH] = { 0 };
        strncpy(fieldName, comparisonRule, optionLength);
        if (strcmp(stringToLower(fieldName), "driverversion") == 0)
        {
            comparisonRule += optionLength;
            comparisonRule += strspn(comparisonRule, " ");
            optionLength = strspn(comparisonRule, "<=>!");
            strncpy(currentRule->comparator, comparisonRule, TF_MIN(optionLength, 3));
            comparisonRule += optionLength;
            comparisonRule += strspn(comparisonRule, " ");
            bool validConversion = parseDriverVersion(comparisonRule, currentVersion);
            // optional, could add check on comparator validity
            if (validConversion)
            {
                strncpy(currentRule->reasonStr, reasonStr, TF_MAX_GPU_VENDOR_STRING_LENGTH);
                gGraphicsConfigRules.mDriverRejectionRulesCount++;
            }
            else
            {
                LOGF(eDEBUG, "Driver Rejection| error parsing this line: '%s', invalid version number", currentLine);
            }
        }
        else
        {
            LOGF(eDEBUG, "Driver Rejection| missing DriverVersion literal in '%s'", currentLine);
        }
    }
    else
    {
        LOGF(eDEBUG, "Driver Rejection| unknown gpu vendor %s in '%s'", vendorStr, currentLine);
    }
}

void parseGPUConfigurationLine(char* currentLine, uint32_t preferedGpuId)
{
    char propertyName[TF_MAX_GPU_VENDOR_STRING_LENGTH] = { 0 };
    char ruleParameters[TF_MAX_GPU_VENDOR_STRING_LENGTH] = { 0 };
    char assignmentValue[TF_MAX_GPU_VENDOR_STRING_LENGTH] = { 0 };

    // parse selection rule
    size_t                ruleLength = strcspn(currentLine, "#");
    const char*           pLineEnd = currentLine + ruleLength;
    GPUConfigurationRule* currentConfigurationRule =
        &gGraphicsConfigRules.mGPUConfigurationRules[gGraphicsConfigRules.mGPUConfigurationRulesCount];
    memset(currentConfigurationRule, 0, sizeof(*currentConfigurationRule));
    char* tokens[] = { propertyName, ruleParameters, assignmentValue };
    tokenizeLine(currentLine, pLineEnd, ";", TF_MAX_GPU_VENDOR_STRING_LENGTH, TF_ARRAY_COUNT(tokens), tokens);
    char* rulesBegin = ruleParameters;
    currentConfigurationRule->pUpdateProperty = propertyNameToGpuProperty(stringToLower(propertyName));
    if (currentConfigurationRule->pUpdateProperty)
    {
        // check assignment value
        int  base = ((strcmp(currentConfigurationRule->pUpdateProperty->name, "vendorid") == 0) ||
                    (strcmp(currentConfigurationRule->pUpdateProperty->name, "deviceid") == 0))
                        ? 16
                        : 10;
        bool validConversion = false;
        if (strcmp(currentConfigurationRule->pUpdateProperty->name, "gpupresetlevel") == 0)
        {
            currentConfigurationRule->assignmentValue = stringToPresetLevel(assignmentValue);
            validConversion = currentConfigurationRule->assignmentValue != TF_GPU_PRESET_NONE;
            ASSERT(validConversion);
        }
        else
        {
            validConversion = stringToLargeInteger(assignmentValue, &currentConfigurationRule->assignmentValue, base);
        }
        // parse comparison rules separated by ","
        parseConfigurationRules(&currentConfigurationRule->pConfigurationRules, &currentConfigurationRule->comparisonRulesCount, rulesBegin,
                                preferedGpuId);
        if (currentConfigurationRule->pConfigurationRules == NULL)
        {
            LOGF(eDEBUG, "parseGPUConfigurationSetting: invalid rules for Field name: '%s'.",
                 currentConfigurationRule->pUpdateProperty->name);
            tf_free(currentConfigurationRule->pConfigurationRules);
        }
        else if (!validConversion)
        {
            LOGF(eDEBUG, "parseGPUConfigurationSetting: cannot convert %s for Field name: '%s'.", assignmentValue,
                 currentConfigurationRule->pUpdateProperty->name);
            tf_free(currentConfigurationRule->pConfigurationRules);
        }
        else
        {
            gGraphicsConfigRules.mGPUConfigurationRulesCount++;
        }
    }
    else
    {
        LOGF(eDEBUG, "parseGPUConfigurationSetting: invalid property: '%s'.", propertyName);
        tf_free(currentConfigurationRule->pConfigurationRules);
    }
}

void parseTextureSupportLine(char* currentLine, uint32_t preferedGpuId)
{
    char                formatName[TF_MAX_GPU_VENDOR_STRING_LENGTH] = { 0 };
    char                ruleParameters[TF_MAX_GPU_VENDOR_STRING_LENGTH] = { 0 };
    char                formatCaps[TF_MAX_GPU_VENDOR_STRING_LENGTH] = { 0 };
    size_t              ruleLength = strcspn(currentLine, "#");
    const char*         pLineEnd = currentLine + ruleLength;
    TextureSupportRule* textureSupportRule = &gGraphicsConfigRules.mTextureSupportRules[gGraphicsConfigRules.mTextureSupportRulesCount];
    memset(textureSupportRule, 0, sizeof(*textureSupportRule));

    char* tokens[] = { formatName, ruleParameters, formatCaps };
    tokenizeLine(currentLine, pLineEnd, ";", TF_MAX_GPU_VENDOR_STRING_LENGTH, TF_ARRAY_COUNT(tokens), tokens);
    char* rulesBegin = ruleParameters;
    textureSupportRule->imageFormat = TinyImageFormat_FromName(formatName);
    if (textureSupportRule->imageFormat == TinyImageFormat_UNDEFINED)
    {
        LOGF(eDEBUG, "parseTextureFormatSetting: %s is not a valid texture format'.", formatName);
        return;
    }

    char* textureCapCursor = formatCaps;
    textureCapCursor += strspn(textureCapCursor, " ");
    if (textureCapCursor[0] == '+')
    {
        textureSupportRule->enableCapibility = true;
        textureCapCursor++;
    }
    else if (textureCapCursor[0] == '-')
    {
        textureSupportRule->enableCapibility = false;
        textureCapCursor++;
    }
    else
    {
        LOGF(eDEBUG, "parseTextureFormatSetting: texture capability must begin with '+' or '-' in '%s'.", formatName);
        return;
    }

    size_t capLength = strcspn(textureCapCursor, " ");
    char   capName[TF_MAX_GPU_VENDOR_STRING_LENGTH] = { 0 };
    strncpy(capName, textureCapCursor, capLength);
    TFFormatCapability formatCap = stringToFormatCapability(capName);
    if (formatCap == TF_FORMAT_CAP_NONE)
    {
        LOGF(eDEBUG, "parseTextureFormatSetting: texture capability invalid '%s' in %s.", capName, formatCaps);
        return;
    }
    textureSupportRule->formatCapability = formatCap;

    parseConfigurationRules(&textureSupportRule->pConfigurationRules, &textureSupportRule->comparisonRulesCount, rulesBegin, preferedGpuId);
    if (textureSupportRule->pConfigurationRules == NULL)
    {
        LOGF(eDEBUG, "parseTextureFormatSetting: invalid rules for Field name: '%s'.", formatName);
        tf_free(textureSupportRule->pConfigurationRules);
    }
    else
    {
        gGraphicsConfigRules.mTextureSupportRulesCount++;
    }
}

void parseResolutionConfigurationLine(char* currentLine)
{
    size_t      ruleLength = strcspn(currentLine, "#");
    const char* pLineEnd = currentLine + ruleLength;
    char        ruleName[MAX_GPU_VENDOR_IDENTIFIER_LENGTH] = { 0 };
    char        resTokens[SCENE_RESOLUTION_COUNT][MAX_GPU_VENDOR_IDENTIFIER_LENGTH] = { { 0 } };
    char*       tokens[] = {
        ruleName,     resTokens[0], resTokens[1],  resTokens[2],  resTokens[3],  resTokens[4],  resTokens[5],  resTokens[6],  resTokens[7],
        resTokens[8], resTokens[9], resTokens[10], resTokens[11], resTokens[12], resTokens[13], resTokens[14], resTokens[15],
    };
    tokenizeLine(currentLine, pLineEnd, ";", MAX_GPU_VENDOR_IDENTIFIER_LENGTH, TF_ARRAY_COUNT(tokens), tokens);

    if (strcmp(ruleName, "SceneResolutions") == 0)
    {
        uint32_t resCount = 0;
        for (uint32_t t = 1; t < TF_ARRAY_COUNT(tokens); ++t)
        {
            char* resolution = tokens[t];
            if (resolution[0] == '\0')
            {
                break;
            }
            if (resCount >= SCENE_RESOLUTION_COUNT)
            {
                LOGF(eDEBUG, "GraphicsConfig - Exceeded max SceneResolutions (Current %u, Max %u). Skipping the rest",
                     TF_ARRAY_COUNT(tokens) - 1, SCENE_RESOLUTION_COUNT);
                break;
            }

            stringToLower(resolution);
            gAvailableSceneResolutions[resCount] = stringToResolution(resolution);
            ++resCount;
        }

        gAvailableSceneResolutionCount = resCount;
    }
    else
    {
        parseGPUConfigurationLine(currentLine, 0);
    }
}

void parseExtendedConfigurationLine(char* currentLine, TFExtendedSettings* pExtendedSettings, uint32_t preferedGpuId)
{
    char settingName[TF_MAX_GPU_VENDOR_STRING_LENGTH] = { 0 };
    char ruleParameters[TF_MAX_GPU_VENDOR_STRING_LENGTH] = { 0 };
    char assignmentValue[TF_MAX_GPU_VENDOR_STRING_LENGTH] = { 0 };

    // parse selection rule
    size_t      ruleLength = strcspn(currentLine, "#");
    const char* pLineEnd = currentLine + ruleLength;
    char*       tokens[] = { settingName, ruleParameters, assignmentValue };
    tokenizeLine(currentLine, pLineEnd, ";", TF_MAX_GPU_VENDOR_STRING_LENGTH, TF_ARRAY_COUNT(tokens), tokens);

    uint32_t settingIndex = getSettingIndex(settingName, pExtendedSettings->mNumSettings, pExtendedSettings->ppSettingNames);
    if (settingIndex != INVALID_OPTION)
    {
        char*                      rulesBegin = ruleParameters;
        ExtendedConfigurationRule* currentExtendedRule =
            &gGraphicsConfigRules.mExtendedConfigurationRules[gGraphicsConfigRules.mExtendedConfigurationRulesCount];
        memset(currentExtendedRule, 0, sizeof(*currentExtendedRule));
        currentExtendedRule->name = pExtendedSettings->ppSettingNames[settingIndex];
        currentExtendedRule->pSettingValue = &pExtendedSettings->pSettings[settingIndex];
        parseConfigurationRules(&currentExtendedRule->pConfigurationRules, &currentExtendedRule->comparisonRulesCount, rulesBegin,
                                preferedGpuId);
        bool validConversion = stringToInteger(assignmentValue, &currentExtendedRule->assignmentValue, 10);
        if (!validConversion)
        {
            LOGF(eDEBUG, "parseUserSettings: cannot convert %s for setting name: '%s'.", assignmentValue, currentExtendedRule->name);
            tf_free(currentExtendedRule->pConfigurationRules);
        }
        else
        {
            gGraphicsConfigRules.mExtendedConfigurationRulesCount++;
        }
    }
    else
    {
        LOGF(eDEBUG, "User Settings| Unknown ExtendedSetting: '%s' from line: '%s'", settingName, currentLine);
    }
}

void parseConfigurationRules(ConfigurationRule** ppConfigurationRules, uint32_t* pRulesCount, char* ruleStr, uint32_t preferedGpuId)
{
    char* rulesEnd = ruleStr + strlen(ruleStr);
    while (ruleStr < rulesEnd)
    {
        (*pRulesCount)++;
        char* currentRule = ruleStr;
        *ppConfigurationRules = (ConfigurationRule*)tf_realloc(*ppConfigurationRules, *pRulesCount * sizeof(ConfigurationRule));
        ConfigurationRule* currentComparisonRule = &((*ppConfigurationRules)[(*pRulesCount) - 1]);
        memset(currentComparisonRule, 0, sizeof(*currentComparisonRule));
        currentComparisonRule->comparatorValue = INVALID_OPTION;

        //  read in the field name
        size_t optionLength = strcspn(currentRule, " <=>!&,");
        ASSERT(optionLength < TF_MAX_GPU_VENDOR_STRING_LENGTH - 1);
        char fieldName[TF_MAX_GPU_VENDOR_STRING_LENGTH] = { 0 };
        strncpy(fieldName, currentRule, optionLength);
        currentComparisonRule->pGpuProperty = propertyNameToGpuProperty(stringToLower(fieldName));

        currentRule += optionLength;
        currentRule += strspn(currentRule, " ");
        optionLength = strspn(currentRule, "<=>!&");

        // this will be triggered for any platform specific rule, isheadless, graphicqueuesupported, ...
        if (currentComparisonRule->pGpuProperty == NULL)
        {
            // discard current set of rules
            LOGF(eDEBUG, "Extended Config| GPU Config invalid Field name: '%s'.", fieldName);
            tf_free(*ppConfigurationRules);
            *ppConfigurationRules = NULL;
            *pRulesCount = 0;
            break;
        }
        // store the comparator and comparatorValue if any
        else if (optionLength > 0)
        {
            ASSERT(optionLength <= 3 && optionLength > 0);
            // optional, could add check on comparator validity
            strncpy(currentComparisonRule->comparator, currentRule, TF_MIN(optionLength, 3));
            //  read in the value relative to the field
            currentRule += optionLength;
            currentRule += strspn(currentRule, " ");
            char parsedValue[TF_MAX_GPU_VENDOR_STRING_LENGTH];
            optionLength = strcspn(currentRule, ",");
            strncpy(parsedValue, currentRule, optionLength);
            parsedValue[optionLength] = '\0';
            // hack for preferred gpu
            if (strstr(stringToLower(parsedValue), "preferredgpu") != 0)
            {
                currentComparisonRule->comparatorValue = preferedGpuId != 0 ? preferedGpuId : INVALID_OPTION;
            }
            else
            {
                int  base = ((strcmp(currentComparisonRule->pGpuProperty->name, "vendorid") == 0) ||
                            (strcmp(currentComparisonRule->pGpuProperty->name, "deviceid") == 0))
                                ? 16
                                : 10;
                bool validConversion = stringToLargeInteger(parsedValue, &currentComparisonRule->comparatorValue, base);
                if (!validConversion)
                {
                    // If parsedValue is enum string, check if there is a str to value function for this property
                    if (currentComparisonRule->pGpuProperty->strToEnum)
                    {
                        currentComparisonRule->comparatorValue = currentComparisonRule->pGpuProperty->strToEnum(parsedValue);
                        validConversion = true;
                    }
                }
                if (!validConversion)
                {
                    // discard current set of rules
                    LOGF(eDEBUG, "Extended Config| GPU Config invalid comparison value: '%s' in rule: %s", parsedValue, ruleStr);
                    tf_free(*ppConfigurationRules);
                    *ppConfigurationRules = NULL;
                    *pRulesCount = 0;
                    break;
                }
            }
            currentRule += optionLength;
        }
        // skip spaces and comma,
        currentRule += strspn(currentRule, " <=>!&,");
        ruleStr = currentRule;
    }
}

void printConfigureRules(ConfigurationRule* pRules, uint32_t rulesCount, char* result)
{
    for (uint32_t ruleIndex = 0; ruleIndex < rulesCount; ruleIndex++)
    {
        char               ruleStr[128] = { 0 };
        ConfigurationRule* currentRule = &pRules[ruleIndex];
        if (currentRule->comparatorValue != INVALID_OPTION)
        {
            snprintf(ruleStr, 128, "%s %s %u", currentRule->pGpuProperty->name, currentRule->comparator,
                     (uint32_t)(currentRule->comparatorValue));
        }
        else
        {
            snprintf(ruleStr, 128, "%s", currentRule->pGpuProperty->name);
        }
        if (ruleIndex > 0)
        {
            strcat(result, " && ");
        }
        strcat(result, ruleStr);
    }
}

// Returns false if the GPU fails any GPU_SELECTION rule that has an explicit comparator value.
// Rules without a comparator value (e.g. bare "VRAM;") are ranking-only hints, not hard requirements.
static bool IsGpuSupported(TFGpuDesc* gpuSettings)
{
    // Check each line within GPU_SELECTION section.
    for (uint32_t selectionIndex = 0; selectionIndex < gGraphicsConfigRules.mGPUSelectionRulesCount; selectionIndex++)
    {
        GPUSelectionRule* selectionRule = &gGraphicsConfigRules.mGPUSelectionRules[selectionIndex];
        // Check each comma separated rule within each GPU_SELECTION line
        for (uint32_t ruleIndex = 0; ruleIndex < selectionRule->comparisonRulesCount; ruleIndex++)
        {
            ConfigurationRule* checkRule = &selectionRule->pGpuComparisonRules[ruleIndex];
            // If the rule is a valid comparison rule
            if (checkRule != NULL && checkRule->comparatorValue != INVALID_OPTION)
            {
                uint64_t checkValue = checkRule->pGpuProperty->readValue(gpuSettings);
                if (checkValue != INVALID_OPTION)
                {
                    bool supported = tokenCompare(checkRule->comparator, checkValue, checkRule->comparatorValue);
                    if (!supported)
                    {
                        if (checkRule->pGpuProperty->enumToStr)
                        {
                            enum
                            {
                                MAX_REQ_VAL_STR = 256
                            };
                            char requiredValueStr[MAX_REQ_VAL_STR] = { 0 };
                            snprintf(requiredValueStr, MAX_REQ_VAL_STR, "%s",
                                     checkRule->pGpuProperty->enumToStr((uint32_t)checkRule->comparatorValue));
                            GenerateUnsupportedMessageStr(checkRule->pGpuProperty->name,
                                                          checkRule->pGpuProperty->enumToStr((uint32_t)checkValue), requiredValueStr);
                        }
                        else
                        {
                            GenerateUnsupportedMessage(checkRule->pGpuProperty->name, checkValue, checkRule->comparator,
                                                       checkRule->comparatorValue);
                        }
                        return false;
                    }
                }
            }
        }
    }
    return true;
}

// Returns true if testSettings should be preferred over refSettings given the ranking rules.
// vendorid/deviceid are excluded from ranking: they gate entry via IsGpuSupported, not rank order.
static bool IsGpuPreferredOver(TFGpuDesc* testSettings, TFGpuDesc* refSettings, GPUSelectionRule* choices, uint32_t choicesCount)
{
    for (uint32_t choiceIndex = 0; choiceIndex < choicesCount; choiceIndex++)
    {
        GPUSelectionRule* currentGPUChoice = &choices[choiceIndex];
        for (uint32_t ruleIndex = 0; ruleIndex < currentGPUChoice->comparisonRulesCount; ruleIndex++)
        {
            ConfigurationRule* currentRule = &currentGPUChoice->pGpuComparisonRules[ruleIndex];
            if (currentRule != NULL)
            {
                if ((strcmp(currentRule->pGpuProperty->name, "vendorid") == 0) ||
                    (strcmp(currentRule->pGpuProperty->name, "deviceid") == 0))
                {
                    // Don't do comparisions for vendorid or deviceid
                    continue;
                }

                bool     refPass = true;
                bool     testPass = true;
                uint64_t refValue = currentRule->pGpuProperty->readValue(refSettings);
                uint64_t testValue = currentRule->pGpuProperty->readValue(testSettings);
                if (refValue != INVALID_OPTION && testValue != INVALID_OPTION)
                {
                    if (currentRule->comparatorValue != INVALID_OPTION)
                    {
                        refPass &= tokenCompare(currentRule->comparator, refValue, currentRule->comparatorValue);
                        testPass &= tokenCompare(currentRule->comparator, testValue, currentRule->comparatorValue);
                    }
                    else
                    {
                        testPass &= testValue >= refValue;
                        refPass &= refValue >= testValue;
                    }
                }

                if (testPass != refPass)
                {
                    // log rule selection
                    TFGpuDesc* chosenSettings = testPass ? testSettings : refSettings;
                    TFGpuDesc* nonChosenSettings = testPass ? refSettings : testSettings;
                    uint64_t   chosenValue = testPass ? testValue : refValue;
                    uint64_t   nonChosenValue = testPass ? refValue : testValue;
                    LOGF(eINFO, "Choosing GPU: %s", chosenSettings->mGpuVendorPreset.mGpuName);
                    if (currentRule->comparatorValue != INVALID_OPTION)
                    {
                        LOGF(eINFO, "%s::%s: %llu %s %llu return true", chosenSettings->mGpuVendorPreset.mGpuName,
                             currentRule->pGpuProperty->name, chosenValue, currentRule->comparator, currentRule->comparatorValue);
                        LOGF(eINFO, "%s::%s: %llu %s %llu return false", nonChosenSettings->mGpuVendorPreset.mGpuName,
                             currentRule->pGpuProperty->name, nonChosenValue, currentRule->comparator, currentRule->comparatorValue);
                    }
                    else
                    {
                        LOGF(eINFO, "%s::%s: %llu >= %s::%s: %llu return true", chosenSettings->mGpuVendorPreset.mGpuName,
                             currentRule->pGpuProperty->name, chosenValue, nonChosenSettings->mGpuVendorPreset.mGpuName,
                             currentRule->pGpuProperty->name, nonChosenValue);
                    }
                    // return if test is better than ref
                    return testPass && !refPass;
                }
            }
        }
    }

    return false;
}

// Driver version is a dotted string (e.g. "30.0.12.3"), not a scalar GPU property, so it cannot
// be expressed as a GPU_SELECTION rule and needs its own multi-component version comparison logic.
static bool CheckDriverRejection(const TFGpuDesc* pGpuDesc)
{
    DriverVersion driverVersion = { { 0 } };
    bool          hasValidDriverStr = parseDriverVersion(pGpuDesc->mGpuVendorPreset.mGpuDriverVersion, &driverVersion);
    if (hasValidDriverStr)
    {
        for (uint32_t i = 0; i < gGraphicsConfigRules.mDriverRejectionRulesCount; i++)
        {
            if (pGpuDesc->mGpuVendorPreset.mVendorId == gGraphicsConfigRules.mDriverRejectionRules[i].vendorId)
            {
                DriverVersion* comparisonVersion = &gGraphicsConfigRules.mDriverRejectionRules[i].driverComparisonValue;
                uint32_t       tokenLength = TF_MAX(comparisonVersion->versionNumbersCount, driverVersion.versionNumbersCount);
                bool           shouldCheckEqualityFirst = strcmp(gGraphicsConfigRules.mDriverRejectionRules[i].comparator, "<=") == 0 ||
                                                strcmp(gGraphicsConfigRules.mDriverRejectionRules[i].comparator, ">=") == 0 ||
                                                strcmp(gGraphicsConfigRules.mDriverRejectionRules[i].comparator, "==") == 0;
                // first check for equality 30.0.12 <= 30.0.12.0
                if (shouldCheckEqualityFirst)
                {
                    bool isEqual = true;
                    for (uint32_t j = 0; j < tokenLength; j++)
                    {
                        isEqual &= driverVersion.versionNumbers[j] == comparisonVersion->versionNumbers[j];
                    }

                    if (isEqual)
                    {
                        LOGF(eINFO, "Driver rejection: %s %s %u.%u.%u.%u", pGpuDesc->mGpuVendorPreset.mGpuDriverVersion,
                             gGraphicsConfigRules.mDriverRejectionRules[i].comparator, comparisonVersion->versionNumbers[0],
                             comparisonVersion->versionNumbers[1], comparisonVersion->versionNumbers[2],
                             comparisonVersion->versionNumbers[3]);
                        LOGF(eINFO, "Driver rejection reason: %s ", gGraphicsConfigRules.mDriverRejectionRules[i].reasonStr);
                        return false;
                    }
                }

                // then return after the first non equal value 30.2.12 < 30.3.12.3
                for (uint32_t j = 0; j < comparisonVersion->versionNumbersCount; j++)
                {
                    if (driverVersion.versionNumbers[j] != comparisonVersion->versionNumbers[j])
                    {
                        bool shouldBeRejected = tokenCompare(gGraphicsConfigRules.mDriverRejectionRules[i].comparator,
                                                             driverVersion.versionNumbers[j], comparisonVersion->versionNumbers[j]);
                        if (shouldBeRejected)
                        {
                            LOGF(eINFO, "Driver rejection: %s %s %u.%u.%u.%u", pGpuDesc->mGpuVendorPreset.mGpuDriverVersion,
                                 gGraphicsConfigRules.mDriverRejectionRules[i].comparator, comparisonVersion->versionNumbers[0],
                                 comparisonVersion->versionNumbers[1], comparisonVersion->versionNumbers[2],
                                 comparisonVersion->versionNumbers[3]);
                            LOGF(eINFO, "Driver rejection reason: %s ", gGraphicsConfigRules.mDriverRejectionRules[i].reasonStr);
                            return false;
                        }
                        break;
                    }
                }
            }
        }
    }
    return true;
}

// Single-pass selection: filters and ranks simultaneously so only valid GPUs ever participate
// in comparisons. Returns UINT32_MAX if no GPU passes all checks.
uint32_t selectBestSupportedGpu(TFGpuDesc* availableSettings, uint32_t gpuCount)
{
    if (gpuCount == 0)
        return UINT32_MAX;

    uint32_t selectedGpu = UINT32_MAX;
    for (uint32_t i = 0; i < gpuCount; ++i)
    {
        if (!IsGpuSupported(&availableSettings[i]))
        {
            LOGF(eWARNING, "GPU[%u] '%s' skipped: %s", i, availableSettings[i].mGpuVendorPreset.mGpuName, getUnsupportedGPUMsg());
            continue;
        }
        if (!CheckDriverRejection(&availableSettings[i]))
        {
            LOGF(eWARNING, "GPU[%u] '%s' skipped: driver rejected by gpu.cfg", i, availableSettings[i].mGpuVendorPreset.mGpuName);
            continue;
        }
        if (selectedGpu == UINT32_MAX ||
            IsGpuPreferredOver(&availableSettings[i], &availableSettings[selectedGpu], gGraphicsConfigRules.mGPUSelectionRules,
                               gGraphicsConfigRules.mGPUSelectionRulesCount))
        {
            selectedGpu = i;
        }
    }

    // User-requested GPU overrides ranking, but only if it passes all checks.
    if (gGpuSelection.mPreferedGpuId != 0)
    {
        for (uint32_t i = 0; i < gpuCount; ++i)
        {
            if (availableSettings[i].mGpuVendorPreset.mModelId == gGpuSelection.mPreferedGpuId && IsGpuSupported(&availableSettings[i]) &&
                CheckDriverRejection(&availableSettings[i]))
            {
                selectedGpu = i;
                break;
            }
        }
    }

    return selectedGpu;
}

void configureGpuSettings(struct TFGpuDesc* pGpuSettings)
{
    pGpuSettings->mGPUTarget = getGPUTarget(pGpuSettings->mGpuVendorPreset.mVendorId, pGpuSettings->mGpuVendorPreset.mModelId);

    for (uint32_t propertyIdx = 0; propertyIdx < gGraphicsConfigRules.mGPUConfigurationRulesCount; ++propertyIdx)
    {
        GPUConfigurationRule* currentGPUConfigurationRule = &gGraphicsConfigRules.mGPUConfigurationRules[propertyIdx];
        bool                  hasValidatedComparisonRules = true;
        for (uint32_t ruleIdx = 0; ruleIdx < currentGPUConfigurationRule->comparisonRulesCount; ++ruleIdx)
        {
            ConfigurationRule* currentRule = &currentGPUConfigurationRule->pConfigurationRules[ruleIdx];
            uint64_t           refValue = currentRule->pGpuProperty->readValue(pGpuSettings);
            if (currentRule->comparatorValue != INVALID_OPTION)
            {
                hasValidatedComparisonRules &= tokenCompare(currentRule->comparator, refValue, currentRule->comparatorValue);
            }
            else
            {
                hasValidatedComparisonRules &= (refValue > 0);
            }
        }

        if (hasValidatedComparisonRules)
        {
            LOGF(eINFO, "GPU: %s, setting %s to %llu", pGpuSettings->mGpuVendorPreset.mGpuName,
                 currentGPUConfigurationRule->pUpdateProperty->name, currentGPUConfigurationRule->assignmentValue);
            currentGPUConfigurationRule->pUpdateProperty->writeValue(pGpuSettings, currentGPUConfigurationRule->assignmentValue);
        }
    }

    for (uint32_t texturePropIdx = 0; texturePropIdx < gGraphicsConfigRules.mTextureSupportRulesCount; ++texturePropIdx)
    {
        TextureSupportRule* currentTextureSupportRule = &gGraphicsConfigRules.mTextureSupportRules[texturePropIdx];
        bool                hasValidatedComparisonRules = true;
        for (uint32_t ruleIdx = 0; ruleIdx < currentTextureSupportRule->comparisonRulesCount; ++ruleIdx)
        {
            ConfigurationRule* currentRule = &currentTextureSupportRule->pConfigurationRules[ruleIdx];
            uint64_t           refValue = currentRule->pGpuProperty->readValue(pGpuSettings);
            if (currentRule->comparatorValue != INVALID_OPTION)
            {
                hasValidatedComparisonRules &= tokenCompare(currentRule->comparator, refValue, currentRule->comparatorValue);
            }
            else
            {
                hasValidatedComparisonRules &= (refValue > 0);
            }
        }

        if (hasValidatedComparisonRules)
        {
            char oldCaps[8] = { 0 };
            char newCaps[8] = { 0 };
            formatCapabilityToCapabilityFlags(pGpuSettings->mFormatCaps[currentTextureSupportRule->imageFormat], oldCaps);
            if (currentTextureSupportRule->enableCapibility)
            {
                pGpuSettings->mFormatCaps[currentTextureSupportRule->imageFormat] |= currentTextureSupportRule->formatCapability;
            }
            else
            {
                pGpuSettings->mFormatCaps[currentTextureSupportRule->imageFormat] &= (~currentTextureSupportRule->formatCapability);
            }
            formatCapabilityToCapabilityFlags(pGpuSettings->mFormatCaps[currentTextureSupportRule->imageFormat], newCaps);
            LOGF(eINFO, "Texture format: %s, change from %s to %s", TinyImageFormat_Name(currentTextureSupportRule->imageFormat), oldCaps,
                 newCaps);
        }
    }
}

static void ConfigureExtendedSettings(TFExtendedSettings* pExtendedSettings, const TFGpuDesc* pGpuDesc)
{
    ASSERT(pExtendedSettings && pExtendedSettings->pSettings);

    // apply rules to TFExtendedSettings
    for (uint32_t extPropIdx = 0; extPropIdx < gGraphicsConfigRules.mExtendedConfigurationRulesCount; ++extPropIdx)
    {
        ExtendedConfigurationRule* currentExtendedRule = &gGraphicsConfigRules.mExtendedConfigurationRules[extPropIdx];
        bool                       hasValidatedComparisonRules = true;
        for (uint32_t ruleIdx = 0; ruleIdx < currentExtendedRule->comparisonRulesCount; ++ruleIdx)
        {
            ConfigurationRule* currentRule = &currentExtendedRule->pConfigurationRules[ruleIdx];
            uint64_t           refValue = currentRule->pGpuProperty->readValue(pGpuDesc);
            if (currentRule->comparatorValue != INVALID_OPTION)
            {
                hasValidatedComparisonRules &= tokenCompare(currentRule->comparator, refValue, currentRule->comparatorValue);
            }
            else
            {
                hasValidatedComparisonRules &= (refValue > 0);
            }
        }

        if (hasValidatedComparisonRules)
        {
            LOGF(eINFO, "Extended setting: setting %s to %u", currentExtendedRule->name, currentExtendedRule->assignmentValue);
            *currentExtendedRule->pSettingValue = currentExtendedRule->assignmentValue;
        }
    }
}

TFGPUPresetLevel getGPUPresetLevel(uint32_t vendorId, uint32_t modelId, const char* vendorName, const char* modelName)
{
    UNREF_PARAM(vendorName);
    UNREF_PARAM(modelName);
#ifdef TF_ENABLE_GRAPHICS_RUNTIME_CHECK
    TFGPUPresetLevel presetLevel = TF_GPU_PRESET_NONE;
#else
    TFGPUPresetLevel presetLevel = gDefaultPresetLevel;
#endif

    if (arrlenu(gGraphicsConfigRules.mGPUModels))
    {
        for (uint32_t gpuModelIndex = 0; gpuModelIndex < arrlenu(gGraphicsConfigRules.mGPUModels); ++gpuModelIndex)
        {
            GPUModelDefinition model = gGraphicsConfigRules.mGPUModels[gpuModelIndex];
            if (model.mVendorId == vendorId && model.mDeviceId == modelId && model.mDeviceId)
            {
                presetLevel = model.mPreset;
                break;
            }
        }
    }

#if defined(TF_ENABLE_GRAPHICS_RUNTIME_CHECK)
    if (presetLevel != TF_GPU_PRESET_NONE)
    {
        LOGF(eINFO, "Setting preset level %s for gpu vendor:%s model:%s", presetLevelToString(presetLevel), vendorName, modelName);
    }
    else
    {
        presetLevel = gDefaultPresetLevel;
        LOGF(eWARNING, "Couldn't find gpu %s model: %s in gpu.data. Setting preset to %s as a default.", vendorName, modelName,
             presetLevelToString(presetLevel));
    }
#endif

    return presetLevel;
}

static GPUTarget getGPUTarget(uint32_t vendorId, uint32_t modelId)
{
    GPUTarget target = gDefaultTarget;

    if (arrlenu(gGraphicsConfigRules.mGPUModels))
    {
        for (uint32_t gpuModelIndex = 0; gpuModelIndex < arrlenu(gGraphicsConfigRules.mGPUModels); ++gpuModelIndex)
        {
            GPUModelDefinition model = gGraphicsConfigRules.mGPUModels[gpuModelIndex];
            if (model.mVendorId == vendorId && model.mDeviceId == modelId && model.mDeviceId)
            {
                target = model.mTarget;
                break;
            }
        }
    }

    return target;
}
///////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////
// HELPER DEFINITIONS

// --- Interpreting Helpers --- //

const GPUProperty* propertyNameToGpuProperty(const char* str)
{
    for (uint32_t i = 0; i < TF_ARRAY_COUNT(availableGpuProperties); i++)
    {
        if (strcmp(str, availableGpuProperties[i].name) == 0)
        {
            return &availableGpuProperties[i];
        }
    }
    return NULL;
}

bool isValidGPUVendorId(uint32_t vendorId)
{
    for (uint32_t i = 0; i < gGPUVendorCount; i++)
    {
        GPUVendorDefinition* currentGPUVendorDefinition = &gGPUVendorDefinitions[i];
        for (uint32_t j = 0; j < currentGPUVendorDefinition->identifierCount; j++)
            if (currentGPUVendorDefinition->identifierArray[j] == vendorId)
            {
                return true;
            }
    }
    return false;
}

const char* getGPUVendorName(uint32_t vendorId)
{
    for (uint32_t i = 0; i < gGPUVendorCount; i++)
    {
        GPUVendorDefinition* currentGPUVendorDefinition = &gGPUVendorDefinitions[i];
        for (uint32_t j = 0; j < currentGPUVendorDefinition->identifierCount; j++)
            if (currentGPUVendorDefinition->identifierArray[j] == vendorId)
            {
                return currentGPUVendorDefinition->vendorName;
            }
    }
    return "UNKNOWN_GPU_VENDOR";
}

bool gpuVendorEquals(uint32_t vendorId, const char* vendorName)
{
    char currentVendorNameLower[TF_MAX_GPU_VENDOR_STRING_LENGTH] = { 0 };
    char comparisonValueLower[TF_MAX_GPU_VENDOR_STRING_LENGTH] = { 0 };
    strncpy(currentVendorNameLower, getGPUVendorName(vendorId), TF_MAX_GPU_VENDOR_STRING_LENGTH - 1);
    strncpy(comparisonValueLower, vendorName, TF_MAX_GPU_VENDOR_STRING_LENGTH - 1);
    stringToLower(currentVendorNameLower);
    stringToLower(comparisonValueLower);
    return strcmp(currentVendorNameLower, comparisonValueLower) == 0;
}

#if defined(__APPLE__)
uint32_t getGPUModelID(const char* modelName)
{
    for (uint32_t i = 0; i < arrlenu(gGraphicsConfigRules.mGPUModels); ++i)
    {
        GPUModelDefinition* model = &gGraphicsConfigRules.mGPUModels[i];
        if (!strncmp(modelName, model->mModelName, TF_ARRAY_COUNT(model->mModelName)))
        {
            return model->mDeviceId;
        }
    }
    return UINT32_MAX;
}
#endif

uint32_t getSettingIndex(const char* settingName, uint32_t numSettings, const char** gameSettingNames)
{
    for (uint32_t i = 0; i < numSettings; ++i)
        if (strcmp(settingName, gameSettingNames[i]) == 0)
            return i;

    return INVALID_OPTION;
}

bool parseDriverVersion(const char* driverStr, DriverVersion* pDriverVersionOut)
{
    const char* driverStrEnd = driverStr + strlen(driverStr);
    if (driverStr[0] == '\0')
    {
        return false;
    }
    size_t tokenLength = strcspn(driverStr, " .-\r\n");
    bool   validConversion = true;
    while (driverStr != driverStrEnd)
    {
        if (pDriverVersionOut->versionNumbersCount > 3)
        {
            validConversion = false;
            break;
        }
        char currentNumber[32] = { 0 };
        strncpy(currentNumber, driverStr, tokenLength);
        // 32 characters is large enough to store a version number, the warning can be suppressed
        validConversion &= stringToInteger(currentNumber, &pDriverVersionOut->versionNumbers[pDriverVersionOut->versionNumbersCount], 10);
        driverStr += tokenLength;
        driverStr += strspn(driverStr, " .-\r\n");
        tokenLength = strcspn(driverStr, " .-\r\n");
        pDriverVersionOut->versionNumbersCount++;
    }
    return validConversion;
}

/*
 * t:  supports being used as a render target
 * s:  supports being sample inside a shader
 * f:  supports being sample by a linear sampler
 * d:  supports being used as a depth/stencil target
 * w:  supports being written in a UAV
 * rw/wr: supports being both written and readen in a UAV
 */
void formatCapabilityToCapabilityFlags(TFFormatCapability caps, char* pStrOut)
{
    TFFormatCapability availableCaps[4] = { TF_FORMAT_CAP_READ, TF_FORMAT_CAP_LINEAR_FILTER, TF_FORMAT_CAP_RENDER_TARGET,
                                            TF_FORMAT_CAP_DEPTH_STENCIL };
    char               availableCapStr[4] = { 's', 'f', 't', 'd' };
    uint8_t            writeIndex = 0;

    if (caps & TF_FORMAT_CAP_READ_WRITE)
    {
        pStrOut[writeIndex++] = 'r';
        pStrOut[writeIndex++] = 'w';
    }
    else if (caps & TF_FORMAT_CAP_WRITE)
    {
        pStrOut[writeIndex++] = 'w';
    }

    for (uint8_t currentCap = 0; currentCap < 4; currentCap++)
    {
        if (caps & availableCaps[currentCap])
        {
            pStrOut[writeIndex++] = availableCapStr[currentCap];
        }
    }
}

void initGPUConfig(TFExtendedSettings* pExtendedSettings) { addGPUConfigurationRules(pExtendedSettings); }

void exitGPUConfig()
{
    removeGPUConfigurationRules();
    gGpuSelection.mAvailableGpuCount = 0;
    gGpuSelection.mSelectedGpuIndex = 0;
}

TFResolution getGPUCfgSceneResolution(uint32_t displayWidth, uint32_t displayHeight)
{
    ASSERTMSG(gAvailableSceneResolutionCount, "getGPUCfgSceneResolution called without specifying list of resolutions in gpucfg");
    const TFResolution* res = &gAvailableSceneResolutions[gSceneResolutionIndex];
    TFResolution        ret = { 0 };
    ret.mWidth = res->mWidth;
    ret.mHeight = res->mHeight;
    // Native resolution
    if (!ret.mWidth || !ret.mHeight)
    {
        ret.mWidth = displayWidth;
        ret.mHeight = displayHeight;
    }

    return ret;
}
/************************************************************************/
// Internal initialization functions
/************************************************************************/
void setGPUConfig(TFGpuDesc* pGpus, uint32_t gpuCount, uint32_t selectedIndex, TFExtendedSettings* pExtendedSettings)
{
    gGpuSelection.mAvailableGpuCount = 0;
    gGpuSelection.mSelectedGpuIndex = 0;
    // update available gpus
    if (pGpus != NULL)
    {
        ASSERT(gpuCount <= TF_MAX_MULTIPLE_GPUS);
        gGpuSelection.mAvailableGpuCount = gpuCount;
        gGpuSelection.mSelectedGpuIndex = selectedIndex;
        for (uint32_t i = 0; i < gpuCount; ++i)
        {
            TFGpuDesc* pGpuDesc = &pGpus[i];
            strncpy(gGpuSelection.ppAvailableGpuNames[i], pGpuDesc->mGpuVendorPreset.mGpuName, TF_MAX_GPU_VENDOR_STRING_LENGTH);
            gGpuSelection.pAvailableGpuIds[i] = pGpuDesc->mGpuVendorPreset.mModelId;
        }

        // configure the user's settings using the newly created device
        if (pExtendedSettings)
        {
            ConfigureExtendedSettings(pExtendedSettings, &pGpus[selectedIndex]);
        }

        if (gAvailableSceneResolutionCount)
        {
            LOGF(eINFO, "Scene resolution: setting to %u for target %u", gSceneResolutionIndex, (uint32_t)pGpus[selectedIndex].mGPUTarget);
        }
    }
}
