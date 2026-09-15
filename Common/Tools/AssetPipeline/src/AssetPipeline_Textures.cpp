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
#include "../../../Utilities/Interfaces/ILog.h"
#include "../../../Utilities/Interfaces/IToolFileSystem.h"
#include "../../../Utilities/Interfaces/IMath.h"
#include "../../../Utilities/Threading/ThreadSystem.h"

#include "AssetPipeline.h"

// Math
#include "../../../Resources/ResourceLoader/ThirdParty/OpenSource/tinyimageformat/tinyimageformat_base.h"

#include "../../../Resources/ResourceLoader/TextureContainers.h"

// TinyImage
#include "../../../Resources/ResourceLoader/ThirdParty/OpenSource/tinyimageformat/tinyimageformat_base.h"
#include "../../../Resources/ResourceLoader/ThirdParty/OpenSource/tinyimageformat/tinyimageformat_apis.h"
#include "../../../Resources/ResourceLoader/ThirdParty/OpenSource/tinydds/tinydds.h"
#include "../../../Resources/ResourceLoader/ThirdParty/OpenSource/tinyktx/tinyktx.h"

// TinyTiff
#include "../../ThirdParty/OpenSource/TinyTIFF/src/tinytiffreader.h"

// ISPC texcomp
#include "../../../../Common/Tools/ThirdParty/OpenSource/ISPCTextureCompressor/ispc_texcomp/ispc_texcomp.h"

// Nothings
#define STBI_NO_STDIO
#define STBI_ASSERT(x)         ASSERT(x)
#define STBI_MALLOC(sz)        tf_malloc(sz)
#define STBI_REALLOC(p, newsz) tf_realloc(p, newsz)
#define STBI_FREE(p)           tf_free(p)
#define STB_IMAGE_IMPLEMENTATION
#include "../../../Utilities/ThirdParty/OpenSource/Nothings/stb_image.h"
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#define STBIR_ASSERT(x)     ASSERT(x)
// Note: We don't use custom allocation contexts for stbi
#define STBIR_MALLOC(sz, c) ((c) == NULL) ? tf_malloc(sz) : NULL
#define STBIR_FREE(p, c)    ((c) == NULL) ? tf_free(p) : ((void)0)
#include "../../../Utilities/ThirdParty/OpenSource/Nothings/stb_ds.h"
#include "../../../Utilities/ThirdParty/OpenSource/Nothings/stb_image_resize.h"

#include "../../../Utilities/Interfaces/IMemory.h" //NOTE: this should be the last include in a .cpp
#include "../../ThirdParty/OpenSource/TinyTIFF/src/tiff_definitions_internal.h"

#define IS_POWER_OF_TWO(x)            ((x) != 0 && ((x) & ((x) - 1)) == 0)

#define TVB_ATLAS_FALLBACK_SLICE_SIZE 512

const char* gExtensions[] = { "dds", "ktx"
#ifdef PROSPERO_GNF
                              ,
                              "gnf", "gnf"
#endif
#ifdef XBOX_SCARLETT_DDS
                              ,
                              "dds"
#endif
};

TinyDDS_WriteCallbacks ddsWriteCallbacks{ [](void* user, char const* msg)
                                          {
                                              UNREF_PARAM(user);
                                              LOGF(eERROR, "%s", msg);
                                          },
                                          [](void* user, size_t size)
                                          {
                                              UNREF_PARAM(user);
                                              return tf_malloc(size);
                                          },
                                          [](void* user, void* memory)
                                          {
                                              UNREF_PARAM(user);
                                              tf_free(memory);
                                          },
                                          [](void* user, const void* buffer, size_t byteCount)
                                          { fsWriteToStream((TFFileStream*)user, buffer, (ssize_t)byteCount); } };

TinyDDS_Callbacks ddsReadCallbacks{ [](void* user, char const* msg)
                                    {
                                        UNREF_PARAM(user);
                                        LOGF(eERROR, "%s", msg);
                                    },
                                    [](void* user, size_t size)
                                    {
                                        UNREF_PARAM(user);
                                        return tf_malloc(size);
                                    },
                                    [](void* user, void* memory)
                                    {
                                        UNREF_PARAM(user);
                                        tf_free(memory);
                                    },
                                    [](void* user, void* buffer, size_t byteCount)
                                    { return fsReadFromStream((TFFileStream*)user, buffer, (ssize_t)byteCount); },
                                    [](void* user, int64_t offset)
                                    { return fsSeekStream((TFFileStream*)user, TF_SBO_START_OF_FILE, (ssize_t)offset); },
                                    [](void* user) { return (int64_t)fsGetStreamSeekPosition((TFFileStream*)user); } };

TinyKtx_WriteCallbacks ktxWriteCallbacks{ [](void* user, char const* msg)
                                          {
                                              UNREF_PARAM(user);
                                              LOGF(eERROR, "%s", msg);
                                          },
                                          [](void* user, size_t size)
                                          {
                                              UNREF_PARAM(user);
                                              return tf_malloc(size);
                                          },
                                          [](void* user, void* memory)
                                          {
                                              UNREF_PARAM(user);
                                              tf_free(memory);
                                          },
                                          [](void* user, const void* buffer, size_t byteCount)
                                          { fsWriteToStream((TFFileStream*)user, buffer, (ssize_t)byteCount); } };

TinyKtx_Callbacks ktxReadCallbacks{ [](void* user, char const* msg)
                                    {
                                        UNREF_PARAM(user);
                                        LOGF(eERROR, "%s", msg);
                                    },
                                    [](void* user, size_t size)
                                    {
                                        UNREF_PARAM(user);
                                        return tf_malloc(size);
                                    },
                                    [](void* user, void* memory)
                                    {
                                        UNREF_PARAM(user);
                                        tf_free(memory);
                                    },
                                    [](void* user, void* buffer, size_t byteCount)
                                    { return fsReadFromStream((TFFileStream*)user, buffer, (ssize_t)byteCount); },
                                    [](void* user, int64_t offset)
                                    { return fsSeekStream((TFFileStream*)user, TF_SBO_START_OF_FILE, (ssize_t)offset); },
                                    [](void* user) { return (int64_t)fsGetStreamSeekPosition((TFFileStream*)user); } };

typedef enum TextureDataType
{
    TEX_DATA_TYPE_U8 = 0,
    TEX_DATA_TYPE_U16 = 1,
    TEX_DATA_TYPE_F16 = 2,
    TEX_DATA_TYPE_F32 = 3
} TextureDataType;

typedef struct InputTextureData
{
    TFTextureDesc   mDesc;
    void*           pData[MAX_MIPLEVELS];
    uint32_t        mDataSize[MAX_MIPLEVELS];
    TextureDataType mDataType;
    bool            isCompressed;
} InputTextureData;

typedef struct CompressImageDescriptor
{
    TextureCompression mCompression;
    ASTC               mASTCCompression;
    DXT                mDXTCompression;
} CompressImageDescriptor;

struct TextureProcessTask
{
    AssetPipelineParams   mAssetParams;
    ProcessTexturesParams mTexturesParams;
    char                  mInFileName[TF_FS_MAX_PATH];
    char                  mInExtension[TF_FS_MAX_PATH];
    char                  mOutFileName[TF_FS_MAX_PATH];
    ProcessedTextureData  mOutData;
    bool                  mHasOutData;
    bool                  mError;
};

static void ProcessTextureTask(void* pData, uint64_t);

uint8_t* ResizeImage(uint8_t* ppData, const uint32_t width, const uint32_t height, const uint32_t newWidth, const uint32_t newHeight,
                     const uint32_t components, const uint32_t mip)
{
    uint8_t* pResizedImageData = (uint8_t*)tf_malloc(newWidth * newHeight * components);
    // stbir_resize_uint8(ppData, width, height, width * components, pResizedImageData, newWidth, newHeight, newWidth * components,
    // components);
    stbir_resize_uint8_srgb_edgemode(ppData, width, height, width * components, pResizedImageData, newWidth, newHeight,
                                     newWidth * components, components, 1, 0, stbir_edge::STBIR_EDGE_CLAMP);
    LOGF(eINFO, "Resized image %ux%u -> %ux%u (%ux%u)", width, height, newWidth, newHeight, newWidth << mip, newHeight << mip);
    return pResizedImageData;
}

TinyImageFormat GetASTCFormat(ASTC astc, bool isSrgb, bool isFloat)
{
    TinyImageFormat outFormat = TinyImageFormat_UNDEFINED;
    switch (astc)
    {
    case ASTC_4x4:
    case ASTC_4x4_SLOW:
        outFormat = isFloat ? TinyImageFormat_ASTC_4x4_SFLOAT : (isSrgb ? TinyImageFormat_ASTC_4x4_SRGB : TinyImageFormat_ASTC_4x4_UNORM);
        break;
    case ASTC_5x4:
    case ASTC_5x4_SLOW:
        outFormat = isFloat ? TinyImageFormat_ASTC_5x4_SFLOAT : (isSrgb ? TinyImageFormat_ASTC_5x4_SRGB : TinyImageFormat_ASTC_5x4_UNORM);
        break;
    case ASTC_5x5:
    case ASTC_5x5_SLOW:
        outFormat = isFloat ? TinyImageFormat_ASTC_5x5_SFLOAT : (isSrgb ? TinyImageFormat_ASTC_5x5_SRGB : TinyImageFormat_ASTC_5x5_UNORM);
        break;
    case ASTC_6x6:
    case ASTC_6x6_SLOW:
        outFormat = isFloat ? TinyImageFormat_ASTC_6x6_SFLOAT : (isSrgb ? TinyImageFormat_ASTC_6x6_SRGB : TinyImageFormat_ASTC_6x6_UNORM);
        break;
    case ASTC_8x5:
    case ASTC_8x5_SLOW:
        outFormat = isFloat ? TinyImageFormat_ASTC_8x5_SFLOAT : (isSrgb ? TinyImageFormat_ASTC_8x5_SRGB : TinyImageFormat_ASTC_8x5_UNORM);
        break;
    case ASTC_8x6:
    case ASTC_8x6_SLOW:
        outFormat = isFloat ? TinyImageFormat_ASTC_8x6_SFLOAT : (isSrgb ? TinyImageFormat_ASTC_8x6_SRGB : TinyImageFormat_ASTC_8x6_UNORM);
        break;
    case ASTC_8x8:
    case ASTC_8x8_SLOW:
        outFormat = isFloat ? TinyImageFormat_ASTC_8x8_SFLOAT : (isSrgb ? TinyImageFormat_ASTC_8x8_SRGB : TinyImageFormat_ASTC_8x8_UNORM);
        break;
    default:
        LOGF(eERROR, "Unknown ASTC compression!");
    }
    return outFormat;
}

TinyImageFormat GetBCFormat(DXT dxt, uint32_t channels, bool isSrgb, bool isSigned)
{
    TinyImageFormat outFormat = TinyImageFormat_UNDEFINED;
    switch (dxt)
    {
    case DXT_BC1:
        outFormat = isSrgb ? (channels < 4 ? TinyImageFormat_DXBC1_RGB_SRGB : TinyImageFormat_DXBC1_RGBA_SRGB)
                           : (channels < 4 ? TinyImageFormat_DXBC1_RGB_UNORM : TinyImageFormat_DXBC1_RGBA_UNORM);
        break;
    case DXT_BC3:
        outFormat = isSrgb ? TinyImageFormat_DXBC3_SRGB : TinyImageFormat_DXBC3_UNORM;
        break;
    case DXT_BC4:
        outFormat = isSigned ? TinyImageFormat_DXBC4_SNORM : TinyImageFormat_DXBC4_UNORM;
        break;
    case DXT_BC5:
        outFormat = isSigned ? TinyImageFormat_DXBC5_SNORM : TinyImageFormat_DXBC5_UNORM;
        break;
    case DXT_BC6:
        outFormat = isSigned ? TinyImageFormat_DXBC6H_SFLOAT : TinyImageFormat_DXBC6H_UFLOAT;
        break;
    case DXT_BC7:
        outFormat = isSrgb ? TinyImageFormat_DXBC7_SRGB : TinyImageFormat_DXBC7_UNORM;
        break;
    default:
        LOGF(eERROR, "Unknown DXT compression!");
    }

    return outFormat;
}

