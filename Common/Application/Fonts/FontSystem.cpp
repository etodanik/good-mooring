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

#include "../../Application/Interfaces/IFont.h"
#include "../../Utilities/Interfaces/IFileSystem.h"
#include "../../Utilities/Interfaces/ILog.h"

#include "../../Graphics/Interfaces/IGraphicsConfig.h"

#include "../../Resources/ResourceLoader/ThirdParty/OpenSource/tinyimageformat/tinyimageformat_query.h"
#include "../../Utilities/ThirdParty/OpenSource/Nothings/stb_ds.h"
#include "../../Utilities/ThirdParty/OpenSource/Nothings/stb_truetype.h"
#include "../../Utilities/ThirdParty/OpenSource/bstrlib/bstrlib.h"

#include "../../Graphics/Interfaces/IGraphics.h"
#include "../../Resources/ResourceLoader/Interfaces/IResourceLoader.h"

#include "../../Graphics/FSL/defaults.h"
#include "../../Graphics/FSL/fsl_srt.h"
#include "../../Utilities/RingBuffer.h"
#include "./Shaders/FSL/Font.srt.h"

#include "../../Utilities/Interfaces/IMemory.h"

static const uint32_t MAX_FRAMES = 3;

#ifdef ENABLE_FORGE_FONTS

typedef struct FontVertex
{
    float4 mPos; // xyz = object-space position, w - 1
    float4 mCol; // RGBA
    float4 mTex; // xy = em-space coords, z - scale
} FontVertex;

#endif

typedef struct TFGlyphData
{
    float4 mBound;
    float4 mTexcoord;
    float4 mOverlappedBound;
    float4 mOverlappedTexcoord;

    uint32_t mCodepoint;
    int32_t  mX;
    int32_t  mY;

    int32_t mAdvance;
    int32_t mLsb;

    int32_t mMSDFDataIdx;
    bool    mIsEmpty;
    bool    mLoaded;
} TFGlyphData;

struct TFFont
{
    bstring mName;
    bstring mPath;
    bstring mTextureName;

    TFMSDFFontInfo mFontInfo;
    stbtt_fontinfo mSTB;
    uint8_t*       pSTBData;

    uint32_t mGlyphResolution;
    uint32_t mAtlasResolution;
    uint32_t mAtlasWriteX;
    uint32_t mAtlasWriteY;

    float mAscentEm;
    float mDescentEm;
    float mLineGapEm;
    float mUnitScaleEm;
    float mScaleMultiplier;

    TFGlyphData* pGlyphs;
    uint32_t     mGlyphCount;

    half* pTempBuffer;

    struct
    {
        TFGlyphData* pQueryNextCodepoint;
        float        mFontSize;
        float        mSpacingCoef;

        float mScale;
        float mSpacing;
        float mRatio;
    } mQuery;

    bool mNeedToUpdateAtlas;

    TFTexture*       pAtlas;
    TFDescriptorSet* pDescriptorSet2D;
    TFBuffer*        pUniformBuffer2D;
};

#ifdef ENABLE_FORGE_FONTS

typedef struct TFFontSystem
{
    TFRenderer*      pRenderer = nullptr;
    TFPipelineCache* pCache = nullptr;
    uint32_t         mFrameMaxCount = 2u;
    const uint32_t*  pFrameIdx = NULL;

    // default screen size
    uint32_t mWidth = 1920;
    uint32_t mHeight = 1080;

    TFShader*   pShaders[2] = {};
    TFPipeline* pPipelines[2] = {};

    // 3D rendering
    TFDescriptorSet* pDescriptorSet = nullptr;
    GPURingBuffer    mUniformRingBuffer[MAX_FRAMES] = {};

    // Common
    GPURingBuffer mMeshRingBuffer[MAX_FRAMES] = {};

    static const uint32_t gMaxPerDrawSets = 512;
    uint32_t              mPerDrawSetIndex = 0;

    uint32_t mBaseTextureWidth = 0;
    uint32_t mReservedVertexCount = 0;
    float    mDpiScaleMin = 0;

    TFFont** ppFonts = NULL;
} TFSlugFontSystem;

static TFFontSystem gFontSystem = {};

static void removeFontInner(TFFont* pFont)
{
    removeResource(pFont->pUniformBuffer2D);
    removeDescriptorSet(gFontSystem.pRenderer, pFont->pDescriptorSet2D);

    removeResource(pFont->pAtlas);

    bdestroy(&pFont->mTextureName);

    tf_free(pFont->pGlyphs);
    tf_free(pFont->pTempBuffer);
    tf_free(pFont->pSTBData);
    tf_free(pFont);
}

static void requestGlyphLoad(TFFont* pFont, uint32_t codepoint)
{
    int32_t idx = stbtt_FindGlyphIndex(&pFont->mSTB, codepoint);
    if (pFont->pGlyphs[idx].mMSDFDataIdx != -1 && !pFont->pGlyphs[idx].mLoaded)
    {
        pFont->pGlyphs[idx].mLoaded = true;
        pFont->mNeedToUpdateAtlas = true;
    }
}

static void updateFontUniformData(TFFont* pFont)
{
    FontUniformData uniformBlockData = {};
    mat4            projection = f4x4OrthographicLH_ReverseZ(0, (float)gFontSystem.mWidth, (float)gFontSystem.mHeight, 0, -1, 1);
    for (uint32_t i = 0; i < VR_MULTIVIEW_COUNT; i++)
    {
        uniformBlockData.mMVP[i] = projection;
    }

    TFBufferUpdateDesc updateDesc = { pFont->pUniformBuffer2D };
    beginUpdateResource(&updateDesc);
    memcpy(updateDesc.pMappedData, &uniformBlockData, sizeof(uniformBlockData));
    endUpdateResource(&updateDesc);

    TFDescriptorData params[2] = {};
    params[0].mIndex = SRT_RES_IDX(FontSrtData, PerDraw, gUniformData);
    params[0].ppBuffers = &pFont->pUniformBuffer2D;
    params[1].mIndex = SRT_RES_IDX(FontSrtData, PerDraw, gAtlas);
    params[1].ppTextures = &pFont->pAtlas;

    updateDescriptorSet(gFontSystem.pRenderer, 0, pFont->pDescriptorSet2D, 2, params);
}

