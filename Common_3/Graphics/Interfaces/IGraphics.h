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

#include "IGraphicsConfig.h"

#include "../../Resources/ResourceLoader/ThirdParty/OpenSource/tinyimageformat/tinyimageformat_base.h"

#include "../../Utilities/Interfaces/ILog.h"
#include "../../Utilities/Interfaces/IThread.h"

//
// default capability levels of the renderer
//
#if !defined(TF_RENDERER_CUSTOM_MAX)
enum
{
    TF_MAX_INSTANCE_EXTENSIONS = 64,
    TF_MAX_DEVICE_EXTENSIONS = 64,
    /// Max number of GPUs in SLI or Cross-Fire
    TF_MAX_LINKED_GPUS = 4,
    /// Max number of GPUs in unlinked mode
    TF_MAX_UNLINKED_GPUS = 4,
    TF_MAX_RENDER_TARGET_ATTACHMENTS = 8,
    TF_MAX_VERTEX_BINDINGS = 15,
    TF_MAX_VERTEX_ATTRIBS = 15,
    TF_MAX_RESOURCE_NAME_LENGTH = 256,
    TF_MAX_SEMANTIC_NAME_LENGTH = 128,
    TF_MAX_DEBUG_NAME_LENGTH = 128,
    TF_MAX_MIP_LEVELS = 0xFFFFFFFF,
    TF_MAX_SAMPLE_LOCATIONS = 16,
    TF_MAX_PUSH_CONSTANTS_32BIT_COUNT = 16,
    TF_MAX_DESCRIPTOR_SETS = 4,
    TF_MAX_DESCRIPTOR_SETS_MASK = 0x0F,
#if defined(DIRECT3D12)
    TF_MAX_DESCRIPTOR_TABLES = 8,
#endif
#if defined(VULKAN)
    TF_MAX_PLANE_COUNT = 3,
    TF_MAX_DESCRIPTOR_POOL_SIZE_ARRAY_COUNT =
        VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT + 2, // +1 for acceleration structure and +1 for 0 based enum
#endif
};
#endif

#ifdef DIRECT3D12
typedef int32_t DxDescriptorID;
#endif

#if defined(ORBIS)
#include "../../../PS4/Common_3/Graphics/Gnm/GnmStructs.h"
#endif
#if defined(PROSPERO)
#include "../../../Prospero/Common_3/Graphics/Agc/AgcStructs.h"
#endif

typedef enum TFRendererApi
{
#if defined(DIRECT3D12)
    TF_RENDERER_API_D3D12,
#endif
#if defined(VULKAN)
    TF_RENDERER_API_VULKAN,
#endif
#if defined(METAL)
    TF_RENDERER_API_METAL,
#endif
#if defined(ORBIS)
    TF_RENDERER_API_ORBIS,
#endif
#if defined(PROSPERO)
    TF_RENDERER_API_PROSPERO,
#endif
    TF_RENDERER_API_COUNT
} TFRendererApi;

typedef enum TFQueueType
{
    TF_QUEUE_TYPE_GRAPHICS = 0,
    TF_QUEUE_TYPE_TRANSFER,
    TF_QUEUE_TYPE_COMPUTE,
    TF_MAX_QUEUE_TYPE
} TFQueueType;

typedef enum TFQueueFlag
{
    TF_QUEUE_FLAG_NONE = 0x0,
    TF_QUEUE_FLAG_DISABLE_GPU_TIMEOUT = 0x1,
    TF_QUEUE_FLAG_INIT_MICROPROFILE = 0x2,
    TF_MAX_QUEUE_FLAG = 0xFFFFFFFF
} TFQueueFlag;
MAKE_ENUM_FLAG(uint32_t, TFQueueFlag)

typedef enum TFQueuePriority
{
    TF_QUEUE_PRIORITY_NORMAL,
    TF_QUEUE_PRIORITY_HIGH,
    TF_QUEUE_PRIORITY_GLOBAL_REALTIME,
    TF_MAX_QUEUE_PRIORITY
} TFQueuePriority;

typedef enum TFLoadActionType
{
    TF_LOAD_ACTION_DONTCARE,
    TF_LOAD_ACTION_LOAD,
    TF_LOAD_ACTION_CLEAR,
    TF_MAX_LOAD_ACTION
} TFLoadActionType;

typedef enum TFStoreActionType
{
    // Store is the most common use case so keep that as default
    TF_STORE_ACTION_STORE,
    TF_STORE_ACTION_DONTCARE,
    TF_STORE_ACTION_NONE,
#if defined(TF_USE_MSAA_RESOLVE_ATTACHMENTS)
    // Resolve into pResolveAttachment and also store the MSAA attachment (rare - maybe used for debug)
    TF_STORE_ACTION_RESOLVE_STORE,
    // Resolve into pResolveAttachment and discard MSAA attachment (most common use case for resolve)
    TF_STORE_ACTION_RESOLVE_DONTCARE,
#endif
    TF_MAX_STORE_ACTION
} TFStoreActionType;

typedef void (*LogFn)(LogLevel, const char*, const char*);

typedef enum TFResourceState
{
    TF_RESOURCE_STATE_UNDEFINED = 0,
    TF_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER = 0x1,
    TF_RESOURCE_STATE_INDEX_BUFFER = 0x2,
    TF_RESOURCE_STATE_RENDER_TARGET = 0x4,
    TF_RESOURCE_STATE_UNORDERED_ACCESS = 0x8,
    TF_RESOURCE_STATE_DEPTH_WRITE = 0x10,
    TF_RESOURCE_STATE_DEPTH_READ = 0x20,
    TF_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE = 0x40,
    TF_RESOURCE_STATE_PIXEL_SHADER_RESOURCE = 0x80,
    TF_RESOURCE_STATE_SHADER_RESOURCE = 0x40 | 0x80,
    TF_RESOURCE_STATE_STREAM_OUT = 0x100,
    TF_RESOURCE_STATE_INDIRECT_ARGUMENT = 0x200,
    TF_RESOURCE_STATE_COPY_DEST = 0x400,
    TF_RESOURCE_STATE_COPY_SOURCE = 0x800,
    TF_RESOURCE_STATE_GENERIC_READ = (((((0x1 | 0x2) | 0x40) | 0x80) | 0x200) | 0x800),
    TF_RESOURCE_STATE_PRESENT = 0x1000,
    TF_RESOURCE_STATE_COMMON = 0x2000,
    TF_RESOURCE_STATE_ACCELERATION_STRUCTURE_READ = 0x4000,
    TF_RESOURCE_STATE_ACCELERATION_STRUCTURE_WRITE = 0x8000,
#if defined(VULKAN) || defined(PROSPERO)
    TF_RESOURCE_STATE_UNORDERED_ACCESS_PIXEL = 0x10000,
#else
    TF_RESOURCE_STATE_UNORDERED_ACCESS_PIXEL = TF_RESOURCE_STATE_UNORDERED_ACCESS,
#endif
#if defined(QUEST_VR)
    TF_RESOURCE_STATE_SHADING_RATE_SOURCE = 0x20000,
#endif
} TFResourceState;
MAKE_ENUM_FLAG(uint32_t, TFResourceState)

/// Choosing Memory Type
typedef enum TFResourceMemoryUsage
{
    /// No intended memory usage specified.
    TF_RESOURCE_MEMORY_USAGE_UNKNOWN = 0,
    /// Memory will be used on device only, no need to be mapped on host.
    TF_RESOURCE_MEMORY_USAGE_GPU_ONLY = 1,
    /// Memory will be mapped on host. Could be used for transfer to device.
    TF_RESOURCE_MEMORY_USAGE_CPU_ONLY = 2,
    /// Memory will be used for frequent (dynamic) updates from host and reads on device.
    TF_RESOURCE_MEMORY_USAGE_CPU_TO_GPU = 3,
    /// Memory will be used for writing on device and readback on host.
    TF_RESOURCE_MEMORY_USAGE_GPU_TO_CPU = 4,
    TF_RESOURCE_MEMORY_USAGE_COUNT,
    TF_RESOURCE_MEMORY_USAGE_MAX_ENUM = 0x7FFFFFFF
} TFResourceMemoryUsage;

// Forward declarations
typedef struct TFRendererContext        TFRendererContext;
typedef struct TFRenderer               TFRenderer;
typedef struct TFQueue                  TFQueue;
typedef struct TFPipeline               TFPipeline;
typedef struct TFBuffer                 TFBuffer;
typedef struct TFTexture                TFTexture;
typedef struct TFRenderTarget           TFRenderTarget;
typedef struct TFRenderTargetDescriptor TFRenderTargetDescriptor;
typedef struct TFShader                 TFShader;
typedef struct TFDescriptorSet          TFDescriptorSet;
typedef struct TFDescriptorIndexMap     TFDescriptorIndexMap;
typedef struct TFPipelineCache          TFPipelineCache;

// Raytracing
typedef struct TFRaytracing            TFRaytracing;
typedef struct TFRaytracingHitGroup    TFRaytracingHitGroup;
typedef struct TFAccelerationStructure TFAccelerationStructure;

typedef struct TFEsramManager TFEsramManager;

typedef struct TFIndirectDrawArguments
{
    uint32_t mVertexCount;
    uint32_t mInstanceCount;
    uint32_t mStartVertex;
    uint32_t mStartInstance;
} TFIndirectDrawArguments;

typedef struct TFIndirectDrawIndexArguments
{
    uint32_t mIndexCount;
    uint32_t mInstanceCount;
    uint32_t mStartIndex;
    uint32_t mVertexOffset;
    uint32_t mStartInstance;
} TFIndirectDrawIndexArguments;

typedef struct TFIndirectDispatchArguments
{
    uint32_t mGroupCountX;
    uint32_t mGroupCountY;
    uint32_t mGroupCountZ;
} TFIndirectDispatchArguments;

#define TF_INDIRECT_DRAW_ELEM_INDEX(m)       (offsetof(TFIndirectDrawArguments, m) / sizeof(uint32_t))
#define TF_INDIRECT_DRAW_INDEX_ELEM_INDEX(m) (offsetof(TFIndirectDrawIndexArguments, m) / sizeof(uint32_t))
#define TF_INDIRECT_DISPATCH_ELEM_INDEX(m)   (offsetof(TFIndirectDispatchArguments, m) / sizeof(uint32_t))

typedef enum TFIndirectArgumentType
{
    TF_INDIRECT_DRAW,
    TF_INDIRECT_DRAW_INDEX,
    TF_INDIRECT_DISPATCH,
    TF_INDIRECT_COMMAND_BUFFER,         // metal ICB
    TF_INDIRECT_COMMAND_BUFFER_RESET,   // metal ICB reset
    TF_INDIRECT_COMMAND_BUFFER_OPTIMIZE // metal ICB optimization
} TFIndirectArgumentType;
/************************************************/

typedef enum TFDescriptorType
{
    TF_DESCRIPTOR_TYPE_UNDEFINED = 0,
    TF_DESCRIPTOR_TYPE_SAMPLER = (1 << 0),

    // SRV Read only texture
    TF_DESCRIPTOR_TYPE_TEXTURE = (1 << 1),

    // UAV Texture
    TF_DESCRIPTOR_TYPE_RW_TEXTURE = (1 << 2),

    // SRV Read only buffer
    TF_DESCRIPTOR_TYPE_BUFFER = (1 << 3),
    TF_DESCRIPTOR_TYPE_BUFFER_RAW = (1 << 4) | TF_DESCRIPTOR_TYPE_BUFFER,

    // UAV Buffer
    TF_DESCRIPTOR_TYPE_RW_BUFFER = (1 << 5),
    TF_DESCRIPTOR_TYPE_RW_BUFFER_RAW = (1 << 6) | TF_DESCRIPTOR_TYPE_RW_BUFFER,

    // Uniform buffer
    TF_DESCRIPTOR_TYPE_UNIFORM_BUFFER = (1 << 7),

    // Push constant / Root constant
    TF_DESCRIPTOR_TYPE_ROOT_CONSTANT = (1 << 8),

    // IA
    TF_DESCRIPTOR_TYPE_VERTEX_BUFFER = (1 << 9),
    TF_DESCRIPTOR_TYPE_INDEX_BUFFER = (1 << 10),
    TF_DESCRIPTOR_TYPE_INDIRECT_BUFFER = (1 << 11),

    // Cubemap SRV
    TF_DESCRIPTOR_TYPE_TEXTURE_CUBE = (1 << 12) | TF_DESCRIPTOR_TYPE_TEXTURE,

    // RTV / DSV per mip slice
    TF_DESCRIPTOR_TYPE_INDIRECT_COMMAND_BUFFER = (1 << 13),

    // Raytracing acceleration structure
    TF_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE = (1 << 14),

    // Mask for UAV resources
    TF_DESCRIPTOR_TYPE_RW_MASK = TF_DESCRIPTOR_TYPE_RW_TEXTURE | TF_DESCRIPTOR_TYPE_RW_BUFFER,

#if defined(VULKAN)
    /// Subpass input (descriptor type only available in Vulkan)
    TF_DESCRIPTOR_TYPE_INPUT_ATTACHMENT = (1 << 15),
    TF_DESCRIPTOR_TYPE_TEXEL_BUFFER = (1 << 16),
    TF_DESCRIPTOR_TYPE_RW_TEXEL_BUFFER = (1 << 17),
    TF_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER = (1 << 18),
#endif
} TFDescriptorType;
MAKE_ENUM_FLAG(uint32_t, TFDescriptorType)

typedef enum TFTextureDimension
{
    TF_TEXTURE_DIM_1D,
    TF_TEXTURE_DIM_2D,
    TF_TEXTURE_DIM_2DMS,
    TF_TEXTURE_DIM_3D,
    TF_TEXTURE_DIM_CUBE,
    TF_TEXTURE_DIM_1D_ARRAY,
    TF_TEXTURE_DIM_2D_ARRAY,
    TF_TEXTURE_DIM_2DMS_ARRAY,
    TF_TEXTURE_DIM_CUBE_ARRAY,
    TF_TEXTURE_DIM_COUNT,
    TF_TEXTURE_DIM_UNDEFINED,
} TFTextureDimension;

typedef enum TFPrimitiveTopology
{
    TF_PRIMITIVE_TOPO_POINT_LIST = 0,
    TF_PRIMITIVE_TOPO_LINE_LIST,
    TF_PRIMITIVE_TOPO_LINE_STRIP,
    TF_PRIMITIVE_TOPO_TRI_LIST,
    TF_PRIMITIVE_TOPO_TRI_STRIP,
    TF_PRIMITIVE_TOPO_PATCH_LIST,
    TF_PRIMITIVE_TOPO_COUNT,
} TFPrimitiveTopology;