TinyImageFormat GetOutputTextureFormat(ProcessTexturesParams* pTexturesParams, TFTextureDesc* pDesc,
                                       CompressImageDescriptor* pOutCompressImageDescriptor)
{
    const uint32_t  channels = TinyImageFormat_ChannelCount(pDesc->mFormat);
    TinyImageFormat outFormat = TinyImageFormat_UNDEFINED;
    bool            isSigned = TinyImageFormat_IsSigned(pDesc->mFormat);
    bool            isSrgb = TinyImageFormat_IsSRGB(pDesc->mFormat);
    bool            isFloat = TinyImageFormat_IsFloat(pDesc->mFormat);

    ASTC astcCompression = ASTC_NONE;
    DXT  dxtCompression = DXT_NONE;

    if (pDesc->mFormat == TinyImageFormat_R32G32B32A32_SFLOAT)
    {
        if (pTexturesParams->mInputLinearColorSpace == false)
        {
            isSrgb = true;
        }
    }

    switch (pTexturesParams->mCompression)
    {
    case COMPRESSION_ASTC:
        if (pTexturesParams->mOverrideASTC == ASTC_NONE)
        {
            if (channels == 1)
            {
                astcCompression = ASTC_6x6; // Eg. Grayscale images. 3.56 bpp
            }
            else if (channels == 2)
            {
                astcCompression = ASTC_4x4; // Eg. Normal maps where xy is stored and z reconstructed. 8 bpp
            }
            else if (channels == 3)
            {
                astcCompression = ASTC_6x6; // Eg. RGB textures with potential for a single bit to store alpha. 3.56 bpp
            }
            else if (channels == 4)
            {
                astcCompression = ASTC_4x4; // Eg. Full RGBA Textures. 8 bpp
            }
        }
        else
        {
            astcCompression = pTexturesParams->mOverrideASTC;
        }
        outFormat = GetASTCFormat(astcCompression, isSrgb, isFloat);

        if (isFloat && isSrgb && astcCompression == ASTC_4x4)
        {
            outFormat = TinyImageFormat_ASTC_4x4_SRGB;
        }
        break;
    case COMPRESSION_BC:
        if (pTexturesParams->mOverrideBC == DXT_NONE)
        {
            if (channels == 1)
            {
                dxtCompression = DXT_BC4; // Eg. Grayscale images. 8 bytes per block
            }
            else if (channels == 2)
            {
                dxtCompression = DXT_BC5; // Eg. Normal maps where xy is stored and z reconstructed. 16 bytes per block
            }
            else if (channels == 3)
            {
                if (isFloat)
                {
                    dxtCompression = DXT_BC6; // Eg. HDR textures with 16:16:16 components. 16 bytes per block
                }
                else
                {
                    dxtCompression = DXT_BC1; // Eg. RGB textures with potential for a single bit to store alpha. 8 bytes per block
                }
            }
            else if (channels == 4)
            {
                dxtCompression = DXT_BC7; // Eg. Full RGBA Textures. 16 bytes per block
            }
        }
        else
        {
            dxtCompression = pTexturesParams->mOverrideBC;
        }
        outFormat = GetBCFormat(dxtCompression, channels, isSrgb, isSigned);
        break;
    default:
        outFormat = pDesc->mFormat;
        break;
    }

    pOutCompressImageDescriptor->mCompression = pTexturesParams->mCompression;
    pOutCompressImageDescriptor->mASTCCompression = astcCompression;
    pOutCompressImageDescriptor->mDXTCompression = dxtCompression;
    return outFormat;
}

TinyImageFormat TextureFormatFromImageInfo(uint32_t componentCount, uint32_t bitDepth, bool srgb, bool hdr)
{
    if (bitDepth == 32)
    {
        switch (componentCount)
        {
        case 1:
            return TinyImageFormat_R32_SFLOAT;
        case 2:
            return TinyImageFormat_R32G32_SFLOAT;
        case 3:
            return TinyImageFormat_R32G32B32_SFLOAT;
        case 4:
            return TinyImageFormat_R32G32B32A32_SFLOAT;
        }
    }
    else if (bitDepth == 16)
    {
        switch (componentCount)
        {
        case 1:
            return hdr ? TinyImageFormat_R16_SFLOAT : TinyImageFormat_R16_UNORM;
        case 2:
            return hdr ? TinyImageFormat_R16G16_SFLOAT : TinyImageFormat_R16G16_UNORM;
        case 3:
            return hdr ? TinyImageFormat_R16G16B16_SFLOAT : TinyImageFormat_R16G16B16_UNORM;
        case 4:
            return hdr ? TinyImageFormat_R16G16B16A16_SFLOAT : TinyImageFormat_R16G16B16A16_UNORM;
        }
    }
    else if (bitDepth == 8)
    {
        switch (componentCount)
        {
        case 1:
            return srgb ? TinyImageFormat_R8_SRGB : TinyImageFormat_R8_UNORM;
        case 2:
            return srgb ? TinyImageFormat_R8G8_SRGB : TinyImageFormat_R8G8_UNORM;
        case 3:
            return srgb ? TinyImageFormat_R8G8B8_SRGB : TinyImageFormat_R8G8B8_UNORM;
        case 4:
            return srgb ? TinyImageFormat_R8G8B8A8_SRGB : TinyImageFormat_R8G8B8A8_UNORM;
        }
    }
    return TinyImageFormat_UNDEFINED;
}

bool LoadTextureData(TFResourceDirectory resourceDir, const char* pFilepath, const char* pExtension, ProcessTexturesParams* pTextureParams,
                     InputTextureData* pOut)
{
    ASSERT(pFilepath);
    ASSERT(pOut);
    ASSERT(pExtension);
    ASSERT(pTextureParams);

    bool success = true;

    int32_t componentCount = 0;
    int32_t forceComponents = 0; // Optional to force loading a certain amount of channels
    int32_t bitDepth = 8;
    bool    isHdr = false;

    int32_t imageWidth = 0;
    int32_t imageHeight = 0;
    int32_t imageDepth = 1; // TODO: support loading 3D images stbi_image seems not to support this?

    bool                isTiff = false;
    TinyTIFFReaderFile* tiffFile = NULL;
    // handling tiff images
    if (STRCMP(pExtension, "tif"))
    {
        tiffFile = TinyTIFFReader_open(resourceDir, pFilepath);

        if (tiffFile)
        {
            imageWidth = TinyTIFFReader_getWidth(tiffFile);
            imageHeight = TinyTIFFReader_getHeight(tiffFile);
            componentCount = TinyTIFFReader_getSamplesPerPixel(tiffFile);
            bitDepth = TinyTIFFReader_getBitsPerSample(tiffFile, 0);
            uint16_t sampleFormat = TinyTIFFReader_getSampleFormat(tiffFile);
            if (sampleFormat == TIFF_SAMPLEFORMAT_IEEEFP)
            {
                isHdr = true;
            }
            success = bitDepth == 32 && (sampleFormat == TIFF_SAMPLEFORMAT_IEEEFP || sampleFormat == TIFF_SAMPLEFORMAT_UINT);

            isTiff = true;
            pOut->mDataType = TEX_DATA_TYPE_F32;
            if (success == false)
            {
                LOGF(eERROR, "asset pipeline input texture '%s' is not  be 32 bit per channel, Aborting", pFilepath);
                TinyTIFFReader_close(tiffFile);
                return success;
            }
        }
    }

    // Read input file
    TFFileStream file = {};
    if (!fsOpenStreamFromPath(resourceDir, pFilepath, TFFileMode(TF_FM_READ | TF_FM_ALLOW_READ), &file))
    {
        LOGF(eERROR, "Could not open file '%s'.", pFilepath);
        success = false;
        return success;
    }

    ssize_t fileSize = fsGetStreamFileSize(&file);
    if (fileSize > INT32_MAX)
    {
        LOGF(eWARNING, "File '%s' exceeds max handled filesize, size: %zi max: %i", pFilepath, fileSize, INT32_MAX);
        success = false;
    }
    if (STRCMP(pExtension, gExtensions[CONTAINER_KTX]))
    {
        LOGF(LogLevel::eERROR, "Unsupported input container format. KTX is not supported as an input format");
        return false;
    }
    else if (STRCMP(pExtension, gExtensions[CONTAINER_DDS]))
    {
        LOGF(LogLevel::eERROR, "Unsupported input container format. DDS is not supported as an input format");
        return false;
    }
    else
    {
        uint8_t* pFileData = NULL;
        if (isTiff == false)
        {
            pFileData = (uint8_t*)tf_malloc(fileSize);
            ssize_t fileReadBytes = fsReadFromStream(&file, pFileData, fileSize);

            if (fileSize != fileReadBytes)
            {
                LOGF(eERROR, "Could not read all bytes from file '%s' %zi/%zi", pFilepath, fileReadBytes, fileSize);
                success = false;
            }

            stbi_info_from_memory(pFileData, (int32_t)fileSize, &imageWidth, &imageHeight, &componentCount);
        }

        forceComponents = componentCount;

        if (pTextureParams->mInputLinearColorSpace)
        {
            // BC compression (or uncompressed) doesn't need to have forced channels.
            // Unless we need to do BC1 compression; ISPC Texture Compressor expects 32bit/pixel for BC1
            if (pTextureParams->mOverrideBC == DXT_BC1)
            {
                forceComponents = 4;
            }

            if (forceComponents < pTextureParams->mSwizzleChannelCount)
            {
                // If input swizzling channels is more than what the image has, also extend the channels needed to be loaded
                forceComponents = pTextureParams->mSwizzleChannelCount;
            }
        }
        else
        {
            // sRGB Color Space
            // DDS container srgb only supports 4 components
            // KTX container with srgb doesn't support 3 components (at least gives some issues on Android)
            forceComponents = 4;
        }

        if (pTextureParams->mProcessAsNormalMap)
        {
            // swizzling for normal maps means we can be having more than 2 channels.
            if (pTextureParams->mSwizzleChannelCount > 0)
            {
                componentCount = 2;
            }
            if (componentCount != 2)
            {
                LOGF(LogLevel::eWARNING, "Forcing the component count for Normal maps to 2.");
                forceComponents = 4;
                componentCount = 2;
            }
            pTextureParams->mSwizzleChannelCount = 4;
            pTextureParams->mSwizzle = { 'x', 'x', 'x', 'y' };
        }

        // Handle color swizzling explicitly for ASTC compression.
        // We always need 4 components, but it's swizzled optimally for the compression
        if (pTextureParams->mCompression == TextureCompression::COMPRESSION_ASTC)
        {
            forceComponents = 4;
            pTextureParams->mSwizzleChannelCount = 4;
            switch (componentCount)
            {
            case 1:
                pTextureParams->mSwizzle = { 'x', 'x', 'x', '1' };
                break;
            case 2:
                pTextureParams->mSwizzle = { 'x', 'x', 'x', 'y' };
                break;
            case 3:
                pTextureParams->mSwizzle = { 'x', 'y', 'z', '1' };
                break;
            case 4:
                pTextureParams->mSwizzle = { 'x', 'y', 'z', 'a' };
                break;
            }
        }
        if (pFileData)
        {
            if (stbi_is_hdr_from_memory(pFileData, (int32_t)fileSize))
            {
                isHdr = true;
                bitDepth = 32;
            }

            if (stbi_is_16_bit_from_memory(pFileData, (int32_t)fileSize))
            {
                bitDepth = 16;
            }
        }

        TinyImageFormat textureFormat =
            TextureFormatFromImageInfo(forceComponents, bitDepth, !pTextureParams->mInputLinearColorSpace, isHdr);

        if (textureFormat == TinyImageFormat_UNDEFINED)
        {
            LOGF(LogLevel::eERROR, "Cannot process texture with texure format UNDEFINED");
            success = false;
        }
        uint32_t bpc = 1;

        if (success)
        {
            pOut->pData[0] = NULL;
            bpc = bitDepth / 8;

            if (isTiff)
            {
                uint32_t pixelCount = imageWidth * imageHeight;
                uint32_t channelSize = imageWidth * imageHeight * bpc;
                uint32_t dataSize = channelSize * max(forceComponents, componentCount);
                pOut->pData[0] = tf_calloc(dataSize, sizeof(uint8_t));

                uint32_t* channelData = (uint32_t*)tf_calloc(imageWidth * imageHeight, sizeof(uint32_t));
                uint8_t*  pDest = (uint8_t*)pOut->pData[0];

                for (uint16_t sample = 0; sample < componentCount; sample++)
                {
                    if (TINYTIFF_TRUE == TinyTIFFReader_getSampleData(tiffFile, channelData, sample))
                    {
                        uint32_t* pDest32 = (uint32_t*)pDest;
                        /// distribute the data so the pixel data is interleaved
                        for (uint32_t pixel = 0; pixel < pixelCount; pixel++)
                        {
                            pDest32[pixel * 4 + sample] = channelData[pixel];
                        }
                        /// non normal maps need to convert into their 32 bbc to sRGB before proceeding
                        if (pTextureParams->mInputLinearColorSpace == false && sample < 3)
                        {
                            float* pDestFloats = (float*)pDest32;

                            for (uint32_t pixel = 0; pixel < pixelCount; pixel++)
                            {
                                float linearValue = pDestFloats[pixel * 4 + sample];

                                if (linearValue < 0.0031308f)
                                {
                                    linearValue = linearValue * 12.92f;
                                }
                                else
                                {
                                    linearValue = 1.055f * powf(linearValue, 1.0f / 2.4f) - 0.055f;
                                }
                                pDestFloats[pixel * 4 + sample] = linearValue;
                            }
                        }
                    }
                    else
                    {
                        LOGF(eERROR, "Error getting tiff sample");
                        success = false;
                    }
                }
                tf_free(channelData);
                TinyTIFFReader_close(tiffFile);
            }
            else
            {
                if (bitDepth == 32)
                {
                    pOut->pData[0] =
                        stbi_loadf_from_memory(pFileData, (int32_t)fileSize, &imageWidth, &imageHeight, &componentCount, forceComponents);
                    pOut->mDataType = TEX_DATA_TYPE_F32;
                }
                else if (bitDepth == 16)
                {
                    pOut->pData[0] =
                        stbi_load_16_from_memory(pFileData, (int32_t)fileSize, &imageWidth, &imageHeight, &componentCount, forceComponents);
                    pOut->mDataType = TEX_DATA_TYPE_U16;
                }
                else
                {
                    pOut->pData[0] =
                        stbi_load_from_memory(pFileData, (int32_t)fileSize, &imageWidth, &imageHeight, &componentCount, forceComponents);
                    pOut->mDataType = TEX_DATA_TYPE_U8;
                }
            }

            pOut->mDataSize[0] = imageWidth * imageHeight * max(forceComponents, componentCount) * bpc;

            pOut->mDesc.mWidth = imageWidth;
            pOut->mDesc.mHeight = imageHeight;
            pOut->mDesc.mDepth = imageDepth;

            pOut->mDesc.mMipLevels = 1;
            pOut->mDesc.mArraySize = 1;
            pOut->mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_TEXTURE;
            pOut->mDesc.mFormat = textureFormat;
        }

        // Release loaded input file data
        if (pFileData)
        {
            tf_free(pFileData);
        }
    }

    fsCloseStream(&file);

    return success;
}