static void calculateTargetTexcoord(TFFont* pFont, TFGlyphData* pGlyph)
{
    pGlyph->mTexcoord = (float4((float)pGlyph->mX, (float)pGlyph->mY, (float)pGlyph->mX, (float)pGlyph->mY) + pGlyph->mOverlappedTexcoord) /
                        (float)pFont->mAtlasResolution;
}

static TFGlyphData* getOrRequestGlyphData(TFFont* pFont, uint32_t codepoint)
{
    int32_t idx = stbtt_FindGlyphIndex(&pFont->mSTB, codepoint);
    if (pFont->pGlyphs[idx].mLoaded)
    {
        return pFont->pGlyphs + idx;
    }

    requestGlyphLoad(pFont, codepoint);
    return NULL;
}

static bool tryToPlaceGlyphs(TFFont* pFont)
{
    // place glyphs to the atlas
    if (pFont->mAtlasWriteY > (pFont->mAtlasResolution - pFont->mGlyphResolution))
    {
        return false;
    }

    for (uint32_t i = 0; i < pFont->mGlyphCount; i++)
    {
        TFGlyphData glyph = pFont->pGlyphs[i];
        if (!glyph.mLoaded || glyph.mIsEmpty)
        {
            continue;
        }

        glyph.mX = pFont->mAtlasWriteX;
        glyph.mY = pFont->mAtlasWriteY;
        pFont->pGlyphs[i] = glyph;

        calculateTargetTexcoord(pFont, pFont->pGlyphs + i);

        pFont->mAtlasWriteX += pFont->mGlyphResolution;

        if (pFont->mAtlasWriteX + pFont->mGlyphResolution > pFont->mAtlasResolution)
        {
            pFont->mAtlasWriteY += pFont->mGlyphResolution;
            pFont->mAtlasWriteX = 0;

            if (pFont->mAtlasWriteY > (pFont->mAtlasResolution - pFont->mGlyphResolution))
            {
                return false;
            }
        }
    }
    return true;
}

static bool updateGlyphsPlacing(TFFont* pFont)
{
    // setup undefined atlas coord
    for (uint32_t i = 0; i < pFont->mGlyphCount; i++)
    {
        pFont->pGlyphs[i].mX = -1;
        pFont->pGlyphs[i].mY = -1;
    }

    pFont->mAtlasWriteX = 0;
    pFont->mAtlasWriteY = 0;
    return tryToPlaceGlyphs(pFont);
}

static void allocateAtlas(TFFont* pFont, uint32_t resolution)
{
    pFont->mAtlasResolution = resolution;

    TFTextureDesc desc = {};
    desc.mArraySize = 1;
    desc.mDepth = 1;
    desc.mDescriptors = TF_DESCRIPTOR_TYPE_TEXTURE;
    desc.mFormat = TinyImageFormat_R16G16B16A16_SFLOAT;
    desc.mHeight = pFont->mAtlasResolution;
    desc.mMipLevels = 1;
    desc.mSampleCount = TF_SAMPLE_COUNT_1;
    desc.mStartState = TF_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    desc.mWidth = pFont->mAtlasResolution;
    desc.pName = bdata(&pFont->mTextureName);
    TFTextureLoadDesc loadDesc = {};
    loadDesc.ppTexture = &pFont->pAtlas;
    loadDesc.pDesc = &desc;

    addResource(&loadDesc, NULL);
}

static void updateAtlas(TFFont* pFont)
{
    TFFileStream fh = {};

    if (fsOpenStreamFromPath(TF_RD_FONTS, bdata(&pFont->mPath), TF_FM_READ, &fh))
    {
        ssize_t bytes = fsGetStreamFileSize(&fh);
        ASSERT(bytes < UINT32_MAX);
    }
    else
    {
        LOGF(LogLevel::eERROR, "Failed to open MSDF font file.Function %s failed with error: %s", FS_ERR_CTX.func,
             getFSErrCodeString(FS_ERR_CTX.code));
        return;
    }

    TFTextureUpdateDesc updateDesc = { pFont->pAtlas, 0, 1, 0, 1, TF_RESOURCE_STATE_PIXEL_SHADER_RESOURCE };
    beginUpdateResource(&updateDesc);
    TFTextureSubresourceUpdate subresource = updateDesc.getSubresourceUpdateDesc(0, 0);

    struct TargetData
    {
        half mX;
        half mY;
        half mZ;
        half mA;
    };

    uint32_t sourcePixelSize = sizeof(half) * 3;
    uint32_t targetPixelSize = sizeof(TargetData);

    uint32_t msdfDataSize = pFont->mGlyphResolution * pFont->mGlyphResolution * sourcePixelSize;

    TargetData targetData{};
    targetData.mA = half(1);

    for (uint32_t i = 0; i < pFont->mGlyphCount; i++)
    {
        TFGlyphData glyph = pFont->pGlyphs[i];
        if (!glyph.mLoaded || glyph.mX == -1 || glyph.mY == -1)
        {
            continue;
        }

        fsSeekStream(&fh, TF_SBO_START_OF_FILE,
                     pFont->mFontInfo.mMSDFGlyphOffset + pFont->mFontInfo.mMSDFGlyphStride * glyph.mMSDFDataIdx + sizeof(TFMSDFGlyphInfo));
        fsReadFromStream(&fh, pFont->pTempBuffer, msdfDataSize);

        uint32_t writePos = glyph.mX * targetPixelSize + glyph.mY * subresource.mDstRowStride;
        for (uint32_t y = 0; y < pFont->mGlyphResolution; y++)
        {
            for (uint32_t x = 0; x < pFont->mGlyphResolution; x++)
            {
                memcpy(&targetData, &(pFont->pTempBuffer[(y * pFont->mGlyphResolution + x) * 3]), sourcePixelSize);
                memcpy(subresource.pMappedData + writePos + x * targetPixelSize, &targetData, targetPixelSize);
            }
            writePos += subresource.mDstRowStride;
        }
    }

    endUpdateResource(&updateDesc);

    fsCloseStream(&fh);
}