typedef enum TFIndexType
{
    TF_INDEX_TYPE_UINT32 = 0,
    TF_INDEX_TYPE_UINT16,
} TFIndexType;

typedef enum TFShaderSemantic
{
    TF_SEMANTIC_UNDEFINED = 0,
    TF_SEMANTIC_POSITION,
    TF_SEMANTIC_NORMAL,
    TF_SEMANTIC_COLOR,
    TF_SEMANTIC_TANGENT,
    TF_SEMANTIC_BITANGENT,
    TF_SEMANTIC_JOINTS,
    TF_SEMANTIC_WEIGHTS,
    TF_SEMANTIC_CUSTOM,
    TF_SEMANTIC_TEXCOORD0,
    TF_SEMANTIC_TEXCOORD1,
    TF_SEMANTIC_TEXCOORD2,
    TF_SEMANTIC_TEXCOORD3,
    TF_SEMANTIC_TEXCOORD4,
    TF_SEMANTIC_TEXCOORD5,
    TF_SEMANTIC_TEXCOORD6,
    TF_SEMANTIC_TEXCOORD7,
    TF_SEMANTIC_TEXCOORD8,
    TF_SEMANTIC_TEXCOORD9,
    TF_MAX_SEMANTICS
} TFShaderSemantic;

typedef enum TFBlendConstant
{
    TF_BC_ZERO = 0,
    TF_BC_ONE,
    TF_BC_SRC_COLOR,
    TF_BC_ONE_MINUS_SRC_COLOR,
    TF_BC_DST_COLOR,
    TF_BC_ONE_MINUS_DST_COLOR,
    TF_BC_SRC_ALPHA,
    TF_BC_ONE_MINUS_SRC_ALPHA,
    TF_BC_DST_ALPHA,
    TF_BC_ONE_MINUS_DST_ALPHA,
    TF_BC_SRC_ALPHA_SATURATE,
    TF_BC_BLEND_FACTOR,
    TF_BC_ONE_MINUS_BLEND_FACTOR,
    TF_MAX_BLEND_CONSTANTS
} TFBlendConstant;

typedef enum TFBlendMode
{
    TF_BM_ADD,
    TF_BM_SUBTRACT,
    TF_BM_REVERSE_SUBTRACT,
    TF_BM_MIN,
    TF_BM_MAX,
    TF_MAX_BLEND_MODES,
} TFBlendMode;

typedef enum TFCompareMode
{
    TF_CMP_NEVER,
    TF_CMP_LESS,
    TF_CMP_EQUAL,
    TF_CMP_LEQUAL,
    TF_CMP_GREATER,
    TF_CMP_NOTEQUAL,
    TF_CMP_GEQUAL,
    TF_CMP_ALWAYS,
    TF_MAX_COMPARE_MODES,
} TFCompareMode;

typedef enum TFStencilOp
{
    TF_STENCIL_OP_KEEP,
    TF_STENCIL_OP_SET_ZERO,
    TF_STENCIL_OP_REPLACE,
    TF_STENCIL_OP_INVERT,
    TF_STENCIL_OP_INCR,
    TF_STENCIL_OP_DECR,
    TF_STENCIL_OP_INCR_SAT,
    TF_STENCIL_OP_DECR_SAT,
    TF_MAX_STENCIL_OPS,
} TFStencilOp;

typedef enum TFColorMask
{
    TF_COLOR_MASK_NONE = 0x0,
    TF_COLOR_MASK_RED = 0x1,
    TF_COLOR_MASK_GREEN = 0x2,
    TF_COLOR_MASK_BLUE = 0x4,
    TF_COLOR_MASK_ALPHA = 0x8,
    TF_COLOR_MASK_ALL = (TF_COLOR_MASK_RED | TF_COLOR_MASK_GREEN | TF_COLOR_MASK_BLUE | TF_COLOR_MASK_ALPHA),
} TFColorMask;
MAKE_ENUM_FLAG(uint8_t, TFColorMask)

// Blend states are always attached to one of the eight or more render targets that
// are in a MRT
// Mask constants
typedef enum TFBlendStateTargets
{
    TF_BLEND_STATE_TARGET_0 = 0x1,
    TF_BLEND_STATE_TARGET_1 = 0x2,
    TF_BLEND_STATE_TARGET_2 = 0x4,
    TF_BLEND_STATE_TARGET_3 = 0x8,
    TF_BLEND_STATE_TARGET_4 = 0x10,
    TF_BLEND_STATE_TARGET_5 = 0x20,
    TF_BLEND_STATE_TARGET_6 = 0x40,
    TF_BLEND_STATE_TARGET_7 = 0x80,
    TF_BLEND_STATE_TARGET_ALL = 0xFF,
} TFBlendStateTargets;
MAKE_ENUM_FLAG(uint32_t, TFBlendStateTargets)

typedef enum TFCullMode
{
    TF_CULL_MODE_NONE = 0,
    TF_CULL_MODE_BACK,
    TF_CULL_MODE_FRONT,
    TF_CULL_MODE_BOTH,
    TF_MAX_CULL_MODES
} TFCullMode;

typedef enum TFFrontFace
{
    TF_FRONT_FACE_CCW = 0,
    TF_FRONT_FACE_CW
} TFFrontFace;

typedef enum TFFillMode
{
    TF_FILL_MODE_SOLID,
    TF_FILL_MODE_WIREFRAME,
    TF_MAX_FILL_MODES
} TFFillMode;

typedef enum TFPipelineType
{
    TF_PIPELINE_TYPE_UNDEFINED = 0,
    TF_PIPELINE_TYPE_COMPUTE,
    TF_PIPELINE_TYPE_GRAPHICS,
#if defined(TF_ENABLE_WORKGRAPH)
    TF_PIPELINE_TYPE_WORKGRAPH,
#endif
    TF_PIPELINE_TYPE_COUNT,
} TFPipelineType;

typedef enum TFFilterType
{
    TF_FILTER_NEAREST = 0,
    TF_FILTER_LINEAR,
} TFFilterType;

typedef enum TFAddressMode
{
    TF_ADDRESS_MODE_MIRROR,
    TF_ADDRESS_MODE_REPEAT,
    TF_ADDRESS_MODE_CLAMP_TO_EDGE,
    TF_ADDRESS_MODE_CLAMP_TO_BORDER
} TFAddressMode;

typedef enum TFMipMapMode
{
    TF_MIPMAP_MODE_NEAREST = 0,
    TF_MIPMAP_MODE_LINEAR
} TFMipMapMode;

typedef union TFClearValue
{
    struct
    {
        float r;
        float g;
        float b;
        float a;
    };
    struct
    {
        float    depth;
        uint32_t stencil;
    };
} TFClearValue;

typedef enum TFBufferCreationFlags
{
    /// Default flag (Buffer will use aliased memory, buffer will not be cpu accessible until mapBuffer is called)
    TF_BUFFER_CREATION_FLAG_NONE = 0x0,
    /// Buffer will allocate its own memory (COMMITTED resource)
    TF_BUFFER_CREATION_FLAG_OWN_MEMORY_BIT = 0x1,
    /// Buffer will be persistently mapped
    TF_BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT = 0x2,
    /// Use ESRAM to store this buffer
    TF_BUFFER_CREATION_FLAG_ESRAM = 0x4,
    /// Flag to specify not to allocate descriptors for the resource
    TF_BUFFER_CREATION_FLAG_NO_DESCRIPTOR_VIEW_CREATION = 0x8,

    TF_BUFFER_CREATION_FLAG_ACCELERATION_STRUCTURE_BUILD_INPUT = 0x10,
    TF_BUFFER_CREATION_FLAG_SHADER_DEVICE_ADDRESS = 0x20,
    TF_BUFFER_CREATION_FLAG_SHADER_BINDING_TABLE = 0x40,
    TF_BUFFER_CREATION_FLAG_MARKER = 0x80,
#ifdef VULKAN
    /* Memory Host Flags */
    TF_BUFFER_CREATION_FLAG_HOST_COHERENT = 0x100,
    TF_BUFFER_CREATION_FLAG_HOST_VISIBLE = 0x200,
#endif

} TFBufferCreationFlags;
MAKE_ENUM_FLAG(uint32_t, TFBufferCreationFlags)

typedef enum TFTextureCreationFlags
{
    /// Default flag (Texture will use default allocation strategy decided by the api specific allocator)
    TF_TEXTURE_CREATION_FLAG_NONE = 0,
    /// Texture will allocate its own memory (COMMITTED resource)
    TF_TEXTURE_CREATION_FLAG_OWN_MEMORY_BIT = 0x01,
    /// Texture will be allocated in memory which can be shared among multiple processes
    TF_TEXTURE_CREATION_FLAG_EXPORT_BIT = 0x02,
    /// Texture will be allocated in memory which can be shared among multiple gpus
    TF_TEXTURE_CREATION_FLAG_EXPORT_ADAPTER_BIT = 0x04,
    /// Texture will be imported from a handle created in another process
    TF_TEXTURE_CREATION_FLAG_IMPORT_BIT = 0x08,
    /// Use ESRAM to store this texture
    TF_TEXTURE_CREATION_FLAG_ESRAM = 0x10,
    /// Use on-tile memory to store this texture
    TF_TEXTURE_CREATION_FLAG_ON_TILE = 0x20,
    /// Prevent compression meta data from generating (XBox)
    TF_TEXTURE_CREATION_FLAG_NO_COMPRESSION = 0x40,
    /// Force 2D instead of automatically determining dimension based on width, height, depth
    TF_TEXTURE_CREATION_FLAG_FORCE_2D = 0x80,
    /// Force 3D instead of automatically determining dimension based on width, height, depth
    TF_TEXTURE_CREATION_FLAG_FORCE_3D = 0x100,
    /// Display target
    TF_TEXTURE_CREATION_FLAG_ALLOW_DISPLAY_TARGET = 0x200,
    /// Create an sRGB texture.
    TF_TEXTURE_CREATION_FLAG_SRGB = 0x400,
    /// Create a normal map texture
    TF_TEXTURE_CREATION_FLAG_NORMAL_MAP = 0x800,
    /// Fast clear
    TF_TEXTURE_CREATION_FLAG_FAST_CLEAR = 0x1000,
    /// Fragment mask
    TF_TEXTURE_CREATION_FLAG_FRAG_MASK = 0x2000,
    /// Doubles the amount of array layers of the texture when rendering VR. Also forces the texture to be a 2D Array texture.
    TF_TEXTURE_CREATION_FLAG_VR_MULTIVIEW = 0x4000,
    /// Binds the FFR fragment density if this texture is used as a render target.
    TF_TEXTURE_CREATION_FLAG_VR_FOVEATED_RENDERING = 0x8000,
#if defined(TF_USE_MSAA_RESOLVE_ATTACHMENTS)
    /// Creates resolve attachment for auto resolve (MSAA on tiled architecture - Resolve can be done on tile through render pass)
    TF_TEXTURE_CREATION_FLAG_CREATE_RESOLVE_ATTACHMENT = 0x10000,
#endif
    TF_TEXTURE_CREATION_FLAG_SAMPLE_LOCATIONS_COMPATIBLE = 0x20000,
    /// Force Array instead of automatically determining dimension based on ArraySize
    TF_TEXTURE_CREATION_FLAG_FORCE_ARRAY = 0x40000,
    /// Enable Delta Color Compression for render targets (Xbox One X, Series S|X)
    TF_TEXTURE_CREATION_FLAG_DCC = 0x80000,
    /// Limit compression of resource so that it can be sampled without full decompression (Xbox One X, Series S|X)
    TF_TEXTURE_CREATION_FLAG_TEXTURE_COMPATABILITY = 0x100000,
    /// Allow addTextureDescriptor to create SRVs with formats that differ from the texture's own format.
    TF_TEXTURE_CREATION_FLAG_MUTABLE_FORMAT = 0x200000
} TFTextureCreationFlags;
MAKE_ENUM_FLAG(uint32_t, TFTextureCreationFlags)

// Used for swapchain
typedef enum TFColorSpace
{
    TF_COLOR_SPACE_SDR_LINEAR = 0x0,
    TF_COLOR_SPACE_SDR_SRGB,
    TF_COLOR_SPACE_P2020,         // BT2020 color space with PQ EOTF
    TF_COLOR_SPACE_EXTENDED_SRGB, // Extended sRGB with linear EOTF
} TFColorSpace;

// Material Unit test use this enum to index a shader table
COMPILE_ASSERT(TF_GPU_PRESET_COUNT == 7);

typedef struct TFBufferBarrier
{
    TFBuffer*       pBuffer;
    TFResourceState mCurrentState;
    TFResourceState mNewState;
    uint8_t         mBeginOnly : 1;
    uint8_t         mEndOnly : 1;
} TFBufferBarrier;

typedef struct TFTextureBarrier
{
    TFTexture*      pTexture;
    TFResourceState mCurrentState;
    TFResourceState mNewState;
    uint8_t         mBeginOnly : 1;
    uint8_t         mEndOnly : 1;
    uint8_t         mAcquire : 1;
    uint8_t         mRelease : 1;
    uint8_t         mQueueType : 5;
    /// Specifiy whether following barrier targets particular subresource
    uint8_t         mSubresourceBarrier : 1;
    /// Following values are ignored if mSubresourceBarrier is false
    uint8_t         mMipLevel : 7;
    uint16_t        mArrayLayer;
} TFTextureBarrier;

typedef struct TFRenderTargetBarrier
{
    TFRenderTarget* pRenderTarget;
    TFResourceState mCurrentState;
    TFResourceState mNewState;
    uint8_t         mBeginOnly : 1;
    uint8_t         mEndOnly : 1;
    uint8_t         mAcquire : 1;
    uint8_t         mRelease : 1;
    uint8_t         mQueueType : 5;
    /// Specifiy whether following barrier targets particular subresource
    uint8_t         mSubresourceBarrier : 1;
    /// Following values are ignored if mSubresourceBarrier is false
    uint8_t         mMipLevel : 7;
    uint16_t        mArrayLayer;
} TFRenderTargetBarrier;

typedef struct TFReadRange
{
    uint64_t mOffset;
    uint64_t mSize;
} TFReadRange;

typedef enum TFQueryType
{
    TF_QUERY_TYPE_TIMESTAMP = 0,
    TF_QUERY_TYPE_OCCLUSION,
    TF_QUERY_TYPE_PIPELINE_STATISTICS,
    TF_QUERY_TYPE_COUNT,
} TFQueryType;

