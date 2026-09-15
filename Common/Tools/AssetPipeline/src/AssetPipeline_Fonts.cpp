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

#include "AssetPipeline.h"

#include "../../../OS/Interfaces/IOperatingSystem.h"
#include "../../../Utilities/Interfaces/IFileSystem.h"
#include "../../../Utilities/Interfaces/ILog.h"
#include "../../../Utilities/Interfaces/IToolFileSystem.h"
#include "../../../Utilities/Interfaces/IMath.h"
#include "../../../Utilities/Interfaces/IMemory.h"
#include "../../../Utilities/ThirdParty/OpenSource/Nothings/stb_ds.h"
#include "../../../Utilities/ThirdParty/OpenSource/Nothings/stb_truetype.h"
#include "../../../Utilities/ThirdParty/OpenSource/bstrlib/bstrlib.h"
#include "../../../Application/Interfaces/IFont.h"

#define SELECT_PIXEL(x, y, w, arr) { arr[(3 * (((y) * w) + (x)))], arr[(3 * (((y) * w) + (x))) + 1], arr[(3 * (((y) * w) + (x))) + 2] }

#define INF                        -1e24f
#define CELL_PADDING               0.0f
#define EDGE_THRESHOLD             0.0005f

typedef struct
{
    float mDist;
    float mD;
} SignedDistance;

typedef struct
{
    int32_t mLeftBearing;
    int32_t mAdvance;
    float   mIx0;
    float   mIx1;
    float   mIy0;
    float   mIy1;

    float mTx0;
    float mTx1;
    float mTy0;
    float mTy1;
} ExMetrics;

// the possible types:
// STBTT_vmove  = start of a contour
// STBTT_vline  = linear segment
// STBTT_vcurve = quadratic segment
// STBTT_vcubic = cubic segment
typedef struct
{
    int32_t color;
    float2  p[4];
    int32_t type;
} EdgeSegment;

enum EdgeColor : int32_t
{
    BLACK = 0,
    RED = 1,
    GREEN = 2,
    YELLOW = 3,
    BLUE = 4,
    MAGENTA = 5,
    CYAN = 6,
    WHITE = 7
};

static inline float median(float a, float b, float c) { return max(min(a, b), min(max(a, b), c)); }

static inline float cross(float2 a, float2 b) { return a[0] * b[1] - a[1] * b[0]; }

static inline int32_t nonzeroSign(float n) { return 2 * (n > 0) - 1; }

int32_t solveQuadratic(float x[2], float a, float b, float c)
{
    if (fabsf(a) < 1e-14)
    {
        if (fabsf(b) < 1e-14)
        {
            if (c == 0)
                return -1;
            return 0;
        }
        x[0] = -c / b;
        return 1;
    }

    float dscr = b * b - 4 * a * c;
    if (dscr > 0)
    {
        dscr = sqrtf(dscr);
        x[0] = (-b + dscr) / (2 * a);
        x[1] = (-b - dscr) / (2 * a);
        return 2;
    }
    else if (dscr == 0)
    {
        x[0] = -b / (2 * a);
        return 1;
    }
    else
    {
        return 0;
    }
}

int32_t solveCubicNormed(float* x, float a, float b, float c)
{
    float a2 = a * a;
    float q = (a2 - 3 * b) / 9;
    float r = (a * (2 * a2 - 9 * b) + 27 * c) / 54;
    float r2 = r * r;
    float q3 = q * q * q;
    float A, B;
    if (r2 < q3)
    {
        float t = r / sqrtf(q3);
        if (t < -1)
            t = -1;
        if (t > 1)
            t = 1;
        t = acosf(t);
        a /= 3;
        q = -2 * sqrtf(q);
        x[0] = q * cosf(t / 3) - a;
        x[1] = q * cosf((t + 2 * PI) / 3) - a;
        x[2] = q * cosf((t - 2 * PI) / 3) - a;
        return 3;
    }
    else
    {
        A = -powf(fabsf(r) + sqrtf(r2 - q3), 1 / 3.f);
        if (r < 0)
            A = -A;
        B = A == 0 ? 0 : q / A;
        a /= 3;
        x[0] = (A + B) - a;
        x[1] = -0.5f * (A + B) - a;
        x[2] = 0.5f * sqrtf(3.) * (A - B);
        if (fabsf(x[2]) < 1e-14f)
            return 2;
        return 1;
    }
}

int32_t solveCubic(float x[3], float a, float b, float c, float d)
{
    if (fabsf(a) < 1e-14)
        return solveQuadratic(x, b, c, d);

    return solveCubicNormed(x, b / a, c / a, d / a);
}

float2 getortho(float2 v, int32_t polarity, int32_t allow_zero)
{
    float2 result = f2Make(0, 0);
    float  len = f2Length(v);

    if (len == 0)
    {
        if (polarity)
        {
            result.x = 0;
            result.y = (allow_zero == 0) ? 1.0f : 0.0f;
        }
        else
        {
            result.x = 0;
            result.y = (allow_zero == 0) ? -1.0f : 0.0f;
        }
        return result;
    }

    if (polarity)
    {
        result.x = -v.y / len;
        result[1] = v.x / len;
    }
    else
    {
        result.x = v.y / len;
        result.y = -v.x / len;
    }
    return result;
}

int32_t pixelClash(float3 a, float3 b, float threshold)
{
    int32_t aIn = (a[0] > .5f) + (a[1] > .5f) + (a[2] > .5f) >= 2;
    int32_t bIn = (b[0] > .5f) + (b[1] > .5f) + (b[2] > .5f) >= 2;
    if (aIn != bIn)
        return 0;
    if ((a[0] > .5f && a[1] > .5f && a[2] > .5f) || (a[0] < .5f && a[1] < .5f && a[2] < .5f) || (b[0] > .5f && b[1] > .5f && b[2] > .5f) ||
        (b[0] < .5f && b[1] < .5f && b[2] < .5f))
        return 0;
    float aa, ab, ba, bb, ac, bc;
    if ((a[0] > .5f) != (b[0] > .5f) && (a[0] < .5f) != (b[0] < .5f))
    {
        aa = a[0];
        ba = b[0];
        if ((a[1] > .5f) != (b[1] > .5f) && (a[1] < .5f) != (b[1] < .5f))
        {
            ab = a[1];
            bb = b[1];
            ac = a[2];
            bc = b[2];
        }
        else if ((a[2] > .5f) != (b[2] > .5f) && (a[2] < .5f) != (b[2] < .5f))
        {
            ab = a[2];
            bb = b[2];
            ac = a[1];
            bc = b[1];
        }
        else
        {
            return 0;
        }
    }
    else if ((a[1] > .5f) != (b[1] > .5f) && (a[1] < .5f) != (b[1] < .5f) && (a[2] > .5f) != (b[2] > .5f) && (a[2] < .5f) != (b[2] < .5f))
    {
        aa = a[1];
        ba = b[1];
        ab = a[2];
        bb = b[2];
        ac = a[0];
        bc = b[0];
    }
    else
    {
        return 0;
    }
    return (fabsf(aa - ba) >= threshold) && (fabsf(ab - bb) >= threshold) && fabsf(ac - .5f) >= fabsf(bc - .5f);
}