static int32_t fontKerning(TFFont* pFont, uint32_t cp1, uint32_t cp2) { return stbtt_GetCodepointKernAdvance(&pFont->mSTB, cp1, cp2); }

static float getVertAlign(TFFont* pFont, float scale) { return pFont->mAscentEm * scale; }

static void drawGlyph(TFFont* pFont, TFGlyphData* pGlyph, float ox, float oy, float ratio, float scale, float4 col, int8_t* pMemory,
                      int32_t* pOffset)
{
    UNREF_PARAM(pFont);

    if (!pGlyph->mIsEmpty)
    {
        float4 coord = float4(ox, oy, ox, oy) + pGlyph->mOverlappedBound * scale;

        float mTx0 = pGlyph->mTexcoord.x;
        float mTy0 = pGlyph->mTexcoord.y;
        float mTx1 = pGlyph->mTexcoord.z;
        float mTy1 = pGlyph->mTexcoord.w;

        FontVertex quad[4];
        quad[0].mPos = float4(coord.x, coord.y, 0, 1);
        quad[0].mTex = float4(mTx0, mTy0, ratio, 0);
        quad[0].mCol = col;

        quad[1].mPos = float4(coord.z, coord.y, 0, 1);
        quad[1].mTex = float4(mTx1, mTy0, ratio, 0);
        quad[1].mCol = col;

        quad[2].mPos = float4(coord.z, coord.w, 0, 1);
        quad[2].mTex = float4(mTx1, mTy1, ratio, 0);
        quad[2].mCol = col;

        quad[3].mPos = float4(coord.x, coord.w, 0, 1);
        quad[3].mTex = float4(mTx0, mTy1, ratio, 0);
        quad[3].mCol = col;

        FontVertex* pVertexDst = (FontVertex*)(pMemory + *pOffset);
        pVertexDst[0] = quad[0];
        pVertexDst[1] = quad[1];
        pVertexDst[2] = quad[2];
        pVertexDst[3] = quad[0];
        pVertexDst[4] = quad[2];
        pVertexDst[5] = quad[3];
        *pOffset += sizeof(FontVertex) * 6;
    }
}

// return text width
static float fillVertexBuffer(TFFont* pFont, const char* pText, float2 cursor, float spacing, float ratio, float scale, float4 color,
                              uint32_t* pOutFirstVertex, uint32_t* pOutVertexCount)
{
    UNREF_PARAM(spacing);
    int32_t vertexCount = (int32_t)strlen(pText) * 6;
    int32_t vertexMemory = vertexCount * sizeof(FontVertex);

    GPURingBufferOffset buffer = getGPURingBufferOffset(gFontSystem.mMeshRingBuffer + (*gFontSystem.pFrameIdx), vertexMemory);
    TFBufferUpdateDesc  update = { buffer.pBuffer, buffer.mOffset };
    beginUpdateResource(&update);
    int32_t memoryOffset = 0;

    const char* p = pText;
    int32_t     startCursorX = (int32_t)cursor.x;
    float       cursorX = (float)startCursorX;
    uint32_t    prevCp = 0;

    while (*p)
    {
        TFCodepoint cp = 0;
        p = fntConsumeSymbol(p, &cp);
        if (prevCp)
        {
            cursorX += fontKerning(pFont, prevCp, cp) * scale; //-V1026
        }

        TFGlyphData* pGlyph = getOrRequestGlyphData(pFont, cp);
        if (pGlyph)
        {
            drawGlyph(pFont, pGlyph, (float)cursorX, cursor.y, ratio, scale, color, (int8_t*)update.pMappedData, &memoryOffset);
            cursorX += pGlyph->mAdvance * scale + spacing; //-V1026
        }

        prevCp = cp;
    }

    endUpdateResource(&update);

    *pOutVertexCount = memoryOffset / sizeof(FontVertex);
    *pOutFirstVertex = (uint32_t)(buffer.mOffset / sizeof(FontVertex));

    return fabsf((float)(cursorX - startCursorX));
}

static float4 getGlyphCoord(TFGlyphData* pGlyph, float ox, float oy, float scale)
{
    return float4(ox, oy, ox, oy) + pGlyph->mBound * scale;
}

#endif

bool platformInitFontSystem()
{
#ifdef ENABLE_FORGE_FONTS
    gFontSystem = {};

    float          dpiScale[2] = {};
    const uint32_t monitorIdx = getActiveMonitorIdx();
    getMonitorDpiScale(monitorIdx, dpiScale);
    gFontSystem.mDpiScaleMin = minf(dpiScale[0], dpiScale[1]);
#endif

    return true;
}

// not used anymore
void platformExitFontSystem()
{
#ifdef ENABLE_FORGE_FONTS
#endif
}

void initFontSystem(TFFontSystemDesc* pDesc)
{
#ifdef ENABLE_FORGE_FONTS
    gFontSystem.pRenderer = pDesc->pRenderer;
    gFontSystem.mFrameMaxCount = pDesc->mFrameMaxCount;
    gFontSystem.pFrameIdx = pDesc->pFrameIdx;
    ASSERT(gFontSystem.mFrameMaxCount <= MAX_FRAMES);

    gFontSystem.mBaseTextureWidth = 256;
    gFontSystem.mReservedVertexCount = 65536;

    TFBufferDesc vbDesc = {};
    vbDesc.mDescriptors = TF_DESCRIPTOR_TYPE_VERTEX_BUFFER;
    vbDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
    vbDesc.mSize = gFontSystem.mReservedVertexCount * sizeof(FontVertex);
    vbDesc.mFlags = TF_BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
    vbDesc.pName = "Font System Reserved Vertices";
    for (uint32_t i = 0; i < gFontSystem.mFrameMaxCount; i++)
    {
        addGPURingBuffer(gFontSystem.pRenderer, &vbDesc, gFontSystem.mMeshRingBuffer + i);
        addUniformGPURingBuffer(gFontSystem.pRenderer, 4096, gFontSystem.mUniformRingBuffer + i, true);
    }
    arrsetlen(gFontSystem.ppFonts, 0);
#else
    UNREF_PARAM(pDesc);
#endif
}