bool ReadTGADimensions(TFResourceDirectory resourceDir, const char* pFilepath, uint32_t* width, uint32_t* height)
{
    ASSERT(pFilepath);

    bool success = true;

    int32_t componentCount = 0;

    int32_t imageWidth = 0;
    int32_t imageHeight = 0;

    // Read input file
    TFFileStream file = {};
    if (!fsOpenStreamFromPath(resourceDir, pFilepath, TFFileMode(TF_FM_READ | TF_FM_ALLOW_READ), &file))
    {
        LOGF(eERROR, "Could not open file '%s'.", pFilepath);
        success = false;
        return success;
    }

    ssize_t  fileSize = fsGetStreamFileSize(&file);
    uint8_t* pFileData = NULL;
    pFileData = (uint8_t*)tf_malloc(fileSize);
    ssize_t fileReadBytes = fsReadFromStream(&file, pFileData, fileSize);
    if (fileSize != fileReadBytes)
    {
        LOGF(eERROR, "Could not read all bytes from file '%s' %zi/%zi", pFilepath, fileReadBytes, fileSize);
        success = false;
    }
    stbi_info_from_memory(pFileData, (int32_t)fileSize, &imageWidth, &imageHeight, &componentCount);
    // Release loaded input file data
    if (pFileData)
    {
        tf_free(pFileData);
    }

    *width = imageWidth;
    *height = imageHeight;
    fsCloseStream(&file);
    return success;
}

static inline bool IsASTCSlow(ASTC method)
{
    ASSERT(method != ASTC_NONE);
    switch (method)
    {
    case ASTC_4x4_SLOW:
    case ASTC_5x4_SLOW:
    case ASTC_6x6_SLOW:
    case ASTC_8x5_SLOW:
    case ASTC_8x6_SLOW:
    case ASTC_8x8_SLOW:
        return true;
    default:
        return false;
    }
}

// Older Compression performed using ISPC
bool ASTCCompression(void* ppData[MAX_MIPLEVELS], void* ppOutCompressed[MAX_MIPLEVELS], uint32_t* pCompressedSize,
                     CompressImageDescriptor* pDesc, TFTextureDesc* pTexDesc)
{
    ASSERT(ppData[0]);
    ASSERT(pDesc);
    ASSERT(pTexDesc->mWidth && pTexDesc->mHeight); // widht/height cannot be 0

    uint32_t blockSizeX = 0;
    uint32_t blockSizeY = 0;

    switch (pDesc->mASTCCompression)
    {
    case ASTC_4x4:
    case ASTC_4x4_SLOW:
        blockSizeX = 4;
        blockSizeY = 4;
        break;
    case ASTC_5x4:
    case ASTC_5x4_SLOW:
        blockSizeX = 5;
        blockSizeY = 4;
        break;
    case ASTC_5x5:
    case ASTC_5x5_SLOW:
        blockSizeX = 5;
        blockSizeY = 5;
        break;
    case ASTC_6x6:
    case ASTC_6x6_SLOW:
        blockSizeX = 6;
        blockSizeY = 6;
        break;
    case ASTC_8x5:
    case ASTC_8x5_SLOW:
        blockSizeX = 8;
        blockSizeY = 5;
        break;
    case ASTC_8x6:
    case ASTC_8x6_SLOW:
        blockSizeX = 8;
        blockSizeY = 6;
        break;
    case ASTC_8x8:
    case ASTC_8x8_SLOW:
        blockSizeX = 8;
        blockSizeY = 8;
        break;
    case ASTC_NONE:
    default:
        blockSizeX = 4;
        blockSizeY = 4;
    }

    const uint32_t channels = TinyImageFormat_ChannelCount(pTexDesc->mFormat);
    ASSERT(channels >= 3); // ISPC astc compression requires atleast 3 channels

    uint32_t bitsPerPixel = TinyImageFormat_BitSizeOfBlock(pTexDesc->mFormat);

    uint32_t bytesPerChannel = (TinyImageFormat_BitSizeOfBlock(pTexDesc->mFormat) / 8) / 4;

    // Get astc encoder settings
    astc_enc_settings astcEncSettings = {};
    if (channels > 3)
    {
        if (pDesc->mASTCCompression == ASTC_4x4_SLOW || pDesc->mASTCCompression == ASTC_5x4_SLOW ||
            pDesc->mASTCCompression == ASTC_5x5_SLOW || pDesc->mASTCCompression == ASTC_6x6_SLOW ||
            pDesc->mASTCCompression == ASTC_8x5_SLOW || pDesc->mASTCCompression == ASTC_8x6_SLOW ||
            pDesc->mASTCCompression == ASTC_8x8_SLOW)
        {
            GetProfile_astc_alpha_slow(&astcEncSettings, blockSizeX, blockSizeY);
        }
        else
        {
            GetProfile_astc_alpha_fast(&astcEncSettings, blockSizeX, blockSizeY);
        }
    }
    else
    {
        GetProfile_astc_fast(&astcEncSettings, blockSizeX, blockSizeY);
    }
    astcEncSettings.bytesPerChannel = bytesPerChannel;

    // Store if texture mip 0 is padded and use that in the and for the texture descriptor
    uint32_t adjustedWidth = pTexDesc->mWidth;
    uint32_t adjustedHeight = pTexDesc->mHeight;
    uint32_t slices = pTexDesc->mArraySize;

    for (uint32_t i = 0; i < pTexDesc->mMipLevels; ++i)
    {
        uint32_t width = max(1u, (pTexDesc->mWidth >> i));
        uint32_t height = max(1u, (pTexDesc->mHeight >> i));
        uint8_t* pData = NULL;
        for (uint32_t slice_index = 0; slice_index < slices; ++slice_index)
        {
            bool     padded = false;
            uint32_t slice_offset = width * height * channels * bytesPerChannel * slice_index;

            if (width % blockSizeX != 0 || height % blockSizeY != 0)
            {
                rgba_surface input;
                input.width = width;
                input.height = height;
                input.stride = width * channels * bytesPerChannel;
                input.ptr = (uint8_t*)ppData[i] + slice_offset;

                uint32_t resizedImageWidth = width % blockSizeX == 0 ? width : width + (blockSizeX - width % blockSizeX);
                uint32_t resizedImageHeight = height % blockSizeY == 0 ? height : height + (blockSizeY - height % blockSizeY);

                pData = (uint8_t*)tf_malloc(resizedImageWidth * resizedImageHeight * channels * bytesPerChannel);
                rgba_surface input_padded;
                input_padded.width = resizedImageWidth;
                input_padded.height = resizedImageHeight;
                input_padded.stride = resizedImageWidth * channels * bytesPerChannel;
                input_padded.ptr = pData;

                ReplicateBorders(&input_padded, &input, 0, 0, bitsPerPixel);

                width = resizedImageWidth;
                height = resizedImageHeight;
                padded = true;

                if (i == 0)
                {
                    adjustedWidth = width;
                    adjustedHeight = height;
                }
            }
            else
            {
                pData = (uint8_t*)ppData[i] + slice_offset;
            }

            const uint32_t xblocks = (width + blockSizeX - 1) / blockSizeX;
            const uint32_t yblocks = (height + blockSizeY - 1) / blockSizeY;
            const uint32_t bytesPerBlock = 16;

            uint32_t compressed_offset = 0;
            if (ppOutCompressed[i] == NULL)
            {
                pCompressedSize[i] = xblocks * yblocks * bytesPerBlock;
                ppOutCompressed[i] = (uint8_t*)tf_malloc(pCompressedSize[i]);
            }
            else
            {
                compressed_offset = pCompressedSize[i];
                pCompressedSize[i] += xblocks * yblocks * bytesPerBlock;
                ppOutCompressed[i] = (uint8_t*)tf_realloc(ppOutCompressed[i], pCompressedSize[i]);
            }

            rgba_surface input;
            input.width = width;
            input.height = height;
            input.stride = width * channels * bytesPerChannel;
            input.ptr = pData;

            CompressBlocksASTC(&input, (uint8_t*)ppOutCompressed[i] + compressed_offset, &astcEncSettings);

            if (padded)
            {
                tf_free(pData);
            }
        }
    }

    // Set image size to padding size
    pTexDesc->mWidth = adjustedWidth;
    pTexDesc->mHeight = adjustedHeight;

    return true;
}

typedef void (*BCCompressionFunc)(const rgba_surface* src, uint8_t* dst);

#define DECLARE_COMPRESS_FUNCTION_BC6H(profile)                              \
    void CompressBlocksBC6H_##profile(const rgba_surface* src, uint8_t* dst) \
    {                                                                        \
        bc6h_enc_settings settings;                                          \
        GetProfile_bc6h_##profile(&settings);                                \
        CompressBlocksBC6H(src, dst, &settings);                             \
    }

DECLARE_COMPRESS_FUNCTION_BC6H(veryfast);
DECLARE_COMPRESS_FUNCTION_BC6H(fast);
DECLARE_COMPRESS_FUNCTION_BC6H(basic);
DECLARE_COMPRESS_FUNCTION_BC6H(slow);
DECLARE_COMPRESS_FUNCTION_BC6H(veryslow);

#define DECLARE_COMPRESS_FUNCTION_BC7(profile)                              \
    void CompressBlocksBC7_##profile(const rgba_surface* src, uint8_t* dst) \
    {                                                                       \
        bc7_enc_settings settings;                                          \
        GetProfile_##profile(&settings);                                    \
        settings.bytesPerChannel = 1;                                       \
        CompressBlocksBC7(src, dst, &settings);                             \
    }

#define DECLARE_COMPRESS_FUNCTION_BC7_32(profile)                                \
    void CompressBlocksBC7_##profile##_32(const rgba_surface* src, uint8_t* dst) \
    {                                                                            \
        bc7_enc_settings settings;                                               \
        GetProfile_##profile(&settings);                                         \
        settings.bytesPerChannel = 4;                                            \
        CompressBlocksBC7(src, dst, &settings);                                  \
    }

