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

#include "DataTypeReflection.h"

#include <stdint.h>

#include "../../Utilities/ThirdParty/OpenSource/Nothings/stb_ds.h" // Resizable array

#include "../../Utilities/Interfaces/IMath.h"

#include "../../Utilities/Log/Log.h"

#if defined(__GNUC__) && !defined(__clang__)
// We check for formatted io functions when using snprintf
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
#endif

TypeInfoBase* RecursiveResolveIndirection(ReflectionTypeResolver* res);

TypeInfoBase* pReflectedTypeTable[MAX_REFLECTED_TYPES];
uint32_t      gReflectedTypeCount = 0;

// Base types
TypeInfoInt  gTypeInfoU8;
TypeInfoInt  gTypeInfoU16;
TypeInfoInt  gTypeInfoU32;
TypeInfoInt  gTypeInfoU64;
TypeInfoInt  gTypeInfoS8;
TypeInfoInt  gTypeInfoS16;
TypeInfoInt  gTypeInfoS32;
TypeInfoInt  gTypeInfoS64;
TypeInfoBase gTypeInfoF32;
TypeInfoBase gTypeInfoF64;
TypeInfoBase gTypeInfoF128; // Only valid if sizeof(long double) == 16

void initDataTypeReflection()
{
    CompilerSpecificInitDataTypeReflection();

    // Some assumptions I make which I need to make sure are correct on all the platforms
    ASSERT(sizeof(float) == 4);
    ASSERT(sizeof(double) == 8);

    // Add base types

    //-V::813

    gTypeInfoU8.mBase.mKind = TYPE_INFO_TAG_INT;
    strcpy(gTypeInfoU8.mBase.pName, "u8");
    gTypeInfoU8.mBase.mSize = sizeof(uint8_t);
    gTypeInfoU8.mIsSigned = false;

    gTypeInfoU16.mBase.mKind = TYPE_INFO_TAG_INT;
    strcpy(gTypeInfoU16.mBase.pName, "u16");
    gTypeInfoU16.mBase.mSize = sizeof(uint16_t);
    gTypeInfoU16.mIsSigned = false;

    gTypeInfoU32.mBase.mKind = TYPE_INFO_TAG_INT;
    strcpy(gTypeInfoU32.mBase.pName, "u32");
    gTypeInfoU32.mBase.mSize = sizeof(uint32_t);
    gTypeInfoU32.mIsSigned = false;

    gTypeInfoU64.mBase.mKind = TYPE_INFO_TAG_INT;
    strcpy(gTypeInfoU64.mBase.pName, "u64");
    gTypeInfoU64.mBase.mSize = sizeof(uint64_t);
    gTypeInfoU64.mIsSigned = false;

    gTypeInfoS8.mBase.mKind = TYPE_INFO_TAG_INT;
    strcpy(gTypeInfoS8.mBase.pName, "s8");
    gTypeInfoS8.mBase.mSize = sizeof(int8_t);
    gTypeInfoS8.mIsSigned = true;

    gTypeInfoS16.mBase.mKind = TYPE_INFO_TAG_INT;
    strcpy(gTypeInfoS16.mBase.pName, "s16");
    gTypeInfoS16.mBase.mSize = sizeof(int16_t);
    gTypeInfoS16.mIsSigned = true;

    gTypeInfoS32.mBase.mKind = TYPE_INFO_TAG_INT;
    strcpy(gTypeInfoS32.mBase.pName, "s32");
    gTypeInfoS32.mBase.mSize = sizeof(int32_t);
    gTypeInfoS32.mIsSigned = true;

    gTypeInfoS64.mBase.mKind = TYPE_INFO_TAG_INT;
    strcpy(gTypeInfoS64.mBase.pName, "s64");
    gTypeInfoS64.mBase.mSize = sizeof(int64_t);
    gTypeInfoS64.mIsSigned = true;

    gTypeInfoF32.mKind = TYPE_INFO_TAG_FLOAT;
    strcpy(gTypeInfoF32.pName, "f32");
    gTypeInfoF32.mSize = sizeof(float);

    gTypeInfoF64.mKind = TYPE_INFO_TAG_FLOAT;
    strcpy(gTypeInfoF64.pName, "f64");
    gTypeInfoF64.mSize = sizeof(double);

    if (sizeof(long double) == 16)
    {
        gTypeInfoF128.mKind = TYPE_INFO_TAG_FLOAT;
        strcpy(gTypeInfoF128.pName, "f128");
        gTypeInfoF128.mSize = sizeof(long double);
    }

    RegisterDataTypeReflection((TypeInfoBase*)&gTypeInfoU8);
    RegisterDataTypeReflection((TypeInfoBase*)&gTypeInfoU16);
    RegisterDataTypeReflection((TypeInfoBase*)&gTypeInfoU32);
    RegisterDataTypeReflection((TypeInfoBase*)&gTypeInfoU64);
    RegisterDataTypeReflection((TypeInfoBase*)&gTypeInfoS8);
    RegisterDataTypeReflection((TypeInfoBase*)&gTypeInfoS16);
    RegisterDataTypeReflection((TypeInfoBase*)&gTypeInfoS32);
    RegisterDataTypeReflection((TypeInfoBase*)&gTypeInfoS64);
    RegisterDataTypeReflection((TypeInfoBase*)&gTypeInfoF32);
    RegisterDataTypeReflection((TypeInfoBase*)&gTypeInfoF64);
    RegisterDataTypeReflection((TypeInfoBase*)&gTypeInfoF128);

    //-V::813

    // Resolve struct member types

    // O(n^2) for structs & enums. There shouldn't be so many struct/enum types that this ever becomes
    // a problem though.
    for (uint32_t i = 0; i < gReflectedTypeCount; i += 1)
    {
        TypeInfoBase* type = pReflectedTypeTable[i];

        if (type->mKind == TYPE_INFO_TAG_STRUCT)
        {
            TypeInfoStruct* structType = (TypeInfoStruct*)type;

            for (uint32_t m = 0; m < structType->mMemberCount; m += 1)
            {
                TypeInfoStructMember* member = &structType->pMembers[m];

                ReflectionTypeResolver res = member->mInternalTypeResolver;

                if (res.mKind == TYPE_INFO_TAG_POINTER || res.mKind == TYPE_INFO_TAG_ARRAY)
                {
                    member->pType = RecursiveResolveIndirection(&res);
                }
                else
                {
                    member->pType = lookupTypeByIdentifier(member->mInternalTypeResolver.pIdentifier);
                    member->mIsBool = member->mInternalTypeResolver.mIsBool;
                }
            }
        }
        else if (type->mKind == TYPE_INFO_TAG_ENUM)
        {
            TypeInfoEnum* enumType = (TypeInfoEnum*)type;
            TypeInfoBase* underlying = lookupTypeByIdentifier(enumType->mInternalTypeResolver.pIdentifier);
            ASSERT(underlying->mKind == TYPE_INFO_TAG_INT);
            enumType->pUnderlyingType = (TypeInfoInt*)underlying;
        }
    }
}

