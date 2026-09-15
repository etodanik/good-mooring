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

// Math utilities to store and access some things in 4x SOA in ozz
typedef float FloatInVec;
typedef uint32_t BoolInVec;


struct SoaFloat3 {
  float4 x, y, z;
};
struct SoaFloat4 {
  float4 x, y, z, w;
};
struct SoaQuaternion {
  float4 x, y, z, w;
};
TF_MATH_ALIGNED_TYPE_PRE struct SoaTransform {
  SoaFloat3 translation;
  SoaQuaternion rotation;
  SoaFloat3 scale;
} TF_MATH_ALIGNED_TYPE_POST;

TF_MATH_ALIGNED_TYPE_PRE struct AffineTransform {
  Vector3 translation;
  Quat rotation;
  Vector3 scale;

} TF_MATH_ALIGNED_TYPE_POST;

struct SoaFloat4x4 {
  SoaFloat4 cols[4];
};

static inline SoaFloat3 soaFloat3One() {
  return {Vector4(1, 1, 1, 1), Vector4(1, 1, 1, 1), Vector4(1, 1, 1, 1)};
}
static inline SoaFloat3 soaFloat3Zero() {
  return {Vector4(0, 0, 0, 0), Vector4(0, 0, 0, 0), Vector4(0, 0, 0, 0)};
}
static inline SoaQuaternion soaQuaternionIdentity() {
  return {Vector4(0, 0, 0, 0), Vector4(0, 0, 0, 0), Vector4(0, 0, 0, 0),
          Vector4(1, 1, 1, 1)};
}
static inline SoaTransform soaTransformIdentity() {
  return {soaFloat3Zero(), soaQuaternionIdentity(), soaFloat3One()};
}
static inline AffineTransform affineTransformIdentity() {
  return {Vector3(0, 0, 0), quatIdentity(), Vector3(1, 1, 1)};
}

static inline SoaFloat4x4 soaFloat4x4FromAffine(
    const SoaFloat3& _translation, const SoaQuaternion& _quaternion,
    const SoaFloat3& _scale) {
  const Vector4 zero = Vector4::zero();
  const Vector4 one = Vector4::one();
  const Vector4 two = one + one;

  const Vector4 xx = mulPerElem(_quaternion.x, _quaternion.x);
  const Vector4 xy = mulPerElem(_quaternion.x, _quaternion.y);
  const Vector4 xz = mulPerElem(_quaternion.x, _quaternion.z);
  const Vector4 xw = mulPerElem(_quaternion.x, _quaternion.w);
  const Vector4 yy = mulPerElem(_quaternion.y, _quaternion.y);
  const Vector4 yz = mulPerElem(_quaternion.y, _quaternion.z);
  const Vector4 yw = mulPerElem(_quaternion.y, _quaternion.w);
  const Vector4 zz = mulPerElem(_quaternion.z, _quaternion.z);
  const Vector4 zw = mulPerElem(_quaternion.z, _quaternion.w);

  const SoaFloat4x4 ret = {
      {{mulPerElem(_scale.x, one - (mulPerElem(two, (yy + zz)))),
        mulPerElem(mulPerElem(_scale.x, two), (xy + zw)),
        mulPerElem(mulPerElem(_scale.x, two), (xz - yw)), zero},
       {mulPerElem(mulPerElem(_scale.y, two), (xy - zw)),
        mulPerElem(_scale.y, one - (mulPerElem(two, (xx + zz)))),
        mulPerElem(mulPerElem(_scale.y, two), (yz + xw)), zero},
       {mulPerElem(mulPerElem(_scale.z, two), (xz + yw)),
        mulPerElem(mulPerElem(_scale.z, two), (yz - xw)),
        mulPerElem(_scale.z, one - (mulPerElem(two, (xx + yy)))), zero},
       {_translation.x, _translation.y, _translation.z, one}}};

  return ret;
}

