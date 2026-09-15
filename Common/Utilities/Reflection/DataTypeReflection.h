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

#include "../../Utilities/Interfaces/ILog.h"

#ifndef __cplusplus
#include <stdbool.h>
#include <string.h> // memcpy, memset
#endif

/*
    This is a C99 rtti system designed specifically for tooling & editors.

    Note: This is a standalone system that operates independently of C++ RTTI.
    It does NOT enable or depend on compiler RTTI settings.

    It lets you reflect types with a macros-API, which lets you use type information
    to reason about data without necessarily knowing about the type in compile-time.

    Type information comes in the form of TypeInfoXXX structs where each starts with a
    TypeInfoBase and contains a TypeInfoTag, which means you can safely check the
    typeInfo->mKind and case to the corresponding struct type.

    Example:
        TypeInfoBase *someType = ...;
        if (someType->mKind == TYPE_INFO_TAG_STRUCT) {
            TypeInfoStruct *structType = (TypeInfoStruct*)someType;
        }

    This system handles the following types:
        - Structs (TypeInfoStruct)
        - Enums (TypeInfoEnum)
        - Integers (TypeInfoInt)
        - Arrays (TypeInfoArray)
        - Pointers (TypeInfoPointer)

    By default, these types are reflected:

        enum TypeInfoTag

    It can handle nested indirections (pointer to array to pointer to array etc..)

    You can also hint reflected members with things like a description, display name,
    Flags etc, which becomes useful for things like automatically generating UI widgets
    for tooling, or for serialization.

    Example:

        REFLECT_STRUCT_BEGIN(SomeStruct)
            R_HINT_MINMAX(float, 0.0f, 10.0f)  R_HINT_DISPLAY_NAME("A Float")
            R_HINT_DESCRIPTION("This is a float property which does something")
            REFLECT_STRUCT_MEMBER(SomeStruct, R_FLOAT(float), mSomeFloatProp)
        REFLECT_STRUCT_END(SomeStruct)



    Reflection API

        REFLECT_STRUCT_BEGIN(StructType)
            - Begin reflecting a struct type. Reflecting a struct will generate a TypeInfoStruct*,
            and an array of TypeInfoStructMember.

        REFLECT_STRUCT_MEMBER(StructType, TypeSpecifier, Member)
            - Reflect a struct member and specify its type and which member it is.
            This will generate a TypeInfoStructMember will gives you information like its offset
            in the struct and which type it is. See TypeInfoXXX structs for more.

            For the TypeSpecifier, you need to specify the type in a very specific way, using one
            of the following (nestable) macros:

                R_INT(IntType)
                R_FLOAT(FloatType)
                R_ENUM(EnumType)
                R_ARRAY(ElemTypeSpecifier, Count)
                R_POINTER(PointerToTypeSpecifier)
                R_BOOL()

            Where R_ARRAY and R_POINTER requres a nested type specifier for the element/pointerto type.
            This means you can recursively make indirections like R_POINTER(R_POINTER(float))) which is
            a pointer to a pointer to a float (float**)

            This may be a bit verbose, but since we don't have any type information in C99 it's the best
            we can do.

        REFLECT_STRUCT_END(StructType)
            - End reflecting a struct type

        REFLECT_ENUM_BEGIN(EnumType)
            - Begin reflecting an enum type. Reflecting an enum will generate a TypeInfoEnum* and
            an array of TypeInfoEnumMember. The TypeInfoEnum* will also contain a TypeInfoInt* for
            the underlying int type.

        REFLECT_ENUM_MEMBER(EnumType, MemberName)
            - Reflect an enum member and specify which member it is.
            This will generate a TypeInfoEnumMember

        REFLECT_ENUM_END(EnumType)
            - End reflecting an enum type

        R_HINT_MINMAX(Type, Min, Max)
            - Hints what the min/max will be in the next struct member. Since min/max can be of
            different types (e.g. int, float, float2 ...), it is stored in a type-agnostic uint8_t[32].
            To extract the min and max values from a TypeInfoStructMember, see getMemberMinMax().
        R_HINT_STEP_RATE(x)
            - Hints what the mStepRate will be set to for the next struct member.
        R_HINT_DESCRIPTION(StringLiteral)
            - Sets the next TypeInfoStructMember::pDescription
        R_HINT_TAG(StringLiteral)
            - Sets the next TypeInfoStructMember::pTag
        R_HINT_DISPLAY_NAME(StringLiteral)
            For struct members:
                - Sets the next TypeInfoStructMember::pDisplayName
            For enum members:
                - Adds the StringLiteral to the array TypeInfoEnumMember::pDisplayNames.
                If this hint is not used, it will add the raw enum name.
        R_HINT_FLAGS(Flags)
            - Sets the next TypeInfoStructMember::mFlags
        R_HINT_ENUM(EnumType)
            - Hints that the next struct member is intended to be used as an enum of
            EnumType. This is meant for int members (like uint64_t) to be treated as
            an enum. The reason this exists is because C99 enums always has the underlying
            type of 'int' (usually 32-bit signed), so if we need values higher than INT32_MAX,
            we need to store it as an int rather than an enum.

        Examples:

            REFLECT_STRUCT_BEGIN(Pet)
                REFLECT_STRUCT_MEMBER(Pet, R_FLOAT(float), mHeight, "Height of the pet")
                REFLECT_STRUCT_MEMBER(Pet, R_ARRAY(R_INT(char), 256), mName, "Name of the pet")
                REFLECT_STRUCT_MEMBER(Pet, R_INT(uint32_t), mAge, "Age of the pet")
            REFLECT_STRUCT_END(Pet)

            REFLECT_STRUCT_BEGIN(Person)
                REFLECT_STRUCT_MEMBER(Person, R_FLOAT(float), mHeight, "Height of the person")
                REFLECT_STRUCT_MEMBER(Person, R_ARRAY(R_INT(char), 256), mName, "Name of the person")
                REFLECT_STRUCT_MEMBER(Person, R_INT(uint32_t), mAge, "Age of the person")
                // Pointer to Pet's
                REFLECT_STRUCT_MEMBER(Person, R_POINTER(R_STRUCT(Pet)), pPet, "The person's pet")
                // Array[4] to pointers of Pet's
                REFLECT_STRUCT_MEMBER(Person, R_ARRAY(R_POINTER(R_STRUCT(Pet)), 4), pPet, "The person's pet")
                REFLECT_STRUCT_MEMBER(Person, R_STRUCT(Props), mProps, "Properties of the person")
                REFLECT_STRUCT_MEMBER(Person, R_ENUM(SomeEnum), mEnum, "An enum")
            REFLECT_STRUCT_END(Person)

            REFLECT_ENUM_BEGIN(TypeInfoTag)
                R_HINT_DISPLAY_NAME("Int")
                REFLECT_ENUM_MEMBER(TypeInfoTag, TYPE_INFO_TAG_INT, "Integer type")
                R_HINT_DISPLAY_NAME("Float")
                REFLECT_ENUM_MEMBER(TypeInfoTag, TYPE_INFO_TAG_FLOAT, "Float type")
                R_HINT_DISPLAY_NAME("Struct")
                REFLECT_ENUM_MEMBER(TypeInfoTag, TYPE_INFO_TAG_STRUCT, "Struct type")
                R_HINT_DISPLAY_NAME("Enum")
                REFLECT_ENUM_MEMBER(TypeInfoTag, TYPE_INFO_TAG_ENUM, "Enum type")
                R_HINT_DISPLAY_NAME("Pointer")
                REFLECT_ENUM_MEMBER(TypeInfoTag, TYPE_INFO_TAG_POINTER, "Pointer type")
                R_HINT_DISPLAY_NAME("Array")
                REFLECT_ENUM_MEMBER(TypeInfoTag, TYPE_INFO_TAG_ARRAY, "Array type")
            REFLECT_ENUM_END(TypeInfoTag)

    Rest of the API:

        void initDataTypeReflection();
            - Init the reflection system. This is necessary for the rtti system to work.
        void exitDataTypeReflection();
            - Call this when you want to clean up all the memory used in the rtti system.

        TypeInfoBase** getDataTypeTable()
            - Get the pointer to the first TypeInfoBase* in the type table.

        uint32_t getDataTypeTableCount()
            - Get the count of valid TypeInfoBase*'s in the type table.

        GET_TYPE_INFO(Type)
            - Get the type info of a reflected type. Type must either be a struct or enum type name.
            If Type is a struct, this will return a TypeInfoStruct*. Otherwise, if it's an Enum, this
            will return a TypeInfoEnum*. This will generate compile errors if the given Type is not
            reflected.

        ENUM_NAME(EnumType, Member)
            - Get the raw string name of a reflected enum member. If the member is not reflected,
            NULL will be returned.
        ENUM_DISPLAY_NAME(EnumType, Member)
            - Get the pretty display name of a reflected enum member, as specified with R_HINT_DISPLAY_NAME.
            If the member is not reflected, NULL will be returned.

        TypeInfoBase* lookupTypeByIdentifier(string)
            - Dynamically look up a reflected type from a string identifier.

        void getMemberMinMax(TypeInfoStructMember m, void *pMin, void *pMax)
            - Extracts the min & max values hinted for the struct member. pMin and pMax
            is expected to be large enough to hold m.pType->mSize bytes.

    Examples:

        To loop through members of a struct:

            SomeStruct *data = ...;
            TypeInfoStruct *structType = GET_TYPE_INFO(SomeStruct);

            for (uint32_t i = 0; i < structType->mMemberCount; i += 1) {
                TypeInfoStructMember member = structType->pMembers[i];

                void *memberData = (uint8_t*)data + member.mOffsetInStruct;
            }

        To loop through member values of an enum:

            TypeInfoEnum *enumType = GET_TYPE_INFO(SomeEnum);

            for (uint32_t i = 0; i < enumType->mMemberCount; i += 1) {
                TypeInfoEnumMember member = enumType->pMembers[i];

                makeUiThing(member->pName, ...); // Or something ...
            }

        To get an enum name from an enum value:

            SomeEnum enumValue = ...;

            const char *enumName = ENUM_NAME(SomeEnum, enumValue);

    Note on portability:
        Since we don't have something like C++ constructors, we have to rely on compiler
        intrinsics to have functions be called before main() (which is needed to be able
        to do the type reflection in the global scope).
        On gcc (or clang/gcc) this is easy enough with the "constructor" attribute
        (__attribute__((constructor))), but since msvc doesn't have a corresponding
        intrinsic, we have to rely on #pragma section and #pragma allocate to manually
        create a data segment where we allocate function pointers to the functions we
        need to call, and in init() go through all valid function pointers in the data
        segment.
        That being said, this won't be able to compile with any compiler that does not
        support either the gcc or the msvc intrinsic (which I can't think of any reasonable
        compiler that doesn't).

*/

