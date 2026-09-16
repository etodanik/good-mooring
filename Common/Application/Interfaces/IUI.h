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

#ifndef IUI_H
#define IUI_H

#include "../../Application/Config.h"

// SCRIPTED TESTING :
// For now, if a script file with the name "Test.lua", exist in the script directory, will run once an execution.
// Lua function name resolution:
// - UI Widget "label"s will be included in the name
//		- For Widget events: label name + "Event Name". e.g., Lua Function name for label - "Press", event - OnEdited : "PressOnEdited"
//		- For Widget modifier ints/floats: "Set" and "Get" function set will be added as a prefix to label name.
//											e.g., "X" variable will have "SetX" and "GetX" pair of functions
// To add global Lua functions, independent of Unit Tests, add definition in UIApp::Init (Check LOGINFO there for example).

#include "../../Utilities/ThirdParty/OpenSource/Nothings/stb_ds.h"
#include "../../Utilities/ThirdParty/OpenSource/bstrlib/bstrlib.h"

#include "../../Utilities/Interfaces/IMath.h"
#include "../../Utilities/Interfaces/INodeGraph.h"
#include "../../Utilities/Reflection/Serialization.h"

typedef struct TFRenderer      TFRenderer;
typedef struct TFCmd           TFCmd;
typedef struct TFRenderTarget  TFRenderTarget;
typedef struct TFSwapChain     TFSwapChain;
typedef struct TFPipelineCache TFPipelineCache;
typedef struct TypeInfoStruct  TypeInfoStruct;
typedef struct TFTexture       TFTexture;
typedef struct TFFont          TFFont;

#define MAX_LABEL_STR_LENGTH              128
#define MAX_FORMAT_STR_LENGTH             30
#define MAX_TITLE_STR_LENGTH              128
#define COMPACT_UI_LABEL_WIDTH_PERCENTAGE 40
#define COMPACT_UI_SLIDER_CURSOR_SIZE     5.0f
#define DEFAULT_ROW_HEIGHT                25.0f
#define DEFAULT_UI_FONT_SIZE              14.0f

/****************************************************************************/
// MARK: - Forge UI Widget API
/****************************************************************************/
typedef struct TFUIWidget TFUIWidget;
typedef void*             TFUIWindowHandle;

enum TFUIWidgetType
{
    TF_WIDGET_TYPE_NONE,
    TF_WIDGET_TYPE_COLLAPSING_HEADER,
    TF_WIDGET_TYPE_COLLAPSING_SELECTABLE_HEADER,
    TF_WIDGET_TYPE_SELECTABLE_LABEL,
    TF_WIDGET_TYPE_DEBUG_TEXTURES,
    TF_WIDGET_TYPE_LABEL,
    TF_WIDGET_TYPE_COLOR_LABEL,
    TF_WIDGET_TYPE_SEPARATOR,
    TF_WIDGET_TYPE_VERTICAL_SEPARATOR,
    TF_WIDGET_TYPE_BUTTON,
    TF_WIDGET_TYPE_BUTTON_IMAGE,
    TF_WIDGET_TYPE_SLIDER_FLOAT,
    TF_WIDGET_TYPE_SLIDER_FLOAT2,
    TF_WIDGET_TYPE_SLIDER_FLOAT3,
    TF_WIDGET_TYPE_SLIDER_FLOAT4,
    TF_WIDGET_TYPE_PROPERTY_INT,
    TF_WIDGET_TYPE_SLIDER_INT,
    TF_WIDGET_TYPE_SLIDER_UINT,
    TF_WIDGET_TYPE_RADIO_BUTTON,
    TF_WIDGET_TYPE_CHECKBOX,
    TF_WIDGET_TYPE_CHECKBOX_IMAGE,
    TF_WIDGET_TYPE_DROPDOWN,
    TF_WIDGET_TYPE_COLUMN,
    TF_WIDGET_TYPE_PROGRESS_BAR,
    TF_WIDGET_TYPE_COLOR_RGBA8_SLIDER,
    TF_WIDGET_TYPE_COLOR_RGBA32_SLIDER,
    TF_WIDGET_TYPE_GRADIENT_RGB32,
    TF_WIDGET_TYPE_GRADIENT_RGBA32,
    TF_WIDGET_TYPE_HISTOGRAM,
    TF_WIDGET_TYPE_PLOT_LINES,
    TF_WIDGET_TYPE_COLOR_PICKER,
    TF_WIDGET_TYPE_COLOR3_PICKER,
    TF_WIDGET_TYPE_COLOR_BUTTON,
    TF_WIDGET_TYPE_COLOR3_BUTTON,
    TF_WIDGET_TYPE_TEXTBOX,
    TF_WIDGET_TYPE_DYNAMIC_TEXT,
    TF_WIDGET_TYPE_FILLED_RECT,
    TF_WIDGET_TYPE_TOOLTIP,
    TF_WIDGET_TYPE_LINE,
    TF_WIDGET_TYPE_CURVE,
    TF_WIDGET_TYPE_CUSTOM_CHART
};

