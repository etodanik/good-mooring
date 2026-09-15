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

#include "../Interfaces/IGraphicsConfig.h"
#include "../Interfaces/IGraphics.h"
#ifdef DIRECT3D12
#ifdef SCARLETT
#ifndef __ID3D12Device8_INTERFACE_DEFINED__
#define __ID3D12Device8_INTERFACE_DEFINED__
#endif
#ifndef __ID3D12Device10_INTERFACE_DEFINED__
#define __ID3D12Device10_INTERFACE_DEFINED__
#endif
#endif
#include "Direct3D12_Cxx.h"

#define AMD_AGS_HELPER_IMPL
#include "../../../Common_3/Graphics/ThirdParty/OpenSource/ags/AgsHelper.h"

#include "../../../Common_3/Graphics/ThirdParty/OpenSource/DirectXShaderCompiler/inc/dxcapi.h"

#define D3D12MA_IMPLEMENTATION
#include "../../Utilities/ThirdParty/OpenSource/Nothings/stb_ds.h"
#include "../../Utilities/ThirdParty/OpenSource/bstrlib/bstrlib.h"
#include "../ThirdParty/OpenSource/D3D12MemoryAllocator/Direct3D12MemoryAllocator.h"

#if defined(FORGE_PROFILE)
#define PROFILE_BUILD // This turns on USE_PIX so that we can set markers even in release build
#endif
#ifndef HOLOLENS2
#if defined(XBOX)
#include <pix3.h>
#else
#include "../../../Common_3/Graphics/ThirdParty/OpenSource/winpixeventruntime/Include/WinPixEventRuntime/pix3.h"
#endif
#endif
#if defined(FORGE_PROFILE)
#undef PROFILE_BUILD
#endif

#ifdef XBOX
// IID_ID3D12CommandSignature etc... are mising from the Xbox 2023, 230300 GDK
// The standard approach from the GDK samples is to use IID_PPV_ARGS but this macro is not supported in C
// we forward declare the symbols in Direct3D12Config.h and define them in a cpp file
const GUID IID_ID3D12CommandSignature_Copy = __uuidof(ID3D12CommandSignature);
const GUID IID_ID3D12QueryHeap_Copy = __uuidof(ID3D12QueryHeap);
#if defined(D3D12_RAYTRACING_AVAILABLE)
const GUID IID_ID3D12Device5_Copy = __uuidof(ID3D12Device5);
const GUID IID_ID3D12GraphicsCommandList4_Copy = __uuidof(ID3D12GraphicsCommandList4);
#endif
// Xbox one specific, missing in SDK
#ifndef DISABLE_PIPELINE_LIBRARY
const GUID IID_ID3D12PipelineLibrary_Copy = __uuidof(ID3D12PipelineLibrary);
const GUID IID_ID3D12Device1_Copy = __uuidof(ID3D12Device1);
#endif
#else
const GUID IID_ID3D12CommandSignature_Copy = IID_ID3D12CommandSignature;
const GUID IID_ID3D12QueryHeap_Copy = IID_ID3D12QueryHeap;
#if defined(D3D12_RAYTRACING_AVAILABLE)
const GUID IID_ID3D12Device5_Copy = IID_ID3D12Device5;
const GUID IID_ID3D12GraphicsCommandList4_Copy = IID_ID3D12GraphicsCommandList4;
#endif
#ifndef DISABLE_PIPELINE_LIBRARY
const GUID IID_ID3D12PipelineLibrary_Copy = IID_ID3D12PipelineLibrary;
const GUID IID_ID3D12Device1_Copy = IID_ID3D12Device1;
#endif
const GUID IID_ID3D12Device10_Copy = IID_ID3D12Device10;
#endif