DECLARE_COMPRESS_FUNCTION_BC7(ultrafast);
DECLARE_COMPRESS_FUNCTION_BC7(veryfast);
DECLARE_COMPRESS_FUNCTION_BC7(fast);
DECLARE_COMPRESS_FUNCTION_BC7(basic);
DECLARE_COMPRESS_FUNCTION_BC7(slow);
DECLARE_COMPRESS_FUNCTION_BC7(alpha_ultrafast);
DECLARE_COMPRESS_FUNCTION_BC7(alpha_veryfast);
DECLARE_COMPRESS_FUNCTION_BC7(alpha_fast);
DECLARE_COMPRESS_FUNCTION_BC7(alpha_basic);
DECLARE_COMPRESS_FUNCTION_BC7(alpha_slow);

DECLARE_COMPRESS_FUNCTION_BC7_32(ultrafast);
DECLARE_COMPRESS_FUNCTION_BC7_32(veryfast);
DECLARE_COMPRESS_FUNCTION_BC7_32(fast);
DECLARE_COMPRESS_FUNCTION_BC7_32(basic);
DECLARE_COMPRESS_FUNCTION_BC7_32(slow);
DECLARE_COMPRESS_FUNCTION_BC7_32(alpha_ultrafast);
DECLARE_COMPRESS_FUNCTION_BC7_32(alpha_veryfast);
DECLARE_COMPRESS_FUNCTION_BC7_32(alpha_fast);
DECLARE_COMPRESS_FUNCTION_BC7_32(alpha_basic);
DECLARE_COMPRESS_FUNCTION_BC7_32(alpha_slow);

bool BCCompression(void* ppData[MAX_MIPLEVELS], void* ppOutCompressed[MAX_MIPLEVELS], uint32_t* pCompressedSize,
                   CompressImageDescriptor* pDesc, TFTextureDesc* pTexDesc)
{
    ASSERT(ppData[0]);
    ASSERT(pDesc);
    ASSERT(pTexDesc->mWidth && pTexDesc->mHeight); // width/height cannot be 0

    const uint32_t    blockSize = 4;
    BCCompressionFunc bcCompress = CompressBlocksBC7_alpha_fast;
    uint32_t          bytesPerBlock = 16;

    uint32_t inputChannels = TinyImageFormat_ChannelCount(pTexDesc->mFormat);
    uint32_t requiredInputChannels = 4;
    uint32_t bitsPerPixel = TinyImageFormat_BitSizeOfBlock(pTexDesc->mFormat);
    uint32_t bytesPerChannel = (bitsPerPixel / 4) / 8;
    //-LDR input is 32 bit / pixel(sRGB), HDR is 64 bit / pixel(half float)
    //	- for BC4 input is 8bit / pixel(R8), for BC5 input is 16bit / pixel(RG8)
    //	- dst buffer must be allocated with enough space for the compressed texture

    switch (pDesc->mDXTCompression)
    {
    case DXT_BC1:
        bcCompress = CompressBlocksBC1;
        bytesPerBlock = 8;
        break;
    case DXT_BC3:
        bcCompress = CompressBlocksBC3;
        break;
    case DXT_BC4:
        bcCompress = CompressBlocksBC4;
        bytesPerBlock = 8;
        requiredInputChannels = 1;
        break;
    case DXT_BC5:
        bcCompress = CompressBlocksBC5;
        requiredInputChannels = 2;
        break;
    case DXT_BC6:
        bcCompress = CompressBlocksBC6H_fast;
        requiredInputChannels = 4;
        if (bitsPerPixel != 64 && !TinyImageFormat_IsFloat(pTexDesc->mFormat))
        {
            LOGF(LogLevel::eERROR, "%s is an unsupported format for BC6 compression", TinyImageFormat_Name(pTexDesc->mFormat));
            return false;
        }
        break;
    case DXT_BC7:

        if (bytesPerChannel == 4)
        {
            bcCompress = inputChannels > 3 ? CompressBlocksBC7_alpha_fast_32 : CompressBlocksBC7_fast_32;
        }
        else
        {
            bcCompress = inputChannels > 3 ? CompressBlocksBC7_alpha_fast : CompressBlocksBC7_fast;
        }
        break;
    default:
        ASSERT(false && "Unknown BC compression request");
    }
    ASSERT(requiredInputChannels <= inputChannels && "Input should always have more data available");

    // Store if texture mip 0 is padded and use that in the and for the texture descriptor
    uint32_t adjustedWidth = pTexDesc->mWidth;
    uint32_t adjustedHeight = pTexDesc->mHeight;
    uint32_t slices = pTexDesc->mArraySize;

    for (uint32_t i = 0; i < pTexDesc->mMipLevels; ++i)
    {
        uint32_t width = max(1u, (pTexDesc->mWidth >> i));
        uint32_t height = max(1u, (pTexDesc->mHeight >> i));
        uint8_t* pData = NULL;

        for (uint32_t slice_index = 0; slice_index < slices; ++slice_index)
        {
            bool     padded = false;
            uint32_t slice_offset = width * height * inputChannels * bytesPerChannel * slice_index;

            if (width % blockSize != 0 || height % blockSize != 0)
            {
                rgba_surface input;
                input.width = width;
                input.height = height;
                input.stride = width * inputChannels * bytesPerChannel;
                input.ptr = (uint8_t*)ppData[i] + slice_offset;

                uint32_t resizedImageWidth = width % blockSize == 0 ? width : width + (blockSize - width % blockSize);
                uint32_t resizedImageHeight = height % blockSize == 0 ? height : height + (blockSize - height % blockSize);

                pData = (uint8_t*)tf_malloc(resizedImageWidth * resizedImageHeight * inputChannels * bytesPerChannel);
                rgba_surface input_padded;
                input_padded.width = resizedImageWidth;
                input_padded.height = resizedImageHeight;
                input_padded.stride = resizedImageWidth * inputChannels * bytesPerChannel;
                input_padded.ptr = pData;

                ReplicateBorders(&input_padded, &input, 0, 0, bitsPerPixel);

                width = resizedImageWidth;
                height = resizedImageHeight;
                padded = true;

                if (i == 0)
                {
                    adjustedWidth = width;
                    adjustedHeight = height;
                }
            }
            else
            {
                pData = (uint8_t*)ppData[i] + slice_offset;
            }

            const uint32_t xblocks = (width + blockSize - 1) / blockSize;
            const uint32_t yblocks = (height + blockSize - 1) / blockSize;

            uint32_t compressed_offset = 0;
            if (ppOutCompressed[i] == NULL)
            {
                pCompressedSize[i] = xblocks * yblocks * bytesPerBlock;
                ppOutCompressed[i] = (uint8_t*)tf_malloc(pCompressedSize[i]);
            }
            else
            {
                compressed_offset = pCompressedSize[i];
                pCompressedSize[i] += xblocks * yblocks * bytesPerBlock;
                ppOutCompressed[i] = (uint8_t*)tf_realloc(ppOutCompressed[i], pCompressedSize[i]);
            }

            // Reorder data
            if (requiredInputChannels != inputChannels)
            {
                const uint32_t pixelCount = width * height;
                for (uint32_t pixelIndex = 0; pixelIndex < pixelCount; ++pixelIndex)
                {
                    uint32_t inputDataIndex = pixelIndex * inputChannels * bytesPerChannel;
                    uint32_t outputDataIndex = pixelIndex * requiredInputChannels * bytesPerChannel;
                    for (uint32_t reorderChannel = 0; reorderChannel < requiredInputChannels * bytesPerChannel; ++reorderChannel)
                    {
                        pData[outputDataIndex + reorderChannel] = pData[inputDataIndex + reorderChannel];
                    }
                }
            }

            rgba_surface input;
            input.width = width;
            input.height = height;
            input.stride = width * requiredInputChannels * bytesPerChannel;
            input.ptr = pData;

            bcCompress(&input, (uint8_t*)ppOutCompressed[i] + compressed_offset);

            if (padded)
            {
                tf_free(pData);
            }
        }
    }

    // Set image size to padding size
    pTexDesc->mWidth = adjustedWidth;
    pTexDesc->mHeight = adjustedHeight;

    return true;
}

bool CompressImageData(void* ppData[MAX_MIPLEVELS], void* ppOutCompressed[MAX_MIPLEVELS], uint32_t* pCompressedSize,
                       CompressImageDescriptor* pDesc, TFTextureDesc* pTextDesc)
{
    if (!ppData[0])
    {
        ppOutCompressed[0] = NULL;
        pCompressedSize[0] = 0;
        return false;
    }

    switch (pDesc->mCompression)
    {
    case COMPRESSION_ASTC:
        return ASTCCompression(ppData, ppOutCompressed, pCompressedSize, pDesc, pTextDesc);
    case COMPRESSION_BC:
        return BCCompression(ppData, ppOutCompressed, pCompressedSize, pDesc, pTextDesc);
    default:
        LOGF(eERROR, "Unknown compression!");
        return false;
    }
}

void ResizeImageData(uint8_t** ppDestData, uint32_t* pDestImageDataSize, uint8_t* pSrcData, TFTextureDesc* pTextDesc,
                     ProcessTexturesParams* pTextureParams, uint32_t targetWidth, uint32_t targetHeight)
{
    uint32_t width = pTextDesc->mWidth;
    uint32_t height = pTextDesc->mHeight;
    uint32_t channels = TinyImageFormat_ChannelCount(pTextDesc->mFormat);

    uint32_t bytePerChannel = 1;

    if (pTextDesc->mFormat == TinyImageFormat_R32G32B32A32_SFLOAT)
    {
        bytePerChannel = 4;
    }

    *ppDestData = (uint8_t*)tf_malloc(targetWidth * targetHeight * channels * bytePerChannel);
    *pDestImageDataSize = targetWidth * targetHeight * channels * bytePerChannel;

    uint8_t* inImageData = pSrcData;
    uint8_t* outImageData = *ppDestData;

    // TODO:
    //  - different color spaces
    //  - other filter options
    //  - other clamping ways
    int result = 0;

    if (pTextDesc->mFormat == TinyImageFormat_R32G32B32A32_SFLOAT)
    {
        result = stbir_resize_float_generic(
            (float*)inImageData, width, height, channels * width * bytePerChannel, (float*)outImageData, targetWidth, targetHeight,
            channels * targetWidth * bytePerChannel, channels, STBIR_ALPHA_CHANNEL_NONE, STBIR_FLAG_ALPHA_USES_COLORSPACE, STBIR_EDGE_CLAMP,
            STBIR_FILTER_DEFAULT, pTextureParams->mInputLinearColorSpace ? STBIR_COLORSPACE_LINEAR : STBIR_COLORSPACE_SRGB, nullptr);
    }
    else
    {
        result = stbir_resize_uint8_generic(
            inImageData, width, height, channels * width, outImageData, targetWidth, targetHeight, channels * targetWidth, channels,
            STBIR_ALPHA_CHANNEL_NONE, STBIR_FLAG_ALPHA_USES_COLORSPACE, STBIR_EDGE_CLAMP, STBIR_FILTER_DEFAULT,
            TinyImageFormat_IsSRGB(pTextDesc->mFormat) ? STBIR_COLORSPACE_SRGB : STBIR_COLORSPACE_LINEAR, nullptr);
    }
    ASSERT(result == 1);
}

