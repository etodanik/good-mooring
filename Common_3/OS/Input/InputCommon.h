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

#if defined(__INTELLISENSE__)
#include "../Interfaces/IOperatingSystem.h"
#include "../../Utilities/Interfaces/ILog.h"
#include "../../Utilities/Interfaces/IThread.h"
#include "../../Utilities/Threading/Atomics.h"
#include "../../Utilities/Interfaces/IMath.h"
#include "../Interfaces/IInput.h"
#endif

#define CUSTOM_BINDING_FIRST (GPAD_R2 + 1)
// 512 custom key bindings
#define CUSTOM_BINDING_LAST  (CUSTOM_BINDING_FIRST + 511)
#define CUSTOM_BINDING_COUNT (CUSTOM_BINDING_LAST - CUSTOM_BINDING_FIRST + 1)

#define MOUSE_BTN_COUNT      (MOUSE_WHEEL_DOWN - MOUSE_1 + 1)
#define MOUSE_AXIS_COUNT     (MOUSE_DY - MOUSE_X + 1)

#define VGPAD_AXIS_COUNT     (VGPAD_RY - VGPAD_LX + 1)

#define K_FIRST              K_ESCAPE
#define K_LAST               K_F15
#define K_COUNT              (K_F15 - K_FIRST + 1)

#define GPAD_BTN_FIRST       GPAD_UP
#define GPAD_BTN_LAST        GPAD_R1
#define GPAD_BTN_COUNT       (GPAD_BTN_LAST - GPAD_BTN_FIRST + 1)

#define GPAD_AXIS_FIRST      GPAD_LX
#define GPAD_AXIS_LAST       GPAD_R2
#define GPAD_AXIS_COUNT      (GPAD_AXIS_LAST - GPAD_AXIS_FIRST + 1)

#define VRCTRL_AXIS_FIRST    VRCTRL_LTR
#define VRCTRL_AXIS_LAST     VRCTRL_RDZ
#define VRCTRL_AXIS_COUNT    (VRCTRL_AXIS_LAST - VRCTRL_AXIS_FIRST + 1)

#define ANATOMIC_INPUT_COUNT (EYE_GAZE_DZ - HAND_L_PINCHING + 1)

typedef struct
{
    uint16_t mDurationTime;
    uint16_t mInputMask;
} InputDoubleState;

typedef struct
{
    const char* pName;

    InputDoubleState mButtonDoubleStates[GPAD_BTN_COUNT];
    bool             mButtons[GPAD_BTN_COUNT];
    bool             mLastButtons[GPAD_BTN_COUNT];

    float mAxis[GPAD_AXIS_COUNT];
    float mLastAxis[GPAD_AXIS_COUNT];
    float mDeadzones[GPAD_AXIS_COUNT][2];

    float mRumbleLow;
    float mRumbleHigh;
    bool  mRumbleStopped;

    float3  mLight;
    uint8_t mLightUpdate : 1;
    uint8_t mLightReset : 1;
    bool    mActive;
} Gamepad;

typedef enum
{
    TOUCH_BEGAN,
    TOUCH_MOVED,
    TOUCH_ENDED,
    TOUCH_CANCELED,
} TouchPhase;

typedef struct
{
    int32_t    mId;
    int32_t    mPos[2];
    TouchPhase mPhase;
} TouchEvent;
static const int32_t TOUCH_ID_INVALID = -1;
typedef void (*InputTouchEventCallback)(const TouchEvent* event, void* pData);

typedef struct
{
    float2  mStartPos;
    float2  mPos;
    float2  mSticks;
    int32_t mId;
    float   mDeadZone;
    float   mRadius;
    bool    mActive;
} VirtualJoystick;

typedef enum
{
    BINDING_FLAG_NONE = 0,
    BINDING_FLAG_REMOVE_DT = 1,
    BINDING_FLAG_CONDITION_RELEASED = 2,

    // Whether value is for button reset or button press (Exit when K_ESCAPE is released, ...)
    BINDING_FLAG_RELEASED = 4,
    BINDING_FLAG_DOUBLE_CLICK = 8,
    BINDING_FLAG_SINGLE_CLICK = 16,
    BINDING_FLAG_REPEAT_CLICK = 32,
} BindingFlags;

