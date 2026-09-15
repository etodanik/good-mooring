#include "Interaction.h"
#include "Water/Ocean.h"
#include "Common/Application/Interfaces/IUI.h"
#include "Common/Application/Interfaces/IProfiler.h"
#include "Common/OS/Interfaces/IInput.h"
#include "Common/OS/Interfaces/IOperatingSystem.h"
#include "Common/Utilities/Interfaces/ILog.h"
#include <cmath>
#include <cstdio>
namespace mooring
{
static bool button(const char* text) { return UI_WIDGET_IS_PRESSED(uiButton(text)); }
static void label(const char* text) { uiLabel(text, TF_ALIGN_LEFT); }
static void inspect(Interaction& ui, Station station)
{
    ui.inspected = station;
    ui.panel = Panel::Station;
    ui.following = false;
    ui.camera.target = deckToWorld(snapshot(ui.world), stationPosition(station));
    ui.camera.targetDistance = 16;
}
static void overview(Interaction& ui)
{
    ui.panel = Panel::Overview;
    ui.following = true;
    ui.camera.targetDistance = 34;
}
static void command(Interaction& ui, CommandType type, float value = 0, unsigned engine = 0)
{
    enqueue(ui.world, { type, 0, ui.inspected, static_cast<uint8_t>(engine), value });
}
void updateInteraction(Interaction& ui, float dt, unsigned width, unsigned height)
{
    MTRACY_ZONE("updateInteraction");
    const bool  stationModal = ui.panel == Panel::Station || ui.panel == Panel::Helm;
    const float timeScale =
        ui.panel == Panel::Session || ui.panel == Panel::Difficulty ? 0 : interactionTimeScale(ui.difficulty, stationModal);
    advance(ui.world, dt, ui.seaLab.paused ? 0 : timeScale);
    Snapshot state = snapshot(ui.world, false);
    if (ui.following)
        ui.camera.target = state.position;
    updateCamera(ui.camera, dt);
    char  status[144];
    float dpi[2];
    getMonitorDpiScale(getActiveMonitorIdx(), dpi);
    float          availableHeight = std::fmax(1.0f, (float(height) - 48) / dpi[1]);
    float          availableWidth = std::fmax(1.0f, (float(width) - 48) / dpi[0]);
    const float    heights[] = { 210, 160, 230, 210, ui.catamaran ? 470.0f : 430.0f, 230, 230 };
    TFUIWindowDesc window = { "Mooring | handling prototype", vec2(24, 24),
                              vec2(std::fmin(265.0f, availableWidth), std::fmin(availableHeight, heights[static_cast<unsigned>(ui.panel)])),
                              TF_UI_WINDOW_BORDER | TF_UI_WINDOW_TITLE };
    if (UI_WINDOW_IS_VISIBLE(uiBeginWidgetWindow(&window)))
    {
        uiLayoutAutoTextRows(1);
        snprintf(status, sizeof status, "%s   %.1f kn", ui.catamaran ? "Lagoon 42" : "Oceanis 40.1", state.speedKnots);
        label(status);
        if (state.travelling)
        {
            snprintf(status, sizeof status, "Skipper walking to %s: %.1f s", stationName(static_cast<Station>(state.destination)),
                     state.travelRemaining);
        }
        else
            snprintf(status, sizeof status, "Skipper at %s", stationName(static_cast<Station>(state.destination)));
        label(status);
        if (timeScale == 0)
            label("Time paused while deciding");
        else if (timeScale < 1)
            label("Quarter speed while deciding");
        if (ui.panel == Panel::Overview)
        {
            label("Tap the skipper or a station marker.");
            label("Drag water to pan. Scroll to zoom.");
            if (button("Select skipper"))
            {
                ui.panel = Panel::Crew;
                ui.following = false;
                ui.camera.target = deckToWorld(state, state.skipperDeck);
                ui.camera.targetDistance = 18;
            }
            if (button("Session"))
                ui.panel = Panel::Session;
            if (button("Sea & weather lab (F2)"))
                ui.seaLab.open = !ui.seaLab.open;
        }
        else if (ui.panel == Panel::Crew)
        {
            label("Skipper selected");
            if (button("Attend a station"))
                ui.panel = Panel::Stations;
            if (state.atHelm && button("Operate helm"))
            {
                ui.inspected = Station::Helm;
                ui.panel = Panel::Helm;
            }
            if (button("Return"))
                overview(ui);
        }
        else if (ui.panel == Panel::Stations)
        {
            label("Choose a station to inspect");
            for (Station station : { Station::Helm, Station::Port, Station::Starboard, Station::Bow })
                if (button(stationName(station)))
                    inspect(ui, station);
            if (button("Back"))
                ui.panel = Panel::Crew;
            if (button("Return"))
                overview(ui);
        }
        else if (ui.panel == Panel::Station)
        {
            label(stationName(ui.inspected));
            label("Inspecting changes only the camera.");
            if (ui.inspected == Station::Helm && state.atHelm)
            {
                if (button("Operate helm"))
                    ui.panel = Panel::Helm;
            }
            else if (button("Attend station"))
            {
                command(ui, CommandType::Attend);
                ui.panel = Panel::Overview;
                LOGF(eINFO, "Station request: %s; travel must complete before occupancy", stationName(ui.inspected));
            }
            if (button("Back"))
                ui.panel = Panel::Stations;
            if (button("Return"))
                overview(ui);
        }
        else if (ui.panel == Panel::Helm)
        {
            label("Helm");
            if (canOperateHelm(ui.world, 0))
            {
                float wheel = state.rudder * 57.29578f;
                snprintf(status, sizeof status, "Wheel: %.0f degrees", wheel);
                label(status);
                if (UI_WIDGET_IS_CHANGED(uiSliderFloat(&wheel, -35, 35, 1)))
                    command(ui, CommandType::Wheel, wheel / 57.29578f);
                if (button("Centre wheel"))
                    command(ui, CommandType::Wheel, 0);
                for (unsigned engine = 0; engine < (ui.catamaran ? 2u : 1u); ++engine)
                {
                    float throttle = state.throttle[engine] * 100;
                    snprintf(status, sizeof status, "%s: %+.0f%%", ui.catamaran ? (engine ? "Starboard engine" : "Port engine") : "Engine",
                             throttle);
                    label(status);
                    if (UI_WIDGET_IS_CHANGED(uiSliderFloat(&throttle, -100, 100, 5)))
                        command(ui, CommandType::Throttle, throttle * .01f, engine);
                }
                if (button("Engines neutral"))
                {
                    command(ui, CommandType::Throttle, 0);
                    if (ui.catamaran)
                        command(ui, CommandType::Throttle, 0, 1);
                }
                bool brake = state.brake;
                if (UI_WIDGET_IS_CHANGED(uiCheckbox("Steering brake", &brake)))
                    command(ui, CommandType::Brake, brake ? 1 : 0);
                label("A / D wheel    W / S throttle");
                float rudderInput = inputGetValue(0, K_D) - inputGetValue(0, K_A);
                float throttleInput = inputGetValue(0, K_W) - inputGetValue(0, K_S);
                if (rudderInput)
                    command(ui, CommandType::Wheel, state.rudder + rudderInput * dt * .4f);
                if (throttleInput)
                    for (unsigned i = 0; i < (ui.catamaran ? 2u : 1u); ++i)
                        command(ui, CommandType::Throttle, state.throttle[i] + throttleInput * dt * .3f, i);
            }
            else
                label("The skipper must occupy the helm.");
            if (button("Attend another station"))
                ui.panel = Panel::Stations;
            if (button("Back"))
                ui.panel = Panel::Station;
            if (button("Return"))
                overview(ui);
        }
        else if (ui.panel == Panel::Session)
        {
            label("Water and handling prototype");
            label("Handling is provisional.");
            if (button("Restart exercise"))
            {
                resetWorld(ui.world);
                overview(ui);
            }
            if (button(ui.catamaran ? "Try monohull" : "Try catamaran"))
            {
                SeaState sea = seaState(worldOcean(ui.world));
                destroyWorld(ui.world);
                ui.catamaran = !ui.catamaran;
                ui.world = createWorld(ui.catamaran);
                setEnvironment(ui.world, sea);
                overview(ui);
            }
            if (button("Interaction difficulty"))
                ui.panel = Panel::Difficulty;
            if (button(ui.diagnostics ? "Hide diagnostics" : "Show diagnostics"))
                ui.diagnostics = !ui.diagnostics;
            if (button("Return"))
                overview(ui);
        }
        else if (ui.panel == Panel::Difficulty)
        {
            label("Time during station decisions");
            if (button("Beginner: pause"))
            {
                ui.difficulty = Difficulty::Beginner;
                overview(ui);
            }
            if (button("Intermediate: quarter speed"))
            {
                ui.difficulty = Difficulty::Intermediate;
                overview(ui);
            }
            if (button("Advanced: live"))
            {
                ui.difficulty = Difficulty::Advanced;
                overview(ui);
            }
            if (button("Back"))
                ui.panel = Panel::Session;
            if (button("Return"))
                overview(ui);
        }
    }
    uiEndWidgetWindow();
    drawSeaLab(ui.seaLab, ui.world, ui.camera, width, height);
    if (ui.diagnostics)
    {
        const auto&    stats = statistics(ui.world);
        float          panelWidth = std::fmin(270.0f, availableWidth);
        TFUIWindowDesc debug = { "Simulation diagnostics", vec2(std::fmax(24.0f, width - panelWidth * dpi[0] - 24), 24),
                                 vec2(panelWidth, std::fmin(220.0f, availableHeight)), TF_UI_WINDOW_BORDER | TF_UI_WINDOW_TITLE };
        if (UI_WINDOW_IS_VISIBLE(uiBeginWidgetWindow(&debug)))
        {
            uiLayoutAutoTextRows(1);
            snprintf(status, sizeof status, "60 Hz / steps: %llu", stats.steps);
            label(status);
            snprintf(status, sizeof status, "Forge CPU: %.1f MiB", stats.liveAllocationBytes / 1048576.0);
            label(status);
            snprintf(status, sizeof status, "Rejected controls: %llu", stats.rejectedCommands);
            label(status);
            const auto& sea = seaState(worldOcean(ui.world));
            snprintf(status, sizeof status, "Wind %.1f m/s / depth %.1f m", sea.windSpeed, sea.depth);
            label(status);
            snprintf(status, sizeof status, "Significant waves %.2f m", oceanMetrics(worldOcean(ui.world)).significantHeight);
            label(status);
            snprintf(status, sizeof status, "Wake packets %u / %u", activeWavePackets(worldOcean(ui.world)), MaxWavePackets);
            label(status);
            snprintf(status, sizeof status, "Frame %.2f ms", getCpuAvgFrameTime());
            label(status);
            label(lastCommandResult(ui.world));
        }
        uiEndWidgetWindow();
    }
    float x = inputGetValue(0, MOUSE_X), y = inputGetValue(0, MOUSE_Y);
    bool  down = inputGetValue(0, MOUSE_1) > 0;
    if (down && !ui.pointerDown && !uiIsFocused())
    {
        ui.pointerDown = true;
        ui.dragging = false;
        ui.pressX = x;
        ui.pressY = y;
        ui.pointerX = x;
        ui.pointerY = y;
    }
    if (ui.pointerDown && down)
    {
        if (std::fabs(x - ui.pressX) + std::fabs(y - ui.pressY) > 8)
            ui.dragging = true;
        if (ui.dragging)
        {
            panCamera(ui.camera, x - ui.pointerX, y - ui.pointerY);
            ui.following = false;
        }
        ui.pointerX = x;
        ui.pointerY = y;
    }
    if (!down && ui.pointerDown)
    {
        if (!ui.dragging)
        {
            Vec3    person = project(ui.camera, deckToWorld(state, state.skipperDeck));
            float   nearest = 42 * 42;
            bool    crew = (person.x - x) * (person.x - x) + (person.y - y) * (person.y - y) < nearest;
            Station chosen = Station::Helm;
            bool    found = false;
            for (Station station : { Station::Helm, Station::Port, Station::Starboard, Station::Bow })
            {
                auto  p = project(ui.camera, deckToWorld(state, stationPosition(station)));
                float d = (p.x - x) * (p.x - x) + (p.y - y) * (p.y - y);
                if (d < nearest)
                {
                    nearest = d;
                    chosen = station;
                    found = true;
                }
            }
            if (crew)
            {
                ui.panel = Panel::Crew;
                ui.following = false;
                ui.camera.target = deckToWorld(state, state.skipperDeck);
                ui.camera.targetDistance = 18;
            }
            else if (found)
                inspect(ui, chosen);
        }
        ui.pointerDown = false;
    }
    if (!uiIsFocused())
        zoomCamera(ui.camera, inputGetValue(0, MOUSE_WHEEL_UP) - inputGetValue(0, MOUSE_WHEEL_DOWN));
}
} // namespace mooring