void exitFontSystem()
{
#ifdef ENABLE_FORGE_FONTS
    int32_t slugFontCount = (int32_t)arrlen(gFontSystem.ppFonts);
    for (int32_t i = 0; i < slugFontCount; i++)
    {
        TFFont* pFont = gFontSystem.ppFonts[i]; //-V595
        removeFontInner(pFont);
    }

    arrfree(gFontSystem.ppFonts);

    for (uint32_t i = 0; i < gFontSystem.mFrameMaxCount; i++)
    {
        removeGPURingBuffer(gFontSystem.mMeshRingBuffer + i);
        removeGPURingBuffer(gFontSystem.mUniformRingBuffer + i);
    }
#endif
}

void loadFontSystem(const TFFontSystemLoadDesc* pDesc)
{
#ifdef ENABLE_FORGE_FONTS
    gFontSystem.pCache = pDesc->pCache;
    gFontSystem.mWidth = pDesc->mWidth;
    gFontSystem.mHeight = pDesc->mHeight;

    TFShaderLoadDesc text2DShaderDesc = {};
    text2DShaderDesc.mVert = { "font.vert" };
    text2DShaderDesc.mFrag = { "font.frag" };

    addShader(gFontSystem.pRenderer, &text2DShaderDesc, &gFontSystem.pShaders[0]);

    TFShaderLoadDesc text3DShaderDesc = {};
    text3DShaderDesc.mVert = { "font.vert" };
    text3DShaderDesc.mFrag = { "font_3D.frag" };

    addShader(gFontSystem.pRenderer, &text3DShaderDesc, &gFontSystem.pShaders[1]);

    TFDescriptorSetDesc setDesc = SRT_SET_DESC(FontSrtData, PerDraw, gFontSystem.gMaxPerDrawSets, 0);
    addDescriptorSet(gFontSystem.pRenderer, &setDesc, &gFontSystem.pDescriptorSet);

    TFVertexLayout vertexLayout = {};
    vertexLayout.mBindingCount = 1;
    vertexLayout.mAttribCount = 3;
    vertexLayout.mAttribs[0].mSemantic = TF_SEMANTIC_POSITION;
    vertexLayout.mAttribs[0].mFormat = TinyImageFormat_R32G32B32A32_SFLOAT;
    vertexLayout.mAttribs[0].mBinding = 0;
    vertexLayout.mAttribs[0].mLocation = 0;
    vertexLayout.mAttribs[0].mOffset = 0;

    vertexLayout.mAttribs[1].mSemantic = TF_SEMANTIC_COLOR;
    vertexLayout.mAttribs[1].mFormat = TinyImageFormat_R32G32B32A32_SFLOAT;
    vertexLayout.mAttribs[1].mBinding = 0;
    vertexLayout.mAttribs[1].mLocation = 1;
    vertexLayout.mAttribs[1].mOffset = offsetof(FontVertex, mCol);

    vertexLayout.mAttribs[2].mSemantic = TF_SEMANTIC_TEXCOORD0;
    vertexLayout.mAttribs[2].mFormat = TinyImageFormat_R32G32B32A32_SFLOAT;
    vertexLayout.mAttribs[2].mBinding = 0;
    vertexLayout.mAttribs[2].mLocation = 2;
    vertexLayout.mAttribs[2].mOffset = offsetof(FontVertex, mTex);

    TFBlendStateDesc blendStateDesc = {};
    blendStateDesc.mSrcFactors[0] = TF_BC_SRC_ALPHA;
    blendStateDesc.mDstFactors[0] = TF_BC_ONE_MINUS_SRC_ALPHA;
    blendStateDesc.mSrcAlphaFactors[0] = TF_BC_SRC_ALPHA;
    blendStateDesc.mDstAlphaFactors[0] = TF_BC_ONE_MINUS_SRC_ALPHA;
    blendStateDesc.mColorWriteMasks[0] = TF_COLOR_MASK_ALL;
    blendStateDesc.mRenderTargetMask = TF_BLEND_STATE_TARGET_ALL;
    blendStateDesc.mIndependentBlend = false;

    TFDepthStateDesc depthStateDesc[2] = {};
    depthStateDesc[0].mDepthTest = false;
    depthStateDesc[0].mDepthWrite = false;

    depthStateDesc[1].mDepthTest = true;
    depthStateDesc[1].mDepthWrite = true;
    depthStateDesc[1].mDepthFunc = (TFCompareMode)pDesc->mDepthCompareMode;

    TFRasterizerStateDesc rasterizerStateDesc[2] = {};
    rasterizerStateDesc[0].mCullMode = TF_CULL_MODE_NONE;
    rasterizerStateDesc[0].mScissor = true;

    rasterizerStateDesc[1].mCullMode = (TFCullMode)pDesc->mCullMode;
    rasterizerStateDesc[1].mScissor = true;

    TFPipelineDesc pipelineDesc = {};
    PIPELINE_LAYOUT_DESC(pipelineDesc, NULL, NULL, NULL, SRT_LAYOUT_DESC(FontSrtData, PerDraw))
    pipelineDesc.pCache = pDesc->pCache;
    pipelineDesc.mType = TF_PIPELINE_TYPE_GRAPHICS;
    pipelineDesc.mGraphicsDesc.mPrimitiveTopo = TF_PRIMITIVE_TOPO_TRI_LIST;
    pipelineDesc.mGraphicsDesc.mRenderTargetCount = 1;
    pipelineDesc.mGraphicsDesc.mSampleCount = TF_SAMPLE_COUNT_1;
    pipelineDesc.mGraphicsDesc.pBlendState = &blendStateDesc;
    pipelineDesc.mGraphicsDesc.pVertexLayout = &vertexLayout;
    pipelineDesc.mGraphicsDesc.mRenderTargetCount = 1;
    pipelineDesc.mGraphicsDesc.mSampleCount = TF_SAMPLE_COUNT_1;
    pipelineDesc.mGraphicsDesc.mSampleQuality = 0;
    pipelineDesc.mGraphicsDesc.pColorFormats = (TinyImageFormat*)&pDesc->mColorFormat;

    bool haveDepthBuffer = pDesc->mDepthFormat == TinyImageFormat_UNDEFINED ? false : true;
    for (uint32_t i = 0; i < 2; ++i)
    {
        pipelineDesc.mGraphicsDesc.mDepthStencilFormat = (i > 0) ? (TinyImageFormat)pDesc->mDepthFormat : TinyImageFormat_UNDEFINED;
        pipelineDesc.mGraphicsDesc.pShaderProgram = gFontSystem.pShaders[i];
        pipelineDesc.mGraphicsDesc.pDepthState = &depthStateDesc[haveDepthBuffer ? i : 0];
        pipelineDesc.mGraphicsDesc.pRasterizerState = &rasterizerStateDesc[i];
        addPipeline(gFontSystem.pRenderer, &pipelineDesc, &gFontSystem.pPipelines[i]);
    }

    int32_t fontCount = (int32_t)arrlen(gFontSystem.ppFonts);
    for (int32_t i = 0; i < fontCount; i++)
    {
        TFFont* pFont = gFontSystem.ppFonts[i];
        updateFontUniformData(pFont);
    }
#else
    UNREF_PARAM(pDesc);
#endif
}

