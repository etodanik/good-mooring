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

#include "../../../OS/Interfaces/IOperatingSystem.h"
#include "../../../Utilities/Interfaces/IFileSystem.h"
#include "../../../Resources/ResourceLoader/Interfaces/IResourceLoader.h"

#include "AssetPipeline.h"

#include "../../../Utilities/ThirdParty/OpenSource/Nothings/stb_ds.h"
#include "../../../Resources/ResourceLoader/ThirdParty/OpenSource/cgltf/cgltf.h"
#include "../../BunyArchive/Buny.h"

#include "../../../Resources/ResourceLoader/ThirdParty/OpenSource/tinyimageformat/tinyimageformat_query.h"

#define FILE_NAME_LEN     64
#define MAX_PACKAGE_LIMIT 512
static const TFResourceDirectory INTERMEDIATE_RD = TF_RD_MIDDLEWARE_4;

bool PackagingCleanup(PackagingData* pPackagingData, size_t gltfCount);
bool StorePackageMetadata(TFResourceDirectory outRD, const char* outFileName, TFPackageDesc* pPackageMetadata);

bool StoreAtlasData(TFResourceDirectory outRD, const char* outFileName, const TFAtlasTextureDesc* atlasDesc,
                    const TFAtlasPlacement* atlasPlacements);
bool StoreTexureMetadata(TFResourceDirectory outRD, const char* outFileName, TFTexturePackagedDesc* pTextureMetadata);

bool PackageAssets(const char filesToPackage[][FILE_NAME_LEN], size_t filesToPackageCount, TFResourceDirectory outputResourceDirectory,
                   const char* packageName);
bool ValidatePackage(TFResourceDirectory outputResourceDirectory, const char* packageName, const char filesToPackage[][FILE_NAME_LEN],
                     size_t filesToPackageCount);
PackagingData ProcessMeshesForPackaging(TFResourceDirectory inputDir, TFResourceDirectory outputDir, const char* inputPath,
                                        char (*filesToPackage)[FILE_NAME_LEN], size_t* packagedFilesCount, size_t prevTexBundleCount);
void          AppendFullPackagingData(PackagingData* fullPackageMetadata, PackagingData* appendedPackageMetadata);

/*
 * Function to generate a complete package.
 * The output package will contain :
 *  - vertex/index buffer binary data
 *  - texture metadata.
 *  - texture data.
 * The input directory should follow this directory structure:
 * /<sectorName>
 *  - /Mesh
 *    - /<mesh_name>.gltf
 *    - /<mesh_name>.bin
 *  - /Textures
 *    - /<mesh_name>
 *       - /<texture_name>.input_tex_format
 */