typedef struct
{
    // Multiplier - Useful for axis control using keys/buttons (non analog inputs) (K_W = 1.0f, K_S = -1.0f, ...)
    float       mMultiplier;
    // Whether the condition is press or release (optional)
    uint32_t    mFlags;
    // Primary button/key/axis/...
    TFInputEnum mInput;
    // Condition (optional)
    TFInputEnum mCondInput;
} InputCustomBindingElement;

#define INPUT_ENUM_NAME_LENGTH_MAX 32
#define CUSTOM_BINDING_ELEMENT_MAX 5

typedef struct
{
    InputCustomBindingElement mBindings[CUSTOM_BINDING_ELEMENT_MAX];
    uint32_t                  mCount;
} InputCustomBindingDesc;

#define INPUT_TOTAL_COUNT              (MOUSE_BTN_COUNT + MOUSE_AXIS_COUNT + VGPAD_AXIS_COUNT + VRCTRL_AXIS_COUNT + ANATOMIC_INPUT_COUNT + K_COUNT + 1)
#define INPUT_TOTAL_DOUBLE_STATE_COUNT (GPAD_BTN_LAST + 1)

typedef struct
{
    bool        mMaskDoubleStateInput[INPUT_TOTAL_DOUBLE_STATE_COUNT];
    TFInputEnum mMainDoubleStaticInput[GPAD_BTN_FIRST];
    TFInputEnum mGpadDoubleStaticInput[GPAD_BTN_COUNT];

    uint32_t mMainInputCount;
    uint32_t mGpadInputCount;
} DoubleStateUsingInfo;

Gamepad              gGamepads[TF_MAX_GAMEPADS] = { { 0 } };
bool                 gInputRepeat[INPUT_TOTAL_COUNT] = { 0 };
float                gInputValues[INPUT_TOTAL_COUNT] = { 0 };
InputDoubleState     gInputDoubleStates[INPUT_TOTAL_COUNT] = { { 0 } };
float                gLastInputValues[INPUT_TOTAL_COUNT] = { 0 };
DoubleStateUsingInfo gDoubleStateUsingInfo = { { 0 } };

uint_least32_t  gCharacterBuffer[128] = { 0 };
uint32_t        gCharacterBufferCount = 0;
GamepadCallback gGamepadAddedCb = { 0 };
GamepadCallback gGamepadRemovedCb = { 0 };
#if __cplusplus
VirtualJoystick gVirtualJoystickLeft = {};
VirtualJoystick gVirtualJoystickRight = {};
#else
VirtualJoystick gVirtualJoystickLeft = { { 0 } };
VirtualJoystick gVirtualJoystickRight = { { 0 } };
#endif
InputTouchEventCallback pCustomTouchEventFn = { 0 };
void*                   pCustomTouchEventCallbackData = { 0 };
bool                    gVirtualJoystickEnable = true;
static float            gDeltaTime = { 0 };

typedef struct
{
    char                      mName[INPUT_ENUM_NAME_LENGTH_MAX];
    InputCustomBindingElement mBindings[CUSTOM_BINDING_ELEMENT_MAX];
    uint32_t                  mCount;
    bool                      mValid;
} InputCustomBinding;

// Wow, these brackets are crazy. Compiler warning with -Wall on clang due to
// -Wmissing-braces, only clang has missing braces as part of -Wall
static InputCustomBinding gCustomInputs[CUSTOM_BINDING_COUNT] = { { { 0 } } };
/************************************************************************/
// Gamepad Helpers
/************************************************************************/
static const char*        gGamepadDisconnectedName = "N/A";

static void GamepadResetState(TFInputPortIndex portIndex)
{
    Gamepad* gpad = &gGamepads[portIndex];
    memset(gpad->mButtons, 0, sizeof(gpad->mButtons));
    memset(gpad->mAxis, 0, sizeof(gpad->mAxis));
    memset(gpad->mLastButtons, 0, sizeof(gpad->mLastButtons));
    memset(gpad->mLastAxis, 0, sizeof(gpad->mLastAxis));
    gpad->mRumbleHigh = 0.0f;
    gpad->mRumbleLow = 0.0f;
    gpad->mRumbleStopped = false;
    gpad->mLightReset = true;
    gpad->mActive = false;
}

void GamepadUpdateLastState(TFInputPortIndex portIndex)
{
    Gamepad* gpad = &gGamepads[portIndex];
    memcpy(gpad->mLastButtons, gpad->mButtons, sizeof(gpad->mButtons));
    memcpy(gpad->mLastAxis, gpad->mAxis, sizeof(gpad->mAxis));
}

