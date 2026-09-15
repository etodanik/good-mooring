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

#include "../../../Graphics/Interfaces/IGraphics.h"

#include "../../../Graphics/Interfaces/IGraphicsConfig.h"
#include "../../../Utilities/Interfaces/IMath.h"
#include "../../../Utilities/Threading/Atomics.h"
#include "../ResourceLoaderShared.h.fsl"

static FORGE_CONSTEXPR const TFResourceState gVertexBufferState =
    TF_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER | TF_RESOURCE_STATE_SHADER_RESOURCE;
static FORGE_CONSTEXPR const TFResourceState gIndexBufferState = TF_RESOURCE_STATE_INDEX_BUFFER | TF_RESOURCE_STATE_SHADER_RESOURCE;

typedef struct TFMappedMemoryRange
{
    uint8_t*  pData;
    TFBuffer* pBuffer;
    uint64_t  mOffset;
    uint64_t  mSize;
    uint32_t  mFlags;
} TFMappedMemoryRange;

typedef enum TFTextureContainerType
{
    /// Use whatever container is designed for that platform
    /// Windows, macOS, Linux - TF_TEXTURE_CONTAINER_DDS
    /// iOS, Android          - TF_TEXTURE_CONTAINER_KTX
    TF_TEXTURE_CONTAINER_DEFAULT = 0,
    /// Explicit container types
    /// .dds
    TF_TEXTURE_CONTAINER_DDS,
    /// .ktx
    TF_TEXTURE_CONTAINER_KTX,
    /// .gnf
    TF_TEXTURE_CONTAINER_GNF,
} TFTextureContainerType;

typedef enum TFRegisterMaterialResult
{

    TF_REGISTER_MATERIAL_SUCCESS = 0x0000,
    TF_REGISTER_MATERIAL_BADFILE = 0x0001,

} TFRegisterMaterialResult;

// MARK: - Resource Loading

typedef struct TFPackagePtr
{
    TFIFileSystem* pPackage;
    uint32_t       mDataID;
} TFPackagePtr;

typedef struct TFBufferLoadDesc
{
    TFBuffer**   ppBuffer;
    // This must stay valid until the buffer load is not completed
    // Use waitForToken (if a token was passed to addResource) or waitForAllResourceLoads before freeing pData
    const void*  pData;
    TFBufferDesc mDesc;
    /// MemZero buffer
    bool         mForceReset;

    // Optional (if user provides staging buffer memory)
    TFBuffer* pSrcBuffer;
    uint64_t  mSrcOffset;
} TFBufferLoadDesc;

typedef struct TFOnTextureLoadedData
{
    uint64_t       mSize;
    TFTextureDesc* pTextureDesc;
    void*          pUserData;
} TFOnTextureLoadedData;

typedef void (*OnTextureLoaded)(const TFOnTextureLoadedData*, TFResourcePlacement* placement);

typedef enum TFAtlasPlacementFlags
{
    TF_ATLAS_PLACEMENT_DEFAULT = 0,
    TF_ATLAS_PLACEMENT_LAST_PLACEMENT
} TFAtlasPlacementFlags;

typedef struct TFAtlasPlacement
{
    uint32_t mAtlasIndex;
    uint32_t mAtlasSlice;
    uint32_t mTextureIndex;
    uint32_t mFlags;
} TFAtlasPlacement;

typedef struct TFTextureLoadDesc
{
    TFTexture** ppTexture;
    union
    {
        /// Load empty texture
        struct
        {
            TFTextureDesc* pDesc;
            /// MemZero texture
            bool           mForceReset;
        };
        /// Ycbcr sampler to use when loading ycbcr texture from file
        TFSampler* pYcbcrSampler;
    };

    union
    {
        // Filename without extension. Extension will be determined based on mContainer
        const char* pFileName;

        // Pointer to package containing texture data with an ID to access the data.
        TFPackagePtr mPackagePtr;
    };
    bool mLoadFromPackage;

    TFTexture*        pAtlasTexture;
    TFAtlasPlacement* pAtlasPlacement;

    /// The index of the GPU in SLI/Cross-Fire that owns this texture, or the TFRenderer index in unlinked mode.
    uint32_t               mNodeIndex;
    /// Following is ignored if pDesc != NULL.  pDesc->mFlags will be considered instead.
    TFTextureCreationFlags mCreationFlag;
    /// The texture file format (dds/ktx/...)
    TFTextureContainerType mContainer;
    /// optional call back
    OnTextureLoaded        mOnTextureLoaded;
    void*                  pUserData;
} TFTextureLoadDesc;

typedef struct TFHeapChunk
{
    uint64_t mOffset;
    uint64_t mSize;
} TFHeapChunk;

typedef struct TFHeapAllocator
{
    uint64_t     mTotalSize;
    uint64_t     mAlignement;
    uint64_t     mFreeChunksCount;
    uint64_t     mMaxChunksCount;
    uint64_t     mUsedMemory;
    bool         mAllowAllocFail;
    TFHeapChunk* pFreeChunks;
} TFHeapAllocator;

typedef struct TFHeapAllocatorDesc
{
    uint64_t mSize;
    uint64_t mMaxChunksCount;
    uint64_t mAlignement;
    bool     mAllowAllocFail;
} TFHeapAllocatorDesc;