extern "C" void PIX_BeginEvent(ID3D12GraphicsCommandList1* context, float r, float g, float b, const char* pName)
{
    // note: USE_PIX isn't the ideal test because we might be doing a debug build where pix
    // is not installed, or a variety of other reasons. It should be a separate #ifdef flag?
#ifdef USE_PIX
    // color is in B8G8R8X8 format where X is padding
    PIXBeginEvent(context, PIX_COLOR((BYTE)(r * 255), (BYTE)(g * 255), (BYTE)(b * 255)), pName);
#else
    UNREF_PARAM(context);
    UNREF_PARAM(r);
    UNREF_PARAM(g);
    UNREF_PARAM(b);
    UNREF_PARAM(pName);
#endif
}

extern "C" void PIX_EndEvent(ID3D12GraphicsCommandList1* context)
{
#ifdef USE_PIX
    PIXEndEvent(context);
#else
    UNREF_PARAM(context);
#endif
}

extern "C" void PIX_SetMarker(ID3D12GraphicsCommandList1* context, float r, float g, float b, const char* pName)
{
#ifdef USE_PIX
    // color is in B8G8R8X8 format where X is padding
    PIXSetMarker(context, PIX_COLOR((BYTE)(r * 255), (BYTE)(g * 255), (BYTE)(b * 255)), pName);
#else
    UNREF_PARAM(context);
    UNREF_PARAM(r);
    UNREF_PARAM(g);
    UNREF_PARAM(b);
    UNREF_PARAM(pName);
#endif
}

extern "C" HRESULT IDxcBlobEncoding_QueryInterface(struct IDxcBlobEncoding* pEncoding, struct IDxcBlobUtf8** pOut)
{
    return pEncoding->QueryInterface(pOut);
}

extern "C" void IDxcBlobEncoding_Release(struct IDxcBlobEncoding* pEncoding) { pEncoding->Release(); }

extern "C" LPVOID IDxcBlobEncoding_GetBufferPointer(struct IDxcBlobEncoding* pEncoding) { return pEncoding->GetBufferPointer(); }

extern "C" SIZE_T IDxcBlobEncoding_GetBufferSize(struct IDxcBlobEncoding* pEncoding) { return pEncoding->GetBufferSize(); }

extern "C" LPCSTR IDxcBlobUtf8_GetStringPointer(struct IDxcBlobUtf8* pBlob) { return pBlob->GetStringPointer(); }

extern "C" SIZE_T IDxcBlobUtf8_GetStringLength(struct IDxcBlobUtf8* pBlob) { return pBlob->GetStringLength(); }

extern "C" HRESULT IDxcUtils_CreateBlob(void* pByteCode, uint32_t byteCodeSize, struct IDxcBlobEncoding** ppEncoding)
{
    IDxcUtils* pUtils;
    HRESULT    res = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&pUtils));
    if (!SUCCEEDED(res))
    {
        return res;
    }

    res = pUtils->CreateBlob(pByteCode, byteCodeSize, DXC_CP_ACP, ppEncoding); //-V522
    if (!SUCCEEDED(res))
    {
        pUtils->Release();
        return res;
    }

    pUtils->Release();
    return 0;
}

extern "C" HRESULT D3D12MA_CreateAllocator(ID3D12Device* pDevice, D3D12MA_IDXGIAdapter* pGpu, struct D3D12MAAllocator_** ppOut)
{
    D3D12MA::ALLOCATOR_DESC desc = {};
    desc.Flags = D3D12MA::ALLOCATOR_FLAG_NONE;
    desc.pDevice = pDevice;
    desc.pAdapter = pGpu;

    D3D12MA::ALLOCATION_CALLBACKS allocationCallbacks = {};
    allocationCallbacks.pAllocate = [](size_t size, size_t alignment, void*) { return tf_memalign(alignment, size); };
    allocationCallbacks.pFree = [](void* ptr, void*) { tf_free(ptr); };
    desc.pAllocationCallbacks = &allocationCallbacks;
    return D3D12MA::CreateAllocator(&desc, (D3D12MA::Allocator**)ppOut);
}

extern "C" void D3D12MA_ReleaseAllocator(struct D3D12MAAllocator_* pAllocator)
{
    if (pAllocator)
    {
        ((D3D12MA::Allocator*)pAllocator)->Release();
    }
}