static void GamepadProcessStick(TFInputPortIndex portIndex, TFInputEnum axisStart)
{
    Gamepad*    gpad = &gGamepads[portIndex];
    float*      x = &gpad->mAxis[axisStart - GPAD_AXIS_FIRST];
    float*      y = &gpad->mAxis[axisStart + 1 - GPAD_AXIS_FIRST];
    vec2        stickVec = { *x, *y };
    const float minDeadzone = gpad->mDeadzones[axisStart - GPAD_AXIS_FIRST][0];
    const float maxDeadzone = gpad->mDeadzones[axisStart - GPAD_AXIS_FIRST][1];
    float       deadZoneTotal = (minDeadzone + maxDeadzone);

    float len = sqrtf(f2LengthSqr(stickVec));
    if (len == 0.0f)
    {
        len = 1.0f;
    }
    if (len < minDeadzone)
    {
        len = 0.0f;
    }
    else if (len > (1.0f - maxDeadzone))
    {
        len = 1.0f;
    }
    else
    {
        len = (len - minDeadzone) / (1.0f - deadZoneTotal);
    }

    *x *= len;
    *y *= len;
}

static void GamepadProcessTrigger(TFInputPortIndex portIndex, TFInputEnum axisStart)
{
    Gamepad*    gpad = &gGamepads[portIndex];
    float*      x = &gpad->mAxis[axisStart - GPAD_AXIS_FIRST];
    const float deadzone = gpad->mDeadzones[axisStart - GPAD_AXIS_FIRST][0];
    if (*x < deadzone)
    {
        *x = 0.0f;
    }
    else if (*x > 1.0f - deadzone)
    {
        *x = 1.0f;
    }
}

// Apply deadzones, ...
void GamepadPostProcess(TFInputPortIndex portIndex)
{
    if (!inputGamepadIsActive(portIndex))
    {
        return;
    }

    GamepadProcessStick(portIndex, GPAD_LX);
    GamepadProcessStick(portIndex, GPAD_RX);
    GamepadProcessTrigger(portIndex, GPAD_L2);
    GamepadProcessTrigger(portIndex, GPAD_R2);

    // TODO vr / hand tracking post processing should occur here
}

static void GamepadDefault()
{
    for (uint32_t portIndex = 0; portIndex < TF_MAX_GAMEPADS; ++portIndex)
    {
        Gamepad* gpad = &gGamepads[portIndex];
        GamepadResetState(portIndex);
        gpad->pName = gGamepadDisconnectedName;
        for (uint32_t a = 0; a < TF_ARRAY_COUNT(gpad->mDeadzones); ++a)
        {
            gpad->mDeadzones[a][0] = TF_GAMEPAD_DEADZONE_DEFAULT_MIN;
            gpad->mDeadzones[a][1] = TF_GAMEPAD_DEADZONE_DEFAULT_MAX;
        }
    }
}
/************************************************************************/
// Other helpers
/************************************************************************/
static void InputInitCommon()
{
    GamepadDefault();
    gVirtualJoystickLeft.mId = TOUCH_ID_INVALID;
    gVirtualJoystickRight.mId = TOUCH_ID_INVALID;

    memset(gCustomInputs, 0, sizeof(gCustomInputs));

    // Custom bindings builtin
    // Keyboard + Mouse
    inputAddCustomBindings("move_x; buttonanalog; K_D; 1.0f; K_A; -1.0f\r\n"
                           "move_y; buttonanalog; K_W; 1.0f; K_S; -1.0f\r\n"
                           "move_up; buttonanalog; K_E; 1.0f; K_Q; -1.0f\r\n"
                           "look_x; analog; MOUSE_DX; 0.0025f; cond; MOUSE_2; pressed; removedt\r\n"
                           "look_y; analog; MOUSE_DY; 0.0025f; cond; MOUSE_2; pressed; removedt\r\n"
                           "look_x; buttonanalog; K_H; 1.0f; K_F; -1.0f\r\n"
                           "look_y; buttonanalog; K_T; 1.0f; K_G; -1.0f\r\n"
                           "reset_view; button; K_SPACE; released\r\n"
                           "toggle_interface; button; K_F2; released\r\n"
                           "dump_profile; button; K_F3; released\r\n"
                           "toggle_fs; button; K_LALT; pressed; cond; K_ENTER; released\r\n"
                           "reload_shaders; button; K_LCTRL; pressed; cond; K_S; released\r\n"
                           "exit; button; K_ESCAPE; released\r\n"
                           "pt_x; analog; MOUSE_X; 1.0f\r\n"
                           "pt_y; analog; MOUSE_Y; 1.0f\r\n"
                           "pt_down; button; MOUSE_2; pressed\r\n");

    // Gamepad
    inputAddCustomBindings("move_x; analog; GPAD_LX; 1.0f\r\n"
                           "move_y; analog; GPAD_LY; 1.0f\r\n"
                           "look_x; analog; GPAD_RX; 1.0f\r\n"
                           "look_y; analog; GPAD_RY; 1.0f\r\n"
                           "reset_view; button; GPAD_Y; released\r\n"
                           "dump_profile; button; GPAD_START; pressed; cond; GPAD_B; released\r\n");

    // Touch
#if defined(ENABLE_FORGE_TOUCH_INPUT)
    inputAddCustomBindings("move_x; analog; VGPAD_LX; 1.0f\r\n"
                           "move_y; analog; VGPAD_LY; 1.0f\r\n"
                           "look_x; analog; VGPAD_RX; 0.005f; removedt\r\n"
                           "look_y; analog; VGPAD_RY; 0.005f; removedt\r\n");
#endif
}