typedef struct TFQueryPoolDesc
{
    const char* pName;
    TFQueryType mType;
    uint32_t    mQueryCount;
    uint32_t    mNodeIndex;
} TFQueryPoolDesc;

typedef struct TFQueryDesc
{
    uint32_t mIndex;
} TFQueryDesc;

typedef struct TFQueryPool
{
#if defined(DIRECT3D12)
    struct
    {
        ID3D12QueryHeap* pQueryHeap;
        TFBuffer*        pReadbackBuffer;
        D3D12_QUERY_TYPE mType;
    } mDx;
#endif
#if defined(VULKAN)
    struct
    {
        VkQueryPool pQueryPool;
        VkQueryType mType;
        uint32_t    mNodeIndex;
    } mVk;
#endif
#if defined(METAL)
    struct
    {
        // Length: 'mCount'
        // Not dynamic. It is of length n, given when a queryPool is added, look at: mtl_addQueryPool()
        // Take a look into QuerySampleRange in MetalRenderer.mm..
        void*                      pQueries;
        // Sampling done only at encoder level..
        id<MTLCounterSampleBuffer> pSampleBuffer;
        // Offset from the start of their relative origin
        uint32_t                   mRenderSamplesOffset;  // Origin: 0.
        uint32_t                   mComputeSamplesOffset; // Origin: RenderSampleCount * mCount.
        uint32_t                   mType;
    };
#endif
#if defined(ORBIS)
    struct
    {
        OrbisQueryPool mStruct;
        uint32_t       mType;
    };
#endif
#if defined(PROSPERO)
    struct
    {
        ProsperoQueryPool mStruct;
        uint32_t          mType;
    };
#endif
    uint32_t mCount;
    uint32_t mStride;
} TFQueryPool;

typedef struct TFPipelineStatisticsQueryData
{
    uint64_t mIAVertices;
    uint64_t mIAPrimitives;
    uint64_t mVSInvocations;
    uint64_t mGSInvocations;
    uint64_t mGSPrimitives;
    uint64_t mCInvocations;
    uint64_t mCPrimitives;
    uint64_t mPSInvocations;
    uint64_t mHSInvocations;
    uint64_t mDSInvocations;
    uint64_t mCSInvocations;
} TFPipelineStatisticsQueryData;

typedef struct TFQueryData
{
    union
    {
        struct
        {
            TFPipelineStatisticsQueryData mPipelineStats;
        };
        struct
        {
            uint64_t mBeginTimestamp;
            uint64_t mEndTimestamp;
        };
        uint64_t mOcclusionCounts;
    };
    bool mValid;
} TFQueryData;

#if defined(VULKAN)
typedef enum TFSamplerRange
{
    TF_SAMPLER_RANGE_FULL = 0,
    TF_SAMPLER_RANGE_NARROW = 1,
} TFSamplerRange;

typedef enum TFSamplerModelConversion
{
    TF_SAMPLER_MODEL_CONVERSION_RGB_IDENTITY = 0,
    TF_SAMPLER_MODEL_CONVERSION_YCBCR_IDENTITY = 1,
    TF_SAMPLER_MODEL_CONVERSION_YCBCR_709 = 2,
    TF_SAMPLER_MODEL_CONVERSION_YCBCR_601 = 3,
    TF_SAMPLER_MODEL_CONVERSION_YCBCR_2020 = 4,
} TFSamplerModelConversion;

typedef enum TFSampleLocation
{
    TF_SAMPLE_LOCATION_COSITED = 0,
    TF_SAMPLE_LOCATION_MIDPOINT = 1,
} TFSampleLocation;
#endif

typedef enum TFResourceHeapCreationFlags
{
    TF_RESOURCE_HEAP_FLAG_NONE = 0,
    TF_RESOURCE_HEAP_FLAG_SHARED = 0x1,
    TF_RESOURCE_HEAP_FLAG_DENY_BUFFERS = 0x2,
    TF_RESOURCE_HEAP_FLAG_ALLOW_DISPLAY = 0x4,
    TF_RESOURCE_HEAP_FLAG_SHARED_CROSS_ADAPTER = 0x8,
    TF_RESOURCE_HEAP_FLAG_DENY_RT_DS_TEXTURES = 0x10,
    TF_RESOURCE_HEAP_FLAG_DENY_NON_RT_DS_TEXTURES = 0x20,
    TF_RESOURCE_HEAP_FLAG_HARDWARE_PROTECTED = 0x40,
    TF_RESOURCE_HEAP_FLAG_ALLOW_WRITE_WATCH = 0x80,
    TF_RESOURCE_HEAP_FLAG_ALLOW_SHADER_ATOMICS = 0x100,

    // These are convenience aliases to manage resource heap tier restrictions. They cannot be bitwise OR'ed together cleanly.
    TF_RESOURCE_HEAP_FLAG_ALLOW_ALL_BUFFERS_AND_TEXTURES = 0x200,
    TF_RESOURCE_HEAP_FLAG_ALLOW_ONLY_BUFFERS = TF_RESOURCE_HEAP_FLAG_DENY_NON_RT_DS_TEXTURES | TF_RESOURCE_HEAP_FLAG_DENY_RT_DS_TEXTURES,
    TF_RESOURCE_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES = TF_RESOURCE_HEAP_FLAG_DENY_BUFFERS | TF_RESOURCE_HEAP_FLAG_DENY_RT_DS_TEXTURES,
    TF_RESOURCE_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES = TF_RESOURCE_HEAP_FLAG_DENY_BUFFERS | TF_RESOURCE_HEAP_FLAG_DENY_NON_RT_DS_TEXTURES,
} TFResourceHeapCreationFlags;

typedef struct TFResourceHeapDesc
{
    uint64_t mSize;
    uint64_t mAlignment;

    TFResourceMemoryUsage       mMemoryUsage;
    TFDescriptorType            mDescriptors;
    TFResourceHeapCreationFlags mFlags;

    uint32_t    mNodeIndex;
    uint32_t    mSharedNodeIndexCount;
    uint32_t*   pSharedNodeIndices;
    const char* pName;
} TFResourceHeapDesc;

#define TF_ALIGN_ResourceHeap 64
typedef struct DEFINE_ALIGNED(TFResourceHeap, TF_ALIGN_ResourceHeap)
{
#if defined(DIRECT3D12)
    struct
    {
        ID3D12Heap* pHeap;

#if defined(XBOX)
        D3D12_GPU_VIRTUAL_ADDRESS mPtr;
#endif
    } mDx;
#endif
#if defined(VULKAN)
    struct
    {
        struct VmaAllocation_T* pAllocation;
        VkDeviceMemory          pMemory;
        void*                   pCpuMappedAddress;
        uint64_t                mOffset;
    } mVk;
#endif
#if defined(METAL)
    struct
    {
        struct VmaAllocation_T* pAllocation;
        NOREFS id<MTLHeap> pHeap;
    };
#endif
#if defined(ORBIS)
    OrbisResourceHeap mStruct;
#endif
#if defined(PROSPERO)
    ProsperoResourceHeap mStruct;
#endif
    uint64_t mSize;
} TFResourceHeap;

typedef struct TFResourceSizeAlign
{
    uint64_t mSize;
    uint64_t mAlignment;
} TFResourceSizeAlign;

typedef struct TFResourcePlacement
{
    TFResourceHeap* pHeap;
    uint64_t        mOffset;

#if defined(ORBIS)
    OrbisResourcePlacement mStruct;
#endif
#if defined(PROSPERO)
    ProsperoResourcePlacement mStruct;
#endif
} TFResourcePlacement;

/// Data structure holding necessary info to create a Buffer
typedef struct TFBufferDesc
{
    /// Optional placement (addBuffer will place/bind buffer in this memory instead of allocating space)
    TFResourcePlacement*   pPlacement;
    /// Size of the buffer (in bytes)
    uint64_t               mSize;
    /// Set this to specify a counter buffer for this buffer (applicable to BUFFER_USAGE_STORAGE_SRV, BUFFER_USAGE_STORAGE_UAV)
    struct TFBuffer*       pCounterBuffer;
    /// Index of the first element accessible by the SRV/UAV (applicable to BUFFER_USAGE_STORAGE_SRV, BUFFER_USAGE_STORAGE_UAV)
    uint32_t               mFirstElement;
    /// Number of elements in the buffer (applicable to BUFFER_USAGE_STORAGE_SRV, BUFFER_USAGE_STORAGE_UAV)
    uint32_t               mElementCount;
    /// Size of each element (in bytes) in the buffer (applicable to BUFFER_USAGE_STORAGE_SRV, BUFFER_USAGE_STORAGE_UAV)
    uint32_t               mStructStride;
    /// Alignment
    uint32_t               mAlignment;
    /// Debug name used in gpu profile
    const char*            pName;
    uint32_t*              pSharedNodeIndices;
    /// Decides which memory heap buffer will use (default, upload, readback)
    TFResourceMemoryUsage  mMemoryUsage;
    /// Creation flags of the buffer
    TFBufferCreationFlags  mFlags;
    /// What type of queue the buffer is owned by
    TFQueueType            mQueueType;
    /// What state will the buffer get created in
    TFResourceState        mStartState;
    /// ICB draw type
    TFIndirectArgumentType mICBDrawType;
    /// ICB max commands in indirect command buffer
    uint32_t               mICBMaxCommandCount;
    /// Format of the buffer (applicable to typed storage buffers (Buffer<T>)
    TinyImageFormat        mFormat;
    /// Flags specifying the suitable usage of this buffer (Uniform buffer, Vertex Buffer, Index Buffer,...)
    TFDescriptorType       mDescriptors;
    /// The index of the GPU in SLI/Cross-Fire that owns this buffer, or the TFRenderer index in unlinked mode.
    uint32_t               mNodeIndex;
    uint32_t               mSharedNodeIndexCount;
} TFBufferDesc;

#define TF_ALIGN_Buffer 64
typedef struct DEFINE_ALIGNED(TFBuffer, TF_ALIGN_Buffer)
{
    /// CPU address of the mapped buffer (applicable to buffers created in CPU accessible heaps (CPU, CPU_TO_GPU, GPU_TO_CPU)
    void* pCpuMappedAddress;
#if defined(DIRECT3D12)
    struct
    {
        /// GPU Address - Cache to avoid calls to ID3D12Resource::GetGpuVirtualAddress
        D3D12_GPU_VIRTUAL_ADDRESS mGpuAddress;
        /// Descriptor handle of the CBV in a CPU visible descriptor heap (applicable to BUFFER_USAGE_UNIFORM)
        DxDescriptorID            mDescriptors;
        /// Offset from mDescriptors for srv descriptor handle
        uint8_t                   mSrvDescriptorOffset;
        /// Offset from mDescriptors for uav descriptor handle
        uint8_t                   mUavDescriptorOffset;
#if !defined(XBOX)
        uint8_t mMarkerBuffer : 1;
#endif
        /// Native handle of the underlying resource
        ID3D12Resource* pResource;
        union
        {
            ID3D12Heap*                pMarkerBufferHeap;
            /// Contains resource allocation info such as parent heap, offset in heap
            struct D3D12MAAllocation_* pAllocation;
        };
    } mDx;
#endif
#if defined(VULKAN)
    struct
    {
        /// Native handle of the underlying resource
        VkBuffer                pBuffer;
        /// Buffer view
        VkBufferView            pStorageTexelView;
        VkBufferView            pUniformTexelView;
        /// Contains resource allocation info such as parent heap, offset in heap
        struct VmaAllocation_T* pAllocation;
        uint64_t                mOffset;
    } mVk;
#endif
#if defined(METAL)
    struct
    {
        struct VmaAllocation_T*      pAllocation;
        id<MTLBuffer>                pBuffer;
        id<MTLIndirectCommandBuffer> pIndirectCommandBuffer;
        uint64_t                     mOffset;
    };
#endif
#if defined(ORBIS)
    OrbisBuffer mStruct;
#endif
#if defined(PROSPERO)
    ProsperoBuffer mStruct;
#endif
    uint64_t mSize : 32;
    uint64_t mDescriptors : 20;
    uint64_t mMemoryUsage : 3;
    uint64_t mNodeIndex : 4;
} TFBuffer;
// One cache line
COMPILE_ASSERT(sizeof(TFBuffer) == 8 * sizeof(uint64_t));

/// Data structure holding necessary info to create a TFTexture
typedef struct TFTextureDesc
{
    /// Optional placement (addTexture will place/bind buffer in this memory instead of allocating space)
    TFResourcePlacement* pPlacement;
    /// Optimized clear value (recommended to use this same value when clearing the rendertarget)
    TFClearValue         mClearValue;
    /// Pointer to native texture handle if the texture does not own underlying resource
    const void*          pNativeHandle;
    /// Debug name used in gpu profile
    const char*          pName;
    /// GPU indices to share this texture
    uint32_t*            pSharedNodeIndices;
#if defined(VULKAN)
    VkSamplerYcbcrConversionInfo* pSamplerYcbcrConversionInfo;
#endif
    /// Texture creation flags (decides memory allocation strategy, sharing access,...)
    TFTextureCreationFlags mFlags;
    /// Width
    uint32_t               mWidth;
    /// Height
    uint32_t               mHeight;
    /// Depth (Should be 1 if not a mType is not TEXTURE_TYPE_3D)
    uint32_t               mDepth;
    /// Texture array size (Should be 1 if texture is not a texture array or cubemap)
    uint32_t               mArraySize;
    /// Number of mip levels
    uint32_t               mMipLevels;
    /// Number of multisamples per pixel (currently Textures created with mUsage TEXTURE_USAGE_SAMPLED_IMAGE only support TF_SAMPLE_COUNT_1)
    TFSampleCount          mSampleCount;
    /// The image quality level. The higher the quality, the lower the performance. The valid range is between zero and the value
    /// appropriate for mSampleCount
    uint32_t               mSampleQuality;
    ///  image format
    TinyImageFormat        mFormat;
    /// What state will the texture get created in
    TFResourceState        mStartState;
    /// Optional list of formats this texture's memory may be reinterpreted as.
    /// Only consulted when TF_TEXTURE_CREATION_FLAG_MUTABLE_FORMAT is set. Each entry must be
    /// size-compatible with mFormat.
    const TinyImageFormat* pAliasFormats;
    /// Descriptor creation
    TFDescriptorType       mDescriptors;
    /// Number of GPUs to share this texture
    uint32_t               mSharedNodeIndexCount;
    /// GPU which will own this texture
    uint32_t               mNodeIndex;
    /// Number of entries in pAliasFormats (0 = no alias declarations).
    uint32_t               mAliasFormatCount;
} TFTextureDesc;