TypeInfoBase* lookupTypeByIdentifier(const char* identifier)
{
    for (uint32_t i = 0; i < gReflectedTypeCount; i += 1)
    {
        TypeInfoBase* type = pReflectedTypeTable[i];

        if (strcmp(type->pName, identifier) == 0)
            return type;
    }
    return NULL;
}

const char* lookupEnumName(TypeInfoEnum* enumInfo, uint64_t valueU64)
{
    for (uint32_t i = 0; i < enumInfo->mMemberCount; i += 1)
    {
        TypeInfoEnumMember member = enumInfo->pMembers[i];

        if (member.mValueU64 == valueU64)
            return member.pName;
    }
    return NULL;
}
const char* lookupEnumDisplayName(TypeInfoEnum* enumInfo, uint64_t valueU64)
{
    for (uint32_t i = 0; i < enumInfo->mMemberCount; i += 1)
    {
        TypeInfoEnumMember member = enumInfo->pMembers[i];

        if (member.mValueU64 == valueU64)
            return enumInfo->pDisplayNames[i];
    }
    return NULL;
}

void getMemberMinMax(TypeInfoStructMember m, void* pMin, void* pMax)
{
    ASSERTMSG(m.pType, "Tried calling getMemberMinMax on a TypeInfoStructMember who's type was not reflected.");
    if (m.mMinMaxSet)
    {
        memcpy(pMin, m.pMinMem, m.pType->mSize);
        memcpy(pMax, m.pMaxMem, m.pType->mSize);
    }
    else
    {
        // Return default min max
        switch (m.pType->mKind)
        {
        case TYPE_INFO_TAG_INT:
        {
            uint64_t zero = 0;
            uint64_t one = 1;
            memcpy(pMin, &zero, m.pType->mSize);
            memcpy(pMax, &one, m.pType->mSize);
            break;
        }
        case TYPE_INFO_TAG_FLOAT:
        {
            *(float*)pMin = 0.0f;
            *(float*)pMax = 1.0f;
            break;
        }
        case TYPE_INFO_TAG_STRUCT:
        {
            float2 min2 = { .x = 0.0f, .y = 0.0f };
            float2 max2 = { .x = 1.0f, .y = 1.0f };

            float3 min3 = { .x = 0.0f, .y = 0.0f, .z = 0.0f };
            float3 max3 = { .x = 1.0f, .y = 1.0f, .z = 1.0f };

            float4 min4 = { .x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 0.0f };
            float4 max4 = { .x = 1.0f, .y = 1.0f, .z = 1.0f, .w = 1.0f };

            if (strcmp(m.pType->pName, "float2") == 0)
            {
                memcpy(pMin, &min2, sizeof(float) * 2);
                memcpy(pMax, &max2, sizeof(float) * 2);
            }
            else if (strcmp(m.pType->pName, "float3") == 0)
            {
                memcpy(pMin, &min3, sizeof(float) * 3);
                memcpy(pMax, &max3, sizeof(float) * 3);
            }
            else if (strcmp(m.pType->pName, "float4") == 0)
            {
                memcpy(pMin, &min4, sizeof(float) * 4);
                memcpy(pMax, &max4, sizeof(float) * 4);
            }
            else
            {
                ASSERTMSG(false, "Tried to call getMemberMinMax on struct member '%s %s;' But its type does not have a concept of min/max.",
                          m.pType->pName, m.pName);
            }

            break;
        }

        default:
            ASSERTMSG(false, "Tried to call getMemberMinMax on struct member '%s %s;' But its type does not have a concept of min/max.",
                      m.pType->pName, m.pName);
        }
    }
}