/************************************************************************/
// IInput implementation
/************************************************************************/

static bool inputIsDoubleStateActive(InputDoubleState state) { return (state.mInputMask & 7) == 5; }

static bool inputHasDoubleClk(TFInputPortIndex index, TFInputEnum binding)
{
    InputDoubleState state;
    if (binding < GPAD_BTN_FIRST)
    {
        state = gInputDoubleStates[binding];
    }
    else if (binding <= GPAD_BTN_LAST)
    {
        state = gGamepads[index].mButtonDoubleStates[binding - GPAD_BTN_FIRST];
    }
    else
    {
        return false;
    }

    return inputIsDoubleStateActive(state); // & 111 == 101
}

static void inputResetDoubleClk(TFInputPortIndex index, TFInputEnum binding)
{
    if (binding < GPAD_BTN_FIRST)
    {
        memset(&gInputDoubleStates[binding], 0, sizeof(InputDoubleState));
    }
    else if (binding <= GPAD_BTN_LAST)
    {
        memset(&gGamepads[index].mButtonDoubleStates[binding - GPAD_BTN_FIRST], 0, sizeof(InputDoubleState));
    }
}

static InputDoubleState inputUpdateDoubleState(uint16_t deltaTime, uint16_t inputMask, InputDoubleState state)
{
    if (inputIsDoubleStateActive(state))
    {
        state.mInputMask = 0;
        state.mDurationTime = 0;
        return state;
    }

    if (inputMask == 0 && state.mInputMask == 0)
    {
        return state;
    }

    if ((state.mInputMask & 1) != inputMask)
    {
        state.mInputMask = (state.mInputMask << 1) | inputMask;
    }

    const uint16_t timeThresholdU = UINT16_MAX / 2;
    state.mDurationTime += deltaTime;
    if (state.mDurationTime >= timeThresholdU)
    {
        state.mInputMask = 0;
        state.mDurationTime = 0;
    }

    return state;
}

void inputUpdateDoubleStates()
{
    if (inputGetValue(0, MOUSE_DX) || inputGetValue(0, MOUSE_DY))
    {
        inputResetDoubleClk(0, MOUSE_1);
        inputResetDoubleClk(0, MOUSE_2);
        inputResetDoubleClk(0, MOUSE_3);
        inputResetDoubleClk(0, MOUSE_4);
    }

    const float    timeThresholdF = 0.3f;
    const uint16_t timeThresholdU = UINT16_MAX / 2;
    const uint16_t deltaTime = (uint16_t)(gDeltaTime / timeThresholdF * timeThresholdU);
    for (uint32_t i = 0; i < gDoubleStateUsingInfo.mMainInputCount; i++)
    {
        TFInputEnum input = gDoubleStateUsingInfo.mMainDoubleStaticInput[i];
        uint16_t    inputMask = gInputValues[input] != 0 ? 1 : 0;
        gInputDoubleStates[input] = inputUpdateDoubleState(deltaTime, inputMask, gInputDoubleStates[input]);
    }

    for (uint32_t i = 0; i < TF_MAX_GAMEPADS; i++)
    {
        if (!gGamepads[i].mActive)
        {
            continue;
        }

        for (uint32_t i2 = 0; i2 < gDoubleStateUsingInfo.mGpadInputCount; i2++)
        {
            TFInputEnum input = gDoubleStateUsingInfo.mMainDoubleStaticInput[i2];

            uint16_t inputMask = gGamepads[i].mButtons[input];
            gGamepads[i].mButtonDoubleStates[input] = inputUpdateDoubleState(deltaTime, inputMask, gGamepads[i].mButtonDoubleStates[input]);
        }
    }
}