void GenerateMipmaps(uint8_t* ppData[MAX_MIPLEVELS], uint32_t* pImageDataSize, TFTextureDesc* pTextDesc,
                     ProcessTexturesParams* pTextureParams)
{
    uint32_t width = pTextDesc->mWidth;
    uint32_t height = pTextDesc->mHeight;
    uint32_t numLevels = max((uint32_t)log2(width), (uint32_t)log2(height)) + 1u;
    uint32_t channels = TinyImageFormat_ChannelCount(pTextDesc->mFormat);
    uint32_t bytesPerChannel = TinyImageFormat_ChannelBitWidthAtPhysical(pTextDesc->mFormat, 0) / 8;

    for (uint32_t i = 1; i < numLevels; ++i)
    {
        uint32_t prevWidth = max(width >> (i - 1u), 1u);
        uint32_t prevHeight = max(height >> (i - 1u), 1u);
        uint32_t mipWidth = max(width >> i, 1u);
        uint32_t mipHeight = max(height >> i, 1u);

        ppData[i] = (uint8_t*)tf_malloc(mipWidth * mipHeight * channels * bytesPerChannel);
        pImageDataSize[i] = mipWidth * mipHeight * channels * bytesPerChannel;

        uint8_t* inImageData = ppData[i - 1];
        uint8_t* outImageData = ppData[i];

        // TODO:
        //  - different color spaces
        //  - other filter options
        //  - other clamping ways
        uint32_t strideInBytes = channels * prevWidth * bytesPerChannel;
        uint32_t strideOutBytes = channels * mipWidth * bytesPerChannel;
        int      result = -1;
        switch (bytesPerChannel)
        {
        case 4:
            result = stbir_resize_float_generic(
                (float*)inImageData, prevWidth, prevHeight, strideInBytes, (float*)outImageData, mipWidth, mipHeight, strideOutBytes,
                channels, STBIR_ALPHA_CHANNEL_NONE, STBIR_FLAG_ALPHA_USES_COLORSPACE, STBIR_EDGE_CLAMP, STBIR_FILTER_DEFAULT,
                pTextureParams->mInputLinearColorSpace ? STBIR_COLORSPACE_LINEAR : STBIR_COLORSPACE_SRGB, nullptr);
            break;
        case 2:
            result = stbir_resize_uint16_generic(
                (uint16_t*)inImageData, prevWidth, prevHeight, strideInBytes, (uint16_t*)outImageData, mipWidth, mipHeight, strideOutBytes,
                channels, STBIR_ALPHA_CHANNEL_NONE, STBIR_FLAG_ALPHA_USES_COLORSPACE, STBIR_EDGE_CLAMP, STBIR_FILTER_DEFAULT,
                pTextureParams->mInputLinearColorSpace ? STBIR_COLORSPACE_LINEAR : STBIR_COLORSPACE_SRGB, nullptr);
            break;
        case 1:
        default:
            result = stbir_resize_uint8_generic(
                inImageData, prevWidth, prevHeight, strideInBytes, outImageData, mipWidth, mipHeight, strideOutBytes, channels,
                STBIR_ALPHA_CHANNEL_NONE, STBIR_FLAG_ALPHA_USES_COLORSPACE, STBIR_EDGE_CLAMP, STBIR_FILTER_DEFAULT,
                pTextureParams->mInputLinearColorSpace ? STBIR_COLORSPACE_LINEAR : STBIR_COLORSPACE_SRGB, nullptr);
            break;
        }

        ASSERT(result == 1);
    }

    pTextDesc->mMipLevels = numLevels;
}

void GenerateVMFFilteredMipmaps(uint8_t* ppData[MAX_MIPLEVELS], uint32_t* pImageDataSize, TFTextureDesc* pTextureDesc,
                                uint32_t channelCount, void* pUserData)
{
    UNREF_PARAM(pImageDataSize);
    ASSERT(channelCount > 2 && "Minimum of 3 channels input is required");

    const uint32_t width = pTextureDesc->mWidth;
    const uint32_t height = pTextureDesc->mHeight;
    const uint32_t numLevels = max((uint32_t)log2(width), (uint32_t)log2(height)) + 1u;
    pTextureDesc->mMipLevels = numLevels;

    vec3* rData[MAX_MIPLEVELS];
    rData[0] = (vec3*)pUserData;

    for (uint32_t mipMap = 1; mipMap < numLevels; ++mipMap)
    {
        uint32_t prevWidth = max(width >> (mipMap - 1u), 1u);
        uint32_t prevHeight = max(height >> (mipMap - 1u), 1u);
        uint32_t mipWidth = max(width >> mipMap, 1u);
        uint32_t mipHeight = max(height >> mipMap, 1u);
        rData[mipMap] = (vec3*)tf_malloc(mipWidth * mipHeight * sizeof(vec3));
        ppData[mipMap] = (uint8_t*)tf_malloc(mipWidth * mipHeight * channelCount);

        for (uint32_t y = 0; y < mipHeight; ++y)
        {
            for (uint32_t x = 0; x < mipWidth; ++x)
            {
                const uint32_t mipPixelIndex = x + y * mipWidth;
                const float    xCenter = clamp((x << 1) + 0.5f, 0.0f, prevWidth - 1.0f);
                const float    yCenter = clamp((y << 1) + 0.5f, 0.0f, prevHeight - 1.0f);
                float          xl = floorf(xCenter);
                float          xu = ceilf(xCenter);
                float          yl = floorf(yCenter);
                float          yu = ceilf(yCenter);

                // bilinear interpolation
                vec3 xlValue = rData[mipMap - 1][(uint32_t)(xl + yl * prevWidth)];
                vec3 xuValue = rData[mipMap - 1][(uint32_t)(xu + yl * prevWidth)];

                vec3 ylValue = rData[mipMap - 1][(uint32_t)(xl + yu * prevWidth)];
                vec3 yuValue = rData[mipMap - 1][(uint32_t)(xu + yu * prevWidth)];

                vec3 horizontalL = xlValue * (xu - xCenter) / (xu - xl) + xuValue * (xCenter - xl) / (xu - xl);
                vec3 horizontalU = ylValue * (xu - xCenter) / (xu - xl) + yuValue * (xCenter - xl) / (xu - xl);

                vec3 rFiltered = horizontalL * (yu - yCenter) / (yu - yl) + horizontalU * (yCenter - yl) / (yu - yl);

                rData[mipMap][mipPixelIndex] = rFiltered;
                rFiltered = normalize(rFiltered);
                // Move normal back to range [0, 1]
                rFiltered = rFiltered * 0.5f + vec3(0.5f);

                const uint32_t mipChannelIndex = mipPixelIndex * channelCount;
                ppData[mipMap][mipChannelIndex + 0] = (uint8_t)(fmaxf(fminf(1.0f, rFiltered[0]), 0.0f) * (float)UINT8_MAX + 0.5f);
                ppData[mipMap][mipChannelIndex + 1] = (uint8_t)(fmaxf(fminf(1.0f, rFiltered[1]), 0.0f) * (float)UINT8_MAX + 0.5f);
                ppData[mipMap][mipChannelIndex + 2] = (uint8_t)(fmaxf(fminf(1.0f, rFiltered[2]), 0.0f) * (float)UINT8_MAX + 0.5f);

                if (channelCount == 4)
                {
                    // Fill alpha if possible
                    ppData[mipMap][mipChannelIndex + 3] = UINT8_MAX;
                }
            }
        }
    }

    for (uint32_t mipMap = 1; mipMap < numLevels; ++mipMap)
    {
        tf_free(rData[mipMap]);
    }
}

bool GenerateVMFLayer(InputTextureData* pNormalTextureData, InputTextureData* pRoughnessTextureData, vec3* rData)
{
    ASSERT(pNormalTextureData);
    ASSERT(pRoughnessTextureData);

    bool success = true;

    /////////////////////////////////
    // Validate input
    /////////////////////////////////
    if (pNormalTextureData->isCompressed || pRoughnessTextureData->isCompressed)
    {
        LOGF(LogLevel::eERROR, "%s: Compressed input data given, requires uncompressed texture data!", __FUNCTION__);
        success = false;
    }

    if (pNormalTextureData->mDesc.mWidth != pRoughnessTextureData->mDesc.mWidth ||
        pNormalTextureData->mDesc.mHeight != pRoughnessTextureData->mDesc.mHeight)
    {
        LOGF(LogLevel::eERROR, "%s: width/height of normal {%u/%u} and roughness {%u/%u} texture do not match!", __FUNCTION__,
             pNormalTextureData->mDesc.mWidth, pNormalTextureData->mDesc.mHeight, pRoughnessTextureData->mDesc.mWidth,
             pRoughnessTextureData->mDesc.mHeight);

        success = false;
    }

    const uint32_t normalTextureChannels = TinyImageFormat_ChannelCount(pNormalTextureData->mDesc.mFormat);
    const uint32_t roughnessTextureChannels = TinyImageFormat_ChannelCount(pRoughnessTextureData->mDesc.mFormat);
    if (normalTextureChannels < 3)
    {
        LOGF(LogLevel::eERROR, "%s: Normal input texture has to few channels %u!", __FUNCTION__, normalTextureChannels);
        success = false;
    }

    /////////////////////////////////
    // Generate layer
    /////////////////////////////////
    if (success)
    {
        uint8_t* textureData = (uint8_t*)pNormalTextureData->pData[0];
        for (uint32_t y = 0; y < pNormalTextureData->mDesc.mHeight; ++y)
        {
            for (uint32_t x = 0; x < pNormalTextureData->mDesc.mWidth; ++x)
            {
                const uint32_t pixelIndex = x + y * pNormalTextureData->mDesc.mWidth;
                const uint32_t pixelIndexNormal = pixelIndex * normalTextureChannels;
                const uint32_t pixelIndexRoughness = pixelIndex * roughnessTextureChannels;

                vec3 normal =
                    vec3(textureData[pixelIndexNormal + 0] / (float)UINT8_MAX, textureData[pixelIndexNormal + 1] / (float)UINT8_MAX,
                         textureData[pixelIndexNormal + 2] / (float)UINT8_MAX);

                // Set normal to range [-1.0, 1.0]
                normal = normal * 2.0f - vec3(1.0f);

                // TODO Optional: Give channel to look into for the rougness value as argument
                // For now expect the first channel to contain the actuall roughness value
                const float roughnessValue = textureData[pixelIndexRoughness + 0] / (float)UINT8_MAX;

                // Convert to r form
                const float invLambda = 0.5f * roughnessValue * roughnessValue;
                const float exp2l = (float)exp(-2.0f / invLambda);
                const float cothLambda = invLambda > 0.1f ? (1.0f + exp2l) / (1.0f - exp2l) : 1.0f;
                vec3        r = normal * (cothLambda - invLambda);
                rData[pixelIndex] = r;
            }
        }
    }

    return success;
}

void SwizzlePixel(uint8_t* pixelData, const TextureSwizzle* swizzle)
{
    const uint32_t channelCount = 4;
    uint8_t        inputData[channelCount];
    memcpy(inputData, pixelData, sizeof(uint8_t) * channelCount);

    for (uint32_t i = 0; i < channelCount; ++i)
    {
        switch (swizzle->mIndices[i])
        {
        case 'r':
        case 'x':
            pixelData[i] = inputData[0];
            break;
        case 'g':
        case 'y':
            pixelData[i] = inputData[1];
            break;
        case 'b':
        case 'z':
            pixelData[i] = inputData[2];
            break;
        case 'a':
        case 'w':
            pixelData[i] = inputData[3];
            break;
        case 'i':
            pixelData[i] = UINT8_MAX - inputData[0];
            break;
        case 'j':
            pixelData[i] = UINT8_MAX - inputData[1];
            break;
        case 'k':
            pixelData[i] = UINT8_MAX - inputData[2];
            break;
        case 'l':
            pixelData[i] = UINT8_MAX - inputData[3];
            break;
        case '1':
            pixelData[i] = UINT8_MAX;
            break;
        case '0':
        default:
            pixelData[i] = 0;
        }
    }
}

void SwizzlePixel(uint16_t* pixelData, const TextureSwizzle* swizzle)
{
    const uint32_t channelCount = 4;
    uint16_t       inputData[channelCount];
    memcpy(inputData, pixelData, sizeof(uint16_t) * channelCount);

    for (uint32_t i = 0; i < channelCount; ++i)
    {
        switch (swizzle->mIndices[i])
        {
        case 'r':
        case 'x':
            pixelData[i] = inputData[0];
            break;
        case 'g':
        case 'y':
            pixelData[i] = inputData[1];
            break;
        case 'b':
        case 'z':
            pixelData[i] = inputData[2];
            break;
        case 'a':
        case 'w':
            pixelData[i] = inputData[3];
            break;
        case '1':
            pixelData[i] = UINT16_MAX;
            break;
        case '0':
        default:
            pixelData[i] = 0;
        }
    }
}

void SwizzlePixel(float_t* pixelData, const TextureSwizzle* swizzle)
{
    const uint32_t channelCount = 4;
    float_t        inputData[channelCount];
    memcpy(inputData, pixelData, sizeof(float_t) * channelCount);

    for (uint32_t i = 0; i < channelCount; ++i)
    {
        switch (swizzle->mIndices[i])
        {
        case 'r':
        case 'x':
            pixelData[i] = inputData[0];
            break;
        case 'g':
        case 'y':
            pixelData[i] = inputData[1];
            break;
        case 'b':
        case 'z':
            pixelData[i] = inputData[2];
            break;
        case 'a':
        case 'w':
            pixelData[i] = inputData[3];
            break;
        case '1':
            pixelData[i] = 1.0f;
            break;
        case '0':
        default:
            pixelData[i] = 0;
        }
    }
}

