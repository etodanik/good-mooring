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

#include "../../Application/Config.h"

#include "../../Application/Interfaces/IApp.h"
#include "../../Application/Interfaces/IScreenshot.h"
#include "../../Application/Interfaces/IUI.h"
#include "../../Game/Interfaces/IScripting.h"
#include "../../OS/Interfaces/IOperatingSystem.h"
#include "../../Utilities/Interfaces/ITime.h"

#define MAX_WINDOW_RES_COUNT                     64
#define MAX_WINDOW_RES_STR_LENGTH                64

#define RECOMMENDED_WINDOW_SIZE_FACTOR           8
#define RECOMMENDED_WINDOW_SIZE_DISPLAY_FRACTION 0.75

static TFWindowDesc* pWindowRef = NULL;

static char    gPlatformNameBuffer[64];
static bstring gPlatformName = bemptyfromarr(gPlatformNameBuffer);

#if defined(_WINDOWS) || defined(__APPLE__) && !defined(TARGET_IOS) || (defined(__linux__) && !defined(__ANDROID__))
#define WINDOW_UI_ENABLED
#endif

// UI Window globals
static TFUIWindowDesc gWindowControlDesc;
#if defined(WINDOW_UI_ENABLED)
static char*    pWindowResNamePtrs[MAX_WINDOW_RES_COUNT + 1];
static int      gWindowResNameCount;
static uint32_t gClientRectWidth;
static uint32_t gClientRectHeight;
static uint32_t gNumDisplays;
static char     gNumDisplaysLabel[64];
static char*    gMonitorLabels;
static char**   gMonitorResLabels;

static uint32_t gWindowSize = 0;
static uint32_t gWindowPrevSize = 0;
static uint32_t gPrevActiveMonitor;

// UI Monitor globals

static int32_t gMonitorResolution[TF_MAX_MONITOR_COUNT] = {};
static int32_t gMonitorLastResolution[TF_MAX_MONITOR_COUNT] = {};

#endif

#if defined(ENABLE_FORGE_UI) && defined(ENABLE_FORGE_SCRIPTING)
// Static UI function prototypes
static void registerLuaWidgets();
#endif

#if !defined(TARGET_IOS) && !defined(TARGET_IOS_SIMULATOR)
void getRecommendedResolution(TFRectDesc* rect)
{
    uint32_t monitorIdx = getActiveMonitorIdx();

    TFMonitorDesc* pMonitor = getMonitor(monitorIdx);

    // Early exit
    if (arrlenu(pMonitor->resolutions) == 1)
    {
        rect->left = 0;
        rect->top = 0;
        rect->right = (int32_t)pMonitor->resolutions[0].mWidth;
        rect->bottom = (int32_t)pMonitor->resolutions[0].mHeight;
        return;
    }

    ASSERT(pMonitor->resolutions && pMonitor->currentResolution < arrlen(pMonitor->resolutions));

    uint32_t monitorWidth = (int32_t)pMonitor->resolutions[pMonitor->currentResolution].mWidth;
    uint32_t monitorHeight = (int32_t)pMonitor->resolutions[pMonitor->currentResolution].mHeight;
    int32_t  desiredWidth = (int32_t)(monitorWidth * RECOMMENDED_WINDOW_SIZE_DISPLAY_FRACTION);
    int32_t  desiredHeight = (int32_t)(monitorHeight * RECOMMENDED_WINDOW_SIZE_DISPLAY_FRACTION);

    *rect = { 0, 0, desiredWidth, desiredHeight };
}
#endif

#if defined(WINDOW_UI_ENABLED)

static bool wndValidateClientRectPos(int32_t x, int32_t y)
{
    TFWindowDesc* winDesc = pWindowRef;
    if ((abs(x - winDesc->clientRect.left) > 1) || (abs(y - winDesc->clientRect.top) > 1))
        return false;

    return true;
}

static bool wndValidateClientRectSize(int32_t width, int32_t height)
{
    if ((abs(getRectWidth(&pWindowRef->clientRect) - width) > 1) || (abs(getRectHeight(&pWindowRef->clientRect) - height) > 1))
        return false;
    return true;
}

static void wndUpdateResolutionsList()
{
    if (!pWindowRef)
    {
        gWindowResNameCount = 0;
    }
    else
    {
        uint32_t       monitorIdx = getActiveMonitorIdx();
        TFMonitorDesc* pMonitor = getMonitor(monitorIdx);
        uint32_t       monitorResolutionIdx = gMonitorResolution[monitorIdx];
        ASSERT(pMonitor->resolutions && monitorResolutionIdx < arrlen(pMonitor->resolutions));
        TFResolution monitorResolution = pMonitor->resolutions[monitorResolutionIdx];

        if (pWindowRef->fullScreen)
        {
            sprintf(pWindowResNamePtrs[0], "%ux%u", monitorResolution.mWidth, monitorResolution.mHeight);
            gWindowResNameCount = 1;
            gWindowSize = 0;
        }
        else
        {
            uint32_t activeResIdx = MAX_WINDOW_RES_COUNT;

            gWindowResNameCount = (int)arrlen(pMonitor->resolutions) - 1;

            for (int i = 0; i < gWindowResNameCount; ++i)
            {
                monitorResolution = pMonitor->resolutions[i];
                sprintf(pWindowResNamePtrs[i], "%ux%u", monitorResolution.mWidth, monitorResolution.mHeight);

                if (pWindowRef->mWndW == (int32_t)monitorResolution.mWidth && pWindowRef->mWndH == (int32_t)monitorResolution.mHeight)
                {
                    activeResIdx = (uint32_t)i;
                }
            }

            gWindowSize = activeResIdx;
        }
    }
}