enum TFUIWidgetState
{
    TF_WIDGET_STATE_MODIFIED = 1 << 1,
    TF_WIDGET_STATE_INACTIVE = 1 << 2,                                             // widget is neither active nor hovered
    TF_WIDGET_STATE_ENTERED = 1 << 3,                                              // widget has been hovered on the current frame
    TF_WIDGET_STATE_HOVER = 1 << 4,                                                // widget is being hovered
    TF_WIDGET_STATE_ACTIVATED = 1 << 5,                                            // widget is currently activated
    TF_WIDGET_STATE_LEFT = 1 << 6,                                                 // widget is not hovered anymore
    TF_WIDGET_STATE_HOVERED = TF_WIDGET_STATE_HOVER | TF_WIDGET_STATE_MODIFIED,    // widget is being hovered
    TF_WIDGET_STATE_ACTIVE = TF_WIDGET_STATE_ACTIVATED | TF_WIDGET_STATE_MODIFIED, // widget is currently activated
};

enum TFUIStyleColor
{
    TF_UI_COLOR_TEXT,
    TF_UI_COLOR_SELECT_NORMAL,
    TF_UI_COLOR_SELECT_HOVER,
    TF_UI_COLOR_SELECT_PRESSED,
    TF_UI_COLOR_SELECT_NORMAL_ACTIVE,
    TF_UI_COLOR_SELECT_HOVER_ACTIVE,
    TF_UI_COLOR_SELECT_PRESSED_ACTIVE,
    TF_UI_COLOR_COUNT
};

enum TFUISymbolType
{
    TF_UI_SYMBOL_NONE,
    TF_UI_SYMBOL_X,
    TF_UI_SYMBOL_UNDERSCORE,
    TF_UI_SYMBOL_CIRCLE_SOLID,
    TF_UI_SYMBOL_CIRCLE_OUTLINE,
    TF_UI_SYMBOL_RECT_SOLID,
    TF_UI_SYMBOL_RECT_OUTLINE,
    TF_UI_SYMBOL_TRIANGLE_UP,
    TF_UI_SYMBOL_TRIANGLE_DOWN,
    TF_UI_SYMBOL_TRIANGLE_LEFT,
    TF_UI_SYMBOL_TRIANGLE_RIGHT,
    TF_UI_SYMBOL_PLUS,
    TF_UI_SYMBOL_MINUS,
    TF_UI_SYMBOL_TRIANGLE_UP_OUTLINE,
    TF_UI_SYMBOL_TRIANGLE_DOWN_OUTLINE,
    TF_UI_SYMBOL_TRIANGLE_LEFT_OUTLINE,
    TF_UI_SYMBOL_TRIANGLE_RIGHT_OUTLINE,
    TF_UI_SYMBOL_MAX
};

typedef uint32_t TFUIWidgetId;

typedef enum TFUIWidgetEditFilter
{
    TF_WIDGET_EDIT_FILTER_NONE = 0,
    TF_WIDGET_EDIT_FILTER_ASCII,
    TF_WIDGET_EDIT_FILTER_FLOAT,
    TF_WIDGET_EDIT_FILTER_DECIMAL,
    TF_WIDGET_EDIT_FILTER_HEX,
    TF_WIDGET_EDIT_FILTER_OCT,
    TF_WIDGET_EDIT_FILTER_BINARY
} TFUIWidgetEditFilter;

typedef enum TFUIWidgetEditEvent
{
    TF_WIDGET_EDIT_EVENT_ACTIVE = 0,  // edit widget is currently being modified
    TF_WIDGET_EDIT_EVENT_INACTIVE,    // edit widget is not active and is not being modified
    TF_WIDGET_EDIT_EVENT_ACTIVATED,   // edit widget went from state inactive to state active
    TF_WIDGET_EDIT_EVENT_DEACTIVATED, // edit widget went from state active to state inactive
    TF_WIDGET_EDIT_EVENT_COMMITED     // edit widget has recieved an enter and lost focus
} TFUIWidgetEditEvent;
typedef uint32_t TFUIWidgetEditEvents;

typedef enum TFUIWidgetChartEvent
{
    TF_WIDGET_CHART_EVENT_HOVERED = 1 << 0,
    TF_WIDGET_CHART_EVENT_CLICKED = 1 << 1
} TFUIWidgetChartEvent;
typedef uint32_t TFUIWidgetChartEvents;

typedef enum TFUIWidgetInteractionType
{
    TF_WIDGET_INTERACTION_UNUSED,
    TF_WIDGET_INTERACTION_VISIBLE,
    TF_WIDGET_INTERACTION_CHANGED,
    TF_WIDGET_INTERACTION_PRESSED,
    TF_WIDGET_INTERACTION_SELECTED,
    TF_WIDGET_INTERACTION_EDIT_EVENTS,
    TF_WIDGET_INTERACTION_CHART_EVENTS
} TFUIWidgetInteractionType;

typedef struct TFUIWidgetInteraction
{
    TFUIWidgetId              id;
    TFUIWidgetInteractionType type;
    union
    {
        bool                  visible;
        bool                  changed;
        bool                  pressed;
        int                   selected;
        TFUIWidgetEditEvents  editEvents;
        TFUIWidgetChartEvents chartEvents;
    };
} TFUIWidgetInteraction;

typedef void (*UICallback)(void* pUserData);

typedef enum TFUIContextMenuItemType
{
    TF_UI_CONTEXT_MENU_ITEM = 0,
    TF_UI_CONTEXT_MENU_TAB = 1,
} TFUIContextMenuItemType;

