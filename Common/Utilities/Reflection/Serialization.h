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

#include "DataTypeReflection.h"

/*
        A serialization system to serialize/deserialize to a readable
        text format.

        It can be used to automatically serialize a struct and it's members with one function call.

        You can, however, also use it to manually write data in the form
        of key-value pairs.

        It's inspired by JSON, with some differences:
            - Simpler syntax
            - Line & block comments
            - Informative error messages
            - One composite value type () instead of array [] vs object {}
            - Keys can be identifiers or string literals (or omitted for empty key, which is the convention for "array" composites)

        To be successfully parsed, the data must start with a specific
        header token: "!TheForgeText".

        After the header token, the parser expectes either
            A) KEY : VALUE
            B) VALUE

        Where KEY can be an identifier or string literal, VALUE can be
        an integer, float, string or composite.

        B) Will generate an empty key ("") when deserialized.

        Example: """""""""""""
            !TheForgeText

            someInt: 5

            // Parsed float should be precise enough to exactly equal this.
            someFloat: 123.456789

            someString: "This is a string"

            // Composite with implicit keys
            someArray: (12 1.2345 "Text element")

            // Composite with explicit keys
            // (required to map unto reflected struct members)
            someStruct: (
                mInt: 0xDEADBEEF // Hexadecimal integers allowed
                mFloat: 1234.5678
                mString: "Text in a struct member"
                mArray: (5 4 3 2 1)
                mSubStruct: (
                    mSubInt: 15
                    mSubFloat: 1234.5678
                    mSubString: "Text in a struct member"
                    mSubArray: (5 4 3 2 1)
                )
            )
            """"""""""""""""""""""""""""""""""""

        API manual serialization:

            bool serializerBeginFile(Serializer* s, TFResourceDirectory dir, const char* path);
            void serializerBeginMemory(Serializer* s);
                - Begin a serializer, either targetting a file or memory.

            void serializerEnd(Serializer* s);
                - End the serialization. For a file serializer, this
                means to close the file stream. For memory serializer,
                this means to free the generated data. When serializing
                to memory, you need to call serializerGetString() to get
                the result before calling serializerEnd().

            void serializerGetString(Serializer* s, char* out, bool nullTerminated);
                - Copies the result so far to *out, optionally ending with
                a null terminator. Use serializerGetString() to know how
                big the data size is. Note that you should have one extra
                byte available in *out if the nullTerminated is true.
            size_t serializerGetStringSize(Serializer* s);
                - Returns the size of the currently generated string.

            void serializerWriteInt(Serializer* s, const char* k, int64_t v);
            void serializerWriteIntHex(Serializer* s, const char* k, int64_t v);
            void serializerWriteFloat(Serializer* s, const char* k, double v);
            void serializerWriteString(Serializer* s, const char* k, const char* v);
                - Append a key-value entry to the serialized data. A NULL
                key is accepted and means no key will be emitted.

            void serializerPushComposite(Serializer* s, const char* k);
                - Push the start of a composite. Any following serializerWrite
                calls will be appended to this composite.
            void serializerPopComposite(Serializer* s);
                - Pop & close the current composite. Any following serialieWrite
                calls will be appended to the previous composite.

            Example:

                Serializer s;
                serializerBeginMemory(&s);

                serializerWriteInt(&s, "someInt", 12);

                serializerPushComposite(&s, "someComposite");
                    serializerWriteFloat(&s, "someFloat", 1.2345);
                    serializerWriteString(&s, "someString", "Some text");
                serializerPopComposite(&s);

                char* result = tf_malloc(serializerGetStringSize(&s)+1);
                serializerGetString(&s, result, true);

                serializerEnd(&s);

                Result: """"""""""""""""""""
                    !TheForgeText
                    "someInt": 12
                    "someComposite": (
                        "someFloat": 1.2345
                        "someString": "Some text"
                    )
                """"""""""""""""""""""""""""

                // Deserialize

                DeserializeResult resultData = deserializeMemory(result);

                resultData.Global.pKeys[0] == "someInt";
                resultData.Global.pValues[0].mIntVal == 12;

                resultData.Global.pKeys[1] == "someComposite";
                    DeserializeCompositeValue *someComposite = &resultData.Global.pValues[1].mComposite;
                    someComposite->pKeys[0] == "someFloat";
                    someComposite->pValues[0].mFltVal == 1.2345;
                    someComposite->pKeys[1] == "someString";
                    someComposite->pValues[1].mStrRef == "Some Text";

                freeDeserializeResult(resultData);

                // To loop through a composite
                for (uint32_t i = 0; i < arrlen(comp->pKeys); i += 1)
                {
                    StringRef keyRef = comp->pKeys[i];
                    char key[256] = {0};
                    memcpy(key, keyRef.pStart, keyRef.mCount);

                    DeserializeValue value = comp->pValues[i];

                    if (value.mKind == DESERIALIZE_VALUE_INT)
                        ...
                }

        API high-level:

            bool serializeStructToFile(TFResourceDirectory dir, const char* path, TypeInfoStruct* structType, void* structData);
                - Serialize all the reflected members of a struct to file
            char* serializeStructToMemory(TypeInfoStruct* structType, void* structData);
                - Serialize all the reflected members of a struct to memory.
                Note that the returned pointer will be dynamically
                allocated and must be freed.

            bool deserializeStructFromFile(TFResourceDirectory dir, const char* path, TypeInfoStruct* structType, void* structData);
                - Load serialized members to memory, from serialized data in a file.
            bool deserializeStructFromFile(TFResourceDirectory dir, const char* path, TypeInfoStruct* structType, void* structData);
                - Load serialized members to memory, from serialized data in memory.

            Example:

                SomeStruct myStruct = ...;

                bool ok = serializeStructToFile(RD_XXXX, "data.tfs", GET_TYPE_INFO(SomeStruct), &myStruct);
                ASSERT(ok);

                ok = deserializeStructFromFile(RD_XXXX, "data.tfs", GET_TYPE_INFO(SomeStruct), &myStruct);
                ASSERT(ok);
*/

