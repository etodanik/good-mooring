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

// Unit Test for testing transformations using a solar system.
// Tests the basic mat4 transformations, such as scaling, rotation, and translation.

// Interfaces
#include "../../../../Common_3/Application/Interfaces/IApp.h"
#include "../../../../Common_3/Application/Interfaces/ICamera.h"
#include "../../../../Common_3/Application/Interfaces/IFont.h"
#include "../../../../Common_3/OS/Interfaces/IInput.h"
#include "../../../../Common_3/Application/Interfaces/IProfiler.h"
#include "../../../../Common_3/Application/Interfaces/IScreenshot.h"
#include "../../../../Common_3/Application/Interfaces/IUI.h"
#include "../../../../Common_3/Game/Interfaces/IScripting.h"
#include "../../../../Common_3/Graphics/Interfaces/IGraphics.h"
#include "../../../../Common_3/Resources/ResourceLoader/Interfaces/IResourceLoader.h"
#include "../../../../Common_3/Utilities/Interfaces/IFileSystem.h"
#include "../../../../Common_3/Utilities/Interfaces/ILog.h"
#include "../../../../Common_3/Utilities/Interfaces/ITime.h"

#include "../../../../Common_3/Utilities/RingBuffer.h"
// Math
#include "../../../../Common_3/Utilities/Interfaces/IMath.h"

#include "../../../../Common_3/Utilities/Interfaces/IMemory.h"

// fsl
#include "../../../../Common_3/Graphics/FSL/defaults.h"
#include "./Shaders/FSL/Global.srt.h"

#define SIZEOF_ARR(x) sizeof(x) / sizeof(x[0])

/// Demo structures

struct UniformBlock
{
    TFCameraMatrix mProjectView;
    mat4           mModelMatCapsule;
    mat4           mModelMatCube;
    mat4           mModelOcclusion;
};

struct Vertex
{
    float3 mPosition;
    float3 mNormal;
    float2 mUV;
};

// #NOTE: Two sets of resources (one in flight and one being used on CPU)
const uint32_t gDataBufferCount = 2;

ProfileToken gGpuProfileToken;
TFRenderer*  pRenderer = NULL;

const int   gSphereResolution = 128;
const float gSphereDiameter = 0.5f;

int    gNumberOfSpherePoints = 0;
float* pSpherePoints = nullptr;

TFQueue*   pGraphicsQueue = NULL;
GpuCmdRing gGraphicsCmdRing = {};

TFSwapChain*    pSwapChain = NULL;
TFRenderTarget* pDepthBuffer = NULL;
TFSemaphore*    pImageAcquiredSemaphore[gDataBufferCount] = { NULL };

TFShader*   pBasicShader = NULL;
TFPipeline* pBasicPipeline = NULL;

TFShader*   pSkyboxShader = NULL;
TFBuffer*   pSkyboxVertexBuffer = NULL;
TFPipeline* pPipelineSkybox = NULL;
TFSampler*  pSamplerSkybox = NULL;
TFTexture*  pSkyboxTextures[6];

// Zip File Test Texture
TFTexture*  pZipTexture[1];
TFShader*   pZipTextureShader = NULL;
TFShader*   pOcclusionShader = NULL;
TFBuffer*   pZipTextureVertexBuffer = NULL;
TFPipeline* pZipTexturePipeline = NULL;

// Occlusion Query
const uint32_t gMaxOcclusionQueries = 2 * VR_MULTIVIEW_COUNT;
const uint32_t gOccTestOcclusionSphereMaxIndex = 0;
const uint32_t gOccTestOcclusionSphereIndex = 1;

TFQueryPool* pOcclusionQueryPool[gDataBufferCount] = {};
TFPipeline*  pOcclusionMax = NULL;
TFPipeline*  pOcclusionTest = NULL;
TFBuffer*    pSphereVertexBuffer = NULL;

unsigned char gOcclusionText[256];
bstring       gOcclusionbstr = bemptyfromarr(gOcclusionText);
float4        gOcclusion1Color = float4(0.0f, 1.0f, 0.0f, 1.0f);

TFBuffer* pProjViewUniformBuffer[gDataBufferCount] = { NULL };

TFDescriptorSet* pDescriptorSetPersistent = NULL;
TFDescriptorSet* pDescriptorSetFramePerFrame = NULL;

int          gNumberOfBasicPoints;
int          gNumberOfCubiodPoints;
UniformBlock gUniformData;

TFICamera* pCamera = NULL;

// VR 2D layer transform (positioned at -1 along the Z axis, default rotation, default scale)
TFVR2DLayerDesc gVR2DLayer{ { 0.0f, 0.0f, -1.0f }, { 0.0f, 0.0f, 0.0f, 1.0f }, 1.0f };

const char* pSkyboxImageFileNames[] = {
    "Skybox_right1.tex", "Skybox_left2.tex", "Skybox_top3.tex", "Skybox_bottom4.tex", "Skybox_front5.tex", "Skybox_back6.tex",
};

const char* pCubeTextureName[] = { "TheForge.tex" };

const char* pTextFileName[] = { "TestDoc.txt" };

const TFResourceDirectory RD_ARCHIVE_TEST = TF_RD_MIDDLEWARE_1;

const char* pModelFileName[] = { "matBall.bin" };

TFFontDrawDesc gFrameTimeDraw;
TFFont*        gFont;

TFUIWindowDesc gGuiTextDataDesc = {};
TFUIWindowDesc gGuiOcclusionDataDesc = {};

// Zip file for testing
const char* pZipReadFile = "ZipFiles/28-ArchiveFileSystem.buny";

bstring gText = bempty();

// structures for loaded model
TFGeometry*    pMesh;
TFVertexLayout gVertexLayoutDefault = {};

TFIFileSystem gArchiveFileSystem = { 0 };

// Numbers stored in a file they way each 8 byte value is an index (0,1,2,3,4,5,...).
// This way we can easily test seek feature for various compression formats.
const int   gNumbersFileCount = 3;
const char* gNumbersFileNames[] = {
    "numbers.raw",
    "numbers.lz4",
    "numbers.zstd",
};

#if 0 // numbers files generator source code
int main()
{
	FILE* f = fopen("numbers", "wb");
	for (uint64_t i = 0; i < 512 * 1024; ++i)
		fwrite(&i, 8, 1, f);
	fclose(f);
}
#endif

typedef bool (*findStreamCb)(TFFileStream* pStream, const void* pFind, size_t findSize, ssize_t maxSeek, ssize_t* pPosition);

static bool testFindStream(const char* name, findStreamCb findCb)
{
    char         txt[] = "THIS IS A TEST TEXT";
    const char   pattern[] = "TEST";
    TFFileStream stream;
    fsOpenStreamFromMemory(txt, sizeof(txt), TF_FM_READ, false, &stream);
    if (findCb == fsFindReverseStream)
        fsSeekStream(&stream, TF_SBO_END_OF_FILE, 0);
    ssize_t pos = INT_MAX;
    findCb(&stream, pattern, sizeof(pattern) - 1, INT_MAX, &pos);
    fsCloseStream(&stream);
    if (pos != 10)
    {
        LOGF(eERROR, "Test condition 'pos == 10' failed for %s.", name);
        return false;
    }

    return true;
}

typedef enum ReadError
{
    READ_NO_ERROR,
    READ_OPEN_ERROR = 1u << 0u,
    READ_CONTENT_ERROR = 1u << 1u,
    READ_ANY_ERROR = READ_OPEN_ERROR | READ_CONTENT_ERROR,
} ReadError;

static const size_t gReadTestMaxDisplayLength = 128;

const char* pTestReadFiles[] = {
    "TestDoc.txt",
    "TestDoc.txt", // different content check
    "TestDoc.txt", // different size check
    "NonExisting.txt",
};

uint64_t gTestReadFilesIds[] = {
    UINT64_MAX,
    UINT64_MAX,
    UINT64_MAX,
    UINT64_MAX,
};

