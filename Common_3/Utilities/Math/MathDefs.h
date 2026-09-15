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

#ifndef _MATHDEFS_H
#define _MATHDEFS_H

#ifndef ISPC
#include "stdint.h"
#ifndef uint
#define uint uint32_t
#endif // ifndef uint
#else  // ifndef ISPC
typedef uint8  uint8_t;
typedef int8   int8_t;
typedef uint16 uint16_t;
typedef int16  int16_t;
typedef uint   uint32_t;
typedef int    int32_t;
#endif

#if defined(__cplusplus) || defined(__STDC_VERSION__)
#include <float.h> // FLT_MAX, FLT_MIN
#endif

#if defined(_MSC_VER)
// msc
#define TF_MATH_ALIGNED(type)    __declspec(align(16)) type
#define TF_MATH_ALIGNED_TYPE_PRE __declspec(align(16))
#define TF_MATH_ALIGNED_TYPE_POST
#elif defined(__GNUC__)
// gcc/clang
#define TF_MATH_ALIGNED(type) type __attribute__((aligned(16)))
#define TF_MATH_ALIGNED_TYPE_PRE
#define TF_MATH_ALIGNED_TYPE_POST __attribute__((aligned(16)))
#else
#define TF_MATH_ALIGNED(type)
#define TF_MATH_ALIGNED_TYPE_PRE
#define TF_MATH_ALIGNED_TYPE_POST
#endif

#ifdef __cplusplus

#define VECTOR_PARAMS_2(T)       T x, T y
#define VECTOR_INIT_2(a, b, ...) x(a), y(b)
#define VECTOR_ASSIGN_2() \
    x = other.x;          \
    y = other.y;

#define VECTOR_PARAMS_3(T)          T x, T y, T z
#define VECTOR_INIT_3(a, b, c, ...) x(a), y(b), z(c)
#define VECTOR_ASSIGN_3() \
    x = other.x;          \
    y = other.y;          \
    z = other.z;

#define VECTOR_PARAMS_4(T)        T x, T y, T z, T w
#define VECTOR_INIT_4(a, b, c, d) x(a), y(b), z(c), w(d)
#define VECTOR_ASSIGN_4() \
    x = other.x;          \
    y = other.y;          \
    z = other.z;          \
    w = other.w;

#define DEFINE_VECTOR_CTORS(T, COUNT)                                                                            \
    constexpr inline T##COUNT(): VECTOR_INIT_##COUNT(0, 0, 0, 0){};                                              \
    constexpr inline T##COUNT(const T##COUNT& other): VECTOR_INIT_##COUNT(other.x, other.y, other.z, other.w) {} \
    constexpr inline T##COUNT(VECTOR_PARAMS_##COUNT(T)): VECTOR_INIT_##COUNT(x, y, z, w) {}                      \
    constexpr inline explicit T##COUNT(T s): VECTOR_INIT_##COUNT(s, s, s, s) {}                                  \
    inline T&                  operator[](int32_t i) { return ((T*)this)[i]; }                                   \
    inline const T&            operator[](int32_t i) const { return ((T*)this)[i]; }                             \
    constexpr inline T##COUNT& operator=(const T##COUNT& other) { VECTOR_ASSIGN_##COUNT() return (*this); }

#define MATRIX_INIT_COLS_2(a, b, ...) \
    v { a, b }

#define MATRIX_ASSIGN_COLS_2() \
    v[0] = other.v[0];         \
    v[1] = other.v[1];
#define MATRIX_ASSIGN_COLS_3() \
    v[0] = other.v[0];         \
    v[1] = other.v[1];         \
    v[2] = other.v[2];
#define MATRIX_ASSIGN_COLS_4() \
    v[0] = other.v[0];         \
    v[1] = other.v[1];         \
    v[2] = other.v[2];         \
    v[3] = other.v[3];

#define MATRIX_COL_PARAMS_2(T) T col0, T col1

#define MATRIX_INIT_COLS_3(a, b, c, ...) \
    v { a, b, c }
#define MATRIX_COL_PARAMS_3(T) T col0, T col1, T col2

#define MATRIX_INIT_COLS_4(a, b, c, d) \
    v { a, b, c, d }
#define MATRIX_COL_PARAMS_4(T)             T col0, T col1, T col2, T col3