typedef struct TFUIContextMenuItem
{
    const char*             pName;
    TFUIContextMenuItemType mType;
    union
    {
        struct
        {
            UICallback pOnSelect;
            void*      pOnSelectUserData;
        } mItemData;

        struct
        {
            uint32_t             mMemberCount;
            TFUIContextMenuItem* pMembers;
        } mTabData;
    };

} TFUIContextMenuItem;

#ifndef MAX_GRADIENT_KEYFRAME_COUNT
#define MAX_GRADIENT_KEYFRAME_COUNT 16
#endif

typedef struct TFGradientType
{
    float4   mKeyColors[MAX_GRADIENT_KEYFRAME_COUNT];
    float    mKeyValues[MAX_GRADIENT_KEYFRAME_COUNT];
    uint32_t mMaxKeyCount;
    uint32_t mAllocatedKeyCount;
    int32_t  mSelectedKeyIdx;
} TFGradientType;

typedef struct TFUIGradientColor
{
    float4*   pKeyColors;
    float*    pKeyValues;
    uint32_t* pAllocatedKeyCount;
    int32_t*  pSelectedKeyIdx;
    uint32_t  mMaxKeyCount;
    float     mStartKeyValue;
    float     mEndKeyValue;
    bool      mFixedEndings;
} TFUIGradientColor;

typedef enum TFUIMessageBoxButton:
    uint32_t
{
    TF_UI_MESSAGE_BOX_BUTTON_OK = 1,
    TF_UI_MESSAGE_BOX_BUTTON_YES = 2,
    TF_UI_MESSAGE_BOX_BUTTON_NO = 4,
    TF_UI_MESSAGE_BOX_BUTTON_CLOSE = 8,

    TF_UI_MESSAGE_BOX_BUTTON_OK_IDX = 0,
    TF_UI_MESSAGE_BOX_BUTTON_YES_IDX = 1,
    TF_UI_MESSAGE_BOX_BUTTON_NO_IDX = 2,
    TF_UI_MESSAGE_BOX_BUTTON_CLOSE_IDX = 3,

    TF_UI_MESSAGE_BOX_BUTTON_COUNT = 4
} TFUIMessageBoxButton;

typedef void (*MessageBoxCallback)(void* pUserData);

typedef struct TFUIMessageBoxInfo
{
    MessageBoxCallback mCallbacks[TF_UI_MESSAGE_BOX_BUTTON_COUNT];
    void*              pUserDatas[TF_UI_MESSAGE_BOX_BUTTON_COUNT];

    const char* pTitle;
    const char* pText;
    uint32_t    mFlags;
} TFUIMessageBoxInfo;

typedef struct TFUINodeGraphEditor TFUINodeGraphEditor;

typedef struct TFNodeDataUI
{
    float  mInputPortsNameOffset[NODE_GRAPH_PORT_MAX];
    float2 mSize;
    float  mInputPortStep;
    float  mOutputPortStep;
    float  mPortHeight;
} TFNodeDataUI;

typedef TFUIWidgetInteraction (*UIDrawCustomNodeCallback)(TFNGNode* pNode, TFNodeDataUI uiData);

typedef struct TFUINodeCustomWidgets
{
    float2                   mRectSize;
    UIDrawCustomNodeCallback pDrawCustomNode;
} TFUINodeCustomWidgets;

typedef enum TFUINodeDescFlags
{
    TF_UI_NODE_DESC_FLAG_NONE = 0,
    TF_UI_NODE_DESC_FLAG_NO_REMOVABLE = 1,
    TF_UI_NODE_DESC_FLAG_HIDDEN_CONTEXT_ITEM = 2,

    // only when custom ui is used for node
    TF_UI_NODE_DESC_FLAG_DRAW_PORT_REFLECTED = 4,
    TF_UI_NODE_DESC_FLAG_DRAW_OTHER_REFLECTION = 8,
} TFUINodeDescFlags;

/// Macros for more concise definition of UI code with result type checking.
/// "interaction" should be the full call to one of the widget functions that return TFUIWidgetInteraction.
/// example: if ( UI_WIDGET_IS_PRESSED( uiButton( label ) ) ) { ... }
FORGE_API TFUIWidgetInteraction uiWidgetAssertType(TFUIWidgetInteraction result, TFUIWidgetInteractionType type);
#define UI_WIDGET_IS_VISIBLE(interaction)      uiWidgetAssertType(interaction, TF_WIDGET_INTERACTION_VISIBLE).visible == true
#define UI_WIDGET_IS_CHANGED(interaction)      uiWidgetAssertType(interaction, TF_WIDGET_INTERACTION_CHANGED).changed == true
#define UI_WIDGET_IS_PRESSED(interaction)      uiWidgetAssertType(interaction, TF_WIDGET_INTERACTION_PRESSED).pressed == true
#define UI_WIDGET_GET_SELECTED(interaction)    uiWidgetAssertType(interaction, TF_WIDGET_INTERACTION_SELECTED).selected
#define UI_WIDGET_GET_EDIT_EVENTS(interaction) uiWidgetAssertType(interaction, TF_WIDGET_INTERACTION_EDIT_EVENTS).editEvents
#define UI_WIDGET_IS_CHART_POINT_CLICKED(interaction) \
    uiWidgetAssertType(interaction, TF_WIDGET_INTERACTION_CHART_EVENTS).chartEvents& TF_WIDGET_CHART_EVENT_CLICKED