typedef struct TFBufferChunk
{
    uint32_t mOffset;
    uint32_t mSize;
} TFBufferChunk;

// Structure used to sub-allocate chunks on a buffer, keeps track of free memory to handle new requests.
// Interface to add/remove this allocator is currently private, could be made public if needed.
typedef struct TFBufferChunkAllocator
{
    TFBuffer*      pBuffer;
    uint32_t       mUsedChunkCount;
    uint32_t       mSize;
    TFBufferChunk* mUnusedChunks;
} TFBufferChunkAllocator;

// Stores huge buffers that are then used to sub-allocate memory for each of the loaded meshes.
// TFGeometryBuffer can be provided to TFGeometryLoadDesc::pGeometryBuffer when loading a mesh, sub-chunks will be allocated
// by mIndex and mVertex allocators and return the TFBufferChunk(s) that where used in TFGeometry::mIndexBufferChunk and
// TFGeometry::mVertexBufferChunks
typedef struct TFGeometryBuffer
{
    TFBufferChunkAllocator mIndex;
    TFBufferChunkAllocator mVertex[TF_MAX_VERTEX_BINDINGS];
} TFGeometryBuffer;

FORGE_CONSTEXPR const char GEOMETRY_FILE_MAGIC_STR[] = { 'G', 'e', 'o', 'm', 'e', 't', 'r', 'y', 'T', 'F' };

typedef struct TFMeshlet
{
    /// Offsets within meshlet_vertices and meshlet_triangles arrays with meshlet data
    uint vertexOffset;
    uint triangleOffset;

    /// Number of vertices and triangles used in the meshlet; data is stored in consecutive range defined by offset and count
    uint vertexCount;
    uint triangleCount;
} TFMeshlet;

typedef struct TFMeshletData
{
    float3 center;
    float  radius;

    /// Normal cone, useful for backface culling
    float3 coneApex;
    float3 coneAxis;
    float  coneCutoff; // = cos(angle/2)
} TFMeshletData;

typedef struct TFGeometryMeshlets
{
    uint64_t       mMeshletCount;
    TFMeshlet*     mMeshlets;
    TFMeshletData* mMeshletsData;

    uint64_t  mVertexCount;
    uint32_t* mVertices;

    uint64_t mTriangleCount;
    uint8_t* mTriangles;
} TFGeometryMeshlets;

typedef struct TFGeometry
{
    union
    {
        struct
        {
            /// Index buffer to bind when drawing this geometry
            TFBuffer* pIndexBuffer;
            /// The array of vertex buffers to bind when drawing this geometry
            TFBuffer* pVertexBuffers[TF_MAX_VERTEX_BINDINGS];
        };
        struct
        {
            /// Used when TFGeometry is loaded to unified TFGeometryBuffer object (when TFGeometryLoadDesc::pGeometryBuffer is valid)
            TFBufferChunk mIndexBufferChunk;
            TFBufferChunk mVertexBufferChunks[TF_MAX_VERTEX_BINDINGS];
        };
    };

    /// The array of traditional draw arguments to draw each subset in this geometry
    TFIndirectDrawIndexArguments* pDrawArgs;

    /// The array of vertex buffer strides to bind when drawing this geometry
    uint32_t mVertexStrides[TF_MAX_VERTEX_BINDINGS];

    /// Number of vertex buffers in this geometry
    uint32_t mVertexBufferCount : 8;
    /// Index type (32 or 16 bit)
    uint32_t mIndexType : 2;
    /// Number of draw args in the geometry
    uint32_t mDrawArgCount : 22;
    /// Number of indices in the geometry
    uint32_t mIndexCount;
    /// Number of vertices in the geometry
    uint32_t mVertexCount;

    // If present, data is stored in pGeometryBuffer
    TFGeometryBuffer* pGeometryBuffer;

    TFGeometryMeshlets meshlets;

    uint32_t mPad[20];
} TFGeometry;

static_assert(sizeof(TFGeometry) == 352, "If Geometry size changes we need to rebuild all custom binary meshes");
static_assert(sizeof(TFGeometry) % 16 == 0, "Geometry size must be a multiple of 16");

typedef struct TFOnGeometryDataLoadedData
{
    TFGeometry* geom;
    uint32_t    mIndexStride;
    void*       pIndexData;
    void*       pVertexPositions;
    void*       pUserData;
} TFOnGeometryDataLoadedData;

typedef void (*OnGeometryLoaded)(const TFOnGeometryDataLoadedData*);