#ifdef __cplusplus
extern "C"
{
#endif

#define STRING_BUILDER_SMALL_DATA_SIZE 128

///
// Utility to efficiently build a dynamic string.
// Maybe worth putting in a library ?

typedef struct StringBuilder
{
    char*  pData; // Not necessarily null-terminated until you call builder_to_string()
    size_t mCount;
    size_t mAllocated;

    char pSmallData[STRING_BUILDER_SMALL_DATA_SIZE];
} StringBuilder;

void  builderInit(StringBuilder* b);
void  builderExit(StringBuilder* b);
void  builderReserve(StringBuilder* b, size_t reserveCount);
void  builderAppend(StringBuilder* b, const char* s);
char* builderGetStringView(StringBuilder* b, bool nullTerminated);
void  builderGetStringCopy(StringBuilder* b, char* s, bool nullTerminated);

typedef enum SerializeMode
{
    SERIALIZE_MODE_MEMORY,
    SERIALIZE_MODE_FILE
} SerializeMode;

typedef struct Serializer
{
    SerializeMode mMode;
    StringBuilder mBuilder; // SERIALIZE_MODE_MEMORY
    TFFileStream  mStream;  // SERIALIZE_MODE_FILE

    uint32_t mDepth;
} Serializer;

bool serializerBeginFile(Serializer* s, TFResourceDirectory dir, const char* path);
void serializerBeginMemory(Serializer* s);
void serializerEnd(Serializer* s);

void serializeStruct(Serializer* s, TypeInfoStruct* structType, void* structData);
void serializeArrayValue(Serializer* s, TypeInfoArray* arrayType, void* src);
void serializerWriteInt(Serializer* s, const char* k, int64_t v);
void serializerWriteIntHex(Serializer* s, const char* k, int64_t v);
void serializerWriteFloat(Serializer* s, const char* k, double v);
void serializerWriteString(Serializer* s, const char* k, const char* v);
void serializerPushComposite(Serializer* s, const char* k);
void serializerPopComposite(Serializer* s);

void   serializerGetString(Serializer* s, char* out, bool nullTerminated);
size_t serializerGetStringSize(Serializer* s);

// NON-NULL-TERMINATED !!
// Used for references to strings in source text.
typedef struct StringRef
{
    char*  pStart;
    size_t mCount;
} StringRef;

typedef enum DeserializeValueKind
{
    DESERIALIZE_VALUE_INT,
    DESERIALIZE_VALUE_FLOAT,
    DESERIALIZE_VALUE_STRING,
    DESERIALIZE_VALUE_COMPOSITE,
} DeserializeValueKind;

struct DeserializeValue;
typedef struct DeserializeCompositeValue
{
    StringRef*               pKeys;   // stb array. Keys are NOT null terminated
    struct DeserializeValue* pValues; // stb array

    uint32_t mInternalNextImplicitIndex;
} DeserializeCompositeValue;

typedef struct DeserializeValue
{
    DeserializeValueKind mKind;
    union
    {
        int64_t                   mIntVal;
        double                    mFltVal;
        StringRef                 mStrRef; // NOT null terminated. Use mCount.
        DeserializeCompositeValue mComposite;
    };
} DeserializeValue;

typedef struct DeserializeResult
{
    char*                     pSource;
    DeserializeCompositeValue mGlobal;

    bool mSuccess;
} DeserializeResult;

void deserializeStruct(DeserializeCompositeValue* scope, TypeInfoStruct* structType, void* structData);
void deserializeArray(DeserializeCompositeValue* comp, TypeInfoArray* arrayType, void* dst);

DeserializeResult deserializeFile(TFResourceDirectory dir, const char* path);
DeserializeResult deserializeMemory(const char* data);

void freeDeserializeResult(DeserializeResult result);

bool  serializeStructToFile(TFResourceDirectory dir, const char* path, TypeInfoStruct* structType, void* structData);
// Returns dynamically allocated memory. tf_free() to cleanup.
char* serializeStructToMemory(TypeInfoStruct* structType, void* structData);

bool deserializeStructFromFile(TFResourceDirectory dir, const char* path, TypeInfoStruct* structType, void* structData);
bool deserializeStructFromMemory(const char* data, TypeInfoStruct* structType, void* structData);

#ifdef __cplusplus
} // extern "C"
#endif
