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

// INTERFACES
#include "../../Application/Interfaces/IUI.h"
#include "../../Game/Interfaces/IScripting.h"
#include "../../Utilities/Interfaces/ILog.h"

#ifdef ENABLE_FORGE_SCRIPTING

// RENDERER
#include "../../Graphics/Interfaces/IGraphics.h"

// PREPROCESSOR DEFINES
#define MAX_LUA_STR_LEN       256
#define MAX_NUM_SCRIPTS       16
#define SCRIPTING_START_FRAME 100
#ifdef AUTOMATED_TESTING
#define AUTOMATEDSCRIPTING_WAIT_INTERVAL 5
#endif
#include "../../Application/Interfaces/IScreenshot.h"

typedef struct ScriptInfo
{
    char*       pFileName = NULL;
    const bool* pWaitCondition = NULL;
} ScriptInfo;

static LuaManager* pLuaManager = NULL;
static bool        sLocalLuaManager = false;
static int32_t     sLuaScriptIntervalCounter = SCRIPTING_START_FRAME;

static ScriptInfo* pTestScripts = NULL;
static uint32_t    sTestScriptCount = 0;
#ifdef AUTOMATED_TESTING
static uint32_t sTestScriptIter = 0;
bool            gAutomatedTestingScriptsFinished = false;
#endif

static ScriptInfo* pRuntimeScripts = NULL;
static uint32_t    sRuntimeScriptCount = 0;
static uint32_t    sRuntimeScriptIter = 0;

#ifdef ENABLE_FORGE_UI

typedef void (*WidgetCallback)(void* pUserData);

//////////////////////////////
// PRIVATE HELPER FUNCTIONS //
//////////////////////////////

static void fixLuaName(bstring* str)
{
    char*       left = (char*)&str->data[0];
    const char* right = (const char*)&str->data[0];
    const char* end = (const char*)&str->data[str->slen];

    // Skip begining of the string, until letter is found
    for (; right < end && !isalpha(*right); ++right)
        ;

    // Do 2 pointer traversal, filling string with valid content
    for (; right < end; ++right)
    {
        if (*right == '_' || isalnum(*right))
            *(left++) = *right;
    }
    *left = '\0';
    str->slen = (int)(left - (const char*)&str->data[0]);
}

void registerWidgetFloat(bstring* functionName, float* var)
{
    pLuaManager->SetFunction((const char*)functionName->data,
                             [var](ILuaStateWrap* state) -> int
                             {
                                 *var = (float)state->GetNumberArg(1);
                                 return 0;
                             });

    functionName->data[0] = 'G';
    pLuaManager->SetFunction((const char*)functionName->data,
                             [var](ILuaStateWrap* state) -> int
                             {
                                 state->PushResultNumber((double)*var);
                                 return 1;
                             });
}

void registerWidgetFloat2(bstring* functionName, float2* var)
{
    pLuaManager->SetFunction((const char*)functionName->data,
                             [var](ILuaStateWrap* state) -> int
                             {
                                 var->x = (float)state->GetNumberArg(1);
                                 var->y = (float)state->GetNumberArg(2);
                                 return 0;
                             });

    functionName->data[0] = 'G';
    pLuaManager->SetFunction((const char*)functionName->data,
                             [var](ILuaStateWrap* state) -> int
                             {
                                 state->PushResultNumber((double)var->x);
                                 state->PushResultNumber((double)var->y);
                                 return 2;
                             });
}

void registerWidgetFloat3(bstring* functionName, float3* var)
{
    pLuaManager->SetFunction((const char*)functionName->data,
                             [var](ILuaStateWrap* state) -> int
                             {
                                 var->x = (float)state->GetNumberArg(1);
                                 var->y = (float)state->GetNumberArg(2);
                                 var->z = (float)state->GetNumberArg(3);
                                 return 0;
                             });

    functionName->data[0] = 'G';
    pLuaManager->SetFunction((const char*)functionName->data,
                             [var](ILuaStateWrap* state) -> int
                             {
                                 state->PushResultNumber((double)var->x);
                                 state->PushResultNumber((double)var->y);
                                 state->PushResultNumber((double)var->z);
                                 return 3;
                             });
}

