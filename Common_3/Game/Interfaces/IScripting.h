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

#ifndef ISCRIPTING_H
#define ISCRIPTING_H

#include "../../Application/Config.h"

// LUA
#include "../Scripting/LuaManager.h"

#ifdef ENABLE_FORGE_SCRIPTING
#define DEFINE_LUA_SCRIPTS(x, y) luaDefineScripts((x), (y))
#else
#define DEFINE_LUA_SCRIPTS(x, y) \
    {                            \
        (void)(x);               \
        (void)(y);               \
    }
#endif

/****************************************************************************/
// MARK: - Lua Scripting Data Structs
/****************************************************************************/

typedef struct TFLuaScriptDesc
{
    const char* pScriptFileName = NULL;
    const bool* pWaitCondition = NULL; // if pWaitCondition is not NULL, the script will not be executed until *pWaitCondition == true
} TFLuaScriptDesc;

typedef enum TFLuaWidgetFunctionType
{
    TF_LUA_WIDGET_FUNCTION_NONE,
    TF_LUA_WIDGET_FUNCTION_ON_HOVER,
    TF_LUA_WIDGET_FUNCTION_ON_ACTIVE,
    TF_LUA_WIDGET_FUNCTION_ON_FOCUS,
    TF_LUA_WIDGET_FUNCTION_ON_EDITED,
    TF_LUA_WIDGET_FUNCTION_ON_DEACTIVATED,
    TF_LUA_WIDGET_FUNCTION_ON_DEACTIVATED_AFTER_EDIT
} TFLuaWidgetFunctionType;

typedef void (*LuaWidgetFunctionCallback)(void*);

typedef struct TFLuaWidgetFunctionDesc
{
    const char*               pLabel;
    TFLuaWidgetFunctionType   mType;
    LuaWidgetFunctionCallback pFunc;
    void*                     pFuncData;
} TFLuaWidgetFunctionDesc;

typedef struct TFLuaWidgetVariableDesc
{
    const char* pLabel;
    uint32_t    mWidgetType; // should match a value from TFUIWidgetType enum
    union
    {
        float*    pFloat;
        float2*   pFloat2;
        float3*   pFloat3;
        float4*   pFloat4;
        int32_t*  pInt;
        uint32_t* pUint;
        size_t*   pSize;
        bool*     pBool;
        bstring*  pText;
    };
} TFLuaWidgetVariableDesc;

/****************************************************************************/
// MARK: - Lua Scripting System Functionality
/****************************************************************************/

/// Destroys the internal LuaManager instance created by the interface
/// This allows the user to define their own custom LuaManager instance
/// This function MUST be called before a new LuaManager is instantiated
FORGE_API void luaDestroyCurrentManager();

/// Assigns custom user-defined LuaManager to be used internally
/// MUST be called to define a new Manager after luaDestroyCurrentManager is invoked
FORGE_API void luaAssignCustomManager(LuaManager* pNewManager);

/// Adds an array of scripts to the Lua interface by filenames
FORGE_API void luaDefineScripts(TFLuaScriptDesc* pDescs, uint32_t count);

/// Add existing defined script to a queue to be executed
/// Script execution will occur on next platform layer system update
FORGE_API void luaQueueScriptToRun(TFLuaScriptDesc* pDesc);

FORGE_API void luaRegisterWidgetFunction(TFLuaWidgetFunctionDesc* pDesc);

FORGE_API void luaRegisterWidgetVariable(TFLuaWidgetVariableDesc* pDesc);

#endif // ISCRIPTING_H
