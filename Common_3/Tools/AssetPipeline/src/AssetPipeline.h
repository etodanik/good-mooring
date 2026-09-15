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

#include "../../../Resources/AnimationSystem/ThirdParty/OpenSource/ozz-animation/include/ozz/animation/runtime/animation.h"
#include "../../../Resources/AnimationSystem/ThirdParty/OpenSource/ozz-animation/include/ozz/animation/runtime/skeleton.h"
#include "../../../Utilities/ThirdParty/OpenSource/bstrlib/bstrlib.h"

#include "../../../Utilities/Interfaces/IFileSystem.h"
#include "../../../Utilities/Interfaces/IToolFileSystem.h"

#include "AssetPipelineConfig.h"

#define STRCMP(a, b)  (stricmp(a, b) == 0)

#define MAX_FLAGS     100
#define MAX_FILTERS   100

#define MAX_MIPLEVELS 16 //= TINYDDS_MAX_MIPMAPLEVELS

typedef struct TFTextureDesc  TFTextureDesc;
typedef struct TFVertexLayout TFVertexLayout;

// Error codes returned by AssetPipelineRun
typedef enum AssetPipelineErrorCode
{
    ASSET_PIPELINE_SUCCESS = 0,
    ASSET_PIPELINE_GENERAL_FAILURE = 1,
    // TODO: add more error codes as needed
} AssetPipelineErrorCode;

enum AssetPipelineProcessMode
{
    PROCESS_MODE_FILE,
    PROCESS_MODE_DIRECTORY,
    PROCESS_MODE_DIRECTORY_RECURSIVE,

    PROCESS_MODE_NONE
};

struct ProcessAssetsSettings
{
    bool quiet;               // Only output warnings.
    bool force;               // Force all assets to be processed.
    uint minLastModifiedTime; // Force all assets older than this to be processed.
};

enum AssetPipelineProcess
{
    PROCESS_ANIMATIONS,
    PROCESS_TFX,
    PROCESS_GLTF,
    PROCESS_TEXTURES,
    PROCESS_IES,
    PROCESS_FONTS,
    PROCESS_WRITE_ZIP,
    PROCESS_WRITE_ZIP_ALL,
    PROCESS_PACKAGING,
    PROCESS_COUNT
};

struct AssetPipelineProcessCommand
{
    const char*          mCommandString;
    AssetPipelineProcess mProcessType;
};

struct AssetPipelineParams
{
    // If processing individual file
    const char* mInFilePath;
    time_t
        mAdditionalModifiedTime; // If not 0 this value might be used to determine if asset needs to be cooked or not (we take max between
                                 // this value and mInFilePath last modification time and compare it with output file modification time)

    const char* mInExt;

    const char* mInDir;
    const char* mOutSubdir; // If set, the subdirectory of the input filepath will be ignored and this one will be used instead

    // From argc and argv
    const char* mFlags[MAX_FLAGS];
    int32_t     mFlagsCount;

    AssetPipelineProcess     mProcessType;
    AssetPipelineProcessMode mPathMode; // FILE or DIRECTORYxw
    ProcessAssetsSettings    mSettings;

    TFResourceDirectory mRDInput;
    TFResourceDirectory mRDOutput;

    // TODO looks like this directory is always set to nothing
    TFResourceDirectory mRDZipWrite;
};

struct SkeletonAndAnimations
{
    struct AnimationFile
    {
        bstring mInputAnim;      // Path relative to AssetPipeline in directory
        bstring mOutputAnimPath; // Path relative to ProcessAnimationsParams::mSkeletonAndAnimOutRd
    };

    bstring mSkeletonInFile;  // Path relative to AssetPipeline in directory
    bstring mSkeletonOutFile; // Path relative to ProcessAnimationsParams::mSkeletonAndAnimOutRd

    AnimationFile* mAnimations; // stbds array
};

typedef enum TextureCompression
{
    COMPRESSION_NONE,
    COMPRESSION_ASTC,
    COMPRESSION_BC,
} TextureCompression;

