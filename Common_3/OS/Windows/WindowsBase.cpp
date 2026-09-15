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
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include "../../Application/Config.h"

#ifdef _WINDOWS

#include <ctime>
#include <ntverp.h>

#include "../CPUConfig.h"

#if !defined(XBOX)
#include <shlwapi.h>
#pragma comment(lib, "shlwapi.lib")
#endif

#include "../../Utilities/ThirdParty/OpenSource/Nothings/stb_ds.h"
#include "../../Utilities/ThirdParty/OpenSource/bstrlib/bstrlib.h"

#include "../../Application/Interfaces/IApp.h"
#include "../../Application/Interfaces/IFont.h"
#include "../../Application/Interfaces/IProfiler.h"
#include "../../Application/Interfaces/IUI.h"
#include "../../Game/Interfaces/IScripting.h"
#include "../../Graphics/Interfaces/IGraphics.h"
#include "../../OS/Interfaces/IOperatingSystem.h"
#include "../../OS/Interfaces/IInput.h"
#include "../../Utilities/Interfaces/IFileSystem.h"
#include "../../Utilities/Interfaces/ILog.h"
#include "../../Utilities/Interfaces/IThread.h"
#include "../../Utilities/Interfaces/ITime.h"
#include "../../Application/Interfaces/IScreenshot.h"

#if defined(ENABLE_FORGE_RELOAD_SHADER)
#include "../../Tools/ReloadServer/ReloadClient.h"
#endif
#include "../../Utilities/Interfaces/IMath.h"

#include "../../Utilities/Interfaces/IMemory.h"

#ifdef ENABLE_FORGE_STACKTRACE_DUMP
#include "WindowsStackTraceDump.h"
#endif

#define elementsOf(a) (sizeof(a) / sizeof((a)[0]))

// App Data
static IApp*         pApp = nullptr;
static TFWindowDesc* gWindowDesc = nullptr;
static bool          gShowPlatformUI = true;
static TFResetDesc   gResetDescriptor = { TF_RESET_TYPE_NONE };
static TFReloadDesc  gReloadDescriptor = { (TFReloadType)(TF_RELOAD_TYPE_RESIZE | TF_RELOAD_TYPE_SHADER | TF_RELOAD_TYPE_RENDERTARGET) };
/// CPU
static CpuInfo       gCpu;
static TFOSInfo      gOsInfo = {};

// UI
static TFUIWindowDesc gVSyncWindowDesc;
static const char*    pVSyncWindowTitle = "VSync Control";
static const char*    pVSyncCheckboxLabel = "Toggle VSync";
#if defined(ENABLE_FORGE_RELOAD_SHADER)
static const char* pReloadControlWindowTitle = "Reload Control";
#endif
static TFUIWindowDesc gGPUSwitchingWindowDesc;
static const char*    pGPUSwitchingWindowTitle = "GPU Switching";
static const char*    pSelectCardDropdownLable = "Select Graphics Card";

static TFUIWidget* pSwitchComponentLabelWidget = NULL;
static TFUIWidget* pSelectGraphicCardWidget = NULL;

extern "C" TFGPUSelection gGpuSelection;
// WindowsWindow.cpp
extern IApp*              pWindowAppRef;
extern TFWindowDesc*      gWindow;
extern bool               gCursorVisible;
extern bool               gCursorInsideRectangle;
extern TFMonitorDesc*     gMonitors;
extern uint32_t           gMonitorCount;
extern bool               initWindowSystem();
extern void               exitWindowSystem();

// WindowsLog.c
extern "C" HWND* gLogWindowHandle;

bool gCaptureCursorOnMouseDown = true;

#ifdef HOLOLENS2
extern "C"
{
    extern bool initOpenXR(const char* appName);
    extern void getOpenXRRecommendedResolution(TFRectDesc* rect);
    extern bool beginOpenXRFrame();
    extern void endFrameOpenXRNOP();
    extern bool pollOpenXREvent(bool* exitRequired);
}
#endif

//------------------------------------------------------------------------
// STATIC HELPER FUNCTIONS
//------------------------------------------------------------------------