typedef struct TFTextureDescriptorDesc
{
    TFTexture*       pTexture;
    TinyImageFormat  mFormat;
    TFDescriptorType mDescriptors;
    uint32_t         mBaseMipLevel;
    uint32_t         mMipLevelCount; // 0 = all remaining mips
    uint32_t         mBaseArrayLayer;
    uint32_t         mArrayLayerCount; // 0 = all remaining layers
    uint32_t         mIsStencil : 1;   // stencil-plane SRV; reads stencil in red channel
} TFTextureDescriptorDesc;

#define TF_ALIGN_TextureDescriptor 16
typedef struct DEFINE_ALIGNED(TFTextureDescriptor, TF_ALIGN_TextureDescriptor)
{
#if defined(DIRECT3D12)
    struct
    {
        DxDescriptorID mSrv; // SRV; D3D12_DESCRIPTOR_ID_NONE if unused
        DxDescriptorID mUav; // UAV mip 0; D3D12_DESCRIPTOR_ID_NONE if unused
    } mDx;
#endif
#if defined(VULKAN)
    struct
    {
        VkImageView pSrvUav; // SRV + UAV (one view; cube UAV requires explicit addTextureDescriptor)
    } mVk;
#endif
#if defined(METAL)
    id<MTLTexture> pSrvUav; // SRV + UAV (same view; cube UAV requires explicit addTextureDescriptor)
#endif
#if defined(ORBIS)
    OrbisTexture mStruct;
#endif
#if defined(PROSPERO)
    ProsperoTexture mStruct;
#endif
    TinyImageFormat mFormat;
} TFTextureDescriptor;

#define TF_ALIGN_Texture 64
typedef struct DEFINE_ALIGNED(TFTexture, TF_ALIGN_Texture)
{
    uint32_t            mWidth : 16;
    uint32_t            mHeight : 16;
    uint32_t            mDepth : 16;
    uint32_t            mMipLevels : 5;
    uint32_t            mArraySizeMinusOne : 11;
    uint32_t            mFormat : 8;
    uint32_t            mNodeIndex : 4;
    uint32_t            mSampleCount : 5;
    /// This value will be false if the underlying resource is not owned by the texture (swapchain textures,...)
    uint32_t            mOwnsImage : 1;
    uint32_t            mLazilyAllocated : 1;
    TFTextureDescriptor mDesc; // default view (mip 0, full array); use addTextureDescriptor for subresource variants
#if defined(DIRECT3D12)
    struct
    {
        ID3D12Resource*            pResource;
        struct D3D12MAAllocation_* pAllocation;
    } mDx;
#endif
#if defined(VULKAN)
    struct
    {
        VkImage                 pImage;
        struct VmaAllocation_T* pAllocation;
    } mVk;
#endif
#if defined(METAL)
    struct
    {
        id<MTLTexture>          pTexture;
        struct VmaAllocation_T* pAllocation;
        id                      mpsTextureAllocator; // non-nil for MPSSVGFTextureAllocator outputs
    };
#endif
} TFTexture;
COMPILE_ASSERT(sizeof(TFTexture) <= 8 * sizeof(uint64_t));

typedef struct TFRenderTargetDesc
{
    /// Optional placement (addRenderTarget will place/bind buffer in this memory instead of allocating space)
    TFResourcePlacement*   pPlacement;
    /// Texture creation flags (decides memory allocation strategy, sharing access,...)
    TFTextureCreationFlags mFlags;
    /// Width
    uint32_t               mWidth;
    /// Height
    uint32_t               mHeight;
    /// Depth (Should be 1 if not a mType is not TEXTURE_TYPE_3D)
    uint32_t               mDepth;
    /// Texture array size (Should be 1 if texture is not a texture array or cubemap)
    uint32_t               mArraySize;
    /// Number of mip levels
    uint32_t               mMipLevels;
    /// MSAA
    TFSampleCount          mSampleCount;
    /// Internal image format
    TinyImageFormat        mFormat;
    /// What state will the texture get created in
    TFResourceState        mStartState;
    /// Optimized clear value (recommended to use this same value when clearing the rendertarget)
    TFClearValue           mClearValue;
    /// The image quality level. The higher the quality, the lower the performance. The valid range is between zero and the value
    /// appropriate for mSampleCount
    uint32_t               mSampleQuality;
    /// Descriptor creation
    TFDescriptorType       mDescriptors;
    const void*            pNativeHandle;
    /// Debug name used in gpu profile
    const char*            pName;
    /// GPU indices to share this texture
    uint32_t*              pSharedNodeIndices;
    /// Number of GPUs to share this texture
    uint32_t               mSharedNodeIndexCount;
    /// GPU which will own this texture
    uint32_t               mNodeIndex;
} TFRenderTargetDesc;

typedef struct TFRenderTargetDescriptorDesc
{
    TFRenderTarget* pRenderTarget;
    uint32_t        mMipSlice : 10;
    uint32_t        mArraySlice : 16;
    uint32_t        mUseMipSlice : 1;
    uint32_t        mUseArraySlice : 1;
} TFRenderTargetDescriptorDesc;

#define TF_ALIGN_RenderTargetDescriptor 16
typedef struct DEFINE_ALIGNED(TFRenderTargetDescriptor, TF_ALIGN_RenderTargetDescriptor)
{
#if defined(DIRECT3D12)
    struct
    {
        DxDescriptorID mDescriptorID;
    } mDx;
#endif
#if defined(VULKAN)
    struct
    {
        VkImageView mView;
        uint32_t    mId; // framebuffer cache key
    } mVk;
#endif
#if defined(METAL)
    id<MTLTexture> pTexture; // resolved MTLTexture for this descriptor (mip/slice pre-applied)
    uint32_t       mMipLevel;
    uint32_t       mSlice;
#endif
#if defined(ORBIS)
    OrbisRenderTarget mStruct;
#endif
#if defined(PROSPERO)
    ProsperoRenderTarget mStruct;
#endif
    TFRenderTarget* pRenderTarget;
    uint32_t        mMipSlice : 10;
    uint32_t        mArraySlice : 16;
    uint32_t        mUseArraySlice : 1;
    uint32_t        mUseMipSlice : 1;
    // padding : 4
} TFRenderTargetDescriptor;

#define TF_ALIGN_RenderTarget 64
typedef struct DEFINE_ALIGNED(TFRenderTarget, TF_ALIGN_RenderTarget)
{
    TFTexture* pTexture;
#if defined(TF_USE_MSAA_RESOLVE_ATTACHMENTS)
    TFRenderTarget* pResolveAttachment;
#endif
    TFClearValue             mClearValue;
    uint32_t                 mArraySize : 16;
    uint32_t                 mDepth : 16;
    uint32_t                 mWidth : 16;
    uint32_t                 mHeight : 16;
    uint32_t                 mDescriptors : 20;
    uint32_t                 mMipLevels : 10;
    uint32_t                 mSampleQuality : 5;
    uint32_t                 mPendingInitialLayout : 1;
    TinyImageFormat          mFormat;
    TFSampleCount            mSampleCount;
    bool                     mVRMultiview;
    bool                     mVRFoveatedRendering;
    TFRenderTargetDescriptor mDesc; // default view (mip 0, full array); use addRenderTargetDescriptor for subresource variants
} TFRenderTarget;
COMPILE_ASSERT(sizeof(TFRenderTarget) <= 32 * sizeof(uint64_t));

typedef struct TFSampleLocations
{
    int8_t mX;
    int8_t mY;
} TFSampleLocations;

typedef struct TFSamplerDesc
{
    TFFilterType  mMinFilter;
    TFFilterType  mMagFilter;
    TFMipMapMode  mMipMapMode;
    TFAddressMode mAddressU;
    TFAddressMode mAddressV;
    TFAddressMode mAddressW;
    float         mMipLodBias;
    bool          mSetLodRange;
    float         mMinLod;
    float         mMaxLod;
    float         mMaxAnisotropy;
    TFCompareMode mCompareFunc;

#if defined(VULKAN)
    struct
    {
        TinyImageFormat          mFormat;
        TFSamplerModelConversion mModel;
        TFSamplerRange           mRange;
        TFSampleLocation         mChromaOffsetX;
        TFSampleLocation         mChromaOffsetY;
        TFFilterType             mChromaFilter;
        bool                     mForceExplicitReconstruction;
    } mSamplerConversionDesc;
#endif
} TFSamplerDesc;

#define TF_ALIGN_Sampler 16
typedef struct DEFINE_ALIGNED(TFSampler, TF_ALIGN_Sampler)
{
#if defined(DIRECT3D12)
    struct
    {
        /// Description for creating the Sampler descriptor for this sampler
        D3D12_SAMPLER_DESC mDesc;
        /// Descriptor handle of the Sampler in a CPU visible descriptor heap
        DxDescriptorID     mDescriptor;
    } mDx;
#endif
#if defined(VULKAN)
    struct
    {
        /// Native handle of the underlying resource
        VkSampler                    pSampler;
        VkSamplerYcbcrConversion     pSamplerYcbcrConversion;
        VkSamplerYcbcrConversionInfo mSamplerYcbcrConversionInfo;
    } mVk;
#endif
#if defined(METAL)
    struct
    {
        /// Native handle of the underlying resource
        id<MTLSamplerState> pSamplerState;
    };
#endif
#if defined(ORBIS)
    OrbisSampler mStruct;
#endif
#if defined(PROSPERO)
    ProsperoSampler mStruct;
#endif
} TFSampler;
#if defined(DIRECT3D12)
COMPILE_ASSERT(sizeof(TFSampler) == 8 * sizeof(uint64_t));
#elif defined(VULKAN)
COMPILE_ASSERT(sizeof(TFSampler) <= 8 * sizeof(uint64_t));
#else
COMPILE_ASSERT(sizeof(TFSampler) == 2 * sizeof(uint64_t));
#endif

typedef struct TFDescriptorDataRange
{
    uint32_t mOffset;
    uint32_t mSize;
    // Specify different structured buffer stride (ignored for raw buffer - ByteAddressBuffer)
    uint32_t mStructStride;
} TFDescriptorDataRange;

typedef struct TFDescriptorData
{
    /// Number of array entries to update (array size of ppTextures/ppBuffers/...)
    uint32_t               mCount : 31;
    /// When set, ppTextureDescriptors is used instead of ppTextures for TEXTURE/RW_TEXTURE descriptors.
    uint32_t               mUseTextureDescriptors : 1;
    /// Dst offset into the array descriptor (useful for updating few entries in a large array)
    // Example: to update 6th entry in a bindless texture descriptor, mArrayOffset will be 6 and mCount will be 1)
    uint32_t               mArrayOffset : 20;
    // Index in TFDescriptorSetDesc::pDescriptors array
    uint32_t               mIndex : 12;
    // Range to bind (buffer offset, size)
    TFDescriptorDataRange* pRanges;
    struct
    {
        // Bind MTLIndirectCommandBuffer along with the MTLBuffer
        const char* pICBName;
        uint32_t    mICBIndex;
        bool        mBindICB;
    };
    /// Array of resources containing descriptor handles or constant to be used in ring buffer memory - DescriptorRange can hold only one
    /// resource type array
    union
    {
        /// Array of texture descriptors (srv and uav textures)
        TFTexture**               ppTextures;
        /// Array of sampler descriptors
        TFSampler**               ppSamplers;
        /// Array of buffer descriptors (srv, uav and cbv buffers)
        TFBuffer**                ppBuffers;
        /// Custom binding (raytracing acceleration structure ...)
        TFAccelerationStructure** ppAccelerationStructures;
        /// Array of pre-created texture subresource descriptors (used when mUseTextureDescriptors == 1)
        TFTextureDescriptor**     ppTextureDescriptors;
    };
} TFDescriptorData;

#define TF_ALIGN_DescriptorSet 64
typedef struct DEFINE_ALIGNED(TFDescriptorSet, TF_ALIGN_DescriptorSet)
{
#if defined(DIRECT3D12)
    struct
    {
        /// Start handle to cbv srv uav descriptor table
        DxDescriptorID             mCbvSrvUavHandle;
        /// Start handle to sampler descriptor table
        DxDescriptorID             mSamplerHandle;
        /// Stride of the cbv srv uav descriptor table (number of descriptors * descriptor size)
        uint32_t                   mCbvSrvUavStride;
        /// Stride of the sampler descriptor table (number of descriptors * descriptor size)
        uint32_t                   mSamplerStride;
        const struct TFDescriptor* pDescriptors;
        uint32_t                   mMaxSets : 16;
        uint32_t                   mNodeIndex : 4;
        uint32_t                   mCbvSrvUavRootIndex : 4;
        uint32_t                   mSamplerRootIndex : 4;
        uint32_t                   mPipelineType : 3;
    } mDx;
#endif
#if defined(VULKAN)
    struct
    {
        VkDescriptorSet*           pHandles;
        VkDescriptorPool           pDescriptorPool;
        const struct TFDescriptor* pDescriptors;
        uint32_t                   mMaxSets;
        uint32_t                   mSetIndex;
        uint32_t                   mNodeIndex;
        size_t                     mHash;
    } mVk;
#endif
#if defined(METAL)
    struct
    {
        id<MTLArgumentEncoder>         mArgumentEncoder;
        TFBuffer*                      mArgumentBuffer;
        struct UntrackedResourceData** ppUntrackedData;
        const struct TFDescriptor*     pDescriptors;
        /// Descriptors that are bound without argument buffers
        /// This is necessary to support Tier 1 argument buffers, which can't include writable textures
        struct RootDescriptorData*     pRootDescriptorData;
        uint32_t*                      pBindings;
        uint32_t*                      pResourceIndices;
        uint32_t                       mStride;
        uint32_t                       mMaxSets;
        uint32_t                       mRootBufferCount : 10;
        uint32_t                       mRootTextureCount : 10;
        uint32_t                       mRootSamplerCount : 10;
        uint8_t                        mNodeIndex;
        uint8_t                        mSetIndex;
        uint8_t                        mArgumentBufferIndex;
        uint8_t                        mStages;
        uint8_t                        mForceArgumentBuffer;
    };
#endif
#if defined(ORBIS)
    OrbisDescriptorSet mStruct;
#endif
#if defined(PROSPERO)
    ProsperoDescriptorSet mStruct;
#endif
} TFDescriptorSet;

typedef struct TFCmdPoolDesc
{
    TFQueue* pQueue;
    bool     mTransient;
} TFCmdPoolDesc;