// Outputs data that's only needed in the CPU side, OTOH the Geometry object holds GPU related information and buffers
typedef struct TFGeometryData
{
    struct Hair
    {
        uint32_t mVertexCountPerStrand;
        uint32_t mGuideCountPerStrand;
    };

    struct CpuData
    {
        void* pIndices;
        void* pAttributes[TF_MAX_SEMANTICS];

        // Strides for the data in pAttributes, this might not match TFGeometry::mVertexStrides since those are generated based on
        // TFGeometryLoadDesc::pVertexLayout, e.g. if the normals are packed on the GPU as half2 then:
        //         - TFGeometry::mVertexStrides will be sizeof(half2)
        //         - CpuData::mVertexStrides might be sizeof(float3) = 12 (or maybe sizeof(float4) = 16)
        // If the data readed from the file in pAttributes is already packed then CpuData::mVertexStrides[i] ==
        // TFGeometry::mVertexStrides[i]
        uint32_t mVertexStrides[TF_MAX_SEMANTICS];

        // We might have a different number of attributes than mVertexCount.
        // This happens for example for Hair
        uint32_t mAttributeCount[TF_MAX_SEMANTICS];
    };

    /// Copy of the geometry vertex and index data if requested through the load flags
    struct CpuData* pCpuData;

    /// The array of joint inverse bind-pose matrices ( object-space )
    mat4*     pInverseBindPoses;
    /// The array of data to remap skin batch local joint ids to global joint ids
    uint32_t* pJointRemaps;

    /// Number of joints in the skinned geometry
    uint32_t mJointCount;

    /// Hair data
    struct Hair mHair;

    uint32_t mPad0[1];

    TFMeshletData* meshlets;

    // Custom data imported by the user in custom AssetPipelines, this can be data that was exported from a custom tool/plugin
    // specific to the engine/game. See AssetPipeline: callbacks in ProcessGLTFParams for more information.
    void*    pUserData;
    uint32_t mUserDataSize;

    uint32_t mPad1[5];
} TFGeometryData;

static_assert(sizeof(TFGeometryData) % 16 == 0, "TFGeometryData size must be a multiple of 16");

typedef enum TFGeometryLoadFlags
{
    TF_GEOMETRY_LOAD_FLAG_NONE = 0x0,
    /// Keep copy of indices and vertices for CPU
    TF_GEOMETRY_LOAD_FLAG_KEEP_CPU_COPY = 0x1,
    /// Use structured buffers instead of raw buffers
    TF_GEOMETRY_LOAD_FLAG_STRUCTURED_BUFFERS = 0x2,
    /// Geometry buffers can be used as input for ray tracing
    TF_GEOMETRY_LOAD_FLAG_RAYTRACING_INPUT = 0x4,
} TFGeometryLoadFlags;
MAKE_ENUM_FLAG(uint32_t, TFGeometryLoadFlags)

typedef struct TFGeometryBufferLoadDesc
{
    TFResourceState mStartState;

    const char* pNameIndexBuffer;
    const char* pNamesVertexBuffers[TF_MAX_VERTEX_BINDINGS];

    uint32_t mIndicesSize;
    uint32_t mVerticesSizes[TF_MAX_VERTEX_BINDINGS];

    TFResourcePlacement* pIndicesPlacement;
    TFResourcePlacement* pVerticesPlacements[TF_MAX_VERTEX_BINDINGS];

    TFGeometryBuffer**  pOutGeometryBuffer;
    TFGeometryLoadFlags mFlags;

} TFGeometryBufferLoadDesc;

typedef struct TFGeometryBufferLayoutDesc
{
    TFIndexType mIndexType;
    uint32_t    mVerticesStrides[TF_MAX_VERTEX_BINDINGS];
    // Vertex buffer/binding idx for each semantic.
    // Used to locate attributes inside specific buffers for loaded Geometry.
    uint32_t    mSemanticBindings[TF_SEMANTIC_TEXCOORD9 + 1];
} TFGeometryBufferLayoutDesc;

typedef struct TFGeometryLoadDesc
{
    /// Output geometry
    TFGeometry**     ppGeometry;
    TFGeometryData** ppGeometryData;

    union
    {
        /// Filename of geometry container
        const char* pFileName;

        // Pointer to a package and an ID to access the data within the package
        TFPackagePtr mPackagePtr;
    };
    bool mLoadFromPackage;

    /// Loading flags
    TFGeometryLoadFlags   mFlags;
    /// Linked gpu node / Unlinked TFRenderer index
    uint32_t              mNodeIndex;
    /// Specifies how to arrange the vertex data loaded from the file into GPU memory
    const TFVertexLayout* pVertexLayout;

    /// Optional preallocated unified buffer for geometry.
    /// When this parameter is specified, TFGeometry::pDrawArgs values are going
    /// to be shifted according to index/vertex location within TFBufferChunkAllocator.
    TFGeometryBuffer* pGeometryBuffer;

    /// Used to convert data to desired state inside TFGeometryBuffer.
    TFGeometryBufferLayoutDesc* pGeometryBufferLayoutDesc;

    /// call back that can be called right when data is loaded in memory, before buffer creation
    OnGeometryLoaded pOnGeometryLoaded;
    void*            pOnGeometryLoadedUserData;
} TFGeometryLoadDesc;

typedef struct TFBufferUpdateDesc
{
    TFBuffer* pBuffer;
    uint64_t  mDstOffset;
    uint64_t  mSize;

    /// To be filled by the caller between beginUpdateResource and endUpdateResource calls
    /// Example:
    /// TFBufferUpdateDesc update = { pBuffer, bufferDstOffset };
    /// beginUpdateResource(&update);
    /// ParticleVertex* vertices = (ParticleVertex*)update.pMappedData;
    ///   for (uint32_t i = 0; i < particleCount; ++i)
    ///	    vertices[i] = { rand() };
    /// endUpdateResource(&update, &token);
    void* pMappedData;

    // Optional (if user provides staging buffer memory)
    TFBuffer*       pSrcBuffer;
    uint64_t        mSrcOffset;
    TFResourceState mCurrentState;

    /// Internal
    struct
    {
        TFMappedMemoryRange mMappedRange;
    } mInternal;
} TFBufferUpdateDesc;