static inline float CounterToSecondsElapsed(int64_t start, int64_t end) { return (float)(end - start) / (float)1e6; }

TFThermalStatus getThermalStatus() { return TF_THERMAL_STATUS_NOT_SUPPORTED; }

//------------------------------------------------------------------------
// OPERATING SYSTEM INTERFACE FUNCTIONS
//------------------------------------------------------------------------

void requestShutdown()
{
#ifdef HOLOLENS2
    extern bool requestShutdownXR();
    if (!requestShutdownXR())
        exit(0);
#else
    PostQuitMessage(0);
#endif
}

void requestReset(const TFResetDesc* pResetDesc) { gResetDescriptor = *pResetDesc; }

void requestReload(const TFReloadDesc* pReloadDesc) { gReloadDescriptor = *pReloadDesc; }

void errorMessagePopup(const char* title, const char* msg, TFWindowHandle* handle, errorMessagePopupCallbackFn callback)
{
#if defined(AUTOMATED_TESTING) || defined(HOLOLENS2)
    UNREF_PARAM(handle);

    LOGF(eERROR, "%s", title);
    LOGF(eERROR, "%s", msg);
#else
    MessageBoxA((HWND)handle->window, msg, title, MB_OK);
#endif
    if (callback)
    {
        callback();
    }
}

CustomMessageProcessor sCustomProc = nullptr;
void                   setCustomMessageProcessor(CustomMessageProcessor proc) { sCustomProc = proc; }

CpuInfo* getCpuInfo() { return &gCpu; }

TFOSInfo* getOsInfo() { return &gOsInfo; }

void getOsVersion(ULONG& majorVersion, ULONG& minorVersion, ULONG& buildNumber)
{
    void(WINAPI * pfnRtlGetNtVersionNumbers)(__out_opt ULONG * pNtMajorVersion, __out_opt ULONG * pNtMinorVersion,
                                             __out_opt ULONG * pNtBuildNumber);

    (FARPROC&)pfnRtlGetNtVersionNumbers = GetProcAddress(GetModuleHandle(TEXT("ntdll.dll")), "RtlGetNtVersionNumbers");

    if (pfnRtlGetNtVersionNumbers)
    {
        pfnRtlGetNtVersionNumbers(&majorVersion, &minorVersion, &buildNumber);
        buildNumber = buildNumber & ~0xF0000000;
    }
}
//------------------------------------------------------------------------
// PLATFORM LAYER CORE SUBSYSTEMS
//------------------------------------------------------------------------

#if defined(HOLOLENS2)
extern "C" void platformInitInput(TFWindowDesc*);
#endif

bool initBaseSubsystems()
{
    // Not exposed in the interface files / app layer
    extern bool platformInitFontSystem();
    extern bool platformInitUserInterface();
    extern void platformSetDefaultFontHeight(float);
    extern void platformInitLuaScriptingSystem();
    extern void platformInitWindowSystem(TFWindowDesc*);
#if !defined(HOLOLENS2)
    extern void platformInitInput(TFWindowDesc*);
#endif
    platformInitWindowSystem(gWindowDesc);
    pApp->pWindow = gWindowDesc;

    platformInitInput(gWindowDesc);

#ifdef ENABLE_FORGE_FONTS
    if (!platformInitFontSystem())
        return false;
#endif

#ifdef ENABLE_FORGE_UI
    if (!platformInitUserInterface())
        return false;

#ifdef HOLOLENS2
    platformSetDefaultFontHeight(22.0f);
#endif
#endif

#ifdef ENABLE_FORGE_SCRIPTING
    platformInitLuaScriptingSystem();

#if defined(ENABLE_FORGE_SCRIPTING) && defined(AUTOMATED_TESTING)
    // Tests below are executed first, before any tests registered in IApp::Init
    const char*     sFirstTestScripts[] = { "Test_Default.lua" };
    const uint32_t  numScripts = sizeof(sFirstTestScripts) / sizeof(sFirstTestScripts[0]);
    TFLuaScriptDesc scriptDescs[numScripts] = {};
    for (uint32_t i = 0; i < numScripts; ++i)
    {
        scriptDescs[i].pScriptFileName = sFirstTestScripts[i];
    }
    luaDefineScripts(scriptDescs, numScripts);
#endif
#endif

    return true;
}

