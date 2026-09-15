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

#ifndef _ISPC_H
#define _ISPC_H

#define UINT_MAX 4294967295

#define _DECL_TYPES(EXPR) \
    EXPR(int)             \
    EXPR(int2)            \
    EXPR(int3)            \
    EXPR(int4)            \
    EXPR(uint)            \
    EXPR(uint2)           \
    EXPR(uint3)           \
    EXPR(uint4)           \
    EXPR(float)           \
    EXPR(float2)          \
    EXPR(float3)          \
    EXPR(float4)
/*
EXPR(half) \
EXPR(half2) \
EXPR(half3) \
EXPR(half4) \
*/

// TODO
/*#define make_f2x2_cols(C0, C1) transpose(f2x2(C0, C1))
#define make_f2x2_rows(R0, R1) f2x2(R0, R1)
#define make_f2x2_col_elems(E00, E01, E10, E11) f2x2(E00, E10, E01, E11)
#define make_f2x3_cols(C0, C1) transpose(f3x2(C0, C1))
#define make_f2x3_rows(R0, R1, R2) f2x3(R0, R1, R2)
#define make_f2x3_col_elems(E00, E01, E10, E11, E20, E21) f2x3(E00, E10, E20, E01, E11, E21)
#define make_f3x3_row_elems  f3x3

inline f3x2 make_f3x2_cols(float2 c0, float2 c1, float2 c2)
{ return transpose(f2x3(c0, c1, c2)); }


#define make_f3x3_cols(C0, C1, C2) transpose(float3x3(C0, C1, C2))
inline f3x3 make_f3x3_rows(float3 r0, float3 r1, float3 r2)
{ return f3x3(r0, r1, r2); }

#define make_f4x4_col_elems(E00, E01, E02, E03, E10, E11, E12, E13, E20, E21, E22, E23, E30, E31, E32, E33) \
    f4x4(E00, E10, E20, E30, E01, E11, E21, E31, E02, E12, E22, E32, E03, E13, E23, E33)
#define make_f4x4_row_elems f4x4
#define make_f4x4_cols(C0, C1, C2, C3) transpose(f4x4(C0, C1, C2, C3))
*/

// Conversions

inline uint2 make_uint2(int2 xy) { return make_uint2((uint)(int)xy.x, (uint)(int)xy.y); }
inline uint2 make_uint2(float2 xy) { return make_uint2((uint)(int)xy.x, (uint)(int)xy.y); }
inline uint2 make_uint2(double2 xy) { return make_uint2((uint)(int)xy.x, (uint)(int)xy.y); }

inline int2 make_int2(uint2 xy) { return make_int2((int)xy.x, (int)xy.y); }
inline int2 make_int2(float2 xy) { return make_int2((int)xy.x, (int)xy.y); }
inline int2 make_int2(double2 xy) { return make_int2((int)xy.x, (int)xy.y); }

inline float2 make_float2(uint2 xy) { return make_float2((float)(int)xy.x, (float)(int)xy.y); }
inline float2 make_float2(int2 xy) { return make_float2((float)(int)xy.x, (float)(int)xy.y); }
inline float2 make_float2(double2 xy) { return make_float2((float)xy.x, (float)xy.y); }

inline double2 make_double2(uint2 xy) { return make_double2((double)(int)xy.x, (double)(int)xy.y); }
inline double2 make_double2(int2 xy) { return make_double2((double)(int)xy.x, (double)(int)xy.y); }
inline double2 make_double2(float2 xy) { return make_double2((double)xy.x, (double)xy.y); }

inline uint3 make_uint3(int3 xyz) { return make_uint3((uint)(int)xyz.x, (uint)(int)xyz.y, (uint)(int)xyz.z); }
inline uint3 make_uint3(float3 xyz) { return make_uint3((uint)(int)xyz.x, (uint)(int)xyz.y, (uint)(int)xyz.z); }
inline uint3 make_uint3(double3 xyz) { return make_uint3((uint)(int)xyz.x, (uint)(int)xyz.y, (uint)(int)xyz.z); }
inline uint3 make_uint3(float3Aligned xyz) { return make_uint3((uint)(int)xyz.x, (uint)(int)xyz.y, (uint)(int)xyz.z); }
inline uint3 make_uint3(double3Aligned xyz) { return make_uint3((uint)(int)xyz.x, (uint)(int)xyz.y, (uint)(int)xyz.z); }
inline uint3 make_uint3(uint3Aligned xyz) { return make_uint3(xyz.x, xyz.y, xyz.z); }
inline uint3 make_uint3(int3Aligned xyz) { return make_uint3((uint)(int)xyz.x, (uint)(int)xyz.y, (uint)(int)xyz.z); }

inline uint3Aligned make_uint3Aligned(uint3 xyz) { return make_uint3Aligned(xyz.x, xyz.y, xyz.z); }
inline uint3Aligned make_uint3Aligned(int3 xyz) { return make_uint3Aligned((uint)(int)xyz.x, (uint)(int)xyz.y, (uint)(int)xyz.z); }
inline uint3Aligned make_uint3Aligned(float3 xyz) { return make_uint3Aligned((uint)(int)xyz.x, (uint)(int)xyz.y, (uint)(int)xyz.z); }
inline uint3Aligned make_uint3Aligned(double3 xyz) { return make_uint3Aligned((uint)(int)xyz.x, (uint)(int)xyz.y, (uint)(int)xyz.z); }
inline uint3Aligned make_uint3Aligned(float3Aligned xyz) { return make_uint3Aligned((uint)(int)xyz.x, (uint)(int)xyz.y, (uint)(int)xyz.z); }
inline uint3Aligned make_uint3Aligned(double3Aligned xyz)
{
    return make_uint3Aligned((uint)(int)xyz.x, (uint)(int)xyz.y, (uint)(int)xyz.z);
}
inline uint3Aligned make_uint3Aligned(int3Aligned xyz) { return make_uint3Aligned((uint)(int)xyz.x, (uint)(int)xyz.y, (uint)(int)xyz.z); }

inline int3 make_int3(uint3 xyz) { return make_int3((int)xyz.x, (int)xyz.y, (int)xyz.z); }
inline int3 make_int3(float3 xyz) { return make_int3((int)xyz.x, (int)xyz.y, (int)xyz.z); }
inline int3 make_int3(double3 xyz) { return make_int3((int)xyz.x, (int)xyz.y, (int)xyz.z); }
inline int3 make_int3(float3Aligned xyz) { return make_int3((int)xyz.x, (int)xyz.y, (int)xyz.z); }
inline int3 make_int3(double3Aligned xyz) { return make_int3((int)xyz.x, (int)xyz.y, (int)xyz.z); }
inline int3 make_int3(uint3Aligned xyz) { return make_int3((int)xyz.x, (int)xyz.y, (int)xyz.z); }
inline int3 make_int3(int3Aligned xyz) { return make_int3(xyz.x, xyz.y, xyz.z); }

inline int3Aligned make_int3Aligned(int3 xyz) { return make_int3Aligned(xyz.x, xyz.y, xyz.z); }
inline int3Aligned make_int3Aligned(uint3 xyz) { return make_int3Aligned((int)xyz.x, (int)xyz.y, (int)xyz.z); }
inline int3Aligned make_int3Aligned(float3 xyz) { return make_int3Aligned((int)xyz.x, (int)xyz.y, (int)xyz.z); }
inline int3Aligned make_int3Aligned(double3 xyz) { return make_int3Aligned((int)xyz.x, (int)xyz.y, (int)xyz.z); }
inline int3Aligned make_int3Aligned(float3Aligned xyz) { return make_int3Aligned((int)xyz.x, (int)xyz.y, (int)xyz.z); }
inline int3Aligned make_int3Aligned(double3Aligned xyz) { return make_int3Aligned((int)xyz.x, (int)xyz.y, (int)xyz.z); }
inline int3Aligned make_int3Aligned(uint3Aligned xyz) { return make_int3Aligned((int)xyz.x, (int)xyz.y, (int)xyz.z); }

inline float3 make_float3(uint3 xyz) { return make_float3((float)(int)xyz.x, (float)(int)xyz.y, (float)(int)xyz.z); }
inline float3 make_float3(int3 xyz) { return make_float3((float)(int)xyz.x, (float)(int)xyz.y, (float)(int)xyz.z); }
inline float3 make_float3(double3 xyz) { return make_float3((float)xyz.x, (float)xyz.y, (float)xyz.z); }
inline float3 make_float3(float3Aligned xyz) { return make_float3(xyz.x, xyz.y, xyz.z); }
inline float3 make_float3(double3Aligned xyz) { return make_float3((float)xyz.x, (float)xyz.y, (float)xyz.z); }

inline float3Aligned make_float3Aligned(float3 xyz) { return make_float3Aligned(xyz.x, xyz.y, xyz.z); }
inline float3Aligned make_float3Aligned(double3 xyz) { return make_float3Aligned((float)xyz.x, (float)xyz.y, (float)xyz.z); }
inline float3Aligned make_float3Aligned(double3Aligned xyz) { return make_float3Aligned((float)xyz.x, (float)xyz.y, (float)xyz.z); }

inline double3 make_double3(uint3 xyz) { return make_double3((double)(int)xyz.x, (double)(int)xyz.y, (double)(int)xyz.z); }
inline double3 make_double3(int3 xyz) { return make_double3((double)(int)xyz.x, (double)(int)xyz.y, (double)(int)xyz.z); }
inline double3 make_double3(float3 xyz) { return make_double3((double)xyz.x, (double)xyz.y, (double)xyz.z); }
inline double3 make_double3(float3Aligned xyz) { return make_double3((double)xyz.x, (double)xyz.y, (double)xyz.z); }
inline double3 make_double3(double3Aligned xyz) { return make_double3(xyz.x, xyz.y, xyz.z); }

inline double3Aligned make_double3Aligned(float3 xyz) { return make_double3Aligned((double)xyz.x, (double)xyz.y, (double)xyz.z); }
inline double3Aligned make_double3Aligned(float3Aligned xyz) { return make_double3Aligned((double)xyz.x, (double)xyz.y, (double)xyz.z); }
inline double3Aligned make_double3Aligned(double3 xyz) { return make_double3Aligned(xyz.x, xyz.y, xyz.z); }

inline uint4 make_uint4(int4 xyzw) { return make_uint4((uint)(int)xyzw.x, (uint)(int)xyzw.y, (uint)(int)xyzw.z, (uint)(int)xyzw.w); }
inline uint4 make_uint4(float4 xyzw) { return make_uint4((uint)(int)xyzw.x, (uint)(int)xyzw.y, (uint)(int)xyzw.z, (uint)(int)xyzw.w); }
inline uint4 make_uint4(double4 xyzw) { return make_uint4((uint)(int)xyzw.x, (uint)(int)xyzw.y, (uint)(int)xyzw.z, (uint)(int)xyzw.w); }

inline int4 make_int4(uint4 xyzw) { return make_int4((int)xyzw.x, (int)xyzw.y, (int)xyzw.z, (int)xyzw.w); }
inline int4 make_int4(float4 xyzw) { return make_int4((int)xyzw.x, (int)xyzw.y, (int)xyzw.z, (int)xyzw.w); }
inline int4 make_int4(double4 xyzw) { return make_int4((int)xyzw.x, (int)xyzw.y, (int)xyzw.z, (int)xyzw.w); }

