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

#include "../../Resources/ResourceLoader/ThirdParty/OpenSource/tinyimageformat/tinyimageformat_query.h"

#ifdef FORGE_UI_USE_32BIT_INDEXES
#define NK_UINT_DRAW_INDEX
#define DRAW_INDEX_TYPE uint32_t
#else
#define DRAW_INDEX_TYPE uint16_t
#endif

// Ignore Nuklear library warnings
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4701) // potentially uninitialized local variable used.
#endif
#if defined(__clang__)
// #pragma clang diagnostic push
// #pragma clang diagnostic ignored "-W..."
#elif defined(__GNUC__)
// #pragma GCC diagnostic push
// #pragma GCC diagnostic ignored "-W..."
#endif

#define NK_IMPLEMENTATION
#define NK_ZERO_COMMAND_MEMORY
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_ASSERT(x) ASSERT(x)
#include "../ThirdParty/OpenSource/nuklear/nuklear.h"

#ifdef _MSC_VER
#pragma warning(pop)
#endif
#if defined(__clang__)
// #pragma clang diagnostic pop
#elif defined(__GNUC__)
// #pragma GCC diagnostic pop
#endif

#include "../../OS/Interfaces/IInput.h"
// #include "../../Application/Interfaces/IFont.h"
#include "../../Application/Interfaces/IFont.h"
#include "../../Application/Interfaces/IUI.h"
#include "../../Resources/ResourceLoader/Interfaces/IResourceLoader.h"
#include "../../Utilities/Interfaces/IFileSystem.h"
#include "../../Utilities/Interfaces/ILog.h"
#include "../../Application/Interfaces/IProfiler.h"
#include "../../Utilities/Math/Algorithms.h"

#include "../../Utilities/Interfaces/IMemory.h"

#include "../../Graphics/FSL/fsl_srt.h"
#include "../../Graphics/FSL/defaults.h"
#include "./Shaders/FSL/NuklearResources.h"
#if defined(ENABLE_FORGE_TOUCH_INPUT)
#include "./Shaders/FSL/TexturedResources.h"
#endif
#if defined(ENABLE_FORGE_VR_UI)
#include "./Shaders/FSL/VRMarker.srt.h"
#endif

// This file should be moved to C, so try to avoid applying PVS C++ analysis on this file
//-V::C++
#define CACHE_WIDGETS                     0
#define CUSTOM_LAYOUT                     0
#define DISPLAY_SLIDER_VALUE              0

#define FALLBACK_FONT_TEXTURE_INDEX       0

#define LABELID(prop, buffer)             snprintf(buffer, MAX_LABEL_STR_LENGTH, "##%llu", (unsigned long long)(prop.pData))
#define LABELID1(prop, buffer)            snprintf(buffer, MAX_LABEL_STR_LENGTH, "##%llu", (unsigned long long)(prop))

#define MAX_LUA_STR_LEN                   256

#define MAX_HISTOGRAM_BINS                64
#define MAX_WIDGET_DATA_SIZE              sizeof(char*) * 4
#define MAX_WIDGET_GROUP_INDEX_STACK_SIZE 32

#define UI_FLOAT_EPSILON                  1e-7f
#define UI_COLOR_GAMMA_CORRECTION         2.2f

static const uint32_t MAX_FRAMES = 3;

typedef struct UILayoutRowItem
{
    float mHeight;
    float mWidth;
    vec2  mOffset;
} UILayoutRowItem;

typedef struct UILayoutRow
{
    TFUILayoutFormat mType;
    float            mHeight;
    float            mTotalWidth;
    vec2             mPos;
    int              mItemCount;
    UILayoutRowItem* pItems;
    ptrdiff_t        mBackgroundStart;
    ptrdiff_t        mBackgroundEnd;
} UILayoutRow;

typedef struct UILayoutPanel
{
    struct nk_rect mBounds; // local space
    float          mBorder;
    uint32_t       mOffsetX;
    uint32_t       mOffsetY;
    float          mFooterHeight;
    float          mHeaderHeight;
    bool           mHasScrolling;
    struct nk_rect mClip;
    UILayoutRow*   pRows;
    nk_flags       mFlags;
    // these walk through row/layout elements each frame
    vec2           mLayoutCursor; // local space
    ivec2          mRowCursor;
} UILayoutPanel;

typedef struct UIWidgetGroup
{
    TFUIWindowId mId;

    UILayoutPanel mPanel;

    UIWidgetGroup* pParentGroup;
    TFUIWidget**   ppChildren; // ungrouped widgets within this group
    UIWidgetGroup* ppChildGroups;
} UIWidgetGroup;

typedef struct TFUIWidget
{
    TFUIWidgetId   mId = 0;
    TFUIWidgetType mType = {}; // Type of the underlying widget

#if defined(FORGE_DEBUG)
    char mLabel[MAX_LABEL_STR_LENGTH] = {};
#endif

    TFUIWidgetState mWidgetState = TF_WIDGET_STATE_INACTIVE;

    // screen space position of the widget
    vec2 mDisplayPosition;

    // maximum bounding box dimensions of the widget this frame
    vec2 mBounds;

    // command markers into the last stored command buffer for the current widget
    ptrdiff_t mCommandStart = 0;
    ptrdiff_t mCommandEnd = 0;

    uint8_t pData[MAX_WIDGET_DATA_SIZE] = {}; // type-specific widget data
} TFUIWidget;

typedef nk_window* UIWindowCacheKey; // Nuklear window pointer
struct UIWindowCacheEntry
{
    // start of the draw commands for the given window from last change to UI
    void*       pLastCmdBuffer;
    size_t      mLastCmdBufferAllocated;
    // stored widgets for the given window
    TFUIWidget* pWidgets;
    uint32_t    mNextWidgetId;

    // used internally by Nuklear to keep track of collapseable trees
    int32_t       mTreeCounter;
    // The top group of the window that stores all lower level groups
    UIWidgetGroup mRootGroup;
    // whether the window's commands need to be drawn
    bool          mRedraw;
};
struct UIWindowCacheT
{
    UIWindowCacheKey   key;
    UIWindowCacheEntry value;
};
typedef struct UIWindowCacheT UIWindowCache;

typedef struct UIFontCache
{
    uint32_t     mFontId;
    nk_user_font mFont;
} UIFontCache;

typedef struct UIBlackboard
{
    UIWindowCacheEntry* pWindowCache;
    ptrdiff_t           mLastWidgetOffset;

    // group tracking
    UIWidgetGroup* pCurrentWidgetGroup;
    size_t         mChildGroupIndex;
    size_t         mGroupIndexStack[MAX_WIDGET_GROUP_INDEX_STACK_SIZE];
    size_t         mGroupIndexStackSize;

    // layout
    UILayoutRow* pCurrentLayoutRow;
    bool         mLayoutRowChanged;
    bool         mLayoutRowManualMode;

    // potentially needed
    TFUIWidget** mWidgetStack;
    vec2         mStackOrigin;
    vec2         mStackBounds;
} UIBlackboard;

typedef struct UIVertex
{
    vec2     pos;
    vec2     uv;
    uint32_t color; // RGBA
    uint32_t textureIndex;
    float    fontScale;
} UIVertex;

enum UIInputType
{
    NONE,
    KEYBOARD,
    GAMEPAD,
    TOUCH
};

static const uint32_t BINDLESS_NULL_INDEX = 0;
static const uint32_t BINDLESS_FONT_INDEX = 1;
static const uint32_t BINDLESS_CUSTOM_START = 2;
static const uint32_t MAX_MSAA_DRAWS_PER_FRAME = 4;

enum TFUIPopupEditorType
{
    TF_UI_POPOP_EDITOR_NONE = 0,
    TF_UI_POPOP_EDITOR_COLOR_PICKER = 1,
    TF_UI_POPOP_EDITOR_GRADIENT_PICKER = 2,
};

typedef struct UIContextMenu
{
    uint32_t*            pTabStack;
    float2               mPosition;
    float2               mSize;
    float2               mRealSize;
    TFUIContextMenuItem* pItems;
    uint32_t             mCountItems;
    bool                 mCreated;
    bool                 mOpenedContextMenu;
    bool                 mUseScrollbar;
} UIContextMenu;

typedef struct GamepadControlButtons
{
    TFInputEnum mKeyLeft;
    TFInputEnum mKeyRight;
    TFInputEnum mKeyUp;
    TFInputEnum mKeyDown;
    TFInputEnum mMajorKeyLeft;
    TFInputEnum mMajorKeyRight;
    TFInputEnum mKeyBack;
    TFInputEnum mKeyHideUI;
    TFInputEnum mScrollControl;
    TFInputEnum mLeftStickX;
    TFInputEnum mLeftStickY;
} GamepadControlButtons;

typedef struct GamepadWidgetSelectionDraw
{
    struct nk_rect mVisBound;
    float2         mMouseLineStart;
    float2         mMouseLineEnd;
} GamepadWidgetSelectionDraw;

typedef struct TFUserInterface
{
    nk_context        mContext;
    nk_allocator      mAllocator;
    // for Nuklear draw commands
    nk_buffer         mCmdBuffer;
    // copy of the previous Nuklear command buffer which is used to prevent rendering UI when nothing changed
    nk_buffer         mLastBuffer;
    nk_window**       pWindowsToDraw;
    bool              mCommandsChanged;
    nk_convert_config mConvertCfg;
    bool              mHaveWindow;
    bool              mForceUpdate;
    float             mPlatformDefaultFontHeight = -1;
    bstring           mClipboardString = bempty();

    // Temp widgets data
    UIContextMenu mContextMenu;
    int32_t       mCurrentTabIdx;
    int32_t       mTabCount;
    int32_t       mTabRowCount;

    float        mFontHeight;
    float        mFontDpiScale;
    TFFont*      pFont;
    nk_user_font mFontNuklear;

    TFUIDpiScaleSettings dpiScaleSettings = TF_UI_DPI_SCALE_SETTINGS_WINDOW_SIZE_APPLY;
    UIBlackboard         mBlackboard;
    nk_flags             mCommonWindowFlags;
    bool                 mFitCurrentWindowHeight;

    int mLastMouseX = 0;
    int mLastMouseY = 0;

    bool       mGamepadActive = false;
    int        mGamepadIndex = 0;
    bool       mGamepadWasUp = false;
    bool       mGamepadWasDown = false;
    float      mGamepadVerticalHoldTime = 0.0f;
    float      mGamepadPreviousVerticalHoldTime = 0.0f;
    bool       mGamepadWasBack = false;
    bool       mGamepadPopupCapturing = false;
    bool       mGamepadScrollControl = false;
    bool       mGamepadUpdateWidgetScrollFocus = false;
    float2     mGamepadLeftStickDelta = float2(0, 0);
    uint32_t   mGamepadSelectedIndex = 0;
    uint32_t   mGamepadPopupIndex = 0;
    uint32_t   mGamepadWidgetCount = 0;
    nk_window* pGamepadActiveWindow = NULL;
    bool       mGamepadWidgetDisableInput = false;
    int        mGamepadHorizontal = 0;
    float      mGamepadHorizontalHoldTime = 0.0f;
    float      mGamepadPreviousHorizontalHoldTime = 0.0f;
    int        mGamepadMajorHorizontal = 0;
    float      mGamepadMajorHorizontalHoldTime = 0.0f;
    float      mGamepadRepeatDelay = 0.3f;

    GamepadWidgetSelectionDraw* pGamepadWidgetStack;

    // Gamepad simplified windows layout
    bool                  mAutoSwitchSWL = false;
    bool                  mEnabledSWL = true;
    bool                  mHiddenUI_SWL = false;
    float2                mOffsetToDrawWindowSWL = float2(0, 0);
    float2                mMaxWindowSizeScreenRationSWL = float2(0.4f, 0.7f);
    TFUIWindowPositionSWL mWindowPositionSWL = TF_UI_WINDOW_POSITION_SWL_LEFT;
    const char**          ppWindowNameArrSWL = NULL;

    // Gamepad control hints
    float mShowGamepadControlHintsTime = 0.0f;
    bool  mShowGamepadControlHints = false;
    bool  mHadConnectedGamepad = false;

    TFRenderTarget* pDrawCacheRt;
    TFShader*       pDrawCacheShader;
    TFPipeline*     pDrawCachePipeline;
    TFResourceState mDrawCacheRtState;

    uint32_t        mFrameMaxCount = 2u;
    const uint32_t* pFrameIdx = NULL;

    // Stops rendering UI elements (disables command recording)
    bool     mEnableRendering = true;
    bool     mInitialized = false;
    nk_hash* pWindowsToHide = NULL;

    // Font width measurement optimization
    struct LastWidthQuery
    {
        const char* pStr = NULL;
        TFFont*     pFont = NULL;
        int         mLength = 0;
        float       mFontSize = 0.0f;
        uint32_t    mCodepoint = 0;

        float mWidth = 0.0f;
    } mLastWidthQuery;

    /////////////// OLD INTERFACE
    float    mWidth = 0.f;
    float    mHeight = 0.f;
    float    mDisplayWidth = 0.f;
    float    mDisplayHeight = 0.f;
    float    mRoundScreenSize = 0.f;
    uint32_t mMaxUserTextures = 20u;

    // Following var is useful for seeing UI capabilities and tweaking style settings.
    // Will only take effect if at least one GUI Component is active.
    bool mShowDemoUiWindow = false;
    // Enable to place labels and widgets on the same line, allow for all components of float2, float3,
    // float4 to be on the same line and use more compact color pickers.
    bool mCompactLayout = false;

    TFRenderer* pRenderer = NULL;

    TFPipelineCache* pPipelineCache = NULL;

    float mDpiScale[2] = { 0.0f };

    TFShader*        pShaderTextured[TF_SAMPLE_COUNT_COUNT] = { NULL };
    TFDescriptorSet* pDescriptorSetPerBatch = NULL;
    TFPipeline*      pPipelineTextured[TF_SAMPLE_COUNT_COUNT] = { NULL };
    TFBuffer*        pVertexBuffer = NULL;
    TFBuffer*        pIndexBuffer = NULL;
    TFBuffer*        pUniformBuffer[MAX_FRAMES] = { NULL };
    /// Default states
    TFVertexLayout   mVertexLayoutTextured = {};

    // Bindless texture tracking (per-frame temporaries)
    uint32_t mMSAADrawCount = 0;

    // Editor button IDs and state
    uint64_t            mCurrEditorButtonID = 0;
    uint64_t            mCurrentlyOpenEditorButton = (uint64_t)-1;
    TFUIPopupEditorType mCurrentlyOpenEditorType = TF_UI_POPOP_EDITOR_NONE;

    struct ColorPickerEditorData
    {
        int32_t mType;
    };

    ColorPickerEditorData mColorPickerEditorData;

    TFUIMessageBoxInfo mMessageBoxInfo = {};
    struct UIMessageBoxData
    {
        float       mButtonWidth[TF_UI_MESSAGE_BOX_BUTTON_COUNT];
        char const* pButtonName[TF_UI_MESSAGE_BOX_BUTTON_COUNT];
        bool        mButtonUsed[TF_UI_MESSAGE_BOX_BUTTON_COUNT];
        float2      mSize;
        float       mDistBetweenButtons;
        float       totalWidth = 0.0f;
        bool        mMessageBoxOpened = false;
    } mUIMessageBoxData;

    TFUIContextMenuItem mContextMenuGradientItems[2]; // delet key, add key

    struct GradientContext
    {
        TFUIGradientColor mGradientData;
        uint32_t          mRemoveKey;
        float             mAddKeyCoord;
    };

    GradientContext mContextMenuDeleteGradientData;

#if defined(ENABLE_FORGE_TOUCH_INPUT)
    // Virtual joystick UI
    TFShader* pVJShader = {};

    TFDescriptorSet* pVJDescriptorSet = {};
    TFPipeline*      pVJPipeline = {};
    TFTexture*       pVJTexture = {};
    uint32_t         mVJRootConstantIndex = {};

    TFBuffer* pVJUniformBuffer[MAX_FRAMES] = { NULL };
#endif

    uint32_t mLastUpdateCount = 0;
    vec2     mLastUpdateMin[64] = {};
    vec2     mLastUpdateMax[64] = {};
    /////////////// OLD INTERFACE

    UIWindowCache* pWidgetMap;

    TFTexture* pNullTexture;

    UIInputType           mActiveInputType;
    TFInputEnum           mInputKeyMap[NK_KEY_MAX];
    TFInputEnum           mInputButtonMap[NK_BUTTON_MAX];
    GamepadControlButtons mGamepadControlButtons;

    struct
    {
        TFInputEnum x;
        TFInputEnum y;
    } mInputCursorBindings;

    struct
    {
        TFInputEnum down;
        TFInputEnum up;
    } mInputScrollBindings;

    TFInputEnum mHideUIBinding;

    // Gamepad Window Selection
    int        mWindowSelectionIndex = 0;
    int        mWindowCounter = 0;
    nk_window* mWindowSelected = NULL;

#ifdef ENABLE_FORGE_VR_UI
    struct
    {
        // Used to raycast 3D controllers
        float2 mSize = float2(0.0f);
        float2 mResolution = float2(0.0f);
        float3 mPosition = float3(0.0f);

        float2 mCursorLocation = float2(0.0f);
        bool   mCursorValid = false;

        // Used to render a marker in the 2D layer
        TFShader*        pVRMarkerShader = NULL;
        TFPipeline*      pVRMarkerPipeline = NULL;
        TFDescriptorSet* pVRMarkerDescriptorSet = NULL;
        TFBuffer*        pVRMarkerBuffer = { NULL };
    } mVRLayer;

    struct
    {
        TFInputEnum mRightTrigger{};
        TFInputEnum mRightControllerTracking{};
        TFInputEnum mRightControllerPosX{};
        TFInputEnum mRightControllerPosY{};
        TFInputEnum mRightControllerPosZ{};
        TFInputEnum mRightControllerDirX{};
        TFInputEnum mRightControllerDirY{};
        TFInputEnum mRightControllerDirZ{};
    } mVRInput;
#endif
} TFUserInterface;

struct HistogramWidgetData
{
    size_t mBinCount;
};

enum GamepadSliderActionType : uint32_t
{
    GAMEPAD_HORIZONTAL_ACTION_TYPE_PRESS = 0,
    GAMEPAD_HORIZONTAL_ACTION_TYPE_HOLD = 1,
};

struct GamepadSliderAction
{
    GamepadSliderActionType mType;
    union
    {
        int32_t mStepDirection;
    };
};

struct GamepadStickAction
{
    float2 mDeltaLeft;
};

#ifdef ENABLE_FORGE_UI
static TFUserInterface* pUserInterface = NULL;

/****************************************************************************/
// MARK: - Static Value Definitions
/****************************************************************************/

static const uint64_t VERTEX_BUFFER_SIZE = FORGE_UI_MAX_VERTEXES * sizeof(UIVertex);
static const uint64_t INDEX_BUFFER_SIZE = FORGE_UI_MAX_INDEXES * sizeof(DRAW_INDEX_TYPE);

/****************************************************************************/
// MARK: - Static Function Declarations
/****************************************************************************/

static inline nk_context* getContext() { return &pUserInterface->mContext; }

static void* alloc_func(nk_handle user_data, void* old, size_t size);
static void  free_func(nk_handle user_data, void* old);

static TFUIWidgetId getWidgetId(TFUIWidget* widget);
#if CACHE_WIDGETS
static bool  compareWidgetCommands(UIWindowCacheEntry* cache, ptrdiff_t start, ptrdiff_t end);
static void* pushbackCachedCommands(UIWindowCacheEntry* cache, ptrdiff_t start, ptrdiff_t end);
#endif
static void         saveLastWidgetOffset();
static TFUIWidget*  updateLastWidget(const char* label, TFUIWidgetType type);
static TFUIWidgetId getNextWidgetId(UIWindowCacheEntry* curWndCache);
static TFUIWidget*  getCachedWidget(UIWindowCacheEntry* cache, TFUIWidgetId id, TFUIWidgetType type);

static void              setDefaultStyle();
static nk_text_alignment alignmentConvertToNk(TFUIAlignmentText flag);
static nk_color          float4ToNkColor(float4 color);
static float4            nkColorTofloat4(nk_color color);
#if CACHE_WIDGETS || CUSTOM_LAYOUT
static ptrdiff_t getLastWidgetCommandStart();
static ptrdiff_t getLastWidgetCommandEnd();
static void*     getCurrentCommandBufferPointer(ptrdiff_t offset);
#endif

static void layoutSpaceBeginImpl(TFUILayoutFormat format, float rowHeight, int widgetCount, bool manualMode);
#if CUSTOM_LAYOUT
static UILayoutRow*   createNewLayoutRow(UILayoutPanel* panel, TFUILayoutFormat format, float rowHeight, int widgetCount);
static void           setPanelLayout(UILayoutPanel* panel, nk_panel_type panelType, nk_flags flags);
static struct nk_rect layoutWidgetSpace();
static void           advanceLayoutRow(UILayoutPanel* panel, struct nk_rect extent);
static struct nk_rect getNextWidgetBounds();
#endif

static void fillDefaultInputKeyMap(TFInputEnum* keyMap, TFInputEnum* buttonMap, TFInputEnum* mouseX, TFInputEnum* mouseY,
                                   TFInputEnum* scrollDown, TFInputEnum* scrollUp, TFInputEnum* hideUI,
                                   GamepadControlButtons* gamepadControlButtons);

/****************************************************************************/
// MARK: - Text Widget Public Functions
/****************************************************************************/
void uiText(const bstring* text, TFUIAlignmentText alignment)
{
    nk_text(getContext(), bdata(text), blength(text), alignmentConvertToNk(alignment));
    //..
}

static void defaultGamepadWidgetSelection(float offsetX = 0.0f, float offsetY = 0.0f, bool disableSelectionBox = false)
{
    struct nk_rect bounds;
    nk_layout_peek(&bounds, getContext());

    nk_window* win = pUserInterface->pGamepadActiveWindow;
    if (isnan(bounds.x))
    {
        bounds.x = win->bounds.x + win->layout->border + getContext()->style.window.padding.x;
    }
    if (isnan(bounds.w))
    {
        bounds.w = win->bounds.w - 2 * (win->layout->border + getContext()->style.window.padding.x);
    }
    if (bounds.h == 0.0f)
    {
        bounds.h = win->layout->row.min_height;
    }
    struct nk_rect contentRegion = nk_window_get_content_region(getContext());

    if (!pUserInterface->mGamepadScrollControl && pUserInterface->mGamepadUpdateWidgetScrollFocus)
    {
        if (bounds.y < contentRegion.y || bounds.y + bounds.h > contentRegion.y + contentRegion.h)
        {
            nk_input_key(getContext(), bounds.y < contentRegion.y ? NK_KEY_SCROLL_UP : NK_KEY_SCROLL_DOWN, true);
        }
    }

    int32_t posX = (int32_t)(bounds.x + bounds.w / 2 + offsetX);
    int32_t posY = (int32_t)(bounds.y + bounds.h / 2 + offsetY);

    posX = (int32_t)clampi(posX, (int32_t)(contentRegion.x + 1), (int32_t)(contentRegion.x + contentRegion.w - 1));
    posY = (int32_t)clampi(posY, (int32_t)(contentRegion.y + 1), (int32_t)(contentRegion.y + contentRegion.h - 1));

    nk_input_motion(getContext(), posX, posY);

    // selection box
    if (!disableSelectionBox)
    {
        uint32_t scrollX = 0, scrollY = 0;
        nk_window_get_scroll(getContext(), &scrollX, &scrollY);
        struct nk_rect visBounds = { bounds.x - contentRegion.x + scrollX + 1.0f, bounds.y - contentRegion.y + scrollY, bounds.w - 1,
                                     bounds.h };

        GamepadWidgetSelectionDraw selectionDraw;

        selectionDraw.mVisBound = visBounds;

        // Dot at simulated mouse position
        if (offsetX != 0 || offsetY != 0)
        {
            float dotSize = 10.0f;
            selectionDraw.mMouseLineStart = float2(posX - contentRegion.x + scrollX - dotSize / 2, posY - contentRegion.y + scrollY);
            selectionDraw.mMouseLineEnd = float2(posX - contentRegion.x + scrollX + dotSize / 2, posY - contentRegion.y + scrollY);
        }

        arrpush(pUserInterface->pGamepadWidgetStack, selectionDraw);
    }
}

static void updateGamepadWidgetDisableInput(bool disableInputA)
{
    pUserInterface->mGamepadWidgetDisableInput = disableInputA;
    if (pUserInterface->mGamepadWidgetDisableInput)
    {
        nk_widget_disable_input(getContext(), 1);
    }
}

GamepadSliderAction BeginGamepadWidget(float offsetX = 0.0f, float offsetY = 0.0f, bool disableSelectionBox = false,
                                       bool disableInputA = false)
{
    GamepadSliderAction action{};
    struct nk_window*   win = getContext()->current;
    if (pUserInterface->mGamepadActive && pUserInterface->pGamepadActiveWindow == win)
    {
        updateGamepadWidgetDisableInput(disableInputA);

        if (pUserInterface->mGamepadWidgetCount == pUserInterface->mGamepadSelectedIndex)
        {
            pUserInterface->mGamepadPopupCapturing = false;
            pUserInterface->mGamepadPopupIndex = 0;

            if (pUserInterface->mGamepadHorizontal != 0 &&
                pUserInterface->mGamepadHorizontalHoldTime >= pUserInterface->mGamepadRepeatDelay)
            {
                action.mType = GamepadSliderActionType::GAMEPAD_HORIZONTAL_ACTION_TYPE_HOLD;
            }

            action.mStepDirection = pUserInterface->mGamepadHorizontal > 0.0f ? 1 : (pUserInterface->mGamepadHorizontal < 0.0f ? -1 : 0);
            pUserInterface->mGamepadHorizontal = 0;
            pUserInterface->mGamepadMajorHorizontal = 0;

            defaultGamepadWidgetSelection(offsetX, offsetY, disableSelectionBox);
        }
        pUserInterface->mGamepadWidgetCount++;
    }

    return action;
}

static void BeginGamepadDropdown(uint32_t itemsCount)
{
    if (pUserInterface->mGamepadActive && pUserInterface->pGamepadActiveWindow == getContext()->current)
    {
        if (pUserInterface->mGamepadWidgetCount == pUserInterface->mGamepadSelectedIndex)
        {
            nk_window* win = pUserInterface->pGamepadActiveWindow;
            if (win->popup.active && win->popup.win && win->popup.type == NK_PANEL_COMBO && !(win->popup.win->flags & NK_WINDOW_HIDDEN))
            {
                win = win->popup.win;

                pUserInterface->mGamepadPopupCapturing = true;
                pUserInterface->mGamepadPopupIndex = (uint32_t)clampu(pUserInterface->mGamepadPopupIndex, 0, itemsCount - 1);

                float          itemHeight = (int)uiCalculateHeightOfTextRows(DEFAULT_ROW_HEIGHT) + getContext()->style.window.spacing.y;
                struct nk_rect bounds = win->bounds;
                struct nk_rect contentRegion = win->layout->clip;
                int            targetY = (int)(bounds.y + itemHeight * pUserInterface->mGamepadPopupIndex + itemHeight / 2.0f);
                int            scrollTargetY = targetY - win->scrollbar.y;
                if (!pUserInterface->mGamepadScrollControl && pUserInterface->mGamepadUpdateWidgetScrollFocus)
                {
                    if (scrollTargetY < contentRegion.y || scrollTargetY > contentRegion.y + contentRegion.h)
                    {
                        nk_input_key(getContext(), scrollTargetY < contentRegion.y ? NK_KEY_SCROLL_UP : NK_KEY_SCROLL_DOWN, true);
                    }
                }
                int32_t posX = (int32_t)(contentRegion.x + contentRegion.w / 2);
                scrollTargetY =
                    (int32_t)clampi(scrollTargetY, (int32_t)(contentRegion.y + 1), (int32_t)(contentRegion.y + contentRegion.h - 1));

                nk_input_motion(getContext(), posX, scrollTargetY);
            }
            else
            {
                defaultGamepadWidgetSelection();
            }
        }

        pUserInterface->mGamepadWidgetCount++;
    }
}

static GamepadStickAction BeginGamepadWidgetStick(bool disableSelectionBox = false, bool disableInputA = false)
{
    GamepadStickAction action{};
    if (pUserInterface->mGamepadActive && pUserInterface->pGamepadActiveWindow == getContext()->current)
    {
        updateGamepadWidgetDisableInput(disableInputA);

        if (pUserInterface->mGamepadWidgetCount == pUserInterface->mGamepadSelectedIndex)
        {
            defaultGamepadWidgetSelection(0, 0, disableSelectionBox);

            action.mDeltaLeft = pUserInterface->mGamepadLeftStickDelta;
        }

        pUserInterface->mGamepadWidgetCount++;
    }
    return action;
}

static void EndGamepadWidget()
{
    if (!pUserInterface->mGamepadActive)
    {
        return;
    }

    if (pUserInterface->mGamepadWidgetDisableInput)
    {
        nk_widget_disable_input(getContext(), 0);
        pUserInterface->mGamepadWidgetDisableInput = false;
    }

    if (arrlen(pUserInterface->pGamepadWidgetStack) == 0)
    {
        return;
    }

    GamepadWidgetSelectionDraw selectionDraw = arrpop(pUserInterface->pGamepadWidgetStack);
    struct nk_rect             visBounds = selectionDraw.mVisBound;

    const float visThickness = 1.0;
    float4      visColor = float4(1.0f, 1.0f, 1.0f, 1.0f);
    uiLine(float2(visBounds.x, visBounds.y), float2(visBounds.x + visBounds.w, visBounds.y), visThickness, visColor);
    uiLine(float2(visBounds.x + visBounds.w, visBounds.y), float2(visBounds.x + visBounds.w, visBounds.y + visBounds.h), visThickness,
           visColor);
    uiLine(float2(visBounds.x + visBounds.w, visBounds.y + visBounds.h), float2(visBounds.x, visBounds.y + visBounds.h), visThickness,
           visColor);
    uiLine(float2(visBounds.x, visBounds.y + visBounds.h), float2(visBounds.x, visBounds.y), visThickness, visColor);

    float2 mouseStart = selectionDraw.mMouseLineStart;
    float2 mouseEnd = selectionDraw.mMouseLineEnd;

    if (mouseStart == float2(0, 0) && mouseEnd == float2(0, 0))
    {
        return;
    }
    float dotSize = 10.0f;
    uiLine(mouseStart, mouseEnd, dotSize, visColor);
}

static int32_t UnpackWidgetSliderActionForSliderf(GamepadSliderAction action, float min, float max, float step, float holdTime,
                                                  float prevHoldTime)
{
    if (action.mType == GamepadSliderActionType::GAMEPAD_HORIZONTAL_ACTION_TYPE_PRESS)
    {
        return action.mStepDirection;
    }
    else
    {
        const float presentPerSecond = 0.2f;
        float       stepsPerSecond = ceilf(((max - min) * presentPerSecond) / step);

        int currentStep = (int32_t)ceilf(holdTime * stepsPerSecond);
        int PreviousStep = (int32_t)ceilf(prevHoldTime * stepsPerSecond);
        if (currentStep != PreviousStep)
        {
            return action.mStepDirection * (currentStep - PreviousStep);
        }
        else
        {
            return 0;
        }
    }
}

static int32_t UnpackWidgetHorizontalActionForSliderf(GamepadSliderAction action, float min, float max, float step)
{
    return UnpackWidgetSliderActionForSliderf(action, min, max, step, pUserInterface->mGamepadHorizontalHoldTime,
                                              pUserInterface->mGamepadPreviousHorizontalHoldTime);
}

static int32_t UnpackWidgetVerticalActionForSliderf(GamepadSliderAction action, float min, float max, float step)
{
    return UnpackWidgetSliderActionForSliderf(action, min, max, step, pUserInterface->mGamepadVerticalHoldTime,
                                              pUserInterface->mGamepadPreviousVerticalHoldTime);
}

/****************************************************************************/
// MARK: - Main Widgets Public Functions
/****************************************************************************/

static void autoLayoutForCollapsingHeader()
{
    // set auto layout for collapsing header
    uiLayoutAutoTextRows(1);
}

static TFUIWidgetInteraction collapsingHeaderBeginImp(const char* label, bool initCollapsed, bool* expanded)
{
    autoLayoutForCollapsingHeader();

    TFUIWidgetInteraction result;
    UIBlackboard*         bb = &pUserInterface->mBlackboard;
    saveLastWidgetOffset();
    BeginGamepadWidget();
    result.type = TF_WIDGET_INTERACTION_VISIBLE;
    if (expanded == NULL)
    {
        int treeId = bb->pWindowCache->mTreeCounter++;
        result.visible = nk_tree_push_id(getContext(), NK_TREE_NODE, label, initCollapsed ? NK_MINIMIZED : NK_MAXIMIZED, NULL, treeId);
    }
    else
    {
        nk_collapse_states state = *expanded ? NK_MAXIMIZED : NK_MINIMIZED;
        result.visible = nk_tree_state_base(getContext(), NK_TREE_NODE, NULL, label, &state, NULL);
        *expanded = state == NK_MAXIMIZED ? true : false;
    }
    EndGamepadWidget();
    result.id = getWidgetId(updateLastWidget(label, TF_WIDGET_TYPE_COLLAPSING_HEADER));
    return result;
}

static TFUIWidgetInteraction collapsingHeaderSelectedBeginImp(const char* label, bool initCollapsed, bool* expanded, bool* selected)
{
    autoLayoutForCollapsingHeader();

    TFUIWidgetInteraction result;
    UIBlackboard*         bb = &pUserInterface->mBlackboard;
    saveLastWidgetOffset();
    struct nk_rect bounds;
    nk_layout_peek(&bounds, getContext());

    BeginGamepadWidget(-bounds.w / 2 + 5);
    result.type = TF_WIDGET_INTERACTION_VISIBLE;

#ifndef NK_INCLUDE_STANDARD_BOOL
    nk_bool internalSelected = (nk_bool)*selected;
#else
    nk_bool internalSelected = *selected;
#endif

    if (expanded == NULL)
    {
        int treeId = bb->pWindowCache->mTreeCounter++;

        result.visible =
            nk_tree_push_id(getContext(), NK_TREE_NODE, label, initCollapsed ? NK_MINIMIZED : NK_MAXIMIZED, &internalSelected, treeId);
    }
    else
    {
        nk_collapse_states state = *expanded ? NK_MAXIMIZED : NK_MINIMIZED;
        result.visible = nk_tree_state_base(getContext(), NK_TREE_NODE, NULL, label, &state, &internalSelected);
        *expanded = state == NK_MAXIMIZED ? true : false;
    }

#ifndef NK_INCLUDE_STANDARD_BOOL
    *selected = (bool)internalSelected;
#else
    *selected = internalSelected;
#endif
    EndGamepadWidget();

    result.id = getWidgetId(updateLastWidget(label, TF_WIDGET_TYPE_COLLAPSING_SELECTABLE_HEADER));
    return result;
}

TFUIWidgetInteraction uiCollapsingHeaderBegin(const char* label, bool collapsed)
{
    return collapsingHeaderBeginImp(label, collapsed, NULL);
}

TFUIWidgetInteraction uiCollapsingHeaderSelectedBegin(const char* label, bool collapsed, bool* selected)
{
    return collapsingHeaderSelectedBeginImp(label, collapsed, NULL, selected);
}

TFUIWidgetInteraction uiCollapsingHeaderBeginWithState(const char* label, bool* expanded)
{
    return collapsingHeaderBeginImp(label, false, expanded);
}

TFUIWidgetInteraction uiCollapsingHeaderSelectedBeginWithState(const char* label, bool* expanded, bool* selected)
{
    return collapsingHeaderSelectedBeginImp(label, false, expanded, selected);
}

void uiCollapsingHeaderEnd() { nk_tree_pop(getContext()); }

TFUIWidgetInteraction uiSelectableLabelBegin(const char* label, bool* selected)
{
    TFUIWidgetInteraction result;
    saveLastWidgetOffset();
    BeginGamepadWidget();
    result.type = TF_WIDGET_INTERACTION_CHANGED;
#ifndef NK_INCLUDE_STANDARD_BOOL
    nk_bool internalActive = (nk_bool)*selected;
    result.changed = nk_selectable_symbol_label(getContext(), NK_SYMBOL_PLUS, label, alignmentConvertToNk(TF_ALIGN_LEFT), &internalActive);
    *selected = (bool)internalActive;
#else
    result.changed =
        nk_selectable_symbol_label(getContext(), NK_SYMBOL_PLUS, label, alignmentConvertToNk(TF_ALIGN_LEFT), (nk_bool*)selected);
#endif
    result.id = getWidgetId(updateLastWidget(label, TF_WIDGET_TYPE_SELECTABLE_LABEL));
    return result;
}

TFUIWidgetInteraction uiDebugTexture(const TFTexture* handle, float2 textureDisplaySize)
{
    UNREF_PARAM(textureDisplaySize);
    TFUIWidgetInteraction result;
    saveLastWidgetOffset();
    struct nk_context* ctx = getContext();
    struct nk_image    tex = nk_image_ptr((void*)handle);
    // tex.w = (nk_ushort)textureDisplaySize.x;
    // tex.h = (nk_ushort)textureDisplaySize.y;
    nk_image(ctx, tex);
    result.id = getWidgetId(updateLastWidget("dtexture", TF_WIDGET_TYPE_LABEL));
    result.type = TF_WIDGET_INTERACTION_UNUSED;
    return result;
}

TFUIWidgetInteraction uiLabel(const char* label, TFUIAlignmentText alignment)
{
    TFUIWidgetInteraction result;
    saveLastWidgetOffset();

    struct nk_context* ctx = getContext();
#if CUSTOM_LAYOUT
    struct nk_window* win = ctx->current;
    struct nk_style*  style = &ctx->style;
    int               len = nk_strlen(label);
    struct nk_color   color = style->text.color;
    struct nk_vec2    item_padding = style->text.padding;
    struct nk_rect    bounds = getNextWidgetBounds();
    struct nk_text    text;

    text.padding.x = item_padding.x;
    text.padding.y = item_padding.y;
    text.background = style->window.background;
    text.text = nk_rgb_factor(color, style->text.color_factor);
    nk_widget_text(&win->buffer, bounds, label, len, &text, alignmentConvertToNk(alignment), style->font);
#else
    nk_label(ctx, label, alignmentConvertToNk(alignment));
#endif

    result.id = getWidgetId(updateLastWidget(label, TF_WIDGET_TYPE_LABEL));
    result.type = TF_WIDGET_INTERACTION_UNUSED;
    return result;
}

TFUIWidgetInteraction uiColorLabel(const char* label, TFUIAlignmentText alignment, float4 color)
{
    TFUIWidgetInteraction result;
    saveLastWidgetOffset();
    nk_label_colored(getContext(), label, alignmentConvertToNk(alignment), float4ToNkColor(color));
    result.id = getWidgetId(updateLastWidget(label, TF_WIDGET_TYPE_COLOR_LABEL));
    result.type = TF_WIDGET_INTERACTION_UNUSED;
    return result;
}

TFUIWidgetInteraction uiSeparator(float4 color, float thickness)
{
    TFUIWidgetInteraction result;
    saveLastWidgetOffset();

    struct nk_rect            bounds;
    struct nk_context*        ctx = getContext();
    struct nk_panel*          layout = ctx->current->layout;
    struct nk_rect            clip = layout->clip;
    struct nk_command_buffer* canvas = nk_window_get_canvas(ctx);
#if CUSTOM_LAYOUT
    UIBlackboard*    bb = &pUserInterface->mBlackboard;
    UILayoutPanel*   panel = &bb->pCurrentWidgetGroup->mPanel;
    UILayoutRow*     row = bb->pCurrentLayoutRow;
    UILayoutRowItem* item = row->pItems && arrlenu(row->pItems) > panel->mRowCursor[0] ? &row->pItems[panel->mRowCursor[0]] : NULL;

    bounds.x = panel->mLayoutCursor[0] + (item ? item->mWidth : 0);
    bounds.y = row->mPos[1];
    bounds.w = thickness;
    bounds.h = row->mHeight;
#else
    nk_layout_peek(&bounds, ctx);
    bounds.x = bounds.x + bounds.w;
    bounds.y = layout->at_y - (float)*layout->offset_y;
    bounds.w = thickness;
    bounds.h = layout->row.height;
#endif

    if (NK_INTERSECT(clip.x, clip.y, clip.w, clip.h, bounds.x, bounds.y, bounds.w, bounds.h))
    {
        struct nk_color nkColor = float4ToNkColor(color);
        nk_fill_rect(canvas, bounds, 0, nkColor);
    }

    result.id = getWidgetId(updateLastWidget("separator", TF_WIDGET_TYPE_SEPARATOR));
    result.type = TF_WIDGET_INTERACTION_UNUSED;
    return result;
}

TFUIWidgetInteraction uiVerticalSeparator(float4 color, float thickness)
{
    TFUIWidgetInteraction result;
    saveLastWidgetOffset();

    struct nk_rect            bounds;
    struct nk_context*        ctx = getContext();
    struct nk_panel*          layout = ctx->current->layout;
    struct nk_rect            clip = layout->clip;
    struct nk_command_buffer* canvas = nk_window_get_canvas(ctx);
#if CUSTOM_LAYOUT
    UIBlackboard*  bb = &pUserInterface->mBlackboard;
    UILayoutPanel* panel = &bb->pCurrentWidgetGroup->mPanel;
    UILayoutRow*   row = bb->pCurrentLayoutRow;

    bounds.x = row->mPos[0];
    bounds.y = row->mPos[1] + row->mHeight;
    bounds.w = panel->mBounds.w;
    bounds.h = thickness;
#else
    bounds.x = layout->at_x - (float)*layout->offset_x;
    bounds.y = layout->at_y + layout->row.height - (float)*layout->offset_y;
    bounds.w = layout->bounds.w;
    bounds.h = thickness;
#endif

    if (NK_INTERSECT(clip.x, clip.y, clip.w, clip.h, bounds.x, bounds.y, bounds.w, bounds.h))
    {
        struct nk_color nkColor = float4ToNkColor(color);
        nk_fill_rect(canvas, bounds, 0, nkColor);
    }

    result.id = getWidgetId(updateLastWidget("vertical_separator", TF_WIDGET_TYPE_VERTICAL_SEPARATOR));
    result.type = TF_WIDGET_INTERACTION_UNUSED;
    return result;
}

TFUIWidgetInteraction uiSeparatorStyled(float thickness)
{
    return uiSeparator(nkColorTofloat4(getContext()->style.window.border_color), thickness);
}

TFUIWidgetInteraction uiVerticalSeparatorStyled(float thickness)
{
    return uiVerticalSeparator(nkColorTofloat4(getContext()->style.window.border_color), thickness);
}

TFUIWidgetInteraction uiButton(const char* label)
{
    TFUIWidgetInteraction result;
    saveLastWidgetOffset();
    BeginGamepadWidget();
    result.type = TF_WIDGET_INTERACTION_PRESSED;
    result.pressed = nk_button_label(getContext(), label);
    result.id = getWidgetId(updateLastWidget(label, TF_WIDGET_TYPE_BUTTON));
    EndGamepadWidget();
    return result;
}

TFUIWidgetInteraction uiSliderCursorFloat(float* val, float min, float max, float step)
{
    TFUIWidgetInteraction result;
    struct nk_context*    ctx = getContext();
    saveLastWidgetOffset();
    BeginGamepadWidget(0, 0, false, true);
    result.type = TF_WIDGET_INTERACTION_CHANGED;
    float prevVal = *val;

    struct nk_rect bounds = {};
    bounds = nk_widget_bounds(ctx);
#if NUKLEAR_UI_VANILLA
    nk_slider_float(ctx, min, val, max, step);
#else
    nk_slider_float_tf(ctx, min, val, max, step);
#endif

#if DISPLAY_SLIDER_VALUE
    // display current value over the slider
    char valBuf[32];
    snprintf(valBuf, sizeof(valBuf), "%.2f", *val);
    struct nk_style*           style = &ctx->style;
    const struct nk_user_font* fnt = ctx->style.font;

    float textWidth = fnt->width(fnt->userdata, fnt->height, valBuf, (int)strlen(valBuf));
    float x = bounds.x + (bounds.w - textWidth) * 0.5f;
    float y = bounds.y;

    nk_draw_text(nk_window_get_canvas(ctx), nk_rect(x, y, textWidth, bounds.h), valBuf, (int)strlen(valBuf), ctx->style.font,
                 nk_rgb(255, 255, 255), style->text.color);

#endif

    result.changed = fabsf(prevVal - *val) > UI_FLOAT_EPSILON;
    result.id = getWidgetId(updateLastWidget("sliderf1", TF_WIDGET_TYPE_SLIDER_FLOAT));
    EndGamepadWidget();
    return result;
}

TFUIWidgetInteraction uiButtonImage(const char* label, const TFTexture* handle, uint2 size, uint4 rect)
{
    TFUIWidgetInteraction result;
    saveLastWidgetOffset();
    BeginGamepadWidget();
    struct nk_image tex = nk_image_ptr((void*)handle);
    tex.w = (nk_short)size.x;
    tex.h = (nk_short)size.y;
    tex.region[0] = (nk_short)rect.x;
    tex.region[1] = (nk_short)rect.y;
    tex.region[2] = (nk_short)rect.z;
    tex.region[3] = (nk_short)rect.w;
    result.type = TF_WIDGET_INTERACTION_PRESSED;
    result.pressed = nk_button_image(getContext(), tex);
    result.id = getWidgetId(updateLastWidget(label, TF_WIDGET_TYPE_BUTTON_IMAGE));
    EndGamepadWidget();
    return result;
}

TFUIWidgetInteraction uiButtonSymbol(TFUISymbolType type)
{
    TFUIWidgetInteraction result;
    saveLastWidgetOffset();
    BeginGamepadWidget();
    result.type = TF_WIDGET_INTERACTION_PRESSED;
    result.pressed = nk_button_symbol(getContext(), (nk_symbol_type)type);
    result.id = getWidgetId(updateLastWidget("buttonsymbol", TF_WIDGET_TYPE_BUTTON));
    EndGamepadWidget();
    return result;
}

TFUIWidgetInteraction uiSliderFloat(float* val, float min, float max, float step)
{
    TFUIWidgetInteraction result;
    struct nk_context*    ctx = getContext();
    saveLastWidgetOffset();
    result.type = TF_WIDGET_INTERACTION_CHANGED;
    float          prevVal = *val;
    struct nk_rect bounds = {};
    bounds = nk_widget_bounds(ctx);
    *val = clampf(*val + step * UnpackWidgetHorizontalActionForSliderf(BeginGamepadWidget(0, 0, false, true), min, max, step), min, max);
#ifdef NUKLEAR_UI_VANILLA
    nk_property_float(ctx, "#", min, val, max, step, step);
#else
    nk_slider_float_tf(ctx, min, val, max, step);
#endif

    result.changed = fabsf(prevVal - *val) > UI_FLOAT_EPSILON;
    result.id = getWidgetId(updateLastWidget("sliderf1", TF_WIDGET_TYPE_SLIDER_FLOAT));
    EndGamepadWidget();
    return result;
}

TFUIWidgetInteraction uiSliderFloat2(float2* val, float2 min, float2 max, float2 step)
{
    TFUIWidgetInteraction result;
    struct nk_context*    ctx = getContext();
    saveLastWidgetOffset();
    result.type = TF_WIDGET_INTERACTION_CHANGED;
    float2 prevVal = *val;
    for (int i = 0; i < 2; i++)
    {
        struct nk_rect bounds = {};
        bounds = nk_widget_bounds(ctx);
        (*val)[i] = clampf(
            (*val)[i] + step[i] * UnpackWidgetHorizontalActionForSliderf(BeginGamepadWidget(0, 0, false, true), min[i], max[i], step[i]),
            min[i], max[i]);
#ifdef NUKLEAR_UI_VANILLA
        nk_property_float(ctx, "#", min[i], &(*val)[i], max[i], step[i], step[i]);
#else
        nk_slider_float_tf(ctx, min[i], &(*val)[i], max[i], step[i]);
#endif
        EndGamepadWidget();
    }
    result.changed = f2MaxElem(f2FabsfPerElem(f2Sub(prevVal, *val))) > UI_FLOAT_EPSILON;
    result.id = getWidgetId(updateLastWidget("sliderf2", TF_WIDGET_TYPE_SLIDER_FLOAT2));
    return result;
}

TFUIWidgetInteraction uiSliderFloat3(float3* val, float3 min, float3 max, float3 step)
{
    UNREF_PARAM(step);
    TFUIWidgetInteraction result;
    struct nk_context*    ctx = getContext();
    saveLastWidgetOffset();
    result.type = TF_WIDGET_INTERACTION_CHANGED;
    float3 prevVal = *val;
    for (int i = 0; i < 3; i++)
    {
        struct nk_rect bounds = {};
        bounds = nk_widget_bounds(ctx);
        (*val)[i] = clampf(
            (*val)[i] + step[i] * UnpackWidgetHorizontalActionForSliderf(BeginGamepadWidget(0, 0, false, true), min[i], max[i], step[i]),
            min[i], max[i]);
#ifdef NUKLEAR_UI_VANILLA
        nk_property_float(ctx, "#", min[i], &(*val)[i], max[i], step[i], step[i]);
#else
        nk_slider_float_tf(ctx, min[i], &(*val)[i], max[i], step[i]);
#endif
        EndGamepadWidget();
    }
    result.changed = f3MaxElem(f3FabsfPerElem(f3Sub(prevVal, *val))) > UI_FLOAT_EPSILON;
    result.id = getWidgetId(updateLastWidget("sliderf3", TF_WIDGET_TYPE_SLIDER_FLOAT3));
    return result;
}

TFUIWidgetInteraction uiSliderFloat4(float4* val, float4 min, float4 max, float4 step)
{
    TFUIWidgetInteraction result;
    struct nk_context*    ctx = getContext();
    saveLastWidgetOffset();
    result.type = TF_WIDGET_INTERACTION_CHANGED;
    float4 prevVal = *val;
    for (int i = 0; i < 4; i++)
    {
        struct nk_rect bounds = {};
        bounds = nk_widget_bounds(ctx);
        (*val)[i] = clampf(
            (*val)[i] + step[i] * UnpackWidgetHorizontalActionForSliderf(BeginGamepadWidget(0, 0, false, true), min[i], max[i], step[i]),
            min[i], max[i]);
        nk_property_float(ctx, "#", min[i], &(*val)[i], max[i], step[i], step[i]);
        EndGamepadWidget();
    }
    result.changed = f4MaxElem(f4FabsfPerElem(f4Sub(prevVal, *val))) > UI_FLOAT_EPSILON;
    result.id = getWidgetId(updateLastWidget("sliderf3", TF_WIDGET_TYPE_SLIDER_FLOAT4));
    EndGamepadWidget();
    return result;
}

TFUIWidgetInteraction uiPropertyInt(int32_t* val, int32_t min, int32_t max, int32_t step)
{
    TFUIWidgetInteraction result;
    struct nk_context*    ctx = getContext();
    saveLastWidgetOffset();
    result.type = TF_WIDGET_INTERACTION_CHANGED;
    int32_t prevVal = *val;
    *val = (int)clampi((*(int*)val) + step * UnpackWidgetHorizontalActionForSliderf(BeginGamepadWidget(0, 0, false, true), (float)min,
                                                                                    (float)max, (float)step),
                       min, max);
    nk_property_int(ctx, "#", min, val, max, step, 0.0f);

    result.changed = (prevVal - *val) != 0;
    result.id = getWidgetId(updateLastWidget("propertyint", TF_WIDGET_TYPE_PROPERTY_INT));
    EndGamepadWidget();
    return result;
}

TFUIWidgetInteraction uiSliderInt(int32_t* val, int32_t min, int32_t max, int32_t step)
{
    TFUIWidgetInteraction result;
    struct nk_context*    ctx = getContext();
    saveLastWidgetOffset();
    result.type = TF_WIDGET_INTERACTION_CHANGED;
    int32_t prevVal = *val;
    *val = (int)clampi((*(int*)val) + step * UnpackWidgetHorizontalActionForSliderf(BeginGamepadWidget(0, 0, false, true), (float)min,
                                                                                    (float)max, (float)step),
                       min, max);
    nk_slider_int(ctx, min, val, max, step);
    result.changed = prevVal != *val;
    result.id = getWidgetId(updateLastWidget("slideri", TF_WIDGET_TYPE_SLIDER_INT));
    EndGamepadWidget();
    return result;
}

TFUIWidgetInteraction uiSliderUint(uint32_t* val, uint32_t min, uint32_t max, uint32_t step)
{
    TFUIWidgetInteraction result;
    struct nk_context*    ctx = getContext();
    saveLastWidgetOffset();
    result.type = TF_WIDGET_INTERACTION_CHANGED;
    uint32_t prevVal = *val;
    *val = (uint32_t)clampi((*(int*)val) + (int)step * UnpackWidgetHorizontalActionForSliderf(BeginGamepadWidget(0, 0, false, true),
                                                                                              (float)min, (float)max, (float)step),
                            min, max);
    nk_slider_int(ctx, min, (int*)val, max, step);
    result.changed = prevVal != *val;
    result.id = getWidgetId(updateLastWidget("sliderui", TF_WIDGET_TYPE_SLIDER_UINT));
    EndGamepadWidget();
    return result;
}

TFUIWidgetInteraction uiRadioButton(const char* label, bool* active)
{
    TFUIWidgetInteraction result;
    saveLastWidgetOffset();
    BeginGamepadWidget();
    result.type = TF_WIDGET_INTERACTION_CHANGED;
#ifndef NK_INCLUDE_STANDARD_BOOL
    // have to convert the bool to a larger Nuklear type (NK_BOOL) to avoid memory corruption
    nk_bool internalActive = (nk_bool)*active;
    result.changed = nk_radio_label(getContext(), label, &internalActive);
    *active = (bool)internalActive;
#else
    result.changed = nk_radio_label(getContext(), label, (nk_bool*)active);
#endif
    result.id = getWidgetId(updateLastWidget(label, TF_WIDGET_TYPE_RADIO_BUTTON));
    EndGamepadWidget();
    return result;
}

FORGE_API TFUIWidgetInteraction uiCheckboxImage(const TFTexture* handle, uint2 size, uint4 rect, bool* active)
{
    struct nk_image tex = nk_image_ptr((void*)handle);
    tex.w = (nk_ushort)size.x;
    tex.h = (nk_ushort)size.y;
    tex.region[0] = (nk_ushort)rect.x;
    tex.region[1] = (nk_ushort)rect.y;
    tex.region[2] = (nk_ushort)rect.z;
    tex.region[3] = (nk_ushort)rect.w;

    TFUIWidgetInteraction result;
    saveLastWidgetOffset();
    BeginGamepadWidget();
    result.type = TF_WIDGET_INTERACTION_CHANGED;
#ifndef NK_INCLUDE_STANDARD_BOOL
    nk_bool internalActive = (nk_bool)*active;
    result.changed = (bool)nk_checkbox_image(getContext(), tex, &internalActive);
    *active = (bool)internalActive;
#else
    result.changed = (bool)nk_checkbox_image(getContext(), tex, (nk_bool*)active);
#endif
    result.id = getWidgetId(updateLastWidget("checkboximageui", TF_WIDGET_TYPE_CHECKBOX_IMAGE));
    EndGamepadWidget();
    return result;
}

TFUIWidgetInteraction uiCheckbox(const char* label, bool* active)
{
    TFUIWidgetInteraction result;
    saveLastWidgetOffset();
    BeginGamepadWidget();
    result.type = TF_WIDGET_INTERACTION_CHANGED;
#ifndef NK_INCLUDE_STANDARD_BOOL
    // have to convert the bool to a larger Nuklear type (NK_BOOL) to avoid memory corruption
    nk_bool internalActive = (nk_bool)*active;
    result.changed = nk_checkbox_label(getContext(), label, &internalActive);
    *active = (bool)internalActive;
#else
    result.changed = nk_checkbox_label(getContext(), label, (nk_bool*)active);
#endif
    result.id = getWidgetId(updateLastWidget(label, TF_WIDGET_TYPE_CHECKBOX));
    EndGamepadWidget();
    return result;
}

TFUIWidgetInteraction uiDropdown(const char* const* items, int count, int selected)
{
    TFUIWidgetInteraction result;
    saveLastWidgetOffset();
    BeginGamepadDropdown(count);
    result.type = TF_WIDGET_INTERACTION_SELECTED;
    // TODO: determine how to define sizes of similar widgets
    struct nk_vec2 size = { 300, 300 };
    struct nk_rect bounds = nk_widget_bounds(getContext());
    int            itemHeight = (int)uiCalculateHeightOfTextRows(DEFAULT_ROW_HEIGHT);
    result.selected = nk_combo(getContext(), items, count, selected, itemHeight, size);

    // Draw arrow
    float           arrowSize = 10.0f;
    float           cx = bounds.x + bounds.w - arrowSize * 1.2f;
    float           cy = bounds.y + bounds.h / 2.0f;
    struct nk_color arrowColor = { (nk_byte)255, (nk_byte)255, (nk_byte)255, (nk_byte)255 };
    nk_fill_triangle(&getContext()->current->buffer, cx - arrowSize * 0.5f, cy - arrowSize * 0.25f, cx + arrowSize * 0.5f,
                     cy - arrowSize * 0.25f, cx, cy + arrowSize * 0.25f, arrowColor);

    result.id = getWidgetId(updateLastWidget("dropdown", TF_WIDGET_TYPE_DROPDOWN));
    EndGamepadWidget();
    return result;
}

TFUIWidgetInteraction uiColumnBegin(const char* label, bool showLabel)
{
    TFUIWidgetInteraction result;
    saveLastWidgetOffset();
    result.type = TF_WIDGET_INTERACTION_VISIBLE;
    result.visible = nk_group_begin(getContext(), label, NK_WINDOW_BORDER | (showLabel ? NK_WINDOW_TITLE : 0)) == 1;
    result.id = getWidgetId(updateLastWidget(label, TF_WIDGET_TYPE_COLUMN));
    return result;
}

void uiColumnEnd() { nk_group_end(getContext()); }

TFUIWidgetInteraction uiProgressBar(size_t* current, size_t max, bool modifyable)
{
    TFUIWidgetInteraction result;
    saveLastWidgetOffset();
    result.type = TF_WIDGET_INTERACTION_CHANGED;
    result.changed = nk_progress(getContext(), current, max, modifyable);
    result.id = getWidgetId(updateLastWidget("progressbar", TF_WIDGET_TYPE_PROGRESS_BAR));
    return result;
}

static void setColorSliderLayout()
{
    // TODO: save previous layout
    // ...
    // set layout for the composite widget
    nk_layout_row_template_begin(getContext(), 0);
    // R G B A properties
    nk_layout_row_template_push_dynamic(getContext());
    nk_layout_row_template_push_dynamic(getContext());
    nk_layout_row_template_push_dynamic(getContext());
    nk_layout_row_template_push_dynamic(getContext());
    // color combobox with color picker popup
    nk_layout_row_template_push_variable(getContext(), 20);
    nk_layout_row_template_end(getContext());
}

TFUIWidgetInteraction uiColorSliderRGBA8(uint32_t* color)
{
    TFUIWidgetInteraction result;
    saveLastWidgetOffset();

    setColorSliderLayout();

    bool            changed = false;
    struct nk_color comboColor;
    struct nk_color newComboColor;
    comboColor.r = (nk_byte)((*color & 0xFF000000) >> 0x18);
    comboColor.g = (nk_byte)((*color & 0x00FF0000) >> 0x10);
    comboColor.b = (nk_byte)((*color & 0x0000FF00) >> 0x8);
    comboColor.a = (nk_byte)(*color & 0x000000FF);

    newComboColor.r = (nk_byte)nk_propertyi(getContext(), "#R:", 0, comboColor.r, 255, 1, 1);
    newComboColor.g = (nk_byte)nk_propertyi(getContext(), "#G:", 0, comboColor.g, 255, 1, 1);
    newComboColor.b = (nk_byte)nk_propertyi(getContext(), "#B:", 0, comboColor.b, 255, 1, 1);
    newComboColor.a = (nk_byte)nk_propertyi(getContext(), "#A:", 0, comboColor.a, 255, 1, 1);
    if (comboColor.r != newComboColor.r || comboColor.g != newComboColor.g || comboColor.b != newComboColor.b ||
        comboColor.a != newComboColor.a)
    {
        changed = true;
    }

    if (nk_combo_begin_color(getContext(), newComboColor, nk_vec2(200, 400), nk_vec2(200, 400)))
    {
        uiLayoutAutoTextRows(1);
        struct nk_colorf comboColorf = nk_color_cf(newComboColor);
        changed |= (bool)nk_color_pick(getContext(), &comboColorf, NK_RGBA);
        nk_combo_end(getContext());
        newComboColor = nk_rgba_cf(comboColorf);
    }

    if (changed)
    {
        uint32_t returnColor = 0;
        returnColor |= (uint32_t)newComboColor.r << 0x18;
        returnColor |= (uint32_t)newComboColor.g << 0x10;
        returnColor |= (uint32_t)newComboColor.b << 0x8;
        returnColor |= (uint32_t)newComboColor.a;

        *color = returnColor;
    }

    result.type = TF_WIDGET_INTERACTION_CHANGED;
    result.changed = changed;
    result.id = getWidgetId(updateLastWidget("colorslider_rgba8", TF_WIDGET_TYPE_COLOR_RGBA8_SLIDER));
    return result;
}

TFUIWidgetInteraction uiColorSliderRGBA32(float4* color)
{
    TFUIWidgetInteraction result;
    saveLastWidgetOffset();

    setColorSliderLayout();

    bool             changed = false;
    struct nk_colorf comboColor;
    struct nk_colorf newComboColor;
    comboColor.r = color->x;
    comboColor.g = color->y;
    comboColor.b = color->z;
    comboColor.a = color->w;

    newComboColor = comboColor;

    const char* propertyName[4] = { "#R:", "#G:", "#B:", "#A:" };

    for (int i = 0; i < 4; i++)
    {
        float* pValue = (&newComboColor.r + i);
        float  gamepadBarInput = UnpackWidgetHorizontalActionForSliderf(BeginGamepadWidget(0, 0, false, true), 0.0f, 1.0f, 0.01f) / 100.0f;
        *pValue = nk_propertyf(getContext(), propertyName[i], 0, *pValue, 1, 0.001f, 0.001f) + gamepadBarInput;
        EndGamepadWidget();
        *pValue = clampf(*pValue, 0, 1);
    }

    if (comboColor.r != newComboColor.r || comboColor.g != newComboColor.g || comboColor.b != newComboColor.b ||
        comboColor.a != newComboColor.a)
    {
        changed = true;
    }

    if (changed)
    {
        color->x = newComboColor.r;
        color->y = newComboColor.g;
        color->z = newComboColor.b;
        color->w = newComboColor.a;
    }

    changed |= uiColorButton(color).changed;

    result.type = TF_WIDGET_INTERACTION_CHANGED;
    result.changed = changed;
    result.id = getWidgetId(updateLastWidget("colorslider_rgba32", TF_WIDGET_TYPE_COLOR_RGBA32_SLIDER));
    return result;
}

TFUIWidgetInteraction uiChartBegin(TFUIChartType type, int numPoints, float min, float max)
{
    TFUIWidgetInteraction result;
    saveLastWidgetOffset();
    result.type = TF_WIDGET_INTERACTION_VISIBLE;
    result.visible = nk_chart_begin(getContext(), (nk_chart_type)type, numPoints, min, max);
    result.id = getWidgetId(updateLastWidget("chart", TF_WIDGET_TYPE_CUSTOM_CHART));
    return result;
}

TFUIWidgetInteraction uiChartBeginColored(TFUIChartType type, float4 color, float4 pointHighlight, int numPoints, float min, float max)
{
    TFUIWidgetInteraction result;
    saveLastWidgetOffset();
    result.type = TF_WIDGET_INTERACTION_VISIBLE;
    result.visible = nk_chart_begin_colored(getContext(), (nk_chart_type)type, nk_rgba_fv(&color[0]), nk_rgba_fv(&pointHighlight[0]),
                                            numPoints, min, max);
    result.id = getWidgetId(updateLastWidget("chart", TF_WIDGET_TYPE_CUSTOM_CHART));
    return result;
}

TFUIWidgetInteraction uiChartPush(float val)
{
    struct nk_context*    ctx = getContext();
    TFUIWidgetInteraction result;
    result.type = TF_WIDGET_INTERACTION_CHART_EVENTS;
    result.chartEvents = nk_chart_push(ctx, val);
    result.id = UINT32_MAX;
    return result;
}

void uiChartEnd() { nk_chart_end(getContext()); }

void uiChartSetColor(float4 color, float4 pointHighlight)
{
    struct nk_context* ctx = getContext();
    struct nk_window*  win = ctx->current;
    struct nk_chart*   chart = &win->layout->chart;
    chart->slots[0].color = nk_rgba_fv(&color[0]);
    chart->slots[0].highlight = nk_rgba_fv(&pointHighlight[0]);
}

void uiChartResetColor()
{
    struct nk_context* ctx = getContext();
    struct nk_window*  win = ctx->current;
    struct nk_chart*   chart = &win->layout->chart;
    chart->slots[0].color = ctx->style.chart.color;
    chart->slots[0].highlight = ctx->style.chart.selected_color;
}

#if CACHE_WIDGETS
// TODO: clipped shapes are not recorded into the window command buffer, so need a way to determine how many were skipped.
// Below code treats all shapes as un-clipped, which won't work for edge cases.
static ptrdiff_t chartBackgroundSkipOffset()
{
    const struct nk_style_chart* style = &getContext()->style.chart;
    const struct nk_style_item*  background = &style->background;
    ptrdiff_t                    offset = 0;
    // added by nk_chart_begin call
    switch (background->type)
    {
    case NK_STYLE_ITEM_IMAGE:
        offset += sizeof(nk_command_image); // background image
        break;
    case NK_STYLE_ITEM_NINE_SLICE:
        offset += 9 * sizeof(nk_command_image); // background image slices
        break;
    case NK_STYLE_ITEM_COLOR:
        offset += 2 * sizeof(nk_command_rect_filled); // two filled rectangles for colored background
        break;
    }
    return offset;
}

// static size_t cachedChartValuesCount(nk_window* wnd, TFUIWidget* widget, nk_chart_type chartType, int slot)
//{
//    ptrdiff_t shapesCountStart = widget->mCommandStart;
//    ptrdiff_t shapesCountEnd = widget->mCommandEnd;
//
//    shapesCountStart += chartBackgroundSkipOffset();
//
//    size_t rectsCount;
//    switch (chartType)
//    {
//    case NK_CHART_COLUMN:
//        rectsCount = (shapesCountEnd - 1 - shapesCountStart) / sizeof(nk_command_rect_filled);
//        break;
//    case NK_CHART_LINES:
//        if (shapesCountEnd - shapesCountStart > 1) // has values
//        {
//            struct nk_chart* c = &wnd->layout->chart;
//            bool showMarkers = c->slots[slot].show_markers;
//            if (showMarkers)
//            {
//                rectsCount++;
//                shapesCountStart += sizeof(nk_command_rect_filled); // first point without connection
//                rectsCount = (shapesCountEnd - 1 - shapesCountStart) / (sizeof(nk_command_line) + sizeof(nk_command_rect_filled));
//            }
//            else
//            {
//                rectsCount = (shapesCountEnd - 1 - shapesCountStart) / sizeof(nk_command_line);
//            }
//        }
//        else
//        {
//            rectsCount = 0;
//        }
//        break;
//    }
//
//    return rectsCount;
//}

static void updateHistogramColumns(TFUIWidget* widget, UIWindowCacheEntry* cache, size_t* binValues, size_t binCount, int slot)
{
    struct nk_chart* chart = &getContext()->current->layout->chart;
    char* columnCmds = (char*)pushbackCachedCommands(cache, widget->mCommandStart, widget->mCommandEnd) + chartBackgroundSkipOffset();
    for (size_t i = 0; i < binCount; i++)
    {
        // TODO: consider enabling NK_INCLUDE_COMMAND_USERDATA to pass histogram specific data
        // which could track current column values, preventing recalculations if values match

        nk_command_rect_filled* column = (nk_command_rect_filled*)columnCmds + i;
        ASSERT(column && column->header.type == NK_COMMAND_RECT_FILLED);
        float value = (float)binValues[i];
        float ratio;

        // update bounds the same way as in nk_chart_push_column
        column->h = (unsigned short)(chart->h * NK_ABS((value / chart->slots[slot].range)));
        if (value >= 0)
        {
            ratio = (value + NK_ABS(chart->slots[slot].min)) / NK_ABS(chart->slots[slot].range);
            column->y = (short)((chart->y + chart->h) - chart->h * ratio);
        }
        else
        {
            ratio = (value - chart->slots[slot].max) / chart->slots[slot].range;
            column->y = (short)(chart->y + (chart->h * NK_ABS(ratio)) - column->h);
        }

        // TODO: will also likely need..
        // clipping based on currently defined layout
        // update input selection/highlight?
    }
    chart->slots[slot].index = (int)binCount;
}

#endif

TFUIWidgetInteraction uiHistogram(float start, float end, float binInterval, const float* values, size_t count)
{
    TFUIWidgetInteraction result;
    result.type = TF_WIDGET_INTERACTION_VISIBLE;

    // fill bins
    const size_t bins = (size_t)((end - start) / binInterval);
    ASSERTMSG(bins <= MAX_HISTOGRAM_BINS, "Number of histogram bins exceeds defined maximum, consider increasing MAX_HISTOGRAM_BINS.");
    ASSERT(binInterval > 0);
    size_t binValues[MAX_HISTOGRAM_BINS] = { 0 };

    for (size_t i = 0; i < count; i++)
    {
        size_t bin = size_t((values[i] - start) / binInterval);
        binValues[bin]++;
    }

    UIWindowCacheEntry* cache = pUserInterface->mBlackboard.pWindowCache;
    TFUIWidgetId        luId = getNextWidgetId(cache);
    TFUIWidget*         widget = getCachedWidget(cache, luId, TF_WIDGET_TYPE_HISTOGRAM);
    // TODO: test potential performance benefit of modifying histogram column rectangles in place
    // from cached command buffer instead of recalculating commands every time this function is called
#if CACHE_WIDGETS
    bool reconstruct = widget == NULL || ((HistogramWidgetData*)widget->pData)->mBinCount != bins;

    if (!reconstruct)
    {
        // TODO: get visibility after implementing layout state tracking
        result.visible = true;
        result.id = luId;

        // TODO: slot index may change if we want to support several graphs within the same chart in the future
        updateHistogramColumns(widget, cache, binValues, bins, 0);
    }
    else
#endif
    {
        saveLastWidgetOffset();
        bool visible = nk_chart_begin(getContext(), NK_CHART_COLUMN, (int)count, start, end);
        if (visible)
        {
            for (size_t i = 0; i < bins; i++)
            {
                nk_chart_push(getContext(), (float)binValues[i]);
            }
            nk_chart_end(getContext());
        }
        widget = updateLastWidget("histogram", TF_WIDGET_TYPE_HISTOGRAM);
        result.id = getWidgetId(widget);
        result.visible = visible;
    }
#if CACHE_WIDGETS
    // store number of bins
    ((HistogramWidgetData*)widget->pData)->mBinCount = bins;
#endif
    return result;
}

TFUIWidgetInteraction uiPlotLines(const float* values, size_t count, float minValue, float maxValue, bool showPoints)
{
    struct nk_context*    ctx = getContext();
    TFUIWidgetInteraction result;
    result.type = TF_WIDGET_INTERACTION_VISIBLE;

    saveLastWidgetOffset();

    struct nk_style_chart* style = &ctx->style.chart;
    style->show_markers = showPoints;

    bool visible = nk_chart_begin(ctx, NK_CHART_LINES, (int)count, minValue, maxValue);
    if (visible)
    {
        for (size_t i = 0; i < count; ++i)
            nk_chart_push(ctx, values[i]);
        nk_chart_end(ctx);
    }
    result.id = getWidgetId(updateLastWidget("plot_lines", TF_WIDGET_TYPE_PLOT_LINES));
    result.visible = visible;

    return result;
}

static bool doRGBColorPicker(struct nk_colorf* pColor, struct nk_rect bounds, bool drawCircle)
{
    bool wasChanged = false;

    struct nk_context*        ctx = getContext();
    struct nk_panel*          layout = ctx->current->layout;
    struct nk_command_buffer* o = &ctx->current->buffer;

    const float    border = ctx->style.gradient.border;
    const nk_color borderColor = ctx->style.gradient.border_color;
    const float    colorFactorBackground = ctx->style.gradient.color_factor_background;

    float barSize = ctx->style.font->height * 2;

    struct nk_rect mainMatrix = nk_rect(bounds.x, bounds.y, bounds.h, bounds.h);
    struct nk_rect bar = nk_rect(mainMatrix.x + mainMatrix.w + uiGetDefaultRowPadding(), bounds.y, barSize, mainMatrix.h);

    nk_layout_space_push(ctx, mainMatrix);

    GamepadStickAction           stickAction = BeginGamepadWidgetStick(false, true);
    enum nk_widget_layout_states state = nk_widget(&mainMatrix, ctx);
    const struct nk_input* in = (state == NK_WIDGET_ROM || state == NK_WIDGET_DISABLED || layout->flags & NK_WINDOW_ROM) ? 0 : &ctx->input;

    float hsva[4];
    nk_colorf_hsva_f(&hsva[0], &hsva[1], &hsva[2], &hsva[3], *pColor);

    const struct nk_color black_trans = { 0, 0, 0, 0 };

    // main matrix
    {
        nk_override_anti_aliasing(o, true, NK_ANTI_ALIASING_ON);

        struct nk_rect borderBound = mainMatrix;

        float  radius = mainMatrix.w / 2;
        float2 center = f2Make(mainMatrix.x + radius + 0.5f, mainMatrix.y + radius + 0.5f);

        if (drawCircle)
        {
            bool   circleUpdated = false;
            float2 dir = float2(0, 0);
            if (nk_button_behavior(&ctx->last_widget_state, mainMatrix, in, NK_BUTTON_REPEATER))
            {
                dir = f2Make(in->mouse.pos.x - center.x, in->mouse.pos.y - center.y);
                circleUpdated = true;
            }
            else if (stickAction.mDeltaLeft != float2(0, 0))
            {
                float2 currentPos = float2(cosf(hsva[0] * (2 * PI)), sinf(hsva[0] * (2 * PI))) * (radius * hsva[1]);
                dir = currentPos + float2(stickAction.mDeltaLeft.x, -stickAction.mDeltaLeft.y) * radius * 2;
                circleUpdated = true;
            }

            if (circleUpdated)
            {
                float2 nDir = f2Normalize(dir);
                hsva[0] = atan2f(nDir.y, nDir.x) / (2 * PI);
                if (hsva[0] < 0)
                    hsva[0] += 1.0f;
                hsva[1] = NK_SATURATE(f2Length(dir) / radius);
                wasChanged = true;
            }

            for (uint32_t i = 0; i < 64; i++)
            {
                float  currCoef = (float)i / 64;
                float  nextCoef = (float)(i + 1) / 64;
                float2 currPos = f2Make(center.x + radius * cosf(currCoef * 2 * PI), center.y + radius * sinf(currCoef * 2 * PI));
                float2 nextPos = f2Make(center.x + radius * cosf(nextCoef * 2 * PI), center.y + radius * sinf(nextCoef * 2 * PI));

                nk_color centerColor = nk_hsva_f(0, 0, hsva[2], 1.0f);
                nk_color currentColor = nk_hsva_f(currCoef, 1, hsva[2], 1.0f);
                nk_color nextColor = nk_hsva_f(nextCoef, 1, hsva[2], 1.0f);

                nk_fill_triangle_multi_color(o, center.x, center.y, currPos.x, currPos.y, nextPos.x, nextPos.y, centerColor, currentColor,
                                             nextColor);
            }

            nk_stroke_circle(o, borderBound, border, nk_rgb_factor(borderColor, colorFactorBackground));
        }
        else
        {
            if (nk_button_behavior(&ctx->last_widget_state, mainMatrix, in, NK_BUTTON_REPEATER))
            {
                hsva[1] = NK_SATURATE((in->mouse.pos.x - mainMatrix.x) / (mainMatrix.w - 1));
                hsva[2] = 1.0f - NK_SATURATE((in->mouse.pos.y - mainMatrix.y) / (mainMatrix.h - 1));
                wasChanged = true;
            }
            else if (stickAction.mDeltaLeft != float2(0, 0))
            {
                hsva[1] = NK_SATURATE(hsva[1] + stickAction.mDeltaLeft.x);
                hsva[2] = NK_SATURATE(hsva[2] + stickAction.mDeltaLeft.y);
                wasChanged = true;
            }

            struct nk_color temp = nk_hsv_f(hsva[0], 1.0f, 1.0f);
            nk_fill_rect_multi_color(o, mainMatrix, nk_white, temp, temp, nk_white);
            nk_fill_rect_multi_color(o, mainMatrix, black_trans, black_trans, nk_black, nk_black);
            nk_stroke_rect(o, borderBound, 0, border, nk_rgb_factor(borderColor, colorFactorBackground));
        }

        nk_override_anti_aliasing(o, false, NK_ANTI_ALIASING_ON);

        {
            struct nk_vec2 p;
            float          S = hsva[1];
            float          V = hsva[2];
            if (drawCircle)
            {
                p.x = center.x + hsva[1] * radius * cosf(hsva[0] * 2 * PI);
                p.y = center.y + hsva[1] * radius * sinf(hsva[0] * 2 * PI);
            }
            else
            {
                p.x = (float)(int)(mainMatrix.x + S * mainMatrix.w);
                p.y = (float)(int)(mainMatrix.y + (1.0f - V) * mainMatrix.h);
            }
            float scale = maxf(pUserInterface->mDpiScale[0], pUserInterface->mDpiScale[1]);
            float thickness = 1.0f * scale;
            float length = 6.0f * scale;
            float gap = 2.0f;
            float outline = 1.0f * scale;

            float bg_thickness = thickness + (outline * 2.0f);
            float bg_length = length + (outline * 2.0f);
            float half_thick = thickness / 2.0f;

            nk_fill_rect(o, nk_rect(p.x - gap - length - outline, p.y - half_thick - outline, bg_length, bg_thickness), 0, nk_black);
            nk_fill_rect(o, nk_rect(p.x - gap - length, p.y - half_thick, length, thickness), 0, nk_white);

            nk_fill_rect(o, nk_rect(p.x + gap - outline, p.y - half_thick - outline, bg_length, bg_thickness), 0, nk_black);
            nk_fill_rect(o, nk_rect(p.x + gap, p.y - half_thick, length, thickness), 0, nk_white);

            nk_fill_rect(o, nk_rect(p.x - half_thick - outline, p.y + gap - outline, bg_thickness, bg_length), 0, nk_black);
            nk_fill_rect(o, nk_rect(p.x - half_thick, p.y + gap, thickness, length), 0, nk_white);

            nk_fill_rect(o, nk_rect(p.x - half_thick - outline, p.y - gap - length - outline, bg_thickness, bg_length), 0, nk_black);
            nk_fill_rect(o, nk_rect(p.x - half_thick, p.y - gap - length, thickness, length), 0, nk_white);
        }
    }
    EndGamepadWidget();

    // bar
    nk_layout_space_push(ctx, bar);

    float gamepadBarInput = UnpackWidgetHorizontalActionForSliderf(BeginGamepadWidget(0, 0, false, true), 0, 255, 1) / 255.0f;

    state = nk_widget(&bar, ctx);
    in = (state == NK_WIDGET_ROM || state == NK_WIDGET_DISABLED || layout->flags & NK_WINDOW_ROM) ? 0 : &ctx->input;

    nk_stroke_rect(o, bar, 0, border, nk_rgb_factor(borderColor, colorFactorBackground));

    float lineWidth = bar.w;
    float lineStart = bar.x;

    bar.x += border;
    bar.y += border;
    bar.w -= border * 2;
    bar.h -= border * 2;
    float value;

    if (drawCircle)
    {
        // value bar
        value = 1.0f - hsva[2];
        if (nk_button_behavior(&ctx->last_widget_state, bar, in, NK_BUTTON_REPEATER))
        {
            hsva[2] = 1.0f - NK_SATURATE((in->mouse.pos.y - bar.y) / (bar.h - 1));
            wasChanged = true;
        }
        else if (gamepadBarInput != 0)
        {
            hsva[2] = NK_SATURATE(hsva[2] - gamepadBarInput);
            wasChanged = true;
        }

        nk_color startColor = nk_hsva_f(hsva[0], 1, 1, 1);
        nk_color endColor = nk_hsva_f(hsva[0], 1, 0, 1);

        nk_fill_rect_multi_color(o, bar, startColor, startColor, endColor, endColor);
    }
    else
    {
        // hue bar
        if (nk_button_behavior(&ctx->last_widget_state, bar, in, NK_BUTTON_REPEATER))
        {
            hsva[0] = NK_SATURATE((in->mouse.pos.y - bar.y) / (bar.h - 1));
            wasChanged = true;
        }
        else if (gamepadBarInput != 0)
        {
            hsva[0] = NK_CLAMP(0, hsva[0] + gamepadBarInput, 1.0f - 1.0f / 360.0f);
            wasChanged = true;
        }

        value = hsva[0];

        for (uint32_t i = 0; i < 6; ++i)
        {
            NK_GLOBAL const struct nk_color hue_colors[] = { { 255, 0, 0, 255 },   { 255, 255, 0, 255 }, { 0, 255, 0, 255 },
                                                             { 0, 255, 255, 255 }, { 0, 0, 255, 255 },   { 255, 0, 255, 255 },
                                                             { 255, 0, 0, 255 } };
            nk_fill_rect_multi_color(o, nk_rect(bar.x, bar.y + ((float)i * (bar.h / 6.0f)), bar.w, (float)nk_iceilf((bar.h / 6.0f))),
                                     hue_colors[i], hue_colors[i], hue_colors[i + 1], hue_colors[i + 1]);
        }
    }

    float line_y = (float)(int)(bar.y + value * mainMatrix.h + 0.5f);
    float thickness = 1.0f * pUserInterface->mDpiScale[0];
    float padding = 1.0f;
    nk_fill_rect(o, nk_rect(lineStart - 1, line_y - (thickness / 2.0f) - padding, lineWidth + 1, thickness + (padding * 2.0f)), 0,
                 nk_black);
    nk_stroke_line(o, lineStart + 1, line_y, lineStart + lineWidth, line_y, thickness, nk_rgb(255, 255, 255));
    EndGamepadWidget();

    if (wasChanged)
    {
        *pColor = nk_hsva_colorfv(hsva);
    }
    return wasChanged;
}

static bool doParameterColorSlider(const struct nk_input* in, struct nk_rect content, nk_colorf startColor, nk_colorf endColor,
                                   int32_t* pValue, int32_t max)
{
    bool result = false;

    struct nk_context*        ctx = getContext();
    struct nk_command_buffer* o = &ctx->current->buffer;

    const float    border = ctx->style.gradient.border;
    const nk_color borderColor = ctx->style.gradient.border_color;
    const float    colorFactorBackground = ctx->style.gradient.color_factor_background;

    nk_stroke_rect(o, content, 0, border, nk_rgb_factor(borderColor, colorFactorBackground));

    float lineHeight = content.h;
    float lineStart = content.y;

    content.x += border;
    content.y += border;
    content.w -= border * 2;
    content.h -= border * 2;

    if (nk_button_behavior(&ctx->last_widget_state, content, in, NK_BUTTON_REPEATER))
    {
        *pValue = (int32_t)(NK_SATURATE((in->mouse.pos.x - content.x) / (content.w - 1)) * max);
        result = true;
    }

    nk_color nkStartColor = nk_rgba_fv(&startColor.r);
    nk_color nkEndColor = nk_rgba_fv(&endColor.r);
    nk_fill_rect_multi_color(o, content, nkStartColor, nkEndColor, nkEndColor, nkStartColor);

    float line_x = (float)(int)(content.x + (float)(*pValue) / max * content.w + 0.5f);
    float thickness = 1.0f * pUserInterface->mDpiScale[0];
    float padding = 1.0f;
    nk_fill_rect(o, nk_rect(line_x - (thickness / 2.0f) - padding, lineStart - 1, thickness + (padding * 2.0f), lineHeight + 1), 0,
                 nk_black);
    nk_stroke_line(o, line_x, lineStart + 1, line_x, lineStart + lineHeight, thickness, nk_rgb(255, 255, 255));

    return result;
}

static bool doParameterHueSlider(const struct nk_input* in, struct nk_rect content, float saturation, float value, int32_t* pValue,
                                 int32_t max)
{
    bool result = false;

    struct nk_context*        ctx = getContext();
    struct nk_command_buffer* o = &ctx->current->buffer;

    const float    border = ctx->style.gradient.border;
    const nk_color borderColor = ctx->style.gradient.border_color;
    const float    colorFactorBackground = ctx->style.gradient.color_factor_background;

    nk_stroke_rect(o, content, 0, border, nk_rgb_factor(borderColor, colorFactorBackground));

    float lineHeight = content.h;
    float lineStart = content.y;

    content.x += border;
    content.y += border;
    content.w -= border * 2;
    content.h -= border * 2;

    if (nk_button_behavior(&ctx->last_widget_state, content, in, NK_BUTTON_REPEATER))
    {
        *pValue = (int32_t)(NK_SATURATE((in->mouse.pos.x - content.x) / (content.w - 1)) * max);
        result = true;
    }

    for (uint32_t i = 0; i < 6; ++i)
    {
        nk_color currColor = nk_hsva_f((float)i / 6.0f, saturation, value, 1);
        nk_color nextColor = nk_hsva_f((float)(i + 1) / 6.0f, saturation, value, 1);

        nk_fill_rect_multi_color(
            o, nk_rect(content.x + ((float)i * (content.w / 6.0f)), content.y, (float)nk_iceilf((content.w / 6.0f)), content.h), currColor,
            nextColor, nextColor, currColor);
    }

    float line_x = (float)(int)(content.x + (float)(*pValue) / max * content.w + 0.5f);
    float thickness = 1.0f * pUserInterface->mDpiScale[0];
    float padding = 1.0f;
    nk_fill_rect(o, nk_rect(line_x - (thickness / 2.0f) - padding, lineStart - 1, thickness + (padding * 2.0f), lineHeight + 1), 0,
                 nk_black);
    nk_stroke_line(o, line_x, lineStart + 1, line_x, lineStart + lineHeight, thickness, nk_rgb(255, 255, 255));

    return result;
}

typedef struct ParameterColorSliderDesc
{
    bool      mDrawHue;
    float     mSaturation;
    float     mValue;
    nk_colorf mStartColor;
    nk_colorf mEndColor;
} ParameterColorSliderDesc;

static bool doIntColorParameter(nk_context* ctx, int* pValue, int max)
{
    int prev = *pValue;
    nk_property_int(ctx, "#", 0, pValue, max, 1, 0.0f);
    return (*pValue - prev) != 0;
}

static bool doParameterColorSlider(const char* label, struct nk_rect bound, ParameterColorSliderDesc desc, int32_t* pValue, int32_t max)
{
    bool result = false;

    struct nk_context* ctx = getContext();
    struct nk_panel*   layout = ctx->current->layout;

    struct nk_text text;
    text.background = ctx->style.window.background;
    text.padding = nk_vec2(0, 0);
    text.text = ctx->style.text.color;

    int32_t prev = *pValue;
    *pValue = *pValue + (int32_t)UnpackWidgetHorizontalActionForSliderf(BeginGamepadWidget(0, 0, false, true), 0, (float)max, 1.0f);
    *pValue = (int32_t)clampi(*pValue, 0, max);
    if (prev != *pValue)
    {
        result = true;
    }

    if (nk_group_begin(ctx, "", (nk_flags)(NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_NO_PADDING)))
    {
        nk_layout_row_begin(ctx, NK_STATIC, uiCalculateHeightOfTextRows(DEFAULT_ROW_HEIGHT), 3);

        float labelWidth = ctx->style.font->width(ctx->style.font->userdata, ctx->style.font->height, "W", 1);
        nk_layout_row_push(ctx, labelWidth);
        nk_label(ctx, label, NK_TEXT_LEFT);

        bound.w -= labelWidth + ctx->style.window.spacing.x;
        {
            float colorWidth = (bound.w) * 0.7f;
            bound.w -= colorWidth + ctx->style.window.spacing.x;

            nk_layout_row_push(ctx, colorWidth);

            struct nk_rect               content;
            enum nk_widget_layout_states state = nk_widget(&content, ctx);
            if (state)
            {
                const struct nk_input* in =
                    (state == NK_WIDGET_ROM || state == NK_WIDGET_DISABLED || layout->flags & NK_WINDOW_ROM) ? 0 : &ctx->input;

                if (desc.mDrawHue)
                {
                    result |= doParameterHueSlider(in, content, desc.mSaturation, desc.mValue, pValue, max);
                }
                else
                {
                    result |= doParameterColorSlider(in, content, desc.mStartColor, desc.mEndColor, pValue, max);
                }
            }
        }

        float propertyWidth = bound.w;
        nk_layout_row_push(ctx, propertyWidth);
        result |= doIntColorParameter(ctx, pValue, max);
        nk_layout_row_end(ctx);
        nk_group_end(ctx);
    }
    EndGamepadWidget();
    return result;
}

static TFUIWidgetInteraction uiColorPicker(const char* label, TFUIWidgetType type, float* r, float* g, float* b, float* a,
                                           nk_color_format format)
{
    TFUIWidgetInteraction result{};
    saveLastWidgetOffset();
    result.type = TF_WIDGET_INTERACTION_CHANGED;

    struct nk_colorf newColor;
    newColor.r = *r;
    newColor.g = *g;
    newColor.b = *b;
    newColor.a = *a;

    struct nk_context* ctx = getContext();
    struct nk_rect     space = ctx->current->layout->bounds;

    struct nk_rect selectorTypeBound = nk_rect(0, 0, 140, uiCalculateHeightOfTextRows(DEFAULT_ROW_HEIGHT));
    float          rowPadding = uiGetDefaultRowPadding();
    float          offset = selectorTypeBound.h + rowPadding;

    struct nk_rect pickerBound = nk_rect(0, offset, space.w, uiCalculateHeightReflectedWidgets(7));

    const float    paramsPadding = ctx->style.window.padding.x * 3;
    float          paramsStartX = pickerBound.x + pickerBound.h + paramsPadding + ctx->style.font->height * 2;
    struct nk_rect parametersBound =
        nk_rect(paramsStartX, pickerBound.y, space.w - paramsStartX, uiCalculateHeightOfTextRows(DEFAULT_ROW_HEIGHT));

    nk_layout_space_begin(ctx, NK_STATIC, -1, 10);
    nk_layout_space_push(ctx, selectorTypeBound);
    bool active = pUserInterface->mColorPickerEditorData.mType ? true : false;

    BeginGamepadWidget();
    active = nk_check_label(ctx, "HSV", active);
    EndGamepadWidget();

    pUserInterface->mColorPickerEditorData.mType = active ? 1 : 0;
    result.changed = doRGBColorPicker(&newColor, pickerBound, pUserInterface->mColorPickerEditorData.mType);

    int colorValues[4];

    colorValues[0] = (int32_t)(newColor.r * 255.0f);
    colorValues[1] = (int32_t)(newColor.g * 255.0f);
    colorValues[2] = (int32_t)(newColor.b * 255.0f);
    colorValues[3] = (int32_t)(newColor.a * 255.0f);

    ParameterColorSliderDesc desc{};
    bool                     rgbChanged = false;
    {
        desc.mStartColor = { 0, newColor.g, newColor.b, 1 };
        desc.mEndColor = { 1, newColor.g, newColor.b, 1 };
        nk_layout_space_push(ctx, parametersBound);
        rgbChanged |= doParameterColorSlider("R", parametersBound, desc, colorValues + 0, 255);
        parametersBound.y += rowPadding + parametersBound.h;
    }

    {
        desc.mStartColor = { newColor.r, 0, newColor.b, 1 };
        desc.mEndColor = { newColor.r, 1, newColor.b, 1 };
        nk_layout_space_push(ctx, parametersBound);
        rgbChanged |= doParameterColorSlider("G", parametersBound, desc, colorValues + 1, 255);
        parametersBound.y += rowPadding + parametersBound.h;
    }

    {
        desc.mStartColor = { newColor.r, newColor.g, 0, 1 };
        desc.mEndColor = { newColor.r, newColor.g, 1, 1 };
        nk_layout_space_push(ctx, parametersBound);
        rgbChanged |= doParameterColorSlider("B", parametersBound, desc, colorValues + 2, 255);
        parametersBound.y += rowPadding + parametersBound.h;
    }

    result.changed |= rgbChanged;
    if (rgbChanged)
    {
        newColor.r = colorValues[0] / 255.0f;
        newColor.g = colorValues[1] / 255.0f;
        newColor.b = colorValues[2] / 255.0f;
    }

    bool  HSVchanged = false;
    float hue, saturation, value, alpha;
    nk_colorf_hsva_f(&hue, &saturation, &value, &alpha, newColor);

    colorValues[0] = (int32_t)(hue * 360.0f);
    colorValues[1] = (int32_t)(saturation * 255.0f);
    colorValues[2] = (int32_t)(value * 255.0f);

    {
        desc.mDrawHue = true;
        desc.mSaturation = saturation;
        desc.mValue = value;
        nk_layout_space_push(ctx, parametersBound);
        HSVchanged = doParameterColorSlider("H", parametersBound, desc, colorValues + 0, 359);
        result.changed |= HSVchanged;
        parametersBound.y += rowPadding + parametersBound.h;
        desc.mDrawHue = false;
        if (HSVchanged)
        {
            hue = colorValues[0] / 360.0f + FLT_EPSILON;
            newColor = nk_hsva_colorf(hue, saturation, value, newColor.a);
        }
    }

    {
        desc.mStartColor = nk_hsva_colorf(hue, 0, 1, 1);
        desc.mEndColor = nk_hsva_colorf(hue, 1, 1, 1);
        nk_layout_space_push(ctx, parametersBound);
        HSVchanged = doParameterColorSlider("S", parametersBound, desc, colorValues + 1, 255);
        result.changed |= HSVchanged;
        parametersBound.y += rowPadding + parametersBound.h;
        if (HSVchanged)
        {
            saturation = colorValues[1] / 255.0f + FLT_EPSILON;
            newColor = nk_hsva_colorf(hue, saturation, value, newColor.a);
        }
    }

    {
        desc.mStartColor = nk_hsva_colorf(hue, saturation, 0, 1);
        desc.mEndColor = nk_hsva_colorf(hue, saturation, 1, 1);
        nk_layout_space_push(ctx, parametersBound);
        HSVchanged = doParameterColorSlider("V", parametersBound, desc, colorValues + 2, 255);
        result.changed |= HSVchanged;
        parametersBound.y += rowPadding + parametersBound.h;
        if (HSVchanged)
        {
            value = colorValues[2] / 255.0f + FLT_EPSILON;
            newColor = nk_hsva_colorf(hue, saturation, value, newColor.a);
        }
    }

    if (format == NK_RGBA)
    {
        desc.mStartColor = { 0, 0, 0, 1 };
        desc.mEndColor = { 1, 1, 1, 1 };
        nk_layout_space_push(ctx, parametersBound);
        bool alphaChanged = doParameterColorSlider("A", parametersBound, desc, colorValues + 3, 255);
        result.changed |= alphaChanged;
        if (alphaChanged)
        {
            newColor.a = colorValues[3] / 255.0f;
        }
        parametersBound.y += rowPadding + parametersBound.h;
    }

    nk_layout_space_end(ctx);

    if (result.changed)
    {
        *r = newColor.r;
        *g = newColor.g;
        *b = newColor.b;
        *a = newColor.a;
    }
    result.id = getWidgetId(updateLastWidget(label, type));
    return result;
}

static struct nk_vec2 calculateAlignmentPopup(float width, float height, struct nk_vec2 startPos)
{
    const float padding = 20;
    startPos.x -= maxf(startPos.x + width + padding - pUserInterface->mWidth, 0);
    startPos.y -= maxf(startPos.y + height + padding - pUserInterface->mHeight, 0);
    return startPos;
}

static struct nk_rect calculateLocalRectPopup(float width, float height, struct nk_vec2 popupPos)
{
    struct nk_rect windowPos = nk_window_get_content_region(getContext());
    struct nk_vec2 localPos = nk_vec2(popupPos.x - windowPos.x, popupPos.y - windowPos.y);
    return nk_rect(localPos.x, localPos.y, width, height);
}

static bool isPosibleToOpenPoputEditor(TFUIPopupEditorType type)
{
    return pUserInterface->mCurrentlyOpenEditorType == TF_UI_POPOP_EDITOR_NONE || pUserInterface->mCurrentlyOpenEditorType == type;
}

static void popupEditorEnd()
{
    if (pUserInterface->mGamepadWasBack)
    {
        nk_popup_close(getContext());
        pUserInterface->mCurrentlyOpenEditorButton = (uint64_t)-1;
        pUserInterface->mCurrentlyOpenEditorType = TF_UI_POPOP_EDITOR_NONE;
    }
    nk_popup_end(getContext());
}

static TFUIWidgetInteraction colorButtonImpl(const char* label, TFUIWidgetType type, float* r, float* g, float* b, float* a,
                                             nk_color_format format)
{
    nk_color              btnColor = { (nk_byte)(*r * 255), (nk_byte)(*g * 255), (nk_byte)(*b * 255), (nk_byte)(*a * 255) };
    float                 fontHeight = getContext()->style.font->height;
    TFUIWidgetInteraction result = {};
    result.type = TF_WIDGET_INTERACTION_CHANGED;

    if ((nk_button_color(getContext(), btnColor) || pUserInterface->mCurrentlyOpenEditorButton == pUserInterface->mCurrEditorButtonID) &&
        isPosibleToOpenPoputEditor(TF_UI_POPOP_EDITOR_COLOR_PICKER))
    {
        pUserInterface->mCurrentlyOpenEditorButton = pUserInterface->mCurrEditorButtonID;
        pUserInterface->mCurrentlyOpenEditorType = TF_UI_POPOP_EDITOR_COLOR_PICKER;

        const float widthPopup = 26 * fontHeight + uiCalculateHeightReflectedWidgets(7);
        const float heightPopup = nk_panel_get_header_height(&getContext()->style) + uiCalculateHeightReflectedWidgets(9);

        struct nk_vec2 pickerPos = calculateAlignmentPopup(widthPopup, heightPopup, nk_widget_position(getContext()));
        struct nk_rect pickerRect = calculateLocalRectPopup(widthPopup, heightPopup, pickerPos);

        if (nk_popup_begin(getContext(), NK_POPUP_STATIC, "Color picker", NK_WINDOW_CLOSABLE | NK_WINDOW_NO_SCROLLBAR, pickerRect))
        {
            result = uiColorPicker(label, type, r, g, b, a, format);
            popupEditorEnd();
        }
        else
        {
            pUserInterface->mCurrentlyOpenEditorButton = (uint64_t)-1;
            pUserInterface->mCurrentlyOpenEditorType = TF_UI_POPOP_EDITOR_NONE;
        }
    }

    return result;
}

TFUIWidgetInteraction uiColor3Button(float3* color)
{
    float r = color->x;
    float g = color->y;
    float b = color->z;
    float a = 1.0;

    BeginGamepadWidget();
    TFUIWidgetInteraction result = colorButtonImpl("color3_button", TF_WIDGET_TYPE_COLOR3_BUTTON, &r, &g, &b, &a, NK_RGB);
    EndGamepadWidget();

    color->x = r;
    color->y = g;
    color->z = b;

    pUserInterface->mCurrEditorButtonID++;
    return result;
}

TFUIWidgetInteraction uiColorButton(float4* color)
{
    float r = color->x;
    float g = color->y;
    float b = color->z;
    float a = color->w;

    BeginGamepadWidget();
    TFUIWidgetInteraction result = colorButtonImpl("color_button", TF_WIDGET_TYPE_COLOR_BUTTON, &r, &g, &b, &a, NK_RGBA);
    EndGamepadWidget();

    color->x = r;
    color->y = g;
    color->z = b;
    color->w = a;

    pUserInterface->mCurrEditorButtonID++;
    return result;
}

static void removeGradientKey(TFUIGradientColor gradientColor, int32_t idx)
{
    uint32_t* pAllocatedKeyCount = gradientColor.pAllocatedKeyCount;
    int32_t*  pSelectedKeyIdx = gradientColor.pSelectedKeyIdx;

    if (*pAllocatedKeyCount <= 1)
    {
        return;
    }

    for (uint32_t i = idx; i < *pAllocatedKeyCount - 1; i++)
    {
        gradientColor.pKeyValues[i] = gradientColor.pKeyValues[i + 1];
        gradientColor.pKeyColors[i] = gradientColor.pKeyColors[i + 1];
    }

    if (*pSelectedKeyIdx == idx)
    {
        *pSelectedKeyIdx = (int32_t)maxu(0, *pSelectedKeyIdx - 1);
    }

    (*pAllocatedKeyCount)--;

    uiSortGradient(&gradientColor);
}

static void removeGradientKeyCallBack(void* pUserData)
{
    TFUserInterface::GradientContext* context = (TFUserInterface::GradientContext*)pUserData;

    removeGradientKey(context->mGradientData, context->mRemoveKey);
}

static void addGradientKey(TFUIGradientColor gradientColor, float t)
{
    uint32_t* pAllocatedKeyCount = gradientColor.pAllocatedKeyCount;
    int32_t*  pSelectedKeyIdx = gradientColor.pSelectedKeyIdx;

    if (*pAllocatedKeyCount == gradientColor.mMaxKeyCount)
    {
        return;
    }

    nk_gradient gradient{};
    gradient.startKeyValue = gradientColor.mStartKeyValue;
    gradient.endKeyValue = gradientColor.mEndKeyValue;
    gradient.keyColors = (nk_colorf*)gradientColor.pKeyColors;
    gradient.keyValues = gradientColor.pKeyValues;
    gradient.keyCount = *pAllocatedKeyCount;
    gradient.selectedKeyIdx = *pSelectedKeyIdx;

    nk_colorf color = nk_get_color_gradient(&gradient, t);

    uint32_t idx = *pAllocatedKeyCount;

    gradientColor.pKeyValues[idx] = t;
    gradientColor.pKeyColors[idx] = f4Make(color.r, color.g, color.b, color.a);
    (*pAllocatedKeyCount)++;

    gradient.keyCount = *pAllocatedKeyCount;
    nk_sort_gradient_keyframes(&gradient, NULL);
}

static void addGradientKeyCallBack(void* pUserData)
{
    TFUserInterface::GradientContext* context = (TFUserInterface::GradientContext*)pUserData;

    addGradientKey(context->mGradientData, context->mAddKeyCoord);
}

static bool checkAndCorrectGradientData(TFUIGradientColor* gradientData)
{
    nk_bool changed = false;
    ASSERT(gradientData->mMaxKeyCount >= 1);
    ASSERT((gradientData->mMaxKeyCount >= 2 && gradientData->mFixedEndings) || !gradientData->mFixedEndings);

    uint32_t* pAllocatedKeyCount = gradientData->pAllocatedKeyCount;
    int32_t*  pSelectedKeyIdx = gradientData->pSelectedKeyIdx;
    if (*pSelectedKeyIdx == -1)
    {
        *pSelectedKeyIdx = 0;
    }
    if (*pAllocatedKeyCount == 0)
    {
        gradientData->pKeyValues[0] = gradientData->mStartKeyValue;
        gradientData->pKeyColors[0] = f4Make(1, 1, 1, 1);
        *pAllocatedKeyCount = 1;

        changed = true;
    }
    if (gradientData->mFixedEndings && *pAllocatedKeyCount < 2)
    {
        gradientData->pKeyValues[1] = gradientData->mEndKeyValue;
        gradientData->pKeyColors[1] = f4Make(1, 1, 1, 1);
        *pAllocatedKeyCount = 2;

        changed = true;
    }
    if (gradientData->mFixedEndings)
    {
        if (gradientData->pKeyValues[0] != gradientData->mStartKeyValue)
        {
            gradientData->pKeyValues[0] = gradientData->mStartKeyValue;
            changed = true;
        }
        if (gradientData->pKeyValues[*pAllocatedKeyCount - 1] != gradientData->mEndKeyValue)
        {
            gradientData->pKeyValues[*pAllocatedKeyCount - 1] = gradientData->mEndKeyValue;
            changed = true;
        }
    }
    return changed;
}

static bool gradientPickerImp(TFUIGradientColor* gradientData, nk_color_format format)
{
    struct nk_context* ctx = getContext();
    nk_bool            changed = false;

    nk_gradient gradient{};
    gradient.startKeyValue = gradientData->mStartKeyValue;
    gradient.endKeyValue = gradientData->mEndKeyValue;
    gradient.keyColors = (nk_colorf*)gradientData->pKeyColors;
    gradient.keyValues = gradientData->pKeyValues;
    gradient.keyCount = *gradientData->pAllocatedKeyCount;
    gradient.selectedKeyIdx = *gradientData->pSelectedKeyIdx;
    gradient.fixedEndings = (nk_bool)gradientData->mFixedEndings;
    gradient.format = format;

    nk_gradient_input               input{};
    const struct nk_style_gradient* style = &ctx->style.gradient;

    nk_layout_row_dynamic(ctx, style->keyframe_size.y * 4, 1);
    struct nk_rect rawGradientBounds = nk_layout_space_bounds(ctx);
    struct nk_rect gradientBounds = nk_gradient_context_bound(ctx, rawGradientBounds);
    changed = nk_gradient_picker(ctx, &gradient, &input);

    *gradientData->pSelectedKeyIdx = gradient.selectedKeyIdx;

    if (!uiIsOpenContextMenu())
    {
        if (input.hoverKey != -1)
        {
            pUserInterface->mContextMenuDeleteGradientData.mRemoveKey = input.hoverKey;

            uiRemoveContextMenu();
            uiAddContextMenu(pUserInterface->mContextMenuGradientItems, 1, f2Make(80, 30), false);
        }

        else if (input.isHoverGradient)
        {
            pUserInterface->mContextMenuDeleteGradientData.mAddKeyCoord = input.mousePosGradientValue;

            uiRemoveContextMenu();
            uiAddContextMenu(pUserInterface->mContextMenuGradientItems + 1, 1, f2Make(80, 30), false);
        }
        struct nk_mouse_button doubleButton = getContext()->input.mouse.buttons[NK_BUTTON_DOUBLE];
        if (input.isHoverGradient && doubleButton.down)
        {
            addGradientKey(*gradientData, input.mousePosGradientValue);
        }
    }
    pUserInterface->mContextMenuDeleteGradientData.mGradientData = *gradientData;

    struct nk_rect space = ctx->current->layout->bounds;
    nk_layout_space_begin(ctx, NK_STATIC, 0, 1);
    nk_layout_space_push(ctx, nk_rect(gradientBounds.x - rawGradientBounds.x, 0, gradientBounds.w, space.h));
    // coord
    if (nk_group_begin(ctx, "", (nk_flags)(NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_NO_PADDING)))
    {
        float coordLabelWidth = (float)uiGetTextWidth("-Coord-");
        float deletButtonWidth = (float)uiGetTextWidth("--Delete--");
        float coordWidth = gradientBounds.w - coordLabelWidth - deletButtonWidth - ctx->style.window.spacing.x * 2;
        uiLayoutRowBegin(TFUILayoutFormat::TF_LAYOUT_STATIC, uiCalculateHeightOfTextRows(DEFAULT_ROW_HEIGHT), 4);
        uiLayoutRowPush(coordLabelWidth);
        nk_label(ctx, "Coord", NK_TEXT_ALIGN_LEFT | NK_TEXT_ALIGN_MIDDLE);
        uiLayoutRowPush(coordWidth);
        nk_bool coordWasChanged = UI_WIDGET_IS_CHANGED(uiSliderFloat(&gradientData->pKeyValues[*gradientData->pSelectedKeyIdx],
                                                                     gradientData->mStartKeyValue, gradientData->mEndKeyValue, 0.01f));
        uiLayoutRowPush(deletButtonWidth);
        if (nk_button_label(ctx, "Delete"))
        {
            removeGradientKey(*gradientData, *gradientData->pSelectedKeyIdx);
        }
        uiLayoutRowEnd();

        if (coordWasChanged)
        {
            nk_sort_gradient_keyframes(&gradient, NULL);
            *gradientData->pSelectedKeyIdx = gradient.selectedKeyIdx;
        }

        changed |= coordWasChanged;

        nk_colorf keyColor = gradient.keyColors[gradient.selectedKeyIdx];
        changed |= UI_WIDGET_IS_CHANGED(uiColorPicker("colorpicker",
                                                      format == NK_RGBA ? TF_WIDGET_TYPE_COLOR_BUTTON : TF_WIDGET_TYPE_COLOR3_BUTTON,
                                                      &keyColor.r, &keyColor.g, &keyColor.b, &keyColor.a, format));
        gradient.keyColors[gradient.selectedKeyIdx] = keyColor;

        nk_group_end(ctx);
    }
    nk_layout_space_end(ctx);
    *gradientData->pSelectedKeyIdx = gradient.selectedKeyIdx;

#ifndef NK_INCLUDE_STANDARD_BOOL
    return (bool)changed;
#else
    return changed;
#endif
}

static TFUIWidgetInteraction gradientButtonImp(TFUIGradientColor* gradientData, nk_color_format format)
{
    TFUIWidgetInteraction result = {};
    result.type = TF_WIDGET_INTERACTION_CHANGED;
    result.changed = checkAndCorrectGradientData(gradientData);

    nk_gradient gradient{};
    gradient.startKeyValue = gradientData->mStartKeyValue;
    gradient.endKeyValue = gradientData->mEndKeyValue;
    gradient.keyColors = (nk_colorf*)gradientData->pKeyColors;
    gradient.keyValues = gradientData->pKeyValues;
    gradient.keyCount = *gradientData->pAllocatedKeyCount;
    gradient.selectedKeyIdx = *gradientData->pSelectedKeyIdx;

    if ((nk_button_gradient(getContext(), &gradient) ||
         pUserInterface->mCurrentlyOpenEditorButton == pUserInterface->mCurrEditorButtonID) &&
        isPosibleToOpenPoputEditor(TF_UI_POPOP_EDITOR_GRADIENT_PICKER))
    {
        pUserInterface->mCurrentlyOpenEditorButton = pUserInterface->mCurrEditorButtonID;
        pUserInterface->mCurrentlyOpenEditorType = TF_UI_POPOP_EDITOR_GRADIENT_PICKER;

        const float    widthPopup = 500;
        const float    heightPopup = 400;
        struct nk_vec2 pickerPos = calculateAlignmentPopup(widthPopup, heightPopup, nk_widget_position(getContext()));
        struct nk_rect pickerRect = calculateLocalRectPopup(widthPopup, heightPopup, pickerPos);

        if (nk_popup_begin(getContext(), NK_POPUP_STATIC, "Gradient picker", NK_WINDOW_CLOSABLE | NK_WINDOW_NO_SCROLLBAR, pickerRect))
        {
            result.changed |= gradientPickerImp(gradientData, format);
            popupEditorEnd();
        }
        else
        {
            pUserInterface->mCurrentlyOpenEditorButton = (uint64_t)-1;
            pUserInterface->mCurrentlyOpenEditorType = TF_UI_POPOP_EDITOR_NONE;
        }
    }

    result.id = getWidgetId(updateLastWidget("gradientbutton", TF_WIDGET_TYPE_GRADIENT_RGBA32));
    return result;
}

TFUIWidgetInteraction uiGradientColor3Button(TFUIGradientColor* gradientData)
{
    TFUIWidgetInteraction result = gradientButtonImp(gradientData, NK_RGB);
    pUserInterface->mCurrEditorButtonID++;
    return result;
}

TFUIWidgetInteraction uiGradientColor4Button(TFUIGradientColor* gradientData)
{
    TFUIWidgetInteraction result = gradientButtonImp(gradientData, NK_RGBA);
    pUserInterface->mCurrEditorButtonID++;
    return result;
}

float4 uiGetGradientColor(TFUIGradientColor* gradientData, float value)
{
    nk_gradient gradient{};
    gradient.startKeyValue = gradientData->mStartKeyValue;
    gradient.endKeyValue = gradientData->mEndKeyValue;
    gradient.keyColors = (nk_colorf*)gradientData->pKeyColors;
    gradient.keyValues = gradientData->pKeyValues;
    gradient.keyCount = *gradientData->pAllocatedKeyCount;
    gradient.selectedKeyIdx = *gradientData->pSelectedKeyIdx;

    nk_colorf col = nk_get_color_gradient(&gradient, value);
    return f4Make(col.r, col.g, col.b, col.a);
}

void uiSortGradient(TFUIGradientColor* gradientData)
{
    nk_gradient gradient{};
    gradient.startKeyValue = gradientData->mStartKeyValue;
    gradient.endKeyValue = gradientData->mEndKeyValue;
    gradient.keyColors = (nk_colorf*)gradientData->pKeyColors;
    gradient.keyValues = gradientData->pKeyValues;
    gradient.keyCount = *gradientData->pAllocatedKeyCount;
    gradient.selectedKeyIdx = *gradientData->pSelectedKeyIdx;

    nk_sort_gradient_keyframes(&gradient, NULL);

    *gradientData->pSelectedKeyIdx = gradient.selectedKeyIdx;
}

static nk_plugin_filter getNkFilter(TFUIWidgetEditFilter textFilter)
{
    switch (textFilter)
    {
    case TF_WIDGET_EDIT_FILTER_NONE:
        return nk_filter_default;
    case TF_WIDGET_EDIT_FILTER_ASCII:
        return nk_filter_ascii;
    case TF_WIDGET_EDIT_FILTER_FLOAT:
        return nk_filter_float;
    case TF_WIDGET_EDIT_FILTER_DECIMAL:
        return nk_filter_decimal;
    case TF_WIDGET_EDIT_FILTER_HEX:
        return nk_filter_hex;
    case TF_WIDGET_EDIT_FILTER_OCT:
        return nk_filter_oct;
    case TF_WIDGET_EDIT_FILTER_BINARY:
        return nk_filter_binary;
    default:
        return nk_filter_default;
    }
}

TFUIWidgetInteraction uiTextbox(const char* label, bstring* text, TFUIWidgetEditFilter textFilter)
{
    ASSERT(text);
    ASSERT(bcapacity(text));

    TFUIWidgetInteraction result;
    saveLastWidgetOffset();
    result.type = TF_WIDGET_INTERACTION_EDIT_EVENTS;

    nk_label(getContext(), label, NK_TEXT_LEFT);
    result.editEvents =
        nk_edit_string(getContext(), NK_EDIT_BOX, (char*)text->data, &text->slen, bcapacity(text) - 1, getNkFilter(textFilter));
    bdata(text)[blength(text)] = 0;

    result.id = getWidgetId(updateLastWidget(label, TF_WIDGET_TYPE_TEXTBOX));
    return result;
}

TFUIWidgetInteraction uiDynamicText(bstring* text, float4 color, TFUITextMode mode, TFUIAlignmentText alignment)
{
    ASSERT(text);

    struct nk_context*    ctx = getContext();
    TFUIWidgetInteraction result;
    int                   textLen = blength(text);
    saveLastWidgetOffset();
    result.type = TF_WIDGET_INTERACTION_UNUSED;
    struct nk_color nkColor = float4ToNkColor(color);

    switch (mode)
    {
    case TF_TEXT_MODE_WRAPPED:
        nk_text_wrap_colored(ctx, (char*)text->data, textLen, nkColor);
        break;
    case TF_TEXT_MODE_ALIGNED:
    {
        constexpr int bufferSize = 1024;
        ASSERT(textLen < bufferSize);
        char        currentLineStr[bufferSize] = {};
        char*       currentTextData = (char*)text->data;
        const char* endTextData = currentTextData + textLen;
        while (bufferedGetLine(currentLineStr, &currentTextData, endTextData))
        {
            nk_text_colored(ctx, currentLineStr, (int)strlen(currentLineStr), alignmentConvertToNk(alignment), nkColor);
        }
        break;
    }
    }

    result.id = getWidgetId(updateLastWidget("dynamic_text", TF_WIDGET_TYPE_TEXTBOX));
    return result;
}

TFUIWidgetInteraction uiFilledRect(float4 color, float rounding)
{
    TFUIWidgetInteraction result;
    saveLastWidgetOffset();
    result.type = TF_WIDGET_INTERACTION_VISIBLE;

    struct nk_command_buffer*    canvas = nk_window_get_canvas(getContext());
    struct nk_rect               space;
    enum nk_widget_layout_states state = nk_widget(&space, getContext());
    struct nk_color              nkColor = float4ToNkColor(color);
    nk_fill_rect(canvas, space, rounding, nkColor);

    result.visible = state == NK_WIDGET_ROM || state == NK_WIDGET_VALID;
    result.id = getWidgetId(updateLastWidget("filled_rect", TF_WIDGET_TYPE_FILLED_RECT));
    return result;
}

void uiSpacer(uint32_t widgetCount)
{
    for (uint32_t i = 0; i < widgetCount; i++)
    {
        nk_spacer(getContext());
    }
}

bool uiIsOpenContextMenu() { return pUserInterface->mContextMenu.mOpenedContextMenu; }

bool uiAddContextMenu(TFUIContextMenuItem* pItems, uint32_t countItems, float2 size, bool useScrollbar)
{
    if (pUserInterface->mContextMenu.mOpenedContextMenu || pUserInterface->mContextMenu.mCreated)
    {
        return false;
    }

    pUserInterface->mContextMenu.mOpenedContextMenu = false;
    pUserInterface->mContextMenu.mCreated = true;
    pUserInterface->mContextMenu.pItems = pItems;
    pUserInterface->mContextMenu.mCountItems = countItems;
    pUserInterface->mContextMenu.mPosition = f2Make(0, 0);
    pUserInterface->mContextMenu.mSize = size;
    pUserInterface->mContextMenu.mRealSize = f2Make(0, 0);
    pUserInterface->mContextMenu.mUseScrollbar = useScrollbar;

    return true;
}

bool uiRemoveContextMenu()
{
    if (pUserInterface->mContextMenu.mOpenedContextMenu || !pUserInterface->mContextMenu.mCreated)
    {
        return false;
    }

    pUserInterface->mContextMenu.mCreated = false;
    pUserInterface->mContextMenu.mOpenedContextMenu = false;

    return true;
}

static void uiOpenContextMenu()
{
    // disable input for other windows
    pUserInterface->mCommonWindowFlags = NK_WINDOW_NOT_INTERACTIVE;
    pUserInterface->mContextMenu.mOpenedContextMenu = true;

    // clear stack
    while (arrlen(pUserInterface->mContextMenu.pTabStack))
    {
        arrpop(pUserInterface->mContextMenu.pTabStack);
    }
}

static void uiCloseContextMenu()
{
    pUserInterface->mCommonWindowFlags = 0;
    pUserInterface->mContextMenu.mOpenedContextMenu = false;
}

void uiUpdateContextMenu()
{
    nk_mouse_button* leftB = &getContext()->input.mouse.buttons[NK_BUTTON_LEFT];
    nk_mouse_button* rightB = &getContext()->input.mouse.buttons[NK_BUTTON_RIGHT];
    bool   openContextMenu = rightB->clicked && !pUserInterface->mContextMenu.mOpenedContextMenu && pUserInterface->mContextMenu.mCreated;
    bool   tryCloseContextMenu = leftB->clicked && pUserInterface->mContextMenu.mOpenedContextMenu;
    float2 position = pUserInterface->mContextMenu.mPosition;
    float2 size = pUserInterface->mContextMenu.mSize;
    float2 realSize = pUserInterface->mContextMenu.mRealSize;
    if (tryCloseContextMenu)
    {
        float4 bound = f4Make(position.x, position.y, position.x + realSize.x, position.y + realSize.y);
        if (leftB->clicked_pos.x >= bound.x && leftB->clicked_pos.x <= bound.z && leftB->clicked_pos.y >= bound.y &&
            leftB->clicked_pos.y <= bound.w)
        {
            tryCloseContextMenu = false;
        }
    }

    if (openContextMenu)
    {
        position = f2Make(rightB->clicked_pos.x, rightB->clicked_pos.y);
        pUserInterface->mContextMenu.mPosition = position;

        uiOpenContextMenu();
    }
    if (tryCloseContextMenu)
    {
        uiCloseContextMenu();
    }

    if (pUserInterface->mContextMenu.mOpenedContextMenu)
    {
        if (nk_begin(getContext(), "contextmenuwindow", nk_rect(0, 0, 1, 1), TF_UI_WINDOW_NO_SCROLLBAR))
        {
            float2 updatedPos = f2Make(position.x - maxf(position.x + realSize.x - pUserInterface->mWidth, 0),
                                       position.y - maxf(position.y + realSize.y - pUserInterface->mHeight, 0));

            if (nk_popup_begin(getContext(), NK_POPUP_DYNAMIC, "",
                               pUserInterface->mContextMenu.mUseScrollbar ? 0 : TF_UI_WINDOW_NO_SCROLLBAR,
                               nk_rect(updatedPos.x, updatedPos.y, size.x, size.y)))
            {
                uint32_t             itemsCount = pUserInterface->mContextMenu.mCountItems;
                TFUIContextMenuItem* pItems = pUserInterface->mContextMenu.pItems;
                for (uint32_t i = 0; i < arrlen(pUserInterface->mContextMenu.pTabStack); i++)
                {
                    uint32_t nextTab = pUserInterface->mContextMenu.pTabStack[i];
                    itemsCount = pItems[nextTab].mTabData.mMemberCount;
                    pItems = pItems[nextTab].mTabData.pMembers;
                }

                uiLayoutAutoTextRows(1);
                for (uint32_t i = 0; i < itemsCount; i++)
                {
                    TFUIContextMenuItem item = pItems[i];

                    if (item.mType == TF_UI_CONTEXT_MENU_TAB && nk_combo_item_label(getContext(), item.pName, NK_TEXT_ALIGN_CENTERED))
                    {
                        arrpush(pUserInterface->mContextMenu.pTabStack, i);
                    }
                    else if (item.mType == TF_UI_CONTEXT_MENU_ITEM && nk_combo_item_label(getContext(), item.pName, NK_TEXT_ALIGN_LEFT))
                    {
                        if (item.mItemData.pOnSelect != NULL)
                        {
                            item.mItemData.pOnSelect(item.mItemData.pOnSelectUserData);
                        }
                        uiCloseContextMenu();
                    }
                }
                struct nk_rect bound = nk_calculate_panel_bound(getContext());
                pUserInterface->mContextMenu.mRealSize = f2Make(bound.w, bound.h);
                nk_popup_end(getContext());
            }
            else
            {
                uiCloseContextMenu();
            }
        }
        nk_end(getContext());
    }
}

void uiMessageBox(const TFUIMessageBoxInfo* pInfo)
{
    if (pUserInterface->mUIMessageBoxData.mMessageBoxOpened == false)
    {
        pUserInterface->mMessageBoxInfo = *pInfo;
        pUserInterface->mUIMessageBoxData.mMessageBoxOpened = true;
        pUserInterface->mUIMessageBoxData.mSize = f2Make(250, 120);
        pUserInterface->mUIMessageBoxData.mDistBetweenButtons = 10.0f;
        pUserInterface->mUIMessageBoxData.pButtonName[0] = "Ok";
        pUserInterface->mUIMessageBoxData.pButtonName[1] = "Yes";
        pUserInterface->mUIMessageBoxData.pButtonName[2] = "No";
        pUserInterface->mUIMessageBoxData.pButtonName[3] = "Close";

        for (uint32_t i = 0; i < TF_UI_MESSAGE_BOX_BUTTON_COUNT; i++)
        {
            pUserInterface->mUIMessageBoxData.mButtonUsed[i] = false;
            pUserInterface->mUIMessageBoxData.mButtonWidth[i] = 0;
        }

        struct nk_context*         ctx = getContext();
        const struct nk_user_font* font = ctx->style.font;
        const float                distBetweenButtons = 10;
        const float                minButtonWidth = 70;

        uint32_t buttonCount = 0;
        float    totalButtonWidth = 0;
        for (uint32_t i = 0; i < sizeof(uint32_t) * 8; i++)
        {
            if ((1 << i) & pUserInterface->mMessageBoxInfo.mFlags)
            {
                const char* pName = pUserInterface->mUIMessageBoxData.pButtonName[i];
                float       width = font->width(font->userdata, font->height, pName, nk_strlen(pName));
                width += (ctx->style.button.padding.x + ctx->style.button.rounding + ctx->style.button.border) * 2;
                width = maxf(width, minButtonWidth);

                buttonCount++;
                totalButtonWidth += width;
                pUserInterface->mUIMessageBoxData.mButtonUsed[i] = true;
                pUserInterface->mUIMessageBoxData.mButtonWidth[i] = width;
            }
        }
        totalButtonWidth += (buttonCount - 1) * distBetweenButtons;
        pUserInterface->mUIMessageBoxData.totalWidth = totalButtonWidth;
    }
}

static void closeMessageBoxWindow()
{
    pUserInterface->mUIMessageBoxData.mMessageBoxOpened = false;
    pUserInterface->mCommonWindowFlags = 0;
}

static void updateMessageBoxWindow()
{
    if (pUserInterface->mUIMessageBoxData.mMessageBoxOpened)
    {
        float2                     size = pUserInterface->mUIMessageBoxData.mSize;
        struct nk_context*         ctx = getContext();
        const struct nk_user_font* font = ctx->style.font;
        if (nk_begin(ctx, pUserInterface->mMessageBoxInfo.pTitle,
                     nk_rect((pUserInterface->mWidth - size.x) / 2, (pUserInterface->mHeight - size.y) / 2, size.x, size.y),
                     TF_UI_WINDOW_NO_SCROLLBAR | TF_UI_WINDOW_BORDER | TF_UI_WINDOW_MOVABLE | TF_UI_WINDOW_TITLE))
        {
            pUserInterface->mCommonWindowFlags = TF_UI_WINDOW_BACKGROUND | TF_UI_WINDOW_NO_INPUT;

            struct nk_panel* layout = ctx->current->layout;
            const float      distBetweenButtons = pUserInterface->mUIMessageBoxData.mDistBetweenButtons;
            const float      buttonHeight = font->height + 15;
            const float contentHeight = layout->bounds.h - buttonHeight - 2 * (ctx->style.window.padding.y + ctx->style.window.spacing.y);

            nk_layout_space_begin(ctx, NK_STATIC, contentHeight, 1);
            nk_layout_space_push(ctx, nk_rect(0, 0, layout->bounds.w, contentHeight));
            if (nk_group_begin(ctx, "", NK_WINDOW_NOT_INTERACTIVE | NK_WINDOW_NO_SCROLLBAR))
            {
                uiLayoutAutoTextRows(1);
                nk_text_wrap_colored(ctx, pUserInterface->mMessageBoxInfo.pText, nk_strlen(pUserInterface->mMessageBoxInfo.pText),
                                     nk_white);
                nk_group_end(ctx);
            }
            nk_layout_space_end(ctx);

            nk_layout_space_begin(ctx, NK_STATIC, 0, TF_UI_MESSAGE_BOX_BUTTON_COUNT);
            float offset = 0;
            float startX = (layout->bounds.w - pUserInterface->mUIMessageBoxData.totalWidth) / 2;
            for (uint32_t i = 0; i < TF_UI_MESSAGE_BOX_BUTTON_COUNT; i++)
            {
                if (pUserInterface->mUIMessageBoxData.mButtonUsed[i])
                {
                    const char* pName = pUserInterface->mUIMessageBoxData.pButtonName[i];
                    float       width = pUserInterface->mUIMessageBoxData.mButtonWidth[i];

                    nk_layout_space_push(ctx, nk_rect(startX + offset, 0, width, buttonHeight));
                    offset += distBetweenButtons + width;

                    if (nk_button_label(ctx, pName))
                    {
                        if (pUserInterface->mMessageBoxInfo.mCallbacks[i])
                        {
                            pUserInterface->mMessageBoxInfo.mCallbacks[i](pUserInterface->mMessageBoxInfo.pUserDatas[i]);
                        }

                        closeMessageBoxWindow();
                    }
                }
            }
            nk_layout_space_end(ctx);
        }
        nk_end(getContext());
    }
}

void uiWinSpacer(uint32_t widgetCount)
{
    for (uint32_t i = 0; i < widgetCount; i++)
    {
        nk_win_spacer(getContext());
    }
}

TFUIWidgetInteraction uiTooltipText(const char* text)
{
    TFUIWidgetInteraction result;
    saveLastWidgetOffset();
    result.type = TF_WIDGET_INTERACTION_UNUSED;

    nk_tooltip(getContext(), text);

    result.id = getWidgetId(updateLastWidget("tooltip_text", TF_WIDGET_TYPE_TOOLTIP));
    return result;
}

TFUIWidgetInteraction uiTooltipBegin(float tooltipWidth)
{
    TFUIWidgetInteraction result;
    saveLastWidgetOffset();
    result.type = TF_WIDGET_INTERACTION_VISIBLE;

    result.visible = nk_tooltip_begin(getContext(), tooltipWidth);

    result.id = getWidgetId(updateLastWidget("tooltip", TF_WIDGET_TYPE_TOOLTIP));
    return result;
}

void uiTooltipEnd() { nk_tooltip_end(getContext()); }

TFUIWidgetInteraction uiLine(vec2 p0, vec2 p1, float thickness, float4 color)
{
    TFUIWidgetInteraction result;
    saveLastWidgetOffset();
    result.type = TF_WIDGET_INTERACTION_UNUSED;

    struct nk_command_buffer* canvas = nk_window_get_canvas(getContext());
    struct nk_color           nkColor = float4ToNkColor(color);
    struct nk_rect            offset = nk_window_get_content_region(getContext());
    uint32_t                  scrollX = 0;
    uint32_t                  scrollY = 0;
    nk_window_get_scroll(getContext(), &scrollX, &scrollY);
    offset.x -= scrollX;
    offset.y -= scrollY;
    nk_stroke_line(canvas, offset.x + p0[0], offset.y + p0[1], offset.x + p1[0], offset.y + p1[1], thickness, nkColor);

    result.id = getWidgetId(updateLastWidget("line", TF_WIDGET_TYPE_LINE));
    return result;
}

TFUIWidgetInteraction uiCurve(vec2 p0, vec2 ctrl0, vec2 ctrl1, vec2 p1, float thickness, float4 color)
{
    TFUIWidgetInteraction result;
    saveLastWidgetOffset();
    result.type = TF_WIDGET_INTERACTION_UNUSED;

    struct nk_command_buffer* canvas = nk_window_get_canvas(getContext());
    struct nk_color           nkColor = float4ToNkColor(color);
    struct nk_rect            offset = nk_window_get_content_region(getContext());
    uint32_t                  scrollX = 0;
    uint32_t                  scrollY = 0;
    nk_window_get_scroll(getContext(), &scrollX, &scrollY);
    offset.x -= scrollX;
    offset.y -= scrollY;
    nk_stroke_curve(canvas, offset.x + p0[0], offset.y + p0[1], offset.x + ctrl0[0], offset.y + ctrl0[1], offset.x + ctrl1[0],
                    offset.y + ctrl1[1], offset.x + p1[0], offset.y + p1[1], thickness, nkColor);

    result.id = getWidgetId(updateLastWidget("curve", TF_WIDGET_TYPE_CURVE));
    return result;
}

/****************************************************************************/
// MARK: - Gamepad simplified windows layout
/****************************************************************************/

void uiEnableGamepadSimplifiedWindowsLayout()
{
    pUserInterface->mEnabledSWL = true;
    pUserInterface->mWindowSelectionIndex = 0;
}

void uiSetActiveSWL(bool enabled) { pUserInterface->mEnabledSWL = enabled; }

void uiSetAutoSwitchSWL(bool enabled) { pUserInterface->mAutoSwitchSWL = enabled; }

bool uiIsActiveSWL() { return pUserInterface->mEnabledSWL; }

void uiSetMaxWindowsSizeSWL(vec2 screenRatio) { pUserInterface->mMaxWindowSizeScreenRationSWL = screenRatio; }

vec2 uiGetMaxWindowsSizeSWL() { return pUserInterface->mMaxWindowSizeScreenRationSWL; }

void uiSetWindowPositionSWL(TFUIWindowPositionSWL position) { pUserInterface->mWindowPositionSWL = position; }

TFUIWindowPositionSWL uiGetWindowPositionSWL() { return pUserInterface->mWindowPositionSWL; }

/****************************************************************************/
// MARK: - Node Graph Editor
/****************************************************************************/

typedef struct UINodeDescData
{
    TFUINodeCustomWidgets mCustomWdigets;
    TFNodeDataUI          mCustomDataUI;
    const char*           pName;
    float2                mStartCustomData;
    float2                mStartDrawReflectionWidgets;
    float2                mSize;
    float2                mSizeCustomData;
    float2                mSizeReflectionWidgets;
    float2                mInputPortStart;
    float2                mOutputPortStart;
    float                 mInputPortStep;
    float                 mOutputPortStep;
    TFUINodeDescFlags     mFlags;

    struct PortWidget
    {
        TFWidgetData* pWidget;
        float4        rect;
    };

    PortWidget            mInputPortWidgets[NODE_GRAPH_PORT_MAX];
    TFReflectedWidgetData mReflectedWidgetData;
    TypeInfoStruct*       pReflectedUserDataInfo;
    uint32_t              mReflectedUserDataOffset;
} UINodeDescData;

typedef struct UINodeDescDataT
{
    int32_t        key;
    UINodeDescData value;
} UINodeDescDataT;

typedef struct UIPortTypeDataT
{
    int32_t  key;
    nk_color value;
} UIPortTypeDataT;

typedef enum UINodeGraphAction
{
    UI_NODE_GRAPH_ACTION_NONE,
    UI_NODE_GRAPH_ACTION_MOVE_VIEW,
    UI_NODE_GRAPH_ACTION_MOVE_NODE,
    UI_NODE_GRAPH_ACTION_CREATING_CONNECTION,
} UINodeGraphAction;

typedef enum UINodeGraphInteract
{
    UI_NODE_GRAPH_INTERACT_NONE,
    UI_NODE_GRAPH_INTERACT_VIEW,
    UI_NODE_GRAPH_INTERACT_NODE,
    UI_NODE_GRAPH_INTERACT_CONNECTION,
    UI_NODE_GRAPH_INTERACT_PORT,
} UINodeGraphInteract;

typedef struct UINodeGraphActionData
{
    UINodeGraphAction mType;

    union
    {
        // move view doesn't have data

        struct
        {
            TFNGNode* pNode;
        } mMoveNode;

        struct
        {
            TFNGNode* pNode;
            uint32_t  mPortIdx;
            bool      mIsInputPort;
        } mCreatingConnection;
    };
} UINodeGraphActionData;

typedef struct UINodeGraphInteractData
{
    UINodeGraphInteract mType;

    union
    {
        struct
        {
            float mMousePos[2];
        } mView;

        struct
        {
            TFNGNode* pNode;
        } mNode;

        struct
        {
            TFNGNode* pNode;
            uint32_t  mPortIdx;
            bool      mIsInputPort;
        } mPort;

        struct
        {
            TFNGNode* pNode;
            uint32_t  mConnectionIdx;
        } mConnection;
    };
} UINodeGraphInteractData;

typedef struct UINodeGraphStyle
{
    float mBorderOffset;
    float mHeightPadding;
    float mPortSize;
    float mActivePortSize;
    float mPortTextPadding;
    float mPortSpace;
    float mSpaceBetweenPorts;
    float mConnectionOffset;
    float mHoveredConnectionFactor;
} UINodeGraphStyle;

typedef struct TFUINodeGraphEditor
{
    TFNodeGraphContext*     pNodeGraphCtx;
    UINodeDescDataT*        pHmNodeDescData;
    UIPortTypeDataT*        pHmTypeColor;
    UINodeGraphInteractData mHoveredData;
    UINodeGraphInteractData mSelectedData;
    UINodeGraphActionData   mActionData;
    UINodeGraphStyle        mStyle;
    float2                  mViewPosition;
    bool                    mWasChanged;

    typedef struct NodeContextMenuData
    {
        TFUINodeGraphEditor* pEditor;
        TFNGNode*            pNode;
    } NodeContextMenuData;

    struct
    {
        TFUINodeGraphEditor::NodeContextMenuData mUserData;
        TFUIContextMenuItem                      mItem;
    } mNodeContextMenu;

    typedef struct ConnectionContextMenuData
    {
        TFUINodeGraphEditor* pEditor;
        TFNGNode*            pNode;
        uint32_t             mConnectionIdx;
    } ConnectionContextMenuData;

    struct
    {
        TFUINodeGraphEditor::ConnectionContextMenuData mUserData;
        TFUIContextMenuItem                            mItem;
    } mConnectionContextMenu;

    typedef struct ViewContextMenuData
    {
        TFUINodeGraphEditor* pEditor;
        uint32_t             mNodeType;
    } ViewContextMenuData;

    struct
    {
        float mLocalMousePos[2];

        TFUINodeGraphEditor::ViewContextMenuData** ppUsersDataArray;
        char**                                     ppTabNameArray;

        // array of items
        TFUIContextMenuItem* pItemArray;
    } mViewContextMenu;
} TFUINodeGraphEditor;

/****************************************************************************/
// MARK: - Node Graph Editor Utility Functions
/****************************************************************************/

static void addBasicNodeDescUI(TFUINodeGraphEditor* pEditor, float2 size, UIDrawCustomNodeCallback callback, TFNodeGraphBasicNode type)
{
    if (ngGetNodeDescByType(ngGetNodeGraphDesc(pEditor->pNodeGraphCtx), type))
    {
        TFUINodeCustomWidgets float4Node{};
        float4Node.mRectSize = size;
        float4Node.pDrawCustomNode = callback;
        uiSetDrawCustomNode(pEditor, &float4Node, type);
    }
}

static UINodeDescData* calculateNodeDescData(TFUINodeGraphEditor* pEditor, const TFNGNodeDesc* pDesc,
                                             const TFUINodeCustomWidgets* pDrawCustomNode)
{
    UINodeDescData data{};
    // copy previous data
    ptrdiff_t      idx = hmgeti(pEditor->pHmNodeDescData, pDesc->mType);
    if (idx != -1)
    {
        data = pEditor->pHmNodeDescData[idx].value;
    }

    struct nk_context*         ctx = getContext();
    struct nk_style*           style = &ctx->style;
    const struct nk_user_font* font = ctx->style.font;

    const float fontHeight = font->height;
    const float portSize = maxf(pEditor->mStyle.mPortSize, fontHeight);
    const float portSpace = pEditor->mStyle.mPortSpace;

    data.mInputPortStep = portSpace;
    data.mOutputPortStep = portSpace;

    // calculate name pointer
    const char* nodeTitle = pDesc->mName;
    {
        uint32_t titleLength = (uint32_t)strlen(pDesc->mName);
        for (uint32_t i = 0; i < titleLength; i++)
        {
            if (pDesc->mName[i] == '/' && i < titleLength - 1)
            {
                nodeTitle = pDesc->mName + i + 1;
            }
        }
    }
    data.pName = nodeTitle;

    bool drawPortReflected = pDrawCustomNode && data.mFlags & TF_UI_NODE_DESC_FLAG_DRAW_PORT_REFLECTED;
    bool drawOtherReflection = pDrawCustomNode && data.mFlags & TF_UI_NODE_DESC_FLAG_DRAW_OTHER_REFLECTION;
    bool drawAnyReflection = drawPortReflected | drawOtherReflection;

    // calculate reflection fields
    uint32_t reflectionWidgetCount = 0;
    if (drawAnyReflection)
    {
        data.pReflectedUserDataInfo = pDesc->pReflectedUserDataInfo;
        data.mReflectedUserDataOffset = pDesc->mReflectedUserDataOffset;

        ASSERT(data.pReflectedUserDataInfo);

        // could be copied from previous calculations
        if (data.mReflectedWidgetData.pWidgets)
        {
            uiRemoveReflectedWidgetData(&data.mReflectedWidgetData);
            data.mReflectedWidgetData = {};
        }

        data.mReflectedWidgetData = uiAddReflectedWidgetData(data.pReflectedUserDataInfo, NULL);
        reflectionWidgetCount = data.mReflectedWidgetData.mWidgetCount;

        for (uint32_t i = 0; i < NODE_GRAPH_PORT_MAX; i++)
        {
            data.mInputPortWidgets[i].pWidget = NULL;
        }

        for (uint32_t i = 0; i < data.mReflectedWidgetData.mWidgetCount; i++)
        {
            TFWidgetData* pWidget = data.mReflectedWidgetData.pWidgets + i;
            int32_t       memberIdx = -1;
            for (int32_t i2 = 0; i2 < (int32_t)data.pReflectedUserDataInfo->mMemberCount; i2++)
            {
                if (pWidget->pMember == data.pReflectedUserDataInfo->pMembers + i2)
                {
                    memberIdx = i2;
                    break;
                }
            }

            if (memberIdx == -1)
            {
                continue;
            }

            if (pWidget->mActive != false)
            {
                for (uint32_t i2 = 0; i2 < pDesc->mInputPortCount; i2++)
                {
                    if (pDesc->pInputPortDesc[i2].mReflectedMemeberIdx == memberIdx)
                    {
                        data.mInputPortWidgets[i2].pWidget = pWidget;
                        pWidget->mActive = false;
                        reflectionWidgetCount--;
                        break;
                    }
                }
            }
        }

        if (!drawOtherReflection)
        {
            reflectionWidgetCount = 0;
        }
    }

    // calculate additional size of node
    float additionalContentWidth = 0;
    float additionalContentHeight = 0;
    float reflectionFieldsHeight = 0;
    bool  skipInputPortName = false;
    if (pDrawCustomNode)
    {
        if (drawAnyReflection)
        {
            reflectionFieldsHeight = uiCalculateHeightReflectedWidgets(reflectionWidgetCount);
            additionalContentHeight += reflectionFieldsHeight;
            skipInputPortName = true;
        }
        if (pDrawCustomNode->pDrawCustomNode)
        {
            additionalContentHeight += pDrawCustomNode->mRectSize.y + uiGetDefaultRowPadding();
        }

        additionalContentWidth = pDrawCustomNode->mRectSize.x;
    }

    // calculate width of node
    float titleNameWidth = font->width(font->userdata, fontHeight, nodeTitle, (int32_t)strlen(nodeTitle));
    titleNameWidth += (style->window.header.padding.x + style->window.header.label_padding.x) * 2.f;

    float maxInputPortNamefloat = 0;
    float inputNameWidths[NODE_GRAPH_NAME_MAX_SIZE] = {};

    for (uint32_t i = 0; i < pDesc->mInputPortCount; i++)
    {
        const char* text = pDesc->pInputPortDesc[i].mName;
        float       width = font->width(font->userdata, fontHeight, text, (int32_t)strlen(text));
        inputNameWidths[i] = width;
        maxInputPortNamefloat = maxf(maxInputPortNamefloat, width);
    }

    float maxOutputPortNamefloat = 0;
    for (uint32_t i = 0; i < pDesc->mOutputPortCount; i++)
    {
        const char* text = pDesc->pOutputPortDesc[i].mName;
        float       width = font->width(font->userdata, fontHeight, text, (int32_t)strlen(text));
        maxOutputPortNamefloat = maxf(maxOutputPortNamefloat, width);
    }

    float width = maxInputPortNamefloat + maxOutputPortNamefloat;
    float inputPortWidth =
        (pDesc->mInputPortCount != 0 ? pEditor->mStyle.mPortSize + pEditor->mStyle.mBorderOffset + pEditor->mStyle.mPortTextPadding : 0);
    width += inputPortWidth;
    inputPortWidth += maxInputPortNamefloat;
    width +=
        (pDesc->mOutputPortCount != 0 ? pEditor->mStyle.mPortSize + pEditor->mStyle.mBorderOffset + pEditor->mStyle.mPortTextPadding : 0);

    if (pDesc->mInputPortCount != 0 && pDesc->mOutputPortCount != 0)
    {
        width += pEditor->mStyle.mSpaceBetweenPorts;
    }
    else
    {
        width += pEditor->mStyle.mBorderOffset;
    }

    // calculate height of node
    float height = maxf(pDesc->mInputPortCount * portSpace + additionalContentHeight, pDesc->mOutputPortCount * portSpace);
    float titleOffset = pEditor->mStyle.mHeightPadding + pEditor->mStyle.mBorderOffset;
    height += titleOffset; // title

    if (pDrawCustomNode)
    {
        float additionalWidth = additionalContentWidth + pEditor->mStyle.mSpaceBetweenPorts;
        width += skipInputPortName ? additionalWidth - maxInputPortNamefloat : additionalWidth;
    }

    width = maxf(width, titleNameWidth);

    data.mSize = float2(width, height);
    data.mInputPortStart = float2(pEditor->mStyle.mBorderOffset, titleOffset);
    data.mInputPortStep = portSpace;
    data.mOutputPortStart = float2(width - pEditor->mStyle.mBorderOffset - pEditor->mStyle.mPortSize, titleOffset);
    data.mOutputPortStep = portSpace;

    if (pDrawCustomNode)
    {
        data.mCustomWdigets = *pDrawCustomNode;
        data.mCustomDataUI.mSize = data.mCustomWdigets.mRectSize;
        data.mInputPortStart.y += additionalContentHeight;
        data.mCustomDataUI.mPortHeight = portSize;
        data.mCustomDataUI.mInputPortStep = portSpace;
        data.mCustomDataUI.mOutputPortStep = portSpace;

        float widthStart;
        if (skipInputPortName)
        {
            widthStart = (pDesc->mInputPortCount != 0 ? pEditor->mStyle.mPortSize + pEditor->mStyle.mBorderOffset : 0) +
                         pEditor->mStyle.mBorderOffset;
        }
        else
        {
            widthStart = pDesc->mInputPortCount != 0 ? (inputPortWidth + pEditor->mStyle.mSpaceBetweenPorts) : 0;
        }

        float customDataOffset = 0;
        if (pDrawCustomNode->pDrawCustomNode)
        {
            data.mSizeCustomData = data.mCustomWdigets.mRectSize;
            data.mStartCustomData = f2Make(widthStart, uiGetDefaultRowPadding());
            customDataOffset += data.mCustomWdigets.mRectSize.y + uiGetDefaultRowPadding();
        }

        if (drawAnyReflection)
        {
            data.mSizeReflectionWidgets.x = data.mCustomWdigets.mRectSize.x;
            data.mSizeReflectionWidgets.y = reflectionFieldsHeight;

            data.mStartDrawReflectionWidgets.x = widthStart;
            data.mStartDrawReflectionWidgets.y = customDataOffset;

            for (uint32_t i = 0; i < NODE_GRAPH_PORT_MAX; i++)
            {
                if (data.mInputPortWidgets[i].pWidget)
                {
                    data.mInputPortWidgets[i].rect =
                        f4Make(data.mStartDrawReflectionWidgets.x, data.mInputPortStart.y - titleOffset + i * data.mInputPortStep,
                               data.mSizeReflectionWidgets.x, uiGetDefaultRowHeight());
                }
            }
        }

        for (uint32_t i = 0; i < pDesc->mInputPortCount; i++)
        {
            data.mCustomDataUI.mInputPortsNameOffset[i] = inputNameWidths[i];
        }
    }

    hmput(pEditor->pHmNodeDescData, pDesc->mType, data);
    idx = hmgeti(pEditor->pHmNodeDescData, pDesc->mType);

    ASSERT(idx != -1);
    return &pEditor->pHmNodeDescData[idx].value;
}

static UINodeDescData* getNodeDescData(TFUINodeGraphEditor* pEditor, TFNGNode* pNode)
{
    TFNGNodeDesc* pDesc = pNode->pDesc;
    ptrdiff_t     idx = hmgeti(pEditor->pHmNodeDescData, pDesc->mType);

    if (idx == -1)
    {
        return calculateNodeDescData(pEditor, pDesc, NULL);
    }
    else
    {
        return &pEditor->pHmNodeDescData[idx].value;
    }
}

static UINodeDescData* getNodeDescByType(TFUINodeGraphEditor* pEditor, int32_t nodeType)
{
    ptrdiff_t idx = hmgeti(pEditor->pHmNodeDescData, nodeType);

    if (idx == -1)
    {
        return calculateNodeDescData(pEditor, ngGetNodeDescByType(ngGetNodeGraphDesc(pEditor->pNodeGraphCtx), nodeType), NULL);
    }
    else
    {
        return &pEditor->pHmNodeDescData[idx].value;
    }
}

static TFUIContextMenuItem* tryFindOrAddNewTab(TFUINodeGraphEditor* pEditor, TFUIContextMenuItem** ppItems, const char* pName,
                                               uint32_t nameLen)
{
    uint32_t len = (uint32_t)arrlen(*ppItems);
    for (uint32_t i = 0; i < len; i++)
    {
        const char* pCheckName = (*ppItems)[i].pName;
        if (memcmp(pCheckName, pName, sizeof(char) * nameLen) == 0)
        {
            return (*ppItems) + i;
        }
    }

    char* pNewName = (char*)tf_calloc(nameLen + 1, sizeof(char));
    memcpy(pNewName, pName, nameLen);
    pNewName[nameLen] = '\0';

    TFUIContextMenuItem newTab{};
    newTab.mType = TF_UI_CONTEXT_MENU_TAB;
    newTab.pName = pNewName;

    arrpush(pEditor->mViewContextMenu.ppTabNameArray, pNewName);
    arrpush((*ppItems), newTab);
    return &(*ppItems)[len]; // uses previous len
}

static TFUIContextMenuItem* addNodeTypeCreation(TFUINodeGraphEditor* pEditor, TFUIContextMenuItem* pItems, UICallback callback,
                                                const char* pName, int32_t type)
{
    int32_t firstSlash = -1;

    uint32_t len = (uint32_t)strlen(pName);
    for (uint32_t i = 0; i < len; i++)

    {
        if (pName[i] == '/')
        {
            firstSlash = i;
            break;
        }
    }

    if (firstSlash != -1)
    {
        // add new tab
        TFUIContextMenuItem* tab = tryFindOrAddNewTab(pEditor, &pItems, pName, firstSlash);
        tab->mTabData.pMembers = addNodeTypeCreation(pEditor, tab->mTabData.pMembers, callback, pName + firstSlash + 1, type);
        tab->mTabData.mMemberCount = (uint32_t)arrlen(tab->mTabData.pMembers);
    }
    else
    {
        TFUINodeGraphEditor::ViewContextMenuData* pUserData =
            (TFUINodeGraphEditor::ViewContextMenuData*)tf_calloc(1, sizeof(TFUINodeGraphEditor::ViewContextMenuData));
        pUserData->pEditor = pEditor;
        pUserData->mNodeType = type;
        arrpush(pEditor->mViewContextMenu.ppUsersDataArray, pUserData);

        TFUIContextMenuItem item{};
        item.pName = pName;
        item.mType = TF_UI_CONTEXT_MENU_ITEM;
        item.mItemData.pOnSelect = callback;
        item.mItemData.pOnSelectUserData = pUserData;
        arrpush(pItems, item);
    }

    return pItems;
}

static void freeTabsOfCreationContextMenu(TFUIContextMenuItem* pItems)
{
    for (uint32_t i = 0; i < (uint32_t)arrlen(pItems); i++)
    {
        if (pItems[i].mType == TF_UI_CONTEXT_MENU_TAB)
        {
            freeTabsOfCreationContextMenu(pItems[i].mTabData.pMembers);
        }
    }

    arrfree(pItems);
}

static void freeCreationContextMenu(TFUINodeGraphEditor* pEditor)
{
    freeTabsOfCreationContextMenu(pEditor->mViewContextMenu.pItemArray);
    pEditor->mViewContextMenu.pItemArray = NULL;

    for (uint32_t i = 0; i < (uint32_t)arrlen(pEditor->mViewContextMenu.ppTabNameArray); i++)
    {
        tf_free(pEditor->mViewContextMenu.ppTabNameArray[i]);
    }
    arrfree(pEditor->mViewContextMenu.ppTabNameArray);
    pEditor->mViewContextMenu.ppTabNameArray = NULL;
}

static void addNodeToNodeGraph(void* pUserData);

static void generateCreationContextMenu(TFUINodeGraphEditor* pEditor)
{
    freeCreationContextMenu(pEditor);

    TFNodeGraphDesc* pNodeGraphDesc = ngGetNodeGraphDesc(pEditor->pNodeGraphCtx);
    for (uint32_t i = 0; i < ngGetNodeDescCount(pNodeGraphDesc); i++)
    {
        const TFNGNodeDesc* desc = ngGetNodeDesc(pNodeGraphDesc, i);
        UINodeDescData*     pData = getNodeDescByType(pEditor, desc->mType);
        if (pData->mFlags & TF_UI_NODE_DESC_FLAG_HIDDEN_CONTEXT_ITEM)
        {
            continue;
        }

        pEditor->mViewContextMenu.pItemArray =
            addNodeTypeCreation(pEditor, pEditor->mViewContextMenu.pItemArray, addNodeToNodeGraph, desc->mName, desc->mType);
    }
}

static struct nk_vec2 getPortGlobalPos(TFUINodeGraphEditor* pEditor, TFNGNode* pNode, uint32_t portIdx, bool isInput)
{
    struct nk_context* ctx = getContext();

    const UINodeDescData* pNodeSizeData = getNodeDescData(pEditor, pNode);
    TFNGUserData*         pData = (TFNGUserData*)pNode->pUserData;
    struct nk_vec2        start = nk_vec2(pData->mPosition[0] + pEditor->mViewPosition.x, pData->mPosition[1] + pEditor->mViewPosition.y);
    start = nk_layout_space_to_screen(ctx, start);

    if (isInput)
    {
        start = nk_vec2(start.x + pNodeSizeData->mInputPortStart.x,
                        start.y + pNodeSizeData->mInputPortStart.y + pNodeSizeData->mInputPortStep * portIdx);
        start.x += pEditor->mStyle.mPortSize / 2.0f;
    }
    else
    {
        start = nk_vec2(start.x + pNodeSizeData->mOutputPortStart.x,
                        start.y + pNodeSizeData->mOutputPortStart.y + pNodeSizeData->mOutputPortStep * portIdx);
        start.x += pEditor->mStyle.mPortSize / 2.0f;
    }

    start.y += ctx->style.font->height / 2.0f;
    return start;
}

static float getDistToLine(struct nk_vec2 start, struct nk_vec2 end, struct nk_vec2 point)
{
    float2 lineSub = f2Sub(f2Make(end.x, end.y), f2Make(start.x, start.y));
    float  lineLength = f2Length(lineSub);
    float2 startToPointSub = f2Sub(f2Make(point.x, point.y), f2Make(start.x, start.y));
    float  projLine = f2Dot(lineSub, startToPointSub) / lineLength; // length of startToPointSub * angle between vectors
    float  t = projLine / lineLength;
    float2 projectedPoint;
    if (t < 0)
    {
        projectedPoint = f2Make(start.x, start.y);
    }
    else if (t > 1)
    {
        projectedPoint = f2Make(end.x, end.y);
    }
    else
    {
        projectedPoint = f2Add(f2MulScalar(lineSub, t), f2Make(start.x, start.y));
    }

    float len = f2Length(f2Sub(projectedPoint, f2Make(point.x, point.y)));
    return len;
}

static nk_color getPortTypeColor(TFUINodeGraphEditor* pEditor, int32_t type)
{
    ptrdiff_t i = hmgeti(pEditor->pHmTypeColor, type);
    if (i == -1)
    {
        return nk_white;
    }
    else
    {
        return pEditor->pHmTypeColor[i].value;
    }
}

/****************************************************************************/
// MARK: - Node Graph Editor Behavior Functions
/****************************************************************************/

static bool mouseIsInNodeGraphEditor(TFUINodeGraphEditor* pEditor) { return pEditor->mHoveredData.mType != UI_NODE_GRAPH_INTERACT_NONE; }

static bool checkIsPossibleToMakeConnection(TFUINodeGraphEditor* pEditor)
{
    if (pEditor->mActionData.mType != UI_NODE_GRAPH_ACTION_CREATING_CONNECTION)
    {
        return false;
    }

    if (pEditor->mHoveredData.mType != UI_NODE_GRAPH_INTERACT_PORT)
    {
        return false;
    }

    if (pEditor->mHoveredData.mPort.pNode == pEditor->mActionData.mCreatingConnection.pNode)
    {
        return false;
    }

    if (pEditor->mHoveredData.mPort.mIsInputPort == pEditor->mActionData.mCreatingConnection.mIsInputPort)
    {
        return false;
    }

    return true;
}

static bool addNodeContextMenu(TFUINodeGraphEditor* pEditor, TFNGNode* pNode)
{
    UINodeDescData* pData = getNodeDescData(pEditor, pNode);
    if ((pData->mFlags & TF_UI_NODE_DESC_FLAG_NO_REMOVABLE) != 0)
    {
        return false;
    }

    pEditor->mNodeContextMenu.mUserData.pNode = pNode;
    return uiAddContextMenu(&pEditor->mNodeContextMenu.mItem, 1, f2Make(100, 30), false);
}

static bool addConnectionContextMenu(TFUINodeGraphEditor* pEditor, TFNGNode* pNode, uint32_t connectionIdx)
{
    pEditor->mNodeContextMenu.mUserData.pNode = pNode;
    pEditor->mConnectionContextMenu.mUserData.pNode = pNode;
    pEditor->mConnectionContextMenu.mUserData.mConnectionIdx = connectionIdx;

    return uiAddContextMenu(&pEditor->mConnectionContextMenu.mItem, 1, f2Make(100, 30), false);
}

static bool addViewContextMenu(TFUINodeGraphEditor* pEditor, float2 mousePos)
{
    // for (uint32_t i = 0; i < ; i++)
    {
    }

    bool res = uiAddContextMenu(pEditor->mViewContextMenu.pItemArray, (uint32_t)arrlen(pEditor->mViewContextMenu.pItemArray),
                                f2Make(150, 200), true);
    if (res)
    {
        pEditor->mViewContextMenu.mLocalMousePos[0] = mousePos.x;
        pEditor->mViewContextMenu.mLocalMousePos[1] = mousePos.y;
    }
    return res;
}

static void resetNodeGraphState(TFUINodeGraphEditor* pEditor, bool resetSelected)
{
    if (resetSelected)
    {
        pEditor->mSelectedData = {};
    }
    pEditor->mHoveredData = {};
    pEditor->mActionData = {};
}

static TFUIWidgetInteraction updateNodeGraphActions(TFUINodeGraphEditor* pEditor)
{
    TFUIWidgetInteraction interaction{};
    interaction.type = TF_WIDGET_INTERACTION_CHANGED;

    struct nk_context*     ctx = getContext();
    const struct nk_input* in = &ctx->input;

    bool mouseLeftReleased = nk_input_is_mouse_released(in, NK_BUTTON_LEFT);

    if (pEditor->mActionData.mType == UI_NODE_GRAPH_ACTION_MOVE_VIEW && mouseLeftReleased)
    {
        pEditor->mActionData.mType = UI_NODE_GRAPH_ACTION_NONE;
    }
    else if (pEditor->mActionData.mType == UI_NODE_GRAPH_ACTION_MOVE_NODE && mouseLeftReleased)
    {
        pEditor->mActionData.mType = UI_NODE_GRAPH_ACTION_NONE;
    }
    else if (pEditor->mActionData.mType == UI_NODE_GRAPH_ACTION_CREATING_CONNECTION && mouseLeftReleased)
    {
        if (checkIsPossibleToMakeConnection(pEditor))
        {
            if (pEditor->mHoveredData.mPort.mIsInputPort)
            {
                TFNGNode* pOutputNode = pEditor->mActionData.mCreatingConnection.pNode;
                uint32_t  outputPortIdx = pEditor->mActionData.mCreatingConnection.mPortIdx;
                TFNGNode* pInputNode = pEditor->mHoveredData.mPort.pNode;
                uint32_t  inputPortIdx = pEditor->mHoveredData.mPort.mPortIdx;
                ngAddConnection(pEditor->pNodeGraphCtx, pOutputNode, outputPortIdx, pInputNode, inputPortIdx, true);
            }
            else
            {
                TFNGNode* pInputNode = pEditor->mActionData.mCreatingConnection.pNode;
                uint32_t  inputPortIdx = pEditor->mActionData.mCreatingConnection.mPortIdx;
                TFNGNode* pOutputNode = pEditor->mHoveredData.mPort.pNode;
                uint32_t  outputPortIdx = pEditor->mHoveredData.mPort.mPortIdx;
                ngAddConnection(pEditor->pNodeGraphCtx, pOutputNode, outputPortIdx, pInputNode, inputPortIdx, true);
            }

            interaction.changed = true;
        }

        pEditor->mActionData.mType = UI_NODE_GRAPH_ACTION_NONE;
    }
    pEditor->mHoveredData.mType = UI_NODE_GRAPH_INTERACT_NONE;

    return interaction;
}

static bool updateNodeGraphHovered(TFUINodeGraphEditor* pEditor, UINodeGraphInteractData newHoveredData)
{
    pEditor->mHoveredData = newHoveredData;
    if (newHoveredData.mType == UI_NODE_GRAPH_INTERACT_NODE)
    {
        uiRemoveContextMenu();
        addNodeContextMenu(pEditor, newHoveredData.mNode.pNode);
    }
    else if (newHoveredData.mType == UI_NODE_GRAPH_INTERACT_CONNECTION)
    {
        uiRemoveContextMenu();
        addConnectionContextMenu(pEditor, newHoveredData.mConnection.pNode, newHoveredData.mConnection.mConnectionIdx);
    }
    else if (newHoveredData.mType == UI_NODE_GRAPH_INTERACT_VIEW)
    {
        uiRemoveContextMenu();
        addViewContextMenu(pEditor, f2Make(newHoveredData.mView.mMousePos[0], newHoveredData.mView.mMousePos[1]));
    }

    return true;
}

static bool isPossibleToStartAction(TFUINodeGraphEditor* pEditor, UINodeGraphActionData newActionData)
{
    if (pEditor->mActionData.mType == UI_NODE_GRAPH_ACTION_NONE)
    {
        if (newActionData.mType == UI_NODE_GRAPH_ACTION_MOVE_VIEW && pEditor->mHoveredData.mType == UI_NODE_GRAPH_INTERACT_VIEW)
        {
            return true;
        }
        else if (newActionData.mType == UI_NODE_GRAPH_ACTION_MOVE_NODE && pEditor->mHoveredData.mType == UI_NODE_GRAPH_INTERACT_NODE)
        {
            return true;
        }
        else if (newActionData.mType == UI_NODE_GRAPH_ACTION_CREATING_CONNECTION &&
                 pEditor->mHoveredData.mType == UI_NODE_GRAPH_INTERACT_PORT)
        {
            return true;
        }
    }
    else if (memcmp(&pEditor->mActionData, &newActionData, sizeof(UINodeGraphActionData)) == 0)
    {
        return true;
    }
    return false;
}

static bool tryUpdateNodeGraphAction(TFUINodeGraphEditor* pEditor, UINodeGraphActionData newActionData)
{
    if (isPossibleToStartAction(pEditor, newActionData))
    {
        pEditor->mActionData = newActionData;
        return true;
    }
    return false;
}

static void selectNode(TFUINodeGraphEditor* pEditor, TFNGNode* pNode)
{
    if (pEditor->mSelectedData.mType != UI_NODE_GRAPH_INTERACT_NODE || pEditor->mSelectedData.mNode.pNode != pNode)
    {
        pEditor->mSelectedData.mType = UI_NODE_GRAPH_INTERACT_NODE;
        pEditor->mSelectedData.mNode.pNode = pNode;

        // selected node was changed, we need to reset main states in window
        struct nk_window* win = getContext()->current;
        memset(&win->property, 0, sizeof(struct nk_property_state));
        memset(&win->edit, 0, sizeof(struct nk_edit_state));
    }
}

static void updateNodeGraphActionPost(TFUINodeGraphEditor* pEditor)
{
    struct nk_context*     ctx = getContext();
    const struct nk_input* in = &ctx->input;

    bool mouseLeftPressed = nk_input_is_mouse_pressed(in, NK_BUTTON_LEFT);

    if (pEditor->mActionData.mType == UI_NODE_GRAPH_ACTION_NONE && mouseLeftPressed)
    {
        if (pEditor->mHoveredData.mType == UI_NODE_GRAPH_INTERACT_PORT)
        {
            selectNode(pEditor, pEditor->mHoveredData.mPort.pNode);
        }
        else if (pEditor->mHoveredData.mType == UI_NODE_GRAPH_INTERACT_NODE)
        {
            selectNode(pEditor, pEditor->mHoveredData.mNode.pNode);
        }
    }
    else if (pEditor->mActionData.mType == UI_NODE_GRAPH_ACTION_MOVE_NODE)
    {
        selectNode(pEditor, pEditor->mActionData.mMoveNode.pNode);
    }
    else if (pEditor->mActionData.mType == UI_NODE_GRAPH_ACTION_MOVE_VIEW)
    {
        pEditor->mSelectedData = {};
    }

    if (pEditor->mHoveredData.mType == UI_NODE_GRAPH_INTERACT_NONE)
    {
        pEditor->mActionData = {};
    }
}

static void removeNodeFromNodeGraph(void* pUserData)
{
    TFUINodeGraphEditor::NodeContextMenuData* pData = (TFUINodeGraphEditor::NodeContextMenuData*)(pUserData);
    resetNodeGraphState(pData->pEditor, true);
    ngRemoveNode(pData->pEditor->pNodeGraphCtx, pData->pNode);

    pData->pEditor->mWasChanged = true;
}

static void removeConnectionFromNodeGraph(void* pUserData)
{
    TFUINodeGraphEditor::ConnectionContextMenuData* pData = (TFUINodeGraphEditor::ConnectionContextMenuData*)(pUserData);
    resetNodeGraphState(pData->pEditor, true);
    ngRemoveConnection(pData->pEditor->pNodeGraphCtx, pData->pNode, pData->mConnectionIdx);

    pData->pEditor->mWasChanged = true;
}

static void addNodeToNodeGraph(void* pUserData)
{
    TFUINodeGraphEditor::ViewContextMenuData data = *(TFUINodeGraphEditor::ViewContextMenuData*)pUserData;

    resetNodeGraphState(data.pEditor, true);
    TFNGNode*     pNode = ngAddNode(data.pEditor->pNodeGraphCtx, data.mNodeType);
    TFNGUserData* pData = (TFNGUserData*)pNode->pUserData;
    pData->mPosition[0] = data.pEditor->mViewContextMenu.mLocalMousePos[0];
    pData->mPosition[1] = data.pEditor->mViewContextMenu.mLocalMousePos[1];

    data.pEditor->mWasChanged = true;
}

/****************************************************************************/
// MARK: - Node Graph Editor Main Do Functions
/****************************************************************************/

static void drawConnection(TFUINodeGraphEditor* pEditor, struct nk_vec2 start, struct nk_vec2 end, nk_color color, bool hovered)
{
    struct nk_context* ctx = getContext();
    nk_command_buffer* canvas = nk_window_get_canvas(ctx);

    nk_color col = hovered ? nk_rgb_factor(color, pEditor->mStyle.mHoveredConnectionFactor) : color;

    float offset = pEditor->mStyle.mConnectionOffset;
    float activePortSize = pEditor->mStyle.mActivePortSize;
    nk_stroke_line(canvas, start.x, start.y, start.x - offset, start.y, activePortSize / 2, col);
    nk_stroke_line(canvas, end.x, end.y, end.x + offset, end.y, activePortSize / 2, col);
    nk_stroke_line(canvas, start.x - offset, start.y, end.x + offset, end.y, activePortSize / 2, col);
}

static TFUIWidgetInteraction doBasicConstNode(TFNGNode* pNode, TFNodeDataUI uiData)
{
    TFUIWidgetInteraction interaction{};
    interaction.type = TF_WIDGET_INTERACTION_CHANGED;

    TFNodeGraphBasicNode nodeType = (TFNodeGraphBasicNode)pNode->mType;
    TFNGUserData128*     pUserData = (TFNGUserData128*)pNode->pUserData;

    int32_t dimOfVector = ngGetVectorDimOfNodeType((TFNodeGraphBasicNode)nodeType);

    float2 size = uiData.mSize;
    if (nodeType == TF_NODE_GRAPH_TOGGLE)
    {
        uiLayoutRowBegin(TF_LAYOUT_STATIC, uiGetDefaultRowHeight(), 1);
        uiLayoutRowPush(size.x);
        bool enabled = pUserData->mSubData[0] != 0 ? true : false;
        interaction.changed |= UI_WIDGET_IS_CHANGED(uiCheckbox("", &enabled));
        pUserData->mSubData[0] = enabled ? ~0 : 0;
        uiLayoutRowEnd();
    }
    else if (nodeType >= TF_NODE_GRAPH_FLOAT && nodeType <= TF_NODE_GRAPH_FLOAT_4)
    {
        uiLayoutRowBegin(TF_LAYOUT_STATIC, uiGetDefaultRowHeight(), 1);
        for (int32_t i = 0; i < dimOfVector; i++)
        {
            uiLayoutRowPush(size.x);
            interaction.changed |= UI_WIDGET_IS_CHANGED(uiSliderFloat(((float*)pUserData->mSubData) + i, -100000, 100000, 0.1f));
        }
        uiLayoutRowEnd();
    }
    else if (nodeType >= TF_NODE_GRAPH_INT && nodeType <= TF_NODE_GRAPH_INT_4)
    {
        uiLayoutRowBegin(TF_LAYOUT_STATIC, uiGetDefaultRowHeight(), 1);

        for (int32_t i = 0; i < dimOfVector; i++)
        {
            uiLayoutRowPush(size.x);
            interaction.changed |= UI_WIDGET_IS_CHANGED(uiPropertyInt(((int32_t*)pUserData->mSubData) + i, INT32_MIN, INT32_MAX, 1));
        }
        uiLayoutRowEnd();
    }

    return interaction;
}

static TFUIWidgetInteraction doPort(TFUINodeGraphEditor* pEditor, struct nk_input* in, const UINodeDescData* pNodeSizeData, TFNGNode* pNode,
                                    bool nodeSelected, struct nk_vec2 globalOrg, uint32_t portIdx, bool isInput)
{
    TFUIWidgetInteraction interaction{};
    interaction.type = TF_WIDGET_INTERACTION_CHANGED;

    struct nk_context*  ctx = getContext();
    nk_command_buffer*  canvas = nk_window_get_canvas(ctx);
    const nk_user_font* font = ctx->style.font;

    float portSize = pEditor->mStyle.mPortSize;
    float activePortSize = pEditor->mStyle.mActivePortSize;
    float textPadding = pEditor->mStyle.mPortTextPadding;
    float textBoundHeiht = maxf(font->height, portSize);

    const char* name = isInput ? pNode->pDesc->pInputPortDesc[portIdx].mName : pNode->pDesc->pOutputPortDesc[portIdx].mName;
    uint32_t    len = nk_strlen(name);
    float       textWidth = font->width(font->userdata, font->height, name, len);

    struct nk_text text;
    text.background = ctx->style.window.background;
    text.padding = nk_vec2(0, 0);
    text.text = ctx->style.text.color;

    struct nk_rect textBound;
    struct nk_rect portBound;
    struct nk_rect activePortBound;

    textBound.w = textWidth;
    textBound.h = textBoundHeiht;
    activePortBound.w = activePortSize;
    activePortBound.h = activePortSize;
    portBound.w = portSize;
    portBound.h = portSize;

    bool isActive = false;

    if (isInput)
    {
        textBound.x = globalOrg.x + pNodeSizeData->mInputPortStart.x + portSize + textPadding;
        textBound.y = globalOrg.y + pNodeSizeData->mInputPortStart.y + portIdx * pNodeSizeData->mInputPortStep;

        portBound.x = globalOrg.x + pNodeSizeData->mInputPortStart.x;
        portBound.y = textBound.y + (font->height - portSize) / 2.0f;

        activePortBound.x = portBound.x + (portSize - activePortSize) / 2.0f;
        activePortBound.y = portBound.y + (portSize - activePortSize) / 2.0f;

        if (pNode->pInputConnections[portIdx].pOutputNode != NULL)
        {
            isActive = true;
        }
    }
    else
    {
        textBound.x = globalOrg.x + pNodeSizeData->mOutputPortStart.x - textPadding - textWidth;
        textBound.y = globalOrg.y + pNodeSizeData->mOutputPortStart.y + portIdx * pNodeSizeData->mOutputPortStep;

        portBound.x = globalOrg.x + pNodeSizeData->mOutputPortStart.x;
        portBound.y = textBound.y + (font->height - portSize) / 2;

        activePortBound.x = portBound.x + (portSize - activePortSize) / 2.0f;
        activePortBound.y = portBound.y + (portSize - activePortSize) / 2.0f;

        if (pNode->pOutputConnectionCounts[portIdx] != 0)
        {
            isActive = true;
        }
    }

    bool isHover = false;

    UINodeGraphActionData portAction{};
    portAction.mType = UI_NODE_GRAPH_ACTION_CREATING_CONNECTION;
    portAction.mCreatingConnection.pNode = pNode;
    portAction.mCreatingConnection.mPortIdx = portIdx;
    portAction.mCreatingConnection.mIsInputPort = isInput;

    if (mouseIsInNodeGraphEditor(pEditor))
    {
        if (nk_input_is_mouse_hovering_rect(in, portBound))
        {
            isHover = true;
            UINodeGraphInteractData hovered{};
            hovered.mType = UI_NODE_GRAPH_INTERACT_PORT;
            hovered.mPort.pNode = pNode;
            hovered.mPort.mPortIdx = portIdx;
            hovered.mPort.mIsInputPort = isInput;

            updateNodeGraphHovered(pEditor, hovered);
            if (nk_input_is_mouse_pressed(in, NK_BUTTON_LEFT))
            {
                tryUpdateNodeGraphAction(pEditor, portAction);
            }
        }
    }

    if (memcmp(&pEditor->mActionData, &portAction, sizeof(UINodeGraphActionData)) == 0)
    {
        isActive = true;
    }

    nk_color portColor = isInput ? getPortTypeColor(pEditor, pNode->pInputPorts[portIdx].mType)
                                 : getPortTypeColor(pEditor, pNode->pOutputPorts[portIdx].mType);

    nk_override_anti_aliasing(canvas, true, NK_ANTI_ALIASING_ON);
    nk_stroke_circle(canvas, portBound, 1, portColor);
    if (isActive)
    {
        nk_fill_circle(canvas, activePortBound, portColor);
    }
    else if (isHover)
    {
        nk_fill_circle(canvas, activePortBound, nk_rgb_factor(portColor, pEditor->mStyle.mHoveredConnectionFactor));
    }
    nk_override_anti_aliasing(canvas, false, NK_ANTI_ALIASING_OFF);

    if (isInput && pNodeSizeData->mInputPortWidgets[portIdx].pWidget)
    {
        float4 rect = pNodeSizeData->mInputPortWidgets[portIdx].rect;
        nk_layout_space_begin(ctx, NK_STATIC, -1, 1);
        nk_layout_space_push(ctx, nk_rect(rect.x, rect.y, rect.z, rect.w));
        if (nk_group_begin(ctx, "", (nodeSelected ? 0 : NK_WINDOW_NOT_INTERACTIVE) | NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_NO_PADDING))
        {
            if (pNode->pInputConnections[portIdx].pOutputNode != NULL)
                uiBeginWidgetDisable();

            TFWidgetData* widget = pNodeSizeData->mInputPortWidgets[portIdx].pWidget;
            uint8_t*      pData = (uint8_t*)pNode->pUserData + pNode->pDesc->mReflectedUserDataOffset;
            bool          isHovered = false;

            widget->mChanged = false;
            uiUpdateBasicWidget(widget, pData + widget->pMember->mOffsetInStruct, &widget->mChanged, &isHovered);

            if (widget->mChanged)
            {
                interaction.changed = true;
            }

            if (pNode->pInputConnections[portIdx].pOutputNode != NULL)
                uiEndWidgetDisable();
            nk_group_end(ctx);
        }
        nk_layout_space_end(ctx);
    }
    else
    {
        nk_widget_text(canvas, textBound, name, nk_strlen(name), &text, NK_TEXT_LEFT, font);
    }

    return interaction;
}

static void doConnection(TFUINodeGraphEditor* pEditor, struct nk_input* in, TFNGNode* pNode, uint32_t connectionIdx)
{
    struct nk_context* ctx = getContext();
    struct nk_window*  win = ctx->current;
    in = win->widgets_disabled ? NULL : in;

    struct nk_vec2 start = getPortGlobalPos(pEditor, pNode, connectionIdx, true);
    struct nk_vec2 end = getPortGlobalPos(pEditor, pNode->pInputConnections[connectionIdx].pOutputNode,
                                          pNode->pInputConnections[connectionIdx].mOutputPortIdx, false);

    bool hovered = false;

    float offset = pEditor->mStyle.mConnectionOffset;
    if (nk_input_is_mouse_hovering_rect(
            in, nk_rect(minf(start.x, end.x), minf(start.y, end.y), fabsf(start.x - end.x) + offset, fabsf(start.y - end.y) + offset)))
    {
        struct nk_vec2 mousePos = in->mouse.pos;

        const float minHoverDist = pEditor->mStyle.mActivePortSize;
        float       minDis = FLT_MAX;
        if (start.x >= mousePos.x && mousePos.x >= start.x - pEditor->mStyle.mConnectionOffset)
        {
            minDis = minf(minDis, fabsf(start.y - mousePos.y));
        }

        if (end.x <= mousePos.x && mousePos.x <= end.x + pEditor->mStyle.mConnectionOffset)
        {
            minDis = minf(minDis, fabsf(end.y - mousePos.y));
        }

        minDis = minf(minDis, getDistToLine(nk_vec2(start.x - offset, start.y), nk_vec2(end.x + offset, end.y), mousePos));

        if (minDis <= minHoverDist)
        {
            hovered = true;
        }
    }

    if (hovered)
    {
        UINodeGraphInteractData hoveredData{};
        hoveredData.mType = UI_NODE_GRAPH_INTERACT_CONNECTION;
        hoveredData.mConnection.pNode = pNode;
        hoveredData.mConnection.mConnectionIdx = connectionIdx;
        updateNodeGraphHovered(pEditor, hoveredData);
    }

    nk_color portColor = getPortTypeColor(pEditor, pNode->pInputPorts[connectionIdx].mType);

    drawConnection(pEditor, start, end, portColor, hovered);
}

TFUIWidgetInteraction doNodeReflectionWidgets(TFUINodeGraphEditor* pEditor, TFNGNode* pNode)
{
    UINodeDescData* uiDescData = getNodeDescData(pEditor, pNode);

    TFUIWidgetInteraction interaction{};
    interaction.type = TF_WIDGET_INTERACTION_CHANGED;

    uint8_t* pData = (uint8_t*)pNode->pUserData + pNode->pDesc->mReflectedUserDataOffset;
    uiUpdateReflectedWidgets(&uiDescData->mReflectedWidgetData, uiDescData->pReflectedUserDataInfo, pData, NULL);

    for (uint32_t i = 0; i < uiDescData->mReflectedWidgetData.mWidgetCount; i++)
    {
        if (uiDescData->mReflectedWidgetData.pWidgets[i].mChanged)
        {
            interaction.changed = true;
            break;
        }
    }

    return interaction;
}

static TFUIWidgetInteraction doNode(TFUINodeGraphEditor* pEditor, struct nk_input* in, TFNGNode* pNode)
{
    TFUIWidgetInteraction interaction{};
    interaction.type = TF_WIDGET_INTERACTION_CHANGED;

    struct nk_context* ctx = getContext();
    struct nk_window*  win = ctx->current;
    in = win->widgets_disabled ? NULL : in;

    struct nk_text text;
    text.background = ctx->style.window.background;
    text.padding = nk_vec2(0, 0);
    text.text = ctx->style.text.color;

    const UINodeDescData* pNodeSizeData = getNodeDescData(pEditor, pNode);
    TFNGUserData*         pData = (TFNGUserData*)pNode->pUserData;
    struct nk_rect        bound = nk_rect(pData->mPosition[0] + pEditor->mViewPosition.x, pData->mPosition[1] + pEditor->mViewPosition.y,
                                          pNodeSizeData->mSize.x, pNodeSizeData->mSize.y);
    struct nk_vec2        globalOrg = nk_layout_space_to_screen(ctx, nk_vec2(bound.x, bound.y));

    bool useMoving = false;
    if (mouseIsInNodeGraphEditor(pEditor))
    {
        if (nk_input_is_mouse_hovering_rect(in, nk_rect(globalOrg.x, globalOrg.y, bound.w, bound.h)))
        {
            UINodeGraphInteractData hovered{};
            hovered.mType = UI_NODE_GRAPH_INTERACT_NODE;
            hovered.mNode.pNode = pNode;
            UINodeGraphActionData action{};
            action.mType = UI_NODE_GRAPH_ACTION_MOVE_NODE;
            action.mMoveNode.pNode = pNode;
            updateNodeGraphHovered(pEditor, hovered);
            if (isPossibleToStartAction(pEditor, action))
            {
                useMoving = true;
            }
        }
        else if (pEditor->mActionData.mType == UI_NODE_GRAPH_ACTION_MOVE_NODE || pEditor->mActionData.mMoveNode.pNode == pNode)
        {
            useMoving = true;
        }
    }

    bool selected = pEditor->mSelectedData.mType == UI_NODE_GRAPH_INTERACT_NODE && pEditor->mSelectedData.mNode.pNode == pNode;

    if (selected)
    {
        nk_style_push_color(ctx, &ctx->style.window.group_border_color, nk_white);
    }

    nk_layout_space_push(ctx, nk_rect(bound.x, bound.y, bound.w, bound.h));
    if (nk_group_begin(ctx, pNodeSizeData->pName,
                       (useMoving ? NK_WINDOW_MOVABLE : 0) | (selected ? 0 : NK_WINDOW_NOT_INTERACTIVE) | NK_WINDOW_NO_SCROLLBAR |
                           NK_WINDOW_BORDER | NK_WINDOW_TITLE | NK_WINDOW_NO_PADDING))
    {
        struct nk_panel* panel = nk_window_get_panel(ctx);

        // update node position after move
        if (panel->isMoving)
        {
            UINodeGraphActionData action{};
            action.mType = UI_NODE_GRAPH_ACTION_MOVE_NODE;
            action.mMoveNode.pNode = pNode;
            if (tryUpdateNodeGraphAction(pEditor, action))
            {
                pData->mPosition[0] += panel->moveDelta.x;
                pData->mPosition[1] += panel->moveDelta.y;
                globalOrg = nk_vec2(globalOrg.x + panel->moveDelta.x, globalOrg.y + panel->moveDelta.y);
            }
        }

        if (pNodeSizeData->mCustomWdigets.pDrawCustomNode != NULL)
        {
            nk_layout_space_begin(ctx, NK_STATIC, -1, 1);
            nk_layout_space_push(ctx, nk_rect(pNodeSizeData->mStartCustomData.x, pNodeSizeData->mStartCustomData.y,
                                              pNodeSizeData->mSizeCustomData.x, pNodeSizeData->mSizeCustomData.y));
            if (nk_group_begin(ctx, "", (selected ? 0 : NK_WINDOW_NOT_INTERACTIVE) | NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_NO_PADDING))
            {
                TFUIWidgetInteraction result = pNodeSizeData->mCustomWdigets.pDrawCustomNode(pNode, pNodeSizeData->mCustomDataUI);
                if (result.changed)
                {
                    interaction = result;
                }
                nk_group_end(ctx);
            }
            nk_layout_space_end(ctx);
        }

        if (pNodeSizeData->mFlags & TF_UI_NODE_DESC_FLAG_DRAW_OTHER_REFLECTION)
        {
            nk_layout_space_begin(ctx, NK_STATIC, -1, 1);
            nk_layout_space_push(ctx, nk_rect(pNodeSizeData->mStartDrawReflectionWidgets.x, pNodeSizeData->mStartDrawReflectionWidgets.y,
                                              pNodeSizeData->mSizeReflectionWidgets.x, pNodeSizeData->mSizeReflectionWidgets.y));
            if (nk_group_begin(ctx, "", (selected ? 0 : NK_WINDOW_NOT_INTERACTIVE) | NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_NO_PADDING))
            {
                TFUIWidgetInteraction result = doNodeReflectionWidgets(pEditor, pNode);
                if (result.changed)
                {
                    interaction = result;
                }
                nk_group_end(ctx);
            }
            nk_layout_space_end(ctx);
        }

        for (uint32_t i = 0; i < pNode->mInputPortCount; i++)
        {
            TFUIWidgetInteraction result = doPort(pEditor, in, pNodeSizeData, pNode, selected, globalOrg, i, true);
            if (result.changed)
            {
                interaction = result;
            }
        }

        for (uint32_t i = 0; i < pNode->mOutputPortCount; i++)
        {
            TFUIWidgetInteraction result = doPort(pEditor, in, pNodeSizeData, pNode, selected, globalOrg, i, false);
            if (result.changed)
            {
                interaction = result;
            }
        }

        nk_group_end(ctx);
    }
    if (selected)
    {
        nk_style_pop_color(ctx);
    }

    return interaction;
}

TFUINodeGraphEditor* uiAddNodeGraphEditor(TFNodeGraphContext* pNodeGraphCtx)
{
    TFUINodeGraphEditor* pEditor = (TFUINodeGraphEditor*)tf_calloc(1, sizeof(TFUINodeGraphEditor));
    *pEditor = {};

    pEditor->pNodeGraphCtx = pNodeGraphCtx;

    pEditor->mNodeContextMenu.mUserData.pEditor = pEditor;
    pEditor->mConnectionContextMenu.mUserData.pEditor = pEditor;

    pEditor->mNodeContextMenu.mItem.pName = "Remove node";
    pEditor->mNodeContextMenu.mItem.mType = TF_UI_CONTEXT_MENU_ITEM;
    pEditor->mNodeContextMenu.mItem.mItemData.pOnSelect = removeNodeFromNodeGraph;
    pEditor->mNodeContextMenu.mItem.mItemData.pOnSelectUserData = &pEditor->mNodeContextMenu.mUserData;

    pEditor->mConnectionContextMenu.mItem.pName = "Remove connection";
    pEditor->mConnectionContextMenu.mItem.mType = TF_UI_CONTEXT_MENU_ITEM;
    pEditor->mConnectionContextMenu.mItem.mItemData.pOnSelect = removeConnectionFromNodeGraph;
    pEditor->mConnectionContextMenu.mItem.mItemData.pOnSelectUserData = &pEditor->mConnectionContextMenu.mUserData;

    // set style for editor

    float windowSpacing = uiGetDefaultRowPadding();

    pEditor->mStyle.mPortSize = 10;
    pEditor->mStyle.mPortSpace = DEFAULT_ROW_HEIGHT + windowSpacing;
    pEditor->mStyle.mBorderOffset = (pEditor->mStyle.mPortSpace - pEditor->mStyle.mPortSize) / 2.0f - windowSpacing;
    pEditor->mStyle.mHeightPadding = nk_panel_get_header_height(&getContext()->style);
    pEditor->mStyle.mActivePortSize = pEditor->mStyle.mPortSize - 6;
    pEditor->mStyle.mPortTextPadding = 10;
    pEditor->mStyle.mSpaceBetweenPorts = 5;
    pEditor->mStyle.mConnectionOffset = 30;
    pEditor->mStyle.mHoveredConnectionFactor = 0.5f;

    // set custom UI for basic nodes

    addBasicNodeDescUI(pEditor, f2Make(25, uiCalculateHeightReflectedWidgets(1)), &doBasicConstNode, TF_NODE_GRAPH_TOGGLE);
    addBasicNodeDescUI(pEditor, f2Make(100, uiCalculateHeightReflectedWidgets(1)), &doBasicConstNode, TF_NODE_GRAPH_FLOAT);
    addBasicNodeDescUI(pEditor, f2Make(100, uiCalculateHeightReflectedWidgets(2)), &doBasicConstNode, TF_NODE_GRAPH_FLOAT_2);
    addBasicNodeDescUI(pEditor, f2Make(100, uiCalculateHeightReflectedWidgets(3)), &doBasicConstNode, TF_NODE_GRAPH_FLOAT_3);
    addBasicNodeDescUI(pEditor, f2Make(100, uiCalculateHeightReflectedWidgets(4)), &doBasicConstNode, TF_NODE_GRAPH_FLOAT_4);
    addBasicNodeDescUI(pEditor, f2Make(100, uiCalculateHeightReflectedWidgets(1)), &doBasicConstNode, TF_NODE_GRAPH_INT);
    addBasicNodeDescUI(pEditor, f2Make(100, uiCalculateHeightReflectedWidgets(2)), &doBasicConstNode, TF_NODE_GRAPH_INT_2);
    addBasicNodeDescUI(pEditor, f2Make(100, uiCalculateHeightReflectedWidgets(3)), &doBasicConstNode, TF_NODE_GRAPH_INT_3);
    addBasicNodeDescUI(pEditor, f2Make(100, uiCalculateHeightReflectedWidgets(4)), &doBasicConstNode, TF_NODE_GRAPH_INT_4);

    addBasicNodeDescUI(pEditor, f2Make(200, 0), NULL, TF_NODE_GRAPH_CLAMP);
    addBasicNodeDescUI(pEditor, f2Make(200, 0), NULL, TF_NODE_GRAPH_MIN);
    addBasicNodeDescUI(pEditor, f2Make(200, 0), NULL, TF_NODE_GRAPH_MAX);

    uiSetNodeDescFlag(pEditor, TF_NODE_GRAPH_CLAMP, TF_UI_NODE_DESC_FLAG_DRAW_PORT_REFLECTED, true);
    uiSetNodeDescFlag(pEditor, TF_NODE_GRAPH_MIN, TF_UI_NODE_DESC_FLAG_DRAW_PORT_REFLECTED, true);
    uiSetNodeDescFlag(pEditor, TF_NODE_GRAPH_MAX, TF_UI_NODE_DESC_FLAG_DRAW_PORT_REFLECTED, true);

    // set colors
    uiSetPortTypeColor(pEditor, f3Make(0.5f, 1.f, 1.f), TF_NODE_GRAPH_TYPE_BOOL);
    uiSetPortTypeColor(pEditor, f3Make(1.f, 0.65f, 0.f), TF_NODE_GRAPH_TYPE_VECTOR);

    generateCreationContextMenu(pEditor);

    return pEditor;
}

void uiRemoveNodeGraphEditor(TFUINodeGraphEditor* pEditor)
{
    freeCreationContextMenu(pEditor);

    for (uint32_t i = 0; i < (uint32_t)arrlen(pEditor->mViewContextMenu.ppUsersDataArray); i++)
    {
        tf_free(pEditor->mViewContextMenu.ppUsersDataArray[i]);
    }
    arrfree(pEditor->mViewContextMenu.ppUsersDataArray);

    for (uint32_t i = 0; i < hmlen(pEditor->pHmNodeDescData); i++)
    {
        if (pEditor->pHmNodeDescData[i].value.pReflectedUserDataInfo)
        {
            uiRemoveReflectedWidgetData(&pEditor->pHmNodeDescData[i].value.mReflectedWidgetData);
        }
    }
    hmfree(pEditor->pHmNodeDescData);

    hmfree(pEditor->pHmTypeColor);

    tf_free(pEditor);
}

TFUIWidgetInteraction uiNodeGraphEditor(TFUINodeGraphEditor* pEditor)
{
    struct nk_context* ctx = getContext();
    struct nk_window*  win = ctx->current;
    struct nk_panel*   layout = win->layout;
    struct nk_input*   in = (layout->flags & NK_WINDOW_ROM || layout->flags & NK_WINDOW_NO_INPUT) ? NULL : &ctx->input;

    /* allocate complete window space */
    nk_command_buffer* canvas = nk_window_get_canvas(ctx);
    struct nk_rect     total_space = nk_window_get_content_region(ctx);
    uint32_t           nodesCount = ngGetNodeCount(pEditor->pNodeGraphCtx);
    nk_layout_space_begin(ctx, NK_STATIC, total_space.h, nodesCount);

    TFUIWidgetInteraction interaction{};
    interaction = updateNodeGraphActions(pEditor);
    interaction.changed |= pEditor->mWasChanged;
    pEditor->mWasChanged = false;

    struct nk_rect contentBound = nk_layout_space_bounds(ctx);
    bool           hovered = nk_input_is_mouse_hovering_rect(in, contentBound);
    if (hovered)
    {
        UINodeGraphInteractData interact{};
        interact.mType = UI_NODE_GRAPH_INTERACT_VIEW;

        interact.mView.mMousePos[0] = in->mouse.pos.x - (contentBound.x + pEditor->mViewPosition.x);
        interact.mView.mMousePos[1] = in->mouse.pos.y - (contentBound.y + pEditor->mViewPosition.y);
        updateNodeGraphHovered(pEditor, interact);
    }
    else
    {
        resetNodeGraphState(pEditor, false);
    }

    // draw grid
    {
        struct nk_rect        size = nk_layout_space_bounds(ctx);
        const float           grid_size = 32.0f;
        const struct nk_color grid_color = ctx->style.window.border_color;
        for (float x = (float)fmod(pEditor->mViewPosition.x, grid_size); x < size.w; x += grid_size)
        {
            nk_stroke_line(canvas, x + size.x, size.y, x + size.x, size.y + size.h, 1.0f, grid_color);
        }
        for (float y = (float)fmod(pEditor->mViewPosition.y, grid_size); y < size.h; y += grid_size)
        {
            nk_stroke_line(canvas, size.x, y + size.y, size.x + size.w, y + size.y, 1.0f, grid_color);
        }
    }

    nk_override_anti_aliasing(canvas, true, NK_ANTI_ALIASING_ON);
    for (uint32_t i = 0; i < nodesCount; i++)
    {
        TFNGNode* pNode = ngGetNode(pEditor->pNodeGraphCtx, i);
        for (uint32_t i2 = 0; i2 < pNode->mInputPortCount; i2++)
        {
            TFNGConnection connection = pNode->pInputConnections[i2];
            if (connection.pOutputNode != NULL)
            {
                doConnection(pEditor, in, pNode, i2);
            }
        }
    }
    nk_override_anti_aliasing(canvas, false, NK_ANTI_ALIASING_ON);

    if (pEditor->mActionData.mType == UI_NODE_GRAPH_ACTION_CREATING_CONNECTION)
    {
        UINodeGraphActionData action = pEditor->mActionData;
        struct nk_vec2        start = getPortGlobalPos(pEditor, action.mCreatingConnection.pNode, action.mCreatingConnection.mPortIdx,
                                                       action.mCreatingConnection.mIsInputPort);
        struct nk_vec2        end = in->mouse.pos;

        uint32_t        portIdx = action.mCreatingConnection.mPortIdx;
        bool            isInput = action.mCreatingConnection.mIsInputPort;
        int32_t         type = isInput ? action.mCreatingConnection.pNode->pInputPorts[portIdx].mType
                                       : action.mCreatingConnection.pNode->pOutputPorts[portIdx].mType;
        struct nk_color color = getPortTypeColor(pEditor, type);
        if (isInput)
        {
            drawConnection(pEditor, start, end, color, true);
        }
        else
        {
            drawConnection(pEditor, end, start, color, true);
        }
    }

    TFNGNode* pSelectedNode = NULL;
    if (pEditor->mSelectedData.mType == UI_NODE_GRAPH_INTERACT_NODE)
    {
        pSelectedNode = pEditor->mSelectedData.mNode.pNode;
    }

    uint32_t selectedNodeIdx = (uint32_t)-1;
    for (uint32_t i = 0; i < nodesCount; i++)
    {
        TFNGNode* pNode = ngGetNode(pEditor->pNodeGraphCtx, i);
        if (pSelectedNode != pNode)
        {
            interaction.changed |= UI_WIDGET_IS_CHANGED(doNode(pEditor, in, pNode));
            interaction.id = i;
        }
        else
        {
            selectedNodeIdx = i;
        }
    }

    // put selected node forward
    if (pSelectedNode)
    {
        interaction.changed |= UI_WIDGET_IS_CHANGED(doNode(pEditor, in, pSelectedNode));
        interaction.id = selectedNodeIdx;
    }

    // scrolling
    if (hovered)
    {
        UINodeGraphActionData action{};
        action.mType = UI_NODE_GRAPH_ACTION_MOVE_VIEW;

        if (nk_input_is_mouse_pressed(in, NK_BUTTON_LEFT))
        {
            tryUpdateNodeGraphAction(pEditor, action);
        }

        if (pEditor->mActionData.mType == UI_NODE_GRAPH_ACTION_MOVE_VIEW)
        {
            pEditor->mViewPosition.x += in->mouse.delta.x;
            pEditor->mViewPosition.y += in->mouse.delta.y;
        }
    }

    updateNodeGraphActionPost(pEditor);
    nk_layout_space_end(ctx);

    return interaction;
}

void uiDrawEmptyNodeGraphEditor()
{
    struct nk_context* ctx = getContext();
    nk_command_buffer* canvas = nk_window_get_canvas(ctx);
    {
        struct nk_rect        total_space = nk_window_get_content_region(ctx);
        const float           grid_size = 32.0f;
        const struct nk_color grid_color = ctx->style.window.border_color;
        for (float x = grid_size; x < total_space.w; x += grid_size)
        {
            nk_stroke_line(canvas, x + total_space.x, total_space.y, x + total_space.x, total_space.y + total_space.h, 1.0f, grid_color);
        }
        for (float y = grid_size; y < total_space.h; y += grid_size)
        {
            nk_stroke_line(canvas, total_space.x, y + total_space.y, total_space.x + total_space.w, y + total_space.y, 1.0f, grid_color);
        }
    }
}

void uiSetPortTypeColor(TFUINodeGraphEditor* pEditor, float3 color, int32_t portType)
{
    hmput(pEditor->pHmTypeColor, portType, nk_rgba_f(color.x, color.y, color.z, 1.0f));
}

void uiSetDrawCustomNode(TFUINodeGraphEditor* pEditor, const TFUINodeCustomWidgets* pDrawCustomNode, int32_t nodeType)
{
    const TFNGNodeDesc* pDesc = ngGetNodeDescByType(ngGetNodeGraphDesc(pEditor->pNodeGraphCtx), nodeType);
    calculateNodeDescData(pEditor, pDesc, pDrawCustomNode);
}

void uiSetNodeDescFlag(TFUINodeGraphEditor* pEditor, int32_t nodeType, TFUINodeDescFlags flag, bool enable)
{
    UINodeDescData* pData = getNodeDescByType(pEditor, nodeType);
    ASSERT(pData);

    if (enable)
    {
        pData->mFlags = (TFUINodeDescFlags)(pData->mFlags | flag);
    }
    else
    {
        pData->mFlags = (TFUINodeDescFlags)(pData->mFlags & ~flag);
    }

    // update creation context menu
    if ((flag & TF_UI_NODE_DESC_FLAG_HIDDEN_CONTEXT_ITEM) != 0)
    {
        generateCreationContextMenu(pEditor);
    }

    if ((flag & (TF_UI_NODE_DESC_FLAG_DRAW_PORT_REFLECTED | TF_UI_NODE_DESC_FLAG_DRAW_OTHER_REFLECTION)) != 0)
    {
        const TFNGNodeDesc*   pDesc = ngGetNodeDescByType(ngGetNodeGraphDesc(pEditor->pNodeGraphCtx), nodeType);
        TFUINodeCustomWidgets customWidgets = pData->mCustomWdigets;
        calculateNodeDescData(pEditor, pDesc, &customWidgets);
    }
}

/****************************************************************************/
// MARK: - Widget Utility Functions
/****************************************************************************/
bool uiIsWidgetHovered(TFUIWidgetId id)
{
    // TODO
    UNREF_PARAM(id);

    return false;
}
bool uiIsWidgetActive(TFUIWidgetId id)
{
    // TODO
    UNREF_PARAM(id);

    return false;
}
bool uiIsWidgetFocused(TFUIWidgetId id)
{
    // TODO
    UNREF_PARAM(id);

    return false;
}
bool uiIsWidgetClicked(TFUIWidgetId id)
{
    // TODO
    UNREF_PARAM(id);

    return false;
}
bool uiIsWidgetVisible(TFUIWidgetId id)
{
    // TODO
    UNREF_PARAM(id);

    return false;
}
bool uiIsWidgetEdited(TFUIWidgetId id)
{
    // TODO
    UNREF_PARAM(id);

    return false;
}
bool uiIsWidgetActivated(TFUIWidgetId id)
{
    // TODO
    UNREF_PARAM(id);

    return false;
}
bool uiIsWidgetDeactivated(TFUIWidgetId id)
{
    // TODO
    UNREF_PARAM(id);

    return false;
}
bool uiIsWidgetDeactivatedAfterEdit(TFUIWidgetId id)
{
    // TODO
    UNREF_PARAM(id);

    return false;
}

// TODO
TFUIWidgetId uiGetWidgetId() { return 0; }
TFUIWidget*  uiGetWidgetById(TFUIWidgetId id)
{
    UNREF_PARAM(id);

    return NULL;
}

uint32_t uiGetTextWidth(const char* cstr)
{
    const struct nk_style* style = &getContext()->style;
    return (uint32_t)style->font->width(style->font->userdata, style->font->height, cstr, (int32_t)strlen(cstr));
}

bool uiIsNextWidgetHovered()
{
    struct nk_context* ctx = getContext();
    return nk_widget_is_hovered(ctx);
}
bool uiIsNextWidgetActive() { return false; }
bool uiIsNextWidgetFocused() { return false; }
bool uiIsNextWidgetClicked() { return false; }
bool uiIsNextWidgetVisible() { return false; }
bool uiIsNextWidgetEdited() { return false; }
bool uiIsNextWidgetActivated() { return false; }
bool uiIsNextWidgetDeactivated() { return false; }
bool uiIsWidgetDeactivatedAfterEdit() { return false; }

/****************************************************************************/
// MARK: - Window Functions
/****************************************************************************/
#if CUSTOM_LAYOUT
static void initPanelLayout(UILayoutPanel* panel)
{
    ASSERT(panel);
    // copy layout data
    struct nk_panel* nkPanel = nk_window_get_panel(getContext());
    panel->mBounds = nkPanel->bounds;
    panel->mBorder = nkPanel->border;
    panel->mClip = nkPanel->clip;
    panel->mFooterHeight = nkPanel->footer_height;
    panel->mHasScrolling = (bool)nkPanel->has_scrolling;
    panel->mHeaderHeight = nkPanel->header_height;
    panel->mFlags = nkPanel->flags;
    panel->pRows = NULL;
    panel->mLayoutCursor = { 0, 0 };
    panel->mRowCursor = { 0, 0 };
}
#endif

TFUIDpiScaleSettings uiGetDpiScaleSettings() { return pUserInterface->dpiScaleSettings; }

void uiSetDpiScaleSettings(TFUIDpiScaleSettings settings) { pUserInterface->dpiScaleSettings = settings; }

float uiGetRoundScreenSize() { return pUserInterface->mRoundScreenSize; }

void uiSetRoundScreenSize(float roundScreenSize) { pUserInterface->mRoundScreenSize = roundScreenSize; }

TFUIWindowInteraction uiBeginWidgetWindow(const TFUIWindowDesc* pDesc)
{
    struct nk_context*    ctx = getContext();
    TFUIWindowInteraction result;
    result.panelType = TF_UI_PANEL_WINDOW;

    TFUIWindowFlags flags = pDesc->mFlags;
    struct nk_vec2  windowsPos = nk_vec2(pDesc->mStartPos.x, pDesc->mStartPos.y);
    struct nk_vec2  windowSize = nk_vec2(pDesc->mStartSize.x, pDesc->mStartSize.y);

    if ((pDesc->mFlags & TF_UI_WINDOW_DISABLE_DPI_SCALE_APPLY) == 0)
    {
        if (pUserInterface->dpiScaleSettings & TF_UI_DPI_SCALE_SETTINGS_WINDOW_POS_APPLY)
        {
            windowsPos.x *= pUserInterface->mDpiScale[0];
            windowsPos.y *= pUserInterface->mDpiScale[1];
        }
        if (pUserInterface->dpiScaleSettings & TF_UI_DPI_SCALE_SETTINGS_WINDOW_SIZE_APPLY)
        {
            windowSize.x *= pUserInterface->mDpiScale[0];
            windowSize.y *= pUserInterface->mDpiScale[1];
        }
    }

    struct nk_window* existingWindow = (struct nk_window*)uiFindWindowByTitle(pDesc->pWindowTitle);

    if (pUserInterface->mEnabledSWL)
    {
        windowsPos.x = pUserInterface->mOffsetToDrawWindowSWL.x;
        windowsPos.y = pUserInterface->mOffsetToDrawWindowSWL.y;
        windowSize.x = minf(windowSize.x, pUserInterface->mMaxWindowSizeScreenRationSWL.x * pUserInterface->mWidth);
        windowSize.y = minf(windowSize.y, pUserInterface->mMaxWindowSizeScreenRationSWL.y * pUserInterface->mHeight);

        flags &= ~(nk_flags)(TF_UI_WINDOW_MOVABLE);
        flags &= ~(nk_flags)(TF_UI_WINDOW_CLOSABLE);
        flags &= ~(nk_flags)(TF_UI_WINDOW_TITLE);
        flags &= ~(nk_flags)(TF_UI_WINDOW_INIT_MINIMIZED);
        flags &= ~(nk_flags)(TF_UI_WINDOW_MINIMIZABLE);
        flags &= ~(nk_flags)(TF_UI_WINDOW_SCALABLE);
        flags |= TF_UI_WINDOW_BORDER;
        flags |= TF_UI_WINDOW_MINIMIZABLE;
        flags |= TF_UI_WINDOW_SCALABLE;

        if (uiGetWindowPositionSWL() == TF_UI_WINDOW_POSITION_SWL_RIGHT)
        {
            flags |= TF_UI_WINDOW_SCALE_LEFT;
            windowsPos.x = pUserInterface->mWidth - windowSize.x;
        }
        arrpush(pUserInterface->ppWindowNameArrSWL, pDesc->pWindowTitle);
    }

    // window enabled by default
    bool isActiveWindow = !pUserInterface->mGamepadActive && !pUserInterface->mEnabledSWL;

    // Gamepad window selection
    if (pUserInterface->mWindowSelectionIndex == pUserInterface->mWindowCounter && !isActiveWindow)
    {
        if (pUserInterface->mWindowSelected != existingWindow && existingWindow != NULL)
        {
            pUserInterface->mWindowSelected = existingWindow;
            nk_window_collapse(getContext(), existingWindow->name_string, NK_MAXIMIZED);
        }

        if (existingWindow != NULL)
        {
            nk_window_show(getContext(), existingWindow->name_string, NK_SHOWN);
            nk_window_set_focus(getContext(), existingWindow->name_string);
        }

        if (pUserInterface->mEnabledSWL && existingWindow != NULL)
        {
            if (uiGetWindowPositionSWL() == TF_UI_WINDOW_POSITION_SWL_RIGHT)
            {
                existingWindow->bounds.x = pUserInterface->mWidth - existingWindow->bounds.w;
            }
            else
            {
                existingWindow->bounds.x = windowsPos.x;
            }

            existingWindow->bounds.y = windowsPos.y;
        }

        isActiveWindow = true;
    }

    pUserInterface->mWindowCounter += 1;

    nk_show_states showState = NK_SHOWN;
    bool           setShowState = false;
    if (pUserInterface->mEnabledSWL)
    {
        setShowState = true;

        if (isActiveWindow && !pUserInterface->mHiddenUI_SWL)
        {
            showState = NK_SHOWN;
        }
        else
        {
            showState = NK_HIDDEN;
        }
    }
    else
    {
        setShowState = true;
    }

    flags |= pUserInterface->mCommonWindowFlags;
    if (!existingWindow)
    {
        pUserInterface->mFitCurrentWindowHeight = flags & TF_UI_WINDOW_INIT_HEIGHT_FIT;

        // Check if this new window was previously hidden
        int     titleLen = (int)nk_strlen(pDesc->pWindowTitle);
        nk_hash titleHash = nk_murmur_hash(pDesc->pWindowTitle, (int)titleLen, NK_WINDOW_TITLE);
        for (uint32_t i = 0; i < arrlenu(pUserInterface->pWindowsToHide); i++)
        {
            if (titleHash == pUserInterface->pWindowsToHide[i])
            {
                flags |= NK_WINDOW_HIDDEN;
                arrdelswap(pUserInterface->pWindowsToHide, i);
                break;
            }
        }
    }
    else
    {
        if (flags & TF_UI_WINDOW_INIT_MINIMIZED)
        {
            flags &= ~(nk_flags)TF_UI_WINDOW_INIT_MINIMIZED;
            pUserInterface->mFitCurrentWindowHeight = flags & TF_UI_WINDOW_INIT_HEIGHT_FIT;
        }
        else
        {
            pUserInterface->mFitCurrentWindowHeight = false;
        }
    }
    pUserInterface->mHaveWindow = true;

    if (setShowState && showState == NK_HIDDEN)
    {
        windowsPos.x = -10000;
        windowsPos.y = -10000;

        if (existingWindow != NULL)
        {
            nk_window_set_position(getContext(), existingWindow->name_string, windowsPos);
        }
    }

    result.fillable = nk_begin(ctx, pDesc->pWindowTitle, nk_rect(windowsPos.x, windowsPos.y, windowSize.x, windowSize.y), flags);
    struct nk_window* win = ctx->current;

    if (result.fillable)
    {
        UIBlackboard* bb = &pUserInterface->mBlackboard;

        if (bb->pWindowCache)
        {
            ASSERTFAIL("uiBeginWidgetWindow called before current window has been finished with uiEndWidgetWindow.");
        }

        // hashed window title that can be used to retrieve the nk_window with nk_find_window
        result.id = win->name;

        ptrdiff_t cacheIndex = hmgeti(pUserInterface->pWidgetMap, win); //-V::568, 574
        if (cacheIndex == -1)
        {
            bb->pWindowCache = &hmput(pUserInterface->pWidgetMap, win, UIWindowCacheEntry{ 0 });
        }
        else
        {
            bb->pWindowCache = &pUserInterface->pWidgetMap[cacheIndex].value;
        }

#if CUSTOM_LAYOUT
        // set a single column for the root group
        nk_layout_row_dynamic(ctx, 0, 1);

        // create root group without the title
        TFUIWindowFlags groupFlags = flags;
        groupFlags &= ~TF_UI_WINDOW_TITLE;
        ASSERT(UI_GROUP_IS_VISIBLE(uiBeginWidgetGroup(pDesc->pWindowTitle, groupFlags)));
#endif
    }

    return result;
}

void uiEndWidgetWindow()
{
    UIBlackboard* bb = &pUserInterface->mBlackboard;

#if CUSTOM_LAYOUT
    uiEndWidgetGroup();
#endif

    if (bb->pWindowCache)
    {
        struct nk_window* win = (struct nk_window*)uiGetCurrentWindow();
        if (win && pUserInterface->mFitCurrentWindowHeight)
        {
            win->bounds.h = uiGetCurrentWindowHeight();
        }

        bb->pWindowCache->mNextWidgetId = 0;
        bb->pWindowCache->mTreeCounter = 0;
        bb->pWindowCache = NULL;
    }

    nk_end(getContext());
}

/****************************************************************************/
// MARK: - Widget Group Functions
/****************************************************************************/
#if CUSTOM_LAYOUT
static bool beginWidgetGroupImpl(UIWidgetGroup* group, bool reconstruct, const char* title, TFUIWindowFlags flags)
{
    UILayoutPanel* panel = &group->mPanel;

    if (reconstruct)
    {
        // allocate new group
        bool fillable = nk_group_scrolled_offset_begin(getContext(), &panel->mOffsetX, &panel->mOffsetY, title, flags);
        initPanelLayout(panel);
        return fillable;
    }
    else
    {
        setPanelLayout(panel, NK_PANEL_GROUP, flags);
        return panel;
    }
}
#endif

TFUIWindowInteraction uiBeginWidgetGroup(const char* title, TFUIWindowFlags flags)
{
    TFUIWindowInteraction result;
    result.panelType = TF_UI_PANEL_GROUP;

#if CUSTOM_LAYOUT
    UIBlackboard* bb = &pUserInterface->mBlackboard;

    UIWidgetGroup* childGroup = NULL;
    if (bb->pCurrentWidgetGroup)
    {
        // create new child group
        if (bb->pCurrentWidgetGroup->ppChildGroups == NULL || arrlenu(bb->pCurrentWidgetGroup->ppChildGroups) == bb->mChildGroupIndex)
        {
            UIWidgetGroup newGroup = { 0 };
            newGroup.pParentGroup = bb->pCurrentWidgetGroup;

            childGroup = &arrput(bb->pCurrentWidgetGroup->ppChildGroups, newGroup);

            result.fillable = beginWidgetGroupImpl(childGroup, true, title, flags);

            createNewLayoutRow(&childGroup->mPanel, TF_LAYOUT_DYNAMIC, 0, 1);
        }
        // get cached child group
        else
        {
            childGroup = &bb->pCurrentWidgetGroup->ppChildGroups[bb->mChildGroupIndex];
            result.fillable = beginWidgetGroupImpl(childGroup, false, title, flags);
        }

        bb->mGroupIndexStack[bb->mGroupIndexStackSize++] = ++bb->mChildGroupIndex;
        bb->pCurrentWidgetGroup = childGroup;
        bb->mChildGroupIndex = 0;
    }
    // initialize the root group
    // this should only be done when starting a new window
    else
    {
        UIWindowCacheEntry* cache = bb->pWindowCache;

        bb->pCurrentWidgetGroup = childGroup = &cache->mRootGroup;
        bb->mChildGroupIndex = 0;
        bb->mGroupIndexStackSize = 0;

        result.fillable = beginWidgetGroupImpl(childGroup, true, title, flags);
    }

    // sets up the starting row
    layoutSpaceBeginImpl(TF_LAYOUT_DYNAMIC, 0, 1, false);

    // Nuklear friendly id for querying offsets
    int titleLen = (int)nk_strlen(title);
    result.id = childGroup->mId = nk_murmur_hash(title, titleLen, NK_PANEL_GROUP);
#else
    result.fillable = nk_group_begin(getContext(), title, flags) == 1;
    // Nuklear friendly id for querying offsets
    int titleLen = (int)nk_strlen(title);
    result.id = nk_murmur_hash(title, titleLen, NK_PANEL_GROUP);
#endif
    return result;
}

void uiEndWidgetGroup() //-V::524
{
#if CUSTOM_LAYOUT
    UIBlackboard* bb = &pUserInterface->mBlackboard;

    UILayoutPanel* panel = &bb->pCurrentWidgetGroup->mPanel;
    panel->mLayoutCursor = vec2(0, 0);
    panel->mRowCursor = ivec2(0, 0);

    bb->mChildGroupIndex = bb->mGroupIndexStack[bb->mGroupIndexStackSize--];
    bb->pCurrentWidgetGroup = bb->pCurrentWidgetGroup->pParentGroup;

    if (bb->pCurrentWidgetGroup)
    {
        panel = &bb->pCurrentWidgetGroup->mPanel;
        bb->pCurrentLayoutRow = &panel->pRows[panel->mRowCursor.y];
    }
#endif
    nk_group_end(getContext());
}

//

float uiCalculateHeightOfTextRows(float rowHeight)
{
    return NK_MAX(pUserInterface->mFontHeight * pUserInterface->mFontDpiScale + 0.01f, rowHeight);
}

float uiGetCurrentWindowHeight()
{
    struct nk_window* win = (struct nk_window*)uiGetCurrentWindow();
    if (win)
    {
        nk_style*      style = &getContext()->style;
        struct nk_rect windowBounds = nk_window_get_bounds(getContext());
        struct nk_rect widgetBounds = nk_widget_bounds(getContext());
        windowBounds.y += win->layout->header_height - style->window.padding.y;
        widgetBounds.h += 2 * style->window.padding.y + win->layout->footer_height;
        return widgetBounds.y - windowBounds.y + widgetBounds.h;
    }

    return 0;
}

// Menu bar
void uiBeginMenuBar()
{
    nk_menubar_begin(getContext());
    nk_layout_row_begin(getContext(), NK_STATIC, uiCalculateHeightOfTextRows(DEFAULT_ROW_HEIGHT), 4);
}

TFUIWidgetInteraction uiBeginMenuBarLabel(const char* title, uint32_t labelWidth, uint32_t dropdownWidth)
{
    ASSERT(title);
    TFUIWidgetInteraction result = {};
    result.type = TF_WIDGET_INTERACTION_PRESSED;
    nk_layout_row_push(getContext(), (float)labelWidth);
    if (nk_menu_begin_label(getContext(), title, NK_TEXT_LEFT, nk_vec2((float)dropdownWidth, 200.0f)))
    {
        nk_layout_row_dynamic(getContext(), DEFAULT_ROW_HEIGHT, 1);
        result.pressed = true;
    }
    return result;
}

void uiEndMenuBarLabel() { nk_menu_end(getContext()); }

TFUIWidgetInteraction uiMenuBarItem(const char* title)
{
    UNREF_PARAM(title);
    TFUIWidgetInteraction result = {};
    result.type = TF_WIDGET_INTERACTION_PRESSED;

    if (nk_menu_item_label(getContext(), title, NK_TEXT_LEFT))
    {
        result.pressed = true;
    }

    return result;
}

TFUIWidgetInteraction uiButtonMenuBarLabel(const char* title, uint32_t labelWidth)
{
    ASSERT(title);
    TFUIWidgetInteraction result = {};
    result.type = TF_WIDGET_INTERACTION_PRESSED;
    nk_context* ctx = getContext();
    nk_layout_row_push(ctx, (float)labelWidth);
    result.pressed = nk_button_label_styled(ctx, &ctx->style.menu_button, title);
    return result;
}

void uiEndMenuBar() { nk_menubar_end(getContext()); }

// Tab bar
void uiBeginTabBar(uint32_t maxTabsInRow)
{
    pUserInterface->mCurrentTabIdx = 0;
    pUserInterface->mTabCount = maxTabsInRow;
    pUserInterface->mTabRowCount = 1;
    nk_menubar_begin(getContext());
    nk_layout_row_begin(getContext(), NK_STATIC, uiCalculateHeightOfTextRows(DEFAULT_ROW_HEIGHT), maxTabsInRow);
}

static void pushTabWidget(float labelWidth)
{
    nk_context* ctx = getContext();

    nk_layout_row_push(ctx, labelWidth);
    float4 nextWidgetBound = uiLayoutPeek();

    if ((nextWidgetBound.x + nextWidgetBound.z) >= (ctx->current->bounds.x + ctx->current->bounds.w))
    {
        nk_layout_row_begin(getContext(), NK_STATIC, uiCalculateHeightOfTextRows(DEFAULT_ROW_HEIGHT), pUserInterface->mTabCount);
        nk_layout_row_push(ctx, labelWidth);
        pUserInterface->mTabRowCount += 1;
    }
}

static void pushTabWidgetForRoundScreen(float labelWidth, float roundSize)
{
    float screenWidth = pUserInterface->mWidth;
    float screenHeight = pUserInterface->mHeight;

    nk_context* ctx = getContext();
    float4      nextWidgetBound = uiLayoutPeek();

    auto getHorizontalOffset = [&](float y) -> float
    {
        if (roundSize <= 0)
            return 0;

        float offset = 0;
        if (y < roundSize)
        {
            float dy = roundSize - y;
            offset = roundSize - sqrtf(roundSize * roundSize - dy * dy);
        }
        else if (y > (screenHeight - roundSize))
        {
            float dy = y - (screenHeight - roundSize);
            offset = roundSize - sqrtf(roundSize * roundSize - dy * dy);
        }
        return offset;
    };

    float currentY = nextWidgetBound.y;
    float currentX = nextWidgetBound.x;

    float leftInset = getHorizontalOffset(currentY);
    float rightInset = screenWidth - getHorizontalOffset(currentY);

    if (currentX < leftInset)
    {
        float spaceNeeded = leftInset - currentX;
        nk_layout_row_push(ctx, spaceNeeded);
        nk_spacer(ctx);

        nextWidgetBound = uiLayoutPeek();
        currentX = nextWidgetBound.x;
    }

    if ((currentX + labelWidth) > rightInset)
    {
        nk_layout_row_begin(ctx, NK_STATIC, uiCalculateHeightOfTextRows(DEFAULT_ROW_HEIGHT), pUserInterface->mTabCount);
        pUserInterface->mTabRowCount += 1;

        float newY = currentY + DEFAULT_ROW_HEIGHT;
        float newLeftInset = getHorizontalOffset(newY);

        if (newLeftInset > 0)
        {
            nk_layout_row_push(ctx, newLeftInset);
            nk_spacer(ctx);
        }
    }

    nk_layout_row_push(ctx, labelWidth);
}

TFUIWidgetInteraction uiTab(const char* title, uint32_t labelWidth, int32_t* pSelectedTab, bool* pActive)
{
    ASSERT(title);
    ASSERT(pUserInterface->mCurrentTabIdx != -1);

    TFUIWidgetInteraction result = {};
    result.type = TF_WIDGET_INTERACTION_VISIBLE;
    if (!*pActive)
    {
        pUserInterface->mCurrentTabIdx++;
        return result;
    }

    nk_context*    ctx = getContext();
    struct nk_rect bounds = nk_layout_space_bounds(getContext());
    pushTabWidget((float)labelWidth + bounds.h);

    BeginGamepadWidget();
    bool wasDeleted = false;
    bool isSelected = pUserInterface->mCurrentTabIdx == *pSelectedTab;
#ifndef NK_INCLUDE_STANDARD_BOOL
    nk_bool internalWasDeleted = false;
    nk_bool internalSelected = isSelected;
    nk_menu_tab_label_closable(ctx, title, NK_TEXT_LEFT, &internalSelected, &internalWasDeleted);
    wasDeleted = (bool)internalWasDeleted;
    isSelected = (bool)internalSelected;
#else
    nk_menu_tab_label_closable(ctx, title, NK_TEXT_LEFT, (nk_bool*)&internalSelected, (nk_bool*)&wasDeleted);
#endif
    if (wasDeleted)
    {
        *pActive = false;
    }
    else if (isSelected)
    {
        *pSelectedTab = pUserInterface->mCurrentTabIdx;
    }

    result.visible = *pActive && pUserInterface->mCurrentTabIdx == *pSelectedTab;
    EndGamepadWidget();

    pUserInterface->mCurrentTabIdx++;
    return result;
}

void uiEndTabBar()
{
    pUserInterface->mCurrentTabIdx = -1;
    pUserInterface->mTabCount = 0;
    pUserInterface->mTabRowCount = 0;
    nk_menubar_end(getContext());
}

void uiStaticBeginTabBar(uint32_t tabCount, int32_t* pSelectedTab)
{
    pUserInterface->mCurrentTabIdx = 0;
    pUserInterface->mTabCount = tabCount;
    pUserInterface->mTabRowCount = 1;

    nk_menubar_begin(getContext());
    nk_layout_row_begin(getContext(), NK_STATIC, uiCalculateHeightOfTextRows(DEFAULT_ROW_HEIGHT), tabCount);

    GamepadSliderAction action = BeginGamepadWidget(0, 0, true);

    if (action.mType != GamepadSliderActionType::GAMEPAD_HORIZONTAL_ACTION_TYPE_PRESS)
    {
        action.mStepDirection = 0;
    }
    int32_t tabSelection = action.mStepDirection;
    *pSelectedTab += tabSelection;
    if (*pSelectedTab < 0)
    {
        *pSelectedTab += tabCount;
    }
    else if (*pSelectedTab >= (int32_t)tabCount)
    {
        *pSelectedTab %= tabCount;
    }
    EndGamepadWidget();
}

static TFUIWidgetInteraction doStaticTabLabel(const char* title, int32_t* pSelectedTab)
{
    TFUIWidgetInteraction result = {};
    result.type = TF_WIDGET_INTERACTION_VISIBLE;
    nk_context* ctx = getContext();
    bool        isSelected = pUserInterface->mCurrentTabIdx == *pSelectedTab;
#ifndef NK_INCLUDE_STANDARD_BOOL
    nk_bool internalSelected = isSelected;
    nk_menu_tab_label(ctx, title, NK_TEXT_LEFT, &internalSelected);
    isSelected = (bool)internalSelected;
#else
    nk_menu_tab_label(ctx, title, NK_TEXT_LEFT, (nk_bool*)&internalSelected);
#endif
    if (isSelected)
    {
        *pSelectedTab = pUserInterface->mCurrentTabIdx;
    }

    result.visible = pUserInterface->mCurrentTabIdx == *pSelectedTab;
    pUserInterface->mCurrentTabIdx++;
    return result;
}

TFUIWidgetInteraction uiStaticTabRoundScreen(const char* title, uint32_t labelWidth, uint32_t roundSize, int32_t* pSelectedTab)
{
    ASSERT(title);
    ASSERT(pUserInterface->mCurrentTabIdx != -1);

    struct nk_rect bounds = nk_layout_space_bounds(getContext());
    pushTabWidgetForRoundScreen((float)labelWidth + bounds.h, (float)roundSize);
    return doStaticTabLabel(title, pSelectedTab);
}

TFUIWidgetInteraction uiStaticTab(const char* title, uint32_t labelWidth, int32_t* pSelectedTab)
{
    ASSERT(title);
    ASSERT(pUserInterface->mCurrentTabIdx != -1);

    struct nk_rect bounds = nk_layout_space_bounds(getContext());
    pushTabWidget((float)labelWidth + bounds.h);
    return doStaticTabLabel(title, pSelectedTab);
}

void uiStaticEndTabBar()
{
    nk_menubar_end(getContext());
    pUserInterface->mCurrentTabIdx = -1;
    pUserInterface->mTabCount = 0;
    pUserInterface->mTabRowCount = 0;
}

int32_t uiGetTabRowCount() { return pUserInterface->mTabRowCount; }

// Window utilities

TFUIWindowHandle uiFindWindowByTitle(const char* title)
{
    int     titleLen = (int)nk_strlen(title);
    nk_hash titleHash = nk_murmur_hash(title, (int)titleLen, NK_WINDOW_TITLE);
    return nk_find_window(getContext(), titleHash, title);
}

TFUIWindowHandle uiGetCurrentWindow() { return getContext()->current; }

void uiBeginWidgetDisable() { nk_widget_disable_begin(getContext()); }

void uiEndWidgetDisable() { nk_widget_disable_end(getContext()); }

vec2 uiGetWindowSize()
{
    const struct nk_window* wnd = (const struct nk_window*)uiGetCurrentWindow();
    return vec2(wnd->bounds.w, wnd->bounds.h);
}

void uiPushWindowBackgroundColor(float4 color)
{
    struct nk_context* ctx = getContext();
    struct nk_style*   style = &ctx->style;
    struct nk_color    backgroundColor = float4ToNkColor(color);
    nk_style_push_color(ctx, &style->window.background, backgroundColor);
}

void uiPopWindowBackgroundColor()
{
    struct nk_context* ctx = getContext();
    nk_style_pop_color(ctx);
}

void uiPushWindowTransparency(float alpha)
{
    struct nk_context* ctx = getContext();
    struct nk_style*   style = &ctx->style;
    struct nk_color    backgroundColor = style->window.background;
    backgroundColor.a = (nk_byte)(NK_SATURATE(alpha) * 255.0f);
    nk_style_push_color(ctx, &style->window.background, backgroundColor);
}

void uiPopWindowTransparency() //-V::524
{
    struct nk_context* ctx = getContext();
    nk_style_pop_color(ctx);
}

void uiUpdateWindowVisibilityByTitle(const char* title, bool visible)
{
    struct nk_window* win = (struct nk_window*)uiFindWindowByTitle(title);
    if (!win)
    {
        int     titleLen = (int)nk_strlen(title);
        nk_hash titleHash = nk_murmur_hash(title, (int)titleLen, NK_WINDOW_TITLE);
        if (visible)
        {
            for (uint32_t i = 0; i < arrlenu(pUserInterface->pWindowsToHide); i++)
            {
                if (titleHash == pUserInterface->pWindowsToHide[i])
                {
                    arrdelswap(pUserInterface->pWindowsToHide, i);
                    break;
                }
            }
        }
        else
        {
            arrpush(pUserInterface->pWindowsToHide, titleHash);
        }
        return;
    }

    nk_window_show(getContext(), title, (nk_show_states)visible);
}

void uiUpdateWindowVisibilityByHandle(TFUIWindowHandle handle, bool visible)
{
    ASSERT(handle && "Attempted to show/hide an invalid window.");
    nk_window_show(getContext(), ((struct nk_window*)handle)->name_string, (nk_show_states)visible);
}

/****************************************************************************/
// MARK: - Layout Manipulation Functions
/****************************************************************************/
#if CUSTOM_LAYOUT
static UILayoutRow* createNewLayoutRow(UILayoutPanel* panel, TFUILayoutFormat format, float rowHeight, int widgetCount)
{
    const struct nk_style* style = &getContext()->style;
    UILayoutRow            newRow;
    newRow.mType = format;
    newRow.mHeight = rowHeight + style->window.spacing.y;
    newRow.mPos = panel->mLayoutCursor;
    newRow.mItemCount = widgetCount;
    newRow.mTotalWidth = 0;
    newRow.pItems = NULL;
    return &arrput(panel->pRows, newRow);
}
#endif

static void layoutSpaceBeginImpl(TFUILayoutFormat format, float rowHeight, int widgetCount, bool manualMode)
{
    struct nk_context* ctx = getContext();
#if CUSTOM_LAYOUT
    const struct nk_style* style = &ctx->style;
    struct nk_window*      win = ctx->current;
    UIBlackboard*          bb = &pUserInterface->mBlackboard;
    UIWidgetGroup*         group = bb->pCurrentWidgetGroup;
    UILayoutPanel*         panel = &group->mPanel;
    UILayoutRow*           nextRow = NULL;

    bb->mLayoutRowChanged = false;

    if (panel->pRows && arrlenu(panel->pRows) != 0)
    {
        nextRow = &panel->pRows[panel->mRowCursor.getY()];
        if (nextRow->mPos != panel->mLayoutCursor || nextRow->mType != format || nextRow->mHeight != rowHeight ||
            nextRow->mItemCount != widgetCount)
        {
            nextRow->mPos = panel->mLayoutCursor;
            nextRow->mType = format;
            nextRow->mHeight = rowHeight;
            nextRow->mItemCount = widgetCount;
            bb->mLayoutRowChanged = true;
        }
    }
    else
    {
        nextRow = createNewLayoutRow(panel, format, rowHeight, widgetCount);
        bb->mLayoutRowChanged = true;
    }

    if (panel->mFlags & NK_WINDOW_DYNAMIC)
    {
        if (bb->mLayoutRowChanged)
        {
            saveLastWidgetOffset();
            // draw background for dynamic panels
            struct nk_rect            background;
            struct nk_command_buffer* out = &win->buffer;
            background.x = win->bounds.x;
            background.w = win->bounds.w;
            background.y = win->bounds.y + nextRow->mHeight - 1.0f;
            background.h = nextRow->mHeight + 1.0f;
            nk_fill_rect(out, background, 0, style->window.background);
            nextRow->mBackgroundStart = getLastWidgetCommandStart();
            nextRow->mBackgroundEnd = getLastWidgetCommandEnd();
        }
        else
        {
            // copy background command
            pushbackCachedCommands(bb->pWindowCache, nextRow->mBackgroundStart, nextRow->mBackgroundEnd);
        }
    }

    bb->pCurrentLayoutRow = nextRow;
    bb->mLayoutRowManualMode = manualMode;

    // setup Nuklear row layout data
    struct nk_panel* layout = win->layout;
    struct nk_vec2   item_spacing = style->window.spacing;
    switch (nextRow->mType)
    {
    case TF_LAYOUT_DYNAMIC:
        (manualMode) ? layout->row.type = NK_LAYOUT_DYNAMIC_FREE : layout->row.type = NK_LAYOUT_DYNAMIC_FIXED;
        break;
    case TF_LAYOUT_STATIC:
        (manualMode) ? layout->row.type = NK_LAYOUT_STATIC_FREE : layout->row.type = NK_LAYOUT_STATIC_FIXED;
        break;
    }
    layout->row.index = 0;
    layout->row.columns = nextRow->mItemCount;
    layout->at_y += layout->row.height;
    if (nextRow->mHeight == 0.0f)
        layout->row.height = NK_MAX(nextRow->mHeight, layout->row.min_height) + item_spacing.y;
    else
        layout->row.height = nextRow->mHeight + item_spacing.y;
#else
    UNREF_PARAM(manualMode);
    nk_layout_space_begin(ctx, (nk_layout_format)format, rowHeight, widgetCount);
#endif
}

void uiLayoutSpaceBegin(TFUILayoutFormat format, float rowHeight, int widgetCount)
{
    layoutSpaceBeginImpl(format, rowHeight, widgetCount, true);
}

void uiLayoutSpacePush(vec2 pos, vec2 size)
{
    struct nk_context* ctx = getContext();
#if CUSTOM_LAYOUT
    UIBlackboard*    bb = &pUserInterface->mBlackboard;
    UILayoutPanel*   panel = &bb->pCurrentWidgetGroup->mPanel;
    UILayoutRow*     row = bb->pCurrentLayoutRow;
    UILayoutRowItem* item = row->pItems && arrlenu(row->pItems) > panel->mRowCursor[0] ? &row->pItems[panel->mRowCursor[0]] : NULL;

    ASSERT(bb->mLayoutRowManualMode);

    if (!item)
    {
        UILayoutRowItem newItem;
        item = &arrput(row->pItems, newItem);
        bb->mLayoutRowChanged = true;
    }
    else if (item->mOffset != pos || item->mWidth != size[0] || item->mHeight != size[1])
    {
        bb->mLayoutRowChanged = true;
    }

    if (bb->mLayoutRowChanged)
    {
        item->mWidth = size[0];
        item->mHeight = size[1];
        item->mOffset = pos;
    }

    struct nk_panel* layout = ctx->current->layout;
    ASSERT(layout);
    layout->row.item = nk_rect(item->mOffset[0], item->mOffset[1], item->mWidth, item->mHeight);
    layout->row.index = panel->mRowCursor[0];

    panel->mRowCursor[0]++;
    panel->mLayoutCursor = pos;
#else
    nk_layout_space_push(ctx, nk_rect(pos.x, pos.y, size.x, size.y));
#endif
}

void uiLayoutSpaceEnd()
{
    struct nk_context* ctx = getContext();
#if CUSTOM_LAYOUT
    UIBlackboard*  bb = &pUserInterface->mBlackboard;
    UILayoutPanel* panel = &bb->pCurrentWidgetGroup->mPanel;
    UILayoutRow*   row = bb->pCurrentLayoutRow;

    if (bb->mLayoutRowChanged)
    {
        float totalHeight = 0;
        float totalWidth = 0;
        for (int i = 0; i < row->mItemCount; i++)
        {
            UILayoutRowItem* item = &row->pItems[i];
            totalHeight += item->mHeight;
            totalWidth += item->mWidth;
        }
        row->mHeight = max(totalHeight, row->mHeight);
        row->mTotalWidth = totalWidth;
    }

    panel->mLayoutCursor[0] = 0.0f;
    panel->mLayoutCursor[1] = row->mPos[1] + row->mHeight;

    struct nk_panel* layout = ctx->current->layout;
    ASSERT(layout);
    nk_zero(&layout->row.item, sizeof(layout->row.item));

    panel->mRowCursor[1]++;
    panel->mRowCursor[0] = 0;
#else
    nk_layout_space_end(ctx);
#endif
}

void uiLayoutRow(TFUILayoutFormat format, float rowHeight, int widgetCount, const float* widgetWidths)
{
    nk_layout_row(getContext(), (nk_layout_format)format, rowHeight, widgetCount, widgetWidths);
}

void uiLayoutRowBegin(TFUILayoutFormat format, float rowHeight, int cols)
{
    nk_layout_row_begin(getContext(), (nk_layout_format)format, rowHeight, cols);
}

void uiLayoutRowPush(float ratioOrWidth) { nk_layout_row_push(getContext(), ratioOrWidth); }

void uiLayoutRowEnd() { nk_layout_row_end(getContext()); }

float uiGetDefaultRowHeight() { return DEFAULT_ROW_HEIGHT; }

float uiGetDefaultRowPadding() { return getContext()->style.window.spacing.y; }

float4 uiLayoutPeek()
{
    struct nk_rect nextWidgetBounds;
    nk_layout_peek(&nextWidgetBounds, getContext());
    return float4(nextWidgetBounds.x, nextWidgetBounds.y, nextWidgetBounds.w, nextWidgetBounds.h);
}

void uiLayoutAutoRows(int colsPerRow) { uiLayoutDynamicRows(DEFAULT_ROW_HEIGHT, colsPerRow); }

void uiLayoutAutoTextRows(int colsPerRow) { uiLayoutDynamicTextRows(DEFAULT_ROW_HEIGHT, colsPerRow); }

void uiLayoutDynamicRows(float rowHeight, int colsPerRow) { nk_layout_row_dynamic(getContext(), rowHeight, colsPerRow); }

void uiLayoutDynamicTextRows(float rowHeight, int colsPerRow) { uiLayoutDynamicRows(uiCalculateHeightOfTextRows(rowHeight), colsPerRow); }

void uiLayoutSetMinRowHeight(float minHeight) { nk_layout_set_min_row_height(getContext(), minHeight); }

void uiLayoutHorizontalSpace(int cols) { nk_spacing(getContext(), cols); }

// Layout helpers
vec2 uiLayoutGetTextSize(const char* text, int textLength)
{
    const struct nk_style* style = &getContext()->style;
    struct nk_vec2         padding = style->text.padding;

    float textWidth = style->font->width(style->font->userdata, style->font->height, text, textLength);
    textWidth += (4 * padding.x);
    float textHeight = (style->font->height + 2 * padding.y);

    return vec2(textWidth, textHeight);
}

float uiLayoutGetFontHeight() { return pUserInterface->mFontHeight; }

vec2 uiLayoutGetPadding()
{
    const struct nk_style* style = &getContext()->style;
    return { style->window.padding.x, style->window.padding.y };
}

float4 uiLayoutSpaceBounds()
{
    struct nk_rect spaceBounds = nk_layout_space_bounds(getContext());
    struct nk_rect windowBounds = nk_window_get_bounds(getContext());
    return { spaceBounds.x - windowBounds.x, spaceBounds.y - windowBounds.y, spaceBounds.w, spaceBounds.h };
}

void uiPushStyleColor(TFUIStyleColor style, float4 color)
{
    static size_t offsets[TF_UI_COLOR_COUNT];
    offsets[TF_UI_COLOR_TEXT] = offsetof(nk_style, text.color);
    offsets[TF_UI_COLOR_SELECT_NORMAL] = offsetof(nk_style, selectable.text_normal);
    offsets[TF_UI_COLOR_SELECT_HOVER] = offsetof(nk_style, selectable.text_hover);
    offsets[TF_UI_COLOR_SELECT_PRESSED] = offsetof(nk_style, selectable.text_pressed);
    offsets[TF_UI_COLOR_SELECT_NORMAL_ACTIVE] = offsetof(nk_style, selectable.text_normal_active);
    offsets[TF_UI_COLOR_SELECT_HOVER_ACTIVE] = offsetof(nk_style, selectable.text_hover_active);
    offsets[TF_UI_COLOR_SELECT_PRESSED_ACTIVE] = offsetof(nk_style, selectable.text_pressed_active);

    nk_context* ctx = getContext();
    uint8_t*    styleData = (uint8_t*)(&ctx->style);
    styleData = styleData + offsets[style];
    nk_style_push_color(ctx, (nk_color*)styleData, float4ToNkColor(color));
}

void uiPopStyleColor() { nk_style_pop_color(getContext()); }

#if CUSTOM_LAYOUT
// automatically provides widget bounds and advances the layout row if manual mode is not enabled
static struct nk_rect layoutWidgetSpace()
{
    UIBlackboard* bb = &pUserInterface->mBlackboard;

    ASSERT(!bb->mLayoutRowManualMode);

    UILayoutPanel*   panel = &bb->pCurrentWidgetGroup->mPanel;
    UILayoutRow*     row = bb->pCurrentLayoutRow;
    UILayoutRowItem* item = row->pItems && arrlenu(row->pItems) > panel->mRowCursor[0] ? &row->pItems[panel->mRowCursor[0]] : NULL;

    if (!item)
    {
        UILayoutRowItem newItem;
        item = &arrput(row->pItems, newItem);
        bb->mLayoutRowChanged = true;
    }
    else if (item->mOffset.getX() != panel->mLayoutCursor.getX() || item->mOffset.getY() != panel->mLayoutCursor.getY())
    {
        bb->mLayoutRowChanged = true;
    }

    if (!bb->mLayoutRowChanged)
    {
        return nk_rect(item->mOffset.getX(), item->mOffset.getY(), item->mWidth, item->mHeight);
    }

    struct nk_rect     newBounds;
    struct nk_context* ctx = getContext();
    struct nk_window*  wnd = ctx->current;
    nk_layout_widget_space(&newBounds, ctx, wnd, true);
    return newBounds;
}

static void advanceLayoutRow(UILayoutPanel* panel, struct nk_rect extent)
{
    UIBlackboard* bb = &pUserInterface->mBlackboard;

    panel->mRowCursor[0]++;

    if (panel->mRowCursor[0] > bb->pCurrentLayoutRow->mItemCount)
    {
        UILayoutRow* row = bb->pCurrentLayoutRow;
        uiLayoutSpaceEnd();
        if (panel->pRows && arrlenu(panel->pRows) != 0)
        {
            row = &panel->pRows[panel->mRowCursor.getY()];
        }
        layoutSpaceBeginImpl(row->mType, row->mHeight, row->mItemCount, false);
    }
    else
    {
        panel->mLayoutCursor[0] = extent.x;
        panel->mLayoutCursor[1] = extent.y;

        struct nk_panel* layout = getContext()->current->layout;
        ASSERT(layout);
        layout->row.item = extent;
        layout->row.index = panel->mRowCursor[0];
    }
}

static struct nk_rect getNextWidgetBounds()
{
    struct nk_context* ctx = getContext();
    struct nk_rect     nextBounds;

    UIBlackboard*    bb = &pUserInterface->mBlackboard;
    struct nk_panel* layout = ctx->current->layout;
    UILayoutPanel*   panel = &bb->pCurrentWidgetGroup->mPanel;
    ASSERT(layout);
    if (bb->mLayoutRowManualMode)
    {
        nextBounds = layout->row.item;
    }
    else
    {
        nextBounds = layoutWidgetSpace();
    }

    advanceLayoutRow(panel, nextBounds);

    return nextBounds;
}
#endif

/****************************************************************************/
// MARK: - Font Public Functions
/****************************************************************************/

/****************************************************************************/
// MARK: - Nuklear Font Callbacks
/****************************************************************************/

void queryGlyphData(nk_handle handle, float font_height, struct nk_user_font_glyph* glyph, nk_rune codepoint, nk_rune next_codepoint)
{
    UNREF_PARAM(handle);
    TFFont* pFont = pUserInterface->pFont;

    // TODO: Get rid of stb_truetype, unicode, font atlas in nuklear

    TFFontGlyphQueryDesc queryDesc{};
    queryDesc.mFontSize = font_height / pUserInterface->mFontDpiScale;
    queryDesc.mCodepoint = codepoint;
    queryDesc.mAdditionalCodepoint = next_codepoint;
    queryDesc.mAdditionalCodepointIsNext = true;

    TFFontGlyphData glyphData = fntQueryGlyph(pFont, &queryDesc);

    glyph->width = glyphData.mBound.z - glyphData.mBound.x;
    glyph->height = glyphData.mBound.w - glyphData.mBound.y;
    glyph->xadvance = glyphData.mXAdvance;
    glyph->uv[0].x = glyphData.mTexcoord.x;
    glyph->uv[0].y = glyphData.mTexcoord.y;
    glyph->uv[1].x = glyphData.mTexcoord.z;
    glyph->uv[1].y = glyphData.mTexcoord.w;
    glyph->offset.x = glyphData.mBound.x;
    glyph->offset.y = glyphData.mBound.y;
}

float queryTextWidth(nk_handle handle, float h, const char* text, int len)
{
    UNREF_PARAM(handle);
    TFFont* pFont = pUserInterface->pFont;

    TFFontGlyphQueryDesc queryDesc = {};
    queryDesc.mFontSize = h / pUserInterface->mFontDpiScale;
    queryDesc.mAdditionalCodepointIsNext = false;
    queryDesc.mOnlyXAdvance = true;

    float width = 0.0f;

    // Optimization specific to text wrapping. Queries are done repeatedly after
    // adding a single codepoint. If this is the case, reuse the previous result.
    bool                                    optimizedMeasure = false;
    struct TFUserInterface::LastWidthQuery* lastQuery = &pUserInterface->mLastWidthQuery;
    if (text == lastQuery->pStr && len > 0 && pFont == lastQuery->pFont && queryDesc.mFontSize == lastQuery->mFontSize)
    {
        if (len == lastQuery->mLength)
        {
            width = lastQuery->mWidth;
            optimizedMeasure = true;
        }
        else if (len > lastQuery->mLength)
        {
            // Consume a single codepoint, then check whether that was all that was added
            const char* current = &text[lastQuery->mLength];
            TFCodepoint codepoint = 0;

            width = lastQuery->mWidth;
            while (*current && current != text + len)
            {
                current = fntConsumeSymbol(current, &codepoint);

                queryDesc.mCodepoint = codepoint;
                queryDesc.mAdditionalCodepoint = lastQuery->mCodepoint;
                TFFontGlyphData glyphData = fntQueryGlyph(pFont, &queryDesc);
                width += glyphData.mXAdvance;
                lastQuery->mCodepoint = codepoint;
            }

            optimizedMeasure = true;
        }
    }

    if (!optimizedMeasure)
    {
        const char* current = text;
        TFCodepoint codepoint = 0;
        width = 0;
        while (*current && current != text + len)
        {
            current = fntConsumeSymbol(current, &codepoint);

            queryDesc.mCodepoint = codepoint;
            queryDesc.mAdditionalCodepoint = lastQuery->mCodepoint;
            TFFontGlyphData glyphData = fntQueryGlyph(pFont, &queryDesc);
            width += glyphData.mXAdvance;
            lastQuery->mCodepoint = codepoint;
        }
    }

    lastQuery->pStr = text;
    lastQuery->mLength = len;
    lastQuery->pFont = pFont;
    lastQuery->mFontSize = queryDesc.mFontSize;
    lastQuery->mWidth = width;

    return width;
}

void clipboardPaste(nk_handle usr, struct nk_text_edit* edit)
{
    UNREF_PARAM(usr);

    const char* text = bdata(&pUserInterface->mClipboardString);
    if (text)
    {
        nk_textedit_paste(edit, text, nk_strlen(text));
    }
}

void clipboardCopy(nk_handle usr, const char* text, int len)
{
    UNREF_PARAM(usr);

    if (len <= 0)
        return;

    bassignblk(&pUserInterface->mClipboardString, text, len);
}

#endif // ENABLE_FORGE_UI

/****************************************************************************/
// MARK: - Application Life Cycle
/****************************************************************************/

#if defined(ENABLE_FORGE_VR_UI)
void initVRUserInterface();
void exitVRUserInterface();
#endif

FORGE_API void initUserInterface(TFUserInterfaceDesc* pDesc)
{
#ifdef ENABLE_FORGE_UI
    pUserInterface->pRenderer = pDesc->pRenderer;
    pUserInterface->pPipelineCache = pDesc->pCache;
    pUserInterface->mMaxUserTextures = pDesc->mMaxUserTextures;
    pUserInterface->mFrameMaxCount = pDesc->mFrameMaxCount;
    pUserInterface->pFrameIdx = pDesc->pFrameIdx;
    pUserInterface->mCompactLayout = pDesc->mCompactLayout;
    ASSERT(pUserInterface->mFrameMaxCount <= MAX_FRAMES);

    // Rendering resources
    TFBufferLoadDesc vbDesc = {};
    vbDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_VERTEX_BUFFER;
    vbDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
    vbDesc.mDesc.mSize = VERTEX_BUFFER_SIZE * pDesc->mFrameMaxCount;
    vbDesc.mDesc.mFlags = TF_BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
    vbDesc.mDesc.pName = "UI Vertex Buffer";
    vbDesc.ppBuffer = &pUserInterface->pVertexBuffer;
    addResource(&vbDesc, NULL);

    TFBufferLoadDesc ibDesc = vbDesc;
    ibDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_INDEX_BUFFER;
    ibDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
    ibDesc.mDesc.mSize = INDEX_BUFFER_SIZE * pDesc->mFrameMaxCount;
    ibDesc.mDesc.mFlags = TF_BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
    ibDesc.mDesc.pName = "UI Index Buffer";
    ibDesc.ppBuffer = &pUserInterface->pIndexBuffer;
    addResource(&ibDesc, NULL);

    TFBufferLoadDesc ubDesc = {};
    ubDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    ubDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
    ubDesc.mDesc.mFlags = TF_BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
    ubDesc.mDesc.mSize = sizeof(UniformBlock);
    ubDesc.mDesc.pName = "UI Uniform Buffer";
    for (uint32_t i = 0; i < pDesc->mFrameMaxCount; ++i)
    {
        ubDesc.ppBuffer = &pUserInterface->pUniformBuffer[i];
        addResource(&ubDesc, NULL);
    }

    TFVertexLayout* vertexLayout = &pUserInterface->mVertexLayoutTextured;
    vertexLayout->mBindingCount = 1;
    vertexLayout->mAttribCount = 5;
    vertexLayout->mAttribs[0].mSemantic = TF_SEMANTIC_POSITION;
    vertexLayout->mAttribs[0].mFormat = TinyImageFormat_R32G32_SFLOAT;
    vertexLayout->mAttribs[0].mBinding = 0;
    vertexLayout->mAttribs[0].mLocation = 0;
    vertexLayout->mAttribs[0].mOffset = 0;
    vertexLayout->mAttribs[1].mSemantic = TF_SEMANTIC_TEXCOORD0;
    vertexLayout->mAttribs[1].mFormat = TinyImageFormat_R32G32_SFLOAT;
    vertexLayout->mAttribs[1].mBinding = 0;
    vertexLayout->mAttribs[1].mLocation = 1;
    vertexLayout->mAttribs[1].mOffset = offsetof(UIVertex, uv);
    vertexLayout->mAttribs[2].mSemantic = TF_SEMANTIC_COLOR;
    vertexLayout->mAttribs[2].mFormat = TinyImageFormat_R8G8B8A8_UNORM;
    vertexLayout->mAttribs[2].mBinding = 0;
    vertexLayout->mAttribs[2].mLocation = 2;
    vertexLayout->mAttribs[2].mOffset = offsetof(UIVertex, color);
    vertexLayout->mAttribs[3].mSemantic = TF_SEMANTIC_TEXCOORD1;
    vertexLayout->mAttribs[3].mFormat = TinyImageFormat_R32_UINT;
    vertexLayout->mAttribs[3].mBinding = 0;
    vertexLayout->mAttribs[3].mLocation = 3;
    vertexLayout->mAttribs[3].mOffset = offsetof(UIVertex, textureIndex);
    vertexLayout->mAttribs[4].mSemantic = TF_SEMANTIC_TEXCOORD2;
    vertexLayout->mAttribs[4].mFormat = TinyImageFormat_R32_SFLOAT;
    vertexLayout->mAttribs[4].mBinding = 0;
    vertexLayout->mAttribs[4].mLocation = 4;
    vertexLayout->mAttribs[4].mOffset = offsetof(UIVertex, fontScale);

    // 1x2 white texture for Nuklear convert config
    TFTextureDesc nullTexDesc = {};
    nullTexDesc.mArraySize = 1;
    nullTexDesc.mDepth = 1;
    nullTexDesc.mFormat = TinyImageFormat_R32G32B32A32_SFLOAT;
    nullTexDesc.mHeight = 2; // needs to be 2 to prevent treating the texture as 1D
    nullTexDesc.mWidth = 1;
    nullTexDesc.mMipLevels = 1;
    nullTexDesc.mSampleCount = TF_SAMPLE_COUNT_1;
    nullTexDesc.mStartState = TF_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    nullTexDesc.mDescriptors = TF_DESCRIPTOR_TYPE_TEXTURE;
    nullTexDesc.pName = "uiNullTex";

    TFTextureLoadDesc nullTexLoadDesc = {};
    nullTexLoadDesc.pDesc = &nullTexDesc;
    nullTexLoadDesc.ppTexture = &pUserInterface->pNullTexture;
    addResource(&nullTexLoadDesc, NULL);

    TFTextureUpdateDesc texUpdateDesc = { pUserInterface->pNullTexture, 0, 1, 0, 1, TF_RESOURCE_STATE_PIXEL_SHADER_RESOURCE };
    beginUpdateResource(&texUpdateDesc);
    TFTextureSubresourceUpdate subresource = texUpdateDesc.getSubresourceUpdateDesc(0, 0);
    for (uint32_t r = 0; r < subresource.mRowCount; ++r)
    {
        const float4 whiteColor = { 1.0f, 1.0f, 1.0f, 1.0f };
        memcpy(subresource.pMappedData + r * subresource.mSrcRowStride, &whiteColor, subresource.mSrcRowStride);
    }
    endUpdateResource(&texUpdateDesc);

    // init internal allocator for Nuklear
    pUserInterface->mAllocator.alloc = alloc_func;
    pUserInterface->mAllocator.free = free_func;
    pUserInterface->mAllocator.userdata.ptr = NULL;

    nk_buffer_init(&pUserInterface->mCmdBuffer, &pUserInterface->mAllocator, NK_BUFFER_DEFAULT_INITIAL_SIZE);
    nk_buffer_init(&pUserInterface->mLastBuffer, &pUserInterface->mAllocator, NK_BUFFER_DEFAULT_INITIAL_SIZE);

    static const struct nk_draw_vertex_layout_element vertex_layout[] = {
        { NK_VERTEX_POSITION, NK_FORMAT_FLOAT, NK_OFFSETOF(UIVertex, pos) },
        { NK_VERTEX_TEXCOORD, NK_FORMAT_FLOAT, NK_OFFSETOF(UIVertex, uv) },
        { NK_VERTEX_COLOR, NK_FORMAT_R8G8B8A8, NK_OFFSETOF(UIVertex, color) },
        { NK_VERTEX_LAYOUT_END }
    };

    // setup Nuklear's convert config for converting draw commands to vertex output
    pUserInterface->mConvertCfg.shape_AA = NK_ANTI_ALIASING_OFF;
    pUserInterface->mConvertCfg.line_AA = NK_ANTI_ALIASING_OFF;
    pUserInterface->mConvertCfg.vertex_layout = vertex_layout;
    pUserInterface->mConvertCfg.vertex_size = sizeof(UIVertex);
    pUserInterface->mConvertCfg.vertex_alignment = NK_ALIGNOF(UIVertex);
    // TODO: parameterize these
    pUserInterface->mConvertCfg.circle_segment_count = 64;
    pUserInterface->mConvertCfg.curve_segment_count = 22;
    pUserInterface->mConvertCfg.arc_segment_count = 22;
    pUserInterface->mConvertCfg.global_alpha = 1.0f;
    pUserInterface->mConvertCfg.tex_null.texture.ptr = pUserInterface->pNullTexture;
    pUserInterface->mConvertCfg.tex_null.texture.id = -1;
    pUserInterface->mConvertCfg.tex_null.uv = { 0, 0 };

    if (pDesc->mFontHeight == -1)
    {
        if (pUserInterface->mPlatformDefaultFontHeight != -1)
        {
            pUserInterface->mFontHeight = pUserInterface->mPlatformDefaultFontHeight;
        }
        else
        {
            pUserInterface->mFontHeight = DEFAULT_UI_FONT_SIZE;
        }
    }
    else
    {
        pUserInterface->mFontHeight = pDesc->mFontHeight;
    }

    pUserInterface->mFontDpiScale = min(pUserInterface->mDpiScale[0], pUserInterface->mDpiScale[1]);
    pUserInterface->pFont = pDesc->pFont;

    nk_user_font* font = &pUserInterface->mFontNuklear;
    font->userdata.ptr = pUserInterface->pFont;
    font->userdata.id = 0;
    font->height = pUserInterface->mFontHeight * pUserInterface->mFontDpiScale;
    font->width = queryTextWidth;
    font->query = queryGlyphData;
    nk_init(getContext(), &pUserInterface->mAllocator, font);

    nk_context* ctx = getContext();
    ctx->clip.copy = clipboardCopy;
    ctx->clip.paste = clipboardPaste;

    fillDefaultInputKeyMap(pUserInterface->mInputKeyMap, pUserInterface->mInputButtonMap, &pUserInterface->mInputCursorBindings.x,
                           &pUserInterface->mInputCursorBindings.y, &pUserInterface->mInputScrollBindings.down,
                           &pUserInterface->mInputScrollBindings.up, &pUserInterface->mHideUIBinding,
                           &pUserInterface->mGamepadControlButtons);

    setDefaultStyle();

    pUserInterface->mContextMenu.pTabStack = NULL;
    pUserInterface->ppWindowNameArrSWL = NULL;

    pUserInterface->mAutoSwitchSWL = false;
    pUserInterface->mHadConnectedGamepad = false;

    pUserInterface->mInitialized = true;

#ifdef ENABLE_FORGE_VR_UI
    initVRUserInterface();
#endif
#else
    (void)pDesc;
#endif
}

FORGE_API void exitUserInterface()
{
#ifdef ENABLE_FORGE_VR_UI
    exitVRUserInterface();
#endif

    arrfree(pUserInterface->pGamepadWidgetStack);
    arrfree(pUserInterface->ppWindowNameArrSWL);
    arrfree(pUserInterface->mContextMenu.pTabStack);
    arrfree(pUserInterface->pWindowsToHide);
    hmfree(pUserInterface->pWidgetMap);

    removeResource(pUserInterface->pVertexBuffer);
    removeResource(pUserInterface->pIndexBuffer);
    for (uint32_t s = 0; s < pUserInterface->mFrameMaxCount; ++s)
    {
        removeResource(pUserInterface->pUniformBuffer[s]);
    }
    removeResource(pUserInterface->pNullTexture);

    bdestroy(&pUserInterface->mClipboardString);
    nk_buffer_free(&pUserInterface->mCmdBuffer);
    nk_buffer_free(&pUserInterface->mLastBuffer);
    nk_free(&pUserInterface->mContext);

    pUserInterface->mInitialized = false;
}

#if defined(ENABLE_FORGE_TOUCH_INPUT)

extern void InputGetVirtualJoystickData(bool* outActive, bool* outPressed, float* outRadius, float* outDeadzone, float2* outStartPos,
                                        float2* outPos);
const char* gVirtualJoystickTextureName = "circlepad.tex";

bool loadVirtualJoystick(TinyImageFormat colorFormat)
{
    bool active = false;
    InputGetVirtualJoystickData(&active, NULL, NULL, NULL, NULL, NULL);
    if (!active)
    {
        return true;
    }

    TFTextureLoadDesc loadDesc = {};
    TFSyncToken       token = {};
    loadDesc.pFileName = gVirtualJoystickTextureName;
    loadDesc.ppTexture = &pUserInterface->pVJTexture;
    // Textures representing color should be stored in SRGB or HDR format
    loadDesc.mCreationFlag = TF_TEXTURE_CREATION_FLAG_SRGB;
    addResource(&loadDesc, &token);
    waitForToken(&token);

    TFBufferLoadDesc ubDesc = {};
    ubDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    ubDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
    ubDesc.mDesc.mFlags = TF_BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
    ubDesc.mDesc.mSize = sizeof(TexturedConstants);
    ubDesc.mDesc.pName = "VJ Uniform Buffer";
    for (uint32_t i = 0; i < MAX_FRAMES; ++i)
    {
        ubDesc.ppBuffer = &pUserInterface->pVJUniformBuffer[i];
        addResource(&ubDesc, NULL);
    }

    TFRenderer*      pRenderer = pUserInterface->pRenderer;
    /************************************************************************/
    // Shader
    /************************************************************************/
    TFShaderLoadDesc texturedShaderDesc = {};
    texturedShaderDesc.mVert.pFileName = "textured_mesh.vert";
    texturedShaderDesc.mFrag.pFileName = "textured_mesh.frag";
    addShader(pRenderer, &texturedShaderDesc, &pUserInterface->pVJShader);

    TFDescriptorSetDesc descriptorSetDesc = SRT_SET_DESC(SrtTexturedData, PerDraw, pUserInterface->mFrameMaxCount, 0);
    addDescriptorSet(pRenderer, &descriptorSetDesc, &pUserInterface->pVJDescriptorSet);

    TFVertexLayout vertexLayout = {};
    vertexLayout.mBindingCount = 1;
    vertexLayout.mAttribCount = 2;
    vertexLayout.mAttribs[0].mSemantic = TF_SEMANTIC_POSITION;
    vertexLayout.mAttribs[0].mFormat = TinyImageFormat_R32G32_SFLOAT;
    vertexLayout.mAttribs[0].mBinding = 0;
    vertexLayout.mAttribs[0].mLocation = 0;
    vertexLayout.mAttribs[0].mOffset = 0;

    vertexLayout.mAttribs[1].mSemantic = TF_SEMANTIC_TEXCOORD0;
    vertexLayout.mAttribs[1].mFormat = TinyImageFormat_R32G32_SFLOAT;
    vertexLayout.mAttribs[1].mBinding = 0;
    vertexLayout.mAttribs[1].mLocation = 1;
    vertexLayout.mAttribs[1].mOffset = TinyImageFormat_BitSizeOfBlock(TinyImageFormat_R32G32_SFLOAT) / 8;

    TFBlendStateDesc blendStateDesc = {};
    blendStateDesc.mSrcFactors[0] = TF_BC_SRC_ALPHA;
    blendStateDesc.mDstFactors[0] = TF_BC_ONE_MINUS_SRC_ALPHA;
    blendStateDesc.mSrcAlphaFactors[0] = TF_BC_SRC_ALPHA;
    blendStateDesc.mDstAlphaFactors[0] = TF_BC_ONE_MINUS_SRC_ALPHA;
    blendStateDesc.mColorWriteMasks[0] = TF_COLOR_MASK_ALL;
    blendStateDesc.mRenderTargetMask = TF_BLEND_STATE_TARGET_ALL;
    blendStateDesc.mIndependentBlend = false;

    TFDepthStateDesc depthStateDesc = {};
    depthStateDesc.mDepthTest = false;
    depthStateDesc.mDepthWrite = false;

    TFRasterizerStateDesc rasterizerStateDesc = {};
    rasterizerStateDesc.mCullMode = TF_CULL_MODE_NONE;
    rasterizerStateDesc.mScissor = true;

    TFPipelineDesc desc = {};
    PIPELINE_LAYOUT_DESC(desc, NULL, NULL, NULL, SRT_LAYOUT_DESC(SrtTexturedData, PerDraw));
    desc.pCache = pUserInterface->pPipelineCache;
    desc.mType = TF_PIPELINE_TYPE_GRAPHICS;
    TFGraphicsPipelineDesc& pipelineDesc = desc.mGraphicsDesc;
    pipelineDesc.mPrimitiveTopo = TF_PRIMITIVE_TOPO_TRI_STRIP;
    pipelineDesc.mDepthStencilFormat = TinyImageFormat_UNDEFINED;
    pipelineDesc.mRenderTargetCount = 1;
    pipelineDesc.mSampleCount = TF_SAMPLE_COUNT_1;
    pipelineDesc.mSampleQuality = 0;
    pipelineDesc.pBlendState = &blendStateDesc;
    pipelineDesc.pColorFormats = &colorFormat;
    pipelineDesc.pDepthState = &depthStateDesc;
    pipelineDesc.pRasterizerState = &rasterizerStateDesc;
    pipelineDesc.pShaderProgram = pUserInterface->pVJShader;
    pipelineDesc.pVertexLayout = &vertexLayout;
    addPipeline(pRenderer, &desc, &pUserInterface->pVJPipeline);

    TFDescriptorData params[2] = {};
    params[0].mIndex = SRT_RES_IDX(SrtTexturedData, PerDraw, gTexture);
    params[0].ppTextures = &pUserInterface->pVJTexture;

    for (uint32_t i = 0; i < pUserInterface->mFrameMaxCount; ++i)
    {
        params[1].mIndex = SRT_RES_IDX(SrtTexturedData, PerDraw, gUniformBlock);
        params[1].ppBuffers = &pUserInterface->pVJUniformBuffer[i];
        updateDescriptorSet(pUserInterface->pRenderer, i, pUserInterface->pVJDescriptorSet, 2, params);
    }

    return true;
}

void unloadVirtualJoystick()
{
    bool active = false;
    InputGetVirtualJoystickData(&active, NULL, NULL, NULL, NULL, NULL);
    if (!active)
    {
        return;
    }

    removeResource(pUserInterface->pVJTexture);

    TFRenderer* pRenderer = pUserInterface->pRenderer;
    removePipeline(pRenderer, pUserInterface->pVJPipeline);

    removeDescriptorSet(pRenderer, pUserInterface->pVJDescriptorSet);
    removeShader(pRenderer, pUserInterface->pVJShader);

    for (uint32_t i = 0; i < MAX_FRAMES; ++i)
    {
        removeResource(pUserInterface->pVJUniformBuffer[i]);
    }
}

void drawVirtualJoystick(TFCmd* pCmd, const float4* color, uint64_t vOffset)
{
    bool   active = false;
    bool   pressed = false;
    float  radius = 0.0f;
    float  deadzone = 0.0f;
    float2 startPos = {};
    float2 pos = {};
    InputGetVirtualJoystickData(&active, &pressed, &radius, &deadzone, &startPos, &pos);
    if (uiIsFocused() || !pressed)
    {
        return;
    }

    TexturedConstants data = {};

    float2 renderSize = { (float)pUserInterface->mWidth, (float)pUserInterface->mHeight };
    float2 renderScale = { (float)pUserInterface->mWidth / pUserInterface->mDisplayWidth,
                           (float)pUserInterface->mHeight / pUserInterface->mDisplayHeight };

    data.color = *color;
    data.scaleBias = { 2.0f / (float)renderSize[0], -2.0f / (float)renderSize[1] };

    uint32_t           frameIdx = *pUserInterface->pFrameIdx;
    TFBufferUpdateDesc update = { pUserInterface->pVJUniformBuffer[frameIdx] };
    beginUpdateResource(&update);
    memcpy(update.pMappedData, &data, sizeof(TexturedConstants));
    endUpdateResource(&update);

    cmdSetViewport(pCmd, 0.0f, 0.0f, renderSize[0], renderSize[1], 0.0f, 1.0f);
    cmdSetScissor(pCmd, 0u, 0u, (uint32_t)renderSize[0], (uint32_t)renderSize[1]);

    cmdBindPipeline(pCmd, pUserInterface->pVJPipeline);
    cmdBindDescriptorSet(pCmd, frameIdx, pUserInterface->pVJDescriptorSet);

    float extSide = radius;
    float intSide = radius * 0.5f;

    // Outer stick
    float2 joystickSize = float2(extSide) * renderScale;
    float2 joystickCenter = startPos * renderScale;
    float2 joystickPos = joystickCenter - joystickSize * 0.5f;

    const uint32_t     vertexStride = sizeof(float4);
    TFBufferUpdateDesc updateDesc = { pUserInterface->pVertexBuffer, vOffset };
    beginUpdateResource(&updateDesc);
    TFTexVertex vertices[4] = {};
    // the last variable can be used to create a border
    MAKETEXQUAD(vertices, joystickPos.x, joystickPos.y, joystickPos.x + joystickSize.x, joystickPos.y + joystickSize.y, 0);
    memcpy(updateDesc.pMappedData, vertices, sizeof(vertices));
    endUpdateResource(&updateDesc);
    cmdBindVertexBuffer(pCmd, 1, &pUserInterface->pVertexBuffer, &vertexStride, &vOffset);
    cmdDraw(pCmd, 4, 0);
    vOffset += sizeof(TFTexVertex) * 4;

    // Inner stick
    float2 stickPos = pos;
    float2 delta = pos - startPos;
    float  halfRad = (radius * 0.5f) - deadzone;
    if (length(delta) > halfRad)
    {
        stickPos = startPos + halfRad * normalize(delta);
    }
    joystickSize = float2(intSide) * renderScale;
    joystickCenter = stickPos * renderScale;
    joystickPos = joystickCenter - joystickSize * 0.5f;
    updateDesc = { pUserInterface->pVertexBuffer, vOffset };
    beginUpdateResource(&updateDesc);
    TFTexVertex verticesInner[4] = {};
    // the last variable can be used to create a border
    MAKETEXQUAD(verticesInner, joystickPos.x, joystickPos.y, joystickPos.x + joystickSize.x, joystickPos.y + joystickSize.y, 0);
    memcpy(updateDesc.pMappedData, verticesInner, sizeof(verticesInner));
    endUpdateResource(&updateDesc);
    cmdBindVertexBuffer(pCmd, 1, &pUserInterface->pVertexBuffer, &vertexStride, &vOffset);
    cmdDraw(pCmd, 4, 0);
}
#endif

#if defined(ENABLE_FORGE_VR_UI)

#define VR_MARKER_SIZE 10.0f

// This quaternion represents a 45 degree rotation around the X axis. Used to make the controller pointing direction a bit more natural,
// since the default rotation has the device flat on a surface
const Quat gControllerRotationCorrection(-0.38268f, 0.0f, 0.0f, 0.92388f);

#ifndef HOLOLENS2
const float4 gVRMarkerColor(0.85f, 0.85f, 0.85f, 0.50f);
#else
const float4 gVRMarkerColor(1.0f, 1.0f, 1.0f, 1.0f);
#endif

void initVRUserInterface()
{
#if defined(HOLOLENS2)
    inputAddCustomBindings(R"(
right_trigger; analog; HAND_R_PINCHING; 1.0f
right_tracking; analog; VRCTRL_RTR; 1.0f
right_pos_x; analog; VRCTRL_RPX; 1.0f
right_pos_y; analog; VRCTRL_RPY; 1.0f
right_pos_z; analog; VRCTRL_RPZ; 1.0f
right_dir_x; analog; VRCTRL_RDX; 1.0f
right_dir_y; analog; VRCTRL_RDY; 1.0f
right_dir_z; analog; VRCTRL_RDZ; 1.0f)");
#else
    inputAddCustomBindings(R"(
right_trigger; button; GPAD_R1; pressed;
right_tracking; analog; VRCTRL_RTR; 1.0f
right_pos_x; analog; VRCTRL_RPX; 1.0f
right_pos_y; analog; VRCTRL_RPY; 1.0f
right_pos_z; analog; VRCTRL_RPZ; 1.0f
right_dir_x; analog; VRCTRL_RDX; 1.0f
right_dir_y; analog; VRCTRL_RDY; 1.0f
right_dir_z; analog; VRCTRL_RDZ; 1.0f)");
#endif

    pUserInterface->mVRInput.mRightTrigger = inputGetCustomBindingEnum("right_trigger");
    pUserInterface->mVRInput.mRightControllerTracking = inputGetCustomBindingEnum("right_tracking");
    pUserInterface->mVRInput.mRightControllerPosX = inputGetCustomBindingEnum("right_pos_x");
    pUserInterface->mVRInput.mRightControllerPosY = inputGetCustomBindingEnum("right_pos_y");
    pUserInterface->mVRInput.mRightControllerPosZ = inputGetCustomBindingEnum("right_pos_z");
    pUserInterface->mVRInput.mRightControllerDirX = inputGetCustomBindingEnum("right_dir_x");
    pUserInterface->mVRInput.mRightControllerDirY = inputGetCustomBindingEnum("right_dir_y");
    pUserInterface->mVRInput.mRightControllerDirZ = inputGetCustomBindingEnum("right_dir_z");
}

void exitVRUserInterface()
{
    inputRemoveCustomBinding(pUserInterface->mVRInput.mRightTrigger);
    inputRemoveCustomBinding(pUserInterface->mVRInput.mRightControllerTracking);
    inputRemoveCustomBinding(pUserInterface->mVRInput.mRightControllerPosX);
    inputRemoveCustomBinding(pUserInterface->mVRInput.mRightControllerPosY);
    inputRemoveCustomBinding(pUserInterface->mVRInput.mRightControllerPosZ);
    inputRemoveCustomBinding(pUserInterface->mVRInput.mRightControllerDirX);
    inputRemoveCustomBinding(pUserInterface->mVRInput.mRightControllerDirY);
    inputRemoveCustomBinding(pUserInterface->mVRInput.mRightControllerDirZ);
}

void loadVRUserInterface(const TFUserInterfaceLoadDesc* pDesc)
{
    // Initialize logical 2D layer size, will be used to raycast VR controllers
    float2 layerRes = float2((float)pDesc->mWidth, (float)pDesc->mHeight);
    float  layerScale = pDesc->mVR2DLayer.mScale;
    pUserInterface->mVRLayer.mPosition = pDesc->mVR2DLayer.mPosition;
    pUserInterface->mVRLayer.mResolution = layerRes;
    pUserInterface->mVRLayer.mSize = float2(layerScale, layerScale * layerRes.y / layerRes.x);

    // Initialize VR marker rendering resources
    TFBufferLoadDesc ubDesc = {};
    ubDesc.mDesc.mDescriptors = TF_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    ubDesc.mDesc.mMemoryUsage = TF_RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
    ubDesc.mDesc.mFlags = TF_BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
    ubDesc.mDesc.mSize = sizeof(VRMarkerConstants);
    ubDesc.mDesc.pName = "VR Marker Constants";
    ubDesc.ppBuffer = &pUserInterface->mVRLayer.pVRMarkerBuffer;
    addResource(&ubDesc, NULL);

    VRMarkerConstants markerConstants;
    markerConstants.mColor = gVRMarkerColor;
    markerConstants.mIsClicked = 0;

    TFBufferUpdateDesc update = { pUserInterface->mVRLayer.pVRMarkerBuffer };
    beginUpdateResource(&update);
    memcpy(update.pMappedData, &markerConstants, sizeof(VRMarkerConstants));
    endUpdateResource(&update);

    TFRenderer*      pRenderer = pUserInterface->pRenderer;
    /************************************************************************/
    // Shader
    /************************************************************************/
    TFShaderLoadDesc vrMarkerShaderDesc = {};
    vrMarkerShaderDesc.mVert.pFileName = "vr_marker.vert";
    vrMarkerShaderDesc.mFrag.pFileName = "vr_marker.frag";
    addShader(pRenderer, &vrMarkerShaderDesc, &pUserInterface->mVRLayer.pVRMarkerShader);

    TFDescriptorSetDesc descriptorSetDesc = SRT_SET_DESC(SrtVRMarker, Persistent, 1, 0);
    addDescriptorSet(pRenderer, &descriptorSetDesc, &pUserInterface->mVRLayer.pVRMarkerDescriptorSet);

    TFBlendStateDesc blendStateDesc = {};
    blendStateDesc.mSrcFactors[0] = TF_BC_SRC_ALPHA;
    blendStateDesc.mDstFactors[0] = TF_BC_ONE_MINUS_SRC_ALPHA;
    blendStateDesc.mSrcAlphaFactors[0] = TF_BC_SRC_ALPHA;
    blendStateDesc.mDstAlphaFactors[0] = TF_BC_ONE_MINUS_SRC_ALPHA;
    blendStateDesc.mColorWriteMasks[0] = TF_COLOR_MASK_ALL;
    blendStateDesc.mRenderTargetMask = TF_BLEND_STATE_TARGET_ALL;
    blendStateDesc.mIndependentBlend = false;

    TFDepthStateDesc depthStateDesc = {};
    depthStateDesc.mDepthTest = false;
    depthStateDesc.mDepthWrite = false;

    TFRasterizerStateDesc rasterizerStateDesc = {};
    rasterizerStateDesc.mCullMode = TF_CULL_MODE_NONE;
    rasterizerStateDesc.mScissor = true;

    TFPipelineDesc desc = {};
    PIPELINE_LAYOUT_DESC(desc, SRT_LAYOUT_DESC(SrtVRMarker, Persistent), NULL, NULL, NULL);
    desc.pCache = pUserInterface->pPipelineCache;
    desc.mType = TF_PIPELINE_TYPE_GRAPHICS;
    TFGraphicsPipelineDesc& pipelineDesc = desc.mGraphicsDesc;
    pipelineDesc.mPrimitiveTopo = TF_PRIMITIVE_TOPO_TRI_STRIP;
    pipelineDesc.mDepthStencilFormat = TinyImageFormat_UNDEFINED;
    pipelineDesc.mRenderTargetCount = 1;
    pipelineDesc.mSampleCount = TF_SAMPLE_COUNT_1;
    pipelineDesc.mSampleQuality = 0;
    pipelineDesc.pBlendState = &blendStateDesc;
    TinyImageFormat colorFormat = (TinyImageFormat)pDesc->mColorFormat;
    pipelineDesc.pColorFormats = &colorFormat;
    pipelineDesc.pDepthState = &depthStateDesc;
    pipelineDesc.pRasterizerState = &rasterizerStateDesc;
    pipelineDesc.pShaderProgram = pUserInterface->mVRLayer.pVRMarkerShader;
    addPipeline(pRenderer, &desc, &pUserInterface->mVRLayer.pVRMarkerPipeline);

    TFDescriptorData params[1] = {};
    params[0].mIndex = SRT_RES_IDX(SrtVRMarker, Persistent, gVRMarkerConstants);
    params[0].ppBuffers = &pUserInterface->mVRLayer.pVRMarkerBuffer;
    updateDescriptorSet(pUserInterface->pRenderer, 0, pUserInterface->mVRLayer.pVRMarkerDescriptorSet, 1, params);
}

void unloadVRUserInterface()
{
    TFRenderer* pRenderer = pUserInterface->pRenderer;
    removePipeline(pRenderer, pUserInterface->mVRLayer.pVRMarkerPipeline);

    removeDescriptorSet(pRenderer, pUserInterface->mVRLayer.pVRMarkerDescriptorSet);
    removeShader(pRenderer, pUserInterface->mVRLayer.pVRMarkerShader);

    removeResource(pUserInterface->mVRLayer.pVRMarkerBuffer);
}

bool CheckController2DLayerIntersection(Vector2& result)
{
    Vector3 controllerPosition(inputGetValue(0, pUserInterface->mVRInput.mRightControllerPosX),
                               inputGetValue(0, pUserInterface->mVRInput.mRightControllerPosY),
                               inputGetValue(0, pUserInterface->mVRInput.mRightControllerPosZ));
    Vector3 controllerDirection(inputGetValue(0, pUserInterface->mVRInput.mRightControllerDirX),
                                inputGetValue(0, pUserInterface->mVRInput.mRightControllerDirY),
                                inputGetValue(0, pUserInterface->mVRInput.mRightControllerDirZ));

    controllerDirection = quatRotateVector(gControllerRotationCorrection, controllerDirection);

    // Raycast controller pos + dir against 2D layer quad
    {
        float3  _layerCenter = pUserInterface->mVRLayer.mPosition;
        Vector3 layerCenter(_layerCenter.x, _layerCenter.y, _layerCenter.z);
        float2  _quadSize = pUserInterface->mVRLayer.mSize;
        Vector2 quadSize(_quadSize.x, _quadSize.y);

        float halfWidth = quadSize.x / 2.0f;
        float halfHeight = quadSize.y / 2.0f;

        // Compute quad normal using world-space corners
        Vector3 topLeftCorner = layerCenter + Vector3(-halfWidth, halfHeight, 0.0f);
        Vector3 topRightCorner = layerCenter + Vector3(halfWidth, halfHeight, 0.0f);
        Vector3 bottomLeftCorner = layerCenter + Vector3(-halfWidth, -halfHeight, 0.0f);

        Vector3 tlToTr = topRightCorner - topLeftCorner;
        Vector3 tlToBl = bottomLeftCorner - topLeftCorner;

        Vector3 planeNormal = normalize(cross(tlToTr, tlToBl));

        // Compute hit on plane
        float denom = dot(planeNormal, controllerDirection);

        FORGE_CONSTEXPR float TOLERANCE = 1e-6f;
        if (abs(denom) < TOLERANCE)
        {
            return false;
        }

        // Get world-space ray hit position
        float hitAlongRayAxis = dot(planeNormal, layerCenter - controllerPosition);
        if (hitAlongRayAxis < 0.0f)
        {
            return false;
        }

        Vector3 intersectionWS = controllerPosition + hitAlongRayAxis * controllerDirection;

        // Convert hit-point to 2D layer local space, to figure out the collision coordinates
        Vector3 localHitPoint = intersectionWS - layerCenter;
        result.x = (dot(localHitPoint, normalize(tlToTr)));
        result.y = (dot(localHitPoint, normalize(tlToBl)));

#ifdef HOLOLENS2
        const float invHololensUIScale = 1.82f; // 1.0f / hololensUIScale
        result *= invHololensUIScale;
#endif
        return true;
    }
}

void updateVRUserMarker()
{
    bool isRightControllerTracked = inputGetValue(0, pUserInterface->mVRInput.mRightControllerTracking) != 0.0f;
    pUserInterface->mVRLayer.mCursorValid = false;
    if (isRightControllerTracked)
    {
        Vector2 cursorLocation;
        if (CheckController2DLayerIntersection(cursorLocation))
        {
            pUserInterface->mVRLayer.mCursorLocation = float2(cursorLocation.x * pUserInterface->mVRLayer.mResolution.x,
                                                              cursorLocation.y * pUserInterface->mVRLayer.mResolution.y);
            pUserInterface->mVRLayer.mCursorValid = true;
        }
    }
}

void drawVRUserMarker(TFCmd* pCmd)
{
    float       triggerValue = inputGetValue(0, pUserInterface->mVRInput.mRightTrigger);
    const float triggerThreshold = 0.75f;

    VRMarkerConstants markerConstants;
    markerConstants.mColor = gVRMarkerColor;
    markerConstants.mIsClicked = (triggerValue > triggerThreshold) ? 1u : 0u; // 1.0f when clicked, 0.0f when idle

    TFBufferUpdateDesc update = { pUserInterface->mVRLayer.pVRMarkerBuffer };
    beginUpdateResource(&update);
    memcpy(update.pMappedData, &markerConstants, sizeof(VRMarkerConstants));
    endUpdateResource(&update);

    // Draw a circle in the UI layer so that the user can track where there controller is pointing to.
    if (pUserInterface->mVRLayer.mCursorValid)
    {
        float2 cursorLocation = pUserInterface->mVRLayer.mCursorLocation;
        float2 viewportLeft = cursorLocation - float2(VR_MARKER_SIZE);

        cmdSetViewport(pCmd, viewportLeft.x, viewportLeft.y, VR_MARKER_SIZE, VR_MARKER_SIZE, 0.0f, 1.0f);
        cmdSetScissor(pCmd, (uint32_t)viewportLeft.x, (uint32_t)viewportLeft.y, (uint32_t)VR_MARKER_SIZE, (uint32_t)VR_MARKER_SIZE);
        cmdBindPipeline(pCmd, pUserInterface->mVRLayer.pVRMarkerPipeline);
        cmdBindDescriptorSet(pCmd, 0, pUserInterface->mVRLayer.pVRMarkerDescriptorSet);
        cmdDraw(pCmd, 3, 0);
    }
}

#endif

FORGE_API void loadUserInterface(const TFUserInterfaceLoadDesc* pDesc)
{
#ifdef ENABLE_FORGE_UI
    pUserInterface->mWidth = (float)pDesc->mWidth;
    pUserInterface->mHeight = (float)pDesc->mHeight;
    pUserInterface->mDisplayWidth = pDesc->mDisplayWidth == 0 ? pUserInterface->mWidth : (float)pDesc->mDisplayWidth;
    pUserInterface->mDisplayHeight = pDesc->mDisplayHeight == 0 ? pUserInterface->mHeight : (float)pDesc->mDisplayHeight;

    TFRenderTargetDesc rtDesc = {};
    rtDesc.mWidth = pDesc->mWidth;
    rtDesc.mHeight = pDesc->mHeight;
    rtDesc.mFormat = (TinyImageFormat)pDesc->mColorFormat;
    rtDesc.mArraySize = 1;
    rtDesc.mClearValue = TFClearValue{ { 0.0f, 0.0f, 0.0f, 0.0f } };
    rtDesc.mDepth = 1;
    rtDesc.mSampleCount = TF_SAMPLE_COUNT_1;
    rtDesc.mSampleQuality = 0;
    rtDesc.mDescriptors = TF_DESCRIPTOR_TYPE_TEXTURE;
    rtDesc.mFlags = TF_TEXTURE_CREATION_FLAG_NONE;
    rtDesc.mStartState = TF_RESOURCE_STATE_RENDER_TARGET;
    addRenderTarget(pUserInterface->pRenderer, &rtDesc, &pUserInterface->pDrawCacheRt);

    pUserInterface->mDrawCacheRtState = TF_RESOURCE_STATE_RENDER_TARGET;
    pUserInterface->mForceUpdate = true;

    const char* nuklearFrag[TF_SAMPLE_COUNT_COUNT] = {
        "nuklear_SAMPLE_COUNT_1.frag", "nuklear_SAMPLE_COUNT_2.frag",  "nuklear_SAMPLE_COUNT_4.frag",
        "nuklear_SAMPLE_COUNT_8.frag", "nuklear_SAMPLE_COUNT_16.frag",
    };
    TFShaderLoadDesc texturedShaderDesc = {};
    texturedShaderDesc.mVert = { "nuklear.vert" };
    for (uint32_t s = 0; s < TF_ARRAY_COUNT(nuklearFrag); ++s)
    {
        texturedShaderDesc.mFrag = { nuklearFrag[s] };
        addShader(pUserInterface->pRenderer, &texturedShaderDesc, &pUserInterface->pShaderTextured[s]);
    }

    // Contains uniform buffers, UI cache RT, and bindless texture array
    {
        // Bindless set indices: [0..frameCount-1] for main bindless array
        // MSAA fallback indices: [frameCount..frameCount + frameCount*MAX_MSAA_DRAWS_PER_FRAME - 1]
        uint32_t            totalSetIndices = pUserInterface->mFrameMaxCount * (1 + MAX_MSAA_DRAWS_PER_FRAME);
        TFDescriptorSetDesc setDesc = SRT_SET_DESC(NkSrtData, PerBatch, totalSetIndices, 0);
        addDescriptorSet(pUserInterface->pRenderer, &setDesc, &pUserInterface->pDescriptorSetPerBatch);

        // Initialize all set indices with null textures in every array slot
        TFTexture* nullTexArray[MAX_BINDLESS_UI_TEXTURES];
        for (uint32_t j = 0; j < MAX_BINDLESS_UI_TEXTURES; ++j)
            nullTexArray[j] = pUserInterface->pNullTexture;

        // Update first frameCount indices with uniform buffers, UI cache RT, and bindless textures
        for (uint32_t i = 0; i < pUserInterface->mFrameMaxCount; ++i)
        {
            TFDescriptorData params[3] = {};
            params[0].mIndex = SRT_RES_IDX(NkSrtData, PerBatch, gUITexture);
            params[0].ppTextures = &pUserInterface->pDrawCacheRt->pTexture;
            params[1].mIndex = SRT_RES_IDX(NkSrtData, PerBatch, gUniformBlock);
            params[1].ppBuffers = &pUserInterface->pUniformBuffer[i];
            params[2].mIndex = SRT_RES_IDX(NkSrtData, PerBatch, gTextures);
            params[2].mCount = MAX_BINDLESS_UI_TEXTURES;
            params[2].ppTextures = nullTexArray;
            updateDescriptorSet(pUserInterface->pRenderer, i, pUserInterface->pDescriptorSetPerBatch, 3, params);
        }

        // Initialize MSAA fallback indices
        for (uint32_t i = pUserInterface->mFrameMaxCount; i < totalSetIndices; ++i)
        {
            TFDescriptorData params[3] = {};
            params[0].mIndex = SRT_RES_IDX(NkSrtData, PerBatch, gUITexture);
            params[0].ppTextures = &pUserInterface->pDrawCacheRt->pTexture;
            params[1].mIndex = SRT_RES_IDX(NkSrtData, PerBatch, gUniformBlock);
            params[1].ppBuffers = &pUserInterface->pUniformBuffer[0];
            params[2].mIndex = SRT_RES_IDX(NkSrtData, PerBatch, gTextures);
            params[2].mCount = MAX_BINDLESS_UI_TEXTURES;
            params[2].ppTextures = nullTexArray;
            updateDescriptorSet(pUserInterface->pRenderer, i, pUserInterface->pDescriptorSetPerBatch, 3, params);
        }
    }

    // Load for cache draw
    {
        TFShaderLoadDesc swapchainDrawShaderDesc = {};
        swapchainDrawShaderDesc.mVert = { "swapchain_draw.vert" };
        swapchainDrawShaderDesc.mFrag = { "swapchain_draw.frag" };
        addShader(pUserInterface->pRenderer, &swapchainDrawShaderDesc, &pUserInterface->pDrawCacheShader);
    }

    TFBlendStateDesc blendStateDesc = {};
    blendStateDesc.mSrcFactors[0] = TF_BC_SRC_ALPHA;
    blendStateDesc.mDstFactors[0] = TF_BC_ONE_MINUS_SRC_ALPHA;
    blendStateDesc.mSrcAlphaFactors[0] = TF_BC_SRC_ALPHA;
    blendStateDesc.mDstAlphaFactors[0] = TF_BC_DST_ALPHA;
    blendStateDesc.mColorWriteMasks[0] = TF_COLOR_MASK_ALL;
    blendStateDesc.mRenderTargetMask = TF_BLEND_STATE_TARGET_ALL;
    blendStateDesc.mIndependentBlend = false;

    TFDepthStateDesc depthStateDesc = {};
    depthStateDesc.mDepthTest = false;
    depthStateDesc.mDepthWrite = false;

    TFRasterizerStateDesc rasterizerStateDesc = {};
    rasterizerStateDesc.mCullMode = TF_CULL_MODE_NONE;
    rasterizerStateDesc.mScissor = true;

    TFPipelineDesc desc = {};
    PIPELINE_LAYOUT_DESC(desc, NULL, NULL, SRT_LAYOUT_DESC(NkSrtData, PerBatch), NULL);
    desc.pCache = pDesc->pCache ? pDesc->pCache : pUserInterface->pPipelineCache;
    desc.mType = TF_PIPELINE_TYPE_GRAPHICS;
    TFGraphicsPipelineDesc& pipelineDesc = desc.mGraphicsDesc;
    pipelineDesc.mDepthStencilFormat = TinyImageFormat_UNDEFINED;
    pipelineDesc.mRenderTargetCount = 1;
    pipelineDesc.mSampleCount = TF_SAMPLE_COUNT_1;
    pipelineDesc.pBlendState = &blendStateDesc;
    pipelineDesc.mSampleQuality = 0;
    pipelineDesc.pColorFormats = (TinyImageFormat*)&pDesc->mColorFormat;
    pipelineDesc.pDepthState = &depthStateDesc;
    pipelineDesc.pRasterizerState = &rasterizerStateDesc;
    pipelineDesc.pVertexLayout = &pUserInterface->mVertexLayoutTextured;
    pipelineDesc.mPrimitiveTopo = TF_PRIMITIVE_TOPO_TRI_LIST;
    pipelineDesc.mVRFoveatedRendering = false;
    for (uint32_t s = 0; s < TF_ARRAY_COUNT(pUserInterface->pShaderTextured); ++s)
    {
        pipelineDesc.pShaderProgram = pUserInterface->pShaderTextured[s];
        addPipeline(pUserInterface->pRenderer, &desc, &pUserInterface->pPipelineTextured[s]);
    }

    PIPELINE_LAYOUT_DESC(desc, NULL, NULL, SRT_LAYOUT_DESC(NkSrtData, PerBatch), NULL);
    pipelineDesc.pVertexLayout = NULL;
    pipelineDesc.pShaderProgram = pUserInterface->pDrawCacheShader;
    addPipeline(pUserInterface->pRenderer, &desc, &pUserInterface->pDrawCachePipeline);

    // gUITexture already initialized in merged descriptor set above

#ifdef ENABLE_FORGE_VR_UI
    loadVRUserInterface(pDesc);
#endif
#if defined(ENABLE_FORGE_TOUCH_INPUT)
    loadVirtualJoystick((TinyImageFormat)pDesc->mColorFormat);
    nk_widget_disable_edit(getContext(), true);
#endif

#else
    (void)pDesc;
#endif
}

FORGE_API void unloadUserInterface()
{
#ifdef ENABLE_FORGE_UI

#if defined(ENABLE_FORGE_TOUCH_INPUT)
    unloadVirtualJoystick();
#endif

#if defined(ENABLE_FORGE_VR_UI)
    unloadVRUserInterface();
#endif

    uiCloseContextMenu();
    uiRemoveContextMenu();

    memset(&pUserInterface->mMessageBoxInfo, 0, sizeof(pUserInterface->mMessageBoxInfo));

    for (uint32_t s = 0; s < TF_ARRAY_COUNT(pUserInterface->pShaderTextured); ++s)
    {
        removePipeline(pUserInterface->pRenderer, pUserInterface->pPipelineTextured[s]);
    }
    removePipeline(pUserInterface->pRenderer, pUserInterface->pDrawCachePipeline);

    removeRenderTarget(pUserInterface->pRenderer, pUserInterface->pDrawCacheRt);

    for (uint32_t s = 0; s < TF_ARRAY_COUNT(pUserInterface->pShaderTextured); ++s)
    {
        removeShader(pUserInterface->pRenderer, pUserInterface->pShaderTextured[s]);
    }
    removeDescriptorSet(pUserInterface->pRenderer, pUserInterface->pDescriptorSetPerBatch);

    removeShader(pUserInterface->pRenderer, pUserInterface->pDrawCacheShader);
#else
    (void)unloadType;
#endif
}

void clearUserInterface()
{
#ifdef ENABLE_FORGE_UI
    if (pUserInterface->mHaveWindow)
    {
        nk_clear(getContext());
        nk_buffer_clear(&pUserInterface->mCmdBuffer);
        pUserInterface->mHaveWindow = false;
        pUserInterface->mCurrEditorButtonID = 0;
    }
#endif
}

static bool uiPreDraw()
{
#ifdef ENABLE_FORGE_UI

    // if draw command buffer contents are not identical, we want to redraw UI cache
#if CACHE_WIDGETS
    if (pUserInterface->mCommandsChanged)
#endif
    {
        nk_context* ctx = getContext();
        void*       cmds = nk_buffer_memory(&getContext()->memory);
        size_t      cmdsAllocated = ctx->memory.allocated;
        void*       last = nk_buffer_memory(&pUserInterface->mLastBuffer);
#if CACHE_WIDGETS
        ASSERTMSG(memcmp(cmds, last, cmdsAllocated), "The command buffers are identical, but UI was marked for redraw");

        arrsetlen(pUserInterface->pWindowsToDraw, 0);

        // copy current commands to last buffer
        nk_buffer_push(&pUserInterface->mLastBuffer, NK_BUFFER_FRONT, cmds, cmdsAllocated, 0);

        // gather windows that need to be redrawn
        for (nk_window* iter = ctx->begin; iter != NULL; iter = iter->next)
        {
            UIWindowCacheEntry* cache = &hmget(pUserInterface->pWidgetMap, iter);

            // TODO: Nuklear windows store the same pointer to commands as the context.
            // We can add our own window struct later that would keep track of the offsets into this buffer
            // when we add support for converting commands for individual UI windows
            if (cache->mRedraw)
            {
                cache->pLastCmdBuffer = pUserInterface->mLastBuffer.memory.ptr;
                cache->mLastCmdBufferAllocated = cmdsAllocated;
                cache->mRedraw = false;
                // arrpush(pUserInterface->pWindowsToDraw, iter);
            }
        }
        pUserInterface->mCommandsChanged = false;
#else
        if (cmdsAllocated == pUserInterface->mLastBuffer.allocated && memcmp(cmds, last, cmdsAllocated) == 0 &&
            !pUserInterface->mForceUpdate)
        {
            // nk__begin here to workaround hangs where nk_convert may never return
            nk__begin(getContext());
            clearUserInterface();
            return false;
        }

        pUserInterface->mForceUpdate = false;
        // copy current commands to last buffer
        nk_buffer_clear(&pUserInterface->mLastBuffer);
        if (cmdsAllocated != 0)
        {
            nk_buffer_push(&pUserInterface->mLastBuffer, NK_BUFFER_FRONT, cmds, cmdsAllocated, 0);
        }
#endif
        return true;
    }

#if CACHE_WIDGETS
    return false;
#endif
#else
    return false;
#endif
}

static void uiPostDraw(TFCmd* pCmd, ProfileToken profileToken, TFRenderTarget* pRt)
{
#ifdef ENABLE_FORGE_UI
    // draw UI to swapchain
    {
        bool updateRtState = pUserInterface->mDrawCacheRtState != TF_RESOURCE_STATE_SHADER_RESOURCE;

        if (profileToken != (ProfileToken)-1)
        {
            cmdBeginGpuTimestampQuery(pCmd, profileToken, "Copy UI Cache to Swapchain");
        }

        if (updateRtState)
        {
            TFRenderTargetBarrier uiRtBarrier = {};
            uiRtBarrier.pRenderTarget = pUserInterface->pDrawCacheRt;
            uiRtBarrier.mCurrentState = pUserInterface->mDrawCacheRtState;
            uiRtBarrier.mNewState = TF_RESOURCE_STATE_SHADER_RESOURCE;
            cmdResourceBarrier(pCmd, 0, NULL, 0, NULL, 1, &uiRtBarrier);
            pUserInterface->mDrawCacheRtState = TF_RESOURCE_STATE_SHADER_RESOURCE;
        }

        TFBindRenderTargetsDesc desc = {};
        desc.mRenderTargetCount = 1;
        desc.mRenderTargets[0] = { pRt, TF_LOAD_ACTION_LOAD };
        cmdBindRenderTargets(pCmd, &desc);
        cmdSetViewport(pCmd, 0.0f, 0.0f, (float)pRt->mWidth, (float)pRt->mHeight, 0.0f, 1.0f);
        cmdSetScissor(pCmd, 0, 0, pRt->mWidth, pRt->mHeight);
        cmdBindPipeline(pCmd, pUserInterface->pDrawCachePipeline);
        cmdBindDescriptorSet(pCmd, *pUserInterface->pFrameIdx, pUserInterface->pDescriptorSetPerBatch);
        cmdDraw(pCmd, 3, 0);

        if (profileToken != (ProfileToken)-1)
        {
            cmdEndGpuTimestampQuery(pCmd, profileToken);
        }
    }

#endif
}

#if defined(ENABLE_FORGE_UI)

// Resolve a draw command's texture to its bindless array index.
// For MSAA textures, returns UINT32_MAX to signal the MSAA fallback path.
static uint32_t resolveBindlessTextureIndex(const nk_draw_command* pNkDrawCmd, TFTexture** pBindlessTextures, uint32_t* pBindlessCount)
{
    int fontCount = 1;
    if (pNkDrawCmd->texture.id >= 0 && pNkDrawCmd->texture.id < fontCount)
        return BINDLESS_FONT_INDEX;

    if (pNkDrawCmd->texture.ptr == pUserInterface->mConvertCfg.tex_null.texture.ptr)
        return BINDLESS_NULL_INDEX;

    TFTexture* tex = (TFTexture*)pNkDrawCmd->texture.ptr;
    if (tex->mSampleCount > 1)
        return UINT32_MAX; // MSAA fallback

    // Look up in existing array
    for (uint32_t i = BINDLESS_CUSTOM_START; i < *pBindlessCount; ++i)
    {
        if (pBindlessTextures[i] == tex)
            return i;
    }

    // Register new texture
    if (*pBindlessCount < MAX_BINDLESS_UI_TEXTURES)
    {
        uint32_t idx = (*pBindlessCount)++;
        pBindlessTextures[idx] = tex;
        return idx;
    }

    LOGF(eWARNING, "Too many bindless UI textures (max %u). Falling back to null texture.", MAX_BINDLESS_UI_TEXTURES);
    return BINDLESS_NULL_INDEX;
}

static void cmdDrawUICommand(TFCmd* pCmd, const nk_draw_command* pNkDrawCmd, const vec2* displaySize, uint32_t* globalIdxOffsetInOut,
                             TFPipeline** ppPipeline, TFPipeline** ppPrevPipeline, bool* pBindlessSetBound)
{
    if (!pNkDrawCmd->elem_count)
    {
        return;
    }

    //  Clamp to viewport as cmdSetScissor() won't accept values that are off bounds
    vec2 clipMin = { clamp(pNkDrawCmd->clip_rect.x, 0.0f, (*displaySize)[0]), clamp(pNkDrawCmd->clip_rect.y, 0.0f, (*displaySize)[1]) };
    vec2 clipMax = { clamp(pNkDrawCmd->clip_rect.x + pNkDrawCmd->clip_rect.w, 0.0f, (*displaySize)[0]),
                     clamp(pNkDrawCmd->clip_rect.y + pNkDrawCmd->clip_rect.h, 0.0f, (*displaySize)[1]) };
    if (clipMax[0] <= clipMin[0] || clipMax[1] <= clipMin[1])
    {
        *globalIdxOffsetInOut += pNkDrawCmd->elem_count;
        return;
    }

    uint2 offset = { (uint32_t)clipMin[0], (uint32_t)clipMin[1] };
    uint2 ext = { (uint32_t)(clipMax[0] - clipMin[0]), (uint32_t)(clipMax[1] - clipMin[1]) };
    cmdSetScissor(pCmd, offset.x, offset.y, ext.x, ext.y);

    bool isMSAA = false;
    int  fontCount = 1;
    if ((pNkDrawCmd->texture.id >= 0 && pNkDrawCmd->texture.id < fontCount) ||
        (pNkDrawCmd->texture.ptr == pUserInterface->mConvertCfg.tex_null.texture.ptr))
    {
        *ppPipeline = pUserInterface->pPipelineTextured[0];
    }
    else
    {
        TFTexture* tex = (TFTexture*)pNkDrawCmd->texture.ptr;
        uint32_t   pipelineIndex = (uint32_t)log2((float)tex->mSampleCount);
        *ppPipeline = pUserInterface->pPipelineTextured[pipelineIndex];
        isMSAA = (tex->mSampleCount > 1);
    }

    if (*ppPrevPipeline != *ppPipeline)
    {
        cmdBindPipeline(pCmd, *ppPipeline);
        *ppPrevPipeline = *ppPipeline;
    }

    uint32_t frameIdx = *pUserInterface->pFrameIdx;
    if (isMSAA)
    {
        // MSAA fallback: bind the pre-updated dedicated set (descriptor was updated in pre-scan pass)
        if (pUserInterface->mMSAADrawCount >= MAX_MSAA_DRAWS_PER_FRAME)
        {
            LOGF(eWARNING, "Too many MSAA UI texture draws per frame (max %u).", MAX_MSAA_DRAWS_PER_FRAME);
        }
        else
        {
            uint32_t msaaSetIdx = pUserInterface->mFrameMaxCount + frameIdx * MAX_MSAA_DRAWS_PER_FRAME + pUserInterface->mMSAADrawCount;

            cmdBindDescriptorSet(pCmd, msaaSetIdx, pUserInterface->pDescriptorSetPerBatch);
            pUserInterface->mMSAADrawCount++;
            *pBindlessSetBound = false;
        }
    }
    else if (!*pBindlessSetBound)
    {
        // Rebind the bindless set (after an MSAA draw displaced it)
        cmdBindDescriptorSet(pCmd, frameIdx, pUserInterface->pDescriptorSetPerBatch);
        *pBindlessSetBound = true;
    }

    cmdDrawIndexed(pCmd, pNkDrawCmd->elem_count, *globalIdxOffsetInOut, 0);
    *globalIdxOffsetInOut += pNkDrawCmd->elem_count;
}

#endif // ENABLE_FORGE_UI

static void cmdDrawUserInterface(TFCmd* pCmd, TFRenderTarget* pRt, ProfileToken profileToken)
{
    // Early return if UI rendering has been disabled
    if (!pUserInterface->mEnableRendering)
    {
        return;
    }

    cmdBindRenderTargets(pCmd, NULL);

    if (uiPreDraw())
    {
        struct nk_context* ctx = getContext();

        // ASSERT(arrlenu(pUserInterface->pWindowsToDraw));

        // get vertex output from Nuklear
        nk_buffer vertexBuffer;
        nk_buffer indexBuffer;
        uint32_t  frameIdx = *pUserInterface->pFrameIdx;

        // wrap current frame's persistently mapped buffer ranges
        uint64_t           vOffset = frameIdx * VERTEX_BUFFER_SIZE;
        uint64_t           iOffset = frameIdx * INDEX_BUFFER_SIZE;
        TFBufferUpdateDesc vtxUpdate = { pUserInterface->pVertexBuffer, vOffset };
        TFBufferUpdateDesc idxUpdate = { pUserInterface->pIndexBuffer, iOffset };
        beginUpdateResource(&vtxUpdate);
        beginUpdateResource(&idxUpdate);
        nk_buffer_init_fixed(&vertexBuffer, vtxUpdate.pMappedData, VERTEX_BUFFER_SIZE);
        nk_buffer_init_fixed(&indexBuffer, idxUpdate.pMappedData, INDEX_BUFFER_SIZE);

        // output vertex data and indices to the mapped buffers based on the draw commands gathered this frame
        // NOTE: this converts all of the draw commands to vertex output, even windows that have not been changed.
        // Nuklear does not support converting commands for specific windows, which is something we will probably have to
        // extend ourselves in the future if the need for even more performant render policy arises. The issue with that would
        // be transparent windows though.

        nk_flags ret = nk_convert(ctx, &pUserInterface->mCmdBuffer, &vertexBuffer, &indexBuffer, &pUserInterface->mConvertCfg);

        switch (ret)
        {
        case NK_CONVERT_INVALID_PARAM:
            ASSERTFAIL("An invalid argument was passed in the nk_convert function call");
            break;
        case NK_CONVERT_COMMAND_BUFFER_FULL:
            ASSERTFAIL("The provided buffer for storing draw commands is full or failed to allocate more memory");
            break;
        case NK_CONVERT_VERTEX_BUFFER_FULL:
            ASSERTFAIL("The provided buffer for storing vertices is full or failed to allocate more memory");
            break;
        case NK_CONVERT_ELEMENT_BUFFER_FULL:
            ASSERTFAIL("The provided buffer for storing indices is full or failed to allocate more memory");
            break;
        default:
            break;
        }

        // Collect unique textures from all draw commands into the bindless array,
        // then stamp the texture index into each vertex via the index buffer.
        TFTexture* bindlessTextures[MAX_BINDLESS_UI_TEXTURES];
        bindlessTextures[BINDLESS_NULL_INDEX] = pUserInterface->pNullTexture;

        TFTexture* fontAtlasTex = fntGetAtlas(pUserInterface->pFont);
        bindlessTextures[BINDLESS_FONT_INDEX] = fontAtlasTex ? fontAtlasTex : pUserInterface->pNullTexture;
        uint32_t bindlessCount = BINDLESS_CUSTOM_START;

        {
            UIVertex* vertices = (UIVertex*)vtxUpdate.pMappedData;
            uint32_t  stampVertOffset = 0;
            float     ratioFont = fntGetRatio(pUserInterface->pFont, pUserInterface->mFontHeight * pUserInterface->mFontDpiScale);

            const nk_draw_command* stampCmd;
            nk_draw_foreach(stampCmd, ctx, &pUserInterface->mCmdBuffer)
            {
                if (!stampCmd->elem_count)
                    continue;

                uint32_t texIdx = resolveBindlessTextureIndex(stampCmd, bindlessTextures, &bindlessCount);
                // For MSAA textures (UINT32_MAX), stamp 0 — the MSAA path handles descriptor binding
                if (texIdx == UINT32_MAX)
                    texIdx = 0;

                // Encode font flag so the shader can branch without a pipeline switch
                int fontCount = 1;
                if (stampCmd->texture.id >= 0 && stampCmd->texture.id < fontCount)
                    texIdx |= TEXTURE_INDEX_FONT_BIT;

                uint32_t vertIdx;
                for (uint32_t i = 0; i < stampCmd->vert_count; ++i)
                {
                    vertIdx = i + stampVertOffset;
                    vertices[vertIdx].textureIndex = texIdx;
                    vertices[vertIdx].fontScale = ratioFont;
                }

                stampVertOffset += stampCmd->vert_count;
            }
        }

        endUpdateResource(&idxUpdate);
        endUpdateResource(&vtxUpdate);
        TFDescriptorData params[3] = {};
        params[0].mIndex = SRT_RES_IDX(NkSrtData, PerBatch, gUITexture);
        params[0].ppTextures = &pUserInterface->pDrawCacheRt->pTexture;
        params[1].mIndex = SRT_RES_IDX(NkSrtData, PerBatch, gUniformBlock);
        params[1].ppBuffers = &pUserInterface->pUniformBuffer[frameIdx];
        params[2].mIndex = SRT_RES_IDX(NkSrtData, PerBatch, gTextures);
        params[2].mArrayOffset = 0;
        params[2].mCount = bindlessCount;
        params[2].ppTextures = bindlessTextures;
        updateDescriptorSet(pUserInterface->pRenderer, frameIdx, pUserInterface->pDescriptorSetPerBatch, 3, params);

        // Pre-scan and batch-update all MSAA texture descriptor sets before drawing
        pUserInterface->mMSAADrawCount = 0;
        {
            const nk_draw_command* scanCmd;
            nk_draw_foreach(scanCmd, ctx, &pUserInterface->mCmdBuffer)
            {
                if (!scanCmd->elem_count)
                    continue;

                // Check if this is an MSAA texture
                int  fontCount = 1;
                bool isMSAA = false;
                if (scanCmd->texture.id < 0 || scanCmd->texture.id >= fontCount)
                {
                    if (scanCmd->texture.ptr != pUserInterface->mConvertCfg.tex_null.texture.ptr)
                    {
                        TFTexture* tex = (TFTexture*)scanCmd->texture.ptr;
                        isMSAA = (tex->mSampleCount > 1);
                    }
                }

                if (isMSAA && pUserInterface->mMSAADrawCount < MAX_MSAA_DRAWS_PER_FRAME)
                {
                    uint32_t msaaSetIdx =
                        pUserInterface->mFrameMaxCount + frameIdx * MAX_MSAA_DRAWS_PER_FRAME + pUserInterface->mMSAADrawCount;

                    TFDescriptorData msaaParams[3] = {};
                    msaaParams[0].mIndex = SRT_RES_IDX(NkSrtData, PerBatch, gUITexture);
                    msaaParams[0].ppTextures = &pUserInterface->pDrawCacheRt->pTexture;
                    msaaParams[1].mIndex = SRT_RES_IDX(NkSrtData, PerBatch, gUniformBlock);
                    msaaParams[1].ppBuffers = &pUserInterface->pUniformBuffer[frameIdx];
                    msaaParams[2].mIndex = SRT_RES_IDX(NkSrtData, PerBatch, gTextures);
                    msaaParams[2].mArrayOffset = 0;
                    msaaParams[2].mCount = 1;
                    msaaParams[2].ppTextures = (TFTexture**)&scanCmd->texture.ptr;
                    updateDescriptorSet(pUserInterface->pRenderer, msaaSetIdx, pUserInterface->pDescriptorSetPerBatch, 3, msaaParams);
                    pUserInterface->mMSAADrawCount++;
                }
            }
        }

        // Reset counter for actual drawing
        pUserInterface->mMSAADrawCount = 0;

        // redraw UI
        vec2 displaySize{ pUserInterface->mWidth, pUserInterface->mHeight };

        TFPipeline* pPipeline = pUserInterface->pPipelineTextured[0];
        TFPipeline* pPreviousPipeline = NULL;

        if (profileToken != (ProfileToken)-1)
        {
            cmdBeginGpuTimestampQuery(pCmd, profileToken, "Draw UI Cache");
        }

        if (pUserInterface->mDrawCacheRtState != TF_RESOURCE_STATE_RENDER_TARGET)
        {
            TFRenderTargetBarrier uiRtBarrier = {};
            uiRtBarrier.pRenderTarget = pUserInterface->pDrawCacheRt;
            uiRtBarrier.mCurrentState = pUserInterface->mDrawCacheRtState;
            uiRtBarrier.mNewState = TF_RESOURCE_STATE_RENDER_TARGET;
            cmdResourceBarrier(pCmd, 0, NULL, 0, NULL, 1, &uiRtBarrier);
            pUserInterface->mDrawCacheRtState = TF_RESOURCE_STATE_RENDER_TARGET;
        }

        TFBindRenderTargetsDesc desc = {};
        desc.mRenderTargetCount = 1;
        desc.mRenderTargets[0] = { pUserInterface->pDrawCacheRt, TF_LOAD_ACTION_CLEAR };
        desc.mRenderTargets[0].mClearValue = pUserInterface->pDrawCacheRt->mClearValue;
        cmdBindRenderTargets(pCmd, &desc);

        UniformBlock block{};
        block.ProjectionMatrix.v[0] = f4Make(2.0f, 0.0f, 0.0f, 0.0f);
        block.ProjectionMatrix.v[1] = f4Make(0.0f, -2.0f, 0.0f, 0.0f);
        block.ProjectionMatrix.v[2] = f4Make(0.0f, 0.0f, -1.0f, 0.0f);
        block.ProjectionMatrix.v[3] = f4Make(-1.0f, 1.0f, 0.0f, 1.0f);
        block.ProjectionMatrix.v[0].x /= displaySize[0];
        block.ProjectionMatrix.v[1].y /= displaySize[1];
        block.GammaCoef = UI_COLOR_GAMMA_CORRECTION;

        TFBufferUpdateDesc update = { pUserInterface->pUniformBuffer[frameIdx] };
        beginUpdateResource(&update);
        memcpy(update.pMappedData, &block, sizeof(block));
        endUpdateResource(&update);

        const uint32_t vertexStride = sizeof(UIVertex);

        cmdSetViewport(pCmd, 0.0f, 0.0f, displaySize[0], displaySize[1], 0.0f, 1.0f);

        cmdBindPipeline(pCmd, pPipeline);

        cmdBindIndexBuffer(pCmd, pUserInterface->pIndexBuffer,
                           sizeof(DRAW_INDEX_TYPE) == sizeof(uint16_t) ? TF_INDEX_TYPE_UINT16 : TF_INDEX_TYPE_UINT32, iOffset);
        cmdBindVertexBuffer(pCmd, 1, &pUserInterface->pVertexBuffer, &vertexStride, &vOffset);

        pPreviousPipeline = pPipeline;

        // Bind the bindless PerBatch set once
        cmdBindDescriptorSet(pCmd, frameIdx, pUserInterface->pDescriptorSetPerBatch);
        bool bindlessSetBound = true;

        const nk_draw_command* cmd;
        uint32_t               globalIdxOffset = 0;
        nk_draw_foreach(cmd, ctx, &pUserInterface->mCmdBuffer)
        {
            cmdDrawUICommand(pCmd, cmd, &displaySize, &globalIdxOffset, &pPipeline, &pPreviousPipeline, &bindlessSetBound);
        }

        cmdBindRenderTargets(pCmd, NULL);

        if (profileToken != (ProfileToken)-1)
        {
            cmdEndGpuTimestampQuery(pCmd, profileToken);
        }
    }

    // draws the updated/cached UI texture to the original swapchain and clears Nuklear context
    uiPostDraw(pCmd, profileToken, pRt);

    if (pUserInterface->pGamepadActiveWindow)
    {
        pUserInterface->pGamepadActiveWindow->meta_data[0] = pUserInterface->mGamepadSelectedIndex;
    }

#if defined(ENABLE_FORGE_TOUCH_INPUT)
    // TODO:
    // float4 color{ 1.0f, 1.0f, 1.0f, 1.0f };
    // drawVirtualJoystick(pCmd, &color, vtxDst);
#endif

#if defined(ENABLE_FORGE_VR_UI)
    drawVRUserMarker(pCmd);
#endif
}

void uiCmdDrawUserInterface(TFCmd* pCmd, TFSwapChain* pSwapchain, TFRenderTarget* pRenderTarget, uint64_t profileToken)
{
    UNREF_PARAM(pCmd);
    UNREF_PARAM(pSwapchain);
    UNREF_PARAM(pRenderTarget);
    UNREF_PARAM(profileToken);

#ifdef ENABLE_FORGE_UI
#ifdef ENABLE_FORGE_VR_UI
    cmdBindRenderTargets(pCmd, NULL);

    TFSwapChain* p2DLayerSwapChain = pSwapchain->mVR.m2DLayer.pSwapchain;
    ASSERT(p2DLayerSwapChain);
    TFRenderTarget*       p2DLayerRT = p2DLayerSwapChain->ppRenderTargets[pSwapchain->mVR.m2DLayer.mCurrentSwapChainIndex];
    TFRenderTargetBarrier barrier = { p2DLayerRT, TF_RESOURCE_STATE_PRESENT, TF_RESOURCE_STATE_RENDER_TARGET };
    cmdResourceBarrier(pCmd, 0, NULL, 0, NULL, 1, &barrier);

    TFBindRenderTargetsDesc bindRenderTargets = {};
    bindRenderTargets.mRenderTargetCount = 1;
    bindRenderTargets.mRenderTargets[0] = { p2DLayerRT, TF_LOAD_ACTION_CLEAR };
    cmdBindRenderTargets(pCmd, &bindRenderTargets);
    cmdSetViewport(pCmd, 0.0f, 0.0f, (float)p2DLayerRT->mWidth, (float)p2DLayerRT->mHeight, 0.0f, 1.0f);
    cmdSetScissor(pCmd, 0, 0, p2DLayerRT->mWidth, p2DLayerRT->mHeight);

    cmdDrawUserInterface(pCmd, p2DLayerRT, profileToken);

    // on Quest we need to trigger end of renderpass before using the barrier
    cmdBindRenderTargets(pCmd, NULL);
    barrier = { p2DLayerRT, TF_RESOURCE_STATE_RENDER_TARGET, TF_RESOURCE_STATE_PRESENT };
    cmdResourceBarrier(pCmd, 0, NULL, 0, NULL, 1, &barrier);
#else
    cmdDrawUserInterface(pCmd, pRenderTarget, profileToken);
#endif
#endif
}

/****************************************************************************/
// MARK: - Other User Interface Functionality
/****************************************************************************/

FORGE_API bool uiIsRenderingEnabled()
{
#ifdef ENABLE_FORGE_UI
    return pUserInterface->mEnableRendering;
#else
    return false;
#endif
}

FORGE_API void uiToggleRendering(bool enabled)
{
#ifdef ENABLE_FORGE_UI
    pUserInterface->mEnableRendering = enabled;
#else
    (void)enabled;
#endif
}

FORGE_API bool uiIsFocused()
{
#ifdef ENABLE_FORGE_UI
    if (pUserInterface->mGamepadActive)
    {
        return pUserInterface->mWindowSelected != NULL &&
               nk_window_is_collapsed(getContext(), pUserInterface->mWindowSelected->name_string) == false;
    }
    else
    {
        bool test = nk_item_is_any_active(getContext());
        return test;
    }

#else
    return false;
#endif
}

FORGE_API bool uiIsInitialized()
{
#ifdef ENABLE_FORGE_UI
    return pUserInterface->mInitialized;
#endif
}

FORGE_API void uiForceReferesh()
{
#ifdef ENABLE_FORGE_UI
    pUserInterface->mForceUpdate = true;
#endif
}

/****************************************************************************/
// MARK: - Private Platform Layer Life Cycle Functions
/****************************************************************************/

bool platformInitUserInterface()
{
#ifdef ENABLE_FORGE_UI
    TFUserInterface* pAppUI = tf_new(TFUserInterface);

    pAppUI->mShowDemoUiWindow = false;

    const uint32_t monitorIdx = getActiveMonitorIdx();
    getMonitorDpiScale(monitorIdx, pAppUI->mDpiScale);

    pAppUI->pWidgetMap = NULL;
    pAppUI->pWindowsToDraw = NULL;

    pUserInterface = pAppUI;

    pUserInterface->mContextMenuGradientItems[0].pName = "Remove key";
    pUserInterface->mContextMenuGradientItems[0].mType = TF_UI_CONTEXT_MENU_ITEM;
    pUserInterface->mContextMenuGradientItems[0].mItemData.pOnSelect = removeGradientKeyCallBack;
    pUserInterface->mContextMenuGradientItems[0].mItemData.pOnSelectUserData = &pUserInterface->mContextMenuDeleteGradientData;

    pUserInterface->mContextMenuGradientItems[1].pName = "Add key";
    pUserInterface->mContextMenuGradientItems[1].mType = TF_UI_CONTEXT_MENU_ITEM;
    pUserInterface->mContextMenuGradientItems[1].mItemData.pOnSelect = addGradientKeyCallBack;
    pUserInterface->mContextMenuGradientItems[1].mItemData.pOnSelectUserData = &pUserInterface->mContextMenuDeleteGradientData;
#endif

    return true;
}

void platformSetDefaultFontHeight(float fontHeight) { pUserInterface->mPlatformDefaultFontHeight = fontHeight; }

void platformExitUserInterface()
{
#ifdef ENABLE_FORGE_UI
    tf_delete(pUserInterface);
#endif
}

void markToUpdateWidgetScrollFocus() { pUserInterface->mGamepadUpdateWidgetScrollFocus = true; }

void updateStateHideUI()
{
    if (pUserInterface->mEnabledSWL)
    {
        pUserInterface->mHiddenUI_SWL = !pUserInterface->mHiddenUI_SWL;
    }
    else if (getContext()->current != NULL)
    {
        // Only hide selected window
        struct nk_window* pCurrentWindow = getContext()->current;
        if ((pCurrentWindow->flags & NK_WINDOW_MINIMIZED) != 0 && (pCurrentWindow->flags & NK_WINDOW_MINIMIZABLE))
        {
            nk_window_collapse(getContext(), pCurrentWindow->name_string, NK_MAXIMIZED);
        }
        else
        {
            nk_window_collapse(getContext(), pCurrentWindow->name_string, NK_MINIMIZED);
        }
    }
}

void updateGamepadInput(float deltaTime)
{
    // Gamepad
    pUserInterface->mGamepadUpdateWidgetScrollFocus = false;

    if (pUserInterface->mAutoSwitchSWL)
    {
        pUserInterface->mEnabledSWL = inputHasActiveGamepad();
    }

    for (int i = 0; i < (int)TF_MAX_GAMEPADS; i++)
    {
        if (i == pUserInterface->mGamepadIndex)
        {
            pUserInterface->mGamepadIndex = i;

            pUserInterface->mGamepadScrollControl = inputGetValue(i, pUserInterface->mGamepadControlButtons.mScrollControl) > 0.0f;

            if (pUserInterface->mGamepadScrollControl)
            {
                pUserInterface->mGamepadActive = true;
            }

            nk_window* currentActiveWin = getContext()->active;
            if (currentActiveWin != NULL && currentActiveWin->popup.active && currentActiveWin->popup.type == NK_PANEL_POPUP &&
                currentActiveWin->popup.win != NULL)
            {
                currentActiveWin = currentActiveWin->popup.win;
            }
            else if (pUserInterface->mWindowSelected != NULL)
            {
                currentActiveWin = pUserInterface->mWindowSelected;
            }

            // reset popup navigation
            if (currentActiveWin != NULL && !(currentActiveWin->popup.active && currentActiveWin->popup.win != NULL))
            {
                pUserInterface->mGamepadPopupCapturing = false;
                pUserInterface->mGamepadPopupIndex = 0;
            }

            // Widget navigation
            bool                upPressed = inputGetValue(i, pUserInterface->mGamepadControlButtons.mKeyUp) > 0.0f;
            bool                downPressed = inputGetValue(i, pUserInterface->mGamepadControlButtons.mKeyDown) > 0.0f;
            GamepadSliderAction verticalAction{};

            pUserInterface->mGamepadPreviousVerticalHoldTime = pUserInterface->mGamepadVerticalHoldTime;
            if (upPressed || downPressed)
            {
                pUserInterface->mGamepadVerticalHoldTime += deltaTime;
                pUserInterface->mGamepadActive = true;

                verticalAction.mStepDirection = upPressed ? -1 : 1;
                if (pUserInterface->mGamepadVerticalHoldTime >= pUserInterface->mGamepadRepeatDelay)
                {
                    verticalAction.mType = GamepadSliderActionType::GAMEPAD_HORIZONTAL_ACTION_TYPE_HOLD;
                }
                else if (pUserInterface->mGamepadWasUp || pUserInterface->mGamepadWasDown)
                {
                    // if vertical buttons were presed but time is not enough, we skip this frame

                    verticalAction.mStepDirection = 0;
                    pUserInterface->mGamepadWasUp = false;
                    pUserInterface->mGamepadWasDown = false;
                }

                pUserInterface->mGamepadWasUp = upPressed;
                pUserInterface->mGamepadWasDown = downPressed;
            }
            else
            {
                pUserInterface->mGamepadVerticalHoldTime = 0.0f;
                pUserInterface->mGamepadWasUp = false;
                pUserInterface->mGamepadWasDown = false;
            }

            if (!pUserInterface->mGamepadScrollControl)
            {
                int offset = UnpackWidgetVerticalActionForSliderf(verticalAction, 0, 50, 1);

                if (offset)
                {
                    markToUpdateWidgetScrollFocus();
                }
                if (!pUserInterface->mGamepadPopupCapturing)
                {
                    pUserInterface->mGamepadSelectedIndex = max(0, pUserInterface->mGamepadSelectedIndex + offset);
                }
                else
                {
                    pUserInterface->mGamepadPopupIndex = max(0, pUserInterface->mGamepadPopupIndex + offset);
                }
            }

            if (pUserInterface->pGamepadActiveWindow != currentActiveWin)
            {
                pUserInterface->pGamepadActiveWindow = currentActiveWin;
                pUserInterface->mGamepadSelectedIndex = currentActiveWin->meta_data[0];
            }
            else
            {
                pUserInterface->mGamepadSelectedIndex =
                    max(0, min(pUserInterface->mGamepadSelectedIndex, pUserInterface->mGamepadWidgetCount - 1));
            }

            pUserInterface->mGamepadWidgetCount = 0;

            // Left/right
            pUserInterface->mGamepadPreviousHorizontalHoldTime = pUserInterface->mGamepadHorizontalHoldTime;
            pUserInterface->mGamepadHorizontal += (int)inputGetValue(i, pUserInterface->mGamepadControlButtons.mKeyRight);
            pUserInterface->mGamepadHorizontal -= (int)inputGetValue(i, pUserInterface->mGamepadControlButtons.mKeyLeft);
            if (pUserInterface->mGamepadHorizontal)
            {
                pUserInterface->mGamepadHorizontalHoldTime += deltaTime;
                pUserInterface->mGamepadActive = true;
            }
            else
            {
                pUserInterface->mGamepadHorizontalHoldTime = 0.0f;
                pUserInterface->mGamepadPreviousHorizontalHoldTime = 0.0f;
            }

            if (pUserInterface->mGamepadHorizontalHoldTime != deltaTime &&
                pUserInterface->mGamepadHorizontalHoldTime < pUserInterface->mGamepadRepeatDelay)
            {
                pUserInterface->mGamepadHorizontal = 0;
                pUserInterface->mGamepadPreviousHorizontalHoldTime = 0;
            }

            // Major Left/Right
            int majorHorizontal = (int)inputGetValue(i, pUserInterface->mGamepadControlButtons.mMajorKeyRight) -
                                  (int)inputGetValue(i, pUserInterface->mGamepadControlButtons.mMajorKeyLeft);
            if (majorHorizontal != 0)
            {
                pUserInterface->mGamepadActive = true;
            }

            pUserInterface->mGamepadMajorHorizontal = majorHorizontal;
            if (pUserInterface->mGamepadMajorHorizontal)
            {
                pUserInterface->mGamepadMajorHorizontalHoldTime += deltaTime;
            }
            else
            {
                pUserInterface->mGamepadMajorHorizontalHoldTime = 0.0f;
            }

            if (pUserInterface->mGamepadMajorHorizontalHoldTime != deltaTime)
            {
                pUserInterface->mGamepadMajorHorizontal = 0;
            }

            // Back pressed
            pUserInterface->mGamepadWasBack = inputGetValue(i, pUserInterface->mGamepadControlButtons.mKeyBack) > 0.0f;
            if (pUserInterface->mGamepadPopupCapturing && pUserInterface->mGamepadWasBack)
            {
                struct nk_window* active = getContext()->active;
                if (active->popup.active && active->popup.win != NULL)
                {
                    active->popup.win->flags |= NK_WINDOW_HIDDEN;
                    pUserInterface->mGamepadPopupCapturing = false;
                    pUserInterface->mGamepadPopupIndex = 0;
                    break;
                }
            }

            // Minimaze pressed
            bool pressHideUI = inputGetValue(i, pUserInterface->mGamepadControlButtons.mKeyHideUI) > 0.0f;
            if (pressHideUI)
            {
                pUserInterface->mGamepadActive = true;
                updateStateHideUI();
            }

            pUserInterface->mGamepadLeftStickDelta =
                float2(inputGetValue(pUserInterface->mGamepadIndex, pUserInterface->mGamepadControlButtons.mLeftStickX),
                       inputGetValue(pUserInterface->mGamepadIndex, pUserInterface->mGamepadControlButtons.mLeftStickY)) *
                deltaTime;

            if (currentActiveWin != NULL && pUserInterface->mGamepadActive)
            {
                // put cursor into window to capture focuse at the beginning
                nk_input_motion(getContext(), (int32_t)currentActiveWin->bounds.x, (int32_t)currentActiveWin->bounds.y);
            }

            if (pUserInterface->mEnabledSWL && pUserInterface->mHiddenUI_SWL)
            {
                pUserInterface->mGamepadActive = false;
            }
            break;
        }
    }
}

void updateGamepadWindowSelection()
{
    if (pUserInterface->mEnabledSWL)
    {
        if (pUserInterface->mWindowSelected != NULL)
        {
            nk_window_set_focus(getContext(), pUserInterface->mWindowSelected->name_string);
        }
    }

    if (pUserInterface->mGamepadActive && !(pUserInterface->mEnabledSWL && pUserInterface->mHiddenUI_SWL))
    {
        if (pUserInterface->mGamepadMajorHorizontal < 0)
        {
            pUserInterface->mWindowSelectionIndex -= 1;

            if (pUserInterface->mWindowSelectionIndex < 0)
            {
                pUserInterface->mWindowSelectionIndex += pUserInterface->mWindowCounter;
            }
        }
        else if (pUserInterface->mGamepadMajorHorizontal > 0)
        {
            pUserInterface->mWindowSelectionIndex = (pUserInterface->mWindowSelectionIndex + 1) % pUserInterface->mWindowCounter;
        }

        if (pUserInterface->mGamepadMajorHorizontal != 0)
        {
            markToUpdateWidgetScrollFocus();
        }

        pUserInterface->mWindowSelectionIndex =
            (int32_t)clampi(pUserInterface->mWindowSelectionIndex, 0, pUserInterface->mWindowCounter - 1);
    }

    pUserInterface->mWindowCounter = 0;

    if (pUserInterface->mEnabledSWL && !pUserInterface->mHiddenUI_SWL)
    {
        int tabRowCount = 1;
        pUserInterface->mHaveWindow = true;
        if (nk_begin(getContext(), "SWL Tab selection", nk_rect(0, 0, pUserInterface->mWidth, pUserInterface->mOffsetToDrawWindowSWL.y),
                     NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR))
        {
            uint32_t windowsCount = (uint32_t)arrlen(pUserInterface->ppWindowNameArrSWL);
            if (windowsCount != 0)
            {
                uiStaticBeginTabBar(windowsCount + 1, &pUserInterface->mWindowSelectionIndex);
                for (uint32_t i = 0; i < windowsCount; i++)
                {
                    const char* title = pUserInterface->ppWindowNameArrSWL[i];
                    uiStaticTabRoundScreen(title, uiGetTextWidth(title), (uint32_t)pUserInterface->mRoundScreenSize,
                                           &pUserInterface->mWindowSelectionIndex);
                }
                tabRowCount = uiGetTabRowCount();
                uiStaticEndTabBar();
            }
        }

        if (tabRowCount > 1)
        {
            pUserInterface->mOffsetToDrawWindowSWL = float2(0, uiGetCurrentWindowHeight());
        }
        else
        {
            pUserInterface->mOffsetToDrawWindowSWL = float2(0, uiCalculateHeightOfTextRows(DEFAULT_ROW_HEIGHT) + 10);
        }

        struct nk_window* win = (struct nk_window*)uiGetCurrentWindow();
        win->bounds.h = pUserInterface->mOffsetToDrawWindowSWL.y;

        nk_end(getContext());
    }

    arrsetlen(pUserInterface->ppWindowNameArrSWL, 0);
}

void updateGamepadHints(float deltaTime)
{
    bool hasActiveGamepad = inputHasActiveGamepad();

    if (!pUserInterface->mHadConnectedGamepad && hasActiveGamepad)
    {
        pUserInterface->mShowGamepadControlHints = true;
        pUserInterface->mShowGamepadControlHintsTime = 0.0f;
    }

    pUserInterface->mHadConnectedGamepad = hasActiveGamepad;

    if (pUserInterface->mShowGamepadControlHints)
    {
        pUserInterface->mShowGamepadControlHintsTime += deltaTime;

        const float timeToHideHints = 10.0f;
        if (pUserInterface->mShowGamepadControlHintsTime > timeToHideHints)
        {
            pUserInterface->mShowGamepadControlHints = false;
            pUserInterface->mShowGamepadControlHintsTime = 0.0f;
        }
    }

    if (pUserInterface->mShowGamepadControlHints)
    {
        const float hintsWidth = 230;
        const float hintsHeight = 210;
        pUserInterface->mHaveWindow = true;
        if (nk_begin(getContext(), "Gamepad UI Control Hints", nk_rect(0, pUserInterface->mHeight - hintsHeight, hintsWidth, hintsHeight),
                     NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_NO_INPUT))
        {
            uiLayoutAutoTextRows(1);
            struct nk_context* ctx = getContext();
            nk_flags           alg = alignmentConvertToNk(TFUIAlignmentText::TF_ALIGN_LEFT);

            nk_label(ctx, "A - Action", alg);
            nk_label(ctx, "B - Back (From pop-up)", alg);
            nk_label(ctx, "Y + Up/Down - Vertical scroll", alg);
            nk_label(ctx, "LB/RB - Window selection", alg);
            nk_label(ctx, "Start - Minimize window", alg);
            nk_label(ctx, "Up/Down - Widget selection", alg);
            nk_label(ctx, "Left/Right - Change (float/int) slider/field", alg);
        }
        nk_end(getContext());
    }
}

void platformUpdateUserInterface(float deltaTime)
{
#ifdef ENABLE_FORGE_UI
    // Render can me nullptr when initUserInterface wasn't called, this can happen when the build compiled using
    // ENABLED_FORGE_UI but then in runtime the App decided not to use the UI
    if (pUserInterface->pRenderer == NULL)
    {
        return;
    }

    clearUserInterface();

    deltaTime = deltaTime == 0.0f ? 0.016f : deltaTime;

    // mirror input state to Nuklear
    nk_input_begin(getContext());

    for (uint32_t i = NK_KEY_SHIFT; i < NK_KEY_MAX; i++)
    {
        nk_input_key(getContext(), (enum nk_keys)i, (nk_bool)(inputGetValue(0, pUserInterface->mInputKeyMap[i]) >= 1.0f));
    }

    // On most platorms, the mouse position is retrieved from the actual hardware values.
    // In VR, we raycast the 2D layer to determine the cursor position
#ifndef ENABLE_FORGE_VR_UI
    int mouseX = (int)inputGetValue(0, pUserInterface->mInputCursorBindings.x);
    int mouseY = (int)inputGetValue(0, pUserInterface->mInputCursorBindings.y);
#else
    updateVRUserMarker();
    int mouseX = 0;
    int mouseY = 0;
    if (pUserInterface->mVRLayer.mCursorValid)
    {
        mouseX = (int)pUserInterface->mVRLayer.mCursorLocation.x;
        mouseY = (int)pUserInterface->mVRLayer.mCursorLocation.y;
    }
#endif
    if (mouseX != pUserInterface->mLastMouseX || mouseY != pUserInterface->mLastMouseY)
    {
        nk_input_motion(getContext(), mouseX, mouseY);

        pUserInterface->mGamepadActive = false;
        pUserInterface->mLastMouseX = mouseX;
        pUserInterface->mLastMouseY = mouseY;
    }

    for (uint32_t i = NK_BUTTON_LEFT; i < NK_BUTTON_MAX; i++)
    {
        nk_input_button(getContext(), (enum nk_buttons)i, (int)getContext()->input.mouse.pos.x, (int)getContext()->input.mouse.pos.y,
                        (nk_bool)(inputGetValue(0, pUserInterface->mInputButtonMap[i]) >= 1.0f));
    }

    nk_input_scroll(getContext(), { 0.0f, 0.2f * (inputGetValue(0, pUserInterface->mInputScrollBindings.up) -
                                                  inputGetValue(0, pUserInterface->mInputScrollBindings.down)) });

    if (inputGetValue(0, pUserInterface->mHideUIBinding) > 0.0f)
    {
        updateStateHideUI();
    }

    if (inputGetValue(0, pUserInterface->mInputKeyMap[NK_KEY_CTRL]) < 1.0f)
    {
        uint_least32_t* pTyped;
        uint32_t        mNumTyped;
        inputGetCharInput(&pTyped, &mNumTyped);
        for (uint32_t i = 0; i < mNumTyped; i++)
        {
            nk_input_char(getContext(), (char)pTyped[i]);
        }
    }

    updateGamepadInput(deltaTime);
    nk_input_end(getContext());

    updateGamepadWindowSelection();
    updateGamepadHints(deltaTime);

    updateMessageBoxWindow();

#else
    (void)deltaTime;
#endif
}

/****************************************************************************/
// MARK: - Macro function definitions
/****************************************************************************/

TFUIWidgetInteraction uiWidgetAssertType(TFUIWidgetInteraction result, TFUIWidgetInteractionType type)
{
#ifdef ENABLE_FORGE_UI
    ASSERT(result.type == type);
    return result;
#else
    UNREF_PARAM(result);
    UNREF_PARAM(type);
    ASSERTFAIL("Attempting to use Forge UI without a define.");
    ASSERTFAIL("Make sure to define 'ENABLE_FORGE_UI' for UI to work.");
    return TFUIWidgetInteraction{};
#endif
}

bool uiPanelAssertTypeGetFillable(TFUIWindowInteraction result, TFUIPanelType type)
{
#ifdef ENABLE_FORGE_UI
    ASSERT(result.panelType == type);
    return result.fillable;
#else
    UNREF_PARAM(result);
    UNREF_PARAM(type);
    ASSERTFAIL("Attempting to use Forge UI without a define.");
    ASSERTFAIL("Make sure to define 'ENABLE_FORGE_UI' for UI to work.");
    return false;
#endif
}

/****************************************************************************/
// MARK: - Static Function Definitions
/****************************************************************************/
#ifdef ENABLE_FORGE_UI
static void* alloc_func(nk_handle user_data, void* old, size_t size)
{
    UNREF_PARAM(user_data);
    UNREF_PARAM(old);
    return tf_malloc(size);
}

static void free_func(nk_handle user_data, void* old)
{
    UNREF_PARAM(user_data);
    tf_free(old);
}

#if CACHE_WIDGETS
static TFUIWidget createNewWidget(TFUIWidgetId id, TFUIWidgetType type)
{
    TFUIWidget newWidget = { 0 };
    newWidget.mId = id;
    newWidget.mType = type;
    return newWidget;
}

static bool compareWidgetCommands(UIWindowCacheEntry* cache, ptrdiff_t start, ptrdiff_t end)
{
    return memcmp((char*)cache->pLastCmdBuffer + start, (char*)getCurrentCommandBufferPointer(start), end - start);
}
#endif

static TFUIWidgetId getWidgetId(TFUIWidget* widget)
{
#if CACHE_WIDGETS
    return widget->mId;
#else
    UNREF_PARAM(widget);
    return 0;
#endif
}

#if CACHE_WIDGETS
static void* pushbackCachedCommands(UIWindowCacheEntry* cache, ptrdiff_t start, ptrdiff_t end)
{
    size_t lastCmdSize = end - start;
    void*  src = (char*)cache->pLastCmdBuffer + start;
    void*  dst = getCurrentCommandBufferPointer(getLastWidgetCommandEnd());
    // TODO: handle new command buffer not having enough space
    return memcpy(dst, src, lastCmdSize);
}
#endif

static void saveLastWidgetOffset()
{
#if CACHE_WIDGETS
    pUserInterface->mBlackboard.mLastWidgetOffset = (ptrdiff_t)getContext()->current->buffer.end;
#endif
}

static TFUIWidget* updateLastWidget(const char* label, TFUIWidgetType type)
{
#if !defined(FORGE_DEBUG)
    UNREF_PARAM(label);
#endif
    // Get cache for the current window
    UIBlackboard*       bb = &pUserInterface->mBlackboard;
    UIWindowCacheEntry* entry = bb->pWindowCache;

#if CACHE_WIDGETS
    TFUIWidget* widget = NULL;
    bool        needsUpdate = false;

    if (!entry)
    {
        ASSERTFAIL("Failed to identify current window. Call uiBeginWidgetWindow and confirm that the window is fillable.");
        return widget;
    }

    if (arrlenu(entry->pWidgets) == entry->mNextWidgetId)
    {
        // new widget was added
        widget = &arrput(entry->pWidgets, createNewWidget(entry->mNextWidgetId, type));
        needsUpdate = true;
    }
    else
    {
        widget = &entry->pWidgets[entry->mNextWidgetId];
    }

    ASSERT(widget);
    entry->mNextWidgetId++;

    // offsets into the current window's command buffer for the last added widget
    ptrdiff_t lastStartOffset = getLastWidgetCommandStart();
    ptrdiff_t lastEndOffset = getLastWidgetCommandEnd();
    // related widget data changed, so update the widget and mark UI and it's root window for redraw
    if (needsUpdate || widget->mType != type || lastStartOffset != widget->mCommandStart || lastEndOffset != widget->mCommandEnd ||
        compareWidgetCommands(entry, lastStartOffset, lastEndOffset))
    {
#if defined(FORGE_DEBUG)
        // add widget id at the end
        snprintf(widget->mLabel, MAX_LABEL_STR_LENGTH, "%s##%d", label, entry->mNextWidgetId - 1);
#endif
        widget->mCommandStart = lastStartOffset;
        widget->mCommandEnd = lastEndOffset;
        widget->mWidgetState = (TFUIWidgetState)getContext()->last_widget_state;
        entry->mRedraw = true;

        // TODO: update bounds
        // ..
        // TODO: rewire parent/children pointers if needed
        // ..

        if (!pUserInterface->mCommandsChanged)
        {
            pUserInterface->mCommandsChanged = true;
        }
    }

    return widget;
#else
    UNREF_PARAM(label);
    UNREF_PARAM(type);

    entry->mRedraw = true;
    pUserInterface->mCommandsChanged = true;

    return NULL;
#endif
}

static TFUIWidgetId getNextWidgetId(UIWindowCacheEntry* curWndCache) { return curWndCache->mNextWidgetId; }

static TFUIWidget* getCachedWidget(UIWindowCacheEntry* cache, TFUIWidgetId id, TFUIWidgetType type)
{
#if CACHE_WIDGETS
    if (arrlenu(cache) <= id)
        return NULL;
    TFUIWidget* widget = &cache->pWidgets[id];
    bool        sameType = widget->mType == type;
    ASSERT(sameType);
    return (sameType) ? widget : NULL;
#else
    UNREF_PARAM(cache);
    UNREF_PARAM(id);
    UNREF_PARAM(type);
    return NULL;
#endif
}

static void setDefaultStyle()
{
    struct nk_color table[NK_COLOR_COUNT];
    struct nk_color active = nk_rgba_f(0.54f, 0.54f, 0.54f, 1.00f);
    struct nk_color text = nk_rgba_f(1.00f, 1.00f, 1.00f, 1.00f);
    struct nk_color sliderBackground = nk_rgba_f(0.26f, 0.26f, 0.26f, 1.00f);
    struct nk_color sliderBackgroundHover = nk_rgba_f(0.29f, 0.29f, 0.29f, 1.00f);
    struct nk_color sliderBar = nk_rgba_f(0.46f, 0.46f, 0.46f, 1.00f);
    table[NK_COLOR_TEXT] = nk_rgba_f(1.00f, 1.00f, 1.00f, 1.00f);
    table[NK_COLOR_WINDOW] = nk_rgba_f(0.33f, 0.33f, 0.33f, 1.00f);
    table[NK_COLOR_HEADER] = nk_rgba_f(0.27f, 0.27f, 0.27f, 1.00f);
    table[NK_COLOR_BORDER] = nk_rgba_f(0.50f, 0.50f, 0.50f, 1.00f);
    table[NK_COLOR_BUTTON] = nk_rgba_f(0.46f, 0.46f, 0.46f, 1.00f);
    table[NK_COLOR_BUTTON_HOVER] = active;
    table[NK_COLOR_BUTTON_ACTIVE] = active;
    table[NK_COLOR_TOGGLE] = nk_rgba_f(0.71f, 0.71f, 0.71f, 0.40f);
    table[NK_COLOR_TOGGLE_HOVER] = nk_rgba_f(0.73f, 0.74f, 0.75f, 1.00f);
    table[NK_COLOR_TOGGLE_CURSOR] = nk_rgba_f(0.97f, 0.97f, 0.97f, 1.00f);
    table[NK_COLOR_SELECT] = nk_rgba_f(0.33f, 0.33f, 0.33f, 1.00f);
    table[NK_COLOR_SELECT_ACTIVE] = active;
    table[NK_COLOR_SLIDER] = nk_rgba_f(0.15f, 0.15f, 0.15f, 0.53f);
    table[NK_COLOR_SLIDER_CURSOR] = nk_rgba_f(0.73f, 0.73f, 0.73f, 1.00f);
    table[NK_COLOR_SLIDER_CURSOR_HOVER] = nk_rgba_f(0.68f, 0.68f, 0.68f, 1.00f);
    table[NK_COLOR_SLIDER_CURSOR_ACTIVE] = nk_rgba_f(0.94f, 0.94f, 0.94f, 1.00f);
    table[NK_COLOR_PROPERTY] = nk_rgba_f(0.44f, 0.44f, 0.44f, 1.00f);
    table[NK_COLOR_EDIT] = nk_rgba_f(0.27f, 0.27f, 0.27f, 1.00f);
    table[NK_COLOR_EDIT_CURSOR] = active;
    table[NK_COLOR_COMBO] = nk_rgba_f(0.44f, 0.44f, 0.44f, 1.00f);
    table[NK_COLOR_CHART] = nk_rgba_f(0.27f, 0.27f, 0.27f, 1.00f);
    table[NK_COLOR_CHART_COLOR] = nk_rgba_f(0.85f, 0.85f, 0.85f, 1.00f);
    table[NK_COLOR_CHART_COLOR_HIGHLIGHT] = nk_rgba_f(1.00f, 0.73f, 0.70f, 1.00f);
    table[NK_COLOR_SCROLLBAR] = nk_rgba_f(0.15f, 0.15f, 0.15f, 0.53f);
    table[NK_COLOR_SCROLLBAR_CURSOR] = nk_rgba_f(0.55f, 0.55f, 0.55f, 1.00f);
    table[NK_COLOR_SCROLLBAR_CURSOR_HOVER] = nk_rgba_f(0.65f, 0.65f, 0.65f, 1.00f);
    table[NK_COLOR_SCROLLBAR_CURSOR_ACTIVE] = nk_rgba_f(0.73f, 0.73f, 0.73f, 1.00f);
    table[NK_COLOR_TAB_HEADER] = nk_rgba_f(0.88f, 0.88f, 0.88f, 0.31f);
    table[NK_COLOR_KNOB] = table[NK_COLOR_SLIDER];
    table[NK_COLOR_KNOB_CURSOR] = table[NK_COLOR_SLIDER_CURSOR];
    table[NK_COLOR_KNOB_CURSOR_HOVER] = table[NK_COLOR_SLIDER_CURSOR_HOVER];
    table[NK_COLOR_KNOB_CURSOR_ACTIVE] = table[NK_COLOR_SLIDER_CURSOR_ACTIVE];

    nk_style_from_table(getContext(), table);

    // Spacing and padding
    struct nk_style* currStyle = &getContext()->style;
    currStyle->window.scrollbar_size.x = currStyle->font->height;
    currStyle->window.scrollbar_size.y = currStyle->font->height;
    currStyle->window.padding = nk_vec2(3, 3);
    currStyle->window.spacing = nk_vec2(3, 5);
    currStyle->window.group_padding = nk_vec2(2, 2);
    currStyle->button.padding = nk_vec2(1, 1);

    currStyle->selectable.padding = nk_vec2(5, 1);
    currStyle->option.padding = nk_vec2(1, 1);
    currStyle->checkbox.padding = nk_vec2(1, 1);
    currStyle->slider.padding = nk_vec2(1, 1);
    currStyle->property.padding = nk_vec2(4, 4);
    currStyle->edit.padding = nk_vec2(3, 3);
    currStyle->tab.padding = nk_vec2(3, 3);
    currStyle->menu_button.padding = nk_vec2(8, 4);
    currStyle->gradient.padding = nk_vec2(1, 1);

    // Borders
    currStyle->slider.border = 1.0f;
    currStyle->slider.rounding = 0.0f;
    currStyle->window.border = 1.0f;
    currStyle->window.rounding = 0.0f;
    currStyle->button.rounding = 1.0f;
    currStyle->button.border = 1.0f;
    currStyle->gradient.rounding = 1.0f;
    currStyle->gradient.border = 1.0f;
    currStyle->edit.rounding = 0.0f;
    currStyle->property.border = 1.0f;
    currStyle->property.rounding = 0.0f;
    currStyle->tab.border = 1.0f;
    currStyle->tab.rounding = 0;
    currStyle->selectable.border = 1.0;
    currStyle->selectable.rounding = 0;
    currStyle->combo.border = 1;
    currStyle->combo.rounding = 0;
    currStyle->checkbox.checkmark_thickness = 4.0f;

    currStyle->property.hover = nk_style_item{ NK_STYLE_ITEM_COLOR, { active } };
    currStyle->combo.hover = nk_style_item{ NK_STYLE_ITEM_COLOR, { active } };

    // Other styling
    // Disable arrow and replace with our own
    currStyle->combo.sym_active = NK_SYMBOL_NONE;
    currStyle->combo.sym_hover = NK_SYMBOL_NONE;
    currStyle->combo.sym_normal = NK_SYMBOL_NONE;

    // Slider
#ifndef NUKLEAR_UI_VANILLA
    currStyle->slider.text_color = text;
    currStyle->slider.bar_filled = sliderBar;
    currStyle->slider.bar_normal = sliderBackground;
    currStyle->slider.bar_hover = sliderBackgroundHover;
#endif
    // nk_style_default(getContext());
}

static nk_text_alignment alignmentConvertToNk(TFUIAlignmentText flag)
{
    switch (flag)
    {
    case TF_ALIGN_LEFT:
        return NK_TEXT_LEFT;
    case TF_ALIGN_CENTER:
        return NK_TEXT_CENTERED;
    case TF_ALIGN_RIGHT:
        return NK_TEXT_RIGHT;
    case TF_ALIGN_WRAPPED:
    default:
        ASSERTFAIL("Invalid TFUIAlignmentText flag provided.");
        return (nk_text_alignment)0;
    }
}

static nk_color float4ToNkColor(float4 color) { return nk_rgba_fv((const float*)&color[0]); }
static float4   nkColorTofloat4(nk_color color)
{
    return f4Make((float)color.r / 255.0f, (float)color.g / 255.0f, (float)color.b / 255.0f, (float)color.a / 255.0f);
}

#if CACHE_WIDGETS || CUSTOM_LAYOUT
static ptrdiff_t getLastWidgetCommandStart() { return pUserInterface->mBlackboard.mLastWidgetOffset; }

static ptrdiff_t getLastWidgetCommandEnd() { return (ptrdiff_t)getContext()->current->buffer.end; }

static void* getCurrentCommandBufferPointer(ptrdiff_t offset) { return (nk_byte*)getContext()->current->buffer.base->memory.ptr + offset; }
#endif
#if CUSTOM_LAYOUT
static struct nk_rect calculatePanelBounds(UILayoutPanel* panel, nk_panel_type panelType)
{
    (void)panelType;

    // TODO: if layout matches
    if (false)
    {
        return panel->mBounds;
    }
    else
    {
        struct nk_rect bounds;
        nk_panel_alloc_space(&bounds, getContext());
        return bounds;
    }
}

static void setPanelLayout(UILayoutPanel* panel, nk_panel_type panelType, nk_flags flags)
{
    struct nk_rect     bounds;
    struct nk_window   fakeWin;
    struct nk_context* ctx = getContext();
    struct nk_window*  win = ctx->current;

    bounds = calculatePanelBounds(panel, panelType);
    {
        const struct nk_rect* c = &win->layout->clip;
        if (!NK_INTERSECT(c->x, c->y, c->w, c->h, bounds.x, bounds.y, bounds.w, bounds.h) && !(flags & NK_WINDOW_MOVABLE))
        {
            return;
        }
    }
    if (win->flags & NK_WINDOW_ROM)
    {
        flags |= NK_WINDOW_ROM;
    }

    if (flags != panel->mFlags)
    {
        // TODO: account for changed flags
    }

    nk_zero(&fakeWin, sizeof(fakeWin));
    fakeWin.bounds = bounds;
    fakeWin.flags = panel->mFlags;
    fakeWin.scrollbar.x = panel->mOffsetX;
    fakeWin.scrollbar.y = panel->mOffsetY;
    fakeWin.buffer = win->buffer;
    fakeWin.layout = (struct nk_panel*)nk_create_panel(ctx);
    nk_zero(fakeWin.layout, sizeof(struct nk_panel));
    fakeWin.layout->type = panelType;
    ctx->current = &fakeWin;

    if (!(fakeWin.flags & NK_WINDOW_HIDDEN) && !(fakeWin.flags & NK_WINDOW_CLOSED))
    {
        struct nk_vec2 panel_padding = nk_panel_get_padding(&ctx->style, panelType);

        fakeWin.layout->flags = panel->mFlags;
        fakeWin.layout->bounds = bounds;
        fakeWin.layout->border = panel->mBorder;
        fakeWin.layout->at_y = fakeWin.layout->bounds.y;
        fakeWin.layout->at_x = fakeWin.layout->bounds.x;
        nk_layout_reset_min_row_height(ctx);
        fakeWin.layout->row.height = panel_padding.y;
        fakeWin.layout->has_scrolling = panel->mHasScrolling;
        fakeWin.layout->clip = panel->mClip;
    }

    /* TODO: figure out how to handle input for the panel */
}
#endif
static void fillDefaultInputKeyMap(TFInputEnum* keyMap, TFInputEnum* buttonMap, TFInputEnum* mouseX, TFInputEnum* mouseY,
                                   TFInputEnum* scrollDown, TFInputEnum* scrollUp, TFInputEnum* hideUI,
                                   GamepadControlButtons* gamepadControlButtons)
{
    // Keyboard + Mouse
    inputAddCustomBindings(R"(
uiKeyShift; button; K_LSHIFT; pressed
uiKeyCtrl; button; K_LCTRL; pressed
uiKeyDel; button; K_DEL; repeat
uiKeyEnter; button; K_ENTER; repeat
uiKeyTab; button; K_TAB; repeat
uiKeyBackspace; button; K_BACKSPACE; repeat
uiKeyCopy; button; K_C; single; cond; K_LCTRL; pressed
uiKeyCut; button; K_X; single; cond; K_LCTRL; pressed
uiKeyPaste; button; K_V; single; cond; K_LCTRL; pressed
uiKeyUp; button; K_UPARROW; repeat
uiKeyDown; button; K_DOWNARROW; repeat
uiKeyLeft; button; K_LEFTARROW; repeat
uiKeyRight; button; K_RIGHTARROW; repeat)
uiHideUI; button; K_F1; single)"
                           /* text shortcuts */
                           R"(
uiKeyTextInsertMode; button; K_INS; single
uiKeyTextReplaceMode; button; K_F11; single
uiKeyTextResetMode; button; K_F12; pressed
uiKeyTextLineStart; button; K_HOME; single
uiKeyTextLineEnd; button; K_END; single
uiKeyTextStart; button; K_HOME; single; cond; K_LCTRL; pressed
uiKeyTextEnd; button; K_END; single; cond; K_LCTRL; pressed
uiKeyTextUndo; button; K_Z; single; cond; K_LCTRL; pressed
uiKeyTextRedo; button; K_Y; single; cond; K_LCTRL; pressed
uiKeyTextSelectAll; button; K_A; single; cond; K_LCTRL; pressed
uiKeyTextWordLeft; button; K_LEFTARROW; single; cond; K_LCTRL; pressed
uiKeyTextWordRight; button; K_RIGHTARROW; single; cond; K_LCTRL; pressed)"
                           /* scrollbar shortcuts*/
                           R"(
uiKeyScrollStart; button; MOUSE_WHEEL_UP; pressed; cond; K_LCTRL; pressed
uiKeyScrollEnd; button; MOUSE_WHEEL_DOWN; pressed; cond; K_LCTRL; pressed
uiKeyScrollDown; button; MOUSE_WHEEL_DOWN; pressed
uiKeyScrollUp; button; MOUSE_WHEEL_UP; pressed
uiKeyScrollPageDown; button; K_PGDN; pressed
uiKeyScrollPageUp; button; K_PGUP; pressed
uiButtonLeft; button; MOUSE_1; pressed
uiButtonMiddle; button; MOUSE_3; pressed
uiButtonRight; button; MOUSE_2; pressed
uiButtonDouble; button; MOUSE_1; double;)"
                           /* cursor bindings */
                           R"(
uiCursorX; analog; MOUSE_X; 1.0f
uiCursorY; analog; MOUSE_Y; 1.0f)");

    // Gamepad
    inputAddCustomBindings(R"(
uiButtonLeft; button; GPAD_A; pressed

uiGMajorKeyLeft; button; GPAD_L1; pressed
uiGMajorKeyRight; button; GPAD_R1; pressed
uiGKeyBackspace; button; GPAD_B; pressed
uiGKeyUp; button; GPAD_UP; pressed
uiGKeyDown; button; GPAD_DOWN; pressed
uiGKeyLeft; button; GPAD_LEFT; pressed
uiGKeyRight; button; GPAD_RIGHT; pressed
uiGLeftStickX; analog; GPAD_LX; 1.0f
uiGLeftStickY; analog; GPAD_LY; 1.0f
)"
                           /* scrollbar shortcuts*/
                           R"(
uiKeyScrollDown; button; GPAD_DOWN; pressed; cond; GPAD_Y; pressed
uiKeyScrollUp; button; GPAD_UP; pressed; cond; GPAD_Y; pressed)"
                           /* window control buttons*/
                           R"(
uiGHideUI; button; GPAD_START; single
uiGScrollControl; button; GPAD_Y; pressed
)");

#if defined(HOLOLENS2)
    inputAddCustomBindings("uiButtonLeft; button; HAND_R_PINCHING; pressed");
#endif

    keyMap[NK_KEY_NONE] = (TFInputEnum)0;
    keyMap[NK_KEY_SHIFT] = inputGetCustomBindingEnum("uiKeyShift");
    keyMap[NK_KEY_CTRL] = inputGetCustomBindingEnum("uiKeyCtrl");
    keyMap[NK_KEY_DEL] = inputGetCustomBindingEnum("uiKeyDel");
    keyMap[NK_KEY_ENTER] = inputGetCustomBindingEnum("uiKeyEnter");
    keyMap[NK_KEY_TAB] = inputGetCustomBindingEnum("uiKeyTab");
    keyMap[NK_KEY_BACKSPACE] = inputGetCustomBindingEnum("uiKeyBackspace");
    keyMap[NK_KEY_COPY] = inputGetCustomBindingEnum("uiKeyCopy");
    keyMap[NK_KEY_CUT] = inputGetCustomBindingEnum("uiKeyCut");
    keyMap[NK_KEY_PASTE] = inputGetCustomBindingEnum("uiKeyPaste");
    keyMap[NK_KEY_UP] = inputGetCustomBindingEnum("uiKeyUp");
    keyMap[NK_KEY_DOWN] = inputGetCustomBindingEnum("uiKeyDown");
    keyMap[NK_KEY_LEFT] = inputGetCustomBindingEnum("uiKeyLeft");
    keyMap[NK_KEY_RIGHT] = inputGetCustomBindingEnum("uiKeyRight");
    /* text shortcuts */
    keyMap[NK_KEY_TEXT_INSERT_MODE] = inputGetCustomBindingEnum("uiKeyTextInsertMode");
    keyMap[NK_KEY_TEXT_REPLACE_MODE] = inputGetCustomBindingEnum("uiKeyTextReplaceMode");
    keyMap[NK_KEY_TEXT_RESET_MODE] = inputGetCustomBindingEnum("uiKeyTextResetMode");
    keyMap[NK_KEY_TEXT_LINE_START] = inputGetCustomBindingEnum("uiKeyTextLineStart");
    keyMap[NK_KEY_TEXT_LINE_END] = inputGetCustomBindingEnum("uiKeyTextLineEnd");
    keyMap[NK_KEY_TEXT_START] = inputGetCustomBindingEnum("uiKeyTextStart");
    keyMap[NK_KEY_TEXT_END] = inputGetCustomBindingEnum("uiKeyTextEnd");
    keyMap[NK_KEY_TEXT_UNDO] = inputGetCustomBindingEnum("uiKeyTextUndo");
    keyMap[NK_KEY_TEXT_REDO] = inputGetCustomBindingEnum("uiKeyTextRedo");
    keyMap[NK_KEY_TEXT_SELECT_ALL] = inputGetCustomBindingEnum("uiKeyTextSelectAll");
    keyMap[NK_KEY_TEXT_WORD_LEFT] = inputGetCustomBindingEnum("uiKeyTextWordLeft");
    keyMap[NK_KEY_TEXT_WORD_RIGHT] = inputGetCustomBindingEnum("uiKeyTextWordRight");
    /* scrollbar shortcuts*/
    keyMap[NK_KEY_SCROLL_START] = inputGetCustomBindingEnum("uiKeyScrollStart");
    keyMap[NK_KEY_SCROLL_END] = inputGetCustomBindingEnum("uiKeyScrollEnd");
    keyMap[NK_KEY_SCROLL_DOWN] = inputGetCustomBindingEnum("uiKeyScrollPageDown");
    keyMap[NK_KEY_SCROLL_UP] = inputGetCustomBindingEnum("uiKeyScrollPageUp");

    gamepadControlButtons->mKeyUp = inputGetCustomBindingEnum("uiGKeyUp");
    gamepadControlButtons->mKeyDown = inputGetCustomBindingEnum("uiGKeyDown");
    gamepadControlButtons->mKeyLeft = inputGetCustomBindingEnum("uiGKeyLeft");
    gamepadControlButtons->mKeyRight = inputGetCustomBindingEnum("uiGKeyRight");
    gamepadControlButtons->mMajorKeyLeft = inputGetCustomBindingEnum("uiGMajorKeyLeft");
    gamepadControlButtons->mMajorKeyRight = inputGetCustomBindingEnum("uiGMajorKeyRight");
    gamepadControlButtons->mKeyBack = inputGetCustomBindingEnum("uiGKeyBackspace");
    gamepadControlButtons->mKeyHideUI = inputGetCustomBindingEnum("uiGHideUI");
    gamepadControlButtons->mScrollControl = inputGetCustomBindingEnum("uiGScrollControl");
    gamepadControlButtons->mLeftStickX = inputGetCustomBindingEnum("uiGLeftStickX");
    gamepadControlButtons->mLeftStickY = inputGetCustomBindingEnum("uiGLeftStickY");

    /* mouse button simulation */
    buttonMap[NK_BUTTON_LEFT] = inputGetCustomBindingEnum("uiButtonLeft");
    buttonMap[NK_BUTTON_MIDDLE] = inputGetCustomBindingEnum("uiButtonMiddle");
    buttonMap[NK_BUTTON_RIGHT] = inputGetCustomBindingEnum("uiButtonRight");
    buttonMap[NK_BUTTON_DOUBLE] = inputGetCustomBindingEnum("uiButtonDouble");

    /* mouse position */
    *mouseX = inputGetCustomBindingEnum("uiCursorX");
    *mouseY = inputGetCustomBindingEnum("uiCursorY");

    /* scroll */
    *scrollDown = inputGetCustomBindingEnum("uiKeyScrollDown");
    *scrollUp = inputGetCustomBindingEnum("uiKeyScrollUp");

    *hideUI = inputGetCustomBindingEnum("uiHideUI");
}

static inline TFUIWidgetType getReflectedWidgetType(TypeInfoStructMember* pMember)
{
    if (pMember->pType->mKind == TYPE_INFO_TAG_STRUCT)
    {
        if (strcmp(pMember->pType->pName, "float2") == 0)
        {
            return TF_WIDGET_TYPE_SLIDER_FLOAT2;
        }
        else if (strcmp(pMember->pType->pName, "float3") == 0)
        {
            if (pMember->mFlags & REFLECT_MEMBER_FLAG_COLOR_RGB)
            {
                return TF_WIDGET_TYPE_COLOR3_BUTTON;
            }
            else
            {
                return TF_WIDGET_TYPE_SLIDER_FLOAT3;
            }
        }
        else if (strcmp(pMember->pType->pName, "float4") == 0)
        {
            if (pMember->mFlags & REFLECT_MEMBER_FLAG_COLOR_RGBA)
            {
                return TF_WIDGET_TYPE_COLOR_BUTTON;
            }
            else
            {
                return TF_WIDGET_TYPE_SLIDER_FLOAT4;
            }
        }
        else if (strcmp(pMember->pType->pName, "TFGradientType") == 0)
        {
            return TF_WIDGET_TYPE_GRADIENT_RGBA32;
        }
        else
        {
            return TF_WIDGET_TYPE_COLLAPSING_HEADER;
        }
    }
    else if (pMember->pType->mKind == TYPE_INFO_TAG_INT)
    {
        if (pMember->mIsBool)
        {
            return TF_WIDGET_TYPE_CHECKBOX;
        }
        else
        {
            return TF_WIDGET_TYPE_SLIDER_INT;
        }
    }
    else if (pMember->pType->mKind == TYPE_INFO_TAG_FLOAT)
    {
        return TF_WIDGET_TYPE_SLIDER_FLOAT;
    }
    else if (pMember->pType->mKind == TYPE_INFO_TAG_ENUM)
    {
        return TF_WIDGET_TYPE_DROPDOWN;
    }
    else if (pMember->pType->mKind == TYPE_INFO_TAG_ENUM)
    {
        return TF_WIDGET_TYPE_DROPDOWN;
    }
    else
    {
        return TF_WIDGET_TYPE_NONE;
    }
}

static inline void measureReflectedWidgetDataRecursive(TFReflectedWidgetData* result, TypeInfoStruct* structType)
{
    for (uint32_t i = 0; i < structType->mMemberCount; i += 1)
    {
        TypeInfoStructMember* pMember = structType->pMembers + i;

        // Skip hidden
        if (pMember->mFlags & REFLECT_MEMBER_FLAG_HIDDEN)
        {
            continue;
        }

        result->mWidgetCount++;
        if (pMember->pType && getReflectedWidgetType(pMember) == TF_WIDGET_TYPE_COLLAPSING_HEADER)
        {
            result->mInternalDepth += 1;
            measureReflectedWidgetDataRecursive(result, (TypeInfoStruct*)pMember->pType);
            result->mInternalDepth -= 1;
        }
    }
}

static inline uint32_t initReflectedWidgetData(TFWidgetData* pWidgets, TypeInfoStruct* structType)
{
    uint32_t offset = 0;
    for (uint32_t i = 0; i < structType->mMemberCount; i++)
    {
        TypeInfoStructMember* pMember = structType->pMembers + i;

        // Skip hidden
        if (pMember->mFlags & REFLECT_MEMBER_FLAG_HIDDEN)
        {
            continue;
        }

        memset(pWidgets + offset, 0, sizeof(TFWidgetData));
        pWidgets[offset].mActive = true;
        pWidgets[offset].pMember = pMember;

        TFUIWidgetType widgetType = getReflectedWidgetType(pMember);
        if (widgetType == TF_WIDGET_TYPE_COLLAPSING_HEADER)
        {
            pWidgets[offset].mType = widgetType;
            pWidgets[offset].internalWidgetsCount = initReflectedWidgetData(pWidgets + offset + 1, (TypeInfoStruct*)pMember->pType);
            offset += pWidgets[offset].internalWidgetsCount;
            offset++;
        }
        else
        {
            pWidgets[offset].mType = widgetType;
            offset++;
        }
    }
    return offset;
}

TFReflectedWidgetData uiAddReflectedWidgetData(TypeInfoStruct* typeType, void* data)
{
    UNREF_PARAM(data);
    TFReflectedWidgetData result;
    memset(&result, 0, sizeof(TFReflectedWidgetData));

    // We need to do one loop first to find out how many widgets we need.
    measureReflectedWidgetDataRecursive(&result, typeType);

    result.pWidgets = (TFWidgetData*)tf_malloc(result.mWidgetCount * sizeof(TFWidgetData));
    initReflectedWidgetData(result.pWidgets, typeType);
    return result;
}

void uiRemoveReflectedWidgetData(TFReflectedWidgetData* result)
{
    if (result->pWidgets)
        tf_free(result->pWidgets);

    result->pWidgets = NULL;
}

int32_t uiFindReflectedWidgetDataByOffsetImpl(TFWidgetData* widgets, uint32_t widgetIdx, uint32_t endIdx, size_t offset)
{
    for (uint32_t i = widgetIdx; i < endIdx; i++)
    {
        TypeInfoStructMember* pMember = widgets[i].pMember;
        if (widgets[i].mType != TF_WIDGET_TYPE_COLLAPSING_HEADER && pMember->mOffsetInStruct == offset)
        {
            return i;
        }
        else if (widgets[i].mType == TF_WIDGET_TYPE_COLLAPSING_HEADER && offset >= pMember->mOffsetInStruct)
        {
            int32_t subIndex = uiFindReflectedWidgetDataByOffsetImpl(widgets, i + 1, i + widgets[i].internalWidgetsCount + 1,
                                                                     offset - pMember->mOffsetInStruct);
            if (subIndex != -1)
            {
                return subIndex;
            }
        }
    }
    return -1;
}

TFWidgetData* uiFindReflectedWidgetDataByOffset(TFReflectedWidgetData* data, size_t offset)
{
    int32_t idx = uiFindReflectedWidgetDataByOffsetImpl(data->pWidgets, 0, data->mWidgetCount, offset);
    if (idx != -1)
    {
        return data->pWidgets + idx;
    }
    else
    {
        return NULL;
    }
}

static bool IsAttributeAllowed(const char* pAttributeName, const char** ppForbiddenAttributes)
{
    uint32_t i = 0;
    while (ppForbiddenAttributes[i] != NULL)
    {
        if (!strcmp(pAttributeName, ppForbiddenAttributes[i]))
            return false;
        i++;
    }
    return true;
}

#define MAKE_TEXT_WIDGET(Literal, Name)                                      \
    {                                                                        \
        static bstring  text = {};                                           \
        static bstring* pText = NULL;                                        \
        static float4   color = { 1.0f, 1.0f, 1.0f, 1.0f };                  \
        if (!pText)                                                          \
        {                                                                    \
            text = bconstfromcstr(Literal);                                  \
            pText = &text;                                                   \
        }                                                                    \
        uiDynamicText(pText, color, TF_TEXT_MODE_WRAPPED, TF_ALIGN_WRAPPED); \
        *isWidgetChanged = false;                                            \
    }

void uiUpdateBasicWidget(TFWidgetData* pWidgetData, void* structData, bool* isWidgetChanged, bool* isWidgetHovered)
{
    TypeInfoStructMember* pMember = pWidgetData->pMember;
    static char           labelBuf[256] = {};

    if (pMember->mFlags & REFLECT_MEMBER_FLAG_HIDDEN_NAME)
    {
        labelBuf[0] = '\0';
    }
    else if (pMember->pDisplayName || pMember->mFlags & REFLECT_MEMBER_FLAG_DISPLAY_NAME_LITERAL)
    {
        strcpy(labelBuf, pMember->pDisplayName ? pMember->pDisplayName : pMember->pName);
    }
    else
    {
        // Create display name from member name
        uint32_t nameLen = (uint32_t)strlen(pMember->pName);
        bool     hasPrefix = nameLen >= 2 && (pMember->pName[0] >= 'a' && pMember->pName[1] <= 'Z');
        uint32_t outIndex = 0;
        bool     wasUppercase = false, wasNumeric = false;
        for (uint32_t j = hasPrefix ? 1 : 0; j < nameLen; j++)
        {
            char inChar = pMember->pName[j];

            bool uppercase = inChar <= 'Z' && inChar >= 'A';
            bool numeric = inChar <= '9' && inChar >= '0';

            if (outIndex > 0 && ((!wasUppercase && uppercase) || (!wasNumeric && numeric)))
            {
                labelBuf[outIndex++] = ' ';
            }
            labelBuf[outIndex++] = inChar;

            wasUppercase = uppercase;
            wasNumeric = numeric;
        }
        labelBuf[outIndex] = 0;
    }

    bool useLabel = strlen(labelBuf) > 0;

    if (pWidgetData->mType == TF_WIDGET_TYPE_SLIDER_FLOAT)
    {
        float min, max;
        getMemberMinMax(*pMember, &min, &max);

        if (useLabel)
        {
            uiLayoutAutoTextRows(2);
            if (uiIsNextWidgetHovered())
                *isWidgetHovered = true;
            uiLabel(labelBuf, TF_ALIGN_LEFT);
        }
        else
        {
            uiLayoutAutoTextRows(1);
        }
        *isWidgetChanged = UI_WIDGET_IS_CHANGED(uiSliderFloat((float*)structData, min, max, pMember->mStepRate));
    }
    else if (pWidgetData->mType == TF_WIDGET_TYPE_DROPDOWN ||
             (pWidgetData->mType == TF_WIDGET_TYPE_SLIDER_INT && pMember->pEnumType != NULL))
    {
        TypeInfoEnum* enumType = pWidgetData->mType == TF_WIDGET_TYPE_DROPDOWN ? (TypeInfoEnum*)pMember->pType : pMember->pEnumType;

        if (useLabel)
        {
            uiLayoutAutoTextRows(2);
            if (uiIsNextWidgetHovered())
                *isWidgetHovered = true;
            uiLabel(labelBuf, TF_ALIGN_LEFT);
        }
        else
        {
            uiLayoutAutoTextRows(1);
        }
        int* v = (int*)structData;
        int  selected = *v;
        selected = UI_WIDGET_GET_SELECTED(uiDropdown(enumType->pDisplayNames, (int)enumType->mMemberCount, selected));
        *isWidgetChanged = selected != *v;
        *v = selected;
    }
    else if (pWidgetData->mType == TF_WIDGET_TYPE_SLIDER_INT)
    {
        if (pMember->pType->mSize != 4)
        {
            MAKE_TEXT_WIDGET("<Integer bit width was not 32, not supported>", pMember->pType->pName);
        }
        else
        {
            int32_t min, max;
            getMemberMinMax(*pMember, &min, &max);

            if (useLabel)
            {
                uiLayoutAutoTextRows(2);
                if (uiIsNextWidgetHovered())
                    *isWidgetHovered = true;
                uiLabel(labelBuf, TF_ALIGN_LEFT);
            }
            else
            {
                uiLayoutAutoTextRows(1);
            }
            *isWidgetChanged = UI_WIDGET_IS_CHANGED(uiSliderInt((int32_t*)structData, min, max, 1));
        }
    }
    else if (pWidgetData->mType == TF_WIDGET_TYPE_CHECKBOX)
    {
        uiLayoutAutoTextRows(1);
        if (uiIsNextWidgetHovered())
            *isWidgetHovered = true;
        *isWidgetChanged = UI_WIDGET_IS_CHANGED(uiCheckbox(labelBuf, (bool*)structData));
    }
    else if (pWidgetData->mType == TF_WIDGET_TYPE_SLIDER_FLOAT2)
    {
        float2 min, max;
        getMemberMinMax(*pMember, &min, &max);

        if (useLabel)
        {
            uiLayoutAutoTextRows(5);
            if (uiIsNextWidgetHovered())
                *isWidgetHovered = true;
            uiLabel(labelBuf, TF_ALIGN_LEFT);
        }
        else
        {
            uiLayoutAutoTextRows(4);
        }
        float2* pMemberFloat2 = (float2*)structData;
        bool    changed = false;

        changed = UI_WIDGET_IS_CHANGED(uiSliderCursorFloat(&pMemberFloat2->x, min.x, max.x, pMember->mStepRate));
        changed |= UI_WIDGET_IS_CHANGED(uiSliderFloat(&pMemberFloat2->x, min.x, max.x, pMember->mStepRate));
        changed |= UI_WIDGET_IS_CHANGED(uiSliderCursorFloat(&pMemberFloat2->y, min.y, max.y, pMember->mStepRate));
        changed |= UI_WIDGET_IS_CHANGED(uiSliderFloat(&pMemberFloat2->y, min.y, max.y, pMember->mStepRate));
        *isWidgetChanged = changed;
    }
    else if (pWidgetData->mType == TF_WIDGET_TYPE_SLIDER_FLOAT3)
    {
        float3 min, max;
        getMemberMinMax(*pMember, &min, &max);

        uiLayoutAutoTextRows(4);
        if (uiIsNextWidgetHovered())
            *isWidgetHovered = true;
        uiLabel(labelBuf, TF_ALIGN_LEFT);
        *isWidgetChanged = UI_WIDGET_IS_CHANGED(uiSliderFloat3((float3*)structData, min, max, float3(pMember->mStepRate)));
    }
    else if (pWidgetData->mType == TF_WIDGET_TYPE_COLOR3_BUTTON)
    {
        if (useLabel)
        {
            uiLayoutAutoTextRows(2);
            if (uiIsNextWidgetHovered())
                *isWidgetHovered = true;
            uiLabel(labelBuf, TF_ALIGN_LEFT);
        }
        else
        {
            uiLayoutAutoTextRows(1);
        }
        *isWidgetChanged = UI_WIDGET_IS_CHANGED(uiColor3Button((float3*)structData));
    }
    else if (pWidgetData->mType == TF_WIDGET_TYPE_SLIDER_FLOAT4)
    {
        float4 min, max;
        getMemberMinMax(*pMember, &min, &max);

        if (useLabel)
        {
            uiLayoutAutoTextRows(5);
            uiLabel(labelBuf, TF_ALIGN_LEFT);
            if (uiIsNextWidgetHovered())
                *isWidgetHovered = true;
        }
        else
        {
            uiLayoutAutoTextRows(4);
        }
        *isWidgetChanged = UI_WIDGET_IS_CHANGED(uiSliderFloat4((float4*)structData, min, max, float4(pMember->mStepRate)));
    }
    else if (pWidgetData->mType == TF_WIDGET_TYPE_COLOR_BUTTON)
    {
        if (useLabel)
        {
            uiLayoutAutoTextRows(2);
            if (uiIsNextWidgetHovered())
                *isWidgetHovered = true;
            uiLabel(labelBuf, TF_ALIGN_LEFT);
        }
        else
        {
            uiLayoutAutoTextRows(1);
        }
        *isWidgetChanged = UI_WIDGET_IS_CHANGED(uiColorButton((float4*)structData));
    }
    else if (pWidgetData->mType == TF_WIDGET_TYPE_GRADIENT_RGBA32)
    {
        TFGradientType* gradientReflection = (TFGradientType*)structData;

        TFUIGradientColor uiGradient{};
        uiGradient.mStartKeyValue = 0.0f;
        uiGradient.mEndKeyValue = 1.0f;
        uiGradient.mFixedEndings = true;
        uiGradient.mMaxKeyCount = MAX_GRADIENT_KEYFRAME_COUNT;
        uiGradient.pAllocatedKeyCount = &gradientReflection->mAllocatedKeyCount;
        uiGradient.pSelectedKeyIdx = &gradientReflection->mSelectedKeyIdx;
        uiGradient.pKeyColors = gradientReflection->mKeyColors;
        uiGradient.pKeyValues = gradientReflection->mKeyValues;

        if (useLabel)
        {
            uiLayoutAutoTextRows(2);
            if (uiIsNextWidgetHovered())
                *isWidgetHovered = true;
            uiLabel(labelBuf, TF_ALIGN_LEFT);
        }
        else
        {
            uiLayoutAutoTextRows(1);
        }
        *isWidgetChanged = UI_WIDGET_IS_CHANGED(uiGradientColor4Button(&uiGradient));
    }
}

static void updateReflectedWidgetsImpl(TFWidgetData* widgetDatas, uint32_t startIdx, uint32_t endIdx, void* structData,
                                       const char** ppForbiddenAttributes)
{
    for (uint32_t i = startIdx; i < endIdx; i++)
    {
        TFWidgetData*         pData = widgetDatas + i;
        TypeInfoStructMember* pMember = pData->pMember;
        bool                  allowed = ppForbiddenAttributes == NULL || IsAttributeAllowed(pMember->pName, ppForbiddenAttributes);

        if ((!allowed && strcmp(pMember->pType->pName, "float2") != 0 && strcmp(pMember->pType->pName, "float3") &&
             strcmp(pMember->pType->pName, "float4")) ||
            !pData->mActive)
        {
            if (pData->mType == TF_WIDGET_TYPE_COLLAPSING_HEADER)
            {
                i += pData->internalWidgetsCount;
            }
            continue;
        }

        bool* isWidgetChanged = &pData->mChanged;
        bool  isWidgetHovered = false;

        if (!pMember->pType)
        {
            uiLayoutAutoTextRows(1);
            MAKE_TEXT_WIDGET("<Unreflected member type>", member.pName);
        }
        else
        {
            void* pStructData = ((uint8_t*)structData + pMember->mOffsetInStruct);
            if (pData->mType == TF_WIDGET_TYPE_COLLAPSING_HEADER)
            {
                if (UI_WIDGET_IS_VISIBLE(uiCollapsingHeaderBeginWithState(pMember->pName, &pData->mIsCollapsed)))
                {
                    updateReflectedWidgetsImpl(widgetDatas, i + 1, i + pData->internalWidgetsCount + 1, pStructData, ppForbiddenAttributes);
                    uiCollapsingHeaderEnd();
                }
                i += pData->internalWidgetsCount;
            }
            else
            {
                uiUpdateBasicWidget(pData, pStructData, isWidgetChanged, &isWidgetHovered);
            }
        }

        if (pMember->pDescription != NULL)
        {
            if (isWidgetHovered)
            {
                uiTooltipText((const char*)pMember->pDescription);
            }
        }

#undef MAKE_TEXT_WIDGET
    }
}

// ppForbiddenAttributes - last data must be NULL
void uiUpdateReflectedWidgets(TFReflectedWidgetData* result, TypeInfoStruct* structType, void* structData,
                              const char** ppForbiddenAttributes)
{
    UNREF_PARAM(structType);

    for (uint32_t i = 0; i < result->mWidgetCount; i++)
    {
        result->pWidgets[i].mChanged = false;
    }
    result->mInternalCurrWidgetIndex = 0;
    updateReflectedWidgetsImpl(result->pWidgets, 0, result->mWidgetCount, structData, ppForbiddenAttributes);
}

float uiCalculateHeightReflectedWidgets(uint32_t widgetCount)
{
    if (widgetCount == 0)
    {
        return 0.0f;
    }
    else
    {
        return uiGetDefaultRowPadding() * (widgetCount - 1) + uiCalculateHeightOfTextRows(DEFAULT_ROW_HEIGHT) * widgetCount;
    }
}

#endif