void exitDataTypeReflection()
{
    for (uint32_t i = 0; i < getDataTypeTableCount(); i += 1)
    {
        TypeInfoBase* type = pReflectedTypeTable[i];

        if (type->mKind == TYPE_INFO_TAG_STRUCT)
        {
            TypeInfoStruct* structType = (TypeInfoStruct*)type;

            if (structType->mMemberCount > 0)
            {
                arrfree(structType->pMembers);
            }
        }
        else if (type->mKind == TYPE_INFO_TAG_ENUM)
        {
            TypeInfoEnum* enumType = (TypeInfoEnum*)type;

            if (enumType->mMemberCount > 0)
            {
                arrfree(enumType->pNames);
                arrfree(enumType->pMembers);
                arrfree(enumType->pDisplayNames);
            }
        }
        else if (type->mKind == TYPE_INFO_TAG_POINTER || type->mKind == TYPE_INFO_TAG_ARRAY)
        {
            tf_free(type);
        }
    }

    gReflectedTypeCount = 0;
}

TypeInfoBase** getDataTypeTable() { return pReflectedTypeTable; }
uint32_t       getDataTypeTableCount() { return gReflectedTypeCount; }

TypeInfoBase* PointifyType(TypeInfoBase* type)
{
    // We don't want duplicates, so look for the indirection type first.
    // #Speed this is where we might see a slowdown if memory is spread out.
    for (uint32_t i = 0; i < gReflectedTypeCount; i += 1)
    {
        TypeInfoBase* query = pReflectedTypeTable[i];

        if (query->mKind == TYPE_INFO_TAG_POINTER)
        {
            TypeInfoPointer* indirection = (TypeInfoPointer*)query;
            if (indirection->pPointerTo == type)
                return (TypeInfoBase*)indirection;
        }
    }

    TypeInfoPointer* indirection = (TypeInfoPointer*)tf_malloc(sizeof(TypeInfoPointer));
    memset(indirection, 0, sizeof(TypeInfoPointer));
    indirection->mBase.mKind = TYPE_INFO_TAG_POINTER;
    strcpy(indirection->mBase.pName, type->pName);
    strcat(indirection->mBase.pName, "*");
    indirection->mBase.mSize = sizeof(void*);
    indirection->pPointerTo = type;

    RegisterDataTypeReflection((TypeInfoBase*)indirection);

    return (TypeInfoBase*)indirection;
}
TypeInfoBase* ArrayifyType(TypeInfoBase* type, size_t arrayCount)
{
    // We don't want duplicates, so look for the indirection type first.
    // #Speed this is where we might see a slowdown if memory is spread out.
    for (uint32_t i = 0; i < gReflectedTypeCount; i += 1)
    {
        TypeInfoBase* query = pReflectedTypeTable[i];

        if (query->mKind == TYPE_INFO_TAG_ARRAY)
        {
            TypeInfoArray* indirection = (TypeInfoArray*)query;
            if (indirection->pElementType == type && indirection->mArrayCount == arrayCount)
                return (TypeInfoBase*)indirection;
        }
    }

    TypeInfoArray* indirection = (TypeInfoArray*)tf_malloc(sizeof(TypeInfoArray));
    memset(indirection, 0, sizeof(TypeInfoArray));
    indirection->mBase.mKind = TYPE_INFO_TAG_ARRAY;

    int32_t requiredLen = snprintf(NULL, 0, "%s[%llu]", type->pName, (unsigned long long)arrayCount);
    ASSERTMSG(requiredLen >= 0 && requiredLen < sizeof(indirection->mBase.pName),
              "A reflected array type identifier is too long or encoding error occured.");
    snprintf(indirection->mBase.pName, sizeof(indirection->mBase.pName), "%s[%llu]", type->pName, (unsigned long long)arrayCount);

    indirection->mBase.mSize = type->mSize * arrayCount;
    indirection->pElementType = type;
    indirection->mArrayCount = arrayCount;

    RegisterDataTypeReflection((TypeInfoBase*)indirection);

    return (TypeInfoBase*)indirection;
}

