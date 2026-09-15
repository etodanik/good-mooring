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

#ifndef FORGE_RENDERER_CONFIG_H
#define FORGE_RENDERER_CONFIG_H

// Support external config file override
#if defined(EXTERNAL_RENDERER_CONFIG_FILEPATH)
#include EXTERNAL_RENDERER_CONFIG_FILEPATH
#elif defined(EXTERNAL_RENDERER_CONFIG_FILEPATH_NO_STRING)
// When invoking clanng from FastBuild the EXTERNAL_CONFIG_FILEPATH define doesn't get expanded to a string,
// quotes are removed, that's why we add this variation of the macro that turns the define back into a valid string
#define TF_EXTERNAL_CONFIG_STRINGIFY2(x) #x
#define TF_EXTERNAL_CONFIG_STRINGIFY(x)  TF_EXTERNAL_CONFIG_STRINGIFY2(x)

#include TF_EXTERNAL_CONFIG_STRINGIFY(EXTERNAL_RENDERER_CONFIG_FILEPATH_NO_STRING)

#undef TF_EXTERNAL_CONFIG_STRINGIFY
#undef TF_EXTERNAL_CONFIG_STRINGIFY2
#else

#include "../../Application/Config.h"
#include "../../OS/Interfaces/IOperatingSystem.h"
#include "../../Resources/ResourceLoader/ThirdParty/OpenSource/tinyimageformat/tinyimageformat_base.h"

// ------------------------------- renderer configuration ------------------------------- //

// Comment/uncomment includes to disable/enable rendering APIs
#if defined(_WINDOWS)
#if defined(FORGE_EXPLICIT_RENDERER_API)
#if defined(FORGE_EXPLICIT_RENDERER_API_VULKAN)
#include "../Vulkan/VulkanConfig.h"
#endif
#endif
#ifndef _WINDOWS7
#if !defined(FORGE_EXPLICIT_RENDERER_API)
#include "../Direct3D12/Direct3D12Config.h"
#endif
#endif
#elif defined(XBOX)
#include "../Direct3D12/Direct3D12Config.h"
#elif defined(__APPLE__)
#include "../Metal/MetalConfig.h"
#elif defined(__ANDROID__)
#ifdef ARCH_ARM64
#include "../Vulkan/VulkanConfig.h"
#endif
#elif defined(NX64)
#include "../Vulkan/VulkanConfig.h"
#elif defined(__linux__)
#include "../Vulkan/VulkanConfig.h"
#endif

#if defined(QUEST_VR)
#define XR_USE_GRAPHICS_API_VULKAN 1
#define XR_USE_PLATFORM_ANDROID    1
#elif defined(HOLOLENS2)
#define XR_USE_GRAPHICS_API_D3D12 1
#define XR_USE_PLATFORM_WIN32     1
#endif

#if defined(QUEST_VR) || defined(HOLOLENS2)
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <xr_linear.h>
#endif

// Uncomment this macro to define custom rendering max options
// #define TF_RENDERER_CUSTOM_MAX
#ifdef TF_RENDERER_CUSTOM_MAX
enum
{
    TF_MAX_INSTANCE_EXTENSIONS = 64,
    TF_MAX_DEVICE_EXTENSIONS = 64,
    /// Max number of GPUs in SLI or Cross-Fire
    TF_MAX_LINKED_GPUS = 4,
    TF_MAX_RENDER_TARGET_ATTACHMENTS = 8,
    TF_MAX_VERTEX_BINDINGS = 15,
    TF_MAX_VERTEX_ATTRIBS = 15,
    TF_MAX_SEMANTIC_NAME_LENGTH = 128,
    TF_MAX_DEBUG_NAME_LENGTH = 128,
    TF_MAX_MIP_LEVELS = 0xFFFFFFFF,
    TF_MAX_GPU_VENDOR_STRING_LENGTH = 64, // max size for TFGPUVendorPreset strings
#if defined(VULKAN)
    TF_MAX_PLANE_COUNT = 3,
#endif
};
#endif

