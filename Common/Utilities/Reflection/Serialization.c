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

#include "Serialization.h"

#include "../../Utilities/ThirdParty/OpenSource/Nothings/stb_ds.h" // Resizable array

#include "../../Utilities/Log/Log.h"

#define TF_SERIALIZE_HEADER "!TheForgeText\n"

// These are defined when compiling with msvc but not when compiling with clang (android).
#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef max
#define max(a, b) ((a) > (b) ? (a) : (b))
#endif

uint64_t getNextPowerOfTwo(uint64_t x)
{
    if (x == 0)
        return 1;
    x |= x >> 1;
    x |= x >> 2;
    x |= x >> 4;
    x |= x >> 8;
    x |= x >> 16;
    x |= x >> 32;
    return x + 1;
}

void builderInit(StringBuilder* b)
{
    *b = (StringBuilder){ 0 };
    b->mAllocated = STRING_BUILDER_SMALL_DATA_SIZE;
}

void builderExit(StringBuilder* b)
{
    if (b->mAllocated > STRING_BUILDER_SMALL_DATA_SIZE)
        tf_free(b->pData);
    *b = (StringBuilder){ 0 };
}

void builderReserve(StringBuilder* b, size_t reserveCount)
{
    if (reserveCount <= b->mAllocated)
        return;

    // Grow exponentially so we rarely trigger an allocation
    reserveCount = getNextPowerOfTwo(reserveCount);

    char* newData = (char*)tf_malloc(reserveCount);

    if (b->pData)
    {
        memcpy(newData, b->pData, b->mCount);
        tf_free(b->pData);
    }
    else
    {
        // If there wasn't a pData, that must mean we were still using
        // small data.
        ASSERT(b->mCount <= STRING_BUILDER_SMALL_DATA_SIZE);
        memcpy(newData, b->pSmallData, b->mCount);
    }

    b->pData = newData;
    b->mAllocated = reserveCount;
}

void builderAppendn(StringBuilder* b, const char* s, size_t n)
{
    size_t sLen = n;
    builderReserve(b, b->mCount + sLen);

    char* dst = b->mAllocated <= STRING_BUILDER_SMALL_DATA_SIZE ? b->pSmallData : b->pData;

    memcpy(dst + b->mCount, s, sLen);
    b->mCount += sLen;
}
void builderAppend(StringBuilder* b, const char* s) { builderAppendn(b, s, strlen(s)); }

char* builderGetStringView(StringBuilder* b, bool nullTerminated)
{
    if (nullTerminated)
        builderReserve(b, b->mCount + 1);

    char* data = b->mAllocated <= STRING_BUILDER_SMALL_DATA_SIZE ? b->pSmallData : b->pData;

    if (nullTerminated)
        data[b->mCount] = 0;

    return data;
}
void builderGetStringCopy(StringBuilder* b, char* s, bool nullTerminated)
{
    memcpy(s, builderGetStringView(b, nullTerminated), b->mCount + (nullTerminated ? 1 : 0));
}

bool serializerBeginFile(Serializer* s, TFResourceDirectory dir, const char* path)
{
    *s = (Serializer){ 0 };

    s->mMode = SERIALIZE_MODE_FILE;

    bool ok = fsOpenStreamFromPath(dir, path, TF_FM_WRITE, &s->mStream);

    if (ok)
    {
        size_t written = fsWriteToStream(&s->mStream, TF_SERIALIZE_HEADER, strlen(TF_SERIALIZE_HEADER));
        ok = written == strlen(TF_SERIALIZE_HEADER);
    }

    return ok;
}

void serializerBeginMemory(Serializer* s)
{
    *s = (Serializer){ 0 };

    s->mMode = SERIALIZE_MODE_MEMORY;

    builderInit(&s->mBuilder);

    builderAppend(&s->mBuilder, TF_SERIALIZE_HEADER);
}
void serializerEnd(Serializer* s)
{
    switch (s->mMode)
    {
    case SERIALIZE_MODE_FILE:
        fsCloseStream(&s->mStream);
        break;
    case SERIALIZE_MODE_MEMORY:
        builderExit(&s->mBuilder);
        break;

    default:
        ASSERT(false);
    }
    *s = (Serializer){ 0 };
}