TypeInfoBase* RecursiveResolveIndirection(ReflectionTypeResolver* res)
{
    ASSERT(res->pIndirection != NULL);
    ASSERT(res->mKind == TYPE_INFO_TAG_POINTER || res->mKind == TYPE_INFO_TAG_ARRAY);

    TypeInfoBase* indirectionTo = NULL;

    if (res->pIndirection->mKind == TYPE_INFO_TAG_POINTER || res->pIndirection->mKind == TYPE_INFO_TAG_ARRAY)
    {
        indirectionTo = RecursiveResolveIndirection(res->pIndirection);
    }
    else
    {
        indirectionTo = lookupTypeByIdentifier(res->pIndirection->pIdentifier);
    }

    tf_free(res->pIndirection);
    res->pIndirection = NULL;

    // Type is not reflected.
    if (!indirectionTo)
        return NULL;

    if (res->mKind == TYPE_INFO_TAG_POINTER)
    {
        return PointifyType(indirectionTo);
    }
    else
    {
        return ArrayifyType(indirectionTo, res->mArrayCount);
    }
}

void RegisterDataTypeReflection(TypeInfoBase* info) { pReflectedTypeTable[gReflectedTypeCount++] = info; }

void RegisterDataTypeReflectionStructMember(TypeInfoStruct* base, TypeInfoStructMember member)
{
    arrpush(base->pMembers, member);
    base->mMemberCount += 1;
}