inline float4 make_float4(uint4 xyzw)
{
    return make_float4((float)(int)xyzw.x, (float)(int)xyzw.y, (float)(int)xyzw.z, (float)(int)xyzw.w);
}
inline float4 make_float4(int4 xyzw) { return make_float4((float)(int)xyzw.x, (float)(int)xyzw.y, (float)(int)xyzw.z, (float)(int)xyzw.w); }
inline float4 make_float4(double4 xyzw) { return make_float4((float)xyzw.x, (float)xyzw.y, (float)xyzw.z, (float)xyzw.w); }

inline double4 make_double4(uint4 xyzw)
{
    return make_double4((double)(int)xyzw.x, (double)(int)xyzw.y, (double)(int)xyzw.z, (double)(int)xyzw.w);
}
inline double4 make_double4(int4 xyzw)
{
    return make_double4((double)(int)xyzw.x, (double)(int)xyzw.y, (double)(int)xyzw.z, (double)(int)xyzw.w);
}
inline double4 make_double4(float4 xyzw) { return make_double4((double)xyzw.x, (double)xyzw.y, (double)xyzw.z, (double)xyzw.w); }

// Scalar initializations

inline float2  make_float2(float s) { return make_float2(s, s); }
inline float3  make_float3(float s) { return make_float3(s, s, s); }
inline float4  make_float4(float s) { return make_float4(s, s, s, s); }
inline double2 make_double2(double s) { return make_double2(s, s); }
inline double3 make_double3(double s) { return make_double3(s, s, s); }
inline double4 make_double4(double s) { return make_double4(s, s, s, s); }
inline uint2   make_uint2(uint s) { return make_uint2(s, s); }
inline uint3   make_uint3(uint s) { return make_uint3(s, s, s); }
inline uint4   make_uint4(uint s) { return make_uint4(s, s, s, s); }
inline int2    make_int2(int s) { return make_int2(s, s); }
inline int3    make_int3(int s) { return make_int3(s, s, s); }
inline int4    make_int4(int s) { return make_int4(s, s, s, s); }

// These just help with code generation & syntax translation
typedef float  float1;
typedef double double1;
typedef uint   uint1;
typedef int    int1;

inline float1  make_float1(float1 x) { return x; }
inline double1 make_double1(double1 x) { return x; }
inline int1    make_int1(int1 x) { return x; }
inline uint1   make_uint1(uint1 x) { return x; }

inline float3  getXYZ(float4 x) { return make_float3(x.x, x.y, x.z); };
inline double3 getXYZ(double4 x) { return make_double3(x.x, x.y, x.z); };
inline uint3   getXYZ(uint4 x) { return make_uint3(x.x, x.y, x.z); };
inline int3    getXYZ(int4 x) { return make_int3(x.x, x.y, x.z); };

inline float3  getXYZ(float3 x) { return make_float3(x.x, x.y, x.z); };
inline double3 getXYZ(double3 x) { return make_double3(x.x, x.y, x.z); };
inline uint3   getXYZ(uint3 x) { return make_uint3(x.x, x.y, x.z); };
inline int3    getXYZ(int3 x) { return make_int3(x.x, x.y, x.z); };

// Working around another ISPC bug which messes up code generation
inline float get_x(float2 v) { return v.x; }
inline float get_y(float2 v) { return v.y; }

inline float get_x(float3 v) { return v.x; }
inline float get_y(float3 v) { return v.y; }
inline float get_z(float3 v) { return v.z; }

inline float get_x(float4 v) { return v.x; }
inline float get_y(float4 v) { return v.y; }
inline float get_z(float4 v) { return v.z; }
inline float get_w(float4 v) { return v.w; }

inline double get_x(double2 v) { return v.x; }
inline double get_y(double2 v) { return v.y; }

inline double get_x(double3 v) { return v.x; }
inline double get_y(double3 v) { return v.y; }
inline double get_z(double3 v) { return v.z; }

inline double get_x(double4 v) { return v.x; }
inline double get_y(double4 v) { return v.y; }
inline double get_z(double4 v) { return v.z; }
inline double get_w(double4 v) { return v.w; }

inline uint get_x(uint2 v) { return v.x; }
inline uint get_y(uint2 v) { return v.y; }

inline uint get_x(uint3 v) { return v.x; }
inline uint get_y(uint3 v) { return v.y; }
inline uint get_z(uint3 v) { return v.z; }

inline uint get_x(uint4 v) { return v.x; }
inline uint get_y(uint4 v) { return v.y; }
inline uint get_z(uint4 v) { return v.z; }
inline uint get_w(uint4 v) { return v.w; }

inline int get_x(int2 v) { return v.x; }
inline int get_y(int2 v) { return v.y; }

inline int get_x(int3 v) { return v.x; }
inline int get_y(int3 v) { return v.y; }
inline int get_z(int3 v) { return v.z; }

inline int get_x(int4 v) { return v.x; }
inline int get_y(int4 v) { return v.y; }
inline int get_z(int4 v) { return v.z; }
inline int get_w(int4 v) { return v.w; }

/////////
//// ISPC operators

// ----- float2 operators -----
inline float2 operator+(float2 a, float2 b) { return make_float2(a.x + b.x, a.y + b.y); }
inline float2 operator-(float2 a, float2 b) { return make_float2(a.x - b.x, a.y - b.y); }
inline float2 operator*(float2 a, float2 b) { return make_float2(a.x * b.x, a.y * b.y); }
inline float2 operator/(float2 a, float2 b) { return make_float2(a.x / b.x, a.y / b.y); }
inline float2 operator*(float2 a, float s) { return make_float2(a.x * s, a.y * s); }
inline float2 operator*(float s, float2 a) { return a * s; }
inline float2 operator/(float2 a, float s) { return make_float2(a.x / s, a.y / s); }
inline float2 operator/(float s, float2 a) { return make_float2(s / a.x, s / a.y); }

// ----- float3 operators -----
inline float3 operator+(float3 a, float3 b) { return make_float3(a.x + b.x, a.y + b.y, a.z + b.z); }
inline float3 operator-(float3 a, float3 b) { return make_float3(a.x - b.x, a.y - b.y, a.z - b.z); }
inline float3 operator*(float3 a, float3 b) { return make_float3(a.x * b.x, a.y * b.y, a.z * b.z); }
inline float3 operator/(float3 a, float3 b) { return make_float3(a.x / b.x, a.y / b.y, a.z / b.z); }
inline float3 operator*(float3 a, float s) { return make_float3(a.x * s, a.y * s, a.z * s); }
inline float3 operator*(float s, float3 a) { return a * s; }
inline float3 operator/(float3 a, float s) { return make_float3(a.x / s, a.y / s, a.z / s); }
inline float3 operator/(float s, float3 a) { return make_float3(s / a.x, s / a.y, s / a.z); }

// ----- float4 operators -----
inline float4 operator+(float4 a, float4 b) { return make_float4(a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w); }
inline float4 operator-(float4 a, float4 b) { return make_float4(a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w); }
inline float4 operator*(float4 a, float4 b) { return make_float4(a.x * b.x, a.y * b.y, a.z * b.z, a.w * b.w); }
inline float4 operator/(float4 a, float4 b) { return make_float4(a.x / b.x, a.y / b.y, a.z / b.z, a.w / b.w); }
inline float4 operator*(float4 a, float s) { return make_float4(a.x * s, a.y * s, a.z * s, a.w * s); }
inline float4 operator*(float s, float4 a) { return a * s; }
inline float4 operator/(float4 a, float s) { return make_float4(a.x / s, a.y / s, a.z / s, a.w / s); }
inline float4 operator/(float s, float4 a) { return make_float4(s / a.x, s / a.y, s / a.z, s / a.w); }
inline float4 operator-(float4 a) { return make_float4(-a.x, -a.y, -a.z, -a.w); }

// ----- double2 operators -----
inline double2 operator+(double2 a, double2 b) { return make_double2(a.x + b.x, a.y + b.y); }
inline double2 operator-(double2 a, double2 b) { return make_double2(a.x - b.x, a.y - b.y); }
inline double2 operator*(double2 a, double2 b) { return make_double2(a.x * b.x, a.y * b.y); }
inline double2 operator/(double2 a, double2 b) { return make_double2(a.x / b.x, a.y / b.y); }
inline double2 operator*(double2 a, double s) { return make_double2(a.x * s, a.y * s); }
inline double2 operator*(double s, double2 a) { return a * s; }
inline double2 operator/(double2 a, double s) { return make_double2(a.x / s, a.y / s); }
inline double2 operator/(double s, double2 a) { return make_double2(s / a.x, s / a.y); }

// ----- double3 operators -----
inline double3 operator+(double3 a, double3 b) { return make_double3(a.x + b.x, a.y + b.y, a.z + b.z); }
inline double3 operator-(double3 a, double3 b) { return make_double3(a.x - b.x, a.y - b.y, a.z - b.z); }
inline double3 operator*(double3 a, double3 b) { return make_double3(a.x * b.x, a.y * b.y, a.z * b.z); }
inline double3 operator/(double3 a, double3 b) { return make_double3(a.x / b.x, a.y / b.y, a.z / b.z); }
inline double3 operator*(double3 a, double s) { return make_double3(a.x * s, a.y * s, a.z * s); }
inline double3 operator*(double s, double3 a) { return a * s; }
inline double3 operator/(double3 a, double s) { return make_double3(a.x / s, a.y / s, a.z / s); }
inline double3 operator/(double s, double3 a) { return make_double3(s / a.x, s / a.y, s / a.z); }

// ----- double4 operators -----
inline double4 operator+(double4 a, double4 b) { return make_double4(a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w); }
inline double4 operator-(double4 a, double4 b) { return make_double4(a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w); }
inline double4 operator*(double4 a, double4 b) { return make_double4(a.x * b.x, a.y * b.y, a.z * b.z, a.w * b.w); }
inline double4 operator/(double4 a, double4 b) { return make_double4(a.x / b.x, a.y / b.y, a.z / b.z, a.w / b.w); }
inline double4 operator*(double4 a, double s) { return make_double4(a.x * s, a.y * s, a.z * s, a.w * s); }
inline double4 operator*(double s, double4 a) { return a * s; }
inline double4 operator/(double4 a, double s) { return make_double4(a.x / s, a.y / s, a.z / s, a.w / s); }
inline double4 operator/(double s, double4 a) { return make_double4(s / a.x, s / a.y, s / a.z, s / a.w); }
inline double4 operator-(double4 a) { return make_double4(-a.x, -a.y, -a.z, -a.w); }

// NOTE on int division:
// Most SIMD instruction sets do not support integer divisions. ISPC will translate it to one scalar division
// for each lane, which is very slow. Here we make approximations by casting to float then back.
// This might be a problem when doing divisions on big integer values. TODO revisit

// ----- uint2 operators -----
inline uint2 operator+(uint2 a, uint2 b) { return make_uint2(a.x + b.x, a.y + b.y); }
inline uint2 operator-(uint2 a, uint2 b) { return make_uint2(a.x - b.x, a.y - b.y); }
inline uint2 operator*(uint2 a, uint2 b) { return make_uint2(a.x * b.x, a.y * b.y); }
inline uint2 operator/(uint2 a, uint2 b) { return make_uint2((uint)(int)((float)a.x / (float)b.x), (uint)(int)((float)a.y / (float)b.y)); }
inline uint2 operator*(uint2 a, uint s) { return make_uint2(a.x * s, a.y * s); }
inline uint2 operator*(uint s, uint2 a) { return a * s; }
inline uint2 operator/(uint2 a, uint s) { return make_uint2((uint)(int)((float)a.x / (float)s), (uint)(int)((float)a.y / (float)s)); }
inline uint2 operator/(uint s, uint2 a) { return make_uint2((uint)(int)((float)s / (float)a.x), (uint)(int)((float)s / (float)a.y)); }