#if defined(HOLOLENS2)
extern "C" void platformUpdateInput(float deltaTime);
#endif

void updateBaseSubsystems(float deltaTime, bool appDrawn)
{
    // Not exposed in the interface files / app layer
    extern void platformUpdateLuaScriptingSystem(bool appDrawn);
    extern void platformUpdateUserInterface(float deltaTime);
    extern void platformUpdateWindowSystem();
#if !defined(HOLOLENS2)
    extern void platformUpdateInput(float deltaTime);
#endif
    platformUpdateInput(deltaTime);

    platformUpdateWindowSystem();

#ifdef ENABLE_FORGE_SCRIPTING
    platformUpdateLuaScriptingSystem(appDrawn);
#else
    (void)appDrawn;
#endif

#ifdef ENABLE_FORGE_UI
    platformUpdateUserInterface(deltaTime);
#endif
}

#if defined(HOLOLENS2)
extern "C" void platformExitInput();
#endif

void exitBaseSubsystems()
{
    // Not exposed in the interface files / app layer

    extern void platformExitFontSystem();
    extern void platformExitUserInterface();
    extern void platformExitLuaScriptingSystem();
    extern void platformExitWindowSystem();
#if !defined(HOLOLENS2)
    extern void platformExitInput();
#endif
    platformExitInput();

    platformExitWindowSystem();

#ifdef ENABLE_FORGE_UI
    platformExitUserInterface();
#endif

#ifdef ENABLE_FORGE_FONTS
    platformExitFontSystem();
#endif

#ifdef ENABLE_FORGE_SCRIPTING
    platformExitLuaScriptingSystem();
#endif
}

//------------------------------------------------------------------------
// PLATFORM LAYER USER INTERFACE
//------------------------------------------------------------------------
#ifdef ENABLE_FORGE_UI
static void selectGraphicsCardOnEditedDropdown(void* pUserData)
{
    UNREF_PARAM(pUserData);
    TFResetDesc resetDescriptor{ TF_RESET_TYPE_GRAPHIC_CARD_SWITCH };
    requestReset(&resetDescriptor);
}
#endif