#define UI_WIDGET_IS_CHART_POINT_HOVERED(interaction) \
    uiWidgetAssertType(interaction, TF_WIDGET_INTERACTION_CHART_EVENTS).chartEvents& TF_WIDGET_CHART_EVENT_HOVERED

// Widgets: Text
typedef enum TFUIAlignmentText
{
    TF_ALIGN_LEFT,
    TF_ALIGN_CENTER,
    TF_ALIGN_RIGHT,
    TF_ALIGN_WRAPPED
} TFUIAlignmentText;

typedef enum TFUITextMode
{
    TF_TEXT_MODE_ALIGNED,
    TF_TEXT_MODE_WRAPPED
} TFUITextMode;

void uiText(const bstring* text, TFUIAlignmentText alignment);

// Widgets: Main
// Disable unused return value PVS warnings, since using TFUIWidgetInteraction is optional
//-V::1071
FORGE_API TFUIWidgetInteraction uiCollapsingHeaderBegin(const char* label, bool initCollapsed);
FORGE_API TFUIWidgetInteraction uiCollapsingHeaderSelectedBegin(const char* label, bool initCollapsed, bool* selected);
FORGE_API TFUIWidgetInteraction uiCollapsingHeaderBeginWithState(const char* label, bool* expanded);
FORGE_API TFUIWidgetInteraction uiCollapsingHeaderSelectedBeginWithState(const char* label, bool* expanded, bool* selected);

FORGE_API void                  uiCollapsingHeaderEnd();
FORGE_API TFUIWidgetInteraction uiSelectableLabelBegin(const char* label, bool* selected);
FORGE_API TFUIWidgetInteraction uiDebugTexture(const TFTexture* handle, float2 textureDisplaySize);
// Draw an image and report a click in normalized image coordinates.
FORGE_API bool uiDebugTexturePick(const TFTexture* handle, float2* pickedUV, const float2* selectedUV = nullptr);
FORGE_API TFUIWidgetInteraction uiLabel(const char* label, TFUIAlignmentText alignment);
FORGE_API TFUIWidgetInteraction uiColorLabel(const char* label, TFUIAlignmentText alignment, float4 color);
FORGE_API TFUIWidgetInteraction uiSeparator(float4 color, float thickness);
FORGE_API TFUIWidgetInteraction uiVerticalSeparator(float4 color, float thickness);
FORGE_API TFUIWidgetInteraction uiSeparatorStyled(float thickness);
FORGE_API TFUIWidgetInteraction uiVerticalSeparatorStyled(float thickness);
FORGE_API TFUIWidgetInteraction uiButton(const char* label);
FORGE_API TFUIWidgetInteraction uiButtonColored(const char* label, float4 background);
FORGE_API TFUIWidgetInteraction uiButtonImage(const char* label, const TFTexture* handle, uint2 size, uint4 rect);
FORGE_API TFUIWidgetInteraction uiButtonSymbol(TFUISymbolType type);
FORGE_API TFUIWidgetInteraction uiSliderCursorFloat(float* val, float min, float max, float step);
FORGE_API TFUIWidgetInteraction uiSliderFloat(float* val, float min, float max, float step);
FORGE_API TFUIWidgetInteraction uiSliderFloat2(float2* val, float2 min, float2 max, float2 step);
FORGE_API TFUIWidgetInteraction uiSliderFloat3(float3* val, float3 min, float3 max, float3 step);
FORGE_API TFUIWidgetInteraction uiSliderFloat4(float4* val, float4 min, float4 max, float4 step);
FORGE_API TFUIWidgetInteraction uiPropertyInt(int32_t* val, int32_t min, int32_t max, int32_t step);
FORGE_API TFUIWidgetInteraction uiPropertyFloat(float* value, float minimum, float maximum, float step);
FORGE_API TFUIWidgetInteraction uiSliderInt(int32_t* val, int32_t min, int32_t max, int32_t step);
FORGE_API TFUIWidgetInteraction uiSliderUint(uint32_t* val, uint32_t min, uint32_t max, uint32_t step);
FORGE_API TFUIWidgetInteraction uiRadioButton(const char* label, bool* active);
FORGE_API TFUIWidgetInteraction uiCheckbox(const char* label, bool* active);
FORGE_API TFUIWidgetInteraction uiCheckboxImage(const TFTexture* handle, uint2 size, uint4 rect, bool* active);
FORGE_API TFUIWidgetInteraction uiDropdown(const char* const* items, int count, int selected);
FORGE_API TFUIWidgetInteraction uiColumnBegin(const char* label, bool showLabel);
FORGE_API void                  uiColumnEnd();
FORGE_API TFUIWidgetInteraction uiProgressBar(size_t* current, size_t max, bool modifyable);
FORGE_API TFUIWidgetInteraction uiColorSliderRGBA8(uint32_t* color);
FORGE_API TFUIWidgetInteraction uiColorSliderRGBA32(float4* color);
FORGE_API TFUIWidgetInteraction uiHistogram(float start, float end, float binInterval, const float* values, size_t count);
FORGE_API TFUIWidgetInteraction uiPlotLines(const float* values, size_t count, float minValue, float maxValue, bool showPoints);
FORGE_API TFUIWidgetInteraction uiColorButton(float4* color);
FORGE_API TFUIWidgetInteraction uiColor3Button(float3* color);
FORGE_API TFUIWidgetInteraction uiGradientColor3Button(TFUIGradientColor* gradientData);
FORGE_API TFUIWidgetInteraction uiGradientColor4Button(TFUIGradientColor* gradientData);
FORGE_API float4                uiGetGradientColor(TFUIGradientColor* gradientData, float value);
FORGE_API void                  uiSortGradient(TFUIGradientColor* gradientData);
FORGE_API TFUIWidgetInteraction uiTextbox(const char* label, bstring* text, TFUIWidgetEditFilter textFilter, bool multiline = true);
FORGE_API TFUIWidgetInteraction uiDynamicText(bstring* text, float4 rgbaColor, TFUITextMode mode, TFUIAlignmentText alignment);
FORGE_API TFUIWidgetInteraction uiFilledRect(float4 rgbaColor, float rounding);
FORGE_API TFUIWidgetInteraction uiTooltipText(const char* text);
FORGE_API TFUIWidgetInteraction uiTooltipBegin(float tooltipWidth);
FORGE_API void                  uiTooltipEnd();
FORGE_API TFUIWidgetInteraction uiLine(vec2 p0, vec2 p1, float thickness, float4 rgbaColor);
FORGE_API TFUIWidgetInteraction uiCurve(vec2 p0, vec2 ctrl0, vec2 ctrl1, vec2 p1, float thickness, float4 rgbaColor);
FORGE_API void                  uiSpacer(uint32_t widgetCount = 1);
FORGE_API bool                  uiIsOpenContextMenu();
FORGE_API bool                  uiAddContextMenu(TFUIContextMenuItem* pItems, uint32_t countItems, float2 size, bool useScrollbar);
FORGE_API bool                  uiRemoveContextMenu();
FORGE_API void                  uiUpdateContextMenu();
FORGE_API void                  uiMessageBox(const TFUIMessageBoxInfo* pInfo);
FORGE_API void                  uiWinSpacer(uint32_t widgetCount = 1);