// ----- uint3 operators -----
inline uint3 operator+(uint3 a, uint3 b) { return make_uint3(a.x + b.x, a.y + b.y, a.z + b.z); }
inline uint3 operator-(uint3 a, uint3 b) { return make_uint3(a.x - b.x, a.y - b.y, a.z - b.z); }
inline uint3 operator*(uint3 a, uint3 b) { return make_uint3(a.x * b.x, a.y * b.y, a.z * b.z); }
inline uint3 operator/(uint3 a, uint3 b)
{
    return make_uint3((uint)(int)((float)a.x / (float)b.x), (uint)(int)((float)a.y / (float)b.y), (uint)(int)((float)a.z / (float)b.z));
}
inline uint3 operator*(uint3 a, uint s) { return make_uint3(a.x * s, a.y * s, a.z * s); }
inline uint3 operator*(uint s, uint3 a) { return a * s; }
inline uint3 operator/(uint3 a, uint s)
{
    return make_uint3((uint)(int)((float)a.x / (float)s), (uint)(int)((float)a.y / (float)s), (uint)(int)((float)a.z / (float)s));
}
inline uint3 operator/(uint s, uint3 a)
{
    return make_uint3((uint)(int)((float)s / (float)a.x), (uint)(int)((float)s / (float)a.y), (uint)(int)((float)s / (float)a.z));
}

// ----- uint4 operators -----
inline uint4 operator+(uint4 a, uint4 b) { return make_uint4(a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w); }
inline uint4 operator-(uint4 a, uint4 b) { return make_uint4(a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w); }
inline uint4 operator*(uint4 a, uint4 b) { return make_uint4(a.x * b.x, a.y * b.y, a.z * b.z, a.w * b.w); }
inline uint4 operator/(uint4 a, uint4 b)
{
    return make_uint4((uint)(int)((float)a.x / (float)b.x), (uint)(int)((float)a.y / (float)b.y), (uint)(int)((float)a.z / (float)b.z),
                      (uint)(int)((float)a.w / (float)b.w));
}
inline uint4 operator*(uint4 a, uint s) { return make_uint4(a.x * s, a.y * s, a.z * s, a.w * s); }
inline uint4 operator*(uint s, uint4 a) { return a * s; }
inline uint4 operator/(uint4 a, uint s)
{
    return make_uint4((uint)(int)((float)a.x / (float)s), (uint)(int)((float)a.y / (float)s), (uint)(int)((float)a.z / (float)s),
                      (uint)(int)((float)a.w / (float)s));
}
inline uint4 operator/(uint s, uint4 a)
{
    return make_uint4((uint)(int)((float)s / (float)a.x), (uint)(int)((float)s / (float)a.y), (uint)(int)((float)s / (float)a.z),
                      (uint)(int)((float)s / (float)a.w));
}

// ----- int2 operators -----
inline int2 operator+(int2 a, int2 b) { return make_int2(a.x + b.x, a.y + b.y); }
inline int2 operator-(int2 a, int2 b) { return make_int2(a.x - b.x, a.y - b.y); }
inline int2 operator*(int2 a, int2 b) { return make_int2(a.x * b.x, a.y * b.y); }
inline int2 operator/(int2 a, int2 b) { return make_int2((int)((float)a.x / (float)b.x), (int)((float)a.y / (float)b.y)); }
inline int2 operator*(int2 a, int s) { return make_int2(a.x * s, a.y * s); }
inline int2 operator*(int s, int2 a) { return a * s; }
inline int2 operator/(int2 a, int s) { return make_int2((int)((float)a.x / (float)s), (int)((float)a.y / (float)s)); }
inline int2 operator/(int s, int2 a) { return make_int2((int)((float)s / (float)a.x), (int)((float)s / (float)a.y)); }

// ----- int3 operators -----
inline int3 operator+(int3 a, int3 b) { return make_int3(a.x + b.x, a.y + b.y, a.z + b.z); }
inline int3 operator-(int3 a, int3 b) { return make_int3(a.x - b.x, a.y - b.y, a.z - b.z); }
inline int3 operator*(int3 a, int3 b) { return make_int3(a.x * b.x, a.y * b.y, a.z * b.z); }
inline int3 operator/(int3 a, int3 b)
{
    return make_int3((int)((float)a.x / (float)b.x), (int)((float)a.y / (float)b.y), (int)((float)a.z / (float)b.z));
}
inline int3 operator*(int3 a, int s) { return make_int3(a.x * s, a.y * s, a.z * s); }
inline int3 operator*(int s, int3 a) { return a * s; }
inline int3 operator/(int3 a, int s)
{
    return make_int3((int)((float)a.x / (float)s), (int)((float)a.y / (float)s), (int)((float)a.z / (float)s));
}
inline int3 operator/(int s, int3 a)
{
    return make_int3((int)((float)s / (float)a.x), (int)((float)s / (float)a.y), (int)((float)s / (float)a.z));
}

// ----- int4 operators -----
inline int4 operator+(int4 a, int4 b) { return make_int4(a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w); }
inline int4 operator-(int4 a, int4 b) { return make_int4(a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w); }
inline int4 operator*(int4 a, int4 b) { return make_int4(a.x * b.x, a.y * b.y, a.z * b.z, a.w * b.w); }
inline int4 operator/(int4 a, int4 b)
{
    return make_int4((int)((float)a.x / (float)b.x), (int)((float)a.y / (float)b.y), (int)((float)a.z / (float)b.z),
                     (int)((float)a.w / (float)b.w));
}
inline int4 operator*(int4 a, int s) { return make_int4(a.x * s, a.y * s, a.z * s, a.w * s); }
inline int4 operator*(int s, int4 a) { return a * s; }
inline int4 operator/(int4 a, int s)
{
    return make_int4((int)((float)a.x / (float)s), (int)((float)a.y / (float)s), (int)((float)a.z / (float)s),
                     (int)((float)a.w / (float)s));
}
inline int4 operator/(int s, int4 a)
{
    return make_int4((int)((float)s / (float)a.x), (int)((float)s / (float)a.y), (int)((float)s / (float)a.z),
                     (int)((float)s / (float)a.w));
}

inline float4 abs(float4 f)
{
    float4 result;
    result.x = abs(f.x);
    result.y = abs(f.y);
    result.z = abs(f.z);
    result.w = abs(f.w);

    return result;
}

inline float fabsf(float x) { return abs(x); }
inline float sinf(float x) { return sin(x); }
inline float cosf(float x) { return cos(x); }
inline float tanf(float x) { return tan(x); }
inline float asinf(float x) { return asin(x); }
inline float acosf(float x) { return acos(x); }
inline float atanf(float x) { return atan(x); }

inline float2 floor(float2 v) { return make_float2(floor(v.x), floor(v.y)); }
inline float3 floor(float3 v) { return make_float3(floor(v.x), floor(v.y), floor(v.z)); }
inline float4 floor(float4 v) { return make_float4(floor(v.x), floor(v.y), floor(v.z), floor(v.w)); }

inline float2 ceil(float2 v) { return make_float2(ceil(v.x), ceil(v.y)); }
inline float3 ceil(float3 v) { return make_float3(ceil(v.x), ceil(v.y), ceil(v.z)); }
inline float4 ceil(float4 v) { return make_float4(ceil(v.x), ceil(v.y), ceil(v.z), ceil(v.w)); }

inline float2 round(float2 v) { return make_float2(round(v.x), round(v.y)); }
inline float3 round(float3 v) { return make_float3(round(v.x), round(v.y), round(v.z)); }
inline float4 round(float4 v) { return make_float4(round(v.x), round(v.y), round(v.z), round(v.w)); }

#define inout(T)          T&
#define out(T)            T&
#define in(T)             T
#define inout_array(T, X) T*
#define out_array(T, X)   T*
#define in_array(T, X)    T*
#define groupshared(T)    uniform T*

#define setElem(M, I, J, V)           \
    {                                 \
        *__subscript(&M.v[I], J) = V; \
    }
#define getElem(M, I, J) (*__subscript(&M.v[I], J))

#define getCol(M, I)     M.v[I]
#define getCol0(M)       getCol(M, 0)
#define getCol1(M)       getCol(M, 1)
#define getCol2(M)       getCol(M, 2)
#define getCol3(M)       getCol(M, 3)

inline float4 getRow(float4x4 M, uint i)
{
    return make_float4(*__subscript(&M.v[0], i), *__subscript(&M.v[1], i), *__subscript(&M.v[2], i), *__subscript(&M.v[3], i));
}
inline float4 getRow(float4x3 M, uint i)
{
    return make_float4(*__subscript(&M.v[0], i), *__subscript(&M.v[1], i), *__subscript(&M.v[2], i), *__subscript(&M.v[3], i));
}
inline float4 getRow(float4x2 M, uint i)
{
    return make_float4(*__subscript(&M.v[0], i), *__subscript(&M.v[1], i), *__subscript(&M.v[2], i), *__subscript(&M.v[3], i));
}

inline float3 getRow(float3x4 M, uint i)
{
    return make_float3(*__subscript(&M.v[0], i), *__subscript(&M.v[1], i), *__subscript(&M.v[2], i));
}
inline float3 getRow(float3x3 M, uint i)
{
    return make_float3(*__subscript(&M.v[0], i), *__subscript(&M.v[1], i), *__subscript(&M.v[2], i));
}
inline float3 getRow(float3x2 M, uint i)
{
    return make_float3(*__subscript(&M.v[0], i), *__subscript(&M.v[1], i), *__subscript(&M.v[2], i));
}

inline float2 getRow(float2x4 M, uint i) { return make_float2(*__subscript(&M.v[0], i), *__subscript(&M.v[1], i)); }
inline float2 getRow(float2x3 M, uint i) { return make_float2(*__subscript(&M.v[0], i), *__subscript(&M.v[1], i)); }
inline float2 getRow(float2x2 M, uint i) { return make_float2(*__subscript(&M.v[0], i), *__subscript(&M.v[1], i)); }

inline double4 getRow(double4x4 M, uint i)
{
    return make_double4(*__subscript(&M.v[0], i), *__subscript(&M.v[1], i), *__subscript(&M.v[2], i), *__subscript(&M.v[3], i));
}
inline double4 getRow(double4x3 M, uint i)
{
    return make_double4(*__subscript(&M.v[0], i), *__subscript(&M.v[1], i), *__subscript(&M.v[2], i), *__subscript(&M.v[3], i));
}
inline double4 getRow(double4x2 M, uint i)
{
    return make_double4(*__subscript(&M.v[0], i), *__subscript(&M.v[1], i), *__subscript(&M.v[2], i), *__subscript(&M.v[3], i));
}

inline double3 getRow(double3x4 M, uint i)
{
    return make_double3(*__subscript(&M.v[0], i), *__subscript(&M.v[1], i), *__subscript(&M.v[2], i));
}
inline double3 getRow(double3x3 M, uint i)
{
    return make_double3(*__subscript(&M.v[0], i), *__subscript(&M.v[1], i), *__subscript(&M.v[2], i));
}
inline double3 getRow(double3x2 M, uint i)
{
    return make_double3(*__subscript(&M.v[0], i), *__subscript(&M.v[1], i), *__subscript(&M.v[2], i));
}