// Must be called after Graphics::initRenderer()
void setupPlatformUI(const IApp::Settings* pSettings)
{
#ifdef ENABLE_FORGE_UI

    // WINDOW AND RESOLUTION CONTROL
    extern void platformSetupWindowSystemUI(IApp*);
    platformSetupWindowSystemUI(pApp);

    // VSYNC CONTROL
    gVSyncWindowDesc.pWindowTitle = pVSyncWindowTitle;
    gVSyncWindowDesc.mStartPos = vec2(pSettings->mWidth * 0.7f, pSettings->mHeight * 0.8f);
    gVSyncWindowDesc.mStartSize = vec2(200.0f, 100.0f);
    gVSyncWindowDesc.mFlags =
        TF_UI_WINDOW_MINIMIZABLE | TF_UI_WINDOW_MOVABLE | TF_UI_WINDOW_SCALABLE | TF_UI_WINDOW_INIT_HEIGHT_FIT | TF_UI_WINDOW_BORDER;

    TFLuaWidgetVariableDesc luaVarDesc;
    luaVarDesc.pLabel = pVSyncCheckboxLabel;
    luaVarDesc.mWidgetType = TF_WIDGET_TYPE_CHECKBOX;
    luaVarDesc.pBool = &pApp->mSettings.mVSyncEnabled;
    luaRegisterWidgetVariable(&luaVarDesc);

    // MICROPROFILER UI
    toggleProfilerMenuUI(true);

#if defined(ENABLE_FORGE_RELOAD_SHADER)
    // RELOAD CONTROL
    TFUIWindowDesc uiDesc = {};
    uiDesc.pWindowTitle = pReloadControlWindowTitle;
    uiDesc.mStartPos = vec2(pSettings->mWidth * 0.7f, pSettings->mHeight * 0.9f);
    uiDesc.mStartSize = vec2(600.0f, 550.0f);
    uiDesc.mFlags =
        TF_UI_WINDOW_MINIMIZABLE | TF_UI_WINDOW_MOVABLE | TF_UI_WINDOW_SCALABLE | TF_UI_WINDOW_INIT_HEIGHT_FIT | TF_UI_WINDOW_BORDER;

    platformSetupReloadClientUI(&uiDesc);
#endif

    // GPU SWITCHING
    gGPUSwitchingWindowDesc.pWindowTitle = pGPUSwitchingWindowTitle;
    gGPUSwitchingWindowDesc.mStartPos = vec2(pSettings->mWidth * 0.6f, pSettings->mHeight * 0.01f);
    gGPUSwitchingWindowDesc.mStartSize = vec2(600.0f, 550.0f);
    gGPUSwitchingWindowDesc.mFlags =
        TF_UI_WINDOW_MINIMIZABLE | TF_UI_WINDOW_MOVABLE | TF_UI_WINDOW_SCALABLE | TF_UI_WINDOW_INIT_HEIGHT_FIT | TF_UI_WINDOW_BORDER;

    TFLuaWidgetFunctionDesc luaFuncDesc;
    luaFuncDesc.pLabel = pSelectCardDropdownLable;
    luaFuncDesc.mType = TF_LUA_WIDGET_FUNCTION_ON_EDITED;
    luaFuncDesc.pFunc = selectGraphicsCardOnEditedDropdown;
    luaFuncDesc.pFuncData = NULL;
    luaRegisterWidgetFunction(&luaFuncDesc);

    luaVarDesc.pLabel = pSelectCardDropdownLable;
    luaVarDesc.mWidgetType = TF_WIDGET_TYPE_DROPDOWN;
    luaVarDesc.pUint = &gGpuSelection.mSelectedGpuIndex;
    luaRegisterWidgetVariable(&luaVarDesc);

#if defined(ENABLE_FORGE_SCRIPTING) && defined(AUTOMATED_TESTING)
    // Tests below are executed last, after tests registered in IApp::Init have executed
    const char*     sLastTestScripts[] = { "Test_API_Switching.lua" };
    const uint32_t  numScripts = sizeof(sLastTestScripts) / sizeof(sLastTestScripts[0]);
    TFLuaScriptDesc scriptDescs[numScripts] = {};
    for (uint32_t i = 0; i < numScripts; ++i)
    {
        scriptDescs[i].pScriptFileName = sLastTestScripts[i];
    }
    luaDefineScripts(scriptDescs, numScripts);
#endif
#else
    (void)pSettings;
#endif
}