void serializerPutn(Serializer* s, const char* str, size_t n)
{
    switch (s->mMode)
    {
    case SERIALIZE_MODE_FILE:

        fsWriteToStream(&s->mStream, str, n);

        break;
    case SERIALIZE_MODE_MEMORY:
        builderAppendn(&s->mBuilder, str, n);
        break;

    default:
        ASSERT(false);
    }
}
void serializerPut(Serializer* s, const char* str) { serializerPutn(s, str, strlen(str)); }
void serializerPad(Serializer* s, uint32_t numSpaces)
{
    char spaces[512];

    memset(spaces, ' ', numSpaces);
    spaces[numSpaces] = 0;

    switch (s->mMode)
    {
    case SERIALIZE_MODE_FILE:

        fsWriteToStream(&s->mStream, spaces, numSpaces);

        break;
    case SERIALIZE_MODE_MEMORY:
        builderAppend(&s->mBuilder, spaces);
        break;

    default:
        ASSERT(false);
    }
}
void serializerPutStringLiteral(Serializer* s, const char* str)
{
    serializerPut(s, "\"");
    // Instead of putting 1 char at a time, we write each chunk of chars
    // up to an escape sequence.
    uint32_t chunkStart = 0;
    uint32_t len = (uint32_t)strlen(str);
    for (uint32_t i = 0; i < len; i += 1)
    {
        bool isEscaped = true;

        const char* p = str + i;

        char esc[3] = { 0 };

        switch (*p)
        {
        case '\"':
            strcpy(esc, "\\\"");
            break;
        case '\\':
            strcpy(esc, "\\\\");
            break;
        case '\b':
            strcpy(esc, "\\b");
            break;
        case '\f':
            strcpy(esc, "\\f");
            break;
        case '\n':
            strcpy(esc, "\\n");
            break;
        case '\r':
            strcpy(esc, "\\r");
            break;
        case '\t':
            strcpy(esc, "\\t");
            break;
        default:
            isEscaped = false;
            break;
        }

        if (isEscaped)
        {
            uint32_t n = i - chunkStart;
            if (n)
                serializerPutn(s, str + chunkStart, n);
            chunkStart = i + 1;
            serializerPut(s, esc);
        }
    }
    uint32_t n = len - chunkStart;
    if (n)
        serializerPut(s, str + chunkStart);

    serializerPut(s, "\"");
}
void serializerPutKey(Serializer* s, const char* k)
{
    serializerPutStringLiteral(s, k);
    serializerPut(s, ": ");
}
void serializerWriteInt(Serializer* s, const char* k, int64_t v)
{
    serializerPad(s, s->mDepth * 4);
    char vStr[64];
    sprintf(vStr, "%lli", (long long int)v);

    if (k)
        serializerPutKey(s, k);

    serializerPut(s, vStr);
    serializerPut(s, "\n");
}
void serializerWriteIntHex(Serializer* s, const char* k, int64_t v)
{
    serializerPad(s, s->mDepth * 4);
    char vStr[64];
    sprintf(vStr, "0x%llx", (long long int)v);

    if (k)
        serializerPutKey(s, k);

    serializerPut(s, vStr);
    serializerPut(s, "\n");
}
void serializerWriteFloat(Serializer* s, const char* k, double v)
{
    serializerPad(s, s->mDepth * 4);
    char   vStr[64];
    size_t vStrLen = sprintf(vStr, "%.10f", (v));
    while (vStr[vStrLen - 1] == '0' && vStr[vStrLen - 2] != '.')
    {
        // Remove trailing 0's
        vStrLen -= 1;
        vStr[vStrLen] = 0;
    }

    if (k)
        serializerPutKey(s, k);

    serializerPut(s, vStr);
    serializerPut(s, "\n");
}
void serializerWriteString(Serializer* s, const char* k, const char* v)
{
    serializerPad(s, s->mDepth * 4);

    if (k)
        serializerPutKey(s, k);

    serializerPutStringLiteral(s, v);
    serializerPut(s, "\n");
}

void serializerPushComposite(Serializer* s, const char* k)
{
    serializerPad(s, s->mDepth * 4);
    if (k)
        serializerPutKey(s, k);
    serializerPut(s, "(\n");
    s->mDepth += 1;
}
void serializerPopComposite(Serializer* s)
{
    s->mDepth -= 1;
    serializerPad(s, s->mDepth * 4);
    serializerPut(s, ")\n");
}

void serializerGetString(Serializer* s, char* out, bool nullTerminated)
{
    ASSERT(s->mMode == SERIALIZE_MODE_MEMORY);

    builderGetStringCopy(&s->mBuilder, out, nullTerminated);
}
size_t serializerGetStringSize(Serializer* s)
{
    ASSERT(s->mMode == SERIALIZE_MODE_MEMORY);
    return s->mBuilder.mCount;
}

typedef enum TokenKind
{
    TOKEN_KIND_UNKNOWN,
    TOKEN_KIND_TF_HEADER,
    TOKEN_KIND_IDENTIFIER,
    TOKEN_KIND_COLON,
    TOKEN_KIND_STRING_LITERAL,
    TOKEN_KIND_INT_LITERAL,
    TOKEN_KIND_FLOAT_LITERAL,
    TOKEN_KIND_OPEN_PAREN,
    TOKEN_KIND_CLOSE_PAREN,
    TOKEN_KIND_EOF,
} TokenKind;
REFLECT_ENUM_BEGIN(TokenKind)
REFLECT_ENUM_MEMBER(TokenKind, TOKEN_KIND_UNKNOWN)
REFLECT_ENUM_MEMBER(TokenKind, TOKEN_KIND_TF_HEADER)
REFLECT_ENUM_MEMBER(TokenKind, TOKEN_KIND_IDENTIFIER)
REFLECT_ENUM_MEMBER(TokenKind, TOKEN_KIND_COLON)
REFLECT_ENUM_MEMBER(TokenKind, TOKEN_KIND_STRING_LITERAL)
REFLECT_ENUM_MEMBER(TokenKind, TOKEN_KIND_INT_LITERAL)
REFLECT_ENUM_MEMBER(TokenKind, TOKEN_KIND_FLOAT_LITERAL)
REFLECT_ENUM_MEMBER(TokenKind, TOKEN_KIND_OPEN_PAREN)
REFLECT_ENUM_MEMBER(TokenKind, TOKEN_KIND_CLOSE_PAREN)
REFLECT_ENUM_MEMBER(TokenKind, TOKEN_KIND_EOF)
REFLECT_ENUM_END(TokenKind)
typedef struct Token
{
    TokenKind mKind;
    char*     pSourcePos;
    size_t    mLength;
} Token;
typedef struct Parser
{
    char* pSource;
    char* pNext;

    uint32_t mNextImplicitIndex;
} Parser;
typedef struct SourceLocation
{
    uint32_t mLineNumber;
    uint32_t mLineLength;
    uint32_t mC0, mC1;
    char*    pLineStart;
} SourceLocation;