inline double2 getRow(double2x4 M, uint i) { return make_double2(*__subscript(&M.v[0], i), *__subscript(&M.v[1], i)); }
inline double2 getRow(double2x3 M, uint i) { return make_double2(*__subscript(&M.v[0], i), *__subscript(&M.v[1], i)); }
inline double2 getRow(double2x2 M, uint i) { return make_double2(*__subscript(&M.v[0], i), *__subscript(&M.v[1], i)); }

#define getRow0(M) getRow(M, 0)
#define getRow1(M) getRow(M, 1)
#define getRow2(M) getRow(M, 2)
#define getRow3(M) getRow(M, 3)

inline f4x4 setCol(inout(f4x4) M, float4 col, const uint i)
{
    M.v[i] = col;
    return M;
}
inline f4x3 setCol(inout(f4x3) M, float3 col, const uint i)
{
    M.v[i] = col;
    return M;
}
inline f4x2 setCol(inout(f4x2) M, float2 col, const uint i)
{
    M.v[i] = col;
    return M;
}

inline f3x4 setCol(inout(f3x4) M, float4 col, const uint i)
{
    M.v[i] = col;
    return M;
}
inline f3x3 setCol(inout(f3x3) M, float3 col, const uint i)
{
    M.v[i] = col;
    return M;
}
inline f3x2 setCol(inout(f3x2) M, float2 col, const uint i)
{
    M.v[i] = col;
    return M;
}

inline f2x4 setCol(inout(f2x4) M, float4 col, const uint i)
{
    M.v[i] = col;
    return M;
}
inline f2x3 setCol(inout(f2x3) M, float3 col, const uint i)
{
    M.v[i] = col;
    return M;
}
inline f2x2 setCol(inout(f2x2) M, float2 col, const uint i)
{
    M.v[i] = col;
    return M;
}

inline d4x4 setCol(inout(d4x4) M, double4 col, const uint i)
{
    M.v[i] = col;
    return M;
}
inline d4x3 setCol(inout(d4x3) M, double3 col, const uint i)
{
    M.v[i] = col;
    return M;
}
inline d4x2 setCol(inout(d4x2) M, double2 col, const uint i)
{
    M.v[i] = col;
    return M;
}

inline d3x4 setCol(inout(d3x4) M, double4 col, const uint i)
{
    M.v[i] = col;
    return M;
}
inline d3x3 setCol(inout(d3x3) M, double3 col, const uint i)
{
    M.v[i] = col;
    return M;
}
inline d3x2 setCol(inout(d3x2) M, double2 col, const uint i)
{
    M.v[i] = col;
    return M;
}

inline d2x4 setCol(inout(d2x4) M, double4 col, const uint i)
{
    M.v[i] = col;
    return M;
}
inline d2x3 setCol(inout(d2x3) M, double3 col, const uint i)
{
    M.v[i] = col;
    return M;
}
inline d2x2 setCol(inout(d2x2) M, double2 col, const uint i)
{
    M.v[i] = col;
    return M;
}

#define setCol0(M, C) setCol(M, C, 0)
#define setCol1(M, C) setCol(M, C, 1)
#define setCol2(M, C) setCol(M, C, 2)
#define setCol3(M, C) setCol(M, C, 3)

inline f4x4 setRow(inout(f4x4) M, float4 row, const uint i)
{
    *__subscript(&M.v[0], i) = *__subscript(&row, 0);
    *__subscript(&M.v[1], i) = *__subscript(&row, 1);
    *__subscript(&M.v[2], i) = *__subscript(&row, 2);
    *__subscript(&M.v[3], i) = *__subscript(&row, 3);
    return M;
}
inline f4x3 setRow(inout(f4x3) M, float4 row, const uint i)
{
    *__subscript(&M.v[0], i) = *__subscript(&row, 0);
    *__subscript(&M.v[1], i) = *__subscript(&row, 1);
    *__subscript(&M.v[2], i) = *__subscript(&row, 2);
    *__subscript(&M.v[3], i) = *__subscript(&row, 3);
    return M;
}
inline f4x2 setRow(inout(f4x2) M, float4 row, const uint i)
{
    *__subscript(&M.v[0], i) = *__subscript(&row, 0);
    *__subscript(&M.v[1], i) = *__subscript(&row, 1);
    *__subscript(&M.v[2], i) = *__subscript(&row, 2);
    *__subscript(&M.v[3], i) = *__subscript(&row, 3);
    return M;
}

inline f3x4 setRow(inout(f3x4) M, float3 row, const uint i)
{
    *__subscript(&M.v[0], i) = *__subscript(&row, 0);
    *__subscript(&M.v[1], i) = *__subscript(&row, 1);
    *__subscript(&M.v[2], i) = *__subscript(&row, 2);
    return M;
}
inline f3x3 setRow(inout(f3x3) M, float3 row, const uint i)
{
    *__subscript(&M.v[0], i) = *__subscript(&row, 0);
    *__subscript(&M.v[1], i) = *__subscript(&row, 1);
    *__subscript(&M.v[2], i) = *__subscript(&row, 2);
    return M;
}
inline f3x2 setRow(inout(f3x2) M, float3 row, const uint i)
{
    *__subscript(&M.v[0], i) = *__subscript(&row, 0);
    *__subscript(&M.v[1], i) = *__subscript(&row, 1);
    *__subscript(&M.v[2], i) = *__subscript(&row, 2);
    return M;
}

inline f2x4 setRow(inout(f2x4) M, float2 row, const uint i)
{
    *__subscript(&M.v[0], i) = *__subscript(&row, 0);
    *__subscript(&M.v[1], i) = *__subscript(&row, 1);
    return M;
}
inline f2x3 setRow(inout(f2x3) M, float2 row, const uint i)
{
    *__subscript(&M.v[0], i) = *__subscript(&row, 0);
    *__subscript(&M.v[1], i) = *__subscript(&row, 1);
    return M;
}
inline f2x2 setRow(inout(f2x2) M, float2 row, const uint i)
{
    *__subscript(&M.v[0], i) = *__subscript(&row, 0);
    *__subscript(&M.v[1], i) = *__subscript(&row, 1);
    return M;
}

#define setRow0(M, R)     setRow(M, R, 0)
#define setRow1(M, R)     setRow(M, R, 1)
#define setRow2(M, R)     setRow(M, R, 2)
#define setRow3(M, R)     setRow(M, R, 3)

#define rgba8             float4

// $ symbols replaced by translator
#define CS_MAIN           $PROGRAM_NAME

#define EXPORT_NAME(name) $PROGRAM_NAME_##name

// #define FSL_REG(REG_0, REG_1)

#define INIT_MAIN
#define RETURN(...)                  return

#define packed_float3                float3

#define out_coverage                 uint

#define GreaterThan(A, B)            ((A) > (B))
#define GreaterThanEqual(A, B)       ((A) >= (B))
#define LessThan(A, B)               ((A) < (B))
#define LessThanEqual(A, B)          ((A) <= (B))

#define select                       lerp

#define fast_min                     min
#define fast_max                     max
//#define isordered(X, Y)           (((X) == (X)) && ((Y) == (Y)))
//#define isunordered(X, Y)         (isnan(X) || isnan(Y))

#define extract_bits(SRC, OFF, BITS) ((SRC) >> (OFF)) & ((1u << (BITS)) - 1)

#define insert_bits(SRC, INS, OFF, BITS) \
    (((INS) << (OFF)) & ((((1u << (BITS)) - 1) << (OFF)) & 0xffffffff)) | ((SRC) & ~((((1u << (BITS)) - 1) << (OFF)) & 0xffffffff));

#define Equal(X, Y)   ((X) == (Y))

#define row_major(X)  transpose(X)

//#define GroupMemoryBarrier
//#define AllMemoryBarrier
#define MemoryBarrier memory_barrier

/*
    TODO
    Investigate implications of *_local vs *_global atomic operations
*/

#define AtomicAdd(DEST, VALUE, ORIGINAL_VALUE)              \
    {                                                       \
        ORIGINAL_VALUE = atomic_add_global((&DEST), VALUE); \
    }
#define AtomicOr(DEST, VALUE, ORIGINAL_VALUE)              \
    {                                                      \
        ORIGINAL_VALUE = atomic_or_global(&(DEST), VALUE); \
    }
#define AtomicAnd(DEST, VALUE, ORIGINAL_VALUE)              \
    {                                                       \
        ORIGINAL_VALUE = atomic_and_global(&(DEST), VALUE); \
    }
#define AtomicXor(DEST, VALUE, ORIGINAL_VALUE)              \
    {                                                       \
        ORIGINAL_VALUE = atomic_xor_global(&(DEST), VALUE); \
    }

#define AtomicStore(DST, VALUE) \
    {                           \
        MemoryBarrier();        \
        (DST) = (VALUE);        \
        MemoryBarrier();        \
    }

#define AtomicLoad(SRC) SRC

#define AtomicExchange(DEST, VALUE, ORIGINAL_VALUE)          \
    {                                                        \
        ORIGINAL_VALUE = atomic_swap_global(&(DEST), VALUE); \
    }

#define AtomicCompareExchange(DEST, COMPARE_VALUE, VALUE, ORIGINAL_VALUE)               \
    {                                                                                   \
        ORIGINAL_VALUE = atomic_compare_exchange_global(&(DEST), COMPARE_VALUE, VALUE); \
    }

#define NUM_THREADS(X, Y, Z)                                         \
    export uniform uint $PROGRAM_NAME_num_threads_x() { return X; }; \
    export uniform uint $PROGRAM_NAME_num_threads_y() { return Y; }; \
    export uniform uint $PROGRAM_NAME_num_threads_z() { return Z; };

#define DATA(TYPE, NAME, SEM) TYPE NAME

#define ByteBuffer            uniform uint8*
#define RWByteBuffer          uniform uint8*
#define WByteBuffer           uniform uint8*

#define LoadByte(BUFF, ADDR)  (uint)(BUFF)[(ADDR)];
#define LoadByte2(BUFF, ADDR)                          \
    {                                                  \
        (uint)(BUFF)[(ADDR)], (uint)(BUFF)[(ADDR) + 1] \
    }
#define LoadByte3(BUFF, ADDR)                                                    \
    {                                                                            \
        (uint)(BUFF)[(ADDR)], (uint)(BUFF)[(ADDR) + 1], (uint)(BUFF)[(ADDR) + 2] \
    }
#define LoadByte4(BUFF, ADDR)                                                                              \
    {                                                                                                      \
        (uint)(BUFF)[(ADDR)], (uint)(BUFF)[(ADDR) + 1], (uint)(BUFF)[(ADDR) + 2], (uint)(BUFF)[(ADDR) + 3] \
    }

// NOTE this is some of the formats from tinyimageformat_base.h which unfortunately doesn't compile in ISPC.
enum TinyImageFormat
{
    TinyImageFormat_R8_SRGB = 11,
    TinyImageFormat_R8G8_UNORM = 32,
    TinyImageFormat_R8G8_SRGB = 38,
    TinyImageFormat_R8G8B8A8_UNORM = 55,
    TinyImageFormat_R8G8B8A8_SRGB = 59,
    TinyImageFormat_R32G32_SFLOAT = 110,
    TinyImageFormat_R32G32B32A32_SFLOAT = 116,
};

