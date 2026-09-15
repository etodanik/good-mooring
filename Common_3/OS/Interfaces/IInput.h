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

#include "../../Utilities/Interfaces/IMath.h"

// clang-format off
#define DECL_INPUTS_BODY(action) \
action(MOUSE_1)                  \
action(MOUSE_2)                  \
action(MOUSE_3)                  \
action(MOUSE_4)                  \
action(MOUSE_WHEEL_UP)           \
action(MOUSE_WHEEL_DOWN)         \
action(MOUSE_X)                  \
action(MOUSE_Y)                  \
action(MOUSE_DX)                 \
action(MOUSE_DY)                 \
action(VGPAD_LX)                 \
action(VGPAD_LY)                 \
action(VGPAD_RX)                 \
action(VGPAD_RY)                 \
action(VRCTRL_LTR)               \
action(VRCTRL_LPX)               \
action(VRCTRL_LPY)               \
action(VRCTRL_LPZ)               \
action(VRCTRL_LDX)               \
action(VRCTRL_LDY)               \
action(VRCTRL_LDZ)               \
action(VRCTRL_RTR)               \
action(VRCTRL_RPX)               \
action(VRCTRL_RPY)               \
action(VRCTRL_RPZ)               \
action(VRCTRL_RDX)               \
action(VRCTRL_RDY)               \
action(VRCTRL_RDZ)               \
action(HAND_L_PINCHING)          \
action(HAND_R_PINCHING)          \
action(HAND_L_POKING)            \
action(HAND_R_POKING)            \
action(HAND_L_POKE_PX)           \
action(HAND_R_POKE_PX)           \
action(HAND_L_POKE_PY)           \
action(HAND_R_POKE_PY)           \
action(HAND_L_POKE_PZ)           \
action(HAND_R_POKE_PZ)           \
action(EYE_GAZE_DX)              \
action(EYE_GAZE_DY)              \
action(EYE_GAZE_DZ)              \
action(K_ESCAPE)                 \
action(K_0)                      \
action(K_1)                      \
action(K_2)                      \
action(K_3)                      \
action(K_4)                      \
action(K_5)                      \
action(K_6)                      \
action(K_7)                      \
action(K_8)                      \
action(K_9)                      \
action(K_MINUS)                  \
action(K_EQUAL)                  \
action(K_KP_EQUALS)              \
action(K_BACKSPACE)              \
action(K_TAB)                    \
action(K_A)                      \
action(K_B)                      \
action(K_C)                      \
action(K_D)                      \
action(K_E)                      \
action(K_F)                      \
action(K_G)                      \
action(K_H)                      \
action(K_I)                      \
action(K_J)                      \
action(K_K)                      \
action(K_L)                      \
action(K_M)                      \
action(K_N)                      \
action(K_O)                      \
action(K_P)                      \
action(K_Q)                      \
action(K_R)                      \
action(K_S)                      \
action(K_T)                      \
action(K_U)                      \
action(K_V)                      \
action(K_W)                      \
action(K_X)                      \
action(K_Y)                      \
action(K_Z)                      \
action(K_LEFTBRACKET)            \
action(K_RIGHTBRACKET)           \
action(K_ENTER)                  \
action(K_KP_ENTER)               \
action(K_LCTRL)                  \
action(K_RCTRL)                  \
action(K_SEMICOLON)              \
action(K_APOSTROPHE)             \
action(K_GRAVE)                  \
action(K_LSHIFT)                 \
action(K_BACKSLASH)              \
action(K_COMMA)                  \
action(K_PERIOD)                 \
action(K_SLASH)                  \
action(K_KP_SLASH)               \
action(K_RSHIFT)                 \
action(K_KP_STAR)                \
action(K_PRINTSCREEN)            \
action(K_LALT)                   \
action(K_RALT)                   \
action(K_SPACE)                  \
action(K_CAPSLOCK)               \
action(K_F1)                     \
action(K_F2)                     \
action(K_F3)                     \
action(K_F4)                     \
action(K_F5)                     \
action(K_F6)                     \
action(K_F7)                     \
action(K_F8)                     \
action(K_F9)                     \
action(K_F10)                    \
action(K_F11)                    \
action(K_F12)                    \
action(K_PAUSE)                  \
action(K_KP_NUMLOCK)             \
action(K_SCROLLLOCK)             \
action(K_KP_HOME)                \
action(K_HOME)                   \
action(K_KP_UPARROW)             \
action(K_UPARROW)                \
action(K_KP_PGUP)                \
action(K_PGUP)                   \
action(K_KP_MINUS)               \
action(K_KP_LEFTARROW)           \
action(K_LEFTARROW)              \
action(K_KP_NUMPAD_5)            \
action(K_KP_RIGHTARROW)          \
action(K_RIGHTARROW)             \
action(K_KP_PLUS)                \
action(K_KP_END)                 \
action(K_END)                    \
action(K_KP_DOWNARROW)           \
action(K_DOWNARROW)              \
action(K_KP_PGDN)                \
action(K_PGDN)                   \
action(K_KP_INS)                 \
action(K_INS)                    \
action(K_KP_DEL)                 \
action(K_DEL)                    \
action(K_LWIN)                   \
action(K_RWIN)                   \
action(K_MENU)                   \
action(K_F13)                    \
action(K_F14)                    \
action(K_F15)                    \
action(GPAD_UP)                  \
action(GPAD_DOWN)                \
action(GPAD_LEFT)                \
action(GPAD_RIGHT)               \
action(GPAD_START)               \
action(GPAD_BACK)                \
action(GPAD_L3)                  \
action(GPAD_R3)                  \
action(GPAD_A)                   \
action(GPAD_B)                   \
action(GPAD_X)                   \
action(GPAD_Y)                   \
action(GPAD_L1)                  \
action(GPAD_R1)                  \
action(GPAD_LX)                  \
action(GPAD_LY)                  \
action(GPAD_RX)                  \
action(GPAD_RY)                  \
action(GPAD_L2)                  \
action(GPAD_R2)