inline SoaFloat3 operator+(const SoaFloat3& _a, const SoaFloat3& _b) {
  const SoaFloat3 r = {_a.x + _b.x, _a.y + _b.y, _a.z + _b.z};
  return r;
}
inline SoaFloat4 operator+(const SoaFloat4& _a, const SoaFloat4& _b) {
  const SoaFloat4 r = {_a.x + _b.x, _a.y + _b.y, _a.z + _b.z, _a.w + _b.w};
  return r;
}
inline SoaFloat3 operator-(const SoaFloat3& _a, const SoaFloat3& _b) {
  const SoaFloat3 r = {_a.x - _b.x, _a.y - _b.y, _a.z - _b.z};
  return r;
}
inline SoaFloat4 operator-(const SoaFloat4& _a, const SoaFloat4& _b) {
  const SoaFloat4 r = {_a.x - _b.x, _a.y - _b.y, _a.z - _b.z, _a.w - _b.w};
  return r;
}
inline SoaFloat3 operator-(const SoaFloat3& _v) {
  const SoaFloat3 r = {-_v.x, -_v.y, -_v.z};
  return r;
}
inline SoaFloat4 operator-(const SoaFloat4& _v) {
  const SoaFloat4 r = {-_v.x, -_v.y, -_v.z, -_v.w};
  return r;
}
inline SoaFloat3 operator*(const SoaFloat3& _a, const SoaFloat3& _b) {
  const SoaFloat3 r = {mulPerElem(_a.x, _b.x), mulPerElem(_a.y, _b.y),
                       mulPerElem(_a.z, _b.z)};
  return r;
}
inline SoaFloat4 operator*(const SoaFloat4& _a, const SoaFloat4& _b) {
  const SoaFloat4 r = {mulPerElem(_a.x, _b.x), mulPerElem(_a.y, _b.y),
                       mulPerElem(_a.z, _b.z), mulPerElem(_a.w, _b.w)};
  return r;
}
inline SoaFloat3 operator*(const SoaFloat3& _a, const Vector4& _f) {
  const SoaFloat3 r = {mulPerElem(_a.x, _f), mulPerElem(_a.y, _f),
                       mulPerElem(_a.z, _f)};
  return r;
}
inline SoaFloat4 operator*(const SoaFloat4& _a, const Vector4& _f) {
  const SoaFloat4 r = {mulPerElem(_a.x, _f), mulPerElem(_a.y, _f),
                       mulPerElem(_a.z, _f), mulPerElem(_a.w, _f)};
  return r;
}
inline SoaFloat3 operator/(const SoaFloat3& _a, const SoaFloat3& _b) {
  const SoaFloat3 r = {divPerElem(_a.x, _b.x), divPerElem(_a.y, _b.y),
                       divPerElem(_a.z, _b.z)};
  return r;
}
inline SoaFloat4 operator/(const SoaFloat4& _a, const SoaFloat4& _b) {
  const SoaFloat4 r = {divPerElem(_a.x, _b.x), divPerElem(_a.y, _b.y),
                       divPerElem(_a.z, _b.z), divPerElem(_a.w, _b.w)};
  return r;
}
inline SoaFloat3 operator/(const SoaFloat3& _a, const Vector4& _f) {
  const SoaFloat3 r = {divPerElem(_a.x, _f), divPerElem(_a.y, _f),
                       divPerElem(_a.z, _f)};
  return r;
}
inline SoaFloat4 operator/(const SoaFloat4& _a, const Vector4& _f) {
  const SoaFloat4 r = {divPerElem(_a.x, _f), divPerElem(_a.y, _f),
                       divPerElem(_a.z, _f), divPerElem(_a.w, _f)};
  return r;
}

inline SoaQuaternion operator-(const SoaQuaternion& _q) {
  const SoaQuaternion r = {-_q.x, -_q.y, -_q.z, -_q.w};
  return r;
}