static void inputUpdateDoubleStateUsing()
{
    memset(&gDoubleStateUsingInfo, 0, sizeof(gDoubleStateUsingInfo));

    for (uint32_t i = 0; i < CUSTOM_BINDING_COUNT; i++)
    {
        if (!gCustomInputs[i].mValid)
        {
            continue;
        }

        for (uint32_t i2 = 0; i2 < CUSTOM_BINDING_ELEMENT_MAX; i2++)
        {
            InputCustomBindingElement binding = gCustomInputs[i].mBindings[i2];
            if ((binding.mFlags & BINDING_FLAG_DOUBLE_CLICK) && (uint32_t)binding.mInput < INPUT_TOTAL_DOUBLE_STATE_COUNT)
            {
                gDoubleStateUsingInfo.mMaskDoubleStateInput[binding.mInput] = true;
            }
        }
    }

    for (uint32_t i = 0; i <= K_LAST; i++)
    {
        if (gDoubleStateUsingInfo.mMaskDoubleStateInput[i] == 0)
        {
            memset(&(gInputDoubleStates[i]), 0, sizeof(InputDoubleState));
            continue;
        }

        gDoubleStateUsingInfo.mMainDoubleStaticInput[gDoubleStateUsingInfo.mMainInputCount] = (TFInputEnum)i;
        gDoubleStateUsingInfo.mMainInputCount++;
    }

    for (uint32_t i = GPAD_BTN_FIRST; i <= GPAD_BTN_LAST; i++)
    {
        if (gDoubleStateUsingInfo.mMaskDoubleStateInput[i] == 0)
        {
            for (uint32_t i2 = 0; i2 < TF_MAX_GAMEPADS; i2++)
            {
                memset(&(gGamepads[i2].mButtonDoubleStates[i - GPAD_BTN_FIRST]), 0, sizeof(InputDoubleState));
            }
            continue;
        }

        gDoubleStateUsingInfo.mGpadDoubleStaticInput[gDoubleStateUsingInfo.mGpadInputCount] = (TFInputEnum)i;
        gDoubleStateUsingInfo.mGpadInputCount++;
    }
}

float inputGetValue(TFInputPortIndex index, TFInputEnum btn)
{
    if (btn < GPAD_BTN_FIRST)
    {
        return gInputValues[btn];
    }
    else if (btn <= GPAD_BTN_LAST)
    {
        ASSERT(index >= 0 && index < (TFInputPortIndex)TF_MAX_GAMEPADS);
        const Gamepad* gpad = &gGamepads[index];
        return gpad->mButtons[btn - GPAD_BTN_FIRST] ? 1.0f : 0.0f;
    }
    else if (btn <= GPAD_AXIS_LAST)
    {
        ASSERT(index >= 0 && index < (TFInputPortIndex)TF_MAX_GAMEPADS);
        const Gamepad* gpad = &gGamepads[index];
        return gpad->mAxis[btn - GPAD_AXIS_FIRST];
    }
    else // if (btn <= CUSTOM_BINDING_LAST)
    {
        ASSERT(gCustomInputs[btn - CUSTOM_BINDING_FIRST].mValid);
        const InputCustomBinding* desc = &gCustomInputs[btn - CUSTOM_BINDING_FIRST];
        float                     value = 0.0f;
        for (uint32_t e = 0; e < desc->mCount; ++e)
        {
            const InputCustomBindingElement* elem = &desc->mBindings[e];
            // Check if condition met
            if (elem->mCondInput != INPUT_NONE)
            {
                const bool condMet = (elem->mFlags & BINDING_FLAG_CONDITION_RELEASED) ? inputGetValueReset(index, elem->mCondInput)
                                                                                      : (bool)inputGetValue(index, elem->mCondInput);
                if (!condMet)
                {
                    continue;
                }
            }

            float bindingValue = 0;
            if (elem->mFlags & BINDING_FLAG_RELEASED)
            {
                bindingValue = inputGetValueReset(index, elem->mInput) ? 1.0f : 0.0f;
            }
            else if (elem->mFlags & BINDING_FLAG_DOUBLE_CLICK)
            {
                bindingValue = inputHasDoubleClk(index, elem->mInput) ? 1.0f : 0.0f;
            }
            else if (elem->mFlags & BINDING_FLAG_SINGLE_CLICK)
            {
                bool previos = (bool)inputGetLastValue(index, elem->mInput);
                bool current = (bool)inputGetValue(index, elem->mInput);
                bindingValue = !previos && current ? 1.0f : 0.0f;
            }
            else if (elem->mFlags & BINDING_FLAG_REPEAT_CLICK)
            {
                bindingValue = inputGetValueRepeat(index, elem->mInput) ? 1.0f : 0.0f;
            }
            else
            {
                bindingValue = inputGetValue(index, elem->mInput);
            }

            bindingValue *= elem->mMultiplier;
            if (elem->mFlags & BINDING_FLAG_REMOVE_DT)
            {
                bindingValue /= maxf(0.000001f, gDeltaTime);
            }
            value += bindingValue;
        }
        return value;
    }
}