void unloadFontSystem()
{
#ifdef ENABLE_FORGE_FONTS
    removePipeline(gFontSystem.pRenderer, gFontSystem.pPipelines[0]);
    removePipeline(gFontSystem.pRenderer, gFontSystem.pPipelines[1]);
    removeDescriptorSet(gFontSystem.pRenderer, gFontSystem.pDescriptorSet);

    removeShader(gFontSystem.pRenderer, gFontSystem.pShaders[0]);
    removeShader(gFontSystem.pRenderer, gFontSystem.pShaders[1]);
#endif
}

TFFont* addFont(TFFontDesc* pFontDesc)
{
#ifdef ENABLE_FORGE_FONTS
    int32_t count = (int32_t)arrlen(gFontSystem.ppFonts);
    for (int32_t i = 0; i < count; i++)
    {
        const char* pPath = bdata(&(gFontSystem.ppFonts[i]->mPath));
        if (pPath != NULL && strcmp(pFontDesc->pFontPath, pPath) == 0)
        {
            return gFontSystem.ppFonts[i];
        }
    }

    uint8_t* pBuffer = NULL;

    TFFileStream fh = {};
    if (fsOpenStreamFromPath(TF_RD_FONTS, pFontDesc->pFontPath, TF_FM_READ, &fh))
    {
        ssize_t bytes = fsGetStreamFileSize(&fh);
        ASSERT(bytes < UINT32_MAX);

        pBuffer = (uint8_t*)tf_malloc(bytes);
        fsReadFromStream(&fh, pBuffer, bytes);

        fsCloseStream(&fh);
    }
    else
    {
        LOGF(LogLevel::eERROR, "Failed to open font file.Function %s failed with error: %s", FS_ERR_CTX.func,
             getFSErrCodeString(FS_ERR_CTX.code));
        return NULL;
    }

    TFMSDFFontInfo fontInto{};
    memcpy(&fontInto, pBuffer, sizeof(TFMSDFFontInfo));

    uint8_t* pSTBData = (uint8_t*)tf_malloc(fontInto.mSTBFontSize);
    memcpy(pSTBData, pBuffer + fontInto.mSTBFontOffset, fontInto.mSTBFontSize);

    stbtt_fontinfo stb;
    if (!stbtt_InitFont(&stb, pSTBData, stbtt_GetFontOffsetForIndex(pSTBData, 0)))
    {
        fprintf(stderr, "Failed to parse font.\n");
        tf_free(pSTBData);
        tf_free(pBuffer);
        return NULL;
    }

    TFFont* pFont = (TFFont*)tf_malloc(sizeof(TFFont));
    *pFont = {};

    pFont->mName = bconstfromcstr(pFontDesc->pFontName);
    pFont->mPath = bconstfromcstr(pFontDesc->pFontPath);
    pFont->mTextureName = bempty();
    bformat(&pFont->mTextureName, "%s MSDF Texture", pFontDesc->pFontName);

    pFont->mFontInfo = fontInto;
    pFont->pSTBData = pSTBData;
    pFont->mSTB = stb;

    int32_t asc, dsc, lg;
    stbtt_GetFontVMetrics(&stb, &asc, &dsc, &lg);
    pFont->mAscentEm = (float)asc;
    pFont->mDescentEm = (float)dsc;
    pFont->mLineGapEm = (float)lg;
    pFont->mUnitScaleEm = stbtt_ScaleForMappingEmToPixels(&pFont->mSTB, 1.0f);
    pFont->mScaleMultiplier = pFontDesc->mScaleMultiplier;

    pFont->mGlyphResolution = fontInto.mGlyphResolution;
    pFont->mAtlasResolution = gFontSystem.mBaseTextureWidth;

    pFont->mGlyphCount = stb.numGlyphs;
    pFont->pGlyphs = (TFGlyphData*)tf_calloc(pFont->mGlyphCount, sizeof(TFGlyphData));

    // fill in default glyph data
    {
        TFGlyphData defaultGlyph = {};
        defaultGlyph.mMSDFDataIdx = -1;
        defaultGlyph.mIsEmpty = true;
        defaultGlyph.mX = -1;
        defaultGlyph.mY = -1;
        for (uint32_t i = 0; i < pFont->mGlyphCount; i++)
        {
            pFont->pGlyphs[i] = defaultGlyph;
        }
    }

    for (uint32_t i = 0; i < fontInto.mMSDFGlyphCount; i++)
    {
        TFMSDFGlyphInfo* pGlyphInfo = (TFMSDFGlyphInfo*)(pBuffer + (fontInto.mMSDFGlyphOffset + fontInto.mMSDFGlyphStride * i));
        TFMSDFGlyphInfo  info = *pGlyphInfo;
        int32_t          idx = stbtt_FindGlyphIndex(&stb, info.mCodepoint);

        TFGlyphData glyph = {};
        glyph.mMSDFDataIdx = i;
        glyph.mCodepoint = info.mCodepoint;
        glyph.mBound = info.mBound;
        glyph.mTexcoord = info.mTexcoord;
        glyph.mOverlappedBound = info.mOverlapedBound;
        glyph.mOverlappedTexcoord = info.mOverlapedTexcoord;
        glyph.mLsb = info.mLeftBearing;
        glyph.mAdvance = info.mAdvance;
        glyph.mIsEmpty = (info.mTexcoord == f4MakeScalar(0));
        glyph.mLoaded = false;
        pFont->pGlyphs[idx] = glyph;
    }

    tf_free(pBuffer);

    pFont->pTempBuffer = (half*)tf_calloc(fontInto.mGlyphResolution * fontInto.mGlyphResolution * 3, sizeof(half));

    if ((pFontDesc->mFlags & TF_FONT_ASCII) != 0)
    {
        for (uint32_t i = 0; i < 128; i++)
        {
            requestGlyphLoad(pFont, i);
        }
    }

    for (uint32_t i = 0; i < pFontDesc->mGlyphCount; i++)
    {
        requestGlyphLoad(pFont, pFontDesc->pGlyphsToLoad[i]);
    }

    if ((pFontDesc->mFlags & TF_FONT_ATLAS_AUTO_RESOLUTION_ON_INIT) != 0)
    {
        pFont->mAtlasResolution = fntGetRecomendedAtlasResolution(pFont);
    }
    else
    {
        ASSERT(pFont->mAtlasResolution >= pFont->mGlyphResolution);
    }

    allocateAtlas(pFont, pFont->mAtlasResolution);

    pFont->mNeedToUpdateAtlas = !updateGlyphsPlacing(pFont);

    updateAtlas(pFont);

    // Uniform data for 2D
    TFDescriptorSetDesc setDesc = SRT_SET_DESC(FontSrtData, PerDraw, 1, 0);
    addDescriptorSet(gFontSystem.pRenderer, &setDesc, &pFont->pDescriptorSet2D);

    TFBufferLoadDesc ubLoadDesc = {};
    ubLoadDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    ubLoadDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
    ubLoadDesc.mDesc.mSize = sizeof(FontUniformData);
    ubLoadDesc.mDesc.mFlags = TF_BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
    ubLoadDesc.mDesc.pName = "Font 2D Uniform Buffer";
    ubLoadDesc.ppBuffer = &pFont->pUniformBuffer2D;
    addResource(&ubLoadDesc, NULL);

    updateFontUniformData(pFont);

    arrpush(gFontSystem.ppFonts, pFont);

    return pFont;
#else
    UNREF_PARAM(pFontDesc);
    return NULL;
#endif
}