/****************************************************************************/
// MARK: - Simplified windows layout
/****************************************************************************/

FORGE_API void uiSetActiveSWL(bool enabled);
FORGE_API void uiSetAutoSwitchSWL(bool enabled);
FORGE_API bool uiIsActiveSWL();
FORGE_API void uiSetMaxWindowsSizeSWL(vec2 screenRatio);
FORGE_API vec2 uiGetMaxWindowsSizeSWL();

typedef enum TFUIWindowPositionSWL
{
    TF_UI_WINDOW_POSITION_SWL_LEFT,
    TF_UI_WINDOW_POSITION_SWL_RIGHT
} TFUIWindowPositionSWL;

FORGE_API void                  uiSetWindowPositionSWL(TFUIWindowPositionSWL position);
FORGE_API TFUIWindowPositionSWL uiGetWindowPositionSWL();

/****************************************************************************/
// MARK: - Node Graph Editor
/****************************************************************************/

FORGE_API TFUINodeGraphEditor*  uiAddNodeGraphEditor(TFNodeGraphContext* pNodeGraphCtx);
FORGE_API TFUIWidgetInteraction uiNodeGraphEditor(TFUINodeGraphEditor* pEditor);
FORGE_API void                  uiDrawEmptyNodeGraphEditor();
FORGE_API void                  uiSetPortTypeColor(TFUINodeGraphEditor* pEditor, float3 color, int32_t portType);
FORGE_API void uiSetDrawCustomNode(TFUINodeGraphEditor* pEditor, const TFUINodeCustomWidgets* pDrawCustomNode, int32_t nodeType);
FORGE_API void uiSetNodeDescFlag(TFUINodeGraphEditor* pEditor, int32_t nodeType, TFUINodeDescFlags flag, bool enable);
FORGE_API void uiRemoveNodeGraphEditor(TFUINodeGraphEditor* pEditor);

typedef enum TFUIChartType
{
    TF_UI_CHART_LINES,
    TF_UI_CHART_COLUMN
} TFUIChartType;
FORGE_API TFUIWidgetInteraction uiChartBegin(TFUIChartType type, int numPoints, float min, float max);
FORGE_API TFUIWidgetInteraction uiChartBeginColored(TFUIChartType type, float4 color, float4 pointHighlight, int numPoints, float min,
                                                    float max);
// Widgets from uiChartPush are not tracked, so do not use the returned id.
// To query the state of the point use UI_WIDGET_IS_CHART_POINT_### macro.
FORGE_API TFUIWidgetInteraction uiChartPush(float val);
FORGE_API void                  uiChartEnd();
// Set colors of the next chart points. Useful when unique points may have different colors.
FORGE_API void                  uiChartSetColor(float4 color, float4 pointHighlight);
FORGE_API void                  uiChartResetColor();

// Widget utilities
// query state of a specific widget by id
FORGE_API bool uiIsWidgetHovered(TFUIWidgetId id);
FORGE_API bool uiIsWidgetActive(TFUIWidgetId id);
FORGE_API bool uiIsWidgetFocused(TFUIWidgetId id);
FORGE_API bool uiIsWidgetClicked(TFUIWidgetId id);
FORGE_API bool uiIsWidgetVisible(TFUIWidgetId id);
FORGE_API bool uiIsWidgetEdited(TFUIWidgetId id);
FORGE_API bool uiIsWidgetActivated(TFUIWidgetId id);
FORGE_API bool uiIsWidgetDeactivated(TFUIWidgetId id);
FORGE_API bool uiIsWidgetDeactivatedAfterEdit(TFUIWidgetId id);

