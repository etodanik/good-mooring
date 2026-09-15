#include "SeaLab.h"
#include "../Simulation/Hydrodynamics.h"
#include "Common/Application/Interfaces/IUI.h"
#include "Common/Application/Interfaces/IProfiler.h"
#include "Common/OS/Interfaces/IInput.h"
#include "Common/OS/Interfaces/IOperatingSystem.h"
#include <cmath>
#include <cstdio>
namespace mooring
{
static bool button(const char* text) { return UI_WIDGET_IS_PRESSED(uiButton(text)); }
static void label(const char* text) { uiLabel(text, TF_ALIGN_LEFT); }
static void slider(const char* name, float& value, float low, float high, float step)
{
    char text[96];
    snprintf(text, sizeof text, "%s: %.2f", name, value);
    label(text);
    float edited = value;
    if (UI_WIDGET_IS_CHANGED(uiSliderFloat(&edited, low, high, step)))
        value = edited;
}
void setSeaPreset(SeaLab& lab, World* world, SeaPreset p)
{
    MTRACY_ZONE("setSeaPreset");
    // Start from a complete state: Calm must not inherit Storm's swell period.
    SeaState sea;
    sea.depth = 30;
    lab.look.overcast = .12f;
    lab.look.rain = 0;
    switch (p)
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
void drawSeaLab(SeaLab& lab, World* world, Camera& camera, unsigned width, unsigned height)
{
    MTRACY_ZONE("drawSeaLab");
    bool key = inputGetValue(0, K_F2) > 0;
    if (key && !lab.keyDown)
        lab.open = !lab.open;
    lab.keyDown = key;
    if (!lab.open)
        return;
    if (!lab.initialized)
    {
        lab.edit = seaState(worldOcean(world));
        lab.initialized = true;
    }
    float dpi[2];
    getMonitorDpiScale(getActiveMonitorIdx(), dpi);
    float          panelWidth = std::fmin(330.0f, std::fmax(1.0f, (float(width) - 48) / dpi[0]));
    float          panelHeight = std::fmax(1.0f, (float(height) - 48) / dpi[1]);
    TFUIWindowDesc window = { "Sea & weather lab  |  F2", vec2(std::fmax(24.0f, width - panelWidth * dpi[0] - 24), 24),
                              vec2(panelWidth, panelHeight), TF_UI_WINDOW_BORDER | TF_UI_WINDOW_TITLE };
    if (UI_WINDOW_IS_VISIBLE(uiBeginWidgetWindow(&window)))
    {
        uiLayoutAutoTextRows(3);
        if (button("Sea"))
            lab.page = 0;
        if (button("Lighting"))
            lab.page = 1;
        if (button("Quality"))
            lab.page = 2;
        uiLayoutAutoTextRows(2);
        if (button("Inspect effects"))
            lab.page = 3;
        if (button("Physics"))
            lab.page = 4;
        uiLayoutAutoTextRows(1);
        uiCheckbox("Pause simulation", &lab.paused);
        if (lab.page == 0)
        {
            uiLayoutAutoTextRows(2);
            if (button("Glass"))
                setSeaPreset(lab, world, SeaPreset::Glass);
            if (button("Calm"))
                setSeaPreset(lab, world, SeaPreset::Calm);
            if (button("Breeze"))
                setSeaPreset(lab, world, SeaPreset::Breeze);
            if (button("Whitecaps"))
                setSeaPreset(lab, world, SeaPreset::Whitecaps);
            if (button("Rough"))
                setSeaPreset(lab, world, SeaPreset::Rough);
            if (button("Storm"))
                setSeaPreset(lab, world, SeaPreset::Storm);
            uiLayoutAutoTextRows(1);
            label("Sea edits apply together and reset the boat.");
            if (button("Apply sea / reset"))
                setEnvironment(world, lab.edit);
            slider("Wind (m/s)", lab.edit.windSpeed, 0, 35, .5f);
            slider("Wind direction (rad)", lab.edit.windDirection, -3.1416f, 3.1416f, .05f);
            slider("Gust fraction", lab.edit.gust, 0, .7f, .05f);
            slider("Wind sea Hs (m)", lab.edit.windWaveHeight, 0, 6, .05f);
            slider("Fetch (m)", lab.edit.fetch, 1000, 200000, 1000);
            slider("Swell Hs (m)", lab.edit.swellHeight, 0, 6, .05f);
            slider("Swell period (s)", lab.edit.swellPeriod, 1.5f, 18, .1f);
            slider("Swell direction (rad)", lab.edit.swellDirection, -3.1416f, 3.1416f, .05f);
            slider("Depth (m)", lab.edit.depth, 1, 100, .5f);
            slider("Choppiness", lab.edit.choppiness, 0, 1, .05f);
            slider("Current X (m/s)", lab.edit.current.x, -2, 2, .05f);
            slider("Current Z (m/s)", lab.edit.current.z, -2, 2, .05f);
        }
        else if (lab.page == 1)
        {
            label("Visual controls apply live; physics is unchanged.");
            slider("Cloud cover", lab.look.overcast, 0, 1, .05f);
            slider("Rain", lab.look.rain, 0, 1, .05f);
            slider("Foam amount", lab.look.foam, 0, 4, .1f);
            slider("Subsurface scattering", lab.look.scattering, 0, 2, .05f);
            slider("Wave volume contrast", lab.look.volumeContrast, 0, 16, .5f);
            slider("Under-foam scattering", lab.look.bubbleScattering, 0, 2, .1f);
            slider("Light shafts", lab.look.shafts, 0, 2, .05f);
            slider("Exposure", lab.look.exposure, .35f, 2, .05f);
            slider("Sun glints", lab.look.sunStrength, 0, 3, .1f);
            slider("Highlight bloom", lab.look.bloom, 0, .3f, .01f);
            slider("Bloom threshold (HDR)", lab.look.bloomThreshold, .25f, 3, .05f);
            slider("Sun elevation (degrees)", lab.look.sunElevation, 3, 85, 1);
            slider("Sun azimuth (degrees)", lab.look.sunAzimuth, -180, 180, 1);
            slider("Reflection", lab.look.reflection, 0, 1, .05f);
            slider("Refraction", lab.look.refraction, 0, 1, .05f);
            slider("Contact foam", lab.look.contactFoam, 0, 2, .05f);
            slider("Foam decay (per second)", lab.look.foamDecay, .05f, 1, .05f);
            slider("Foam diffusion (m2/s)", lab.look.foamSpread, 0, .08f, .005f);
            slider("Foam patch breakup", lab.look.foamBreakup, 0, 1, .05f);
            slider("Wave-driven foam flow", lab.look.foamWaveFlow, 0, 2, .1f);
            slider("Breaking bias (0.55 = baseline)", lab.look.breakingThreshold, .4f, .75f, .025f);
            slider("Spray emission", lab.look.sprayRate, 0, 3, .1f);
        }
        else if (lab.page == 2)
        {
            uiLayoutAutoTextRows(3);
            if (button("Low"))
                lab.look.preset(0);
            if (button("Balanced"))
                lab.look.preset(1);
            if (button("Ultra"))
                lab.look.preset(2);
            uiLayoutAutoTextRows(1);
            label("Apply reallocates graphics resources once.");
            if (button("Apply graphics resources"))
                lab.rebuild = true;
            label("FFT resolution");
            const char* fft[] = { "256 x 256", "512 x 512" };
            lab.look.fftSize = UI_WIDGET_GET_SELECTED(uiDropdown(fft, 2, lab.look.fftSize == 512)) ? 512 : 256;
            label("Water mesh");
            const char* grid[] = { "128 x 128", "256 x 256", "512 x 512" };
            lab.look.gridSize = 128 << UI_WIDGET_GET_SELECTED(uiDropdown(grid, 3,
                                                                         lab.look.gridSize == 128   ? 0
                                                                         : lab.look.gridSize == 256 ? 1
                                                                                                    : 2));
            label("Shadow map");
            const char* shadow[] = { "1024 x 1024", "2048 x 2048", "4096 x 4096" };
            lab.look.shadowSize = 1024 << UI_WIDGET_GET_SELECTED(uiDropdown(shadow, 3,
                                                                            lab.look.shadowSize == 1024   ? 0
                                                                            : lab.look.shadowSize == 2048 ? 1
                                                                                                          : 2));
            label("Reflection capture resolution");
            const char* reflection[] = { "Full", "Half", "Quarter" };
            lab.look.reflectionDivisor = 1 << UI_WIDGET_GET_SELECTED(uiDropdown(reflection, 3,
                                                                                lab.look.reflectionDivisor == 1   ? 0
                                                                                : lab.look.reflectionDivisor == 2 ? 1
                                                                                                                  : 2));
            label("Particle budget (live)");
            uiSliderInt(&lab.look.particles, 0, 4096, 256);
            label("Underwater ray samples (air uses 4x)");
            uiSliderInt(&lab.look.shaftSteps, 0, 24, 4);
            label("Cloud volume samples (live)");
            uiSliderInt(&lab.look.cloudSteps, 16, 64, 8);
            label("Refraction ray samples (live)");
            uiSliderInt(&lab.look.refractionSteps, 4, 16, 2);
            label("Reflection ray samples (live)");
            uiSliderInt(&lab.look.reflectionSteps, 16, 128, 16);
            label("Water light-depth resolution");
            const char* lightDepth[] = { "256 x 256", "512 x 512", "1024 x 1024" };
            lab.look.waterLightSize = 256 << UI_WIDGET_GET_SELECTED(uiDropdown(lightDepth, 3,
                                                                               lab.look.waterLightSize == 256   ? 0
                                                                               : lab.look.waterLightSize == 512 ? 1
                                                                                                                : 2));
            label("Volume scattering samples (live)");
            uiSliderInt(&lab.look.scatteringSamples, 2, 8, 2);
            slider("Normal filter width", lab.look.normalFilter, .65f, 2, .05f);
            slider("Fine ripple detail", lab.look.microDetail, 0, 1.5f, .05f);
            slider("Surface roughness", lab.look.roughness, .015f, .12f, .005f);
            slider("Foam texture scale", lab.look.foamScale, .5f, 2, .1f);
            slider("Foam shading relief (m)", lab.look.foamRelief, 0, .06f, .005f);
            slider("Foam clump height limit (m)", lab.look.foamClumpHeight, 0, .4f, .02f);
            uiCheckbox("Edge antialiasing", &lab.look.antialias);
        }
        else if (lab.page == 3)
        {
            label("Effect isolation (linear values, no tone map)");
            const char*    views[] = { "Beauty",
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
            const unsigned viewCount = sizeof(views) / sizeof(views[0]);
            lab.look.debugView = UI_WIDGET_GET_SELECTED(uiDropdown(views, viewCount, lab.look.debugView));
            uiLayoutAutoTextRows(2);
            if (button("Previous effect"))
                lab.look.debugView = (lab.look.debugView + viewCount - 1) % viewCount;
            if (button("Next effect"))
                lab.look.debugView = (lab.look.debugView + 1) % viewCount;
            uiLayoutAutoTextRows(1);
            label(views[lab.look.debugView]);
            slider("Foam / light / height view gain", lab.look.debugGain, 1, 16, 1);
            slider("Camera elevation (degrees)", camera.elevation, 4, 70, 1);
            slider("Camera azimuth (degrees)", camera.azimuth, -180, 180, 5);
            if (button("Water-level comparison"))
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
            if (button("Capture this view"))
                lab.capture = true;
            label("Crests need Rough/Storm; wakes need motion.");
            label("Rays need clouds or an occluding object.");
        }
        else
        {
            const auto& vessel = worldLayout(world);
            if (lab.curveVessel != vessel.name)
            {
                for (unsigned i = 0; i < 37; ++i)
                    lab.rightingArms[i] = equilibriumAtHeel(vessel, i * .08726646f).rightingArm;
                lab.curveVessel = vessel.name;
            }
            auto  s = snapshot(world, false);
            auto  q = s.rotation;
            float roll = std::atan2(2 * (q.w * q.z + q.x * q.y), 1 - 2 * (q.z * q.z + q.x * q.x)) * 57.29578f;
            float pitch = std::asin(std::fmax(-1.0f, std::fmin(1.0f, 2 * (q.w * q.x - q.y * q.z)))) * 57.29578f;
            char  text[100];
            snprintf(text, sizeof text, "Roll %.1f deg / pitch %.1f deg", roll, pitch);
            label(text);
            snprintf(text, sizeof text, "Heave %.2f m / speed %.2f kn", s.position.y, s.speedKnots);
            label(text);
            label("Estimated GZ (m): heel 0 to 180 deg");
            uiPlotLines(lab.rightingArms, 37, -1.0f, 3.0f, false);
            slider("Heel release (deg)", lab.impulseHeel, 0, 170, 5);
            if (button("Release at heel (open water)"))
            {
                InitialConditions initial;
                initial.position = { -25, equilibriumAtHeel(vessel, lab.impulseHeel * .01745329252f).height, 0 };
                float a = lab.impulseHeel * .008726646f;
                initial.rotation = { 0, 0, std::sin(a), std::cos(a) };
                resetWorld(world, &initial);
            }
            if (button("Reset at dock"))
                resetWorld(world);
            slider("Local wavelength (m)", lab.packetWavelength, 1.5f, 12, .5f);
            slider("Local wave energy (J)", lab.packetEnergy, 50, 30000, 250);
            if (button("Send a wave toward the boat"))
            {
                emitWavePacket(worldOcean(world), { s.position.x - 7, 0, s.position.z }, { 1, 0, 0 }, lab.packetWavelength,
                               lab.packetEnergy);
            }
            const auto& stats = statistics(world);
            snprintf(text, sizeof text, "CPU frame %.2f ms / heap %.1f MiB", getCpuAvgFrameTime(), stats.liveAllocationBytes / 1048576.0);
            label(text);
            snprintf(text, sizeof text, "Packets %u / overflow %u", activeWavePackets(worldOcean(world)),
                     droppedWavePackets(worldOcean(world)));
            label(text);
            snprintf(text, sizeof text, "Physics errors %llu / step allocations %llu", stats.physicsErrors, stats.steadyStepAllocations);
            label(text);
            snprintf(text, sizeof text, "Dock contacts %u / groundings %u", stats.dockContacts, stats.groundings);
            label(text);
            label("GZ uses estimated hulls and CG, not factory data.");
        }
        if (button("Close lab"))
            lab.open = false;
    }
    uiEndWidgetWindow();
}
} // namespace mooring