typedef struct TFCmdPool
{
#if defined(DIRECT3D12)
    ID3D12CommandAllocator* pCmdAlloc;
#endif
#if defined(VULKAN)
    VkCommandPool pCmdPool;
#endif
    TFQueue* pQueue;
} TFCmdPool;

typedef struct TFCmdDesc
{
    TFCmdPool* pPool;
#if defined(ORBIS) || defined(PROSPERO)
    uint32_t mMaxSize;
#endif
    bool mSecondary;
#ifdef TF_ENABLE_GRAPHICS_DEBUG_ANNOTATION
    const char* pName;
#endif // TF_ENABLE_GRAPHICS_DEBUG_ANNOTATION
} TFCmdDesc;

typedef enum TFMarkerFlags
{
    /// Default flag
    TF_MARKER_FLAG_NONE = 0,
    TF_MARKER_FLAG_WAIT_FOR_WRITE = 0x1,
} TFMarkerFlags;
MAKE_ENUM_FLAG(uint8_t, TFMarkerFlags)

typedef struct TFMarkerDesc
{
    TFBuffer*     pBuffer;
    uint32_t      mOffset;
    uint32_t      mValue;
    TFMarkerFlags mFlags;
} TFMarkerDesc;

#if !defined(PROSPERO) && !defined(XBOX)
#define TF_GPU_MARKER_SIZE                        sizeof(uint32_t)
#define TF_GPU_MARKER_VALUE(markerBuffer, offset) (*((uint32_t*)markerBuffer->pCpuMappedAddress) + ((offset) / TF_GPU_MARKER_SIZE))
#endif

#if !defined(GFX_ESRAM_ALLOCATIONS)
#define TF_ESRAM_BEGIN_ALLOC(...)
#define TF_ESRAM_CURRENT_OFFSET(renderer, offset) \
    uint32_t offset = 0u;                         \
    UNREF_PARAM(offset);
#define TF_ESRAM_END_ALLOC(...)
#define TF_ESRAM_RESET_ALLOCS(...)
#endif

#define TF_ALIGN_Cmd 64
typedef struct DEFINE_ALIGNED(TFCmd, TF_ALIGN_Cmd)
{
#if defined(DIRECT3D12)
    struct
    {
#if defined(XBOX)
        DmaCmd mDma;
#endif
        ID3D12GraphicsCommandList1* pCmdList;
#if defined(TF_ENABLE_GRAPHICS_VALIDATION) && defined(_WINDOWS)
        // For resource state validation
        ID3D12DebugCommandList* pDebugCmdList;
#endif
        // Cached in beginCmd to avoid fetching them during rendering
        struct DescriptorHeap*      pBoundHeaps[2];
        D3D12_GPU_DESCRIPTOR_HANDLE mBoundHeapStartHandles[2];

        // Command buffer state
        D3D12_GPU_DESCRIPTOR_HANDLE mBoundDescriptorSets[TF_MAX_DESCRIPTOR_TABLES];
#if defined(XBOX)
        D3D12_SAMPLE_POSITION mSampleLocations[TF_MAX_SAMPLE_LOCATIONS];
#endif
        uint32_t mNodeIndex : 4;
        uint32_t mType : 3;
        uint32_t mPipelineType : 3;
        uint32_t mDescriptorTableDirtyMask: TF_MAX_DESCRIPTOR_TABLES;
#if defined(XBOX)
        // Required for setting occlusion query control
        uint32_t mSampleCount : 5;
        // Required for setting sample locations
        uint32_t mNumPixel : 3;
#endif
        TFCmdPool* pCmdPool;
    } mDx;
#endif
#if defined(VULKAN)
    struct
    {
        VkCommandBuffer   pCmdBuf;
        VkRenderPass      pActiveRenderPass;
        VkPipeline        pBoundPipeline;
        VkPipelineLayout  pBoundPipelineLayout;
        // VkSampleLocationEXT is 8 byte each. Choose to store TFSampleLocation instead.
        TFSampleLocations mSampleLocations[TF_MAX_SAMPLE_LOCATIONS];
        TFCmdPool*        pCmdPool;
        uint32_t          mNodeIndex : 4;
        uint32_t          mType : 3;
        uint32_t          mPipelineType : 3;
        uint32_t          mIsRendering : 1;
        // Required for vkSetSampleLocations
        uint32_t          mGridSizeX : 2;
        uint32_t          mGridSizeY : 2;
        uint32_t          mSampleCount : 5;
        uint32_t          mShouldRebindDescriptorSetsMask: TF_MAX_DESCRIPTOR_SETS;
        TFDescriptorSet*  mBoundDescriptorSets[TF_MAX_DESCRIPTOR_SETS];
        uint32_t          mBoundDescriptorSetIndices[TF_MAX_DESCRIPTOR_SETS];
        size_t            mBoundDescriptorSetHashes[TF_MAX_DESCRIPTOR_SETS];
    } mVk;
#endif
#if defined(METAL)
    struct
    {
#ifdef TRACY_ENABLE
        void* pTracy;
#endif
        id<MTLCommandBuffer>         pCommandBuffer;
        id<MTLRenderCommandEncoder>  pRenderEncoder;
        id<MTLComputeCommandEncoder> pComputeEncoder;
        id<MTLBlitCommandEncoder>    pBlitEncoder;
#if defined(MTL_RAYTRACING_AVAILABLE)
        id<MTLAccelerationStructureCommandEncoder> pASEncoder IOS18_API;
#endif

        // Stored in cmdBindPipeline. Used in
        // - cmdDraw functions to check for tessellation and patch control point count
        // - cmdDispatch functions to check for num threads per group (Metal needs to specify numThreadsPerThreadGroup explicitly)
        TFPipeline*  pBoundPipeline;
        TFQueryPool* pCurrentQueryPool;
        int32_t      mCurrentQueryIndex;
        // Stored in cmdBindIndexBuffer and used in cmdDrawIndexed functions (no bindIndexBuffer in Metal)
        NOREFS id<MTLBuffer> mBoundIndexBuffer;
        // Stored in cmdBindIndexBuffer and used in cmdDrawIndexed functions (no bindIndexBuffer in Metal)
        uint32_t             mBoundIndexBufferOffset;
        // Stored in cmdBindIndexBuffer and used in cmdDrawIndexed functions (no bindIndexBuffer in Metal)
        uint32_t             mIndexType : 2;
        // Stored in cmdBindIndexBuffer and used in cmdDrawIndexed functions (no bindIndexBuffer in Metal)
        uint32_t             mIndexStride : 3;
        // Stored in cmdBindPipeline and used in all draw functions (primitive type does not go in PSO but specified in the draw call)
        uint32_t             mSelectedPrimitiveType : 4;
        uint32_t             mPipelineType : 3;
        uint32_t             mShouldRebindPipeline : 1;
        uint32_t             mShouldRebindDescriptorSetsMask: TF_MAX_DESCRIPTOR_SETS;
        TFDescriptorSet*     mBoundDescriptorSets[TF_MAX_DESCRIPTOR_SETS];
        uint32_t             mBoundDescriptorSetIndices[TF_MAX_DESCRIPTOR_SETS];
#ifdef ENABLE_DRAW_INDEX_BASE_VERTEX_FALLBACK
        // When first vertex is not supported for indexed draw, we have to offset the
        // vertex buffer manually using setVertexBufferOffset
        // mOffsets, mStrides stored in cmdBindVertexBuffer and used in cmdDrawIndexed functions
        uint32_t mOffsets[TF_MAX_VERTEX_BINDINGS];
        uint32_t mStrides[TF_MAX_VERTEX_BINDINGS];
        uint32_t mFirstVertex;
#endif
#ifdef TF_ENABLE_GRAPHICS_DEBUG_ANNOTATION
        char mDebugMarker[TF_MAX_DEBUG_NAME_LENGTH];
#endif
    };
#endif
#if defined(ORBIS)
    OrbisCmd mStruct;
#endif
#if defined(PROSPERO)
    ProsperoCmd mStruct;
#endif
    TFRenderer* pRenderer;
    TFQueue*    pQueue;
} TFCmd;
COMPILE_ASSERT(sizeof(TFCmd) <= 64 * sizeof(uint64_t));

typedef enum TFFenceStatus
{
    TF_FENCE_STATUS_COMPLETE = 0,
    TF_FENCE_STATUS_INCOMPLETE,
    TF_FENCE_STATUS_NOTSUBMITTED,
} TFFenceStatus;

typedef struct TFFence
{
#if defined(DIRECT3D12)
    struct
    {
        ID3D12Fence* pFence;
        HANDLE       pWaitIdleFenceEvent;
        uint64_t     mFenceValue;
    } mDx;
#endif
#if defined(VULKAN)
    struct
    {
        VkFence pFence;
    } mVk;
#endif
#if defined(METAL)
    struct
    {
        dispatch_semaphore_t pSemaphore;
        uint32_t             mSubmitted : 1;
    };
#endif
#if defined(ORBIS)
    OrbisFence mStruct;
#endif
#if defined(PROSPERO)
    ProsperoFence mStruct;
#endif
} TFFence;

typedef struct TFSemaphore
{
#if defined(DIRECT3D12)
    // DirectX12 does not have a concept of semaphores
    // All synchronization is done using fences
    // Simlate semaphore signal and wait using DirectX12 fences

    // Semaphores used in DirectX12 only in queueSubmit
    // queueSubmit -> How the semaphores work in DirectX12

    // pp_wait_semaphores -> queue->Wait is manually called on each fence in this
    // array before calling ExecuteCommandLists to make the fence work like a wait semaphore

    // pp_signal_semaphores -> Manually call queue->Signal on each fence in this array after
    // calling ExecuteCommandLists and increment the underlying fence value

    // queuePresent does not use the wait semaphore since the swapchain Present function
    // already does the synchronization in this case
    struct
    {
        ID3D12Fence* pFence;
        HANDLE       pWaitIdleFenceEvent;
        uint64_t     mFenceValue;
    } mDx;
#endif
#if defined(VULKAN)
    struct
    {
        VkSemaphore         pSemaphore;
        struct TFSemaphore* pSwapchainSemaphore;
        uint32_t            mCurrentNodeIndex : 5;
        uint32_t            mSignaled : 1;
    } mVk;
#endif
#if defined(METAL)
    struct
    {
        id<MTLEvent> pSemaphore;
        uint64_t     mValue : 63;
        uint64_t     mSignaled : 1;
    };
#endif
#if defined(ORBIS)
    OrbisSemaphore mStruct;
#endif
#if defined(PROSPERO)
    ProsperoSemaphore mStruct;
#endif
} TFSemaphore;

typedef struct TFQueueDesc
{
    TFQueueType     mType;
    TFQueueFlag     mFlag;
    TFQueuePriority mPriority;
    uint32_t        mNodeIndex;
    const char*     pName;
} TFQueueDesc;

typedef struct TFQueue
{
#if defined(DIRECT3D12)
    struct
    {
        ID3D12CommandQueue* pQueue;
        TFFence*            pFence;
        TFRenderer*         pRenderer;
    } mDx;
#endif
#if defined(VULKAN)
    struct
    {
        VkQueue     pQueue;
        TFRenderer* pRenderer;
        TFMutex*    pSubmitMutex;
        float       mTimestampPeriod;
        uint32_t    mQueueFamilyIndex : 5;
        uint32_t    mQueueIndex : 5;
        uint32_t    mGpuMode : 3;
    } mVk;
#endif
#if defined(METAL)
    struct
    {
#ifdef TRACY_ENABLE
        void* pTracy;
#endif
        id<MTLCommandQueue> pCommandQueue;
        id<MTLFence>        pQueueFence;
        uint32_t            mBarrierFlags;
    };
#endif
#if defined(ORBIS)
    OrbisQueue mStruct;
#endif
#if defined(PROSPERO)
    ProsperoQueue mStruct;
#endif
    uint32_t mType : 3;
    uint32_t mNodeIndex : 4;
} TFQueue;

/// TFShaderConstant only supported by Vulkan and Metal APIs
typedef struct TFShaderConstant
{
    const void* pValue;
    uint32_t    mIndex;
    uint32_t    mSize;
} TFShaderConstant;

typedef struct TFBinaryShaderStageDesc
{
    const char* pName;
#if defined(PROSPERO)
    ProsperoBinaryShaderStageDesc mStruct;
#else
    /// Byte code array
    void* pByteCode;
    uint32_t mByteCodeSize;
    const char* pEntryPoint;
#if defined(METAL)
    uint32_t mNumThreadsPerGroup[3];
    uint32_t mOutputRenderTargetTypesMask;
#endif
#endif
} TFBinaryShaderStageDesc;

typedef struct TFBinaryShaderDesc
{
    TFShaderStage           mStages;
    /// Specify whether shader will own byte code memory
    uint32_t                mOwnByteCode : 1;
    TFBinaryShaderStageDesc mVert;
    TFBinaryShaderStageDesc mFrag;
    TFBinaryShaderStageDesc mGeom;
    TFBinaryShaderStageDesc mHull;
    TFBinaryShaderStageDesc mDomain;
    TFBinaryShaderStageDesc mComp;
    const TFShaderConstant* pConstants;
    uint32_t                mConstantCount;
#if defined(QUEST_VR) || defined(HOLOLENS2)
    bool mIsMultiviewVR : 1;
#endif
} TFBinaryShaderDesc;

typedef struct TFShader
{
    TFShaderStage mStages : 31;
    bool          mIsMultiviewVR : 1;
    uint32_t      mNumThreadsPerGroup[3];
    uint32_t      mOutputRenderTargetTypesMask;
#if defined(DIRECT3D12)
    struct
    {
        LPCWSTR*                 pEntryNames;
        struct IDxcBlobEncoding* pVSBlob;
        struct IDxcBlobEncoding* pHSBlob;
        struct IDxcBlobEncoding* pDSBlob;
        struct IDxcBlobEncoding* pGSBlob;
        struct IDxcBlobEncoding* pPSBlob;
        struct IDxcBlobEncoding* pCSBlob;
    } mDx;
#endif
#if defined(VULKAN)
    struct
    {
        union
        {
            struct
            {
                VkShaderModule pVS;
                VkShaderModule pDS;
                VkShaderModule pHS;
                VkShaderModule pGS;
                VkShaderModule pPS;
            };
            VkShaderModule pCS;
        };
        VkSpecializationInfo* pSpecializationInfo;
    } mVk;
#endif
#if defined(METAL)
    struct
    {
        id<MTLFunction> pVertexShader;
        id<MTLFunction> pFragmentShader;
        id<MTLFunction> pComputeShader;
        uint32_t        mTessellation : 1;
        uint32_t        mICB : 1;
    };
#endif
#if defined(ORBIS)
    OrbisShader mStruct;
#endif
#if defined(PROSPERO)
    ProsperoShader mStruct;
#endif

    uint32_t mNumControlPoints;

} TFShader;