void RegisterDataTypeReflectionEnumMember(TypeInfoEnum* base, TypeInfoEnumMember member, const char* displayName)
{
    arrpush(base->pMembers, member);
    arrpush(base->pNames, (char*)member.pName);
    arrpush(base->pDisplayNames, (char*)(displayName ? displayName : member.pName));
    base->mMemberCount += 1;
}

ReflectionTypeResolver MakeTypeResolverFloat(size_t floatSize)
{
    ReflectionTypeResolver res = { 0 };
    res.mKind = TYPE_INFO_TAG_FLOAT;
    res.mSize = floatSize;
    res.mArrayCount = 0;
    if (floatSize == 4)
        strcpy(res.pIdentifier, "f32");
    else if (floatSize == 8)
        strcpy(res.pIdentifier, "f64");
    else if (floatSize == 16)
        strcpy(res.pIdentifier, "f128");
    else
        ASSERT(false);

    return res;
}
ReflectionTypeResolver MakeTypeResolverInt(size_t intSize, bool isSigned, bool isBool)
{
    ReflectionTypeResolver res = { 0 };
    res.mKind = TYPE_INFO_TAG_INT;
    res.mSize = intSize;
    res.mIsBool = isBool;
    res.mArrayCount = 0;
    if (isSigned)
    {
        if (intSize == 1)
            strcpy(res.pIdentifier, "s8");
        else if (intSize == 2)
            strcpy(res.pIdentifier, "s16");
        else if (intSize == 4)
            strcpy(res.pIdentifier, "s32");
        else if (intSize == 8)
            strcpy(res.pIdentifier, "s64");
        else
            ASSERT(false);
    }
    else
    {
        if (intSize == 1)
            strcpy(res.pIdentifier, "u8");
        else if (intSize == 2)
            strcpy(res.pIdentifier, "u16");
        else if (intSize == 4)
            strcpy(res.pIdentifier, "u32");
        else if (intSize == 8)
            strcpy(res.pIdentifier, "u64");
        else
            ASSERT(false);
    }

    return res;
}
ReflectionTypeResolver MakeTypeResolverEnum(size_t enumSize, bool isSigned, const char* name)
{
    ReflectionTypeResolver res = { 0 };
    res.mKind = TYPE_INFO_TAG_ENUM;
    res.mSize = enumSize;
    res.mArrayCount = 0;
    if (isSigned)
    {
        if (enumSize == 1)
            strcpy(res.pUnderlyingIdentifier, "s8");
        else if (enumSize == 2)
            strcpy(res.pUnderlyingIdentifier, "s16");
        else if (enumSize == 4)
            strcpy(res.pUnderlyingIdentifier, "s32");
        else if (enumSize == 8)
            strcpy(res.pUnderlyingIdentifier, "s64");
        else
            ASSERT(false);
    }
    else
    {
        if (enumSize == 1)
            strcpy(res.pUnderlyingIdentifier, "u8");
        else if (enumSize == 2)
            strcpy(res.pUnderlyingIdentifier, "u16");
        else if (enumSize == 4)
            strcpy(res.pUnderlyingIdentifier, "u32");
        else if (enumSize == 8)
            strcpy(res.pUnderlyingIdentifier, "u64");
        else
            ASSERT(false);
    }
    strcpy(res.pIdentifier, name);

    return res;
}
ReflectionTypeResolver MakeTypeResolverStruct(size_t structSize, const char* structName)
{
    ReflectionTypeResolver res = { 0 };
    res.mKind = TYPE_INFO_TAG_STRUCT;
    res.mSize = structSize;
    res.mArrayCount = 0;
    strcpy(res.pIdentifier, structName);

    return res;
}
ReflectionTypeResolver MakeTypeResolverArray(ReflectionTypeResolver elemResolver, size_t count)
{
    ReflectionTypeResolver res = { 0 };
    res.mKind = TYPE_INFO_TAG_ARRAY;
    res.mSize = count * elemResolver.mSize;
    res.mArrayCount = count;

    int32_t requiredLen = snprintf(NULL, 0, "%s[%llu]", elemResolver.pIdentifier, (unsigned long long)count);
    ASSERTMSG(requiredLen >= 0 && requiredLen < sizeof(res.pIdentifier),
              "A reflected array type identifier is too long or encoding error occured.");
    snprintf(res.pIdentifier, sizeof(res.pIdentifier), "%s[%llu]", elemResolver.pIdentifier, (unsigned long long)count);

    res.pIndirection = (ReflectionTypeResolver*)tf_malloc(sizeof(ReflectionTypeResolver));
    *res.pIndirection = elemResolver;

    return res;
}
ReflectionTypeResolver MakeTypeResolverPointer(ReflectionTypeResolver elemResolver)
{
    ReflectionTypeResolver res = { 0 };
    res.mKind = TYPE_INFO_TAG_POINTER;
    res.mSize = sizeof(void*);
    res.mArrayCount = 0;

    int32_t requiredLen = snprintf(NULL, 0, "%s*", elemResolver.pIdentifier);
    ASSERTMSG(requiredLen >= 0 && requiredLen < sizeof(res.pIdentifier),
              "A reflected array type identifier is too long or encoding error occured.");
    snprintf(res.pIdentifier, sizeof(res.pIdentifier), "%s*", elemResolver.pIdentifier);

    res.pIndirection = (ReflectionTypeResolver*)tf_malloc(sizeof(ReflectionTypeResolver));
    *res.pIndirection = elemResolver;

    return res;
}