typedef struct TFTextureSubresourceUpdate
{
    /// Filled by ResourceLaoder in beginUpdateResource
    /// Size of each row in destination including padding - Needs to be respected otherwise texture data will be corrupted if dst row stride
    /// is not the same as src row stride
    uint32_t mDstRowStride;
    /// Number of rows in this slice of the texture
    uint32_t mRowCount;
    /// Src row stride for convenience (mRowCount * width * texture format size)
    uint32_t mSrcRowStride;
    /// Size of each slice in destination including padding - Use for offsetting dst data updating 3D textures
    uint32_t mDstSliceStride;
    /// Size of each slice in src - Use for offsetting src data when updating 3D textures
    uint32_t mSrcSliceStride;
    /// To be filled by the caller
    /// Example:
    /// TFBufferUpdateDesc update = { pTexture, 2, 1 };
    /// beginUpdateResource(&update);
    /// Row by row copy is required if mDstRowStride > mSrcRowStride. Single memcpy will work if mDstRowStride == mSrcRowStride
    /// 2D
    /// for (uint32_t r = 0; r < update.mRowCount; ++r)
    ///     memcpy(update.pMappedData + r * update.mDstRowStride, srcPixels + r * update.mSrcRowStride, update.mSrcRowStride);
    /// 3D
    /// for (uint32_t z = 0; z < depth; ++z)
    /// {
    ///     uint8_t* dstData = update.pMappedData + update.mDstSliceStride * z;
    ///     uint8_t* srcData = srcPixels + update.mSrcSliceStride * z;
    ///     for (uint32_t r = 0; r < update.mRowCount; ++r)
    ///         memcpy(dstData + r * update.mDstRowStride, srcData + r * update.mSrcRowStride, update.mSrcRowStride);
    /// }
    /// endUpdateResource(&update, &token);
    uint8_t* pMappedData;
} TFTextureSubresourceUpdate;

/// #NOTE: Only use for procedural textures which are created on CPU (noise textures, font texture, ...)
typedef struct TFTextureUpdateDesc
{
    TFTexture*      pTexture;
    /// First mip to update.
    uint32_t        mBaseMipLevel;
    /// Number of mips to update starting at mBaseMipLevel.
    uint32_t        mMipLevels;
    /// First array layer to update.
    uint32_t        mBaseArrayLayer;
    /// Number of array layers to update starting at mBaseArrayLayer.
    uint32_t        mLayerCount;
    TFResourceState mCurrentState;
    // Optional - If we want to run the update on user specified command buffer instead
    TFCmd*          pCmd;

#if defined(__cplusplus)
    FORGE_RENDERER_API TFTextureSubresourceUpdate getSubresourceUpdateDesc(uint32_t mip, uint32_t layer);
#endif
    /// Internal
    struct
    {
        TFMappedMemoryRange mMappedRange;
        uint32_t            mDstSliceStride;
        bool                mSkipBarrier;
    } mInternal;
} TFTextureUpdateDesc;

typedef struct TFTextureCopyDesc
{
    TFTexture*      pTexture;
    TFBuffer*       pBuffer;
    /// Semaphore to synchronize graphics/compute operations that write to the texture with the texture -> buffer copy.
    TFSemaphore*    pWaitSemaphore;
    uint32_t        mTextureMipLevel;
    uint32_t        mTextureArrayLayer;
    /// Current texture state.
    TFResourceState mTextureState;
    /// Queue the texture is copied from.
    TFQueueType     mQueueType;
    uint64_t        mBufferOffset;
} TFTextureCopyDesc;

typedef struct TFShaderStageLoadDesc
{ //-V802 : Very user-facing struct, and order is highly important to convenience
    const char* pFileName;
    const char* pEntryPointName;
} TFShaderStageLoadDesc;

typedef struct TFShaderLoadDesc
{
    TFShaderStageLoadDesc mVert;
    TFShaderStageLoadDesc mFrag;
    TFShaderStageLoadDesc mGeom;
    TFShaderStageLoadDesc mHull;
    TFShaderStageLoadDesc mDomain;
    TFShaderStageLoadDesc mComp;
#if defined(TF_ENABLE_WORKGRAPH)
    TFShaderStageLoadDesc mGraph;
#endif
    const TFShaderConstant* pConstants;
    uint32_t                mConstantCount;
} TFShaderLoadDesc;

typedef struct TFPipelineCacheLoadDesc
{
    const char*          pFileName;
    TFPipelineCacheFlags mFlags;
} TFPipelineCacheLoadDesc;

typedef struct TFPipelineCacheSaveDesc
{
    const char* pFileName;
} TFPipelineCacheSaveDesc;

typedef struct TFRootSignatureDesc
{
    const char* pGraphicsFileName;
    const char* pComputeFileName;
#if defined(VULKAN)
    const TFStaticSamplerDesc* pStaticSamplers;
    uint32_t                   mStaticSamplerCount;
#endif
} TFRootSignatureDesc;

typedef uint64_t TFSyncToken;

typedef struct TFMaterial TFMaterial;