bool ProcessPackaging(AssetPipelineParams* assetParams, ProcessPackageParams* packageParams)
{
    bool status = true;

    if (packageParams->outputPlatform == PACKAGE_PLATFORM_UNDEFINED)
    {
        LOGF(LogLevel::eERROR, "Invalid package platform.");
        return false;
    }

    if (assetParams->mPathMode == PROCESS_MODE_FILE)
    {
        LOGF(LogLevel::eERROR, "Invalid parameter PROCESS_MODE_FILE for packaging");
        return false;
    }

    if (packageParams->outputPackageName == NULL)
    {
        LOGF(LogLevel::eERROR, "No output package name found. Supply the --package-name argument");
        return false;
    }

    char filesToPackage[MAX_PACKAGE_LIMIT][FILE_NAME_LEN] = { 0 };

    // Reserve the first NodeIndex for the packageMetadata blob
    size_t       filesToPackageCount = 1;
    const size_t packageMetadataNodeIndex = 0;

    // ---------------- Setup the Resource Directories ----------------------------
    char tempPath[TF_FS_MAX_PATH] = { 0 };

    // Create the intermediate directory which will store all the processed assets used as the source for the package
    // Set the Resource Directory for the intermediate dir

    const char* outDir = fsGetResourceDirectory(assetParams->mRDOutput);
    size_t      pathLen = fsNormalizePath(outDir, '/', tempPath);
    if (tempPath[pathLen - 1] != '/')
    {
        tempPath[pathLen] = '/';
    }

    char interimDirPath[TF_FS_MAX_PATH] = { 0 };
    int  snprintfResult = snprintf(interimDirPath, TF_FS_MAX_PATH, "%sIntermediate/", tempPath); //-V541
    if (snprintfResult < 0)
    {
        LOGF(LogLevel::eWARNING, "Path truncated");
    }

    fsSetPathForResourceDir(pSystemFileIO, INTERMEDIATE_RD, interimDirPath);
    bool dirExists = fsCreateDirectory(INTERMEDIATE_RD, "", true);
    if (!dirExists)
    {
        LOGF(LogLevel::eERROR, "Unable the create intermediate directory.");
        return false;
    }

    memset(tempPath, 0, sizeof(char) * TF_FS_MAX_PATH);

    // Set the TFResourceDirectory for the input directory containing the meshes and verify it exists
    TFResourceDirectory INPUT_MESHES_RD = TF_RD_MIDDLEWARE_5;
    pathLen = fsNormalizePath(assetParams->mInDir, '/', tempPath);
    if (tempPath[pathLen - 1] != '/')
    {
        tempPath[pathLen] = '/';
    }

    char meshDirPath[TF_FS_MAX_PATH + 5] = { 0 };
    snprintfResult = snprintf(meshDirPath, TF_FS_MAX_PATH + 5, "%sMesh/", tempPath); //-V541
    if (snprintfResult < 0)
    {
        LOGF(LogLevel::eWARNING, "Path truncated");
    }

    fsSetPathForResourceDir(pSystemFileIO, INPUT_MESHES_RD, meshDirPath);
    bool exists, isDir, isFile;
    fsCheckPath(INPUT_MESHES_RD, "", &exists, &isDir, &isFile);
    if (!exists)
    {
        LOGF(LogLevel::eERROR, "The Mesh Dir doesn't exist");
        return false;
    }

    memset(tempPath, 0, sizeof(char) * TF_FS_MAX_PATH);

    // The RD used for textures will be set for each mesh that has a set of textures
    TFResourceDirectory INPUT_TEXTURES_RD = TF_RD_MIDDLEWARE_6;

    // Set the Resource Directory for the input directory containing the animations and verify it exists.
    TFResourceDirectory INPUT_ANIMATIONS_RD = TF_RD_MIDDLEWARE_7;
    pathLen = fsNormalizePath(assetParams->mInDir, '/', tempPath);
    if (tempPath[pathLen - 1] != '/')
    {
        tempPath[pathLen] = '/';
    }

    char animsDirPath[TF_FS_MAX_PATH] = { 0 };
    snprintfResult = snprintf(animsDirPath, TF_FS_MAX_PATH, "%sAnimations/", tempPath); //-V541
    if (snprintfResult < 0)
    {
        LOGF(LogLevel::eWARNING, "Path truncated");
    }

    fsSetPathForResourceDir(pSystemFileIO, INPUT_ANIMATIONS_RD, animsDirPath);
    fsCheckPath(INPUT_ANIMATIONS_RD, "", &exists, &isDir, &isFile);
    bool processAnimations = true;
    if (!exists)
    {
        LOGF(LogLevel::eINFO, "The Animations Dir doesn't exist. Skipping animation processing");
        processAnimations = false;
    }

    //==========================================================================

    LOGF(LogLevel::eINFO, "--- Process Static Meshes for packaging ---");
    PackagingData fullPackagingData =
        ProcessMeshesForPackaging(INPUT_MESHES_RD, INTERMEDIATE_RD, meshDirPath, filesToPackage, &filesToPackageCount, 0);
    size_t totalBaseTexCount = 0;
    size_t totalBaseGeoCount = fullPackagingData.pPackageMetadata->mGeoCount;
    for (uint32_t geoIndex = 0; geoIndex < totalBaseGeoCount; geoIndex++)
    {
        totalBaseTexCount += fullPackagingData.pTextureMetadata[geoIndex].mTextureCount;
    }

    if (processAnimations)
    {
        // Next process the animations and the meshes used in the animations
        LOGF(LogLevel::eINFO, "--- Process Animations for packaging ---");
        OnDiscoverAnimationsParam discoveredAnimations = {};
        AssetPipelineParams       animationPipelineParams = {};
        animationPipelineParams.mSettings.force = true;
        animationPipelineParams.mSettings.quiet = false;
        animationPipelineParams.mInFilePath = "";
        animationPipelineParams.mInExt = "";
        animationPipelineParams.mFlagsCount = 0;
        animationPipelineParams.mPathMode = PROCESS_MODE_DIRECTORY;

        animationPipelineParams.mRDInput = INPUT_ANIMATIONS_RD;
        animationPipelineParams.mRDOutput = INTERMEDIATE_RD;
        DirectorySearch(animationPipelineParams.mRDInput, NULL, "gltf", OnDiscoverAnimation, (void*)&discoveredAnimations, false);

        ProcessAnimationsParams processAnimationParams = {};
        processAnimationParams.pSkeletonAndAnims = discoveredAnimations.pSkeletonAndAnimations;
        processAnimationParams.mAnimationSettings.mSkeletonAndAnimOutRd = INTERMEDIATE_RD;
        // TODO: Add a packaging param for this flag?
        processAnimationParams.mAnimationSettings.mOptimizeTracks = false;
        bool   animsError = ProcessAnimations(&animationPipelineParams, &processAnimationParams);
        size_t meshesWithAnimationCount = arrlen(processAnimationParams.pSkeletonAndAnims);

        if (meshesWithAnimationCount > 0)
        {
            if (animsError)
            {
                LOGF(LogLevel::eERROR, "--- Processing animations for packaging failed ---");
                PackagingCleanup(&fullPackagingData, totalBaseGeoCount);
                return false;
            }

            PackagingData animationPackagingData = ProcessMeshesForPackaging(INPUT_ANIMATIONS_RD, INTERMEDIATE_RD, interimDirPath,
                                                                             filesToPackage, &filesToPackageCount, totalBaseTexCount);

            ASSERT(meshesWithAnimationCount == animationPackagingData.pPackageMetadata->mGeoCount);

            // Update the fullPackagingData with the newly processed animation packaging data
            AppendFullPackagingData(&fullPackagingData, &animationPackagingData);
        }

        memset(fullPackagingData.pPackageMetadata->pAnimationNodeIDRanges, 0,
               fullPackagingData.pPackageMetadata->mGeoCount * sizeof(TFAnimationNodeIDRange));
        for (uint32_t meshIndex = 0, meshCount = (uint32_t)meshesWithAnimationCount; meshIndex < meshCount; ++meshIndex)
        {
            const SkeletonAndAnimations* skeletonAndAnims = &processAnimationParams.pSkeletonAndAnims[meshIndex];

            // Save the file names of the skeleton and animation files to be packaged later
            const char* skeletonOutputFile = (char*)skeletonAndAnims->mSkeletonOutFile.data;
            fullPackagingData.pPackageMetadata->pAnimationNodeIDRanges[totalBaseGeoCount + meshIndex].mStartNodeId = filesToPackageCount;
            fullPackagingData.pPackageMetadata->pAnimationNodeIDRanges[totalBaseGeoCount + meshIndex].mAnimationCount++;
            strcpy(filesToPackage[filesToPackageCount++], skeletonOutputFile);

            for (uint32_t a = 0, animEnd = (uint32_t)arrlen(skeletonAndAnims->mAnimations); a < animEnd; ++a)
            {
                const SkeletonAndAnimations::AnimationFile* anim = &skeletonAndAnims->mAnimations[a];
                const char*                                 animOutputPath = (char*)anim->mOutputAnimPath.data;

                if (animOutputPath && animOutputPath[0] != '\0')
                {
                    strcpy(filesToPackage[filesToPackageCount++], animOutputPath);
                }
                else
                {
                    const char* animInputFile = (char*)anim->mInputAnim.data;
                    char        inAnimFilename[FILE_NAME_LEN] = {};
                    fsGetPathFileName(animInputFile, inAnimFilename);
                    snprintfResult = snprintf(filesToPackage[filesToPackageCount++], FILE_NAME_LEN, "%s.ozz", inAnimFilename);
                    if (snprintfResult < 0)
                    {
                        LOGF(LogLevel::eWARNING, "Path truncated");
                    }
                }
                fullPackagingData.pPackageMetadata->pAnimationNodeIDRanges[totalBaseGeoCount + meshIndex].mAnimationCount++;
            }
        }
        ReleaseSkeletonAndAnimationParams(discoveredAnimations.pSkeletonAndAnimations);
    }

    bool                  bCreateAtlasData = packageParams->generateAtlas;
    TFAtlasPlacement*     atlasPlacements = NULL;
    ProcessedTextureData* pProcessedTextureData = NULL;
    // Finally process the textures. Each processed GLTF will have one texture set.
    size_t                currNameIndex = 0;
    size_t                currTextureIndex = 0;

    for (uint32_t geoIndex = 0; geoIndex < fullPackagingData.pPackageMetadata->mGeoCount; ++geoIndex)
    {
        const char* processedFileName = fullPackagingData.pPackageMetadata->pGeoNames + currNameIndex;
        size_t      nameLen = strlen(processedFileName) + 1;
        currNameIndex += nameLen;
        LOGF(LogLevel::eINFO, "Processing Textures for GLTF File : %s", processedFileName);

        char texSourceDir[FILE_NAME_LEN] = { 0 };
        strncpy(texSourceDir, processedFileName, nameLen - 5); // remove the .bin extension

        AssetPipelineParams texProcessParams = {};
        texProcessParams.mSettings.force = true;
        texProcessParams.mSettings.quiet = false;
        texProcessParams.mInFilePath = "";
        texProcessParams.mInExt = "";
        texProcessParams.mFlagsCount = 0;
        texProcessParams.mPathMode = PROCESS_MODE_DIRECTORY;

        // RD_1,2 and 3 are being used by AssetpipelineCmd
        texProcessParams.mRDOutput = INTERMEDIATE_RD; // The intermediate dir as already been set on this RD
        texProcessParams.mRDInput = INPUT_TEXTURES_RD;

        memset(tempPath, 0, sizeof(char) * TF_FS_MAX_PATH);

        pathLen = fsNormalizePath(assetParams->mInDir, '/', tempPath);
        if (tempPath[pathLen - 1] != '/')
        {
            tempPath[pathLen] = '/';
        }

        char texDirPath[TF_FS_MAX_PATH] = { 0 };
        snprintfResult = snprintf(texDirPath, TF_FS_MAX_PATH, "%sTextures/%s/", tempPath, texSourceDir); //-V541
        if (snprintfResult < 0)
        {
            LOGF(LogLevel::eWARNING, "Path truncated");
        }
        texProcessParams.mInDir = texDirPath;

        // Set input and ouput for the mesh processing step
        fsSetPathForResourceDir(pSystemFileIO, texProcessParams.mRDInput, texDirPath);
        fsCheckPath(texProcessParams.mRDInput, "", &exists, &isDir, &isFile);
        if (!exists)
        {
            LOGF(LogLevel::eERROR, "The Texture Dir %s doesn't exist", texDirPath);
            status = false;
            break;
        }

        ProcessTexturesParams texturesParams = {};
        texturesParams.mInExt = assetParams->mInExt;
        switch (packageParams->outputPlatform)
        {
        case PACKAGE_PLATFORM_ANDROID:
        case PACKAGE_PLATFORM_IOS:
        case PACKAGE_PLATFORM_MACOS:
        case PACKAGE_PLATFORM_QUEST:
        case PACKAGE_PLATFORM_SWITCH:
        case PACKAGE_PLATFORM_STEAM_DECK:
            texturesParams.mContainer = CONTAINER_KTX;
            texturesParams.mCompression = COMPRESSION_ASTC;
            break;
        case PACKAGE_PLATFORM_PC:
            texturesParams.mContainer = CONTAINER_DDS;
            texturesParams.mCompression = COMPRESSION_BC;
            break;
#ifdef XBOX_SCARLETT_DDS
        case PACKAGE_PLATFORM_XBONE:
        case PACKAGE_PLATFORM_SCARLETT:
            texturesParams.mContainer = CONTAINER_SCARLETT_DDS;
            texturesParams.mCompression = COMPRESSION_BC;
            break;
#endif // XBOX_SCARLETT_DDS
#ifdef PROSPERO_GNF
        case PACKAGE_PLATFORM_ORBIS:
            texturesParams.mContainer = CONTAINER_GNF_ORBIS;
            texturesParams.mCompression = COMPRESSION_BC;
            break;
        case PACKAGE_PLATFORM_PROSPERO:
            texturesParams.mContainer = CONTAINER_GNF_PROSPERO;
            texturesParams.mCompression = COMPRESSION_BC;
            break;
#endif // PROSPERO_GNF
        default:
            ASSERT(false);
            break;
        }
        texturesParams.mGenerateMipmaps = MIPMAP_DEFAULT;
        texturesParams.mInputLinearColorSpace = false;
        texturesParams.mSwizzle = { 'r', 'g', 'b', 'a' };
        texturesParams.mSwizzleChannelCount = 0;

        TFTexturePackagedDesc* pTextureMetadata = &fullPackagingData.pTextureMetadata[geoIndex];
        texturesParams.mProcessForPackaging = true;
        texturesParams.mProcessForAtlas = packageParams->generateAtlas;
        texturesParams.mTextureNameCount = pTextureMetadata->mTextureCount;
        texturesParams.pInputFileNameList = pTextureMetadata->pTextureNames;

        texturesParams.ppOutProcessedTextureData = &pProcessedTextureData;

        bool errorOccured = ProcessTextures(&texProcessParams, &texturesParams);

        if (!errorOccured && pProcessedTextureData)
        {
            size_t nameIndex = 0;
            // Save the processed texturenames to be packaged
            fullPackagingData.pPackageMetadata->pTextureStartNodeIds[geoIndex] = filesToPackageCount;
            for (uint32_t textureIndex = 0; textureIndex < pTextureMetadata->mTextureCount; textureIndex++)
            {
                const char* texName = pTextureMetadata->pTextureNames + nameIndex;
                size_t      texNameLength = strlen(texName) + 1;
                ASSERT(texNameLength < FILE_NAME_LEN);
                char* texNameToPackage = filesToPackage[filesToPackageCount++];
                char  tempName[FILE_NAME_LEN] = { '\0' };
                strncpy(tempName, texNameToPackage, FILE_NAME_LEN - 1);
                // Replace the original extension with the processed .tex extension
                fsGetPathFileName(texName, tempName);
                snprintfResult = snprintf(texNameToPackage, FILE_NAME_LEN, "%s.tex", tempName); //-V541
                if (snprintfResult < 0)
                {
                    LOGF(LogLevel::eWARNING, "Path truncated");
                }
                nameIndex += texNameLength;

                pProcessedTextureData[currTextureIndex].mIndex = (uint32_t)currTextureIndex;
                currTextureIndex++;
            }
        }
    }

    TFAtlasTextureDesc* aAtlasTextures = NULL;

    if (bCreateAtlasData && pProcessedTextureData)
    {
        int (*findAtlasIndex)(const TFAtlasTextureDesc*, uint32_t, uint32_t, TinyImageFormat) =
            [](const TFAtlasTextureDesc* atlases, uint32_t width, uint32_t height, TinyImageFormat fmt) -> int
        {
            for (int i = 0; i < arrlen(atlases); ++i)
            {
                const TFAtlasTextureDesc* pCurr = atlases + i;

                if (pCurr->mWidth == width && pCurr->mHeight == height && pCurr->mFormat == fmt)
                    return i;
            }
            return -1;
        };

        /// atlas slice size should be the first one.

        // now with these sorted, we need to know the unique sizes of texture, and how much of each we have
        TFAtlasTextureDesc* pCurrentAtlas = NULL;

        // collecting information of how many texture of each resolution we have
        for (uint32_t i = 0; i < arrlen(pProcessedTextureData); i++)
        {
            ProcessedTextureData* pCurrTexture = pProcessedTextureData + i;

            int atlasIndex =
                findAtlasIndex(aAtlasTextures, pCurrTexture->mWidth, pCurrTexture->mHeight, (TinyImageFormat)pCurrTexture->mFormat);
            bool needNewAtlas = atlasIndex == -1;

            if (needNewAtlas)
            {
                atlasIndex = (int)arrlen(aAtlasTextures);
                TFAtlasTextureDesc newAtlas = {};
                newAtlas.mWidth = pCurrTexture->mWidth;
                newAtlas.mHeight = pCurrTexture->mHeight;
                newAtlas.mSliceCount = 0;
                newAtlas.mFormat = (TinyImageFormat)pCurrTexture->mFormat;
                newAtlas.mMipLevels = pCurrTexture->mMipLevels;
                arrpush(aAtlasTextures, newAtlas);
            }

            pCurrentAtlas = aAtlasTextures + atlasIndex; //-V769

            TFAtlasPlacement atlasPlacement = {};
            // store atlas index and slice together
            atlasPlacement.mAtlasIndex = atlasIndex;
            atlasPlacement.mAtlasSlice = pCurrentAtlas->mSliceCount;
            atlasPlacement.mTextureIndex = pCurrTexture->mIndex;
            atlasPlacement.mFlags = TF_ATLAS_PLACEMENT_DEFAULT;

            pCurrentAtlas->mSliceCount++;
            arrpush(atlasPlacements, atlasPlacement);
        }

        // reorder them by texture index so they are ready to be serialized in the package
        std::qsort(atlasPlacements, arrlen(atlasPlacements), sizeof(TFAtlasPlacement),
                   [](const void* a, const void* b)
                   {
                       const TFAtlasPlacement* placementA = static_cast<const TFAtlasPlacement*>(a);
                       const TFAtlasPlacement* placementB = static_cast<const TFAtlasPlacement*>(b);

                       uint32_t indexA = placementA->mTextureIndex;
                       uint32_t indexB = placementB->mTextureIndex;
                       if (indexA < indexB)
                           return -1;
                       else if (indexA > indexB)
                           return 1;

                       return 0;
                   });

        // mark last placement in each atlas
        for (uint32_t i = 0; i < arrlen(atlasPlacements); i++)
        {
            TFAtlasPlacement*   currPlacement = atlasPlacements + i;
            uint32_t            atlasIndex = currPlacement->mAtlasIndex;
            uint32_t            sliceIndex = currPlacement->mAtlasSlice;
            TFAtlasTextureDesc* currAtlas = aAtlasTextures + atlasIndex;
            if (sliceIndex == currAtlas->mSliceCount - 1)
            {
                currPlacement->mFlags = TF_ATLAS_PLACEMENT_LAST_PLACEMENT;
            }
        }

        int (*findAtlasPlacement)(const TFAtlasPlacement*, uint32_t) = [](const TFAtlasPlacement* placements, uint32_t textureIndex) -> int
        {
            for (int i = 0; i < arrlen(placements); ++i)
            {
                const TFAtlasPlacement* pCurr = placements + i;

                if (pCurr->mTextureIndex == textureIndex)
                    return i;
            }
            return -1;
        };

        // calculate placements which would be used in shaders.
        for (uint32_t geoIndex = 0; geoIndex < fullPackagingData.pPackageMetadata->mGeoCount; ++geoIndex)
        {
            uint32_t meshCount = (uint32_t)fullPackagingData.pTextureMetadata[geoIndex].mMeshCount;
            for (uint32_t meshIndex = 0; meshIndex < meshCount; meshIndex++)
            {
                TFTextureBundleID texBundleID = fullPackagingData.pTextureMetadata[geoIndex].pTextureBundleIDs[meshIndex];

                TFTextureID colorTexID = GET_COLOR_TEXID(texBundleID);
                TFTextureID normTexID = GET_NORMAL_TEXID(texBundleID);
                TFTextureID specTexID = GET_SPEC_TEXID(texBundleID);

                for (uint32_t textureType = 0; textureType < 3; textureType++)
                {
                    uint32_t texIndex = 0;

                    if (textureType == 0)
                    {
                        texIndex = colorTexID;
                    }
                    else if (textureType == 1)
                    {
                        texIndex = normTexID;
                    }
                    else
                    {
                        texIndex = specTexID;
                    }

                    int placementIndex = findAtlasPlacement(atlasPlacements, texIndex);

                    ASSERT(placementIndex >= 0);
                    TFAtlasPlacement atlasPlacement = atlasPlacements[placementIndex]; //-V595
                    arrpush(atlasPlacements, atlasPlacement);
                }
            }
        }
    }

    for (uint32_t i = 0; i < arrlen(pProcessedTextureData); i++)
    {
        bdestroy(&pProcessedTextureData[i].mOutputFilePath);
    }
    arrfree(pProcessedTextureData);

    if (bCreateAtlasData)
    {
        fullPackagingData.pPackageMetadata->mTextureAtlasNodeID = filesToPackageCount;
        const char atlasTexturesName[] = "AtlasTextures";
        StoreAtlasData(INTERMEDIATE_RD, atlasTexturesName, aAtlasTextures, atlasPlacements);
        strcpy(filesToPackage[filesToPackageCount], atlasTexturesName);
        filesToPackageCount++;
    }
    arrfree(aAtlasTextures);
    arrfree(atlasPlacements);

    const char packageMetadataName[] = "packageMetadata";
    StorePackageMetadata(INTERMEDIATE_RD, packageMetadataName, fullPackagingData.pPackageMetadata);
    // The packagemetadata will be available in the first node
    strcpy(filesToPackage[packageMetadataNodeIndex], packageMetadataName);

    // Package all of the processed source assets
    ASSERT(filesToPackageCount < MAX_PACKAGE_LIMIT);
    if (status)
    {
        if (PackageAssets(filesToPackage, filesToPackageCount, assetParams->mRDOutput, packageParams->outputPackageName))
        {
            status = ValidatePackage(assetParams->mRDOutput, packageParams->outputPackageName, filesToPackage, filesToPackageCount);
        }
    }

    PackagingCleanup(&fullPackagingData, fullPackagingData.pPackageMetadata->mGeoCount);

    return status;
}

