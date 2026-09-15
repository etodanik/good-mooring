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

#ifndef msdf_font_srt_h
#define msdf_font_srt_h

#ifndef VR_MULTIVIEW_COUNT
#if defined(TARGET_QUEST) || defined(HOLOLENS2)
#define VR_MULTIVIEW_COUNT 2
#else
#define VR_MULTIVIEW_COUNT 1
#endif
#endif

STRUCT(FontUniformData)
{
    DATA(float4x4, mMVP[VR_MULTIVIEW_COUNT], None);
};

BEGIN_SRT(FontSrtData)
    BEGIN_SRT_SET(PerDraw)
        DECL_CBUFFER(PerDraw, CBUFFER(FontUniformData), gUniformData)
        DECL_TEXTURE(PerDraw, Tex2D(float4), gAtlas)
    END_SRT_SET(PerDraw)
END_SRT(FontSrtData)

#endif