void registerWidgetFloat4(bstring* functionName, float4* var)
{
    pLuaManager->SetFunction((const char*)functionName->data,
                             [var](ILuaStateWrap* state) -> int
                             {
                                 var->x = (float)state->GetNumberArg(1);
                                 var->y = (float)state->GetNumberArg(2);
                                 var->z = (float)state->GetNumberArg(3);
                                 var->w = (float)state->GetNumberArg(4);
                                 return 0;
                             });

    functionName->data[0] = 'G';
    pLuaManager->SetFunction((const char*)functionName->data,
                             [var](ILuaStateWrap* state) -> int
                             {
                                 state->PushResultNumber((double)var->x);
                                 state->PushResultNumber((double)var->y);
                                 state->PushResultNumber((double)var->z);
                                 state->PushResultNumber((double)var->w);
                                 return 4;
                             });
}

void registerWidgetInt(bstring* functionName, int32_t* var)
{
    pLuaManager->SetFunction((const char*)functionName->data,
                             [var](ILuaStateWrap* state) -> int
                             {
                                 *var = (int32_t)state->GetIntegerArg(1);
                                 return 0;
                             });

    functionName->data[0] = 'G';
    pLuaManager->SetFunction((const char*)functionName->data,
                             [var](ILuaStateWrap* state) -> int
                             {
                                 state->PushResultInteger((int)*var);
                                 return 1;
                             });
}

void registerWidgetUint(bstring* functionName, uint32_t* var)
{
    pLuaManager->SetFunction((const char*)functionName->data,
                             [var](ILuaStateWrap* state) -> int
                             {
                                 *var = (uint32_t)state->GetIntegerArg(1);
                                 return 0;
                             });

    functionName->data[0] = 'G';
    pLuaManager->SetFunction((const char*)functionName->data,
                             [var](ILuaStateWrap* state) -> int
                             {
                                 state->PushResultInteger((int)*var);
                                 return 1;
                             });
}

void registerWidgetSize(bstring* functionName, size_t* var)
{
    pLuaManager->SetFunction((const char*)functionName->data,
                             [var](ILuaStateWrap* state) -> int
                             {
                                 *var = (size_t)state->GetIntegerArg(1);
                                 return 0;
                             });

    functionName->data[0] = 'G';
    pLuaManager->SetFunction((const char*)functionName->data,
                             [var](ILuaStateWrap* state) -> int
                             {
                                 state->PushResultInteger((int)*var);
                                 return 1;
                             });
}

void registerWidgetBool(bstring* functionName, bool* var)
{
    pLuaManager->SetFunction((const char*)functionName->data,
                             [var](ILuaStateWrap* state) -> int
                             {
                                 *var = (bool)state->GetIntegerArg(1);
                                 return 0;
                             });

    functionName->data[0] = 'G';
    pLuaManager->SetFunction((const char*)functionName->data,
                             [var](ILuaStateWrap* state) -> int
                             {
                                 state->PushResultInteger((int)*var);
                                 return 1;
                             });
}

void registerWidgetText(bstring* functionName, bstring* var)
{
    pLuaManager->SetFunction((const char*)functionName->data,
                             [var](ILuaStateWrap* state) -> int
                             {
                                 const char* str;
                                 state->GetStringArg(1, &str);
                                 if (!str)
                                     str = "";
                                 bassigncstr(var, str);
                                 return 0;
                             });

    functionName->data[0] = 'G';
    pLuaManager->SetFunction((const char*)functionName->data,
                             [var](ILuaStateWrap* state) -> int
                             {
                                 state->PushResultString((const char*)var->data);
                                 return 1;
                             });
}

#endif

static void RegisterDefaultLuaFunctions(LuaManager* pManager)
{
    pManager->SetFunction("LOGINFO",
                          [](ILuaStateWrap* state) -> int
                          {
                              const char* str;
                              state->GetStringArg(1, &str);
                              if (!str)
                                  str = "";

                              LOGF(LogLevel::eINFO, "%s", str);
                              return 0;
                          });

    pManager->SetFunction("SetCounter",
                          [](ILuaStateWrap* state) -> int
                          {
                              sLuaScriptIntervalCounter = (int32_t)state->GetIntegerArg(1);
                              return 0;
                          });

    pManager->SetFunction("GetDefaultAutomationFrameCount",
                          [](ILuaStateWrap* state) -> int
                          {
#ifdef DEFAULT_AUTOMATION_FRAME_COUNT
                              state->PushResultInteger(DEFAULT_AUTOMATION_FRAME_COUNT);
#else
		// If we didn't compile with AUTOMATED_TESTING means that DEFAULT_AUTOMATION_FRAME_COUNT might not be defined,
		// in that case we just return a magic value, don't really matter since we are not doing AUTOMATED_TESTING
		state->PushResultInteger(240);
#endif
                              return 1;
                          });
#ifdef AUTOMATED_TESTING
    pManager->SetFunction("RequestScreenshotCapture",
                          [](ILuaStateWrap* state) -> int
                          {
                              const char* name;
                              state->GetStringArg(1, &name);
                              if (!name)
                                  name = "";
                              requestScreenshotCapture(name);
                              return 0;
                          });
#endif
}

