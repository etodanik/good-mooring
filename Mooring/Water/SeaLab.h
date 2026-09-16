#pragma once
#include "Ocean.h"
#include "WaterLook.h"
#include "../Camera.h"
#include "../ToolUI.h"
namespace mooring
{
enum class SeaPreset
{
    Glass,
    Calm,
    Breeze,
    Rough,
    Storm,
    Whitecaps
};
struct SeaLab
{
    SeaState            edit;
    WaterLook           look, resourceEdit;
    toolui::WindowState window;
    bool                open = false, initialized = false, paused = false;
    bool                rebuild = false, capture = false;
    int                 page = 0;
    char                effectSearch[128] = {};
    float               rightingArms[37] = {};
    const char*         curveVessel = nullptr;
    float               impulseHeel = 20, packetWavelength = 5, packetEnergy = 4000;
};
void drawSeaLab(SeaLab&, World*, Camera&, unsigned width, unsigned height);
void setSeaPreset(SeaLab&, World*, SeaPreset);
} // namespace mooring