const ReadError pReadErrors[] = {
    READ_NO_ERROR,
    READ_CONTENT_ERROR,
    READ_CONTENT_ERROR,
    READ_OPEN_ERROR,
};

const char* pTestReadContent[] = {
    "Hello World! This is an example for the The Forge's Zip FileSystem.",
    "Hello World! This is an example for the The Forge's Zip FileSystem!",
    "Hello World! This is an example for the The Forge's Zip.",
    "",
};
const size_t pTestReadContentSize[] = {
    strlen(pTestReadContent[0]),
    strlen(pTestReadContent[1]),
    strlen(pTestReadContent[2]),
    strlen(pTestReadContent[3]),
};

static bool testReadFile(const char* testName, ReadError expectedError, TFResourceDirectory rd, const char* pFileName,
                         const char* expectedContent, size_t expectedContentSize)
{
    ASSERT(testName && pFileName);
    TFFileStream fStream = {};
    TFFileMode   mode = TF_FM_READ;
    bool         noerr = true;

    if (!fsOpenStreamFromPath(rd, pFileName, mode, &fStream))
    {
        if (!(expectedError & READ_OPEN_ERROR))
        {
            LOGF(eERROR, "\"%s\": Couldn't open file '%s' for read. Function %s failed with error: %s", testName, pFileName,
                 FS_ERR_CTX.func, getFSErrCodeString(FS_ERR_CTX.code));
            return false;
        }
        return true;
    }
    else if (expectedError & READ_OPEN_ERROR)
    {
        LOGF(eERROR, "\"%s\": Expected open error for '%s' file.", testName, pFileName);
        fsCloseStream(&fStream);
        return false;
    }

    if (!expectedContent)
    {
        noerr = fsCloseStream(&fStream);
        if (!noerr)
            LOGF(eERROR, "\"%s\": Failed to close read stream of '%s' file.", testName, pFileName);
        return noerr;
    }

    size_t fileSize = fsGetStreamFileSize(&fStream);

    char*  content = (char*)tf_malloc(fileSize + 1);
    size_t readBytes = fsReadFromStream(&fStream, content, fileSize);
    if (readBytes != fileSize)
    {
        LOGF(eERROR, "\"%s\": Couldn't read %ul bytes from file '%s'. Read %ul bytes instead.", testName, (unsigned long)fileSize,
             pFileName, (unsigned long)readBytes);
        noerr = false;
    }
    content[readBytes] = '\0';

    int diff = 0;
    if (readBytes < expectedContentSize)
        diff = -(int)(expectedContentSize - readBytes);
    else if (readBytes > expectedContentSize)
        diff = (int)(expectedContentSize - readBytes);

    if (diff == 0 && readBytes != 0)
    {
        diff = memcmp(content, expectedContent, readBytes);
    }

    if (diff != 0)
    {
        if (!(expectedError & READ_CONTENT_ERROR))
        {
            LOGF(eERROR, "\"%s\": Content of file '%s' differs from expected. Diff result: %d.", testName, pFileName, diff);
            if (readBytes < gReadTestMaxDisplayLength && expectedContentSize < gReadTestMaxDisplayLength)
            {
                LOGF(eINFO, "\"%s\": File content:\n%s", testName, content);
                LOGF(eINFO, "\"%s\": Expected content:\n%s", testName, expectedContent);
            }
            noerr = false;
        }
    }
    else if (expectedError & READ_CONTENT_ERROR)
    {
        LOGF(eERROR, "\"%s\": Expected content difference for '%s' file.", testName, pFileName);
        noerr = false;
    }

    tf_free(content);
    if (!fsCloseStream(&fStream))
    {
        LOGF(eERROR, "\"%s\": Failed to close read stream for file '%s'.", testName, pFileName);

        noerr = false;
    }
    return noerr;
}

static bool testReadFileByIndex(const char* testName, ReadError expectedError, TFIFileSystem* pIO, uint64_t fileIndex,
                                const char* expectedContent, size_t expectedContentSize)
{
    ASSERT(testName);
    TFFileStream fStream = {};
    bool         noerr = true;

    if (!pIO->OpenByUid(pIO, fileIndex, TF_FM_READ, &fStream))
    {
        if (!(expectedError & READ_OPEN_ERROR))
        {
            LOGF(eERROR, "\"%s\": Couldn't open file '%llu' for read.", testName, (unsigned long long)fileIndex);
            return false;
        }
        return true;
    }
    else if (expectedError & READ_OPEN_ERROR)
    {
        LOGF(eERROR, "\"%s\": Expected open error for '%llu' file.", testName, (unsigned long long)fileIndex);
        fsCloseStream(&fStream);
        return false;
    }

    if (!expectedContent)
    {
        noerr = fsCloseStream(&fStream);
        if (!noerr)
            LOGF(eERROR, "\"%s\": Failed to close read stream of '%llu' file.", testName, (unsigned long long)fileIndex);
        return noerr;
    }

    size_t fileSize = fsGetStreamFileSize(&fStream);

    char*  content = (char*)tf_malloc(fileSize + 1);
    size_t readBytes = fsReadFromStream(&fStream, content, fileSize);
    if (readBytes != fileSize)
    {
        LOGF(eERROR, "\"%s\": Couldn't read %ul bytes from file '%llu'. Read %ul bytes instead.", testName, (unsigned long)fileSize,
             (unsigned long long)fileIndex, (unsigned long)readBytes);
        noerr = false;
    }
    content[readBytes] = '\0';

    int diff = 0;
    if (readBytes < expectedContentSize)
        diff = -(int)(expectedContentSize - readBytes);
    else if (readBytes > expectedContentSize)
        diff = (int)(expectedContentSize - readBytes);

    if (diff == 0 && readBytes != 0)
    {
        diff = memcmp(content, expectedContent, readBytes);
    }

    if (diff != 0)
    {
        if (!(expectedError & READ_CONTENT_ERROR))
        {
            LOGF(eERROR, "\"%s\": Content of file '%llu' differs from expected. Diff result: %d.", testName, (unsigned long long)fileIndex,
                 diff);
            if (readBytes < gReadTestMaxDisplayLength && expectedContentSize < gReadTestMaxDisplayLength)
            {
                LOGF(eINFO, "\"%s\": File content:\n%s", testName, content);
                LOGF(eINFO, "\"%s\": Expected content:\n%s", testName, expectedContent);
            }
            noerr = false;
        }
    }
    else if (expectedError & READ_CONTENT_ERROR)
    {
        LOGF(eERROR, "\"%s\": Expected content difference for '%llu' file.", testName, (unsigned long long)fileIndex);
        noerr = false;
    }

    tf_free(content);
    if (!fsCloseStream(&fStream))
    {
        LOGF(eERROR, "\"%s\": Failed to close read stream for file '%llu'.", testName, (unsigned long long)fileIndex);

        noerr = false;
    }
    return noerr;
}