void getTokenString(const Token* token, char* buffer, size_t bufferSize)
{
    if (token->mKind == TOKEN_KIND_EOF)
    {
        buffer[0] = 0;
        return;
    }
    size_t copySize = min(bufferSize - 1, token->mLength);
    memcpy(buffer, token->pSourcePos, copySize);
    buffer[copySize] = 0;
}

SourceLocation getTokenLocation(Parser* parser, const Token* token)
{
    SourceLocation loc = (SourceLocation){ 0 };
    loc.mLineNumber = 1;

    char* p = parser->pSource;

    loc.pLineStart = p;

    while (p < token->pSourcePos)
    {
        if (*p == '\n')
        {
            loc.mLineNumber += 1;
            loc.pLineStart = p + 1;
        }
        p += 1;
    }

    p = loc.pLineStart;

    while (*p != 0 && *p != '\n')
    {
        loc.mLineLength += 1;
        p += 1;
    }

    loc.mC0 = (uint32_t)(token->pSourcePos - loc.pLineStart);
    loc.mC1 = loc.mC0 + (uint32_t)token->mLength;

    return loc;
}

void getLocationLine(const SourceLocation* loc, char* buffer, size_t bufferSize)
{
    size_t copySize = min(bufferSize - 1, loc->mLineLength);
    memcpy(buffer, loc->pLineStart, copySize);
    buffer[copySize] = 0;
}

void stringifyToken(Parser* parser, const Token* token, char* buffer, size_t bufferSize)
{
    SourceLocation loc = getTokenLocation(parser, token);

    // Hard limits, but should be safe as it should just cut off the
    // string if it's too long rather than overrun.

    char name[128];
    getTokenString(token, name, 128);
    char line[256];
    getLocationLine(&loc, line, 256);

    char marker[256] = "";
    for (uint32_t i = 0; i < min(loc.mC0, 256); i += 1)
        strcat(marker, line[i] == '\t' ? "\t" : " ");
    for (uint32_t i = loc.mC0; i < min(loc.mC1, 256); i += 1)
        strcat(marker, "^");

    snprintf(buffer, bufferSize, "Line %i: Token '%s' (%s)\n\t%s\n\t%s", (int)loc.mLineNumber, name, ENUM_NAME(TokenKind, token->mKind),
             line, marker);
}

void logToken(LogLevel level, Parser* parser, const Token* token, const char* msg)
{
    char tokenString[1024] = "";
    stringifyToken(parser, token, tokenString, sizeof(tokenString));

    if (msg)
        LOGF(level, "\n%s\n%s", msg, tokenString);
    else
        LOGF(level, "\n%s", tokenString);
}

bool isWhitespace(char c) { return c == '\n' || c == ' ' || c == '\t' || c == '\r'; }
bool isAlpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
bool isDigit(char c) { return (c >= '0' && c <= '9'); }