#ifdef __cplusplus
extern "C"
{
#endif
// clang-format off
#define MAX_REFLECTED_TYPES 4096

typedef enum TypeInfoTag
{
    TYPE_INFO_TAG_INT,
    TYPE_INFO_TAG_FLOAT,
    TYPE_INFO_TAG_STRUCT,
    TYPE_INFO_TAG_ENUM,
    TYPE_INFO_TAG_POINTER,
    TYPE_INFO_TAG_ARRAY,
} TypeInfoTag;

// For internal use
typedef struct ReflectionTypeResolver
{
    TypeInfoTag mKind;
    char        pIdentifier[256];
    size_t      mSize;

    char pUnderlyingIdentifier[256]; // Underlying int type for enum

    size_t mArrayCount;

    bool mIsBool; // Only relevant for struct members

    struct ReflectionTypeResolver* pIndirection; // [] or * to
} ReflectionTypeResolver;

typedef struct TypeInfoBase
{
    TypeInfoTag mKind;
    char        pName[256];
    size_t      mSize;

} TypeInfoBase;

typedef struct TypeInfoInt
{
    TypeInfoBase mBase;
    bool         mIsSigned;
} TypeInfoInt;

typedef struct TypeInfoPointer
{
    TypeInfoBase mBase;

    TypeInfoBase*          pPointerTo;
    ReflectionTypeResolver mInternalTypeResolver;

} TypeInfoPointer;

typedef struct TypeInfoArray
{
    TypeInfoBase mBase;

    TypeInfoBase*          pElementType;
    ReflectionTypeResolver mInternalTypeResolver;

    size_t mArrayCount;

} TypeInfoArray;

struct TypeInfoEnum;

typedef struct TypeInfoStructMember
{
    const char* pName;
    size_t      mOffsetInStruct;

    TypeInfoBase*          pType; // This will be null if the member's type has not been reflected
    ReflectionTypeResolver mInternalTypeResolver;
    bool                   mIsBool;

    // These are set through R_HINT's
    uint8_t     pMinMem[32]; // Use getMemberMinMax() to get min / max values
    uint8_t     pMaxMem[32]; // Use getMemberMinMax() to get min / max values
    bool        mMinMaxSet;
    float       mStepRate;
    const char* pDescription;
    const char* pTag;
    const char* pDisplayName;
    uint64_t    mFlags;
    struct TypeInfoEnum *pEnumType; // If member needs to be a specific integer, but treated as an enum

} TypeInfoStructMember;

typedef struct TypeInfoStruct
{
    TypeInfoBase          mBase;
    size_t                mSize;
    TypeInfoStructMember* pMembers;
    size_t                mMemberCount;
} TypeInfoStruct;

typedef struct TypeInfoEnumMember
{
    const char* pName;
    uint64_t    mValueU64; // If your enum type has negative values, you will need to transmute this.
    
} TypeInfoEnumMember;

typedef struct TypeInfoEnum
{
    TypeInfoBase        mBase;
    char **pNames; // If you need a contigious array of only the names. stb array.
    char **pDisplayNames;
    TypeInfoEnumMember* pMembers;
    size_t              mMemberCount;

    TypeInfoInt*           pUnderlyingType;
    ReflectionTypeResolver mInternalTypeResolver;
} TypeInfoEnum;

typedef enum ReflectionMemberFlags {
	REFLECT_MEMBER_FLAG_NONE = 0,
	REFLECT_MEMBER_FLAG_COLOR_RGB = 1 << 0,
	REFLECT_MEMBER_FLAG_COLOR_RGBA = 1 << 1,
	REFLECT_MEMBER_FLAG_STRING = 1 << 2,
	REFLECT_MEMBER_FLAG_HEX = 1 << 3,

    REFLECT_MEMBER_FLAG_HIDDEN = 1 << 4, // Don't create corresponding UI element
    REFLECT_MEMBER_FLAG_HIDDEN_NAME = 1 << 5, // Don't create label name
    REFLECT_MEMBER_FLAG_DISPLAY_NAME_LITERAL = 1 << 6 // Don't beautify member name when no displayname is set
} ReflectionMemberFlags;

#define FORWARD_DECL_REFLECT_STRUCT(Type) extern TypeInfoStruct Type##_TypeInfo
	
#define FORWARD_DECL_REFLECT_ENUM(Type) extern TypeInfoEnum Type##_TypeInfo
// TypeInfoTag is reflected in DataTypeReflection.c
FORWARD_DECL_REFLECT_ENUM(TypeInfoTag);

///
// API
///

#define GET_TYPE_INFO(Type)         (&Type##_TypeInfo)
#define ENUM_NAME(EnumType, Member) lookupEnumName(GET_TYPE_INFO(EnumType), (uint64_t)Member)
#define ENUM_DISPLAY_NAME(EnumType, Member) lookupEnumDisplayName(GET_TYPE_INFO(EnumType), (uint64_t)Member)


void initDataTypeReflection();
void exitDataTypeReflection();

TypeInfoBase** getDataTypeTable();
uint32_t       getDataTypeTableCount();

// Searches through type table for a type with matching identifier. Returns NULL if none was found.
TypeInfoBase* lookupTypeByIdentifier(const char* identifier);
// Returns NULL if value is not a valid enum value
const char*   lookupEnumName(TypeInfoEnum* enumInfo, uint64_t valueU64);
const char*   lookupEnumDisplayName(TypeInfoEnum* enumInfo, uint64_t valueU64);

int32_t lookupMemberByOffset(TypeInfoStruct* type, size_t offset);

void getMemberMinMax(TypeInfoStructMember m, void* pMin, void* pMax);

///
// Internal
///

void CompilerSpecificInitDataTypeReflection();
void RegisterDataTypeReflection(TypeInfoBase* info);
void RegisterDataTypeReflectionStructMember(TypeInfoStruct* base, TypeInfoStructMember member);
void RegisterDataTypeReflectionEnumMember(TypeInfoEnum* base, TypeInfoEnumMember member, const char *displayName);


typedef void (*ConstructorFunction)();

// clang-format on

///
// Macros
///

// Constructing structs with {} looks different in pure C99 vs C++
// When C++, we need to prefix the init functions with extern "C" for proper linkage
// to data segment functions
// Also needs to be marked volatile to not get optimized out since
// they are indirectly called.
#ifdef __cplusplus
#define __R_CONSTR(Type, ...) \
    Type { __VA_ARGS__ }
#define __INIT_FN_PREFIX extern "C"
#else
#define __R_CONSTR(Type, ...) \
    (Type) { __VA_ARGS__ }
#define __INIT_FN_PREFIX
#endif

// If we have clang intrinsics available, we prefer that over the msvc data segments.
#if defined(_MSC_VER) && !defined(__clang__)

///
// We will allocate .ctors section in binary, containing an array of void() functions
// We put some dummy memory in $A so we can get the pointer to the start of the segment,
// and $C so we can know when the data segment ends.
// For portability, we need to deal with arbitrary padding so we loop from $A through $C and skip
// everything that's 0. This is with the assumption that the padding is filled with zeroes but
// I would be surprised if there was a platform which didn't do that... (#Portability)
// (/MAP command line for msvc linker will let you inspect data segments)
//                                      - Charlie Malmqvist, 26th November 2024
#pragma section(".ctors$A", read)
#pragma section(".ctors$B", read)
#pragma section(".ctors$C", read)

// Allocate a function pointer in .ctors section and link to the XXX_InitReflection function
// generated with reflection macros
#define _DATA_TYPE_REFLECTION_INIT_FUNC(Type, func)                                                                              \
    __pragma(comment(linker, "/include:" #func)) __declspec(allocate(".ctors$B")) volatile void (*func##_ptr)() = func;          \
    __declspec(dllexport) volatile void Type##__dummy()                                                                          \
    {                                                                                                                            \
        volatile void* dummy_ref = (volatile void*)Type##_InitReflection_ptr; /* Try to get compiler to not optimize away this*/ \
        (void)dummy_ref;                                                                                                         \
    }

#define _DATA_TYPE_REFLECTION_INIT_SIGNATURE(Type) __INIT_FN_PREFIX volatile void Type##_InitReflection()

#else // _MSC_VER

extern ConstructorFunction pTypeReflectionInitFunctions[MAX_REFLECTED_TYPES];
extern uint32_t            gTypeReflectionInitFunctionsCount;

// Assuming gcc/clang
#if !defined(__GNUC__) && !defined(__clang__)
#error Compiler does not support IDataTypeReflection.h
#endif // !defined(__GNUC__) && !defined(__clang__)

#define _DATA_TYPE_REFLECTION_INIT_FUNC(Type, func) \
    __attribute__((constructor)) void Type##_AddInitFunction() { pTypeReflectionInitFunctions[gTypeReflectionInitFunctionsCount++] = func; }

#define _DATA_TYPE_REFLECTION_INIT_SIGNATURE(Type) __INIT_FN_PREFIX void Type##_InitReflection()

#endif // !_MSC_VER

#define REFLECT_STRUCT_BEGIN(Type)                                                                                                     \
    TypeInfoStruct Type##_TypeInfo;                                                                                                    \
    _DATA_TYPE_REFLECTION_INIT_SIGNATURE(Type)                                                                                         \
    {                                                                                                                                  \
        Type##_TypeInfo = __R_CONSTR(TypeInfoStruct, __R_CONSTR(TypeInfoBase, TYPE_INFO_TAG_STRUCT, #Type, sizeof(Type)), 0, NULL, 0); \
        TypeInfoStruct* typeInfo = &Type##_TypeInfo;                                                                                   \
        typeInfo->pMembers = NULL;                                                                                                     \
        typeInfo->mMemberCount = 0;                                                                                                    \
        uint8_t       hintMinMem[32] = { 0 };                                                                                          \
        uint8_t       hintMaxMem[32] = { 0 };                                                                                          \
        bool          hintMinMaxSet = false;                                                                                           \
        float         hintStepRate = 0.01f;                                                                                            \
        const char*   hintDescription = NULL;                                                                                          \
        const char*   hintTag = NULL;                                                                                                  \
        const char*   hintDispName = NULL;                                                                                             \
        uint64_t      hintFlags = 0;                                                                                                   \
        TypeInfoEnum* hintEnumType = NULL;

#define R_HINT_MINMAX(Type, min, max)            \
    {                                            \
        Type _min = min;                         \
        Type _max = max;                         \
        memcpy(hintMinMem, &_min, sizeof(Type)); \
        memcpy(hintMaxMem, &_max, sizeof(Type)); \
        hintMinMaxSet = true;                    \
    }
#define R_HINT_STEP_RATE(x)                hintStepRate = x;
#define R_HINT_DESCRIPTION(StringLiteral)  hintDescription = StringLiteral;
#define R_HINT_TAG(StringLiteral)          hintTag = StringLiteral;
#define R_HINT_DISPLAY_NAME(StringLiteral) hintDispName = StringLiteral;
#define R_HINT_FLAGS(Flags)                hintFlags = (uint64_t)Flags;
#define R_HINT_ENUM(EnumType)              hintEnumType = GET_TYPE_INFO(EnumType);

#define REFLECT_STRUCT_MEMBER(StructType, TypeResolver, Member)                                                                              \
    {                                                                                                                                        \
        ReflectionTypeResolver res = TypeResolver;                                                                                           \
        TypeInfoStructMember   m = __R_CONSTR(TypeInfoStructMember, #Member, offsetof(StructType, Member), NULL, res, false, { 0 }, { 0 },   \
                                              hintMinMaxSet, hintStepRate, hintDescription, hintTag, hintDispName, hintFlags, hintEnumType); \
        if (hintMinMaxSet)                                                                                                                   \
        {                                                                                                                                    \
            memcpy(m.pMinMem, hintMinMem, sizeof(m.pMinMem));                                                                                \
            memcpy(m.pMaxMem, hintMaxMem, sizeof(m.pMaxMem));                                                                                \
        }                                                                                                                                    \
        RegisterDataTypeReflectionStructMember(typeInfo, m);                                                                                 \
        ASSERTMSG(res.mSize == sizeof(((StructType*)0))->Member,                                                                             \
                  "Type argument in REFLECT_STRUCT_MEMBER ('%s') of size %zu, does not match member size %i", res.pIdentifier, res.mSize,    \
                  sizeof(((StructType*)0))->Member);                                                                                         \
    }                                                                                                                                        \
    hintMinMaxSet = false;                                                                                                                   \
    hintStepRate = 0.01f;                                                                                                                    \
    hintDescription = NULL;                                                                                                                  \
    hintTag = NULL;                                                                                                                          \
    hintDispName = NULL;                                                                                                                     \
    hintFlags = 0;                                                                                                                           \
    hintEnumType = NULL;

#define REFLECT_STRUCT_END(Type)                                 \
    RegisterDataTypeReflection((TypeInfoBase*)&Type##_TypeInfo); \
    }                                                            \
    _DATA_TYPE_REFLECTION_INIT_FUNC(Type, Type##_InitReflection)

#define REFLECT_ENUM_BEGIN(Type)                                                                                                           \
    TypeInfoEnum Type##_TypeInfo;                                                                                                          \
    _DATA_TYPE_REFLECTION_INIT_SIGNATURE(Type)                                                                                             \
    {                                                                                                                                      \
        Type##_TypeInfo = __R_CONSTR(TypeInfoEnum, __R_CONSTR(TypeInfoBase, TYPE_INFO_TAG_ENUM, #Type, sizeof(Type)), NULL, NULL, NULL, 0, \
                                     NULL, R_INT(Type));                                                                                   \
        TypeInfoEnum* typeInfo = &Type##_TypeInfo;                                                                                         \
        const char*   hintDispName = NULL;

#define REFLECT_ENUM_MEMBER(Type, Member)                                                                                    \
    RegisterDataTypeReflectionEnumMember(typeInfo, __R_CONSTR(TypeInfoEnumMember, #Member, (uint64_t)Member), hintDispName); \
    hintDispName = NULL;

#define REFLECT_ENUM_END(Type)                                   \
    RegisterDataTypeReflection((TypeInfoBase*)&Type##_TypeInfo); \
    }                                                            \
    _DATA_TYPE_REFLECTION_INIT_FUNC(Type, Type##_InitReflection)

// Resolvers

ReflectionTypeResolver MakeTypeResolverFloat(size_t floatSize);
ReflectionTypeResolver MakeTypeResolverInt(size_t intSize, bool isSigned, bool isBool);
ReflectionTypeResolver MakeTypeResolverEnum(size_t enumSize, bool isSigned, const char* name);
ReflectionTypeResolver MakeTypeResolverStruct(size_t structSize, const char* structName);
ReflectionTypeResolver MakeTypeResolverArray(ReflectionTypeResolver elemResolver, size_t count);
ReflectionTypeResolver MakeTypeResolverPointer(ReflectionTypeResolver elemResolver);

// Avoiding namespace collisions with obscure naming
#define ___DTR_FIRST_ARG(arg1, ...)     arg1
#define ___DTR_FIRST_ARG_STR(arg1, ...) #arg1

#define R_FLOAT(...)                    MakeTypeResolverFloat(sizeof(___DTR_FIRST_ARG(__VA_ARGS__)))
#define R_INT(...)                      MakeTypeResolverInt(sizeof(___DTR_FIRST_ARG(__VA_ARGS__)), (___DTR_FIRST_ARG(__VA_ARGS__))(-1) < 0, false)
// bool is just int, but we get a compiler warning from the < comparison with bools, so this is how to work around that warning..
#define R_BOOL(...)                     MakeTypeResolverInt(sizeof(bool), true, true)
#define R_STRUCT(...)                   MakeTypeResolverStruct(sizeof(___DTR_FIRST_ARG(__VA_ARGS__)), ___DTR_FIRST_ARG_STR(__VA_ARGS__))
#define R_ENUM(...)                                                                                                                     \
    MakeTypeResolverEnum(sizeof(___DTR_FIRST_ARG(__VA_ARGS__)), (___DTR_FIRST_ARG(__VA_ARGS__))(-1) < (___DTR_FIRST_ARG(__VA_ARGS__))0, \
                         ___DTR_FIRST_ARG_STR(__VA_ARGS__))
#define R_ARRAY(...)   MakeTypeResolverArray(__VA_ARGS__)
#define R_POINTER(...) MakeTypeResolverPointer(___DTR_FIRST_ARG(__VA_ARGS__))

#ifdef __cplusplus
} // extern "C"
#endif