void SwizzleUncompressedData(void* ppData[MAX_MIPLEVELS], const TFTextureDesc* pTexDesc, TextureSwizzle swizzle,
                             const TextureDataType dataType)
{
    const uint32_t channelCount = TinyImageFormat_ChannelCount(pTexDesc->mFormat);

    for (uint32_t mip = 0; mip < pTexDesc->mMipLevels; ++mip)
    {
        uint32_t mipWidth = max(pTexDesc->mWidth >> mip, 1u);
        uint32_t mipHeight = max(pTexDesc->mHeight >> mip, 1u);

        for (uint32_t y = 0; y < mipHeight; ++y)
        {
            for (uint32_t x = 0; x < mipWidth; ++x)
            {
                const uint32_t pixelIndex = (x + y * mipWidth) * channelCount;
                uint8_t*       p8Data = NULL;
                uint16_t*      p16Data = NULL;
                float_t*       pFData = NULL;
                switch (dataType)
                {
                case TEX_DATA_TYPE_U8:
                    p8Data = (uint8_t*)ppData[mip] + pixelIndex;
                    SwizzlePixel(p8Data, &swizzle);
                    break;
                case TEX_DATA_TYPE_U16:
                case TEX_DATA_TYPE_F16:
                    p16Data = (uint16_t*)ppData[mip] + pixelIndex;
                    SwizzlePixel(p16Data, &swizzle);
                    break;
                case TEX_DATA_TYPE_F32:
                    pFData = (float_t*)ppData[mip] + pixelIndex;
                    SwizzlePixel(pFData, &swizzle);
                    break;
                }
            }
        }
    }
}

void onTextureFound(TFResourceDirectory resourceDir, const char* filename, void* pUserData)
{
    UNREF_PARAM(resourceDir);
    bstring** fileNames = (bstring**)pUserData;
    arrpush(*fileNames, bdynfromcstr(filename));
}

static void FreeInputTextureData(InputTextureData* inputTextureData)
{
    for (uint32_t mip = 0; mip < inputTextureData->mDesc.mMipLevels; ++mip)
    {
        tf_free(inputTextureData->pData[mip]);
        inputTextureData->pData[mip] = NULL;
    }
};

bool ProcessTextures(AssetPipelineParams* assetParams, ProcessTexturesParams* texturesParams)
{
    // TODO:
    //  - Texture arrays
    //  - Cubemaps
    //  - HDR texture support

    bool     error = false;
    // Get all image files
    bstring* inputImgFileNames = NULL;
    size_t   imgFileCount = 0;

    size_t currFileIndex = 0;
    if (texturesParams->mProcessForPackaging)
    {
        imgFileCount = texturesParams->mTextureNameCount;
    }
    else
    {
        if (assetParams->mPathMode == PROCESS_MODE_FILE)
        {
            arrpush(inputImgFileNames, bdynfromcstr(assetParams->mInFilePath));
        }
        else
        {
            DirectorySearch(assetParams->mRDInput, NULL, texturesParams->mInExt, onTextureFound, (void*)&inputImgFileNames,
                            assetParams->mPathMode == PROCESS_MODE_DIRECTORY_RECURSIVE);
        }

        imgFileCount = (uint32_t)arrlenu(inputImgFileNames);
    }

    if (imgFileCount <= 0)
    {
        return false; // nothing to do
    }

    uint32_t            taskCount = 0;
    TextureProcessTask* pTasks = (TextureProcessTask*)tf_calloc(imgFileCount, sizeof(TextureProcessTask));

    for (uint32_t i = 0; i < imgFileCount; ++i)
    {
        ProcessTexturesParams copyTextureParams = *texturesParams;

        const char* inFileName;
        if (texturesParams->mProcessForPackaging)
        {
            inFileName = texturesParams->pInputFileNameList + currFileIndex;
            currFileIndex += strlen(inFileName) + 1;
            copyTextureParams.mInputLinearColorSpace = false;
            copyTextureParams.mProcessAsNormalMap = false;
            if (strstr(inFileName, "_SPEC") != NULL)
            {
                copyTextureParams.mInputLinearColorSpace = true;
                // using the slow algorithm here fixes artifacts on some textures
                copyTextureParams.mOverrideASTC = ASTC_4x4_SLOW;
            }

            if (strstr(inFileName, "_NRM") != NULL)
            {
                copyTextureParams.mProcessAsNormalMap = true;
                copyTextureParams.mInputLinearColorSpace = true;
            }
            /// look for a matching tga file to extract original size (packages only)
            char srcTgafileName[TF_FS_MAX_PATH] = {};
            strcat(srcTgafileName, ".//tga//");
            strcat(srcTgafileName, inFileName);
            fsReplacePathExtension(srcTgafileName, "tga", srcTgafileName);

            uint32_t targetWidth = TVB_ATLAS_FALLBACK_SLICE_SIZE;
            uint32_t targetHeight = TVB_ATLAS_FALLBACK_SLICE_SIZE;
            if (copyTextureParams.mProcessForAtlas == false)
            {
                if (fsFileExist(assetParams->mRDInput, srcTgafileName))
                {
                    ReadTGADimensions(assetParams->mRDInput, srcTgafileName, &targetWidth, &targetHeight);
                }
            }
            copyTextureParams.mTargetWidth = targetWidth;
            copyTextureParams.mTargetHeight = targetHeight;
        }
        else
        {
            ASSERT(inputImgFileNames);
            inFileName = (char*)inputImgFileNames[i].data;
        }

        char inExtension[TF_FS_MAX_PATH] = { 0 };
        fsGetPathExtension(inFileName, inExtension);

        char outFileName[TF_FS_MAX_PATH] = { 0 };

        if (assetParams->mOutSubdir)
        {
            char fileName[TF_FS_MAX_PATH] = {};
            fsGetPathFileName(inFileName, fileName);

            strcat(fileName, ".tex");

            fsAppendPathComponent(assetParams->mOutSubdir, fileName, outFileName);
        }
        else
        {
            fsReplacePathExtension(inFileName, "tex", outFileName);
        }

        // If input file newer than output file redo compression
        if (!assetParams->mSettings.force && fsFileExist(assetParams->mRDOutput, outFileName))
        {
            time_t lastModified = fsGetLastModifiedTime(assetParams->mRDInput, inFileName);
            if (assetParams->mAdditionalModifiedTime != 0)
                lastModified = max(lastModified, assetParams->mAdditionalModifiedTime);

            time_t lastProcessed = fsGetLastModifiedTime(assetParams->mRDOutput, outFileName);

            if (lastModified < lastProcessed)
            {
                LOGF(eINFO, "Skipping %s", inFileName);
                continue;
            }
        }

        LOGF(eINFO, "Converting texture %s from .%s to .%s with output container : %s", inFileName, copyTextureParams.mInExt, "tex",
             gExtensions[copyTextureParams.mContainer]);

        // Make sure output folder exists
        {
            char assetPath[TF_FS_MAX_PATH] = {};
            fsGetParentPath(outFileName, assetPath);
            fsCreateDirectory(assetParams->mRDOutput, assetPath, true);
        }

        // Populate task and run synchronously
        if (pTasks != NULL)
        {
            TextureProcessTask* pTask = &pTasks[taskCount++];
            if (pTask != NULL)
            {
                pTask->mAssetParams = *assetParams;
                pTask->mTexturesParams = copyTextureParams;
                pTask->mError = false;
                snprintf(pTask->mInFileName, TF_FS_MAX_PATH, "%s", inFileName);
                snprintf(pTask->mInExtension, TF_FS_MAX_PATH, "%s", inExtension);
                snprintf(pTask->mOutFileName, TF_FS_MAX_PATH, "%s", outFileName);
            }
        }
    }

    if (taskCount > 0)
    {
        // Run all the tasks
        ThreadSystem threadSystem;
        threadSystemInit(&threadSystem, &gThreadSystemInitDescDefault);
        threadSystemAddTaskGroup(threadSystem, ProcessTextureTask, taskCount, pTasks);
        threadSystemWaitIdle(threadSystem);
        threadSystemExit(&threadSystem, &gThreadSystemExitDescDefault);

        for (uint32_t i = 0; i < taskCount; ++i)
        {
            error |= pTasks[i].mError;

            if (pTasks[i].mHasOutData)
            {
                arrpush(*texturesParams->ppOutProcessedTextureData, pTasks[i].mOutData);
            }
        }
    }

    tf_free(pTasks);
    pTasks = NULL;

    if (inputImgFileNames)
    {
        for (uint32_t i = 0; i < imgFileCount; ++i)
        {
            bdestroy(&inputImgFileNames[i]);
        }

        arrfree(inputImgFileNames);
        inputImgFileNames = NULL;
    }

    return error;
}