typedef struct TFResourceLoaderDesc
{
    uint64_t mBufferSize;
    uint32_t mBufferCount;
    bool     mSingleThreaded;
#ifdef ENABLE_FORGE_MATERIALS
    bool mUseMaterials;
#endif
} TFResourceLoaderDesc;

typedef struct TFStreamZone
{
    vec3 mMin;
    vec3 mMax;
    vec3 mCenter;
} TFStreamZone;

typedef uint16_t TFTextureID;
typedef uint64_t TFTextureBundleID;

typedef uint16_t TFMaterialFlags;

typedef struct TFMaterialProps
{
    uint32_t        mBaseColor;
    uint32_t        mSpecEmissiveStrength;
    TFMaterialFlags mFlags;

} TFMaterialProps;

typedef struct TFTexturePackagedDesc
{
    // Total number of textures available in the package
    size_t             mTextureCount;
    // Names of the textures available in the package
    char*              pTextureNames;
    // Number of meshes that each use a different textureBundleID.
    size_t             mMeshCount;
    // List of IDs where each ID represents the index of the 3 textures + material flags used in the shader.
    TFTextureBundleID* pTextureBundleIDs;
    // Non texture material properties and mesh flags
    TFMaterialProps*   pMaterialProps;
    // The bounding boxes that will be used for texture streaming
    TFStreamZone*      pStreamZones;
} TFTexturePackagedDesc;

typedef struct TFAnimationNodeIDRange
{
    size_t mStartNodeId;
    size_t mAnimationCount;
} TFAnimationNodeIDRange;

typedef struct TFAtlasTextureDesc
{
    uint32_t        mWidth;
    uint32_t        mHeight;
    uint32_t        mDepth;
    uint32_t        mSliceCount;
    uint32_t        mMipLevels;
    TinyImageFormat mFormat;
} TFAtlasTextureDesc;

#define TF_PAK_HEADER_STR  "TF_PAK_00"
#define TF_PAK_HEADER_SIZE 10
typedef struct TFPackageDesc
{
    // Header string to verify that the Package being loaded is the same version as was Packaged
    char                    mHeaderStr[TF_PAK_HEADER_SIZE];
    // Total number of processed geo binaries in the package
    size_t                  mGeoCount;
    // NodeIDs of the geo within the final package
    size_t*                 pGeoNodeIds;
    // NodeIDs of the textureMetadata within the final package
    size_t*                 pTexMetadataNodeIds;
    // NodeIDs of the animations within the final package
    TFAnimationNodeIDRange* pAnimationNodeIDRanges;
    // The node ID pointing to the first texture available for each geo.
    size_t*                 pTextureStartNodeIds;

    // NodeID of the atlas of textures in the final package
    size_t mTextureAtlasNodeID;
    // Size of the buffer used to store all the geo binary names
    size_t mNameBufferSize;
    // Names of all the geo binaries
    char*  pGeoNames;

} TFPackageDesc;

static inline TFTextureBundleID PackTextureBundle(TFTextureID baseTexIndex, TFTextureID specTexIndex, TFTextureID normTexIndex)
{
    return ((TFTextureBundleID)baseTexIndex | ((TFTextureBundleID)specTexIndex << SPEC_BITSHIFT) |
            ((TFTextureBundleID)normTexIndex << NORM_BITSHIFT));
}

static inline TFMaterialProps PackMaterialProperties(float4 baseColor, float2 spec, float emissiveStrength, TFMaterialFlags flags)
{
    TFMaterialProps result;
    result.mBaseColor = packR8G8B8A8(baseColor);
    result.mSpecEmissiveStrength = (packR8G8(spec) << 16) | (half(emissiveStrength).sh & 0xffff);
    result.mFlags = flags;
    return result;
}

typedef struct TFPackage
{
    // Name of the package to open.
    const char* packageName;

    // If true, then we will load both the metadata and actual data from the package.
    // Else, will only load the metadata from the package.
    bool loadGeoData;
    bool loadTexData;

    TFIFileSystem* pPackageHandle;

    // Data read from the package
    TFPackageDesc*      pPackageMetadata;
    TFGeometryLoadDesc* pGeoData;

    TFTexturePackagedDesc* pTextureMetadata;
    TFTextureLoadDesc*     pTextureData;

    uint32_t            mAtlasCount;
    uint32_t            mAtlasPlacementsCount;
    TFAtlasTextureDesc* pAtlasDescriptors;
    TFAtlasPlacement*   pAtlasPlacements;
    TFBuffer*           pAtlasPlacementsBuffer;

    TFTexture** ppAtlasTextures;

    TFTexture** ppAllTextures;
} TFPackage;

typedef struct TFPackageLoadDesc
{
    const char* packageName;

    bool loadGeoData;
    bool loadTexData;

    bool createAtlas;

    TFGeometryBuffer*           pGeometryBuffer;
    TFGeometryBufferLayoutDesc* pGeometryBufferLayout;
    TFGeometryLoadFlags         geometryLoadFlags;
    TFVertexLayout*             pGeoVertexLayout;
    OnGeometryLoaded            pOnGeometryLoaded;
    void*                       pOnGeometryLoadedUserData;

    TFPackage** ppOutPackage;
} TFPackageLoadDesc;

#define PACKAGE_STRINGS_ENABLED FORGE_DEBUG