#ifdef ENABLE_FORGE_UI

void wndSetWindowed(void* pUserData)
{
    UNREF_PARAM(pUserData);
    setWindowed(pWindowRef);
    wndUpdateResolutionsList();
}

void wndSetFullscreen(void* pUserData)
{
    UNREF_PARAM(pUserData);
    setFullscreen(pWindowRef);
    wndUpdateResolutionsList();
}

void wndSetBorderless(void* pUserData)
{
    UNREF_PARAM(pUserData);
    setBorderless(pWindowRef);
    wndUpdateResolutionsList();
}

void wndMaximizeWindow(void* pUserData)
{
    UNREF_PARAM(pUserData);
    TFWindowDesc* pWindow = pWindowRef;

    maximizeWindow(pWindow);
}

void wndMinimizeWindow(void* pUserData)
{
    UNREF_PARAM(pUserData);
    pWindowRef->mMinimizeRequested = true;
}
void wndCenterWindow(void* pUserData)
{
    UNREF_PARAM(pUserData);
    centerWindow(pWindowRef);
}

void wndHideWindow()
{
    TFWindowDesc* pWindow = pWindowRef;

    hideWindow(pWindow);
}

void wndShowWindow()
{
    TFWindowDesc* pWindow = pWindowRef;

    showWindow(pWindow);
}

void wndMoveWindow(void* pUserData)
{
    UNREF_PARAM(pUserData);
    TFWindowDesc* pWindow = pWindowRef;
    int32_t       expectedClientWidth = pWindowRef->mWndW;
    int32_t       expectedClientHeight = pWindowRef->mWndH;
    int32_t       expectedClientX = pWindowRef->mWndX;
    int32_t       expectedClientY = pWindowRef->mWndY;

    if (pWindow->mWindowMode == TF_WM_FULLSCREEN)
    {
        setWindowed(pWindow);
    }
    wndUpdateResolutionsList();
    TFRectDesc rectDesc{ pWindowRef->mWndX, pWindowRef->mWndY, pWindowRef->mWndX + pWindowRef->mWndW,
                         pWindowRef->mWndY + pWindowRef->mWndH };
    setWindowClientRect(pWindow, &rectDesc);
    LOGF(LogLevel::eINFO, "MoveWindow() Position check: %s",
         wndValidateClientRectPos(expectedClientX, expectedClientY) ? "SUCCESS" : "FAIL");
    LOGF(LogLevel::eINFO, "MoveWindow() Size check: %s",
         wndValidateClientRectSize(expectedClientWidth, expectedClientHeight) ? "SUCCESS" : "FAIL");
}

void wndSetRecommendedWindowSize(void* pUserData)
{
    UNREF_PARAM(pUserData);
    TFWindowDesc* pWindow = pWindowRef;

    wndSetWindowed(NULL);

    TFRectDesc rect;
    getRecommendedWindowRect(pWindowRef, &rect);

    setWindowRect(pWindow, &rect);
}

void wndHideCursor()
{
    pWindowRef->mCursorHidden = true;
    hideCursor();
}

void wndShowCursor()
{
    pWindowRef->mCursorHidden = false;
    showCursor();
}

void wndUpdateCaptureCursor(void* pUserData)
{
    UNREF_PARAM(pUserData);
#ifdef ENABLE_FORGE_INPUT
    captureCursor(pWindowRef, pWindowRef->mCursorCaptured);
#endif
}

static void wndUpdateResolution()
{
    uint32_t activeMonitorIdx = getActiveMonitorIdx();

    if (gWindowSize == gWindowPrevSize && activeMonitorIdx == gPrevActiveMonitor)
    {
        return;
    }

    if (activeMonitorIdx != gPrevActiveMonitor)
    {
        wndUpdateResolutionsList();
    }
    gPrevActiveMonitor = activeMonitorIdx;

    TFMonitorDesc* pMonitor = getMonitor(activeMonitorIdx);

    gWindowPrevSize = gWindowSize;
    setWindowClientSize(pWindowRef, pMonitor->resolutions[gWindowSize].mWidth, pMonitor->resolutions[gWindowSize].mHeight);
}

static void wndSelectRecommendedWindowResolution(const TFMonitorDesc* pMonitor)
{
    ASSERT(pMonitor);
    TFRectDesc recommendedRect = {};
    getRecommendedResolution(&recommendedRect);
    const uint32_t recommendedWidth = recommendedRect.right - recommendedRect.left;
    const uint32_t recommendedHeight = recommendedRect.bottom - recommendedRect.top;

    uint32_t resolutionIndex = 0;
    for (; resolutionIndex < arrlen(pMonitor->resolutions) && resolutionIndex < pMonitor->currentResolution; ++resolutionIndex)
    {
        if (pMonitor->resolutions[resolutionIndex].mWidth == recommendedWidth &&
            pMonitor->resolutions[resolutionIndex].mHeight == recommendedHeight)
        {
            break;
        }
    }

    if (resolutionIndex >= arrlen(pMonitor->resolutions))
    {
        resolutionIndex = pMonitor->currentResolution > 0 ? pMonitor->currentResolution - 1 : pMonitor->currentResolution;
    }

    gWindowSize = resolutionIndex;
    wndUpdateResolution();
}