// Enable raytracing if available
// Possible renderers: D3D12, Vulkan, Metal
#if defined(D3D12_RAYTRACING_AVAILABLE) || defined(VK_RAYTRACING_AVAILABLE) || defined(MTL_RAYTRACING_AVAILABLE) || defined(PROSPERO)
#define TF_ENABLE_RAYTRACING
#endif

#if defined(VK_COOP_VECTORS_AVAILABLE)
#define TF_ENABLE_COOP_VECTORS
#endif

#if defined(D3D12_COOP_VECTORS_AVAILABLE)
#define TF_ENABLE_COOP_VECTORS
#endif

#if defined(D3D12_WORKGRAPH_AVAILABLE)
#define TF_ENABLE_WORKGRAPH
#endif

#ifdef ENABLE_PROFILER
#if defined(DIRECT3D12) || defined(VULKAN) || defined(METAL) || defined(ORBIS) || defined(PROSPERO)
#define TF_ENABLE_GPU_PROFILER
#endif
#endif

// Enable graphics debug if general debug is turned on
#ifdef FORGE_DEBUG
// Runtime checks by Forge itself
#define TF_ENABLE_GRAPHICS_RUNTIME_CHECK
// Graphics API Validation
#define TF_ENABLE_GRAPHICS_VALIDATION
// Object names, markers and labels
#define TF_ENABLE_GRAPHICS_DEBUG_ANNOTATION
#endif