/*
 * Initializes the TFPackageDesc using the gltf data
 * - Sets the geometry count
 * - Initializes the list of NodeIds.
 */
bool InitPackageMetadataForPackage(uint32_t processedFileCount, TFPackageDesc* outPackageMetadata)
{
    ASSERT(outPackageMetadata != NULL);

    snprintf(outPackageMetadata->mHeaderStr, TF_PAK_HEADER_SIZE, "%s", TF_PAK_HEADER_STR);
    outPackageMetadata->mGeoCount = processedFileCount;
    outPackageMetadata->pGeoNodeIds = (size_t*)tf_calloc(processedFileCount, sizeof(size_t));
    outPackageMetadata->pTexMetadataNodeIds = (size_t*)tf_calloc(processedFileCount, sizeof(size_t));
    outPackageMetadata->pAnimationNodeIDRanges = (TFAnimationNodeIDRange*)tf_calloc(processedFileCount, sizeof(TFAnimationNodeIDRange));
    outPackageMetadata->pTextureStartNodeIds = (size_t*)tf_calloc(processedFileCount, sizeof(size_t));
    outPackageMetadata->mNameBufferSize = 0;
    outPackageMetadata->pGeoNames = NULL;

    outPackageMetadata->mTextureAtlasNodeID = 0;

    return true;
}