#if defined(__cplusplus)
extern "C"
{
#endif

FORGE_RENDERER_API extern TFResourceLoaderDesc gDefaultResourceLoaderDesc;

// MARK: - Resource Loader Functions
/// Multiple TFRenderer (unlinked GPU) variants. The Resource Loader must be shared between Renderers.
FORGE_RENDERER_API void initResourceLoaderInterface(TFRenderer** ppRenderers, uint32_t rendererCount, TFResourceLoaderDesc* pDesc);
FORGE_RENDERER_API void exitResourceLoaderInterface(TFRenderer** ppRenderers, uint32_t rendererCount);

// MARK: App Material Management
#ifdef ENABLE_FORGE_MATERIALS

// Will load a material and all related shaders/textures (if they are not already loaded, Material shaders/textures are shared across
// all Materials)
FORGE_RENDERER_API uint32_t addMaterial(const char* pMaterialFileName, TFMaterial** pMaterial, TFSyncToken* pSyncToken);
// Will unload all the related shaders/textures (if they are not still used by some other Material)
FORGE_RENDERER_API void     removeMaterial(TFMaterial* pMaterial);

// TODO: Functions below are a simple interface to get resources from materials, as we develop materials further this interface will
// probably change.
FORGE_RENDERER_API uint32_t getMaterialSetIndex(TFMaterial* pMaterial, const char* name);
FORGE_RENDERER_API void     getMaterialShader(TFMaterial* pMaterial, uint32_t materialSetIndex, TFShader** ppOutShader);
FORGE_RENDERER_API void     getMaterialTextures(TFMaterial* pMaterial, uint32_t materialSetIndex, const char** ppOutTextureBindingNames,
                                                TFTexture** ppOutTextures, uint32_t outTexturesSize);

#endif // ENABLE_FORGE_MATERIALS

// MARK: addResource and updateResource
FORGE_RENDERER_API void getResourceSizeAlign_Buffer(const TFBufferLoadDesc* pDesc, TFResourceSizeAlign* pOut);
FORGE_RENDERER_API void getResourceSizeAlign_Texture(const TFTextureLoadDesc* pDesc, TFResourceSizeAlign* pOut);

/// Adding and updating resources can be done using a addResource or
/// beginUpdateResource/endUpdateResource pair.
/// if addResource(TFBufferLoadDesc) is called with a data size larger than the ResourceLoader's staging buffer, the ResourceLoader
/// will perform multiple copies/flushes rather than failing the copy.

/// If token is NULL, the resource will be available when allResourceLoadsCompleted() returns true.
/// If token is non NULL, the resource will be available after isTokenCompleted(token) returns true.
FORGE_RENDERER_API void addResource_Buffer(TFBufferLoadDesc* pBufferDesc, TFSyncToken* token);
FORGE_RENDERER_API void addResource_Texture(TFTextureLoadDesc* pTextureDesc, TFSyncToken* token);
FORGE_RENDERER_API void addResource_Geometry(TFGeometryLoadDesc* pGeomDesc, TFSyncToken* token);
FORGE_RENDERER_API void addGeometryBuffer(TFGeometryBufferLoadDesc* pDesc);

FORGE_RENDERER_API void beginUpdateResource_Buffer(TFBufferUpdateDesc* pBufferDesc);
FORGE_RENDERER_API void beginUpdateResource_Texture(TFTextureUpdateDesc* pTextureDesc);
FORGE_RENDERER_API void endUpdateResource_Buffer(TFBufferUpdateDesc* pBuffer);
FORGE_RENDERER_API void endUpdateResource_Texture(TFTextureUpdateDesc* pTexture);

/// This function is used to acquire geometry buffer location.
/// It can be used on index or vertex buffer
/// When there are no continious chunk with enough size, output chunk contains 0 size.
/// Use releaseGeometryBufferPart to release chunk.
/// Make sure all chunks are released before removeGeometryBuffer.
FORGE_RENDERER_API void addGeometryBufferPart(TFBufferChunkAllocator* buffer, uint32_t size, uint32_t alignment, TFBufferChunk* pOut,
                                              TFBufferChunk* pPreferredChunk);

/// Release previously claimed chunk to buffer.
/// Buffer must be the one passed to claimGeometryBufferPart for this chunk.
FORGE_RENDERER_API void removeGeometryBufferPart(TFBufferChunkAllocator* buffer, TFBufferChunk* chunk);

/// allocation/deallocation from inside pre-allocated resource heaps
FORGE_RENDERER_API void addHeapAllocator(const TFHeapAllocatorDesc* pDesc, TFHeapAllocator** ppAllocator);
FORGE_RENDERER_API void removeHeapAllocator(TFHeapAllocator* pAllocator);
FORGE_RENDERER_API bool heapAllocatorAlloc(TFHeapAllocator* allocator, uint64_t size, TFHeapChunk* pOut);
FORGE_RENDERER_API void heapAllocatorFree(TFHeapAllocator* allocator, uint64_t offset, uint64_t size);

typedef struct FlushResourceUpdateDesc
{
    uint32_t      mNodeIndex;
    uint32_t      mWaitSemaphoreCount;
    TFSemaphore** ppWaitSemaphores;
    TFFence*      pOutFence;
    TFSemaphore*  pOutSubmittedSemaphore;
} FlushResourceUpdateDesc;
FORGE_RENDERER_API void flushResourceUpdates(FlushResourceUpdateDesc* pDesc);

/// Copies data from GPU to the CPU, typically for transferring it to another GPU in unlinked mode.
/// For optimal use, the amount of data to transfer should be minimized as much as possible and applications should
/// provide additional graphics/compute work that the GPU can execute alongside the copy.
FORGE_RENDERER_API void copyResource(TFTextureCopyDesc* pTextureDesc, TFSyncToken* token);

// MARK: removeResource
FORGE_RENDERER_API void removeResource_Buffer(TFBuffer* pBuffer);
FORGE_RENDERER_API void removeResource_Texture(TFTexture* pTexture);
FORGE_RENDERER_API void removeResource_Geometry(TFGeometry* pGeom);
FORGE_RENDERER_API void removeResource_GeometryData(TFGeometryData* pGeom);
FORGE_RENDERER_API void removeGeometryBuffer(TFGeometryBuffer* pGeomBuffer);
// Frees pGeom->pCpuData in case it was requested with TF_GEOMETRY_LOAD_FLAG_KEEP_CPU_COPY and you are already done with it
FORGE_RENDERER_API void removeGeometryCpuData(TFGeometryData* pGeom);

// MARK: Waiting for Loads

/// Returns whether all submitted resource loads and updates have been completed.
FORGE_RENDERER_API bool allResourceLoadsCompleted();

/// Blocks the calling thread until allResourceLoadsCompleted() returns true.
/// Note that if more resource loads or updates are submitted from a different thread while
/// while the calling thread is blocked, those loads or updates are not guaranteed to have
/// completed when this function returns.
FORGE_RENDERER_API void waitForAllResourceLoads();

/// Wait for the copy queue to finish all work
FORGE_RENDERER_API void waitCopyQueueIdle();

/// Returns wheter the resourceloader is single threaded or not
FORGE_RENDERER_API bool isResourceLoaderSingleThreaded();

/// A TFSyncToken is an array of monotonically increasing integers.
/// getLastTokenCompleted() returns the last value for which
/// isTokenCompleted(token) is guaranteed to return true.
FORGE_RENDERER_API TFSyncToken getLastTokenCompleted();
FORGE_RENDERER_API bool        isTokenCompleted(const TFSyncToken* token);
FORGE_RENDERER_API void        waitForToken(const TFSyncToken* token);

/// Allows clients to synchronize with the submission of copy commands (as opposed to their completion).
/// This can reduce the wait time for clients but requires using the Semaphore from getLastSemaphoreCompleted() in a wait
/// operation in a submit that uses the textures just updated.
FORGE_RENDERER_API TFSyncToken getLastTokenSubmitted();
FORGE_RENDERER_API bool        isTokenSubmitted(const TFSyncToken* token);
FORGE_RENDERER_API void        waitForTokenSubmitted(const TFSyncToken* token);

/// Return the semaphore for the last copy operation of a specific GPU.
/// Could be NULL if no operations have been executed.
FORGE_RENDERER_API TFSemaphore* getLastSemaphoreSubmitted(uint32_t nodeIndex);

/// Either loads the cached shader bytecode or compiles the shader to create new bytecode depending on whether source is newer than
/// binary
FORGE_RENDERER_API void addShader(TFRenderer* pRenderer, const TFShaderLoadDesc* pDesc, TFShader** pShader);

/// Save/Load pipeline cache from disk
FORGE_RENDERER_API void loadPipelineCache(TFRenderer* pRenderer, const TFPipelineCacheLoadDesc* pDesc, TFPipelineCache** ppPipelineCache);
FORGE_RENDERER_API void savePipelineCache(TFRenderer* pRenderer, TFPipelineCache* pPipelineCache, TFPipelineCacheSaveDesc* pDesc);

FORGE_RENDERER_API void initRootSignature(TFRenderer* pRenderer, const TFRootSignatureDesc* pDesc);
FORGE_RENDERER_API void exitRootSignature(TFRenderer* pRenderer);

/// Determines whether we are using Uniform Memory Architecture or not.
/// Do not assume this variable will be the same, if code was compiled with multiple APIs result of this function might change per API.
FORGE_RENDERER_API TFUMASupportFlags getUmaFlags();

#if defined(__cplusplus)
}
#endif