Token tokenizeNext(Parser* parser)
{
    // Skip whitespace
    while (isWhitespace(*parser->pNext))
        parser->pNext += 1;

    // Skip block comments
    int depth = 0;
    while (parser->pNext[0] == '/' && parser->pNext[1] == '*')
    {
        depth += 1;
        while (true)
        {
            parser->pNext += 1;

            if (parser->pNext[0] == '/' && parser->pNext[1] == '*')
                depth += 1;

            if (parser->pNext[0] == '*' && parser->pNext[1] == '/')
            {
                parser->pNext += 2;
                depth -= 1;
                if (depth == 0)
                    break;
            }
            if (*parser->pNext == 0)
            {
                depth -= 1;
                if (depth == 0)
                    break;
            }
        }

        // Skip whitespace
        while (isWhitespace(*parser->pNext))
            parser->pNext += 1;
    }

    // Skip line comments
    if (parser->pNext[0] == '/' && parser->pNext[1] == '/')
    {
        while (*parser->pNext != '\n' && *parser->pNext)
        {
            parser->pNext += 1;
        }
        return tokenizeNext(parser);
    }

    // Skip whitespace
    while (isWhitespace(*parser->pNext))
        parser->pNext += 1;

    Token token = (Token){ 0 };
    token.pSourcePos = parser->pNext;

    // Identifier
    if (isAlpha(*parser->pNext))
    {
        while (isAlpha(*parser->pNext) || isDigit(*parser->pNext) || *parser->pNext == '_')
            parser->pNext += 1;

        token.mKind = TOKEN_KIND_IDENTIFIER;
        token.mLength = (parser->pNext - token.pSourcePos);
        return token;
    }

    if (*parser->pNext == ':')
    {
        parser->pNext += 1;
        token.mKind = TOKEN_KIND_COLON;
        token.mLength = (parser->pNext - token.pSourcePos);
        return token;
    }

    if (*parser->pNext == '(')
    {
        parser->pNext += 1;
        token.mKind = TOKEN_KIND_OPEN_PAREN;
        token.mLength = (parser->pNext - token.pSourcePos);
        return token;
    }

    if (*parser->pNext == ')')
    {
        parser->pNext += 1;
        token.mKind = TOKEN_KIND_CLOSE_PAREN;
        token.mLength = (parser->pNext - token.pSourcePos);
        return token;
    }

    // Hexadecimal digit
    if (memcmp(parser->pNext, "0x", 2) == 0 || memcmp(parser->pNext, "-0x", 3) == 0)
    {
        // Skip 0x
        parser->pNext += 2;

        // accept digit, a-f, A-F
        while (isDigit(*parser->pNext) || (*parser->pNext >= 'a' && *parser->pNext <= 'f') ||
               (*parser->pNext >= 'A' && *parser->pNext <= 'F'))
            parser->pNext += 1;
        token.mKind = TOKEN_KIND_INT_LITERAL;
        token.mLength = (parser->pNext - token.pSourcePos);
        return token;
    }

    // Digit, or a '-' followed by a digit
    if (isDigit(*parser->pNext) || (parser->pNext[0] == '-' && isDigit(parser->pNext[1])))
    {
        bool  hasDot = false;
        char* first = parser->pNext;

        while (isDigit(*parser->pNext) || *parser->pNext == '.' || (*parser->pNext == '-' && parser->pNext == first))
        {
            if (*parser->pNext == '.')
            {
                if (hasDot)
                    break;
                hasDot = true;
            }
            parser->pNext += 1;
        }

        token.mKind = hasDot ? TOKEN_KIND_FLOAT_LITERAL : TOKEN_KIND_INT_LITERAL;
        token.mLength = (parser->pNext - token.pSourcePos);
        return token;
    }

    // TODO escape sequences
    if (*parser->pNext == '"')
    {
        parser->pNext += 1;
        token.pSourcePos += 1; // Skip "

        while (*parser->pNext != '"' && *parser->pNext != 0)
            parser->pNext += 1;

        token.mKind = TOKEN_KIND_STRING_LITERAL;
        token.mLength = (parser->pNext - token.pSourcePos);
        parser->pNext += 1; // Skip last "
        return token;
    }

    if (*parser->pNext == 0)
    {
        token.mKind = TOKEN_KIND_EOF;
        token.mLength = 1;
        return token;
    }

    if (memcmp(parser->pNext, "!TheForgeText", strlen("!TheForgeText")) == 0)
    {
        parser->pNext += strlen("!TheForgeText");
        token.mKind = TOKEN_KIND_TF_HEADER;
        token.mLength = (parser->pNext - token.pSourcePos);
        return token;
    }

    // Unexpected token
    parser->pNext += 1;
    token.mKind = TOKEN_KIND_UNKNOWN;
    token.mLength = (parser->pNext - token.pSourcePos);
    return token;
}

DeserializeValue parseValue(Parser* parser, DeserializeResult* result, const Token* firstTok)
{
    Token tok = *firstTok;

    // If string fits in "valStrSmall", use that, otherwise tf_malloc. (We don't want to call malloc, and potential sys calls, for every
    // value)
    char  valStrSmall[512] = "";
    char* valStr = valStrSmall;
    if (tok.mLength + 1 > sizeof(valStrSmall))
    {
        valStr = tf_malloc(tok.mLength + 1);
        getTokenString(&tok, valStr, tok.mLength + 1);
    }
    else
    {
        getTokenString(&tok, valStr, sizeof(valStrSmall));
    }

    DeserializeValue val;

    if (tok.mKind == TOKEN_KIND_INT_LITERAL)
    {
        val.mKind = DESERIALIZE_VALUE_INT;
        if (strlen(valStr) > 2 && valStr[0] == '0' && valStr[1] == 'x')
            val.mIntVal = (int64_t)strtoll(valStr, NULL, 16); // hexadecimal
        else
            val.mIntVal = (int64_t)strtoll(valStr, NULL, 10); // decimal
    }
    else if (tok.mKind == TOKEN_KIND_FLOAT_LITERAL)
    {
        val.mKind = DESERIALIZE_VALUE_FLOAT;
        val.mFltVal = atof(valStr);
    }
    else if (tok.mKind == TOKEN_KIND_STRING_LITERAL)
    {
        val.mKind = DESERIALIZE_VALUE_STRING;
        val.mStrRef.pStart = tok.pSourcePos;
        val.mStrRef.mCount = tok.mLength;

        // Look for escape sequences
        // Maybe we should do this in one contiguous pass before we
        // start parsing ? #Speed
        for (uint32_t i = 0; i < val.mStrRef.mCount; i += 1)
        {
            char* p = val.mStrRef.pStart + i;

            if (p[0] == '\\' && (i + 1) < val.mStrRef.mCount)
            {
                switch (p[1])
                {
                case '\"':
                    p[0] = '\"';
                    break;
                case '\\':
                    p[0] = '\\';
                    break;
                case 'b':
                    p[0] = '\b';
                    break;
                case 'f':
                    p[0] = '\f';
                    break;
                case 'n':
                    p[0] = '\n';
                    break;
                case 'r':
                    p[0] = '\r';
                    break;
                case 't':
                    p[0] = '\t';
                    break;
                default:
                    p[0] = p[1];
                    break;
                }

                size_t remainder = val.mStrRef.mCount - (i + 2);
                memmove(p + 1, p + 2, remainder);

                val.mStrRef.mCount -= 1;
            }
        }
    }
    else
    {
        result->mSuccess = false;
        logToken(eERROR, parser, &tok, "Unexpected token. Expected a value. Got this instead.");
        if (tok.mLength + 1 > sizeof(valStrSmall))
            tf_free(valStr);
        return (DeserializeValue){ 0 };
    }

    if (tok.mLength + 1 > sizeof(valStrSmall))
        tf_free(valStr);

    return val;
}