#ifdef ENABLE_FORGE_UI
static void updatePlatformUI()
{
    if (!uiIsInitialized())
    {
        return;
    }

    if (gShowPlatformUI != pApp->mSettings.mShowPlatformUI)
    {
        gShowPlatformUI = pApp->mSettings.mShowPlatformUI;

        extern void platformToggleWindowSystemUI(bool);
        platformToggleWindowSystemUI(gShowPlatformUI);

        uiUpdateWindowVisibilityByTitle(pVSyncWindowTitle, gShowPlatformUI);
        uiUpdateWindowVisibilityByTitle(pGPUSwitchingWindowTitle, gShowPlatformUI);
#if defined(ENABLE_FORGE_RELOAD_SHADER)
        uiUpdateWindowVisibilityByTitle(pReloadControlWindowTitle, gShowPlatformUI);
#endif
    }

    extern void platformUpdateWindowSystemUI();
    platformUpdateWindowSystemUI();

    // VSYNC CONTROL
    if (UI_WINDOW_IS_VISIBLE(uiBeginWidgetWindow(&gVSyncWindowDesc)))
    {
        uiLayoutAutoTextRows(1);
        uiCheckbox(pVSyncCheckboxLabel, &pApp->mSettings.mVSyncEnabled);
    }
    uiEndWidgetWindow();

    // GPU SWITCHING
    if (UI_WINDOW_IS_VISIBLE(uiBeginWidgetWindow(&gGPUSwitchingWindowDesc)))
    {
        uiLayoutAutoTextRows(1);
        static const char* gpuNames[] = { gGpuSelection.ppAvailableGpuNames[0], gGpuSelection.ppAvailableGpuNames[1],
                                          gGpuSelection.ppAvailableGpuNames[2], gGpuSelection.ppAvailableGpuNames[3] };

        uiLabel(pSelectCardDropdownLable, TF_ALIGN_LEFT);
        int selectedCard = UI_WIDGET_GET_SELECTED(uiDropdown(gpuNames, gGpuSelection.mAvailableGpuCount, gGpuSelection.mSelectedGpuIndex));
        if ((uint32_t)selectedCard != gGpuSelection.mSelectedGpuIndex)
        {
            ASSERT(selectedCard >= 0);
            gGpuSelection.mSelectedGpuIndex = (uint32_t)selectedCard;
            selectGraphicsCardOnEditedDropdown(NULL);
        }
    }
    uiEndWidgetWindow();

#if defined(ENABLE_FORGE_RELOAD_SHADER)
    platformUpdateReloadClientUI();
#endif
}
#endif

//------------------------------------------------------------------------
// APP ENTRY POINT
//------------------------------------------------------------------------

int          IApp::argc;
const char** IApp::argv;

#if defined(HOLOLENS2)
extern "C" void platformUpdateLastInputState();
#endif