/*
 * Collect the processedFileName and store it in the PackageMetadata blob
 */
bool CollectGeoNameForPackage(const char* processedFileName, TFPackageDesc* outPackageMetadata)
{
    ASSERT(outPackageMetadata != NULL);
    size_t currentNameOffset = outPackageMetadata->mNameBufferSize;
    size_t fileNameLen = strlen(processedFileName) + 1;
    outPackageMetadata->pGeoNames = (char*)tf_realloc(outPackageMetadata->pGeoNames, currentNameOffset + fileNameLen * sizeof(char));
    char* currentGeoName = outPackageMetadata->pGeoNames + currentNameOffset;
    strncpy(currentGeoName, processedFileName, fileNameLen);
    outPackageMetadata->mNameBufferSize = currentNameOffset + fileNameLen;

    return true;
}

/*
 * Initiatlizes the TFTexturePackagedDesc using the gltf data
 * - Sets the texture count and allocates buffer for the texture names
 * - Sets the mesh count and allocates buffer for the textureIDs
 */
bool InitTextureMetadataForPackage(cgltf_data* pData, TFTexturePackagedDesc* outTextureMetadata)
{
    ASSERT(outTextureMetadata != NULL);

    // Get all image file names
    outTextureMetadata->mTextureCount = pData->images_count;
    ASSERTMSG(outTextureMetadata->mTextureCount <= MAX_TEXID, "Exceeded max texture count");

    // Allocate buffer for texture names
    char*  currTextureName = NULL;
    size_t allTextureNamesLen = 0;
    // Get all the textures file names that will be required
    for (uint32_t imageIndex = 0; imageIndex < outTextureMetadata->mTextureCount; ++imageIndex)
    {
        const cgltf_image* img = &pData->images[imageIndex];
        size_t             texNameLen = strlen(img->uri) + 1; // Extra 1 for null character
        outTextureMetadata->pTextureNames =
            (char*)tf_realloc(outTextureMetadata->pTextureNames, sizeof(char) * (allTextureNamesLen + texNameLen));
        currTextureName = outTextureMetadata->pTextureNames + allTextureNamesLen;
        strncpy(currTextureName, img->uri, texNameLen);
        allTextureNamesLen += texNameLen;
        LOGF(LogLevel::eINFO, "The texture name at index %d is %s", imageIndex, currTextureName);
    }

    // Allocate buffer for textureIDs
    outTextureMetadata->mMeshCount = pData->meshes_count;
    outTextureMetadata->pTextureBundleIDs = (TFTextureBundleID*)tf_calloc(outTextureMetadata->mMeshCount, sizeof(TFTextureBundleID));
    outTextureMetadata->pMaterialProps = (TFMaterialProps*)tf_calloc(outTextureMetadata->mMeshCount, sizeof(TFMaterialProps));

    // Allocate buffer for StreamZones
    outTextureMetadata->pStreamZones = (TFStreamZone*)tf_calloc(outTextureMetadata->mMeshCount, sizeof(TFStreamZone));
    return true;
}

/*
 * Collect all 4 texture indices for a specific mesh.
 * Then pack it into a TFTextureBundleID along with the material flags.
 * Finally store it into the textureMetadata blob
 */