float2 mix(float2 a, float2 b, float weight)
{
    float2 r{};
    r.x = (1 - weight) * a[0] + weight * b[0];
    r.y = (1 - weight) * a[1] + weight * b[1];
    return r;
}

float2 linearDirection(EdgeSegment* e)
{
    float2 r{};
    r.x = e->p[1][0] - e->p[0][0];
    r.y = e->p[1][1] - e->p[0][1];

    return r;
}

float2 quadraticDirection(EdgeSegment* e, float param)
{
    float2 a = e->p[1] - e->p[0];
    float2 b = e->p[2] - e->p[1];
    float2 r = mix(a, b, param);
    return r;
}

float2 cubicDirection(EdgeSegment* e, float param)
{
    float2 a, b, c, d, t;
    a = e->p[1] - e->p[0];
    b = e->p[2] - e->p[1];
    c = mix(a, b, param);

    a = e->p[3] - e->p[2];
    d = mix(b, a, param);
    t = mix(c, d, param);

    float2 r{};
    if (!t[0] && !t[1])
    {
        if (param == 0)
        {
            r[0] = e->p[2][0] - e->p[0][0];
            r[1] = e->p[2][1] - e->p[0][1];
            return r;
        }
        if (param == 1)
        {
            r[0] = e->p[3][0] - e->p[1][0];
            r[1] = e->p[3][1] - e->p[1][1];
            return r;
        }
    }

    r[0] = t[0];
    r[1] = t[1];
    return r;
}

float2 direction(EdgeSegment* e, float param)
{
    switch (e->type)
    {
    case STBTT_vline:
    {
        return linearDirection(e);
    }
    case STBTT_vcurve:
    {
        return quadraticDirection(e, param);
    }
    case STBTT_vcubic:
    {
        return cubicDirection(e, param);
    }
    }

    return {};
}

float2 linearPoint(EdgeSegment* e, float param) { return mix(e->p[0], e->p[1], param); }

float2 quadraticPoint(EdgeSegment* e, float param)
{
    float2 a = mix(e->p[0], e->p[1], param);
    float2 b = mix(e->p[1], e->p[2], param);
    return mix(a, b, param);
}

float2 cubicPoint(EdgeSegment* e, float param)
{
    float2 p12, a, b, c, d;
    p12 = mix(e->p[1], e->p[2], param);

    a = mix(e->p[0], e->p[1], param);
    b = mix(a, p12, param);

    c = mix(e->p[2], e->p[3], param);
    d = mix(p12, c, param);

    return mix(b, d, param);
}

float2 point(EdgeSegment* e, float param)
{
    switch (e->type)
    {
    case STBTT_vline:
    {
        return linearPoint(e, param);
    }
    case STBTT_vcurve:
    {
        return quadraticPoint(e, param);
    }
    case STBTT_vcubic:
    {
        return cubicPoint(e, param);
    }
    }
    return {};
}

// linear edge signed distance
SignedDistance linearDist(EdgeSegment* e, float2 origin, float* param)
{
    float2 aq, ab, eq;
    aq = origin - e->p[0];
    ab = e->p[1] - e->p[0];
    *param = f2Dot(aq, ab) / f2Dot(ab, ab);
    eq = e->p[*param > .5] - origin;

    float endpoint_distance = f2Length(eq);
    if (*param > 0 && *param < 1)
    {
        float2 ab_ortho = getortho(ab, 0, 0);
        float  ortho_dist = f2Dot(ab_ortho, aq);
        if (fabsf(ortho_dist) < endpoint_distance)
            return { ortho_dist, 0 };
    }

    ab = f2Normalize(ab);
    eq = f2Normalize(eq);
    float dist = nonzeroSign(cross(aq, ab)) * endpoint_distance;
    float d = fabsf(f2Dot(ab, eq));
    return { dist, d };
}

// quadratic edge signed distance
SignedDistance quadraticDist(EdgeSegment* e, float2 origin, float* param)
{
    float2 qa, ab, br;
    qa = e->p[0] - origin;
    ab = e->p[1] - e->p[0];

    br[0] = e->p[0][0] + e->p[2][0] - e->p[1][0] - e->p[1][0];
    br[1] = e->p[0][1] + e->p[2][1] - e->p[1][1] - e->p[1][1];

    float   a = f2Dot(br, br);
    float   b = 3 * f2Dot(ab, br);
    float   c = 2 * f2Dot(ab, ab) + f2Dot(qa, br);
    float   d = f2Dot(qa, ab);
    float   t[3];
    int32_t solutions = solveCubic(t, a, b, c, d);

    // distance from a
    float mMinDistance = nonzeroSign(cross(ab, qa)) * f2Length(qa);
    *param = -f2Dot(qa, ab) / f2Dot(ab, ab);
    {
        float2 aV = e->p[2] - e->p[1];
        float2 bV = e->p[2] - origin;

        // distance from b
        float distance = nonzeroSign(cross(aV, bV)) * f2Length(bV);
        if (fabsf(distance) < fabsf(mMinDistance))
        {
            mMinDistance = distance;

            aV = origin - e->p[1];
            bV = e->p[2] - e->p[1];
            *param = f2Dot(aV, bV) / f2Dot(bV, bV);
        }
    }

    for (int32_t i = 0; i < solutions; ++i)
    {
        if (t[i] > 0 && t[i] < 1)
        {
            float2 end_point, aV, bV;
            end_point[0] = e->p[0][0] + 2 * t[i] * ab[0] + t[i] * t[i] * br[0];
            end_point[1] = e->p[0][1] + 2 * t[i] * ab[1] + t[i] * t[i] * br[1];

            aV = e->p[2] - e->p[0];
            bV = end_point - origin;

            float distance = nonzeroSign(cross(aV, bV)) * f2Length(bV);
            if (fabsf(distance) <= fabsf(mMinDistance))
            {
                mMinDistance = distance;
                *param = t[i];
            }
        }
    }

    if (*param >= 0 && *param <= 1)
    {
        return { mMinDistance, 0 };
    }

    float2 aa, bb;
    ab = f2Normalize(ab);
    qa = f2Normalize(qa);

    aa = e->p[2] - e->p[1];
    aa = f2Normalize(aa);

    bb = e->p[2] - origin;
    bb = f2Normalize(bb);

    if (*param < 0.5f)
    {
        return { mMinDistance, fabsf(f2Dot(ab, qa)) };
    }
    else
    {
        return { mMinDistance, fabsf(f2Dot(aa, bb)) };
    }
}