typedef struct TFBlendStateDesc
{
    /// Source blend factor per render target.
    TFBlendConstant     mSrcFactors[TF_MAX_RENDER_TARGET_ATTACHMENTS];
    /// Destination blend factor per render target.
    TFBlendConstant     mDstFactors[TF_MAX_RENDER_TARGET_ATTACHMENTS];
    /// Source alpha blend factor per render target.
    TFBlendConstant     mSrcAlphaFactors[TF_MAX_RENDER_TARGET_ATTACHMENTS];
    /// Destination alpha blend factor per render target.
    TFBlendConstant     mDstAlphaFactors[TF_MAX_RENDER_TARGET_ATTACHMENTS];
    /// Blend mode per render target.
    TFBlendMode         mBlendModes[TF_MAX_RENDER_TARGET_ATTACHMENTS];
    /// Alpha blend mode per render target.
    TFBlendMode         mBlendAlphaModes[TF_MAX_RENDER_TARGET_ATTACHMENTS];
    /// Write mask per render target.
    TFColorMask         mColorWriteMasks[TF_MAX_RENDER_TARGET_ATTACHMENTS];
    /// Mask that identifies the render targets affected by the blend state.
    TFBlendStateTargets mRenderTargetMask;
    /// Set whether alpha to coverage should be enabled.
    bool                mAlphaToCoverage;
    /// Set whether each render target has an unique blend function. When false the blend function in slot 0 will be used for all render
    /// targets.
    bool                mIndependentBlend;
} TFBlendStateDesc;

typedef struct TFDepthStateDesc
{
    bool          mDepthTest;
    bool          mDepthWrite;
    TFCompareMode mDepthFunc;
    bool          mStencilTest;
    uint8_t       mStencilReadMask;
    uint8_t       mStencilWriteMask;
    TFCompareMode mStencilFrontFunc;
    TFStencilOp   mStencilFrontFail;
    TFStencilOp   mDepthFrontFail;
    TFStencilOp   mStencilFrontPass;
    TFCompareMode mStencilBackFunc;
    TFStencilOp   mStencilBackFail;
    TFStencilOp   mDepthBackFail;
    TFStencilOp   mStencilBackPass;
} TFDepthStateDesc;

typedef struct TFRasterizerStateDesc
{
    TFCullMode  mCullMode;
    int32_t     mDepthBias;
    float       mSlopeScaledDepthBias;
    TFFillMode  mFillMode;
    TFFrontFace mFrontFace;
    bool        mMultiSample;
    bool        mScissor;
    bool        mDepthClampEnable;
} TFRasterizerStateDesc;

typedef enum TFVertexBindingRate
{
    TF_VERTEX_BINDING_RATE_VERTEX = 0,
    TF_VERTEX_BINDING_RATE_INSTANCE = 1,
    TF_VERTEX_BINDING_RATE_COUNT,
} TFVertexBindingRate;

typedef struct TFVertexBinding
{
    uint32_t            mStride;
    TFVertexBindingRate mRate;
} TFVertexBinding;

typedef struct TFVertexAttrib
{
    TFShaderSemantic mSemantic;
    uint32_t         mSemanticNameLength;
    char             mSemanticName[TF_MAX_SEMANTIC_NAME_LENGTH];
    TinyImageFormat  mFormat;
    uint32_t         mBinding;
    uint32_t         mLocation;
    uint32_t         mOffset;
} TFVertexAttrib;

typedef struct TFVertexLayout
{
    TFVertexBinding mBindings[TF_MAX_VERTEX_BINDINGS];
    TFVertexAttrib  mAttribs[TF_MAX_VERTEX_ATTRIBS];
    uint32_t        mBindingCount;
    uint32_t        mAttribCount;
} TFVertexLayout;

#if defined(VULKAN)
typedef struct TFStaticSamplerDesc
{
    TFSamplerDesc mDesc;
    uint32_t      mBinding;
} TFStaticSamplerDesc;
#endif

#if defined(METAL)
// for metal : Buffers 0 and 1 are reserved for markers
// for metal : Buffers 2 to 5 are reserved for Argument buffers
#define TF_METAL_BUFFER_BIND_START_INDEX  6
#define TF_METAL_TEXTURE_BIND_START_INDEX 0
typedef struct TFMetalDescriptorSet
{
    void*                pUnused;
    uint32_t             mDescriptorCount;
    struct TFDescriptor* pDescriptors;
} TFMetalDescriptorSet;
#endif

typedef struct TFDescriptor
{
    IF_VALIDATE_DESCRIPTOR_MEMBER(const char*, pName)
    IF_VALIDATE_DESCRIPTOR_MEMBER(uint32_t, mSetIndex)
    TFDescriptorType mType;
    uint32_t         mCount;
    uint32_t         mOffset;
#if defined(VULKAN)
    TFTextureDimension mTextureDimension;
#endif
#if defined(METAL)
    uint32_t mUseArgumentBuffer;
#endif
} TFDescriptor;

typedef struct TFDescriptorSetDesc
{
    uint32_t            mIndex;
    uint32_t            mMaxSets;
    uint32_t            mNodeIndex;
    uint32_t            mDescriptorCount;
    const TFDescriptor* pDescriptors;
#if defined(METAL)
    uint32_t                    mSetIndex;
    uint32_t                    mForceArgumentBuffer;
    const TFMetalDescriptorSet* pSrtSets;
    uint32_t                    mSrtSetCount;
#endif
#if defined(VULKAN)
    const TFStaticSamplerDesc* pStaticSamplers;
    uint32_t                   mStaticSamplerCount;
#endif
} TFDescriptorSetDesc;

#if defined(VULKAN)
typedef struct TFDescriptorSetLayoutDesc
{
    const TFDescriptor*        pDescriptors;
    const TFStaticSamplerDesc* pStaticSamplers;
    uint32_t                   mDescriptorCount;
    uint32_t                   mStaticSamplerCount;
} TFDescriptorSetLayoutDesc;
#endif

typedef struct TFGraphicsPipelineDesc
{
    /// pShaderProgram is a combined graphics shader. Only if it is unset other separate shader pointers will be used.
    TFShader*              pShaderProgram;
    TFShader*              pVertexShader;
    TFShader*              pHullShader;
    TFShader*              pDomainShader;
    TFShader*              pGeometryShader;
    TFShader*              pFragmentShader;
    TFVertexLayout*        pVertexLayout;
    TFBlendStateDesc*      pBlendState;
    TFDepthStateDesc*      pDepthState;
    TFRasterizerStateDesc* pRasterizerState;
    TinyImageFormat*       pColorFormats;
#if defined(TF_USE_MSAA_RESOLVE_ATTACHMENTS)
    /// Used to specify resolve attachment for render pass
    TFStoreActionType* pColorResolveActions;
#endif
    uint32_t            mRenderTargetCount;
    TFSampleCount       mSampleCount;
    uint32_t            mSampleQuality;
    TinyImageFormat     mDepthStencilFormat;
    TFPrimitiveTopology mPrimitiveTopo;
    bool                mSupportIndirectCommandBuffer;
    bool                mVRFoveatedRendering;
    bool                mUseCustomSampleLocations;
} TFGraphicsPipelineDesc;

typedef struct TFComputePipelineDesc
{
    TFShader* pShaderProgram;
} TFComputePipelineDesc;

#if defined(TF_ENABLE_WORKGRAPH)
typedef struct TFWorkgraphPipelineDesc
{
    TFShader*   pShaderProgram;
    const char* pWorkgraphName;
} TFWorkgraphPipelineDesc;
#endif

typedef struct TFPipelineDesc
{
    union
    {
        TFComputePipelineDesc  mComputeDesc;
        TFGraphicsPipelineDesc mGraphicsDesc;
#if defined(TF_ENABLE_WORKGRAPH)
        TFWorkgraphPipelineDesc mWorkgraphDesc;
#endif
    };
    TFPipelineCache* pCache;
    void*            pPipelineExtensions;
    const char*      pName;
    TFPipelineType   mType;
    uint32_t         mExtensionCount;
#if defined(VULKAN)
    const TFDescriptorSetLayoutDesc** pLayouts;
    uint32_t                          mLayoutCount;
#endif
} TFPipelineDesc;

#define TF_ALIGN_Pipeline 64
typedef struct DEFINE_ALIGNED(TFPipeline, TF_ALIGN_Pipeline)
{
#if defined(DIRECT3D12)
    struct
    {
        union
        {
            ID3D12PipelineState* pPipelineState;
#if defined(TF_ENABLE_WORKGRAPH)
            struct
            {
                ID3D12StateObject* pStateObject;
                WCHAR*             pWorkgraphName;
            };
#endif
        };
        TFPipelineType         mType;
        D3D_PRIMITIVE_TOPOLOGY mPrimitiveTopology;
    } mDx;
#endif
#if defined(VULKAN)
    struct
    {
        VkPipeline               pPipeline;
        struct TFPipelineLayout* pPipelineLayout;
        TFPipelineType           mType;
#if defined(SHADER_STATS_AVAILABLE)
        TFShaderStage mShaderStages;
#endif
    } mVk;
#endif
#if defined(METAL)
    struct
    {
        id<MTLRenderPipelineState>  pRenderPipelineState;
        id<MTLComputePipelineState> pComputePipelineState;
        id<MTLDepthStencilState>    pDepthStencilState;
        union
        {
            // Graphics
            struct
            {
                uint32_t mCullMode : 3;
                uint32_t mFillMode : 3;
                uint32_t mWinding : 3;
                uint32_t mDepthClipMode : 1;
                uint32_t mPrimitiveType : 4;
                // Between 0-32
                uint32_t mPatchControlPointCount : 6;
                uint32_t mTessellation : 1;
                float    mDepthBias;
                float    mSlopeScale;
            };
            // Compute
            struct
            {
                MTLSize mNumThreadsPerGroup;
            };
        };
        TFPipelineType mType;
    };
#endif
#if defined(ORBIS)
    OrbisPipeline mStruct;
#endif
#if defined(PROSPERO)
    ProsperoPipeline mStruct;
#endif
} TFPipeline;
#if defined(ORBIS)
// Requires more cache lines due to no concept of an encapsulated pipeline state object
COMPILE_ASSERT(sizeof(TFPipeline) <= 64 * sizeof(uint64_t));
#elif defined(PROSPERO)
COMPILE_ASSERT(sizeof(TFPipeline) == 16 * sizeof(uint64_t));
#elif defined(TF_ENABLE_DEPENDENCY_TRACKER)
// Two cache lines
COMPILE_ASSERT(sizeof(TFPipeline) <= 16 * sizeof(uint64_t));
#else
// One cache line
COMPILE_ASSERT(sizeof(TFPipeline) == 8 * sizeof(uint64_t));
#endif

typedef enum TFPipelineCacheFlags
{
    TF_PIPELINE_CACHE_FLAG_NONE = 0x0,
    TF_PIPELINE_CACHE_FLAG_EXTERNALLY_SYNCHRONIZED = 0x1,
} TFPipelineCacheFlags;
MAKE_ENUM_FLAG(uint32_t, TFPipelineCacheFlags);

typedef struct TFPipelineCacheDesc
{
    /// Initial pipeline cache data (can be NULL which means empty pipeline cache)
    void*                pData;
    /// Initial pipeline cache size
    size_t               mSize;
    TFPipelineCacheFlags mFlags;
} TFPipelineCacheDesc;

typedef struct TFPipelineCache
{
#if defined(DIRECT3D12)
    struct
    {
        ID3D12PipelineLibrary* pLibrary;
        void*                  pData;
    } mDx;
#endif
#if defined(VULKAN)
    struct
    {
        VkPipelineCache pCache;
    } mVk;
#endif
} TFPipelineCache;

#if defined(SHADER_STATS_AVAILABLE)
typedef struct TFShaderStats
{
#if defined(VULKAN)
    struct
    {
        void*    pDisassemblyAMD;
        uint32_t mDisassemblySize;
    } mVk;
#endif
    uint32_t mUsedVgprs;
    uint32_t mUsedSgprs;
    uint32_t mLdsSizePerLocalWorkGroup;
    uint32_t mLdsUsageSizeInBytes;
    uint32_t mScratchMemUsageInBytes;
    uint32_t mPhysicalVgprs;
    uint32_t mPhysicalSgprs;
    uint32_t mAvailableVgprs;
    uint32_t mAvailableSgprs;
    uint32_t mComputeWorkGroupSize[3];
    bool     mValid;
} TFShaderStats;

typedef struct TFPipelineStats
{
    TFShaderStats mVert;
    TFShaderStats mHull;
    TFShaderStats mDomain;
    TFShaderStats mGeom;
    TFShaderStats mFrag;
    TFShaderStats mComp;
} TFPipelineStats;
#endif

#if defined(TF_ENABLE_WORKGRAPH)
typedef struct TFWorkgraphDesc
{
    TFPipeline* pPipeline;
} TFWorkgraphDesc;

typedef struct TFWorkgraph
{
    TFBuffer*   pBackingBuffer;
    TFPipeline* pPipeline;
#if defined(DIRECT3D12)
    D3D12_PROGRAM_IDENTIFIER mId;
#endif
} TFWorkgraph;

typedef enum TFDispatchGraphInputType
{
    TF_DISPATCH_GRAPH_INPUT_CPU = 0,
    TF_DISPATCH_GRAPH_INPUT_GPU,
    TF_DISPATCH_GRAPH_INPUT_COUNT,
} TFDispatchGraphInputType;

typedef struct TFDispatchGraphDesc
{
    TFWorkgraph* pWorkgraph;
    union
    {
        struct
        {
            void*    pInput;
            uint32_t mInputStride;
        };
        struct
        {
            TFBuffer* pInputBuffer;
            uint32_t  mInputBufferOffset;
        };
    };
    TFDispatchGraphInputType mInputType;
    bool                     mInitialize;
} TFDispatchGraphDesc;
#endif

