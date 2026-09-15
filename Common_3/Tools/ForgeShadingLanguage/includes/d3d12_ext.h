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

#if FT_RAYTRACING && !defined(TARGET_SCARLETT) && !defined(TARGET_XBOX)

// Ray query interface

#define RayTraversalFlags uint

#define RAY_TRAVERSAL_FLAG_NONE                       0
#define RAY_TRAVERSAL_FLAG_NO_AABB_GEOMETRY           0
#define RAY_TRAVERSAL_FLAG_NO_AABB_INSTANCE           0
#define RAY_TRAVERSAL_FLAG_USE_SHARED_STACK           0
#define RAY_TRAVERSAL_FLAG_USE_SHARED_STACK_TOP_LEVEL 0
#define RAY_TRAVERSAL_FLAG_IGNORE_INSTANCE_MASKING    0
#define RAY_TRAVERSAL_FLAG_IGNORE_INSTANCE_CULLING    0
#define RAY_TRAVERSAL_FLAG_IGNORE_INSTANCE_OPACITY    0
#define RAY_TRAVERSAL_FLAG_IGNORE_INSTANCE_FLAGS      0
#define RAY_TRAVERSAL_FLAG_AUTO_LDS_SIZE              0
#define RAY_TRAVERSAL_FLAG_DEFAULT                    0

#define RAY_FLAG_SKIP_AABB RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES

#define RayQueryClosestHit(tlas, traversalFlags, rayFlags, ray, mask, hit)   \
RayQuery<rayFlags> hit;                                                      \
hit.TraceRayInline( tlas, 0, mask, ray);                                     \
bool hit##HasHitCandidates = hit.Proceed();                                    

#define RayQueryAnyHit(tlas, traversalFlags, rayFlags, ray, mask, hit)       \
RayQuery<rayFlags | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> hit;           \
hit.TraceRayInline( tlas, 0, mask, ray);                                     \
bool hit##HasHitCandidates = hit.Proceed();                                    

#define RayQueryBeginForEachCandidate(hit) while (hit##HasHitCandidates)
#define RayQueryEndForEachCandidate(hit)
#define RayQueryIsHit(hit) (hit.CommittedStatus() != COMMITTED_NOTHING)
#define RayQueryIsHitTriangle(hit) (hit.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
#define RayQueryIsHitNonOpaqueTriangle(hit) (hit.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE)
#define RayQueryCommitCandidate(hit) hit.CommitNonOpaqueTriangleHit();
#define RayQueryProceed(hit) hit##HasHitCandidates = hit.Proceed();

#define RayQueryBarycentrics(hit) (hit.CommittedTriangleBarycentrics())
#define RayQueryPrimitiveIndex(hit) (hit.CommittedPrimitiveIndex())
#define RayQueryInstanceID(hit) (hit.CommittedInstanceID())
#define RayQueryGeometryIndex(hit) (hit.CommittedGeometryIndex())
#define RayQueryInstanceIndex(hit) (hit.CommittedInstanceIndex())
#define RayQueryRayT(hit) (hit.CommittedRayT())

#define RayQueryCandidateBarycentrics(hit) (hit.CandidateTriangleBarycentrics())
#define RayQueryCandidatePrimitiveIndex(hit) (hit.CandidatePrimitiveIndex())
#define RayQueryCandidateInstanceID(hit) (hit.CandidateInstanceID())
#define RayQueryCandidateGeometryIndex(hit) (hit.CandidateGeometryIndex())

#endif

#if FT_MULTIVIEW
#if defined(STAGE_VERT)
    #define VR_VIEW_ID viewID
#else
    #define VR_VIEW_ID(VID) VID
#endif
    #define VR_MULTIVIEW_COUNT 2
#else
    #if defined(STAGE_VERT)
        #define VR_VIEW_ID 0
    #else
        #define VR_VIEW_ID(VID) (0)
    #endif
    #define VR_MULTIVIEW_COUNT 1
#endif