void removeFont(TFFont* pFont)
{
#ifdef ENABLE_FORGE_FONTS
    int32_t count = (int32_t)arrlen(gFontSystem.ppFonts);
    for (int32_t i = 0; i < count; i++)
    {
        if (gFontSystem.ppFonts[i] == pFont)
        {
            arrdel(gFontSystem.ppFonts, i);
            break;
        }
    }

    removeFontInner(pFont);
#else
    UNREF_PARAM(pFont);
#endif
}

uint32_t fntGetRecomendedAtlasResolution(TFFont* pFont)
{
#ifdef ENABLE_FORGE_FONTS
    int32_t glyphCount = 0;
    for (uint32_t i = 0; i < pFont->mGlyphCount; i++)
    {
        TFGlyphData glyph = pFont->pGlyphs[i];
        if (glyph.mLoaded && !glyph.mIsEmpty)
        {
            glyphCount++;
        }
    }

    uint32_t glyphsPerSide = (uint32_t)ceilf(sqrtf((float)glyphCount));
    return glyphsPerSide * pFont->mGlyphResolution;
#else
    UNREF_PARAM(pFont);
    return 0;
#endif
}

bool fntIsNeedUpdateAtlas(TFFont* pFont)
{
#ifdef ENABLE_FORGE_FONTS
    return pFont->mNeedToUpdateAtlas;
#else
    UNREF_PARAM(pFont);
    return false;
#endif
}

bool fntUpdateAtlas(TFFont* pFont, int32_t newResolution)
{
#ifdef ENABLE_FORGE_FONTS
    bool isEnoughSpace = tryToPlaceGlyphs(pFont);
    if (!isEnoughSpace || newResolution != (int32_t)pFont->mAtlasResolution)
    {
        removeResource(pFont->pAtlas);
        allocateAtlas(pFont, newResolution == -1 ? fntGetRecomendedAtlasResolution(pFont) : newResolution);

        pFont->mNeedToUpdateAtlas = !updateGlyphsPlacing(pFont);
    }
    else
    {
        pFont->mNeedToUpdateAtlas = false;
    }
    updateAtlas(pFont);

    updateFontUniformData(pFont);
    waitForAllResourceLoads();
    return !pFont->mNeedToUpdateAtlas;
#else
    UNREF_PARAM(pFont);
    UNREF_PARAM(newResolution);
    return false;
#endif
}

/**
 * @brief Decodes a single UTF-8 character into its Unicode codepoint.
 *
 * This function reads a UTF-8 string pointer, determines the byte length of the
 * current character (1 to 4 bytes) based on its leading byte, extracts the
 * decoded Unicode codepoint value, and advances the string pointer forward.
 */