static void monitorUpdateResolutionUICallback()
{
    uint32_t monitorCount = getMonitorCount();
    for (uint32_t i = 0; i < monitorCount; ++i)
    {
        if (gMonitorResolution[i] != gMonitorLastResolution[i])
        {
            TFMonitorDesc* pMonitor = getMonitor(i);
            setResolution(pMonitor, &pMonitor->resolutions[gMonitorResolution[i]]);

            gMonitorLastResolution[i] = gMonitorResolution[i];
            if (i == getActiveMonitorIdx() && !pWindowRef->fullScreen)
            {
                wndUpdateResolutionsList();
                wndSelectRecommendedWindowResolution(getMonitor(i));
            }
        }
    }
}
#endif
#endif

extern "C" TFResolution gAvailableSceneResolutions[];
extern "C" uint32_t     gAvailableSceneResolutionCount;
extern "C" uint32_t     gSceneResolutionIndex;

#ifdef ENABLE_FORGE_UI
void platformUpdateWindowSystemUI()
{
    if (UI_WINDOW_IS_VISIBLE(uiBeginWidgetWindow(&gWindowControlDesc)))
    {
        uiLayoutAutoTextRows(2);
        uiLabel("Platform Name:", TF_ALIGN_LEFT);
        uiText(&gPlatformName, TF_ALIGN_LEFT);

#ifdef WINDOW_UI_ENABLED
        uiLayoutAutoTextRows(1);

        /// WINDOW MODE
        bool isWindowed = false;
        bool isFullscreen = false;
        bool isBorderless = false;
        switch (pWindowRef->mWindowMode)
        {
        case TF_WM_WINDOWED:
            isWindowed = true;
            break;
        case TF_WM_FULLSCREEN:
            isFullscreen = true;
            break;
        case TF_WM_BORDERLESS:
            isBorderless = true;
            break;
        }
        if (UI_WIDGET_IS_CHANGED(uiRadioButton("Windowed", &isWindowed)) && isWindowed)
        {
            pWindowRef->mWindowMode = TF_WM_WINDOWED;
            wndSetWindowed(NULL);
        }
        if (UI_WIDGET_IS_CHANGED(uiRadioButton("Fullscreen", &isFullscreen)) && isFullscreen)
        {
            pWindowRef->mWindowMode = TF_WM_FULLSCREEN;
            wndSetFullscreen(NULL);
        }
        if (UI_WIDGET_IS_CHANGED(uiRadioButton("Borderless", &isBorderless)) && isBorderless)
        {
            pWindowRef->mWindowMode = TF_WM_BORDERLESS;
            wndSetBorderless(NULL);
        }

        if (UI_WIDGET_IS_PRESSED(uiButton("Maximize")))
        {
            wndMaximizeWindow(NULL);
        }
        if (UI_WIDGET_IS_PRESSED(uiButton("Minimize")))
        {
            wndMinimizeWindow(NULL);
        }
        if (UI_WIDGET_IS_PRESSED(uiButton("Center")))
        {
            wndCenterWindow(NULL);
        }

        /// CLIENT RECTANGLE
        if (UI_WIDGET_IS_VISIBLE(uiCollapsingHeaderBegin("Client Rectangle", true)))
        {
            uiLayoutAutoTextRows(2);
            uiLabel("X Offset", TF_ALIGN_LEFT);
            uiSliderInt(&pWindowRef->mWndX, 0, gClientRectWidth, 1);
            uiLabel("Y Offset", TF_ALIGN_LEFT);
            uiSliderInt(&pWindowRef->mWndY, 0, gClientRectHeight, 1);
            uiLabel("Width", TF_ALIGN_LEFT);
            uiSliderInt(&pWindowRef->mWndW, 144, getRectWidth(&pWindowRef->fullscreenRect), 1);
            uiLabel("Height", TF_ALIGN_LEFT);
            uiSliderInt(&pWindowRef->mWndH, 144, getRectHeight(&pWindowRef->fullscreenRect), 1);

            uiLayoutAutoTextRows(1);
            if (UI_WIDGET_IS_PRESSED(uiButton("Set client rectangle")))
            {
                wndMoveWindow(NULL);
            }
            if (UI_WIDGET_IS_PRESSED(uiButton("Set recommended client rectangle")))
            {
                wndSetRecommendedWindowSize(NULL);
            }
            uiCollapsingHeaderEnd();
        }

        // WINDOW DETAILS
#if WINDOW_DETAILS
        if (UI_WIDGET_IS_VISIBLE(uiCollapsingHeaderBegin("Window Details", true)))
        {
            uiText(&pWindowRef->pWindowedRectLabel, TF_ALIGN_LEFT);
            uiText(&pWindowRef->pFullscreenRectLabel, TF_ALIGN_LEFT);
            uiText(&pWindowRef->pClientRectLabel, TF_ALIGN_LEFT);
            uiText(&pWindowRef->pWndLabel, TF_ALIGN_LEFT);
            uiText(&pWindowRef->pFullscreenLabel, TF_ALIGN_LEFT);
            uiText(&pWindowRef->pCursorCapturedLabel, TF_ALIGN_LEFT);
            uiText(&pWindowRef->pIconifiedLabel, TF_ALIGN_LEFT);
            uiText(&pWindowRef->pMaximizedLabel, TF_ALIGN_LEFT);
            uiText(&pWindowRef->pMinimizedLabel, TF_ALIGN_LEFT);
            uiText(&pWindowRef->pNoResizeFrameLabel, TF_ALIGN_LEFT);
            uiText(&pWindowRef->pBorderlessWindowLabel, TF_ALIGN_LEFT);
            uiText(&pWindowRef->pOverrideDefaultPositionLabel, TF_ALIGN_LEFT);
            uiText(&pWindowRef->pForceLowDPILabel, TF_ALIGN_LEFT);
            uiText(&pWindowRef->pWindowModeLabel, TF_ALIGN_LEFT);

            uiCollapsingHeaderEnd();
        }
#endif

        /// MONITOR RESOLUTION SELECTION HEADERS
        uiLabel(gNumDisplaysLabel, TF_ALIGN_LEFT);
        for (uint32_t i = 0; i < gNumDisplays; ++i)
        {
            const uint32_t monitorLabelIdx = i * MAX_LABEL_STR_LENGTH;
            if (UI_WIDGET_IS_VISIBLE(uiCollapsingHeaderBegin(gMonitorLabels + monitorLabelIdx, true)))
            {
                TFMonitorDesc* monitor = getMonitor(i);
                uint32_t       monitorResCount = (uint32_t)arrlenu(monitor->resolutions);

                for (uint32_t j = 0; j < monitorResCount; ++j)
                {
                    const uint32_t monitorResLabelIdx = j * MAX_LABEL_STR_LENGTH;
                    bool           resSelected = gMonitorResolution[i] == (int32_t)j;
                    if (!resSelected && UI_WIDGET_IS_CHANGED(uiRadioButton(gMonitorResLabels[i] + monitorResLabelIdx, &resSelected)))
                    {
                        gMonitorResolution[i] = (int32_t)j;
                        monitorUpdateResolutionUICallback();
                    }
                }

                uiCollapsingHeaderEnd();
            }
        }

        /// RESOLUTION DROPDOWN
        uint32_t activeMonitorIdx = getActiveMonitorIdx();
        if (gPrevActiveMonitor != activeMonitorIdx)
        {
            wndUpdateResolutionsList();
            gPrevActiveMonitor = activeMonitorIdx;
        }
        uiLabel("Window resolution", TF_ALIGN_LEFT);
        uint32_t selectedRes = UI_WIDGET_GET_SELECTED(uiDropdown(pWindowResNamePtrs, gWindowResNameCount, gWindowSize));
        if (selectedRes != gWindowSize)
        {
            gWindowSize = selectedRes;
            wndUpdateResolution();
        }

        /// INPUT CONTROLS
        if (UI_WIDGET_IS_VISIBLE(uiCollapsingHeaderBegin("Cursor", true)))
        {
            uiCheckbox("Cursor inside window?", &pWindowRef->mCursorInsideWindow);

            if (UI_WIDGET_IS_CHANGED(uiCheckbox("Capture cursor", &pWindowRef->mCursorCaptured)))
            {
                wndUpdateCaptureCursor(NULL);
            }

            uiCollapsingHeaderEnd();
        }
#endif

        /// SCENE RESOLUTION OPTIONS
        if (gAvailableSceneResolutionCount && UI_WIDGET_IS_VISIBLE(uiCollapsingHeaderBegin("Scene Resolutions Options", true)))
        {
            static char        sceneResLabels[64][TF_MAX_DEBUG_NAME_LENGTH + 1] = {};
            static const char* sceneResLabelPtr[64] = {};

            for (uint32_t r = 0; r < gAvailableSceneResolutionCount; ++r)
            {
                const TFResolution* res = &gAvailableSceneResolutions[r];
                if (res->mWidth && res->mHeight)
                {
                    snprintf(sceneResLabels[r], TF_MAX_DEBUG_NAME_LENGTH, "%ux%u", res->mWidth, res->mHeight);
                }
                else
                {
                    strncpy(sceneResLabels[r], "native", TF_MAX_DEBUG_NAME_LENGTH);
                }
                sceneResLabelPtr[r] = sceneResLabels[r];
            }

            uiLabel("Scene resolutions", TF_ALIGN_LEFT);
            uint32_t selectedSceneRes =
                UI_WIDGET_GET_SELECTED(uiDropdown(sceneResLabelPtr, gAvailableSceneResolutionCount, gSceneResolutionIndex));
            if (selectedSceneRes != gSceneResolutionIndex)
            {
                gSceneResolutionIndex = selectedSceneRes;

                TFReloadDesc reloadDesc = {};
                reloadDesc.mType = TF_RELOAD_TYPE_RESIZE;
                requestReload(&reloadDesc);
            }

            uiCollapsingHeaderEnd();
        }
    }
    uiEndWidgetWindow();
}
#endif