bool CollectTextureIndicesForPackage(cgltf_data* pData, uint32_t meshIndex, uint32_t primitiveIndex,
                                     TFTexturePackagedDesc* outTextureMetadata)
{
    ASSERT(outTextureMetadata != NULL);
    ASSERT(outTextureMetadata->pMaterialProps != NULL);
    ASSERT(outTextureMetadata->pTextureBundleIDs != NULL);

    // Collect the material data to get the TextureIDs
    TFTextureID baseTexIndex = TEX_IDX_INVALID;
    TFTextureID specTexIndex = TEX_IDX_INVALID;
    TFTextureID normTexIndex = TEX_IDX_INVALID;
    TFTextureID emisTexIndex = TEX_IDX_INVALID;

    float4 baseColor = BASE_COLOR_DEFAULT;
    float2 spec = SPEC_DEFAULT;
    float  emissiveStrength = 0.f;

    TFMaterialFlags matFlags = MATERIAL_FLAG_NONE;

    cgltf_material* pMaterial = pData->meshes[meshIndex].primitives[primitiveIndex].material;
    if (pMaterial->has_pbr_metallic_roughness)
    {
        if (pMaterial->pbr_metallic_roughness.base_color_texture.texture != NULL)
        {
            baseTexIndex = (TFTextureID)(pMaterial->pbr_metallic_roughness.base_color_texture.texture->image - pData->images);
            matFlags |= MATERIAL_FLAG_BASE_COLOR_TEX;
        }
        else
        {
            baseColor =
                float4(pMaterial->pbr_metallic_roughness.base_color_factor[0], pMaterial->pbr_metallic_roughness.base_color_factor[1],
                       pMaterial->pbr_metallic_roughness.base_color_factor[2], pMaterial->pbr_metallic_roughness.base_color_factor[3]);
        }

        if (pMaterial->pbr_metallic_roughness.metallic_roughness_texture.texture != NULL)
        {
            specTexIndex = (TFTextureID)(pMaterial->pbr_metallic_roughness.metallic_roughness_texture.texture->image - pData->images);
            matFlags |= MATERIAL_FLAG_SPEC_TEX;
        }
        else
        {
            spec = float2(pMaterial->pbr_metallic_roughness.roughness_factor, pMaterial->pbr_metallic_roughness.metallic_factor);
        }
    }

    if (pMaterial->normal_texture.texture != NULL)
    {
        normTexIndex = (TFTextureID)(pMaterial->normal_texture.texture->image - pData->images);
        matFlags |= MATERIAL_FLAG_NORMAL_TEX;
    }

    if (pMaterial->has_emissive_strength)
    {
        emissiveStrength = pMaterial->emissive_strength.emissive_strength;
        matFlags |= MATERIAL_FLAG_EMISSIVE;
    }

    LOGF(LogLevel::eINFO, "Texture IDs to bundle: base %d | spec %d | norm %d | emis %d |", baseTexIndex, specTexIndex, normTexIndex,
         emisTexIndex);

    switch (pMaterial->alpha_mode)
    {
    case cgltf_alpha_mode_blend:
        matFlags |= MATERIAL_FLAG_TRANSPARENT;
        break;
    case cgltf_alpha_mode_mask:
        matFlags |= MATERIAL_FLAG_ALPHA_TESTED;
        break;
    default:
        break;
    }

    if (pMaterial->double_sided)
    {
        matFlags |= MATERIAL_FLAG_TWO_SIDED;
    }

    outTextureMetadata->pTextureBundleIDs[meshIndex] = PackTextureBundle(baseTexIndex, specTexIndex, normTexIndex);
    outTextureMetadata->pMaterialProps[meshIndex] = PackMaterialProperties(baseColor, spec, emissiveStrength, matFlags);

    return true;
}

/*
 * Store the bounding box information for the streaming zone
 * This will be used for streaming textures during runtime.
 */
bool GenerateStreamZonesForPackage(struct cgltf_data* pData, uint32_t meshIndex, uint32_t primitiveIndex,
                                   struct TFTexturePackagedDesc* outTextureMetadata)
{
    ASSERT(outTextureMetadata != NULL);
    ASSERT(outTextureMetadata->pStreamZones != NULL);

    const cgltf_primitive* prim = &pData->meshes[meshIndex].primitives[primitiveIndex];
    TFStreamZone*          pStreamZone = &outTextureMetadata->pStreamZones[meshIndex];

    for (uint32_t attribIndex = 0; attribIndex < prim->attributes_count; attribIndex++)
    {
        if (prim->attributes[attribIndex].type == cgltf_attribute_type_position)
        {
            pStreamZone->mMin.x = prim->attributes->data->min[0];
            pStreamZone->mMin.y = prim->attributes->data->min[1];
            pStreamZone->mMin.z = prim->attributes->data->min[2];

            pStreamZone->mMax.x = prim->attributes->data->max[0];
            pStreamZone->mMax.y = prim->attributes->data->max[1];
            pStreamZone->mMax.z = prim->attributes->data->max[2];

            pStreamZone->mCenter = pStreamZone->mMin + ((pStreamZone->mMax - pStreamZone->mMin) * 0.5f);
            break;
        }
    }

    return true;
}

/*
 * Save the TFPackageDesc to be packaged.
 * Contains the number of processed geo binary files and the names
 */
bool StorePackageMetadata(TFResourceDirectory outRD, const char* outFileName, TFPackageDesc* pPackageMetadata)
{
    bool         status = true;
    TFFileStream fStream = {};
    if (!fsOpenStreamFromPath(outRD, outFileName, TF_FM_WRITE_ALLOW_READ, &fStream))
    {
        LOGF(eERROR, "Couldn't open file '%s' for write.", outFileName);
        status = false;
    }
    else
    {
        // Write out the PackageMetadata in the same format as the PackageMetadata struct
        size_t totalWriteSize = 0;
        totalWriteSize += fsWriteToStream(&fStream, &pPackageMetadata->mHeaderStr, TF_PAK_HEADER_SIZE * sizeof(char));
        totalWriteSize += fsWriteToStream(&fStream, &pPackageMetadata->mGeoCount, sizeof(size_t));
        totalWriteSize += fsWriteToStream(&fStream, pPackageMetadata->pGeoNodeIds, pPackageMetadata->mGeoCount * sizeof(size_t));
        totalWriteSize += fsWriteToStream(&fStream, pPackageMetadata->pTexMetadataNodeIds, pPackageMetadata->mGeoCount * sizeof(size_t));
        totalWriteSize += fsWriteToStream(&fStream, pPackageMetadata->pAnimationNodeIDRanges,
                                          pPackageMetadata->mGeoCount * sizeof(TFAnimationNodeIDRange));
        totalWriteSize += fsWriteToStream(&fStream, pPackageMetadata->pTextureStartNodeIds, pPackageMetadata->mGeoCount * sizeof(size_t));

        totalWriteSize += fsWriteToStream(&fStream, &pPackageMetadata->mTextureAtlasNodeID, sizeof(size_t));
        totalWriteSize += fsWriteToStream(&fStream, &pPackageMetadata->mNameBufferSize, sizeof(size_t));
        totalWriteSize += fsWriteToStream(&fStream, pPackageMetadata->pGeoNames, sizeof(char) * pPackageMetadata->mNameBufferSize);

        size_t intendedWriteSize = 0;
        intendedWriteSize += TF_PAK_HEADER_SIZE * sizeof(char);
        intendedWriteSize += sizeof(size_t);
        intendedWriteSize += pPackageMetadata->mGeoCount * sizeof(size_t);
        intendedWriteSize += pPackageMetadata->mGeoCount * sizeof(size_t);
        intendedWriteSize += pPackageMetadata->mGeoCount * sizeof(TFAnimationNodeIDRange);
        intendedWriteSize += pPackageMetadata->mGeoCount * sizeof(size_t);
        intendedWriteSize += sizeof(size_t);
        intendedWriteSize += sizeof(size_t);
        intendedWriteSize += sizeof(char) * pPackageMetadata->mNameBufferSize;

        if (totalWriteSize != intendedWriteSize)
        {
            LOGF(eERROR, "Error in writing out the PackageMetadata blob");
            status = false;
        }

        if (!fsCloseStream(&fStream))
        {
            LOGF(eERROR, "Failed to close write stream for file '%s'.", outFileName);
            status = false;
        }
    }

    return status;
}

/*
 * Save the packageMetadatablob to be packaged.
 * Contains the number of processed geo binary files and the names
 */
