# Copyright (c) 2017-2026 The Forge Interactive Inc.
#
# This file is part of The-Forge
# (see https://github.com/ConfettiFX/The-Forge).
#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.

from utils import Stages, WaveopsFlags, ShaderBinary, Platforms, iter_lines
from utils import isArray, getArrayLen, getArrayBaseName, getMacroName, is_groupshared_decl
from utils import getMacroFirstArg, getHeader, getShader, getMacro, platform_langs, get_whitespace
from utils import get_fn_table, Features, clang_format_file, OSTargets, ArchTargets
from enum import Enum, Flag
import os, re, sys, platform
import traceback
import time
import tempfile
import subprocess
from datetime import datetime

class ParseError(ValueError):
    pass

class TokenKind(Enum):
    UNKNOWN = 0
    PUNCTUATION = 1
    IDENTIFIER = 2
    NUMBER_LITERAL = 3
    DIRECTIVE = 4
    STRING_LITERAL = 5
    EOF = 6

class PunctuationKind(Enum):
    UNKNOWN = 0
    DOT = 1
    LEFT_PAREN = 2
    RIGHT_PAREN = 3
    LEFT_BRACE = 4
    RIGHT_BRACE = 5
    SEMICOLON = 6
    EQUALS = 7
    GT_EQUALS = 8 # >=
    LT_EQUALS = 9 # <=
    NOT_EQUALS = 10 # !=
    EXCLAMATION = 11 # !
    LSHIFT = 12 # <<
    RSHIFT = 13 # >>
    COMMA = 14
    DOUBLEEQUALS = 15 # ==
    LEFT_BRACKET = 16
    RIGHT_BRACKET = 17
    MINUS = 18
    DOUBLEAND = 19
    DOUBLEOR = 20
    TRIPLE_DOT = 21 # ...
    PLUS_EQUALS = 23 # +=
    MINUS_EQUALS = 24 # -=
    MUL_EQUALS = 25 # *=
    DIV_EQUALS = 26 # /=
    OR_EQUALS = 27 # |=
    RSHIFT_EQ = 28 # >>=
    LSHIFT_EQ = 29 # <<=
    TILDE = 30


class DirectiveKind(Enum):
    UNKNOWN = 0
    LINE = 1

class Token:
    def __init__(self):
        self.start_pos = 0
        self.kind = TokenKind.UNKNOWN
        self.punctuation_kind = PunctuationKind.UNKNOWN
        self.directive_kind = DirectiveKind.UNKNOWN
        self.text = ""
        self.literal = None
        self.line_number = 0
        self.full_line = ""
        self.pos_in_line = 0
        self.filepath = ""

class TypeKind(Enum):
    UNKNOWN = 0
    BASE = 1
    COMPOSITE = 2 # This ended up being used for Vector types, and STRUCT and MATRIX are their own types
    STRUCT = 3
    TEX1D = 4
    TEX2D = 5
    TEX3D = 6
    SAMPLER = 7
    BUFFER = 8
    ARRAY = 9
    UNIFORM = 10
    MATRIX = 11

class Type:
    def __init__(self):
        self.name = ""
        self.kind = TypeKind.UNKNOWN
        self.composite_count = 0 # TypeKind.COMPOSITE
        self.underlying_type = "" # TypeKind.COMPOSITE, TypeKind.MATRIX
        self.members = [] # VarDecl's  TypeKind.STRUCT
        self.elem_type = None # TypeKind.ARRAY
        self.elem_count = 0 # TypeKind.ARRAY
        self.row_count = 0 # TypeKind.MATRIX
        self.col_count = 0 # TypeKind.MATRIX

class VarDecl:
    def __init__(self):
        self.name = ""
        self.decl_type = None # Type
        self.is_ref = False


composite_types = [
    "float2", "float3", "float3Aligned", "float4",
    "quat",
    "double2", "double3", "double3Aligned", "double4",
    "uint2", "uint3", "uint3Aligned", "uint4",
    "int2", "int3", "int3Aligned", "int4",
]

def is_alpha(x):
    return (ord(x) >= ord("A") and ord(x) <= ord("Z")) or (ord(x) >= ord("a") and ord(x) <= ord("z"))
def is_digit(x):
    return (ord(x) >= ord("0") and ord(x) <= ord("9"))
def is_whitespace(x):
    return x == "\n" or x == " " or x == "\t" or x == "\r"
def is_punctuation(x):
    return x != "_" and (
        (ord(x) >= ord('!') and ord(x) <= ord('/'))
        or (ord(x) >= ord(':') and ord(x) <= ord('@'))
        or (ord(x) >= ord('[') and ord(x) <= ord('`'))
        or (ord(x) >= ord('{') and ord(x) <= ord('~'))
    )

# Returns a [] of Token's
def tokenize(s: str, filepath: str):

    pos = 0
    end = len(s)

    tokens = []
    current_line = 1
    current_line_start = 0

    while pos < end:
        # Skip whitespace
        while pos < end and is_whitespace(s[pos]):
            if s[pos] == '\n':
                current_line += 1
                current_line_start = pos+1
            pos += 1

        while pos < end and s[pos] == '/' and (pos + 1 < end and (s[pos + 1] == '/' or s[pos + 1] == '*')):
            # Skip single-line comments
            while pos < end and s[pos] == '/' and (pos + 1 < end and s[pos + 1] == '/'):

                while pos < end and s[pos] != '\n':
                    pos += 1
                if pos < end and s[pos] == '\n':
                    current_line += 1
                    current_line_start = pos+1
                    pos += 1

            # Skip whitespace after comments
            while pos < end and is_whitespace(s[pos]):
                assert s[pos] != '\r'
                if s[pos] == '\n':
                    current_line += 1
                    current_line_start = pos+1
                pos += 1

            # Skip multi-line comments
            depth = 0
            while pos < end and s[pos] == '/' and (pos + 1 < end and s[pos + 1] == '*'):
                while (pos + 1) < end and not (s[pos] == '*' and s[pos + 1] == '/' and depth == 1):
                    if pos + 1 < end and s[pos] == '/' and s[pos + 1] == '*':
                        depth += 1
                    if pos + 1 < end and s[pos] == '*' and s[pos + 1] == '/':
                        depth -= 1
                    if s[pos] == '\n':
                        current_line += 1
                        current_line_start = pos+1
                    pos += 1
                if pos + 1 < end and s[pos] == '*' and s[pos + 1] == '/':
                    pos += 2

            # Skip whitespace after multi-line comments
            while pos < end and is_whitespace(s[pos]):
                if s[pos] == '\n':
                    current_line += 1
                    current_line_start = pos+1
                pos += 1

        # Skip remaining whitespace
        while pos < end and is_whitespace(s[pos]):
            if s[pos] == '\n':
                current_line += 1
                current_line_start = pos+1
            pos += 1

        if pos >= end:
            break

        token = Token()
        token.start_pos = pos
        token.line_number = current_line
        token.filepath = filepath

        token.pos_in_line = pos-current_line_start

        line_end_pos = pos
        while line_end_pos < end:
            if s[line_end_pos] == "\n":
                break
            line_end_pos += 1

        token.full_line = s[current_line_start:line_end_pos]

        # Tokenize string literal
        if s[pos] == '"':

            pos += 1
            while pos < end and s[pos] != '"':
                pos += 1
            if s[pos] == '"':
                pos += 1

            token.kind = TokenKind.STRING_LITERAL
            token.text = s[token.start_pos:pos]
            token.literal = token.text[1:len(token.text)-1] if len(token.text) > 2 else ""

            tokens += [token]

            continue

        # Tokenize number literal
        if is_digit(s[pos]):

            is_hex = False
            if pos+1 < end and s[pos] == '0' and s[pos+1] == 'x':
                # Hex number
                pos += 2
                token.start_pos += 2;
                is_hex = True

            has_dot = False
            while is_digit(s[pos]) or s[pos] == '.' or (is_hex and ((ord(s[pos]) >= ord('A') and ord(s[pos]) >= ord('F')) or (ord(s[pos]) >= ord('a') and ord(s[pos]) >= ord('f')))):
                if s[pos] == '.' and ((has_dot) or is_hex):
                    break
                if s[pos] == '.' and not has_dot:
                    has_dot = True
                pos += 1

            # Check for scientific notation
            if not is_hex and pos < end and (s[pos] == 'e' or s[pos] == 'E'):
                pos += 1
                if pos < end and (s[pos] == '+' or s[pos] == '-'):
                    pos += 1
                # Accept digits of exponent
                while pos < end and is_digit(s[pos]):
                    pos += 1
                has_dot = True  # Scientific notation implies float

            token.kind = TokenKind.NUMBER_LITERAL
            token.literal = int(s[token.start_pos:pos], base=16 if is_hex else 10) if not has_dot else float(s[token.start_pos:pos])
            token.text = str(token.literal)

            if s[pos] == "f":
                pos += 1


            tokens += [token]
            continue


        # Tokenize directive
        if s[pos] == "#" and pos < end-1 and is_alpha(s[pos+1]):
            pos += 1
            word_start = pos
            while pos < end and is_alpha(s[pos]) or s[pos] == "_" or is_digit(s[pos]):
                pos += 1
            word = s[word_start:pos]


            if word == "line":
                token.directive_kind = DirectiveKind.LINE

                pos += 1
                num_start = pos
                while is_digit(s[pos]):
                    pos += 1
                current_line = int(s[num_start:pos])

                pos += 1
                path_start = pos

                while s[pos] != '\n':
                    pos += 1

                filepath = s[path_start:pos].replace('"', '')
                pos += 1
                current_line_start = pos
            else:
                # include entire directive
                while pos < end and (s[pos] != '\n' or (pos > 0 and s[pos-1] == '\\')):
                    pos += 1

            token.kind = TokenKind.DIRECTIVE
            token.text = s[token.start_pos:pos]

            tokens += [token]



            continue

        # Tokenize punctuation
        # (ascii ranges bettween alpha-numeric ranges)
        if is_punctuation(s[pos]):

            token.kind = TokenKind.PUNCTUATION
            punc_len = 2

            if pos+1<end and s[pos:pos+2] == "==": # VOLATILE this check must happen before '='
                token.punctuation_kind = PunctuationKind.DOUBLEEQUALS
            elif pos+1<end and s[pos:pos+2] == ">=":
                token.punctuation_kind = PunctuationKind.GT_EQUALS
            elif pos+1<end and s[pos:pos+2] == "<=":
                token.punctuation_kind = PunctuationKind.LT_EQUALS
            elif pos+1<end and s[pos:pos+2] == "!=":
                token.punctuation_kind = PunctuationKind.NOT_EQUALS
            elif pos+1<end and s[pos:pos+2] == "&&":
                token.punctuation_kind = PunctuationKind.LSHIFT
            elif pos+2<end and s[pos:pos+3] == ">>=":
                punc_len = 3
                token.punctuation_kind = PunctuationKind.RSHIFT_EQ
            elif pos+2<end and s[pos:pos+3] == "<<=":
                punc_len = 3
                token.punctuation_kind = PunctuationKind.LSHIFT_EQ
            elif pos+1<end and s[pos:pos+2] == "<<":
                token.punctuation_kind = PunctuationKind.RSHIFT
            elif pos+1<end and s[pos:pos+2] == ">>":
                token.punctuation_kind = PunctuationKind.DOUBLEAND
            elif pos+1<end and s[pos:pos+2] == "||":
                token.punctuation_kind = PunctuationKind.DOUBLEOR
            elif pos+1<end and s[pos:pos+2] == "+=":
                token.punctuation_kind = PunctuationKind.PLUS_EQUALS
            elif pos+1<end and s[pos:pos+2] == "-=":
                token.punctuation_kind = PunctuationKind.MINUS_EQUALS
            elif pos+1<end and s[pos:pos+2] == "*=":
                token.punctuation_kind = PunctuationKind.MUL_EQUALS
            elif pos+1<end and s[pos:pos+2] == "/=":
                token.punctuation_kind = PunctuationKind.DIV_EQUALS
            elif pos+1<end and s[pos:pos+2] == "|=":
                token.punctuation_kind = PunctuationKind.OR_EQUALS
            elif pos+2<end and s[pos:pos+3] == "...":
                punc_len = 3
                token.punctuation_kind = PunctuationKind.TRIPLE_DOT
            elif pos+1<end and s[pos:pos+2] == "..": # Triple dot is ispc thing, and its easy to accidentally type .. instead of ..., so why not allow programmers to type .. as well.
                token.punctuation_kind = PunctuationKind.TRIPLE_DOT
            else:
                punc_len = 1
                # Single punctuation
                if s[pos] == ".":
                    token.punctuation_kind = PunctuationKind.DOT
                elif s[pos] == "(":
                    token.punctuation_kind = PunctuationKind.LEFT_PAREN
                elif s[pos] == ")":
                    token.punctuation_kind = PunctuationKind.RIGHT_PAREN
                elif s[pos] == "{":
                    token.punctuation_kind = PunctuationKind.LEFT_BRACE
                elif s[pos] == "}":
                    token.punctuation_kind = PunctuationKind.RIGHT_BRACE
                elif s[pos] == ";":
                    token.punctuation_kind = PunctuationKind.SEMICOLON
                elif s[pos] == "=":
                    token.punctuation_kind = PunctuationKind.EQUALS
                elif s[pos] == "!":
                    token.punctuation_kind = PunctuationKind.EXCLAMATION
                elif s[pos] == ",":
                    token.punctuation_kind = PunctuationKind.COMMA
                elif s[pos] == "[":
                    token.punctuation_kind = PunctuationKind.LEFT_BRACKET
                elif s[pos] == "]":
                    token.punctuation_kind = PunctuationKind.RIGHT_BRACKET
                elif s[pos] == "-":
                    token.punctuation_kind = PunctuationKind.MINUS
                elif s[pos] == "~":
                    token.punctuation_kind = PunctuationKind.TILDE

            token.text = s[token.start_pos:pos+punc_len]
            tokens += [token]
            pos += punc_len
            continue


        # Tokenize identifier
        if is_alpha(s[pos]) or s[pos] == "_":
            while pos < end and is_alpha(s[pos]) or s[pos] == "_" or is_digit(s[pos]):
                pos += 1

            token.kind = TokenKind.IDENTIFIER
            token.text = s[token.start_pos:pos]

            tokens += [token]
            continue


        print(f"ERROR: Unhandled token '{s[pos]}'")
        pos += 1

    # Put an EOF token at end
    eof = Token()
    eof.kind = TokenKind.EOF
    eof.text = "<eof>"
    eof.start_pos = pos-1
    eof.line_number = current_line
    eof.full_line = s[current_line_start:pos]
    eof.pos_in_line = len(eof.full_line)-1
    eof.filepath = filepath
    return tokens + [eof]