float inputGetLastValue(TFInputPortIndex index, TFInputEnum btn)
{
    if (btn < GPAD_BTN_FIRST)
    {
        return gLastInputValues[btn];
    }
    else if (btn <= GPAD_BTN_LAST)
    {
        ASSERT(index >= 0 && index < (TFInputPortIndex)TF_MAX_GAMEPADS);
        const Gamepad* gpad = &gGamepads[index];
        return gpad->mLastButtons[btn - GPAD_BTN_FIRST] ? 1.0f : 0.0f;
    }
    else if (btn <= GPAD_AXIS_LAST)
    {
        ASSERT(index >= 0 && index < (TFInputPortIndex)TF_MAX_GAMEPADS);
        const Gamepad* gpad = &gGamepads[index];
        return gpad->mLastAxis[btn - GPAD_AXIS_FIRST];
    }

    return 0.0f;
}

float inputGetValueRepeat(TFInputPortIndex index, TFInputEnum btn)
{
    if (btn >= K_FIRST && btn <= K_LAST)
    {
        return gInputRepeat[btn] ? 1.0f : 0.0f;
    }
    else
    {
        return inputGetValue(index, btn);
    }
}

void inputGamepadSetAddedCallback(GamepadCallback cb)
{
    // Call after detecting new controller
    gGamepadAddedCb = cb;
}

void inputGamepadSetRemovedCallback(GamepadCallback cb)
{
    // Call after detecting controller removed
    gGamepadRemovedCb = cb;
}

bool inputGamepadIsActive(TFInputPortIndex index)
{
    ASSERT(index >= 0 && index < (TFInputPortIndex)TF_MAX_GAMEPADS);
    Gamepad* gpad = &gGamepads[index];
    return gpad->mActive;
}

const char* inputGamepadName(TFInputPortIndex index)
{
    ASSERT(index >= 0 && index < (TFInputPortIndex)TF_MAX_GAMEPADS);
    Gamepad* gpad = &gGamepads[index];
    return gpad->pName;
}

void inputGamepadSetDeadzone(TFInputPortIndex index, TFInputEnum btn, float minDeadzone, float maxDeadzone)
{
    ASSERT(index >= 0 && index < (TFInputPortIndex)TF_MAX_GAMEPADS);
    ASSERT(btn >= GPAD_AXIS_FIRST && btn <= GPAD_AXIS_LAST);
    Gamepad* gpad = &gGamepads[index];
    gpad->mDeadzones[btn - GPAD_AXIS_FIRST][0] = minDeadzone;
    gpad->mDeadzones[btn - GPAD_AXIS_FIRST][1] = maxDeadzone;
}