static void ProcessTextureTask(void* pData, uint64_t)
{
    {
        TextureProcessTask*   pTask = (TextureProcessTask*)pData;
        AssetPipelineParams*  assetParams = &pTask->mAssetParams;
        ProcessTexturesParams copyTextureParams = pTask->mTexturesParams;
        const char*           inFileName = pTask->mInFileName;
        const char*           inExtension = pTask->mInExtension;
        const char*           outFileName = pTask->mOutFileName;

        // per-task error flag
        bool& error = pTask->mError;

        TinyImageFormat outFormat = TinyImageFormat_UNDEFINED;
        bool            useVMF = copyTextureParams.pRoughnessFilePath != NULL;

        /////////////////////////////////
        // Load raw image data
        ////////////////////////////////
        InputTextureData inputTextureData = {};
        if (!LoadTextureData(assetParams->mRDInput, inFileName, inExtension, &copyTextureParams, &inputTextureData))
        {
            error = true;
            return;
        }

        /////////////////////////////////
        // vMF
        /////////////////////////////////
        vec3* rData = nullptr;

        if (useVMF)
        {
            InputTextureData inputRoughnessTextureData = {};
            if (!LoadTextureData(assetParams->mRDInput, copyTextureParams.pRoughnessFilePath, inExtension, &copyTextureParams,
                                 &inputRoughnessTextureData))
            {
                FreeInputTextureData(&inputTextureData);
                error = true;
                return;
            }

            rData = (vec3*)tf_malloc(inputTextureData.mDesc.mWidth * inputTextureData.mDesc.mHeight * sizeof(vec3));

            if (!GenerateVMFLayer(&inputTextureData, &inputRoughnessTextureData, rData))
            {
                error = true;
            }

            // Release rougness texture data
            for (size_t mip = 0; mip < inputTextureData.mDesc.mMipLevels; ++mip)
            {
                tf_free(inputRoughnessTextureData.pData[mip]);
                inputRoughnessTextureData.pData[mip] = NULL;
                inputRoughnessTextureData.mDataSize[mip] = 0;
            }

            copyTextureParams.pCallbackUserData = rData;
            copyTextureParams.mGenerateMipmaps = TextureMipmap::MIPMAP_CUSTOM;
            copyTextureParams.pGenerateMipmapsCallback = GenerateVMFFilteredMipmaps;

            if (copyTextureParams.mCompression == COMPRESSION_BC && copyTextureParams.mOverrideBC == DXT_NONE)
            {
                LOGF(eINFO, "Using DXT_BC5 compression for vMF output");
                copyTextureParams.mOverrideBC = DXT_BC5;
            }
        }

        /////////////////////////////////
        // Generate mipmaps
        /////////////////////////////////
        if (copyTextureParams.mGenerateMipmaps == MIPMAP_CUSTOM)
        {
            ASSERT(copyTextureParams.pGenerateMipmapsCallback && "MIPMAP_CUSTOM requires pGenerateMipmapsCallback to be set");
            if (inputTextureData.pData[0])
            {
                uint32_t channels = TinyImageFormat_ChannelCount(inputTextureData.mDesc.mFormat);
                copyTextureParams.pGenerateMipmapsCallback((uint8_t**)inputTextureData.pData, inputTextureData.mDataSize,
                                                           &inputTextureData.mDesc, channels, copyTextureParams.pCallbackUserData);
            }
        }

        uint32_t targetWidth = copyTextureParams.mTargetWidth;
        uint32_t targetHeight = copyTextureParams.mTargetHeight;

        if (targetWidth > 0 && targetHeight > 0)
        {
            uint8_t* pDestImageData = NULL;
            uint32_t destImageSize = 0;

            ResizeImageData(&pDestImageData, &destImageSize, (uint8_t*)inputTextureData.pData[0], &inputTextureData.mDesc,
                            &copyTextureParams, targetWidth, targetHeight);

            tf_free(inputTextureData.pData[0]);
            inputTextureData.pData[0] = pDestImageData;
            inputTextureData.mDesc.mWidth = targetWidth;
            inputTextureData.mDesc.mHeight = targetHeight;
        }

        if (copyTextureParams.mGenerateMipmaps == MIPMAP_DEFAULT && inputTextureData.mDesc.mMipLevels <= 1 &&
            !inputTextureData.isCompressed)
        {
            if (inputTextureData.pData[0])
            {
                GenerateMipmaps((uint8_t**)inputTextureData.pData, inputTextureData.mDataSize, &inputTextureData.mDesc, &copyTextureParams);
            }
        }

        if (useVMF)
        {
            tf_free(rData);
        }

        /////////////////////////////////
        // Swizzle uncompressed data
        ////////////////////////////////
        if (copyTextureParams.mSwizzleChannelCount > 0)
        {
            if (inputTextureData.isCompressed)
            {
                LOGF(eWARNING, "Input texture is compressed, cannot be swizzled");
                FreeInputTextureData(&inputTextureData);
                error = true;
                return;
            }
            SwizzleUncompressedData(inputTextureData.pData, &inputTextureData.mDesc, copyTextureParams.mSwizzle,
                                    inputTextureData.mDataType);
        }

        /////////////////////////////////
        // Compress
        /////////////////////////////////
        CompressImageDescriptor compressDesc = {};

        if (inputTextureData.isCompressed)
        {
            outFormat = inputTextureData.mDesc.mFormat;
            LOGF(eWARNING, "Input texture '%s' is already compressed {%s}, copy texture to destination", inFileName,
                 TinyImageFormat_Name(outFormat));
        }
        else
        {
            outFormat = GetOutputTextureFormat(&copyTextureParams, &inputTextureData.mDesc, &compressDesc);
        }

        void*    pCompressedData[MAX_MIPLEVELS] = { NULL };
        uint32_t compressedDataSize[MAX_MIPLEVELS] = { 0 };

        if (outFormat == TinyImageFormat_UNDEFINED)
        {
            LOGF(eERROR, "Undefined Image format");
            FreeInputTextureData(&inputTextureData);
            error = true;
            return;
        }

        if (!inputTextureData.isCompressed && copyTextureParams.mCompression != TextureCompression::COMPRESSION_NONE)
        {
            // Process raw image data
            if (!CompressImageData(inputTextureData.pData, pCompressedData, compressedDataSize, &compressDesc, &inputTextureData.mDesc))
            {
                LOGF(eERROR, "Failed to compress texture %s", inFileName);
                error = true;
            }

            // Free raw image data, can be released once image is compressed
            if (inputTextureData.pData[0])
            {
                // Release image data
                for (uint32_t mip = 0; mip < inputTextureData.mDesc.mMipLevels; ++mip)
                {
                    stbi_image_free(inputTextureData.pData[mip]);
                    inputTextureData.pData[mip] = NULL;
                }
            }
        }
        else
        {
            // Set raw pImageData as out data
            for (uint32_t mip = 0; mip < inputTextureData.mDesc.mMipLevels; ++mip)
            {
                pCompressedData[mip] = inputTextureData.pData[mip];
                compressedDataSize[mip] = inputTextureData.mDataSize[mip];
            }
        }

        /////////////////////////////////
        // Write output
        /////////////////////////////////

        // Remove old file
        if (!error)
        {
            fsRemoveFile(assetParams->mRDOutput, outFileName);
        }

        // NOTE: output directory was pre-created by ProcessTextures before dispatch

        TFFileStream outFile = {};
        if (!fsOpenStreamFromPath(assetParams->mRDOutput, outFileName, TF_FM_WRITE, &outFile))
        {
            LOGF(eERROR, "Could not open file '%s' for write.", outFileName);
            error = true;

            for (size_t mip = 0; mip < inputTextureData.mDesc.mMipLevels; ++mip)
            {
                tf_free(pCompressedData[mip]);
                pCompressedData[mip] = NULL;
            }
            return;
        }

        // Write .ktx file
        bool isCubemap = (inputTextureData.mDesc.mDescriptors & TF_DESCRIPTOR_TYPE_TEXTURE_CUBE) == TF_DESCRIPTOR_TYPE_TEXTURE_CUBE;
        if (copyTextureParams.mProcessAsCubeMap)
        {
            if (TinyImageFormat_IsSRGB(outFormat) && copyTextureParams.mInputLinearColorSpace)
            {
                outFormat = TinyImageFormat_ToUNORM(outFormat);
            }
            isCubemap = true;
            if (inputTextureData.mDesc.mWidth * 6 != inputTextureData.mDesc.mHeight)
            {
                LOGF(eERROR, "Cubemap must have 6 images vertically, width should be 1/6 the height ");
                error = true;
            }
            inputTextureData.mDesc.mHeight = inputTextureData.mDesc.mWidth;
        }
        // Array size in the disk image needs to be 1 since we'll already multiply it by 6 when loading the texture in runtime
        const uint32_t arraySize = isCubemap ? inputTextureData.mDesc.mArraySize / 6 : inputTextureData.mDesc.mArraySize;
        if (copyTextureParams.mContainer == CONTAINER_KTX)
        {
            TinyKtx_Format outKtxFormat = TinyImageFormat_ToTinyKtxFormat(outFormat);
            if (!TinyKtx_WriteImage(&ktxWriteCallbacks, &outFile, inputTextureData.mDesc.mWidth, inputTextureData.mDesc.mHeight,
                                    inputTextureData.mDesc.mDepth, arraySize, inputTextureData.mDesc.mMipLevels, outKtxFormat, isCubemap,
                                    compressedDataSize, (const void**)pCompressedData))
            {
                LOGF(eERROR, "Couldn't create ktx file '%s' with format '%s'", outFileName, TinyImageFormat_Name(outFormat));
                error = true;
            }
        }
        // Write .dds file
        else if (copyTextureParams.mContainer == CONTAINER_DDS)
        {
            TinyDDS_Format outDDSFormat = TinyImageFormat_ToTinyDDSFormat(outFormat);
            if (!TinyDDS_WriteImage(&ddsWriteCallbacks, &outFile, inputTextureData.mDesc.mWidth, inputTextureData.mDesc.mHeight,
                                    inputTextureData.mDesc.mDepth, arraySize, inputTextureData.mDesc.mMipLevels, outDDSFormat, isCubemap,
                                    false, compressedDataSize, (const void**)pCompressedData))
            {
                LOGF(eERROR, "Couldn't create dds file '%s' with format '%s'", outFileName, TinyImageFormat_Name(outFormat));
                error = true;
            }
        }
#ifdef XBOX_SCARLETT_DDS
        else if (copyTextureParams.mContainer == CONTAINER_SCARLETT_DDS)
        {
            extern bool swizzleAndWriteDds(TinyDDS_WriteCallbacks const* callbacks, void* user, uint32_t width, uint32_t height,
                                           uint32_t depth, uint32_t slices, uint32_t mipmaplevels, TinyDDS_Format format, bool cubemap,
                                           uint32_t const* mipmapsizes, void const** mipmaps);

            TinyDDS_Format outDDSFormat = TinyImageFormat_ToTinyDDSFormat(outFormat);
            if (!swizzleAndWriteDds(&ddsWriteCallbacks, &outFile, inputTextureData.mDesc.mWidth, inputTextureData.mDesc.mHeight,
                                    inputTextureData.mDesc.mDepth, inputTextureData.mDesc.mArraySize, inputTextureData.mDesc.mMipLevels,
                                    outDDSFormat, isCubemap, compressedDataSize, (const void**)pCompressedData))
            {
                LOGF(eERROR, "Couldn't create Scarlett dds file '%s' with format '%s'", outFileName, TinyImageFormat_Name(outFormat));
                error = true;
            }
        }
#endif
#ifdef PROSPERO_GNF
        else if (copyTextureParams.mContainer == CONTAINER_GNF_ORBIS || copyTextureParams.mContainer == CONTAINER_GNF_PROSPERO)
        {
            extern bool writeGnfTexture(TFFileStream * outFile, uint32_t width, uint32_t height, uint32_t depth, uint32_t slices,
                                        uint32_t mipmaplevels, TinyImageFormat format, bool cubemap, TextureContainer outTexContainer,
                                        uint32_t tilingQuality, uint32_t const* mipmapsizes, void const** mipmaps);

            if (!writeGnfTexture(&outFile, inputTextureData.mDesc.mWidth, inputTextureData.mDesc.mHeight, inputTextureData.mDesc.mDepth,
                                 inputTextureData.mDesc.mArraySize, inputTextureData.mDesc.mMipLevels, outFormat, isCubemap,
                                 copyTextureParams.mContainer, 1, compressedDataSize, (const void**)pCompressedData))
            {
                LOGF(eERROR, "Couldn't create gnf file '%s' with format '%s'", outFileName, TinyImageFormat_Name(outFormat));
                error = true;
            }
        }
#endif
        else
        {
            ASSERT(false && "No supported output extension");
        }

        // Close out file stream
        fsCloseStream(&outFile);

        if (pCompressedData[0])
        {
            for (size_t mip = 0; mip < inputTextureData.mDesc.mMipLevels; ++mip)
            {
                tf_free(pCompressedData[mip]);
                pCompressedData[mip] = NULL;
                compressedDataSize[mip] = 0;
            }
        }

        if (copyTextureParams.ppOutProcessedTextureData)
        {
            pTask->mOutData.mOutputFilePath = bdynfromcstr(outFileName);
            pTask->mOutData.mWidth = inputTextureData.mDesc.mWidth;
            pTask->mOutData.mHeight = inputTextureData.mDesc.mHeight;
            pTask->mOutData.mDepth = inputTextureData.mDesc.mDepth;
            pTask->mOutData.mArraySize = inputTextureData.mDesc.mArraySize;
            pTask->mOutData.mMipLevels = inputTextureData.mDesc.mMipLevels;
            pTask->mOutData.mFormat = (uint32_t)outFormat;
            pTask->mHasOutData = true;
        }

        // Remove the output file if it was created but process textures failed.
        if (error)
        {
            fsRemoveFile(assetParams->mRDOutput, outFileName);
        }
    }
}