#endif

////////////////////////////////
// PUBLIC INTERFACE FUNCTIONS //
////////////////////////////////

void platformInitLuaScriptingSystem()
{
#ifdef ENABLE_FORGE_SCRIPTING
    ASSERT(pLuaManager == NULL);

    pLuaManager = tf_new(LuaManager);
    pLuaManager->Init();
    sLocalLuaManager = true;
#ifdef AUTOMATED_TESTING
    gAutomatedTestingScriptsFinished = false;
#endif

    RegisterDefaultLuaFunctions(pLuaManager);

    pTestScripts = (ScriptInfo*)tf_calloc(MAX_NUM_SCRIPTS, sizeof(ScriptInfo));
    pRuntimeScripts = (ScriptInfo*)tf_calloc(MAX_NUM_SCRIPTS, sizeof(ScriptInfo));

    for (uint32_t i = 0; i < MAX_NUM_SCRIPTS; ++i)
    {
        pTestScripts[i].pFileName = (char*)tf_calloc(MAX_LUA_STR_LEN, sizeof(char));
        pRuntimeScripts[i].pFileName = (char*)tf_calloc(MAX_LUA_STR_LEN, sizeof(char));
    }

#endif
}

void platformExitLuaScriptingSystem()
{
#ifdef ENABLE_FORGE_SCRIPTING
    for (uint32_t i = 0; i < MAX_NUM_SCRIPTS; ++i)
    {
        tf_free(pTestScripts[i].pFileName);
        tf_free(pRuntimeScripts[i].pFileName);
    }

    tf_free(pTestScripts);
    tf_free(pRuntimeScripts);

    pTestScripts = NULL;
    pRuntimeScripts = NULL;

    sTestScriptCount = 0;
    sRuntimeScriptCount = 0;

#ifdef AUTOMATED_TESTING
    sTestScriptIter = 0;
    gAutomatedTestingScriptsFinished = true;
#endif
    sRuntimeScriptIter = 0;

    if (sLocalLuaManager)
    {
        pLuaManager->Exit();
        tf_delete(pLuaManager);
        sLocalLuaManager = false;
    }

    pLuaManager = NULL;
    sLuaScriptIntervalCounter = SCRIPTING_START_FRAME;
#endif
}

void platformUpdateLuaScriptingSystem(bool appDrawn)
{
#ifdef ENABLE_FORGE_SCRIPTING
    if (appDrawn && sLuaScriptIntervalCounter > 0)
        --sLuaScriptIntervalCounter;

#ifdef AUTOMATED_TESTING
    if (sTestScriptCount > 0 && !sLuaScriptIntervalCounter)
    {
        ASSERT(sTestScriptIter >= 0 && sTestScriptIter < sTestScriptCount);
        ScriptInfo testScript = pTestScripts[sTestScriptIter];

        if (!testScript.pWaitCondition || *testScript.pWaitCondition)
        {
            LOGF(eINFO, "Automated Test Script %s is running...", testScript.pFileName);
            pLuaManager->RunScript(testScript.pFileName);

            ++sTestScriptIter;

            if (sTestScriptIter == sTestScriptCount)
            {
                sTestScriptCount = 0;
                sTestScriptIter = 0;
            }

            sLuaScriptIntervalCounter += AUTOMATEDSCRIPTING_WAIT_INTERVAL;
        }
    }

    gAutomatedTestingScriptsFinished = !sTestScriptCount;
#endif

    if (sRuntimeScriptCount > 0 && !sLuaScriptIntervalCounter)
    {
        ASSERT(sRuntimeScriptIter >= 0 && sRuntimeScriptIter < sRuntimeScriptCount);
        ScriptInfo runtimeScript = pRuntimeScripts[sRuntimeScriptIter];

        if (!runtimeScript.pWaitCondition || *runtimeScript.pWaitCondition)
        {
            LOGF(LogLevel::eINFO, "Script %s is running...", runtimeScript.pFileName);
            pLuaManager->RunScript(runtimeScript.pFileName);

            ++sRuntimeScriptIter;

            if (sRuntimeScriptIter == sRuntimeScriptCount)
            {
                sRuntimeScriptCount = 0;
                sRuntimeScriptIter = 0;
            }
        }
    }
#else
    (void)appDrawn;
#endif
}

