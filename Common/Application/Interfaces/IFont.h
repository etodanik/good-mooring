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

#ifndef I_FONT_H
#define I_FONT_H

#include "../../Application/Config.h"
#include "../../Application/Interfaces/ICamera.h"

#include "../../Utilities/Interfaces/IMath.h"

typedef struct TFRenderer      TFRenderer;
typedef struct TFCmd           TFCmd;
typedef struct TFRenderTarget  TFRenderTarget;
typedef struct TFPipelineCache TFPipelineCache;
typedef struct TFTexture       TFTexture;
typedef uint32_t               TFCodepoint;

typedef struct TFFont TFFont;

typedef struct TFMSDFGlyphInfo
{
    float4   mBound;
    float4   mTexcoord;
    float4   mOverlapedBound;
    float4   mOverlapedTexcoord;
    int32_t  mAdvance;
    int32_t  mLeftBearing;
    uint32_t mCodepoint;
} TFMSDFGlyphInfo;

typedef struct TFMSDFFontInfo
{
    float    mPxRange;
    uint32_t mGlyphResolution;
    uint32_t mSTBFontOffset;
    uint32_t mSTBFontSize;

    uint32_t mMSDFGlyphOffset;
    uint32_t mMSDFGlyphStride;
    uint32_t mMSDFGlyphCount;
} TFMSDFFontInfo;

// The MSDF converter measures signed distance in font units divided by 64.
#define TF_MSDF_FONT_UNITS_PER_DISTANCE_UNIT 64.0f

typedef struct TFFontSystemDesc
{
    TFRenderer* pRenderer = NULL;
    uint32_t    mBufferSizeBytes = 1024 * 1024;

    uint32_t        mFrameMaxCount = 2u;
    const uint32_t* pFrameIdx = NULL;
} TFFontSystemDesc;

typedef struct TFFontSystemLoadDesc
{
    TFPipelineCache* pCache;
    uint32_t         mColorFormat; // enum TinyImageFormat
    uint32_t         mDepthFormat; // enum TinyImageFormat
    uint32_t         mWidth;
    uint32_t         mHeight;
    uint32_t         mCullMode;         // enum TFCullMode
    uint32_t         mDepthCompareMode; // enum TFCompareMode
} TFFontSystemLoadDesc;

enum TFFontFlags : uint32_t
{
    TF_FONT_NONE = 0,
    TF_FONT_ASCII = 1,
    TF_FONT_ATLAS_AUTO_RESOLUTION_ON_INIT = 2,
};

typedef struct TFFontDesc
{
    const char* pFontName = "default";
    const char* pFontPath = NULL;

    int32_t  mAtlasResolution = -1;
    float    mScaleMultiplier = 0.65f;
    uint32_t mFlags = TF_FONT_NONE;

    uint32_t        mGlyphCount = 0;
    const uint32_t* pGlyphsToLoad = NULL;
} TFFontDesc;

typedef struct TFFontDrawDesc
{
    TFFont*     pFont = NULL;
    const char* pText = NULL;

    // Provided color should be A8B8G8R8_SRGB
    uint32_t mFontColor = 0xffffffff;
    float    mFontSize = 16.0f;
    float    mFontSpacing = 0.0f;
} TFFontDrawDesc;

typedef struct TFFontGlyphQueryDesc
{
    float       mFontSize = 16.0f;
    float       mFontSpacing = 0.0f;
    TFCodepoint mCodepoint = 0;
    TFCodepoint mAdditionalCodepoint = 0;
    bool        mAdditionalCodepointIsNext = false;
    bool        mOnlyXAdvance = false;
} TFFontGlyphQueryDesc;

typedef struct TFFontGlyphData
{
    float4 mBound;
    float4 mTexcoord;
    float  mRatio;
    float  mXAdvance;
} TFFontGlyphData;

FORGE_API void initFontSystem(TFFontSystemDesc* pDesc);

FORGE_API void exitFontSystem();

FORGE_API void loadFontSystem(const TFFontSystemLoadDesc* pDesc);

FORGE_API void unloadFontSystem();

FORGE_API TFFont* addFont(TFFontDesc* pFontDesc);

FORGE_API void removeFont(TFFont* pFont);

FORGE_API uint32_t fntGetRecomendedAtlasResolution(TFFont* pFont);

FORGE_API bool fntIsNeedUpdateAtlas(TFFont* pFont);

// if newResolution == -1: use optimal or keep current, return true if there is enough space to place all glyphs
FORGE_API bool fntUpdateAtlas(TFFont* pFont, int32_t newResolution);

FORGE_API const char* fntConsumeSymbol(const char* pSymbol, TFCodepoint* pCodepoint);

FORGE_API TFFontGlyphData fntQueryGlyph(TFFont* pFont, const TFFontGlyphQueryDesc* pQueryDesc);

FORGE_API float2 fntMeasureFontText(TFFont* pFont, const char* pText, uint32_t textLength, float fontSize);

FORGE_API TFTexture* fntGetAtlas(TFFont* pFont);

FORGE_API float fntGetRatio(TFFont* pFont, float fontSize);

FORGE_API void cmdDrawText(TFCmd* pCmd, float2 screenPos, const TFFontDrawDesc* pDrawDesc);

FORGE_API void cmdDrawWorldSpaceText(TFCmd* pCmd, const mat4* pMatWorld, const TFCameraMatrix* pMatProjView,
                                     const TFFontDrawDesc* pDrawDesc);
#endif