typedef enum DXT
{
    DXT_NONE,
    DXT_BC1, // RGB + 1A
    DXT_BC3, // RGB + 8A
    DXT_BC4, // Single Channel
    DXT_BC5, // Two channels
    DXT_BC6, // RGB HalfFloats
    DXT_BC7,
} DXT;

typedef enum ASTC
{
    ASTC_NONE,
    ASTC_4x4,
    ASTC_4x4_SLOW, // 8.00 Bits/Pixel
    ASTC_5x4,
    ASTC_5x4_SLOW, // 6.40 Bits/Pixel
    ASTC_5x5,
    ASTC_5x5_SLOW, // 5.12 Bits/Pixel
    ASTC_6x6,
    ASTC_6x6_SLOW, // 3.56 Bits/Pixel
    ASTC_8x5,
    ASTC_8x5_SLOW, // 3.20 Bits/Pixel
    ASTC_8x6,
    ASTC_8x6_SLOW, // 2.67 Bits/Pixel
    ASTC_8x8,
    ASTC_8x8_SLOW, // 2.00 Bits/Pixel
} ASTC;

typedef enum TextureContainer
{
    CONTAINER_DDS,
    CONTAINER_KTX
#ifdef PROSPERO_GNF
    ,
    CONTAINER_GNF_ORBIS,
    CONTAINER_GNF_PROSPERO
#endif
#ifdef XBOX_SCARLETT_DDS
    ,
    CONTAINER_SCARLETT_DDS
#endif
} TextureContainer;

typedef enum TextureMipmap
{
    MIPMAP_DEFAULT,
    MIPMAP_CUSTOM,
    MIPMAP_NONE
} TextureMipmap;

typedef struct ProcessedTextureData
{
    bstring  mOutputFilePath;
    uint32_t mWidth;
    uint32_t mHeight;
    uint32_t mDepth;
    uint32_t mArraySize;
    uint32_t mMipLevels;
    uint32_t mFormat;
    uint32_t mIndex;
} ProcessedTextureData;

typedef union TextureSwizzle
{
    // Shader style swizzling language
    // Source channel indices are stored as characters 'r'/'x', 'g'/'y', 'b'/'z', 'a'/'w', '0', '1'
    struct
    {
        uint8_t mR;
        uint8_t mG;
        uint8_t mB;
        uint8_t mA;
    };
    uint8_t mIndices[4];
} TextureSwizzle;

void GenerateMipmaps(uint8_t* ppData[MAX_MIPLEVELS], uint32_t* pImageDataSize, TFTextureDesc* pTextDesc);

typedef void (*GenerateMipmapsCallback)(uint8_t* ppData[MAX_MIPLEVELS], uint32_t* pImageDataSize, TFTextureDesc* pTextureDesc,
                                        uint32_t channelsCount, void* pUserData);

typedef struct TexturePackagingData
{
    size_t mTextureCount;
    size_t mNameBufferSize;
    char*  pNameBuffer;
} TexturePackagingData;

typedef struct ProcessTexturesParams
{
    const char*             mInExt;
    GenerateMipmapsCallback pGenerateMipmapsCallback;
    void*                   pCallbackUserData;
    TextureContainer        mContainer;
    TextureCompression      mCompression;
    ASTC                    mOverrideASTC;
    DXT                     mOverrideBC;
    TextureMipmap           mGenerateMipmaps;
    TextureSwizzle          mSwizzle;
    int32_t                 mSwizzleChannelCount;
    int32_t                 mTargetWidth;
    int32_t                 mTargetHeight;
    bool                    mProcessAsNormalMap;
    bool                    mProcessAsCubeMap;
    bool                    mInputLinearColorSpace;

    // Used for vMF with a normal texture (only single file mode for now)
    const char* pRoughnessFilePath;

    // Optional, leave null if you don't want this data
    // User is in charge of freeing the memory of this array and internal elements
    ProcessedTextureData** ppOutProcessedTextureData;

    // Params for Packaging
    bool   mProcessForPackaging;
    bool   mProcessForAtlas;
    size_t mTextureNameCount;
    char*  pInputFileNameList;
} ProcessTexturesParams;

typedef struct ProcessFontsParams
{
    uint32_t mGlyphResolution;
    float    mPxRange;
    bool     mOnlyASCII;
} ProcessFontsParams;