void luaDestroyCurrentManager()
{
#ifdef ENABLE_FORGE_SCRIPTING
    pLuaManager->Exit();
    tf_delete(pLuaManager);
    sLocalLuaManager = false;
#else
    LOGF(LogLevel::eWARNING, "Attempting to use Forge Lua Scripting without define!");
    LOGF(LogLevel::eWARNING, "Make sure to define 'ENABLE_FORGE_SCRIPTING' for Scripting to work!");
#endif
}

void luaAssignCustomManager(LuaManager* pNewManager)
{
#ifdef ENABLE_FORGE_SCRIPTING
    ASSERT(pNewManager && "Invalid Manager Handle provided!");

    RegisterDefaultLuaFunctions(pNewManager);

    pLuaManager = pNewManager;
    sLocalLuaManager = false;
#else
    (void)pNewManager;
    LOGF(LogLevel::eWARNING, "Attempting to use Forge Lua Scripting without define!");
    LOGF(LogLevel::eWARNING, "Make sure to define 'ENABLE_FORGE_SCRIPTING' for Scripting to work!");
#endif
}

void luaDefineScripts(TFLuaScriptDesc* pDescs, uint32_t count)
{
#ifdef ENABLE_FORGE_SCRIPTING
    ASSERT(pDescs);
    ASSERT(sTestScriptCount + count < MAX_NUM_SCRIPTS);

    for (uint32_t i = 0; i < count; ++i)
    {
        if (pDescs[i].pScriptFileName)
        {
            strcpy(pTestScripts[sTestScriptCount].pFileName, pDescs[i].pScriptFileName);
            pTestScripts[sTestScriptCount].pWaitCondition = pDescs[i].pWaitCondition;
        }

        ++sTestScriptCount;
    }
#else
    (void)pDescs;
    (void)count;
    LOGF(LogLevel::eWARNING, "Attempting to use Forge Lua Scripting without define!");
    LOGF(LogLevel::eWARNING, "Make sure to define 'ENABLE_FORGE_SCRIPTING' for Scripting to work!");
#endif
}

void luaQueueScriptToRun(TFLuaScriptDesc* pDesc)
{
#ifdef ENABLE_FORGE_SCRIPTING
    ASSERT(pDesc);
    ASSERT(sRuntimeScriptCount < MAX_NUM_SCRIPTS);

    if (pDesc->pScriptFileName)
    {
        strcpy(pRuntimeScripts[sRuntimeScriptCount].pFileName, pDesc->pScriptFileName);
        pRuntimeScripts[sRuntimeScriptCount].pWaitCondition = pDesc->pWaitCondition;
    }

    ++sRuntimeScriptCount;
#else
    (void)pDesc;
    LOGF(LogLevel::eWARNING, "Attempting to use Forge Lua Scripting without define!");
    LOGF(LogLevel::eWARNING, "Make sure to define 'ENABLE_FORGE_SCRIPTING' for Scripting to work!");
#endif
}

void luaRegisterWidgetFunction(TFLuaWidgetFunctionDesc* pDesc)
{
#if defined(ENABLE_FORGE_UI) && defined(ENABLE_FORGE_SCRIPTING)
    ASSERT(pDesc);

    unsigned char buf[MAX_LABEL_STR_LENGTH + 32];

    bstring functionName = bemptyfromarr(buf);

    bassigncstr(&functionName, pDesc->pLabel);

    fixLuaName(&functionName);

    // handle post fixes for function names
    switch (pDesc->mType)
    {
    case TF_LUA_WIDGET_FUNCTION_NONE:
        ASSERTFAIL("No function type was provided when registering a Lua widget function with name %s", pDesc->pLabel);
        break;
    case TF_LUA_WIDGET_FUNCTION_ON_HOVER:
        bcatliteral(&functionName, "OnHover");
        break;
    case TF_LUA_WIDGET_FUNCTION_ON_ACTIVE:
        bcatliteral(&functionName, "OnActive");
        break;
    case TF_LUA_WIDGET_FUNCTION_ON_FOCUS:
        bcatliteral(&functionName, "OnFocus");
        break;
    case TF_LUA_WIDGET_FUNCTION_ON_EDITED:
        bcatliteral(&functionName, "OnEdited");
        break;
    case TF_LUA_WIDGET_FUNCTION_ON_DEACTIVATED:
        bcatliteral(&functionName, "OnDeactivated");
        break;
    case TF_LUA_WIDGET_FUNCTION_ON_DEACTIVATED_AFTER_EDIT:
        bcatliteral(&functionName, "OnDeactivatedAfterEdit");
        break;
    }

    pLuaManager->SetFunction((const char*)functionName.data,
                             [fn = (WidgetCallback)pDesc->pFunc, data = pDesc->pFuncData](ILuaStateWrap* state) -> int
                             {
                                 UNREF_PARAM(state);
                                 fn(data);
                                 return 0;
                             });

    bdestroy(&functionName);
#else
    (void)pDesc;
    LOGF(LogLevel::eWARNING, "Attempting to use Forge Lua Scripting without define!");
    LOGF(LogLevel::eWARNING, "Make sure to define 'ENABLE_FORGE_SCRIPTING' for Scripting to work!");
#endif
}