#define DECL_INPUTS(action)  \
action(INPUT_NONE)           \
DECL_INPUTS_BODY(action)
// clang-format on

#define ENUM_ACTION(x) x,

typedef enum
{
    DECL_INPUTS(ENUM_ACTION)
} TFInputEnum;

// Low-level input interface to be implemented by each platform
// This interface can be used to implement higher-level platform independent input handling on the app level
// See Application/Input/Input.cpp - Reference implementation of platform agnostic input using input actions

typedef int32_t               TFInputPortIndex;
static const TFInputPortIndex PORT_INDEX_INVALID = -1;

#define TF_MAX_GAMEPADS 8
static const float TF_GAMEPAD_DEADZONE_DEFAULT_MIN = 0.1f;
static const float TF_GAMEPAD_DEADZONE_DEFAULT_MAX = 0.01f;
typedef void (*GamepadCallback)(TFInputPortIndex port);

typedef enum
{
    TF_INPUT_EFFECT_NONE = 0,
    TF_INPUT_EFFECT_GPAD_RUMBLE_LOW,
    TF_INPUT_EFFECT_GPAD_RUMBLE_HIGH,
    TF_INPUT_EFFECT_GPAD_LIGHT,
    TF_INPUT_EFFECT_GPAD_LIGHT_RESET,
} TFInputEffect;

typedef union
{
    float3 mLight;
    float  mRumble;
} TFInputEffectValue;

#if defined(__cplusplus)
extern "C"
{
#endif

    float              inputGetValue(TFInputPortIndex index, TFInputEnum btn);
    float              inputGetLastValue(TFInputPortIndex index, TFInputEnum btn);
    float              inputGetValueRepeat(TFInputPortIndex index, TFInputEnum btn);
    static inline bool inputGetValueReset(TFInputPortIndex index, TFInputEnum btn)
    {
        // Button released, ...
        return inputGetLastValue(index, btn) && !inputGetValue(index, btn);
    }
    void        inputAddCustomBindings(const char* bindings);
    void        inputRemoveCustomBinding(TFInputEnum binding);
    TFInputEnum inputGetCustomBindingEnum(const char* name);

    void        inputGamepadSetAddedCallback(GamepadCallback cb);
    void        inputGamepadSetRemovedCallback(GamepadCallback cb);
    bool        inputGamepadIsActive(TFInputPortIndex index);
    const char* inputGamepadName(TFInputPortIndex index);
    void        inputGamepadSetDeadzone(TFInputPortIndex index, TFInputEnum btn, float minDeadzone, float maxDeadzone);
    void        inputSetEffect(TFInputPortIndex index, TFInputEffect effect, const TFInputEffectValue* value);
    bool        inputHasActiveGamepad();

    void inputGetCharInput(uint_least32_t** pOutChars, uint32_t* pCount);

#if defined(__cplusplus)
}
#endif