struct ProcessTressFXParams
{
    uint32_t mFollowHairCount;
    float    mMaxRadiusAroundGuideHair;
    float    mTipSeperationFactor;
};

struct WriteZipParams
{
    const char* mFilters[MAX_FILTERS];
    int         mFiltersCount;
    const char* mZipFileName;
};

typedef enum PackageOutputPlatform
{
    PACKAGE_PLATFORM_UNDEFINED,
    PACKAGE_PLATFORM_CONSOLE_BEGIN,
    PACKAGE_PLATFORM_XBONE = PACKAGE_PLATFORM_CONSOLE_BEGIN,
    PACKAGE_PLATFORM_SCARLETT,
    PACKAGE_PLATFORM_ORBIS,
    PACKAGE_PLATFORM_PROSPERO,
    PACKAGE_PLATFORM_SWITCH,
    PACKAGE_PLATFORM_CONSOLE_END = PACKAGE_PLATFORM_SWITCH,
    PACKAGE_PLATFORM_MOBILE_BEGIN,
    PACKAGE_PLATFORM_ANDROID = PACKAGE_PLATFORM_MOBILE_BEGIN,
    PACKAGE_PLATFORM_IOS,
    PACKAGE_PLATFORM_QUEST,
    PACKAGE_PLATFORM_MOBILE_END = PACKAGE_PLATFORM_QUEST,
    PACKAGE_PLATFORM_STEAM_DECK,
    PACKAGE_PLATFORM_PC,
    PACKAGE_PLATFORM_MACOS,
    PACKAGE_PLATFORM_COUNT,
} PackageOutputPlatform;

typedef struct ProcessPackageParams
{
    const char*           outputPackageName;
    PackageOutputPlatform outputPlatform;
    bool                  generateAtlas;
} ProcessPackageParams;

struct RuntimeAnimationSettings
{
    // Resource dir where all animation/skeleton (.ozz assets) will be outputed to
    TFResourceDirectory mSkeletonAndAnimOutRd = TF_RD_MIDDLEWARE_2;

    // Enabling this might introduce noise to the animation
    bool mOptimizeTracks = false;

    // ozz defaults, 1mm tolerance and 10cm distance
    float mOptimizationTolerance = 1e-3f;
    float mOptimizationDistance = 1e-1f;

    // TODO: Add settings per joints, we might want root joint to have less optimization because it affects all the joints of the skeleton
    // and everything might look shaky
    //       See joints_setting_override variable in ozz::animation::offline::AnimationOptimizer for more information on this
};

struct ProcessAnimationsParams
{
    SkeletonAndAnimations*   pSkeletonAndAnims = nullptr;
    RuntimeAnimationSettings mAnimationSettings; // Global settings applied to all animations
};

// Callback used to provide the user with the 'extras' in each cgltf_mesh node of the gltf file.
// This allows the user to read custom data that might be stored in this field.
//     - jsonDataLenght is inclusive, you have to read from indexes to jsonData [0, jsonDataLength]
typedef void (*GLTFReadExtrasCallback)(uint32_t meshIdx, uint32_t meshCount, const char* jsonData, const uint32_t jsonDataLength,
                                       void* pUserData);

// Callback used to let the user write into the binary mesh asset any custom fields/flags that were imported
// from the gltf file.
// This callback is invoked twice:
//     1. pDst being nullptr: the user should just write the size it requires to write all data into pOutSize.
//     2. pDst being a valid pointer: the user should serialize the data into pDst, the buffer will be at least
//        as big as the value assigned to pOutSize in the first call to this callback.
typedef void (*MeshWriteExtrasCallback)(uint32_t* pOutSize, void* pDst, void* pUserData);

typedef enum MeshOptimizerFlags
{
    MESH_OPTIMIZATION_FLAG_OFF = 0x0,
    /// Vertex cache optimization
    MESH_OPTIMIZATION_FLAG_VERTEXCACHE = 0x1,
    /// Overdraw optimization
    MESH_OPTIMIZATION_FLAG_OVERDRAW = 0x2,
    /// Vertex fetch optimization
    MESH_OPTIMIZATION_FLAG_VERTEXFETCH = 0x4,
    /// All
    MESH_OPTIMIZATION_FLAG_ALL = 0x7,
} MeshOptimizerFlags;
MAKE_ENUM_FLAG(uint32_t, MeshOptimizerFlags)