#if defined(__cplusplus)
// MARK: - Resource Loader Functions
FORGE_RENDERER_API void initResourceLoaderInterface(TFRenderer* pRenderer, TFResourceLoaderDesc* pDesc = nullptr);
FORGE_RENDERER_API void exitResourceLoaderInterface(TFRenderer* pRenderer);

/// Multiple TFRenderer (unlinked GPU) variants. The Resource Loader must be shared between Renderers.
FORGE_RENDERER_API void initResourceLoaderInterface(TFRenderer** ppRenderers, uint32_t rendererCount);

// MARK: addResource and updateResource
FORGE_RENDERER_API void getResourceSizeAlign(const TFBufferLoadDesc* pDesc, TFResourceSizeAlign* pOut);
FORGE_RENDERER_API void getResourceSizeAlign(const TFTextureLoadDesc* pDesc, TFResourceSizeAlign* pOut);

/// If token is NULL, the resource will be available when allResourceLoadsCompleted() returns true.
/// If token is non NULL, the resource will be available after isTokenCompleted(token) returns true.
FORGE_RENDERER_API void addResource(TFBufferLoadDesc* pBufferDesc, TFSyncToken* token);
FORGE_RENDERER_API void addResource(TFTextureLoadDesc* pTextureDesc, TFSyncToken* token);
FORGE_RENDERER_API void addResource(TFGeometryLoadDesc* pGeomDesc, TFSyncToken* token);