typedef enum TFSwapChainCreationFlags
{
    TF_SWAP_CHAIN_CREATION_FLAG_NONE = 0x0,
    TF_SWAP_CHAIN_CREATION_FLAG_ENABLE_FOVEATED_RENDERING_VR = 0x1,
    TF_SWAP_CHAIN_CREATION_FLAG_ENABLE_2D_VR_LAYER = 0x2,
    TF_SWAP_CHAIN_CREATION_FLAG_ENABLE_VR_PASSTHROUGH = 0x4,
} TFSwapChainCreationFlags;
MAKE_ENUM_FLAG(uint32_t, TFSwapChainCreationFlags);

typedef enum TFVRFoveationLevel
{
    TF_FOVEATION_LEVEL_LOW = 0,
    TF_FOVEATION_LEVEL_MEDIUM = 1,
    TF_FOVEATION_LEVEL_HIGH = 2,
    TF_FOVEATION_LEVEL_DYNAMIC = 3
} TFVRFoveationLevel;
MAKE_ENUM_FLAG(uint32_t, TFVRFoveationLevel);

typedef enum TFVrPassthroughCreationFlags
{
    TF_PASSTHROUGH_FLAG_NONE = 0x0,
    // set if the projection layer should have alpha premultiplied or not
    TF_PASSTHROUGH_UNPREMULTIPLIED_ALPHA_BIT = 0x1,
} TFVrPassthroughCreationFlags;
MAKE_ENUM_FLAG(uint32_t, TFVrPassthroughCreationFlags);

typedef struct TFVR2DLayerDesc
{
    /// World-space position of the UI/2D layer
    struct
    {
        float x;
        float y;
        float z;
    } m2DLayerPosition;
    // Layer rotation. Default rotation with surface normal facing the +Z axis.
    struct
    {
        float x;
        float y;
        float z;
        float w;
    } m2DLayerRotQuat;
    /// Scale of the UI layer
    float m2DLayerScale;
} TFVR2DLayerDesc;

typedef struct TFSwapChainDesc
{
    /// Window handle
    TFWindowHandle           mWindowHandle;
    /// Queues which should be allowed to present
    TFQueue**                ppPresentQueues;
    /// Number of present queues
    uint32_t                 mPresentQueueCount;
    /// Number of backbuffers in this swapchain
    uint32_t                 mImageCount;
    /// Width of the swapchain
    uint32_t                 mWidth;
    /// Height of the swapchain
    uint32_t                 mHeight;
    /// Color format of the swapchain
    TinyImageFormat          mColorFormat;
    /// Clear value
    TFClearValue             mColorClearValue;
    /// Swapchain creation flags
    TFSwapChainCreationFlags mFlags;
    /// Set whether swap chain will be presented using vsync
    bool                     mEnableVsync;
    /// We can toggle to using FLIP model if app desires.
    bool                     mUseFlipSwapEffect;
    /// Optional colorspace for HDR
    TFColorSpace             mColorSpace;
    // Optional VR settings
    struct
    {
        TFVR2DLayerDesc    m2DLayer;
        TFVRFoveationLevel mFoveationLevel;
    } mVR;
} TFSwapChainDesc;

typedef struct TFSwapChain
{
    /// Render targets created from the swapchain back buffers
    TFRenderTarget** ppRenderTargets;
#if defined(DIRECT3D12)
    struct
    {
#if defined(XBOX)
        uint64_t mFramePipelineToken;
        /// Sync interval to specify how interval for vsync
        uint32_t mSyncInterval : 3;
        uint32_t mFlags : 10;
        uint32_t mIndex;
        void*    pWindow;
        TFQueue* pPresentQueue;
#else
        /// Use IDXGISwapChain3 for now since IDXGISwapChain4
        /// isn't supported by older devices.
        IDXGISwapChain3*                         pSwapChain;
        /// Sync interval to specify how interval for vsync
        uint32_t                                 mSyncInterval : 3;
        uint32_t                                 mFlags : 10;
#endif
    } mDx;
#endif
#if defined(VULKAN)
    struct
    {
        /// Present queue if one exists (queuePresent will use this queue if the hardware has a dedicated present queue)
        VkQueue          pPresentQueue;
        VkSwapchainKHR   pSwapChain;
        VkSurfaceKHR     pSurface;
        TFSwapChainDesc* pDesc;
        TFSemaphore**    ppSubmitPresentSemaphores;
        uint32_t         mPresentQueueFamilyIndex : 5;
    } mVk;
#endif
#if defined(METAL)
    struct
    {
#if defined(TARGET_IOS)
        UIView* pForgeView;
#else
        NSView*                                  pForgeView;
#endif
        id<CAMetalDrawable>  mMTKDrawable;
        id<MTLCommandBuffer> presentCommandBuffer;
        uint32_t             mIndex;
    };
#endif
#if defined(ORBIS)
    OrbisSwapChain mStruct;
#endif
#if defined(PROSPERO)
    ProsperoSwapChain mStruct;
#endif
#if defined(QUEST_VR) || defined(HOLOLENS2)
    struct
    {
        XrSwapchain                 pSwapchain;
        XrSwapchainImageBaseHeader* pSwapchainImages;
        struct
        {
            struct TFSwapChain*    pSwapchain;
            uint32_t               mCurrentSwapChainIndex;
            struct TFVR2DLayerDesc mDesc;
        } m2DLayer;

        struct
        {
            XrPassthroughFB              mPassthroughSystem;
            XrPassthroughLayerFB         mPassthroughLayer;
            TFVrPassthroughCreationFlags mPassthroughFlags;
        } mPassthrough;
        TFRenderTarget** ppFoveationFragmentDensityMaps;
    } mVR;
#endif // QUEST_VR HOLOLENS2
    uint32_t        mImageCount : 8;
    uint32_t        mEnableVsync : 1;
    TFColorSpace    mColorSpace : 4;
    TinyImageFormat mFormat : 8;
} TFSwapChain;

typedef enum TFShaderTarget
{
    // 5.1 is supported on all DX12 hardware
    TF_SHADER_TARGET_5_1,
    TF_SHADER_TARGET_6_0,
    TF_SHADER_TARGET_6_1,
    TF_SHADER_TARGET_6_2,
    TF_SHADER_TARGET_6_3, // required for Raytracing
    TF_SHADER_TARGET_6_4, // required for VRS
    TF_SHADER_TARGET_6_9,
} TFShaderTarget;

typedef enum TFGpuMode
{
    TF_GPU_MODE_SINGLE = 0,
    TF_GPU_MODE_LINKED,
    TF_GPU_MODE_UNLINKED,
} TFGpuMode;

typedef struct TFRendererDesc
{
#if defined(DIRECT3D12)
    struct
    {
        D3D_FEATURE_LEVEL mFeatureLevel;
    } mDx;
#endif
#if defined(VULKAN)
    struct
    {
        const char** ppInstanceLayers;
        const char** ppInstanceExtensions;
        const char** ppDeviceExtensions;
        uint32_t     mInstanceLayerCount;
        uint32_t     mInstanceExtensionCount;
        uint32_t     mDeviceExtensionCount;
        /// Flag to specify whether to request all queues from the gpu or just one of each type
        /// This will affect memory usage - Around 200 MB more used if all queues are requested
        bool         mRequestAllAvailableQueues;
    } mVk;
#endif
#if defined(ORBIS)
    OrbisExtendedDesc mExt;
#endif
#if defined(PROSPERO)
    ProsperoExtendedDesc mExt;
#endif

    TFShaderTarget mShaderTarget;
    TFGpuMode      mGpuMode;

    /// Apps may want to query additional state for their applications. That information is transferred through here.
    TFExtendedSettings* pExtendedSettings;

    /// Required when creating unlinked multiple renderers. Optional otherwise, can be used for explicit GPU selection.
    TFRendererContext* pContext;
    uint32_t           mGpuIndex;

    /// This results in new validation not possible during API calls on the CPU, by creating patched shaders that have validation added
    /// directly to the shader. However, it can slow things down a lot, especially for applications with numerous PSOs. Time to see the
    /// first render frame may take several minutes
    bool mEnableGpuBasedValidation;
#if defined(SHADER_STATS_AVAILABLE)
    bool mEnableShaderStats;
#endif

    // to align on PC on 40 bytes
    bool mPaddingA;
    bool mPaddingB;
    bool mPaddingC;
} TFRendererDesc;

#define TF_ALIGN_Renderer 64
typedef struct DEFINE_ALIGNED(TFRenderer, TF_ALIGN_Renderer)
{
#if defined(DIRECT3D12)
    struct
    {
        // API specific descriptor heap and memory allocator
        struct DescriptorHeap**   pCPUDescriptorHeaps;
        struct DescriptorHeap**   pCbvSrvUavHeaps;
        struct DescriptorHeap**   pSamplerHeaps;
        struct D3D12MAAllocator_* pResourceAllocator;
        // Filled by user - See initGraphicsRootSignature, initComputeRootSignature
        ID3D12RootSignature*      pGraphicsRootSignature;
        ID3D12RootSignature*      pComputeRootSignature;
#if defined(XBOX)
        ID3D12Device*   pDevice;
        TFEsramManager* pESRAMManager;
#elif defined(DIRECT3D12)
        ID3D12Device*                            pDevice;
#endif
#if defined(_WINDOWS) && defined(TF_ENABLE_GRAPHICS_VALIDATION)
        ID3D12InfoQueue1* pDebugValidation;
        DWORD             mCallbackCookie;
        bool              mUseDebugCallback;
        bool              mSuppressMismatchingCommandListDuringPresent;
#endif
    } mDx;
#endif
#if defined(VULKAN)
    struct
    {
        VkDevice               pDevice;
        uint32_t**             pAvailableQueueCount;
        uint32_t**             pUsedQueueCount;
        VkDescriptorPool       pEmptyDescriptorPool;
        VkDescriptorSetLayout  pEmptyDescriptorSetLayout;
        VkDescriptorSet        pEmptyDescriptorSet;
        TFDescriptorSet*       pEmptyStaticSamplerDescriptorSet;
        struct VmaAllocator_T* pVmaAllocator;
        union
        {
            uint8_t mGraphicsQueueFamilyIndex;
            uint8_t mTransferQueueFamilyIndex;
            uint8_t mComputeQueueFamilyIndex;
        };
        uint8_t mQueueFamilyIndices[3];
    } mVk;
#endif
#if defined(METAL)
    struct
    {
        id<MTLDevice>               pDevice;
        struct VmaAllocator_T*      pVmaAllocator;
        id<MTLComputePipelineState> pFillBufferPipeline;
        NOREFS id<MTLHeap>* pHeaps;
        uint32_t            mHeapCount;
        uint32_t            mHeapCapacity;
        // To synchronize resource allocation done through automatic heaps
        TFMutex*            pHeapMutex;
#if defined(ENABLE_MTL_RESIDENCY_SETS)
        id<MTLResidencySet> pMainResidencySet IOS18_API;
        TFMutex                               mResidencySetMutex;
        bool                                  mResidencySetDirty;
#endif
        double       mGpuToCpuTimestampFactor;
        MTLTimestamp mPrevCpuTimestamp;
        MTLTimestamp mPrevGpuTimestamp;
    };
#endif
#if defined(QUEST_VR)
    struct
    {
        bool            mIsFoveationEnabled;
        TFRenderTarget* pCurrentFDM;
    } mVR;
#endif

    struct NullDescriptors*   pNullDescriptors;
    struct TFRendererContext* pContext;
    const struct TFGpuDesc*   pGpu;
    const char*               pName;
    TFRendererApi             mRendererApi;
    uint32_t                  mLinkedNodeCount : 4;
    uint32_t                  mUnlinkedRendererIndex : 4;
    uint32_t                  mGpuMode : 3;
    uint32_t                  mShaderTarget : 4;
    uint32_t                  mOwnsContext : 1;
    uint32_t                  mDeviceLost : 1;
    bool                      mRenderDocQueriedAndLoaded : 1;

} TFRenderer;
// 4 cache lines
COMPILE_ASSERT(sizeof(TFRenderer) <= 32 * sizeof(uint64_t));

typedef struct TFRendererContextDesc
{
#if defined(DIRECT3D12)
    struct
    {
        D3D_FEATURE_LEVEL mFeatureLevel;
    } mDx;
#endif
#if defined(VULKAN)
    struct
    {
        const char** ppInstanceLayers;
        const char** ppInstanceExtensions;
        const char** ppDeviceExtensions;
        uint32_t     mInstanceLayerCount;
        uint32_t     mInstanceExtensionCount;
        uint32_t     mDeviceExtensionCount;
        /// Flag to specify whether to request all queues from the gpu or just one of each type
        /// This will affect memory usage - Around 200 MB more used if all queues are requested
        bool         mRequestAllAvailableQueues;
    } mVk;
#endif

    bool mEnableGpuBasedValidation;
#if defined(SHADER_STATS_AVAILABLE)
    bool mEnableShaderStats;
#endif
} TFRendererContextDesc;

typedef struct TFRendererContext
{
#if defined(DIRECT3D12)
    struct
    {
#if defined(XBOX)
        IDXGIFactory2* pDXGIFactory;
#elif defined(DIRECT3D12)
        IDXGIFactory6*                           pDXGIFactory;
        ID3D12Debug*                             pDebug;
#if defined(_WINDOWS) && defined(DRED)
        ID3D12DeviceRemovedExtendedDataSettings* pDredSettings;
#endif
#endif
    } mDx;
#endif
#if defined(VULKAN)
    struct
    {
        VkInstance               pInstance;
        VkDebugUtilsMessengerEXT pDebugUtilsMessenger;
        VkDebugReportCallbackEXT pDebugReport;
        uint32_t                 mDebugUtilsExtension : 1;
        uint32_t                 mDebugReportExtension : 1;
        uint32_t                 mDeviceGroupCreationExtension : 1;
    } mVk;
#endif
#if defined(METAL)
    struct
    {
        uint32_t mExtendedEncoderDebugReport : 1;
    } mMtl;
#endif
#if defined(QUEST_VR)
    struct
    {
        XrGraphicsBindingVulkanKHR mXRGraphicsBinding;
    } mVR;
#endif
    TFGpuDesc mGpus[TF_MAX_MULTIPLE_GPUS];
    uint32_t  mGpuCount;
} TFRendererContext;

typedef struct TFQueueSubmitDesc
{
    TFCmd**       ppCmds;
    TFFence*      pSignalFence;
    TFSemaphore** ppWaitSemaphores;
    TFSemaphore** ppSignalSemaphores;
    uint32_t      mCmdCount;
    uint32_t      mWaitSemaphoreCount;
    uint32_t      mSignalSemaphoreCount;
    bool          mSubmitDone;
} TFQueueSubmitDesc;