int WindowsMain(int argc, char** argv, IApp* app)
{
    UNREF_PARAM(argc);
    UNREF_PARAM(argv);
    if (!initMemAlloc(app->GetName()))
        return EXIT_FAILURE;

    TFFileSystemInitDesc fsDesc = {};
    fsDesc.pAppName = app->GetName();
    if (!initFileSystem(&fsDesc))
        return EXIT_FAILURE;

#if !defined(_WINDOWS) && defined(TF_ENABLE_GRAPHICS_VALIDATION) && defined(VULKAN) && VK_OVERRIDE_LAYER_PATH
    // We are now shipping validation layer in the repo itself to remove dependency on Vulkan SDK to be installed
    // Set VK_LAYER_PATH to executable location so it can find the layer files that our application wants to use
    SetEnvironmentVariableA("VK_LAYER_PATH", pSystemFileIO->GetResourceMount(RM_DEBUG));
#endif

    initLog(app->GetName(), DEFAULT_LOG_LEVEL);

    ULONG majorVersion = 0;
    ULONG minorVersion = 0;
    ULONG buildNumber = 0;
    getOsVersion(majorVersion, minorVersion, buildNumber);
    snprintf(gOsInfo.osName, 256, "Windows PC");
    snprintf(gOsInfo.osVersion, 256, "%lu.%lu (Build: %lu)", majorVersion, minorVersion, buildNumber);
    snprintf(gOsInfo.osDeviceName, 256, "Unknown");
    LOGF(LogLevel::eINFO, "Operating System: %s. Version: %s. Device Name: %s.", gOsInfo.osName, gOsInfo.osVersion, gOsInfo.osDeviceName);

#ifdef ENABLE_FORGE_STACKTRACE_DUMP
    if (!WindowsStackTrace::Init())
        return EXIT_FAILURE;
#endif

    pApp = app;
    pWindowAppRef = app;

#ifdef HOLOLENS2
    // init openxr before window system because window system needs info
    // that can only be obtained with openxr instance
    if (!initOpenXR(pApp->GetName()))
        return EXIT_FAILURE;
#endif

    if (!initWindowSystem())
    {
        return EXIT_FAILURE;
    }

    // Used for automated testing, if enabled app will exit after DEFAULT_AUTOMATION_FRAME_COUNT (240) frames
#if defined(AUTOMATED_TESTING)
    uint32_t targetFrameCount = DEFAULT_AUTOMATION_FRAME_COUNT;
#endif

    initCpuInfo(&gCpu);

    IApp::Settings* pSettings = &pApp->mSettings;
    TFWindowDesc    window = {};
    gWindow = &window;                               // WindowsWindow.cpp
    gWindowDesc = &window;                           // WindowsBase.cpp
    gLogWindowHandle = (HWND*)&window.handle.window; // WindowsLog.c, save the address to this handle to avoid having to adding includes to
                                                     // WindowsLog.c to use TFWindowDesc*.

    if (pSettings->mMonitorIndex < 0 || pSettings->mMonitorIndex >= (int)gMonitorCount)
    {
        pSettings->mMonitorIndex = 0;
    }

    if (pSettings->mWidth <= 0 || pSettings->mHeight <= 0)
    {
        TFRectDesc rect = {};
#ifdef HOLOLENS2
        getOpenXRRecommendedResolution(&rect);
#else
        getRecommendedResolution(&rect);
#endif
        pSettings->mWidth = getRectWidth(&rect);
        pSettings->mHeight = getRectHeight(&rect);
    }

    TFMonitorDesc* monitor = getMonitor(pSettings->mMonitorIndex);
    ASSERT(monitor != nullptr);

    gWindow->clientRect = {};
    gWindow->clientRect.left = (int)pSettings->mWindowX + monitor->monitorRect.left;
    gWindow->clientRect.top = (int)pSettings->mWindowY + monitor->monitorRect.top;
    gWindow->clientRect.right = gWindow->clientRect.left + pSettings->mWidth;
    gWindow->clientRect.bottom = gWindow->clientRect.top + pSettings->mHeight;

    gWindow->windowedRect = gWindow->clientRect;
    gWindow->fullScreen = pSettings->mFullScreen;
    gWindow->maximized = false;
    gWindow->noresizeFrame = !pSettings->mDragToResize;
    gWindow->borderlessWindow = pSettings->mBorderlessWindow;
    gWindow->forceLowDPI = pSettings->mForceLowDPI;
    gWindow->overrideDefaultPosition = true;
    gWindow->cursorCaptured = false;

    if (!pSettings->mExternalWindow)
        openWindow(pApp->GetName(), gWindow);

    pSettings->mWidth = gWindow->fullScreen ? getRectWidth(&gWindow->fullscreenRect) : getRectWidth(&gWindow->clientRect);
    pSettings->mHeight = gWindow->fullScreen ? getRectHeight(&gWindow->fullscreenRect) : getRectHeight(&gWindow->clientRect);

    pApp->pCommandLine = GetCommandLineA();

#ifdef AUTOMATED_TESTING
    bool paramRenderingAPIFound = false;
    char benchmarkOutput[1024] = { "\0" };
    // Check if benchmarking was given through command line
    for (int i = 0; i < argc; i += 1)
    {
        if (strcmp(argv[i], "-b") == 0)
        {
            pSettings->mBenchmarking = true;
            if (i + 1 < argc && isdigit(*argv[i + 1]))
                targetFrameCount = min(max(atoi(argv[i + 1]), 32), 512);
        }
        // Run forever, this is useful when the app will control when the automated tests are over
        else if (strcmp(argv[i], "--no-auto-exit") == 0)
        {
            targetFrameCount = UINT32_MAX;
        }
        else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc)
        {
            strncpy_s(benchmarkOutput, sizeof(benchmarkOutput), argv[i + 1], 1024);
        }
        // Allow to set renderer API through command line so that we are able to test the same build with differnt APIs
        // On the TheForge Jenkins setup we change APIs through a lua script that changes the selector variable in the UI,
        // but for projects where we compile without our lua interface we cannot do this.
#if defined(DIRECT3D12)
        else if (strcmp(argv[i], "--d3d12") == 0)
        {
            if (paramRenderingAPIFound)
            {
                LOGF(eERROR, "Two command line parameters are requesting the rendering API, only one is allowed.");
                ASSERT(false);
                return -1;
            }
            paramRenderingAPIFound = true;
        }
#endif
#if defined(VULKAN)
        else if (strcmp(argv[i], "--vulkan") == 0)
        {
            if (paramRenderingAPIFound)
            {
                LOGF(eERROR, "Two command line parameters are requesting the rendering API, only one is allowed.");
                ASSERT(false);
                return -1;
            }
            paramRenderingAPIFound = true;
        }
#endif
    }