void inputSetEffect(TFInputPortIndex index, TFInputEffect effect, const TFInputEffectValue* value)
{
    switch (effect)
    {
    case TF_INPUT_EFFECT_GPAD_RUMBLE_LOW:
    {
        ASSERT(index >= 0 && index < (TFInputPortIndex)TF_MAX_GAMEPADS);
        Gamepad* gpad = &gGamepads[index];
        gpad->mRumbleLow = value->mRumble;
        if (value->mRumble != 0.0f)
        {
            gpad->mRumbleStopped = false;
        }
        break;
    }
    case TF_INPUT_EFFECT_GPAD_RUMBLE_HIGH:
    {
        ASSERT(index >= 0 && index < (TFInputPortIndex)TF_MAX_GAMEPADS);
        Gamepad* gpad = &gGamepads[index];
        gpad->mRumbleHigh = value->mRumble;
        if (value->mRumble != 0.0f)
        {
            gpad->mRumbleStopped = false;
        }
        break;
    }
    case TF_INPUT_EFFECT_GPAD_LIGHT:
    {
        ASSERT(index >= 0 && index < (TFInputPortIndex)TF_MAX_GAMEPADS);
        Gamepad* gpad = &gGamepads[index];
        gpad->mLight = value->mLight;
        gpad->mLightUpdate = true;
        gpad->mLightReset = false;
        break;
    }
    case TF_INPUT_EFFECT_GPAD_LIGHT_RESET:
    {
        ASSERT(index >= 0 && index < (TFInputPortIndex)TF_MAX_GAMEPADS);
        Gamepad* gpad = &gGamepads[index];
        gpad->mLightUpdate = false;
        gpad->mLightReset = true;
        break;
    }
    default:
        break;
    }
}