// cubic edge signed distance
SignedDistance cubicDist(EdgeSegment* e, float2 origin, float* param)
{
    float2 qa, ab, br, as;
    qa = e->p[0] - origin;
    ab = e->p[1] - e->p[0];

    br[0] = e->p[2][0] - e->p[1][0] - ab[0];
    br[1] = e->p[2][1] - e->p[1][1] - ab[1];
    as[0] = (e->p[3][0] - e->p[2][0]) - (e->p[2][0] - e->p[1][0]) - br[0];
    as[1] = (e->p[3][1] - e->p[2][1]) - (e->p[2][1] - e->p[1][1]) - br[1];

    float2 ep_dir = direction(e, 0);

    // distance from a
    float mMinDistance = nonzeroSign(cross(ep_dir, qa)) * f2Length(qa);
    *param = -f2Dot(qa, ep_dir) / f2Dot(ep_dir, ep_dir);
    {
        float2 a = e->p[3] - origin;
        ep_dir = direction(e, 1);

        // distance from b
        float distance = nonzeroSign(cross(ep_dir, a)) * f2Length(a);
        if (fabsf(distance) < fabsf(mMinDistance))
        {
            mMinDistance = distance;

            a[0] = origin[0] + ep_dir[0] - e->p[3][0];
            a[1] = origin[1] + ep_dir[1] - e->p[3][1];
            *param = f2Dot(a, ep_dir) / f2Dot(ep_dir, ep_dir);
        }
    }

    const int32_t search_starts = 4;
    for (int32_t i = 0; i <= search_starts; ++i)
    {
        float t = (float)i / search_starts;
        for (int32_t step = 0;; ++step)
        {
            float2 qpt = point(e, t);
            qpt = qpt - origin;

            float2 d = direction(e, t);
            float  distance = nonzeroSign(cross(d, qpt)) * f2Length(qpt);
            if (fabsf(distance) < fabsf(mMinDistance))
            {
                mMinDistance = distance;
                *param = t;
            }
            if (step == search_starts)
                break;

            float2 d1, d2;
            d1[0] = 3 * as[0] * t * t + 6 * br[0] * t + 3 * ab[0];
            d1[1] = 3 * as[1] * t * t + 6 * br[1] * t + 3 * ab[1];
            d2[0] = 6 * as[0] * t + 6 * br[0];
            d2[1] = 6 * as[1] * t + 6 * br[1];

            t -= f2Dot(qpt, d1) / (f2Dot(d1, d1) + f2Dot(qpt, d2));
            if (t < 0 || t > 1)
                break;
        }
    }

    if (*param >= 0 && *param <= 1)
    {
        return { mMinDistance, 0 };
    }

    float2 d0 = direction(e, 0);
    float2 d1 = direction(e, 1);
    d0 = f2Normalize(d0);
    d1 = f2Normalize(d1);
    qa = f2Normalize(qa);

    float2 a = e->p[3] - origin;
    a = f2Normalize(a);

    if (*param < 0.5f)
    {
        return { mMinDistance, fabsf(f2Dot(d0, qa)) };
    }
    else
    {
        return { mMinDistance, fabsf(f2Dot(d1, a)) };
    }
}

void distToPseudo(SignedDistance* distance, float2 origin, float param, EdgeSegment* e)
{
    if (param < 0)
    {
        float2 dir = direction(e, 0);
        dir = f2Normalize(dir);

        float2 p = point(e, 0);
        float2 aq = origin - p;

        float ts = f2Dot(aq, dir);
        if (ts < 0)
        {
            float pseudo_dist = cross(aq, dir);
            if (fabsf(pseudo_dist) <= fabsf(distance->mDist))
            {
                distance->mDist = pseudo_dist;
                distance->mD = 0;
            }
        }
    }
    else if (param > 1)
    {
        float2 dir = direction(e, 1);
        dir = f2Normalize(dir);

        float2 p = point(e, 1);
        float2 bq = origin - p;

        float ts = f2Dot(bq, dir);
        if (ts > 0)
        {
            float pseudo_dist = cross(bq, dir);
            if (fabsf(pseudo_dist) <= fabsf(distance->mDist))
            {
                distance->mDist = pseudo_dist;
                distance->mD = 0;
            }
        }
    }
}

int32_t signedCompare(SignedDistance a, SignedDistance b)
{
    return fabsf(a.mDist) < fabsf(b.mDist) || (fabsf(a.mDist) == fabsf(b.mDist) && a.mD < b.mD);
}

int32_t isCorner(float2 a, float2 b, float threshold) { return f2Dot(a, b) <= 0 || fabsf(cross(a, b)) > threshold; }

void switchColor(EdgeColor* color, unsigned long long* seed, EdgeColor banned)
{
    EdgeColor combined = (EdgeColor)((*color) & banned);
    if (combined == RED || combined == GREEN || combined == BLUE)
    {
        *color = (EdgeColor)(combined ^ WHITE);
        return;
    }

    if (*color == BLACK || *color == WHITE)
    {
        static const EdgeColor start[3] = { CYAN, MAGENTA, YELLOW };
        *color = start[*seed & 3];
        *seed /= 3;
        return;
    }

    int32_t shifted = *color << (1 + (*seed & 1));
    *color = (EdgeColor)((shifted | shifted >> 3) & WHITE);
    *seed >>= 1;
}

void linearSplit(EdgeSegment* e, EdgeSegment* p1, EdgeSegment* p2, EdgeSegment* p3)
{
    float2 p = point(e, 1 / 3.0f);
    p1->p[0] = e->p[0];
    p1->p[1] = p;
    p1->color = e->color;

    p = point(e, 1 / 3.0f);
    p2->p[0] = p;

    p = point(e, 2 / 3.0f);
    p2->p[1] = p;
    p2->color = e->color;

    p = point(e, 2 / 3.0f);
    p3->p[0] = p;

    p = point(e, 2 / 3.0f);
    p3->p[1] = e->p[1];
    p3->color = e->color;
}

void quadraticSplit(EdgeSegment* e, EdgeSegment* p1, EdgeSegment* p2, EdgeSegment* p3)
{
    float2 p, a, b;

    p1->p[0] = e->p[0];
    p = mix(e->p[0], e->p[1], 1 / 3.0f);
    p1->p[1] = p;
    p = point(e, 1 / 3.0f);
    p1->p[2] = p;
    p1->color = e->color;

    p = point(e, 1 / 3.0f);
    p2->p[0] = p;
    a = mix(e->p[0], e->p[1], 5 / 9.0f);
    b = mix(e->p[1], e->p[2], 4 / 9.0f);
    p = mix(a, b, 0.5f);
    p2->p[1] = p;
    p = point(e, 2 / 3.0f);
    p2->p[2] = p;
    p2->color = e->color;

    p = point(e, 2 / 3.0f);
    p3->p[0] = p;

    p = mix(e->p[1], e->p[2], 2 / 3.0f);
    p3->p[1] = p;
    p3->p[2] = e->p[2];
    p3->color = e->color;
}