const char* fntConsumeSymbol(const char* pSymbol, TFCodepoint* pCodepoint)
{
#ifdef ENABLE_FORGE_FONTS
    const uint8_t* s = (const uint8_t*)pSymbol;

    if (s[0] < 0x80)
    {
        // 1-byte sequence (Standard ASCII: 0xxxxxxx)
        *pCodepoint = s[0];
        pSymbol += 1;
    }
    else if ((s[0] & 0xE0) == 0xC0)
    {
        // 2-byte sequence (110xxxxx 10xxxxxx)
        // Mask out header bits and combine the payload bits from both bytes
        *pCodepoint = ((s[0] & 0x1F) << 6) | (s[1] & 0x3F);
        pSymbol += 2;
    }
    else if ((s[0] & 0xF0) == 0xE0)
    {
        // 3-byte sequence (1110xxxx 10xxxxxx 10xxxxxx)
        *pCodepoint = ((s[0] & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
        pSymbol += 3;
    }
    else
    {
        // 4-byte sequence (11110xxx 10xxxxxx 10xxxxxx 10xxxxxx)
        *pCodepoint = ((s[0] & 0x07) << 18) | ((s[1] & 0x3F) << 12) | ((s[2] & 0x3F) << 6) | (s[3] & 0x3F);
        pSymbol += 4;
    }
    return pSymbol;
#else
    UNREF_PARAM(pSymbol);
    UNREF_PARAM(pCodepoint);
    return NULL;
#endif
}

TFFontGlyphData fntQueryGlyph(TFFont* pFont, const TFFontGlyphQueryDesc* pQueryDesc)
{
#ifdef ENABLE_FORGE_FONTS
    TFGlyphData* pGlyph = NULL;
    if (pFont->mQuery.pQueryNextCodepoint && pFont->mQuery.pQueryNextCodepoint->mCodepoint == pQueryDesc->mCodepoint)
    {
        pGlyph = pFont->mQuery.pQueryNextCodepoint;
    }
    else
    {
        pGlyph = getOrRequestGlyphData(pFont, pQueryDesc->mCodepoint);
    }

    TFFontGlyphData resultData{};
    if (pGlyph == NULL)
    {
        return resultData;
    }

    float kerningAdvance = 0.0f;

    if (pQueryDesc->mFontSize != pFont->mQuery.mFontSize || pQueryDesc->mFontSpacing != pFont->mQuery.mSpacingCoef)
    {
        float fontSize = pQueryDesc->mFontSize * gFontSystem.mDpiScaleMin;
        pFont->mQuery.mSpacing = pQueryDesc->mFontSpacing * gFontSystem.mDpiScaleMin;
        pFont->mQuery.mScale = stbtt_ScaleForPixelHeight(&pFont->mSTB, fontSize);
        pFont->mQuery.mRatio = fontSize / pFont->mGlyphResolution;

        pFont->mQuery.mFontSize = pQueryDesc->mFontSize;
        pFont->mQuery.mSpacingCoef = pQueryDesc->mFontSpacing;
    }

    float fontScale = pFont->mQuery.mScale;
    float spacing = pFont->mQuery.mSpacing;

    TFGlyphData* pAdditionalGlyph = NULL;
    if (pQueryDesc->mAdditionalCodepoint > 0)
    {
        if (pFont->mQuery.pQueryNextCodepoint && pFont->mQuery.pQueryNextCodepoint->mCodepoint == pQueryDesc->mAdditionalCodepoint)
        {
            pAdditionalGlyph = pFont->mQuery.pQueryNextCodepoint;
        }
        else
        {
            pAdditionalGlyph = getOrRequestGlyphData(pFont, pQueryDesc->mAdditionalCodepoint);
        }

        kerningAdvance = (float)fontKerning(pFont, pQueryDesc->mCodepoint, pQueryDesc->mAdditionalCodepoint) * fontScale;
    }

    // Cache to reduce glyph queries when querying all codepoints in a continuous string
    if (pQueryDesc->mAdditionalCodepoint && pQueryDesc->mAdditionalCodepointIsNext)
    {
        pFont->mQuery.pQueryNextCodepoint = pAdditionalGlyph;
    }
    else
    {
        pFont->mQuery.pQueryNextCodepoint = pGlyph;
    }

    if (!pQueryDesc->mOnlyXAdvance)
    {
        float yAlign = getVertAlign(pFont, fontScale);
        resultData.mBound = pGlyph->mOverlappedBound * fontScale + float4(0, yAlign, 0, yAlign);
        resultData.mTexcoord = pGlyph->mTexcoord;
        resultData.mRatio = pFont->mQuery.mRatio;
    }

    resultData.mXAdvance = pGlyph->mAdvance * fontScale + spacing + kerningAdvance;

    return resultData;
#else
    UNREF_PARAM(pFont);
    UNREF_PARAM(pQueryDesc);
    TFFontGlyphData resultData{};
    return resultData;
#endif
}

float2 fntMeasureFontText(TFFont* pFont, const char* pText, uint32_t textLength, float fontSize)
{
#ifdef ENABLE_FORGE_FONTS
    fontSize *= gFontSystem.mDpiScaleMin;
    float scale = stbtt_ScaleForPixelHeight(&pFont->mSTB, fontSize);
    float oy = getVertAlign(pFont, scale);
    float ox = 0;

    const char* p = pText;
    uint32_t    prevCp = 0;
    float4      bound = f4Make(0, oy, 0, oy);

    for (uint32_t i = 0; i < textLength; i++)
    {
        TFCodepoint cp = 0;
        p = fntConsumeSymbol(p, &cp);
        if (prevCp)
        {
            ox += (float)fontKerning(pFont, prevCp, cp) * scale;
        }

        TFGlyphData* pGlyph = getOrRequestGlyphData(pFont, cp);
        if (pGlyph && !pGlyph->mIsEmpty)
        {
            float4 coord = getGlyphCoord(pGlyph, (float)ox, oy, scale);
            ox += pGlyph->mAdvance * scale;

            if (coord.x < bound.x)
                bound.x = coord.x;
            if (coord.z > bound.x)
                bound.x = coord.z;

            if (coord.w < bound.y)
                bound.y = coord.w;
            if (coord.y > bound.w)
                bound.w = coord.y;
        }

        prevCp = cp;
    }

    // wasn't update
    if (bound.x == FLT_MAX)
    {
        return f2Make(0, 0);
    }

    return f2Make(fabsf(bound.z - bound.x), fabsf(bound.w - bound.y));
#else
    UNREF_PARAM(pFont);
    UNREF_PARAM(pText);
    UNREF_PARAM(textLength);
    UNREF_PARAM(fontSize);
    return f2Make(0, 0);
#endif
}

TFTexture* fntGetAtlas(TFFont* pFont)
{
#ifdef ENABLE_FORGE_FONTS
    return pFont->pAtlas;
#else
    UNREF_PARAM(pFont);
    return NULL;
#endif
}

float fntGetRatio(TFFont* pFont, float fontSize)
{
#ifdef ENABLE_FORGE_FONTS
    // Undo the encoder's distance normalization, then convert font units to
    // physical pixels. Glyph atlas resolution does not determine edge width.
    return stbtt_ScaleForPixelHeight(&pFont->mSTB, fontSize) * TF_MSDF_FONT_UNITS_PER_DISTANCE_UNIT * pFont->mFontInfo.mPxRange *
           pFont->mScaleMultiplier;
#else
    UNREF_PARAM(pFont);
    UNREF_PARAM(fontSize);
    return 0.0f;
#endif
}

void cmdDrawText(TFCmd* pCmd, float2 screenPos, const TFFontDrawDesc* pDrawDesc)
{
#ifdef ENABLE_FORGE_FONTS
    TFFont* pFont = pDrawDesc->pFont;
    float   fontSize = pDrawDesc->mFontSize * gFontSystem.mDpiScaleMin;
    float   spacing = pDrawDesc->mFontSpacing * gFontSystem.mDpiScaleMin;
    float   ratio = fntGetRatio(pDrawDesc->pFont, fontSize);
    float   scale = stbtt_ScaleForPixelHeight(&pFont->mSTB, fontSize);

    screenPos.y += getVertAlign(pDrawDesc->pFont, scale);

    screenPos.x = floorf(screenPos.x);
    screenPos.y = floorf(screenPos.y);

    uint32_t vertexCount = 0;
    uint32_t firstVertex = 0;
    fillVertexBuffer(pFont, pDrawDesc->pText, screenPos, spacing, ratio, scale, unpackR8G8B8A8_SRGB(pDrawDesc->mFontColor), &firstVertex,
                     &vertexCount);

    cmdBindPipeline(pCmd, gFontSystem.pPipelines[0]);
    cmdBindDescriptorSet(pCmd, 0, pFont->pDescriptorSet2D);
    const uint32_t stride = sizeof(FontVertex);
    const uint64_t offset = 0;
    cmdBindVertexBuffer(pCmd, 1, &gFontSystem.mMeshRingBuffer[*gFontSystem.pFrameIdx].pBuffer, &stride, &offset);
    cmdDraw(pCmd, vertexCount, firstVertex);
#else
    UNREF_PARAM(pCmd);
    UNREF_PARAM(screenPos);
    UNREF_PARAM(pDrawDesc);
#endif
}

void cmdDrawWorldSpaceText(TFCmd* pCmd, const mat4* pMatWorld, const TFCameraMatrix* pMatProjView, const TFFontDrawDesc* pDrawDesc)
{
#ifdef ENABLE_FORGE_FONTS
    TFFont*  pFont = pDrawDesc->pFont;
    float    spacing = pDrawDesc->mFontSpacing * 100.0f;
    float    ratio = pFont->mScaleMultiplier;
    float    scale = stbtt_ScaleForPixelHeight(&pFont->mSTB, 100.0f);
    uint32_t frameIdx = *gFontSystem.pFrameIdx;

    uint32_t vertexCount = 0;
    uint32_t firstVertex = 0;
    float width = fillVertexBuffer(pFont, pDrawDesc->pText, float2(0, 0), spacing, ratio, scale, unpackR8G8B8A8_SRGB(pDrawDesc->mFontColor),
                                   &firstVertex, &vertexCount);

    FontUniformData     uniformBlockData = {};
    GPURingBufferOffset uniformBlock = getGPURingBufferOffset(&gFontSystem.mUniformRingBuffer[frameIdx], sizeof(FontUniformData));
    TFBufferUpdateDesc  updateDesc = { uniformBlock.pBuffer, uniformBlock.mOffset };
    beginUpdateResource(&updateDesc);

    // scale correction
    // fillVertexBuffer - fill in data first of all for UI, it doesn't fit for 3D space. We need to multiply by negative value to correct
    // this. To prevent loss of precision when the buffer is being filled, multiplication by 100 is used.
    float4x4       modelM = f4x4Mul(f4x4Scale(f3Make(-0.01f, -0.01f, 0)), f4x4Translation(f3Make(-width / 2.0f, 0, 0)));
    float4x4       mat = f4x4Mul(*pMatWorld, modelM);
    TFCameraMatrix mvp = camMatMulMat4(pMatProjView, &mat);
    for (uint viewIndex = 0; viewIndex < VR_MULTIVIEW_COUNT; ++viewIndex)
    {
        uniformBlockData.mMVP[viewIndex] = mvp.mMatrices[viewIndex];
    }
    memcpy(updateDesc.pMappedData, &uniformBlockData, sizeof(FontUniformData));
    endUpdateResource(&updateDesc);

    if (gFontSystem.mPerDrawSetIndex >= gFontSystem.gMaxPerDrawSets)
    {
        gFontSystem.mPerDrawSetIndex = 0;
    }

    TFDescriptorDataRange range = { (uint32_t)uniformBlock.mOffset, sizeof(FontUniformData) };
    TFDescriptorData      params[2] = {};
    params[0].mIndex = SRT_RES_IDX(FontSrtData, PerDraw, gUniformData);
    params[0].ppBuffers = &uniformBlock.pBuffer;
    params[0].pRanges = &range;
    params[1].mIndex = SRT_RES_IDX(FontSrtData, PerDraw, gAtlas);
    params[1].ppTextures = &pFont->pAtlas;

    updateDescriptorSet(gFontSystem.pRenderer, gFontSystem.mPerDrawSetIndex, gFontSystem.pDescriptorSet, 2, params);

    cmdBindPipeline(pCmd, gFontSystem.pPipelines[1]);
    cmdBindDescriptorSet(pCmd, gFontSystem.mPerDrawSetIndex, gFontSystem.pDescriptorSet);

    gFontSystem.mPerDrawSetIndex++;

    const uint32_t stride = sizeof(FontVertex);
    const uint64_t offset = 0;
    cmdBindVertexBuffer(pCmd, 1, &gFontSystem.mMeshRingBuffer[frameIdx].pBuffer, &stride, &offset);
    cmdDraw(pCmd, vertexCount, firstVertex);
#else
    UNREF_PARAM(pCmd);
    UNREF_PARAM(pMatWorld);
    UNREF_PARAM(pMatProjView);
    UNREF_PARAM(pDrawDesc);
#endif
}