#if defined(TF_ENABLE_GRAPHICS_RUNTIME_CHECK) || defined(PVS_STUDIO)
#define VALIDATE_DESCRIPTOR(descriptor, msgFmt, ...)                           \
    if (!VERIFYMSG((descriptor), "%s : " msgFmt, __FUNCTION__, ##__VA_ARGS__)) \
    {                                                                          \
        continue;                                                              \
    }
#define IF_VALIDATE_DESCRIPTOR(...)            __VA_ARGS__
#define IF_VALIDATE_DESCRIPTOR_MEMBER(T, Name) T Name;
#else
#define VALIDATE_DESCRIPTOR(descriptor, ...)
#define IF_VALIDATE_DESCRIPTOR(...)
#define IF_VALIDATE_DESCRIPTOR_MEMBER(T, Name)
#endif

#ifdef FORGE_PROFILE
// If configuration is profile, this disables validation while keeping debug annotation on
#undef TF_ENABLE_GRAPHICS_RUNTIME_CHECK
#undef TF_ENABLE_GRAPHICS_VALIDATION
#define TF_ENABLE_GRAPHICS_DEBUG_ANNOTATION
#endif

#if (defined(DIRECT3D12) + defined(VULKAN) + defined(METAL) + defined(ORBIS) + defined(PROSPERO) + defined(NX64)) == 0
#error "No rendering API defined"
#endif

#if defined(ANDROID) || defined(SWITCH) || defined(TARGET_APPLE_ARM64)
#define TF_USE_MSAA_RESOLVE_ATTACHMENTS
#endif

#ifdef FORGE_DEBUG
#define TF_ENABLE_DEPENDENCY_TRACKER
#endif

#if defined(FORGE_DEBUG) && defined(VULKAN)
#define TF_GFX_DRIVER_MEMORY_TRACKING
#define TF_GFX_DEVICE_MEMORY_TRACKING
#endif

#if defined(_WIN32) && !defined(XBOX)
#define TF_D3D11_DYNAMIC_LOADING
#define TF_D3D12_DYNAMIC_LOADING
#endif

#endif
#endif

// max size for TFGPUVendorPreset strings
#define TF_MAX_GPU_VENDOR_STRING_LENGTH 256u
/// Max number of GPus for either linked or unlinked mode. must update WindowsBase::setupPlatformUI accordingly
#define TF_MAX_MULTIPLE_GPUS            4u

// ------------------------------- gpu configuration rules ------------------------------- //

struct TFGpuDesc;
struct TFRenderer;
typedef struct TFExtendedSettings
{
    uint32_t     mNumSettings;
    uint32_t*    pSettings;
    const char** ppSettingNames;
} TFExtendedSettings;

typedef enum TFGPUPresetLevel
{
    TF_GPU_PRESET_NONE = 0,
    TF_GPU_PRESET_OFFICE,  // This means unsupported
    TF_GPU_PRESET_VERYLOW, // Mostly for mobile GPU
    TF_GPU_PRESET_LOW,
    TF_GPU_PRESET_MEDIUM,
    TF_GPU_PRESET_HIGH,
    TF_GPU_PRESET_ULTRA,
    TF_GPU_PRESET_COUNT
} TFGPUPresetLevel;

typedef enum TFFormatCapability
{
    TF_FORMAT_CAP_NONE = 0,
    TF_FORMAT_CAP_LINEAR_FILTER = 0x1,
    TF_FORMAT_CAP_READ = 0x2,
    TF_FORMAT_CAP_WRITE = 0x4,
    TF_FORMAT_CAP_READ_WRITE = 0x8,
    TF_FORMAT_CAP_RENDER_TARGET = 0x10,
    TF_FORMAT_CAP_DEPTH_STENCIL = 0x20,
} TFFormatCapability;
MAKE_ENUM_FLAG(uint32_t, TFFormatCapability);

typedef struct TFGPUVendorPreset
{
    uint32_t         mVendorId;
    uint32_t         mModelId;
    uint32_t         mRevisionId; // Optional as not all gpu's have that. Default is : 0x00
    TFGPUPresetLevel mPresetLevel;
    char             mVendorName[TF_MAX_GPU_VENDOR_STRING_LENGTH];
    char             mGpuName[TF_MAX_GPU_VENDOR_STRING_LENGTH]; // If GPU Name is missing then value will be empty string
    char             mGpuDriverVersion[TF_MAX_GPU_VENDOR_STRING_LENGTH];
    char             mGpuDriverDate[TF_MAX_GPU_VENDOR_STRING_LENGTH];
    uint32_t         mRTCoresCount;
} TFGPUVendorPreset;

typedef enum TFWaveOpsSupportFlags
{
    TF_WAVE_OPS_SUPPORT_FLAG_NONE = 0x0,
    TF_WAVE_OPS_SUPPORT_FLAG_BASIC_BIT = 0x00000001,
    TF_WAVE_OPS_SUPPORT_FLAG_VOTE_BIT = 0x00000002,
    TF_WAVE_OPS_SUPPORT_FLAG_ARITHMETIC_BIT = 0x00000004,
    TF_WAVE_OPS_SUPPORT_FLAG_BALLOT_BIT = 0x00000008,
    TF_WAVE_OPS_SUPPORT_FLAG_SHUFFLE_BIT = 0x00000010,
    TF_WAVE_OPS_SUPPORT_FLAG_SHUFFLE_RELATIVE_BIT = 0x00000020,
    TF_WAVE_OPS_SUPPORT_FLAG_CLUSTERED_BIT = 0x00000040,
    TF_WAVE_OPS_SUPPORT_FLAG_QUAD_BIT = 0x00000080,
    TF_WAVE_OPS_SUPPORT_FLAG_PARTITIONED_BIT_NV = 0x00000100,
    TF_WAVE_OPS_SUPPORT_FLAG_ALL = 0x7FFFFFFF
} TFWaveOpsSupportFlags;
MAKE_ENUM_FLAG(uint32_t, TFWaveOpsSupportFlags);

typedef enum TFUMASupportFlags
{
    TF_UMA_SUPPORT_NONE = 0x0,
    TF_UMA_SUPPORT_READ = 0x1,
    TF_UMA_SUPPORT_WRITE = 0x2,
    TF_UMA_SUPPORT_READ_WRITE = TF_UMA_SUPPORT_READ | TF_UMA_SUPPORT_WRITE,
} TFUMASupportFlags;
MAKE_ENUM_FLAG(uint8_t, TFUMASupportFlags);

typedef enum TFShaderStage
{
    TF_SHADER_STAGE_NONE = 0,
    TF_SHADER_STAGE_VERT = 0x1,
    TF_SHADER_STAGE_FRAG = 0x2,
    TF_SHADER_STAGE_COMP = 0x4,
    TF_SHADER_STAGE_GEOM = 0x8,
    TF_SHADER_STAGE_TESC = 0x10,
    TF_SHADER_STAGE_TESE = 0x20,
    TF_SHADER_STAGE_ALL_GRAPHICS = ((uint32_t)TF_SHADER_STAGE_VERT | (uint32_t)TF_SHADER_STAGE_TESC | (uint32_t)TF_SHADER_STAGE_TESE |
                                    (uint32_t)TF_SHADER_STAGE_GEOM | (uint32_t)TF_SHADER_STAGE_FRAG),
    TF_SHADER_STAGE_HULL = TF_SHADER_STAGE_TESC,
    TF_SHADER_STAGE_DOMN = TF_SHADER_STAGE_TESE,
#if defined(TF_ENABLE_WORKGRAPH)
    TF_SHADER_STAGE_WORKGRAPH = 0x40,
    TF_SHADER_STAGE_COUNT = 7,
#else
    TF_SHADER_STAGE_COUNT = 6,
#endif
} TFShaderStage;
MAKE_ENUM_FLAG(uint32_t, TFShaderStage)

typedef struct TFGpuDesc
{
#if defined(DIRECT3D12)
    struct
    {
#if defined(XBOX)
        IDXGIAdapter* pGpu;
        ID3D12Device* pDevice;
#elif defined(DIRECT3D12)
        IDXGIAdapter4* pGpu;
#endif
    } mDx;
#endif
#if defined(VULKAN)
    struct
    {
        VkPhysicalDevice            pGpu;
        VkPhysicalDeviceProperties2 mGpuProperties;
    } mVk;
#endif

#if defined(METAL)
    void*    pGPU;                 // id<MTLDevice>
    void*    pCounterSetTimestamp; // id<MTLCounterSet>
    uint32_t mDrawBoundarySamplingSupported : 1;
    uint32_t mStageBoundarySamplingSupported : 1;
#endif

    TFFormatCapability mFormatCaps[TinyImageFormat_Count];

    /*************************************************************************************/
    // GPU Properties
    /*************************************************************************************/
    // update availableGpuProperties, setDefaultGPUProperties in GraphicsConfig.c
    // if you made changes to this list
    uint64_t mVRAM;
    uint32_t mUniformBufferAlignment;
    uint32_t mUploadBufferAlignment;
    uint32_t mUploadBufferTextureAlignment;
    uint32_t mUploadBufferTextureRowAlignment;
    uint32_t mMaxVertexInputBindings;
#if defined(DIRECT3D12)
    uint32_t mMaxRootSignatureDWORDS;
#endif
    uint32_t              mWaveLaneCount;
    TFWaveOpsSupportFlags mWaveOpsSupportFlags;
    TFGPUVendorPreset     mGpuVendorPreset;
    TFShaderStage         mWaveOpsSupportedStageFlags;

    uint32_t mMaxTotalComputeThreads;
    uint32_t mMaxComputeThreads[3];
    uint32_t mMultiDrawIndirect : 1;
    uint32_t mMultiDrawIndirectCount : 1;
    uint32_t mRootConstant : 1;
    uint32_t mIndirectRootConstant : 1;
    uint32_t mBuiltinDrawID : 1;
    uint32_t mIndirectCommandBuffer : 1;
    uint32_t mROVsSupported : 1;
    uint32_t mTessellationSupported : 1;
    uint32_t mGeometryShaderSupported : 1;
    uint32_t mGpuMarkers : 1;
    uint32_t mHDRSupported : 1;
    uint32_t mTimestampQueries : 1;
    uint32_t mOcclusionQueries : 1;
    uint32_t mPipelineStatsQueries : 1;
    uint32_t mAllowBufferTextureInSameHeap : 1;
    uint32_t mRaytracingSupported : 1;
    uint32_t mUnifiedMemorySupport : 2;
    uint32_t mRayPipelineSupported : 1;
    uint32_t mRayQuerySupported : 1;
    uint32_t mWorkgraphSupported : 1;
    uint32_t mSoftwareVRSSupported : 1;
    uint32_t mPrimitiveIdSupported : 1;
    uint32_t mPrimitiveIdPsSupported : 1;
    uint32_t m64BitAtomicsSupported : 1;
    uint32_t mCoopVectorsSupported : 1;
    uint32_t mShaderFloat16Supported : 1;
    uint32_t mShaderInt8Supported : 1;
    uint32_t m16bitStorageSupported : 1;
    uint32_t mVulkanMemoryModelSupported : 1;
#if defined(DIRECT3D12)
    D3D_FEATURE_LEVEL mFeatureLevel;
    uint32_t          mSuppressInvalidSubresourceStateAfterExit : 1;
    uint32_t          mRelaxedFormatCastingSupported : 1;
    uint32_t          mIsIntelIntegrated : 1;
#endif
#if defined(VULKAN)
    uint32_t mVulkanVersionMinor : 1;
    uint32_t mVulkanVersionMajor : 1;
    uint32_t mSubgroupSize;
    uint32_t mCoopMatM16N16K16Float16Supported : 1;
    uint32_t mCoopMatM8N16K16Float16Supported : 1;
    uint32_t mDynamicRenderingSupported : 1;
    uint32_t mXclipseTransferQueueWorkaround : 1;
    uint32_t mDeviceMemoryReportCrashWorkaround : 1;
    uint32_t mYCbCrExtension : 1;
    uint32_t mFillModeNonSolid : 1;
    uint32_t mKHRRayQueryExtension : 1;
    uint32_t mAMDGCNShaderExtension : 1;
    uint32_t mAMDDrawIndirectCountExtension : 1;
    uint32_t mAMDShaderInfoExtension : 1;
    uint32_t mDescriptorIndexingExtension : 1;
    uint32_t mDynamicRenderingExtension : 1;
    uint32_t mNonUniformResourceIndexSupported : 1;
    uint32_t mBufferDeviceAddressSupported : 1;
    uint32_t mDrawIndirectCountExtension : 1;
    uint32_t mDedicatedAllocationExtension : 1;
    uint32_t mDebugMarkerExtension : 1;
    uint32_t mMemoryReq2Extension : 1;
    uint32_t mFragmentShaderInterlockExtension : 1;
    uint32_t mBufferDeviceAddressExtension : 1;
    uint32_t mAccelerationStructureExtension : 1;
    uint32_t mRayTracingPipelineExtension : 1;
    uint32_t mRayQueryExtension : 1;
    uint32_t mShaderAtomicInt64Extension : 1;
    uint32_t mBufferDeviceAddressFeature : 1;
    uint32_t mShaderFloatControlsExtension : 1;
    uint32_t mSpirv14Extension : 1;
    uint32_t mDeferredHostOperationsExtension : 1;
    uint32_t mDeviceFaultExtension : 1;
    uint32_t mDeviceFaultSupported : 1;
    uint32_t mASTCDecodeModeExtension : 1;
    uint32_t mDeviceMemoryReportExtension : 1;
    uint32_t mAMDBufferMarkerExtension : 1;
    uint32_t mAMDDeviceCoherentMemoryExtension : 1;
    uint32_t mAMDDeviceCoherentMemorySupported : 1;
    uint32_t mPipelineExecutablePropertiesExtension : 1;
    uint32_t mCooperativeMatrixExtension : 1;
    uint32_t mShaderFloat16Int8Extension : 1;
    uint32_t m16bitStorageExtension : 1;
    uint32_t mVulkanMemoryModelExtension : 1;
    uint32_t mAtomicFloatExtension : 1;
    uint32_t mShaderViewportIndexLayerExtension : 1;
#if defined(VK_USE_PLATFORM_WIN32_KHR)
    uint32_t mExternalMemoryExtension : 1;
    uint32_t mExternalMemoryWin32Extension : 1;
#endif
#if defined(QUEST_VR)
    uint32_t mMultiviewExtension : 1;
#endif

#endif
    uint32_t mMaxBoundTextures;
    uint32_t mMaxStorageBufferSize;
    uint32_t mSamplerAnisotropySupported : 1;
    uint32_t mGraphicsQueueSupported : 1;
#if defined(METAL)
    uint32_t mHeaps : 1;
    uint32_t mPlacementHeaps : 1;
    uint32_t mTessellationIndirectDrawSupported : 1;
    uint32_t mDrawIndexVertexOffsetSupported : 1;
    uint32_t mCubeMapTextureArraySupported : 1;
    uint32_t mResidencySetsSupported : 1;
#if !defined(TARGET_IOS)
    uint32_t mIsHeadLess : 1; // indicates whether a GPU device does not have a connection to a display.
#endif
#endif
    uint32_t mAmdAsicFamily;
    uint32_t mFrameBufferSamplesCount;
    uint32_t mGPUTarget;
    uint32_t mTargetFPS : 8;
} TFGpuDesc;

typedef enum TFSampleCount
{
    TF_SAMPLE_COUNT_1 = 1,
    TF_SAMPLE_COUNT_2 = 2,
    TF_SAMPLE_COUNT_4 = 4,
    TF_SAMPLE_COUNT_8 = 8,
    TF_SAMPLE_COUNT_16 = 16,
    TF_SAMPLE_COUNT_COUNT = 5,
    TF_SAMPLE_COUNT_ALL_BITS = ((uint32_t)TF_SAMPLE_COUNT_1 | (uint32_t)TF_SAMPLE_COUNT_2 | (uint32_t)TF_SAMPLE_COUNT_4 |
                                (uint32_t)TF_SAMPLE_COUNT_8 | (uint32_t)TF_SAMPLE_COUNT_16),
} TFSampleCount;

typedef struct TFGPUSelection
{
    // Available GPU capabilities
    char     ppAvailableGpuNames[TF_MAX_MULTIPLE_GPUS][TF_MAX_GPU_VENDOR_STRING_LENGTH];
    uint32_t pAvailableGpuIds[TF_MAX_MULTIPLE_GPUS];
    uint32_t mAvailableGpuCount;
    uint32_t mSelectedGpuIndex;
    // Could add swap chain size, render target format, ...
    uint32_t mPreferedGpuId;
} TFGPUSelection;

#ifdef __cplusplus
extern "C"
{
#endif

// ------ app lifecycle ------
// Load gpu.cfg/gpu.data and initialise the GPU configuration system. Call before initRenderer.
FORGE_API void initGPUConfig(TFExtendedSettings* pExtendedSettings);
// Bind a live renderer to the configuration system and apply extended settings. Call after initRenderer.
FORGE_API void setGPUConfig(struct TFGpuDesc* pGpus, uint32_t gpuCount, uint32_t selectedIndex, TFExtendedSettings* pExtendedSettings);
FORGE_API void exitGPUConfig();

// ------ app utilities ------
// Scene resolution is the resolution at which we render the scene (world, terrain, skybox), distinct from UI resolution.
FORGE_API TFResolution     getGPUCfgSceneResolution(uint32_t displayWidth, uint32_t displayHeight);
FORGE_API const char*      getUnsupportedGPUMsg();
FORGE_API const char*      presetLevelToString(TFGPUPresetLevel preset);
FORGE_API TFGPUPresetLevel stringToPresetLevel(const char* presetLevel);

// ------ graphics API runtime internals ------
FORGE_API void             addGPUConfigurationRules(TFExtendedSettings* pExtendedSettings);
FORGE_API void             removeGPUConfigurationRules();
// Set the defaults for TFGpuDesc as it gets filled out later in each API
FORGE_API void             setDefaultGPUProperties(struct TFGpuDesc* pGpuDesc);
// Apply gpu.cfg overrides
FORGE_API void             configureGpuSettings(struct TFGpuDesc* pGpuSettings);
FORGE_API uint32_t         selectBestSupportedGpu(struct TFGpuDesc* availableSettings, uint32_t gpuCount);
FORGE_API TFGPUPresetLevel getGPUPresetLevel(uint32_t vendorId, uint32_t modelId, const char* vendorName, const char* modelName);
FORGE_API bool             gpuVendorEquals(uint32_t vendorId, const char* vendorName);
FORGE_API const char*      getGPUVendorName(uint32_t modelId);
#ifdef __cplusplus
}
#endif