#define DEFINE_MATRIX_CTORS(T, COLS, ROWS) DEFINE_MATRIX_CTORS_NAMED(T##COLS##x##ROWS, T, COLS, ROWS)

#define DEFINE_MATRIX_CTORS_NAMED(NAME, T, COLS, ROWS)                                                                             \
    inline NAME(): v{ T##ROWS(0) } {}                                                                                              \
    inline NAME(const NAME& other): MATRIX_INIT_COLS_##COLS(other.v[0], other.v[1], other.v[2], other.v[3]) {}                     \
    inline NAME(MATRIX_COL_PARAMS_##COLS(T##ROWS)): MATRIX_INIT_COLS_##COLS(col0, col1, col2, col3) {}                             \
    explicit inline NAME(T scalar): MATRIX_INIT_COLS_##COLS(T##ROWS(scalar), T##ROWS(scalar), T##ROWS(scalar), T##ROWS(scalar)) {} \
    inline T##ROWS&       operator[](int32_t i) { return *(T##ROWS*)&(v[i]); }                                                     \
    inline const T##ROWS& operator[](int32_t i) const { return *(T##ROWS*)&(v[i]); }                                               \
    inline NAME&          operator=(const NAME& other) { MATRIX_ASSIGN_COLS_##COLS() return *this; }

#endif // __cplusplus

struct float2;
struct float3;
struct float4;
struct double2;
struct double3;
struct double4;
struct int2;
struct int3;
struct int4;
struct uint2;
struct uint3;
struct uint4;
struct float3Aligned;
struct double3Aligned;
struct int3Aligned;
struct uint3Aligned;

struct float2
{
#ifdef __cplusplus
    DEFINE_VECTOR_CTORS(float, 2)

    explicit inline float2(const double2& v);
    explicit inline float2(const int2& v);
    explicit inline float2(const uint2& v);

    inline static float2 zero() { return float2(0, 0); }
    inline static float2 one() { return float2(1, 1); }
#endif
    float x;
    float y;
};

struct float3
{
#ifdef __cplusplus
    DEFINE_VECTOR_CTORS(float, 3)
    inline float2 getXY() { return float2(x, y); }

    explicit inline float3(const double3& v);
    explicit inline float3(const int3& v);
    explicit inline float3(const uint3& v);

    inline static float3 zero() { return float3(0, 0, 0); }
    inline static float3 one() { return float3(1, 1, 1); }

    inline static float3 xAxis() { return float3(1, 0, 0); }
    inline static float3 yAxis() { return float3(0, 1, 0); }
    inline static float3 zAxis() { return float3(0, 0, 1); }
#endif
    float x;
    float y;
    float z;
};

TF_MATH_ALIGNED_TYPE_PRE struct float3Aligned
{
#ifdef __cplusplus
    constexpr inline float3Aligned(): x(0), y(0), z(0), _w(0) {}
    constexpr inline float3Aligned(const float3Aligned& other): x(other.x), y(other.y), z(other.z), _w(0) {}
    constexpr inline float3Aligned(float x, float y, float z): x(x), y(y), z(z), _w(0) {}
    constexpr inline float3Aligned(float s): x(s), y(s), z(s), _w(0) {}
    inline float&                   operator[](int32_t i) { return ((float*)this)[i]; }
    inline const float&             operator[](int32_t i) const { return ((float*)this)[i]; }
    constexpr inline float3Aligned& operator=(const float3Aligned& other)
    {
        x = other.x;
        y = other.y;
        z = other.z;
        _w = 0.0f;
        return *this;
    }
    float3Aligned(const float3& f): x(f.x), y(f.y), z(f.z), _w(0) {}
    operator float3() const { return float3(x, y, z); }

    inline float2&       getXY() { return *(float2*)this; } //-V::1027
    inline const float2& getXY() const { return *(float2*)this; }

    inline static float3Aligned zero() { return float3Aligned(0, 0, 0); }
    inline static float3Aligned one() { return float3Aligned(1, 1, 1); }
    inline static float3Aligned xAxis() { return float3Aligned(1, 0, 0); }
    inline static float3Aligned yAxis() { return float3Aligned(0, 1, 0); }
    inline static float3Aligned zAxis() { return float3Aligned(0, 0, 1); }
#endif // __cplusplus

    float x;
    float y;
    float z;
    float _w; // padding
} TF_MATH_ALIGNED_TYPE_POST;

struct float4
{
#ifdef __cplusplus
    DEFINE_VECTOR_CTORS(float, 4)
    constexpr inline float4(float3 xyz): x(xyz.x), y(xyz.y), z(xyz.z), w(0) {}
    explicit constexpr inline float4(float3Aligned xyz): x(xyz.x), y(xyz.y), z(xyz.z), w(0) {}
    constexpr inline float4(float3 xyz, float w): x(xyz.x), y(xyz.y), z(xyz.z), w(w) {}

    explicit inline float4(const double4& v);
    explicit inline float4(const int4& v);
    explicit inline float4(const uint4& v);

    inline float2&       getXY() { return *(float2*)this; }
    inline float3&       getXYZ() { return *(float3*)this; }
    inline const float2& getXY() const { return *(float2*)this; }
    inline const float3& getXYZ() const { return *(float3*)this; }

    inline static float4 zero() { return float4(0, 0, 0, 0); }
    inline static float4 one() { return float4(1, 1, 1, 1); }

    inline static float4 xAxis() { return float4(1, 0, 0, 0); }
    inline static float4 yAxis() { return float4(0, 1, 0, 0); }
    inline static float4 zAxis() { return float4(0, 0, 1, 0); }
    inline static float4 wAxis() { return float4(0, 0, 0, 1); }
#endif
    float x;
    float y;
    float z;
    float w;
};

struct quat
{
#ifdef __cplusplus
    inline quat(): x(0), y(0), z(0), w(0) {}
    inline quat(float x, float y, float z, float w): x(x), y(y), z(z), w(w) {}
    explicit inline quat(float s): x(s), y(s), z(s), w(s) {}
    explicit inline quat(float4 v): x(v.x), y(v.y), z(v.z), w(v.w) {}

    explicit operator float4() const { return float4(x, y, z, w); }

    inline float&       operator[](int32_t i) { return ((float*)this)[i]; }
    inline const float& operator[](int32_t i) const { return ((float*)this)[i]; }

    static inline quat identity() { return quat(0.0f, 0.0f, 0.0f, 1.0f); }
#endif

    float x;
    float y;
    float z;
    float w;
};

typedef struct float4 TF_MATH_ALIGNED_TYPE_PRE float4Aligned TF_MATH_ALIGNED_TYPE_POST;

struct double2
{
#ifdef __cplusplus
    DEFINE_VECTOR_CTORS(double, 2)

    explicit inline double2(const float2& v);
    explicit inline double2(const int2& v);
    explicit inline double2(const uint2& v);
#endif
    double x;
    double y;
};

struct double3
{
#ifdef __cplusplus
    DEFINE_VECTOR_CTORS(double, 3)

    explicit inline double3(const float3& v);
    explicit inline double3(const int3& v);
    explicit inline double3(const uint3& v);
#endif
    double x;
    double y;
    double z;
};

TF_MATH_ALIGNED_TYPE_PRE struct double3Aligned
{
#ifdef __cplusplus
    constexpr inline double3Aligned(): x(0), y(0), z(0), _w(0) {}
    constexpr inline double3Aligned(const double3Aligned& other): x(other.x), y(other.y), z(other.z), _w(0) {}
    constexpr inline double3Aligned(double x, double y, double z): x(x), y(y), z(z), _w(0) {}
    constexpr inline double3Aligned(double s): x(s), y(s), z(s), _w(0) {}
    inline double&       operator[](int32_t i) { return ((double*)this)[i]; }
    inline const double& operator[](int32_t i) const { return ((double*)this)[i]; }
    double3Aligned(const double3& f): x(f.x), y(f.y), z(f.z), _w(0) {}
    operator double3() const { return double3(x, y, z); }

    constexpr inline double3Aligned& operator=(const double3Aligned& other)
    {
        x = other.x;
        y = other.y;
        z = other.z;
        _w = 0.0f;
        return *this;
    }

    inline double2& getXY() { return *(double2*)this; }

    inline static double3Aligned zero() { return double3Aligned(0, 0, 0); }
    inline static double3Aligned one() { return double3Aligned(1, 1, 1); }
#endif // __cplusplus
    double x;
    double y;
    double z;
    double _w; // padding
} TF_MATH_ALIGNED_TYPE_POST;

TF_MATH_ALIGNED_TYPE_PRE struct double4
{
#ifdef __cplusplus
    DEFINE_VECTOR_CTORS(double, 4)
    explicit constexpr inline double4(double3Aligned xyz): x(xyz.x), y(xyz.y), z(xyz.z), w(0) {}

    explicit inline double4(const float4& v);
    explicit inline double4(const int4& v);
    explicit inline double4(const uint4& v);
#endif
    double x;
    double y;
    double z;
    double w;
} TF_MATH_ALIGNED_TYPE_POST;

typedef struct TF_MATH_ALIGNED_TYPE_PRE double4 double4Aligned TF_MATH_ALIGNED_TYPE_POST;

struct int2
{
#ifdef __cplusplus
    DEFINE_VECTOR_CTORS(int, 2)

    explicit inline int2(const float2& v);
    explicit inline int2(const double2& v);
    explicit inline int2(const uint2& v);
#endif
    int x;
    int y;
};

struct int3
{
#ifdef __cplusplus
    DEFINE_VECTOR_CTORS(int, 3)

    explicit inline int3(const float3& v);
    explicit inline int3(const double3& v);
    explicit inline int3(const uint3& v);
#endif
    int x;
    int y;
    int z;
};

TF_MATH_ALIGNED_TYPE_PRE struct int3Aligned
{
#ifdef __cplusplus
    constexpr inline int3Aligned(): x(0), y(0), z(0), _w(0) {}
    constexpr inline int3Aligned(const int3Aligned& other): x(other.x), y(other.y), z(other.z), _w(0) {}
    constexpr inline int3Aligned(int x, int y, int z): x(x), y(y), z(z), _w(0) {}
    constexpr inline int3Aligned(int s): x(s), y(s), z(s), _w(0) {}
    inline int&       operator[](int32_t i) { return ((int*)this)[i]; }
    inline const int& operator[](int32_t i) const { return ((int*)this)[i]; }
    int3Aligned(const int3& f): x(f.x), y(f.y), z(f.z), _w(0) {}
    operator int3&() { return *(int3*)this; }

    inline int3Aligned& operator=(const int3Aligned& other)
    {
        x = other.x;
        y = other.y;
        z = other.z;
        _w = 0.0f;
        return *this;
    }

    inline int2& getXY() { return *(int2*)this; }

    inline static int3Aligned zero() { return int3Aligned(0, 0, 0); }
    inline static int3Aligned one() { return int3Aligned(1, 1, 1); }
#endif // __cplusplus
    int x;
    int y;
    int z;
    int _w; // padding
} TF_MATH_ALIGNED_TYPE_POST;

struct int4
{
#ifdef __cplusplus
    DEFINE_VECTOR_CTORS(int, 4)
    explicit constexpr inline int4(int3Aligned xyz): x(xyz.x), y(xyz.y), z(xyz.z), w(0) {}

    explicit inline int4(const float4& v);
    explicit inline int4(const double4& v);
    explicit inline int4(const uint4& v);
#endif
    int x;
    int y;
    int z;
    int w;
};

typedef struct int4 TF_MATH_ALIGNED_TYPE_PRE int4Aligned TF_MATH_ALIGNED_TYPE_POST;

struct uint2
{
#ifdef __cplusplus
    DEFINE_VECTOR_CTORS(uint, 2)

    explicit inline uint2(const float2& v);
    explicit inline uint2(const double2& v);
    explicit inline uint2(const int2& v);
#endif
    uint x;
    uint y;
};

struct uint3
{
#ifdef __cplusplus
    DEFINE_VECTOR_CTORS(uint, 3)

    explicit inline uint3(const float3& v);
    explicit inline uint3(const double3& v);
    explicit inline uint3(const int3& v);
#endif
    uint x;
    uint y;
    uint z;
};

TF_MATH_ALIGNED_TYPE_PRE struct uint3Aligned
{
#ifdef __cplusplus
    constexpr inline uint3Aligned(): x(0), y(0), z(0), _w(0) {}
    constexpr inline uint3Aligned(const uint3Aligned& other): x(other.x), y(other.y), z(other.z), _w(0) {}
    constexpr inline uint3Aligned(uint x, uint y, uint z): x(x), y(y), z(z), _w(0) {}
    constexpr inline uint3Aligned(uint s): x(s), y(s), z(s), _w(0) {}
    inline uint&       operator[](int32_t i) { return ((uint*)this)[i]; }
    inline const uint& operator[](int32_t i) const { return ((uint*)this)[i]; }
    uint3Aligned(const uint3& f): x(f.x), y(f.y), z(f.z), _w(0) {}
                         operator uint3() const { return uint3(x, y, z); }
    inline uint3Aligned& operator=(const uint3Aligned& other)
    {
        x = other.x;
        y = other.y;
        z = other.z;
        _w = 0.0f;
        return *this;
    }

    inline uint2& getXY() { return *(uint2*)this; }

    inline static uint3Aligned zero() { return uint3Aligned(0, 0, 0); }
    inline static uint3Aligned one() { return uint3Aligned(1, 1, 1); }
#endif // __cplusplus
    uint x;
    uint y;
    uint z;
    uint _w; // padding
} TF_MATH_ALIGNED_TYPE_POST;

struct uint4
{
#ifdef __cplusplus
    DEFINE_VECTOR_CTORS(uint, 4)
    explicit constexpr inline uint4(uint3Aligned xyz): x(xyz.x), y(xyz.y), z(xyz.z), w(0) {}

    explicit inline uint4(const float4& v);
    explicit inline uint4(const double4& v);
    explicit inline uint4(const int4& v);
#endif
    uint x;
    uint y;
    uint z;
    uint w;
};

typedef struct uint4 TF_MATH_ALIGNED_TYPE_PRE uint4Aligned TF_MATH_ALIGNED_TYPE_POST;

struct float2x2
{
#ifdef __cplusplus
    DEFINE_MATRIX_CTORS(float, 2, 2)

    static float2x2 identity();
#endif
    struct float2 v[2];
};

struct float3x2
{
#ifdef __cplusplus
    DEFINE_MATRIX_CTORS(float, 3, 2)
#endif
    struct float2 v[3];
};

struct float4x2
{
#ifdef __cplusplus
    DEFINE_MATRIX_CTORS(float, 4, 2)
#endif
    struct float2 v[4];
};

struct float2x3
{
#ifdef __cplusplus
    DEFINE_MATRIX_CTORS(float, 2, 3)
#endif
    struct float3 v[2];
};

struct float3x3
{
#ifdef __cplusplus
    DEFINE_MATRIX_CTORS(float, 3, 3)

    static float3x3 identity();
    static float3x3 rotationZYX(float3 radiansXYZ);
    static float3x3 rotationQuat(quat q);
#endif
    struct float3 v[3];
};

TF_MATH_ALIGNED_TYPE_PRE struct float3x3Aligned
{
#ifdef __cplusplus
    DEFINE_MATRIX_CTORS_NAMED(float3x3Aligned, float, 3, 3)

    inline float3x3Aligned(const float3x3& m): v{ m[0], m[1], m[2] } {}
    inline operator float3x3() const { return float3x3((float3)v[0], (float3)v[1], (float3)v[2]); }

    static float3x3Aligned identity();
    static float3x3Aligned rotationZYX(float3 radiansXYZ);
    static float3x3Aligned rotationQuat(quat q);
#endif
    struct float3Aligned v[3];
} TF_MATH_ALIGNED_TYPE_POST;

struct float4x3
{
#ifdef __cplusplus
    DEFINE_MATRIX_CTORS(float, 4, 3)
#endif
    struct float3 v[4];
};

struct float2x4
{
#ifdef __cplusplus
    DEFINE_MATRIX_CTORS(float, 2, 4)
#endif
    struct float4 v[2];
};

struct float3x4
{
#ifdef __cplusplus
    DEFINE_MATRIX_CTORS(float, 3, 4)
#endif
    struct float4 v[3];
};

struct TF_MATH_ALIGNED_TYPE_PRE float4x4
{
#ifdef __cplusplus
    DEFINE_MATRIX_CTORS(float, 4, 4)

    inline float4x4(float3x3 m3, float3 translation):
        v{ float4(m3[0], 0.0f), float4(m3[1], 0.0f), float4(m3[2], 0.0f), float4(translation, 1.0f) }
    {
    }

    static float4x4 identity();
    static float4x4 perspectiveLH(float fovRadians, float aspectInverse, float zNear, float zFar);
    static float4x4 perspectiveRH(float fovRadians, float aspectInverse, float zNear, float zFar);
    static float4x4 perspectiveLH_ReverseZ(float fovRadians, float aspectInverse, float zNear, float zFar);
    static float4x4 perspectiveLH_AsymmetricFov(float leftDegrees, float rightDegrees, float upDegrees, float downDegrees, float zNear,
                                                float zFar, bool isDegrees);
    static float4x4 perspectiveLH_ReverseZ_AsymmetricFov(float leftDegrees, float rightDegrees, float upDegrees, float downDegrees,
                                                         float zNear, float zFar, bool isDegrees);
    static float4x4 orthographicLH(float left, float right, float bottom, float top, float zNear, float zFar);
    static float4x4 orthographicRH(float left, float right, float bottom, float top, float zNear, float zFar);
    static float4x4 orthographicLH_ReverseZ(float left, float right, float bottom, float top, float zNear, float zFar);
    static float4x4 cubeProjectionLH(float zNear, float zFar);
    static float4x4 cubeProjectionRH(float zNear, float zFar);
    static float4x4 cubeView(uint side);
    static void     extractFrustumClipPlanes(float4x4 vp, float4& rplane, float4& lplane, float4& tplane, float4& bplane, float4& fplane,
                                             float4& nplane, bool normalizePlanes);
    static float4x4 rotationX(float radians);
    static float4x4 rotationY(float radians);
    static float4x4 rotationZ(float radians);
    static float4x4 rotation(float radians, float3 unitVec);
    static float4x4 rotationYX(float radiansY, float radiansX);
    static float4x4 rotationXY(float radiansX, float radiansY);
    static float4x4 rotationZYX(float3 radiansXYZ);
    static float4x4 rotationQuat(quat q);
    static float4x4 scale(float3 scale);
    static float4x4 translation(float3 v);
    static float4x4 lookAtLH(float3 eyePos, float3 lookAtPos, float3 upVec);
    static float4x4 lookAtRH(float3 eyePos, float3 lookAtPos, float3 upVec);
    static float4x4 frustumLH(float left, float right, float bottom, float top, float zNear, float zFar);

    float4x4& setTranslation(float3 t);

#endif
    struct float4 v[4];
} TF_MATH_ALIGNED_TYPE_POST;

struct double2x2
{
#ifdef __cplusplus
    static double2x2 identity();
#endif
    struct double2 v[2];
};

struct double3x2
{
    struct double2 v[3];
};

struct double4x2
{
    struct double2 v[4];
};

struct double2x3
{
    struct double3 v[2];
};

struct double3x3
{
#ifdef __cplusplus
    DEFINE_MATRIX_CTORS(double, 3, 3)

    static double3x3 identity();
    static double3x3 rotationZYX(double3 radiansXYZ);
    static double3x3 rotationQuat(quat q);
#endif
    struct double3 v[3];
};

struct TF_MATH_ALIGNED_TYPE_PRE double3x3Aligned
{
#ifdef __cplusplus
    DEFINE_MATRIX_CTORS_NAMED(double3x3Aligned, double, 3, 3)

    inline double3x3Aligned(const double3x3& m): v{ m[0], m[1], m[2] } {}
    inline operator double3x3() const { return double3x3((double3)v[0], (double3)v[1], (double3)v[2]); }
#endif
    struct double3Aligned v[3];
} TF_MATH_ALIGNED_TYPE_POST;

struct double4x3
{
    struct double3 v[4];
};

struct double2x4
{
    struct double4 v[2];
};

struct double3x4
{
    struct double4 v[3];
};

struct TF_MATH_ALIGNED_TYPE_PRE double4x4
{
#ifdef __cplusplus
    static double4x4 identity();
    static double4x4 perspectiveLH(double fovRadians, double aspectInverse, double zNear, double zFar);
    static double4x4 perspectiveRH(double fovRadians, double aspectInverse, double zNear, double zFar);
    static double4x4 perspectiveLH_ReverseZ(double fovRadians, double aspectInverse, double zNear, double zFar);
    static double4x4 orthographicLH(double left, double right, double bottom, double top, double zNear, double zFar);
    static double4x4 orthographicRH(double left, double right, double bottom, double top, double zNear, double zFar);
    static double4x4 orthographicLH_ReverseZ(double left, double right, double bottom, double top, double zNear, double zFar);
    static double4x4 cubeProjectionLH(double zNear, double zFar);
    static double4x4 cubeProjectionRH(double zNear, double zFar);
    static double4x4 cubeView(uint side);

    static double4x4 lookAtLH(double3 eyePos, double3 lookAtPos, double3 upVec);
    static double4x4 lookAtRH(double3 eyePos, double3 lookAtPos, double3 upVec);
#endif
    struct double4 v[4];
} TF_MATH_ALIGNED_TYPE_POST;

struct TFAABB
{
#ifdef __cplusplus
    inline TFAABB(): min(float3Aligned(FLT_MAX)), max(float3Aligned(-FLT_MAX)) {}
    inline TFAABB(float3Aligned min, float3Aligned max): min(min), max(max) {}
#endif
    struct float3Aligned min;
    struct float3Aligned max;
};

struct half
{
    uint16_t sh;
#ifdef __cplusplus
    half() = default;
#if defined(__clang__)
// Some versions of clang break this function when optimizing for release
#pragma clang optimize off
#endif
    inline explicit half(float x)
    {
        union
        {
            float        floatI;
            unsigned int i;
        };
        floatI = x;

        //	unsigned int i = *((unsigned int *) &mX);
        int e = ((i >> 23) & 0xFF) - 112;
        int m = i & 0x007FFFFF;

        sh = (i >> 16) & 0x8000;
        if (e <= 0)
        {
            // Denorm
            m = ((m | 0x00800000) >> (1 - e)) + 0x1000;
            sh |= (m >> 13);
        }
        else if (e == 143)
        {
            sh |= 0x7C00;
            if (m != 0)
            {
                // NAN
                m >>= 13;
                sh |= m | (m == 0);
            }
        }
        else
        {
            m += 0x1000;
            if (m & 0x00800000)
            {
                // Mantissa overflow
                m = 0;
                e++;
            }
            if (e >= 31)
            {
                // Exponent overflow
                sh |= 0x7C00;
            }
            else
            {
                sh |= (e << 10) | (m >> 13);
            }
        }
    }
#if defined(__clang__)
#pragma clang optimize on
#endif

    inline operator float() const
    {
        union
        {
            unsigned int s;
            float        result;
        };

        s = (sh & 0x8000) << 16;
        unsigned int e = (sh >> 10) & 0x1F;
        unsigned int m = sh & 0x03FF;

        if (e == 0)
        {
            // +/- 0
            if (m == 0)
                return result;

            // Denorm
            while ((m & 0x0400) == 0)
            {
                m += m;
                e--;
            }
            e++;
            m &= ~0x0400;
        }
        else if (e == 31)
        {
            // INF / NAN
            s |= 0x7F800000 | (m << 13);
            return result;
        }

        s |= ((e + 112) << 23) | (m << 13);

        return result;
    }
#endif
};

#if !defined(__cplusplus) && !defined(ISPC)

// We need to typedef structs in C

typedef struct float2         float2;
typedef struct float3         float3;
typedef struct float4         float4;
typedef struct float3Aligned  float3Aligned;
typedef struct double2        double2;
typedef struct double3        double3;
typedef struct double4        double4;
typedef struct double3Aligned double3Aligned;
typedef struct uint2          uint2;
typedef struct uint3          uint3;
typedef struct uint4          uint4;
typedef struct uint3Aligned   uint3Aligned;
typedef struct int2           int2;
typedef struct int3           int3;
typedef struct int4           int4;
typedef struct int3Aligned    int3Aligned;

typedef struct float2x2        float2x2;
typedef struct float3x2        float3x2;
typedef struct float4x2        float4x2;
typedef struct float2x3        float2x3;
typedef struct float3x3        float3x3;
typedef struct float3x3Aligned float3x3Aligned;
typedef struct float4x3        float4x3;
typedef struct float2x4        float2x4;
typedef struct float3x4        float3x4;
typedef struct float4x4        float4x4;

typedef struct double2x2        double2x2;
typedef struct double3x2        double3x2;
typedef struct double4x2        double4x2;
typedef struct double2x3        double2x3;
typedef struct double3x3        double3x3;
typedef struct double3x3Aligned double3x3Aligned;
typedef struct double4x3        double4x3;
typedef struct double2x4        double2x4;
typedef struct double3x4        double3x4;
typedef struct double4x4        double4x4;

typedef struct TFAABB TFAABB;
typedef struct quat   quat;

#endif // __STDC_VERSION__

// Alias for consistency with FSL matrix notation

typedef float2x2 f2x2;
typedef float3x2 f3x2;
typedef float4x2 f4x2;
typedef float2x3 f2x3;
typedef float3x3 f3x3;
typedef float4x3 f4x3;
typedef float2x4 f2x4;
typedef float3x4 f3x4;
typedef float4x4 f4x4;

typedef double2x2 d2x2;
typedef double3x2 d3x2;
typedef double4x2 d4x2;
typedef double2x3 d2x3;
typedef double3x3 d3x3;
typedef double4x3 d4x3;
typedef double2x4 d2x4;
typedef double3x4 d3x4;
typedef double4x4 d4x4;

// Portable initializers, mostly used for code generation, or C constructors

#if !defined(__cplusplus) && !defined(ISPC) && !defined(FSL_SHADER_LIB) && !defined(inline)
#define TF_C_MATH_STATIC_INLINE
#define inline static inline
#endif

inline float2 make_float2(float x, float y)
{
    float2 v;
    v.x = x;
    v.y = y;
    return v;
}
inline float3 make_float3(float x, float y, float z)
{
    float3 v;
    v.x = x;
    v.y = y;
    v.z = z;
    return v;
}
inline float3Aligned make_float3Aligned(float x, float y, float z)
{
    float3Aligned v;
    v.x = x;
    v.y = y;
    v.z = z;
    return v;
}
inline float4 make_float4(float x, float y, float z, float w)
{
    float4 v;
    v.x = x;
    v.y = y;
    v.z = z;
    v.w = w;
    return v;
}
inline quat make_quat(float x, float y, float z, float w)
{
    quat q;
    q.x = x;
    q.y = y;
    q.z = z;
    q.w = w;
    return q;
}
inline double2 make_double2(double x, double y)
{
    double2 v;
    v.x = x;
    v.y = y;
    return v;
}
inline double3 make_double3(double x, double y, double z)
{
    double3 v;
    v.x = x;
    v.y = y;
    v.z = z;
    return v;
}
inline double3Aligned make_double3Aligned(double x, double y, double z)
{
    double3Aligned v;
    v.x = x;
    v.y = y;
    v.z = z;
    return v;
}
inline double4 make_double4(double x, double y, double z, double w)
{
    double4 v;
    v.x = x;
    v.y = y;
    v.z = z;
    v.w = w;
    return v;
}
inline int2 make_int2(int x, int y)
{
    int2 v;
    v.x = x;
    v.y = y;
    return v;
}
inline int3 make_int3(int x, int y, int z)
{
    int3 v;
    v.x = x;
    v.y = y;
    v.z = z;
    return v;
}
inline int3Aligned make_int3Aligned(int x, int y, int z)
{
    int3Aligned v;
    v.x = x;
    v.y = y;
    v.z = z;
    return v;
}
inline int4 make_int4(int x, int y, int z, int w)
{
    int4 v;
    v.x = x;
    v.y = y;
    v.z = z;
    v.w = w;
    return v;
}
inline uint2 make_uint2(uint x, uint y)
{
    uint2 v;
    v.x = x;
    v.y = y;
    return v;
}
inline uint3 make_uint3(uint x, uint y, uint z)
{
    uint3 v;
    v.x = x;
    v.y = y;
    v.z = z;
    return v;
}
inline uint3Aligned make_uint3Aligned(uint x, uint y, uint z)
{
    uint3Aligned v;
    v.x = x;
    v.y = y;
    v.z = z;
    return v;
}
inline uint4 make_uint4(uint x, uint y, uint z, uint w)
{
    uint4 v;
    v.x = x;
    v.y = y;
    v.z = z;
    v.w = w;
    return v;
}

inline float3 f3FromAligned(float3Aligned a)
{
    float3 r;
    r.x = a.x;
    r.y = a.y;
    r.z = a.z;

    return r;
}
inline float3Aligned f3ToAligned(float3 a)
{
    float3Aligned r;
    r.x = a.x;
    r.y = a.y;
    r.z = a.z;

    return r;
}

#ifdef TF_C_MATH_STATIC_INLINE
#undef inline
#undef TF_C_MATH_STATIC_INLINE
#endif

#endif // _MATHDEFS_H