void platformInitWindowSystem(TFWindowDesc* pData)
{
    ASSERT(pWindowRef == NULL);

    pWindowRef = pData;

#if WINDOW_DETAILS
    pWindowRef->pWindowedRectLabel = bempty();
    pWindowRef->pFullscreenRectLabel = bempty();
    pWindowRef->pClientRectLabel = bempty();
    pWindowRef->pWndLabel = bempty();
    pWindowRef->pFullscreenLabel = bempty();
    pWindowRef->pCursorCapturedLabel = bempty();
    pWindowRef->pIconifiedLabel = bempty();
    pWindowRef->pMaximizedLabel = bempty();
    pWindowRef->pMinimizedLabel = bempty();
    pWindowRef->pNoResizeFrameLabel = bempty();
    pWindowRef->pBorderlessWindowLabel = bempty();
    pWindowRef->pOverrideDefaultPositionLabel = bempty();
    pWindowRef->pForceLowDPILabel = bempty();
    pWindowRef->pWindowModeLabel = bempty();
#endif
}

void platformExitWindowSystem()
{
#if WINDOW_DETAILS
    bdestroy(&pWindowRef->pWindowedRectLabel);
    bdestroy(&pWindowRef->pFullscreenRectLabel);
    bdestroy(&pWindowRef->pClientRectLabel);
    bdestroy(&pWindowRef->pWndLabel);
    bdestroy(&pWindowRef->pFullscreenLabel);
    bdestroy(&pWindowRef->pCursorCapturedLabel);
    bdestroy(&pWindowRef->pIconifiedLabel);
    bdestroy(&pWindowRef->pMaximizedLabel);
    bdestroy(&pWindowRef->pMinimizedLabel);
    bdestroy(&pWindowRef->pNoResizeFrameLabel);
    bdestroy(&pWindowRef->pBorderlessWindowLabel);
    bdestroy(&pWindowRef->pOverrideDefaultPositionLabel);
    bdestroy(&pWindowRef->pForceLowDPILabel);
    bdestroy(&pWindowRef->pWindowModeLabel);
#endif

#if defined(ENABLE_FORGE_UI) && defined(WINDOW_UI_ENABLED)
    for (uint32_t i = 0; i < gNumDisplays; ++i)
    {
        tf_free(gMonitorResLabels[i]);
    }
    tf_free(gMonitorResLabels);
    tf_free(gMonitorLabels);
#endif

    pWindowRef = NULL;
}