inline SoaQuaternion operator+(const SoaQuaternion& _a,
                               const SoaQuaternion& _b) {
  const SoaQuaternion r = {_a.x + _b.x, _a.y + _b.y, _a.z + _b.z, _a.w + _b.w};
  return r;
}

inline SoaQuaternion operator*(const SoaQuaternion& _q, const Vector4& _f) {
  const SoaQuaternion r = {mulPerElem(_q.x, _f), mulPerElem(_q.y, _f),
                           mulPerElem(_q.z, _f), mulPerElem(_q.w, _f)};
  return r;
}

inline SoaQuaternion operator*(const SoaQuaternion& _a,
                               const SoaQuaternion& _b) {
  const SoaQuaternion r = {mulPerElem(_a.w, _b.x) + mulPerElem(_a.x, _b.w) +
                               mulPerElem(_a.y, _b.z) - mulPerElem(_a.z, _b.y),
                           mulPerElem(_a.w, _b.y) + mulPerElem(_a.y, _b.w) +
                               mulPerElem(_a.z, _b.x) - mulPerElem(_a.x, _b.z),
                           mulPerElem(_a.w, _b.z) + mulPerElem(_a.z, _b.w) +
                               mulPerElem(_a.x, _b.y) - mulPerElem(_a.y, _b.x),
                           mulPerElem(_a.w, _b.w) - mulPerElem(_a.x, _b.x) -
                               mulPerElem(_a.y, _b.y) - mulPerElem(_a.z, _b.z)};
  return r;
}

inline SoaFloat4 Lerp(const SoaFloat4& _a, const SoaFloat4& _b,
                      const Vector4& _f) {
  const SoaFloat4 r = {mulPerElem((_b.x - _a.x), _f) + _a.x,
                       mulPerElem((_b.y - _a.y), _f) + _a.y,
                       mulPerElem((_b.z - _a.z), _f) + _a.z,
                       mulPerElem((_b.w - _a.w), _f) + _a.w};
  return r;
}
inline SoaFloat3 Lerp(const SoaFloat3& _a, const SoaFloat3& _b,
                      const Vector4& _f) {
  const SoaFloat3 r = {mulPerElem((_b.x - _a.x), _f) + _a.x,
                       mulPerElem((_b.y - _a.y), _f) + _a.y,
                       mulPerElem((_b.z - _a.z), _f) + _a.z};
  return r;
}

inline SoaQuaternion NLerpEst(const SoaQuaternion& _a, const SoaQuaternion& _b,
                              const Vector4& _f) {
  const SoaFloat4 lerp = {mulPerElem((_b.x - _a.x), _f) + _a.x,
                          mulPerElem((_b.y - _a.y), _f) + _a.y,
                          mulPerElem((_b.z - _a.z), _f) + _a.z,
                          mulPerElem((_b.w - _a.w), _f) + _a.w};
  const Vector4 len2 = mulPerElem(lerp.x, lerp.x) + mulPerElem(lerp.y, lerp.y) +
                       mulPerElem(lerp.z, lerp.z) + mulPerElem(lerp.w, lerp.w);
  // Uses RSqrtEstNR (with one more Newton-Raphson step) as quaternions loose
  // much precision due to normalization.
  const Vector4 inv_len = rSqrtEstNR(len2);
  const SoaQuaternion r = {
      mulPerElem(lerp.x, inv_len), mulPerElem(lerp.y, inv_len),
      mulPerElem(lerp.z, inv_len), mulPerElem(lerp.w, inv_len)};
  return r;
}

inline Vector4Int v4_all_true() {
  const Vector4Int ret = {~0, ~0, ~0, ~0};
  return ret;
}

inline Vector4Int v4_mask_0000() {
  const Vector4Int ret = {0, 0, 0, 0};
  return ret;
}

inline Vector4Int v4_mask_f000() {
  const Vector4Int ret = {~0, 0, 0, 0};
  return ret;
}