static bool runArchiveReadTests()
{
    ASSERT(SIZEOF_ARR(pTestReadFiles) == SIZEOF_ARR(pReadErrors));
    ASSERT(SIZEOF_ARR(pTestReadFiles) == SIZEOF_ARR(pTestReadContent));
    ASSERT(SIZEOF_ARR(pTestReadFiles) == SIZEOF_ARR(pTestReadContentSize));

    for (size_t i = 0; i < SIZEOF_ARR(pTestReadFiles); ++i)
    {
        static char testNameFormat[] = "Read test #%lu";
        char        testName[256] = { 0 };
        snprintf(testName, 256, testNameFormat, (unsigned long)i);

        ReadError   expectedError = pReadErrors[i];
        const char* pFileName = pTestReadFiles[i];
        const char* pContent = pTestReadContent[i];
        size_t      contentSize = pTestReadContentSize[i];

        if (!testReadFile(testName, expectedError, RD_ARCHIVE_TEST, pFileName, pContent, contentSize))
            return false;
    }

    // Fill in array
    for (size_t i = 0; i < SIZEOF_ARR(pTestReadFiles); ++i)
    {
        ReadError   expectedError = pReadErrors[i];
        const char* pFileName = pTestReadFiles[i];

        bool noerr = gArchiveFileSystem.GetFileUid(&gArchiveFileSystem, RD_ARCHIVE_TEST, pFileName, &gTestReadFilesIds[i]);

        if (!noerr && expectedError != READ_OPEN_ERROR)
        {
            LOGF(eERROR, "Failed to find entry '%s'", pFileName);
            return false;
        }
        else if (noerr && expectedError == READ_OPEN_ERROR)
        {
            LOGF(eERROR, "Found entry '%s', but it shouldn't exist.", pFileName);
            return false;
        }
    }

    // Test
    for (size_t i = 0; i < SIZEOF_ARR(pTestReadFiles); ++i)
    {
        static char testNameFormat[] = "Read test by index #%lu";
        char        testName[256] = { 0 };
        snprintf(testName, 256, testNameFormat, (unsigned long)i);

        ReadError   expectedError = pReadErrors[i];
        uint64_t    fileIndex = gTestReadFilesIds[i];
        const char* pContent = pTestReadContent[i];
        size_t      contentSize = pTestReadContentSize[i];

        if (!testReadFileByIndex(testName, expectedError, &gArchiveFileSystem, fileIndex, pContent, contentSize))
            return false;
    }

    return true;
}

// TODO should we put open/close function calls out of a benchmark scope?
// This benchmark compares read speed of various compression formats
static bool runArchiveBenchmark()
{
    bool success = true;
    for (int i = 0; i < gNumbersFileCount; ++i)
    {
        int64_t startTime = getUSec(true);

        TFFileStream fs;
        if (!fsOpenStreamFromPath(RD_ARCHIVE_TEST, gNumbersFileNames[i], TF_FM_READ, &fs))
            return false;

        ssize_t expected_size = fsGetStreamFileSize(&fs);

        size_t size = (size_t)expected_size;

        while (size)
        {
            uint8_t buffer[4 * 1024];
            size_t  rs = fsReadFromStream(&fs, buffer, sizeof(buffer));
            size -= rs;
            if (rs == 0)
                break;
        }

        fsCloseStream(&fs);

        int64_t endTime = getUSec(true);

        if (size != 0)
        {
            LOGF(eERROR,
                 "Archive read test for file %s is failed after reading %zu out "
                 "of %zu (%s)",
                 gNumbersFileNames[i], (size_t)expected_size - size, (size_t)expected_size, humanReadableTime(endTime - startTime).str);
            success = false;
        }
        else
        {
            LOGF(eINFO, "Archive read test for file %s took %s", gNumbersFileNames[i], humanReadableTime(endTime - startTime).str);
        }
    }

    return success;
}

static bool runArchiveSeekTests()
{
    bool testSuccess = true;
    for (int i = 0; i < gNumbersFileCount; ++i)
    {
        bool success = true;

        int64_t startTime = getUSec(true);

        TFFileStream fs;
        if (!fsOpenStreamFromPath(RD_ARCHIVE_TEST, gNumbersFileNames[i], TF_FM_READ, &fs))
            return false;

        ssize_t size = fsGetStreamFileSize(&fs);

        while ((size -= 8 * 1024) > 0)
        {
            if (!fsSeekStream(&fs, TF_SBO_START_OF_FILE, size))
            {
                success = false;
                break;
            }

            uint64_t number;
            if (fsReadFromStream(&fs, &number, 8) != 8)
            {
                success = false;
                break;
            }

            if (number != (uint64_t)size / 8)
            {
                success = false;
                break;
            }
        }

        fsCloseStream(&fs);

        int64_t endTime = getUSec(true);

        LOGF(eINFO, "Archive seek test for file %s %s. %s", gNumbersFileNames[i], success ? "success" : "failure",
             humanReadableTime(endTime - startTime).str);

        if (!success)
            testSuccess = false;
    }

    return testSuccess;
}

static bool runTests()
{
    if (!testFindStream("forward", fsFindStream))
        return false;
    LOGF(eINFO, "Forward find succeeded.");
    if (!testFindStream("backward", fsFindReverseStream))
        return false;
    LOGF(eINFO, "Backward find succeeded.");

    if (!runArchiveReadTests())
        return false;
    if (!runArchiveBenchmark())
        return false;
    if (!runArchiveSeekTests())
        return false;

    LOGF(eINFO, "Archive tests succeeded.");

    return true;
}