bool StoreAtlasData(TFResourceDirectory outRD, const char* outFileName, const TFAtlasTextureDesc* atlasDescriptors,
                    const TFAtlasPlacement* atlasPlacements)
{
    bool         status = true;
    TFFileStream fStream = {};
    if (!fsOpenStreamFromPath(outRD, outFileName, TF_FM_WRITE_ALLOW_READ, &fStream))
    {
        LOGF(eERROR, "Couldn't open file '%s' for write.", outFileName);
        status = false;
    }
    else
    {
        // Write out the number of atlased textures
        uint32_t placementsCount = (uint32_t)arrlen(atlasPlacements);
        uint32_t atlasCount = (uint32_t)arrlen(atlasDescriptors);
        size_t   expectedWriteSize =
            sizeof(uint32_t) + sizeof(uint32_t) + sizeof(TFAtlasTextureDesc) * atlasCount + sizeof(TFAtlasPlacement) * placementsCount;
        // write the full array
        size_t totalWriteSize = 0;
        totalWriteSize += fsWriteToStream(&fStream, &atlasCount, sizeof(uint32_t));
        totalWriteSize += fsWriteToStream(&fStream, atlasDescriptors, sizeof(TFAtlasTextureDesc) * atlasCount);
        totalWriteSize += fsWriteToStream(&fStream, &placementsCount, sizeof(uint32_t));
        totalWriteSize += fsWriteToStream(&fStream, atlasPlacements, sizeof(TFAtlasPlacement) * placementsCount);
        if (totalWriteSize != expectedWriteSize)
        {
            LOGF(eERROR, "Error in writing out Atlas textures blob");
            status = false;
        }

        if (!fsCloseStream(&fStream))
        {
            LOGF(eERROR, "Failed to close write stream for file '%s'.", outFileName);
            status = false;
        }
    }

    return status;
}

/*
 * Write out the texture metadata blob into a file
 * TextureMetadata contains:
 *   - number of textures
 *   - size of the textureNames buffer
 *   - textureNames for debugging
 *   - number of meshes that corresponds to the number of textureIDs in the buffer
 *   - buffer of textureIDs that can be used to directly index into the array of textures without requiring any texturename
 *   - a buffer of StreamZones that contain the bounding box data for the corresponding mesh
 * Each processed gltf will have an associated texture metadata file.
 */
bool StoreTexureMetadata(TFResourceDirectory outRD, const char* outFileName, TFTexturePackagedDesc* pTextureMetadata)
{
    bool         status = true;
    TFFileStream fStream = {};
    if (!fsOpenStreamFromPath(outRD, outFileName, TF_FM_WRITE_ALLOW_READ, &fStream))
    {
        LOGF(eERROR, "Couldn't open file '%s' for write.", outFileName);
        status = false;
    }
    else
    {
        // Write out the TextureMetadata in the same format as the TFTexturePackagedDesc struct
        size_t totalWriteSize = 0;
        totalWriteSize += fsWriteToStream(&fStream, &pTextureMetadata->mTextureCount, sizeof(size_t));

        // Remove the extensions from the textureNames before saving it in the file
        size_t         fileNameIndex = 0;
        const uint32_t maxFileNameLen = 128;
        char           fileNameWithoutExt[maxFileNameLen] = { 0 };
        char*          nameBuffer = NULL;
        size_t         totalBufferSize = 0;
        for (uint32_t textureIndex = 0; textureIndex < pTextureMetadata->mTextureCount; ++textureIndex)
        {
            const char* currentTexFileName = pTextureMetadata->pTextureNames + fileNameIndex;
            ASSERTMSG(strlen(currentTexFileName) < maxFileNameLen, "Texture name %s exceeded max name length %d", currentTexFileName,
                      maxFileNameLen);
            fsGetPathFileName(currentTexFileName, fileNameWithoutExt);
            size_t fileNameLen = strlen(fileNameWithoutExt) + 1;
            nameBuffer = (char*)tf_realloc(nameBuffer, totalBufferSize + fileNameLen);
            strncpy(nameBuffer + totalBufferSize, fileNameWithoutExt, fileNameLen);
            totalBufferSize += fileNameLen;
            fileNameIndex += strlen(currentTexFileName) + 1;
        }
        totalWriteSize += fsWriteToStream(&fStream, &totalBufferSize, sizeof(size_t));
        totalWriteSize += fsWriteToStream(&fStream, nameBuffer, sizeof(char) * totalBufferSize);
        totalWriteSize += fsWriteToStream(&fStream, &pTextureMetadata->mMeshCount, sizeof(size_t));
        totalWriteSize +=
            fsWriteToStream(&fStream, pTextureMetadata->pTextureBundleIDs, sizeof(TFTextureBundleID) * pTextureMetadata->mMeshCount);
        totalWriteSize +=
            fsWriteToStream(&fStream, pTextureMetadata->pMaterialProps, sizeof(TFMaterialProps) * pTextureMetadata->mMeshCount);
        totalWriteSize += fsWriteToStream(&fStream, pTextureMetadata->pStreamZones, sizeof(TFStreamZone) * pTextureMetadata->mMeshCount);

        if (totalWriteSize !=
            (sizeof(size_t) + sizeof(size_t) + (sizeof(char) * totalBufferSize) + sizeof(size_t) +
             sizeof(TFTextureBundleID) * pTextureMetadata->mMeshCount + sizeof(TFMaterialProps) * pTextureMetadata->mMeshCount +
             sizeof(TFStreamZone) * pTextureMetadata->mMeshCount))

        {
            LOGF(eERROR, "Error in writing out the TFTexturePackagedDesc");
            status = false;
        }

        if (!fsCloseStream(&fStream))
        {
            LOGF(eERROR, "Failed to close write stream for file '%s'.", outFileName);
            status = false;
        }

        if (nameBuffer)
        {
            tf_free(nameBuffer);
        }
    }

    return status;
}

/*
 * Take all the processed fileNames that are currently in the intermediate directory and package them all into a single buny archive
 * Packaged in the following order:
 * - PackageMetadata
 * - foreach <processedMeshName> in <meshNames>
 * -     <processedMeshName>.bin
 * -     <processedMeshName>.bin.texdata
 * -     foreach <processedTexName> in <processedMeshName>.bin.texdata
 * -         <processedTexName>.tex
 */
bool PackageAssets(const char filesToPackage[][FILE_NAME_LEN], size_t filesToPackageCount, TFResourceDirectory outputResourceDirectory,
                   const char* packageName)
{
    struct BunyArLibEntryCreateDesc* createDescList =
        (struct BunyArLibEntryCreateDesc*)tf_calloc(filesToPackageCount, sizeof(BunyArLibEntryCreateDesc));

    for (uint32_t fileNameIndex = 0; fileNameIndex < filesToPackageCount; ++fileNameIndex)
    {
        LOGF(LogLevel::eINFO, "Packaging the following file: %s", filesToPackage[fileNameIndex]);
        struct BunyArLibEntryCreateDesc* createDesc = createDescList + fileNameIndex;

        *createDesc = BUNYAR_LIB_FUNC_CREATE_DEFAULT_ENTRY_DESC;
        createDesc->inputRd = INTERMEDIATE_RD;
        createDesc->inputPath = filesToPackage[fileNameIndex];
        createDesc->outputName = filesToPackage[fileNameIndex];
    }

    BunyArLibCreateDesc archiveCreateDesc = { 0 };

    archiveCreateDesc.entryCount = filesToPackageCount;
    archiveCreateDesc.entries = createDescList;
    archiveCreateDesc.verbose = 1;
    archiveCreateDesc.threadPoolSize = 0;

    bool skipArchivePreprocessing = true; // Skip the pre-processing steps since we've already collected the list of files to be packaged.
    bool archiveIsCreated = bunyArLibCreate(outputResourceDirectory, packageName, &archiveCreateDesc, skipArchivePreprocessing);

    tf_free(createDescList);

    return archiveIsCreated;
}