// Gets id of the last processed widget
FORGE_API TFUIWidgetId uiGetWidgetId();
FORGE_API TFUIWidget*  uiGetWidgetById(TFUIWidgetId id);
FORGE_API uint32_t     uiGetTextWidth(const char* cstr);

FORGE_API bool uiIsNextWidgetHovered();
FORGE_API bool uiIsNextWidgetActive();
FORGE_API bool uiIsNextWidgetFocused();
FORGE_API bool uiIsNextWidgetClicked();
FORGE_API bool uiIsNextWidgetVisible();
FORGE_API bool uiIsNextWidgetEdited();
FORGE_API bool uiIsNextWidgetActivated();
FORGE_API bool uiIsNextWidgetDeactivated();
FORGE_API bool uiIsNextWidgetDeactivatedAfterEdit();

// UI windows
// Act as the canvas and main persistent state
typedef uint32_t TFUIWindowId;
typedef uint32_t TFUIWindowFlags;

typedef struct TFUIWindowDesc
{
    const char*     pWindowTitle;
    vec2            mStartPos;
    vec2            mStartSize;
    TFUIWindowFlags mFlags;
} TFUIWindowDesc;

enum TFUIWindowFlag
{
    TF_UI_WINDOW_BORDER = 1 << 0,
    TF_UI_WINDOW_MOVABLE = 1 << 1,
    TF_UI_WINDOW_SCALABLE = 1 << 2,
    TF_UI_WINDOW_CLOSABLE = 1 << 3,
    TF_UI_WINDOW_MINIMIZABLE = 1 << 4,
    TF_UI_WINDOW_NO_SCROLLBAR = 1 << 5,
    TF_UI_WINDOW_TITLE = 1 << 6,
    TF_UI_WINDOW_SCROLL_AUTO_HIDE = 1 << 7,
    TF_UI_WINDOW_BACKGROUND = 1 << 8,
    TF_UI_WINDOW_SCALE_LEFT = 1 << 9,
    TF_UI_WINDOW_NO_INPUT = 1 << 10,
    TF_UI_WINDOW_INIT_MINIMIZED = 1 << 15,
    TF_UI_WINDOW_INIT_HEIGHT_FIT = 1 << 17,
    TF_UI_WINDOW_DISABLE_DPI_SCALE_APPLY = 1 << 18,
};

typedef enum TFUIPanelType
{
    TF_UI_PANEL_WINDOW,
    TF_UI_PANEL_GROUP,
    TF_UI_PANEL_MENU
    // UI_PANEL_POPUP
} TFUIPanelType;

typedef enum TFUIDpiScaleSettings
{
    TF_UI_DPI_SCALE_SETTINGS_NONE = 0,
    TF_UI_DPI_SCALE_SETTINGS_WINDOW_POS_APPLY = 1,
    TF_UI_DPI_SCALE_SETTINGS_WINDOW_SIZE_APPLY = 2,
} TFUIDpiScaleSettings;

typedef struct TFUIWindowInteraction
{
    TFUIWindowId  id;
    TFUIPanelType panelType;
    bool          fillable;
} TFUIWindowInteraction;

FORGE_API bool uiPanelAssertTypeGetFillable(TFUIWindowInteraction result, TFUIPanelType type);
#define UI_WINDOW_IS_VISIBLE(interaction) uiPanelAssertTypeGetFillable(interaction, TF_UI_PANEL_WINDOW) == true
#define UI_GROUP_IS_VISIBLE(interaction)  uiPanelAssertTypeGetFillable(interaction, TF_UI_PANEL_GROUP) == true

FORGE_API TFUIDpiScaleSettings uiGetDpiScaleSettings();
FORGE_API void                 uiSetDpiScaleSettings(TFUIDpiScaleSettings settings);
FORGE_API float                uiGetRoundScreenSize();
FORGE_API void                 uiSetRoundScreenSize(float roundScreenSize);

// An optional open flag enables the title-bar close button and receives its closed state.
FORGE_API TFUIWindowInteraction uiBeginWidgetWindow(const TFUIWindowDesc* pDesc, bool* open = nullptr);
FORGE_API void                  uiEndWidgetWindow();

// Widget groups
// Act as sub-windows used for complex widget layouting
FORGE_API TFUIWindowInteraction uiBeginWidgetGroup(const char* title, TFUIWindowFlags flags);
FORGE_API void                  uiEndWidgetGroup();

//
FORGE_API float uiCalculateHeightOfTextRows(float rowHeight);
FORGE_API float uiGetCurrentWindowHeight();

//
FORGE_API float uiCalculateHeightOfTextRows(float rowHeight);
FORGE_API float uiGetCurrentWindowHeight();

// Menu bar
FORGE_API void                  uiBeginMenuBar();
FORGE_API TFUIWidgetInteraction uiBeginMenuBarLabel(const char* title, uint32_t labelWidth, uint32_t dropdownWidth);
FORGE_API TFUIWidgetInteraction uiMenuBarItem(const char* title);
FORGE_API TFUIWidgetInteraction uiButtonMenuBarLabel(const char* title, uint32_t labelWidth);
FORGE_API void                  uiEndMenuBarLabel();
FORGE_API void                  uiEndMenuBar();