class FileSystemUnitTest: public IApp
{
public:
    bool Init()
    {
        // Generate sphere vertex buffer
        if (!pSpherePoints)
        {
            gNumberOfSpherePoints = 0;
            generateSpherePoints(NULL, &gNumberOfSpherePoints, gSphereResolution, gSphereDiameter);
            pSpherePoints = (float*)tf_malloc(sizeof(float) * gNumberOfSpherePoints);
            generateSpherePoints(pSpherePoints, &gNumberOfSpherePoints, gSphereResolution, gSphereDiameter);
        }

        struct TFArchiveOpenDesc openDesc = { 0 };
        openDesc.mmap = true;

        if (!fsArchiveOpen(TF_RD_OTHER_FILES, pZipReadFile, &openDesc, &gArchiveFileSystem))
        {
            LOGF(eERROR, "Failed to open zip file for read. Function %s failed with error: %s", FS_ERR_CTX.func,
                 getFSErrCodeString(FS_ERR_CTX.code));
            return false;
        }

        // Load files processed by asset pipeline and zipped
        fsSetPathForResourceDir(&gArchiveFileSystem, TF_RD_MESHES, "Meshes/");
        fsSetPathForResourceDir(&gArchiveFileSystem, TF_RD_TEXTURES, "Textures/");
        fsSetPathForResourceDir(&gArchiveFileSystem, TF_RD_FONTS, "Fonts/");
        fsSetPathForResourceDir(&gArchiveFileSystem, RD_ARCHIVE_TEST, "");

        gVertexLayoutDefault.mBindingCount = 1;
        gVertexLayoutDefault.mAttribCount = 3;
        gVertexLayoutDefault.mAttribs[0].mSemantic = TF_SEMANTIC_POSITION;
        gVertexLayoutDefault.mAttribs[0].mFormat = TinyImageFormat_R32G32B32_SFLOAT;
        gVertexLayoutDefault.mAttribs[0].mBinding = 0;
        gVertexLayoutDefault.mAttribs[0].mLocation = 0;
        gVertexLayoutDefault.mAttribs[0].mOffset = 0;
        gVertexLayoutDefault.mAttribs[1].mSemantic = TF_SEMANTIC_NORMAL;
        gVertexLayoutDefault.mAttribs[1].mFormat = TinyImageFormat_R32_UINT;
        gVertexLayoutDefault.mAttribs[1].mBinding = 0;
        gVertexLayoutDefault.mAttribs[1].mLocation = 1;
        gVertexLayoutDefault.mAttribs[1].mOffset = sizeof(float) * 3;
        gVertexLayoutDefault.mAttribs[2].mSemantic = TF_SEMANTIC_TEXCOORD0;
        gVertexLayoutDefault.mAttribs[2].mFormat = TinyImageFormat_R32_UINT;
        gVertexLayoutDefault.mAttribs[2].mBinding = 0;
        gVertexLayoutDefault.mAttribs[2].mLocation = 2;
        gVertexLayoutDefault.mAttribs[2].mOffset = sizeof(float) * 3 + sizeof(uint32_t);

        if (!runTests())
            LOGF(eERROR, "Couldn't run tests successfully.");

        TFFileStream textFileHandle = {};
        if (!fsOpenStreamFromPath(RD_ARCHIVE_TEST, pTextFileName[0], TF_FM_READ, &textFileHandle))
        {
            LOGF(LogLevel::eERROR, "\"%s\": ERROR in searching for file.", pTextFileName[0]);
            return false;
        }

        ssize_t textFileSize = fsGetStreamFileSize(&textFileHandle);
        if (textFileSize < 0)
        {
            LOGF(LogLevel::eERROR, "\"%s\": Error in reading file.", pTextFileName[0]);
            return false;
        }
        size_t bytesRead = fsReadBstringFromStream(&textFileHandle, &gText, textFileSize);
        fsCloseStream(&textFileHandle);

        if (bytesRead != (size_t)textFileSize)
        {
            LOGF(LogLevel::eERROR, "\"%s\": Error in reading file.", pTextFileName[0]);
            return false;
        }

        // Actual diffs and tests
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

        mSettings.mFrameMaxCount = gDataBufferCount;

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

        if (pRenderer->pGpu->mOcclusionQueries)
        {
            TFQueryPoolDesc queryPoolDesc = {};
            queryPoolDesc.mType = TF_QUERY_TYPE_OCCLUSION;
            queryPoolDesc.mQueryCount = gMaxOcclusionQueries;
            for (uint32_t i = 0; i < gDataBufferCount; ++i)
            {
                initQueryPool(pRenderer, &queryPoolDesc, &pOcclusionQueryPool[i]);
            }
        }

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

        // Initialize micro profiler and its UI.
        TFProfilerDesc profiler = {};
        profiler.pRenderer = pRenderer;
        initProfiler(&profiler);

        // Gpu profiler can only be added after initProfile.
        gGpuProfileToken = initGpuProfiler(pRenderer, pGraphicsQueue, "Graphics");

        // Load Zip file texture
        TFTextureLoadDesc textureDescZip = {};
        textureDescZip.pFileName = pCubeTextureName[0];
        textureDescZip.ppTexture = &pZipTexture[0];
        // Textures representing color should be stored in SRGB or HDR format
        textureDescZip.mCreationFlag = TF_TEXTURE_CREATION_FLAG_SRGB;
        addResource(&textureDescZip, NULL);

        // Loads Skybox Textures
        for (int i = 0; i < 6; ++i)
        {
            TFTextureLoadDesc textureDesc = {};
            textureDesc.pFileName = pSkyboxImageFileNames[i];
            textureDesc.ppTexture = &pSkyboxTextures[i];
            // Textures representing color should be stored in SRGB or HDR format
            textureDesc.mCreationFlag = TF_TEXTURE_CREATION_FLAG_SRGB;
            addResource(&textureDesc, NULL);
        }

        TFGeometryLoadDesc loadDesc = {};
        loadDesc.pFileName = pModelFileName[0];
        loadDesc.ppGeometry = &pMesh;
        loadDesc.pVertexLayout = &gVertexLayoutDefault;
        addResource(&loadDesc, NULL);

        TFSamplerDesc samplerDesc = { TF_FILTER_LINEAR,
                                      TF_FILTER_LINEAR,
                                      TF_MIPMAP_MODE_NEAREST,
                                      TF_ADDRESS_MODE_CLAMP_TO_EDGE,
                                      TF_ADDRESS_MODE_CLAMP_TO_EDGE,
                                      TF_ADDRESS_MODE_CLAMP_TO_EDGE };
        addSampler(pRenderer, &samplerDesc, &pSamplerSkybox);

        // Generate Cuboid Vertex Buffer

        float widthCube = 1.0f;
        float heightCube = 1.0f;
        float depthCube = 1.0f;

        float CubePoints[] = {
            // Position				        //Normals				//TexCoords
            -widthCube, -heightCube, -depthCube, 0.0f,        0.0f,       -1.0f,       0.0f,       0.0f,        widthCube,  -heightCube,
            -depthCube, 0.0f,        0.0f,       -1.0f,       1.0f,       0.0f,        widthCube,  heightCube,  -depthCube, 0.0f,
            0.0f,       -1.0f,       1.0f,       1.0f,        widthCube,  heightCube,  -depthCube, 0.0f,        0.0f,       -1.0f,
            1.0f,       1.0f,        -widthCube, heightCube,  -depthCube, 0.0f,        0.0f,       -1.0f,       0.0f,       1.0f,
            -widthCube, -heightCube, -depthCube, 0.0f,        0.0f,       -1.0f,       0.0f,       0.0f,

            -widthCube, -heightCube, depthCube,  0.0f,        0.0f,       1.0f,        0.0f,       0.0f,        widthCube,  -heightCube,
            depthCube,  0.0f,        0.0f,       1.0f,        1.0f,       0.0f,        widthCube,  heightCube,  depthCube,  0.0f,
            0.0f,       1.0f,        1.0f,       1.0f,        widthCube,  heightCube,  depthCube,  0.0f,        0.0f,       1.0f,
            1.0f,       1.0f,        -widthCube, heightCube,  depthCube,  0.0f,        0.0f,       1.0f,        0.0f,       1.0f,
            -widthCube, -heightCube, depthCube,  0.0f,        0.0f,       1.0f,        0.0f,       0.0f,

            -widthCube, heightCube,  depthCube,  -1.0f,       0.0f,       0.0f,        1.0f,       0.0f,        -widthCube, heightCube,
            -depthCube, -1.0f,       0.0f,       0.0f,        1.0f,       1.0f,        -widthCube, -heightCube, -depthCube, -1.0f,
            0.0f,       0.0f,        0.0f,       1.0f,        -widthCube, -heightCube, -depthCube, -1.0f,       0.0f,       0.0f,
            0.0f,       1.0f,        -widthCube, -heightCube, depthCube,  -1.0f,       0.0f,       0.0f,        0.0f,       0.0f,
            -widthCube, heightCube,  depthCube,  -1.0f,       0.0f,       0.0f,        1.0f,       0.0f,

            widthCube,  heightCube,  depthCube,  1.0f,        0.0f,       0.0f,        1.0f,       0.0f,        widthCube,  heightCube,
            -depthCube, 1.0f,        0.0f,       0.0f,        1.0f,       1.0f,        widthCube,  -heightCube, -depthCube, 1.0f,
            0.0f,       0.0f,        0.0f,       1.0f,        widthCube,  -heightCube, -depthCube, 1.0f,        0.0f,       0.0f,
            0.0f,       1.0f,        widthCube,  -heightCube, depthCube,  1.0f,        0.0f,       0.0f,        0.0f,       0.0f,
            widthCube,  heightCube,  depthCube,  1.0f,        0.0f,       0.0f,        1.0f,       0.0f,

            -widthCube, -heightCube, -depthCube, 0.0f,        -1.0f,      0.0f,        0.0f,       1.0f,        widthCube,  -heightCube,
            -depthCube, 0.0f,        -1.0f,      0.0f,        1.0f,       1.0f,        widthCube,  -heightCube, depthCube,  0.0f,
            -1.0f,      0.0f,        1.0f,       0.0f,        widthCube,  -heightCube, depthCube,  0.0f,        -1.0f,      0.0f,
            1.0f,       0.0f,        -widthCube, -heightCube, depthCube,  0.0f,        -1.0f,      0.0f,        0.0f,       0.0f,
            -widthCube, -heightCube, -depthCube, 0.0f,        -1.0f,      0.0f,        0.0f,       1.0f,

            -widthCube, heightCube,  -depthCube, 0.0f,        1.0f,       0.0f,        0.0f,       1.0f,        widthCube,  heightCube,
            -depthCube, 0.0f,        1.0f,       0.0f,        1.0f,       1.0f,        widthCube,  heightCube,  depthCube,  0.0f,
            1.0f,       0.0f,        1.0f,       0.0f,        widthCube,  heightCube,  depthCube,  0.0f,        1.0f,       0.0f,
            1.0f,       0.0f,        -widthCube, heightCube,  depthCube,  0.0f,        1.0f,       0.0f,        0.0f,       0.0f,
            -widthCube, heightCube,  -depthCube, 0.0f,        1.0f,       0.0f,        0.0f,       1.0f
        };

        uint64_t         cubiodDataSize = 288 * sizeof(float);
        TFBufferLoadDesc cubiodVbDesc = {};
        cubiodVbDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_VERTEX_BUFFER;
        cubiodVbDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_GPU_ONLY;
        cubiodVbDesc.mDesc.mSize = cubiodDataSize;
        cubiodVbDesc.pData = CubePoints;
        cubiodVbDesc.ppBuffer = &pZipTextureVertexBuffer;
        addResource(&cubiodVbDesc, NULL);

        // Generate sky box vertex buffer
        float skyBoxPoints[] = {
            10.0f,  -10.0f, -10.0f, 6.0f, // -z
            -10.0f, -10.0f, -10.0f, 6.0f,   -10.0f, 10.0f,  -10.0f, 6.0f,   -10.0f, 10.0f,
            -10.0f, 6.0f,   10.0f,  10.0f,  -10.0f, 6.0f,   10.0f,  -10.0f, -10.0f, 6.0f,

            -10.0f, -10.0f, 10.0f,  2.0f, //-x
            -10.0f, -10.0f, -10.0f, 2.0f,   -10.0f, 10.0f,  -10.0f, 2.0f,   -10.0f, 10.0f,
            -10.0f, 2.0f,   -10.0f, 10.0f,  10.0f,  2.0f,   -10.0f, -10.0f, 10.0f,  2.0f,

            10.0f,  -10.0f, -10.0f, 1.0f, //+x
            10.0f,  -10.0f, 10.0f,  1.0f,   10.0f,  10.0f,  10.0f,  1.0f,   10.0f,  10.0f,
            10.0f,  1.0f,   10.0f,  10.0f,  -10.0f, 1.0f,   10.0f,  -10.0f, -10.0f, 1.0f,

            -10.0f, -10.0f, 10.0f,  5.0f, // +z
            -10.0f, 10.0f,  10.0f,  5.0f,   10.0f,  10.0f,  10.0f,  5.0f,   10.0f,  10.0f,
            10.0f,  5.0f,   10.0f,  -10.0f, 10.0f,  5.0f,   -10.0f, -10.0f, 10.0f,  5.0f,

            -10.0f, 10.0f,  -10.0f, 3.0f, //+y
            10.0f,  10.0f,  -10.0f, 3.0f,   10.0f,  10.0f,  10.0f,  3.0f,   10.0f,  10.0f,
            10.0f,  3.0f,   -10.0f, 10.0f,  10.0f,  3.0f,   -10.0f, 10.0f,  -10.0f, 3.0f,

            10.0f,  -10.0f, 10.0f,  4.0f, //-y
            10.0f,  -10.0f, -10.0f, 4.0f,   -10.0f, -10.0f, -10.0f, 4.0f,   -10.0f, -10.0f,
            -10.0f, 4.0f,   -10.0f, -10.0f, 10.0f,  4.0f,   10.0f,  -10.0f, 10.0f,  4.0f,
        };

        uint64_t         skyBoxDataSize = 4 * 6 * 6 * sizeof(float);
        TFBufferLoadDesc skyboxVbDesc = {};
        skyboxVbDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_VERTEX_BUFFER;
        skyboxVbDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_GPU_ONLY;
        skyboxVbDesc.mDesc.mSize = skyBoxDataSize;
        skyboxVbDesc.pData = skyBoxPoints;
        skyboxVbDesc.ppBuffer = &pSkyboxVertexBuffer;
        addResource(&skyboxVbDesc, NULL);

        TFBufferLoadDesc ubDesc = {};
        ubDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        ubDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
        ubDesc.mDesc.mSize = sizeof(UniformBlock);
        ubDesc.mDesc.mFlags = TF_BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
        ubDesc.pData = NULL;

        uint64_t         sphereDataSize = gNumberOfSpherePoints * sizeof(float);
        TFBufferLoadDesc sphereVbDesc = {};
        sphereVbDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_VERTEX_BUFFER;
        sphereVbDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_GPU_ONLY;
        sphereVbDesc.mDesc.mSize = sphereDataSize;
        sphereVbDesc.pData = pSpherePoints;
        sphereVbDesc.ppBuffer = &pSphereVertexBuffer;
        addResource(&sphereVbDesc, NULL);

        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            ubDesc.ppBuffer = &pProjViewUniformBuffer[i];
            addResource(&ubDesc, NULL);
        }