bool ProcessIES(AssetPipelineParams* assetParams, TextureContainer outContainer)
{
    bool error = false;

    // Get all IES files
    bstring* iesFiles = NULL;
    uint32_t iesFileCount = 0;

    if (assetParams->mPathMode == PROCESS_MODE_FILE)
    {
        bstring bstrPath = bempty();
        bassigncstr(&bstrPath, assetParams->mInFilePath);
        arrpush(iesFiles, bstrPath);
    }
    else
    {
        DirectorySearch(assetParams->mRDInput, NULL, "ies", onTextureFound, (void*)&iesFiles,
                        assetParams->mPathMode == PROCESS_MODE_DIRECTORY_RECURSIVE);
    }

    iesFileCount = (uint32_t)arrlenu(iesFiles);

    for (uint32_t i = 0; i < iesFileCount; i++)
    {
        const char* fileName = (char*)iesFiles[i].data;

        char newFileName[TF_FS_MAX_PATH] = { 0 };
        if (assetParams->mOutSubdir)
        {
            char extractedFileName[TF_FS_MAX_PATH] = {};
            fsGetPathFileName(fileName, extractedFileName);

            strcat(extractedFileName, ".ies");
            fsAppendPathComponent(assetParams->mOutSubdir, extractedFileName, newFileName);
        }
        else
        {
            fsReplacePathExtension(fileName, "ies", newFileName);
        }

        if (!assetParams->mSettings.force && fsFileExist(assetParams->mRDOutput, newFileName))
        {
            time_t lastModified = fsGetLastModifiedTime(assetParams->mRDInput, fileName);
            if (assetParams->mAdditionalModifiedTime != 0)
                lastModified = max(lastModified, assetParams->mAdditionalModifiedTime);
            time_t lastProcessed = fsGetLastModifiedTime(assetParams->mRDOutput, newFileName);

            if (lastModified < lastProcessed)
            {
                LOGF(eINFO, "Skipping %s", fileName);
                continue;
            }
        }

        LOGF(eINFO, "Converting %s to texture", fileName);

        TFFileStream file = {};
        if (!fsOpenStreamFromPath(assetParams->mRDInput, fileName, TF_FM_READ, &file))
        {
            LOGF(eERROR, "Failed to open IES file %s", fileName);
            error = true;
            continue;
        }

        char outFileName[TF_FS_MAX_PATH] = { 0 };
        if (assetParams->mOutSubdir)
        {
            char name[TF_FS_MAX_PATH] = {};
            fsGetPathFileName(fileName, name);
            strcat(name, ".tex");
            fsAppendPathComponent(assetParams->mOutSubdir, name, outFileName);
        }
        else
        {
            fsReplacePathExtension(fileName, "tex", outFileName);
        }

        ssize_t fileSize = fsGetStreamFileSize(&file);
        void*   fileData = tf_malloc(fileSize);

        fsReadFromStream(&file, fileData, fileSize);
        fsCloseStream(&file);

        char* pRead = (char*)fileData;

        // Check header
        const char* iesHeader = "IESNA:LM-63-";
        if (strncmp(pRead, iesHeader, strlen(iesHeader)) != 0)
        {
            LOGF(eERROR, "IES file does not contain required header: %s", assetParams->mInFilePath);
            error = true;
            tf_free(fileData);
            continue;
        }
        do
        {
            pRead++;
        } while (*(pRead - 1) != '\n');

        // Skip keyword lines
        while (*pRead == '[')
        {
            do
            {
                pRead++;
            } while (*(pRead - 1) != '\n');
        }

        // Error on tilt != NONE
        const char* tiltRequired = "TILT=NONE";
        if (strncmp(pRead, tiltRequired, strlen(tiltRequired)) != 0)
        {
            LOGF(eERROR, "Unsupported value for tilt in %s", assetParams->mInFilePath);
            error = true;
            tf_free(fileData);
            continue;
        }
        do
        {
            pRead++;
        } while (*(pRead - 1) != '\n');

        float* pValues = NULL;
        // Read values
        while (pRead - (char*)fileData < fileSize)
        {
            if ((*pRead >= '0' && *pRead <= '9') || *pRead == '.')
            {
                arrpush(pValues, strtof(pRead, &pRead));
            }
            pRead++;
        }
        tf_free(fileData);

        // Bake to texture
        const uint32_t width = 32;  // Horizontal
        const uint32_t height = 32; // Vertical
        float*         pImageData = (float*)tf_malloc(width * height * sizeof(float));
        float          maxCandela = 0;
        {
            float    candelaMultiplier = pValues[2];
            uint32_t numAnglesVertical = (uint32_t)roundf(pValues[3]);
            uint32_t numAnglesHorizontal = (uint32_t)roundf(pValues[4]);
            float    ballastFactor = pValues[10];
            float*   pAnglesVertical = &pValues[13];
            float*   pAnglesHorizontal = &pValues[13 + numAnglesVertical];
            float*   pCandela = &pValues[13 + numAnglesVertical + numAnglesHorizontal];

            for (uint32_t j = 0; j < numAnglesHorizontal * numAnglesVertical; j++)
            {
                maxCandela = pCandela[j] > maxCandela ? pCandela[j] : maxCandela;
            }
            maxCandela *= candelaMultiplier * ballastFactor;

            uint32_t angleVertical = 0;
            for (uint32_t y = 0; y < height; y++)
            {
                float angleY = ((float)y / (height - 1)) * 180;

                while (angleVertical + 1 < numAnglesVertical && pAnglesVertical[angleVertical + 1] <= angleY)
                {
                    angleVertical++;
                }
                uint32_t secondAngleVertical = (angleVertical + 1 < numAnglesVertical) ? angleVertical + 1 : angleVertical;

                float ratioVertical = 0.0;
                if (angleVertical != secondAngleVertical)
                {
                    ratioVertical =
                        (angleY - pAnglesVertical[angleVertical]) / (pAnglesVertical[secondAngleVertical] - pAnglesVertical[angleVertical]);
                }

                uint32_t angleHorizontal = 0;
                for (uint32_t x = 0; x < width; x++)
                {
                    float angleX = ((float)x / (width - 1)) * 360;

                    while (angleHorizontal + 1 < numAnglesHorizontal && pAnglesHorizontal[angleHorizontal + 1] <= angleX)
                    {
                        angleHorizontal++;
                    }
                    uint32_t secondAngleHorizontal = (angleHorizontal + 1 < numAnglesHorizontal) ? angleHorizontal + 1 : 0;

                    float ratioHorizontal = 0.0;
                    if (secondAngleHorizontal < angleHorizontal)
                    {
                        ratioHorizontal = (angleX - pAnglesHorizontal[angleHorizontal]) /
                                          (pAnglesHorizontal[secondAngleHorizontal] + 360.0f - pAnglesHorizontal[angleHorizontal]);
                    }
                    else if (angleHorizontal != secondAngleHorizontal)
                    {
                        ratioHorizontal = (angleX - pAnglesHorizontal[angleHorizontal]) /
                                          (pAnglesHorizontal[secondAngleHorizontal] - pAnglesHorizontal[angleHorizontal]);
                    }

                    uint2 vertexIndices[4];
                    vertexIndices[0] = uint2(angleHorizontal, angleVertical);
                    vertexIndices[1] = uint2(secondAngleHorizontal, angleVertical);
                    vertexIndices[2] = uint2(angleHorizontal, secondAngleVertical);
                    vertexIndices[3] = uint2(secondAngleHorizontal, secondAngleVertical);

                    float candela[4];
                    for (int j = 0; j < 4; j++)
                    {
                        candela[j] = pCandela[vertexIndices[j].x * numAnglesVertical + vertexIndices[j].y];
                    }

                    // Bilinear interpolation
                    float interpolatedCandela = candela[0] + (candela[1] - candela[0]) * ratioHorizontal +
                                                (candela[2] - candela[0]) * ratioVertical +
                                                (candela[3] - candela[1] - candela[2] + candela[0]) * ratioHorizontal * ratioVertical;

                    float outputCandela = interpolatedCandela * candelaMultiplier * ballastFactor;
                    float outputFactor = outputCandela / maxCandela;

                    pImageData[y * width + x] = outputFactor;
                }
            }
        }

        arrfree(pValues);

        float*   ppFloatData[MAX_MIPLEVELS] = { NULL };
        uint32_t pImageDataSize[MAX_MIPLEVELS] = { 0 };
        ppFloatData[0] = pImageData;
        pImageDataSize[0] = width * height * sizeof(float);

        // Generate mipmaps
        uint32_t numLevels = max((uint32_t)log2(width), (uint32_t)log2(height)) + 1u;
        for (uint32_t j = 1; j < numLevels; ++j)
        {
            uint32_t prevWidth = max(width >> (j - 1u), 1u);
            uint32_t prevHeight = max(height >> (j - 1u), 1u);
            uint32_t mipWidth = max(width >> j, 1u);
            uint32_t mipHeight = max(height >> j, 1u);
            ppFloatData[j] = (float*)tf_malloc(mipWidth * mipHeight * sizeof(float));
            pImageDataSize[j] = mipWidth * mipHeight * sizeof(float);

            float* inImageData = ppFloatData[j - 1];
            float* outImageData = ppFloatData[j];
            int result = stbir_resize_float_generic(inImageData, prevWidth, prevHeight, prevWidth * sizeof(float), outImageData, mipWidth,
                                                    mipHeight, mipWidth * sizeof(float), 1, -1, 1 << 1, STBIR_EDGE_CLAMP,
                                                    STBIR_FILTER_DEFAULT, STBIR_COLORSPACE_LINEAR, nullptr);
            ASSERT(result == 1);
        }

        // Maximum value in 1x1 and 2x2 mips
        const float maxCandelaLimit = 10000.0f;
        float       maxCandelaFactor = clamp(maxCandela, 0.0f, maxCandelaLimit) / maxCandelaLimit;
        ppFloatData[numLevels - 1][0] = maxCandelaFactor;
        for (uint32_t j = 0; j < 4; j++)
            ppFloatData[numLevels - 2][j] = maxCandelaFactor;

        // Convert to R16_SFLOAT
        TinyImageFormat outFormat = TinyImageFormat_R16_SFLOAT;
        half*           ppData[MAX_MIPLEVELS] = { NULL };
        for (uint32_t lvl = 0; lvl < numLevels; lvl++)
        {
            pImageDataSize[lvl] /= sizeof(float) / sizeof(half);
            ppData[lvl] = (half*)tf_malloc(pImageDataSize[lvl]);
            for (uint32_t j = 0; j < pImageDataSize[lvl] / sizeof(half); j++)
            {
                ppData[lvl][j] = half(ppFloatData[lvl][j]);
            }
            tf_free(ppFloatData[lvl]);
        }

        // Write output
        {
            fsRemoveFile(assetParams->mRDOutput, outFileName);
            {
                char assetPath[TF_FS_MAX_PATH] = {};
                fsGetParentPath(outFileName, assetPath);
                fsCreateDirectory(assetParams->mRDOutput, assetPath, true);
            }
            TFFileStream outFile = {};
            if (!fsOpenStreamFromPath(assetParams->mRDOutput, outFileName, TF_FM_WRITE, &outFile))
            {
                LOGF(eERROR, "Could not open file '%s' for write.", outFileName);
                error = true;
                for (uint32_t j = 0; j < numLevels; j++)
                {
                    if (ppData[j])
                        tf_free(ppData[j]);
                }
                continue;
            }

            if (outContainer == CONTAINER_KTX)
            {
                TinyKtx_Format outKtxFormat = TinyImageFormat_ToTinyKtxFormat(outFormat);
                if (!TinyKtx_WriteImage(&ktxWriteCallbacks, &outFile, width, height, 1, 1, numLevels, outKtxFormat, false, pImageDataSize,
                                        (const void**)ppData))
                {
                    LOGF(eERROR, "Couldn't create ktx file '%s' with format '%s'", outFileName, TinyImageFormat_Name(outFormat));
                    error = true;
                }
            }
            // Write .dds file
            else if (outContainer == CONTAINER_DDS)
            {
                TinyDDS_Format outDDSFormat = TinyImageFormat_ToTinyDDSFormat(outFormat);
                if (!TinyDDS_WriteImage(&ddsWriteCallbacks, &outFile, width, height, 1, 1, numLevels, outDDSFormat, false, false,
                                        pImageDataSize, (const void**)ppData))
                {
                    LOGF(eERROR, "Couldn't create dds file '%s' with format '%s'", outFileName, TinyImageFormat_Name(outFormat));
                    error = true;
                }
            }
#ifdef XBOX_SCARLETT_DDS
            else if (outContainer == CONTAINER_SCARLETT_DDS)
            {
                extern bool swizzleAndWriteDds(TinyDDS_WriteCallbacks const* callbacks, void* user, uint32_t width, uint32_t height,
                                               uint32_t depth, uint32_t slices, uint32_t mipmaplevels, TinyDDS_Format format, bool cubemap,
                                               uint32_t const* mipmapsizes, void const** mipmaps);

                TinyDDS_Format outDDSFormat = TinyImageFormat_ToTinyDDSFormat(outFormat);
                if (!swizzleAndWriteDds(&ddsWriteCallbacks, &outFile, width, height, 1, 1, numLevels, outDDSFormat, false, pImageDataSize,
                                        (const void**)ppData))
                {
                    LOGF(eERROR, "Couldn't create Scarlett dds file '%s' with format '%s'", outFileName, TinyImageFormat_Name(outFormat));
                    error = true;
                }
            }
#endif
#ifdef PROSPERO_GNF
            else if (outContainer == CONTAINER_GNF_ORBIS || outContainer == CONTAINER_GNF_PROSPERO)
            {
                extern bool writeGnfTexture(TFFileStream * outFile, uint32_t width, uint32_t height, uint32_t depth, uint32_t slices,
                                            uint32_t mipmaplevels, TinyImageFormat format, bool cubemap, TextureContainer outTexContainer,
                                            uint32_t tilingQuality, uint32_t const* mipmapsizes, void const** mipmaps);

                if (!writeGnfTexture(&outFile, width, height, 1, 1, numLevels, outFormat, false, outContainer, 1, pImageDataSize,
                                     (const void**)ppData))
                {
                    LOGF(eERROR, "Couldn't create gnf file '%s' with format '%s'", outFileName, TinyImageFormat_Name(outFormat));
                    error = true;
                }
            }
#endif
            else
            {
                ASSERT(false && "No supported output extension");
            }
        }

        for (uint32_t j = 0; j < numLevels; j++)
        {
            if (ppData[j])
                tf_free(ppData[j]);
        }
    }

    arrfree(iesFiles);

    return error;
}