void freeComposite(DeserializeCompositeValue* comp)
{
    for (uint32_t i = 0; i < arrlen(comp->pValues); i += 1)
    {
        DeserializeValue val = comp->pValues[i];

        if (val.mKind == DESERIALIZE_VALUE_COMPOSITE)
        {
            freeComposite(&val.mComposite);
        }
    }

    if (comp->pKeys)
        arrfree(comp->pKeys);
    if (comp->pValues)
        arrfree(comp->pValues);
}

void             parseNextEntry(Parser* parser, DeserializeResult* result, DeserializeCompositeValue* scope, const Token* firstTok);
DeserializeValue parseComposite(Parser* parser, DeserializeResult* result, const Token* firstTok)
{
    ASSERT(firstTok->mKind == TOKEN_KIND_OPEN_PAREN);

    DeserializeValue val = (DeserializeValue){ 0 };
    val.mKind = DESERIALIZE_VALUE_COMPOSITE;

    while (true)
    {
        Token tok = tokenizeNext(parser);

        if (tok.mKind == TOKEN_KIND_CLOSE_PAREN || tok.mKind == TOKEN_KIND_EOF)
            break;

        parseNextEntry(parser, result, &val.mComposite, &tok);
        if (!result->mSuccess)
        {
            freeComposite(&val.mComposite);
            return (DeserializeValue){ 0 };
        }
    }

    return val;
}

void parseNextEntry(Parser* parser, DeserializeResult* result, DeserializeCompositeValue* scope, const Token* firstTok)
{
    Token tok = *firstTok;

    bool couldBeValue = false;

    if (tok.mKind == TOKEN_KIND_STRING_LITERAL)
    {
        // Need to peek if next is :
        char* backupPos = parser->pNext;
        Token nextTok = tokenizeNext(parser);
        parser->pNext = backupPos;
        couldBeValue = nextTok.mKind != TOKEN_KIND_COLON;
    }
    else if (tok.mKind == TOKEN_KIND_OPEN_PAREN || tok.mKind == TOKEN_KIND_FLOAT_LITERAL || tok.mKind == TOKEN_KIND_INT_LITERAL)
        couldBeValue = true;
    else if (tok.mKind != TOKEN_KIND_IDENTIFIER)
    {
        result->mSuccess = false;
        logToken(eERROR, parser, &tok, "Unexpected token. Expected a key or value.");
        return;
    }

    StringRef keyStr = (StringRef){ 0 };

    if (!couldBeValue)
    {
        keyStr = (StringRef){ tok.pSourcePos, tok.mLength };
        tok = tokenizeNext(parser);

        if (tok.mKind != TOKEN_KIND_COLON)
        {
            result->mSuccess = false;
            logToken(eERROR, parser, &tok, "Unexpected token. Expected a ':' to specify value for key. Got this instead.");
            return;
        }

        tok = tokenizeNext(parser);
    }
    else
    {
        // If next token could be a value, then we generate the next key
        // number.
        if (tok.mKind == TOKEN_KIND_OPEN_PAREN || tok.mKind == TOKEN_KIND_STRING_LITERAL || tok.mKind == TOKEN_KIND_FLOAT_LITERAL ||
            tok.mKind == TOKEN_KIND_INT_LITERAL)
        {
            char num[64] = "";
            sprintf(num, "%i", (int)scope->mInternalNextImplicitIndex++);

            keyStr.pStart = "";
            keyStr.mCount = 0;
        }
        else
        {
            result->mSuccess = false;
            logToken(eERROR, parser, &tok, "Unexpected token. Expected a key or array value. Got this instead.");
            return;
        }
    }

    DeserializeValue val;

    if (tok.mKind == TOKEN_KIND_OPEN_PAREN)
    {
        val = parseComposite(parser, result, &tok);
        if (!result->mSuccess)
            return;
    }
    else
    {
        val = parseValue(parser, result, &tok);
        if (!result->mSuccess)
            return;
    }

    arrpush(scope->pKeys, keyStr);
    arrpush(scope->pValues, val);
}

void parseSource(DeserializeResult* result)
{
    Parser parser = (Parser){ 0 };

    parser.pSource = result->pSource;
    parser.pNext = result->pSource;

    Token tok = tokenizeNext(&parser);

    result->mSuccess = true;

    if (tok.mKind != TOKEN_KIND_TF_HEADER)
    {
        result->mSuccess = false;
        logToken(eERROR, &parser, &tok, "Unexpected token. Expected header \"!TheForgeText\". Got this instead.");
        return;
    }

    while (true)
    {
        tok = tokenizeNext(&parser);

        if (tok.mKind == TOKEN_KIND_EOF)
            break;

        parseNextEntry(&parser, result, &result->mGlobal, &tok);
        if (!result->mSuccess)
            return;
    }

    result->mSuccess = true;
}

