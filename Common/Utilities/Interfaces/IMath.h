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

#include "../../Application/Config.h"
#include "ILog.h"                                                // ASSERT
#include "../ThirdParty/OpenSource/murmurhash3/MurmurHash3_32.h" // MurmurHash3_x86_32

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4244) // conversion from 'double' to 'float', possible loss of data
#pragma warning(disable : 4305) // 'initializing': truncation from 'double' to 'float'
#elif defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdouble-promotion"
#pragma clang diagnostic ignored "-Wconversion"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdouble-promotion"
#pragma GCC diagnostic ignored "-Wconversion"
#endif

static inline float maxf(float a, float b) { return a > b ? a : b; }
static inline float minf(float a, float b) { return a < b ? a : b; }
static inline float clampf(float x, float a, float b) { return minf(maxf(x, a), b); }

static inline uint64_t maxu(uint64_t a, uint64_t b) { return a > b ? a : b; }
static inline uint64_t minu(uint64_t a, uint64_t b) { return a < b ? a : b; }
static inline uint64_t clampu(uint64_t x, uint64_t a, uint64_t b) { return minu(maxu(x, a), b); }

static inline int64_t maxi(int64_t a, int64_t b) { return a > b ? a : b; }
static inline int64_t mini(int64_t a, int64_t b) { return a < b ? a : b; }
static inline int64_t clampi(int64_t x, int64_t a, int64_t b) { return mini(maxi(x, a), b); }

#ifdef max
#undef max
#endif
#ifdef min
#undef min
#endif

#include "../Math/MathDefs.h"

#ifndef __cplusplus

inline double max(double a, double b) { return a > b ? a : b; }
inline double min(double a, double b) { return a < b ? a : b; }
inline double clamp(double x, double a, double b) { return min(max(x, a), b); }

#else // !__cplusplus

template<typename T, typename A>
inline constexpr T min(const T& a, const A& b)
{
    return (a < (T)b) ? a : (T)b;
}

template<typename T, typename A>
inline constexpr T max(const T& a, const A& b)
{
    return (a > (T)b) ? a : (T)b;
}

inline float4 max(const float4& a, const float4& b)
{
    float4 result = {};
    result.x = max(a.x, b.x);
    result.y = max(a.y, b.y);
    result.z = max(a.z, b.z);
    result.w = max(a.w, b.w);
    return result;
}

inline float4 min(const float4& a, const float4& b)
{
    float4 result = {};
    result.x = min(a.x, b.x);
    result.y = min(a.y, b.y);
    result.z = min(a.z, b.z);
    result.w = min(a.w, b.w);
    return result;
}

template<typename X, typename A, typename B>
inline constexpr X clamp(const X& x, const A& a, const B& b)
{
    return min(max(x, (X)a), (X)b);
}

#endif

#ifndef PI
#define PI 3.14159265358979323846f
#endif

/////////
/// Row/elem access

static inline void f4x4SetElem(float4x4* m, int i, int j, float val) { ((float*)&(*m).v[i])[j] = val; }

static inline float f4x4GetElem(float4x4 m, int i, int j) { return ((float*)&m.v[i])[j]; }
static inline void  f3x3SetElem(float3x3* m, int i, int j, float val) { ((float*)&(*m).v[i])[j] = val; }
static inline float f3x3GetElem(float3x3 m, int i, int j) { return ((float*)&m.v[i])[j]; }
static inline void  f2x2SetElem(float2x2* m, int i, int j, float val) { ((float*)&(*m).v[i])[j] = val; }
static inline float f2x2GetElem(float2x2 m, int i, int j) { return ((float*)&m.v[i])[j]; }

static inline float4 f4x4GetCol(float4x4 m, unsigned int i) { return m.v[i]; } //-V::810,813

static inline void f4x4SetCol(float4x4* m, float4 col, unsigned int i) { (*m).v[i] = col; }

static inline float4 f4x4GetRow(float4x4 m, unsigned int i)
{
    float4 ret;
    ret.x = ((float*)&m.v[0])[i];
    ret.y = ((float*)&m.v[1])[i];
    ret.z = ((float*)&m.v[2])[i];
    ret.w = ((float*)&m.v[3])[i];
    return ret;
}

static inline void f4x4SetRow(float4x4* m, float4 row, unsigned int i)
{
    ((float*)&(*m).v[0])[i] = row.x;
    ((float*)&(*m).v[1])[i] = row.y;
    ((float*)&(*m).v[2])[i] = row.z;
    ((float*)&(*m).v[3])[i] = row.w;
}

static inline float3 f4x3GetCol(float4x3 m, unsigned int i) { return m.v[i]; }

static inline void f4x3SetCol(float4x3* m, float3 col, unsigned int i) { (*m).v[i] = col; }

static inline float4 f4x3GetRow(float4x3 m, unsigned int i)
{
    float4 ret;
    ret.x = ((float*)&m.v[0])[i];
    ret.y = ((float*)&m.v[1])[i];
    ret.z = ((float*)&m.v[2])[i];
    ret.w = ((float*)&m.v[3])[i];
    return ret;
}

static inline void f4x3SetRow(float4x3* m, float4 row, unsigned int i)
{
    ((float*)&(*m).v[0])[i] = row.x;
    ((float*)&(*m).v[1])[i] = row.y;
    ((float*)&(*m).v[2])[i] = row.z;
    ((float*)&(*m).v[3])[i] = row.w;
}

static inline float2 f4x2GetCol(float4x2 m, unsigned int i) { return m.v[i]; }

static inline void f4x2SetCol(float4x2* m, float2 col, unsigned int i) { (*m).v[i] = col; }

static inline float4 f4x2GetRow(float4x2 m, unsigned int i)
{
    float4 ret;
    ret.x = ((float*)&m.v[0])[i];
    ret.y = ((float*)&m.v[1])[i];
    ret.z = ((float*)&m.v[2])[i];
    ret.w = ((float*)&m.v[3])[i];
    return ret;
}

static inline void f4x2SetRow(float4x2* m, float4 row, unsigned int i)
{
    ((float*)&(*m).v[0])[i] = row.x;
    ((float*)&(*m).v[1])[i] = row.y;
    ((float*)&(*m).v[2])[i] = row.z;
    ((float*)&(*m).v[3])[i] = row.w;
}

static inline float4 f3x4GetCol(float3x4 m, unsigned int i) { return m.v[i]; }

static inline void f3x4SetCol(float3x4* m, float4 col, unsigned int i) { (*m).v[i] = col; }

static inline float3 f3x4GetRow(float3x4 m, unsigned int i)
{
    float3 ret;
    ret.x = ((float*)&m.v[0])[i];
    ret.y = ((float*)&m.v[1])[i];
    ret.z = ((float*)&m.v[2])[i];
    return ret;
}

static inline void f3x4SetRow(float3x4* m, float3 row, unsigned int i)
{
    ((float*)&(*m).v[0])[i] = row.x;
    ((float*)&(*m).v[1])[i] = row.y;
    ((float*)&(*m).v[2])[i] = row.z;
}

static inline float3 f3x3GetCol(float3x3 m, unsigned int i) { return m.v[i]; }

static inline void f3x3SetCol(float3x3* m, float3 col, unsigned int i) { (*m).v[i] = col; }

static inline float3 f3x3GetRow(float3x3 m, unsigned int i)
{
    float3 ret;
    ret.x = ((float*)&m.v[0])[i];
    ret.y = ((float*)&m.v[1])[i];
    ret.z = ((float*)&m.v[2])[i];
    return ret;
}

static inline void f3x3SetRow(float3x3* m, float3 row, unsigned int i)
{
    ((float*)&(*m).v[0])[i] = row.x;
    ((float*)&(*m).v[1])[i] = row.y;
    ((float*)&(*m).v[2])[i] = row.z;
}

static inline float2 f3x2GetCol(float3x2 m, unsigned int i) { return m.v[i]; }

static inline void f3x2SetCol(float3x2* m, float2 col, unsigned int i) { (*m).v[i] = col; }

static inline float3 f3x2GetRow(float3x2 m, unsigned int i)
{
    float3 ret;
    ret.x = ((float*)&m.v[0])[i];
    ret.y = ((float*)&m.v[1])[i];
    ret.z = ((float*)&m.v[2])[i];
    return ret;
}

static inline void f3x2SetRow(float3x2* m, float3 row, unsigned int i)
{
    ((float*)&(*m).v[0])[i] = row.x;
    ((float*)&(*m).v[1])[i] = row.y;
    ((float*)&(*m).v[2])[i] = row.z;
}

static inline float4 f2x4GetCol(float2x4 m, unsigned int i) { return m.v[i]; }

static inline void f2x4SetCol(float2x4* m, float4 col, unsigned int i) { (*m).v[i] = col; }

static inline float2 f2x4GetRow(float2x4 m, unsigned int i)
{
    float2 ret;
    ret.x = ((float*)&m.v[0])[i];
    ret.y = ((float*)&m.v[1])[i];
    return ret;
}

static inline void f2x4SetRow(float2x4* m, float2 row, unsigned int i)
{
    ((float*)&(*m).v[0])[i] = row.x;
    ((float*)&(*m).v[1])[i] = row.y;
}

static inline float3 f2x3GetCol(float2x3 m, unsigned int i) { return m.v[i]; }

static inline void f2x3SetCol(float2x3* m, float3 col, unsigned int i) { (*m).v[i] = col; }

static inline float2 f2x3GetRow(float2x3 m, unsigned int i)
{
    float2 ret;
    ret.x = ((float*)&m.v[0])[i];
    ret.y = ((float*)&m.v[1])[i];
    return ret;
}

static inline void f2x3SetRow(float2x3* m, float2 row, unsigned int i)
{
    ((float*)&(*m).v[0])[i] = row.x;
    ((float*)&(*m).v[1])[i] = row.y;
}

static inline float2 f2x2GetCol(float2x2 m, unsigned int i) { return m.v[i]; }

static inline void f2x2SetCol(float2x2* m, float2 col, unsigned int i) { (*m).v[i] = col; }

static inline float2 f2x2GetRow(float2x2 m, unsigned int i)
{
    float2 ret;
    ret.x = ((float*)&m.v[0])[i];
    ret.y = ((float*)&m.v[1])[i];
    return ret;
}

static inline void f2x2SetRow(float2x2* m, float2 row, unsigned int i)
{
    ((float*)&(*m).v[0])[i] = row.x;
    ((float*)&(*m).v[1])[i] = row.y;
}

static inline void d4x4SetElem(double4x4* m, int i, int j, double val) { ((double*)&(*m).v[i])[j] = val; }

static inline double d4x4GetElem(double4x4 m, int i, int j) { return ((double*)&m.v[i])[j]; }
static inline void   d3x3SetElem(double3x3* m, int i, int j, double val) { ((double*)&(*m).v[i])[j] = val; }
static inline double d3x3GetElem(double3x3 m, int i, int j) { return ((double*)&m.v[i])[j]; }
static inline void   d2x2SetElem(double2x2* m, int i, int j, double val) { ((double*)&(*m).v[i])[j] = val; }
static inline double d2x2GetElem(double2x2 m, int i, int j) { return ((double*)&m.v[i])[j]; }

static inline double4 d4x4GetCol(double4x4 m, unsigned int i) { return m.v[i]; }

static inline void d4x4SetCol(double4x4* m, double4 col, unsigned int i) { (*m).v[i] = col; }

static inline double4 d4x4GetRow(double4x4 m, unsigned int i)
{
    double4 ret;
    ret.x = ((double*)&m.v[0])[i];
    ret.y = ((double*)&m.v[1])[i];
    ret.z = ((double*)&m.v[2])[i];
    ret.w = ((double*)&m.v[3])[i];
    return ret;
}

static inline void d4x4SetRow(double4x4* m, double4 row, unsigned int i)
{
    ((double*)&(*m).v[0])[i] = row.x;
    ((double*)&(*m).v[1])[i] = row.y;
    ((double*)&(*m).v[2])[i] = row.z;
    ((double*)&(*m).v[3])[i] = row.w;
}

static inline double3 d4x3GetCol(double4x3 m, unsigned int i) { return m.v[i]; }

static inline void d4x3SetCol(double4x3* m, double3 col, unsigned int i) { (*m).v[i] = col; }

static inline double4 d4x3GetRow(double4x3 m, unsigned int i)
{
    double4 ret;
    ret.x = ((double*)&m.v[0])[i];
    ret.y = ((double*)&m.v[1])[i];
    ret.z = ((double*)&m.v[2])[i];
    ret.w = ((double*)&m.v[3])[i];
    return ret;
}

static inline void d4x3SetRow(double4x3* m, double4 row, unsigned int i)
{
    ((double*)&(*m).v[0])[i] = row.x;
    ((double*)&(*m).v[1])[i] = row.y;
    ((double*)&(*m).v[2])[i] = row.z;
    ((double*)&(*m).v[3])[i] = row.w;
}