void cubicSplit(EdgeSegment* e, EdgeSegment* p1, EdgeSegment* p2, EdgeSegment* p3)
{
    float2 p, a, b, c, d;

    p1->p[0] = e->p[0]; // p1 0
    if (e->p[0] == e->p[1])
    {
        p1->p[1] = e->p[0]; // ? p1 1
    }
    else
    {
        p = mix(e->p[0], e->p[1], 1 / 3.0f);
        p1->p[1] = p; // ? p1 1
    }
    a = mix(e->p[0], e->p[1], 1 / 3.0f);
    b = mix(e->p[1], e->p[2], 1 / 3.0f);
    p = mix(a, b, 1 / 3.0f);
    p1->p[2] = p; // p1 2
    p = point(e, 1 / 3.0f);
    p1->p[3] = p; // p1 3
    p1->color = e->color;

    p = point(e, 1 / 3.0f);
    p2->p[0] = p; // p2 0
    a = mix(e->p[0], e->p[1], 1 / 3.0f);
    b = mix(e->p[1], e->p[2], 1 / 3.0f);
    c = mix(a, b, 1 / 3.0f);
    a = mix(e->p[1], e->p[2], 1 / 3.0f);
    b = mix(e->p[2], e->p[3], 1 / 3.0f);
    d = mix(a, b, 1 / 3.0f);
    p = mix(c, d, 2 / 3.0f);
    p2->p[1] = p; // p2 1
    a = mix(e->p[0], e->p[1], 2 / 3.0f);
    b = mix(e->p[1], e->p[2], 2 / 3.0f);
    c = mix(a, b, 2 / 3.0f);
    a = mix(e->p[1], e->p[2], 2 / 3.0f);
    b = mix(e->p[2], e->p[3], 2 / 3.0f);
    d = mix(a, b, 2 / 3.0f);
    p = mix(c, d, 1 / 3.0f);
    p2->p[2] = p; // p2 2
    p = point(e, 2 / 3.0f);
    p2->p[3] = p; // p2 3
    p2->color = e->color;

    p = point(e, 2 / 3.0f);
    p3->p[0] = p; // p3 0

    a = mix(e->p[1], e->p[2], 2 / 3.0f);
    b = mix(e->p[2], e->p[3], 2 / 3.0f);
    p = mix(a, b, 2 / 3.0f);
    p3->p[1] = p; // p3 1

    if (e->p[2] == e->p[3])
    {
        p3->p[2] = e->p[3]; // ? p3 2
    }
    else
    {
        p = mix(e->p[2], e->p[3], 2 / 3.0f);
        p3->p[2] = p; // ? p3 2
    }

    p3->p[3] = e->p[3]; // p3 3
}

void edgeSplit(EdgeSegment* e, EdgeSegment* p1, EdgeSegment* p2, EdgeSegment* p3)
{
    switch (e->type)
    {
    case STBTT_vline:
    {
        linearSplit(e, p1, p2, p3);
        break;
    }
    case STBTT_vcurve:
    {
        quadraticSplit(e, p1, p2, p3);
        break;
    }
    case STBTT_vcubic:
    {
        cubicSplit(e, p1, p2, p3);
        break;
    }
    }
}

float shoelace(float2 a, float2 b) { return (b[0] - a[0]) * (a[1] + b[1]); }

void flipContourDirection(stbtt_vertex* vertices, int count)
{
    if (count <= 1)
        return;

    stbtt_vertex* buf = (stbtt_vertex*)tf_malloc(count * sizeof(stbtt_vertex));
    for (int i = 0; i < count; ++i)
        buf[i] = vertices[i];

    vertices[0].x = buf[count - 1].x;
    vertices[0].y = buf[count - 1].y;
    vertices[0].type = STBTT_vmove;

    for (int i = 1; i < count; ++i)
    {
        int curr_old = count - i;
        int prev_old = curr_old - 1;

        vertices[i].type = buf[curr_old].type;

        vertices[i].x = buf[prev_old].x;
        vertices[i].y = buf[prev_old].y;

        if (buf[curr_old].type == STBTT_vcurve)
        {
            vertices[i].cx = buf[curr_old].cx;
            vertices[i].cy = buf[curr_old].cy;
        }
        else if (buf[curr_old].type == STBTT_vcubic)
        {
            vertices[i].cx = buf[curr_old].cx1;
            vertices[i].cy = buf[curr_old].cy1;
            vertices[i].cx1 = buf[curr_old].cx;
            vertices[i].cy1 = buf[curr_old].cy;
        }
    }

    tf_free(buf);
}

void fixGlyphOrientation(stbtt_vertex* vertices, int numVerts)
{
    double total_area = 0;

    int i = 0;
    while (i < numVerts)
    {
        int start_idx = i;
        int count = 1;
        while (i + 1 < numVerts && vertices[i + 1].type != STBTT_vmove)
        {
            i++;
            count++;
        }

        double contour_area = 0;
        for (int j = 0; j < count; ++j)
        {
            int curr = start_idx + j;
            int next = start_idx + (j + 1) % count;
            contour_area += (double)vertices[curr].x * vertices[next].y;
            contour_area -= (double)vertices[next].x * vertices[curr].y;
        }
        total_area += contour_area;
        i++;
    }

    if (total_area > 0)
    {
        i = 0;
        while (i < numVerts)
        {
            int start_idx = i;
            int count = 1;
            while (i + 1 < numVerts && vertices[i + 1].type != STBTT_vmove)
            {
                i++;
                count++;
            }
            flipContourDirection(&vertices[start_idx], count);
            i++;
        }
    }
}