// Tab bar
FORGE_API void                  uiBeginTabBar(uint32_t maxTabsInRow);
FORGE_API TFUIWidgetInteraction uiTab(const char* title, uint32_t labelWidth, int32_t* pSelectedTab, bool* pActive);
FORGE_API void                  uiEndTabBar();

FORGE_API void                  uiStaticBeginTabBar(uint32_t tabCount, int32_t* pSelectedTab);
FORGE_API TFUIWidgetInteraction uiStaticTabRoundScreen(const char* title, uint32_t labelWidth, uint32_t roundSize, int32_t* pSelectedTab);
FORGE_API TFUIWidgetInteraction uiStaticTab(const char* title, uint32_t labelWidth, int32_t* pSelectedTab);
FORGE_API void                  uiStaticEndTabBar();

FORGE_API int32_t uiGetTabRowCount();

//
// Window utilities
FORGE_API TFUIWindowHandle uiFindWindowByTitle(const char* title);
FORGE_API TFUIWindowHandle uiGetCurrentWindow();

// Widget disabe
FORGE_API void uiBeginWidgetDisable();
FORGE_API void uiEndWidgetDisable();

// queries for last window widget in the stack
// maybe we can get away with using uiIsWidget### functions for windows, although both Imgui and Nuklear differentiate between them
// FORGE_API bool uiIsWindowHidden();
// FORGE_API bool uiIsWindowCollapsed();
// FORGE_API bool uiIsWindowFocused();
// FORGE_API bool uiIsWindowHovered();

FORGE_API vec2 uiGetWindowPos();
FORGE_API vec2 uiGetWindowSize();
FORGE_API vec2 uiGetWindowWidth();
FORGE_API vec2 uiGetWindowHeight();

// setting functions for last window widget, e.g. ...
FORGE_API void uiSetWindowPos(vec2 pos);
FORGE_API void uiSetWindowSize(vec2 pos);
FORGE_API void uiSetWindowCollapsed(bool collapsed);
FORGE_API void uiSetWindowFocus(const char* title = nullptr);

FORGE_API void uiPushWindowBackgroundColor(float4 color);
FORGE_API void uiPopWindowBackgroundColor();
FORGE_API void uiPushWindowTransparency(float alpha);
FORGE_API void uiPopWindowTransparency();

FORGE_API void uiUpdateWindowVisibilityByTitle(const char* title, bool visible);
FORGE_API void uiUpdateWindowVisibilityByHandle(TFUIWindowHandle handle, bool visible);

typedef struct TFWidgetData
{
    TypeInfoStructMember* pMember;
    TFUIWidgetType        mType;
    bool                  mActive;
    bool                  mChanged;

    // additional data
    union
    {
        // collapsed header
        struct
        {
            bool     mIsCollapsed;
            uint32_t internalWidgetsCount;
        };
    };
} TFWidgetData;

typedef struct TFReflectedWidgetData
{
    TFWidgetData* pWidgets;
    uint32_t      mWidgetCount;
    uint32_t      mInternalDepth;
    uint32_t      mInternalCurrWidgetIndex;
} TFReflectedWidgetData;

/****************************************************************************/
// MARK: - Layout Manipulation API
/****************************************************************************/

enum TFUILayoutFormat
{
    // provided sizes and positions will be..
    TF_LAYOUT_DYNAMIC = 0, // in ratios [0.0f, 1.0f]
    TF_LAYOUT_STATIC       // in static pixels
};

// begins a new layout space that allows to specify each widget's position and size with uiLayoutSpacePush
FORGE_API void uiLayoutSpaceBegin(TFUILayoutFormat format, float rowHeight, int widgetCount);
// specifies position and size for the next widget in its own relative coordinate space
FORGE_API void uiLayoutSpacePush(vec2 pos, vec2 size);
// ends layout space
FORGE_API void uiLayoutSpaceEnd();

// Less flexible version than uiLayoutSpace, but is more concise. Allows to specify widget widths within a row.
// Supports auto repeat.
FORGE_API void   uiLayoutRow(TFUILayoutFormat format, float rowHeight, int widgetCount, const float* widgetWidths);
FORGE_API void   uiLayoutRowBegin(TFUILayoutFormat format, float rowHeight, int widgetCount);
FORGE_API void   uiLayoutRowPush(float ratioOrWidth);
FORGE_API void   uiLayoutRowEnd();
FORGE_API float  uiGetDefaultRowHeight();
FORGE_API float  uiGetDefaultRowPadding();
FORGE_API float4 uiLayoutPeek();

// Automatically layouts a provided number of widgets within a row. Supports auto repeat.
FORGE_API void uiLayoutAutoRows(int colsPerRow);
FORGE_API void uiLayoutAutoTextRows(int colsPerRow);
// Includes the editor's padding and border around one line of text.
FORGE_API void uiLayoutAutoTextboxRows(int colsPerRow);
FORGE_API void uiLayoutDynamicRows(float rowHeight, int colsPerRow);
FORGE_API void uiLayoutDynamicTextRows(float rowHeight, int colsPerRow);
FORGE_API void uiLayoutSetMinRowHeight(float minHeight);
FORGE_API void uiLayoutHorizontalSpace(int cols);