        // TFCameraMotionParameters cmp{ 160.0f, 600.0f, 200.0f };
        vec3 camPos{ 48.0f, 48.0f, 20.0f };
        vec3 lookAt{ 0 };

        pCamera = initFpsCamera(camPos, lookAt);

        AddCustomInputBindings();

        waitForAllResourceLoads();
        initScreenshotCapturer(pRenderer, pGraphicsQueue, GetName());
        return true;
    }

    void Exit()
    {
        if (pSpherePoints)
        {
            tf_free(pSpherePoints);
            pSpherePoints = nullptr;
        }
        exitScreenshotCapturer();
        fsArchiveClose(&gArchiveFileSystem);

        bdestroy(&gText);

        exitCamera(pCamera);

        exitProfiler();

        exitUserInterface();

        removeFont(gFont);
        exitFontSystem();

        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            removeResource(pProjViewUniformBuffer[i]);
        }

        removeResource(pSphereVertexBuffer);
        removeResource(pSkyboxVertexBuffer);
        removeResource(pZipTextureVertexBuffer);

        for (uint i = 0; i < 6; ++i)
            removeResource(pSkyboxTextures[i]);

        // Remove loaded zip test texture
        removeResource(pZipTexture[0]);

        // Remove loaded zip test models
        removeResource(pMesh);
        pMesh = NULL;

        removeSampler(pRenderer, pSamplerSkybox);

        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            exitSemaphore(pRenderer, pImageAcquiredSemaphore[i]);
        }
        exitGpuCmdRing(pRenderer, &gGraphicsCmdRing);
        exitRootSignature(pRenderer);
        exitResourceLoaderInterface(pRenderer);
        if (pRenderer->pGpu->mOcclusionQueries)
        {
            for (uint32_t i = 0; i < gDataBufferCount; ++i)
            {
                exitQueryPool(pRenderer, pOcclusionQueryPool[i]);
            }
        }
        exitQueue(pRenderer, pGraphicsQueue);
        exitRenderer(pRenderer);
        exitGPUConfig();
        pRenderer = NULL;
    }

    bool Load(TFReloadDesc* pReloadDesc)
    {
        UNREF_PARAM(pReloadDesc);

        addShaders();
        addDescriptorSets();

        loadProfilerUI(mSettings.mWidth, mSettings.mHeight);

        gGuiTextDataDesc = {};
        gGuiTextDataDesc.mStartPos = vec2(mSettings.mWidth * 0.01f, mSettings.mHeight * 0.2f);
        gGuiTextDataDesc.mStartSize = vec2(450, 600);
        gGuiTextDataDesc.pWindowTitle = "Opened Document";
        gGuiTextDataDesc.mFlags = TF_UI_WINDOW_TITLE | TF_UI_WINDOW_SCALABLE | TF_UI_WINDOW_MOVABLE | TF_UI_WINDOW_MINIMIZABLE |
                                  TF_UI_WINDOW_BORDER | TF_UI_WINDOW_INIT_HEIGHT_FIT;

        if (pRenderer->pGpu->mOcclusionQueries)
        {
            gGuiOcclusionDataDesc = {};
            gGuiOcclusionDataDesc.mStartPos = vec2(mSettings.mWidth * 0.01f, mSettings.mHeight * .3f);
            gGuiOcclusionDataDesc.mStartSize = vec2(450, 600);
            gGuiOcclusionDataDesc.pWindowTitle = "Occlusion Test";
            gGuiOcclusionDataDesc.mFlags = TF_UI_WINDOW_TITLE | TF_UI_WINDOW_SCALABLE | TF_UI_WINDOW_MOVABLE | TF_UI_WINDOW_MINIMIZABLE |
                                           TF_UI_WINDOW_BORDER | TF_UI_WINDOW_INIT_HEIGHT_FIT;
        }

        if (!addSwapChain())
            return false;

        if (!addDepthBuffer())
            return false;

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
        fontLoad.mHeight = mSettings.mHeight;
        fontLoad.mWidth = mSettings.mWidth;
        loadFontSystem(&fontLoad);

        return true;
    }

    void Unload(TFReloadDesc* pReloadDesc)
    {
        UNREF_PARAM(pReloadDesc);

        waitQueueIdle(pGraphicsQueue);

        unloadFontSystem();
        unloadUserInterface();

        removePipelines();

        removeSwapChain(pRenderer, pSwapChain);
        removeRenderTarget(pRenderer, pDepthBuffer);
        unloadProfilerUI();

        removeDescriptorSets();
        removeShaders();
    }

    void Update(float deltaTime)
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

        pCamera->update(deltaTime);

        /************************************************************************/
        // Scene Update
        /****************************************************************/
        // Update camera with time
        TFCameraMatrix viewMat = pCamera->getViewMatrix();

        const float    aspectInverse = (float)mSettings.mHeight / (float)mSettings.mWidth;
        const float    horizontalFov = PI / 2.0f;
        TFCameraMatrix projMat = camMatPerspectiveReverseZ(horizontalFov, aspectInverse, 0.1f, 1000.0f);

        // Projection and View Matrix
        gUniformData.mProjectView = camMatMul(&projMat, &viewMat);

        // Model Matrix
        mat4 trans = mat4::translation(vec3(15.0f, 0.0f, 22.0f));
        mat4 scale = mat4::scale(vec3(5.0f));
        gUniformData.mModelMatCapsule = trans * scale;

        // Uniform buffer data of the cube with zip texture
        mat4 translationMatZip = mat4::translation(vec3(10.5f, 1.0f, 3.0f));
        mat4 scaleMatZip = mat4::scale(vec3(10.5f));

        gUniformData.mModelMatCube = translationMatZip * scaleMatZip;

        gUniformData.mModelOcclusion = mat4::translation(vec3(-10.5f, 2.5f, -10.0f)) * mat4::scale(vec3(10.5f));
    }

    void Draw()
    {
        if ((bool)pSwapChain->mEnableVsync != mSettings.mVSyncEnabled)
        {
            waitQueueIdle(pGraphicsQueue);
            ::toggleVSync(pRenderer, &pSwapChain);
        }

        uint32_t swapchainImageIndex;
        acquireNextImage(pRenderer, pSwapChain, pImageAcquiredSemaphore[mSettings.mFrameIdx], NULL, &swapchainImageIndex);

        TFRenderTarget*   pRenderTarget = pSwapChain->ppRenderTargets[swapchainImageIndex];
        GpuCmdRingElement elem = getNextGpuCmdRingElement(&gGraphicsCmdRing, true, 1);

        // Stall if CPU is running "gDataBufferCount" frames ahead of GPU
        TFFenceStatus fenceStatus;
        getFenceStatus(pRenderer, elem.pFence, &fenceStatus);
        if (fenceStatus == TF_FENCE_STATUS_INCOMPLETE)
            waitForFences(pRenderer, 1, &elem.pFence);

        if (pRenderer->pGpu->mOcclusionQueries)
        {
            TFQueryData occlusionData = {};
            getQueryData(pRenderer, pOcclusionQueryPool[mSettings.mFrameIdx], gOccTestOcclusionSphereMaxIndex, &occlusionData);
            const uint64_t maxOcclusion = occlusionData.mOcclusionCounts;
            getQueryData(pRenderer, pOcclusionQueryPool[mSettings.mFrameIdx], gOccTestOcclusionSphereIndex * VR_MULTIVIEW_COUNT,
                         &occlusionData);
            const uint64_t testOcclusion = occlusionData.mOcclusionCounts;
            float          visibility = ((maxOcclusion == 0.0f) ? 0.0f : (((float)testOcclusion) / ((float)maxOcclusion))) * 100.0f;
            bformat(&gOcclusionbstr, "Visible texels %u | Total texels %u | (%f%%)", testOcclusion, maxOcclusion, visibility);
        }

        resetCmdPool(pRenderer, elem.pCmdPool);

        // Update uniform buffers
        TFBufferUpdateDesc viewProjCbv = { pProjViewUniformBuffer[mSettings.mFrameIdx] };
        beginUpdateResource(&viewProjCbv);
        memcpy(viewProjCbv.pMappedData, &gUniformData, sizeof(gUniformData));
        endUpdateResource(&viewProjCbv);

        // Simply record the screen cleaning command
        TFCmd* cmd = elem.pCmds[0];
        beginCmd(cmd);
        cmdBindDescriptorSet(cmd, 0, pDescriptorSetPersistent);
        cmdBindDescriptorSet(cmd, mSettings.mFrameIdx, pDescriptorSetFramePerFrame);

        cmdBeginGpuFrameProfile(cmd, gGpuProfileToken);

        if (pRenderer->pGpu->mOcclusionQueries)
        {
            cmdResetQuery(cmd, pOcclusionQueryPool[mSettings.mFrameIdx], 0, gMaxOcclusionQueries);
        }

        // Resource Transition
        TFRenderTargetBarrier barriers[] = {
            { pRenderTarget, TF_RESOURCE_STATE_PRESENT, TF_RESOURCE_STATE_RENDER_TARGET },
        };
        cmdResourceBarrier(cmd, 0, NULL, 0, NULL, 1, barriers);

        // Bind Render Targets
        TFBindRenderTargetsDesc bindRenderTargets = {};
        bindRenderTargets.mRenderTargetCount = 1;
        bindRenderTargets.mRenderTargets[0] = { pRenderTarget, TF_LOAD_ACTION_CLEAR };
        bindRenderTargets.mDepthStencil = { pDepthBuffer, TF_LOAD_ACTION_CLEAR };
        cmdBindRenderTargets(cmd, &bindRenderTargets);
        cmdSetViewport(cmd, 0.0f, 0.0f, (float)pRenderTarget->mWidth, (float)pRenderTarget->mHeight, 0.0f, 1.0f);
        cmdSetScissor(cmd, 0, 0, pRenderTarget->mWidth, pRenderTarget->mHeight);

        TFQueryDesc occlusionQueryDesc = {};

        const uint32_t sphereStride = sizeof(float) * 6;

        // Occlusion max pass
        if (pRenderer->pGpu->mOcclusionQueries)
        {
            cmdBeginGpuTimestampQuery(cmd, gGpuProfileToken, "Draw Occlusion Max");

            cmdSetViewport(cmd, 0.0f, 0.0f, (float)pRenderTarget->mWidth, (float)pRenderTarget->mHeight, 1.0f, 1.0f);
            cmdBindPipeline(cmd, pOcclusionMax);

            cmdBindDescriptorSet(cmd, mSettings.mFrameIdx, pDescriptorSetFramePerFrame);

            occlusionQueryDesc.mIndex = gOccTestOcclusionSphereMaxIndex;
            cmdBeginQuery(cmd, pOcclusionQueryPool[mSettings.mFrameIdx], &occlusionQueryDesc);
            cmdBindVertexBuffer(cmd, 1, &pSphereVertexBuffer, &sphereStride, NULL);
            cmdDraw(cmd, gNumberOfSpherePoints / 6, 0);
            cmdEndQuery(cmd, pOcclusionQueryPool[mSettings.mFrameIdx], &occlusionQueryDesc);
            cmdEndGpuTimestampQuery(cmd, gGpuProfileToken);
        }

        // Skybox pass
        {
            cmdBeginGpuTimestampQuery(cmd, gGpuProfileToken, "Draw Skybox");

            cmdSetViewport(cmd, 0.0f, 0.0f, (float)pRenderTarget->mWidth, (float)pRenderTarget->mHeight, 1.0f, 1.0f);
            cmdBindPipeline(cmd, pPipelineSkybox);

            const uint32_t skyboxStride = sizeof(float) * 4;
            cmdBindVertexBuffer(cmd, 1, &pSkyboxVertexBuffer, &skyboxStride, NULL);
            cmdDraw(cmd, 36, 0);
            cmdSetViewport(cmd, 0.0f, 0.0f, (float)pRenderTarget->mWidth, (float)pRenderTarget->mHeight, 0.0f, 1.0f);
            cmdEndGpuTimestampQuery(cmd, gGpuProfileToken);
        }

        // Zip model pass
        {
            cmdBeginGpuTimestampQuery(cmd, gGpuProfileToken, "Draw Zip Model");

            cmdBindPipeline(cmd, pBasicPipeline);
            cmdBindVertexBuffer(cmd, 1, &pMesh->pVertexBuffers[0], &pMesh->mVertexStrides[0], NULL);
            cmdBindIndexBuffer(cmd, pMesh->pIndexBuffer, pMesh->mIndexType, 0);
            cmdDrawIndexed(cmd, pMesh->mIndexCount, 0, 0);
            cmdEndGpuTimestampQuery(cmd, gGpuProfileToken);
        }

        // Cube with Zip texture pass
        {
            cmdBeginGpuTimestampQuery(cmd, gGpuProfileToken, "Draw Zip File Texture");

            cmdBindPipeline(cmd, pZipTexturePipeline);
            const uint32_t cubeStride = sizeof(float) * 8;
            cmdBindVertexBuffer(cmd, 1, &pZipTextureVertexBuffer, &cubeStride, NULL);
            cmdDraw(cmd, 36, 0);
            cmdEndGpuTimestampQuery(cmd, gGpuProfileToken);
        }

        // Occlusion test pass
        if (pRenderer->pGpu->mOcclusionQueries)
        {
            cmdBeginGpuTimestampQuery(cmd, gGpuProfileToken, "Occlusion Test");

            cmdBindPipeline(cmd, pOcclusionTest);
            cmdBindDescriptorSet(cmd, mSettings.mFrameIdx, pDescriptorSetFramePerFrame);

            occlusionQueryDesc.mIndex = gOccTestOcclusionSphereIndex * VR_MULTIVIEW_COUNT;
            cmdBeginQuery(cmd, pOcclusionQueryPool[mSettings.mFrameIdx], &occlusionQueryDesc);
            cmdBindVertexBuffer(cmd, 1, &pSphereVertexBuffer, &sphereStride, NULL);
            cmdDraw(cmd, gNumberOfSpherePoints / 6, 0);
            cmdEndQuery(cmd, pOcclusionQueryPool[mSettings.mFrameIdx], &occlusionQueryDesc);
            cmdEndGpuTimestampQuery(cmd, gGpuProfileToken);
        }

        // UI pass
        {
            cmdBeginGpuTimestampQuery(cmd, gGpuProfileToken, "Draw UI");

            bindRenderTargets = {};
            bindRenderTargets.mRenderTargetCount = 1;
            bindRenderTargets.mRenderTargets[0] = { pRenderTarget, TF_LOAD_ACTION_LOAD };
            cmdBindRenderTargets(cmd, &bindRenderTargets);

            gFrameTimeDraw.mFontColor = 0xff00ffff;
            gFrameTimeDraw.mFontSize = 18.0f;
            gFrameTimeDraw.pFont = gFont;
            float2 txtSize = cmdDrawCpuProfile(cmd, float2(8.0f, 15.0f), &gFrameTimeDraw);
            cmdDrawGpuProfile(cmd, float2(8.f, txtSize.y + 75.f), gGpuProfileToken, &gFrameTimeDraw);

            uiCmdDrawUserInterface(cmd, pSwapChain, pRenderTarget, gGpuProfileToken);
            cmdBindRenderTargets(cmd, NULL);
            cmdEndGpuTimestampQuery(cmd, gGpuProfileToken);
        }

        barriers[0] = { pRenderTarget, TF_RESOURCE_STATE_RENDER_TARGET, TF_RESOURCE_STATE_PRESENT };
        cmdResourceBarrier(cmd, 0, NULL, 0, NULL, 1, barriers);

        cmdEndGpuFrameProfile(cmd, gGpuProfileToken);
        if (pRenderer->pGpu->mOcclusionQueries)
        {
            cmdResolveQuery(cmd, pOcclusionQueryPool[mSettings.mFrameIdx], 0, gMaxOcclusionQueries);
        }
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

        /// Exit if quick exit is enabled
#if ZIP_TESTS_QUICK_EXIT
        mSettings.mQuit = true;
#endif
    }

    const char* GetName() { return "12_ZipFileSystem"; }