int32_t generateGlyph(stbtt_fontinfo* font, uint32_t c, int32_t w, int32_t h, float pxRange, float* bitmap, ExMetrics* metrics,
                      int32_t autofit)
{
    // Funit to pixel scale
    float scale = stbtt_ScaleForMappingEmToPixels(font, (float)h);

    // get glyph bounding box (scaled later)
    int32_t mIx0, mIy0, mIx1, mIy1;
    float   xoff = .5, yoff = .5;
    stbtt_GetGlyphBox(font, stbtt_FindGlyphIndex(font, c), &mIx0, &mIy0, &mIx1, &mIy1);

    if (autofit)
    {
        // calculate new height
        float newh = h + (h - max(mIy1 - mIy0, mIx1 - mIx0) * scale) - 2;

        // calculate new scale
        // see 'stbtt_ScaleForMappingEmToPixels' in stb_truetype.h
        uint8_t* p = font->data + font->head + 18;
        int32_t  unitsPerEm = p[0] * 256 + p[1];
        scale = newh / unitsPerEm;

        // make sure we are centered
        xoff = .0;
        yoff = .0;
    }

    // get left offset and advance
    int32_t left_bearing, advance;
    stbtt_GetGlyphHMetrics(font, stbtt_FindGlyphIndex(font, c), &advance, &left_bearing);
    left_bearing = (int32_t)(left_bearing * scale);

    // calculate offset for centering glyph on bitmap
    float translate_x = ((w / 2.0f) - ((mIx1 - mIx0) * scale) / 2.0f - left_bearing);
    float translate_y = ((h / 2.0f) - ((mIy1 - mIy0) * scale) / 2.0f - mIy0 * scale);

    // set the glyph metrics
    // (pre-scale them)
    if (metrics)
    {
        metrics->mLeftBearing = left_bearing;
        metrics->mAdvance = advance;
        metrics->mIx0 = (float)mIx0;
        metrics->mIx1 = (float)mIx1;
        metrics->mIy0 = -(float)mIy0;
        metrics->mIy1 = -(float)mIy1;
        metrics->mTx0 = translate_x + mIx0 * scale;
        metrics->mTx1 = translate_x + mIx1 * scale;
        metrics->mTy0 = translate_y + mIy0 * scale;
        metrics->mTy1 = translate_y + mIy1 * scale;
    }

    stbtt_vertex* pVerts;
    int32_t       numVerts = stbtt_GetGlyphShape(font, stbtt_FindGlyphIndex(font, c), &pVerts);

    // figure out how many contours exist
    int32_t contourCount = 0;
    for (int32_t i = 0; i < numVerts; i++)
    {
        if (pVerts[i].type == STBTT_vmove)
            contourCount++;
    }

    if (contourCount == 0)
    {
        stbtt_FreeShape(font, pVerts);
        return 0;
    }

    fixGlyphOrientation(pVerts, numVerts);

    // determin what vertices belong to what contours
    typedef struct
    {
        int32_t mStart;
        int32_t mEnd;
    } Indices;
    Indices* pContours = NULL;
    arrsetlen(pContours, 1);

    {
        int32_t j = 0;
        for (int32_t i = 0; i < numVerts; i++)
        {
            if (pVerts[i].type == STBTT_vmove)
            {
                if (i > 0)
                {
                    pContours[j].mEnd = i; //-V595
                    Indices emptyIndices{};
                    arrpush(pContours, emptyIndices);
                    j++;
                }

                pContours[j].mStart = i;
            }
        }
        pContours[j].mEnd = numVerts;
    }

    contourCount = (int32_t)arrlen(pContours);

    typedef struct
    {
        SignedDistance mMinDistance;
        EdgeSegment*   mNearEdge;
        float          mNearParam;
    } edge_point_t;

    typedef struct
    {
        EdgeSegment* pEdges;
        int32_t      mEdgeCount;
    } Contour;

    // process pVerts into series of contour-specific edge lists
    float2   initial = { 0, 0 }; // fix this?
    Contour* pContourData = (Contour*)tf_malloc(sizeof(Contour) * contourCount);
    float    cscale = TF_MSDF_FONT_UNITS_PER_DISTANCE_UNIT;
    for (int32_t i = 0; i < contourCount; i++)
    {
        int32_t count = pContours[i].mEnd - pContours[i].mStart;
        pContourData[i].pEdges = (EdgeSegment*)tf_malloc(sizeof(EdgeSegment) * count);
        pContourData[i].mEdgeCount = 0;

        int32_t k = 0;
        for (int32_t j = pContours[i].mStart; j < pContours[i].mEnd; j++)
        {
            EdgeSegment*  e = &pContourData[i].pEdges[k];
            stbtt_vertex* v = &pVerts[j];
            e->type = v->type;
            e->color = WHITE;

            switch (v->type)
            {
            case STBTT_vmove:
            {
                float2 p = { v->x / cscale, v->y / cscale };
                initial = p;
                break;
            }

            case STBTT_vline:
            {
                float2 p = { v->x / cscale, v->y / cscale };
                e->p[0] = initial;
                e->p[1] = p;
                initial = p;
                pContourData[i].mEdgeCount++;
                k++;
                break;
            }

            case STBTT_vcurve:
            {
                float2 p = { v->x / cscale, v->y / cscale };
                float2 cV = { v->cx / cscale, v->cy / cscale };

                e->p[0] = initial;
                e->p[1] = cV;
                e->p[2] = p;

                if ((e->p[0][0] == e->p[1][0] && e->p[0][1] == e->p[1][1]) || (e->p[1][0] == e->p[2][0] && e->p[1][1] == e->p[2][1]))
                {
                    e->p[1][0] = 0.5f * (e->p[0][0] + e->p[2][0]);
                    e->p[1][1] = 0.5f * (e->p[0][1] + e->p[2][1]);
                }

                initial = p;
                pContourData[i].mEdgeCount++;
                k++;
                break;
            }

            case STBTT_vcubic:
            {
                float2 p = { v->x / cscale, v->y / cscale };
                float2 cV = { v->cx / cscale, v->cy / cscale };
                float2 c1 = { v->cx1 / cscale, v->cy1 / cscale };

                e->p[0] = initial;
                e->p[1] = cV;
                e->p[2] = c1;
                e->p[3] = p;
                initial = p;
                pContourData[i].mEdgeCount++;
                k++;
                break;
            }
            }
        }
    }

    // calculate edge-colors
    unsigned long long seed = 0;
    float              anglethreshold = 3.0;
    float              crossthreshold = sinf(anglethreshold);
    int32_t            corner_count = 0;
    for (int32_t i = 0; i < contourCount; ++i)
    {
        for (int32_t j = 0; j < pContourData[i].mEdgeCount; ++j)
        {
            corner_count++;
        }
    }

    int32_t* corners = (int32_t*)tf_malloc(sizeof(int32_t) * corner_count);
    int32_t  corner_index = 0;
    for (int32_t i = 0; i < contourCount; ++i)
    {
        if (pContourData[i].mEdgeCount > 0)
        {
            float2 prev_dir = direction(&pContourData[i].pEdges[pContourData[i].mEdgeCount - 1], 1);

            int32_t index = 0;
            for (int32_t j = 0; j < pContourData[i].mEdgeCount; ++j, ++index)
            {
                EdgeSegment* e = &pContourData[i].pEdges[j];
                float2       dir = direction(e, 0);

                dir = f2Normalize(dir);
                prev_dir = f2Normalize(prev_dir);

                if (isCorner(prev_dir, dir, crossthreshold))
                {
                    corners[corner_index++] = index;
                }
                prev_dir = direction(e, 1);
            }
        }

        if (corner_index == 0)
        {
            for (int32_t j = 0; j < pContourData[i].mEdgeCount; ++j)
            {
                pContourData[i].pEdges[j].color = WHITE;
            }
        }
        else if (corner_index == 1)
        {
            EdgeColor colors[3] = { WHITE, WHITE };
            switchColor(&colors[0], &seed, BLACK);
            colors[2] = colors[0];
            switchColor(&colors[2], &seed, BLACK);

            int32_t corner = corners[0];
            if (pContourData[i].mEdgeCount >= 3)
            {
                int32_t m = pContourData[i].mEdgeCount;
                for (int32_t j = 0; j < m; ++j)
                {
                    pContourData[i].pEdges[(corner + j) % m].color = (colors + 1)[(int32_t)(3 + 2.875 * i / (m - 1) - 1.4375 + .5) - 3];
                }
            }
            else if (pContourData[i].mEdgeCount >= 1)
            {
                EdgeSegment parts[7] = {};
                edgeSplit(&pContourData[i].pEdges[0], parts + (0 + 3 * corner), parts + (1 + 3 * corner), parts + (2 + 3 * corner));
                if (pContourData[i].mEdgeCount >= 2)
                {
                    edgeSplit(&pContourData[i].pEdges[1], parts + (3 - 3 * corner), parts + (4 - 3 * corner), parts + (5 - 3 * corner));
                    parts[0].color = parts[1].color = colors[0];
                    parts[2].color = parts[3].color = colors[1];
                    parts[4].color = parts[5].color = colors[2];
                }
                else
                {
                    parts[0].color = colors[0];
                    parts[1].color = colors[1];
                    parts[2].color = colors[2];
                }

                tf_free(pContourData[i].pEdges);
                pContourData[i].pEdges = (EdgeSegment*)tf_malloc(sizeof(EdgeSegment) * 7);
                pContourData[i].mEdgeCount = 0;

                int32_t     index = 0;
                EdgeSegment def{};
                for (int32_t j = 0; memcmp(&def, &parts[j], sizeof(EdgeSegment)) != 0; ++j)
                {
                    pContourData[i].pEdges[index++] = parts[j];
                    pContourData[i].mEdgeCount++;
                }
            }
        }
        else
        {
            int32_t spline = 0;
            int32_t start = corners[0];
            int32_t m = pContourData[i].mEdgeCount;

            EdgeColor color = WHITE;
            switchColor(&color, &seed, BLACK);

            EdgeColor initial_color = color;
            for (int32_t j = 0; j < m; ++j)
            {
                int32_t index = (start + j) % m;
                if (spline + 1 < corner_count && corners[spline + 1] == index)
                {
                    ++spline;

                    EdgeColor s = (EdgeColor)((spline == corner_count - 1) * initial_color);
                    switchColor(&color, &seed, s);
                }
                pContourData[i].pEdges[index].color = color;
            }
        }
    }
    tf_free(corners);

    // normalize shape
    for (int32_t i = 0; i < contourCount; i++)
    {
        if (pContourData[i].mEdgeCount == 1)
        {
            EdgeSegment parts[3] = {};
            edgeSplit(&pContourData[i].pEdges[0], parts + 0, parts + 1, parts + 2);
            tf_free(pContourData[i].pEdges);
            pContourData[i].pEdges = (EdgeSegment*)tf_malloc(sizeof(EdgeSegment) * 3);
            pContourData[i].mEdgeCount = 3;
            for (int32_t j = 0; j < 3; j++)
            {
                pContourData[i].pEdges[j] = parts[j];
            }
        }
    }

    // calculate pWindings
    int32_t* pWindings = (int32_t*)tf_malloc(sizeof(int32_t) * contourCount);
    for (int32_t i = 0; i < contourCount; i++)
    {
        int32_t mEdgeCount = pContourData[i].mEdgeCount;
        if (mEdgeCount == 0)
        {
            pWindings[i] = 0;
            continue;
        }

        float total = 0;

        if (mEdgeCount == 1)
        {
            float2 aV = point(&pContourData[i].pEdges[0], 0);
            float2 bV = point(&pContourData[i].pEdges[0], 1 / 3.0f);
            float2 cV = point(&pContourData[i].pEdges[0], 2 / 3.0f);
            total += shoelace(aV, bV);
            total += shoelace(bV, cV);
            total += shoelace(cV, aV);
        }
        else if (mEdgeCount == 2)
        {
            float2 aV = point(&pContourData[i].pEdges[0], 0);
            float2 bV = point(&pContourData[i].pEdges[0], 0.5f);
            float2 cV = point(&pContourData[i].pEdges[1], 0);
            float2 dV = point(&pContourData[i].pEdges[1], 0.5f);
            total += shoelace(aV, bV);
            total += shoelace(bV, cV);
            total += shoelace(cV, dV);
            total += shoelace(dV, aV);
        }
        else
        {
            float2 prev = point(&pContourData[i].pEdges[mEdgeCount - 1], 0);
            for (int32_t j = 0; j < mEdgeCount; j++)
            {
                float2 cur = point(&pContourData[i].pEdges[j], 0);
                total += shoelace(prev, cur);
                prev = cur;
            }
        }

        pWindings[i] = ((0 < total) - (total < 0));
    }

    typedef struct
    {
        float mR, mG, mB;
        float mMed;
    } MultiDistance;

    MultiDistance* pContourSD;
    pContourSD = (MultiDistance*)tf_malloc(sizeof(MultiDistance) * contourCount);

    for (int32_t y = 0; y < h; ++y)
    {
        int32_t row = mIy0 > mIy1 ? y : h - y - 1;
        for (int32_t x = 0; x < w; ++x)
        {
            float2 p = { (x + xoff - translate_x) / (scale * 64.0f), (y + yoff - translate_y) / (scale * 64.0f) };

            edge_point_t sr, sg, sb;
            sr.mNearEdge = sg.mNearEdge = sb.mNearEdge = NULL;
            sr.mNearParam = sg.mNearParam = sb.mNearParam = 0;
            sr.mMinDistance.mDist = sg.mMinDistance.mDist = sb.mMinDistance.mDist = INF;
            sr.mMinDistance.mD = sg.mMinDistance.mD = sb.mMinDistance.mD = 1;
            float   d = fabsf(INF);
            float   neg_dist = -INF;
            float   pos_dist = INF;
            int32_t winding = 0;

            for (int32_t j = 0; j < contourCount; ++j)
            {
                edge_point_t r, g, b;
                r.mNearEdge = g.mNearEdge = b.mNearEdge = NULL;
                r.mNearParam = g.mNearParam = b.mNearParam = 0;
                r.mMinDistance.mDist = g.mMinDistance.mDist = b.mMinDistance.mDist = INF;
                r.mMinDistance.mD = g.mMinDistance.mD = b.mMinDistance.mD = 1;

                for (int32_t k = 0; k < pContourData[j].mEdgeCount; ++k)
                {
                    EdgeSegment*   e = &pContourData[j].pEdges[k];
                    float          param = 0;
                    SignedDistance distance;
                    distance.mDist = INF;
                    distance.mD = 1;

                    // calculate signed distance
                    switch (e->type)
                    {
                    case STBTT_vline:
                    {
                        distance = linearDist(e, p, &param);
                        break;
                    }
                    case STBTT_vcurve:
                    {
                        distance = quadraticDist(e, p, &param);
                        break;
                    }
                    case STBTT_vcubic:
                    {
                        distance = cubicDist(e, p, &param);
                        break;
                    }
                    }

                    if (e->color & RED && signedCompare(distance, r.mMinDistance))
                    {
                        r.mMinDistance = distance;
                        r.mNearEdge = e;
                        r.mNearParam = param;
                    }
                    if (e->color & GREEN && signedCompare(distance, g.mMinDistance))
                    {
                        g.mMinDistance = distance;
                        g.mNearEdge = e;
                        g.mNearParam = param;
                    }
                    if (e->color & BLUE && signedCompare(distance, b.mMinDistance))
                    {
                        b.mMinDistance = distance;
                        b.mNearEdge = e;
                        b.mNearParam = param;
                    }
                }

                if (signedCompare(r.mMinDistance, sr.mMinDistance))
                {
                    sr = r;
                }
                if (signedCompare(g.mMinDistance, sg.mMinDistance))
                {
                    sg = g;
                }
                if (signedCompare(b.mMinDistance, sb.mMinDistance))
                {
                    sb = b;
                }

                float medMinDist = fabsf(median(r.mMinDistance.mDist, g.mMinDistance.mDist, b.mMinDistance.mDist));

                if (medMinDist < d)
                {
                    d = medMinDist;
                    winding = -pWindings[j];
                }

                if (r.mNearEdge)
                {
                    distToPseudo(&r.mMinDistance, p, r.mNearParam, r.mNearEdge);
                }
                if (g.mNearEdge)
                {
                    distToPseudo(&g.mMinDistance, p, g.mNearParam, g.mNearEdge);
                }
                if (b.mNearEdge)
                {
                    distToPseudo(&b.mMinDistance, p, b.mNearParam, b.mNearEdge);
                }

                medMinDist = median(r.mMinDistance.mDist, g.mMinDistance.mDist, b.mMinDistance.mDist);
                pContourSD[j].mR = r.mMinDistance.mDist;
                pContourSD[j].mG = g.mMinDistance.mDist;
                pContourSD[j].mB = b.mMinDistance.mDist;
                pContourSD[j].mMed = medMinDist;

                if (pWindings[j] > 0 && medMinDist >= 0 && fabsf(medMinDist) < fabsf(pos_dist))
                {
                    pos_dist = medMinDist;
                }
                if (pWindings[j] < 0 && medMinDist <= 0 && fabsf(medMinDist) < fabsf(neg_dist))
                {
                    neg_dist = medMinDist;
                }
            }

            if (sr.mNearEdge)
            {
                distToPseudo(&sr.mMinDistance, p, sr.mNearParam, sr.mNearEdge);
            }
            if (sg.mNearEdge)
            {
                distToPseudo(&sg.mMinDistance, p, sg.mNearParam, sg.mNearEdge);
            }
            if (sb.mNearEdge)
            {
                distToPseudo(&sb.mMinDistance, p, sb.mNearParam, sb.mNearEdge);
            }

            MultiDistance msd;
            msd.mR = msd.mG = msd.mB = msd.mMed = INF;
            if (pos_dist >= 0 && fabsf(pos_dist) <= fabsf(neg_dist))
            {
                msd.mMed = INF;
                winding = 1;
                for (int32_t i = 0; i < contourCount; ++i)
                {
                    if (pWindings[i] > 0 && pContourSD[i].mMed > msd.mMed && fabsf(pContourSD[i].mMed) < fabsf(neg_dist))
                    {
                        msd = pContourSD[i];
                    }
                }
            }
            else if (neg_dist <= 0 && fabsf(neg_dist) <= fabsf(pos_dist))
            {
                msd.mMed = -INF;
                winding = -1;
                for (int32_t i = 0; i < contourCount; ++i)
                {
                    if (pWindings[i] < 0 && pContourSD[i].mMed < msd.mMed && fabsf(pContourSD[i].mMed) < fabsf(pos_dist))
                    {
                        msd = pContourSD[i];
                    }
                }
            }

            for (int32_t i = 0; i < contourCount; ++i)
            {
                if (pWindings[i] != winding && fabsf(pContourSD[i].mMed) < fabsf(msd.mMed))
                {
                    msd = pContourSD[i];
                }
            }
            if (median(sr.mMinDistance.mDist, sg.mMinDistance.mDist, sb.mMinDistance.mDist) == msd.mMed)
            {
                msd.mR = sr.mMinDistance.mDist;
                msd.mG = sg.mMinDistance.mDist;
                msd.mB = sb.mMinDistance.mDist;
            }

            int32_t index = 3 * ((row * w) + x);
            bitmap[index] = (float)msd.mR / pxRange + .5f;     // r
            bitmap[index + 1] = (float)msd.mG / pxRange + .5f; // g
            bitmap[index + 2] = (float)msd.mB / pxRange + .5f; // b
        }
    }

    for (int32_t i = 0; i < contourCount; i++)
    {
        tf_free(pContourData[i].pEdges);
    }

    tf_free(pContourData);
    tf_free(pContourSD);
    arrfree(pContours);
    tf_free(pWindings);
    stbtt_FreeShape(font, pVerts);

    // msdf error correction
    typedef struct
    {
        int32_t x, y;
    } Clashes;
    Clashes* clashes = (Clashes*)tf_malloc(sizeof(Clashes) * w * h);
    int32_t  cindex = 0;

    float tx = EDGE_THRESHOLD / (scale * pxRange);
    float ty = EDGE_THRESHOLD / (scale * pxRange);
    for (int32_t y = 0; y < h; y++)
    {
        for (int32_t x = 0; x < w; x++)
        {
            if ((x > 0 && pixelClash(SELECT_PIXEL(x, y, w, bitmap), SELECT_PIXEL(maxi(x - 1, 0), y, w, bitmap), tx)) ||
                (x < w - 1 && pixelClash(SELECT_PIXEL(x, y, w, bitmap), SELECT_PIXEL(mini(x + 1, w - 1), y, w, bitmap), tx)) ||
                (y > 0 && pixelClash(SELECT_PIXEL(x, y, w, bitmap), SELECT_PIXEL(x, maxi(y - 1, 0), w, bitmap), ty)) ||
                (y < h - 1 && pixelClash(SELECT_PIXEL(x, y, w, bitmap), SELECT_PIXEL(x, mini(y + 1, h - 1), w, bitmap), ty)))
            {
                clashes[cindex].x = x;
                clashes[cindex++].y = y;
            }
        }
    }

    for (int32_t i = 0; i < cindex; i++)
    {
        int32_t index = 3 * ((clashes[i].y * w) + clashes[i].x);
        float   med = median(bitmap[index], bitmap[index + 1], bitmap[index + 2]);
        bitmap[index + 0] = med;
        bitmap[index + 1] = med;
        bitmap[index + 2] = med;
    }
    tf_free(clashes);

    return 1;
}

