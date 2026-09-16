#include "SeaLab.h"
#include "../Simulation/Hydrodynamics.h"
#include "Common/Application/Interfaces/IProfiler.h"
#include <cstdio>
#include <cstring>
namespace mooring
{
using namespace toolui;
void setSeaPreset(SeaLab& lab, World* world, SeaPreset preset)
{
    MTRACY_ZONE("setSeaPreset");
    // Start from a complete state: Calm must not inherit Storm's swell period.
    SeaState sea;
    sea.depth = 30;
    lab.look.overcast = .12f;
    lab.look.rain = 0;
    switch (preset)
    {
    case SeaPreset::Glass:
        sea.windSpeed = .2f;
        sea.windWaveHeight = 0;
        sea.swellHeight = .06f;
        sea.swellPeriod = 8;
        sea.choppiness = .2f;
        break;
    case SeaPreset::Calm:
        sea.windSpeed = 1;
        sea.windWaveHeight = .015f;
        sea.swellHeight = .12f;
        sea.fetch = 5000;
        sea.choppiness = .5f;
        break;
    case SeaPreset::Breeze:
        sea.windSpeed = 7;
        sea.windWaveHeight = .55f;
        sea.swellHeight = .35f;
        sea.fetch = 18000;
        sea.depth = 8;
        break;
    case SeaPreset::Rough:
        sea.windSpeed = 15;
        sea.windWaveHeight = 1.5f;
        sea.swellHeight = 1.2f;
        sea.swellPeriod = 8;
        sea.fetch = 65000;
        sea.gust = .2f;
        lab.look.overcast = .65f;
        lab.look.rain = .25f;
        break;
    case SeaPreset::Storm:
        sea.windSpeed = 26;
        sea.windWaveHeight = 5;
        sea.swellHeight = 2.5f;
        sea.swellPeriod = 10;
        sea.fetch = 30000;
        sea.gust = .35f;
        sea.choppiness = 1;
        lab.look.overcast = .94f;
        lab.look.rain = .8f;
        break;
    case SeaPreset::Whitecaps:
        sea.windSpeed = 19;
        sea.windWaveHeight = 2.8f;
        sea.swellHeight = 1.2f;
        sea.swellPeriod = 7;
        sea.fetch = 16000;
        sea.choppiness = 1;
        lab.look.overcast = .3f;
        break;
    }
    lab.edit = sea;
    setEnvironment(world, sea);
}

static bool sameSea(const SeaState& left, const SeaState& right)
{
    return left.windSpeed == right.windSpeed && left.windDirection == right.windDirection && left.fetch == right.fetch &&
           left.gust == right.gust && left.windWaveHeight == right.windWaveHeight && left.swellHeight == right.swellHeight &&
           left.swellPeriod == right.swellPeriod && left.swellDirection == right.swellDirection && left.depth == right.depth &&
           left.level == right.level && left.choppiness == right.choppiness && left.current.x == right.current.x &&
           left.current.y == right.current.y && left.current.z == right.current.z && left.seed == right.seed;
}
static void copyResourceSizes(WaterLook& destination, const WaterLook& source)
{
    destination.fftSize = source.fftSize;
    destination.gridSize = source.gridSize;
    destination.shadowSize = source.shadowSize;
    destination.reflectionDivisor = source.reflectionDivisor;
    destination.waterLightSize = source.waterLightSize;
}
static bool sameResourceSizes(const WaterLook& left, const WaterLook& right)
{
    return left.fftSize == right.fftSize && left.gridSize == right.gridSize && left.shadowSize == right.shadowSize &&
           left.reflectionDivisor == right.reflectionDivisor && left.waterLightSize == right.waterLightSize;
}
static void angle(const char* name, float& radians)
{
    constexpr float DegreesPerRadian = 57.295779513f;
    float           degrees = radians * DegreesPerRadian;
    if (number(name, degrees, -180, 180, 1))
        radians = degrees / DegreesPerRadian;
}
static void drawSeaState(SeaLab& lab, World* world)
{
    heading("Presets");
    wrapped("Presets apply immediately and reset the boat.");
    uiLayoutAutoTextRows(3);
    const char* names[] = { "Glass", "Calm", "Breeze", "Rough", "Storm", "Whitecaps" };
    for (unsigned index = 0; index < 6; ++index)
        if (button(names[index]))
            setSeaPreset(lab, world, SeaPreset(index));
    heading("Wind");
    number("Speed (m/s)", lab.edit.windSpeed, 0, 35, .5f);
    angle("Direction (degrees)", lab.edit.windDirection);
    number("Gust fraction", lab.edit.gust, 0, .7f, .05f);
    number("Fetch (m)", lab.edit.fetch, 1000, 200000, 1000);
    heading("Waves & swell");
    number("Wind waves Hs (m)", lab.edit.windWaveHeight, 0, 6, .05f);
    number("Swell Hs (m)", lab.edit.swellHeight, 0, 6, .05f);
    number("Swell period (s)", lab.edit.swellPeriod, 1.5f, 18, .1f);
    angle("Swell direction (degrees)", lab.edit.swellDirection);
    number("Choppiness", lab.edit.choppiness, 0, 1, .05f);
    heading("Depth & current");
    number("Depth (m)", lab.edit.depth, 1, 100, .5f);
    number("Current X (m/s)", lab.edit.current.x, -2, 2, .05f);
    number("Current Z (m/s)", lab.edit.current.z, -2, 2, .05f);
    wrapped("Hs: significant wave height.");
}
static void drawWeather(WaterLook& look)
{
    heading("Weather");
    number("Cloud cover", look.overcast, 0, 1, .05f);
    number("Rain", look.rain, 0, 1, .05f);
    heading("Sun & atmosphere");
    number("Sun elevation (degrees)", look.sunElevation, 3, 85, 1);
    number("Sun azimuth (degrees)", look.sunAzimuth, -180, 180, 1);
    number("Sun glints", look.sunStrength, 0, 3, .1f);
    number("Light shafts", look.shafts, 0, 2, .05f);
    heading("Exposure & bloom");
    number("Exposure", look.exposure, .35f, 2, .05f);
    number("Highlight bloom", look.bloom, 0, .3f, .01f);
    number("Bloom threshold (HDR)", look.bloomThreshold, .25f, 3, .05f);
}
static void drawWater(WaterLook& look)
{
    heading("Surface & light");
    number("Reflection", look.reflection, 0, 1, .05f);
    number("Refraction", look.refraction, 0, 1, .05f);
    number("Scattering", look.scattering, 0, 2, .05f);
    number("Wave volume contrast", look.volumeContrast, 0, 16, .5f);
    number("Under-foam scattering", look.bubbleScattering, 0, 2, .1f);
    number("Fine ripple detail", look.microDetail, 0, 1.5f, .05f);
    number("Surface roughness", look.roughness, .015f, .12f, .005f);
    heading("Foam appearance");
    number("Amount", look.foam, 0, 4, .1f);
    number("Contact foam", look.contactFoam, 0, 2, .05f);
    number("Texture scale", look.foamScale, .5f, 2, .1f);
    number("Shading relief (m)", look.foamRelief, 0, .06f, .005f);
    number("Clump height limit (m)", look.foamClumpHeight, 0, .4f, .02f);
    heading("Foam motion & spray");
    number("Decay (per second)", look.foamDecay, .05f, 1, .05f);
    number("Diffusion (m2/s)", look.foamSpread, 0, .08f, .005f);
    number("Patch breakup", look.foamBreakup, 0, 1, .05f);
    number("Wave-driven flow", look.foamWaveFlow, 0, 2, .1f);
    number("Breaking bias", look.breakingThreshold, .4f, .75f, .025f);
    number("Spray emission", look.sprayRate, 0, 3, .1f);
    wrapped("Breaking bias 0.55 is the baseline.");
}
static void drawQuality(SeaLab& lab)
{
    heading("Quality presets");
    wrapped("Presets change live settings now. Resource sizes wait for Apply.");
    uiLayoutAutoTextRows(3);
    const char* presets[] = { "Low", "Balanced", "Ultra" };
    for (unsigned index = 0; index < 3; ++index)
        if (button(presets[index]))
        {
            const WaterLook active = lab.look;
            lab.look.preset(index);
            copyResourceSizes(lab.resourceEdit, lab.look);
            copyResourceSizes(lab.look, active);
        }
    heading("Resource sizes / apply together");
    const char* fft[] = { "256 x 256", "512 x 512" };
    const int   fftValues[] = { 256, 512 };
    const char* grid[] = { "128 x 128", "256 x 256", "512 x 512" };
    const int   gridValues[] = { 128, 256, 512 };
    const char* shadow[] = { "1024 x 1024", "2048 x 2048", "4096 x 4096" };
    const int   shadowValues[] = { 1024, 2048, 4096 };
    const char* reflection[] = { "Full", "Half", "Quarter" };
    const int   reflectionValues[] = { 1, 2, 4 };
    const char* light[] = { "256 x 256", "512 x 512", "1024 x 1024" };
    const int   lightValues[] = { 256, 512, 1024 };
    choice("FFT resolution", lab.resourceEdit.fftSize, fft, fftValues, 2);
    choice("Water mesh", lab.resourceEdit.gridSize, grid, gridValues, 3);
    choice("Shadow map", lab.resourceEdit.shadowSize, shadow, shadowValues, 3);
    choice("Reflection capture", lab.resourceEdit.reflectionDivisor, reflection, reflectionValues, 3);
    choice("Water light depth", lab.resourceEdit.waterLightSize, light, lightValues, 3);
    heading("Sample budgets / live");
    integer("Particles", lab.look.particles, 0, 4096, 256);
    integer("Underwater rays", lab.look.shaftSteps, 0, 24, 4);
    integer("Cloud volume", lab.look.cloudSteps, 16, 64, 8);
    integer("Refraction rays", lab.look.refractionSteps, 4, 16, 2);
    integer("Reflection rays", lab.look.reflectionSteps, 16, 128, 16);
    integer("Volume scattering", lab.look.scatteringSamples, 2, 8, 2);
    wrapped("Atmospheric rays use four times the underwater sample count.");
    heading("Filtering / live");
    number("Normal filter width", lab.look.normalFilter, .65f, 2, .05f);
    uiLayoutAutoTextRows(1);
    uiCheckbox("Edge antialiasing", &lab.look.antialias);
}
static void drawEffects(SeaLab& lab, Camera& camera)
{
    const char* views[] = { "Beauty",
                            "Mesh / normal LOD",
                            "Combined foam",
                            "Surface normals",
                            "FFT foam density",
                            "Local foam history",
                            "Contact foam",
                            "Hull / dock shadow",
                            "Traced reflection",
                            "Optical depth",
                            "Wave volume scattering",
                            "Refraction / absorption",
                            "Sun glints",
                            "Underwater light rays",
                            "Cloud shadows",
                            "Wake / local displacement",
                            "Wave compression",
                            "Fine ripples",
                            "Spray / foam particles (actual size)",
                            "Rain particles",
                            "Atmospheric light rays",
                            "Fog",
                            "FFT displacement",
                            "Local normals (8x slope)",
                            "Sky radiance",
                            "Cloud opacity",
                            "Displaced mesh lighting",
                            "Reflection coverage",
                            "Foam relief normals",
                            "Foam flow (RG velocity, B density)",
                            "Cloud noise slice (R shape, G erosion)",
                            "Cloud density section (10 x 4 km)",
                            "Water light path (white = 12 m)",
                            "Under-foam scattering",
                            "Active breaking (spray source)",
                            "Transported foam age (red fresh, blue 8 s)",
                            "Entrained air (R amount, G depth in m)",
                            "Highlight bloom (tone mapped)",
                            "Airborne spray (actual material)",
                            "Raised foam / crest froth (actual material)",
                            "Mist (actual material)",
                            "Crest sources (red emission, green age)",
                            "Environment reflection contribution",
                            "Water body contribution",
                            "Mean environment Fresnel" };

    heading("Effect isolation");
    wrapped("Linear values without tone mapping, except views marked tone mapped.");
    if (lab.look.debugView >= 1000)
        wrapped("A Shader Lab scene probe is active. Select an effect to replace it.", Warning);
    else if (lab.look.debugView >= 0 && unsigned(lab.look.debugView) < TF_ARRAY_COUNT(views))
        wrapped(views[lab.look.debugView], Heading);
    uiLayoutAutoTextboxRows(1);
    bstring query = bfromarr(lab.effectSearch);
    uiTextbox("Search effects", &query, TF_WIDGET_EDIT_FILTER_ASCII, false);
    float dpi[2];
    getMonitorDpiScale(getActiveMonitorIdx(), dpi);
    uiLayoutDynamicRows(170 * dpi[1], 1);
    if (UI_GROUP_IS_VISIBLE(uiBeginWidgetGroup("Sea effect list", TF_UI_WINDOW_BORDER)))
    {
        uiLayoutAutoTextRows(1);
        bool found = false;
        for (unsigned index = 0; index < TF_ARRAY_COUNT(views); ++index)
            if (containsIgnoringCase(views[index], lab.effectSearch))
            {
                found = true;
                if (button(views[index], lab.look.debugView == int(index), true, views[index]))
                    lab.look.debugView = int(index);
            }
        if (!found)
            wrapped("No matching effects.");
        uiEndWidgetGroup();
    }
    number("View gain", lab.look.debugGain, 1, 16, 1);
    heading("Camera");
    number("Elevation (degrees)", camera.elevation, 4, 70, 1);
    number("Azimuth (degrees)", camera.azimuth, -180, 180, 5);
    uiLayoutAutoTextRows(2);
    if (button("Water level"))
    {
        camera.elevation = 8;
        camera.targetDistance = 28;
    }
    if (button("Docking overview"))
    {
        camera.elevation = 32.6f;
        camera.azimuth = 147.4f;
        camera.targetDistance = 34;
    }
    wrapped("Crests need Rough or Storm; wakes need motion. Rays need clouds or an occluding object.");
}
static void drawPhysics(SeaLab& lab, World* world)
{
    const auto& vessel = worldLayout(world);
    if (lab.curveVessel != vessel.name)
    {
        for (unsigned index = 0; index < 37; ++index)
            lab.rightingArms[index] = equilibriumAtHeel(vessel, index * .08726646f).rightingArm;
        lab.curveVessel = vessel.name;
    }
    const auto  state = snapshot(world, false);
    const auto  rotation = state.rotation;
    const float roll =
        std::atan2(2 * (rotation.w * rotation.z + rotation.x * rotation.y), 1 - 2 * (rotation.z * rotation.z + rotation.x * rotation.x)) *
        57.29578f;
    const float pitch = std::asin(std::clamp(2 * (rotation.w * rotation.x - rotation.y * rotation.z), -1.0f, 1.0f)) * 57.29578f;
    char        text[128];
    heading("Live motion");
    snprintf(text, sizeof text, "Roll %.1f deg / pitch %.1f deg", roll, pitch);
    label(text);
    snprintf(text, sizeof text, "Heave %.2f m / speed %.2f kn", state.position.y, state.speedKnots);
    label(text);
    heading("Righting arm / GZ");
    float dpi[2];
    getMonitorDpiScale(getActiveMonitorIdx(), dpi);
    uiLayoutDynamicRows(105 * dpi[1], 1);
    uiPlotLines(lab.rightingArms, 37, -1.0f, 3.0f, false);
    wrapped("Horizontal: heel 0 to 180 degrees. Vertical: righting arm -1 to 3 m. Estimated hull and centre of gravity.");
    number("Heel release (degrees)", lab.impulseHeel, 0, 170, 5);
    uiLayoutAutoTextRows(2);
    if (button("Release offshore"))
    {
        InitialConditions initial;
        initial.position = { -25, equilibriumAtHeel(vessel, lab.impulseHeel * .01745329252f).height, 0 };
        const float halfAngle = lab.impulseHeel * .008726646f;
        initial.rotation = { 0, 0, std::sin(halfAngle), std::cos(halfAngle) };
        resetWorld(world, &initial);
    }
    if (button("Reset at dock"))
        resetWorld(world);
    heading("Local wave packet");
    number("Wavelength (m)", lab.packetWavelength, 1.5f, 12, .5f);
    number("Energy (J)", lab.packetEnergy, 50, 30000, 250);
    uiLayoutAutoTextRows(1);
    if (button("Send wave toward boat"))
        emitWavePacket(worldOcean(world), { state.position.x - 7, 0, state.position.z }, { 1, 0, 0 }, lab.packetWavelength,
                       lab.packetEnergy);
}
void drawSeaLab(SeaLab& lab, World* world, Camera& camera, unsigned width, unsigned height)
{
    MTRACY_ZONE("drawSeaLab");
    if (!beginWindow("Sea & Weather", lab.open, lab.window, width, height, 660, 660, 560, 390, true))
        return;
    if (!lab.initialized)
    {
        lab.edit = seaState(worldOcean(world));
        lab.resourceEdit = lab.look;
        lab.initialized = true;
    }
    float dpi[2];
    getMonitorDpiScale(getActiveMonitorIdx(), dpi);
    const char*  pages[] = { "Sea state", "Weather & light", "Water & foam", "Quality", "Inspect effects", "Physics" };
    const char*  descriptions[] = { "Sea edits apply together and reset the boat.",
                                    "Live visual controls. Physics is unchanged.",
                                    "Changes apply live.",
                                    "Resource sizes apply together; sample budgets are live.",
                                    nullptr,
                                    nullptr };
    const float4 content = uiLayoutSpaceBounds();
    const float  bodyWidth = content[2];
    const float  navigationWidth = std::min(145 * dpi[0], bodyWidth * .26f);
    const float  gap = 8 * dpi[0];
    const float  bodyHeight = std::max(80.0f, lab.window.size.y - content[1] - 58 * dpi[1]);
    uiLayoutSpaceBegin(TF_LAYOUT_STATIC, bodyHeight, 2);
    uiLayoutSpacePush(vec2(0, 0), vec2(navigationWidth, bodyHeight));
    if (UI_GROUP_IS_VISIBLE(uiBeginWidgetGroup("Sea navigation", TF_UI_WINDOW_NO_SCROLLBAR)))
    {
        heading("Sections");
        for (int page = 0; page < 6; ++page)
            if (button(pages[page], lab.page == page))
                lab.page = page;
        uiEndWidgetGroup();
    }
    uiLayoutSpacePush(vec2(navigationWidth + gap, 0), vec2(bodyWidth - navigationWidth - gap, bodyHeight));
    char groupName[64];
    snprintf(groupName, sizeof groupName, "Sea controls %d", lab.page);
    if (UI_GROUP_IS_VISIBLE(uiBeginWidgetGroup(groupName, TF_UI_WINDOW_BORDER)))
    {
        heading(pages[lab.page]);
        if (descriptions[lab.page])
            wrapped(descriptions[lab.page]);
        switch (lab.page)
        {
        case 0:
            drawSeaState(lab, world);
            break;
        case 1:
            drawWeather(lab.look);
            break;
        case 2:
            drawWater(lab.look);
            break;
        case 3:
            drawQuality(lab);
            break;
        case 4:
            drawEffects(lab, camera);
            break;
        case 5:
            drawPhysics(lab, world);
            break;
        }
        uiEndWidgetGroup();
    }
    uiLayoutSpaceEnd();
    const bool pendingSea = !sameSea(lab.edit, seaState(worldOcean(world)));
    const bool pendingResources = !sameResourceSizes(lab.resourceEdit, lab.look);
    uiLayoutAutoTextRows(1);
    const char* status = pendingSea && pendingResources ? "Pending: sea edits and graphics resource sizes"
                         : pendingSea                   ? "Pending: sea edits"
                         : pendingResources             ? "Pending: graphics resource sizes"
                                                        : "All changes applied";
    uiColorLabel(status, TF_ALIGN_LEFT, pendingSea || pendingResources ? Warning : Muted);
    uiLayoutAutoTextRows(3);
    if (lab.page == 0)
    {
        if (button("Apply sea + reset boat", true, pendingSea))
            setEnvironment(world, lab.edit);
        if (button("Discard sea edits", false, pendingSea))
            lab.edit = seaState(worldOcean(world));
        if (button("Reset boat"))
            resetWorld(world);
    }
    else if (lab.page == 3)
    {
        if (button("Apply resource sizes", true, pendingResources))
        {
            copyResourceSizes(lab.look, lab.resourceEdit);
            lab.rebuild = true;
        }
        if (button("Discard size edits", false, pendingResources))
            copyResourceSizes(lab.resourceEdit, lab.look);
        if (button("Capture view"))
            lab.capture = true;
    }
    else
    {
        if (button("Restore beauty", false, lab.look.debugView != 0))
            lab.look.debugView = 0;
        if (button("Capture view"))
            lab.capture = true;
        if (button("Hide lab"))
            lab.open = false;
    }
    uiEndWidgetWindow();
}
} // namespace mooring