struct PackagingData
{
    struct TFPackageDesc*         pPackageMetadata; // Single Output geometry metadata after processing the input assets for packaging
    struct TFTexturePackagedDesc* pTextureMetadata; // List of texture metadata after processing the input assets required for packaging
};

// Used when discovering animations
struct OnDiscoverAnimationsParam
{
    SkeletonAndAnimations* pSkeletonAndAnimations = nullptr;
};

struct ProcessGLTFParams
{ //-V802
    TFVertexLayout* pVertexLayout;
    bool mIgnoreMissingAttributes; // Set to false if asset processing should fail if any attribute specified in pVertexLayout is not
                                   // present in the gltf file
    bool mProcessMeshlets;
    int  mNumMaxVertices;
    int  mNumMaxTriangles;
    MeshOptimizerFlags mOptimizationFlags;

    // Callbacks to process custom data fields in the gltf file
    // (fields custom to a project or generated by a custom tool/plugin)
    GLTFReadExtrasCallback  pReadExtrasCallback;
    MeshWriteExtrasCallback pWriteExtrasCallback;
    void*                   pCallbackUserData; // User data passed to all the callbacks in this structure

    bool          mProcessForPackaging; // Set to true for additional steps to prepare for packaging
    PackagingData outPackagingData;     // Output from processing to prepare for packaging
};

void CheckIfGltfHasAnimations(TFResourceDirectory resourceDirInput, const char* gltfFileName, uint32_t& numAnimationsOut);

bool ProcessAnimations(AssetPipelineParams* assetParams, ProcessAnimationsParams* pProcessAnimationsParams);
void ReleaseSkeletonAndAnimationParams(SkeletonAndAnimations* pStbdsArray);

void OnDiscoverAnimation(TFResourceDirectory resourceDir, const char* filename, void* pUserData);

void CreateDirectoryForFile(TFResourceDirectory resourceDir, const char* filename);

typedef void (*OnFind)(TFResourceDirectory resourceDir, const char* fileName, void* pUserData);
void DirectorySearch(TFResourceDirectory resourceDir, const char* subDir, const char* ext, OnFind onFindCallback, void* pUserData,
                     bool recursive);

bool ProcessTFX(AssetPipelineParams* assetParams, ProcessTressFXParams* tfxParams);
bool ProcessGLTF(AssetPipelineParams* assetParams, ProcessGLTFParams* glTFParams);
bool ProcessTextures(AssetPipelineParams* assetParams, ProcessTexturesParams* texturesParams);
bool ProcessIES(AssetPipelineParams* assetParams, TextureContainer outContainer);
bool ProcessFonts(AssetPipelineParams* assetParams, ProcessFontsParams fontParams);
bool WriteZip(AssetPipelineParams* assetParams, WriteZipParams* zipParams);
bool ZipAllAssets(AssetPipelineParams* assetParams, WriteZipParams* zipParams);
bool ProcessPackaging(AssetPipelineParams* assetParams, ProcessPackageParams* packageParams);

bool InitPackageMetadataForPackage(uint32_t processedFileCount, struct TFPackageDesc* outPackageMetadata);
bool CollectGeoNameForPackage(const char* processedGltfName, struct TFPackageDesc* outPackageMetadata);
bool InitTextureMetadataForPackage(struct cgltf_data* pData, struct TFTexturePackagedDesc* outTextureMetadata);
bool CollectTextureIndicesForPackage(struct cgltf_data* pData, uint32_t meshIndex, uint32_t primitiveIndex,
                                     struct TFTexturePackagedDesc* outTextureMetadata);
bool GenerateStreamZonesForPackage(struct cgltf_data* pData, uint32_t meshIndex, uint32_t primitiveIndex,
                                   struct TFTexturePackagedDesc* outTextureMetadata);

// Error code 0 means success, see AssetPipelineErrorCode for other codes
int AssetPipelineRun(AssetPipelineParams* assetParams);