inline Vector4Int v4_mask_0f00() {
  const Vector4Int ret = {0, ~0, 0, 0};
  return ret;
}

inline Vector4Int v4_mask_00f0() {
  const Vector4Int ret = {0, 0, ~0, 0};
  return ret;
}

inline Vector4Int v4_mask_000f() {
  const Vector4Int ret = {0, 0, 0, ~0};
  return ret;
}
inline Vector4Int v4_Load(int _x, int _y, int _z, int _w) {
  const Vector4Int ret = {_x, _y, _z, _w};
  return ret;
}

inline Vector4Int v4_LoadPtr(const int* _i) {
  const Vector4Int ret = {_i[0], _i[1], _i[2], _i[3]};
  return ret;
}

inline Vector4Int v4_all_false() {
  const Vector4Int ret = {0, 0, 0, 0};
  return ret;
}

inline Vector4Int v4_mask_sign() {
  const Vector4Int ret = {
      static_cast<int>(0x80000000), static_cast<int>(0x80000000),
      static_cast<int>(0x80000000), static_cast<int>(0x80000000)};
  return ret;
}

// Returns the estimated normalized SoaQuaternion _q.
inline SoaQuaternion NormalizeEst(const SoaQuaternion& _q) {
  const Vector4 len2 = mulPerElem(_q.x, _q.x) + mulPerElem(_q.y, _q.y) +
                       mulPerElem(_q.z, _q.z) + mulPerElem(_q.w, _q.w);
  // Uses RSqrtEstNR (with one more Newton-Raphson step) as quaternions loose
  // much precision due to normalization.
  const Vector4 inv_len = rSqrtEstNR(len2);
  const SoaQuaternion r = {mulPerElem(_q.x, inv_len), mulPerElem(_q.y, inv_len),
                           mulPerElem(_q.z, inv_len),
                           mulPerElem(_q.w, inv_len)};
  return r;
}

inline bool AreAllTrue3(const Vector4Int& _v) {
  return _v.x != 0 && _v.y != 0 && _v.z != 0;
}

inline SoaQuaternion Conjugate(const SoaQuaternion& _q) {
  const SoaQuaternion r = {-_q.x, -_q.y, -_q.z, _q.w};
  return r;
}

inline int MoveMask(const Vector4Int& _v) {
  return ((_v.x & 0x80000000) >> 31) | ((_v.y & 0x80000000) >> 30) |
         ((_v.z & 0x80000000) >> 29) | ((_v.w & 0x80000000) >> 28);
}

inline void transpose3x4(const float4 in[3], float4 out[4]) {
  out[0].x = (in[0].x);
  out[0].y = (in[1].x);
  out[0].z = (in[2].x);
  out[0].w = (0.f);
  out[1].x = (in[0].y);
  out[1].y = (in[1].y);
  out[1].z = (in[2].y);
  out[1].w = (0.f);
  out[2].x = (in[0].z);
  out[2].y = (in[1].z);
  out[2].z = (in[2].z);
  out[2].w = (0.f);
  out[3].x = (in[0].w);
  out[3].y = (in[1].w);
  out[3].z = (in[2].w);
  out[3].w = (0.f);
}

inline void transpose4x4(const float4 in[4], float4 out[4]) {
  out[0].x = (in[0].x);
  out[1].x = (in[0].y);
  out[2].x = (in[0].z);
  out[3].x = (in[0].w);
  out[0].y = (in[1].x);
  out[1].y = (in[1].y);
  out[2].y = (in[1].z);
  out[3].y = (in[1].w);
  out[0].z = (in[2].x);
  out[1].z = (in[2].y);
  out[2].z = (in[2].z);
  out[3].z = (in[2].w);
  out[0].w = (in[3].x);
  out[1].w = (in[3].y);
  out[2].w = (in[3].z);
  out[3].w = (in[3].w);
}