static void calculateOverlappedBounds(float glyphResolution, float4 bound, float4 texcoord, float4* pOverlapedBound,
                                      float4* pOverlapedTexcoord)
{
    const float pixelOverlappingOffset = 1.0f;

    texcoord = texcoord / (glyphResolution - pixelOverlappingOffset);
    float texcoordWidth = texcoord.z - texcoord.x;
    float texcoordHeight = texcoord.w - texcoord.y;

    if (texcoordWidth != 0 && texcoordHeight != 0)
    {
        float4 coord = bound;
        float2 glyphExtends = float2(fabsf(coord.z - coord.x), fabsf(coord.w - coord.y)) / 2.0f;
        glyphExtends = float2(glyphExtends.x / texcoordWidth, glyphExtends.y / texcoordHeight);

        float2 glyphCenter = float2(coord.z + coord.x, coord.w + coord.y) / 2.0f;
        float2 glyphOffset = float2(texcoord.z + texcoord.x, texcoord.w + texcoord.y) / 2.0f - float2(0.5f, 0.5f);
        glyphOffset = float2(glyphOffset.x * glyphExtends.x, glyphOffset.y * glyphExtends.y) * 2;
        coord = float4(glyphCenter.x - glyphExtends.x - glyphOffset.x, glyphCenter.y - glyphExtends.y - glyphOffset.y,
                       glyphCenter.x + glyphExtends.x - glyphOffset.x, glyphCenter.y + glyphExtends.y - glyphOffset.y);
        *pOverlapedBound = coord;
    }
    else
    {
        *pOverlapedBound = bound;
    }

    float halfPixelOverlappingOffset = pixelOverlappingOffset / 2.0f;
    *pOverlapedTexcoord = float4(halfPixelOverlappingOffset, halfPixelOverlappingOffset, glyphResolution - halfPixelOverlappingOffset,
                                 glyphResolution - halfPixelOverlappingOffset);
}