#define _DECL_Texture(TYPE)             \
    struct Texture1D##TYPE              \
    {                                   \
        uniform TinyImageFormat format; \
        uniform uint            width;  \
        uniform uint8* uniform data;    \
    };                                  \
    struct Texture2D##TYPE              \
    {                                   \
        uniform TinyImageFormat format; \
        uniform uint            width;  \
        uniform uint            height; \
        uniform uint8* uniform data;    \
    };                                  \
    struct Texture3D##TYPE              \
    {                                   \
        uniform TinyImageFormat format; \
        uniform uint            width;  \
        uniform uint            height; \
        uniform uint            depth;  \
        uniform uint8* uniform data;    \
    };
_DECL_TYPES(_DECL_Texture)

typedef Texture1Dfloat Texture1Dfloat1;
typedef Texture1Duint  Texture1Duint1;
typedef Texture1Dint   Texture1Dint1;
typedef Texture2Dfloat Texture2Dfloat1;
typedef Texture2Duint  Texture2Duint1;
typedef Texture2Dint   Texture2Dint1;
typedef Texture3Dfloat Texture3Dfloat1;
typedef Texture3Duint  Texture3Duint1;
typedef Texture3Dint   Texture3Dint1;

typedef int SamplerState;

// Semantic utility functions.
// These are to work around syntax edge cases in the load/write function generation macros

inline float get_comp_or_0(float1 val, int index)
{
    if (index == 0)
        return val;
    return 0;
}
inline float get_comp_or_0(float2 val, int index)
{
    if (index == 0)
        return val.x;
    else if (index == 1)
        return val.y;
    return 0;
}
inline float get_comp_or_0(float3 val, int index)
{
    if (index == 0)
        return val.x;
    else if (index == 1)
        return val.y;
    else if (index == 2)
        return val.z;
    return 0;
}
inline float get_comp_or_0(float4 val, int index)
{
    if (index == 0)
        return val.x;
    else if (index == 1)
        return val.y;
    else if (index == 2)
        return val.z;
    else if (index == 3)
        return val.w;
    return 0;
}
inline uint get_comp_or_0(uint1 val, int index)
{
    if (index == 0)
        return val;
    return 0;
}
inline uint get_comp_or_0(uint2 val, int index)
{
    if (index == 0)
        return val.x;
    else if (index == 1)
        return val.y;
    return 0;
}
inline uint get_comp_or_0(uint3 val, int index)
{
    if (index == 0)
        return val.x;
    else if (index == 1)
        return val.y;
    else if (index == 2)
        return val.z;
    return 0;
}
inline uint get_comp_or_0(uint4 val, int index)
{
    if (index == 0)
        return val.x;
    else if (index == 1)
        return val.y;
    else if (index == 2)
        return val.z;
    else if (index == 3)
        return val.w;
    return 0;
}
inline int get_comp_or_0(int1 val, int index)
{
    if (index == 0)
        return val;
    return 0;
}
inline int get_comp_or_0(int2 val, int index)
{
    if (index == 0)
        return val.x;
    else if (index == 1)
        return val.y;
    return 0;
}
inline int get_comp_or_0(int3 val, int index)
{
    if (index == 0)
        return val.x;
    else if (index == 1)
        return val.y;
    else if (index == 2)
        return val.z;
    return 0;
}
inline int get_comp_or_0(int4 val, int index)
{
    if (index == 0)
        return val.x;
    else if (index == 1)
        return val.y;
    else if (index == 2)
        return val.z;
    else if (index == 3)
        return val.w;
    return 0;
}

inline float1 set_comp_or_0(float1 dst, int index, float val)
{
    if (index == 0)
        dst = val;
    return dst;
}
inline float2 set_comp_or_0(float2 dst, int index, float val)
{
    if (index == 0)
        dst.x = val;
    else if (index == 1)
        dst.y = val;
    return dst;
}
inline float3 set_comp_or_0(float3 dst, int index, float val)
{
    if (index == 0)
        dst.x = val;
    else if (index == 1)
        dst.y = val;
    else if (index == 2)
        dst.z = val;
    return dst;
}
inline float4 set_comp_or_0(float4 dst, int index, float val)
{
    if (index == 0)
        dst.x = val;
    else if (index == 1)
        dst.y = val;
    else if (index == 2)
        dst.z = val;
    else if (index == 3)
        dst.w = val;
    return dst;
}
inline uint1 set_comp_or_0(uint1 dst, int index, uint val)
{
    if (index == 0)
        dst = val;
    return dst;
}
inline uint2 set_comp_or_0(uint2 dst, int index, uint val)
{
    if (index == 0)
        dst.x = val;
    else if (index == 1)
        dst.y = val;
    return dst;
}
inline uint3 set_comp_or_0(uint3 dst, int index, uint val)
{
    if (index == 0)
        dst.x = val;
    else if (index == 1)
        dst.y = val;
    else if (index == 2)
        dst.z = val;
    return dst;
}
inline uint4 set_comp_or_0(uint4 dst, int index, uint val)
{
    if (index == 0)
        dst.x = val;
    else if (index == 1)
        dst.y = val;
    else if (index == 2)
        dst.z = val;
    else if (index == 3)
        dst.w = val;
    return dst;
}
inline int1 set_comp_or_0(int1 dst, int index, int val)
{
    if (index == 0)
        dst = val;
    return dst;
}
inline int2 set_comp_or_0(int2 dst, int index, int val)
{
    if (index == 0)
        dst.x = val;
    else if (index == 1)
        dst.y = val;
    return dst;
}
inline int3 set_comp_or_0(int3 dst, int index, int val)
{
    if (index == 0)
        dst.x = val;
    else if (index == 1)
        dst.y = val;
    else if (index == 2)
        dst.z = val;
    return dst;
}
inline int4 set_comp_or_0(int4 dst, int index, int val)
{
    if (index == 0)
        dst.x = val;
    else if (index == 1)
        dst.y = val;
    else if (index == 2)
        dst.z = val;
    else if (index == 3)
        dst.w = val;
    return dst;
}

inline float srgb_to_linear(float c) { return (c <= 0.04045f) ? c / 12.92f : pow((c + 0.055f) / 1.055f, 2.4f); }
inline float linear_to_srgb(float c) { return (c <= 0.0031308f) ? c * 12.92f : 1.055f * pow(c, 1.0f / 2.4f) - 0.055f; }

#define READ_UNORM_BYTE(tex, base, ch)     ((float)(tex.data[base + (ch)]) / 255.0f)
#define READ_SRGB_BYTE(tex, base, ch)      (srgb_to_linear((float)(tex.data[base + (ch)]) / 255.0f))
#define READ_BYTE_INT(tex, base, ch)       ((int)(tex.data[base + (ch)]))

#define WRITE_UNORM_BYTE(tex, base, ch, v) (tex.data[base + (ch)] = (uint8)(clamp((int)((v)*255.0f + 0.5f), 0, 255)))
#define WRITE_SRGB_BYTE(tex, base, ch, v)  (tex.data[base + (ch)] = (uint8)(clamp((int)(linear_to_srgb(v) * 255.0f + 0.5f), 0, 255)))
#define WRITE_BYTE_INT(tex, base, ch, v)   (tex.data[base + (ch)] = (uint8)(clamp(v, 0, 255)))