void platformUpdateWindowSystem()
{
    pWindowRef->mCursorInsideWindow = isCursorInsideTrackingArea();

    if (pWindowRef->mMinimizeRequested)
    {
        minimizeWindow(pWindowRef);
        pWindowRef->mMinimizeRequested = false;
    }

#if WINDOW_DETAILS
    bdestroy(&pWindowRef->pWindowedRectLabel);
    bformat(&pWindowRef->pWindowedRectLabel, "WindowedRect L: %d, T: %d, R: %d, B: %d", pWindowRef->windowedRect.left,
            pWindowRef->windowedRect.top, pWindowRef->windowedRect.right, pWindowRef->windowedRect.bottom);
    bdestroy(&pWindowRef->pFullscreenRectLabel);
    bformat(&pWindowRef->pFullscreenRectLabel, "FullscreenRect L: %d, T: %d, R: %d, B: %d", pWindowRef->fullscreenRect.left,
            pWindowRef->fullscreenRect.top, pWindowRef->fullscreenRect.right, pWindowRef->fullscreenRect.bottom);
    bdestroy(&pWindowRef->pClientRectLabel);
    bformat(&pWindowRef->pClientRectLabel, "ClientRect L: %d, T: %d, R: %d, B: %d", pWindowRef->clientRect.left, pWindowRef->clientRect.top,
            pWindowRef->clientRect.right, pWindowRef->clientRect.bottom);
    bdestroy(&pWindowRef->pWndLabel);
    bformat(&pWindowRef->pWndLabel, "Wnd X: %d, Y: %d, W: %d, H: %d", pWindowRef->mWndX, pWindowRef->mWndY, pWindowRef->mWndW,
            pWindowRef->mWndH);
    bdestroy(&pWindowRef->pFullscreenLabel);
    bformat(&pWindowRef->pFullscreenLabel, "Fullscreen: %s", pWindowRef->fullScreen ? "True" : "False");
    bdestroy(&pWindowRef->pCursorCapturedLabel);
    bformat(&pWindowRef->pCursorCapturedLabel, "CursorCaptured: %s", pWindowRef->cursorCaptured ? "True" : "False");
    bdestroy(&pWindowRef->pIconifiedLabel);
    bformat(&pWindowRef->pIconifiedLabel, "Iconified: %s", pWindowRef->iconified ? "True" : "False");
    bdestroy(&pWindowRef->pMaximizedLabel);
    bformat(&pWindowRef->pMaximizedLabel, "Maximized: %s", pWindowRef->maximized ? "True" : "False");
    bdestroy(&pWindowRef->pMinimizedLabel);
    bformat(&pWindowRef->pMinimizedLabel, "Minimized: %s", pWindowRef->minimized ? "True" : "False");
    bdestroy(&pWindowRef->pNoResizeFrameLabel);
    bformat(&pWindowRef->pNoResizeFrameLabel, "NoResizeFrame: %s", pWindowRef->noresizeFrame ? "True" : "False");
    bdestroy(&pWindowRef->pBorderlessWindowLabel);
    bformat(&pWindowRef->pBorderlessWindowLabel, "BorderlessWindow: %s", pWindowRef->borderlessWindow ? "True" : "False");
    bdestroy(&pWindowRef->pOverrideDefaultPositionLabel);
    bformat(&pWindowRef->pOverrideDefaultPositionLabel, "OverrideDefaultPosition: %s",
            pWindowRef->overrideDefaultPosition ? "True" : "False");
    bdestroy(&pWindowRef->pForceLowDPILabel);
    bformat(&pWindowRef->pForceLowDPILabel, "ForceLowDPI: %s", pWindowRef->forceLowDPI ? "True" : "False");
    bdestroy(&pWindowRef->pWindowModeLabel);
    bformat(&pWindowRef->pWindowModeLabel, "WindowMode: %s",
            pWindowRef->mWindowMode == TF_WM_BORDERLESS ? "Borderless"
                                                        : (pWindowRef->mWindowMode == TF_WM_FULLSCREEN ? "Fullscreen" : "Windowed"));
#endif
}