inline void transpose4x3(const float4 in[4], float4 out[4]) {
  out[0].x = (in[0].x);
  out[0].y = (in[1].x);
  out[0].z = (in[2].x);
  out[0].w = (in[3].x);
  out[1].x = (in[0].y);
  out[1].y = (in[1].y);
  out[1].z = (in[2].y);
  out[1].w = (in[3].y);
  out[2].x = (in[0].z);
  out[2].y = (in[1].z);
  out[2].z = (in[2].z);
  out[2].w = (in[3].z);
}

inline void transpose16x16(const float4 in[16], float4 out[16]) {
  for (int i = 0; i < 4; ++i) {
    const int i4 = i * 4;

    out[i4 + 0].x = (*((&in[0].x) + i));
    out[i4 + 0].y = (*((&in[1].x) + i));
    out[i4 + 0].z = (*((&in[2].x) + i));
    out[i4 + 0].w = (*((&in[3].x) + i));
    out[i4 + 1].x = (*((&in[4].x) + i));
    out[i4 + 1].y = (*((&in[5].x) + i));
    out[i4 + 1].z = (*((&in[6].x) + i));
    out[i4 + 1].w = (*((&in[7].x) + i));
    out[i4 + 2].x = (*((&in[8].x) + i));
    out[i4 + 2].y = (*((&in[9].x) + i));
    out[i4 + 2].z = (*((&in[10].x) + i));
    out[i4 + 2].w = (*((&in[11].x) + i));
    out[i4 + 3].x = (*((&in[12].x) + i));
    out[i4 + 3].y = (*((&in[13].x) + i));
    out[i4 + 3].z = (*((&in[14].x) + i));
    out[i4 + 3].w = (*((&in[15].x) + i));
  }
}

inline float HalfToFloat(uint16_t _h) {
  const union {
    uint32_t u;
    float f;
  } magic = {(254 - 15) << 23};
  const union {
    uint32_t u;
    float f;
  } infnan = {(127 + 16) << 23};

  const uint32_t sign = _h & 0x8000;
  const union {
    int32_t u;
    float f;
  } exp_mant = {(_h & 0x7fff) << 13};
  const union {
    float f;
    uint32_t u;
  } adjust = {exp_mant.f * magic.f};
  // Make sure Inf/NaN survive
  const union {
    uint32_t u;
    float f;
  } result = {(adjust.f >= infnan.f ? (adjust.u | 255 << 23) : adjust.u) |
              (sign << 16)};
  return result.f;
}

inline const float4 halfToFloat(int4 vecInt) {
  const float4 ret = {
      HalfToFloat(vecInt.x & 0x0000ffff), HalfToFloat(vecInt.y & 0x0000ffff),
      HalfToFloat(vecInt.z & 0x0000ffff), HalfToFloat(vecInt.w & 0x0000ffff)};
  return ret;
}

inline float rsqrtf(const float v) {
  union {
    float vh;
    int i0;
  } a;

  union {
    float vr;
    int i1;
  } b;

  a.vh = v * 0.5f;
  b.i1 = 0x5f3759df - (a.i0 >> 1);
  return b.vr * (1.5f - a.vh * b.vr * b.vr);
}

typedef union VectorFI4 {
  float4 f;
  int4 i;
} VectorFI4;
typedef union VectorIF4 {
  int4 i;
  float4 f;
} VectorIF4;

typedef union VectorDI4 {
  double4 d;
  int4 i;
} VectorDI4;
typedef union VectorID4 {
  int4 i;
  double4 d;
} VectorID4;

typedef union ScalarIF {
  int i;
  float f;
} ScalarIF;
typedef union ScalarFI {
  float f;
  int i;
} ScalarFI;

static inline int4 signBit(float4 v) {
  VectorFI4 fi = {v};
  const int4 ret = {fi.i.x & (int)(0x80000000), fi.i.y & (int)(0x80000000),
                    fi.i.z & (int)(0x80000000), fi.i.w & (int)(0x80000000)};
  return ret;
}