private:
    void updateGui()
    {
        // Show the text loaded from the archive.
        if (UI_WINDOW_IS_VISIBLE(uiBeginWidgetWindow(&gGuiTextDataDesc)))
        {
            uiLayoutAutoTextRows(1);
            uiLabel((const char*)gText.data, TF_ALIGN_LEFT);
        }
        uiEndWidgetWindow();

        if (pRenderer->pGpu->mOcclusionQueries)
        {
            if (UI_WINDOW_IS_VISIBLE(uiBeginWidgetWindow(&gGuiOcclusionDataDesc)))
            {
                uiLayoutAutoTextRows(1);
                uiDynamicText(&gOcclusionbstr, gOcclusion1Color, TF_TEXT_MODE_WRAPPED, TF_ALIGN_WRAPPED);
            }
            uiEndWidgetWindow();
        }
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
        swapChainDesc.mEnableVsync = mSettings.mVSyncEnabled;
        swapChainDesc.mFlags = TF_SWAP_CHAIN_CREATION_FLAG_ENABLE_2D_VR_LAYER;
        swapChainDesc.mVR.m2DLayer = gVR2DLayer;

        ::addSwapChain(pRenderer, &swapChainDesc, &pSwapChain);

        return pSwapChain != NULL;
    }

    void addDescriptorSets()
    {
        TFDescriptorSetDesc setDesc = SRT_SET_DESC(SrtData, Persistent, 1, 0);
        addDescriptorSet(pRenderer, &setDesc, &pDescriptorSetPersistent);
        setDesc = SRT_SET_DESC(SrtData, PerFrame, gDataBufferCount, 0);
        addDescriptorSet(pRenderer, &setDesc, &pDescriptorSetFramePerFrame);
    }

    void removeDescriptorSets()
    {
        removeDescriptorSet(pRenderer, pDescriptorSetPersistent);
        removeDescriptorSet(pRenderer, pDescriptorSetFramePerFrame);
    }

    void updateDescriptorSets()
    {
        // Skybox
        {
            // Prepare descriptor sets
            TFDescriptorData params[8] = {};
            params[0].mIndex = SRT_RES_IDX(SrtData, Persistent, gRightTexture);
            params[0].ppTextures = &pSkyboxTextures[0];
            params[1].mIndex = SRT_RES_IDX(SrtData, Persistent, gLeftTexture);
            params[1].ppTextures = &pSkyboxTextures[1];
            params[2].mIndex = SRT_RES_IDX(SrtData, Persistent, gTopTexture);
            params[2].ppTextures = &pSkyboxTextures[2];
            params[3].mIndex = SRT_RES_IDX(SrtData, Persistent, gBotTexture);
            params[3].ppTextures = &pSkyboxTextures[3];
            params[4].mIndex = SRT_RES_IDX(SrtData, Persistent, gFrontTexture);
            params[4].ppTextures = &pSkyboxTextures[4];
            params[5].mIndex = SRT_RES_IDX(SrtData, Persistent, gBackTexture);
            params[5].ppTextures = &pSkyboxTextures[5];
            params[6].mIndex = SRT_RES_IDX(SrtData, Persistent, gZipTexture);
            params[6].ppTextures = pZipTexture;
            params[7].mIndex = SRT_RES_IDX(SrtData, Persistent, gSampler);
            params[7].ppSamplers = &pSamplerSkybox;
            updateDescriptorSet(pRenderer, 0, pDescriptorSetPersistent, 8, params);
        }

        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            TFDescriptorData params[1] = {};
            params[0].mIndex = SRT_RES_IDX(SrtData, PerFrame, gUniformBlock);
            params[0].ppBuffers = &pProjViewUniformBuffer[i];
            updateDescriptorSet(pRenderer, i, pDescriptorSetFramePerFrame, 1, params);
        }
    }

    void addShaders()
    {
        TFShaderLoadDesc skyShader = {};
        skyShader.mVert.pFileName = "skybox.vert";
        skyShader.mFrag.pFileName = "skybox.frag";
        TFShaderLoadDesc basicShader = {};
        basicShader.mVert.pFileName = "basic.vert";
        basicShader.mFrag.pFileName = "basic.frag";
        TFShaderLoadDesc occlusionShader = {};
        occlusionShader.mVert.pFileName = "occlusion.vert";
        occlusionShader.mFrag.pFileName = "occlusion.frag";
        TFShaderLoadDesc zipTextureShader = {};
        zipTextureShader.mVert.pFileName = "zipTexture.vert";
        zipTextureShader.mFrag.pFileName = "zipTexture.frag";

        addShader(pRenderer, &skyShader, &pSkyboxShader);
        addShader(pRenderer, &basicShader, &pBasicShader);
        addShader(pRenderer, &zipTextureShader, &pZipTextureShader);
        addShader(pRenderer, &occlusionShader, &pOcclusionShader);
    }

    void removeShaders()
    {
        removeShader(pRenderer, pOcclusionShader);
        removeShader(pRenderer, pBasicShader);
        removeShader(pRenderer, pSkyboxShader);
        removeShader(pRenderer, pZipTextureShader);
    }

    void addPipelines()
    {
        TFDepthStateDesc noDepthStateDesc = {};
        TFBlendStateDesc blendStateNoWriteDesc{};

        TFRasterizerStateDesc rasterizerStateFrontDesc = {};
        rasterizerStateFrontDesc.mFillMode = TF_FILL_MODE_SOLID;
        rasterizerStateFrontDesc.mCullMode = TF_CULL_MODE_FRONT;

        // Layout and pipeline for zip model draw
        TFRasterizerStateDesc rasterizerStateDesc = {};
        rasterizerStateDesc.mCullMode = TF_CULL_MODE_NONE;

        TFRasterizerStateDesc cubeRasterizerStateDesc = {};
        cubeRasterizerStateDesc.mCullMode = TF_CULL_MODE_NONE;

        TFRasterizerStateDesc binCubeRasterizerStateDesc = {};
        binCubeRasterizerStateDesc.mCullMode = TF_CULL_MODE_FRONT;

        TFDepthStateDesc depthStateDesc = {};
        depthStateDesc.mDepthTest = true;
        depthStateDesc.mDepthWrite = true;
        depthStateDesc.mDepthFunc = TF_CMP_GEQUAL;

        TFPipelineDesc desc = {};
        desc.mType = TF_PIPELINE_TYPE_GRAPHICS;
        PIPELINE_LAYOUT_DESC(desc, SRT_LAYOUT_DESC(SrtData, Persistent), SRT_LAYOUT_DESC(SrtData, PerFrame), NULL, NULL);
        TFGraphicsPipelineDesc& pipelineSettings = desc.mGraphicsDesc;
        pipelineSettings.mPrimitiveTopo = TF_PRIMITIVE_TOPO_TRI_LIST;
        pipelineSettings.mRenderTargetCount = 1;
        pipelineSettings.pDepthState = &depthStateDesc;
        pipelineSettings.pColorFormats = &pSwapChain->ppRenderTargets[0]->mFormat;
        pipelineSettings.mSampleCount = pSwapChain->ppRenderTargets[0]->mSampleCount;
        pipelineSettings.mSampleQuality = pSwapChain->ppRenderTargets[0]->mSampleQuality;
        pipelineSettings.mDepthStencilFormat = pDepthBuffer->mFormat;
        pipelineSettings.pShaderProgram = pBasicShader;
        pipelineSettings.pVertexLayout = &gVertexLayoutDefault;
        pipelineSettings.pRasterizerState = &binCubeRasterizerStateDesc;
        addPipeline(pRenderer, &desc, &pBasicPipeline);

        // Layout and pipeline for skybox draw
        TFVertexLayout vertexLayout = {};
        vertexLayout.mBindingCount = 1;
        vertexLayout.mAttribCount = 1;
        vertexLayout.mAttribs[0].mSemantic = TF_SEMANTIC_POSITION;
        vertexLayout.mAttribs[0].mFormat = TinyImageFormat_R32G32B32A32_SFLOAT;
        vertexLayout.mAttribs[0].mBinding = 0;
        vertexLayout.mAttribs[0].mLocation = 0;
        vertexLayout.mAttribs[0].mOffset = 0;

        pipelineSettings.pVertexLayout = &vertexLayout;
        pipelineSettings.pDepthState = NULL;
        pipelineSettings.pRasterizerState = &rasterizerStateDesc;
        pipelineSettings.pShaderProgram = pSkyboxShader;
        addPipeline(pRenderer, &desc, &pPipelineSkybox);

        // Layout and pipeline for the zip test texture

        vertexLayout = {};
        vertexLayout.mBindingCount = 1;
        vertexLayout.mAttribCount = 3;
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
        vertexLayout.mAttribs[2].mSemantic = TF_SEMANTIC_TEXCOORD0;
        vertexLayout.mAttribs[2].mFormat = TinyImageFormat_R32G32_SFLOAT;
        vertexLayout.mAttribs[2].mBinding = 0;
        vertexLayout.mAttribs[2].mLocation = 2;
        vertexLayout.mAttribs[2].mOffset = 6 * sizeof(float);

        pipelineSettings.pDepthState = &depthStateDesc;
        pipelineSettings.pRasterizerState = &cubeRasterizerStateDesc;
        pipelineSettings.pShaderProgram = pZipTextureShader;
        addPipeline(pRenderer, &desc, &pZipTexturePipeline);

        vertexLayout = {};
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

        desc.mType = TF_PIPELINE_TYPE_GRAPHICS;
        pipelineSettings.mPrimitiveTopo = TF_PRIMITIVE_TOPO_TRI_LIST;
        pipelineSettings.mRenderTargetCount = 1;
        pipelineSettings.pDepthState = &depthStateDesc;
        pipelineSettings.pColorFormats = &pSwapChain->ppRenderTargets[0]->mFormat;
        pipelineSettings.mSampleCount = pSwapChain->ppRenderTargets[0]->mSampleCount;
        pipelineSettings.mSampleQuality = pSwapChain->ppRenderTargets[0]->mSampleQuality;
        pipelineSettings.mDepthStencilFormat = pDepthBuffer->mFormat;

        pipelineSettings.pVertexLayout = &vertexLayout;
        pipelineSettings.pRasterizerState = &rasterizerStateFrontDesc;
        pipelineSettings.pShaderProgram = pOcclusionShader;
        addPipeline(pRenderer, &desc, &pOcclusionTest);

        pipelineSettings.pDepthState = &noDepthStateDesc;
        pipelineSettings.pBlendState = &blendStateNoWriteDesc;
        pipelineSettings.pRasterizerState = &rasterizerStateFrontDesc;
        addPipeline(pRenderer, &desc, &pOcclusionMax);
    }

    void removePipelines()
    {
        removePipeline(pRenderer, pZipTexturePipeline);
        removePipeline(pRenderer, pPipelineSkybox);
        removePipeline(pRenderer, pBasicPipeline);
        removePipeline(pRenderer, pOcclusionTest);
        removePipeline(pRenderer, pOcclusionMax);
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
};

DEFINE_APPLICATION_MAIN(FileSystemUnitTest)