///
// Compiler specific

#if defined(_MSC_VER) && !defined(__clang__)

__declspec(allocate(".ctors$A")) static volatile ConstructorFunction __ctors_start_dummy_value = 0;
__declspec(allocate(".ctors$C")) static volatile ConstructorFunction __ctors_end_dummy_value = 0;

void CompilerSpecificInitDataTypeReflection()
{
    ConstructorFunction* start = (ConstructorFunction*)(&__ctors_start_dummy_value);
    ConstructorFunction* end = (ConstructorFunction*)(&__ctors_end_dummy_value);

    for (ConstructorFunction* constr = start; constr < end; constr += 1)
    {
        if (*constr)
        {
            (*constr)();
        }
    }
}

#else // _MSC_VER

ConstructorFunction pTypeReflectionInitFunctions[MAX_REFLECTED_TYPES] = {};
uint32_t            gTypeReflectionInitFunctionsCount = 0;

// Assuming gcc/clang

void CompilerSpecificInitDataTypeReflection()
{
    for (uint32_t i = 0; i < gTypeReflectionInitFunctionsCount; i += 1)
    {
        pTypeReflectionInitFunctions[i]();
    }
}

#endif // !_MSC_VER

REFLECT_ENUM_BEGIN(TypeInfoTag)
REFLECT_ENUM_MEMBER(TypeInfoTag, TYPE_INFO_TAG_INT)
REFLECT_ENUM_MEMBER(TypeInfoTag, TYPE_INFO_TAG_FLOAT)
REFLECT_ENUM_MEMBER(TypeInfoTag, TYPE_INFO_TAG_STRUCT)
REFLECT_ENUM_MEMBER(TypeInfoTag, TYPE_INFO_TAG_ENUM)
REFLECT_ENUM_MEMBER(TypeInfoTag, TYPE_INFO_TAG_POINTER)
REFLECT_ENUM_MEMBER(TypeInfoTag, TYPE_INFO_TAG_ARRAY)
REFLECT_ENUM_END(TypeInfoTag)

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop // "-Wformat-truncation"
#endif
