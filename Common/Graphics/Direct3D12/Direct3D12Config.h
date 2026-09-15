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
#error "Direct3D12Config should be included from RendererConfig only"
#endif

#define DIRECT3D12

#ifdef XBOX
#define HMONITOR void*
#endif
#if defined(SCARLETT) || defined(HOLOLENS2)
// ID3D12PipelineLibrary is not yet implemented in d3d12_xs
#define DISABLE_PIPELINE_LIBRARY
#endif
#pragma warning(disable : 4115) // struct forward declaration inside of function parameter

#if !defined(XBOX) || !defined(__cplusplus)

// Adjust the D3D12_AGILITY_SDK_VERSION in TF_Shared.props to the current preview one to test preview features (the current preview SDK 717)
// Similar to `d3d12.h`, `dxgiX_X.h` also have mismatching `__declspec(dllimport)` between PC/Xbox
#if !defined(USE_PREVIEW_AGILITY)
#include "../../../Common/Graphics/ThirdParty/OpenSource/Direct3d12Agility/include/dxgi1_6.h"
#else
#include "../../../Common/Graphics/ThirdParty/OpenSource/Direct3d12AgilityPreview/include/dxgi1_6.h"
#endif

#include <dxgidebug.h>

// `d3d12.h` requires `_WIN32` to NOT be defined in order to use the correct Xbox COM definitions
#if defined(XBOX) && defined(_WIN32)
#define FORGE_WIN32_DEFINED
#undef _WIN32
#endif

#if !defined(_WIN32)
#define D3D12_GET_DESC(Resource, OutDescPtr) \
    COM_CALL(GetDesc, Resource, OutDescPtr) //(*(OutDescPtr) = (Resource)->lpVtbl->GetDesc(Resource))
#else
#define D3D12_GET_DESC(Resource, OutDescPtr) COM_CALL(GetDesc, Resource, OutDescPtr)
#endif

#if !defined(USE_PREVIEW_AGILITY)
#include "../../../Common/Graphics/ThirdParty/OpenSource/Direct3d12Agility/include/d3d12.h"
#else
#include "../../../Common/Graphics/ThirdParty/OpenSource/Direct3d12AgilityPreview/include/d3d12.h"
#endif

// Restore _WIN32 definition
#if defined(XBOX) && defined(FORGE_WIN32_DEFINED)
#define _WIN32 1
#undef FORGE_WIN32_DEFINED
#endif

#endif

#ifdef XBOX
#include "../../../Xbox/Common/Graphics/Direct3D12/Direct3D12X.h"
#endif

//////////////////////////////////////////////
//// Availability macros
//// Do not modify
//////////////////////////////////////////////

#if defined(D3D12_RAYTRACING_AABB_BYTE_ALIGNMENT) && (defined(SCARLETT) || (!defined(XBOX) && !defined(HOLOLENS2)))
#define D3D12_RAYTRACING_AVAILABLE
#endif

#if (D3D12_PREVIEW_SDK_VERSION == 717) && !defined(XBOX)
#define D3D12_COOP_VECTORS_AVAILABLE
#endif

#if defined(D3D12_WORK_GRAPHS_BACKING_MEMORY_ALIGNMENT_IN_BYTES) && !defined(XBOX)
#define D3D12_WORKGRAPH_AVAILABLE
#endif

#ifdef __cplusplus
#define IID_REF(Type)                                IID_##Type

#define IID_ARGS(Type, Ptr)                          __uuidof(Type), (void**)(Ptr)

#define COM_CALL(METHOD, CALLER, ...)                (CALLER)->METHOD(__VA_ARGS__)

#define COM_CALL_RETURN(METHOD, CALLER, dst, ...)    dst = (CALLER)->METHOD(##__VA_ARGS__)

#define COM_CALL_RETURN_NO_ARGS(METHOD, CALLER, dst) COM_CALL_RETURN(METHOD, CALLER, dst)

#define COM_RELEASE(ptr)            \
    if (ptr && ptr->Release() == 0) \
    ptr = NULL
#else
#define IID_REF(Type)                 &IID_##Type

#define IID_ARGS(Type, Ptr)           &IID_##Type, (void**)(Ptr)

#define COM_CALL(METHOD, CALLER, ...) (CALLER)->lpVtbl->METHOD(CALLER, ##__VA_ARGS__)

#ifdef XBOX
#define COM_CALL_RETURN(METHOD, CALLER, dst, ...)           dst = (CALLER)->lpVtbl->METHOD(CALLER, ##__VA_ARGS__)

#define COM_CALL_RETURN_DST_FIRST(METHOD, CALLER, dst, ...) dst = (CALLER)->lpVtbl->METHOD(CALLER, __VA_ARGS__)

#define COM_CALL_RETURN_NO_ARGS(METHOD, CALLER, dst)        dst = (CALLER)->lpVtbl->METHOD(CALLER)
#else
#define COM_CALL_RETURN(METHOD, CALLER, dst, ...)           (CALLER)->lpVtbl->METHOD(CALLER, __VA_ARGS__, &dst)

#define COM_CALL_RETURN_DST_FIRST(METHOD, CALLER, dst, ...) (CALLER)->lpVtbl->METHOD(CALLER, &dst, __VA_ARGS__)

#define COM_CALL_RETURN_NO_ARGS(METHOD, CALLER, dst)        (CALLER)->lpVtbl->METHOD(CALLER, &dst)
#endif

#define COM_RELEASE(ptr)                       \
    if (ptr && ptr->lpVtbl->Release(ptr) == 0) \
    ptr = NULL
#endif

// IID_ID3D12CommandSignature etc... are mising from the Xbox 2023, 230300 GDK
// The standard approach from the GDK samples is to use IID_PPV_ARGS but this macro is not supported in C
// we forward declare the symbols here and define them in Direct3D12_Cxx.cpp
#ifdef __cplusplus
extern "C"
{
#endif
extern const GUID IID_ID3D12CommandSignature_Copy;
extern const GUID IID_ID3D12QueryHeap_Copy;
#if defined(D3D12_RAYTRACING_AVAILABLE)
extern const GUID IID_ID3D12Device5_Copy;
extern const GUID IID_ID3D12GraphicsCommandList4_Copy;
#endif
// Xbox one specific, missing in SDK
#ifndef DISABLE_PIPELINE_LIBRARY
extern const GUID IID_ID3D12PipelineLibrary_Copy;
extern const GUID IID_ID3D12Device1_Copy;
#endif
#if !defined(XBOX)
extern const GUID IID_ID3D12Device10_Copy;
#endif
#ifdef __cplusplus
}
#endif