DeserializeResult deserializeFile(TFResourceDirectory dir, const char* path)
{
    DeserializeResult result = (DeserializeResult){ 0 };

    TFFileStream stream;

    bool ok = fsOpenStreamFromPath(dir, path, TF_FM_READ, &stream);

    if (!ok)
    {
        result.mSuccess = false;
        return result;
    }

    ssize_t sourceSize = fsGetStreamFileSize(&stream);

    if (sourceSize == -1)
    {
        result.mSuccess = false;
        return result;
    }

    result.pSource = (char*)tf_malloc(sourceSize + 1);
    result.pSource[sourceSize] = 0;

    ssize_t read = fsReadFromStream(&stream, result.pSource, sourceSize);
    ASSERT(read == sourceSize);

    char* p = result.pSource;
    while (*p++)
    {
        if (*p == '\t')
            *p = ' ';
    }

    fsCloseStream(&stream);

    parseSource(&result);

    return result;
}
DeserializeResult deserializeMemory(const char* data)
{
    DeserializeResult result = (DeserializeResult){ 0 };

    size_t dataSize = strlen(data) + 1;

    result.pSource = tf_malloc(dataSize);
    memcpy(result.pSource, data, dataSize);

    parseSource(&result);

    return result;
}

void freeDeserializeResult(DeserializeResult result)
{
    tf_free(result.pSource);

    freeComposite(&result.mGlobal);
}

int64_t serializeInt(TypeInfoInt* intType, void* src)
{
    int64_t intVal = 0;

    if (intType->mBase.mSize == 1 && !intType->mIsSigned)
        intVal = (int64_t)*(uint8_t*)src;
    else if (intType->mBase.mSize == 1 && intType->mIsSigned)
        intVal = (int64_t)*(int8_t*)src;
    else if (intType->mBase.mSize == 2 && !intType->mIsSigned)
        intVal = (int64_t)*(uint16_t*)src;
    else if (intType->mBase.mSize == 2 && intType->mIsSigned)
        intVal = (int64_t)*(int16_t*)src;
    else if (intType->mBase.mSize == 4 && !intType->mIsSigned)
        intVal = (int64_t)*(uint32_t*)src;
    else if (intType->mBase.mSize == 4 && intType->mIsSigned)
        intVal = (int64_t)*(int32_t*)src;
    else if (intType->mBase.mSize == 8 && !intType->mIsSigned)
        intVal = (int64_t)*(uint64_t*)src;
    else if (intType->mBase.mSize == 8 && intType->mIsSigned)
        intVal = (int64_t)*(int64_t*)src;
    else
        memcpy(&intVal, src, intType->mBase.mSize);

    return intVal;
}
double serializeFloat(TypeInfoBase* floatType, void* src)
{
    double fltVal = 0.0;

    if (floatType->mSize == 4)
    {
        fltVal = (double)*(float*)src;
    }
    else if (floatType->mSize == 8)
    {
        fltVal = (double)*(double*)src;
    }
    else if (sizeof(long double) == 16 && floatType->mSize == 16) /* Some platforms have 16-byte floats */
    {
        fltVal = (double)*(long double*)src;
    }
    else
    {
        memcpy(&fltVal, src, floatType->mSize);
    }

    return fltVal;
}

void serializeArrayValue(Serializer* s, TypeInfoArray* arrayType, void* src)
{
    TypeInfoBase* elemType = arrayType->pElementType;

    for (uint32_t i = 0; i < arrayType->mArrayCount; i += 1)
    {
        void* pElem = (uint8_t*)src + i * elemType->mSize;

        if (elemType->mKind == TYPE_INFO_TAG_INT || elemType->mKind == TYPE_INFO_TAG_ENUM)
        {
            int64_t intVal = serializeInt(
                elemType->mKind == TYPE_INFO_TAG_INT ? (TypeInfoInt*)elemType : ((TypeInfoEnum*)elemType)->pUnderlyingType, pElem);
            serializerWriteInt(s, NULL, intVal);
        }
        else if (elemType->mKind == TYPE_INFO_TAG_FLOAT)
        {
            double fltVal = serializeFloat(elemType, pElem);
            serializerWriteFloat(s, NULL, fltVal);
        }
        else if (elemType->mKind == TYPE_INFO_TAG_STRUCT)
        {
            serializerPushComposite(s, NULL);
            serializeStruct(s, (TypeInfoStruct*)elemType, pElem);
            serializerPopComposite(s);
        }
        else if (elemType->mKind == TYPE_INFO_TAG_ARRAY)
        {
            serializerPushComposite(s, NULL);
            serializeArrayValue(s, (TypeInfoArray*)elemType, pElem);
            serializerPopComposite(s);
        }
        else
            LOGF(eWARNING, "Could not serialize array element '%s' because it's kind (%s) is not supported.",
                 arrayType->pElementType->pName, ENUM_NAME(TypeInfoTag, elemType->mKind));
    }
}
void serializeStruct(Serializer* s, TypeInfoStruct* structType, void* structData)
{
    // TODO
    // Support more types than floats and ints.

    for (uint32_t i = 0; i < structType->mMemberCount; i += 1)
    {
        TypeInfoStructMember member = structType->pMembers[i];

        if (!member.pType)
            continue; // Member type not reflected

        void* pMember = (uint8_t*)structData + member.mOffsetInStruct;

        if ((member.mFlags & REFLECT_MEMBER_FLAG_STRING) && member.pType->mKind == TYPE_INFO_TAG_ARRAY)
        {
            char* str = (char*)pMember;
            serializerWriteString(s, member.pName, str);
        }
        else if (member.pType->mKind == TYPE_INFO_TAG_INT || member.pType->mKind == TYPE_INFO_TAG_ENUM)
        {
            int64_t intVal = serializeInt(member.pType->mKind == TYPE_INFO_TAG_INT ? (TypeInfoInt*)member.pType
                                                                                   : ((TypeInfoEnum*)member.pType)->pUnderlyingType,
                                          pMember);
            if (member.mFlags & REFLECT_MEMBER_FLAG_HEX)
                serializerWriteIntHex(s, member.pName, intVal);
            else
                serializerWriteInt(s, member.pName, intVal);
        }
        else if (member.pType->mKind == TYPE_INFO_TAG_FLOAT)
        {
            double fltVal = serializeFloat(member.pType, pMember);
            serializerWriteFloat(s, member.pName, fltVal);
        }
        else if (member.pType->mKind == TYPE_INFO_TAG_STRUCT)
        {
            serializerPushComposite(s, member.pName);
            serializeStruct(s, (TypeInfoStruct*)member.pType, pMember);
            serializerPopComposite(s);
        }
        else if (member.pType->mKind == TYPE_INFO_TAG_ARRAY)
        {
            serializerPushComposite(s, member.pName);
            serializeArrayValue(s, (TypeInfoArray*)member.pType, pMember);
            serializerPopComposite(s);
        }
        else
        {
            LOGF(eWARNING, "Could not serialize member '%s::%s %s;' because it's kind (%s) is not supported.", structType->mBase.pName,
                 member.pType->pName, member.pName, ENUM_NAME(TypeInfoTag, member.pType->mKind));
        }
    }
}