/*
 * Open and readback the package.
 * Validate that each file in the package has a Node Index and that it corresponds to the order that the files were packaged in.
 * The order is important since we want to maintain the sequential access.
 */
bool ValidatePackage(TFResourceDirectory outputResourceDirectory, const char* packageName, const char filesToPackage[][FILE_NAME_LEN],
                     size_t filesToPackageCount)
{
    struct TFArchiveOpenDesc openDesc = { 0 };
    openDesc.mmap = true;

    TFIFileSystem archiveFS = { 0 };

    if (!fsArchiveOpen(outputResourceDirectory, packageName, &openDesc, &archiveFS))
    {
        LOGF(LogLevel::eERROR, "Failed to open the Packaged Archive File.");
        return false;
    }

    bool validated = true;
    for (uint32_t fileNameIndex = 0; fileNameIndex < filesToPackageCount; ++fileNameIndex)
    {
        uint64_t    nodeIndex = 0;
        const char* packagedFileName = filesToPackage[fileNameIndex];
        if (fsArchiveGetNodeId(&archiveFS, packagedFileName, &nodeIndex))
        {
            LOGF(LogLevel::eINFO, "Extracted the file: %s at node Index: %llu", packagedFileName, nodeIndex);
            if (nodeIndex != fileNameIndex)
            {
                LOGF(LogLevel::eERROR, "Invalid Node Index [%llu] for File : %s. Expected [%u]", nodeIndex, packagedFileName,
                     fileNameIndex);
                validated = false;
            }
        }
        else
        {
            LOGF(LogLevel::eERROR, "Failed to get the Node Index for File : %s", packagedFileName);
            validated = false;
        }
    }

    fsArchiveClose(&archiveFS);

    return validated;
}

PackagingData ProcessMeshesForPackaging(TFResourceDirectory inputDir, TFResourceDirectory outputDir, const char* inputPath,
                                        char (*filesToPackage)[FILE_NAME_LEN], size_t* packagedFilesCount, size_t prevTexBundleCount)
{
    AssetPipelineParams meshProcessParams = {};
    meshProcessParams.mSettings.force = true;
    meshProcessParams.mSettings.quiet = false;
    meshProcessParams.mInFilePath = "";
    meshProcessParams.mInExt = "";
    meshProcessParams.mFlagsCount = 0;
    meshProcessParams.mPathMode = PROCESS_MODE_DIRECTORY;

    // RD_1,2 and 3 are being used by AssetpipelineCmd
    meshProcessParams.mRDInput = inputDir;
    meshProcessParams.mRDOutput = outputDir;

    meshProcessParams.mInDir = inputPath;

    TFVertexLayout vertexLayout = { 0 };
    vertexLayout.mAttribCount = 6;
    vertexLayout.mAttribs[0].mSemantic = TF_SEMANTIC_POSITION;
    vertexLayout.mAttribs[0].mFormat = TinyImageFormat_R32G32B32_SFLOAT;
    vertexLayout.mAttribs[0].mBinding = 0;
    vertexLayout.mAttribs[0].mLocation = 0;
    vertexLayout.mAttribs[0].mOffset = 0;
    vertexLayout.mAttribs[1].mSemantic = TF_SEMANTIC_NORMAL;
    vertexLayout.mAttribs[1].mFormat = TinyImageFormat_R32_UINT;
    vertexLayout.mAttribs[1].mBinding = 1;
    vertexLayout.mAttribs[1].mLocation = 1;
    vertexLayout.mAttribs[1].mOffset = 0;
    vertexLayout.mAttribs[2].mSemantic = TF_SEMANTIC_TANGENT;
    vertexLayout.mAttribs[2].mFormat = TinyImageFormat_R32_UINT;
    vertexLayout.mAttribs[2].mBinding = 2;
    vertexLayout.mAttribs[2].mLocation = 2;
    vertexLayout.mAttribs[2].mOffset = 0;
    vertexLayout.mAttribs[3].mSemantic = TF_SEMANTIC_TEXCOORD0;
    vertexLayout.mAttribs[3].mFormat = TinyImageFormat_R32_UINT;
    vertexLayout.mAttribs[3].mBinding = 3;
    vertexLayout.mAttribs[3].mLocation = 3;
    vertexLayout.mAttribs[3].mOffset = 0;
    vertexLayout.mAttribs[4].mSemantic = TF_SEMANTIC_JOINTS;
    vertexLayout.mAttribs[4].mFormat = TinyImageFormat_R16G16B16A16_UINT;
    vertexLayout.mAttribs[4].mBinding = 4;
    vertexLayout.mAttribs[4].mLocation = 4;
    vertexLayout.mAttribs[4].mOffset = 0;
    vertexLayout.mAttribs[5].mSemantic = TF_SEMANTIC_WEIGHTS;
    vertexLayout.mAttribs[5].mFormat = TinyImageFormat_R32G32B32A32_SFLOAT;
    vertexLayout.mAttribs[5].mBinding = 5;
    vertexLayout.mAttribs[5].mLocation = 5;
    vertexLayout.mAttribs[5].mOffset = 0;

    ProcessGLTFParams processParams = {};
    processParams.pVertexLayout = &vertexLayout;
    processParams.mIgnoreMissingAttributes = true;
    processParams.mProcessMeshlets = false;
    // Default Params from Assetpipeline.cpp for PROCESS_GLTF
    // Add packaging params to control for these options.
    processParams.mNumMaxVertices = 128;
    processParams.mNumMaxTriangles = 256;
    processParams.mOptimizationFlags = MESH_OPTIMIZATION_FLAG_OFF;
    // Specify that this mesh is for a package
    processParams.mProcessForPackaging = true;

    // First process the input GLTFs
    bool errorOccured = ProcessGLTF(&meshProcessParams, &processParams);

    if (errorOccured)
    {
        LOGF(LogLevel::eERROR, "Mesh processing failed. Verify the validity of the GLTF within the mesh folder");
        return { 0 };
    }
    else
    {
        LOGF(LogLevel::eINFO, "Validate texture bundle");
        size_t       currNameIndex = 0;
        const size_t processedGeoCount = processParams.outPackagingData.pPackageMetadata->mGeoCount;
        for (uint32_t geoIndex = 0; geoIndex < processedGeoCount; ++geoIndex)
        {
            const char* processedFileName = processParams.outPackagingData.pPackageMetadata->pGeoNames + currNameIndex;
            currNameIndex += strlen(processedFileName) + 1;
            LOGF(LogLevel::eINFO, "For GLTF File : %s", processedFileName);

            TFTexturePackagedDesc* pTextureMetadata = &processParams.outPackagingData.pTextureMetadata[geoIndex];
            if (pTextureMetadata != NULL)
            {
                for (uint32_t meshIndex = 0; meshIndex < pTextureMetadata->mMeshCount; ++meshIndex)
                {
                    TFTextureBundleID* pBundleID = pTextureMetadata->pTextureBundleIDs + meshIndex;
                    TFTextureID        baseID = GET_COLOR_TEXID(*pBundleID);
                    TFTextureID        specID = GET_SPEC_TEXID(*pBundleID);
                    TFTextureID        normID = GET_NORMAL_TEXID(*pBundleID);

                    // Each BundleID is processed separately, hence they all start at an index of 0
                    // However when we package the textures, they are packaged contiguously
                    // Hence we need to offset the start index by the prevous textureBundle count
                    baseID += (TFTextureID)prevTexBundleCount;
                    specID += (TFTextureID)prevTexBundleCount;
                    normID += (TFTextureID)prevTexBundleCount;

                    *pBundleID = PackTextureBundle(baseID, specID, normID);
                    LOGF(LogLevel::eINFO, "Validate texture ID at Index %u : base %u | spec %u | norm %u", meshIndex, baseID, specID,
                         normID);
                }

                // Save the file names and nodeIDs to be packaged later
                processParams.outPackagingData.pPackageMetadata->pGeoNodeIds[geoIndex] = *packagedFilesCount;
                strcpy(filesToPackage[(*packagedFilesCount)++], processedFileName);

                const char metadataExtension[] = ".texdata";
                processParams.outPackagingData.pPackageMetadata->pTexMetadataNodeIds[geoIndex] = *packagedFilesCount;
                char* textureMetadataName = filesToPackage[(*packagedFilesCount)++];
                snprintf(textureMetadataName, 64, "%s%s", processedFileName, metadataExtension);
                StoreTexureMetadata(meshProcessParams.mRDOutput, textureMetadataName, pTextureMetadata);

                // By default the mesh will not have any associated animation data so we set this to 0.
                processParams.outPackagingData.pPackageMetadata->pAnimationNodeIDRanges->mAnimationCount = 0;
                processParams.outPackagingData.pPackageMetadata->pAnimationNodeIDRanges->mStartNodeId = 0;

                // Save the count to modify the texBundleIds to account for the offset.
                prevTexBundleCount += pTextureMetadata->mTextureCount;
            }
            else
            {
                LOGF(eERROR, "Failed to Process TextureMetadata.");
                return { 0 };
            }
        }
    }
    return processParams.outPackagingData;
}