void inputGetCharInput(uint_least32_t** pOutChars, uint32_t* pCount)
{
    *pOutChars = gCharacterBuffer;
    *pCount = gCharacterBufferCount;
}
/************************************************************************/
// Custom input bindings
/************************************************************************/
// Internal function which also return hardware specific enum by name
// IInput function will only return custom user defined enums
static TFInputEnum InputGetEnum(const char* name)
{
#define NAME_COMP_ACTION(x)                             \
    if (!strncmp(name, #x, INPUT_ENUM_NAME_LENGTH_MAX)) \
    {                                                   \
        return x;                                       \
    }
    DECL_INPUTS(NAME_COMP_ACTION)

    return inputGetCustomBindingEnum(name);
}

static InputCustomBinding* InputGetCustomBinding(const char* name)
{
    for (uint32_t b = 0; b < TF_ARRAY_COUNT(gCustomInputs); ++b)
    {
        if (!gCustomInputs[b].mValid)
        {
            continue;
        }
        if (!strncmp(gCustomInputs[b].mName, name, INPUT_ENUM_NAME_LENGTH_MAX))
        {
            return &gCustomInputs[b];
        }
    }

    for (uint32_t i = 0; i < TF_ARRAY_COUNT(gCustomInputs); ++i)
    {
        if (gCustomInputs[i].mValid)
        {
            continue;
        }
        InputCustomBinding* customInput = &gCustomInputs[i];
        customInput->mValid = true;
        strncpy(customInput->mName, name, INPUT_ENUM_NAME_LENGTH_MAX);
        return customInput;
    }

    return NULL;
}

void inputRemoveCustomBinding(TFInputEnum binding)
{
    ASSERT(binding >= CUSTOM_BINDING_FIRST); // && binding <= CUSTOM_BINDING_LAST);
    gCustomInputs[binding - CUSTOM_BINDING_FIRST].mValid = false;

    inputUpdateDoubleStateUsing();
}

void inputAddCustomBindings(const char* bindings)
{
    char  line[TF_FS_MAX_PATH] = { 0 };
    char* fileCursor = (char*)bindings;
    char* gGpuDataFileEnd = (char*)bindings + strlen(bindings);

    char  tokensData[12][256] = { { 0 } };
    char* tokens[TF_ARRAY_COUNT(tokensData)] = { 0 };
    for (uint32_t t = 0; t < TF_ARRAY_COUNT(tokensData); ++t)
    {
        tokens[t] = tokensData[t];
    }

    while (bufferedGetLine(line, &fileCursor, gGpuDataFileEnd))
    {
        uint32_t tokenCount = tokenizeLine(line, line + strlen(line), ";", 256, 8, tokens);
        if (!tokenCount)
        {
            continue;
        }
        const char*         bindingName = tokens[0];
        InputCustomBinding* binding = InputGetCustomBinding(bindingName);
        ASSERT(binding);
        InputCustomBindingElement* elem = binding->mBindings + binding->mCount;
        uint32_t                   elemCount = 0;
        uint32_t                   tokensRead = 0;

        const char* bindingType = tokens[1];
        if (!stricmp(bindingType, "buttonanalog"))
        {
            elemCount = 2;
            ASSERT(binding->mCount + elemCount <= TF_ARRAY_COUNT(binding->mBindings));
            binding->mCount += elemCount;

            //   1    2     3    4     5     6
            // name; axis; K_D; 1.0f; K_A; -1.0f
            ASSERT(tokenCount >= 6);
            tokensRead = 6;
            const char* input0 = tokens[2];
            const char* mul0 = tokens[3];
            const char* input1 = tokens[4];
            const char* mul1 = tokens[5];
            elem[0].mInput = InputGetEnum(input0);
            elem[0].mMultiplier = (float)atof(mul0);
            elem[1].mInput = InputGetEnum(input1);
            elem[1].mMultiplier = (float)atof(mul1);
        }
        else if (!stricmp(bindingType, "button"))
        {
            elemCount = 1;
            ASSERT(binding->mCount + elemCount <= TF_ARRAY_COUNT(binding->mBindings));
            binding->mCount += elemCount;

            //   1      2     3      4
            // name; button; K_D; pressed
            ASSERT(tokenCount >= 4);
            tokensRead = 4;
            const char* input = tokens[2];
            const char* phase = tokens[3];
            elem[0].mInput = InputGetEnum(input);
            elem[0].mMultiplier = 1.0f;
            if (!stricmp(phase, "released"))
            {
                elem[0].mFlags |= BINDING_FLAG_RELEASED;
            }
            if (!stricmp(phase, "double"))
            {
                elem[0].mFlags |= BINDING_FLAG_DOUBLE_CLICK;
            }
            if (!stricmp(phase, "single"))
            {
                elem[0].mFlags |= BINDING_FLAG_SINGLE_CLICK;
            }
            if (!stricmp(phase, "repeat"))
            {
                elem[0].mFlags |= BINDING_FLAG_REPEAT_CLICK;
            }
        }
        else if (!stricmp(bindingType, "analog"))
        {
            elemCount = 1;
            ASSERT(binding->mCount + elemCount <= TF_ARRAY_COUNT(binding->mBindings));
            binding->mCount += elemCount;

            //   1      2     3       4
            // name; stick; GPAD_LX; 1.0f
            ASSERT(tokenCount >= 4);
            tokensRead = 4;
            const char* input = tokens[2];
            const char* mul = tokens[3];
            elem[0].mInput = InputGetEnum(input);
            elem[0].mMultiplier = (float)atof(mul);
            elem[0].mFlags &= ~BINDING_FLAG_RELEASED;
        }

        for (uint32_t e = 0; e < elemCount; ++e)
        {
            ASSERT(elem[e].mInput != INPUT_NONE);
        }

        for (uint32_t t = tokensRead; t < tokenCount; ++t)
        {
            if (!stricmp(tokens[t], "cond"))
            {
                const char* condInput = tokens[++t];
                const char* condPhase = tokens[++t];
                for (uint32_t e = 0; e < elemCount; ++e)
                {
                    elem[e].mCondInput = InputGetEnum(condInput);
                    ASSERT(elem[e].mCondInput != INPUT_NONE);
                    if (!stricmp(condPhase, "released"))
                    {
                        elem[e].mFlags |= BINDING_FLAG_CONDITION_RELEASED;
                    }
                }
            }
            else if (!stricmp(tokens[t], "removedt"))
            {
                for (uint32_t e = 0; e < elemCount; ++e)
                {
                    elem[e].mFlags |= BINDING_FLAG_REMOVE_DT;
                }
            }
        }
    }

    inputUpdateDoubleStateUsing();
}

TFInputEnum inputGetCustomBindingEnum(const char* name)
{
    for (uint32_t b = 0; b < TF_ARRAY_COUNT(gCustomInputs); ++b)
    {
        if (!gCustomInputs[b].mValid)
        {
            continue;
        }
        if (!strcmp(gCustomInputs[b].mName, name))
        {
            return (TFInputEnum)(CUSTOM_BINDING_FIRST + b);
        }
    }

    return INPUT_NONE;
}

bool inputHasActiveGamepad()
{
    for (uint32_t i = 0; i < TF_MAX_GAMEPADS; i++)
    {
        if (gGamepads[i].mActive)
        {
            return true;
        }
    }
    return false;
}