#define IMPLEMENT_LOAD_TEX_FLOAT(DIM, COMP)                                                                      \
    inline varying float##COMP ImplLoadTex(uniform Texture##DIM##Dfloat##COMP tex, varying int index)            \
    {                                                                                                            \
        float##COMP result;                                                                                      \
        int         base = 0;                                                                                    \
        switch (tex.format)                                                                                      \
        {                                                                                                        \
        case TinyImageFormat_R8_SRGB:                                                                            \
        {                                                                                                        \
            base = index * 1;                                                                                    \
            result = set_comp_or_0(result, 0, READ_SRGB_BYTE(tex, base, 0));                                     \
            if (COMP > 1)                                                                                        \
                result = set_comp_or_0(result, 1, 0.0f);                                                         \
            if (COMP > 2)                                                                                        \
                result = set_comp_or_0(result, 2, 0.0f);                                                         \
            if (COMP > 3)                                                                                        \
                result = set_comp_or_0(result, 3, 0.0f);                                                         \
            break;                                                                                               \
        }                                                                                                        \
        case TinyImageFormat_R8G8_UNORM:                                                                         \
        {                                                                                                        \
            base = index * 2;                                                                                    \
            result = set_comp_or_0(result, 0, READ_UNORM_BYTE(tex, base, 0));                                    \
            if (COMP > 1)                                                                                        \
                result = set_comp_or_0(result, 1, READ_UNORM_BYTE(tex, base, 1));                                \
            if (COMP > 2)                                                                                        \
                result = set_comp_or_0(result, 2, 0.0f);                                                         \
            if (COMP > 3)                                                                                        \
                result = set_comp_or_0(result, 3, 0.0f);                                                         \
            break;                                                                                               \
        }                                                                                                        \
        case TinyImageFormat_R8G8_SRGB:                                                                          \
        {                                                                                                        \
            base = index * 2;                                                                                    \
            result = set_comp_or_0(result, 0, READ_SRGB_BYTE(tex, base, 0));                                     \
            if (COMP > 1)                                                                                        \
                result = set_comp_or_0(result, 1, READ_SRGB_BYTE(tex, base, 1));                                 \
            if (COMP > 2)                                                                                        \
                result = set_comp_or_0(result, 2, 0.0f);                                                         \
            if (COMP > 3)                                                                                        \
                result = set_comp_or_0(result, 3, 0.0f);                                                         \
            break;                                                                                               \
        }                                                                                                        \
        case TinyImageFormat_R8G8B8A8_UNORM:                                                                     \
        {                                                                                                        \
            base = index * 4;                                                                                    \
            result = set_comp_or_0(result, 0, READ_UNORM_BYTE(tex, base, 0));                                    \
            if (COMP > 1)                                                                                        \
                result = set_comp_or_0(result, 1, READ_UNORM_BYTE(tex, base, 1));                                \
            if (COMP > 2)                                                                                        \
                result = set_comp_or_0(result, 2, READ_UNORM_BYTE(tex, base, 2));                                \
            if (COMP > 3)                                                                                        \
                result = set_comp_or_0(result, 3, READ_UNORM_BYTE(tex, base, 3));                                \
            break;                                                                                               \
        }                                                                                                        \
        case TinyImageFormat_R8G8B8A8_SRGB:                                                                      \
        {                                                                                                        \
            base = index * 4;                                                                                    \
            result = set_comp_or_0(result, 0, READ_SRGB_BYTE(tex, base, 0));                                     \
            if (COMP > 1)                                                                                        \
                result = set_comp_or_0(result, 1, READ_SRGB_BYTE(tex, base, 1));                                 \
            if (COMP > 2)                                                                                        \
                result = set_comp_or_0(result, 2, READ_SRGB_BYTE(tex, base, 2));                                 \
            if (COMP > 3)                                                                                        \
                result = set_comp_or_0(result, 3, READ_SRGB_BYTE(tex, base, 3));                                 \
            break;                                                                                               \
        }                                                                                                        \
        case TinyImageFormat_R32G32_SFLOAT:                                                                      \
        {                                                                                                        \
            uniform float* fdata = (uniform float*)tex.data;                                                     \
            result = set_comp_or_0(result, 0, fdata[index * 2 + 0]);                                             \
            if (COMP > 1)                                                                                        \
                result = set_comp_or_0(result, 1, fdata[index * 2 + 1]);                                         \
            if (COMP > 2)                                                                                        \
                result = set_comp_or_0(result, 2, 0.0f);                                                         \
            if (COMP > 3)                                                                                        \
                result = set_comp_or_0(result, 3, 0.0f);                                                         \
            break;                                                                                               \
        }                                                                                                        \
        case TinyImageFormat_R32G32B32A32_SFLOAT:                                                                \
        {                                                                                                        \
            uniform float* fdata = (uniform float*)tex.data;                                                     \
            result = set_comp_or_0(result, 0, fdata[index * 4 + 0]);                                             \
            if (COMP > 1)                                                                                        \
                result = set_comp_or_0(result, 1, fdata[index * 4 + 1]);                                         \
            if (COMP > 2)                                                                                        \
                result = set_comp_or_0(result, 2, fdata[index * 4 + 2]);                                         \
            if (COMP > 3)                                                                                        \
                result = set_comp_or_0(result, 3, fdata[index * 4 + 3]);                                         \
            break;                                                                                               \
        }                                                                                                        \
        default:                                                                                                 \
        {                                                                                                        \
            result = make_float##COMP(0.0f);                                                                     \
            break;                                                                                               \
        }                                                                                                        \
        }                                                                                                        \
        return result;                                                                                           \
    }                                                                                                            \
                                                                                                                 \
    inline void ImplWriteTex(uniform Texture##DIM##Dfloat##COMP tex, varying int index, varying float##COMP val) \
    {                                                                                                            \
        int base = 0;                                                                                            \
        switch (tex.format)                                                                                      \
        {                                                                                                        \
        case TinyImageFormat_R8_SRGB:                                                                            \
        {                                                                                                        \
            base = index * 1;                                                                                    \
            WRITE_SRGB_BYTE(tex, base, 0, get_comp_or_0(val, 0));                                                \
            break;                                                                                               \
        }                                                                                                        \
        case TinyImageFormat_R8G8_UNORM:                                                                         \
        {                                                                                                        \
            base = index * 2;                                                                                    \
            WRITE_UNORM_BYTE(tex, base, 0, get_comp_or_0(val, 0));                                               \
            if (COMP > 1)                                                                                        \
                WRITE_UNORM_BYTE(tex, base, 1, get_comp_or_0(val, 1));                                           \
            break;                                                                                               \
        }                                                                                                        \
        case TinyImageFormat_R8G8_SRGB:                                                                          \
        {                                                                                                        \
            base = index * 2;                                                                                    \
            WRITE_SRGB_BYTE(tex, base, 0, get_comp_or_0(val, 0));                                                \
            if (COMP > 1)                                                                                        \
                WRITE_SRGB_BYTE(tex, base, 1, get_comp_or_0(val, 1));                                            \
            break;                                                                                               \
        }                                                                                                        \
        case TinyImageFormat_R8G8B8A8_UNORM:                                                                     \
        {                                                                                                        \
            base = index * 4;                                                                                    \
            WRITE_UNORM_BYTE(tex, base, 0, get_comp_or_0(val, 0));                                               \
            if (COMP > 1)                                                                                        \
                WRITE_UNORM_BYTE(tex, base, 1, get_comp_or_0(val, 1));                                           \
            if (COMP > 2)                                                                                        \
                WRITE_UNORM_BYTE(tex, base, 2, get_comp_or_0(val, 2));                                           \
            if (COMP > 3)                                                                                        \
                WRITE_UNORM_BYTE(tex, base, 3, get_comp_or_0(val, 3));                                           \
            break;                                                                                               \
        }                                                                                                        \
        case TinyImageFormat_R8G8B8A8_SRGB:                                                                      \
        {                                                                                                        \
            base = index * 4;                                                                                    \
            WRITE_SRGB_BYTE(tex, base, 0, get_comp_or_0(val, 0));                                                \
            if (COMP > 1)                                                                                        \
                WRITE_SRGB_BYTE(tex, base, 1, get_comp_or_0(val, 1));                                            \
            if (COMP > 2)                                                                                        \
                WRITE_SRGB_BYTE(tex, base, 2, get_comp_or_0(val, 2));                                            \
            if (COMP > 3)                                                                                        \
                WRITE_SRGB_BYTE(tex, base, 3, get_comp_or_0(val, 3));                                            \
            break;                                                                                               \
        }                                                                                                        \
        case TinyImageFormat_R32G32_SFLOAT:                                                                      \
        {                                                                                                        \
            uniform float* fdata = (uniform float*)tex.data;                                                     \
            fdata[index * 2 + 0] = get_comp_or_0(val, 0);                                                        \
            if (COMP > 1)                                                                                        \
                fdata[index * 2 + 1] = get_comp_or_0(val, 1);                                                    \
            break;                                                                                               \
        }                                                                                                        \
        case TinyImageFormat_R32G32B32A32_SFLOAT:                                                                \
        {                                                                                                        \
            uniform float* fdata = (uniform float*)tex.data;                                                     \
            fdata[index * 4 + 0] = get_comp_or_0(val, 0);                                                        \
            if (COMP > 1)                                                                                        \
                fdata[index * 4 + 1] = get_comp_or_0(val, 1);                                                    \
            if (COMP > 2)                                                                                        \
                fdata[index * 4 + 2] = get_comp_or_0(val, 2);                                                    \
            if (COMP > 3)                                                                                        \
                fdata[index * 4 + 3] = get_comp_or_0(val, 3);                                                    \
            break;                                                                                               \
        }                                                                                                        \
        default:                                                                                                 \
            break;                                                                                               \
        }                                                                                                        \
    }

#define IMPLEMENT_LOAD_TEX_INT(DIM, COMP)                                                                    \
    inline varying int##COMP ImplLoadTex(uniform Texture##DIM##Dint##COMP tex, varying int index)            \
    {                                                                                                        \
        int##COMP result;                                                                                    \
        int       base = 0;                                                                                  \
        switch (tex.format)                                                                                  \
        {                                                                                                    \
        case TinyImageFormat_R8_SRGB:                                                                        \
        {                                                                                                    \
            base = index * 1;                                                                                \
            result = set_comp_or_0(result, 0, READ_BYTE_INT(tex, base, 0));                                  \
            if (COMP > 1)                                                                                    \
                result = set_comp_or_0(result, 1, 0);                                                        \
            if (COMP > 2)                                                                                    \
                result = set_comp_or_0(result, 2, 0);                                                        \
            if (COMP > 3)                                                                                    \
                result = set_comp_or_0(result, 3, 0);                                                        \
            break;                                                                                           \
        }                                                                                                    \
        case TinyImageFormat_R8G8_UNORM:                                                                     \
        {                                                                                                    \
            base = index * 2;                                                                                \
            result = set_comp_or_0(result, 0, READ_BYTE_INT(tex, base, 0));                                  \
            if (COMP > 1)                                                                                    \
                result = set_comp_or_0(result, 1, READ_BYTE_INT(tex, base, 1));                              \
            if (COMP > 2)                                                                                    \
                result = set_comp_or_0(result, 2, 0);                                                        \
            if (COMP > 3)                                                                                    \
                result = set_comp_or_0(result, 3, 0);                                                        \
            break;                                                                                           \
        }                                                                                                    \
        case TinyImageFormat_R8G8_SRGB:                                                                      \
        {                                                                                                    \
            base = index * 2;                                                                                \
            result = set_comp_or_0(result, 0, READ_BYTE_INT(tex, base, 0));                                  \
            if (COMP > 1)                                                                                    \
                result = set_comp_or_0(result, 1, READ_BYTE_INT(tex, base, 1));                              \
            if (COMP > 2)                                                                                    \
                result = set_comp_or_0(result, 2, 0);                                                        \
            if (COMP > 3)                                                                                    \
                result = set_comp_or_0(result, 3, 0);                                                        \
            break;                                                                                           \
        }                                                                                                    \
        case TinyImageFormat_R8G8B8A8_UNORM:                                                                 \
        {                                                                                                    \
            base = index * 4;                                                                                \
            result = set_comp_or_0(result, 0, READ_BYTE_INT(tex, base, 0));                                  \
            if (COMP > 1)                                                                                    \
                result = set_comp_or_0(result, 1, READ_BYTE_INT(tex, base, 1));                              \
            if (COMP > 2)                                                                                    \
                result = set_comp_or_0(result, 2, READ_BYTE_INT(tex, base, 2));                              \
            if (COMP > 3)                                                                                    \
                result = set_comp_or_0(result, 3, READ_BYTE_INT(tex, base, 3));                              \
            break;                                                                                           \
        }                                                                                                    \
        case TinyImageFormat_R8G8B8A8_SRGB:                                                                  \
        {                                                                                                    \
            base = index * 4;                                                                                \
            result = set_comp_or_0(result, 0, READ_BYTE_INT(tex, base, 0));                                  \
            if (COMP > 1)                                                                                    \
                result = set_comp_or_0(result, 1, READ_BYTE_INT(tex, base, 1));                              \
            if (COMP > 2)                                                                                    \
                result = set_comp_or_0(result, 2, READ_BYTE_INT(tex, base, 2));                              \
            if (COMP > 3)                                                                                    \
                result = set_comp_or_0(result, 3, READ_BYTE_INT(tex, base, 3));                              \
            break;                                                                                           \
        }                                                                                                    \
        case TinyImageFormat_R32G32_SFLOAT:                                                                  \
        {                                                                                                    \
            uniform float* fdata = (uniform float*)tex.data;                                                 \
            result = set_comp_or_0(result, 0, (int)(fdata[index * 2 + 0]));                                  \
            if (COMP > 1)                                                                                    \
                result = set_comp_or_0(result, 1, (int)(fdata[index * 2 + 1]));                              \
            if (COMP > 2)                                                                                    \
                result = set_comp_or_0(result, 2, 0);                                                        \
            if (COMP > 3)                                                                                    \
                result = set_comp_or_0(result, 3, 0);                                                        \
            break;                                                                                           \
        }                                                                                                    \
        case TinyImageFormat_R32G32B32A32_SFLOAT:                                                            \
        {                                                                                                    \
            uniform float* fdata = (uniform float*)tex.data;                                                 \
            result = set_comp_or_0(result, 0, (int)(fdata[index * 4 + 0]));                                  \
            if (COMP > 1)                                                                                    \
                result = set_comp_or_0(result, 1, (int)(fdata[index * 4 + 1]));                              \
            if (COMP > 2)                                                                                    \
                result = set_comp_or_0(result, 2, (int)(fdata[index * 4 + 2]));                              \
            if (COMP > 3)                                                                                    \
                result = set_comp_or_0(result, 3, (int)(fdata[index * 4 + 3]));                              \
            break;                                                                                           \
        }                                                                                                    \
        default:                                                                                             \
        {                                                                                                    \
            result = make_int##COMP(0);                                                                      \
            break;                                                                                           \
        }                                                                                                    \
        }                                                                                                    \
        return result;                                                                                       \
    }                                                                                                        \
                                                                                                             \
    inline void ImplWriteTex(uniform Texture##DIM##Dint##COMP tex, varying int index, varying int##COMP val) \
    {                                                                                                        \
        int base = 0;                                                                                        \
        switch (tex.format)                                                                                  \
        {                                                                                                    \
        case TinyImageFormat_R8_SRGB:                                                                        \
        {                                                                                                    \
            base = index * 1;                                                                                \
            WRITE_BYTE_INT(tex, base, 0, get_comp_or_0(val, 0));                                             \
            break;                                                                                           \
        }                                                                                                    \
        case TinyImageFormat_R8G8_UNORM:                                                                     \
        {                                                                                                    \
            base = index * 2;                                                                                \
            WRITE_BYTE_INT(tex, base, 0, get_comp_or_0(val, 0));                                             \
            if (COMP > 1)                                                                                    \
                WRITE_BYTE_INT(tex, base, 1, get_comp_or_0(val, 1));                                         \
            break;                                                                                           \
        }                                                                                                    \
        case TinyImageFormat_R8G8_SRGB:                                                                      \
        {                                                                                                    \
            base = index * 2;                                                                                \
            WRITE_BYTE_INT(tex, base, 0, get_comp_or_0(val, 0));                                             \
            if (COMP > 1)                                                                                    \
                WRITE_BYTE_INT(tex, base, 1, get_comp_or_0(val, 1));                                         \
            break;                                                                                           \
        }                                                                                                    \
        case TinyImageFormat_R8G8B8A8_UNORM:                                                                 \
        {                                                                                                    \
            base = index * 4;                                                                                \
            WRITE_BYTE_INT(tex, base, 0, get_comp_or_0(val, 0));                                             \
            if (COMP > 1)                                                                                    \
                WRITE_BYTE_INT(tex, base, 1, get_comp_or_0(val, 1));                                         \
            if (COMP > 2)                                                                                    \
                WRITE_BYTE_INT(tex, base, 2, get_comp_or_0(val, 2));                                         \
            if (COMP > 3)                                                                                    \
                WRITE_BYTE_INT(tex, base, 3, get_comp_or_0(val, 3));                                         \
            break;                                                                                           \
        }                                                                                                    \
        case TinyImageFormat_R8G8B8A8_SRGB:                                                                  \
        {                                                                                                    \
            base = index * 4;                                                                                \
            WRITE_BYTE_INT(tex, base, 0, get_comp_or_0(val, 0));                                             \
            if (COMP > 1)                                                                                    \
                WRITE_BYTE_INT(tex, base, 1, get_comp_or_0(val, 1));                                         \
            if (COMP > 2)                                                                                    \
                WRITE_BYTE_INT(tex, base, 2, get_comp_or_0(val, 2));                                         \
            if (COMP > 3)                                                                                    \
                WRITE_BYTE_INT(tex, base, 3, get_comp_or_0(val, 3));                                         \
            break;                                                                                           \
        }                                                                                                    \
        case TinyImageFormat_R32G32_SFLOAT:                                                                  \
        {                                                                                                    \
            uniform float* fdata = (uniform float*)tex.data;                                                 \
            fdata[index * 2 + 0] = (float)(get_comp_or_0(val, 0));                                           \
            if (COMP > 1)                                                                                    \
                fdata[index * 2 + 1] = (float)(get_comp_or_0(val, 1));                                       \
            break;                                                                                           \
        }                                                                                                    \
        case TinyImageFormat_R32G32B32A32_SFLOAT:                                                            \
        {                                                                                                    \
            uniform float* fdata = (uniform float*)tex.data;                                                 \
            fdata[index * 4 + 0] = (float)(get_comp_or_0(val, 0));                                           \
            if (COMP > 1)                                                                                    \
                fdata[index * 4 + 1] = (float)(get_comp_or_0(val, 1));                                       \
            if (COMP > 2)                                                                                    \
                fdata[index * 4 + 2] = (float)(get_comp_or_0(val, 2));                                       \
            if (COMP > 3)                                                                                    \
                fdata[index * 4 + 3] = (float)(get_comp_or_0(val, 3));                                       \
            break;                                                                                           \
        }                                                                                                    \
        default:                                                                                             \
            break;                                                                                           \
        }                                                                                                    \
    }

#define IMPLEMENT_LOAD_TEX_UINT(DIM, COMP)                                                                     \
    inline varying uint##COMP ImplLoadTex(uniform Texture##DIM##Duint##COMP tex, varying int index)            \
    {                                                                                                          \
        uint##COMP result;                                                                                     \
        int        base = 0;                                                                                   \
        switch (tex.format)                                                                                    \
        {                                                                                                      \
        case TinyImageFormat_R8_SRGB:                                                                          \
        {                                                                                                      \
            base = index * 1;                                                                                  \
            result = set_comp_or_0(result, 0, (uint)(READ_BYTE_INT(tex, base, 0)));                            \
            if (COMP > 1)                                                                                      \
                result = set_comp_or_0(result, 1, 0u);                                                         \
            if (COMP > 2)                                                                                      \
                result = set_comp_or_0(result, 2, 0u);                                                         \
            if (COMP > 3)                                                                                      \
                result = set_comp_or_0(result, 3, 0u);                                                         \
            break;                                                                                             \
        }                                                                                                      \
        case TinyImageFormat_R8G8_UNORM:                                                                       \
        {                                                                                                      \
            base = index * 2;                                                                                  \
            result = set_comp_or_0(result, 0, (uint)(READ_BYTE_INT(tex, base, 0)));                            \
            if (COMP > 1)                                                                                      \
                result = set_comp_or_0(result, 1, (uint)(READ_BYTE_INT(tex, base, 1)));                        \
            if (COMP > 2)                                                                                      \
                result = set_comp_or_0(result, 2, 0u);                                                         \
            if (COMP > 3)                                                                                      \
                result = set_comp_or_0(result, 3, 0u);                                                         \
            break;                                                                                             \
        }                                                                                                      \
        case TinyImageFormat_R8G8_SRGB:                                                                        \
        {                                                                                                      \
            base = index * 2;                                                                                  \
            result = set_comp_or_0(result, 0, (uint)(READ_BYTE_INT(tex, base, 0)));                            \
            if (COMP > 1)                                                                                      \
                result = set_comp_or_0(result, 1, (uint)(READ_BYTE_INT(tex, base, 1)));                        \
            if (COMP > 2)                                                                                      \
                result = set_comp_or_0(result, 2, 0u);                                                         \
            if (COMP > 3)                                                                                      \
                result = set_comp_or_0(result, 3, 0u);                                                         \
            break;                                                                                             \
        }                                                                                                      \
        case TinyImageFormat_R8G8B8A8_UNORM:                                                                   \
        {                                                                                                      \
            base = index * 4;                                                                                  \
            result = set_comp_or_0(result, 0, (uint)(READ_BYTE_INT(tex, base, 0)));                            \
            if (COMP > 1)                                                                                      \
                result = set_comp_or_0(result, 1, (uint)(READ_BYTE_INT(tex, base, 1)));                        \
            if (COMP > 2)                                                                                      \
                result = set_comp_or_0(result, 2, (uint)(READ_BYTE_INT(tex, base, 2)));                        \
            if (COMP > 3)                                                                                      \
                result = set_comp_or_0(result, 3, (uint)(READ_BYTE_INT(tex, base, 3)));                        \
            break;                                                                                             \
        }                                                                                                      \
        case TinyImageFormat_R8G8B8A8_SRGB:                                                                    \
        {                                                                                                      \
            base = index * 4;                                                                                  \
            result = set_comp_or_0(result, 0, (uint)(READ_BYTE_INT(tex, base, 0)));                            \
            if (COMP > 1)                                                                                      \
                result = set_comp_or_0(result, 1, (uint)(READ_BYTE_INT(tex, base, 1)));                        \
            if (COMP > 2)                                                                                      \
                result = set_comp_or_0(result, 2, (uint)(READ_BYTE_INT(tex, base, 2)));                        \
            if (COMP > 3)                                                                                      \
                result = set_comp_or_0(result, 3, (uint)(READ_BYTE_INT(tex, base, 3)));                        \
            break;                                                                                             \
        }                                                                                                      \
        case TinyImageFormat_R32G32_SFLOAT:                                                                    \
        {                                                                                                      \
            uniform float* fdata = (uniform float*)tex.data;                                                   \
            result = set_comp_or_0(result, 0, (uint)(int)(fdata[index * 2 + 0]));                              \
            if (COMP > 1)                                                                                      \
                result = set_comp_or_0(result, 1, (uint)(int)(fdata[index * 2 + 1]));                          \
            if (COMP > 2)                                                                                      \
                result = set_comp_or_0(result, 2, 0u);                                                         \
            if (COMP > 3)                                                                                      \
                result = set_comp_or_0(result, 3, 0u);                                                         \
            break;                                                                                             \
        }                                                                                                      \
        case TinyImageFormat_R32G32B32A32_SFLOAT:                                                              \
        {                                                                                                      \
            uniform float* fdata = (uniform float*)tex.data;                                                   \
            result = set_comp_or_0(result, 0, (uint)(int)(fdata[index * 4 + 0]));                              \
            if (COMP > 1)                                                                                      \
                result = set_comp_or_0(result, 1, (uint)(int)(fdata[index * 4 + 1]));                          \
            if (COMP > 2)                                                                                      \
                result = set_comp_or_0(result, 2, (uint)(int)(fdata[index * 4 + 2]));                          \
            if (COMP > 3)                                                                                      \
                result = set_comp_or_0(result, 3, (uint)(int)(fdata[index * 4 + 3]));                          \
            break;                                                                                             \
        }                                                                                                      \
        default:                                                                                               \
        {                                                                                                      \
            result = make_uint##COMP(0u);                                                                      \
            break;                                                                                             \
        }                                                                                                      \
        }                                                                                                      \
        return result;                                                                                         \
    }                                                                                                          \
                                                                                                               \
    inline void ImplWriteTex(uniform Texture##DIM##Duint##COMP tex, varying int index, varying uint##COMP val) \
    {                                                                                                          \
        int base = 0;                                                                                          \
        switch (tex.format)                                                                                    \
        {                                                                                                      \
        case TinyImageFormat_R8_SRGB:                                                                          \
        {                                                                                                      \
            base = index * 1;                                                                                  \
            WRITE_BYTE_INT(tex, base, 0, (int)(get_comp_or_0(val, 0)));                                        \
            break;                                                                                             \
        }                                                                                                      \
        case TinyImageFormat_R8G8_UNORM:                                                                       \
        {                                                                                                      \
            base = index * 2;                                                                                  \
            WRITE_BYTE_INT(tex, base, 0, (int)(get_comp_or_0(val, 0)));                                        \
            if (COMP > 1)                                                                                      \
                WRITE_BYTE_INT(tex, base, 1, (int)(get_comp_or_0(val, 1)));                                    \
            break;                                                                                             \
        }                                                                                                      \
        case TinyImageFormat_R8G8_SRGB:                                                                        \
        {                                                                                                      \
            base = index * 2;                                                                                  \
            WRITE_BYTE_INT(tex, base, 0, (int)(get_comp_or_0(val, 0)));                                        \
            if (COMP > 1)                                                                                      \
                WRITE_BYTE_INT(tex, base, 1, (int)(get_comp_or_0(val, 1)));                                    \
            break;                                                                                             \
        }                                                                                                      \
        case TinyImageFormat_R8G8B8A8_UNORM:                                                                   \
        {                                                                                                      \
            base = index * 4;                                                                                  \
            WRITE_BYTE_INT(tex, base, 0, (int)(get_comp_or_0(val, 0)));                                        \
            if (COMP > 1)                                                                                      \
                WRITE_BYTE_INT(tex, base, 1, (int)(get_comp_or_0(val, 1)));                                    \
            if (COMP > 2)                                                                                      \
                WRITE_BYTE_INT(tex, base, 2, (int)(get_comp_or_0(val, 2)));                                    \
            if (COMP > 3)                                                                                      \
                WRITE_BYTE_INT(tex, base, 3, (int)(get_comp_or_0(val, 3)));                                    \
            break;                                                                                             \
        }                                                                                                      \
        case TinyImageFormat_R8G8B8A8_SRGB:                                                                    \
        {                                                                                                      \
            base = index * 4;                                                                                  \
            WRITE_BYTE_INT(tex, base, 0, (int)(get_comp_or_0(val, 0)));                                        \
            if (COMP > 1)                                                                                      \
                WRITE_BYTE_INT(tex, base, 1, (int)(get_comp_or_0(val, 1)));                                    \
            if (COMP > 2)                                                                                      \
                WRITE_BYTE_INT(tex, base, 2, (int)(get_comp_or_0(val, 2)));                                    \
            if (COMP > 3)                                                                                      \
                WRITE_BYTE_INT(tex, base, 3, (int)(get_comp_or_0(val, 3)));                                    \
            break;                                                                                             \
        }                                                                                                      \
        case TinyImageFormat_R32G32_SFLOAT:                                                                    \
        {                                                                                                      \
            uniform float* fdata = (uniform float*)tex.data;                                                   \
            fdata[index * 2 + 0] = (float)(get_comp_or_0(val, 0));                                             \
            if (COMP > 1)                                                                                      \
                fdata[index * 2 + 1] = (float)(get_comp_or_0(val, 1));                                         \
            break;                                                                                             \
        }                                                                                                      \
        case TinyImageFormat_R32G32B32A32_SFLOAT:                                                              \
        {                                                                                                      \
            uniform float* fdata = (uniform float*)tex.data;                                                   \
            fdata[index * 4 + 0] = (float)(get_comp_or_0(val, 0));                                             \
            if (COMP > 1)                                                                                      \
                fdata[index * 4 + 1] = (float)(get_comp_or_0(val, 1));                                         \
            if (COMP > 2)                                                                                      \
                fdata[index * 4 + 2] = (float)(get_comp_or_0(val, 2));                                         \
            if (COMP > 3)                                                                                      \
                fdata[index * 4 + 3] = (float)(get_comp_or_0(val, 3));                                         \
            break;                                                                                             \
        }                                                                                                      \
        default:                                                                                               \
            break;                                                                                             \
        }                                                                                                      \
    }

// TODO
// Consider moving this into a separately compiled ISPC library that's linked to all other ISPC libraries.
// This generates A LOT of code which may slow down compilation times.

// --- FLOAT textures ---
IMPLEMENT_LOAD_TEX_FLOAT(1, 1)
IMPLEMENT_LOAD_TEX_FLOAT(2, 1)
IMPLEMENT_LOAD_TEX_FLOAT(3, 1)

IMPLEMENT_LOAD_TEX_FLOAT(1, 2)
IMPLEMENT_LOAD_TEX_FLOAT(2, 2)
IMPLEMENT_LOAD_TEX_FLOAT(3, 2)

IMPLEMENT_LOAD_TEX_FLOAT(1, 3)
IMPLEMENT_LOAD_TEX_FLOAT(2, 3)
IMPLEMENT_LOAD_TEX_FLOAT(3, 3)

IMPLEMENT_LOAD_TEX_FLOAT(1, 4)
IMPLEMENT_LOAD_TEX_FLOAT(2, 4)
IMPLEMENT_LOAD_TEX_FLOAT(3, 4)

// --- INT textures ---
IMPLEMENT_LOAD_TEX_INT(1, 1)
IMPLEMENT_LOAD_TEX_INT(2, 1)
IMPLEMENT_LOAD_TEX_INT(3, 1)

IMPLEMENT_LOAD_TEX_INT(1, 2)
IMPLEMENT_LOAD_TEX_INT(2, 2)
IMPLEMENT_LOAD_TEX_INT(3, 2)

IMPLEMENT_LOAD_TEX_INT(1, 3)
IMPLEMENT_LOAD_TEX_INT(2, 3)
IMPLEMENT_LOAD_TEX_INT(3, 3)

IMPLEMENT_LOAD_TEX_INT(1, 4)
IMPLEMENT_LOAD_TEX_INT(2, 4)
IMPLEMENT_LOAD_TEX_INT(3, 4)

// --- UINT textures ---
IMPLEMENT_LOAD_TEX_UINT(1, 1)
IMPLEMENT_LOAD_TEX_UINT(2, 1)
IMPLEMENT_LOAD_TEX_UINT(3, 1)

IMPLEMENT_LOAD_TEX_UINT(1, 2)
IMPLEMENT_LOAD_TEX_UINT(2, 2)
IMPLEMENT_LOAD_TEX_UINT(3, 2)

IMPLEMENT_LOAD_TEX_UINT(1, 3)
IMPLEMENT_LOAD_TEX_UINT(2, 3)
IMPLEMENT_LOAD_TEX_UINT(3, 3)

IMPLEMENT_LOAD_TEX_UINT(1, 4)
IMPLEMENT_LOAD_TEX_UINT(2, 4)
IMPLEMENT_LOAD_TEX_UINT(3, 4)

// Working around an ISPC bug
inline int _calc2D(int2 xy, int w)
{
    int x = xy.x;
    int y = xy.y;
    return y * w + x;
}
inline int _calc3D(int3 xyz, int w, int h)
{
    int x = xyz.x;
    int y = xyz.y;
    int z = xyz.z;
    return z * w * h + y * w + x;
}

#define LoadTex1D(TEX, SMP, P, LOD) ImplLoadTex(TEX, P)
#define LoadTex2D(TEX, SMP, P, LOD) ImplLoadTex(TEX, _calc2D((P), (TEX).width))
#define LoadTex3D(TEX, SMP, P, LOD) ImplLoadTex(TEX, _calc3D((P).x, (TEX).width, (TEX).height))

#define Load1D(TEX, P)              ImplLoadTex(TEX, P)
#define Load2D(TEX, P)              ImplLoadTex(TEX, _calc2D((P), (TEX).width))
#define Load3D(TEX, P)              ImplLoadTex(TEX, calc3D((P).x, (TEX).width, (TEX).height))

#define Write1D(TEX, P, VAL)        ImplWriteTex(TEX, P, VAL)
#define Write2D(TEX, P, VAL)        ImplWriteTex(TEX, _calc2D((P), (TEX).width), VAL)
#define Write3D(TEX, P, VAL)        ImplWriteTex(TEX, calc3D((P).x, (TEX).width, (TEX).height), VAL)

#define Tex1D(ELEM_TYPE)            uniform Texture1D##ELEM_TYPE
#define Tex2D(ELEM_TYPE)            uniform Texture2D##ELEM_TYPE
#define Tex3D(ELEM_TYPE)            uniform Texture3D##ELEM_TYPE

#define RWTex1D                     Tex1D
#define RWTex2D                     Tex2D
#define RWTex3D                     Tex3D

#define WTex1D                      Tex1D
#define WTex2D                      Tex2D
#define WTex3D                      Tex3D

#define RTex1D                      Tex1D
#define RTex2D                      Tex2D
#define RTex3D                      Tex3D

// #ifndef ORBIS
// #if !defined(ORBIS) && !defined(PROSPERO)
// #endif

// #define WRITE2D(NAME, COORD, VAL) NAME[int2(COORD.xy)] = VAL
#define WRITE2D(NAME, COORD, VAL)   Write2D(NAME, COORD.xy, VAL)
#define WRITE3D(NAME, COORD, VAL)   Write3D(NAME, COORD.xyz, VAL)

#define UNROLL_N(X)                 _Pragma(unroll)
#define UNROLL                      _Pragma(unroll)
#define LOOP
#define FLATTEN

//#define ROOT_CONSTANT(T)

#define Buffer(TYPE)           uniform TYPE * uniform
#define RWBuffer(TYPE)         uniform TYPE * uniform
#define WBuffer(TYPE)          uniform TYPE * uniform
#define CBUFFER(TYPE)          uniform TYPE * uniform

#define RWCoherentBuffer(TYPE) RWBuffer(TYPE)

#define NO_SAMPLER             0

#define _DECL_GetDimensions(TYPE)                                                               \
    inline uniform int  GetDimensions(Texture1D##TYPE tex, uniform int _) { return tex.width; } \
    inline uniform int2 GetDimensions(Texture2D##TYPE tex, uniform int _)                       \
    {                                                                                           \
        uniform int2 i = { tex.width, tex.height };                                             \
        return i;                                                                               \
    }                                                                                           \
    inline uniform int3 GetDimensions(Texture3D##TYPE tex, uniform int _)                       \
    {                                                                                           \
        uniform int3 i = { tex.width, tex.height, tex.depth };                                  \
        return i;                                                                               \
    }
_DECL_TYPES(_DECL_GetDimensions)

//#define TexCube(ELEM_TYPE)      TextureCube<ELEM_TYPE>
//#define TexCubeArray(ELEM_TYPE) TextureCubeArray<ELEM_TYPE>

#define Uniform(TYPE)                             uniform TYPE

/*#define Tex1DArray(ELEM_TYPE)   Texture1DArray<ELEM_TYPE>
#define Tex2DArray(ELEM_TYPE)   Texture2DArray<ELEM_TYPE>

#define RWTex1DArray(ELEM_TYPE) RWTexture1DArray<ELEM_TYPE>
#define RWTex2DArray(ELEM_TYPE) RWTexture2DArray<ELEM_TYPE>

#define WTex1DArray             RWTex1DArray
#define WTex2DArray             RWTex2DArray

#define RTex1DArray             RWTex1DArray
#define RTex2DArray             RWTex2DArray*/

/*#define RasterizerOrderedTex2D(ELEM_TYPE, GROUP_INDEX)      RWTex2D(ELEM_TYPE)
#define RasterizerOrderedTex2DArray(ELEM_TYPE, GROUP_INDEX) RWTex2DArray(ELEM_TYPE)*/

#define Depth2D                                   Tex2D
//#define Depth2DMS                                 Tex2DMS

#define SHADER_CONSTANT(INDEX, TYPE, NAME, VALUE) TYPE NAME = VALUE

#define FSL_CONST(TYPE, NAME)                     TYPE NAME
#define STATIC
#define INLINE inline

// TODO manually declare each inline function overload
//#define ToFloat3x3(NAME)                          ((float3x3)NAME)

#ifdef ENABLE_WAVEOPS

// The ISPC "wave" is just the width of the target SIMD registers (SSE: 4, AVX: 8, AVX512: 16)

// Since inactive lanes are masked of, this will give us the max of the active lane indices
#define WaveGetMaxActiveIndex(...) reduce_max(programIndex)

// ISPC doesn't really have helper lanes. It has active and inactive (masked off) lanes. This
// can only get called on active lanes.
inline bool WaveIsHelperLane() { return false; }

/*#define ballot_t uint4
#if !defined(ballot_t)
#endif

#if !defined(CountBallot)
#define CountBallot(B) (countbits((B).x) + countbits((B).y) + countbits((B).z) + countbits((B).w))
#endif*/

#endif

#endif // _ISPC_H