// Append the data from the second PacakgeData to the first.
// Cleanup any of the data from the appended package
void AppendFullPackagingData(PackagingData* fullPackageData, PackagingData* appendedPackageData)
{
    const size_t geoOffset = fullPackageData->pPackageMetadata->mGeoCount;
    fullPackageData->pPackageMetadata->mGeoCount += appendedPackageData->pPackageMetadata->mGeoCount;
    const size_t totalGeoCount = fullPackageData->pPackageMetadata->mGeoCount;

    fullPackageData->pPackageMetadata->pGeoNodeIds =
        (size_t*)tf_realloc(fullPackageData->pPackageMetadata->pGeoNodeIds, totalGeoCount * sizeof(size_t));
    fullPackageData->pPackageMetadata->pTexMetadataNodeIds =
        (size_t*)tf_realloc(fullPackageData->pPackageMetadata->pTexMetadataNodeIds, totalGeoCount * sizeof(size_t));
    fullPackageData->pPackageMetadata->pAnimationNodeIDRanges = (TFAnimationNodeIDRange*)tf_realloc(
        fullPackageData->pPackageMetadata->pAnimationNodeIDRanges, totalGeoCount * sizeof(TFAnimationNodeIDRange));
    fullPackageData->pPackageMetadata->pTextureStartNodeIds =
        (size_t*)tf_realloc(fullPackageData->pPackageMetadata->pTextureStartNodeIds, totalGeoCount * sizeof(size_t));

    size_t nameSizeOffset = fullPackageData->pPackageMetadata->mNameBufferSize;
    fullPackageData->pPackageMetadata->mNameBufferSize += appendedPackageData->pPackageMetadata->mNameBufferSize;
    fullPackageData->pPackageMetadata->pGeoNames =
        (char*)tf_realloc(fullPackageData->pPackageMetadata->pGeoNames, fullPackageData->pPackageMetadata->mNameBufferSize);

    // Copy over the data from the appended PackageData
    memcpy(fullPackageData->pPackageMetadata->pGeoNodeIds + geoOffset, appendedPackageData->pPackageMetadata->pGeoNodeIds,
           appendedPackageData->pPackageMetadata->mGeoCount * sizeof(size_t));
    memcpy(fullPackageData->pPackageMetadata->pTexMetadataNodeIds + geoOffset, appendedPackageData->pPackageMetadata->pTexMetadataNodeIds,
           appendedPackageData->pPackageMetadata->mGeoCount * sizeof(size_t));
    memcpy(fullPackageData->pPackageMetadata->pGeoNames + nameSizeOffset, appendedPackageData->pPackageMetadata->pGeoNames,
           appendedPackageData->pPackageMetadata->mNameBufferSize);

    // Update the textute metadata with the processed animation texture data
    fullPackageData->pTextureMetadata =
        (TFTexturePackagedDesc*)tf_realloc(fullPackageData->pTextureMetadata, totalGeoCount * sizeof(TFTexturePackagedDesc));
    memcpy(fullPackageData->pTextureMetadata + geoOffset, appendedPackageData->pTextureMetadata,
           appendedPackageData->pPackageMetadata->mGeoCount * sizeof(TFTexturePackagedDesc));

    // Cleanup the appended packagedata
    tf_free(appendedPackageData->pPackageMetadata->pGeoNodeIds);
    appendedPackageData->pPackageMetadata->pGeoNodeIds = NULL;

    tf_free(appendedPackageData->pPackageMetadata->pTexMetadataNodeIds);
    appendedPackageData->pPackageMetadata->pTexMetadataNodeIds = NULL;

    tf_free(appendedPackageData->pPackageMetadata->pAnimationNodeIDRanges);
    appendedPackageData->pPackageMetadata->pAnimationNodeIDRanges = NULL;

    tf_free(appendedPackageData->pPackageMetadata->pTextureStartNodeIds);
    appendedPackageData->pPackageMetadata->pTextureStartNodeIds = NULL;

    tf_free(appendedPackageData->pPackageMetadata->pGeoNames);
    appendedPackageData->pPackageMetadata->pGeoNames = NULL;

    tf_free(appendedPackageData->pPackageMetadata);
    appendedPackageData->pPackageMetadata = NULL;

    tf_free(appendedPackageData->pTextureMetadata);
    appendedPackageData->pTextureMetadata = NULL;
}

/*
 * Cleanup any of the resources allocated during the packaging steps
 */
bool PackagingCleanup(PackagingData* pPackagingData, size_t geoCount)
{
    ASSERT(pPackagingData != NULL);

    for (uint32_t geoIndex = 0; geoIndex < geoCount; ++geoIndex)
    {
        TFTexturePackagedDesc* pTextureMetadata = pPackagingData->pTextureMetadata + geoIndex;
        if (pTextureMetadata != NULL)
        {
            if (pTextureMetadata->pTextureNames != NULL)
            {
                tf_free(pTextureMetadata->pTextureNames);
                pTextureMetadata->pTextureNames = NULL;
            }

            if (pTextureMetadata->pTextureBundleIDs != NULL)
            {
                tf_free(pTextureMetadata->pTextureBundleIDs);
                pTextureMetadata->pTextureBundleIDs = NULL;
            }

            if (pTextureMetadata->pMaterialProps != NULL)
            {
                tf_free(pTextureMetadata->pMaterialProps);
                pTextureMetadata->pMaterialProps = NULL;
            }

            if (pTextureMetadata->pStreamZones != NULL)
            {
                tf_free(pTextureMetadata->pStreamZones);
                pTextureMetadata->pStreamZones = NULL;
            }
        }
    }

    tf_free(pPackagingData->pPackageMetadata->pGeoNodeIds);
    pPackagingData->pPackageMetadata->pGeoNodeIds = NULL;

    tf_free(pPackagingData->pPackageMetadata->pTexMetadataNodeIds);
    pPackagingData->pPackageMetadata->pTexMetadataNodeIds = NULL;

    tf_free(pPackagingData->pPackageMetadata->pAnimationNodeIDRanges);
    pPackagingData->pPackageMetadata->pAnimationNodeIDRanges = NULL;

    tf_free(pPackagingData->pPackageMetadata->pTextureStartNodeIds);
    pPackagingData->pPackageMetadata->pTextureStartNodeIds = NULL;

    tf_free(pPackagingData->pPackageMetadata->pGeoNames);
    pPackagingData->pPackageMetadata->pGeoNames = NULL;

    tf_free(pPackagingData->pPackageMetadata);
    pPackagingData->pPackageMetadata = NULL;

    tf_free(pPackagingData->pTextureMetadata);
    pPackagingData->pTextureMetadata = NULL;

    return true;
}