void onFontFind(TFResourceDirectory resourceDir, const char* filename, void* pUserData)
{
    UNREF_PARAM(resourceDir);
    bstring** inputFileNames = (bstring**)pUserData;
    arrpush(*inputFileNames, bdynfromcstr(filename));
}

bool ProcessFonts(AssetPipelineParams* assetParams, ProcessFontsParams fontParams)
{
    // Get all gltf files
    bstring* fontFiles = NULL;
    uint32_t fontFileCount = 0;

    bool error = false;

    if (assetParams->mPathMode == PROCESS_MODE_FILE)
    {
        arrpush(fontFiles, bdynfromcstr(assetParams->mInFilePath));
    }
    else
    {
        DirectorySearch(assetParams->mRDInput, NULL, "otf", onFontFind, (void*)&fontFiles,
                        assetParams->mPathMode == PROCESS_MODE_DIRECTORY_RECURSIVE);
        DirectorySearch(assetParams->mRDInput, NULL, "ttf", onFontFind, (void*)&fontFiles,
                        assetParams->mPathMode == PROCESS_MODE_DIRECTORY_RECURSIVE);
    }

    fontFileCount = (uint32_t)arrlenu(fontFiles);

    for (uint32_t fontFileIndex = 0; fontFileIndex < fontFileCount; ++fontFileIndex)
    {
        const char* fileName = (char*)fontFiles[fontFileIndex].data;

        char newFileName[TF_FS_MAX_PATH] = { 0 };
        if (assetParams->mOutSubdir)
        {
            char extractedFileName[TF_FS_MAX_PATH] = {};
            fsGetPathFileName(fileName, extractedFileName);

            strcat(extractedFileName, ".msdf");
            fsAppendPathComponent(assetParams->mOutSubdir, extractedFileName, newFileName);
        }
        else
        {
            fsReplacePathExtension(fileName, "msdf", newFileName);
        }

        if (!assetParams->mSettings.force && fsFileExist(assetParams->mRDOutput, newFileName))
        {
            time_t lastModified = fsGetLastModifiedTime(assetParams->mRDInput, fileName);
            if (assetParams->mAdditionalModifiedTime != 0)
                lastModified = max(lastModified, assetParams->mAdditionalModifiedTime);
            time_t lastProcessed = fsGetLastModifiedTime(assetParams->mRDOutput, newFileName);

            if (lastModified < lastProcessed)
            {
                LOGF(eINFO, "Skipping %s", fileName);
                continue;
            }
        }

        LOGF(eINFO, "Converting %s to MSDF font data", fileName);

        TFFileStream file = {};
        if (!fsOpenStreamFromPath(assetParams->mRDInput, fileName, TF_FM_READ, &file))
        {
            LOGF(eERROR, "Failed to open font file %s", fileName);
            error = true;
            continue;
        }

        ssize_t  fileSize = fsGetStreamFileSize(&file);
        uint8_t* fileData = (uint8_t*)tf_malloc(fileSize);

        fsReadFromStream(&file, fileData, fileSize);
        fsCloseStream(&file);

        stbtt_fontinfo stb;
        if (!stbtt_InitFont(&stb, fileData, stbtt_GetFontOffsetForIndex(fileData, 0)))
        {
            fprintf(stderr, "Failed to parse font %s\n", fileName);
            tf_free(fileData);
            continue;
        }

        CreateDirectoryForFile(assetParams->mRDOutput, newFileName);

        TFFileStream fStream = {};
        if (!fsOpenStreamFromPath(assetParams->mRDOutput, newFileName, TF_FM_WRITE_ALLOW_READ, &fStream))
        {
            LOGF(eERROR, "Couldn't open file '%s' for write.", newFileName);
            error = true;
        }
        else
        {
            int32_t  glyphResSize = fontParams.mGlyphResolution * fontParams.mGlyphResolution * 3;
            uint32_t glyphDataSize = sizeof(TFMSDFGlyphInfo) + sizeof(half) * glyphResSize;

            TFMSDFFontInfo fontInfo{};
            fontInfo.mGlyphResolution = fontParams.mGlyphResolution;
            fontInfo.mPxRange = fontParams.mPxRange;
            fontInfo.mSTBFontSize = (uint32_t)fileSize;
            fontInfo.mSTBFontOffset = sizeof(TFMSDFFontInfo);
            fontInfo.mMSDFGlyphStride = glyphDataSize;
            fontInfo.mMSDFGlyphOffset = fontInfo.mSTBFontOffset + fontInfo.mSTBFontSize;
            fontInfo.mMSDFGlyphCount = fontParams.mOnlyASCII ? 128 : stb.numGlyphs;

            fsWriteToStream(&fStream, &fontInfo, sizeof(TFMSDFFontInfo));
            fsWriteToStream(&fStream, fileData, fontInfo.mSTBFontSize);

            int32_t* idxToCd = (int32_t*)tf_malloc(sizeof(int32_t) * fontInfo.mMSDFGlyphCount);
            memset(idxToCd, 0, sizeof(int32_t) * fontInfo.mMSDFGlyphCount);
            if (fontParams.mOnlyASCII)
            {
                for (int32_t cd = 0; cd < 128; cd++)
                {
                    int32_t idx = stbtt_FindGlyphIndex(&stb, cd);
                    if (idx == 0)
                    {
                        continue;
                    }

                    idxToCd[cd] = cd;
                }
            }
            else
            {
                for (int32_t cd = 0; cd < UINT16_MAX; cd++)
                {
                    int32_t idx = stbtt_FindGlyphIndex(&stb, cd);
                    if (idx == 0)
                    {
                        continue;
                    }

                    idxToCd[idx] = cd;
                }
            }

            float* pFBitmap = (float*)tf_malloc(sizeof(float) * glyphResSize);
            half*  pHBitmap = (half*)tf_malloc(sizeof(half) * glyphResSize);

            TFMSDFGlyphInfo glyphInfo{};
            ExMetrics       metrics{};
            for (uint32_t i = 0; i < fontInfo.mMSDFGlyphCount; i++)
            {
                memset(pFBitmap, 0, sizeof(float) * glyphResSize);
                int32_t result = generateGlyph(&stb, idxToCd[i], fontParams.mGlyphResolution, fontParams.mGlyphResolution,
                                               fontParams.mPxRange, pFBitmap, &metrics, 1);
                glyphInfo.mBound = f4Make(metrics.mIx0, metrics.mIy0, metrics.mIx1, metrics.mIy1);
                glyphInfo.mTexcoord = f4Make(metrics.mTx0, metrics.mTy0, metrics.mTx1, metrics.mTy1);
                glyphInfo.mAdvance = metrics.mAdvance;
                glyphInfo.mLeftBearing = metrics.mLeftBearing;
                glyphInfo.mCodepoint = (uint32_t)idxToCd[i];

                if (result)
                {
                    calculateOverlappedBounds((float)fontParams.mGlyphResolution, glyphInfo.mBound, glyphInfo.mTexcoord,
                                              &glyphInfo.mOverlapedBound, &glyphInfo.mOverlapedTexcoord);
                    for (int32_t i2 = 0; i2 < glyphResSize; i2++)
                    {
                        pHBitmap[i2] = half(pFBitmap[i2]);
                    }
                }
                else
                {
                    glyphInfo.mTexcoord = f4MakeScalar(0);
                    glyphInfo.mOverlapedBound = f4MakeScalar(0);
                    glyphInfo.mOverlapedTexcoord = f4MakeScalar(0);
                    memset(pHBitmap, 0, sizeof(half) * glyphResSize);
                }

                fsWriteToStream(&fStream, &glyphInfo, sizeof(TFMSDFGlyphInfo));
                fsWriteToStream(&fStream, pHBitmap, sizeof(half) * glyphResSize);
            }

            tf_free(pHBitmap);
            tf_free(pFBitmap);
            tf_free(idxToCd);

            if (!fsCloseStream(&fStream))
            {
                LOGF(eERROR, "Failed to close write stream for file '%s'.", newFileName);
                error = true;
            }
        }

        tf_free(fileData);
    }

    if (fontFiles)
    {
        for (uint32_t fontFileIndex = 0; fontFileIndex < fontFileCount; ++fontFileIndex)
        {
            bdestroy(&fontFiles[fontFileIndex]);
        }
        arrfree(fontFiles);
    }

    return error;
}
