#include "Interaction.h"
#include "Water/Ocean.h"
#include "Water/ShaderLab.h"
#include "Common/Application/Interfaces/IProfiler.h"
#include "Common/OS/Interfaces/IInput.h"
#include "Common/Utilities/Interfaces/ITime.h"
#include "Common/Utilities/Interfaces/ILog.h"
#include <cmath>
#include <cstdio>
namespace mooring
{
using namespace toolui;
static void inspect(Interaction& ui, Station station)
{
    ui.inspected = station;
    ui.skipperOpen = true;
    ui.inspecting = true;
    ui.operating = false;
    ui.following = false;
    ui.camera.target = deckToWorld(snapshot(ui.world), stationPosition(station));
    ui.camera.targetDistance = 16;
}
static void overview(Interaction& ui)
{
    ui.inspecting = ui.operating = false;
    ui.following = true;
    ui.camera.targetDistance = 34;
}
static void command(Interaction& ui, CommandType type, float value = 0, unsigned engine = 0)
{
    enqueue(ui.world, { type, 0, ui.inspected, static_cast<uint8_t>(engine), value });
}
static void toggleWindow(Interaction& ui, unsigned index)
{
    const bool restoring = !ui.showUI;
    ui.showUI = true;
    switch (index)
    {
    case 0:
        ui.skipperOpen = restoring || !ui.skipperOpen;
        if (!ui.skipperOpen)
            overview(ui);
        break;
    case 1:
        ui.seaLab.open = restoring || !ui.seaLab.open;
        break;
    case 2:
        if (shaderLabAvailable())
            setShaderLabOpen(restoring || !shaderLabIsOpen());
        break;
    case 3:
        ui.sessionOpen = restoring || !ui.sessionOpen;
        break;
    case 4:
        ui.diagnostics = restoring || !ui.diagnostics;
        break;
    }
}
static void performAction(Interaction& ui, unsigned index)
{
    switch (index)
    {
    case 0:
        ui.seaLab.paused = !ui.seaLab.paused;
        break;
    case 1:
        if (shaderLabAvailable())
            shaderLabRequestReload();
        break;
    case 2:
        ui.seaLab.capture = true;
        break;
    case 3:
        ui.seaLab.look.debugView = 0;
        break;
    case 4:
        ui.uncappedFPS = !ui.uncappedFPS;
        break;
    case 5:
        ui.showUI = !ui.showUI;
        break;
    }
}
static bool controlHeld() { return inputGetValue(0, K_LCTRL) > 0 || inputGetValue(0, K_RCTRL) > 0; }
static void drawHelm(Interaction& ui, const Snapshot& state)
{
    heading("Helm controls");
    float wheel = state.rudder * 57.29578f;
    if (number("Wheel (degrees)", wheel, -35, 35, 1))
        command(ui, CommandType::Wheel, wheel / 57.29578f);
    for (unsigned engine = 0; engine < (ui.catamaran ? 2u : 1u); ++engine)
    {
        float       throttle = state.throttle[engine] * 100;
        const char* name = ui.catamaran ? (engine ? "Starboard engine (%)" : "Port engine (%)") : "Engine (%)";
        if (number(name, throttle, -100, 100, 5))
            command(ui, CommandType::Throttle, throttle * .01f, engine);
    }
    uiLayoutAutoTextRows(2);
    if (button("Centre wheel"))
        command(ui, CommandType::Wheel, 0);
    if (button("Engines neutral"))
    {
        command(ui, CommandType::Throttle, 0);
        if (ui.catamaran)
            command(ui, CommandType::Throttle, 0, 1);
    }
    uiLayoutAutoTextRows(1);
    bool brake = state.brake;
    if (UI_WIDGET_IS_CHANGED(uiCheckbox("Steering brake", &brake)))
        command(ui, CommandType::Brake, brake ? 1 : 0);
}
static void drawSkipper(Interaction& ui, unsigned width, unsigned height)
{
    if (!beginWindow("Skipper & stations", ui.skipperOpen, ui.skipperWindow, width, height, 400, 320, 360, 280, false, true))
    {
        if (!ui.skipperOpen && (ui.inspecting || ui.operating))
            overview(ui);
        return;
    }
    const auto state = snapshot(ui.world, false);
    char       text[160];
    snprintf(text, sizeof text, "%s / %.1f kn", ui.catamaran ? "Lagoon 42" : "Oceanis 40.1", state.speedKnots);
    heading(text);
    if (state.travelling)
        snprintf(text, sizeof text, "Walking to %s / %.1f s remaining", stationName(Station(state.destination)), state.travelRemaining);
    else
        snprintf(text, sizeof text, "Skipper at %s", stationName(Station(state.destination)));
    wrapped(text, state.travelling ? Warning : Muted);
    heading("Inspect a station");
    uiLayoutAutoTextRows(2);
    for (Station station : { Station::Helm, Station::Port, Station::Starboard, Station::Bow })
        if (button(stationName(station), ui.inspected == station))
            inspect(ui, station);
    wrapped("Inspection does not move the skipper.");
    const bool occupied = !state.travelling && state.destination == uint8_t(ui.inspected);
    if (!ui.operating)
        uiLayoutAutoTextRows(1);
    if (ui.inspected == Station::Helm && canOperateHelm(ui.world, 0))
    {
        if (!ui.operating && button("Take helm controls", true))
        {
            ui.operating = true;
            float dpi[2];
            getMonitorDpiScale(getActiveMonitorIdx(), dpi);
            uiSetWindowSize(
                vec2(ui.skipperWindow.size.x, std::max(ui.skipperWindow.size.y, std::min(420 * dpi[1], float(height) - 20 * dpi[1]))));
        }
        if (ui.operating)
            drawHelm(ui, state);
    }
    else
    {
        ui.operating = false;
        snprintf(text, sizeof text, occupied ? "Skipper is at %s" : "Send skipper to %s", stationName(ui.inspected));
        if (button(text, true, !occupied && !(state.travelling && state.destination == uint8_t(ui.inspected))))
        {
            command(ui, CommandType::Attend);
            ui.inspecting = false;
            LOGF(eINFO, "Station request: %s; travel must complete before occupancy", stationName(ui.inspected));
        }
    }
    const float scale = interactionTimeScale(ui.difficulty, ui.inspecting || ui.operating);
    if (ui.seaLab.paused || ui.sessionOpen)
        wrapped(ui.seaLab.paused ? "Simulation paused." : "Simulation paused while Session is open.", Warning);
    else if (scale < 1)
        wrapped(scale == 0 ? "Beginner mode: simulation paused while deciding." : "Intermediate mode: quarter speed while deciding.",
                Warning);
    uiLayoutAutoTextRows(1);
    if (button(ui.operating ? "Leave controls / follow boat" : "Follow boat"))
        overview(ui);
    uiEndWidgetWindow();
}
static void drawSession(Interaction& ui, unsigned width, unsigned height)
{
    if (!beginWindow("Session", ui.sessionOpen, ui.sessionWindow, width, height, 420, 350, 340, 260, false, true))
        return;
    heading("Exercise");
    wrapped("Simulation pauses while this window is open.", Warning);
    uiLayoutAutoTextRows(1);
    if (button("Restart exercise"))
    {
        resetWorld(ui.world);
        ui.sessionOpen = false;
        overview(ui);
    }
    if (button(ui.catamaran ? "Switch to Oceanis 40.1" : "Switch to Lagoon 42"))
    {
        SeaState sea = seaState(worldOcean(ui.world));
        destroyWorld(ui.world);
        ui.catamaran = !ui.catamaran;
        ui.world = createWorld(ui.catamaran);
        setEnvironment(ui.world, sea);
        ui.sessionOpen = false;
        overview(ui);
    }
    heading("Time during station decisions");
    const char* names[] = { "Beginner / paused", "Intermediate / quarter speed", "Advanced / live" };
    ui.difficulty = Difficulty(UI_WIDGET_GET_SELECTED(uiDropdown(names, 3, int(ui.difficulty))));
    wrapped("Skipper travel runs at normal speed.");
    heading("Display");
    if (button(ui.fullscreen ? "Exit full screen / Ctrl+Cmd+F" : "Enter full screen / Ctrl+Cmd+F"))
        ui.fullscreenRequested = true;
    const bool hovered = uiIsNextWidgetHovered();
    uiCheckbox("Uncapped FPS", &ui.uncappedFPS);
    if (hovered)
        uiTooltipText("Disables the 30 fps cap and VSync.");
    if (button("Hide all UI / F10"))
        ui.showUI = false;
    uiLayoutAutoTextRows(1);
    if (button("Return to exercise", true))
        ui.sessionOpen = false;
    uiEndWidgetWindow();
}
static void drawDiagnostics(Interaction& ui, unsigned width, unsigned height)
{
    if (!beginWindow("Diagnostics", ui.diagnostics, ui.diagnosticsWindow, width, height, 460, 480, 350, 250, true, true))
        return;
    const auto& stats = statistics(ui.world);
    char        text[160];
    heading("Simulation");
    snprintf(text, sizeof text, "60 Hz / %llu steps / %llu rejected controls", stats.steps, stats.rejectedCommands);
    wrapped(text);
    snprintf(text, sizeof text, "Physics errors %llu / allocations during steps %llu", stats.physicsErrors, stats.steadyStepAllocations);
    wrapped(text);
    snprintf(text, sizeof text, "Dock contacts %u / groundings %u", stats.dockContacts, stats.groundings);
    wrapped(text);
    snprintf(text, sizeof text, "Forge CPU heap %.1f MiB", stats.liveAllocationBytes / 1048576.0);
    wrapped(text);
    heading("Last command");
    wrapped(lastCommandResult(ui.world));
    heading("Water");
    const auto& sea = seaState(worldOcean(ui.world));
    snprintf(text, sizeof text, "Wind %.1f m/s / depth %.1f m / waves Hs %.2f m", sea.windSpeed, sea.depth,
             oceanMetrics(worldOcean(ui.world)).significantHeight);
    wrapped(text);
    snprintf(text, sizeof text, "Wave packets %u / %u / overflow %u", activeWavePackets(worldOcean(ui.world)), MaxWavePackets,
             droppedWavePackets(worldOcean(ui.world)));
    wrapped(text);
    heading("Reading the bottom bar");
    wrapped("Frame is the frame interval, including pacing. Sim is CPU simulation work. Update includes Sim and the tools UI.");
    wrapped(
        "Render is CPU encoding and submission. Wait is drawable and GPU-fence waiting. GPU is elapsed GPU work; it overlaps CPU work.");
    wrapped("Values are smoothed milliseconds and are not additive. A dash means the measurement is unavailable.");
    uiEndWidgetWindow();
}
struct FooterLayout
{
    static constexpr unsigned CommandCount = 6, CounterCount = 7;
    float                     commandWidth, counterWidth, rowHeight, height;
    unsigned                  commandColumns, counterColumns;
};
static FooterLayout footerLayout(const Interaction& ui, unsigned width)
{
    constexpr float CommandWidth = 120, CounterWidth = 108;
    constexpr float WindowBorder = 1;
    float           dpi[2];
    getMonitorDpiScale(getActiveMonitorIdx(), dpi);
    const vec2   padding = uiLayoutGetPadding();
    const float  spacing = uiGetDefaultRowPadding();
    const float  availableWidth = std::max(1.0f, float(width) - 2 * (padding.x + WindowBorder));
    FooterLayout layout{};
    layout.commandWidth = std::min(CommandWidth * dpi[0], availableWidth);
    layout.counterWidth = std::min(CounterWidth * dpi[0], availableWidth);
    layout.rowHeight = std::ceil(uiCalculateHeightOfTextRows(uiGetDefaultRowHeight()));
    // Keep the same six command slots in both modes. Narrow windows wrap instead of stretching the slots.
    const auto columns = [&](float slotWidth, unsigned count)
    { return std::clamp(unsigned((availableWidth + spacing) / (slotWidth + spacing)), 1u, count); };
    layout.commandColumns = columns(layout.commandWidth, FooterLayout::CommandCount);
    layout.counterColumns = columns(layout.counterWidth, FooterLayout::CounterCount);
    const unsigned commandRows = (FooterLayout::CommandCount + layout.commandColumns - 1) / layout.commandColumns;
    const unsigned counterRows = (FooterLayout::CounterCount + layout.counterColumns - 1) / layout.counterColumns;
    const unsigned helmRows = ui.skipperOpen && ui.operating ? 1 : 0;
    layout.height = 2 * (padding.y + WindowBorder) + (commandRows + counterRows + helmRows) * (layout.rowHeight + spacing);
    return layout;
}
static float footerHeight(const Interaction& ui, unsigned width) { return footerLayout(ui, width).height; }
static void  drawFooter(Interaction& ui, unsigned width, unsigned height)
{
    const FooterLayout layout = footerLayout(ui, width);
    const float        barHeight = layout.height;
    TFUIWindowDesc     window{ "Mooring command bar", vec2(0, std::max(0.0f, float(height) - barHeight)), vec2(float(width), barHeight),
                           TF_UI_WINDOW_BORDER | TF_UI_WINDOW_NO_SCROLLBAR | TF_UI_WINDOW_DISABLE_DPI_SCALE_APPLY };
    if (UI_WINDOW_IS_VISIBLE(uiBeginWidgetWindow(&window)))
    {
        const bool  actions = controlHeld();
        const char* windows[] = { "F1 Skipper", "F2 Sea Lab", "F3 Shaders", "F4 Session", "F5 Stats" };
        const bool  open[] = { ui.skipperOpen, ui.seaLab.open, shaderLabIsOpen(), ui.sessionOpen, ui.diagnostics };
        const char* commands[] = { ui.seaLab.paused ? "F1 Resume" : "F1 Pause",      "F2 Reload",  "F3 Capture", "F4 Beauty",
                                   ui.uncappedFPS ? "F5 Limit FPS" : "F5 Uncap FPS", "F10 Hide UI" };
        float       commandWidths[FooterLayout::CommandCount];
        std::fill_n(commandWidths, FooterLayout::CommandCount, layout.commandWidth);
        uiLayoutRow(TF_LAYOUT_STATIC, layout.rowHeight, layout.commandColumns, commandWidths);
        for (unsigned index = 0; index < FooterLayout::CommandCount; ++index)
        {
            if (actions)
            {
                const bool enabled = (index != 1 || shaderLabAvailable()) && (index != 3 || ui.seaLab.look.debugView != 0);
                const bool active = (index == 0 && ui.seaLab.paused) || (index == 4 && ui.uncappedFPS);
                if (button(commands[index], active, enabled))
                    performAction(ui, index);
            }
            else if (index < 5)
            {
                if (button(windows[index], open[index], index != 2 || shaderLabAvailable()))
                    toggleWindow(ui, index);
            }
            else
                uiColorLabel("Ctrl: actions", TF_ALIGN_LEFT, Muted);
        }
        const float values[] = { ui.timings.frame,  ui.timings.simulation, ui.timings.update,
                                 ui.timings.render, ui.timings.wait,       ui.timings.gpu };
        const char* labels[] = { "Frame", "Sim", "Update", "Render", "Wait", "GPU" };
        const char* meanings[] = { "Frame interval, including pacing and waits.",          "CPU simulation time. Included in Update.",
                                   "CPU application update, including simulation and UI.", "CPU command encoding, submission and present.",
                                   "CPU time waiting for a drawable and GPU fence.",       "Elapsed GPU work. Overlaps CPU work." };
        float       counterWidths[FooterLayout::CounterCount];
        std::fill_n(counterWidths, FooterLayout::CounterCount, layout.counterWidth);
        uiLayoutRow(TF_LAYOUT_STATIC, layout.rowHeight, layout.counterColumns, counterWidths);
        char text[80];
        for (unsigned index = 0; index < 6; ++index)
        {
            const bool hovered = uiIsNextWidgetHovered();
            if (values[index] < 0)
                snprintf(text, sizeof text, "%s --", labels[index]);
            else
                snprintf(text, sizeof text, "%s %.2f ms", labels[index], values[index]);
            uiColorLabel(text, TF_ALIGN_LEFT, Muted);
            if (hovered)
                uiTooltipText(meanings[index]);
        }
        snprintf(text, sizeof text, "%.0f fps", ui.timings.frame > 0 ? 1000 / ui.timings.frame : 0);
        uiColorLabel(text, TF_ALIGN_LEFT, Heading);
        if (ui.skipperOpen && ui.operating)
        {
            uiLayoutDynamicRows(layout.rowHeight, 1);
            uiColorLabel("HELM   A / D wheel     W / S throttle", TF_ALIGN_LEFT, Heading);
        }
    }
    uiEndWidgetWindow();
}
void updateInteraction(Interaction& ui, float dt, unsigned width, unsigned height)
{
    MTRACY_ZONE("updateInteraction");
    const bool visibilityDown = inputGetValue(0, K_F10) > 0;
    if (visibilityDown && !ui.visibilityKeyDown)
        ui.showUI = !ui.showUI;
    ui.visibilityKeyDown = visibilityDown;
    const TFInputEnum keys[] = { K_F1, K_F2, K_F3, K_F4, K_F5 };
    for (unsigned index = 0; index < 5; ++index)
    {
        const bool down = inputGetValue(0, keys[index]) > 0;
        if (down && !ui.shortcutDown[index])
        {
            if (controlHeld())
                performAction(ui, index);
            else
                toggleWindow(ui, index);
        }
        ui.shortcutDown[index] = down;
    }
    const bool     stationModal = ui.skipperOpen && (ui.inspecting || ui.operating);
    const float    timeScale = ui.sessionOpen ? 0 : interactionTimeScale(ui.difficulty, stationModal);
    const uint64_t simulationStarted = getUSec(true);
    advance(ui.world, dt, ui.seaLab.paused ? 0 : timeScale);
    FrameTimings::sample(ui.timings.simulation, (getUSec(true) - simulationStarted) * .001f);
    FrameTimings::sample(ui.timings.frame, dt * 1000);
    Snapshot state = snapshot(ui.world, false);
    if (ui.following)
        ui.camera.target = state.position;
    updateCamera(ui.camera, dt);
    if (ui.showUI)
    {
        const unsigned usableHeight = unsigned(std::max(1.0f, height - footerHeight(ui, width)));
        drawSkipper(ui, width, usableHeight);
        drawSession(ui, width, usableHeight);
        drawSeaLab(ui.seaLab, ui.world, ui.camera, width, usableHeight);
        drawShaderLab(ui.seaLab.look, width, usableHeight);
        drawDiagnostics(ui, width, usableHeight);
        drawFooter(ui, width, height);
        focusOpenedWindow();
    }
    uiToggleRendering(ui.showUI);
    if (ui.skipperOpen && ui.operating && !ui.sessionOpen && canOperateHelm(ui.world, 0) && (!ui.showUI || !uiWantsTextInput()))
    {
        const float rudderInput = inputGetValue(0, K_D) - inputGetValue(0, K_A);
        const float throttleInput = inputGetValue(0, K_W) - inputGetValue(0, K_S);
        if (rudderInput)
            command(ui, CommandType::Wheel, state.rudder + rudderInput * dt * .4f);
        if (throttleInput)
            for (unsigned engine = 0; engine < (ui.catamaran ? 2u : 1u); ++engine)
                command(ui, CommandType::Throttle, state.throttle[engine] + throttleInput * dt * .3f, engine);
    }
    float x = inputGetValue(0, MOUSE_X), y = inputGetValue(0, MOUSE_Y);
    bool  down = inputGetValue(0, MOUSE_1) > 0;
    if (down && !ui.pointerDown && (!ui.showUI || !uiIsFocused()))
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
        if (!ui.dragging && ui.showUI)
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
                ui.skipperOpen = true;
                ui.inspecting = true;
                ui.operating = false;
                ui.following = false;
                ui.camera.target = deckToWorld(state, state.skipperDeck);
                ui.camera.targetDistance = 18;
            }
            else if (found)
                inspect(ui, chosen);
        }
        ui.pointerDown = false;
    }
    if (!ui.showUI || !uiIsFocused())
        zoomCamera(ui.camera, inputGetValue(0, MOUSE_WHEEL_UP) - inputGetValue(0, MOUSE_WHEEL_DOWN));
}
} // namespace mooring