void platformSetupWindowSystemUI(IApp* pApp)
{
#ifdef ENABLE_FORGE_UI

#if defined(_WINDOWS)
    bassignliteral(&gPlatformName, "Windows");
#elif defined(__APPLE__) && !defined(TARGET_IOS)
    bassignliteral(&gPlatformName, "MacOS");
#elif defined(__linux__) && !defined(__ANDROID__)
    bassignliteral(&gPlatformName, "Linux");
#else
    bassignliteral(&gPlatformName, "Unsupported");
#endif

    gWindowControlDesc.pWindowTitle = "Window and Resolution Controls";
    gWindowControlDesc.mStartPos = { pApp->mSettings.mWidth * 0.775f, pApp->mSettings.mHeight * 0.01f };
    gWindowControlDesc.mStartSize = vec2(400.f, 750.f);
    gWindowControlDesc.mFlags = TF_UI_WINDOW_MINIMIZABLE | TF_UI_WINDOW_MOVABLE | TF_UI_WINDOW_SCALABLE | TF_UI_WINDOW_BORDER |
                                TF_UI_WINDOW_INIT_HEIGHT_FIT | TF_UI_WINDOW_INIT_MINIMIZED;

#if defined(WINDOW_UI_ENABLED)

    TFRectDesc recRes;
    getRecommendedResolution(&recRes);

    gClientRectWidth = recRes.right - recRes.left;
    gClientRectHeight = recRes.bottom - recRes.top;

    static char windowResNames[MAX_WINDOW_RES_COUNT + 1][MAX_WINDOW_RES_STR_LENGTH];
    memset(windowResNames, 0, sizeof(windowResNames));

    for (int32_t i = 0; i < MAX_WINDOW_RES_COUNT + 1; ++i)
    {
        pWindowResNamePtrs[i] = &windowResNames[i][0];
    }

    gWindowSize = 0;
    gWindowPrevSize = 0;
    gWindowResNameCount = 0;

    gNumDisplays = getMonitorCount();

    snprintf(gNumDisplaysLabel, 64, "Number of displays: ");

    char monitors[10];
    snprintf(monitors, 10, "%u", gNumDisplays);
    strcat(gNumDisplaysLabel, monitors);

    gMonitorLabels = (char*)tf_malloc((size_t)gNumDisplays * MAX_LABEL_STR_LENGTH);
    gMonitorResLabels = (char**)tf_malloc((size_t)gNumDisplays * sizeof(char*));

    for (uint32_t i = 0; i < gNumDisplays; ++i)
    {
        TFMonitorDesc* monitor = getMonitor(i);
        uint32_t       monitorResCount = (uint32_t)arrlenu(monitor->resolutions);

        char publicDisplayName[128];
#if defined(_WINDOWS) || defined(XBOX) // Win platform uses wide chars
        if (128 == wcstombs(publicDisplayName, monitor->publicDisplayName, sizeof(publicDisplayName)))
            publicDisplayName[127] = '\0';
#elif !defined(TARGET_IOS)
        strcpy(publicDisplayName, monitor->publicDisplayName);
#endif
        const uint32_t monitorLabelIdx = i * MAX_LABEL_STR_LENGTH;
        snprintf(gMonitorLabels + monitorLabelIdx, 128, "%s", publicDisplayName);
        strcat(gMonitorLabels + monitorLabelIdx, " (");

        char buffer[10];
        snprintf(buffer, 10, "%u", monitor->physicalSize[0]);
        strcat(gMonitorLabels + monitorLabelIdx, buffer);
        strcat(gMonitorLabels + monitorLabelIdx, "x");

        snprintf(buffer, 10, "%u", monitor->physicalSize[1]);
        strcat(gMonitorLabels + monitorLabelIdx, buffer);
        strcat(gMonitorLabels + monitorLabelIdx, " mm; ");

        snprintf(buffer, 10, "%u", monitor->dpi[0]);
        strcat(gMonitorLabels + monitorLabelIdx, buffer);
        strcat(gMonitorLabels + monitorLabelIdx, " dpi; ");

        snprintf(buffer, 10, "%u", monitorResCount);
        strcat(gMonitorLabels + monitorLabelIdx, buffer);
        strcat(gMonitorLabels + monitorLabelIdx, " resolutions)");

        gMonitorResLabels[i] = (char*)tf_malloc((size_t)monitorResCount * MAX_LABEL_STR_LENGTH);

        for (uint32_t j = 0; j < monitorResCount; ++j)
        {
            TFResolution res = monitor->resolutions[j];

            const uint32_t monitorResLabelIdx = j * MAX_LABEL_STR_LENGTH;
            snprintf(gMonitorResLabels[i] + monitorResLabelIdx, MAX_LABEL_STR_LENGTH, "%u", res.mWidth);
            strcat(gMonitorResLabels[i] + monitorResLabelIdx, "x");

            char height[10];
            snprintf(height, 10, "%u", res.mHeight);
            strcat(gMonitorResLabels[i] + monitorResLabelIdx, height);

            if (monitor->defaultResolution.mWidth == res.mWidth && monitor->defaultResolution.mHeight == res.mHeight)
            {
                strcat(gMonitorResLabels[i] + monitorResLabelIdx, " (native)");
                gMonitorResolution[i] = (int32_t)j;
                gMonitorLastResolution[i] = (int32_t)j;
            }
        }

        if (i == getActiveMonitorIdx())
        {
            // Update window resolutions list after display resolutions are populated
            wndUpdateResolutionsList();
        }
    }
#else // WINDOW_UI_ENABLED
    (void)pApp;
#endif

#ifdef ENABLE_FORGE_SCRIPTING
    registerLuaWidgets();
#endif

#else // ENABLE_FORGE_UI
    (void)pApp;
#endif
}