#if FT_COOP_VECTORS && !defined(TARGET_SCARLETT) && !defined(TARGET_XBOX)
#include "../../../Graphics/ThirdParty/OpenSource/DirectXShaderCompiler/inc/hlsl/dx/linalg.h"
// Data Types
#define COOP_DT_SINT16           dx::linalg::DATA_TYPE_SINT16
#define COOP_DT_UINT16           dx::linalg::DATA_TYPE_UINT16
#define COOP_DT_SINT32           dx::linalg::DATA_TYPE_SINT32
#define COOP_DT_UINT32           dx::linalg::DATA_TYPE_UINT32
#define COOP_DT_FLOAT16          dx::linalg::DATA_TYPE_FLOAT16
#define COOP_DT_FLOAT32          dx::linalg::DATA_TYPE_FLOAT32
#define COOP_DT_SINT8_T4_PACKED  dx::linalg::DATA_TYPE_SINT8_T4_PACKED
#define COOP_DT_UINT8_T4_PACKED  dx::linalg::DATA_TYPE_UINT8_T4_PACKED
#define COOP_DT_UINT8            dx::linalg::DATA_TYPE_UINT8
#define COOP_DT_SINT8            dx::linalg::DATA_TYPE_SINT8
#define COOP_DT_FLOAT8_E4M3      dx::linalg::DATA_TYPE_FLOAT8_E4M3
#define COOP_DT_FLOAT8_E5M2      dx::linalg::DATA_TYPE_FLOAT8_E5M2

// Matrix Layouts
#define COOP_LAYOUT_ROW_MAJOR             dx::linalg::MATRIX_LAYOUT_ROW_MAJOR
#define COOP_LAYOUT_COLUMN_MAJOR          dx::linalg::MATRIX_LAYOUT_COLUMN_MAJOR
#define COOP_LAYOUT_MUL_OPTIMAL           dx::linalg::MATRIX_LAYOUT_MUL_OPTIMAL
#define COOP_LAYOUT_OUTER_PRODUCT_OPTIMAL dx::linalg::MATRIX_LAYOUT_OUTER_PRODUCT_OPTIMAL

// Types
#define MatrixRef(coopDT, dimM, dimK, layout) \
    dx::linalg::MatrixRef<coopDT, dimM, dimK, layout>

#define RWMatrixRef(coopDT, dimM, dimK, layout) \
    dx::linalg::RWMatrixRef<coopDT, dimM, dimK, layout>

#define MatrixRefTranspose(coopDT, dimM, dimK, layout) \
    dx::linalg::MatrixRef<coopDT, dimM, dimK, layout, true>

#define RWMatrixRefTranspose(coopDT, dimM, dimK, layout) \
    dx::linalg::RWMatrixRef<coopDT, dimM, dimK, layout, true>

#define VectorRef(coopDT) \
    dx::linalg::VectorRef<coopDT>

#define RWVectorRef(coopDT) \
    dx::linalg::RWVectorRef<coopDT>

#define CoopInterpretedVector(elemType, count, coopDT) \
    dx::linalg::InterpretedVector<elemType, count, coopDT>


// Construct interpreted vector
#define CoopVecMake(vec, elemType, count, coopDT) \
    dx::linalg::MakeInterpretedVector<coopDT, elemType, count>(vec)

// Matrix * Vector multiply
#define CoopVecMul(outType, inType, inCount, matBufType, coopDT, matDT, dimM, dimK, layout, transpose, matrix, vector) \
    dx::linalg::Mul<outType, inType, inCount, matBufType, coopDT, matDT, dimM, dimK, layout, transpose>(matrix, vector)

#define CoopVecMulSimple(outType, matrixRef, interpVec) \
    dx::linalg::Mul< \
        outType \
    >(matrixRef, interpVec)

// Matrix * Vector + Bias
#define CoopVecMulAdd(outType, inType, inCount, matBufType, coopDT, matDT, dimM, dimK, layout, transpose, biasBufType, biasDT, matrix, vector, bias) \
    dx::linalg::MulAdd<outType, inType, inCount, matBufType, coopDT, matDT, dimM, dimK, layout, transpose, biasBufType, biasDT>(matrix, vector, bias)

#define CoopVecMulAddSimple(outType, matrixRef, interpVec, biasRef) \
    dx::linalg::MulAdd< \
        outType \
    >(matrixRef, interpVec, biasRef)

// Outer product accumulate
#define CoopVecOuterProductAccumulate(elemType, dimM, dimN, coopDT, layout, vec1, vec2, matrixRef) \
    dx::linalg::OuterProductAccumulate<elemType, dimM, dimN, coopDT, layout>(vec1, vec2, matrixRef)

#define CoopVecOuterProductAccumulateSimple(vec1, vec2, matrixRef) \
    dx::linalg::OuterProductAccumulate(vec1, vec2, matrixRef)

// Vector accumulate
#define CoopVecAccumulate(vec, buffer, offset) \
    dx::linalg::VectorAccumulate(vec, buffer, offset)
#endif