def extract_parentheses_content(s):
    depth = 0
    start = -1

    for i, char in enumerate(s):
        if char == '(':
            if depth == 0:
                start = i + 1
            depth += 1
        elif char == ')':
            depth -= 1
            if depth == 0:
                return s[start:i]

    return None

def split_args(argument_str):
    """Splits arguments while respecting nested parentheses."""
    args = []
    current_arg = []
    depth = 0

    for char in argument_str:
        if char == ',' and depth == 0:
            args.append(''.join(current_arg).strip())
            current_arg = []
        else:
            if char == '(':
                depth += 1
            elif char == ')':
                depth -= 1
            current_arg.append(char)

    if current_arg:
        args.append(''.join(current_arg).strip())

    return args

def print_token(token: Token):
    print(f"'{token.text}' {token.kind}{' ' + token.punctuation_kind.name if token.kind == TokenKind.PUNCTUATION else ''}{' ' + token.literal if token.kind == TokenKind.STRING_LITERAL else ''}")
    print(f"{token.filepath}({token.line_number},{token.pos_in_line}):")
    print(f"{token.full_line}")
    print(f'{" "*token.pos_in_line}{"^"*len(token.text)}')


def ispc(*args):
    debug, binary, dst = args

    fsl = binary.preprocessed_srcs[Platforms.ISPC]

    program_name = binary.filename.split(".")[0]

    shader_lines = []

    ispc_gen_helpers = """
inline varying float * varying __subscript(varying float2 * varying v, const varying int i) {
    varying float * varying values[2] = { &v->x, &v->y };
    return values[i];
}
inline varying float * varying __subscript(varying float3 * varying v, const varying int i) {
    varying float * varying values[3] = { &v->x, &v->y, &v->z };
    return values[i];
}
inline varying float * varying __subscript(varying float3Aligned * varying v, const varying int i) {
    varying float * varying values[3] = { &v->x, &v->y, &v->z };
    return values[i];
}
inline varying float * varying __subscript(varying float4 * varying v, const varying int i) {
    varying float * varying values[4] = { &v->x, &v->y, &v->z, &v->w };
    return values[i];
}
inline uniform float * uniform __subscript(uniform float2 * uniform v, const uniform int i) {
    uniform float * uniform values[2] = { &v->x, &v->y };
    return values[i];
}
inline uniform float * uniform __subscript(uniform float3 * uniform v, const uniform int i) {
    uniform float * uniform values[3] = { &v->x, &v->y, &v->z };
    return values[i];
}
inline uniform float * uniform __subscript(uniform float3Aligned * uniform v, const uniform int i) {
    uniform float * uniform values[3] = { &v->x, &v->y, &v->z };
    return values[i];
}
inline uniform float * uniform __subscript(uniform float4 * uniform v, const uniform int i) {
    uniform float * uniform values[4] = { &v->x, &v->y, &v->z, &v->w };
    return values[i];
}
inline uniform float * varying __subscript(uniform float2 * uniform v, const varying int i) {
    uniform float * uniform values[2] = { &v->x, &v->y };
    return values[i];
}
inline uniform float * varying __subscript(uniform float3 * uniform v, const varying int i) {
    uniform float * uniform values[3] = { &v->x, &v->y, &v->z };
    return values[i];
}
inline uniform float * varying __subscript(uniform float3Aligned * uniform v, const varying int i) {
    uniform float * uniform values[3] = { &v->x, &v->y, &v->z };
    return values[i];
}
inline uniform float * varying __subscript(uniform float4 * uniform v, const varying int i) {
    uniform float * uniform values[4] = { &v->x, &v->y, &v->z, &v->w };
    return values[i];
}
inline uniform float * varying __subscript(uniform float4 * varying v, const varying int i) {
    uniform float * varying values[4] = { &v->x, &v->y, &v->z, &v->w };
    return values[i];
}

inline varying double * varying __subscript(varying double2 * varying v, const varying int i) {
    varying double * varying values[2] = { &v->x, &v->y };
    return values[i];
}
inline varying double * varying __subscript(varying double3 * varying v, const varying int i) {
    varying double * varying values[3] = { &v->x, &v->y, &v->z };
    return values[i];
}
inline varying double * varying __subscript(varying double3Aligned * varying v, const varying int i) {
    varying double * varying values[3] = { &v->x, &v->y, &v->z };
    return values[i];
}
inline varying double * varying __subscript(varying double4 * varying v, const varying int i) {
    varying double * varying values[4] = { &v->x, &v->y, &v->z, &v->w };
    return values[i];
}
inline uniform double * uniform __subscript(uniform double2 * uniform v, const uniform int i) {
    uniform double * uniform values[2] = { &v->x, &v->y };
    return values[i];
}
inline uniform double * uniform __subscript(uniform double3 * uniform v, const uniform int i) {
    uniform double * uniform values[3] = { &v->x, &v->y, &v->z };
    return values[i];
}
inline uniform double * uniform __subscript(uniform double3Aligned * uniform v, const uniform int i) {
    uniform double * uniform values[3] = { &v->x, &v->y, &v->z };
    return values[i];
}
inline uniform double * uniform __subscript(uniform double4 * uniform v, const uniform int i) {
    uniform double * uniform values[4] = { &v->x, &v->y, &v->z, &v->w };
    return values[i];
}
inline uniform double * varying __subscript(uniform double2 * uniform v, const varying int i) {
    uniform double * uniform values[2] = { &v->x, &v->y };
    return values[i];
}
inline uniform double * varying __subscript(uniform double3 * uniform v, const varying int i) {
    uniform double * uniform values[3] = { &v->x, &v->y, &v->z };
    return values[i];
}
inline uniform double * varying __subscript(uniform double3Aligned * uniform v, const varying int i) {
    uniform double * uniform values[3] = { &v->x, &v->y, &v->z };
    return values[i];
}
inline uniform double * varying __subscript(uniform double4 * uniform v, const varying int i) {
    uniform double * uniform values[4] = { &v->x, &v->y, &v->z, &v->w };
    return values[i];
}
inline uniform double * varying __subscript(uniform double4 * varying v, const varying int i) {
    uniform double * varying values[4] = { &v->x, &v->y, &v->z, &v->w };
    return values[i];
}

inline varying uint * varying __subscript(varying uint2 * varying v, const varying int i) {
    varying uint * varying values[2] = { &v->x, &v->y };
    return values[i];
}
inline varying uint * varying __subscript(varying uint3 * varying v, const varying int i) {
    varying uint * varying values[3] = { &v->x, &v->y, &v->z };
    return values[i];
}
inline varying uint * varying __subscript(varying uint3Aligned * varying v, const varying int i) {
    varying uint * varying values[3] = { &v->x, &v->y, &v->z };
    return values[i];
}
inline varying uint * varying __subscript(varying uint4 * varying v, const varying int i) {
    varying uint * varying values[4] = { &v->x, &v->y, &v->z, &v->w };
    return values[i];
}
inline uniform uint * uniform __subscript(uniform uint2 * uniform v, const uniform int i) {
    uniform uint * uniform values[2] = { &v->x, &v->y };
    return values[i];
}
inline uniform uint * uniform __subscript(uniform uint3 * uniform v, const uniform int i) {
    uniform uint * uniform values[3] = { &v->x, &v->y, &v->z };
    return values[i];
}
inline uniform uint * uniform __subscript(uniform uint3Aligned * uniform v, const uniform int i) {
    uniform uint * uniform values[3] = { &v->x, &v->y, &v->z };
    return values[i];
}
inline uniform uint * uniform __subscript(uniform uint4 * uniform v, const uniform int i) {
    uniform uint * uniform values[4] = { &v->x, &v->y, &v->z, &v->w };
    return values[i];
}
inline uniform uint * varying __subscript(uniform uint2 * uniform v, const varying int i) {
    uniform uint * uniform values[2] = { &v->x, &v->y };
    return values[i];
}
inline uniform uint * varying __subscript(uniform uint3 * uniform v, const varying int i) {
    uniform uint * uniform values[3] = { &v->x, &v->y, &v->z };
    return values[i];
}
inline uniform uint * varying __subscript(uniform uint3Aligned * uniform v, const varying int i) {
    uniform uint * uniform values[3] = { &v->x, &v->y, &v->z };
    return values[i];
}
inline uniform uint * varying __subscript(uniform uint4 * uniform v, const varying int i) {
    uniform uint * uniform values[4] = { &v->x, &v->y, &v->z, &v->w };
    return values[i];
}

inline varying int * varying __subscript(varying int2 * varying v, const varying int i) {
    varying int * varying values[2] = { &v->x, &v->y };
    return values[i];
}
inline varying int * varying __subscript(varying int3 * varying v, const varying int i) {
    varying int * varying values[3] = { &v->x, &v->y, &v->z };
    return values[i];
}
inline varying int * varying __subscript(varying int3Aligned * varying v, const varying int i) {
    varying int * varying values[3] = { &v->x, &v->y, &v->z };
    return values[i];
}
inline varying int * varying __subscript(varying int4 * varying v, const varying int i) {
    varying int * varying values[4] = { &v->x, &v->y, &v->z, &v->w };
    return values[i];
}
inline uniform int * uniform __subscript(uniform int2 * uniform v, const uniform int i) {
    uniform int * uniform values[2] = { &v->x, &v->y };
    return values[i];
}
inline uniform int * uniform __subscript(uniform int3 * uniform v, const uniform int i) {
    uniform int * uniform values[3] = { &v->x, &v->y, &v->z };
    return values[i];
}
inline uniform int * uniform __subscript(uniform int3Aligned * uniform v, const uniform int i) {
    uniform int * uniform values[3] = { &v->x, &v->y, &v->z };
    return values[i];
}
inline uniform int * uniform __subscript(uniform int4 * uniform v, const uniform int i) {
    uniform int * uniform values[4] = { &v->x, &v->y, &v->z, &v->w };
    return values[i];
}
inline uniform int * varying __subscript(uniform int2 * uniform v, const varying int i) {
    uniform int * uniform values[2] = { &v->x, &v->y };
    return values[i];
}
inline uniform int * varying __subscript(uniform int3 * uniform v, const varying int i) {
    uniform int * uniform values[3] = { &v->x, &v->y, &v->z };
    return values[i];
}
inline uniform int * varying __subscript(uniform int3Aligned * uniform v, const varying int i) {
    uniform int * uniform values[3] = { &v->x, &v->y, &v->z };
    return values[i];
}
inline uniform int * varying __subscript(uniform int4 * uniform v, const varying int i) {
    uniform int * uniform values[4] = { &v->x, &v->y, &v->z, &v->w };
    return values[i];
}

    """

    # Paste Math Headers
    shader_lines += ["#noparse_start\n"]
    header_path = os.path.join(os.path.dirname(os.path.dirname(__file__)), '../../Utilities/Math', 'MathDefs.h').replace("\\", "/")
    header_lines = open(header_path).readlines()
    shader_lines += [f'#line 1 "{header_path}"\n']
    shader_lines += [l.replace("$PROGRAM_NAME", program_name) for l in header_lines] + ["\n"]

    shader_lines += [l + "\n" for l in ispc_gen_helpers.split("\n")]
    shader_lines += ["#noparse_end\n"]

    # header_path = os.path.join(os.path.dirname(os.path.dirname(__file__)), 'includes', 'MathFSLBase.h').replace("\\", "/")
    # header_lines = open(header_path).readlines()
    # shader_lines += [f'#line 1 "{header_path}"\n']
    # shader_lines += [l.replace("$PROGRAM_NAME", program_name) for l in header_lines] + ["\n"]

    # Paste ispc.h
    header_path = os.path.join(os.path.dirname(os.path.dirname(__file__)), 'includes', 'ispc.h').replace("\\", "/")
    header_lines = open(header_path).readlines()
    shader_lines += [f'#line 1 "{header_path}"\n']
    shader_lines += [l.replace("$PROGRAM_NAME", program_name) for l in header_lines] + ["\n"]

    if program_name.lower() != "mathlibrary":
        # Paste MathLibrary.h
        header_path = os.path.join(os.path.dirname(os.path.dirname(__file__)), 'includes', 'MathLibrary.h').replace("\\", "/")
        header_lines = open(header_path).readlines()
        shader_lines += [f'#line 1 "{header_path}"\n']
        shader_lines += [l.replace("$PROGRAM_NAME", program_name) for l in header_lines] + ["\n"]

    # We do a first pass to translate the things that are trivial to translate

    STATE_NONE = 0
    STATE_PARSING_SRT = 1
    STATE_PARSING_SRT_SET = 2
    STATE_PARSING_SRT_DECL = 3

    state = STATE_NONE

    srt_name = ""
    srt_set = ""

    main_root_signature = ""

    # As we add/remove lines in the output code, we need to keep track of that and modify
    # #line directives for ISPC to give correct error messages.
    line_offset = 0

    """
    srt: {
        set: [
            resource: { freq: "", type: "", underlying_type = "", name = "" }
        ]
    }
    """
    resources = {}

    has_cs_main = False
    has_num_threads = False

    for line in fsl:
        # replace $ symbols
        line = line.replace("$PROGRAM_NAME", program_name)
        line_stripped = line.strip()

        # Maintain line number coherency for error messages
        if line_stripped.startswith("#line") or (line_stripped.startswith("# ") and is_digit(line_stripped[2])):
            parts = line_stripped.split(" ")
            original_line = int(parts[1])
            src_file = parts[2]
            if len(parts) > 3:
                # Sometimes mac preprocessor puts line directive with a weird format and this is us dealing with that
                src_file = "\"" + "".join(("".join(parts[2:])).split("\"")[0:2]) + "\""

            shader_lines += [f"#line {original_line+line_offset} {src_file}\n"]
            continue

        if state == STATE_NONE:
            if line_stripped.startswith("BEGIN_SRT("):
                srt_name = line_stripped.split("(")[1].split(")")[0]
                resources[srt_name] = {}
                state = STATE_PARSING_SRT
                line_offset += 1
                continue
            if "ROOT_SIGNATURE(" in line_stripped:
                main_root_signature = line_stripped.split("ROOT_SIGNATURE(")[1].split(")")[0].strip()
                continue
            if "NUM_THREADS(" in line_stripped:
                has_num_threads = True
                shader_lines += ["#noparse_start\n"]
                shader_lines += [line_stripped]
                shader_lines += ["\n#noparse_end\n"]
                line_offset -= 2 #noparse lines
                continue

            if "CS_MAIN(" in line_stripped:
                has_cs_main = True
                content = extract_parentheses_content(line_stripped)
                args = split_args(content)
                args = [arg.strip() for arg in args]

                main_def_line = "void CS_MAIN("

                group_thread_id_name = None
                group_index_name = None
                group_id_name = None
                dispatch_thread_id_name = None

                # in the ISPC we will pass the CS_MAIN parameters in the same order always,
                # and use placeholder names if the FSL code has not specified that parameter
                for arg in args:
                    if arg.startswith("SV_GroupThreadID("):
                        underlying = arg.split("SV_GroupThreadID(")[1].split(")")[0]
                        if underlying != "uint3":
                            print(f"ERROR: Expected uint3 type, got {underlying}\n\t", line_stripped)
                            raise ParseError("Main arguments error")
                        group_thread_id_name = arg.split("SV_GroupThreadID(")[1].split(")")[1]

                    if arg.startswith("SV_GroupIndex("):
                        underlying = arg.split("SV_GroupIndex(")[1].split(")")[0]
                        if underlying != "uint":
                            print(f"ERROR: Expected uint type, got {underlying}\n\t", line_stripped)
                            raise ParseError("Main arguments error")
                        group_index_name = arg.split("SV_GroupIndex(")[1].split(")")[1]

                    if arg.startswith("SV_GroupID("):
                        underlying = arg.split("SV_GroupID(")[1].split(")")[0]
                        if underlying != "uint3":
                            print(f"ERROR: Expected uint3 type, got {underlying}\n\t", line_stripped)
                            raise ParseError("Main arguments error")
                        group_id_name = arg.split("SV_GroupID(")[1].split(")")[1]

                    if arg.startswith("SV_DispatchThreadID("):
                        underlying = arg.split("SV_DispatchThreadID(")[1].split(")")[0]
                        if underlying != "uint3":
                            print(f"ERROR: Expected uint3 type, got {underlying}\n\t", line_stripped)
                            raise ParseError("Main arguments error")
                        dispatch_thread_id_name = arg.split("SV_DispatchThreadID(")[1].split(")")[1]

                # The generated ISPC entry function will always have the same signature,
                # so just put placeholders for the arguments that were not specified in
                # the FSL code
                main_def_line += "uint3 "
                if group_thread_id_name:
                    main_def_line += group_thread_id_name
                else:
                    main_def_line += "___"
                main_def_line += ", "
                main_def_line += "uint "
                if group_index_name:
                    main_def_line += group_index_name
                else:
                    main_def_line += "____"
                main_def_line += ", "
                main_def_line += "uint3 "
                if group_id_name:
                    main_def_line += group_id_name
                else:
                    main_def_line += "_____"
                main_def_line += ", "
                main_def_line += "uint3 "
                if dispatch_thread_id_name:
                    main_def_line += dispatch_thread_id_name
                else:
                    main_def_line += "______"

                main_def_line += ")"

                shader_lines += [main_def_line]
                continue

            shader_lines += [line]
            continue

        if state == STATE_PARSING_SRT:
            if line_stripped.startswith("BEGIN_SRT_SET("):
                srt_set = line_stripped.split("(")[1].split(")")[0]
                state = STATE_PARSING_SRT_SET
                resources[srt_name][srt_set] = []
                continue
            if line_stripped.startswith("END_SRT("):
                state = STATE_NONE
                line_offset += 1
                continue

        if state == STATE_PARSING_SRT_SET:
            if line_stripped.startswith("END_SRT_SET("):
                state = STATE_PARSING_SRT
                # Emit resource declarations
                shader_lines += ["#noparse_start\n"]
                for res in resources[srt_name][srt_set]:
                    res_line = ""
                    # res_line += "export " + res["type"]
                    res_line += res["type"]
                    if res["underlying_type"] and res["underlying_type"] != "":
                        res_line += "(" + res["underlying_type"] + ")"
                    res_line += " " + res["name"] + ";"
                    shader_lines += [res_line+"\n"]
                shader_lines += ["#noparse_end\n"]
                line_offset -= 2 #noparse lines
                continue

            if line_stripped.startswith("DECL_"):

                decl_type = line_stripped.split("_")[1].split("(")[0]

                content = extract_parentheses_content(line_stripped)
                if not content:
                    print("ERROR: Bad arguments in resource declaration\n\t", line_stripped)
                    raise ParseError("Resource Declaration Error")

                args = split_args(content)
                args = [arg.strip() for arg in args]

                if len(args) < 3:
                    print(args)
                    print("ERROR: Not enough arguments in resource declaration\n\t", line_stripped)
                    raise ParseError("Resource Declaration Error")

                frequency = args[0]
                resource_info = args[1]
                resource_name = args[2]

                # Check if resource type has underlying type
                # For example Tex2D(float4) has underlying type float4
                match = re.match(r'(\w+)\s*\((.*?)\)', resource_info)
                if match:
                    resource_type = match.group(1)
                    resource_underlying_type = match.group(2)
                else:
                    resource_type = resource_info
                    resource_underlying_type = None

                resources[srt_name][srt_set] += [{
                    "freq": frequency,
                    "type": resource_type,
                    "underlying_type": resource_underlying_type,
                    "name": resource_name,
                }]

                continue

    global output_ispc_src
    global output_ispc_line_num
    global output_ispc_brace_depth
    global output_c_src
    global output_c_line_num
    global output_c_brace_depth
    global output_fsl_src
    global output_fsl_line_num
    global output_fsl_brace_depth

    global emit_mask

    global close_paren_after_assign

    output_ispc_src = ""
    output_ispc_line_num = 1
    output_ispc_brace_depth = 0
    output_c_src = ""
    output_c_line_num = 1
    output_c_brace_depth = 0
    output_fsl_src = ""
    output_fsl_line_num = 1
    output_fsl_brace_depth = 0

    emit_mask = {'ispc': True, 'c': False, 'fsl': False}

    close_paren_after_assign = 0

    shader_lines += ["#noparse_start\n"]

    if (has_cs_main or has_num_threads) and has_cs_main != has_num_threads:
        print("ISPC shaders must be either both or none of the following specified: CS_MAIN, NUM_THREADS")
        raise ParseError("Main Parser Error")

    if has_cs_main and has_num_threads:
        res_index = 0
        res_count = 0 # Need this for generating get_uniform_index
        get_uniform_pointer_lines = [
            f"export void * uniform {program_name}_get_uniform_pointer (uniform int index)",
            "{"
        ]
        for srt_name, srt_sets in resources.items():
            for srt_set_name, srt_set in srt_sets.items():
                for res in srt_set:
                    get_uniform_pointer_lines.append(f"\tif (index == {res_index})")
                    get_uniform_pointer_lines.append(f"\t\treturn &{res['name']};")
                    res_index += 1
                    res_count += 1 # Need this for generating get_uniform_index
        get_uniform_pointer_lines.append(f"\treturn 0;")
        get_uniform_pointer_lines.append(r"}")
        get_uniform_pointer_block = "\n".join(get_uniform_pointer_lines)

        res_index = 0
        get_uniform_index_lines = [
            f"export int uniform {program_name}_get_uniform_index (uniform uint8 * uniform cString)",
            "{"
        ]
        for srt_name, srt_sets in resources.items():
            for srt_set_name, srt_set in srt_sets.items():
                for res in srt_set:
                    name = res['name']
                    length = len(name)

                    # build comma-separated list of byte values for each char
                    byte_list = ",".join(str(ord(c)) for c in name)

                    # We need to do a C string compare manually, because ISPC has limited support for strings
                    get_uniform_index_lines.append(f"\t{{")
                    get_uniform_index_lines.append(f"\t\tuniform uint8 nameArray[] = {{ {byte_list}, 0 }}; // '{name}'")
                    get_uniform_index_lines.append(f"\t\tuniform bool match = true;")
                    get_uniform_index_lines.append(f"\t\tfor (uniform int j = 0; j < {length}; ++j) {{")
                    get_uniform_index_lines.append(f"\t\t\tif (cString[j] != nameArray[j]) {{")
                    get_uniform_index_lines.append(f"\t\t\t\tmatch = false;")
                    get_uniform_index_lines.append(f"\t\t\t\tbreak;")
                    get_uniform_index_lines.append(f"\t\t\t}}")
                    get_uniform_index_lines.append(f"\t\t}}")
                    get_uniform_index_lines.append(f"\t\tif (match && cString[{length}] == 0) return {res_index};")
                    get_uniform_index_lines.append(f"\t}}  // end check for \"{name}\"")
                    res_index += 1

        get_uniform_index_lines.append(f"\treturn -1; // Return -1 when no uniform with given name")
        get_uniform_index_lines.append(r"}")
        get_uniform_index_block = "\n".join(get_uniform_index_lines)

        dispatch_block = f"""\

    task void {program_name}_task_y(uniform int groupCountX, uniform int groupCountY, uniform int groupCountZ)
    {{
        uniform uint numThreadsX = {program_name}_num_threads_x();
        uniform uint numThreadsY = {program_name}_num_threads_y();
        uniform uint numThreadsZ = {program_name}_num_threads_z();

        uniform uint numKernelsX = groupCountX * numThreadsX;
        uniform uint numKernelsY = groupCountY * numThreadsY;
        uniform uint numKernelsZ = groupCountZ * numThreadsZ;

        uniform uint y = taskIndex;
        uniform uint groupY  = y / numThreadsY;
        uniform uint threadY = y % numThreadsY;

        uniform uint z = 0;
        while (z < numKernelsZ)
        {{
            uniform uint groupZ  = z / numThreadsZ;
            uniform uint threadZ = z % numThreadsZ;

            foreach (x = 0 ... numKernelsX)
            {{
                uint groupX  = x / numThreadsX;
                uint threadX = x % numThreadsX;

                uint3 groupThreadID = {{threadX, threadY, threadZ}};

                uint groupIndex = threadZ * (numThreadsX * numThreadsY)
                                + threadY * numThreadsX
                                + threadX;

                uint3 groupID = {{groupX, groupY, groupZ}};

                uint3 dispatchThreadID = {{ (uint)x, (uint)y, (uint)z }};

                {program_name}(groupThreadID, groupIndex, groupID, dispatchThreadID);
            }}

            z += 1;
        }}
    }}

    export void {program_name}_dispatch (uniform int groupCountX, uniform int groupCountY, uniform int groupCountZ)
    {{

        uniform uint numThreadsY = {program_name}_num_threads_y();
        uniform uint numKernelsY = groupCountY * numThreadsY;

        launch [numKernelsY] {program_name}_task_y(groupCountX, groupCountY, groupCountZ);
    }}"""

        kernel_interface = f"{get_uniform_pointer_block}\n\n{get_uniform_index_block}\n\n{dispatch_block}"

        shader_lines += [l + "\n" for l in kernel_interface.split("\n")]

        output_c_src += "#ifdef __cplusplus\n"
        output_c_src += "extern \"C\"\n{\n"
        output_c_src += "#endif\n"
        output_c_src += f"extern void {program_name}_dispatch(int32_t groupCountX, int32_t groupCountY, int32_t groupCountZ);\n"
        output_c_src += f"extern void* {program_name}_get_uniform_pointer(int32_t index);\n"
        output_c_src += f"extern int32_t {program_name}_get_uniform_index(const char* name);\n"
        output_c_src += "#ifdef __cplusplus\n"
        output_c_src += "}\n"
        output_c_src += "#endif\n"

    shader_lines += ["#noparse_end\n"]

    first_pass = "".join([l.replace("\t", "    ") for l in shader_lines])

    tokens = tokenize(first_pass, dst)

    global token_index
    token_index = 0

    def format_decl_string(decl: VarDecl, is_reference, target: str, strip_uniform_semantic = False, arrays_are_pointers = False):

        if decl.decl_type.kind != TypeKind.ARRAY:
            type_name = decl.decl_type.name
            if strip_uniform_semantic and decl.decl_type.kind == TypeKind.UNIFORM:
                type_name = decl.decl_type.underlying_type

            if target == 'fsl':
                return f"{'inout(' if is_reference else ''}{type_name}{')' if is_reference else ''} {decl.name}"
            elif target == 'ispc':
                return f"{type_name}{'&' if is_reference else ''} {decl.name}"
            elif target == 'c':
                return f"{type_name}{'*' if is_reference else ''} {decl.name}"
        else:

            name_output = decl.name
            indirection = decl.decl_type
            ptrs = 0
            while indirection.kind == TypeKind.ARRAY:
                if not arrays_are_pointers:
                    name_output = name_output[0:len(decl.name)] + f"[{indirection.elem_count}]" + (name_output[len(decl.name):] if len(name_output) > len(decl.name) else "")
                else:
                    ptrs += 1
                indirection = indirection.elem_type

            type_name = indirection.name
            if strip_uniform_semantic and indirection.kind == TypeKind.UNIFORM:
                type_name = indirection.underlying_type

            return f"{type_name}{ptrs*'*'} {name_output}"

    def resolve_struct_type(struct_decls, name):
        for decl in struct_decls:
            if decl.name == name:
                return decl
        return None

    def resolve_var_decl(var_stack, name):

        if name == "programIndex" or name == "programCount" or name == "taskIndex":
            v = VarDecl()
            v.name = name
            v.decl_type = resolve_type_ident("int")
            return v;

        for decl in var_stack:
            if decl.name == name:
                return decl
        return None

    def expect_token(token: Token, kind: TokenKind):
        if token.kind != kind:
            print(f"ERROR: Unexpected token. Expected {kind.name}, got {token.kind.name}.")
            print_token(token)
            raise ParseError("Parse error")
    def expect_token_punc(token: Token, kind: PunctuationKind):
        if token.kind != TokenKind.PUNCTUATION:
            print(f"ERROR: Unexpected token. Expected {kind.name}, got {token.kind.name}, {token.punctuation_kind}.")
            print_token(token)
            raise ParseError("Parse error")
        if token.punctuation_kind != kind:
            print(f"ERROR: Unexpected token. Expected {kind.name}, got {token.punctuation_kind.name}.")
            print_token(token)
            raise ParseError("Parse error")
    def expect_token_text(token: Token, kind: TokenKind, text: str):
        if token.kind != kind or token.text != text:
            print(f"ERROR: Unexpected token. Expected {text}, got {token.text}.")
            print_token(token)
            raise ParseError("Parse error")

    def consume():
        global token_index
        token = tokens[min(token_index, len(tokens)-1)]
        token_index += 1
        return token
    def peek(x = 0):
        global token_index
        return tokens[min(token_index+x, len(tokens)-1)]

    def emit_ispc(s: str, line_num):
        global emit_mask

        if not emit_mask['ispc']:
            return

        global output_ispc_src
        global output_ispc_line_num
        global output_ispc_brace_depth

        for c in s:
            if c == "}":
                output_ispc_brace_depth -= 1

        sformatted = s.replace("\n", "\n"+output_ispc_brace_depth*"\t")

        if line_num > output_ispc_line_num:
            output_ispc_src += '\n'*(line_num-output_ispc_line_num)+output_ispc_brace_depth*"\t"
            output_ispc_line_num += line_num-output_ispc_line_num

        output_ispc_src += sformatted

        for c in s:
            if c == "\n":
                output_ispc_line_num += 1
            if c == "{":
                output_ispc_brace_depth += 1

    def emit_c(s: str, line_num):
        global emit_mask

        if not emit_mask['c']:
            return

        global output_c_src
        global output_c_line_num
        global output_c_brace_depth

        for c in s:
            if c == "}":
                output_c_brace_depth -= 1

        sformatted = s.replace("\n", "\n"+output_c_brace_depth*"\t")

        if line_num > output_c_line_num:
            output_c_src += "\n"+output_c_brace_depth*"\t"
            output_c_line_num = line_num

        output_c_src += sformatted

        for c in s:
            if c == "\n":
                output_c_line_num += 1
            if c == "{":
                output_c_brace_depth += 1

    def emit_fsl(s: str, line_num):
        global emit_mask

        if not emit_mask['fsl']:
            return

        global output_fsl_src
        global output_fsl_line_num
        global output_fsl_brace_depth

        for c in s:
            if c == "}":
                output_fsl_brace_depth -= 1

        sformatted = s.replace("\n", "\n"+output_fsl_brace_depth*"\t")

        if line_num > output_fsl_line_num:
            output_fsl_src += "\n"+output_fsl_brace_depth*"\t"
            output_fsl_line_num = line_num

        output_fsl_src += sformatted

        for c in s:
            if c == "\n":
                output_fsl_line_num += 1
            if c == "{":
                output_fsl_brace_depth += 1

    def emit_multiple(s: str, line_num, targets):
        if "ispc" in targets:
            emit_ispc(s, line_num)
        if "c" in targets:
            emit_c(s, line_num)
        if "fsl" in targets:
            emit_fsl(s, line_num)

    def emit_all(s: str, line_num):
        emit_multiple(s, line_num, ['ispc', 'c', 'fsl'])

    def unemit_ispc(count):
        global output_ispc_src
        global output_ispc_line_num
        global output_ispc_brace_depth

        to_pos = len(output_ispc_src)-count
        unemitted_text = output_ispc_src[to_pos:]
        for c in unemitted_text:
            if c == "\n":
                output_ispc_line_num -= 1
            if c == "{":
                output_ispc_brace_depth -= 1
            if c == "}":
                output_ispc_brace_depth += 1
        output_ispc_src = output_ispc_src[0:to_pos]
    def unemit_c(count):
        global output_c_src
        global output_c_line_num
        global output_c_brace_depth

        to_pos = len(output_c_src)-count
        unemitted_text = output_c_src[to_pos:]
        for c in unemitted_text:
            if c == "\n":
                output_c_line_num -= 1
            if c == "{":
                output_c_brace_depth -= 1
            if c == "}":
                output_c_brace_depth += 1
        output_c_src = output_c_src[0:to_pos]
    def unemit_fsl(count):
        global output_fsl_src
        global output_fsl_line_num
        global output_fsl_brace_depth

        to_pos = len(output_fsl_src)-count
        unemitted_text = output_fsl_src[to_pos:]
        for c in unemitted_text:
            if c == "\n":
                output_fsl_line_num -= 1
            if c == "{":
                output_fsl_brace_depth -= 1
            if c == "}":
                output_fsl_brace_depth += 1
        output_fsl_src = output_fsl_src[0:to_pos]

    def unemit_multiple(count, targets):
        if "ispc" in targets:
            unemit_ispc(count)
        if "c" in targets:
            unemit_c(count)
        if "fsl" in targets:
            unemit_fsl(count)

    def unemit_all(count):
        unemit_multiple(count, ['ispc', 'c', 'fsl'])

    def resolve_type_ident(ident: str):
        t = Type()

        if ident == "float2":
            t.name = "float2"
            t.kind = TypeKind.COMPOSITE
            t.composite_count = 2
            t.underlying_type = "float"
        elif ident == "float3":
            t.name = "float3"
            t.kind = TypeKind.COMPOSITE
            t.composite_count = 3
            t.underlying_type = "float"
        elif ident == "float3Aligned":
            t.name = "float3Aligned"
            t.kind = TypeKind.COMPOSITE
            t.composite_count = 3
            t.underlying_type = "float"
        elif ident == "float4":
            t.name = "float4"
            t.kind = TypeKind.COMPOSITE
            t.composite_count = 4
            t.underlying_type = "float"
        elif ident == "quat":
            t.name = "quat"
            t.kind = TypeKind.COMPOSITE
            t.composite_count = 4
            t.underlying_type = "float"
        elif ident == "double2":
            t.name = "double2"
            t.kind = TypeKind.COMPOSITE
            t.composite_count = 2
            t.underlying_type = "double"
        elif ident == "double3":
            t.name = "double3"
            t.kind = TypeKind.COMPOSITE
            t.composite_count = 3
            t.underlying_type = "double"
        elif ident == "double3Aligned":
            t.name = "double3Aligned"
            t.kind = TypeKind.COMPOSITE
            t.composite_count = 3
            t.underlying_type = "double"
        elif ident == "double4":
            t.name = "double4"
            t.kind = TypeKind.COMPOSITE
            t.composite_count = 4
            t.underlying_type = "double"
        elif ident == "int2":
            t.name = "int2"
            t.kind = TypeKind.COMPOSITE
            t.composite_count = 2
            t.underlying_type = "int"
        elif ident == "int3":
            t.name = "int3"
            t.kind = TypeKind.COMPOSITE
            t.composite_count = 3
            t.underlying_type = "int"
        elif ident == "int3Aligned":
            t.name = "int3Aligned"
            t.kind = TypeKind.COMPOSITE
            t.composite_count = 3
            t.underlying_type = "int"
        elif ident == "int4":
            t.name = "int4"
            t.kind = TypeKind.COMPOSITE
            t.composite_count = 4
            t.underlying_type = "int"
        elif ident == "uint2":
            t.name = "uint2"
            t.kind = TypeKind.COMPOSITE
            t.composite_count = 2
            t.underlying_type = "uint"
        elif ident == "uint3":
            t.name = "uint3"
            t.kind = TypeKind.COMPOSITE
            t.composite_count = 3
            t.underlying_type = "uint"
        elif ident == "uint3Aligned":
            t.name = "uint3Aligned"
            t.kind = TypeKind.COMPOSITE
            t.composite_count = 3
            t.underlying_type = "uint"
        elif ident == "uint4":
            t.name = "uint4"
            t.kind = TypeKind.COMPOSITE
            t.composite_count = 4
            t.underlying_type = "uint"
        elif ident == "f2x2":
            t.name = ident
            t.kind = TypeKind.MATRIX
            t.col_count = 2
            t.row_count = 2
            t.underlying_type = "float2"
        elif ident == "f2x3":
            t.name = ident
            t.kind = TypeKind.MATRIX
            t.col_count = 2
            t.row_count = 3
            t.underlying_type = "float3"
        elif ident == "f2x4":
            t.name = ident
            t.kind = TypeKind.MATRIX
            t.col_count = 2
            t.row_count = 4
            t.underlying_type = "float4"
        elif ident == "f3x2":
            t.name = ident
            t.kind = TypeKind.MATRIX
            t.col_count = 3
            t.row_count = 2
            t.underlying_type = "float2"
        elif ident == "f3x3":
            t.name = ident
            t.kind = TypeKind.MATRIX
            t.col_count = 3
            t.row_count = 3
            t.underlying_type = "float3"
        elif ident == "f3x4":
            t.name = ident
            t.kind = TypeKind.MATRIX
            t.col_count = 3
            t.row_count = 4
            t.underlying_type = "float4"
        elif ident == "f4x2":
            t.name = ident
            t.kind = TypeKind.MATRIX
            t.col_count = 4
            t.row_count = 2
            t.underlying_type = "float2"
        elif ident == "f4x3":
            t.name = ident
            t.kind = TypeKind.MATRIX
            t.col_count = 4
            t.row_count = 3
            t.underlying_type = "float3"
        elif ident == "f4x4":
            t.name = ident
            t.kind = TypeKind.MATRIX
            t.col_count = 4
            t.row_count = 4
            t.underlying_type = "float4"
        elif ident == "d2x2":
            t.name = ident
            t.kind = TypeKind.MATRIX
            t.col_count = 2
            t.row_count = 2
            t.underlying_type = "double2"
        elif ident == "d2x3":
            t.name = ident
            t.kind = TypeKind.MATRIX
            t.col_count = 2
            t.row_count = 3
            t.underlying_type = "double3"
        elif ident == "d2x4":
            t.name = ident
            t.kind = TypeKind.MATRIX
            t.col_count = 2
            t.row_count = 4
            t.underlying_type = "double4"
        elif ident == "d3x2":
            t.name = ident
            t.kind = TypeKind.MATRIX
            t.col_count = 3
            t.row_count = 2
            t.underlying_type = "double2"
        elif ident == "d3x3":
            t.name = ident
            t.kind = TypeKind.MATRIX
            t.col_count = 3
            t.row_count = 3
            t.underlying_type = "double3"
        elif ident == "d3x4":
            t.name = ident
            t.kind = TypeKind.MATRIX
            t.col_count = 3
            t.row_count = 4
            t.underlying_type = "double4"
        elif ident == "d4x2":
            t.name = ident
            t.kind = TypeKind.MATRIX
            t.col_count = 4
            t.row_count = 2
            t.underlying_type = "double2"
        elif ident == "d4x3":
            t.name = ident
            t.kind = TypeKind.MATRIX
            t.col_count = 4
            t.row_count = 3
            t.underlying_type = "double3"
        elif ident == "d4x4":
            t.name = ident
            t.kind = TypeKind.MATRIX
            t.col_count = 4
            t.row_count = 4
            t.underlying_type = "double4"
        elif ident == "TFAABB":
            t.name = "TFAABB"
            t.kind = TypeKind.STRUCT

            memberMin = VarDecl()
            memberMin.name = "min"
            memberMin.is_ref = False
            memberMin.decl_type = resolve_type_ident("float3Aligned")
            memberMax = VarDecl()
            memberMax.name = "max"
            memberMax.is_ref = False
            memberMax.decl_type = resolve_type_ident("float3Aligned")

            t.members = [memberMin, memberMax]
        else:
            struct_type = resolve_struct_type(struct_decls, ident)
            if struct_type != None:
                return struct_type

            # Just assume a base type if no other type could be parsed
            # (And let ISPC compiler deal with resolving this type)
            t.kind = TypeKind.BASE
            t.name = ident

        return t

    def parse_type(tokens):
        if len(tokens) == 4:
            t = Type()
            # Could be for example Tex2D(float4), etc
            if tokens[0].text in ["Tex1D", "RWTex1D", "WTex1D"]:
                t.kind = TypeKind.TEX1D
            elif tokens[0].text in ["Tex2D", "RWTex2D", "WTex2D"]:
                t.kind = TypeKind.TEX2D
            elif tokens[0].text in ["Tex3D", "RWTex3D", "WTex3D"]:
                t.kind = TypeKind.TEX3D
            elif tokens[0].text in ["Buffer", "RWBuffer", "WBuffer"]:
                t.kind = TypeKind.BUFFER
            elif tokens[0].text == "SamplerState":
                t.kind = TypeKind.SAMPLER
            elif tokens[0].text == "Uniform":
                t.kind = TypeKind.UNIFORM
            else:
                print("ERROR: Could not parse type")
                print_token(tokens[0])
                raise ParseError("Type Parse Error")

            expect_token_punc(tokens[1], PunctuationKind.LEFT_PAREN)
            underlying_tok = tokens[2]
            expect_token(underlying_tok, TokenKind.IDENTIFIER)
            expect_token_punc(tokens[3], PunctuationKind.RIGHT_PAREN)

            t.underlying_type = underlying_tok.text
            t.name = f"{tokens[0].text}({underlying_tok.text})"

            return t
        elif len(tokens) == 1:

            return resolve_type_ident(tokens[0].text)

        else:
            print("ERROR: Could not parse type")
            print_token(tokens[0])
            raise ParseError("Type Parse Error")

    def parse_var_decl(first: Token):
        decl = VarDecl()

        is_enclosed = False
        if first.text == '(':
            is_enclosed = True
            first = consume()

        while first.text == "const" or first.text == "static":
            first = consume()

        type_tokens = [first]
        if peek().punctuation_kind == PunctuationKind.LEFT_PAREN:
            type_tokens += [ consume(), consume(), consume() ]

        if is_enclosed:
            expect_token_punc(consume(), PunctuationKind.RIGHT_PAREN)

        decl.decl_type = parse_type(type_tokens)
        expect_token(peek(), TokenKind.IDENTIFIER)
        decl.name = consume().text

        next_tok = peek()

        if next_tok.punctuation_kind == PunctuationKind.LEFT_BRACKET:
            while next_tok.punctuation_kind == PunctuationKind.LEFT_BRACKET:
                consume() # [
                next_tok = consume()
                count = 0

                if (next_tok.punctuation_kind != PunctuationKind.RIGHT_BRACKET):
                    expect_token(next_tok, TokenKind.NUMBER_LITERAL)
                    count = int(float(next_tok.text))
                    next_tok = consume()

                expect_token_punc(next_tok, PunctuationKind.RIGHT_BRACKET)

                array_type = Type()
                array_type.kind = TypeKind.ARRAY
                array_type.elem_count = count
                array_type.name = f"{decl.decl_type.name}[{array_type.elem_count}]"
                array_type.elem_type = decl.decl_type
                decl.decl_type = array_type

                next_tok = peek()

        return decl

    def can_be_function_decl(first, result_sig_tokens: dict):

        # We look for
        # A) TYPE IDENT (
        # B) inline TYPE IDENT (

        # And then output the modifier, type and name tokens into result_sig_tokens in the format
        # {
        #     'modifier': Token,
        #     'type': [Token's...],
        #     'name': Token,
        # }

        # Single-identifier types

        # modifier IDENT IDENT (
        if first.text in ["inline", "cexport", "transpile", "task"]:
            if peek(0).kind == TokenKind.IDENTIFIER and peek(1).kind == TokenKind.IDENTIFIER and peek(2).punctuation_kind == PunctuationKind.LEFT_PAREN:
                result_sig_tokens['modifier'] = first
                result_sig_tokens['type'] = [peek(0)]
                result_sig_tokens['name'] = peek(1)
                return True

        # modifier Type(XX) IDENT (
        if first.text in ["inline", "cexport", "transpile", "task"]:
            if peek(0).kind == TokenKind.IDENTIFIER and peek(1).punctuation_kind == PunctuationKind.LEFT_PAREN and peek(2).kind == TokenKind.IDENTIFIER and peek(3).punctuation_kind == PunctuationKind.RIGHT_PAREN:
                if peek(4).kind == TokenKind.IDENTIFIER and peek(5).punctuation_kind == PunctuationKind.LEFT_PAREN:
                    result_sig_tokens['modifier'] = first
                    result_sig_tokens['type'] = [peek(0), peek(1), peek(2), peek(3)]
                    result_sig_tokens['name'] = peek(4)
                    return True

        # Type(XX) IDENT (
        if first.kind == TokenKind.IDENTIFIER and peek(0).punctuation_kind == PunctuationKind.LEFT_PAREN and peek(1).kind == TokenKind.IDENTIFIER and peek(2).punctuation_kind == PunctuationKind.RIGHT_PAREN:
            if peek(3).kind == TokenKind.IDENTIFIER and peek(4).punctuation_kind == PunctuationKind.LEFT_PAREN:
                result_sig_tokens['modifier'] = None
                result_sig_tokens['type'] = [first, peek(0), peek(1), peek(2)]
                result_sig_tokens['name'] = peek(3)
                return True

        # IDENT IDENT (
        if first.kind == TokenKind.IDENTIFIER and peek(0).kind == TokenKind.IDENTIFIER and peek(1).punctuation_kind == PunctuationKind.LEFT_PAREN:
            result_sig_tokens['modifier'] = None
            result_sig_tokens['type'] = [first]
            result_sig_tokens['name'] = peek()
            return True

        return False

    def can_be_var_decl_type_ident(first):

        next_tok = first;

        if next_tok.text in ["return", "else"]:
            return False

        if first.text in ["in", "out", "inout"]:
            next_tok = peek();

        if next_tok.text in ["Tex1D", "Tex2D", "Tex3D", "Buffer", "RWBuffer", "WBuffer", "Uniform"]:
            return peek(0).punctuation_kind == PunctuationKind.LEFT_PAREN and peek(1).kind == TokenKind.IDENTIFIER and peek(2).punctuation_kind == PunctuationKind.RIGHT_PAREN

        allowed_puncs = [PunctuationKind.SEMICOLON, PunctuationKind.EQUALS, PunctuationKind.COMMA, PunctuationKind.RIGHT_PAREN, PunctuationKind.LEFT_BRACKET]

        if next_tok.kind == TokenKind.IDENTIFIER and peek(0).kind == TokenKind.IDENTIFIER:
            if peek(1).punctuation_kind in allowed_puncs:
                return True
            if peek(1).kind == TokenKind.IDENTIFIER and "const" in [next_tok.text, peek(0).text, peek(1).text]:
                if peek(2).punctuation_kind in allowed_puncs:
                    return True

        return False

    def can_be_expr(first):
        if first.kind == TokenKind.NUMBER_LITERAL:
            return True

        if first.kind == TokenKind.IDENTIFIER:
            return peek().kind != TokenKind.IDENTIFIER

        return False

    def process_one_expr(var_stack, first: Token):
        negate = False
        bit_not = False
        logical_not = False
        next_tok = first

        type = None
        decl = None

        # We need to reorder expressions here sometimes which gets messy
        global output_ispc_src
        global output_c_src
        global output_fsl_src
        expr_start_ispc_pos = len(output_ispc_src)
        expr_start_c_pos = len(output_c_src)
        expr_start_fsl_pos = len(output_fsl_src)

        if next_tok.punctuation_kind == PunctuationKind.MINUS:
            negate = True
            emit_fsl("-", next_tok.line_number)
            next_tok = consume()

        if next_tok.punctuation_kind == PunctuationKind.TILDE:
            bit_not = True
            emit_fsl("~", next_tok.line_number)
            next_tok = consume()

        if next_tok.punctuation_kind == PunctuationKind.EXCLAMATION:
            logical_not = True
            emit_fsl("!", next_tok.line_number)
            next_tok = consume()

        if next_tok.kind == TokenKind.NUMBER_LITERAL or next_tok.kind == TokenKind.STRING_LITERAL:
            # Literal
            if negate:
                emit_multiple("-", first.line_number, ['ispc', 'c'])
            if bit_not:
                emit_multiple("~", first.line_number, ['ispc', 'c'])
            if logical_not:
                emit_multiple("!", first.line_number, ['ispc', 'c'])
            emit_all(next_tok.text, next_tok.line_number)
        elif next_tok.kind == TokenKind.IDENTIFIER:

            if peek().punctuation_kind == PunctuationKind.LEFT_PAREN:
                # Function call

                if negate:
                    emit_multiple("-", first.line_number, ['ispc', 'c'])
                if bit_not:
                    emit_multiple("~", first.line_number, ['ispc', 'c'])
                if logical_not:
                    emit_multiple("!", first.line_number, ['ispc', 'c'])

                lparen = consume() # (

                if next_tok.text in composite_types:
                    emit_multiple("make_" + resolve_type_ident(next_tok.text).name , next_tok.line_number, ['ispc', 'c'])
                    emit_fsl(resolve_type_ident(next_tok.text).name , next_tok.line_number)
                elif next_tok.text == "float" or next_tok.text == "double" or next_tok.text == "int" or next_tok.text == "uint":
                    emit_ispc("make_" + resolve_type_ident(next_tok.text).name + "1", next_tok.line_number)
                    emit_c(f"({next_tok.text})", next_tok.line_number)
                    emit_fsl(next_tok.text, next_tok.line_number)
                else:
                    emit_all(next_tok.text, next_tok.line_number)

                emit_all("(", lparen.line_number)

                if next_tok.text == "frexp":
                    # We need to use pointer semantics for frexp in C and ISPC

                    process_expr(var_stack, consume(), [PunctuationKind.COMMA, PunctuationKind.RIGHT_PAREN])

                    next_tok = consume()
                    expect_token_punc(next_tok, PunctuationKind.COMMA) # Expect 2 args

                    emit_all(", ", next_tok.line_number)

                    # For 2nd arg, take adress of
                    emit_multiple("&(", next_tok.line_number, ['ispc', 'c'])

                    process_expr(var_stack, consume(), [PunctuationKind.COMMA, PunctuationKind.RIGHT_PAREN])

                    next_tok = consume()
                    expect_token_punc(next_tok, PunctuationKind.RIGHT_PAREN)
                    emit_multiple(")", next_tok.line_number, ['ispc', 'c'])
                else:
                    next_tok = consume()
                    if next_tok.punctuation_kind != PunctuationKind.RIGHT_PAREN:
                        process_expr(var_stack, next_tok, [PunctuationKind.RIGHT_PAREN])
                        next_tok = consume()

                expect_token_punc(next_tok, PunctuationKind.RIGHT_PAREN)
                emit_all(")", next_tok.line_number);
            else:
                # Var
                decl = resolve_var_decl(var_stack, next_tok.text)

                if decl == None:
                    print("ERROR: Undefined identifier used in expression")
                    print_token(next_tok)
                    raise ParseError("Expression Parse Error")

                # If there was a prefix operator, it is already emitted in fsl
                emit_fsl(decl.name, next_tok.line_number)

                if (negate or bit_not or logical_not) and decl.decl_type.kind == TypeKind.COMPOSITE:
                    comps = ["x", "y", "z", "w"]

                    # If a vector is being dereferenced, then we dont need to reconstruct a vector
                    # to then dereference. ISPC also has a bug where it does not allow dereferencing
                    # return value.
                    if peek().punctuation_kind == PunctuationKind.DOT and peek(1).text in comps:
                        consume() # .
                        comp = consume().text

                        emit_multiple(f"-{decl.name}.{comp}", first.line_number, ['ispc', 'c'])
                        emit_fsl(f".{comp}", first.line_number);

                        type = resolve_type_ident(decl.decl_type.underlying_type)
                    elif peek(0).punctuation_kind == PunctuationKind.LEFT_BRACKET and peek(1).kind == TokenKind.NUMBER_LITERAL and peek(2).punctuation_kind == PunctuationKind.RIGHT_BRACKET:

                        consume() # [
                        num = count = int(float(consume().text))
                        consume() # ]

                        if num >= decl.decl_type.composite_count or num < 0:
                            print(f"ERROR: Index {num} is out of range for vector type {decl.decl_type.name}")
                            print_token(token)
                            raise ParseError("Parse error")

                        comp = comps[num]

                        emit_multiple(f"-{decl.name}.{comp}", first.line_number, ['ispc', 'c'])
                        emit_fsl(f".{comp}", first.line_number);

                        type = resolve_type_ident(decl.decl_type.underlying_type)

                    else:
                        emit_multiple(f"make_{decl.decl_type.name}(", first.line_number, ['ispc', 'c'])

                        for i in range(0, decl.decl_type.composite_count):
                            if negate:
                                emit_multiple("-", first.line_number, ['ispc', 'c'])
                            if bit_not:
                                emit_multiple("~", first.line_number, ['ispc', 'c'])
                            if logical_not:
                                emit_multiple("!", first.line_number, ['ispc', 'c'])

                            emit_ispc(f"{decl.name}.{comps[i]}", next_tok.line_number)
                            emit_c(f"({'*' if decl.is_ref else ''}{decl.name}).{comps[i]}", next_tok.line_number)
                            if i != decl.decl_type.composite_count-1:
                                emit_multiple(f", ", next_tok.line_number, ['ispc', 'c'])

                        emit_multiple(")", next_tok.line_number, ['ispc', 'c'])

                        type = decl.decl_type
                else:
                    if negate:
                        emit_multiple("-", first.line_number, ['ispc', 'c'])
                    if bit_not:
                        emit_multiple("~", first.line_number, ['ispc', 'c'])
                    if logical_not:
                        emit_multiple("!", first.line_number, ['ispc', 'c'])
                    emit_ispc(decl.name, next_tok.line_number)
                    emit_c(f"({'*' if decl.is_ref else ''}{decl.name})", next_tok.line_number)

                    type = decl.decl_type

        elif next_tok.punctuation_kind == PunctuationKind.LEFT_PAREN:
            if negate:
                emit_multiple("-", first.line_number, ['ispc', 'c'])
            if bit_not:
                emit_multiple("~", first.line_number, ['ispc', 'c'])
            if logical_not:
                emit_multiple("!", first.line_number, ['ispc', 'c'])
            emit_all("(", next_tok.line_number)
            process_expr(var_stack, consume(), [PunctuationKind.RIGHT_PAREN])
            next_tok = consume()
            expect_token_punc(next_tok, PunctuationKind.RIGHT_PAREN)
            emit_all(")", next_tok.line_number)
        elif next_tok.punctuation_kind == PunctuationKind.LEFT_BRACE:
            if negate:
                emit_multiple("-", first.line_number, ['ispc', 'c'])
            if bit_not:
                emit_multiple("~", first.line_number, ['ispc', 'c'])
            if logical_not:
                emit_multiple("!", first.line_number, ['ispc', 'c'])
            emit_all("{", next_tok.line_number)
            process_expr(var_stack, consume(), [PunctuationKind.RIGHT_BRACE])
            next_tok = consume()
            expect_token_punc(next_tok, PunctuationKind.RIGHT_BRACE)
            emit_all("}", next_tok.line_number)
        else:
            print("ERROR: Expected an expression, but this was not recognized as such")
            print_token(next_tok)
            raise ParseError("Expression Parse Error")

        # Deref/Subscript chain
        if peek().punctuation_kind == PunctuationKind.LEFT_BRACKET or peek().punctuation_kind == PunctuationKind.DOT:
            if type == None:
                # TODO function type info so we can deref function return values. Note that dereferncing function
                # return values isn't even supported in ISPC as of 1.26.0.
                print("ERROR: Dereferencing this kind of expression is current unsupported in ISPC Shaders. You can work around this by assigning the expression to a variable first, and then dereferencing the variable.")
                print_token(peek())
                raise ParseError("Unimplemented")

            deref_types = [type]
            first_expr_text = first_pass[first.start_pos:peek().start_pos];
            # if type.kind == TypeKind.STRUCT:
            # emit_all(first_expr_text, next_tok.line_number)
            last_expr_text = first_expr_text

            next_tok = peek()
            while peek().punctuation_kind == PunctuationKind.LEFT_BRACKET or peek().punctuation_kind == PunctuationKind.DOT:

                last_type: Type = deref_types[len(deref_types)-1]
                if last_type.kind == TypeKind.UNIFORM:
                    last_type = resolve_type_ident(last_type.underlying_type)

                if peek().punctuation_kind == PunctuationKind.LEFT_BRACKET:
                    lbracket = consume()

                    if last_type.kind == TypeKind.COMPOSITE:

                        next_tok = consume()

                        if next_tok.kind == TokenKind.NUMBER_LITERAL:
                            if next_tok.literal > last_type.composite_count or next_tok.literal < 0:
                                print(f"ERROR: Vector index {next_tok.literal} is out of the vector's range of {last_type.composite_count} ({last_type.name})")
                                print_token(next_tok)
                                raise ParseError("Vector Subscript Parse Error")

                            if next_tok.literal == 0:
                                emit_all(f".x", next_tok.line_number)
                            elif next_tok.literal == 1:
                                emit_all(f".y", next_tok.line_number)
                            elif next_tok.literal == 2:
                                emit_all(f".z", next_tok.line_number)
                            elif next_tok.literal == 3:
                                emit_all(f".w", next_tok.line_number)

                            next_tok = consume()
                            expect_token_punc(next_tok, PunctuationKind.RIGHT_BRACKET)
                        else:
                            # in ISPC: a[x] -> (*__subscript(&a, x))
                            # We need to inject __subscript( at the start of the expression
                            last_expr_text = output_ispc_src[expr_start_ispc_pos:]
                            unemit_ispc(len(output_ispc_src) - expr_start_ispc_pos)
                            emit_ispc(f"(*__subscript(&({last_expr_text}), ", lbracket.line_number)

                            # Subscripting vector should be fine in all shading languages
                            emit_fsl("[", lbracket.line_number)

                            # in C we do a pointer cast and index into that
                            last_expr_text = output_c_src[expr_start_c_pos:]
                            unemit_c(len(output_c_src) - expr_start_c_pos)
                            emit_c(f"(({last_type.underlying_type}*)&{last_expr_text})[", lbracket.line_number)

                            process_expr(var_stack, next_tok, [PunctuationKind.RIGHT_BRACKET])
                            next_tok = consume()
                            expect_token_punc(next_tok, PunctuationKind.RIGHT_BRACKET)

                            emit_ispc("))", next_tok.line_number)

                            emit_fsl("]", next_tok.line_number)
                            emit_c("]", next_tok.line_number)

                    elif last_type.kind == TypeKind.MATRIX:

                        last_expr_text_fsl = output_fsl_src[expr_start_fsl_pos:]
                        last_expr_text_ispc = output_ispc_src[expr_start_ispc_pos:]
                        last_expr_text_c = output_c_src[expr_start_c_pos:]

                        unemit_fsl(len(output_fsl_src) - expr_start_fsl_pos)

                        fsl_start_pos = len(output_fsl_src)

                        emit_fsl(f"getCol({last_expr_text_fsl}, ", lbracket.line_number)

                        emit_multiple(".v[", lbracket.line_number, ['ispc', 'c'])

                        fsl_subscript_pos_x = len(output_fsl_src)
                        ispc_subscript_pos_x = len(output_ispc_src)
                        c_subscript_pos_x = len(output_c_src)
                        fsl_subscript_pos_y = 0
                        ispc_subscript_pos_y = 0
                        c_subscript_pos_y = 0

                        process_expr(var_stack, consume(), [PunctuationKind.RIGHT_BRACKET])
                        next_tok = consume()
                        expect_token_punc(next_tok, PunctuationKind.RIGHT_BRACKET)

                        subscript_expr_text_fsl_x = output_fsl_src[fsl_subscript_pos_x:]
                        subscript_expr_text_ispc_x = output_ispc_src[ispc_subscript_pos_x:]
                        subscript_expr_text_c_x = output_c_src[c_subscript_pos_x:]

                        emit_multiple("]", next_tok.line_number, ['ispc', 'c'])

                        is_elem = False
                        if peek().text == '[' or (peek().text == "." and peek(1).kind == TokenKind.IDENTIFIER and len(peek(1).text) == 1):
                            unemit_fsl(len(output_fsl_src) - fsl_start_pos)
                            unemit_ispc(len(output_ispc_src) - expr_start_ispc_pos)
                            unemit_c(len(output_c_src) - expr_start_c_pos)

                            vec_type  = resolve_type_ident(last_type.underlying_type)
                            elem_type = resolve_type_ident(vec_type.underlying_type)

                            is_elem = True
                            emit_fsl(f"getElem({last_expr_text_fsl}, {subscript_expr_text_fsl_x}, ",   lbracket.line_number)
                            emit_ispc(f"(*__subscript(&({last_expr_text_ispc}.v[{subscript_expr_text_ispc_x}]), ", lbracket.line_number)
                            emit_c(f"(({elem_type.name}*)&({last_expr_text_c}.v[{subscript_expr_text_c_x}]))[",   lbracket.line_number)

                            fsl_subscript_pos_y  = len(output_fsl_src)
                            ispc_subscript_pos_y = len(output_ispc_src)
                            c_subscript_pos_y    = len(output_c_src)

                            accessor = consume()  # '[' or '.'

                            if accessor.text == '[':
                                process_expr(var_stack, consume(), [PunctuationKind.RIGHT_BRACKET])
                                next_tok = consume()
                                expect_token_punc(next_tok, PunctuationKind.RIGHT_BRACKET)
                            else: # .
                                member_tok = consume()

                                comps = ['x', 'y', 'z', 'w']
                                comp_index = -1
                                for i in range(0, len(comps), 1):
                                    if member_tok.text == comps[i]:
                                        comp_index = i
                                        break

                                if comp_index == -1:
                                    print("ERROR: Expected a vector member identifier (x, y, z, w)")
                                    print_token(member_tok)
                                    raise ParseError("Expression Parse Error")

                                emit_all(str(comp_index), member_tok.line_number)

                        if peek().text == '=':
                            global close_paren_after_assign
                            if not is_elem:
                                unemit_fsl(len(output_fsl_src) - fsl_start_pos)
                                emit_fsl(f"setCol({last_expr_text_fsl}, ", lbracket.line_number)
                                close_paren_after_assign += 1
                            else:
                                subscript_expr_text_y = output_fsl_src[fsl_subscript_pos_y:]
                                unemit_fsl(len(output_fsl_src) - fsl_start_pos)
                                emit_fsl(f"setElem({last_expr_text_fsl}, {subscript_expr_text_fsl_x}, {subscript_expr_text_y}, ", lbracket.line_number)
                                close_paren_after_assign += 1

                            if is_elem:
                                emit_ispc("))", lbracket.line_number)
                                emit_c("]", lbracket.line_number)

                            consume()  # '='
                            emit_multiple("=", lbracket.line_number, ['ispc', 'c'])

                            process_expr(var_stack, consume(), [PunctuationKind.SEMICOLON])

                            if not is_elem:
                                emit_fsl(f", {subscript_expr_text_fsl_x}", lbracket.line_number)

                        elif is_elem:
                            emit_ispc("))", lbracket.line_number)
                            emit_c("]", lbracket.line_number)

                        emit_fsl(")", lbracket.line_number)

                        deref_types += [resolve_type_ident(last_type.underlying_type)]
                    elif last_type.kind == TypeKind.ARRAY:
                        emit_all("[", lbracket.line_number)
                        process_expr(var_stack, consume(), [PunctuationKind.RIGHT_BRACKET])
                        next_tok = consume()
                        expect_token_punc(next_tok, PunctuationKind.RIGHT_BRACKET)
                        emit_all("]", next_tok.line_number)
                        deref_types += [last_type.elem_type]
                    else:
                        print(f"ERROR: Trying to subscript '{last_type.name}' which is not of a vector/matrix type (it is a {last_type.kind})")
                        print_token(lbracket)
                        raise ParseError("Expression Parse Error")

                    next_tok = peek()
                else:
                    dot_tok = consume() # .
                    next_ident = consume()
                    expect_token(next_ident, TokenKind.IDENTIFIER)

                    if last_type.kind == TypeKind.STRUCT:
                        next_decl: VarDecl = None
                        match = False
                        for member in last_type.members:
                            if member.name == next_ident.text:
                                next_decl = member
                                deref_types += [next_decl.decl_type]
                                emit_all(f".{next_decl.name}", next_ident.line_number)
                                last_expr_text = next_decl.name
                                match = True
                                break

                        if match:
                            continue
                        print(f"ERROR: No such member '{next_ident.text}' in variable '{last_expr_text}' of type '{last_type.name}'")
                        print_token(next_ident)
                        raise ParseError("Expression Parse Error")
                    elif last_type.kind == TypeKind.COMPOSITE:
                        # Composite deref

                        emit_fsl(f".{next_ident.text}", next_ident.line_number)

                        swizzle_char_sets = [["x", "y", "z", "w"], ["r", "g", "b", "a"], ["s", "t", "p", "q"]]

                        swizzle_set_index = -1

                        # Find out if all letters are in a swizzle set
                        for c in next_ident.text:
                            found_in_set_index = None
                            for i, sw_set in enumerate(swizzle_char_sets):
                                if c in sw_set:
                                    found_in_set_index = i
                                    break

                            if found_in_set_index is None:
                                swizzle_set_index = -1
                                break

                            if swizzle_set_index == -1:
                                swizzle_set_index = found_in_set_index
                            elif swizzle_set_index != found_in_set_index:
                                swizzle_set_index = -1
                                break

                        # All letters matched to a single swizzle set
                        if swizzle_set_index != -1:

                            swizzle_set = swizzle_char_sets[swizzle_set_index]

                            indices = []
                            for i, c in enumerate(next_ident.text):
                                index = -1
                                for j, sw in enumerate(swizzle_set):
                                    if c == sw:
                                        index = j;
                                        break;
                                indices += [index]

                            if len(next_ident.text) == 1:
                                emit_multiple(f".{swizzle_char_sets[0][indices[0]]}", next_ident.line_number, ['ispc', 'c'])
                                deref_types += [resolve_type_ident(last_type.underlying_type)]
                            else:
                                # We need to inject make_xxx( at the start of the expression

                                last_expr_text_ispc = output_ispc_src[expr_start_ispc_pos:]
                                unemit_ispc(len(output_ispc_src)-expr_start_ispc_pos)

                                last_expr_text_c = output_c_src[expr_start_c_pos:]
                                unemit_c(len(output_c_src)-expr_start_c_pos)

                                emit_multiple(f"make_{last_type.underlying_type}{len(next_ident.text)}(", next_ident.line_number, ['ispc', 'c'])
                                is_first = True
                                for i in indices:
                                    if not is_first:
                                        emit_multiple(", ", next_ident.line_number, ['ispc', 'c'])
                                    emit_ispc(f"{last_expr_text_ispc}.{swizzle_char_sets[0][i]} ", next_ident.line_number)
                                    emit_c(f"{last_expr_text_c}.{swizzle_char_sets[0][i]} ", next_ident.line_number)
                                    is_first = False
                                emit_multiple(")", next_ident.line_number, ['ispc', 'c'])

                                if peek().punctuation_kind == PunctuationKind.EQUALS:
                                    print("ERROR: Swizzle/subvector are R-values in CPU shaders; they cannot be assigned to. ISPC does not have built-in swizzle.")
                                    print_token(next_ident)
                                    raise ParseError("Parse Error")

                                deref_types += [resolve_type_ident(f"{last_type.underlying_type}{len(next_ident.text)}")]
                        else:
                            print("ERROR: Invalid dereference/swizzle of composite type '{last_type.name}'")
                            print_token(next_ident)
                            raise ParseError("Parse Error")

                    elif last_type.kind == TypeKind.BUFFER:
                        next_decl: VarDecl = None
                        match = False
                        struct_type = resolve_type_ident(last_type.underlying_type);
                        for member in struct_type.members:
                            if member.name == next_ident.text:
                                next_decl = member
                                deref_types += [next_decl.decl_type]
                                emit_all(f"->{next_decl.name}", next_ident.line_number)
                                last_expr_text = next_decl.name
                                match = True
                                break

                        if match:
                            continue
                        print(f"ERROR: No such member '{next_ident.text}' in variable '{last_expr_text}' of type '{struct_type.name}'")
                        print_token(next_ident)
                        raise ParseError("Expression Parse Error")
                    else:
                        print(f"ERROR: Trying to dereference '{last_type.name}' which is not of a composite or struct type")
                        print_token(dot_tok)
                        raise ParseError("Expression Parse Error")

    def process_expr(var_stack, first: Token, end_tokens):
        next_tok = first
        do_close_paren = False

        last_op = None

        while next_tok.kind != TokenKind.EOF and next_tok.punctuation_kind not in end_tokens:

            if next_tok.punctuation_kind == PunctuationKind.LEFT_PAREN:
                if peek(0).kind == TokenKind.IDENTIFIER and peek(1).punctuation_kind == PunctuationKind.RIGHT_PAREN and peek(2).kind != TokenKind.PUNCTUATION:
                    # It's a cast, just emit it
                    emit_all(f"({peek(0).text})", peek(0).line_number)
                    next_tok = consume()
                    next_tok = consume()
                    next_tok = consume()
                    process_one_expr(var_stack, next_tok)
                else:
                    emit_all("(", next_tok.line_number)
                    process_expr(var_stack, consume(), [PunctuationKind.RIGHT_PAREN])
                    next_tok = consume()
                    expect_token_punc(next_tok, PunctuationKind.RIGHT_PAREN)
                    emit_all(")", next_tok.line_number)
            else:
                process_one_expr(var_stack, next_tok)

            # Special case when matrix[] in fsl we emit setCol(matrix, expr) but when parsing
            # the subscript we do not know what expression comes after
            # if last_op and last_op == "=" and close_paren_after_assign:
            # emit_fsl(")booa", next_tok.line_number)

            next_tok = peek()

            if next_tok.punctuation_kind not in end_tokens:
                # expression followed by punctuation, we will assume operation
                consume()
                expect_token(next_tok, TokenKind.PUNCTUATION)

                last_op = next_tok.text

                emit_all(f" {next_tok.text} ", next_tok.line_number)
                next_tok = consume()

        if next_tok.punctuation_kind not in end_tokens:
            print("ERROR: Unexpected token")
            print_token(next_tok)
            raise ParseError("Parse Error")

        if do_close_paren:
            emit_all(")", next_tok.line_number);

    def process_block_item(var_stack, first):
        if first.text == "INIT_MAIN":
            expect_token_punc(consume(), PunctuationKind.SEMICOLON)
            emit_all("INIT_MAIN;", first.line_number)
            first = consume()

        if can_be_var_decl_type_ident(first):

            next_tok = first

            decl = parse_var_decl(next_tok)

            var_stack += [decl]

            emit_ispc(format_decl_string(decl, False, 'ispc'), first.line_number)
            emit_c(format_decl_string(decl, False, 'c'), first.line_number)
            emit_fsl(format_decl_string(decl, False, 'fsl'), first.line_number)

            next_tok = consume()

            if next_tok.punctuation_kind == PunctuationKind.EQUALS:
                emit_all(" = ", next_tok.line_number)
                next_tok = consume();
                process_expr(var_stack, next_tok, [PunctuationKind.SEMICOLON])
                next_tok = consume()
            expect_token_punc(next_tok, PunctuationKind.SEMICOLON);
            emit_all(";", next_tok.line_number)
        elif first.text in ["if", "while", "else", "for"]:

            emit_all(first.text + " ", first.line_number)
            next_tok = consume()

            is_else_if = False

            if first.text == "else" and next_tok.text == "if":
                is_else_if = True
                emit_all(next_tok.text, next_tok.line_number)
                next_tok = consume()

            if first.text == "for":
                expect_token_punc(next_tok, PunctuationKind.LEFT_PAREN)
                emit_all("(", next_tok.line_number)
                decl = parse_var_decl(consume())

                emit_ispc(format_decl_string(decl, decl.is_ref, 'ispc'), first.line_number)
                emit_c(format_decl_string(decl, decl.is_ref, 'c'), first.line_number)
                emit_fsl(format_decl_string(decl, decl.is_ref, 'fsl'), first.line_number)

                var_stack += [decl]
                next_tok = consume()
                if next_tok.text == "=":
                    emit_all(" = ", next_tok.line_number)
                    process_expr(var_stack, consume(), [PunctuationKind.SEMICOLON])
                    next_tok = consume()
                expect_token_punc(next_tok, PunctuationKind.SEMICOLON)
                emit_all("; ", next_tok.line_number)
                process_expr(var_stack, consume(), [PunctuationKind.RIGHT_PAREN])
                expect_token_punc(consume(), PunctuationKind.RIGHT_PAREN)
                next_tok = consume()
                emit_all(") ", first.line_number)
            elif first.text != "else" or is_else_if:
                expect_token_punc(next_tok, PunctuationKind.LEFT_PAREN)
                emit_all("(", next_tok.line_number)
                next_tok = consume();
                process_expr(var_stack, next_tok, [PunctuationKind.RIGHT_PAREN])
                expect_token_punc(consume(), PunctuationKind.RIGHT_PAREN)
                next_tok = consume()
                emit_all(") ", first.line_number)

            process_block([v for v in var_stack], next_tok)

        elif first.text == "return":
            emit_all("return ", first.line_number)
            if peek().punctuation_kind != PunctuationKind.SEMICOLON:
                process_expr(var_stack, consume(), [PunctuationKind.SEMICOLON])
            expect_token_punc(consume(), PunctuationKind.SEMICOLON)
            emit_all(";", first.line_number)
        elif first.text == "foreach":

            if emit_mask['fsl'] or emit_mask['c']:
                print("ERROR: 'foreach' is only available in CPU Shaders, and cannot be transpiled to C or FSL.")
                print_token(first)
                raise ParseError("Block Parse Error")

            emit_ispc("foreach (", first.line_number)
            expect_token_punc(consume(), PunctuationKind.LEFT_PAREN)

            # ISPC style widening execution to multiple lanes
            # foreach (x = y ... z) {}
            # where x will be a int variable, and y & z are lane start and end

            expect_token(peek(), TokenKind.IDENTIFIER)

            while True:
                v = VarDecl()
                v.name = peek().text
                v.decl_type = resolve_type_ident("int")
                var_stack += [v]

                process_expr(var_stack, consume(), [PunctuationKind.TRIPLE_DOT])

                next_tok = consume()
                expect_token_punc(next_tok, PunctuationKind.TRIPLE_DOT)
                emit_ispc(" ... ", next_tok.line_number)

                process_expr(var_stack, consume(), [PunctuationKind.RIGHT_PAREN, PunctuationKind.COMMA])

                if peek().punctuation_kind == PunctuationKind.RIGHT_PAREN:
                    break

                next_tok = consume()
                expect_token_punc(next_tok, PunctuationKind.COMMA)
                emit_ispc(", ", next_tok.line_number)

            next_tok = consume()
            expect_token_punc(next_tok, PunctuationKind.RIGHT_PAREN)

            emit_ispc(")", next_tok.line_number)

            next_tok = consume()

            process_block([v for v in var_stack], next_tok)

        elif first.text == "launch":
            if emit_mask['fsl'] or emit_mask['c']:
                print("ERROR: 'launch' is only available in CPU Shaders, and cannot be transpiled to C or FSL.")
                print_token(first)
                raise ParseError("Block Parse Error")

            emit_ispc("launch ", first.line_number)
            if peek().punctuation_kind == PunctuationKind.LEFT_BRACKET:
                emit_ispc("[", first.line_number)
                consume()
                process_expr(var_stack, consume(), [PunctuationKind.RIGHT_BRACKET])
                expect_token_punc(consume(), PunctuationKind.RIGHT_BRACKET)
                emit_ispc("] ", first.line_number)

            process_expr(var_stack, consume(), [PunctuationKind.SEMICOLON])
            expect_token_punc(consume(), PunctuationKind.SEMICOLON)
            emit_ispc(";", first.line_number)

        elif can_be_expr(first):
            process_expr(var_stack, first, [PunctuationKind.SEMICOLON])
            expect_token_punc(consume(), PunctuationKind.SEMICOLON)
            emit_all(";", first.line_number)
        elif (first.kind == TokenKind.DIRECTIVE):
            emit_ispc(first.text + "\n", first.line_number)

            if first.text.startswith("#line"):
                output_ispc_line_num = int(first.text.split(" ")[1])+1
                output_c_line_num = int(first.text.split(" ")[1])+1
                output_fsl_line_num = int(first.text.split(" ")[1])+1
            elif first.text == "#" and peek().kind == TokenKind.NUMBER_LITERAL:
                output_ispc_line_num = peek().literal + 1
                output_c_line_num = peek().literal + 1
                output_fsl_line_num = peek().literal + 1
                consume()

        elif first.punctuation_kind == PunctuationKind.LEFT_BRACE:
            process_block([v for v in var_stack], consume())
        else:
            print("ERROR: Cannot decide intent with this token in procedural block")
            print_token(first)
            raise ParseError("Block Parse Error")

    def process_block(var_stack, first):
        if first.punctuation_kind != PunctuationKind.LEFT_BRACE:
            process_block_item(var_stack, first)
            return
        else:
            emit_all("{", first.line_number)

        brace_depth = 1
        while brace_depth > 0:
            next_tok = consume()
            if next_tok.punctuation_kind == PunctuationKind.LEFT_BRACE:
                brace_depth += 1
            if next_tok.punctuation_kind == PunctuationKind.RIGHT_BRACE:
                brace_depth -= 1
            if next_tok.kind == TokenKind.EOF:
                break

            if brace_depth <= 0:
                break
            process_block_item(var_stack, next_tok)

        expect_token_punc(next_tok, PunctuationKind.RIGHT_BRACE)
        if first.punctuation_kind == PunctuationKind.LEFT_BRACE:
            emit_all("}", next_tok.line_number)

    struct_decls = [] # Type's
    var_stack = [] # VarDecl's

    # Add resource var decls
    for srt_name in resources:
        for srt_set in resources[srt_name]:
            for res in resources[srt_name][srt_set]:

                type_string = res['type']
                if res["underlying_type"] and res["underlying_type"] != "":
                    type_string += "(" + res["underlying_type"] + ")"

                decl = VarDecl()

                t = Type()

                if res['type'] in ["Tex1D", "RWTex1D", "WTex1D"]:
                    t.kind = TypeKind.TEX1D
                elif res['type'] in ["Tex2D", "RWTex2D", "WTex2D"]:
                    t.kind = TypeKind.TEX2D
                elif res['type'] in ["Tex3D", "RWTex3D", "WTex3D"]:
                    t.kind = TypeKind.TEX3D
                elif res['type'] in ["Buffer", "RWBuffer", "WBuffer", "CBUFFER"]:
                    t.kind = TypeKind.BUFFER
                elif res['type'] == "SamplerState":
                    t.kind = TypeKind.SAMPLER
                elif res['type'] == "Uniform":
                    t.kind = TypeKind.UNIFORM
                else:
                    print(f"UNhandled resource type {res['type']}")
                    raise ParseError("Internal Error")

                t.underlying_type = res['underlying_type']
                t.name = type_string

                decl.decl_type = t

                decl.name = res['name']

                var_stack += [decl]

    while peek().kind != TokenKind.EOF:
        # print_token(tokens[i])

        first = consume()

        # Skip & plainly copy over everything in ispc.h
        while first.filepath.endswith("ispc.h"):
            next_tok = first
            while next_tok.kind != TokenKind.EOF and next_tok.filepath.endswith("ispc.h"):
                next_tok = consume()

            emit_ispc(first_pass[first.start_pos:next_tok.start_pos] + "\n", first.line_number)

            first = next_tok

        # Skip & plainly copy over sections that we previously marked as noparse
        if first.kind == TokenKind.DIRECTIVE and "#noparse_start" in first.text:
            next_tok = first
            while next_tok.kind != TokenKind.EOF and "#noparse_end" not in next_tok.text:
                next_tok = consume()

            emit_all(first_pass[first.start_pos+len(first.text):next_tok.start_pos], first.line_number)

            first = consume()

        if (first.kind == TokenKind.DIRECTIVE):
            emit_all(first.text + "\n", first.line_number)
            if first.text.startswith("#line"):
                output_ispc_line_num = int(first.text.split(" ")[1])+1
                output_c_line_num = int(first.text.split(" ")[1])+1
                output_fsl_line_num = int(first.text.split(" ")[1])+1
            elif first.text == "#" and peek().kind == TokenKind.NUMBER_LITERAL:
                output_ispc_line_num = peek().literal + 1
                output_c_line_num = peek().literal + 1
                output_fsl_line_num = peek().literal + 1
                consume()

        elif first.kind == TokenKind.IDENTIFIER:
            function_sig_tokens = {}
            if first.text == 'STRUCT':
                """"""""""""""""""""
                """    Struct    """
                """"""""""""""""""""

                expect_token_punc(consume(), PunctuationKind.LEFT_PAREN)
                struct_ident = consume()
                expect_token(struct_ident, TokenKind.IDENTIFIER)
                expect_token_punc(consume(), PunctuationKind.RIGHT_PAREN)

                lbrace = consume()
                expect_token_punc(lbrace, PunctuationKind.LEFT_BRACE)

                # emit start of struct decl
                emit_multiple(f"struct {struct_ident.text}", first.line_number, ['ispc', 'c'])
                emit_fsl(f"STRUCT({struct_ident.text})", first.line_number)
                emit_all("{", lbrace.line_number)

                struct_decl = Type()
                struct_decl.name = struct_ident.text
                struct_decl.kind = TypeKind.STRUCT
                struct_decl.members = []

                next_tok = consume()

                while next_tok.punctuation_kind != PunctuationKind.RIGHT_BRACE and next_tok.kind != TokenKind.EOF:
                    expect_token_text(next_tok, TokenKind.IDENTIFIER, "DATA")
                    member_start_tok = next_tok
                    expect_token_punc(consume(), PunctuationKind.LEFT_PAREN)
                    next_tok = consume()
                    type_tokens = []
                    name_tokens = []
                    semantic_tokens = []

                    arg_pos = 0
                    while next_tok.punctuation_kind != PunctuationKind.RIGHT_PAREN and next_tok.kind != TokenKind.EOF:
                        if next_tok.punctuation_kind != PunctuationKind.COMMA and arg_pos == 0:
                            type_tokens += [next_tok]
                        if next_tok.punctuation_kind != PunctuationKind.COMMA and arg_pos == 1:
                            name_tokens += [next_tok]
                        if next_tok.punctuation_kind != PunctuationKind.COMMA and arg_pos == 2:
                            semantic_tokens += [next_tok]
                        next_tok = consume()

                        if next_tok.punctuation_kind == PunctuationKind.COMMA:
                            if arg_pos == 2:
                                print("ERROR: Expected exactly 3 arguments in DATA struct member")
                                print_token(next_tok)
                                raise ParseError("Parse error")
                            arg_pos += 1

                    member_type: Type = parse_type(type_tokens)

                    if name_tokens[0].kind != TokenKind.IDENTIFIER:
                        print("ERROR: Expected member name identifier")
                        print_token(name_tokens[0])
                        raise ParseError("Parse error")

                    member_name = name_tokens[0].text

                    if len(name_tokens) > 0:
                        i = 1

                        while i+2 < len(name_tokens):
                            expect_token_punc(name_tokens[i+0], PunctuationKind.LEFT_BRACKET)
                            expect_token(name_tokens[i+1], TokenKind.NUMBER_LITERAL)
                            expect_token_punc(name_tokens[i+2], PunctuationKind.RIGHT_BRACKET)

                            array_type = Type()
                            array_type.kind = TypeKind.ARRAY
                            array_type.elem_count = int(float(name_tokens[i+1].text))
                            array_type.elem_type = member_type
                            array_type.name = f"{member_type.name}[{array_type.elem_count}]"

                            member_type = array_type

                            i += 3

                    member = VarDecl()
                    member.name = member_name
                    member.decl_type = member_type
                    struct_decl.members += [member]

                    # emit struct member

                    ## Because of wonky C array syntax, we need to attach [X] to the output name
                    member_name_output = member.name
                    indirection = member_type
                    while indirection.kind == TypeKind.ARRAY:
                        member_name_output = member_name_output[0:len(member.name)] + f"[{indirection.elem_count}]" + (member_name_output[len(member.name):] if len(member_name_output) > len(member.name) else "")
                        indirection = indirection.elem_type
                    member_type_output = indirection.name

                    emit_multiple(f"{member_type_output} {member_name_output};", member_start_tok.line_number, ['ispc', 'c'])
                    emit_fsl(f"DATA({member_type_output}, {member_name_output}, None);", member_start_tok.line_number)

                    expect_token_punc(consume(), PunctuationKind.SEMICOLON)
                    next_tok = consume()

                after_struct = consume()
                expect_token_punc(after_struct, PunctuationKind.SEMICOLON)
                # emit struct end
                emit_all("};", after_struct.line_number)

                struct_decls += [struct_decl]

            elif can_be_function_decl(first, function_sig_tokens):
                """"""""""""""""""""
                """  Functions   """
                """"""""""""""""""""

                next_tok = first

                # print([(i.text if isinstance(i, Token) else [t.text for t in i]) for i in function_sig_tokens.values()])

                modifier_tok = function_sig_tokens['modifier']
                return_type_tokens = function_sig_tokens['type']
                name_tok = function_sig_tokens['name']

                is_c_export = False
                is_transpile = False
                if modifier_tok and modifier_tok.text == "cexport":
                    emit_ispc("export ", modifier_tok.line_number)
                    is_c_export = True

                if modifier_tok and modifier_tok.text == "task":
                    emit_ispc("task ", modifier_tok.line_number)

                if modifier_tok and modifier_tok.text == "inline":
                    emit_multiple("inline ", modifier_tok.line_number, ["ispc", "c"])

                last_mask = emit_mask.copy()
                if modifier_tok and modifier_tok.text == "transpile":
                    emit_mask['ispc'] = True
                    emit_mask['c'] = True
                    emit_mask['fsl'] = True
                    emit_multiple("inline ", modifier_tok.line_number, ["ispc", "c"])
                    is_transpile = True

                all_sig_tokens = ([modifier_tok] if modifier_tok != None else []) + return_type_tokens + [name_tok]

                for tok in all_sig_tokens[1:]:
                    if consume() != tok:
                        print("ERROR: (internal error) These should be the same but aren't")
                        print_token(peek(-1))
                        print_token(tok)
                        raise ValueError("Internal Parse Error")

                return_type = parse_type(return_type_tokens)

                emit_all(return_type.name + " ", return_type_tokens[0].line_number)

                emit_all(name_tok.text, name_tok.line_number)

                next_tok = consume()

                expect_token_punc(next_tok, PunctuationKind.LEFT_PAREN)
                next_tok = consume()

                emit_all("(", next_tok.line_number)

                is_first_param = True
                param_vars = []
                while next_tok.punctuation_kind != PunctuationKind.RIGHT_PAREN and next_tok.kind != TokenKind.EOF:
                    is_ref_decl = False

                    if next_tok.text in ["in", "inout", "out"]:
                        if next_tok.text != "in":
                            is_ref_decl = True # if inout or out then it needs to be a ISPC reference (&)
                        next_tok = consume()

                    decl = parse_var_decl(next_tok)
                    decl.is_ref = is_ref_decl
                    param_vars += [decl]

                    if is_c_export and (decl.decl_type.kind != TypeKind.UNIFORM) and not (decl.decl_type.kind == TypeKind.ARRAY or resolve_type_ident(decl.decl_type.underlying_type).kind == TypeKind.UNIFORM):
                        print("ERROR: Paremeters in exported functions must be Uniform types. Use syntax: 'Uniform(Type) param' to declare uniform parameters.")
                        print_token(next_tok)
                        raise ParseError("Function Parameter Parse Error")

                    emit_ispc(format_decl_string(decl, is_ref_decl, 'ispc', arrays_are_pointers=not is_c_export), next_tok.line_number)
                    emit_c(format_decl_string(decl, is_ref_decl, 'c', arrays_are_pointers=True), next_tok.line_number)
                    emit_fsl(format_decl_string(decl, is_ref_decl, 'fsl', arrays_are_pointers=True), next_tok.line_number)

                    is_first_param = False

                    next_tok = consume()

                    if next_tok.punctuation_kind != PunctuationKind.COMMA:
                        break

                    emit_all(", ", next_tok.line_number)

                    next_tok = consume()

                expect_token_punc(next_tok, PunctuationKind.RIGHT_PAREN)
                emit_all(")", next_tok.line_number)
                next_tok = consume()
                expect_token_punc(next_tok, PunctuationKind.LEFT_BRACE)

                c_was_on = emit_mask['c']
                emit_mask['c'] = True
                # Generate C function signature
                if is_c_export:
                    emit_c("#ifdef __cplusplus\n", next_tok.line_number-1)
                    emit_c("extern \"C\"", next_tok.line_number-1)
                    emit_c("\n#else\n", next_tok.line_number-1)
                    emit_c("extern", next_tok.line_number-1)
                    emit_c("\n#endif\n", next_tok.line_number-1)

                    emit_c(f"{return_type.name} {name_tok.text}(", next_tok.line_number)
                    for i, decl in enumerate(param_vars):
                        emit_c(format_decl_string(decl, decl.is_ref, 'c', arrays_are_pointers=True, strip_uniform_semantic=True), next_tok.line_number)
                        if i < len(param_vars)-1:
                            emit_c(", ", next_tok.line_number)
                    emit_c(");\n", next_tok.line_number)
                emit_mask['c'] = c_was_on

                process_block([v for v in var_stack] + [v for v in param_vars], next_tok)

                emit_mask = last_mask

            elif first.text == "typedef":
                # Emit all tokens until ;
                next_tok = first

                while next_tok.punctuation_kind != PunctuationKind.SEMICOLON and next_tok.kind != TokenKind.EOF:
                    emit_all(next_tok.text + " ", next_tok.line_number)
                    next_tok = consume()
                expect_token_punc(next_tok, PunctuationKind.SEMICOLON)
                emit_all(";", next_tok.line_number)
            elif can_be_var_decl_type_ident(first):
                """"""""""""""""""""
                """  Global Var  """
                """"""""""""""""""""

                next_tok = first;

                underlying_type = decl.decl_type if decl.decl_type.kind != TypeKind.UNIFORM else resolve_type_ident(decl.decl_type.underlying_type)

                decl = parse_var_decl(next_tok)
                var_stack += [decl]

                emit_ispc(format_decl_string(decl, False, 'ispc'), first.line_number)
                emit_c(format_decl_string(decl, False, 'c'), first.line_number)
                emit_fsl(format_decl_string(decl, False, 'fsl'), first.line_number)

                next_tok = consume()
                if next_tok.punctuation_kind == PunctuationKind.EQUALS:
                    start = next_tok.start_pos

                    while next_tok.kind != TokenKind.EOF and next_tok.punctuation_kind != PunctuationKind.SEMICOLON:
                        next_tok = consume()

                    expect_token_punc(next_tok, PunctuationKind.SEMICOLON)

                    emit_all(first_pass[start:next_tok.start_pos+len(next_tok.text)], first.line_number)
                else:
                    expect_token_punc(next_tok, PunctuationKind.SEMICOLON)
                    emit_all(";", next_tok.line_number)

            else:
                print("ERROR: Unexpected top-scope token")
                print_token(first)
                raise ParseError("Parse error")

    copyright_notice = """/*
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
 */"""

    now = datetime.now().astimezone()
    # DD MONTH YYYY HH:MM TIMEZONE
    timestamp = now.strftime("%d %B %Y %H:%M %Z")

    header_guard = program_name.replace(' ', '_').upper() + "_H"

    prefix = "\n"
    prefix += "////////////////////////////////////////////\n"
    prefix += "////////////// DO NOT MODIFY THIS FILE !!!\n"
    prefix += "////////////////////////////////////////////\n\n"
    prefix += f"// This file was generated in compile-time from '{binary.fsl_filepath}' @ {timestamp}\n\n"
    prefix += copyright_notice + "\n"
    prefix += f"#ifndef {header_guard}\n"
    prefix += f"#define {header_guard}\n"

    suffix = ""
    suffix = f"#endif // {header_guard}\n"

    c_header = prefix
    c_header += "#include <stdint.h>\n"
    c_header += "#include <math.h>\n"
    c_header += "#ifndef uint\n"
    c_header += "#define uint uint32_t\n"
    c_header += "#endif\n"
    # mute pvs-studio for generated header
    c_header += "// -V::550,525,537\n"
    c_header += "#if !defined(__cplusplus) && !defined(ISPC) && !defined(inline)\n"
    c_header += "#define TF_GENERATED_C_STATIC_INLINE\n"
    c_header += "#define inline static inline\n"
    c_header += "#endif\n"
    c_header += output_c_src + "\n"
    c_header += "#ifdef TF_GENERATED_C_STATIC_INLINE\n"
    c_header += "#undef inline\n"
    c_header += "#undef TF_GENERATED_C_STATIC_INLINE\n"
    c_header += "#endif\n"
    c_header += suffix

    fsl_header = ""
    fsl_header += prefix
    fsl_header += output_fsl_src + "\n"
    fsl_header += suffix

    os.makedirs(binary.ispc_cpu_header_dir, exist_ok=True)
    os.makedirs(binary.ispc_shader_header_dir, exist_ok=True)
    c_header_path = os.path.join(binary.ispc_cpu_header_dir, f'{os.path.splitext(os.path.basename(dst))[0]}.h')
    fsl_header_path = os.path.join(binary.ispc_shader_header_dir, f'{os.path.splitext(os.path.basename(dst))[0]}.h')

    open(dst, 'w+').write(output_ispc_src)
    clang_format_file(dst)

    # In Jenkins, multiple projects may generate this file at the same time causing race condition, so this is how we work around it
    def atomic_write(path, data, mode='w', **open_kwargs):
        dir_ = os.path.dirname(path)
        # ensure temp goes in same directory so replace is atomic
        fd, tmp = tempfile.mkstemp(dir=dir_,
                                    prefix='.tmp_',
                                    suffix=os.path.basename(path))
        try:
            with os.fdopen(fd, mode, **open_kwargs) as f:
                f.write(data)
            try:
                os.replace(tmp, path)
            except PermissionError:
                # somebody else holds it open — just give up
                return
        finally:
            # clean up the temp file if it still exists
            if os.path.exists(tmp):
                os.unlink(tmp)

    atomic_write(c_header_path.strip(), c_header)
    atomic_write(fsl_header_path.strip(), fsl_header)

    clang_format_file(c_header_path)
    clang_format_file(fsl_header_path)

    return 0, []