inline const float3 xorPerElem(float3 a, const float b) {
  VectorFI4 c{float4(a)};
  VectorFI4 d{float4(b)};
  VectorIF4 result = {};
  result.i = {c.i.x ^ d.i.x, c.i.y ^ d.i.y, c.i.z ^ d.i.z, c.i.w ^ d.i.w};
  return result.f.getXYZ();
}

static inline float4 xorPerElem(float4 a, int4 b) {
  const VectorFI4 c = {a};
  const VectorIF4 ret = {{c.i.x ^ b.x, c.i.y ^ b.y, c.i.z ^ b.z, c.i.w ^ b.w}};
  return ret.f;
}

static inline float xorPerElem(float a, int b) {
  const ScalarFI c = {a};
  ScalarFI ret;
  ret.i = c.i ^ b;
  return ret.f;
}

static inline float4 xorPerElem(float4 a, float4 b) {
  const VectorFI4 c = {a};
  const VectorFI4 d = {b};
  const VectorIF4 ret = {
      {c.i.x ^ d.i.x, c.i.y ^ d.i.y, c.i.z ^ d.i.z, c.i.w ^ d.i.w}};
  return ret.f;
}

static inline float4 orPerElem(float4 a, int4 b) {
  const VectorFI4 c = {a};
  const VectorIF4 ret = {{c.i.x | b.x, c.i.y | b.y, c.i.z | b.z, c.i.w | b.w}};
  return ret.f;
}

static inline float4 orPerElem(float4 a, float4 b) {
  const VectorFI4 c = {a};
  const VectorFI4 d = {b};
  const VectorIF4 ret = {
      {c.i.x | d.i.x, c.i.y | d.i.y, c.i.z | d.i.z, c.i.w | d.i.w}};
  return ret.f;
}

static inline float4 andPerElem(float4 a, int4 b) {
  const VectorFI4 c = {a};
  const VectorIF4 ret = {{c.i.x & b.x, c.i.y & b.y, c.i.z & b.z, c.i.w & b.w}};
  return ret.f;
}

static inline float andPerElem(float a, int4 b) {
  float4 a4(a, a, a, a);
  return andPerElem(a4, b).x;
}

static inline float4 andNotPerElem(float4 a, int4 b) {
    const VectorFI4 c = {a};
    const VectorIF4 ret = {
        {c.i.x & !b.x, c.i.y & !b.y, c.i.z & !b.z, c.i.w & !b.w}};
    return ret.f;
}

static inline float andNotPerElem(float a, int4 b) {
  const ScalarFI c = {a};
  ScalarFI ret;
  ret.i = {c.i & !b.x};
  return ret.f;
}

static inline float rcpEst(float v) {
  float ret;
  float V = v;
  float r = 0.0f;
  RCP_EST(V, r);
  ret = r;
  return ret;
}

static inline float rSqrtEstNR(float v) {
  float ret;
  float V = v;
  float r = 0.0f;
  RSQRT_EST_NR(V, r);
  ret = r;
  return ret;
}

static inline float3 rSqrtEstNR(float3 v) {
  float3 ret;
  float vX = v.x;
  float vY = v.y;
  float vZ = v.z;

  float rX = 0.0f;
  float rY = 0.0f;
  float rZ = 0.0f;

  RSQRT_EST_NR(vX, rX);
  RSQRT_EST_NR(vY, rY);
  RSQRT_EST_NR(vZ, rZ);

  ret.x = (rX);
  ret.y = (rY);
  ret.z = (rZ);

  return ret;
}

inline const Vector4 aCos(const Vector4& arg) {
  return Vector4(acosf(arg.x), acosf(arg.y), acosf(arg.z), acosf(arg.w));
}

inline const int4 mask_sign() {
  return int4(0x80000000, 0x80000000, 0x80000000, 0x80000000);
}