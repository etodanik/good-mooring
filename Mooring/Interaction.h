#pragma once
#include "Scene.h"
#include "Water/SeaLab.h"
namespace mooring
{
struct FrameTimings
{
    float       frame = -1, simulation = -1, update = -1, render = -1, wait = -1, gpu = -1;
    static void sample(float& average, float milliseconds)
    {
        average = milliseconds < 0 ? -1 : average < 0 ? milliseconds : average + .1f * (milliseconds - average);
    }
};
struct Interaction
{
    World*              world;
    SeaLab              seaLab;
    Camera              camera;
    FrameTimings        timings;
    toolui::WindowState skipperWindow, sessionWindow, diagnosticsWindow;
    Station             inspected = Station::Helm;
    Difficulty          difficulty = Difficulty::Beginner;
    bool                catamaran = false, diagnostics = false, following = true;
    bool                skipperOpen = true, sessionOpen = false, inspecting = false, operating = false;
    bool                showUI = true, uncappedFPS = false, visibilityKeyDown = false;
    bool                fullscreen = false, fullscreenRequested = false;
    bool                shortcutDown[5] = {};
    bool                pointerDown = false, dragging = false;
    float               pointerX, pointerY, pressX, pressY;
};
void updateInteraction(Interaction& interaction, float dt, unsigned width, unsigned height);
} // namespace mooring