static inline double2 d4x2GetCol(double4x2 m, unsigned int i) { return m.v[i]; }

static inline void d4x2SetCol(double4x2* m, double2 col, unsigned int i) { (*m).v[i] = col; }

static inline double4 d4x2GetRow(double4x2 m, unsigned int i)
{
    double4 ret;
    ret.x = ((double*)&m.v[0])[i];
    ret.y = ((double*)&m.v[1])[i];
    ret.z = ((double*)&m.v[2])[i];
    ret.w = ((double*)&m.v[3])[i];
    return ret;
}

static inline void d4x2SetRow(double4x2* m, double4 row, unsigned int i)
{
    ((double*)&(*m).v[0])[i] = row.x;
    ((double*)&(*m).v[1])[i] = row.y;
    ((double*)&(*m).v[2])[i] = row.z;
    ((double*)&(*m).v[3])[i] = row.w;
}

static inline double4 d3x4GetCol(double3x4 m, unsigned int i) { return m.v[i]; }

static inline void d3x4SetCol(double3x4* m, double4 col, unsigned int i) { (*m).v[i] = col; }

static inline double3 d3x4GetRow(double3x4 m, unsigned int i)
{
    double3 ret;
    ret.x = ((double*)&m.v[0])[i];
    ret.y = ((double*)&m.v[1])[i];
    ret.z = ((double*)&m.v[2])[i];
    return ret;
}

static inline void d3x4SetRow(double3x4* m, double3 row, unsigned int i)
{
    ((double*)&(*m).v[0])[i] = row.x;
    ((double*)&(*m).v[1])[i] = row.y;
    ((double*)&(*m).v[2])[i] = row.z;
}

static inline double3 d3x3GetCol(double3x3 m, unsigned int i) { return m.v[i]; }

static inline void d3x3SetCol(double3x3* m, double3 col, unsigned int i) { (*m).v[i] = col; }

static inline double3 d3x3GetRow(double3x3 m, unsigned int i)
{
    double3 ret;
    ret.x = ((double*)&m.v[0])[i];
    ret.y = ((double*)&m.v[1])[i];
    ret.z = ((double*)&m.v[2])[i];
    return ret;
}

static inline void d3x3SetRow(double3x3* m, double3 row, unsigned int i)
{
    ((double*)&(*m).v[0])[i] = row.x;
    ((double*)&(*m).v[1])[i] = row.y;
    ((double*)&(*m).v[2])[i] = row.z;
}

static inline double2 d3x2GetCol(double3x2 m, unsigned int i) { return m.v[i]; }

static inline void d3x2SetCol(double3x2* m, double2 col, unsigned int i) { (*m).v[i] = col; }

static inline double3 d3x2GetRow(double3x2 m, unsigned int i)
{
    double3 ret;
    ret.x = ((double*)&m.v[0])[i];
    ret.y = ((double*)&m.v[1])[i];
    ret.z = ((double*)&m.v[2])[i];
    return ret;
}

static inline void d3x2SetRow(double3x2* m, double3 row, unsigned int i)
{
    ((double*)&(*m).v[0])[i] = row.x;
    ((double*)&(*m).v[1])[i] = row.y;
    ((double*)&(*m).v[2])[i] = row.z;
}

static inline double4 d2x4GetCol(double2x4 m, unsigned int i) { return m.v[i]; }

static inline void d2x4SetCol(double2x4* m, double4 col, unsigned int i) { (*m).v[i] = col; }

static inline double2 d2x4GetRow(double2x4 m, unsigned int i)
{
    double2 ret;
    ret.x = ((double*)&m.v[0])[i];
    ret.y = ((double*)&m.v[1])[i];
    return ret;
}

static inline void d2x4SetRow(double2x4* m, double2 row, unsigned int i)
{
    ((double*)&(*m).v[0])[i] = row.x;
    ((double*)&(*m).v[1])[i] = row.y;
}

static inline double3 d2x3GetCol(double2x3 m, unsigned int i) { return m.v[i]; }

static inline void d2x3SetCol(double2x3* m, double3 col, unsigned int i) { (*m).v[i] = col; }

static inline double2 d2x3GetRow(double2x3 m, unsigned int i)
{
    double2 ret;
    ret.x = ((double*)&m.v[0])[i];
    ret.y = ((double*)&m.v[1])[i];
    return ret;
}

static inline void d2x3SetRow(double2x3* m, double2 row, unsigned int i)
{
    ((double*)&(*m).v[0])[i] = row.x;
    ((double*)&(*m).v[1])[i] = row.y;
}

static inline double2 d2x2GetCol(double2x2 m, unsigned int i) { return m.v[i]; }

static inline void d2x2SetCol(double2x2* m, double2 col, unsigned int i) { (*m).v[i] = col; }

static inline double2 d2x2GetRow(double2x2 m, unsigned int i)
{
    double2 ret;
    ret.x = ((double*)&m.v[0])[i];
    ret.y = ((double*)&m.v[1])[i];
    return ret;
}

static inline void d2x2SetRow(double2x2* m, double2 row, unsigned int i)
{
    ((double*)&(*m).v[0])[i] = row.x;
    ((double*)&(*m).v[1])[i] = row.y;
}

// Generated from compiling MathLibrary.fsl
#include "../Math/MathLibrary.h"

#ifdef _MSC_VER
#pragma warning(pop)
#elif defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

static inline uint64_t round_up_64(uint64_t value, uint64_t multiple) { return ((value + multiple - 1) / multiple) * multiple; }
static inline uint64_t round_down_64(uint64_t value, uint64_t multiple) { return value - value % multiple; }

#define RCP_EST(_in, _out)                         \
    {                                              \
        const float in = _in;                      \
        const union                                \
        {                                          \
            float f;                               \
            int   i;                               \
        } uf = { in };                             \
        const union                                \
        {                                          \
            int   i;                               \
            float f;                               \
        } ui = { (0x3f800000 * 2) - uf.i };        \
        const float fp = ui.f * (2.f - in * ui.f); \
        _out = fp * (2.f - in * fp);               \
    }

#define RSQRT_EST(_in, _out)                                       \
    {                                                              \
        const float in = _in;                                      \
        union                                                      \
        {                                                          \
            float f;                                               \
            int   i;                                               \
        } uf = { in };                                             \
        union                                                      \
        {                                                          \
            int   i;                                               \
            float f;                                               \
        } ui = { 0x5f3759df - (uf.i / 2) };                        \
        const float fp = ui.f * (1.5f - (in * .5f * ui.f * ui.f)); \
        _out = fp * (1.5f - (in * .5f * fp * fp));                 \
    }

#define RSQRT_EST_NR(_in, _out)                        \
    {                                                  \
        float fp2;                                     \
        RSQRT_EST(_in, fp2);                           \
        _out = fp2 * (1.5f - (_in * .5f * fp2 * fp2)); \
    }

static inline float4 rcpEst(float4 v)
{
    float4 ret;
    float  vX = v.x;
    float  vY = v.y;
    float  vZ = v.z;
    float  vW = v.w;

    float rX = 0.0f;
    float rY = 0.0f;
    float rZ = 0.0f;
    float rW = 0.0f;

    RCP_EST(vX, rX);
    RCP_EST(vY, rY);
    RCP_EST(vZ, rZ);
    RCP_EST(vW, rW);

    ret.x = (rX);
    ret.y = (rY);
    ret.z = (rZ);
    ret.w = (rW);

    return ret;
}

static inline float4 rSqrtEst(float4 v)
{
    float4 ret;
    float  vX = v.x;
    float  vY = v.y;
    float  vZ = v.z;
    float  vW = v.w;

    float rX = 0.0f;
    float rY = 0.0f;
    float rZ = 0.0f;
    float rW = 0.0f;

    RSQRT_EST(vX, rX);
    RSQRT_EST(vY, rY);
    RSQRT_EST(vZ, rZ);
    RSQRT_EST(vW, rW);

    ret.x = (rX);
    ret.y = (rY);
    ret.z = (rZ);
    ret.w = (rW);

    return ret;
}

static inline float4 rSqrtEstNR(float4 v)
{
    float4 ret;
    float  vX = v.x;
    float  vY = v.y;
    float  vZ = v.z;
    float  vW = v.w;

    float rX = 0.0f;
    float rY = 0.0f;
    float rZ = 0.0f;
    float rW = 0.0f;

    RSQRT_EST_NR(vX, rX);
    RSQRT_EST_NR(vY, rY);
    RSQRT_EST_NR(vZ, rZ);
    RSQRT_EST_NR(vW, rW);

    ret.x = (rX);
    ret.y = (rY);
    ret.z = (rZ);
    ret.w = (rW);

    return ret;
}

static inline float saturate(float x) { return clampf(x, 0.f, 1.f); }
static inline float sign(const float v) { return (v > 0) ? 1.0f : (v < 0) ? -1.0f : 0.0f; }

///
// C++ operators, overloads & extensions

#ifdef __cplusplus

#define DEF_VEC_LIKE_OPERATORS_ARGS_2(A, B, C, D) A, B
#define DEF_VEC_LIKE_OPERATORS_ARGS_3(A, B, C, D) A, B, C
#define DEF_VEC_LIKE_OPERATORS_ARGS_4(A, B, C, D) A, B, C, A