typedef struct TFQueuePresentDesc
{
    TFSwapChain*  pSwapChain;
    TFSemaphore** ppWaitSemaphores;
    uint32_t      mWaitSemaphoreCount;
    uint8_t       mIndex;
    bool          mSubmitDone;
} TFQueuePresentDesc;

// Uses render targets' sample count in bindRenderTargetsDesc
typedef struct TFSampleLocationDesc
{
    TFSampleLocations* pLocations;
    uint32_t           mGridSizeX;
    uint32_t           mGridSizeY;
} TFSampleLocationDesc;

typedef struct TFBindRenderTargetDesc
{
    TFRenderTarget*           pRenderTarget; // default: mip 0, full array (mUseRenderTargetDescriptor == 0)
    TFLoadActionType          mLoadAction;
    TFStoreActionType         mStoreAction;
    TFLoadActionType          mLoadActionStencil;  // depth-stencil only
    TFStoreActionType         mStoreActionStencil; // depth-stencil only
    TFClearValue              mClearValue;
    uint32_t                  mUseRenderTargetDescriptor : 1;
    uint32_t                  mOverrideClearValue : 1;
    TFRenderTargetDescriptor* pDescriptor; // explicit subresource (mUseRenderTargetDescriptor == 1)
} TFBindRenderTargetDesc;

typedef struct TFBindRenderTargetsDesc
{
    uint32_t               mRenderTargetCount;
    TFBindRenderTargetDesc mRenderTargets[TF_MAX_RENDER_TARGET_ATTACHMENTS];
    TFBindRenderTargetDesc mDepthStencil;
    TFSampleLocationDesc   mSampleLocation;
    // Explicit viewport for empty render pass
    uint32_t               mExtent[2];
} TFBindRenderTargetsDesc;

#if defined(TF_ENABLE_COOP_VECTORS)
typedef struct TFCoopMatrixDesc
{
    TFBuffer* mMatrixBuffer;
    uint64_t  mOffset;
    uint32_t  mLayout;
    uint32_t  mDataType;
    uint32_t  mElemSize;
} TFCoopMatrixDesc;

typedef struct TFMatrixConvertionDesc
{
    TFCoopMatrixDesc mInputMatrixDesc;
    TFCoopMatrixDesc mOutputMatrixDesc;
    uint32_t         mWidth;
    uint32_t         mHeight;
} TFMatrixConvertionDesc;
#endif
// clang-format off

// API functions
#ifdef __cplusplus
extern "C" {
#endif

// Multiple renderer API (optional)
FORGE_RENDERER_API void FORGE_CALLCONV initRendererContext(const char* appName, const TFRendererContextDesc* pSettings, TFRendererContext** ppContext);
FORGE_RENDERER_API void FORGE_CALLCONV exitRendererContext(TFRendererContext* pContext);

// allocates memory and initializes the renderer -> returns pRenderer
//
FORGE_RENDERER_API void FORGE_CALLCONV initRenderer(const char* appName, const TFRendererDesc* pSettings, TFRenderer** ppRenderer);
FORGE_RENDERER_API void FORGE_CALLCONV exitRenderer(TFRenderer* pRenderer);

void initFence(TFRenderer* pRenderer, TFFence** ppFence);
void exitFence(TFRenderer* pRenderer, TFFence* pFence);

void initSemaphore(TFRenderer* pRenderer, TFSemaphore** ppSemaphore);
void exitSemaphore(TFRenderer* pRenderer, TFSemaphore* pSemaphore);

void initQueue(TFRenderer* pRenderer, TFQueueDesc* pQDesc, TFQueue** ppQueue);
void exitQueue(TFRenderer* pRenderer, TFQueue* pQueue);

void addSwapChain(TFRenderer* pRenderer, const TFSwapChainDesc* pDesc, TFSwapChain** ppSwapChain);
void removeSwapChain(TFRenderer* pRenderer, TFSwapChain* pSwapChain);

// memory functions
void addResourceHeap(TFRenderer* pRenderer, const TFResourceHeapDesc* pDesc, TFResourceHeap** ppHeap);
void removeResourceHeap(TFRenderer* pRenderer, TFResourceHeap* pHeap);

// command pool functions
void initCmdPool(TFRenderer* pRenderer, const TFCmdPoolDesc* pDesc, TFCmdPool** ppCmdPool);
void exitCmdPool(TFRenderer* pRenderer, TFCmdPool* pCmdPool);
void initCmd(TFRenderer* pRenderer, const TFCmdDesc* pDesc, TFCmd** ppCmd);
void exitCmd(TFRenderer* pRenderer, TFCmd* pCmd);
void initCmd_n(TFRenderer* pRenderer, const TFCmdDesc* pDesc, uint32_t cmdCount, TFCmd*** pppCmds);
void exitCmd_n(TFRenderer* pRenderer, uint32_t cmdCount, TFCmd** ppCmds);

//
// All buffer, texture loading handled by resource system -> IResourceLoader.*
//

void addRenderTarget(TFRenderer* pRenderer, const TFRenderTargetDesc* pDesc, TFRenderTarget** ppRenderTarget);
void removeRenderTarget(TFRenderer* pRenderer, TFRenderTarget* pRenderTarget);
void addSampler(TFRenderer* pRenderer, const TFSamplerDesc* pDesc, TFSampler** ppSampler);
void removeSampler(TFRenderer* pRenderer, TFSampler* pSampler);

// texture subresource descriptors (SRV / UAV over a specific mip/array range or aliased format)
void addTextureDescriptor(TFRenderer* pRenderer, const TFTextureDescriptorDesc* pDesc, TFTextureDescriptor** ppDescriptor);
void removeTextureDescriptor(TFRenderer* pRenderer, TFTextureDescriptor* pDescriptor);

// render target view descriptors (pre-baked mip/slice view handle; load/store/clear stay inline at bind time)
void addRenderTargetDescriptor(TFRenderer* pRenderer, const TFRenderTargetDescriptorDesc* pDesc, TFRenderTargetDescriptor** ppDescriptor);
void removeRenderTargetDescriptor(TFRenderer* pRenderer, TFRenderTargetDescriptor* pDescriptor);

// shader functions
void addShaderBinary(TFRenderer* pRenderer, const TFBinaryShaderDesc* pDesc, TFShader** ppShaderProgram);
void removeShader(TFRenderer* pRenderer, TFShader* pShaderProgram);

// pipeline functions
void addPipeline(TFRenderer* pRenderer, const TFPipelineDesc* pPipelineSettings, TFPipeline** ppPipeline);
void removePipeline(TFRenderer* pRenderer, TFPipeline* pPipeline);
void addPipelineCache(TFRenderer* pRenderer, const TFPipelineCacheDesc* pDesc, TFPipelineCache** ppPipelineCache);
void getPipelineCacheData(TFRenderer* pRenderer, TFPipelineCache* pPipelineCache, size_t* pSize, void* pData);
#if defined(SHADER_STATS_AVAILABLE)
void addPipelineStats(TFRenderer* pRenderer, TFPipeline* pPipeline, bool generateDisassembly, TFPipelineStats* pOutStats);
void removePipelineStats(TFRenderer* pRenderer, TFPipelineStats* pStats);
#endif
void removePipelineCache(TFRenderer* pRenderer, TFPipelineCache* pPipelineCache);

// Descriptor Set functions
void addDescriptorSet(TFRenderer* pRenderer, const TFDescriptorSetDesc* pDesc, TFDescriptorSet** ppDescriptorSet);
void removeDescriptorSet(TFRenderer* pRenderer, TFDescriptorSet* pDescriptorSet);
void updateDescriptorSet(TFRenderer* pRenderer, uint32_t index, TFDescriptorSet* pDescriptorSet, uint32_t count, const TFDescriptorData* pParams);

// command buffer functions
void resetCmdPool(TFRenderer* pRenderer, TFCmdPool* pCmdPool);
void beginCmd(TFCmd* pCmd);
void endCmd(TFCmd* pCmd);
void cmdBindRenderTargets(TFCmd* pCmd, const TFBindRenderTargetsDesc* pDesc);
void cmdSetViewport(TFCmd* pCmd, float x, float y, float width, float height, float minDepth, float maxDepth);
void cmdSetScissor(TFCmd* pCmd, uint32_t x, uint32_t y, uint32_t width, uint32_t height);
void cmdSetStencilReferenceValue(TFCmd* pCmd, uint32_t val);
void cmdBindPipeline(TFCmd* pCmd, TFPipeline* pPipeline);
void cmdBindDescriptorSet(TFCmd* pCmd, uint32_t index, TFDescriptorSet* pDescriptorSet);
void cmdBindIndexBuffer(TFCmd* pCmd, TFBuffer* pBuffer, uint32_t indexType, uint64_t offset);
void cmdBindVertexBuffer(TFCmd* pCmd, uint32_t bufferCount, TFBuffer** ppBuffers, const uint32_t* pStrides, const uint64_t* pOffsets);
void cmdDraw(TFCmd* pCmd, uint32_t vertexCount, uint32_t firstVertex);
void cmdDrawInstanced(TFCmd* pCmd, uint32_t vertexCount, uint32_t firstVertex, uint32_t instanceCount, uint32_t firstInstance);
void cmdDrawIndexed(TFCmd* pCmd, uint32_t indexCount, uint32_t firstIndex, uint32_t firstVertex);
void cmdDrawIndexedInstanced(TFCmd* pCmd, uint32_t indexCount, uint32_t firstIndex, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance);
void cmdDispatch(TFCmd* pCmd, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ);

// Transition Commands
void cmdResourceBarrier(TFCmd* pCmd, uint32_t bufferBarrierCount, TFBufferBarrier* pBufferBarriers, uint32_t textureBarrierCount, TFTextureBarrier* pTextureBarriers, uint32_t rtBarrierCount, TFRenderTargetBarrier* pRtBarriers);

// queue/fence/swapchain functions
void acquireNextImage(TFRenderer* pRenderer, TFSwapChain* pSwapChain, TFSemaphore* pSignalSemaphore, TFFence* pFence, uint32_t* pImageIndex);
void queueSubmit(TFQueue* pQueue, const TFQueueSubmitDesc* pDesc);
void queuePresent(TFQueue* pQueue, const TFQueuePresentDesc* pDesc);
void waitQueueIdle(TFQueue* pQueue);
void getFenceStatus(TFRenderer* pRenderer, TFFence* pFence, TFFenceStatus* pFenceStatus);
void waitForFences(TFRenderer* pRenderer, uint32_t fenceCount, TFFence** ppFences);
void toggleVSync(TFRenderer* pRenderer, TFSwapChain** ppSwapchain);

//Returns the recommended format for the swapchain.
//If true is passed for the hintHDR parameter, it will return an HDR format IF the platform supports it
//If false is passed or the platform does not support HDR a non HDR format is returned.
//If true is passed for the hintSrgb parameter, it will return format that is will do gamma correction automatically
//If false is passed for the hintSrgb parameter the gamma correction should be done as a postprocess step before submitting image to swapchain
TinyImageFormat getSupportedSwapchainFormat(TFRenderer* pRenderer, const TFSwapChainDesc* pDesc, TFColorSpace colorSpace);
uint32_t getRecommendedSwapchainImageCount(TFRenderer* pRenderer, const TFWindowHandle* hwnd);

//indirect Draw functions
void cmdExecuteIndirect(TFCmd* pCmd, TFIndirectArgumentType type, unsigned int maxCommandCount, TFBuffer* pIndirectBuffer, uint64_t bufferOffset, TFBuffer* pCounterBuffer, uint64_t counterBufferOffset);

// Workgraph functions
#if defined(TF_ENABLE_WORKGRAPH)
void addWorkgraph(TFRenderer* pRenderer, const TFWorkgraphDesc* pDesc, TFWorkgraph** ppWorkgraph);
void removeWorkgraph(TFRenderer* pRenderer, TFWorkgraph* pWorkgraph);
void cmdDispatchWorkgraph(TFCmd* pCmd, const TFDispatchGraphDesc* pDesc);
#endif
/************************************************************************/
// GPU Query Interface
/************************************************************************/
void getTimestampFrequency(TFQueue* pQueue, double* pFrequency);
void initQueryPool(TFRenderer* pRenderer, const TFQueryPoolDesc* pDesc, TFQueryPool** ppQueryPool);
void exitQueryPool(TFRenderer* pRenderer, TFQueryPool* pQueryPool);
void cmdBeginQuery(TFCmd* pCmd, TFQueryPool* pQueryPool, TFQueryDesc* pQuery);
void cmdEndQuery(TFCmd* pCmd, TFQueryPool* pQueryPool, TFQueryDesc* pQuery);
void cmdResolveQuery(TFCmd* pCmd, TFQueryPool* pQueryPool, uint32_t startQuery, uint32_t queryCount);
void cmdResetQuery(TFCmd* pCmd, TFQueryPool* pQueryPool, uint32_t startQuery, uint32_t queryCount);
void getQueryData(TFRenderer* pRenderer, TFQueryPool* pQueryPool, uint32_t queryIndex, TFQueryData* pOutData);
/************************************************************************/
// Stats Info Interface
/************************************************************************/
void logMemoryStats(TFRenderer* pRenderer);
void calculateMemoryUse(TFRenderer* pRenderer, uint64_t* usedBytes, uint64_t* totalAllocatedBytes);
/************************************************************************/
// Debug Marker Interface
/************************************************************************/
void cmdBeginDebugMarker(TFCmd* pCmd, float r, float g, float b, const char* pName);
void cmdEndDebugMarker(TFCmd* pCmd);
void cmdAddDebugMarker(TFCmd* pCmd, float r, float g, float b, const char* pName);
void cmdWriteMarker(TFCmd* pCmd, const TFMarkerDesc* pDesc);
/************************************************************************/
// Resource Debug Naming Interface
/************************************************************************/
void setBufferName(TFRenderer* pRenderer, TFBuffer* pBuffer, const char* pName);
void setTextureName(TFRenderer* pRenderer, TFTexture* pTexture, const char* pName);
void setRenderTargetName(TFRenderer* pRenderer, TFRenderTarget* pRenderTarget, const char* pName);
void setPipelineName(TFRenderer* pRenderer, TFPipeline* pPipeline, const char* pName);
/************************************************************************/
// Cooperative Matrix Interface
/************************************************************************/
#if defined(TF_ENABLE_COOP_VECTORS)
void cmdConvertCoopMat(TFRenderer* pRenderer, TFCmd * pCmd, TFMatrixConvertionDesc* pDesc, const uint32_t descCount);
#endif
/************************************************************************/
// clang-format on
#ifdef __cplusplus
}
#endif
