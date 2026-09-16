#pragma once
#include "Common/Application/Interfaces/IUI.h"
#include "Common/OS/Interfaces/IOperatingSystem.h"
#include <algorithm>
#include <cmath>

namespace mooring::toolui
{
inline const float4 Heading = { .55f, .78f, 1, 1 };
inline const float4 Muted = { .68f, .73f, .79f, 1 };
inline const float4 Accent = { .16f, .36f, .55f, 1 };
inline const float4 Warning = { 1, .77f, .38f, 1 };
inline const char*  pendingFocus = nullptr;

inline void focusOpenedWindow()
{
    if (pendingFocus)
        uiSetWindowFocus(pendingFocus);
    pendingFocus = nullptr;
}

struct WindowState
{
    vec2 position{}, size{};
    bool placed = false, wasOpen = false;
};

// A successful begin needs uiEndWidgetWindow(). Hidden and closed windows are ended here.
inline bool beginWindow(const char* title, bool& open, WindowState& state, unsigned width, unsigned height, float preferredWidth,
                        float preferredHeight, float minimumWidth, float minimumHeight, bool right = false, bool scroll = false)
{
    if (!open)
    {
        state.wasOpen = false;
        return false;
    }
    float dpi[2];
    getMonitorDpiScale(getActiveMonitorIdx(), dpi);
    const vec2 margin(10 * dpi[0], 10 * dpi[1]);
    const vec2 available(std::max(1.0f, float(width) - 2 * margin.x), std::max(1.0f, float(height) - 2 * margin.y));
    if (!state.placed)
    {
        state.size = vec2(std::min(preferredWidth * dpi[0], available.x), std::min(preferredHeight * dpi[1], available.y));
        state.position = vec2(right ? width - state.size.x - margin.x : margin.x, margin.y);
        state.placed = true;
    }
    TFUIWindowDesc window{ title, state.position, state.size,
                           TF_UI_WINDOW_BORDER | TF_UI_WINDOW_TITLE | TF_UI_WINDOW_MOVABLE | TF_UI_WINDOW_SCALABLE |
                               TF_UI_WINDOW_DISABLE_DPI_SCALE_APPLY | (scroll ? 0u : TF_UI_WINDOW_NO_SCROLLBAR) };
    const bool     visible = UI_WINDOW_IS_VISIBLE(uiBeginWidgetWindow(&window, &open));
    if (!visible)
    {
        state.wasOpen = open;
        uiEndWidgetWindow();
        return false;
    }
    if (!state.wasOpen)
        pendingFocus = title;
    state.wasOpen = true;
    const vec2 size = uiGetWindowSize(), position = uiGetWindowPos();
    state.size = vec2(std::clamp(size.x, std::min(minimumWidth * dpi[0], available.x), available.x),
                      std::clamp(size.y, std::min(minimumHeight * dpi[1], available.y), available.y));
    state.position = vec2(std::clamp(position.x, margin.x, std::max(margin.x, float(width) - margin.x - state.size.x)),
                          std::clamp(position.y, margin.y, std::max(margin.y, float(height) - margin.y - state.size.y)));
    if (state.size.x != size.x || state.size.y != size.y)
        uiSetWindowSize(state.size);
    if (state.position.x != position.x || state.position.y != position.y)
        uiSetWindowPos(state.position);
    return true;
}

inline void label(const char* text) { uiLabel(text, TF_ALIGN_LEFT); }
inline void heading(const char* text)
{
    uiLayoutAutoTextRows(1);
    uiColorLabel(text, TF_ALIGN_LEFT, Heading);
}
inline void wrapped(const char* text, float4 color = Muted)
{
    const float width = std::max(40.0f, uiLayoutSpaceBounds()[2] - 24);
    const float rows = std::max(1.0f, std::ceil(float(uiGetTextWidth(text)) / width));
    uiLayoutDynamicRows(rows * uiLayoutGetTextSize("M", 1).y + 6, 1);
    bstring message = bconstfromcstr(text);
    uiDynamicText(&message, color, TF_TEXT_MODE_WRAPPED, TF_ALIGN_LEFT);
}
inline bool button(const char* text, bool accent = false, bool enabled = true, const char* hint = nullptr)
{
    const bool hovered = hint && uiIsNextWidgetHovered();
    if (!enabled)
        uiBeginWidgetDisable();
    const bool pressed = UI_WIDGET_IS_PRESSED(accent ? uiButtonColored(text, Accent) : uiButton(text));
    if (!enabled)
        uiEndWidgetDisable();
    if (hovered)
        uiTooltipText(hint);
    return enabled && pressed;
}
inline bool number(const char* name, float& value, float low, float high, float step)
{
    uiLayoutAutoTextRows(2);
    label(name);
    const float before = value;
    uiPropertyFloat(&value, std::min(low, value), std::max(high, value), step);
    return before != value;
}
inline void integer(const char* name, int& value, int low, int high, int step)
{
    uiLayoutAutoTextRows(2);
    label(name);
    uiPropertyInt(&value, std::min(low, value), std::max(high, value), step);
}
inline void choice(const char* name, int& value, const char* const* names, const int* values, unsigned count)
{
    uiLayoutAutoTextRows(2);
    label(name);
    unsigned selected = 0;
    for (unsigned index = 0; index < count; ++index)
        if (value == values[index])
            selected = index;
    const unsigned edited = UI_WIDGET_GET_SELECTED(uiDropdown(names, count, selected));
    if (edited != selected)
        value = values[edited];
}
} // namespace mooring::toolui
