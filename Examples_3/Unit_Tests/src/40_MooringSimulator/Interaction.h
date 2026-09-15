#pragma once
#include "Scene.h"
#include "Water/SeaLab.h"
namespace mooring {
enum class Panel : uint8_t { Overview, Crew, Stations, Station, Helm, Session, Difficulty };
struct Interaction {
    World* world;
    SeaLab seaLab;
    Camera camera;
    Panel panel = Panel::Overview;
    Station inspected = Station::Helm;
    Difficulty difficulty = Difficulty::Beginner;
    bool catamaran = false, diagnostics = false, following = true;
    bool pointerDown = false, dragging = false;
    float pointerX, pointerY, pressX, pressY;
};
void updateInteraction(Interaction& interaction, float dt, unsigned width, unsigned height);
}