bool serializeStructToFile(TFResourceDirectory dir, const char* path, TypeInfoStruct* structType, void* structData)
{
    Serializer s;
    bool       ok = serializerBeginFile(&s, dir, path);

    if (!ok)
        return false;

    serializeStruct(&s, structType, structData);
    serializerEnd(&s);

    return true;
}
char* serializeStructToMemory(TypeInfoStruct* structType, void* structData)
{
    Serializer s;

    serializerBeginMemory(&s);

    serializeStruct(&s, structType, structData);

    char* result = (char*)tf_malloc(serializerGetStringSize(&s) + 1);
    serializerGetString(&s, result, true);

    serializerEnd(&s);

    return result;
}

void deserializeInt(DeserializeValue value, TypeInfoInt* intType, void* dst)
{
    if (intType->mBase.mSize == 1 && !intType->mIsSigned)
        *(uint8_t*)dst = (uint8_t)value.mIntVal;
    else if (intType->mBase.mSize == 1 && intType->mIsSigned)
        *(int8_t*)dst = (int8_t)value.mIntVal;
    else if (intType->mBase.mSize == 2 && !intType->mIsSigned)
        *(uint16_t*)dst = (uint16_t)value.mIntVal;
    else if (intType->mBase.mSize == 2 && intType->mIsSigned)
        *(int16_t*)dst = (int16_t)value.mIntVal;
    else if (intType->mBase.mSize == 4 && !intType->mIsSigned)
        *(uint32_t*)dst = (uint32_t)value.mIntVal;
    else if (intType->mBase.mSize == 4 && intType->mIsSigned)
        *(int32_t*)dst = (int32_t)value.mIntVal;
    else if (intType->mBase.mSize == 8 && !intType->mIsSigned)
        *(uint64_t*)dst = (uint64_t)value.mIntVal;
    else if (intType->mBase.mSize == 8 && intType->mIsSigned)
        *(int64_t*)dst = (int64_t)value.mIntVal;
    else
        memcpy(dst, &value.mIntVal, min(intType->mBase.mSize, sizeof(int64_t)));
}
void deserializeFloat(DeserializeValue value, TypeInfoBase* floatType, void* dst)
{
    if (floatType->mSize == 4)
    {
        *(float*)dst = (float)value.mFltVal;
    }
    else if (floatType->mSize == 8)
    {
        *(double*)dst = (double)value.mFltVal;
    }
    else if (sizeof(long double) == 16 && floatType->mSize == 16)
    {
        *(long double*)dst = (long double)value.mFltVal;
    }
    else
    {
        memcpy(dst, &value.mFltVal, min(floatType->mSize, sizeof(double)));
    }
}
void deserializeString(const DeserializeValue* value, TypeInfoArray* arrayType, char* dst)
{
    size_t len = min(arrayType->mArrayCount - 1, value->mStrRef.mCount);
    memcpy(dst, value->mStrRef.pStart, len);
    dst[len] = 0;
}
void deserializeArray(DeserializeCompositeValue* comp, TypeInfoArray* arrayType, void* dst)
{
    TypeInfoBase* elemType = arrayType->pElementType;

    uint32_t count = min((uint32_t)arrlen(comp->pValues), (uint32_t)arrayType->mArrayCount);
    for (uint32_t i = 0; i < count; i += 1)
    {
        DeserializeValue value = comp->pValues[i];

        void* pElem = (uint8_t*)dst + i * elemType->mSize;

        if (value.mKind == DESERIALIZE_VALUE_STRING && elemType->mKind == TYPE_INFO_TAG_ARRAY)
        {
            char* str = (char*)&pElem;

            deserializeString(&value, (TypeInfoArray*)elemType, str);
        }
        else if (value.mKind == DESERIALIZE_VALUE_INT && (elemType->mKind == TYPE_INFO_TAG_INT || elemType->mKind == TYPE_INFO_TAG_ENUM))
        {
            TypeInfoInt* intType =
                elemType->mKind == TYPE_INFO_TAG_INT ? (TypeInfoInt*)elemType : ((TypeInfoEnum*)elemType)->pUnderlyingType;
            deserializeInt(value, intType, pElem);
            continue;
        }
        else if (value.mKind == DESERIALIZE_VALUE_FLOAT && elemType->mKind == TYPE_INFO_TAG_FLOAT)
        {
            deserializeFloat(value, elemType, pElem);
            continue;
        }
        else if (value.mKind == DESERIALIZE_VALUE_COMPOSITE && elemType->mKind == TYPE_INFO_TAG_STRUCT)
        {
            deserializeStruct(&value.mComposite, (TypeInfoStruct*)elemType, pElem);
            continue;
        }
        else if (value.mKind == DESERIALIZE_VALUE_COMPOSITE && elemType->mKind == TYPE_INFO_TAG_ARRAY)
        {
            deserializeArray(&value.mComposite, (TypeInfoArray*)elemType, pElem);
            continue;
        }
    }
}