void platformToggleWindowSystemUI(bool active)
{
#ifdef WINDOW_UI_ENABLED
    uiUpdateWindowVisibilityByTitle(gWindowControlDesc.pWindowTitle, active);
#else
    (void)active;
#endif
}

#if defined(ENABLE_FORGE_UI) && defined(ENABLE_FORGE_SCRIPTING)
static void registerLuaWidgets()
{
    TFLuaWidgetVariableDesc luaPlatformNameDesc;
    luaPlatformNameDesc.pLabel = "Platform Name";
    luaPlatformNameDesc.mWidgetType = TF_WIDGET_TYPE_TEXTBOX;
    luaPlatformNameDesc.pText = &gPlatformName;
    luaRegisterWidgetVariable(&luaPlatformNameDesc);

#if defined(WINDOW_UI_ENABLED)
    /// WINDOW MODE
    TFLuaWidgetFunctionDesc luaFuncDesc;
    luaFuncDesc.pLabel = "Windowed";
    luaFuncDesc.mType = TF_LUA_WIDGET_FUNCTION_ON_EDITED;
    luaFuncDesc.pFunc = wndSetWindowed;
    luaFuncDesc.pFuncData = NULL;
    luaRegisterWidgetFunction(&luaFuncDesc);
    luaFuncDesc.pLabel = "Fullscreen";
    luaFuncDesc.pFunc = wndSetFullscreen;
    luaRegisterWidgetFunction(&luaFuncDesc);
    luaFuncDesc.pLabel = "Borderless";
    luaFuncDesc.pFunc = wndSetBorderless;
    luaRegisterWidgetFunction(&luaFuncDesc);
    luaFuncDesc.pLabel = "Maximize";
    luaFuncDesc.pFunc = wndMaximizeWindow;
    luaRegisterWidgetFunction(&luaFuncDesc);
    luaFuncDesc.pLabel = "Minimize";
    luaFuncDesc.pFunc = wndMinimizeWindow;
    luaRegisterWidgetFunction(&luaFuncDesc);
    luaFuncDesc.pLabel = "Center";
    luaFuncDesc.pFunc = wndCenterWindow;
    luaRegisterWidgetFunction(&luaFuncDesc);

    /// CLIENT RECTANGLE
    TFLuaWidgetVariableDesc luaVarDesc;
    luaVarDesc.mWidgetType = TF_WIDGET_TYPE_SLIDER_INT;
    luaVarDesc.pLabel = "Client Rectangle X Offset";
    luaVarDesc.pInt = &pWindowRef->mWndX;
    luaRegisterWidgetVariable(&luaVarDesc);
    luaVarDesc.pLabel = "Client Rectangle Y Offset";
    luaVarDesc.pInt = &pWindowRef->mWndY;
    luaRegisterWidgetVariable(&luaVarDesc);
    luaVarDesc.pLabel = "Client Rectangle Width";
    luaVarDesc.pInt = &pWindowRef->mWndW;
    luaRegisterWidgetVariable(&luaVarDesc);
    luaVarDesc.pLabel = "Client Rectangle Height";
    luaVarDesc.pInt = &pWindowRef->mWndH;
    luaRegisterWidgetVariable(&luaVarDesc);

    luaFuncDesc.pLabel = "Set client rectangle";
    luaFuncDesc.pFunc = wndMoveWindow;
    luaRegisterWidgetFunction(&luaFuncDesc);
    luaFuncDesc.pLabel = "Set recommended client rectangle";
    luaFuncDesc.pFunc = wndSetRecommendedWindowSize;
    luaRegisterWidgetFunction(&luaFuncDesc);

    /// WINDOW DETAILS
#if WINDOW_DETAILS
    luaVarDesc.mWidgetType = TF_WIDGET_TYPE_TEXTBOX;
    luaVarDesc.pLabel = "WindowedRect";
    luaVarDesc.pText = &pWindowRef->pWindowedRectLabel;
    luaRegisterWidgetVariable(&luaVarDesc);
    luaVarDesc.pLabel = "FullscreenRect";
    luaVarDesc.pText = &pWindowRef->pFullscreenRectLabel;
    luaRegisterWidgetVariable(&luaVarDesc);
    luaVarDesc.pLabel = "ClientRect";
    luaVarDesc.pText = &pWindowRef->pClientRectLabel;
    luaRegisterWidgetVariable(&luaVarDesc);
    luaVarDesc.pLabel = "Wnd";
    luaVarDesc.pText = &pWindowRef->pWndLabel;
    luaRegisterWidgetVariable(&luaVarDesc);
    luaVarDesc.pLabel = "Fullscreen";
    luaVarDesc.pText = &pWindowRef->pFullscreenLabel;
    luaRegisterWidgetVariable(&luaVarDesc);
    luaVarDesc.pLabel = "CursorCaptured";
    luaVarDesc.pText = &pWindowRef->pCursorCapturedLabel;
    luaRegisterWidgetVariable(&luaVarDesc);
    luaVarDesc.pLabel = "Iconified";
    luaVarDesc.pText = &pWindowRef->pIconifiedLabel;
    luaRegisterWidgetVariable(&luaVarDesc);
    luaVarDesc.pLabel = "Maximized";
    luaVarDesc.pText = &pWindowRef->pMaximizedLabel;
    luaRegisterWidgetVariable(&luaVarDesc);
    luaVarDesc.pLabel = "Minimized";
    luaVarDesc.pText = &pWindowRef->pMinimizedLabel;
    luaRegisterWidgetVariable(&luaVarDesc);
    luaVarDesc.pLabel = "NoResizeFrame";
    luaVarDesc.pText = &pWindowRef->pNoResizeFrameLabel;
    luaRegisterWidgetVariable(&luaVarDesc);
    luaVarDesc.pLabel = "BorderlessWindow";
    luaVarDesc.pText = &pWindowRef->pBorderlessWindowLabel;
    luaRegisterWidgetVariable(&luaVarDesc);
    luaVarDesc.pLabel = "OverrideDefaultPosition";
    luaVarDesc.pText = &pWindowRef->pOverrideDefaultPositionLabel;
    luaRegisterWidgetVariable(&luaVarDesc);
    luaVarDesc.pLabel = "ForceLowDPI";
    luaVarDesc.pText = &pWindowRef->pForceLowDPILabel;
    luaRegisterWidgetVariable(&luaVarDesc);
    luaVarDesc.pLabel = "WindowMode";
    luaVarDesc.pText = &pWindowRef->pWindowModeLabel;
    luaRegisterWidgetVariable(&luaVarDesc);
#endif

    /// INPUT CONTROL
    const char* inputControlLabel = "Capture cursor";
    luaVarDesc.mWidgetType = TF_WIDGET_TYPE_CHECKBOX;
    luaVarDesc.pLabel = inputControlLabel;
    luaVarDesc.pBool = &pWindowRef->mCursorCaptured;
    luaRegisterWidgetVariable(&luaVarDesc);
    luaFuncDesc.pLabel = inputControlLabel;
    luaFuncDesc.pFunc = wndUpdateCaptureCursor;
    luaRegisterWidgetFunction(&luaFuncDesc);
#endif
    /// SCENE RESOLUTION OPTIONS
    if (gAvailableSceneResolutionCount)
    {
        TFLuaWidgetVariableDesc luaSceneResVarDesc = {};
        TFLuaWidgetFunctionDesc luaSceneResFuncDesc = {};
        const char*             sceneResOptsLabel = "Scene resolutions";
        luaSceneResVarDesc.mWidgetType = TF_WIDGET_TYPE_DROPDOWN;
        luaSceneResVarDesc.pLabel = sceneResOptsLabel;
        luaSceneResVarDesc.pUint = &gSceneResolutionIndex;
        luaRegisterWidgetVariable(&luaSceneResVarDesc);
        luaSceneResFuncDesc.mType = TF_LUA_WIDGET_FUNCTION_ON_EDITED;
        luaSceneResFuncDesc.pLabel = sceneResOptsLabel;
        luaSceneResFuncDesc.pFuncData = NULL;
        luaSceneResFuncDesc.pFunc = [](void*)
        {
            TFReloadDesc reloadDesc = {};
            reloadDesc.mType = TF_RELOAD_TYPE_RESIZE;
            requestReload(&reloadDesc);
        };
        luaRegisterWidgetFunction(&luaSceneResFuncDesc);
    }
}
#endif