extern "C" HRESULT D3D12MA_CreateResource(struct D3D12MAAllocator_* pAllocator, const D3D12MA_ALLOCATION_DESC* pDesc,
                                          const D3D12_RESOURCE_DESC* pResDesc, struct D3D12MAAllocation_** ppAlloc,
                                          ID3D12Resource** ppResource)
{
    D3D12MA::ALLOCATION_DESC alloc_desc = {};
    alloc_desc.HeapType = pDesc->HeapType;
    alloc_desc.ExtraHeapFlags = pDesc->ExtraHeapFlags;
    if (pDesc->mUseDedicatedAllocation)
    {
        alloc_desc.Flags |= D3D12MA::ALLOCATION_FLAG_COMMITTED;
    }
    return ((D3D12MA::Allocator*)pAllocator)
        ->CreateResource(&alloc_desc, pResDesc, pDesc->ResourceStates, pDesc->pOptimizedClearValue, (D3D12MA::Allocation**)ppAlloc,
                         IID_ID3D12Resource, (void**)ppResource);
}

#if defined(__ID3D12Device10_INTERFACE_DEFINED__) && !defined(XBOXONE)
extern "C" HRESULT D3D12MA_CreateResource3(struct D3D12MAAllocator_* pAllocator, const D3D12MA_ALLOCATION_DESC3* pDesc,
                                           const D3D12_RESOURCE_DESC1* pResDesc, struct D3D12MAAllocation_** ppAlloc,
                                           ID3D12Resource** ppResource)
{
    D3D12MA::ALLOCATION_DESC alloc_desc = {};
    alloc_desc.HeapType = pDesc->HeapType;
    alloc_desc.ExtraHeapFlags = pDesc->ExtraHeapFlags;
    if (pDesc->mUseDedicatedAllocation)
    {
        alloc_desc.Flags |= D3D12MA::ALLOCATION_FLAG_COMMITTED;
    }
    return ((D3D12MA::Allocator*)pAllocator)
        ->CreateResource3(&alloc_desc, pResDesc, pDesc->InitialLayout, pDesc->pOptimizedClearValue, pDesc->NumCastableFormats,
                          pDesc->pCastableFormats, (D3D12MA::Allocation**)ppAlloc, IID_ID3D12Resource, (void**)ppResource);
}
#endif

extern "C" void D3D12MA_ReleaseAllocation(struct D3D12MAAllocation_* pAlloc)
{
    if (pAlloc)
    {
        ((D3D12MA::Allocation*)pAlloc)->Release();
    }
}

extern "C" void D3D12MA_CalculateMemoryUse(struct D3D12MAAllocator_* pAllocator, uint64_t* usedBytes, uint64_t* totalAllocatedBytes)
{
    D3D12MA::TotalStatistics stats;
    ((D3D12MA::Allocator*)pAllocator)->CalculateStatistics(&stats);
    *usedBytes = stats.Total.Stats.BlockBytes;
    *totalAllocatedBytes = stats.Total.Stats.AllocationBytes;
}

extern "C" void D3D12MA_BuildStatsString(struct D3D12MAAllocator_* pAllocator, BOOL detailedMap)
{
    WCHAR* pStatsString = NULL;
    ((D3D12MA::Allocator*)pAllocator)->BuildStatsString(&pStatsString, detailedMap);
    if (pStatsString)
    {
        LOGF(eINFO, "%ls", pStatsString);
        ((D3D12MA::Allocator*)pAllocator)->FreeStatsString(pStatsString);
    }
}

extern "C" void setDefaultGPUDirect3D12Properties(TFGpuDesc* pGpuDesc)
{
    pGpuDesc->mUniformBufferAlignment = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
    pGpuDesc->mUploadBufferTextureAlignment = D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT;
    pGpuDesc->mUploadBufferTextureRowAlignment = D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;
}
#endif