FORGE_RENDERER_API void beginUpdateResource(TFBufferUpdateDesc* pBufferDesc);
FORGE_RENDERER_API void beginUpdateResource(TFTextureUpdateDesc* pTextureDesc);
FORGE_RENDERER_API void endUpdateResource(TFBufferUpdateDesc* pBuffer);
FORGE_RENDERER_API void endUpdateResource(TFTextureUpdateDesc* pTexture);

/// This function is used to acquire geometry buffer location.
/// It can be used on index or vertex buffer
/// When there are no continious chunk with enough size, output chunk contains 0 size.
/// Use releaseGeometryBufferPart to release chunk.
/// Make sure all chunks are released before removeGeometryBuffer.
FORGE_RENDERER_API void addGeometryBufferPart(TFBufferChunkAllocator* buffer, uint32_t size, uint32_t alignment, TFBufferChunk* pOut);

// MARK: removeResource
FORGE_RENDERER_API void removeResource(TFBuffer* pBuffer);
FORGE_RENDERER_API void removeResource(TFTexture* pTexture);
FORGE_RENDERER_API void removeResource(TFGeometry* pGeom);
FORGE_RENDERER_API void removeResource(TFGeometryData* pGeom);

FORGE_RENDERER_API void removeGeometryBuffer(TFGeometryBuffer* pGeomBuffer);
// Frees pGeom->pCpuData in case it was requested with TF_GEOMETRY_LOAD_FLAG_KEEP_CPU_COPY and you are already done with it
FORGE_RENDERER_API void removeGeometryCpuData(TFGeometryData* pGeom);

// MARK: Waiting for Loads

/// Returns whether all submitted resource loads and updates have been completed.
FORGE_RENDERER_API bool allResourceLoadsCompleted();

/// Blocks the calling thread until allResourceLoadsCompleted() returns true.
/// Note that if more resource loads or updates are submitted from a different thread while
/// while the calling thread is blocked, those loads or updates are not guaranteed to have
/// completed when this function returns.
FORGE_RENDERER_API void waitForAllResourceLoads();

/// Wait for the copy queue to finish all work
FORGE_RENDERER_API void waitCopyQueueIdle();

/// Returns wheter the resourceloader is single threaded or not
FORGE_RENDERER_API bool isResourceLoaderSingleThreaded();

/// A TFSyncToken is an array of monotonically increasing integers.
/// getLastTokenCompleted() returns the last value for which
/// isTokenCompleted(token) is guaranteed to return true.
FORGE_RENDERER_API TFSyncToken getLastTokenCompleted();
FORGE_RENDERER_API bool        isTokenCompleted(const TFSyncToken* token);
FORGE_RENDERER_API void        waitForToken(const TFSyncToken* token);

/// Allows clients to synchronize with the submission of copy commands (as opposed to their completion).
/// This can reduce the wait time for clients but requires using the Semaphore from getLastSemaphoreCompleted() in a wait
/// operation in a submit that uses the textures just updated.
FORGE_RENDERER_API TFSyncToken getLastTokenSubmitted();
FORGE_RENDERER_API bool        isTokenSubmitted(const TFSyncToken* token);
FORGE_RENDERER_API void        waitForTokenSubmitted(const TFSyncToken* token);

/// Return the semaphore for the last copy operation of a specific GPU.
/// Could be NULL if no operations have been executed.
FORGE_RENDERER_API TFSemaphore* getLastSemaphoreSubmitted(uint32_t nodeIndex);

/// Either loads the cached shader bytecode or compiles the shader to create new bytecode depending on whether source is newer than binary
FORGE_RENDERER_API void addShader(TFRenderer* pRenderer, const TFShaderLoadDesc* pDesc, TFShader** pShader);

/// Save/Load pipeline cache from disk
FORGE_RENDERER_API void loadPipelineCache(TFRenderer* pRenderer, const TFPipelineCacheLoadDesc* pDesc, TFPipelineCache** ppPipelineCache);
FORGE_RENDERER_API void savePipelineCache(TFRenderer* pRenderer, TFPipelineCache* pPipelineCache, TFPipelineCacheSaveDesc* pDesc);

FORGE_RENDERER_API void initRootSignature(TFRenderer* pRenderer, const TFRootSignatureDesc* pDesc);
FORGE_RENDERER_API void exitRootSignature(TFRenderer* pRenderer);

/// Determines whether we are using Uniform Memory Architecture or not.
/// Do not assume this variable will be the same, if code was compiled with multiple APIs result of this function might change per API.
FORGE_RENDERER_API TFUMASupportFlags getUmaFlags();

FORGE_RENDERER_API bool addResourcesFromPackage(TFPackageLoadDesc* pDesc, TFTexture* pNullTextureResource);
FORGE_RENDERER_API void removeResourcesFromPackage(TFPackage* pPackage);

#endif