#define DEF_VEC_LIKE_OPERATORS(NAME, T, TPREFIX, COUNT)                                \
    inline NAME operator+(const NAME& a, const NAME& b) { return TPREFIX##Add(a, b); } \
    inline NAME operator-(const NAME& a, const NAME& b) { return TPREFIX##Sub(a, b); } \
    inline NAME operator*(const NAME& a, const NAME& b) { return TPREFIX##Mul(a, b); } \
    inline NAME operator/(const NAME& a, const NAME& b) { return TPREFIX##Div(a, b); } \
    inline NAME operator*(const NAME& a, T s) { return TPREFIX##MulScalar(a, s); }     \
    inline NAME operator*(T s, const NAME& a) { return TPREFIX##MulScalar(a, s); }     \
    inline NAME operator/(const NAME& a, T s) { return TPREFIX##DivScalar(a, s); }     \
    inline NAME operator/(T s, const NAME& b) { return TPREFIX##Div(NAME(s), b); }     \
    inline bool operator==(const NAME& a, const NAME& b)                               \
    {                                                                                  \
        for (int i = 0; i < COUNT; i += 1)                                             \
        {                                                                              \
            if (a[i] != b[i])                                                          \
                return false;                                                          \
        }                                                                              \
        return true;                                                                   \
    }                                                                                  \
    inline bool  operator!=(const NAME& a, const NAME& b) { return !(a == b); }        \
    inline NAME& operator+=(NAME& a, const NAME& b) { return a = a + b; }              \
    inline NAME& operator-=(NAME& a, const NAME& b) { return a = a - b; }              \
    inline NAME& operator*=(NAME& a, const NAME& b) { return a = a * b; }              \
    inline NAME& operator/=(NAME& a, const NAME& b) { return a = a / b; }              \
    inline NAME& operator*=(NAME& a, T b) { return a = a * b; }                        \
    inline NAME& operator/=(NAME& a, T b) { return a = a / b; }

#define DEF_VEC_LIKE_OPERATORS_SIGNED(NAME, T, TPREFIX, COUNT) \
    DEF_VEC_LIKE_OPERATORS(NAME, T, TPREFIX, COUNT)            \
    inline NAME operator-(const NAME& v) { return NAME(DEF_VEC_LIKE_OPERATORS_ARGS_##COUNT(-v.x, -v.y, -v.z, -v.w)); }

DEF_VEC_LIKE_OPERATORS_SIGNED(float2, float, f2, 2);
DEF_VEC_LIKE_OPERATORS_SIGNED(float3, float, f3, 3);
DEF_VEC_LIKE_OPERATORS_SIGNED(float3Aligned, float, f3, 3);
DEF_VEC_LIKE_OPERATORS_SIGNED(float4, float, f4, 4);

DEF_VEC_LIKE_OPERATORS_SIGNED(quat, float, quat, 4);

DEF_VEC_LIKE_OPERATORS_SIGNED(double2, double, d2, 2);
DEF_VEC_LIKE_OPERATORS_SIGNED(double3, double, d3, 3);
// DEF_VEC_LIKE_OPERATORS_SIGNED(double3Aligned, double, d3, 3);
DEF_VEC_LIKE_OPERATORS_SIGNED(double4, double, d4, 4);

DEF_VEC_LIKE_OPERATORS_SIGNED(int2, int, i2, 2);
DEF_VEC_LIKE_OPERATORS_SIGNED(int3, int, i3, 3);
// DEF_VEC_LIKE_OPERATORS_SIGNED(int3Aligned, int, i3, 3);
DEF_VEC_LIKE_OPERATORS_SIGNED(int4, int, i4, 4);

DEF_VEC_LIKE_OPERATORS(uint2, uint, u2, 2);
DEF_VEC_LIKE_OPERATORS(uint3, uint, u3, 3);
// DEF_VEC_LIKE_OPERATORS(uint3Aligned, uint, u3, 3);
DEF_VEC_LIKE_OPERATORS(uint4, uint, u4, 4);

inline float2x2  operator*(const float2x2& a, const float2x2& b) { return f2x2Mul(a, b); }
inline float2    operator*(const float2x2& m, const float2& v) { return f2x2Mulf2(m, v); }
inline float2    operator*(const float2& v, const float2x2& m) { return f2Mulf2x2(v, m); }
inline float2x2  operator*(const float2x2& m, float s) { return f2x2MulScalar(m, s); }
inline float2x2  operator*(float s, const float2x2& m) { return f2x2MulScalar(m, s); }
inline float2x2& operator*=(float2x2& a, const float2x2& b) { return a = a * b; }

inline float3x3        operator*(const float3x3& a, const float3x3& b) { return f3x3Mul(a, b); }
inline float3          operator*(const float3x3& m, const float3& v) { return f3x3Mulf3(m, v); }
inline float3          operator*(const float3& v, const float3x3& m) { return f3Mulf3x3(v, m); }
inline float3x3Aligned operator*(const float3x3Aligned& a, const float3x3Aligned& b) { return f3x3Mul(a, b); }
inline float3          operator*(const float3x3Aligned& m, const float3& v) { return f3x3Mulf3(m, v); }
inline float3          operator*(const float3& v, const float3x3Aligned& m) { return f3Mulf3x3(v, m); }
inline float3x3        operator*(const float3x3& m, float s) { return f3x3MulScalar(m, s); }
inline float3x3        operator*(float s, const float3x3& m) { return f3x3MulScalar(m, s); }
inline float3x3&       operator*=(float3x3& a, const float3x3& b) { return a = a * b; }

inline float4x4  operator*(const float4x4& a, const float4x4& b) { return f4x4Mul(a, b); }
inline float4    operator*(const float4x4& m, const float4& v) { return f4x4Mulf4(m, v); }
inline float4    operator*(const float4& v, const float4x4& m) { return f4Mulf4x4(v, m); }
inline float4x4  operator*(const float4x4& m, float s) { return f4x4MulScalar(m, s); }
inline float4x4  operator*(float s, const float4x4& m) { return f4x4MulScalar(m, s); }
inline float4x4& operator*=(float4x4& a, const float4x4& b) { return a = a * b; }

inline float lengthSqr(const float2& v) { return f2LengthSqr(v); }
inline float lengthSqr(const float3& v) { return f3LengthSqr(v); }
inline float lengthSqr(const float4& v) { return f4LengthSqr(v); }

inline float length(const float f) { return f; }
inline float length(const float2& v) { return f2Length(v); }
inline float length(const float3& v) { return f3Length(v); }
inline float length(const float4& v) { return f4Length(v); }

inline float2 normalize(const float2& v) { return f2Normalize(v); }
inline float3 normalize(const float3& v) { return f3Normalize(v); }
inline float4 normalize(const float4& v) { return f4Normalize(v); }
inline quat   normalize(const quat& q) { return f4ToQuat(f4Normalize(float4(q.x, q.y, q.z, q.w))); }

inline float dot(const float2& v0, const float2& v1) { return f2Dot(v0, v1); }
inline float dot(const float3& v0, const float3& v1) { return f3Dot(v0, v1); }
inline float dot(const float4& v0, const float4& v1) { return f4Dot(v0, v1); }
inline float dot(const quat& v0, const quat& v1) { return quatDot(v0, v1); }

inline float3 cross(const float3& v0, const float3& v1) { return f3Cross(v0, v1); }

inline bool isNormalizedEst(const float3& v) { return f3IsNormalizedEst(v); }

inline float2x2 transpose(const float2x2& m) { return f2x2Transpose(m); }
inline float3x3 transpose(const float3x3& m) { return f3x3Transpose(m); }
inline float4x4 transpose(const float4x4& m) { return f4x4Transpose(m); }

inline float determinant(const float2x2& m) { return f2x2Determinant(m); }
inline float determinant(const float3x3& m) { return f3x3Determinant(m); }
inline float determinant(const float4x4& m) { return f4x4Determinant(m); }

inline float2x2 inverse(const float2x2& m) { return f2x2Inverse(m); }
inline float3x3 inverse(const float3x3& m) { return f3x3Inverse(m); }
inline float4x4 inverse(const float4x4& m) { return f4x4Inverse(m); }

inline float2x2        float2x2::identity() { return f2x2Identity(); }
inline float3x3        float3x3::identity() { return f3x3Identity(); }
inline float3x3Aligned float3x3Aligned::identity() { return f3x3Identity(); }
inline float4x4        float4x4::identity() { return f4x4Identity(); }

inline float3x3        float3x3::rotationZYX(float3 radiansXYZ) { return f3x3RotationZYX(radiansXYZ); }
inline float3x3Aligned float3x3Aligned::rotationZYX(float3 radiansXYZ) { return f3x3RotationZYX(radiansXYZ); }
inline float3x3        float3x3::rotationQuat(quat q) { return f3x3RotationQuat(q); }
inline float3x3Aligned float3x3Aligned::rotationQuat(quat q) { return f3x3RotationQuat(q); }

inline double2x2  operator*(const double2x2& a, const double2x2& b) { return d2x2Mul(a, b); }
inline double2    operator*(const double2x2& m, const double2& v) { return d2x2Muld2(m, v); }
inline double2    operator*(const double2& v, const double2x2& m) { return d2Muld2x2(v, m); }
inline double2x2& operator*=(double2x2& a, const double2x2& b) { return a = a * b; }

inline double3x3  operator*(const double3x3& a, const double3x3& b) { return d3x3Mul(a, b); }
inline double3    operator*(const double3x3& m, const double3& v) { return d3x3Muld3(m, v); }
inline double3    operator*(const double3& v, const double3x3& m) { return d3Muld3x3(v, m); }
inline double3x3& operator*=(double3x3& a, const double3x3& b) { return a = a * b; }

inline double4x4  operator*(const double4x4& a, const double4x4& b) { return d4x4Mul(a, b); }
inline double4    operator*(const double4x4& m, const double4& v) { return d4x4Muld4(m, v); }
inline double4    operator*(const double4& v, const double4x4& m) { return d4Muld4x4(v, m); }
inline double4x4  operator*(const double4x4& m, double s) { return d4x4MulScalar(m, s); }
inline double4x4  operator*(double s, const double4x4& m) { return d4x4MulScalar(m, s); }
inline double4x4& operator*=(double4x4& a, const double4x4& b) { return a = a * b; }

inline float2::float2(const double2& v): x((float)v.x), y((float)v.y) {}
inline float2::float2(const int2& v): x((float)v.x), y((float)v.y) {}
inline float2::float2(const uint2& v): x((float)v.x), y((float)v.y) {}
inline float3::float3(const double3& v): x((float)v.x), y((float)v.y), z((float)v.z) {}
inline float3::float3(const int3& v): x((float)v.x), y((float)v.y), z((float)v.z) {}
inline float3::float3(const uint3& v): x((float)v.x), y((float)v.y), z((float)v.z) {}
inline float4::float4(const double4& v): x((float)v.x), y((float)v.y), z((float)v.z), w((float)v.w) {}
inline float4::float4(const int4& v): x((float)v.x), y((float)v.y), z((float)v.z), w((float)v.w) {}
inline float4::float4(const uint4& v): x((float)v.x), y((float)v.y), z((float)v.z), w((float)v.w) {}
inline double2::double2(const float2& v): x((double)v.x), y((double)v.y) {}
inline double2::double2(const int2& v): x((double)v.x), y((double)v.y) {}
inline double2::double2(const uint2& v): x((double)v.x), y((double)v.y) {}
inline double3::double3(const float3& v): x((double)v.x), y((double)v.y), z((double)v.z) {}
inline double3::double3(const int3& v): x((double)v.x), y((double)v.y), z((double)v.z) {}
inline double3::double3(const uint3& v): x((double)v.x), y((double)v.y), z((double)v.z) {}
inline double4::double4(const float4& v): x((double)v.x), y((double)v.y), z((double)v.z), w((double)v.w) {}
inline double4::double4(const int4& v): x((double)v.x), y((double)v.y), z((double)v.z), w((double)v.w) {}
inline double4::double4(const uint4& v): x((double)v.x), y((double)v.y), z((double)v.z), w((double)v.w) {}
inline int2::int2(const float2& v): x((int)v.x), y((int)v.y) {}
inline int2::int2(const double2& v): x((int)v.x), y((int)v.y) {}
inline int2::int2(const uint2& v): x((int)v.x), y((int)v.y) {}
inline int3::int3(const float3& v): x((int)v.x), y((int)v.y), z((int)v.z) {}
inline int3::int3(const double3& v): x((int)v.x), y((int)v.y), z((int)v.z) {}
inline int3::int3(const uint3& v): x((int)v.x), y((int)v.y), z((int)v.z) {}
inline int4::int4(const float4& v): x((int)v.x), y((int)v.y), z((int)v.z), w((int)v.w) {}
inline int4::int4(const double4& v): x((int)v.x), y((int)v.y), z((int)v.z), w((int)v.w) {}
inline int4::int4(const uint4& v): x((int)v.x), y((int)v.y), z((int)v.z), w((int)v.w) {}
inline uint2::uint2(const float2& v): x((uint)v.x), y((uint)v.y) {}
inline uint2::uint2(const double2& v): x((uint)v.x), y((uint)v.y) {}
inline uint2::uint2(const int2& v): x((uint)v.x), y((uint)v.y) {}
inline uint3::uint3(const float3& v): x((uint)v.x), y((uint)v.y), z((uint)v.z) {}
inline uint3::uint3(const double3& v): x((uint)v.x), y((uint)v.y), z((uint)v.z) {}
inline uint3::uint3(const int3& v): x((uint)v.x), y((uint)v.y), z((uint)v.z) {}
inline uint4::uint4(const float4& v): x((uint)v.x), y((uint)v.y), z((uint)v.z), w((uint)v.w) {}
inline uint4::uint4(const double4& v): x((uint)v.x), y((uint)v.y), z((uint)v.z), w((uint)v.w) {}
inline uint4::uint4(const int4& v): x((uint)v.x), y((uint)v.y), z((uint)v.z), w((uint)v.w) {}

inline double lengthSqr(const double2& v) { return d2LengthSqr(v); }
inline double lengthSqr(const double3& v) { return d3LengthSqr(v); }
inline double lengthSqr(const double4& v) { return d4LengthSqr(v); }

inline double length(const double f) { return f; }
inline double length(const double2& v) { return d2Length(v); }
inline double length(const double3& v) { return d3Length(v); }
inline double length(const double4& v) { return d4Length(v); }

inline double2 normalize(const double2& v) { return d2Normalize(v); }
inline double3 normalize(const double3& v) { return d3Normalize(v); }
inline double4 normalize(const double4& v) { return d4Normalize(v); }

inline double dot(const double2& v0, const double2& v1) { return d2Dot(v0, v1); }
inline double dot(const double3& v0, const double3& v1) { return d3Dot(v0, v1); }
inline double dot(const double4& v0, const double4& v1) { return d4Dot(v0, v1); }

inline bool isNormalizedEst(const double3& v) { return d3IsNormalizedEst(v); }

inline double3 cross(const double3& v0, const double3& v1) { return d3Cross(v0, v1); }

inline double2x2 transpose(const double2x2& m) { return d2x2Transpose(m); }
inline double3x3 transpose(const double3x3& m) { return d3x3Transpose(m); }
inline double4x4 transpose(const double4x4& m) { return d4x4Transpose(m); }

inline double determinant(const double2x2& m) { return d2x2Determinant(m); }
inline double determinant(const double3x3& m) { return d3x3Determinant(m); }
inline double determinant(const double4x4& m) { return d4x4Determinant(m); }

inline double2x2 inverse(const double2x2& m) { return d2x2Inverse(m); }
inline double3x3 inverse(const double3x3& m) { return d3x3Inverse(m); }
inline double4x4 inverse(const double4x4& m) { return d4x4Inverse(m); }

inline double2x2 double2x2::identity() { return d2x2Identity(); }
inline double3x3 double3x3::identity() { return d3x3Identity(); }
inline double4x4 double4x4::identity() { return d4x4Identity(); }

inline float4x4 float4x4::perspectiveLH(float fovRadians, float aspectInverse, float zNear, float zFar)
{
    return f4x4PerspectiveLH(fovRadians, aspectInverse, zNear, zFar);
}
inline float4x4 float4x4::perspectiveRH(float fovRadians, float aspectInverse, float zNear, float zFar)
{
    return f4x4PerspectiveRH(fovRadians, aspectInverse, zNear, zFar);
}
inline float4x4 float4x4::perspectiveLH_ReverseZ(float fovRadians, float aspectInverse, float zNear, float zFar)
{
    return f4x4PerspectiveLH_ReverseZ(fovRadians, aspectInverse, zNear, zFar);
}
inline float4x4 float4x4::perspectiveLH_AsymmetricFov(float leftDegrees, float rightDegrees, float upDegrees, float downDegrees,
                                                      float zNear, float zFar, bool isDegrees)
{
    return f4x4PerspectiveLH_AsymmetricFov(leftDegrees, rightDegrees, upDegrees, downDegrees, zNear, zFar, isDegrees);
}
inline float4x4 float4x4::perspectiveLH_ReverseZ_AsymmetricFov(float leftDegrees, float rightDegrees, float upDegrees, float downDegrees,
                                                               float zNear, float zFar, bool isDegrees)
{
    return f4x4PerspectiveLH_ReverseZ_AsymmetricFov(leftDegrees, rightDegrees, upDegrees, downDegrees, zNear, zFar, isDegrees);
}
inline float4x4 float4x4::orthographicLH(float left, float right, float bottom, float top, float zNear, float zFar)
{
    return f4x4OrthographicLH(left, right, bottom, top, zNear, zFar);
}
inline float4x4 float4x4::orthographicRH(float left, float right, float bottom, float top, float zNear, float zFar)
{
    return f4x4OrthographicRH(left, right, bottom, top, zNear, zFar);
}
inline float4x4 float4x4::orthographicLH_ReverseZ(float left, float right, float bottom, float top, float zNear, float zFar)
{
    return f4x4OrthographicLH_ReverseZ(left, right, bottom, top, zNear, zFar);
}
inline float4x4 float4x4::cubeProjectionLH(float zNear, float zFar) { return f4x4CubeProjectionLH(zNear, zFar); }
inline float4x4 float4x4::cubeProjectionRH(float zNear, float zFar) { return f4x4CubeProjectionRH(zNear, zFar); }
inline float4x4 float4x4::cubeView(uint32_t side) { return f4x4CubeView(side); }
inline void float4x4::extractFrustumClipPlanes(float4x4 vp, float4& rplane, float4& lplane, float4& tplane, float4& bplane, float4& fplane,
                                               float4& nplane, bool normalizePlanes)
{
    f4x4ExtractFrustumClipPlanes(vp, &rplane, &lplane, &tplane, &bplane, &fplane, &nplane, normalizePlanes);
}

inline float4x4 float4x4::rotationX(float radians) { return f4x4RotationX(radians); }
inline float4x4 float4x4::rotationY(float radians) { return f4x4RotationY(radians); }
inline float4x4 float4x4::rotationZ(float radians) { return f4x4RotationZ(radians); }
inline float4x4 float4x4::rotation(float radians, float3 unitVec) { return f4x4Rotation(radians, unitVec); }
inline float4x4 float4x4::rotationYX(float radiansY, float radiansX) { return f4x4RotationYX(radiansY, radiansX); }
inline float4x4 float4x4::rotationXY(float radiansX, float radiansY) { return f4x4RotationXY(radiansX, radiansY); }
inline float4x4 float4x4::rotationZYX(float3 radiansXYZ) { return f4x4RotationZYX(radiansXYZ); }
inline float4x4 float4x4::rotationQuat(quat q) { return f4x4RotationQuat(q); }

inline float4x4& float4x4::setTranslation(float3 t)
{
    *this = f4x4SetTranslation(*this, t);
    return *this;
}

inline float4x4 float4x4::scale(float3 scale) { return f4x4Scale(scale); }
inline float4x4 float4x4::translation(float3 v) { return f4x4Translation(v); }
inline float4x4 float4x4::lookAtLH(float3 eyePos, float3 lookAtPos, float3 upVec) { return f4x4LookAtLH(eyePos, lookAtPos, upVec); }
inline float4x4 float4x4::lookAtRH(float3 eyePos, float3 lookAtPos, float3 upVec) { return f4x4LookAtRH(eyePos, lookAtPos, upVec); }
inline float4x4 float4x4::frustumLH(float left, float right, float bottom, float top, float zNear, float zFar)
{
    return f4x4FrustumLH(left, right, bottom, top, zNear, zFar);
}

inline double4x4 double4x4::perspectiveLH(double fovRadians, double aspectInverse, double zNear, double zFar)
{
    return d4x4PerspectiveLH(fovRadians, aspectInverse, zNear, zFar);
}
inline double4x4 double4x4::perspectiveRH(double fovRadians, double aspectInverse, double zNear, double zFar)
{
    return d4x4PerspectiveRH(fovRadians, aspectInverse, zNear, zFar);
}
inline double4x4 double4x4::perspectiveLH_ReverseZ(double fovRadians, double aspectInverse, double zNear, double zFar)
{
    return d4x4PerspectiveLH_ReverseZ(fovRadians, aspectInverse, zNear, zFar);
}
inline double4x4 double4x4::orthographicLH(double left, double right, double bottom, double top, double zNear, double zFar)
{
    return d4x4OrthographicLH(left, right, bottom, top, zNear, zFar);
}
inline double4x4 double4x4::orthographicRH(double left, double right, double bottom, double top, double zNear, double zFar)
{
    return d4x4OrthographicRH(left, right, bottom, top, zNear, zFar);
}
inline double4x4 double4x4::orthographicLH_ReverseZ(double left, double right, double bottom, double top, double zNear, double zFar)
{
    return d4x4OrthographicLH_ReverseZ(left, right, bottom, top, zNear, zFar);
}
inline double4x4 double4x4::cubeProjectionLH(double zNear, double zFar) { return d4x4CubeProjectionLH(zNear, zFar); }
inline double4x4 double4x4::cubeProjectionRH(double zNear, double zFar) { return d4x4CubeProjectionRH(zNear, zFar); }
inline double4x4 double4x4::cubeView(uint32_t side) { return d4x4CubeView(side); }
inline double4x4 double4x4::lookAtLH(double3 eyePos, double3 lookAtPos, double3 upVec) { return d4x4LookAtLH(eyePos, lookAtPos, upVec); }
inline double4x4 double4x4::lookAtRH(double3 eyePos, double3 lookAtPos, double3 upVec) { return d4x4LookAtRH(eyePos, lookAtPos, upVec); }

static inline float  lerp(float x0, float x1, float t) { return fLerp(x0, x1, t); }
static inline float2 lerp(float2 v0, float2 v1, float t) { return f2Lerp(v0, v1, t); }
static inline float3 lerp(float3 v0, float3 v1, float t) { return f3Lerp(v0, v1, t); }
static inline float4 lerp(float4 v0, float4 v1, float t) { return f4Lerp(v0, v1, t); }
static inline quat   lerp(quat v0, quat v1, float t) { return quatLerp(v0, v1, t); }

static inline double  lerp(double v0, double v1, double t) { return dLerp(v0, v1, t); }
static inline double2 lerp(double2 v0, double2 v1, double t) { return d2Lerp(v0, v1, t); }
static inline double3 lerp(double3 v0, double3 v1, double t) { return d3Lerp(v0, v1, t); }
static inline double4 lerp(double4 v0, double4 v1, double t) { return d4Lerp(v0, v1, t); }

static inline float4x4 appendScale(float4x4 m, float3 scale) { return f4x4AppendScale(m, scale); }
static inline float4x4 prependScale(float3 scale, float4x4 m) { return f4x4PrependScale(scale, m); }

static inline float2 mulPerElem(float2 v0, float2 v1) { return f2MulPerElem(v0, v1); }
static inline float3 mulPerElem(float3 v0, float3 v1) { return f3MulPerElem(v0, v1); }
static inline float4 mulPerElem(float4 v0, float4 v1) { return f4MulPerElem(v0, v1); }

static inline float2 divPerElem(float2 v0, float2 v1) { return f2DivPerElem(v0, v1); }
static inline float3 divPerElem(float3 v0, float3 v1) { return f3DivPerElem(v0, v1); }
static inline float4 divPerElem(float4 v0, float4 v1) { return f4DivPerElem(v0, v1); }

static inline float2 recipPerElem(float2 v0) { return f2RecipPerElem(v0); }
static inline float3 recipPerElem(float3 v0) { return f3RecipPerElem(v0); }
static inline float4 recipPerElem(float4 v0) { return f4RecipPerElem(v0); }

static inline float2 sqrtPerElem(float2 v0) { return f2SqrtPerElem(v0); }
static inline float3 sqrtPerElem(float3 v0) { return f3SqrtPerElem(v0); }
static inline float4 sqrtPerElem(float4 v0) { return f4SqrtPerElem(v0); }

static inline float2 rsqrtPerElem(float2 v0) { return f2RsqrtPerElem(v0); }
static inline float3 rsqrtPerElem(float3 v0) { return f3RsqrtPerElem(v0); }
static inline float4 rsqrtPerElem(float4 v0) { return f4RsqrtPerElem(v0); }

static inline float2 fabsfPerElem(float2 v0) { return f2FabsfPerElem(v0); }
static inline float3 fabsfPerElem(float3 v0) { return f3FabsfPerElem(v0); }
static inline float4 fabsfPerElem(float4 v0) { return f4FabsfPerElem(v0); }

static inline float2 maxPerElem(float2 v0, float2 v1) { return f2MaxPerElem(v0, v1); }
static inline float3 maxPerElem(float3 v0, float3 v1) { return f3MaxPerElem(v0, v1); }
static inline float4 maxPerElem(float4 v0, float4 v1) { return f4MaxPerElem(v0, v1); }

static inline float maxElem(float2 v0) { return f2MaxElem(v0); }
static inline float maxElem(float3 v0) { return f3MaxElem(v0); }
static inline float maxElem(float4 v0) { return f4MaxElem(v0); }

static inline float2 minPerElem(float2 v0, float2 v1) { return f2MinPerElem(v0, v1); }
static inline float3 minPerElem(float3 v0, float3 v1) { return f3MinPerElem(v0, v1); }
static inline float4 minPerElem(float4 v0, float4 v1) { return f4MinPerElem(v0, v1); }

static inline float minElem(float2 v0) { return f2MinElem(v0); }
static inline float minElem(float3 v0) { return f3MinElem(v0); }
static inline float minElem(float4 v0) { return f4MinElem(v0); }

static inline float sum(float2 v0) { return f2Sum(v0); }
static inline float sum(float3 v0) { return f3Sum(v0); }
static inline float sum(float4 v0) { return f4Sum(v0); }

static inline float2 absPerElem(float2 v) { return f2AbsPerElem(v); }
static inline float3 absPerElem(float3 v) { return f3AbsPerElem(v); }
static inline float4 absPerElem(float4 v) { return f4AbsPerElem(v); }

static inline int4 absPerElem(int4 v) { return i4AbsPerElem(v); }

template<typename T>
static inline size_t tf_mem_hash(const T* mem, size_t size, size_t prev = 2166136261U)
{
    uint32_t result = (uint32_t)prev; // Intentionally uint32_t instead of size_t, so the behavior is the same
    // regardless of size.
    while (size--)
        result = (result * 16777619) ^ *mem++;
    return (size_t)result;
}

#endif

#define VECTORMATH_MODE_SCALAR 0

#include <math.h> // Basic trigonometry functions, sqrt, etc

/////////////
/// Row/elem access C++ overloads

#ifdef __cplusplus

inline void  setElem(float4x4* M, int i, int j, float val) { f4x4SetElem(M, i, j, val); }
inline float getElem(float4x4 M, int i, int j) { return f4x4GetElem(M, i, j); }
inline void  setElem(float3x3* M, int i, int j, float val) { f3x3SetElem(M, i, j, val); }
inline float getElem(float3x3 M, int i, int j) { return f3x3GetElem(M, i, j); }
inline void  setElem(float2x2* M, int i, int j, float val) { f2x2SetElem(M, i, j, val); }
inline float getElem(float2x2 M, int i, int j) { return f2x2GetElem(M, i, j); }

inline float4 getCol(float4x4 M, const uint i) { return f4x4GetCol(M, i); }
inline void   setCol(float4x4* M, float4 col, const uint i) { f4x4SetCol(M, col, i); }
inline float4 getRow(float4x4 M, const uint i) { return f4x4GetRow(M, i); }
inline void   setRow(float4x4* M, float4 row, const uint i) { f4x4SetRow(M, row, i); }

inline float3 getCol(float4x3 M, const uint i) { return f4x3GetCol(M, i); }
inline void   setCol(float4x3* M, float3 col, const uint i) { f4x3SetCol(M, col, i); }
inline float4 getRow(float4x3 M, const uint i) { return f4x3GetRow(M, i); }
inline void   setRow(float4x3* M, float4 row, const uint i) { f4x3SetRow(M, row, i); }

inline float2 getCol(float4x2 M, const uint i) { return f4x2GetCol(M, i); }
inline void   setCol(float4x2* M, float2 col, const uint i) { f4x2SetCol(M, col, i); }
inline float4 getRow(float4x2 M, const uint i) { return f4x2GetRow(M, i); }
inline void   setRow(float4x2* M, float4 row, const uint i) { f4x2SetRow(M, row, i); }

inline float4 getCol(float3x4 M, const uint i) { return f3x4GetCol(M, i); }
inline void   setCol(float3x4* M, float4 col, const uint i) { f3x4SetCol(M, col, i); }
inline float3 getRow(float3x4 M, const uint i) { return f3x4GetRow(M, i); }
inline void   setRow(float3x4* M, float3 row, const uint i) { f3x4SetRow(M, row, i); }

inline float3 getCol(float3x3 M, const uint i) { return f3x3GetCol(M, i); }
inline void   setCol(float3x3* M, float3 col, const uint i) { f3x3SetCol(M, col, i); }
inline float3 getRow(float3x3 M, const uint i) { return f3x3GetRow(M, i); }
inline void   setRow(float3x3* M, float3 row, const uint i) { f3x3SetRow(M, row, i); }

inline float2 getCol(float3x2 M, const uint i) { return f3x2GetCol(M, i); }
inline void   setCol(float3x2* M, float2 col, const uint i) { f3x2SetCol(M, col, i); }
inline float3 getRow(float3x2 M, const uint i) { return f3x2GetRow(M, i); }
inline void   setRow(float3x2* M, float3 row, const uint i) { f3x2SetRow(M, row, i); }

inline float4 getCol(float2x4 M, const uint i) { return f2x4GetCol(M, i); }
inline void   setCol(float2x4* M, float4 col, const uint i) { f2x4SetCol(M, col, i); }
inline float2 getRow(float2x4 M, const uint i) { return f2x4GetRow(M, i); }
inline void   setRow(float2x4* M, float2 row, const uint i) { f2x4SetRow(M, row, i); }

inline float3 getCol(float2x3 M, const uint i) { return f2x3GetCol(M, i); }
inline void   setCol(float2x3* M, float3 col, const uint i) { f2x3SetCol(M, col, i); }
inline float2 getRow(float2x3 M, const uint i) { return f2x3GetRow(M, i); }
inline void   setRow(float2x3* M, float2 row, const uint i) { f2x3SetRow(M, row, i); }

inline float2 getCol(float2x2 M, const uint i) { return f2x2GetCol(M, i); }
inline void   setCol(float2x2* M, float2 col, const uint i) { f2x2SetCol(M, col, i); }
inline float2 getRow(float2x2 M, const uint i) { return f2x2GetRow(M, i); }
inline void   setRow(float2x2* M, float2 row, const uint i) { f2x2SetRow(M, row, i); }

inline void   setElem(double4x4* M, int i, int j, double val) { d4x4SetElem(M, i, j, val); }
inline double getElem(double4x4 M, int i, int j) { return d4x4GetElem(M, i, j); }
inline void   setElem(double3x3* M, int i, int j, double val) { d3x3SetElem(M, i, j, val); }
inline double getElem(double3x3 M, int i, int j) { return d3x3GetElem(M, i, j); }
inline void   setElem(double2x2* M, int i, int j, double val) { d2x2SetElem(M, i, j, val); }
inline double getElem(double2x2 M, int i, int j) { return d2x2GetElem(M, i, j); }

inline double4 getCol(double4x4 M, const uint i) { return d4x4GetCol(M, i); }
inline void    setCol(double4x4* M, double4 col, const uint i) { d4x4SetCol(M, col, i); }
inline double4 getRow(double4x4 M, const uint i) { return d4x4GetRow(M, i); }
inline void    setRow(double4x4* M, double4 row, const uint i) { d4x4SetRow(M, row, i); }

inline double3 getCol(double4x3 M, const uint i) { return d4x3GetCol(M, i); }
inline void    setCol(double4x3* M, double3 col, const uint i) { d4x3SetCol(M, col, i); }
inline double4 getRow(double4x3 M, const uint i) { return d4x3GetRow(M, i); }
inline void    setRow(double4x3* M, double4 row, const uint i) { d4x3SetRow(M, row, i); }

inline double2 getCol(double4x2 M, const uint i) { return d4x2GetCol(M, i); }
inline void    setCol(double4x2* M, double2 col, const uint i) { d4x2SetCol(M, col, i); }
inline double4 getRow(double4x2 M, const uint i) { return d4x2GetRow(M, i); }
inline void    setRow(double4x2* M, double4 row, const uint i) { d4x2SetRow(M, row, i); }

inline double4 getCol(double3x4 M, const uint i) { return d3x4GetCol(M, i); }
inline void    setCol(double3x4* M, double4 col, const uint i) { d3x4SetCol(M, col, i); }
inline double3 getRow(double3x4 M, const uint i) { return d3x4GetRow(M, i); }
inline void    setRow(double3x4* M, double3 row, const uint i) { d3x4SetRow(M, row, i); }

inline double3 getCol(double3x3 M, const uint i) { return d3x3GetCol(M, i); }
inline void    setCol(double3x3* M, double3 col, const uint i) { d3x3SetCol(M, col, i); }
inline double3 getRow(double3x3 M, const uint i) { return d3x3GetRow(M, i); }
inline void    setRow(double3x3* M, double3 row, const uint i) { d3x3SetRow(M, row, i); }

inline double2 getCol(double3x2 M, const uint i) { return d3x2GetCol(M, i); }
inline void    setCol(double3x2* M, double2 col, const uint i) { d3x2SetCol(M, col, i); }
inline double3 getRow(double3x2 M, const uint i) { return d3x2GetRow(M, i); }
inline void    setRow(double3x2* M, double3 row, const uint i) { d3x2SetRow(M, row, i); }

inline double4 getCol(double2x4 M, const uint i) { return d2x4GetCol(M, i); }
inline void    setCol(double2x4* M, double4 col, const uint i) { d2x4SetCol(M, col, i); }
inline double2 getRow(double2x4 M, const uint i) { return d2x4GetRow(M, i); }
inline void    setRow(double2x4* M, double2 row, const uint i) { d2x4SetRow(M, row, i); }

inline double3 getCol(double2x3 M, const uint i) { return d2x3GetCol(M, i); }
inline void    setCol(double2x3* M, double3 col, const uint i) { d2x3SetCol(M, col, i); }
inline double2 getRow(double2x3 M, const uint i) { return d2x3GetRow(M, i); }
inline void    setRow(double2x3* M, double2 row, const uint i) { d2x3SetRow(M, row, i); }

inline double2 getCol(double2x2 M, const uint i) { return d2x2GetCol(M, i); }
inline void    setCol(double2x2* M, double2 col, const uint i) { d2x2SetCol(M, col, i); }
inline double2 getRow(double2x2 M, const uint i) { return d2x2GetRow(M, i); }
inline void    setRow(double2x2* M, double2 row, const uint i) { d2x2SetRow(M, row, i); }

#define getCol0(M)    getCol(M, 0)
#define getCol1(M)    getCol(M, 1)
#define getCol2(M)    getCol(M, 2)
#define getCol3(M)    getCol(M, 3)

#define getRow0(M)    getRow(M, 0)
#define getRow1(M)    getRow(M, 1)
#define getRow2(M)    getRow(M, 2)
#define getRow3(M)    getRow(M, 3)

#define setCol0(M, C) setCol(M, C, 0)
#define setCol1(M, C) setCol(M, C, 1)
#define setCol2(M, C) setCol(M, C, 2)
#define setCol3(M, C) setCol(M, C, 3)

#define setRow0(M, R) setRow(M, R, 0)
#define setRow1(M, R) setRow(M, R, 1)
#define setRow2(M, R) setRow(M, R, 2)
#define setRow3(M, R) setRow(M, R, 3)

#endif

///////
/// Typedefs

typedef float2        Vector2;
typedef float3Aligned Vector3;
typedef float4Aligned Vector4;

typedef float2        Point2;
typedef float3Aligned Point3;
typedef float4Aligned Point4;

typedef double2        Vector2d;
typedef double3Aligned Vector3d;
typedef double4Aligned Vector4d;

typedef int2        IVector2;
typedef int3Aligned IVector3;
typedef int4Aligned IVector4;

typedef uint2        UVector2;
typedef uint3Aligned UVector3;
typedef uint4Aligned UVector4;

typedef float2x2        Matrix2;
typedef float3x3Aligned Matrix3;
typedef float4x4        Matrix4;

typedef double2x2 Matrix2d;
typedef double3x3 Matrix3d;
typedef double4x4 Matrix4d;

typedef quat Quat;

typedef IVector4 Vector4Int;

typedef Vector2 vec2;
typedef Vector3 vec3;
typedef Vector4 vec4;

typedef IVector2 ivec2;
typedef IVector3 ivec3;
typedef IVector4 ivec4;

typedef UVector2 uvec2;
typedef UVector3 uvec3;
typedef UVector4 uvec4;

typedef Matrix2 mat2;
typedef Matrix3 mat3;
typedef Matrix4 mat4;

// Double-precision's inception was to fix facebook maps. Initial implementation was with scalars.
// Neon implementation was made to improve performance. Neon mirrors sse (see sse2neon.h) so sse exists as a bonus.
// Playstation uses its own SCE implementation hence this preprocessor.
// (Currently no need to support double-precision elsewhere then moble).
#if !VECTORMATH_MODE_SCE
typedef Vector3d vec3d;
typedef Vector4d vec4d;
typedef Matrix3d mat3d;
typedef Matrix4d mat4d;
#endif

/////////////
/// TF Random

#define TF_RAND_MAX     0x7FFFFFFF
#define TF_INITIAL_SEED 0x9747b28c

static inline int32_t getRandomInt(void)
{
    static uint32_t       seed = TF_INITIAL_SEED;
    static const uint32_t messageHash[] = {
        0x8C2100D0, 0xEC843F56, 0xDD467E25, 0xC22461F6, 0xA1368AB0, 0xBBDA7B12, 0xA175F888, 0x6BD9BDA2,
        0x999AC54C, 0x7C043DD3, 0xD502088F, 0x1B5B4D72, 0x94BB5742, 0x2CDA891E, 0x88613640, 0x31A50479
    };

    int32_t result;
    MurmurHash3_x86_32(&messageHash, TF_ARRAY_COUNT(messageHash) * sizeof(uint32_t), seed--, &result);
    return abs(result);
}

// Range [0.f, 1.f]
static inline float randomFloat01() { return (float)getRandomInt() / (float)TF_RAND_MAX; }

// Range [mn, mx]
static inline float randomFloat(float mn, float mx) { return randomFloat01() * (mx - mn) + mn; }

// Range [mn, mx)
static inline int randomInt(int mn, int mx)
{
    ASSERT(((long long)mx - (long long)mn) <= TF_RAND_MAX);
    return getRandomInt() % (mx - mn) + mn;
}

//----------------------------------------------------------------------------
// Mesh generation helpers
//----------------------------------------------------------------------------
// Generates an array of vertices and normals for a sphere
// If pPoints is NULL or pNumberOfPoints is less than computed number of floats required -
//    only writes number of floats required for pPoints into pNumberOfPoints
static inline void generateSpherePoints(float* pPoints, int* pNumberOfPoints, int numberOfDivisions, float radius)
{
    ASSERT(pNumberOfPoints);
    int   pointsSize = *pNumberOfPoints;
    float numStacks = (float)numberOfDivisions;
    float numSlices = (float)numberOfDivisions;

    int numberOfPoints = numberOfDivisions * numberOfDivisions * 6;
    *pNumberOfPoints = numberOfPoints * 3 * 2;
    if (pPoints == NULL || pointsSize < *pNumberOfPoints)
        return;

    uint32_t vertexCounter = 0;
    float3*  pPoints3 = (float3*)pPoints;

    for (int i = 0; i < numberOfDivisions; i++)
    {
        for (int j = 0; j < numberOfDivisions; j++)
        {
            // Sectioned into quads, utilizing two triangles
            float3 topLeftPoint = f3MulScalar(f3Make((float)(-cos(2.0f * PI * i / numStacks) * sin(PI * (j + 1.0f) / numSlices)),
                                                     (float)(-cos(PI * (j + 1.0f) / numSlices)),
                                                     (float)(sin(2.0f * PI * i / numStacks) * sin(PI * (j + 1.0f) / numSlices))),
                                              radius);
            float3 topRightPoint = f3MulScalar(f3Make((float)(-cos(2.0f * PI * (i + 1.0) / numStacks) * sin(PI * (j + 1.0) / numSlices)),
                                                      (float)(-cos(PI * (j + 1.0) / numSlices)),
                                                      (float)(sin(2.0f * PI * (i + 1.0) / numStacks) * sin(PI * (j + 1.0) / numSlices))),
                                               radius);
            float3 botLeftPoint =
                f3MulScalar(f3Make((float)(-cos(2.0f * PI * i / numStacks) * sin(PI * j / numSlices)), (float)(-cos(PI * j / numSlices)),
                                   (float)(sin(2.0f * PI * i / numStacks) * sin(PI * j / numSlices))),
                            radius);
            float3 botRightPoint = f3MulScalar(f3Make((float)(-cos(2.0f * PI * (i + 1.0) / numStacks) * sin(PI * j / numSlices)),
                                                      (float)(-cos(PI * j / numSlices)),
                                                      (float)(sin(2.0f * PI * (i + 1.0) / numStacks) * sin(PI * j / numSlices))),
                                               radius);

            // Top right triangle
            pPoints3[vertexCounter++] = (topLeftPoint);
            pPoints3[vertexCounter++] = (f3Normalize(topLeftPoint));
            pPoints3[vertexCounter++] = (botRightPoint);
            pPoints3[vertexCounter++] = (f3Normalize(botRightPoint));
            pPoints3[vertexCounter++] = (topRightPoint);
            pPoints3[vertexCounter++] = (f3Normalize(topRightPoint));

            // Bot left triangle
            pPoints3[vertexCounter++] = (topLeftPoint);
            pPoints3[vertexCounter++] = (f3Normalize(topLeftPoint));
            pPoints3[vertexCounter++] = (botLeftPoint);
            pPoints3[vertexCounter++] = (f3Normalize(botLeftPoint));
            pPoints3[vertexCounter++] = (botRightPoint);
            pPoints3[vertexCounter++] = (f3Normalize(botRightPoint));
        }
    }
}

// Generates an array of vertices and normals for a quad
// If pPoints is NULL or pNumberOfPoints is less than computed number of floats required -
//    only writes number of floats required for pPoints into pNumberOfPoints
static inline void generateQuad(float* pPoints, int* pNumberOfPoints, float sideLength)
{
    int numberOfPoints = 4;
    int pointsSize = *pNumberOfPoints;
    *pNumberOfPoints = numberOfPoints * 3 * 2;
    if (pPoints == NULL || pointsSize < *pNumberOfPoints)
        return;

    float3*  pPoints3 = (float3*)pPoints;
    uint32_t vertexCounter = 0;

    // Single quad
    float3 topLeftPoint = f3MulScalar(f3Make(-1.0f, 1.0f, 0.0f), sideLength);
    float3 topRightPoint = f3MulScalar(f3Make(1.0f, 1.0f, 0.0f), sideLength);
    float3 botLeftPoint = f3MulScalar(f3Make(-1.0f, -1.0f, 0.0f), sideLength);
    float3 botRightPoint = f3MulScalar(f3Make(1.0f, -1.0f, 0.0f), sideLength);

    pPoints3[vertexCounter++] = (topLeftPoint);
    pPoints3[vertexCounter++] = (f3Normalize(topLeftPoint));
    pPoints3[vertexCounter++] = (topRightPoint);
    pPoints3[vertexCounter++] = (f3Normalize(topRightPoint));
    pPoints3[vertexCounter++] = (botLeftPoint);
    pPoints3[vertexCounter++] = (f3Normalize(botLeftPoint));
    pPoints3[vertexCounter++] = (botRightPoint);
    pPoints3[vertexCounter++] = (f3Normalize(botRightPoint));
}

// Generates an array of vertices and normals for a 3D rectangle (cuboid)
// If pPoints is NULL or pNumberOfPoints is less than computed number of floats required -
//    only writes number of floats required for pPoints into pNumberOfPoints
static inline void generateCuboidPoints(float* pPoints, int* pNumberOfPoints, float width, float height, float depth, float3 center)
{
    ASSERT(pNumberOfPoints);

    int numberOfPoints = 6 * 6;
    int pointsSize = *pNumberOfPoints;

    *pNumberOfPoints = numberOfPoints * 3 * 2;
    if (pPoints == NULL || pointsSize < *pNumberOfPoints)
        return;

    float3*  pPoints3 = (float3*)pPoints;
    uint32_t vertexCounter = 0;

    float3 topLeftFrontPoint = f3Add(f3Make(-width / 2, height / 2, depth / 2), center);
    float3 topRightFrontPoint = f3Add(f3Make(width / 2, height / 2, depth / 2), center);
    float3 botLeftFrontPoint = f3Add(f3Make(-width / 2, -height / 2, depth / 2), center);
    float3 botRightFrontPoint = f3Add(f3Make(width / 2, -height / 2, depth / 2), center);

    float3 topLeftBackPoint = f3Add(f3Make(-width / 2, height / 2, -depth / 2), center);
    float3 topRightBackPoint = f3Add(f3Make(width / 2, height / 2, -depth / 2), center);
    float3 botLeftBackPoint = f3Add(f3Make(-width / 2, -height / 2, -depth / 2), center);
    float3 botRightBackPoint = f3Add(f3Make(width / 2, -height / 2, -depth / 2), center);

    float3 leftNormal = f3Make(-1.0f, 0.0f, 0.0f);
    float3 rightNormal = f3Make(1.0f, 0.0f, 0.0f);
    float3 botNormal = f3Make(0.0f, -1.0f, 0.0f);
    float3 topNormal = f3Make(0.0f, 1.0f, 0.0f);
    float3 backNormal = f3Make(0.0f, 0.0f, -1.0f);
    float3 frontNormal = f3Make(0.0f, 0.0f, 1.0f);

    // Front Face
    // Top right triangle
    pPoints3[vertexCounter++] = (topLeftFrontPoint);
    pPoints3[vertexCounter++] = (frontNormal);
    pPoints3[vertexCounter++] = (botRightFrontPoint);
    pPoints3[vertexCounter++] = (frontNormal);
    pPoints3[vertexCounter++] = (topRightFrontPoint);
    pPoints3[vertexCounter++] = (frontNormal);
    // Bot left triangle
    pPoints3[vertexCounter++] = (topLeftFrontPoint);
    pPoints3[vertexCounter++] = (frontNormal);
    pPoints3[vertexCounter++] = (botLeftFrontPoint);
    pPoints3[vertexCounter++] = (frontNormal);
    pPoints3[vertexCounter++] = (botRightFrontPoint);
    pPoints3[vertexCounter++] = (frontNormal);

    // Back Face
    // Top right triangle
    pPoints3[vertexCounter++] = (topLeftBackPoint);
    pPoints3[vertexCounter++] = (backNormal);
    pPoints3[vertexCounter++] = (topRightBackPoint);
    pPoints3[vertexCounter++] = (backNormal);
    pPoints3[vertexCounter++] = (botRightBackPoint);
    pPoints3[vertexCounter++] = (backNormal);

    // Bot left triangle
    pPoints3[vertexCounter++] = (topLeftBackPoint);
    pPoints3[vertexCounter++] = (backNormal);
    pPoints3[vertexCounter++] = (botRightBackPoint);
    pPoints3[vertexCounter++] = (backNormal);
    pPoints3[vertexCounter++] = (botLeftBackPoint);
    pPoints3[vertexCounter++] = (backNormal);

    // Left Face
    // Top right triangle
    pPoints3[vertexCounter++] = (topLeftBackPoint);
    pPoints3[vertexCounter++] = (leftNormal);
    pPoints3[vertexCounter++] = (botLeftFrontPoint);
    pPoints3[vertexCounter++] = (leftNormal);
    pPoints3[vertexCounter++] = (topLeftFrontPoint);
    pPoints3[vertexCounter++] = (leftNormal);

    // Bot left triangle
    pPoints3[vertexCounter++] = (topLeftBackPoint);
    pPoints3[vertexCounter++] = (leftNormal);
    pPoints3[vertexCounter++] = (botLeftBackPoint);
    pPoints3[vertexCounter++] = (leftNormal);
    pPoints3[vertexCounter++] = (botLeftFrontPoint);
    pPoints3[vertexCounter++] = (leftNormal);

    // Right Face
    // Top right triangle
    pPoints3[vertexCounter++] = (topRightBackPoint);
    pPoints3[vertexCounter++] = (rightNormal);
    pPoints3[vertexCounter++] = (topRightFrontPoint);
    pPoints3[vertexCounter++] = (rightNormal);
    pPoints3[vertexCounter++] = (botRightFrontPoint);
    pPoints3[vertexCounter++] = (rightNormal);

    // Bot left triangle
    pPoints3[vertexCounter++] = (topRightBackPoint);
    pPoints3[vertexCounter++] = (rightNormal);
    pPoints3[vertexCounter++] = (botRightFrontPoint);
    pPoints3[vertexCounter++] = (rightNormal);
    pPoints3[vertexCounter++] = (botRightBackPoint);
    pPoints3[vertexCounter++] = (rightNormal);

    // Top Face
    // Top right triangle
    pPoints3[vertexCounter++] = (topLeftBackPoint);
    pPoints3[vertexCounter++] = (topNormal);
    pPoints3[vertexCounter++] = (topRightFrontPoint);
    pPoints3[vertexCounter++] = (topNormal);
    pPoints3[vertexCounter++] = (topRightBackPoint);
    pPoints3[vertexCounter++] = (topNormal);

    // Bot left triangle
    pPoints3[vertexCounter++] = (topLeftBackPoint);
    pPoints3[vertexCounter++] = (topNormal);
    pPoints3[vertexCounter++] = (topLeftFrontPoint);
    pPoints3[vertexCounter++] = (topNormal);
    pPoints3[vertexCounter++] = (topRightFrontPoint);
    pPoints3[vertexCounter++] = (topNormal);

    // Bottom Face
    // Top right triangle
    pPoints3[vertexCounter++] = (botLeftBackPoint);
    pPoints3[vertexCounter++] = (botNormal);
    pPoints3[vertexCounter++] = (botRightBackPoint);
    pPoints3[vertexCounter++] = (botNormal);
    pPoints3[vertexCounter++] = (botRightFrontPoint);
    pPoints3[vertexCounter++] = (botNormal);

    // Bot left triangle
    pPoints3[vertexCounter++] = (botLeftBackPoint);
    pPoints3[vertexCounter++] = (botNormal);
    pPoints3[vertexCounter++] = (botRightFrontPoint);
    pPoints3[vertexCounter++] = (botNormal);
    pPoints3[vertexCounter++] = (botLeftFrontPoint);
    pPoints3[vertexCounter++] = (botNormal);
}

// Generates an array of vertices and normals for a bone of length = 1.f and width = widthRatio
// If pPoints is NULL or pNumberOfPoints is less than computed number of floats required -
//    only writes number of floats required for pPoints into pNumberOfPoints
static inline void generateBonePoints(float* pPoints, int* pNumberOfPoints, float widthRatio)
{
    ASSERT(pNumberOfPoints);
    int numberOfPoints = 8 * 3;
    int pointsSize = *pNumberOfPoints;
    *pNumberOfPoints = numberOfPoints * 3 * 2;
    if (pPoints == NULL || pointsSize < *pNumberOfPoints)
        return;

    float3*  pPoints3 = (float3*)pPoints;
    uint32_t vertexCounter = 0;

    float3 origin = f3Make(0.f, 0.f, 0.f);
    float3 topWidth = f3Make(widthRatio, 0.05f, 0.05f);
    float3 botWidth = f3Make(widthRatio, -0.05f, -0.05f);
    float3 frontWidth = f3Make(widthRatio, -0.05f, 0.05f);
    float3 backWidth = f3Make(widthRatio, 0.05f, -0.05f);
    float3 boneLength = f3Make(1.f, 0.f, 0.f);

    float3 frontTopLeftFaceNormal = f3Normalize(f3Cross(f3Sub(frontWidth, origin), f3Sub(topWidth, origin)));
    float3 backTopLeftFaceNormal = f3Normalize(f3Cross(f3Sub(topWidth, origin), f3Sub(backWidth, origin)));
    float3 frontBotLeftFaceNormal = f3Normalize(f3Cross(f3Sub(botWidth, origin), f3Sub(frontWidth, origin)));
    float3 backBotLeftFaceNormal = f3Normalize(f3Cross(f3Sub(backWidth, origin), f3Sub(botWidth, origin)));
    float3 frontTopRightFaceNormal = f3Normalize(f3Cross(f3Sub(boneLength, frontWidth), f3Sub(topWidth, frontWidth)));
    float3 backTopRightFaceNormal = f3Normalize(f3Cross(f3Sub(topWidth, backWidth), f3Sub(boneLength, backWidth)));
    float3 frontBotRightFaceNormal = f3Normalize(f3Cross(f3Sub(botWidth, frontWidth), f3Sub(boneLength, frontWidth)));
    float3 backBotRightFaceNormal = f3Normalize(f3Cross(f3Sub(boneLength, backWidth), f3Sub(botWidth, backWidth)));

    float rightFaceArea = f3Length(f3Cross(f3Sub(boneLength, frontWidth), f3Sub(topWidth, frontWidth)));
    float leftFaceArea = f3Length(f3Cross(f3Sub(frontWidth, origin), f3Sub(topWidth, origin)));
    float maxFaceArea = (leftFaceArea > rightFaceArea) ? leftFaceArea : rightFaceArea;

    float leftRatio = leftFaceArea / maxFaceArea;
    float rightRatio = rightFaceArea / maxFaceArea;

    float3 sumOrigin = f3Add(f3Add(frontTopLeftFaceNormal, backTopLeftFaceNormal), f3Add(frontBotLeftFaceNormal, backBotLeftFaceNormal));
    float3 avgOrigin = f3DivScalar(sumOrigin, 4.f);
    float3 originNorm = f3Normalize(avgOrigin);

    float3 sumBone = f3Add(f3Add(frontTopRightFaceNormal, backTopRightFaceNormal), f3Add(frontBotRightFaceNormal, backBotRightFaceNormal));
    float3 avgBone = f3DivScalar(sumBone, 4.f);
    float3 boneLengthNorm = f3Normalize(avgBone);

    float3 leftFTL = f3MulScalar(frontTopLeftFaceNormal, leftRatio);
    float3 leftBTL = f3MulScalar(backTopLeftFaceNormal, leftRatio);
    float3 rightFTR = f3MulScalar(frontTopRightFaceNormal, rightRatio);
    float3 rightBTR = f3MulScalar(backTopRightFaceNormal, rightRatio);

    float3 sumTopWidth = f3Add(f3Add(leftFTL, leftBTL), f3Add(rightFTR, rightBTR));
    float3 avgTopWidth = f3DivScalar(sumTopWidth, 4.f);
    float3 topWidthNorm = f3Normalize(avgTopWidth);

    float3 leftFBL = f3MulScalar(frontBotLeftFaceNormal, leftRatio);
    float3 leftBBL = f3MulScalar(backBotLeftFaceNormal, leftRatio);
    float3 rightFBR = f3MulScalar(frontBotRightFaceNormal, rightRatio);
    float3 rightBBR = f3MulScalar(backBotRightFaceNormal, rightRatio);

    float3 sumBotWidth = f3Add(f3Add(leftFBL, leftBBL), f3Add(rightFBR, rightBBR));
    float3 avgBotWidth = f3DivScalar(sumBotWidth, 4.f);
    float3 botWidthNorm = f3Normalize(avgBotWidth);

    float3 leftFBot = f3MulScalar(frontBotLeftFaceNormal, leftRatio);
    float3 leftFTop = f3MulScalar(frontTopLeftFaceNormal, leftRatio);
    float3 rightFBot = f3MulScalar(frontBotRightFaceNormal, rightRatio);
    float3 rightFTop = f3MulScalar(frontTopRightFaceNormal, rightRatio);

    float3 sumFrontWidth = f3Add(f3Add(leftFBot, leftFTop), f3Add(rightFBot, rightFTop));
    float3 avgFrontWidth = f3DivScalar(sumFrontWidth, 4.f);
    float3 frontWidthNorm = f3Normalize(avgFrontWidth);

    float3 leftBBot = f3MulScalar(backBotLeftFaceNormal, leftRatio);
    float3 leftBTop = f3MulScalar(backTopLeftFaceNormal, leftRatio);
    float3 rightBBot = f3MulScalar(backBotRightFaceNormal, rightRatio);
    float3 rightBTop = f3MulScalar(backTopRightFaceNormal, rightRatio);

    float3 sumBackWidth = f3Add(f3Add(leftBBot, leftBTop), f3Add(rightBBot, rightBTop));
    float3 avgBackWidth = f3DivScalar(sumBackWidth, 4.f);
    float3 backWidthNorm = f3Normalize(avgBackWidth);

    // Front
    // Top left triangle
    pPoints3[vertexCounter++] = (origin);
    pPoints3[vertexCounter++] = (f3Normalize(originNorm));
    pPoints3[vertexCounter++] = (frontWidth);
    pPoints3[vertexCounter++] = (f3Normalize(frontWidthNorm));
    pPoints3[vertexCounter++] = (topWidth);
    pPoints3[vertexCounter++] = (f3Normalize(topWidthNorm));

    // Top right triangle
    pPoints3[vertexCounter++] = (topWidth);
    pPoints3[vertexCounter++] = (topWidthNorm);
    pPoints3[vertexCounter++] = (frontWidth);
    pPoints3[vertexCounter++] = (frontWidthNorm);
    pPoints3[vertexCounter++] = (boneLength);
    pPoints3[vertexCounter++] = (boneLengthNorm);

    // Bot left triangle
    pPoints3[vertexCounter++] = (origin);
    pPoints3[vertexCounter++] = (originNorm);
    pPoints3[vertexCounter++] = (botWidth);
    pPoints3[vertexCounter++] = (botWidthNorm);
    pPoints3[vertexCounter++] = (frontWidth);
    pPoints3[vertexCounter++] = (frontWidthNorm);

    // Bot right triangle
    pPoints3[vertexCounter++] = (frontWidth);
    pPoints3[vertexCounter++] = (frontWidthNorm);
    pPoints3[vertexCounter++] = (botWidth);
    pPoints3[vertexCounter++] = (botWidthNorm);
    pPoints3[vertexCounter++] = (boneLength);
    pPoints3[vertexCounter++] = (boneLengthNorm);

    // Back
    // Top left triangle
    pPoints3[vertexCounter++] = (origin);
    pPoints3[vertexCounter++] = (originNorm);
    pPoints3[vertexCounter++] = (topWidth);
    pPoints3[vertexCounter++] = (topWidthNorm);
    pPoints3[vertexCounter++] = (backWidth);
    pPoints3[vertexCounter++] = (backWidthNorm);

    // Top right triangle
    pPoints3[vertexCounter++] = (topWidth);
    pPoints3[vertexCounter++] = (topWidthNorm);
    pPoints3[vertexCounter++] = (boneLength);
    pPoints3[vertexCounter++] = (boneLengthNorm);
    pPoints3[vertexCounter++] = (backWidth);
    pPoints3[vertexCounter++] = (backWidthNorm);

    // Bot left triangle
    pPoints3[vertexCounter++] = (origin);
    pPoints3[vertexCounter++] = (originNorm);
    pPoints3[vertexCounter++] = (backWidth);
    pPoints3[vertexCounter++] = (backWidthNorm);
    pPoints3[vertexCounter++] = (botWidth);
    pPoints3[vertexCounter++] = (botWidthNorm);

    // Bot right triangle
    pPoints3[vertexCounter++] = (backWidth);
    pPoints3[vertexCounter++] = (backWidthNorm);
    pPoints3[vertexCounter++] = (boneLength);
    pPoints3[vertexCounter++] = (boneLengthNorm);
    pPoints3[vertexCounter++] = (botWidth);
    pPoints3[vertexCounter++] = (botWidthNorm);
}

typedef struct TFIndexBonePoint
{
    float3   position;
    float3   normal;
    uint16_t jointIndices[4];
} TFIndexBonePoint;

// Generates an array of vertices and normals for a bone of length = 1.f and width = widthRatio
// If pPoints is NULL or pNumberOfPoints is less than computed number of floats required -
//    only writes number of floats required for pPoints into pNumberOfPoints
static inline void generateIndexedBonePoints(float* pPoints, int* pNumberOfPoints, float widthRatio, uint32_t boneCount,
                                             const int16_t* parentIndices)
{
    int      pointsSize = *pNumberOfPoints;
    uint32_t numberOfPoints = 8 * 3 * boneCount;

    COMPILE_ASSERT((sizeof(TFIndexBonePoint) % sizeof(float) == 0) ? 1 : 0);
    *pNumberOfPoints = numberOfPoints * (sizeof(TFIndexBonePoint) / sizeof(float));

    if (pPoints == NULL || pointsSize < *pNumberOfPoints)
        return;

    TFIndexBonePoint* pPointsStruct = (TFIndexBonePoint*)pPoints;
    uint32_t          vertexCounter = 0;

    // Initialize vertices for bone geometry.
    float3 origin = f3Make(0.f, 0.f, 0.f);
    float3 topWidth = f3Make(widthRatio, 0.05f, 0.05f);
    float3 botWidth = f3Make(widthRatio, -0.05f, -0.05f);
    float3 frontWidth = f3Make(widthRatio, -0.05f, 0.05f);
    float3 backWidth = f3Make(widthRatio, 0.05f, -0.05f);
    float3 boneLength = f3Make(1.f, 0.f, 0.f);

    // Compute face normals using our arithmetic functions.
    float3 frontTopLeftFaceNormal = f3Normalize(f3Cross(f3Sub(frontWidth, origin), f3Sub(topWidth, origin)));
    float3 backTopLeftFaceNormal = f3Normalize(f3Cross(f3Sub(topWidth, origin), f3Sub(backWidth, origin)));
    float3 frontBotLeftFaceNormal = f3Normalize(f3Cross(f3Sub(botWidth, origin), f3Sub(frontWidth, origin)));
    float3 backBotLeftFaceNormal = f3Normalize(f3Cross(f3Sub(backWidth, origin), f3Sub(botWidth, origin)));
    float3 frontTopRightFaceNormal = f3Normalize(f3Cross(f3Sub(boneLength, frontWidth), f3Sub(topWidth, frontWidth)));
    float3 backTopRightFaceNormal = f3Normalize(f3Cross(f3Sub(topWidth, backWidth), f3Sub(boneLength, backWidth)));
    float3 frontBotRightFaceNormal = f3Normalize(f3Cross(f3Sub(botWidth, frontWidth), f3Sub(boneLength, frontWidth)));
    float3 backBotRightFaceNormal = f3Normalize(f3Cross(f3Sub(boneLength, backWidth), f3Sub(botWidth, backWidth)));

    // Compute face areas using f3Length.
    float rightFaceArea = f3Length(f3Cross(f3Sub(boneLength, frontWidth), f3Sub(topWidth, frontWidth)));
    float leftFaceArea = f3Length(f3Cross(f3Sub(frontWidth, origin), f3Sub(topWidth, origin)));
    float maxFaceArea = (leftFaceArea > rightFaceArea) ? leftFaceArea : rightFaceArea;

    float leftRatio = leftFaceArea / maxFaceArea;
    float rightRatio = rightFaceArea / maxFaceArea;

    // Compute averaged normals using explicit addition, scalar multiplication, and division.
    float3 sumOrigin = f3Add(f3Add(frontTopLeftFaceNormal, backTopLeftFaceNormal), f3Add(frontBotLeftFaceNormal, backBotLeftFaceNormal));
    float3 originNorm = f3Normalize(f3DivScalar(sumOrigin, 4.f));

    float3 sumBoneLength =
        f3Add(f3Add(frontTopRightFaceNormal, backTopRightFaceNormal), f3Add(frontBotRightFaceNormal, backBotRightFaceNormal));
    float3 boneLengthNorm = f3Normalize(f3DivScalar(sumBoneLength, 4.f));

    // Top width normal.
    float3 part1 = f3MulScalar(frontTopLeftFaceNormal, leftRatio);
    float3 part2 = f3MulScalar(backTopLeftFaceNormal, leftRatio);
    float3 part3 = f3MulScalar(frontTopRightFaceNormal, rightRatio);
    float3 part4 = f3MulScalar(backTopRightFaceNormal, rightRatio);
    float3 sumTopWidth = f3Add(f3Add(part1, part2), f3Add(part3, part4));
    float3 topWidthNorm = f3Normalize(f3DivScalar(sumTopWidth, 4.f));

    // Bottom width normal.
    float3 part5 = f3MulScalar(frontBotLeftFaceNormal, leftRatio);
    float3 part6 = f3MulScalar(backBotLeftFaceNormal, leftRatio);
    float3 part7 = f3MulScalar(frontBotRightFaceNormal, rightRatio);
    float3 part8 = f3MulScalar(backBotRightFaceNormal, rightRatio);
    float3 sumBotWidth = f3Add(f3Add(part5, part6), f3Add(part7, part8));
    float3 botWidthNorm = f3Normalize(f3DivScalar(sumBotWidth, 4.f));

    // Front width normal.
    float3 part9 = f3MulScalar(frontBotLeftFaceNormal, leftRatio);
    float3 part10 = f3MulScalar(frontTopLeftFaceNormal, leftRatio);
    float3 part11 = f3MulScalar(frontBotRightFaceNormal, rightRatio);
    float3 part12 = f3MulScalar(frontTopRightFaceNormal, rightRatio);
    float3 sumFrontWidth = f3Add(f3Add(part9, part10), f3Add(part11, part12));
    float3 frontWidthNorm = f3Normalize(f3DivScalar(sumFrontWidth, 4.f));

    // Back width normal.
    float3 part13 = f3MulScalar(backBotLeftFaceNormal, leftRatio);
    float3 part14 = f3MulScalar(backTopLeftFaceNormal, leftRatio);
    float3 part15 = f3MulScalar(backBotRightFaceNormal, rightRatio);
    float3 part16 = f3MulScalar(backTopRightFaceNormal, rightRatio);
    float3 sumBackWidth = f3Add(f3Add(part13, part14), f3Add(part15, part16));
    float3 backWidthNorm = f3Normalize(f3DivScalar(sumBackWidth, 4.f));

    // Loop through each bone to generate its vertices.
    for (uint32_t boneIndex = 0; boneIndex < boneCount; boneIndex++)
    {
        uint16_t currBoneIndex = (uint16_t)boneIndex;
        uint16_t currParentIndex = (uint16_t)parentIndices[boneIndex];
        // For the root bone, the parent index remains what is in parentIndices.
        if (boneIndex == 0)
        {
            currParentIndex = (uint16_t)parentIndices[boneIndex];
        }
        // Front Face
        // Top left triangle
        pPointsStruct[vertexCounter].position = origin;
        pPointsStruct[vertexCounter].normal = f3Normalize(originNorm);
        pPointsStruct[vertexCounter].jointIndices[0] = currBoneIndex;
        pPointsStruct[vertexCounter].jointIndices[1] = currParentIndex;
        vertexCounter++;

        pPointsStruct[vertexCounter].position = frontWidth;
        pPointsStruct[vertexCounter].normal = f3Normalize(frontWidthNorm);
        pPointsStruct[vertexCounter].jointIndices[0] = currBoneIndex;
        pPointsStruct[vertexCounter].jointIndices[1] = currParentIndex;
        vertexCounter++;

        pPointsStruct[vertexCounter].position = topWidth;
        pPointsStruct[vertexCounter].normal = f3Normalize(topWidthNorm);
        pPointsStruct[vertexCounter].jointIndices[0] = currBoneIndex;
        pPointsStruct[vertexCounter].jointIndices[1] = currParentIndex;
        vertexCounter++;

        // Top right triangle
        pPointsStruct[vertexCounter].position = topWidth;
        pPointsStruct[vertexCounter].normal = topWidthNorm;
        pPointsStruct[vertexCounter].jointIndices[0] = currBoneIndex;
        pPointsStruct[vertexCounter].jointIndices[1] = currParentIndex;
        vertexCounter++;

        pPointsStruct[vertexCounter].position = frontWidth;
        pPointsStruct[vertexCounter].normal = frontWidthNorm;
        pPointsStruct[vertexCounter].jointIndices[0] = currBoneIndex;
        pPointsStruct[vertexCounter].jointIndices[1] = currParentIndex;
        vertexCounter++;

        pPointsStruct[vertexCounter].position = boneLength;
        pPointsStruct[vertexCounter].normal = boneLengthNorm;
        pPointsStruct[vertexCounter].jointIndices[0] = currBoneIndex;
        pPointsStruct[vertexCounter].jointIndices[1] = currParentIndex;
        vertexCounter++;

        // Bottom Face
        // Bot left triangle
        pPointsStruct[vertexCounter].position = origin;
        pPointsStruct[vertexCounter].normal = originNorm;
        pPointsStruct[vertexCounter].jointIndices[0] = currBoneIndex;
        pPointsStruct[vertexCounter].jointIndices[1] = currParentIndex;
        vertexCounter++;

        pPointsStruct[vertexCounter].position = botWidth;
        pPointsStruct[vertexCounter].normal = botWidthNorm;
        pPointsStruct[vertexCounter].jointIndices[0] = currBoneIndex;
        pPointsStruct[vertexCounter].jointIndices[1] = currParentIndex;
        vertexCounter++;

        pPointsStruct[vertexCounter].position = frontWidth;
        pPointsStruct[vertexCounter].normal = frontWidthNorm;
        pPointsStruct[vertexCounter].jointIndices[0] = currBoneIndex;
        pPointsStruct[vertexCounter].jointIndices[1] = currParentIndex;
        vertexCounter++;

        // Bot right triangle
        pPointsStruct[vertexCounter].position = frontWidth;
        pPointsStruct[vertexCounter].normal = frontWidthNorm;
        pPointsStruct[vertexCounter].jointIndices[0] = currBoneIndex;
        pPointsStruct[vertexCounter].jointIndices[1] = currParentIndex;
        vertexCounter++;

        pPointsStruct[vertexCounter].position = botWidth;
        pPointsStruct[vertexCounter].normal = botWidthNorm;
        pPointsStruct[vertexCounter].jointIndices[0] = currBoneIndex;
        pPointsStruct[vertexCounter].jointIndices[1] = currParentIndex;
        vertexCounter++;

        pPointsStruct[vertexCounter].position = boneLength;
        pPointsStruct[vertexCounter].normal = boneLengthNorm;
        pPointsStruct[vertexCounter].jointIndices[0] = currBoneIndex;
        pPointsStruct[vertexCounter].jointIndices[1] = currParentIndex;
        vertexCounter++;

        // Back Face
        // Top left triangle
        pPointsStruct[vertexCounter].position = origin;
        pPointsStruct[vertexCounter].normal = originNorm;
        pPointsStruct[vertexCounter].jointIndices[0] = currBoneIndex;
        pPointsStruct[vertexCounter].jointIndices[1] = currParentIndex;
        vertexCounter++;

        pPointsStruct[vertexCounter].position = topWidth;
        pPointsStruct[vertexCounter].normal = topWidthNorm;
        pPointsStruct[vertexCounter].jointIndices[0] = currBoneIndex;
        pPointsStruct[vertexCounter].jointIndices[1] = currParentIndex;
        vertexCounter++;

        pPointsStruct[vertexCounter].position = backWidth;
        pPointsStruct[vertexCounter].normal = backWidthNorm;
        pPointsStruct[vertexCounter].jointIndices[0] = currBoneIndex;
        pPointsStruct[vertexCounter].jointIndices[1] = currParentIndex;
        vertexCounter++;

        // Top right triangle
        pPointsStruct[vertexCounter].position = topWidth;
        pPointsStruct[vertexCounter].normal = topWidthNorm;
        pPointsStruct[vertexCounter].jointIndices[0] = currBoneIndex;
        pPointsStruct[vertexCounter].jointIndices[1] = currParentIndex;
        vertexCounter++;

        pPointsStruct[vertexCounter].position = boneLength;
        pPointsStruct[vertexCounter].normal = boneLengthNorm;
        pPointsStruct[vertexCounter].jointIndices[0] = currBoneIndex;
        pPointsStruct[vertexCounter].jointIndices[1] = currParentIndex;
        vertexCounter++;

        pPointsStruct[vertexCounter].position = backWidth;
        pPointsStruct[vertexCounter].normal = backWidthNorm;
        pPointsStruct[vertexCounter].jointIndices[0] = currBoneIndex;
        pPointsStruct[vertexCounter].jointIndices[1] = currParentIndex;
        vertexCounter++;

        // Bot left triangle
        pPointsStruct[vertexCounter].position = origin;
        pPointsStruct[vertexCounter].normal = originNorm;
        pPointsStruct[vertexCounter].jointIndices[0] = currBoneIndex;
        pPointsStruct[vertexCounter].jointIndices[1] = currParentIndex;
        vertexCounter++;

        pPointsStruct[vertexCounter].position = backWidth;
        pPointsStruct[vertexCounter].normal = backWidthNorm;
        pPointsStruct[vertexCounter].jointIndices[0] = currBoneIndex;
        pPointsStruct[vertexCounter].jointIndices[1] = currParentIndex;
        vertexCounter++;

        pPointsStruct[vertexCounter].position = botWidth;
        pPointsStruct[vertexCounter].normal = botWidthNorm;
        pPointsStruct[vertexCounter].jointIndices[0] = currBoneIndex;
        pPointsStruct[vertexCounter].jointIndices[1] = currParentIndex;
        vertexCounter++;

        // Bot right triangle
        pPointsStruct[vertexCounter].position = backWidth;
        pPointsStruct[vertexCounter].normal = backWidthNorm;
        pPointsStruct[vertexCounter].jointIndices[0] = currBoneIndex;
        pPointsStruct[vertexCounter].jointIndices[1] = currParentIndex;
        vertexCounter++;

        pPointsStruct[vertexCounter].position = boneLength;
        pPointsStruct[vertexCounter].normal = boneLengthNorm;
        pPointsStruct[vertexCounter].jointIndices[0] = currBoneIndex;
        pPointsStruct[vertexCounter].jointIndices[1] = currParentIndex;
        vertexCounter++;

        pPointsStruct[vertexCounter].position = botWidth;
        pPointsStruct[vertexCounter].normal = botWidthNorm;
        pPointsStruct[vertexCounter].jointIndices[0] = currBoneIndex;
        pPointsStruct[vertexCounter].jointIndices[1] = currParentIndex;
        vertexCounter++;
    }
}

typedef struct TFTexVertex
{
    float2 position;
    float2 texCoord;
} TFTexVertex;

static inline TFTexVertex texVertexMake(float2 p, float2 t)
{
    TFTexVertex v;
    v.position = p;
    v.texCoord = t;
    return v;
}

#define MAKETEXQUAD(vert, x0, y0, x1, y1, o)                       \
    vert[0] = texVertexMake(f2Make(x0 + o, y0 + o), f2Make(0, 0)); \
    vert[1] = texVertexMake(f2Make(x0 + o, y1 - o), f2Make(0, 1)); \
    vert[2] = texVertexMake(f2Make(x1 - o, y0 + o), f2Make(1, 0)); \
    vert[3] = texVertexMake(f2Make(x1 - o, y1 - o), f2Make(1, 1));

//----------------------------------------------------------------------------
// Intersection Helpers
//----------------------------------------------------------------------------

typedef struct TFRay
{
    float3Aligned origin;
    float3Aligned direction;
} TFRay;

static inline TFRay rayMake(float3 origin, float3 direction)
{
    TFRay r;
    r.origin = f3ToAligned(origin);
    r.direction = f3ToAligned(direction);
    return r;
}

static inline void rayTransform(TFRay* r, float4x4 m)
{
    float4 temp = f4x4Mulf4(m, f4Fromf3(f3FromAligned(r->origin), 1.0f));
    r->origin = f3ToAligned(f3DivScalar(f4GetXYZ(temp), temp.w));
    r->direction = f3ToAligned(f3Normalize(f4GetXYZ(f4x4Mulf4(m, f4Fromf3(f3FromAligned(r->direction), 0.0f)))));
}

static inline float3 rayEval(TFRay ray, float t) { return f3Add(f3FromAligned(ray.origin), f3MulScalar(f3FromAligned(ray.direction), t)); }

typedef struct TFPlane
{
    float3Aligned normal;
    float         distance;
} TFPlane;

static inline TFPlane planeMakeFromDir(float3 normal, float distance)
{
    TFPlane p;
    p.normal = f3ToAligned(normal);
    p.distance = distance;
    return p;
}

static inline TFPlane planeMakeFromPoint(float3 normal, float3 point)
{
    TFPlane p;
    p.normal = f3ToAligned(f3Normalize(normal));
    p.distance = f3Dot(normal, point);
    return p;
}

//----------------------------------------------------------------------------
// Cooperative Matrix Helpers
//----------------------------------------------------------------------------

typedef enum TFMatrixLayout
{
    TF_ROW_MAJOR = 0,
    TF_COLUMN_MAJOR = 1,
    TF_MUL_OPTIMAL = 2,
    TF_OUTER_PRODUCT_OPTIMAL = 3
} TFMatrixLayout;

typedef enum TFMatrixDataType
{
    TF_COOP_MAT_SINT16 = 2,
    TF_COOP_MAT_UINT16 = 3,
    TF_COOP_MAT_SINT32 = 4,
    TF_COOP_MAT_UINT32 = 5,
    TF_COOP_MAT_FLOAT16 = 7,
    TF_COOP_MAT_FLOAT32 = 8,
    TF_COOP_MAT_SINT8_T4_PACKED = 16,
    TF_COOP_MAT_UINT8_T4_PACKED = 17,
    TF_COOP_MAT_UINT8 = 18,
    TF_COOP_MAT_SINT8 = 19,
    TF_COOP_MAT_FLOAT_E4M3 = 20,
    TF_COOP_MAT_FLOAT_E5M2 = 21
} TFMatrixDataType;