void deserializeStruct(DeserializeCompositeValue* scope, TypeInfoStruct* structType, void* structData)
{
    ASSERT(arrlen(scope->pKeys) == arrlen(scope->pValues));

    // Potentially slow, but the result keys should mostly be hot in cache, so the inner loop should be fast. Since member count won't ever
    // be a crazy high number, this should be fine.

    for (uint32_t i = 0; i < structType->mMemberCount; i += 1)
    {
        TypeInfoStructMember member = structType->pMembers[i];

        if (!member.pType)
            continue;

        void* pMember = (uint8_t*)structData + member.mOffsetInStruct;

        size_t memberNameLen = strlen(member.pName);

        // Try to match a key to member.
        for (uint32_t j = 0; j < arrlen(scope->pKeys); j += 1)
        {
            StringRef keyRef = scope->pKeys[j];

            if (keyRef.mCount != memberNameLen)
                continue;

            if (memcmp(keyRef.pStart, member.pName, keyRef.mCount) == 0)
            {
                DeserializeValue value = scope->pValues[j];

                char key[256];
                memcpy(key, keyRef.pStart, min(keyRef.mCount, sizeof(key)));
                key[min(keyRef.mCount, sizeof(key) - 1)] = 0;

                if (value.mKind == DESERIALIZE_VALUE_STRING && member.pType->mKind == TYPE_INFO_TAG_ARRAY)
                {
                    char* str = (char*)pMember;

                    deserializeString(&value, (TypeInfoArray*)member.pType, str);
                }
                else if (value.mKind == DESERIALIZE_VALUE_INT &&
                         (member.pType->mKind == TYPE_INFO_TAG_INT || member.pType->mKind == TYPE_INFO_TAG_ENUM))
                {
                    TypeInfoInt* intType = member.pType->mKind == TYPE_INFO_TAG_INT ? (TypeInfoInt*)member.pType
                                                                                    : ((TypeInfoEnum*)member.pType)->pUnderlyingType;
                    deserializeInt(value, intType, pMember);
                    break;
                }
                else if (value.mKind == DESERIALIZE_VALUE_FLOAT && member.pType->mKind == TYPE_INFO_TAG_FLOAT)
                {
                    deserializeFloat(value, member.pType, pMember);
                    break;
                }
                else if (value.mKind == DESERIALIZE_VALUE_COMPOSITE && member.pType->mKind == TYPE_INFO_TAG_STRUCT)
                {
                    deserializeStruct(&value.mComposite, (TypeInfoStruct*)member.pType, pMember);
                    break;
                }
                else if (value.mKind == DESERIALIZE_VALUE_COMPOSITE && member.pType->mKind == TYPE_INFO_TAG_ARRAY)
                {
                    deserializeArray(&value.mComposite, (TypeInfoArray*)member.pType, pMember);
                    break;
                }
                else
                {
                    LOGF(eWARNING, "Serialized member %s type does not match type of member '%s::%s %s;'. This was skipped.", key,
                         structType->mBase.pName, member.pType->pName, member.pName);
                    continue;
                }
            }
        }
    }
}
bool deserializeStructFromFile(TFResourceDirectory dir, const char* path, TypeInfoStruct* structType, void* structData)
{
    DeserializeResult result = deserializeFile(dir, path);
    if (!result.mSuccess)
    {
        freeDeserializeResult(result);
        return false;
    }

    deserializeStruct(&result.mGlobal, structType, structData);

    freeDeserializeResult(result);

    return true;
}
bool deserializeStructFromMemory(const char* data, TypeInfoStruct* structType, void* structData)
{
    DeserializeResult result = deserializeMemory(data);
    if (!result.mSuccess)
    {
        freeDeserializeResult(result);
        return false;
    }

    deserializeStruct(&result.mGlobal, structType, structData);
    freeDeserializeResult(result);

    return true;
}