#endif

    {
        if (!initBaseSubsystems())
            return EXIT_FAILURE;

        TFTimer t;
        initTimer(&t);
        if (!pApp->Init())
        {
            if (pApp->mUnsupported)
            {
                errorMessagePopup("Application unsupported", pApp->pUnsupportedReason ? pApp->pUnsupportedReason : "",
                                  &pApp->pWindow->handle, NULL);
                exitLog();
                return 0;
            }

            return EXIT_FAILURE;
        }

        setupPlatformUI(pSettings);
        pSettings->mInitialized = true;

        if (!pApp->Load(&gReloadDescriptor))
            return EXIT_FAILURE;
        gReloadDescriptor.mType = (TFReloadType)0;

        LOGF(LogLevel::eINFO, "Application Init+Load+Reload %fms", getTimerMSec(&t, false) / 1000.0f);
    }

#ifdef AUTOMATED_TESTING
    if (pSettings->mBenchmarking)
        setAggregateFrames(targetFrameCount / 2);
#endif

    bool baseSubsystemAppDrawn = false;
    bool quit = false;
    pApp->mSettings.mFrames = 0;
    int64_t lastCounter = getUSec(false);
    while (!quit)
    {
        int64_t counter = getUSec(false);
        float   deltaTime = CounterToSecondsElapsed(lastCounter, counter);
        lastCounter = counter;

#ifdef FORGE_DEBUG
        // if framerate appears to drop below about 6, assume we're at a breakpoint and simulate 20fps.
        if (deltaTime > 0.15f)
            deltaTime = 0.05f;
#endif

#if defined(AUTOMATED_TESTING)
        // Used to keep screenshot results consistent across CI runs
        deltaTime = AUTOMATION_FIXED_FRAME_TIME;
#endif

        bool lastMinimized = gWindow->minimized;
#if !defined(HOLOLENS2)
        extern void platformUpdateLastInputState();
#endif
        platformUpdateLastInputState();

#if defined(HOLOLENS2)
        bool xrSessionStarted = pollOpenXREvent(&quit);
        bool xrFrameBegun = false;
        bool xrShouldRender = false;
        if (xrSessionStarted)
        {
            xrShouldRender = beginOpenXRFrame();
            xrFrameBegun = true;
        }
#endif

        extern bool handleMessages();
        quit |= handleMessages() || pSettings->mQuit;

        // UPDATE BASE INTERFACES
        updateBaseSubsystems(deltaTime, baseSubsystemAppDrawn);
        baseSubsystemAppDrawn = false;

        if (gResetDescriptor.mType != TF_RESET_TYPE_NONE)
        {
#if defined(HOLOLENS2)
            if (xrFrameBegun)
                endFrameOpenXRNOP();
#endif
            if (gResetDescriptor.mType & TF_RESET_TYPE_GRAPHIC_CARD_SWITCH)
            {
                ASSERT(gGpuSelection.mSelectedGpuIndex < gGpuSelection.mAvailableGpuCount);
                gGpuSelection.mPreferedGpuId = gGpuSelection.pAvailableGpuIds[gGpuSelection.mSelectedGpuIndex];
            }

            gReloadDescriptor.mType = (TFReloadType)(TF_RELOAD_TYPE_RESIZE | TF_RELOAD_TYPE_SHADER | TF_RELOAD_TYPE_RENDERTARGET);
            pApp->Unload(&gReloadDescriptor);
            pApp->Exit();

            pSettings->mInitialized = false;

            closeWindow(app->pWindow);
            openWindow(app->GetName(), app->pWindow);

            exitBaseSubsystems();

            {
                if (!initBaseSubsystems())
                    return EXIT_FAILURE;

                TFTimer t;
                initTimer(&t);
                if (!pApp->Init())
                {
                    if (pApp->mUnsupported)
                    {
                        errorMessagePopup("Application unsupported", pApp->pUnsupportedReason ? pApp->pUnsupportedReason : "",
                                          &pApp->pWindow->handle, NULL);
                        exitLog();
                        return 0;
                    }
                    return EXIT_FAILURE;
                }

                setupPlatformUI(pSettings);
                pSettings->mInitialized = true;

                if (!pApp->Load(&gReloadDescriptor))
                    return EXIT_FAILURE;
                gReloadDescriptor.mType = (TFReloadType)0;

                LOGF(LogLevel::eINFO, "Application Reset %fms", getTimerMSec(&t, false) / 1000.0f);
            }

            gResetDescriptor.mType = TF_RESET_TYPE_NONE;
            continue;
        }

        if (gReloadDescriptor.mType)
        {
#if defined(HOLOLENS2)
            if (xrFrameBegun)
                endFrameOpenXRNOP();
#endif
            TFTimer t;
            initTimer(&t);

            pApp->Unload(&gReloadDescriptor);
            if (!pApp->Load(&gReloadDescriptor))
                return EXIT_FAILURE;

            LOGF(LogLevel::eINFO, "Application Reload %fms", getTimerMSec(&t, false) / 1000.0f);
            gReloadDescriptor.mType = (TFReloadType)0;
            continue;
        }

#ifdef ENABLE_FORGE_UI
        updatePlatformUI();
        extern void updateProfilerUI();
        updateProfilerUI();
#endif

        // If window is minimized let other processes take over
        if (gWindow->minimized)
        {
#if defined(HOLOLENS2)
            if (xrFrameBegun)
                endFrameOpenXRNOP();
#endif
            // Call update once after minimize so app can react.
            if (lastMinimized != gWindow->minimized)
            {
                pApp->Update(deltaTime);
            }
            threadSleep(1);
            continue;
        }

        // UPDATE APP
        pApp->Update(deltaTime);
        // Ensure no reload types have been requested in Update() before proceeding to Draw()
#ifdef HOLOLENS2
        if (xrFrameBegun)
        {
            if (xrShouldRender && !gReloadDescriptor.mType)
            {
#else
        if (!gReloadDescriptor.mType)
        {
#endif
                pApp->Draw();
                pApp->mSettings.mFrames++;
                pApp->mSettings.mFrameIdx = pApp->mSettings.mFrames % pApp->mSettings.mFrameMaxCount;
                baseSubsystemAppDrawn = true;
#ifdef HOLOLENS2
            }
            else
            {
                endFrameOpenXRNOP();
            }
        }
#else
        }
#endif

#if defined(ENABLE_FORGE_RELOAD_SHADER)
        platformUpdateReloadClient();
#endif

#ifdef AUTOMATED_TESTING
        extern bool gAutomatedTestingScriptsFinished;
        // wait for the automated testing if it hasn't managed to finish in time
        if (gAutomatedTestingScriptsFinished && (uint32_t)(pApp->mSettings.mFrames) >= targetFrameCount)
            quit = true;
#endif
    }

#ifdef AUTOMATED_TESTING
    if (pSettings->mBenchmarking)
    {
        dumpBenchmarkData(pSettings, benchmarkOutput, pApp->GetName());
        dumpProfileData(benchmarkOutput, targetFrameCount);
    }
#endif

    gReloadDescriptor.mType = (TFReloadType)(TF_RELOAD_TYPE_RESIZE | TF_RELOAD_TYPE_SHADER | TF_RELOAD_TYPE_RENDERTARGET);
    pApp->mSettings.mQuit = true;
    pApp->Unload(&gReloadDescriptor);
    pApp->Exit();

#ifdef ENABLE_FORGE_STACKTRACE_DUMP
    WindowsStackTrace::Exit();
#endif

    exitBaseSubsystems();

    exitWindowSystem();

    exitLog();

    exitFileSystem();

    exitMemAlloc();

    gWindow = NULL;
    gWindowDesc = NULL;
    gLogWindowHandle = NULL;
    return 0;
}
#endif