void luaRegisterWidgetVariable(TFLuaWidgetVariableDesc* pDesc)
{
#if defined(ENABLE_FORGE_UI) && defined(ENABLE_FORGE_SCRIPTING)
    ASSERT(pDesc);

    unsigned char buf[MAX_LABEL_STR_LENGTH + 3];
    bstring       functionName = bemptyfromarr(buf);

    bcatliteral(&functionName, "Set");
    bcatcstr(&functionName, pDesc->pLabel);
    fixLuaName(&functionName);

    TFUIWidgetType type = (TFUIWidgetType)pDesc->mWidgetType;

    switch (type)
    {
    case TF_WIDGET_TYPE_SLIDER_FLOAT:
    {
        registerWidgetFloat(&functionName, pDesc->pFloat);
        break;
    }

    case TF_WIDGET_TYPE_SLIDER_FLOAT2:
    {
        registerWidgetFloat2(&functionName, pDesc->pFloat2);
        break;
    }

    case TF_WIDGET_TYPE_SLIDER_FLOAT3:
    case TF_WIDGET_TYPE_COLOR3_PICKER:
    {
        registerWidgetFloat3(&functionName, pDesc->pFloat3);
        break;
    }

    case TF_WIDGET_TYPE_SLIDER_FLOAT4:
    case TF_WIDGET_TYPE_COLOR_RGBA32_SLIDER:
    case TF_WIDGET_TYPE_COLOR_PICKER:
    {
        registerWidgetFloat4(&functionName, pDesc->pFloat4);
        break;
    }

    case TF_WIDGET_TYPE_SLIDER_INT:
    case TF_WIDGET_TYPE_DROPDOWN:
    {
        registerWidgetInt(&functionName, pDesc->pInt);
        break;
    }

    case TF_WIDGET_TYPE_SLIDER_UINT:
    case TF_WIDGET_TYPE_COLOR_RGBA8_SLIDER:
    {
        registerWidgetUint(&functionName, pDesc->pUint);
        break;
    }

    case TF_WIDGET_TYPE_CHECKBOX:
    {
        registerWidgetBool(&functionName, pDesc->pBool);
        break;
    }

    case TF_WIDGET_TYPE_PROGRESS_BAR:
    {
        registerWidgetSize(&functionName, pDesc->pSize);
        break;
    }

    case TF_WIDGET_TYPE_TEXTBOX: // TODO: do we need to change color?
    case TF_WIDGET_TYPE_DYNAMIC_TEXT:
    {
        registerWidgetText(&functionName, pDesc->pText);
        break;
    }

    case TF_WIDGET_TYPE_COLLAPSING_HEADER:
    case TF_WIDGET_TYPE_RADIO_BUTTON:
    case TF_WIDGET_TYPE_COLUMN:
    case TF_WIDGET_TYPE_HISTOGRAM:
    case TF_WIDGET_TYPE_PLOT_LINES:
    case TF_WIDGET_TYPE_DEBUG_TEXTURES:
    case TF_WIDGET_TYPE_LABEL:
    case TF_WIDGET_TYPE_COLOR_LABEL:
    case TF_WIDGET_TYPE_SEPARATOR:
    case TF_WIDGET_TYPE_VERTICAL_SEPARATOR:
    case TF_WIDGET_TYPE_BUTTON:
    case TF_WIDGET_TYPE_FILLED_RECT:
    case TF_WIDGET_TYPE_TOOLTIP:
    case TF_WIDGET_TYPE_LINE:
    case TF_WIDGET_TYPE_CURVE:
    {
        ASSERTFAIL(
            "No data will be modified when registering a Widget of the provided type. Please remove this luaRegisterWidgetFunction call.");
        break;
    }

    default:
    {
        ASSERTFAIL("Trying to register a Widget of incompatible type!");
    }
    }

    bdestroy(&functionName);
#else
    (void)pWidgetHandle;
    LOGF(LogLevel::eWARNING, "Attempting to use Forge Lua Scripting without define!");
    LOGF(LogLevel::eWARNING, "Make sure to define 'ENABLE_FORGE_SCRIPTING' for Scripting to work!");
#endif
}