// helper functions
// float4 bounds - (x, y, w, h)
FORGE_API vec2   uiLayoutGetTextSize(const char* text, int textLength);
FORGE_API float  uiLayoutGetFontHeight();
FORGE_API vec2   uiLayoutGetPadding();
// call after uiLayoutSpaceBegin. Calculates total allocated space for the current layout space
FORGE_API float4 uiLayoutSpaceBounds();
// convert position from layout space coord space to screen space
FORGE_API vec2   uiLayoutSpaceSpaceToScreen(vec2 pos);
// convert position from screen space to layout space coord space
FORGE_API vec2   uiLayoutSpaceSpaceToLocal(vec2 pos);
// convert rectangle from layout space coord space to screen space
FORGE_API float4 uiLayoutSpaceSpaceRectToScreen(float4 bounds);
// convert rectangle from screen space to layout space coord space
FORGE_API float4 uiLayoutSpaceSpaceRectToLocal(float4 bounds);

/****************************************************************************/
// MARK: - Style
/****************************************************************************/

FORGE_API void uiPushStyleColor(TFUIStyleColor style, float4 color);
FORGE_API void uiPopStyleColor();

/****************************************************************************/
// MARK: - Draw Lists
/****************************************************************************/

// previous implementation did not expose underlying draw lists, but something to consider

/****************************************************************************/
// MARK: - Forge UI Data Structures
/****************************************************************************/

typedef struct TFUserInterfaceDesc
{
    TFRenderer*      pRenderer = NULL;
    TFPipelineCache* pCache = NULL;
    char const*      mSettingsFilename = nullptr;

    TFFont*         pFont = NULL;
    const uint32_t* pFrameIdx = NULL;

    uint32_t mFrameMaxCount = 2u;
    uint32_t mMaxUserTextures = 20u;
    float    mFontHeight = -1; // max height of the font

    bool mEnableDocking = false;
    bool mCompactLayout = false;
} TFUserInterfaceDesc;

typedef struct TFUserInterfaceLoadDesc
{
    TFPipelineCache* pCache;
    uint32_t         mColorFormat; // enum TinyImageFormat
    uint32_t         mWidth;
    uint32_t         mHeight;
    uint32_t         mDisplayWidth;
    uint32_t         mDisplayHeight;
    struct
    {
        // The world-space position of the UI layer when rendering in VR
        float3 mPosition;
        // The world-space scale of the UI layer when rendering in VR
        float  mScale;
    } mVR2DLayer;
} TFUserInterfaceLoadDesc;

/****************************************************************************/
// MARK: - Application Life Cycle
/****************************************************************************/

/// Initializes the Forge Rendering objects associated with the User Interface
/// To be called at application initialization time by the App Layer
FORGE_API void initUserInterface(TFUserInterfaceDesc* pDesc);

/// Frees Forge Rendering objects and memory associated with the User Interface
/// To be called at application shutdown time by the App Layer
FORGE_API void exitUserInterface();

/// Creates graphics pipelines associated with the User Interface
/// To be called at application load time by the App Layer
FORGE_API void loadUserInterface(const TFUserInterfaceLoadDesc* pDesc);

/// Destroys graphics pipelines associated with the User Interface
/// To be called at application unload time by the App Layer
FORGE_API void unloadUserInterface();

FORGE_API void uiCmdDrawUserInterface(TFCmd* pCmd, TFSwapChain* pSwapchain, TFRenderTarget* pRenderTarget,
                                      uint64_t profileToken = (uint64_t)-1);

/****************************************************************************/
// MARK: - Other User Interface Functionality
/****************************************************************************/

FORGE_API bool uiIsRenderingEnabled();
/// Toggle UI rendering, input processing still works (used to stop UI draws while taking screenshots)
FORGE_API void uiToggleRendering(bool enabled);

/// Returns true if any of the UI windows/widgets are currently hovered/active
FORGE_API bool uiIsFocused();
// True while the active window owns a text or numeric edit.
FORGE_API bool uiWantsTextInput();

FORGE_API bool uiIsInitialized();
FORGE_API void uiForceReferesh();

/****************************************************************************/
// MARK: - Utilities for Generating Widgets from Reflected Structures
/****************************************************************************/

/// Generates TF UI widgets from a reflected type
/// It DOES allocate some memory, so make sure you store the result and free it with 'uiRemoveReflectedWidget' when done.
FORGE_API TFReflectedWidgetData uiAddReflectedWidgetData(TypeInfoStruct* typeInfo, void* data);
/// Free all memory allocated with uiAddReflectedWidget
FORGE_API void                  uiRemoveReflectedWidgetData(TFReflectedWidgetData* data);

FORGE_API TFWidgetData* uiFindReflectedWidgetDataByOffset(TFReflectedWidgetData* data, size_t offset);

void uiUpdateBasicWidget(TFWidgetData* pWidgetData, void* structData, bool* isWidgetChanged, bool* isWidgetHovered);

// ppForbiddenAttributes - last data must be NULL
void uiUpdateReflectedWidgets(TFReflectedWidgetData* result, TypeInfoStruct* structType, void* structData,
                              const char** ppForbiddenAttributes);

float uiCalculateHeightReflectedWidgets(uint32_t widgetCount);

#endif // IUI_H
